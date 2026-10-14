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
2. Pull a failing trace (`tid=` in the log) — `make tail-trace TID=<id>` shows
   the `db.*` child spans with the SQL template + pool label.
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

---

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
lost.

Check that backups are real, read-only:

```sh
kubectl --context oci-k3s -n db get backups.postgresql.cnpg.io        # one "completed" per day
kubectl --context oci-k3s -n db get cluster postgresql \
  -o jsonpath='{.status.firstRecoverabilityPoint}{"  "}{.status.lastSuccessfulBackup}{"\n"}'
```

The restore below has not been drilled on this cluster yet. Do one into a
scratch cluster before the backups are needed.

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
