# Changelog

All notable changes to this project are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [SemVer](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.31.0] - 2026-10-18

### Added
- API keys take a `scope`: `full` (the default, reads and writes the owner's data) or `read` (GET, HEAD and OPTIONS only; any other method is answered 403 `key_read_only` by the middleware). Migration 045 adds the column; every existing key stays `full`. An agent can now be given a key of the owner that reads everything and changes nothing.
- The dock of Now can be reordered by dragging a tile (mouse or touch); the order is the same `corners` setting the Arrange sheet edits, so the sheet remains the keyboard way.
- The app installs on a phone's home screen and opens standalone, without the browser's bars: a web manifest (`/manifest.json`, icons rendered from the favicon) and the Apple meta tags in `index.html`. Standalone Safari keeps its own cookies, so the first open asks to sign in once.

### Changed
- The glass bar (the section pill and the account button) is denser, so the text of a page scrolled under it no longer shows through.
- A goal row no longer carries its kind ("number", "count", "steps") as a line of its own; the kind and the unit open with the row.
- Hide numbers moved into the account menu as well; on a phone the floating pill is gone, since it sat over the last row of every long list.
- Money Ledger on a phone: the period on one line, Filter and Add on the next, the period totals one currency per line.

### Fixed
- Money categories: the list has a Show archived switch and the editor an Archived checkbox, so an archived category can be seen and brought back (the API accepted `archived: false` already, the page never listed archived categories).
- The More menu of a section's tabs works with the keyboard: ArrowDown or ArrowUp opens it on the first or the last item, the arrows walk it with wrap, Home and End jump, Escape closes it and returns the focus.

## [1.30.1] - 2026-10-18

### Fixed
- Money accounts: the editor has an Archived checkbox, so an archived account can be brought back (the API accepted `archived: false` already, the form never sent it).
- Now on a phone: the Add, Links and Tasks row and the dock sit in the flow of the page under the focus instead of fixed over it, so they no longer cover the focus picker on a short viewport (iOS Safari with its bars); the pill nav fades the edge that hides more sections instead of cutting a label.

## [1.30.0] - 2026-10-15

### Changed
- Now shows the state of the day in one glass dock at the bottom, a tile per placed chip (Body, Goals, Food and money, Habits, in the Arrange order), and keeps the actions in the corners the way Momentum does: Add top left (a task, a meal or a spend in one line), the weather top right, Links bottom left, Tasks bottom right with its counts. The clock and the focus line are smaller. Quick links are a new field of the Now settings (`links`, migration 044, up to eight rows of title and a path or an http(s) URL), edited in Arrange, whose four selects now read as the dock order and no longer offer Tasks. The capture left the Food and money panel: the Add corner covers it. Hide numbers moved to the bottom right.

### Fixed
- The `migrate` init container of the API chart carries `AUTH_MODE` like the server container. Since 1.29.0 the ConfigMap renders `app.env: production`, and the boot-time guard refused the migrate step with `auth.mode=none with APP_ENV=production` before any migration ran, so the 1.29.0 API pod never left `Init`.

## [1.29.0] - 2026-10-15

### Added
- Three CI gates from wave 3 of the 2026-10-14 review, all in the `openapi-drift` job: `scripts/check-openapi-breaking.sh` runs the pinned oasdiff 1.33.0 (sha256 checked) over `docs/openapi.yaml` against the merge base with master and fails on any breaking change, so the API contract is additive-only by machine; `scripts/check-openapi-gen-fresh.sh` fails when `tests/e2e/openapi.gen.json` lags behind the YAML; `scripts/check-module-deps.sh` now runs Tarjan over the declared include edges and fails on any cycle not listed in `docs/module-cycles-allowlist.txt` (today only the composition root `api,core,root`). The api test bucket validates handler responses against the OpenAPI schemas through `TestHelpers::expect_matches_schema` (the money, food, tasks, goals, today, billing, jobs, habits, travel, notes, projects, sounds and wallpapers suites); `check-selftest.sh` plants four more breakages for the new checks (33 in all) and prints a visible SKIP where oasdiff or pyyaml is missing.

### Changed
- One production profile. `config/config.production.json` is gone: nothing deployed it, and it had drifted from the chart the pods run (its `jobs.visibility_timeout_sec: 300` never reached the worker, its `api.public_paths` override dropped the PayPal webhook). `make prod-check` now renders `helm/life-os-cpp` with `deploy/values-prod.yaml` and checks the result; `scripts/check-helm-render.sh` asserts the wider floor in CI (`REGISTRATION_OPEN=false`, `APP_ENV=production`, docs UI off, HSTS on, worker lease on, `WORKER_ID` from the pod name, `automountServiceAccountToken: false` on every pod, NetworkPolicy rendered, the gated CronJob and PrometheusRule templates still render) and refuses a non-mikefarah `yq`; `check-selftest.sh` plants seven more breakages for those assertions (28 in all). Of the keys only that file carried: `jobs.visibility_timeout_sec=300` moved to the worker chart (`jobs.visibilityTimeoutSec`, 300 in production), `security.hsts=true` and `docs.enabled=false` to the API chart (`security.hsts`, `docs.enabled`), `app.env` is rendered into the ConfigMap; `server.max_body_bytes`, `database.acquire_timeout_ms`, `database.statement_timeout_ms`, `jobs.retry_backoff_max_ms`, `auth.cookies.*_ttl_sec`, `rate_limit.protected_requests` and `security.hsts_max_age` were dropped because each equalled the code default in `docs/CONFIG.md`; the `api.public_paths` override was dropped on purpose (the built-in list is right); `cache.url` built from `REDIS_HOST`/`REDIS_PORT` was dropped because the chart renders it from `externalRedis.url`. Charts: `WORKER_ID` defaults to the pod name (`worker.id` still pins it), the worker's uploads emptyDir is capped at 256Mi like the API's, the outbox relay is on in production (`outbox.drainIntervalSec: 5`), a `checksum/extra-env` pod annotation plus a `secretsVersion` value roll the pods after a secret rotation, the API NetworkPolicy is enabled in `deploy/values-prod.yaml` (ingress from the frontend pods, egress to namespace `db`, DNS and 443), the PrometheusRule alerts carry the names of `docs/SLO.md` (`High5xxRate`, `HighP99Latency`, `ReplicationLagHigh`, `DeadLetterQueueGrowing`, `JobsQueueBacklog`, `DbPoolSaturationHigh`, `RetriesExhaustedSpike`, `ApiTargetDown`, `WorkerTargetDown`) with its thresholds (5 % 5xx, 60 s lag, DLQ above 0 for 15 m, 0.1 exhausted retries/s), the monitoring blocks are present but off in both prod overlays with the exact keys to flip once prometheus-operator is confirmed, and a disabled `backup-mirror` CronJob (`rclone copy` of the barman bucket to an off-cluster bucket) is ready to enable. Compose and CI run `postgres:18-alpine` like production (the compose primary names `PGDATA` because the 18 image moved its data dir). `migrations/README.md` states the expand/contract rule and the PR template asks for it. `scripts/env-check.sh` no longer accepts an exported empty value as set.
- Configuration keys live in one registry, `src/utils/ConfigKeys.hpp` (one `Config::Keys::<name>` per knob, 153 at the time), read as `cfg.get(Keys::name, default)`; the eleven module switches of `Modules.hpp` share one function over it and the worker reads its id and concurrency by entry. `scripts/check-config-sync.sh` parses the registry (every literal triple has an entry, every entry is read, every `Keys::name` is declared) and fails when two sites read the same knob with two different literal defaults, naming both sites; the selftest plants that case.
- Module layout: each module's job handlers moved from `src/jobs/` next to their module (`src/food/jobs/ParseJob.hpp`, `src/money/jobs/{ParseJob,AdvisorJob,RatesJob}.hpp`, `src/tasks/jobs/ParseJob.hpp`, `src/fitness/jobs/SyncJob.hpp`; job types and payloads unchanged) and register through `<Module>::register_jobs()`, called once by `Core::register_job_handlers()` (`src/core/JobHandlers.cpp`); the email handlers register from `src/email/jobs/`, so `src/jobs/` holds only the queue, the dispatcher, the outbox and the webhook and demo handlers and includes no module. `core/Modules.hpp` moved to `utils/Modules.hpp` (same `Core::` names). The email/jobs/core include cycle is gone. `GET /api/v1/today` reads each module through one digest facade (`src/<module>/now/Digest.hpp`) instead of composing eight modules' repositories in the controller; the response is unchanged. Scaffolders follow the current shape: `new-job.sh <module> <job-type>` writes the handler into `src/<module>/jobs/` and wires its registration, `new-resource.sh` emits a `.hpp` + `.cpp` controller pair and a repository under `src/repositories/<module>/`, `new-endpoint.sh` emits a `.hpp` + `.cpp` pair and an OpenAPI response with a schema reference, `new-migration.sh` no longer offers a `your_table` skeleton to copy.

### Fixed
- CI on master runs once per commit: back-to-back merges no longer cancel the
  pending run of an earlier commit, which had left the 1.28.0 release commit
  without a CI run and failed release.yml's ci-green check.
- The three minors left from the 1.27 review. Cancelling a job also removes it
  from the delayed set, so a retry waiting out its backoff or a deferred job is
  not promoted and run after the cancel. A parse or advisor row failed before
  its claim (module off, queue gave up) closes only a queued row or a stale
  running one; a running row a live worker holds keeps its own outcome.
  `config/config.sample.json` lists `/api/v1/account/join-from-invite/*` in
  `api.public_paths` like `config.json` and the built-in default do.
- A refund of a payment that never credited the wallet (a capture that failed
  the amount or currency check keeps its capture id with status `failed`) is
  recorded with the outcome `skipped_not_credited` and no longer debits the
  credits that other payments put on the balance (migration 041 widens the
  `billing_refunds.outcome` check).
- The `runbook_url` of every alert points at this repository on GitHub instead
  of a GitLab host; `docs/CONFIG.md` lists the nine job types a worker
  registers and the compose stack subscribes to all of them (`email.send` and
  `webhook.deliver` were missing); the README quick start says that `make up`
  runs the last release image and names the targets that build the checkout.
- Frontend: one amount parser (`src/lib/amounts.ts`) replaces the ten copies
  in money, goals, food and workout forms, so `1,234` is refused instead of
  being read as 1.234 and `0x10` or `1e3` are not numbers; the Body page names
  a custom period by its dates instead of "Last custom days"; every POST, PUT,
  PATCH and DELETE carries an `Idempotency-Key`, minted once per call and
  reused on the replay after a token refresh, so a retried batch or parse is
  applied once.
- `fail()`, `defer()` and `dead_letter()` put the job on its next list (live
  queue, delayed set or DLQ) before releasing it from the worker's processing
  list, so a crash between the two writes duplicates the job instead of losing
  it; `pick()`, `recover_processing()` and `promote_due_jobs()` skip a job
  whose blob is already completed, dead or cancelled, so a cancel that lost a
  race with a pop, or a crash right after a completion, cannot run the job
  again.
- Every "today" default in the API now reads the day in `?tz=` (UTC without it), the way the agenda, the digest and the habits already did: `GET /food/day`, the food targets behind `GET`/`PUT /food/goals`, `/food/week` and the day, `POST /goals/{id}/checkins` without a `date`, `GET /money/summary`, `GET /money/rates`, `GET /money/rates/convert`, the `date` of `POST /money/advisor/run`, and `done_since` of `GET /tasks/items`, which starts the day in `tz` instead of UTC. A task linked to a money ledger row is checked for ownership whether the money module is on or not. `GET /money/transfers` answers `total` next to `count` so a client can page (the ledger page loads more transfers on demand). `GET /tasks/items?status=` documents `in_progress`. A parse job that is not the caller's answers `parse_job` in every module (food already did; money said `money_parse`, tasks `task_parse`).
- Documentation of the 2026-10-14 review, wave 2. `docs/RUNBOOK.md` gains "Rotate a secret" (JWT, the Mi Fitness token key, the S3 key, the PayPal secret, the database role password: what each one reaches and the restart through `secretsVersion`), a "Job types" table of the nine handler types with producer, payload and retry contract, the off-cluster backup mirror, a restore drill, the monitoring enablement check and the first rollout of the API NetworkPolicy; `make tail-trace` is described as the log grep it is. `docs/SLO.md` carries the chart thresholds and names `helm/life-os-cpp/values.yaml` and `docker/prometheus-rules.yml` as their source. `docs/CONFIG.md` has one heading per module instead of everything under "Food module", says what the Default column shows, and gives the code defaults of `MAIL_SMTP_HOST` and `DATABASE_PRIMARY_URL`. `docs/ARCHITECTURE.md` §8 replaces "75 routes in 10 controllers" with the counted 243 routes in 22 controllers and records that the next step is an `oasdiff` gate. `CONTRIBUTING.md` states the real commit rule (plain subjects, `chore(release): x.y.z`), drops the required reviewers and the security team, lists every bump point of `release.sh` and says the coverage floor is local-only. The fitness design note is marked historical, `THIRD_PARTY_NOTICES.md` lists the vendored inja, the OpenAPI `info` block names the module families and the real host, the `0xff` admin comments in `Role.hpp` and `Guards.hpp` describe the sentinel bit, `docs/INDEX.md`, `docs/BENCHMARKS.md`, `docs/CI-PROFILES.md`, `docs/CONVENTIONS.md`, `changelog.d/README.md` and the README lose their stale statements (`make docs`, four presets, the quick gate job, `config.production.json`, the one-shot modes, the de-inlined inventory, "existing flows are not routed through the outbox"), `docs/Doxyfile` reads 1.28.0 and the gate selftest step labels say 28 planted breakages.
- The LLM job rows (food, money and tasks parse jobs, the money advisor review) now write their queue job into the transactional outbox inside the same transaction as the row when `OUTBOX_DRAIN_INTERVAL_SEC` is set, so a crash or a Redis outage right after the commit can no longer leave a row `queued` with no job behind it (with the drain off the direct submit after the commit stays as before); a `queued` row nobody picked for ten minutes reads as `failed` with `queue_lost` instead of queued forever, and a late delivery still runs it; and a worker subscribed to several job types moves each id from its queue into the processing list with one Redis command (BRPOPLPUSH per type in rotation) instead of a BRPOP followed by an RPUSH, which lost the id when the worker died in between.
- A delete refused by an `ON DELETE RESTRICT` foreign key answers 409 (`in_use`,
  `role_in_use`, billing history) on PostgreSQL 18 too: that major reports the
  violation as SQLSTATE 23001 (`restrict_violation`), which the code treated as
  an unknown error and turned into a 500. Tests and compose now run Postgres 18,
  which is how it showed up.
- Migration 042 attaches the shared `touch_updated_at()` trigger to the 26 tables that had an `updated_at` column and no trigger (fitness, food, money, tasks, goals, now, travel, notes, projects, wallet balances), and the repositories stop setting the column by hand; a test asserts every such table carries it. Migration 043 adds the `(quote, date)` index `money_fx_rates` is read by. `GET /money/currencies` reads before it seeds, so only a new user's first call writes. `PATCH /money/currencies/{code}` answers 409 `decimals_in_use` to fewer decimals while an account, a budget or a receipt keeps the currency. Ticking a task focus (`POST /today/focus/done`) closes the task and marks the focus in one transaction. The notes search uses the shared `escape_like`.
- Frontend: Hide numbers now covers the classic pages too (the toggle sits in
  the top nav, the wallet balance and the billing figures wear `Num`); a
  sign-in returns to the full location with its query string; "Post all" and
  "Copy packing" run as one sequential batch that reports "N of M failed";
  notes dates render in the browser zone; admin tables and tab strips work
  from the keyboard; ledger filters live in the URL; the workout session
  survives a blocked localStorage; rendered-hook tests cover the parse job,
  the set queue and the sign-in round trip; eslint runs with zero warnings and
  prettier is checked in CI.

### Security
- A JWT whose header or payload is not a JSON object, or whose `alg`/`typ` is
  not a string, answers 401 (`malformed_header`, `unsupported_alg`,
  `malformed_payload`) instead of throwing out of the auth middleware before
  the signature check; the middleware also turns any exception from token
  verification into 401 `malformed_token`.

## [1.28.0] - 2026-10-14

### Added
- Money Accounts by dollar value: the list sorts by what each account is worth
  in USD (from `/money/summary`), largest first, with the dollar value under each
  non-USD account; "Hide under" keeps accounts worth less than $1, $10, $100 or
  $1,000 out of sight (by value, so 0.05 BTC stays); archived accounts show only
  when asked; "By currency" keeps the old groups. The choice is remembered in
  the browser.

### Fixed
- An API key of an administrator keeps the data permissions (general, fitness
  read, fitness sync) once its admin bit is masked: the Administrator role is
  the sentinel bit alone, so 1.27.0 left such a key with no permission at all
  and every fitness, workout and habits call answered 403.

## [1.27.0] - 2026-10-14

### Added
- Invite-only registration: `auth.registration_open` / `REGISTRATION_OPEN`
  (default `true`, `false` in prod) makes `POST /api/v1/auth/register` answer 403
  `registration_closed` while admin invites keep working, and the public probe
  `GET /api/v1/auth/registration` returns `{"data":{"open":bool}}` for the
  sign-in page.

### Changed
- Controllers share one copy of their helpers: `parse_object`, `no_content` and
  the 50/200 list limits in `api/HandlerSupport.hpp`, `require_user` and the
  `API_MODULE_USER_GUARD` / `API_MODULE_PERMISSION_USER_GUARD` module guards in
  `api/Guards.hpp` (same status codes and bodies), and `SQL_ISO_UTC` in
  `repositories/SqlTime.hpp` for the timestamp SQL. The tasks inbox repository
  is `InboxRepository`; `Jobs::cancel` is atomic under concurrent calls;
  `scripts/new-module.sh` and `scripts/new-react-page.sh` scaffold today's
  module switch and kit page.
- Frontend cleanup: a data router (`createBrowserRouter`) built from the route manifest; one `lib/dates`, one `formatAmount`, one `useModuleProbe` and one `useParseJob` in place of the copies; the tasks inbox is `useInbox` / `qk.tasks.inbox`; the unused billing metrics helpers, kit `Amount`, TailAdmin tokens and direct typescript-eslint packages are gone.
- Infra and CI: the worker pod waits in a `wait-for-schema` init container
  until the database carries every migration of its version (API image,
  `--verify-migrations`, 10-minute cap), and the upgrade order API, worker,
  frontend is in `docs/RUNBOOK.md` and the `release.sh` next steps.
  `release.yml` builds only a tag on `master` whose commit passed CI, refuses a
  manual rebuild of a published version without `force`, signs before promote
  and fails on fixable HIGH as well as CRITICAL. CI scopes a push by the whole
  push and no longer cancels runs on `master`; helm and yq downloads are pinned
  by sha256. The frontend nginx gzips text assets, hides its version and sends
  `form-action 'self'`. `docs/openapi.yaml` and `frontend/package.json` carry
  the app version (checked by `check-version-sync.sh`, bumped by `release.sh`);
  the `values-prod.example.yaml` overlays are gone; README, ARCHITECTURE and
  the RUNBOOK backup and restore section match the cluster.
- Notes: the search, the topic switch and New note sit in one row under the
  headline; the search is a quiet pill with a glyph (Food's search too); on a
  phone the project of a note moves under its title instead of cutting it.

### Removed
- Unused code and config: the Kafka messaging subsystem (`src/messaging`,
  librdkafka, the `messaging.*` config keys, the compose Kafka/Zookeeper/exporter
  services, the cpp-env Kafka template, `make up-kafka`), `Webhooks::send`, the
  `note` part of `GET /api/v1/today` (`PUT`/`DELETE /api/v1/today/note` stay),
  the empty `src/billing/Billing.hpp`, `Repositories::safe_order_by` and the
  unread `billing.provider` key.

### Fixed
- Security fixes from the review: logout works after the access cookie expired
  and revokes the refresh token; `/refresh` refuses an expired token; emails
  over 254 bytes fail validation before the regex; API keys lose the admin bit
  and answer 401 `key_not_allowed` on `/api/v1/account/*` and `/api/v1/admin/*`
  (path canonicalized first); the default role cannot hold the admin bit and the
  last admin role cannot lose it; `POST /api/v1/jobs` takes only `echo`, `slow`,
  `fail` and `money_rates`; webhook hosts are resolved and refused when any
  address is internal; a used reset link invalidates the others; password
  reset/change revoke API keys; admin delete or role change drops refresh
  sessions; the shared HTTP client follows only http(s) and caps bodies at 16 MiB.
- Data and job fixes from the 2026-10-14 review. Deleting a user now also
  deletes the Mi Fitness rows of the Xiaomi account they linked, and only the
  billing foreign keys are reported as `user_has_billing_history`. A parse or
  advisor job whose row another delivery still holds waits until the row turns
  stale instead of spending its retries; every write after the claim is fenced
  by the attempt; a row whose worker died reads as `failed` (`worker_lost`), and
  a row the queue gave up on is closed as `failed` (`queue_gave_up`). Queued
  rows older than ten minutes no longer count toward `too_many_parses`. Retry
  backoff is on by default (`JOBS_RETRY_BACKOFF_BASE_MS=2000`). A late Xiaomi
  login can no longer overwrite a fresh re-link (writes fenced by `rotated_at`).
  With `?tz=` and no date, Today, Tasks, Goals, Travel and the Money report and
  parse take today in that zone instead of UTC. Habit streaks no longer lose
  their newest days when an old range is viewed. A money account or category
  delete that loses a race answers 409 `in_use`, not 500. Money amounts with
  more than 15 significant digits are refused (`precision`). `/today` counts the
  inbox instead of loading it, and three foreign keys got indexes (migration
  040).
- Frontend fixes from the review: a failed session refetch no longer replaces the page when a user is cached; the notes editor keeps words typed during a save and asks before leaving a dirty draft; the tasks inbox posts once per Enter; the sheet's close button asks like Escape; sign-out reports a failure and wipes the user's browser storage (the Now cache keeps only today); Notes shows while its probe runs and Now waits for every module probe before redirecting; detail pages say the record is gone on a 404; the pill nav keeps the current section in view; a mutation stays pending until its lists refetch; Billing in the account menu only when billing answers; the day panel compares rates with the local date; the sign-up links hide when `GET /api/v1/auth/registration` says closed.

## [1.26.0] - 2026-10-14

### Added
- Habits under `/api/v1/habits` (module switch `HABITS_ENABLED`, migration 037):
  habits every day or N times a week with an optional goal, ticked by hand or
  from data (sleep, steps and active kcal from Mi Fitness, a finished Training
  session), evaluated on read on the caller's local day, with the current and
  the best streak. A Habits section (Today and Week) and a Habits plate on Now
  that Arrange can place; `/api/v1/today` gains the `habits` part.
- Learning: courses are projects of kind course, with platform, dates, link and
  certificate. The section lists them with progress by steps; a course page
  holds its steps (tasks) and its notes. Shown when Tasks is on.
- Notes: private Markdown notes with the fields of a tarassov.me post (slug,
  title, summary, body, topic, tags), so a note can go to the blog later. A list
  grouped by month with search and topic chips, an editor with a live preview
  that never runs HTML, a note made from a task or for a project or course.
  Switch `NOTES_ENABLED`.
- Tasks: projects group tasks. A Projects tab lists them with open tasks and the
  next step; a project page holds its tasks and a quick add filed under it; a
  task row shows its project; the task form picks one; "#code" in a typed task
  files it under that project. Courses of Learning are projects of kind course.

## [1.25.1] - 2026-10-14

### Changed
- The image's own nginx config no longer reads X-Forwarded-For (added in 1.25.0
  on a wrong diagnosis); the cluster uses the chart's config and never changed.

### Removed
- The note of the day is off the Now page: it crowded it. The note routes stay
  in the API.

### Fixed
- Now: with a city set, the top line is the place, the degrees and a text glyph
  for the weather (the word in its tooltip), without the time zone; Arrange moved
  from that line into the account menu (on Now only); Hide numbers
  leaves task and goal titles readable, as on Tasks; the corner layout paints
  from the last answer, so a moved corner does not flash. Capture: a spend line
  shows its currency and account, an empty parse says so, a meal parse error is
  readable, the Meal/Spend switch is a pair of toggle buttons and the field takes
  the focus back. Travel: deleting a document asks first. Arrange drops a chip it
  does not know. An odd answer of the geocoder or the weather service no longer
  gives a 500.

## [1.25.0] - 2026-10-14

### Added
- Now: an Arrange sheet sets the weather city (geocoded once, weather from
  Open-Meteo, cached for an hour) and which chip sits in which corner, a corner
  may stay empty; a note of the day under the focus with yesterday's below; the
  Food and money panel takes one line of text and adds the parsed meal to the
  day or the parsed spend to the Money inbox.
- Travel: a Documents tab for the family's passports, visas and insurance with
  their expiry; a trip warns about a passport valid less than six months past
  its end and a visa or insurance that ends before it does.

### Fixed
- The image's own nginx config (used by docker compose, not by the cluster,
  whose config comes from the cpp-frontend chart) read the client address
  from X-Forwarded-For for 10.42.0.0/16. Correction after the release: the
  login throttle on the cluster already saw the client, because Traefik sets
  X-Real-Ip and the chart's nginx passes it on; the change is reverted.
