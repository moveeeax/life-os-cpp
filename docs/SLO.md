# SLOs & alert thresholds

Defines what "healthy" means so the alerts in `docker/prometheus-rules.yml` /
the Helm `PrometheusRule` have a rationale, and on-call has a target rather
than a guess. These are **starting defaults** — set yours from real traffic.

## Service-level objectives (suggested)

| SLO | Target | Measured by |
|---|---|---|
| Availability (non-5xx) | 99.9% / 30d | `1 - rate(http_requests_total{status=~"5.."}) / rate(http_requests_total)` |
| Latency | p99 < 1s | `histogram_quantile(0.99, http_request_duration_seconds)` |
| Job delivery | DLQ drains < 15m | `jobs_dlq_depth` returns to 0 |
| Job timeliness | queue drains, no backlog | `jobs_queue_depth{type="_total"}` stays low |
| DB pool saturation | < 90% utilized | `db_pool_active_connections / db_pool_size` |

Error budget at 99.9% ≈ 43 min/month of full unavailability. The `High5xxRate`
alert fires at 5% (well above budget burn) so it catches incidents, not slow
burn — add a multi-window burn-rate alert if you adopt strict budgeting.

## Alert thresholds → SLO mapping

| Alert | Threshold | Why | Tune when |
|---|---|---|---|
| `High5xxRate` | 5xx > 5% for 5m | Fast incident signal, not budget burn | Lower to 1% once traffic is steady |
| `HighP99Latency` | p99 > 1s for 10m | Matches latency SLO | Set to your real p99 + headroom |
| `ReplicationLagHigh` | lag > 60s for 5m | Stale reads become user-visible | Lower if you serve read-heavy traffic from replicas |
| `DeadLetterQueueGrowing` | DLQ > 0 (or `dlqDepth`) for 15m | Jobs silently failing | Raise `dlqDepth`/`dlqPerTypeDepth` if some failures are expected |
| `RetriesExhaustedSpike` (`WorkerRetriesExhaustedSpike` on the worker chart) | exhausted retries > 0.1/s for 5m | Downstream past retry budget | Raise once the normal rate is known |
| `JobsQueueBacklog` | queue `_total` > 100 for 10m | Workers outrun by submitters | Set to your throughput headroom |
| `DbPoolSaturationHigh` | active/size > 90% for 5m | Connections about to time out on acquire | Raise pool size first, then revisit |
| `ApiTargetDown` / `WorkerTargetDown` | no scrape 2m (API) / 5m (worker) | Process down/wedged | Keep |

Alerts the Helm charts add on top of the table (not in the compose rules):

| Alert | Threshold | Needs |
|---|---|---|
| `DeadLetterQueueSpikeByType` | one type's DLQ depth > 5 for 10m (`dlqPerTypeDepth`) | `monitoring.jobsAlerts` |
| `ApiPodNotReady` | a pod NotReady for 5m | kube-state-metrics |
| `ApiCrashLoopBackOff` | container restarts over 15m | kube-state-metrics |
| `BackupJobFailed` | the newest `life-os-cpp-backup*` Job failed, 5m | `backup.enabled`, `monitoring.jobsAlerts`, kube-state-metrics |
| `BackupTooOld` | no completed backup Job for 26h (`backupMaxAgeSeconds`) | same |

Source of the numbers: `helm/life-os-cpp/values.yaml` under
`monitoring.thresholds` (`errorRate: 0.05`, `latencyP99Sec: 1`,
`dlqDepth: 0`, `queueDepth: 100`, `dbPoolSaturation: 0.9`,
`replicaLagSec: 60`, `dlqPerTypeDepth: 5`, `retriesExhaustedPerSec: 0.1`),
the worker chart's `monitoring.thresholds` for its two rules, and the
compose copy inlined in `docker/prometheus-rules.yml`. The three carry the
same numbers as this table; change all of them together. Every alert links
to `docs/RUNBOOK.md` via `runbook_url`. In production the rules are off
until prometheus-operator is confirmed on the cluster (`docs/RUNBOOK.md`,
"Monitoring enablement").

## What's NOT measured yet (gaps to wire when you need them)

- No multi-window burn-rate alerting (single-threshold only).
- Dashboards are provisioned (`docker/grafana/`) but the panel set is minimal —
  build per-SLO panels from the queries above.
