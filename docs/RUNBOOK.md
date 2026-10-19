# Operator runbook

What to do when an alert fires or an incident lands. Each alert in
`docker/prometheus-rules.yml` / the Helm `PrometheusRule` carries a
`runbook_url` anchor that points here.

Conventions: `make` targets assume the compose stack; in k8s substitute
`kubectl exec`/`logs`. Trace IDs are in every access-log line (`tid=`) and the
`X-Request-Id` response header — `make tail-trace TID=<id>` follows one request.

---

## ApiTargetDown / WorkerTargetDown {#targetdown}

Prometheus hasn't scraped the process for 2 min (API; 5 min for the worker) —
it's down or wedged.

1. `kubectl get pods` / `make ps` — is it crash-looping or gone?
2. Logs: `kubectl logs <pod> --previous` / `make logs`. Look for the init
   order line that's missing (Config→Observability→Database→…→Mailer).
3. Common causes: DB/Redis unreachable at boot (Database throws → Core aborts;
   replica being down does NOT abort, that's handled), bad `JWT_SECRET` under
   `AUTH_MODE=jwt`, port already bound.
4. `/healthz` (alive) vs `/ready` (dependencies). 503 on `/ready` with 200 on
   `/healthz` = draining or a dependency is unhealthy → check `/health`.

## High5xxRate {#high5xx}

>5% of responses are 5xx.

1. Find the failing route: Grafana `http_requests_total{status=~"5.."}` by
   `path`. The path is normalized (`/api/v1/jobs/:id`), tokens redacted.
2. Pull a failing trace (`tid=` in the log). `make tail-trace TID=<id>` greps
   the app log for that trace id, so it shows the access-log line and what
   the handler logged, not the spans. The `db.*` child spans with the SQL
   template (`db.statement`) and the pool label are on the OTLP trace: open
   the same id in Jaeger (`make up-monitoring`, `:16686`) or whatever
   receives `OTLP_ENDPOINT`.
3. DB-driven? See [HighP99Latency](#p99). Dependency down? See
   [RetriesExhaustedSpike](#retries).

## HighP99Latency {#p99}

p99 > 1s for 10 min. Almost always DB pool saturation or slow queries.

1. `db_queries_total{pool=...}` — are reads hitting the replica or all on
   primary? (replica down → fallback to primary, see its alert).
2. Pool saturation: effective concurrency is `min(pool_size, server.threads)`
   — `pool_size > threads` does nothing. Check `SERVER_THREADS` vs
   `DB_POOL_SIZE` (docs/CONFIG.md).
3. Slow query: `statement_timeout` (default 30s) caps it; find it via the
   trace's `db.statement`. Add an index or paginate.

## ReplicationLagHigh {#replag}

Read replica >60s behind. Stale reads are being served.

1. On the replica: `SELECT now() - pg_last_xact_replay_timestamp();`
2. Check replica I/O / WAL volume pressure and the network to the primary.
3. Mitigation: the app already fails reads over to the primary if a replica
   errors, but lag (not error) still serves stale data. To force primary-only
   temporarily, set `DATABASE_REPLICA_URLS=""` and restart.

## DeadLetterQueueGrowing {#dlq}

Jobs exhausted their retries and landed in the DLQ.

1. Inspect: admin UI **/admin/jobs → DLQ tab**, or
   `GET /api/v1/jobs/dlq` (admin token). Each row has the last `error` and a
   `trace_id` → open the worker trace.
2. Transient cause now fixed (e.g. SMTP was down)? Requeue:
   `POST /api/v1/jobs/dlq/{id}/requeue` or the UI button.
3. Code bug? Fix the handler, redeploy the worker, then requeue.
4. Account emails specifically: check `WORKER_TYPES` includes `account_email`
   and the worker has `JWT_SECRET` + `MAIL_*` (a missing secret DLQ's every
   email with `master_secret must be set`).
5. The LLM job rows (`food_parse`, `money_parse`, `money_advisor`,
   `tasks_parse`) close their Postgres row as `failed` with `queue_gave_up`
   when the job lands in the DLQ, so the page that polls it stops waiting. A
   requeue from the DLQ claims the row again. What each type carries and
   what a redelivery does to it: [Job types](#job-types).

## RetriesExhaustedSpike {#retries}

A downstream (DB/Redis) is failing past the retry budget.

1. Which? `retries_total{component=...,outcome="exhausted"}`.
2. Redis: cache ops are fail-open (degraded, not down) EXCEPT session-mint
   (login fails closed if it can't record the refresh JTI) — so a Redis
   outage shows as login failures + rate-limit fail-closed (if configured).
3. DB: see [HighP99Latency](#p99) / [ApiTargetDown](#targetdown).

## JobsQueueBacklog {#queuebacklog}

The waiting queue (`jobs_queue_depth{type="_total"}`) is deep and not draining —
submitters are outrunning the worker pool. This is the *leading* signal; left
alone, jobs eventually age into the DLQ ([DeadLetterQueueGrowing](#dlq)).

1. Which type? `jobs_queue_depth` is labeled by `type`.
2. Workers alive and consuming? Check `up{job="life_os_cpp_worker"}` and the worker
   logs. A stuck handler (one slow job type blocking its thread) starves the
   rest — BRPOP concurrency equals the thread count.
3. Genuine load spike? Scale worker replicas / `WORKER_CONCURRENCY`.
4. A job whose pod was killed mid-run is not lost and does not wait for a
   worker of the same name: in production the lease
   (`JOBS_VISIBILITY_TIMEOUT_SEC=300`, `deploy/values-worker-prod.yaml`)
   returns it to the queue after 300 s, and `WORKER_ID` is the pod name, so
   a replaced pod never inherits a processing list. The lease sits above
   the longest handler (fitness sync 180 s per data type, the money LLM
   calls 90 s); raise `jobs.visibilityTimeoutSec` together with those
   timeouts, or the reaper fails a job that is still running.

## DbPoolSaturationHigh {#dbpool}

A connection pool is over 90% utilized (`db_pool_active_connections /
db_pool_size`). At saturation, `acquire()` blocks up to the acquire timeout and
then throws — surfacing as 5xx and high p99.

1. Which pool? The alert is labeled `pool` (primary/replica).
2. Slow queries holding connections? Check the DB's `pg_stat_activity` and the
   `db.statement` attribute on slow `db.*` spans.
3. Real concurrency demand? Raise `DB_POOL_SIZE` (and the server's
   `max_connections`), or shed load. A leaked connection (active stuck high
   while traffic is idle) points at a handler that never returns its txn.

## Job types {#job-types}

The nine types a worker registers (`src/core/JobHandlers.cpp` calls each
module's `register_jobs()`), what
puts them on the queue and what a second delivery of the same job does.
Every handler runs at least once: the queue retries a thrown exception with
backoff up to `JOBS_MAX_RETRIES` (3) and then dead-letters it, the lease
redelivers a job whose worker died, and the outbox relay is at-least-once.
"Duplicate" below is what happens on that second delivery. The compose
stack subscribes to all nine; production (`deploy/values-worker-prod.yaml`)
to the six without mail and webhooks.

| Type | Producer | Payload | Retry and duplicate |
|---|---|---|---|
| `account_email` | `Email::AccountEmails::send_*` in the API (confirm, reset, change-email, invite) when `MAIL_VIA_JOBS=true` and Jobs is on; inline send otherwise | `{kind, user_id, new_email?}` | The worker issues the token at send time (`src/email/AccountEmailWorker.hpp`), throws on a render or SMTP failure, retries, then DLQ. A duplicate sends the mail again with a fresh token. Needs `JWT_SECRET` and `MAIL_*` on the worker |
| `email.send` | `Email::SendEmail::send` in the API (billing receipts, `src/email/BillingEmails.hpp`); inline send when Jobs is off | `{to, subject, text, html?}` | Throws on SMTP refusal, retries, then DLQ. A duplicate is a second copy of the mail |
| `webhook.deliver` | Nothing yet: `src/webhooks/Webhooks.hpp` is the delivery seam, no subscriptions table submits it | `{url, event, body, secret?}` | Signed POST (`X-Webhook-Signature: sha256=` HMAC of the body when `secret` is set); a non-2xx throws, retries, then DLQ. The receiver has to dedupe by `event` and body |
| `fitness_sync` | `POST /api/v1/fitness/sync`, and the `fitness_sync_schedule` timer in the API pod every `MI_FITNESS_SYNC_SCHEDULE_HOURS` for every linked account | `{run_id, owner_id, from, to, data_types}` | The run row in `sync_runs` is created before the submit. No link, module off or no `MI_FITNESS_TOKEN_KEY` finish the run as `failed` without a retry; a cloud or database error propagates and the queue retries. A duplicate runs the same range again (upserts per day). 180 s per data type (`MI_FITNESS_SYNC_TYPE_TIMEOUT`) |
| `food_parse` | `POST /api/v1/food/parse` | `{job_id, owner_id, max_attempts}` | Row in `food_parse_jobs`. Module off, no `FOOD_LLM_*`, a provider refusal (401/403) or an answer outside the schema close the row as `failed` with no retry; 429, 5xx and timeouts throw and the queue retries up to the row's attempts. A redelivery of a row that is `running` is deferred until the row turns stale (`JobRows::kStaleRunning`, 10 min); a `done` or `failed` row is left alone. DLQ closes the row with `queue_gave_up` |
| `money_parse` | `POST /api/v1/money/parse`, `POST /api/v1/money/parse/receipt` | `{job_id, owner_id, max_attempts}` | As `food_parse`, row in `money_parse_jobs`. The request carries the user's accounts, categories and remembered merchants |
| `money_rates` | The `money_rates_schedule` timer every `MONEY_RATES_SCHEDULE_HOURS`, `POST /api/v1/money/rates/refresh` (today, or one job per missing day of a backfill), and `POST /api/v1/jobs` by an admin (one of the four hand-submittable types) | `{date?}`: `latest` or `YYYY-MM-DD` | Idempotent: a day already stored is written again with the same numbers. A source outage throws and the queue retries; a day the source never published finishes as `no_snapshot` |
| `money_advisor` | `POST /api/v1/money/advisor/run`, and the `money_advisor_schedule` timer at `MONEY_ADVISOR_HOUR_UTC` on each user's weekday | `{report_id, owner_id, max_attempts}` | As `money_parse`, row in `money_advisor_reports`. A period without posted rows gets a one-line review and no provider call. A submit the queue refuses marks the row `queue_unavailable` |
| `tasks_parse` | `POST /api/v1/tasks/parse` | `{job_id, owner_id, max_attempts}` | As `money_parse`, row in `task_parse_jobs`; falls back to the money provider when `TASKS_LLM_*` is not set |
| `now_media_sweep` | The `now_media_sweep_schedule` timer in the API pod, hourly while `NOW_ENABLED` is on | `{}` | Deletes wallpaper and sound rows still `pending` an hour after their create (an upload the browser never finished, or a delete whose store call failed), objects first. A row the store would not clear stays for the next tick. Skipped without `S3_*` on the worker |
| `files_sweep` | The `files_sweep_schedule` timer in the API pod, hourly while `MEDICAL_ENABLED` or `TRAVEL_ENABLED` is on | `{}` | Deletes `private_files` rows still `pending` an hour after their create, then removes the object of every `private_file_purges` entry older than an hour (a file, record, document or account deleted) and forgets the entry. An object the store would not remove keeps its entry for the next tick. Skipped without `S3_*` on the worker. See [Private files](#private-files) |

The four LLM rows are written through the transactional outbox when
`OUTBOX_DRAIN_INTERVAL_SEC` is above zero (production: 5 s on the API
chart): the outbox row rides the same transaction as the job row, and the
API pod relays it to Redis within one drain interval of the commit. With
the drain off (compose, the test suites) the controller submits directly
after the commit. A `queued` row nobody picked for ten minutes reads as
`failed` with `queue_lost`; a late delivery still claims and runs it.

The three demo types `echo`, `slow` and `fail` are registered for tests
and `POST /api/v1/jobs`; they carry no data.

---

## Private files {#private-files}

The scans of medical records and travel documents live in the same bucket as
the wallpapers, under `<S3_PREFIX>private/<owner id>/<file id>/file.<ext>`
(in prod: bucket `life-os`, keys `life-os/private/...`). They are never read
through `S3_PUBLIC_BASE_URL`: the browser PUTs a file through a presigned URL
valid 15 minutes, and every read is a presigned GET valid 120 seconds that
the API signs for the owner of the row on each request
(`GET /api/v1/files/{id}/download`). Another user gets 404 for the row and
for the link.

The MinIO user `life-os` has put, get and delete on `life-os/wallpapers/*`
and `life-os/sounds/*` only (docs/wallpapers.md, "Prod"). Before the first
upload the owner extends that policy to the private prefix, and leaves the
anonymous rules as they are:

1. Look at the policy as it is now:
   `mc admin policy info <alias> life-os-rw`.
2. Add `arn:aws:s3:::life-os/life-os/private/*` to the resources of the
   statement with `s3:PutObject`, `s3:GetObject` and `s3:DeleteObject`. With
   the rights docs/wallpapers.md describes, the whole policy then reads:

   ```json
   {
     "Version": "2012-10-17",
     "Statement": [
       {
         "Effect": "Allow",
         "Action": ["s3:PutObject", "s3:GetObject", "s3:DeleteObject"],
         "Resource": [
           "arn:aws:s3:::life-os/life-os/wallpapers/*",
           "arn:aws:s3:::life-os/life-os/sounds/*",
           "arn:aws:s3:::life-os/life-os/private/*"
         ]
       },
       {
         "Effect": "Allow",
         "Action": ["s3:ListBucket"],
         "Resource": ["arn:aws:s3:::life-os"]
       }
     ]
   }
   ```

   If step 1 shows more than this, keep the rest and add only the one
   resource line.
3. Load it under the same name: `mc admin policy create <alias> life-os-rw life-os-rw.json`
   (older `mc` releases call the command `mc admin policy add`), then run
   step 1 again and check the new line is there.
4. Do NOT add an anonymous rule for the private prefix. `mc anonymous list <alias>/life-os`
   must show `life-os/wallpapers/*` and `life-os/sounds/*` and nothing else,
   in particular no rule on the whole bucket.
5. Check from outside: `curl -sI https://s3.tarassov.me/life-os/life-os/private/probe`
   answers 403 (no anonymous read). A 404 means anonymous reads are allowed
   there and the bucket rules need fixing before anyone uploads.

The app checks the same thing on every upload: `POST /api/v1/files/{id}/ready`
sends an unsigned HEAD to the object's public URL, and if it answers 2xx the
file is deleted and the call answers 503 `storage_public`. The other answers
when storage is not ready:

| Answer | Cause | Fix |
|---|---|---|
| 503 `storage_not_configured` | `S3_*` incomplete on the API | The `storage.s3` values (docs/wallpapers.md) |
| 503 `storage_unavailable` | The store refused or did not answer the HEAD (a policy without the private prefix answers 403) | Steps 1 to 3 above; the API log line `files.ready: the store refused` carries the HTTP status |
| Upload fails in the browser, "the storage refused the file" | MinIO refused the presigned PUT (403 without the private prefix) | Steps 1 to 3 above |
| 503 `storage_public` | The private prefix is readable without a key | Step 4 above; the file was deleted, upload it again after the fix |

Rows, records, visits and medications work without storage; only the files
answer 503. Deleting a file, a record, a document or an account removes the
rows at once and leaves the object keys in `private_file_purges`; the API
removes the object of a deleted file right away, and the hourly `files_sweep`
job removes whatever is left (see the job table above).

## Backups {#backup}

Production data lives in the CloudNativePG cluster `postgresql` in namespace
`db` (PostgreSQL 18, two instances, Longhorn volumes). The cluster is shared:
it also hosts the databases of tarassov.me and tgw, so every backup and every
restore below covers all of them, not only `life_os`.

What runs (read off the cluster on 2026-10-14):

- CNPG barman backups to the bucket `s3://tarassov-me-backups/cnpg` on the
  in-cluster MinIO (`http://minio.minio.svc.cluster.local:9000`), server name
  `postgresql-oci-k3s`. WAL is archived continuously (gzip); a base backup is
  taken daily at 00:00 UTC by the ScheduledBackup `scheduled-backup-postgresql`.
  Retention is 14 days (`retentionPolicy: 14d`), so any point in time inside
  that window can be restored.
- The chart's `pg_dump` CronJob (`backup.enabled`, `backup-cronjob.yaml`) is
  not used in production; it stays off.

The backups are on-cluster. MinIO is a single pod on a Longhorn volume in the
same k3s cluster, so losing the cluster or its storage loses the database and
its backups together. This is the owner's decision of 2026-10-14: no
off-cluster copy for now. Revisit it before storing anything that cannot be
lost. The chart carries the copy job ready to switch on, below.

### Off-cluster mirror {#backup-mirror}

The API chart has a CronJob `backup-mirror`
(`helm/life-os-cpp/templates/backup-mirror-cronjob.yaml`, off by default)
that runs `rclone copy` (or `sync`, `backupMirror.mode`) from the barman
bucket `s3://tarassov-me-backups/cnpg` on the in-cluster MinIO to a bucket
elsewhere. `deploy/values-prod.yaml` carries the block with the source
filled in and the destination as placeholders. To enable it:

1. Create a bucket at another provider (anything S3-compatible) with a
   lifecycle rule that expires objects after about 30 days. Barman keeps 14
   days and `copy` never deletes at the destination, so that rule is the
   retention.
2. In namespace `life-os` create two Secrets: a copy of
   `minio-backups-creds` from namespace `db` (keys `ACCESS_KEY_ID` /
   `ACCESS_SECRET_KEY`; a CronJob cannot read another namespace's Secret)
   and one with the destination keys (`access-key-id` / `secret-access-key`).
3. Fill `backupMirror.source.credentialsSecret.name`,
   `destination.endpoint`, `destination.provider`, `destination.region`,
   `destination.bucket` and `destination.credentialsSecret.name`, set
   `backupMirror.enabled: true`, upgrade the API release. The default
   schedule is 01:30 UTC, after the 00:00 base backup has landed.
4. Check: `kubectl -n life-os get cronjob life-os-cpp-backup-mirror`, then
   `kubectl -n life-os create job --from=cronjob/life-os-cpp-backup-mirror mirror-now`,
   read its log and list the destination bucket.
5. With `monitoring.jobsAlerts` on, `BackupJobFailed` and `BackupTooOld`
   match job names `life-os-cpp-backup.*`, which includes
   `life-os-cpp-backup-mirror-*`: a failed mirror run fires
   `BackupJobFailed` too.

Once the mirror runs, the "on-cluster only" paragraph above no longer holds
and the restore drill below gets a second path.

Check that backups are real, read-only:

```sh
kubectl --context oci-k3s -n db get backups.postgresql.cnpg.io        # one "completed" per day
kubectl --context oci-k3s -n db get cluster postgresql \
  -o jsonpath='{.status.firstRecoverabilityPoint}{"  "}{.status.lastSuccessfulBackup}{"\n"}'
```

The restore below has not been drilled on this cluster yet. Do one into a
scratch cluster before the backups are needed, as follows.

### Restore drill {#restore-drill}

Goal: prove once that the barman store restores, and time it. Nothing
touches the live cluster.

1. `kubectl -n db get backups.postgresql.cnpg.io` and the
   `firstRecoverabilityPoint` / `lastSuccessfulBackup` query above: note
   both times.
2. Apply the `postgresql-restore` manifest from "Restore Postgres" with
   `recoveryTarget.targetTime` inside the window (an hour ago, say),
   `instances: 1` and no `backup` section.
3. Wait for `kubectl -n db get cluster postgresql-restore` to report
   "Cluster in healthy state"; the elapsed time is the RTO figure.
4. Verify the data: `kubectl -n db exec -it postgresql-restore-1 -- psql -U postgres -d life_os -c 'select max(created_at) from users'`
   and one row count per hot table (`food_entries`, `money_transactions`,
   `tasks`). The newest rows before `targetTime` must be there, nothing
   after.
5. Run the binary's schema check against it: a one-off pod from the API
   image of the deployed tag with `DATABASE_HOST=postgresql-restore-rw.db.svc.cluster.local`,
   the other `DATABASE_*` parts and the password from the Secret, command
   `/app/life_os_cpp --verify-migrations config/config.json`. Exit 0 means
   the restored schema carries every migration of that version.
6. Delete the recovery cluster (`kubectl -n db delete cluster postgresql-restore`)
   and its PVCs.
7. Record here: date, `targetTime`, elapsed time, the row counts compared,
   who did it. Repeat after every PostgreSQL major upgrade and at least
   yearly.

With the mirror enabled, drill the second path too: point an
`externalClusters` entry at the destination bucket (same `serverName`
`postgresql-oci-k3s`, the destination endpoint and credentials) and
bootstrap from it. That is the restore that matters when the cluster is
gone.

Drills done: none yet.

## Restore Postgres {#restore}

A restore is a deliberate human action. Never restore over the live cluster:
CNPG recovery always bootstraps a NEW cluster from the barman store, and the
live one keeps running until you cut over.

1. Create a recovery cluster next to the live one. It reads the archive of
   `postgresql-oci-k3s` and must write its own backups under a different server
   name (or not at all), so it never overwrites the archive it restores from.
   Copy `instances`, `imageName`, storage and `managed.roles` from the live
   cluster's spec (`kubectl -n db get cluster postgresql -o yaml`).

   ```yaml
   apiVersion: postgresql.cnpg.io/v1
   kind: Cluster
   metadata:
     name: postgresql-restore
     namespace: db
   spec:
     instances: 1
     imageName: ghcr.io/cloudnative-pg/postgresql:18.3-system-trixie
     storage:
       storageClass: longhorn-single
       size: 10Gi
     bootstrap:
       recovery:
         source: oci-k3s-archive
         # Optional point in time; without it the newest WAL is replayed.
         # recoveryTarget:
         #   targetTime: "2026-10-14 08:00:00+00"
     externalClusters:
       - name: oci-k3s-archive
         barmanObjectStore:
           destinationPath: s3://tarassov-me-backups/cnpg
           endpointURL: http://minio.minio.svc.cluster.local:9000
           serverName: postgresql-oci-k3s
           s3Credentials:
             accessKeyId: {name: minio-backups-creds, key: ACCESS_KEY_ID}
             secretAccessKey: {name: minio-backups-creds, key: ACCESS_SECRET_KEY}
           wal: {maxParallel: 2}
   ```

   The live cluster was itself created this way (its `bootstrap.recovery`
   names the external cluster `talos-nbg1`, server name
   `postgresql-tarassov-me`), so the procedure is known to work with this
   bucket.

2. Wait until `kubectl -n db get cluster postgresql-restore` reports a healthy
   state, then check the data: connect to `postgresql-restore-rw` as `life-os`
   (as in `deploy/db/README.md`, step 5) and look at the newest rows.

3. Cut over. Two ways, pick by the damage:
   - Only `life_os` is bad: dump it from the recovery cluster and load it into
     the live one (`pg_dump -Fc -d life_os` from `postgresql-restore-rw`,
     `pg_restore --clean --if-exists -d life_os` into `postgresql-rw`) with the
     API and worker scaled to 0. The other databases stay untouched.
   - The whole cluster is lost: point `externalDatabase.host` (and
     `replicaHost`) in the three `deploy/values-*prod.yaml` at
     `postgresql-restore-rw` / `-ro`, then upgrade in the order below. Give the
     recovery cluster its own `backup.barmanObjectStore` with a new server name
     before relying on it.

4. Delete the recovery cluster once it is no longer needed.

Migrations are idempotent and run in the API's init container, so a restored
older schema is brought forward by the next API rollout.

## Upgrade order {#upgrade-order}

A release is three helm releases, upgraded in this order:

1. API (`life-os-cpp`, `deploy/values-prod.yaml`). Its `migrate` init
   container applies the new migrations before the new API starts.
2. Worker (`life-os-cpp-worker`, `deploy/values-worker-prod.yaml`). Its
   `wait-for-schema` init container runs `life_os_cpp --verify-migrations`
   from the API image of the same tag every 10 seconds and lets the worker
   start only when the database carries every migration of that version. If
   the API was not upgraded first, the worker pod sits in `Init` and fails
   after 10 minutes (`schemaWait.timeoutSeconds`); upgrade the API and the
   pod recovers on its next restart.
3. Frontend (`life-os-cpp-frontend`, `deploy/values-frontend-prod.yaml`), last,
   so the new SPA never calls routes the API does not serve yet.

The helm commands are in the README ("Kubernetes"). Images come only from
`release.yml` (tag `v<ver>` on master, CI green on that commit).

### NetworkPolicy on the API pods {#networkpolicy}

`deploy/values-prod.yaml` enables `helm/life-os-cpp/templates/networkpolicy.yaml`:
ingress to 8080 only from the pods labelled
`app.kubernetes.io/name=cpp-frontend`, `app.kubernetes.io/instance=life-os-cpp-frontend`;
ingress to 9090 from namespace `monitoring` (a placeholder until the
Prometheus namespace is known); egress to namespace `db` on 5432 and 6379,
DNS, and TCP 443 anywhere (s3.tarassov.me, the LLM providers, Xiaomi, Open
Food Facts). Left out on purpose: SMTP, OTLP and Sentinel, all off in
production. The worker and frontend charts have no NetworkPolicy template.

On the first rollout with it, after the API upgrade: `kubectl -n life-os get pods`
must show the API pod Ready (the kubelet's probes come from the node
address; k3s's network policy controller is expected to let them through,
and this is the one thing to watch), the `migrate` init container must have
reached Postgres, and a public API route must answer through the frontend,
for example `curl https://life-os.tarassov.me/api/v1/auth/registration`
(the frontend's own `/healthz` is answered by nginx and says nothing about
the API). If the pod never becomes Ready, `helm upgrade ... --set networkPolicy.enabled=false`
restores the previous state while the policy is fixed.

### Monitoring enablement {#monitoring}

Nothing in the repository says whether prometheus-operator runs on
`oci-k3s`, so both prod overlays carry the monitoring block switched off,
with the keys to flip written next to it. The check, from the owner's
machine:

```sh
kubectl --context oci-k3s get crd servicemonitors.monitoring.coreos.com prometheusrules.monitoring.coreos.com
```

Both CRDs present: in `deploy/values-prod.yaml` set `serviceMonitor.enabled: true`,
`monitoring.alertsEnabled: true`, `monitoring.jobsAlerts: true`,
`monitoring.targetSelector` to the label set the cluster's Prometheus puts
on this Service's targets (Prometheus UI, Status, Targets; the chart default
is `service="life-os-cpp"`), and `networkPolicy.ingress.metricsFromSelectors`
to the namespace that Prometheus runs in. In `deploy/values-worker-prod.yaml`:
`serviceMonitor.enabled: true`, `monitoring.alertsEnabled: true`,
`targetSelector` with `service="life-os-cpp-worker"`. Upgrade API, then
worker.

CRDs absent: a helm upgrade with these on fails ("no matches for kind
ServiceMonitor"). Either install kube-prometheus-stack first, or keep an
external uptime probe of a public API route through the frontend as the
only alert.

The alert names in the charts equal the names in `docs/SLO.md` and the
anchors in this file; the thresholds are in `docs/SLO.md`.

## Roll back a release {#rollback}

- k8s: `helm rollback <release> <REVISION>` (`helm history <release>`) for
  each of `life-os-cpp`, `life-os-cpp-worker`, `life-os-cpp-frontend`; roll
  back in the reverse order: frontend, worker, API.
- Images are tagged with the unprefixed version (`1.26.0`) in GHCR; pin the
  previous tag if needed.
- Migrations are forward-only — a rollback of the app does NOT revert schema.
  An older worker still passes `wait-for-schema` (a newer schema satisfies
  it). If a migration is the problem, write a new forward migration that
  fixes it.

## Rotate a secret {#rotate-secret}

The pods hash only the chart's own ConfigMap (`checksum/config`). The
credentials arrive through `extraEnvFrom` from the hand-made Secrets
(`life-os-app`, `life-os-food-llm`, `life-os-money-llm`, `life-os-s3`), so
editing a Secret changes nothing in the pod template and the running pods
keep the old value. Both app charts add a second annotation,
`checksum/extra-env`, computed from the `extraEnvFrom` list plus the value
`secretsVersion` (`deploy/values-prod.yaml` and
`deploy/values-worker-prod.yaml`, empty by default). The procedure:

1. Write the new value into the Secret (`kubectl -n life-os edit secret life-os-app`,
   or re-apply the manifest kept outside the repository).
2. Set `secretsVersion` to the date (`"2026-10-14"`) in both prod overlays
   and commit.
3. Upgrade in the usual order, API then worker (the frontend holds no
   secret). The annotation changes, every pod of the release restarts and
   reads the new value.
4. Without a chart upgrade, `kubectl -n life-os rollout restart deployment life-os-cpp life-os-cpp-worker`
   does the same restart; `secretsVersion` only makes it reproducible from
   git.

What each secret reaches:

`JWT_SECRET` signs the access and refresh JWTs and, through per-purpose
derived keys (`src/security/Tokens.hpp`), the confirm, reset, change-email
and invite tokens in account emails; the worker issues those at send time,
so it holds the secret too. The moment the new value is live every session
is invalidated (access and refresh tokens no longer verify) and every
emailed link issued before the rotation stops working. Do it at a quiet
hour and roll both deployments in one go.

`MI_FITNESS_TOKEN_KEY` is the libsodium secretbox key that seals the Mi
Fitness passToken in `mi_accounts`. There is no re-keying path: a token
sealed under the old key fails authentication under the new one
(`Xiaomi::unseal`, "wrong key or tampered"), so after a rotation every user
links their Mi account again through the QR sign-in, and scheduled syncs
fail with that error until they do. Rotate it only when the key is
suspected leaked.

`S3_SECRET_ACCESS_KEY` (Secret `life-os-s3`, key `secret-access-key`, with
its `access-key-id`) signs the wallpaper and sound uploads, deletes and
15-minute presigned PUTs (`src/storage/`). Create the new key pair for the
MinIO user first, so both pairs are valid while the pods roll, put the new
pair into the Secret, roll the API, then delete the old pair in MinIO.
Public reads go through `S3_PUBLIC_BASE_URL` anonymously and are not
affected.

`PAYPAL_CLIENT_SECRET` is the OAuth client secret of the PayPal REST calls
(`src/billing/PayPalClient.hpp`: order create and capture, webhook
signature verification). Create the new secret in the PayPal app settings,
put it in the Secret, roll, then retire the old one. While the pods still
hold a revoked secret every top-up capture and every webhook verification
fails, so keep the gap short and look at the admin payments page for
captures that failed in the window. The PayPal calls are made by the API
pod; the worker initializes the billing client too and refuses to boot
with billing enabled and empty credentials, so roll both as in the
procedure above.

The database role password (`DATABASE_PASSWORD` in `life-os-app`) belongs
to the CNPG managed role `life-os`, whose password comes from Secret
`postgresql-life-os` in namespace `db` (`deploy/db/README.md`). Change that
Secret first and let the operator apply it to the role (check with a
`psql` run as in step 5 of that README), then `DATABASE_PASSWORD` in
`life-os-app`, then roll API and worker. The old pods fail their next pool
reconnect once the role password has changed, so keep the gap short; the
`migrate` init container of the new API pod needs the new password already.