- Travel: a flight can be marked as needing no border crossing (domestic, or one
  you are not on) and the trip page stops asking for one; a trip total shows a
  rate day only when a rate was used; a spend linked to one trip is not moved to
  another by linking; creating or editing a trip answers in the asked currency;
  a counter past an "over" threshold reads "Threshold met".

## [1.24.0] - 2026-10-13

### Added
- Day counters take a calendar-year window (January 1 to the date), the way
  Russian tax residency sets the final status for a year's income.

## [1.23.1] - 2026-10-13

### Security
- The frontend image takes the Alpine package fixes at build time, which brings
  OpenSSL to 3.3.7 (CVE-2026-31789); 1.23.0 was not deployed because its image
  scan failed on that CVE.

## [1.23.0] - 2026-10-13

### Added
- Travel under `/api/v1/travel` (module switch `TRAVEL_ENABLED`): border
  crossings, day counters per rule (rolling window, 12 months ending this year,
  since the last entry; at most or over a threshold) with the Schengen 90/180
  and Kazakhstan tax residency templates, the date a running stay meets the
  threshold and the date a full stay fits again; trips with a plan of flights,
  stays and notes, packing from the tasks linked by `trip_id`, and expenses
  linked to a trip with a total in one currency. `/api/v1/today` carries the
  next trip and the counters within 14 days of their threshold.
