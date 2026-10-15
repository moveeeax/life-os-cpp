/**
 * @file Outbox.hpp
 * @brief Transactional outbox: atomic "domain write + event" dispatch to the
 *        Jobs queue. Opt-in — nothing drains until `outbox.drain_interval_sec`
 *        (OUTBOX_DRAIN_INTERVAL_SEC) is set > 0.
 *
 * ## When to use the outbox vs the after-response hook
 *
 * The default dispatch discipline is the after-response hook — the 4-arg
 * `Api::with_repo_errors` overload (api/HandlerSupport.hpp): commit the DB
 * write, send the response, then fire the side effect (Jobs::submit, email,
 * webhook). That is best-effort BY DESIGN: a process that dies between the DB
 * commit and the submit loses the event, and a failed submit is only logged.
 * For a confirm-email link the user can re-request, that trade-off is right —
 * don't add an outbox row, a table and a drain hop for events whose loss is
 * an inconvenience.
 *
 * Use the outbox when losing the event corrupts truth someone relies on:
 *   - money paths: a receipt/refund notice for a ledger write that DID happen
 *     (src/email/BillingEmails.hpp discipline — the wallet write is durable,
 *     so the notice about it must eventually go out too);
 *   - webhooks that downstream systems reconcile against ("payment captured");
 *   - any event where "DB committed but nobody was told" needs a human to
 *     notice and repair by hand.
 *
 * Mechanics: `enqueue(txn, kind, payload)` INSERTs into the `outbox` table
 * INSIDE the caller's open transaction — commit makes the event durable
 * atomically with the domain write, rollback erases both. A periodic task
 * (`Outbox::drain`, scheduled by Core when the interval is > 0) claims
 * unclaimed rows (`FOR UPDATE SKIP LOCKED` — concurrent drainers never double-
 * claim) and relays each to `Jobs::submit(kind, payload)`: the row is DELETEd
 * on success, or released (attempts+1, last_error, claimed_at back to NULL)
 * on failure so the next drain retries it. Delivery is therefore
 * AT-LEAST-ONCE: a drainer dying between submit and delete redelivers after
 * kStaleClaimSec — job handlers for outbox kinds must tolerate a duplicate
 * (the same contract Jobs' retry/visibility-timeout paths already impose).
 *
 * Most flows are NOT routed through here — the after-response paths keep
 * their exact behaviour; the outbox is the opt-in upgrade for the call sites
 * that need it. Worked example: docs/CONVENTIONS.md gotcha 20.
 *
 * The LLM job rows (food, money and tasks parse jobs, the advisor review) are
 * such a call site: a row left `queued` with no job behind it never runs and
 * only reads as failed after JobRows::kStaleRunning. They go through
 * `enqueue_if_drained` (below): the outbox row rides the row's own
 * transaction when a drain is configured, and the pre-outbox direct submit
 * after the commit is kept for a stack that runs without the drain.
 */

#pragma once

#include <string>

#include "jobs/Job.hpp"

namespace Jobs {
namespace Outbox {

/// A claim older than this is considered abandoned (the drainer died between
/// claiming and finalizing) and becomes drainable again — the self-healing
/// half of the at-least-once contract.
inline constexpr int kStaleClaimSec = 300;

/**
 * @brief Record an event in the SAME transaction as the caller's domain
 *        write. @p txn is the `auto& txn` every repository lambda receives
 *        from Database::execute_write / execute_transaction
 *        (Database::detail::TracingTxn — only exec/exec_params are used).
 *        @p kind must be a job type a worker handles (core/JobHandlers.cpp
 *        or any other registered handler); @p payload is passed to Jobs::submit
 *        verbatim. Throws on SQL failure — which rolls the caller's
 *        transaction back, exactly the atomicity the pattern promises.
 */
template <typename Txn>
void enqueue(Txn& txn, const std::string& kind, const json& payload) {
    txn.exec_params("INSERT INTO outbox (kind, payload) VALUES ($1, $2::jsonb)", kind, payload.dump());
}

/**
 * @brief Whether a drain is configured for this deployment
 *        (`outbox.drain_interval_sec` / OUTBOX_DRAIN_INTERVAL_SEC > 0, read
 *        from the loaded config) and the job queue is up. Core schedules the
 *        drain in server mode under the same condition; a worker process with
 *        the knob set writes rows the API pods drain, the table being shared.
 *        False without a loaded config or an initialized queue.
 */
bool drain_configured();

/**
 * @brief Record the job of a row written in @p txn, when a drain will relay
 *        it. With the drain configured, `enqueue` inside the same transaction
 *        and return true: the commit makes the row and its job durable
 *        together, a rollback erases both, and the caller is done. With the
 *        drain off (OUTBOX_DRAIN_INTERVAL_SEC=0: the compose stack, the test
 *        suites) nothing is written and false comes back: the caller then
 *        submits the job directly once the transaction has committed, the
 *        pre-outbox behaviour, so a stack without the drain never strands a
 *        row in the table. Throws what `enqueue` throws.
 */
template <typename Txn>
bool enqueue_if_drained(Txn& txn, const std::string& kind, const json& payload) {
    if (!drain_configured()) {
        return false;
    }
    enqueue(txn, kind, payload);
    return true;
}

/// What one drain pass did — returned for tests/observability; the periodic
/// task just logs it.
struct DrainStats {
    long submitted = 0;  ///< rows relayed to Jobs::submit and deleted
    long failed = 0;     ///< rows whose submit threw (attempts bumped, retried next pass)
};

/**
 * @brief Relay up to @p batch pending outbox rows to the Jobs queue.
 *        Claim (UPDATE ... WHERE claimed_at IS NULL ... FOR UPDATE SKIP
 *        LOCKED RETURNING) → Jobs::submit per row → one finalize transaction
 *        (DELETE the submitted, release the failed). Safe to call from
 *        several processes at once. Throws only when the claim/finalize
 *        transactions themselves fail (DB down) — per-row submit failures are
 *        recorded on the row, never thrown.
 * @param reclaim_sec claims older than this are treated as abandoned and
 *        re-claimed (defaults to kStaleClaimSec).
 */
DrainStats drain(int batch = 100, int reclaim_sec = kStaleClaimSec);

}  // namespace Outbox
}  // namespace Jobs
