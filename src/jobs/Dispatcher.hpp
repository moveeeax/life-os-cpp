/**
 * @file Dispatcher.hpp
 * @brief Job-type → handler registry. Replaces the hardcoded if-ladder that
 *        used to live in worker_main.cpp so that:
 *          1. adding a job type is a register_handler() call, not an edit to the
 *             one un-includable .cpp (mirrors how controllers self-register);
 *          2. the dispatch/unknown-type path is unit-testable without booting
 *             the worker process.
 *        dispatch() throws Jobs::PermanentJobError on an unknown type, which the
 *        worker routes straight to the DLQ (no retry storm).
 *
 * Non-trivial bodies live in Dispatcher.cpp (compiled once into app_core;
 * docs/ARCHITECTURE.md §4). The nlohmann json alias arrives via
 * jobs/Job.hpp.
 */

#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "jobs/Job.hpp"  // Job + PermanentJobError + json alias

namespace Jobs {

class Dispatcher {
public:
    /// A handler takes the job payload and returns the result JSON. It may throw:
    /// a std::exception → retry then DLQ; a PermanentJobError → straight to DLQ.
    using Handler = std::function<json(const json& payload)>;

    static Dispatcher& get();

    void register_handler(const std::string& type, Handler fn) { handlers_[type] = std::move(fn); }

    bool has_handler(const std::string& type) const { return handlers_.find(type) != handlers_.end(); }

    /// Registered type strings — the source of truth for "valid job types"
    /// (e.g. to validate WORKER_TYPES at startup).
    std::vector<std::string> known_types() const;

    /// Of @p subscribed job types, those with NO registered handler. Used at
    /// worker startup to surface a WORKER_TYPES entry that would silently
    /// dead-letter every job of that type (a producer/consumer mismatch),
    /// instead of pretending to subscribe to it.
    std::vector<std::string> unregistered(const std::vector<std::string>& subscribed) const;

    /// Run the handler for @p job.type. Throws PermanentJobError if no handler
    /// is registered — surfacing a producer/consumer mismatch loudly instead of
    /// faking success, and without burning the retry budget.
    json dispatch(const Job& job) const;

    /// Called when the queue gives a job of one type up (dead-lettered): the
    /// place to close a record the job was meant to finish, so it does not
    /// stay `queued` or `running` for a page that polls it.
    using DeadHook = std::function<void(const json& payload, const std::string& error)>;

    void register_dead_letter(const std::string& type, DeadHook fn) { dead_hooks_[type] = std::move(fn); }

    /// Runs the dead-letter hook of @p job.type, if any. Never throws: the job
    /// is already in the DLQ, a failing hook is logged and left at that.
    void on_dead(const Job& job, const std::string& error) const;

private:
    std::unordered_map<std::string, Handler> handlers_;
    std::unordered_map<std::string, DeadHook> dead_hooks_;
};

/**
 * @brief Static-init self-registration helper, so a handler header can register
 *        itself the way controllers self-register with Drogon. Declare one at
 *        namespace scope in the handler's header:
 *            inline const Jobs::JobHandlerRegistrar k_reindex_job{"reindex", &reindex_handler};
 *        then just #include that header from worker_main.cpp.
 */
struct JobHandlerRegistrar {
    JobHandlerRegistrar(const std::string& type, Dispatcher::Handler fn) {
        Dispatcher::get().register_handler(type, std::move(fn));
    }
};

}  // namespace Jobs