- A Travel section with Trips, Days and Rules tabs: a trip page with its plan,
  packing list and linked spends, crossings and counters on Days, rules with
  their source. Now counts down to the next trip and puts a counter close to
  its threshold on the Goals corner.

### Changed
- The helm gate now renders the real prod values in `deploy/` and requires rate
  limiting (fail-closed), CSRF and idempotency there; `release.sh` bumps and
  `check-version-sync.sh` checks the prod image tags; the unused stage overlay is
  gone; the release workflow scans the frontend image and no longer runs twice
  for one tag; CI lints the OpenAPI spec; docs match the current prod setup.

### Fixed
- Backend fixes from a review: parse jobs left running by a dead worker no
  longer lock a user out of new parses, and a late result cannot overwrite a
  finished one; a money advisor review the queue refused is marked failed, and
  Run now rewrites a review made before its period ended; the focus of the day
  handles Cyrillic titles; a failed fitness probe answers with a JSON 503;
  deleting a user removes their money, tasks, goals, food, workout and focus
  rows; a refresh token can be used only once even under a race; a racing
  workout start is a 409; the exercise search matches `%` and `_` literally;
  money amounts go down to one satoshi.
- Now refreshes after a change in Goals, Food, Money or a band sync; failed goal and task actions say so; Tasks and Goals name their own module when it is switched off; dialogs are announced by their heading; the goal gap on Now blurs under Hide numbers. In production login is throttled, cookie sessions use a CSRF token and retried requests are answered once.
- Dialogs on the admin and profile pages keep the page's own controls and font; a transfer row shows the received amount under its title so two long amounts fit on a phone.

