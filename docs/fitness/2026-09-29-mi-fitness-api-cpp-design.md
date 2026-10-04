# Design: mi-fitness-api, porting the Mi Fitness bridge to C++

Date: 2026-09-29. Status: pending approval.

## Why

Today Mi Fitness data is pulled into the cluster by [mi_fitness_data_bridge](https://github.com/shkyyy18/mi_fitness_data_bridge) in Python: SQLite on a PVC plus an MCP server behind an HTTP proxy. The owner's task: replace it with an own C++ service on top of `cpp-rapid-rest-template`, with a REST API instead of MCP, data in Postgres and the token in a Secret.

Success looks like this: the service logs into the Xiaomi cloud on its own, pulls eight record types, stores them in Postgres and serves them over REST with the same numbers the current Python bridge returns for the same dates. After that the Python bridge, its PVC and `mcp-proxy` are removed from the cluster.

## Decisions made before the design

1. A full port, including the Xiaomi adapter. The service never depends on Python at any point.
2. One Xiaomi account, one API consumer. No registration, no roles, no mail, no billing. The base template was stripped to its minimal variant at the time (that script no longer exists in this repo).
3. License AGPL-3.0-only. The port is done while reading AGPL code; a translation to another language is a derivative work. The template's MIT can be included in an AGPL project, not the other way round.
4. Topology: one image, two deployments. REST in the main binary, sync in the worker via `Jobs` (the template's Redis queue with retries and DLQ). The schedule is set by a `Tasks::` timer in the API process.
5. No PVC at all. State lives in Postgres, secrets in a Secret, the queue in Redis.
6. The first stage covers all eight data types at once, not a single vertical slice.

## The constraint that shapes half of the design

`passToken` rotates on every login: Xiaomi returns a new one in the `serviceLogin` response, and the old one stops working after a while. Observation from 2026-09-29: the previous token stayed valid for at least tens of minutes after rotation, so invalidation is not instant, but this cannot be relied on.

Hence a two-tier storage scheme:

1. The Secret holds `MI_FITNESS_USER_ID`, the initial `MI_FITNESS_PASS_TOKEN` and the encryption key `MI_FITNESS_TOKEN_KEY` (32 bytes, base64).
2. The `xiaomi_credentials` table holds the current live token, encrypted with `crypto_secretbox_easy` from libsodium. The key never reaches the database.
3. On start the service takes the token from the database. If the row is missing, it seeds it from the Secret. If `MI_FITNESS_RESEED=1` is set, it overwrites the database with the value from the Secret.

A bonus: libsodium is already wired into the template and used in `src/security/Password.hpp`, and the pod needs no write access to cluster secrets.

## Architecture

Module boundaries chosen so that each one reads and tests separately.

```
src/xiaomi/
  Crypto.hpp/.cpp        RC4, nonce, signed_nonce, signature. Pure functions, no network.
  CloudClient.hpp/.cpp   login, token rotation, signed POST, pagination. Knows HTTP, knows nothing about the domain.
  Normalize.hpp/.cpp     raw JSON -> domain structures. Pure functions, no network and no database.
  Regions.hpp            region -> host table, candidate list.
src/domain/
  Activity.hpp Sleep.hpp Workout.hpp Body.hpp Samples.hpp
src/repositories/
  ActivityRepository ... one per type, plus SyncStateRepository, CredentialsRepository
src/sync/
  SyncService.hpp/.cpp   range chunking, type order, idempotent writes, progress tracking
src/jobs/
  XiaomiSyncHandler      job handler in the worker, on top of the template's Jobs
src/api/
  ActivityController SleepController WorkoutController BodyController SamplesController SyncController
```

Dependency rule: `Crypto` knows nothing, `CloudClient` depends only on `Crypto` and `Regions`, `Normalize` depends only on the domain, `SyncService` glues `CloudClient`, `Normalize` and the repositories together, controllers depend only on repositories. A controller never calls Xiaomi directly.

## The protocol that must be ported exactly

Login, two steps:

1. `GET https://account.xiaomi.com/pass/serviceLogin?_json=true&sid=miothealth` with the header `Cookie: userId=<id>; passToken=<token>`. The response starts with the prefix `&&&START&&&`, then JSON. Required fields: `passToken`, `userId`, `ssecurity`, `location`.
2. `GET <location>`, and the response must carry the `serviceToken` cookie. A redirect is accepted only to the hosts `xiaomi.com` and `mi.com` and their subdomains.

The new `passToken` from the response is saved before the second request goes out: otherwise a network error on the redirect would lose an already issued token.

Signed data request:

1. `form = {"data": <compact JSON payload>}`.
2. `nonce` = 8 random bytes plus 4 bytes big-endian with the current minute number since the epoch.
3. `signed_nonce` = `SHA256(ssecurity_bytes || nonce)`.
4. `rc4_hash__` = signature of the unmodified form.
5. Every form value is RC4-encrypted and base64-encoded, then `signature` is computed over the encrypted values.
6. Signature: `base64(SHA1("POST&" + path + "&data=" + data + ["&rc4_hash__=" + hash] + "&" + base64(signed_nonce)))`.
7. The body goes as `application/x-www-form-urlencoded`, the response arrives as base64 and is decrypted with the same RC4 key.

RC4 here is non-standard: after the key schedule initialization, 1024 bytes of the stream are discarded. This is a mandatory detail; without it decryption yields garbage.

Endpoints and regions:

| What | Path | Note |
| --- | --- | --- |
| Records by time | `/app/v1/data/get_fitness_data_by_time` | the main one, pagination via `next_key` and `has_more` |
| Daily reports | `/app/v1/data/get_aggregated_fitness_data_by_time` | only for the sleep score |
| Workouts | `/app/v1/data/get_sport_records_by_time` | separate record format |

Host: `https://hlth.io.mi.com` for region `cn` and the empty value, otherwise `https://<region>.hlth.io.mi.com`. Region candidates: `ru, cn, de, i2, sg, us`. Region auto-detection iterates the candidates over the keys `weight`, `steps`, `heart_rate` and takes the first one that responds non-empty.

Data type keys in the request: `steps`, `calories`, `sleep`, `weight`, `heart_rate`, `resting_heart_rate`, `spo2`, `stress`, `abnormal_heart_beat`.

Range bounds are computed in the region's time zone: for `cn` it is hard-coded UTC+8, for the rest UTC. Pagination is guarded by a page ceiling and a set of already seen cursors, so that a cursor loop does not turn into an infinite loop.

## Normalization rules that cannot be simplified

This is not style, this is data correctness. Each rule gets its own test.

1. Steps are aggregated by the pair (local date, local minute). With several records in one minute from different devices, the single record with the largest tuple (steps, distance, calories) is taken, not the sum. Summing overstates on days with two devices. The suppressed step count is counted and logged.
2. A zero from the server means "no data", not a measured zero. Such values go to the database as NULL.
3. The sleep score is taken from the record itself. If it is absent, pulling the score from the daily aggregate is allowed, but only onto one unambiguously identified main session. The main session is the one with `is_nap = false`, duration greater than zero and at most 1440 minutes. With several candidates, selection goes by a single source and maximum duration, and if there is no unambiguous choice, no score is set at all. No local score computation.
4. The score stores its provenance: `sleep_record` or `daily_report`. Unknown provenance is NULL.
5. A failure of the optional daily reports request does not discard the sleep sessions themselves and does not erase a previously known score with the same bounds and the same nap flag.
6. Resting heart rate goes into the same table but with a different `sample_type`, so the uniqueness key includes it.
7. Re-syncing the same range is idempotent: records are not duplicated.

## Postgres schema

The eight data tables repeat the SQLite composition, but with Postgres types and natural keys instead of a string `id`. Common part for all: `provider text`, `source_type text`, `source_record_id text`, `user_id text`, `device_id text`, `timezone text`, `collected_at timestamptz`, `created_at timestamptz default now()`, `updated_at timestamptz`.

| Table | Specific columns | Uniqueness key |
| --- | --- | --- |
| `daily_activity` | `date date`, `steps int`, `distance_m double precision`, `active_kcal double precision`, `total_kcal double precision`, `floors int`, `active_minutes int` | `(user_id, date, device_id)` |
| `sleep_sessions` | `sleep_id text`, `start_at timestamptz`, `end_at timestamptz`, `duration_minutes int`, `time_asleep_minutes int`, `time_awake_minutes int`, `sleep_score int`, `sleep_score_source text`, `is_nap boolean`, `stages jsonb` | `(user_id, sleep_id)` |
| `workouts` | `workout_id text`, `activity_type text`, `start_at`, `end_at`, `duration_minutes int`, `distance_m`, `calories_kcal`, `avg_heart_rate_bpm int`, `max_heart_rate_bpm int`, `avg_pace_sec_per_km`, `max_pace_sec_per_km`, `total_steps int` | `(user_id, workout_id)` |
| `body_measurements` | `timestamp timestamptz`, `weight_kg`, `bmi`, `body_fat_pct`, `muscle_mass_kg`, `water_pct`, `bone_mass_kg`, `visceral_fat_score int`, `basal_metabolism_kcal int`, `metabolic_age int` | `(user_id, timestamp, device_id)` |
| `heart_rate_samples` | `timestamp timestamptz`, `bpm int`, `sample_type text` | `(user_id, timestamp, sample_type)` |
| `spo2_samples` | `timestamp timestamptz`, `spo2_pct int` | `(user_id, timestamp)` |
| `stress_samples` | `timestamp timestamptz`, `stress_score int`, `level text` | `(user_id, timestamp)` |
| `abnormal_heart_beat_events` | `event_id text`, `start_at`, `end_at`, `duration_seconds int` | `(user_id, event_id)` |

Three service tables:

1. `xiaomi_credentials`: `user_id text primary key`, `pass_token_sealed bytea`, `nonce bytea`, `region text`, `rotated_at timestamptz`.
2. `sync_state`: `data_type text primary key`, `last_sync_at timestamptz`, `last_record_timestamp timestamptz`, `records_count bigint`.
3. `sync_runs`: `id bigserial`, `started_at`, `finished_at`, `status text` (`running`, `succeeded`, `failed`, `interrupted`), `requested_start date`, `requested_end date`, `data_types text[]`, `result jsonb` with the count of added, updated and skipped records per type.

Writes go through `INSERT ... ON CONFLICT (key) DO UPDATE`, which gives idempotency instead of a manual existence check. Migrations are created with the template generator `make new-migration`.

## REST API

Prefix `/api/v1`, authorization by the template's API key, responses in the template envelope, every route must be present at the same time in the controller, in `Api::get_endpoints()` and in `docs/openapi.yaml`, otherwise CI fails.

| Method and path | Parameters | Returns |
| --- | --- | --- |
| `GET /api/v1/activity` | `from`, `to`, `limit`, `offset` | daily activity |
| `GET /api/v1/sleep` | `from`, `to`, `include_naps`, pagination | sleep sessions with stages |
| `GET /api/v1/sleep/summary` | `from`, `to` | averages by local wake-up date, share of sessions with a score |
| `GET /api/v1/workouts` | `from`, `to`, `type`, pagination | workouts |
| `GET /api/v1/body` | `from`, `to`, `metrics`, pagination | weight and body composition |
| `GET /api/v1/samples/{kind}` | `kind` ∈ `heart_rate, spo2, stress, abnormal_heart_beat`, `from`, `to`, pagination | point samples |
| `GET /api/v1/coverage` | none | per type: first and last date, number of days, time of the last sync |
| `POST /api/v1/sync` | body with `from`, `to`, `data_types` | enqueues a job, returns `run_id` |
| `GET /api/v1/sync/{run_id}` | none | run status and per-type counters |
| `GET /api/v1/export` | `format` ∈ `json, csv`, filters as for the others | export, semantically compatible with the Python bridge's `schema_version 1.0` |

Input dates are strictly `YYYY-MM-DD`, `from` not later than `to`, filters include the bounds. Pagination as in the template: `limit`, `offset`, a next-page flag.

Numeric fields absent in the data are returned as `null`, not as zero. This continues normalization rule 2 and is part of the API contract.

## Synchronization

1. `POST /api/v1/sync` creates a row in `sync_runs` and enqueues a job of type `xiaomi_sync` in the `Jobs` queue. Responds immediately, body with `run_id`.
2. A `Tasks::` timer in the API process enqueues the same job once a day with a window of the last `MI_FITNESS_LOOKBACK_DAYS` days, four by default. The window is wider than one day because Xiaomi backfills history retroactively: on 28 September 2026 the same day first showed 100 steps, after a re-sync 2303.
3. The worker takes the job and walks the types sequentially, chunking the range of each type by `MI_FITNESS_CHUNK_DAYS` days, seven by default.
4. Each type has its own time ceiling, `MI_FITNESS_SYNC_TYPE_TIMEOUT`, 180 seconds by default. For a four-month range that is not enough, so for history backfill the value is raised via the variable. The ceiling is per type, not per whole run.
5. One sync runs at a time. The guarantee comes from the Postgres advisory lock `pg_try_advisory_lock`, not only from the queue setting. If the lock is not acquired, the job finishes with status `skipped`.
6. A failure of one type does not cancel the others. The result of each type is written to `sync_runs.result`, and a failed job goes to the template's DLQ.
7. Runs left unfinished at restart are marked `interrupted` and are not picked up automatically.

## Errors

1. A Xiaomi authorization error differs from a network one: the former is not retried by the queue, but writes status `failed` with an `auth` flag to `sync_runs` and raises a metric. A retry here would only burn attempts.
2. Server error texts, token values and account identifiers never reach the logs or the API responses. Masking as in the Python bridge: six asterisks and the last two characters.
3. An empty successful response is not an error. Zero records means there are no records of that type in the range.
4. A decryption or signature error is treated as a protocol incompatibility: a separate metric, because it is the first sign that Xiaomi changed the format.

## Tests

The key problem of the port: the Python bridge has 297 tests, the new code has none, and checking numbers on a live account is slow and non-reproducible. Therefore:

1. Golden vectors for the crypto. A one-off Python script captures input-output pairs for RC4 with the 1024-byte discard, `signed_nonce` and the signature on fixed inputs, and stores them in `tests/fixtures/xiaomi_crypto_vectors.json`. The C++ test runs the same ones. This catches all algorithm porting errors without network and without an account.
2. Normalization is checked on synthetic responses modeled on the format of the Python fixtures. Mandatory cases: two records in one minute from different devices, zero as missing data, sleep across midnight, a nap next to the main session, ambiguous score candidates, re-sync of the same range.
3. The client is checked against a local mock server: the `&&&START&&&` prefix, missing required fields, a redirect to a foreign host, missing `serviceToken`, a cursor loop, token rotation mid-session.
4. Repositories and the API are checked by the template's integration tests on a live Postgres, the e2e bucket validates responses against the `docs/openapi.yaml` schemas.
5. Acceptance on real data: for the same week, the export from the Python bridge and from the new API must match on steps, distance, weight and sleep session bounds. This is the only check that catches protocol misunderstanding errors, and it is mandatory before the Python bridge is removed.

## What this project does not have

1. MCP in any form.
2. Multi-user mode, registration, roles, mail, billing, the content module, the template's React frontend.
3. Kafka and mail sending: the template modules stay in the code but are disabled by the settings `MESSAGING_ENABLED`, `KAFKA_PRODUCER_ENABLED`, `KAFKA_CONSUMER_ENABLED`, `MAIL_ENABLED`. Cutting their code out is not worth the effort.
4. Medical conclusions, condition assessments, recommendations. The service returns numbers.
5. Storing other people's accounts and any public access to the data.

## Migration from the Python bridge

1. The new service is deployed into a separate namespace and syncs the same account. During the transition both log in, and this is exactly the case where token rotation can interfere. So the Python bridge is paused for the transition: CronJob `suspend: true`, the MCP deployment scaled to zero replicas.
2. History is loaded into Postgres by the new service's sync with a raised per-type time ceiling, not by transferring the SQLite. This verifies the whole path end to end.
3. A one-week comparison per item 5 of the tests section.
4. After the comparison, the `mi-fitness` namespace, the PVC and the `mi-fitness-bridge` image are removed.

## Risks

1. Xiaomi changes its private endpoints without notice. Mitigation: separate metrics for signature and decryption errors, an honest status in `sync_runs`.
2. The cloud has no data earlier than mid-July 2026, verified by a request from 1 June. History depth is limited by the server, not by the client.
3. Two-factor account confirmation will break the login: the response will lack the required fields. Handling: a clear `auth` error, no attempt to automate the confirmation.
4. Porting the normalization rules by eye would yield silently wrong health data. Mitigation: golden vectors, synthetic fixtures and the mandatory comparison with the Python bridge before it is removed.