## [1.22.0] - 2026-10-13

### Changed
- Body sits on the calm sheet: the period as facts on one line, the steps chart open, the other charts as rows that open on a click (two at a time), workouts and SpO2 as rows, sync folded into one row.
- Food sits on the calm sheet: the day as kcal of the goal with thin macro bars, meals as groups of rows, the week as a chart and day rows, and the numbers of the add sheet in two rows on a phone.
- Goals sit on the calm sheet: the count of active goals as the headline, the three tiles of a goal as one facts line, and the milestones of a pass or fail goal as a list instead of a timeline that ran off a phone.
- Dialogs open over the whole screen, centred on a desktop and from the bottom on a phone.
- Money sits on one calm sheet: rows with details on click, filters and forms in sheets, all accounts as the headline, and Hide numbers blurs only the amounts.
- Tasks moved onto the same calm sheet frame as the other sections; the weekly review line is a banner and the capture example is neutral.
- Training sits on the calm sheet: readiness as one facts line, routines, recent workouts and history as rows, exercises as rows with a small photo, and the session keeps time, sets and volume pinned while you scroll.

### Removed
- Every section is on the calm sheet; the wide legacy sheet and its whole-sheet blur are gone. Dialogs opened from admin and profile pages keep their look.

### Fixed
- The Goals panel on Now uses the pace words of the Goals page ("behind 2",
  "not updated", "79 days left"); `/api/v1/today` carries each goal's unit and
  full progress for that. A focus session started, paused or ended in one tab
  shows up in the others. An opened corner panel takes the keyboard focus. The
  all-accounts total on Money shows USD instead of disappearing when the
  remembered currency has no rate. The task capture shows a neutral example
  instead of a personal one.

## [1.21.0] - 2026-10-13

### Added
- `GET /api/v1/money/summary?currency=USD&days=90` gives all Money accounts in
  one currency at the day's rates: the total with each account's rate and rate
  day, money without a rate listed apart (the total then says it is partial),
  the total of every day of the period and the change split into what the ledger
  did and what the rates did. It answers to an API key too. `/api/v1/today`
  carries the same total in USD as `money.net_worth`. The rates job always keeps
  RUB with USD, EUR and KZT.
- Money has a Summary tab: all accounts in USD, EUR, KZT or RUB at the day's
  rates, the line over 30, 90 or 365 days with a hover readout, the change split
  into the ledger and the rates, each account with its rate and rate day, and
  money without a rate listed apart. A card on top of the Ledger and a row in the
  Food and money panel on Now show the same total. Everywhere the total shows
  the rate day and says "partial" when some money has no rate; the change says
  so when money had no rate on the first or the last day.

### Changed
- Wallpapers, posters and focus sounds are uploaded with `Cache-Control:
  public, max-age=31536000, immutable` (a file never changes under its key), so
  the browser keeps them instead of downloading the video again for every new tab.

## [1.20.0] - 2026-10-13

### Added
- Focus mode on Now: "Focus" with 25, 50 or 90 minutes under the day's focus.
  The nav, the corners, the clock and the shade go away; the wallpaper, a big
  countdown and the focus stay, and a sound of the library plays in a loop with
  a gentle fade in. A thin glass bar (pause, sound and volume, exit) shows when
  the pointer moves and fades after three quiet seconds; Escape exits. At zero
  the sound fades out and Done offers to tick the focus. A session survives a
  reload. The admin's library (`/admin/library`) now keeps wallpapers and focus
  sounds; `/admin/wallpapers` leads there. The old 25-minute timer is gone.
- A library of focus sounds next to the wallpapers (`/api/v1/sounds`,
  `/api/v1/admin/sounds`): the admin uploads MP3, AAC/M4A or Ogg files up to
  30 MB straight to the bucket through presigned URLs, every user lists the
  ready ones. Migration 032.

### Fixed
- Deleting a task, a goal, a ledger row or a transfer no longer shows "returned
  an empty body" and leaves the row on screen: the API answers these with 204 No
  Content, and the client now takes an empty 204 as done.

## [1.19.0] - 2026-10-12

### Added
- Now gets four corners, Momentum style: Body (sleep and steps, weight over the
  week), Goals (the nearest countdown, a goal behind), Food and money (protein
  left, spent today) and Tasks (today and late). Each corner is one quiet line
  with a dot when something is off; a click opens its glass panel over the
  picture (on a phone, a sheet from the bottom): sleep stages and resting heart
  rate, countdowns and pace, kcal and macros with today's spends, late and
  today's tasks with ticks and a one-phrase quick add for today. A "Focus 25 min"
  timer sits under the focus and keeps its time across tabs. The quiet lines
  under the focus give way to the corners, and "Hide numbers" moves to the bottom
  centre.

### Changed
- `GET /api/v1/today` carries what the corners of Now show: up to five late
  tasks and the status of today's tasks, each goal's progress and days left,
  last night's sleep with its stages, steps, active kcal and resting heart rate
  at the band account's day, the weight and its change over the week, fat and
  carbs eaten against their targets, and up to five of today's spends, each in
  its own currency. The money block is now `{currencies, spends_today}`.

## [1.18.0] - 2026-10-12

### Added
- Now: a new module (`NOW_ENABLED`) behind the Now screen. `GET /api/v1/today`
  gives the day's digest in one call: late and today's tasks, active goals with
  their pace (behind first), food eaten against the targets, money spent today
  per currency with the month's projection and usual spend, last night's sleep,
  the day's steps and the latest weight. A section that is off or fails is left
  out and named in `errors`. The focus of the day (`/api/v1/today/focus`) is a
  task, a goal or typed text; ticking a task focus closes the task. The
  wallpaper library (`/api/v1/wallpapers`, `/api/v1/admin/wallpapers`) keeps
  videos and images in an S3-compatible bucket (`S3_*` settings): the admin
  uploads straight to the bucket through presigned URLs, and every user lists
  the ready ones per device. Migration 031.
- Now and the glass shell: every screen now sits over one picture, a random
  wallpaper from the shared library (a video on a desktop, an image on a
  phone), with a glass pill nav (Now, Tasks, Goals, Money, Food, Body,
  Training) and an account menu instead of the sidebar. `/now` is the new
  landing screen: the clock, a greeting by name, the day's one focus (a task,
  a goal or typed text; ticking a task closes it) and up to three quiet lines
  when something is off (late tasks, a goal behind, protein left in the
  evening or a month above usual spend). Sections not redesigned yet sit on one
  glass sheet. "Hide numbers" blurs amounts for a shared screen. Admins upload
  wallpapers on `/admin/wallpapers` straight to the bucket, with a poster frame
  taken in the browser. With the now module off, Now is left out and the app
  opens on the first section. The frontend chart's `csp.storageHosts` lets the
  bucket through the CSP (`docs/wallpapers.md`). A Chrome extension in
  `tools/newtab-extension` opens Now in every new tab.

## [1.17.1] - 2026-10-08

### Fixed
- Tasks: `%` and `_` in the search are literal, `done_since` counts from
  midnight UTC, the someday list is newest first, an empty due from the phrase
  reader means no date, a failed parse says what went wrong in words, and a
  task's details have "Move date". A task from a ledger row opens that row
  (`/money?row=<id>`). Goals: a check-in before the goal's start is refused, a
  pass / fail goal is done exactly when its result is set (no closing without
  one, no reopening with one), Passed and Not passed ask before they close the
  goal, the list reads every check-in in one query, and a task of a closed goal
  still shows that goal in its form. Money: refreshing today's rates while a
  rates job waits queues nothing new and answers `already_queued`.

## [1.17.0] - 2026-10-08

### Added
- Goals: a new module (`GOALS_ENABLED`) for goals of four kinds. A `number`
  goal has a start, a target and a unit, and its check-ins (one per date); a
  `steps` goal has sections of tasks; a `count` goal counts done tasks against a
  target; a `binary` goal has milestones and a result that closes it. Every
  goal comes with its progress and pace at the caller's date (`?date=`):
  expected value, gap, needed pace per week, "not updated" after 14 days.
  Tasks can belong to a goal and to one of its sections, and gain the
  `in_progress` status; a goal's undated tasks stay off the agenda's someday.
  Migration 030.
- Goals page: active goals as rows with the area colour, the kind, the due
  date, a progress bar and a pace word ("on track", "behind 1.2 kg", "not
  updated", "84 days left"). A row opens in place: a chart of check-ins against
  the even pace for a number goal, sections with their tasks for a steps goal,
  a shelf of done items for a count goal, a deadline line with milestones and
  Passed / Not passed for a binary goal. Tasks get a Goal, a Section and a
  Status (with "in progress") in their form, and show their goal.

## [1.16.1] - 2026-10-08

### Fixed
- Tasks: the `/api/v1/tasks` routes answered 404 in 1.16.0 because the
  controller was not compiled into the API binary (it was missing from
  `src/api/Api.hpp`, the header that pulls every controller in).
  `scripts/check-routes-registered.sh` now fails when a controller with routes
  is not included there.

## [1.16.0] - 2026-10-08

### Added
- Tasks: a new module (`TASKS_ENABLED`) for personal tasks. `/api/v1/tasks/items`
  keeps tasks with one of six areas, an effort, a due date, a next step and a
  source (a link or a money ledger row of the same user); `GET
  /api/v1/tasks/agenda?date=&tz=` groups them into late, the next two days,
  dated, someday, done today and two review lists, reading the day in the
  caller's time zone; `/api/v1/tasks/notes` is the inbox; `POST
  /api/v1/tasks/parse` turns one phrase into drafts through the `tasks_parse`
  job, which uses the money LLM provider with its own prompt
  (`TASKS_LLM_PROMPT_PARSE`, ConfigMap `life-os-tasks-llm`). Migration 029.
- Tasks pages: Today (late, the next two days, done today and a weekly review
  line), Later (dated, someday and the review lists) and Inbox (notes that
  become tasks). A task is typed as one phrase and confirmed as a draft; a row
  shows the area colour, the title and the due date, and opens its next step,
  effort and source on click. The sidebar's Tasks entry is live while
  `TASKS_ENABLED` is on.

## [1.15.0] - 2026-10-07

### Added
- Money: a Currencies tab lists each currency with its smallest amount and the
  unit's name (0.01 · kopeck, 0.00000001 · satoshi), adds a currency (a code
  outside the ISO table names its decimals and unit) and edits the name, role
  and archive flag. In a category's dialog a merchant chip opens a picker that
  moves the merchant to another category: its next rows are suggested there,
  posted rows keep their category.

### Changed
- Money: `PATCH /api/v1/money/merchants` takes `{merchant_key, category_id}` in
  the body and replaces `PATCH /api/v1/money/merchants/{key}`, since keys hold
  spaces, `&`, `'` and any script. A display name is normalized to its key.
  `POST /api/v1/money/rates/refresh` with a range answers 409
  `backfill_running` while earlier rate jobs still wait in the queue and the
  range has days without rates, so no day is queued twice.

### Fixed
- Money reports: a bank recalculation now counts in the category of the expense
  it adjusts, so it moves that category's budget share and, for a fixed
  category, the fixed share. Week reports find monthly charges: recurring
  charges are looked for over at least the 120 days up to the period's end.
  A posted row's `final_amount` takes only posted adjustments (a pending one
  waits in the inbox, like the balance); each adjustment now carries its
  `status`.

## [1.14.0] - 2026-10-07

### Added
- Money pages: the ledger filters by type and category as well (and opens
  filtered from `/money?category=…`); the Reports tab takes a custom range next
  to the calendar week, month and quarter; each category lists the merchants the
  memory sends to it, on the list and in the editor, with a link to its rows;
  the manual form warns when a posted row of the same account and amount sits
  within a day of the date, and its button turns into "Add anyway".
- Money: every currency has its smallest unit. A code in the built-in table
  (ISO 4217 minor units plus BTC) gets its decimals and the unit's name on
  `POST /api/v1/money/currencies` (the tiyn of KZT, the kopeck of RUB, the
  satoshi of BTC, 0 decimals for VND), and an amount finer than that unit is
  refused with 400 on every write: transactions, receipts, transfers and fees,
  opening balances, budgets. Another code gives its decimals (0..8) and may name
  its unit. Migration 028 widens money amounts to eight decimals and adds
  `minor_unit`; `POST /api/v1/money/rates/refresh` takes `{from, to}` and
  queues one rates job per day without rates (the source starts on 2024-03-02).

## [1.13.0] - 2026-10-07

### Added
- Money module, fourth part: the advisor. A weekly written review of the past
  week, on each user's chosen weekday at `MONEY_ADVISOR_HOUR_UTC` (default 7),
  and on demand for this week, month or quarter. The `money_advisor` worker job
  sends the LLM only numbers the server computed (the Reports page's figures,
  category names, balances per currency, the user's note) with the prompt
  `MONEY_LLM_PROMPT_ADVISOR`, and stores the review with those facts so every
  figure can be checked; a period without rows gets one line and no provider
  call; one review per user and period. Routes `GET /api/v1/money/advisor/reports`,
  `GET .../{id}`, `POST /api/v1/money/advisor/run`; the Advisor tab renders the
  review as plain text (headings, lists, bold) with the facts underneath. The
  report building moves to `repositories/money/ReportBuilder.hpp`, shared by the
  page and the job.

## [1.12.1] - 2026-10-07

### Fixed
- Money pages after review: the period totals come from the server's report
  (all rows of the period, not the loaded page), transfers and exchanges of
  the period are listed and can be edited or deleted, a bank recalculation
  sits under its original, the add form forgets a remembered account that no
  longer exists and drops a parsed category of the other kind, empty dates
  and names are caught before the request, the inbox buttons wait for the
  request, the search waits for a pause in typing, receipt amounts use the
  currency's format, and the "as if" note reads correctly when the target
  currency itself has no rate.

## [1.12.0] - 2026-10-07

### Added
- Money module, first part (off by default, `MONEY_ENABLED`): currencies,
  accounts with computed balances, categories with a budget in one currency,
  the ledger of incomes, expenses and the bank's signed fx adjustments,
  transfers and exchanges as one row with the amounts sent and received,
  an inbox of pending rows with a duplicate flag, merchant memory, period
  reports per currency (never summed across) with the previous period, the
  median of three, the fixed share, the projection, recurring charges and
  new merchants, and an optional "as if" block converted at a stored daily
  rate. Rates come from fawazahmed0/currency-api through the `money_rates`
  worker job (`MONEY_RATES_SCHEDULE_HOURS`). Every page action is a route
  under `/api/v1/money/`, so an agent with an API key can do the same.
- Money section in the dashboard, for every confirmed user: the ledger of a
  week, month or quarter with per-currency totals (never one sum), the inbox
  of proposed rows with a check for possible duplicates, and an add form by
  hand (merchant memory fills the category), from a bank mail or a list
  through the LLM, from a receipt photo (scaled in the browser before upload),
  or as a transfer (the received amount only across currencies); accounts
  grouped by currency with computed balances; categories with budgets against
  this month's spend in the budget's currency; reports per currency with the
  previous period, the median of three, the projection, recurring charges and
  new merchants, and an optional "as if" currency with the rates and dates.
  The sidebar item hides while the money module is off.
- Money module, second part: a bank mail, an SMS, a list of purchases or a
  receipt photo becomes proposed ledger lines through the `money_parse` worker
  job. The job sends the user's accounts (with the card's last four digits),
  categories and remembered merchants to the OpenAI-compatible API named by
  `MONEY_LLM_*` (its own ConfigMap and Secret `life-os-money-llm`); an account
  or category the model names that is not the user's is dropped with a note.
  `POST /api/v1/money/parse`, `POST /api/v1/money/parse/receipt` (a data URL,
  4 MB), `GET /api/v1/money/parse/{id}`, and `POST .../accept`, which puts the
  edited lines into the inbox as pending rows, once. The LLM call is shared
  with the food parse (`src/llm/Chat.hpp`).

## [1.11.3] - 2026-10-06

### Fixed
- Food module after review. A `food_parse` row can no longer stay `running`:
  an unexpected error after the claim requeues the job or fails it on the
  last attempt, a row a crashed worker left behind is claimed again after
  10 minutes (migration 024, `started_at`), the retry limit travels with the
  job, and a `null` model in the answer is tolerated. Dates are checked as
  calendar days between 1900 and 2100 (`2026-02-30` is a 400, not a 503);
  `null` in a required entry field is a 400, not a 500; names and notes are
  limited and cut by characters, so a Cyrillic name has the same room as an
  English one; a copy from Open Food Facts obeys the API's own limits; two
  parallel copies of one barcode no longer answer 500; `%` and `_` in the
  product search are text; a moved entry goes to the end of its new meal;
  the parse prompt gets the products touched last; at most three parses per
  user may be open at once (429 `too_many_parses`).

## [1.11.2] - 2026-10-06

### Fixed
- A `food_parse` job that the queue would dead-letter (the provider kept
  timing out or answering 429/5xx) no longer stays `queued`: the last attempt
  marks it `failed` with `provider_unavailable`, and the page says so instead
  of waiting out its deadline. Migration 023 adds `attempts` to
  `food_parse_jobs`.

## [1.11.1] - 2026-10-06

### Fixed
- The `food_parse` job adapts its request to the provider: a 400 that names
  `max_tokens` (OpenAI's reasoning models want `max_completion_tokens`),
  `temperature` (only the default is allowed there) or `response_format` is
  answered by renaming or dropping that parameter and trying again, one
  parameter per round. The provider's message is stored with the error code
  and shown on the page.

## [1.11.0] - 2026-10-06

### Added
- Food module, first part (off by default, `FOOD_ENABLED`): own products, a
  diary of meals per day with copied numbers, daily goals computed from the
  profile and the weight of the Mi scale (Mifflin-St Jeor, activity factor,
  deficit from the weekly pace, overridable per number), day and week views
  with the active kcal of the band, and a proxy to Open Food Facts search and
  product lookup, under `/api/v1/food/`. Every confirmed user has it.
- Food section in the dashboard, for every confirmed user: the day with its
  four meals against the goals (ring and macro bars, the balance with the
  band's active calories), an add form with own and recent products, Open
  Food Facts search, a text description parsed by the LLM job (shown only
  when the server has it configured) and a quick entry; the week with a
  chart and a table; own products with servings and archive; the goals
  profile with the computed numbers and per-target overrides. The sidebar
  item hides while the food module is off.
- Food module, second part: a text description of a meal is parsed by an LLM
  into diary lines. The worker job `food_parse` speaks the OpenAI-compatible
  chat completions API to whatever `FOOD_LLM_BASE_URL`, `FOOD_LLM_API_KEY`,
  `FOOD_LLM_MODEL` and `FOOD_LLM_PROMPT` name; the answer is checked against a
  schema (an item id must be the user's own) and waits in the job for the
  user's confirmation. `POST /api/v1/food/parse`, `GET /api/v1/food/parse/{id}`;
  `GET /api/v1/food/goals` reports `llm_available`.

## [1.10.0] - 2026-10-06

### Added
- Mi account linking per user (backend). A signed-in user links a Xiaomi
  account by QR sign-in through `/api/v1/fitness/account/*`: start an attempt,
  confirm it in a Xiaomi app, and the service verifies the token, finds the
  account's cloud region and stores the link sealed. The link has a status:
  `ok`, or `reauth_required` once Xiaomi refuses the stored token. Credentials
  moved from the single-row `xiaomi_credentials` to `mi_accounts` (one row per
  user; migration 020 gives the existing account to the oldest administrator).
- Profile page: a "Mi Fitness" block to link a Xiaomi account by QR sign-in
  (scan the code in a Xiaomi app, or open the confirmation link on the phone),
  see whether the saved sign-in still works, check it, pick or detect the cloud
  region, and unlink with or without the synced data. Health and Workout show
  "Link your Mi account" to a user without a link instead of empty charts, and
  Health shows a notice when Xiaomi asks to sign in again.

### Changed
- Fitness and Workout data are per user. Every fitness route works on the
  caller's own Mi account: reads return only the rows of that account (a user
  without a link gets empty lists), a sync run belongs to an account and is
  visible only to it, and the schedule enqueues one sync per linked account
  whose token Xiaomi still accepts. Workout readiness, the body-weight snapshot
  and the match with band data read the session owner's account. The default
  role `User` gets `fitness:read` and `fitness:sync` (migration 021), so every
  confirmed user has Health and Workout for their own data. An API key reads
  the data of its own user: a key issued under another user no longer sees the
  owner's data.

### Removed
- The Xiaomi credential seed from the environment: `MI_FITNESS_USER_ID`,
  `MI_FITNESS_PASS_TOKEN`, `MI_FITNESS_REGION` and `MI_FITNESS_RESEED` are no
  longer read. An account is linked from the profile; the region is detected
  per account. `GET /api/v1/fitness/probe` without a linked account answers 409
  `not_linked` instead of 503 `not_configured`.

## [1.9.0] - 2026-10-05

### Added
- Workout module, first part (off by default, `WORKOUT_ENABLED`): an exercise
  library of 876 exercises seeded from the public-domain free-exercise-db,
  custom exercises, and routines with per-exercise targets, under
  `/api/v1/workout/exercises` and `/api/v1/workout/routines`. Access needs
  `fitness:read`.
- Workout module, second part: logged sessions under
  `/api/v1/workout/sessions` (start empty or from a routine, sets with
  client-generated ids so a resend replaces itself, history with volume),
  `/api/v1/workout/readiness`, and matching of finished sessions with Mi Fitness
  heart-rate samples and band workouts. The match runs when a session is
  finished or moved, after every fitness sync, and on
  `POST /api/v1/workout/reconcile` (`fitness:sync`).
- Workout pages in the dashboard, first part: `/workout/exercises` (search and
  filters over the exercise library, photos, own exercises) and
  `/workout/routines` with an editor (weekday, exercises from the library,
  target sets, rep range or duration, rest). The frontend image now carries the
  exercise photos of free-exercise-db under `/exercise-media/` (about 100 MB).
- Workout section in the dashboard sidebar (`/workout`, needs `fitness:read`;
  the item stays inactive while the module is off): start a workout from a
  routine or empty, the active session screen for a phone (sets pre-filled from
  the previous session, rest timer with a sound, add or remove exercises), sets
  that are kept on the device and resent when the connection drops, history
  with volume, and per workout a Health block with heart rate from the band, a
  band workout's calories and a "Sync now" button while the data is awaited.

### Changed
- Prod: the API release no longer has an ingress. The frontend release is the
  only public entry point of `life-os.tarassov.me` and proxies `/api/` to the API
  Service; `/`, `/ready`, `/health` and `/metrics` of the API are reachable only
  inside the cluster.

## [1.8.0] - 2026-10-05

### Added
- Health section of the dashboard (`/health`): tiles (steps, sleep, resting
  heart rate, weight), per-day charts of activity, sleep stages and score, heart
  rate, stress and body composition, tables of SpO2 and workouts, all over the
  existing `/api/v1/fitness/*` routes for a period of 7, 30 or 90 days or a
  custom range kept in the URL. Users with `fitness:sync` also get the coverage
  table, a form that starts a sync run and follows it to completion, and a cloud
  connection check. Charts use ApexCharts.
- Dashboard shell in the frontend, adapted from TailAdmin React: `/` is a
  full-screen sign-in form for a guest and a redirect to `/health` for a signed-in
  user, `/login` redirects to `/`, and `/health` opens inside a sidebar layout
  that lists the Life OS sections (Health is active, the rest are marked "soon").
  The Health page is a placeholder until its charts ship. Admin and account pages
  keep their layout. `deploy/values-frontend-prod.yaml` holds the prod values of
  the frontend chart.

### Changed
- Frontend toolchain upgraded: React 19, Tailwind 4 (Vite plugin, theme in
  `src/index.css`, no `tailwind.config.js` or PostCSS config), Vite 8, vitest 5,
  `react-router` 8 in place of `react-router-dom` 6. The frontend image and the
  CI jobs that install frontend dependencies run on Node 22. No page changes.

## [1.7.2] - 2026-10-04

### Changed
- The ADRs in `docs/adr/` are replaced by one `docs/ARCHITECTURE.md`. Template
  and flask-base wording is removed from docs, comments and names: `GET /` now
  returns `"message": "life-os-cpp"` (was `"C++ API Template"`), and the OpenAPI
  title is `life-os-cpp API`. Migrations 000 and 001 changed in comments only;
  a database that already applied them needs
  `UPDATE schema_migrations SET checksum = NULL WHERE version IN (0, 1)` before
  it boots this version with migrations enabled.

### Fixed
- The API chart did not mount a writable `/app/uploads`, so with the read-only
  root filesystem Drogon logged 256 "Error 30 creating path ./uploads/tmp/NN/"
  lines on every start and had nowhere to spool request bodies. The API
  Deployment now mounts an `emptyDir` there, as the worker chart already did.

## [1.7.1] - 2026-10-04

### Fixed
- Fitness sleep sync stored one night as several rows: the Xiaomi cloud keeps
  each partial upload of a night as its own record, and `sleep_id` was derived
  from the record time. `sleep_id` is now derived from the bedtime, snapshots
  of one night collapse to the one that ends last, and a shorter snapshot no
  longer overwrites a longer stored night. Migration 016 removes the existing
  duplicates and re-keys `sleep_sessions.sleep_id`.

## [1.7.0] - 2026-10-01

### Added
- `fitness` module: Mi Fitness sync ported from mi-fitness-api 1.9.2
  (Xiaomi cloud client, sync, eight health data types, schema 1.0 / CSV
  export) under `/api/v1/fitness/*`, with the `kFitnessRead` / `kFitnessSync`
  permission bits and the Fitness Reader / Fitness Operator roles;
  `FITNESS_ENABLED` master switch.

### Changed
- Repository history reset; template scaffolding, demo overlays and planning
  documents removed.

[Unreleased]: https://github.com/moveeeax/life-os-cpp/compare/v1.7.0...master
[1.7.0]: https://github.com/moveeeax/life-os-cpp/releases/tag/v1.7.0
