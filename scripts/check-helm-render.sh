#!/usr/bin/env bash
#
# Render the cpp-env umbrella with CI values and assert semantic properties of
# the rendered manifests. Catches the deploy-path bug CLASS that compilation and
# unit tests can't — and that bit us repeatedly: ingress↔service port drift
# (frontend served 8080 but ingress said 80), baseDomain left at example.com,
# and MAIL_VIA_JOBS silently dropped because its env is gated on mail.enabled.
#
# It needs no cluster — pure `helm template` smoke test. Run via `make
# helm-validate` or the helm-charts CI job. Re-runs `helm dependency build` so a
# stale vendored subchart (charts/*.tgz are gitignored) can't skew the render.
set -euo pipefail

ROOT="${REPO_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
CHART="$ROOT/helm/cpp-env"
VALUES="$CHART/values-ci.yaml"

command -v helm >/dev/null 2>&1 || {
    echo "helm not installed — brew install helm"
    exit 1
}
command -v yq >/dev/null 2>&1 || {
    echo "yq not installed — brew install yq"
    exit 1
}
# Two unrelated tools answer to `yq`: mikefarah/yq (Go, the one every query
# below is written for) and the Python jq wrapper, whose syntax differs and
# whose every `select(...)` here would error out or print nothing — a false
# FAIL at best, a silent pass of a broken render at worst.
yq --version 2>/dev/null | grep -q mikefarah || {
    echo "yq is not mikefarah/yq (got: $(yq --version 2>&1 | head -1)) — install https://github.com/mikefarah/yq (brew install yq; the Python 'yq' package is a different tool)"
    exit 1
}

echo "==> helm dependency build (re-vendor subcharts)"
helm dependency build "$CHART" >/dev/null

echo "==> helm lint (umbrella with CI values)"
helm lint "$CHART" -f "$VALUES" >/dev/null || {
    echo "FAIL: helm lint"
    exit 1
}

echo "==> helm template (CI values) + semantic assertions"
RENDERED="$(helm template ci-smoke "$CHART" -f "$VALUES")"

fail() {
    echo "FAIL: $1" >&2
    exit 1
}
q() { printf '%s' "$RENDERED" | yq "$1"; }

# 1+2. Ingress backend port must equal the service port (8080) for BOTH api and
#      the rootless-nginx frontend. This is the exact 80↔8080 drift bug.
[ "$(q 'select(.kind=="Ingress" and .metadata.name=="api") | .spec.rules[0].http.paths[0].backend.service.port.number')" = "8080" ] ||
    fail "api ingress backend port != 8080"
[ "$(q 'select(.kind=="Ingress" and .metadata.name=="app") | .spec.rules[0].http.paths[0].backend.service.port.number')" = "8080" ] ||
    fail "app/web ingress backend port != 8080 (rootless nginx listens on 8080)"

# 3. MAIL_VIA_JOBS must render on the api container — it's gated on
#    life-os-cpp.mail.enabled, so a missing enabled silently drops it (mail breaks).
[ "$(q 'select(.kind=="Deployment" and .metadata.name=="api") | .spec.template.spec.containers[0].env[] | select(.name=="MAIL_VIA_JOBS") | .value')" = "true" ] ||
    fail "MAIL_VIA_JOBS not rendered =true on api (set life-os-cpp.mail.enabled=true)"

# 4. Host templating must expand from baseDomain (catches an un-overridden
#    baseDomain — the values-ci.yaml domain is ci.example.test).
host="$(q 'select(.kind=="Ingress" and .metadata.name=="api") | .spec.rules[0].host')"
[ "$host" = "api.ci.ci.example.test" ] || fail "api ingress host did not expand from baseDomain (got: '$host')"

# 5. No Service may render a null port (a templating slip that 503s the route).
nullports="$(q 'select(.kind=="Service") | .spec.ports[] | select(.port == null) | .name' | grep -c . || true)"
[ "$nullports" = "0" ] || fail "$nullports service port(s) rendered null"

# 6. The minimal preset must stay minimal — render values-minimal.yaml and
#    assert the heavy optional services are gone, so the lighter onboarding
#    preset can't silently rot back into the kitchen sink.
echo "==> helm template (minimal preset)"
MINIMAL="$(helm template ci-smoke "$CHART" -f "$VALUES" -f "$CHART/values-minimal.yaml")"
workloads="$(printf '%s' "$MINIMAL" | yq 'select(.kind=="Deployment" or .kind=="StatefulSet") | .metadata.name')"
printf '%s' "$workloads" | grep -qi jaeger && fail "values-minimal still renders a Jaeger workload"

# 7. Production security floor. prod-check.sh validates the JSON app-config, but
#    the deploy-path env comes from Helm — so the rate limiter shipping fail-OPEN
#    in prod (a Redis blip silently disables login throttling) was invisible to
#    it. Render the REAL prod releases — the values files under deploy/ that the
#    cluster runs (namespace life-os, releases life-os-cpp, life-os-cpp-worker,
#    life-os-cpp-frontend) — with their charts, and assert the security-critical
#    env the API pod actually gets. These deploy/ files are the only prod
#    overlays in the repo.
API_CHART="$ROOT/helm/life-os-cpp"
WORKER_CHART="$ROOT/helm/life-os-cpp-worker"
FRONTEND_CHART="$ROOT/helm/cpp-frontend"
PROD_API_VALUES="$ROOT/deploy/values-prod.yaml"
PROD_WORKER_VALUES="$ROOT/deploy/values-worker-prod.yaml"
PROD_FRONTEND_VALUES="$ROOT/deploy/values-frontend-prod.yaml"
for f in "$PROD_API_VALUES" "$PROD_WORKER_VALUES" "$PROD_FRONTEND_VALUES"; do
    [ -f "$f" ] || fail "prod values file $f is gone — update the list in this script"
done

echo "==> helm lint + template (prod: deploy/values-*prod.yaml) + security assertions"
helm lint "$API_CHART" -f "$PROD_API_VALUES" >/dev/null || fail "helm lint life-os-cpp with deploy/values-prod.yaml"
helm lint "$WORKER_CHART" -f "$PROD_WORKER_VALUES" >/dev/null || fail "helm lint life-os-cpp-worker with deploy/values-worker-prod.yaml"
helm lint "$FRONTEND_CHART" -f "$PROD_FRONTEND_VALUES" >/dev/null || fail "helm lint cpp-frontend with deploy/values-frontend-prod.yaml"
PROD_RENDERED="$(helm template life-os-cpp "$API_CHART" -n life-os -f "$PROD_API_VALUES")"
WORKER_RENDERED="$(helm template life-os-cpp-worker "$WORKER_CHART" -n life-os -f "$PROD_WORKER_VALUES")"
FRONTEND_RENDERED="$(helm template life-os-cpp-frontend "$FRONTEND_CHART" -n life-os -f "$PROD_FRONTEND_VALUES")"
penv() {
    printf '%s' "$PROD_RENDERED" |
        yq "select(.kind==\"Deployment\") | .spec.template.spec.containers[0].env[] | select(.name==\"$1\") | .value"
}

[ "$(penv RATE_LIMIT_ENABLED)" = "true" ] ||
    fail "deploy/values-prod.yaml: RATE_LIMIT_ENABLED != true (login is unthrottled). Set rateLimit.enabled=true."
[ "$(penv RATE_LIMIT_FAIL_OPEN)" = "false" ] ||
    fail "deploy/values-prod.yaml: rate limiter is fail-OPEN — a Redis outage disables the brute-force throttle on login. Set rateLimit.failOpen=false."
[ "$(penv SECURITY_CSRF_ENABLED)" = "true" ] ||
    fail "deploy/values-prod.yaml: SECURITY_CSRF_ENABLED != true (cookie auth without CSRF protection). Set auth.csrf.enabled=true."
[ "$(penv IDEMPOTENCY_ENABLED)" = "true" ] ||
    fail "deploy/values-prod.yaml: IDEMPOTENCY_ENABLED != true (retried POSTs are not de-duplicated). Set idempotency.enabled=true."
[ "$(penv AUTH_MODE)" = "jwt" ] || fail "deploy/values-prod.yaml: AUTH_MODE != jwt (endpoints would be public)"
[ "$(penv AUTH_COOKIE_SECURE)" = "true" ] || fail "deploy/values-prod.yaml: AUTH_COOKIE_SECURE != true (__Host- cookies need Secure)"

# No plaintext secret may be baked into a TRACKED values file — DB password / JWT
# secret must arrive via --set or external-secrets (rendered as secretKeyRef),
# so their plain .value is empty here. A non-empty value = a committed secret.
for s in DATABASE_PASSWORD JWT_SECRET; do
    [ -z "$(penv "$s")" ] || fail "deploy/values-prod.yaml: $s carries a plaintext value — pass it via --set / external-secrets, never a tracked overlay"
done

# 7b. The rest of the production floor. Production is invite-only, the
#     boot-time guard must be armed, the docs UI off (env AND the rendered
#     config.json, the two places the binary reads), HSTS on.
[ "$(penv REGISTRATION_OPEN)" = "false" ] ||
    fail "deploy/values-prod.yaml: REGISTRATION_OPEN != false (self sign-up is open; production is invite-only). Set registration.open=false."
[ "$(penv APP_ENV)" = "production" ] ||
    fail "deploy/values-prod.yaml: APP_ENV != production (the boot-time prod guard that refuses auth.mode=none is disarmed). Set app.env=production."
docs_env="$(penv DOCS_ENABLED)"
[ -z "$docs_env" ] || [ "$docs_env" = "false" ] ||
    fail "deploy/values-prod.yaml: DOCS_ENABLED=$docs_env (the Swagger UI would be public). Set docs.enabled=false."
prod_cfg="$(printf '%s' "$PROD_RENDERED" | yq 'select(.kind=="ConfigMap" and .data."config.json" != null) | .data."config.json"')"
[ -n "$prod_cfg" ] || fail "life-os-cpp render has no ConfigMap with config.json — the chart layout changed, update this script"
[ "$(printf '%s' "$prod_cfg" | yq '.docs.enabled')" = "false" ] ||
    fail "deploy/values-prod.yaml: rendered config.json has docs.enabled != false (the Swagger UI would be public). Set docs.enabled=false."
[ "$(printf '%s' "$prod_cfg" | yq '.security.hsts')" = "true" ] ||
    fail "deploy/values-prod.yaml: rendered config.json has security.hsts != true. Set security.hsts=true."

# 7b'. The internet-facing API pod does not hold the agent's upstream model
#     key: the Secret life-os-ani-agent reaches it key by key (aniAgentSecret),
#     never whole through envFrom, and without ANI_AGENT_UPSTREAM_KEY and
#     ANI_AGENT_PROXY_URL (the ani worker's alone). Every other ANI_AGENT_* and
#     ANI_TELEGRAM_* variable the code reads is on the list, so a new knob is
#     not lost on the way to the API.
api_env_names="$(printf '%s' "$PROD_RENDERED" | yq 'select(.kind=="Deployment") | .spec.template.spec.containers[0].env[].name')"
whole="$(printf '%s' "$PROD_RENDERED" |
    yq 'select(.kind=="Deployment") | (.spec.template.spec.containers[], .spec.template.spec.initContainers[]) | .envFrom[]? | select(.secretRef.name=="life-os-ani-agent") | .secretRef.name' | grep -v '^---$' || true)"
[ -z "$whole" ] ||
    fail "deploy/values-prod.yaml: the API takes the Secret life-os-ani-agent whole through extraEnvFrom; it carries the upstream model key. Use aniAgentSecret.name instead."
for k in ANI_AGENT_UPSTREAM_KEY ANI_AGENT_PROXY_URL; do
    if printf '%s\n' "$api_env_names" | grep -qx "$k"; then
        fail "life-os-cpp: the API pod gets $k, which only the ani worker uses (aniAgentSecret.keys)"
    fi
done
for k in $(grep -rhoE '"ANI_(AGENT|TELEGRAM)_[A-Z_]+"' "$ROOT/src" | tr -d '"' | sort -u); do
    case "$k" in ANI_AGENT_UPSTREAM_KEY | ANI_AGENT_PROXY_URL) continue ;; esac
    printf '%s\n' "$api_env_names" | grep -qx "$k" ||
        fail "life-os-cpp: the code reads $k, but the API pod does not get it from life-os-ani-agent; add it to aniAgentSecret.keys in helm/life-os-cpp/values.yaml"
done

# 7c. The worker's processing lease. With 0 a job whose pod was killed stays
#     in its Redis processing list forever; the lease returns it to the queue.
#     And the id of that list must be the pod name, or two replicas (or a
#     replaced pod) share one list.
wenv() {
    printf '%s' "$WORKER_RENDERED" |
        yq "select(.kind==\"Deployment\") | .spec.template.spec.containers[0].env[] | select(.name==\"$1\") | .value"
}
lease="$(wenv JOBS_VISIBILITY_TIMEOUT_SEC)"
case "$lease" in
'' | *[!0-9]*) fail "deploy/values-worker-prod.yaml: JOBS_VISIBILITY_TIMEOUT_SEC is not rendered as an integer on the worker (got '$lease'). Set jobs.visibilityTimeoutSec=300." ;;
esac
[ "$lease" -gt 0 ] ||
    fail "deploy/values-worker-prod.yaml: JOBS_VISIBILITY_TIMEOUT_SEC is 0 — the processing lease is off, a job whose worker pod is killed stays 'processing' forever. Set jobs.visibilityTimeoutSec=300."
worker_id_src="$(printf '%s' "$WORKER_RENDERED" |
    yq 'select(.kind=="Deployment") | .spec.template.spec.containers[0].env[] | select(.name=="WORKER_ID") | .valueFrom.fieldRef.fieldPath')"
[ "$worker_id_src" = "metadata.name" ] ||
    fail "deploy/values-worker-prod.yaml: WORKER_ID is not the pod name (valueFrom fieldRef metadata.name) — replicas would share one processing list. Leave worker.id empty."

# 7d. No pod may mount a ServiceAccount token: nothing here talks to the
#     Kubernetes API, and a mounted token is a credential for anyone who gets
#     code execution in the container. Every Deployment of the three prod
#     releases, and both CronJobs of the API chart (rendered with them on,
#     since production keeps them off).
assert_no_sa_token() {
    local label="$1" manifests="$2" bad
    bad="$(printf '%s' "$manifests" |
        yq 'select(.kind=="Deployment" and .spec.template.spec.automountServiceAccountToken != false) | .metadata.name' | grep -v '^---$' | grep . || true)"
    [ -z "$bad" ] || fail "$label: Deployment(s) without automountServiceAccountToken: false in the pod spec: $(printf '%s' "$bad" | paste -sd' ' -)"
    bad="$(printf '%s' "$manifests" |
        yq 'select(.kind=="CronJob" and .spec.jobTemplate.spec.template.spec.automountServiceAccountToken != false) | .metadata.name' | grep -v '^---$' | grep . || true)"
    [ -z "$bad" ] || fail "$label: CronJob(s) without automountServiceAccountToken: false in the pod spec: $(printf '%s' "$bad" | paste -sd' ' -)"
}
assert_no_sa_token "life-os-cpp + deploy/values-prod.yaml" "$PROD_RENDERED"
assert_no_sa_token "life-os-cpp-worker + deploy/values-worker-prod.yaml" "$WORKER_RENDERED"
assert_no_sa_token "cpp-frontend + deploy/values-frontend-prod.yaml" "$FRONTEND_RENDERED"

echo "==> helm template (prod API + backups + monitoring on) + CronJob / PrometheusRule assertions"
PROD_FULL="$(helm template life-os-cpp "$API_CHART" -n life-os -f "$PROD_API_VALUES" \
    --set backup.enabled=true --set backup.s3.bucket=render-check \
    --set backupMirror.enabled=true --set backupMirror.source.credentialsSecret.name=render-src \
    --set backupMirror.destination.bucket=render-check --set backupMirror.destination.credentialsSecret.name=render-dst \
    --set serviceMonitor.enabled=true --set monitoring.alertsEnabled=true --set monitoring.jobsAlerts=true)"
cronjobs="$(printf '%s' "$PROD_FULL" | yq 'select(.kind=="CronJob") | .metadata.name' | grep -v '^---$' | grep -c . || true)"
[ "$cronjobs" = "2" ] || fail "expected the backup and backup-mirror CronJobs to render with backup.enabled / backupMirror.enabled (got $cronjobs CronJob(s))"
assert_no_sa_token "life-os-cpp + backups on" "$PROD_FULL"
# The alert set is gated behind the CRD flags in production, so this is the
# only render that proves the PrometheusRule template still compiles and
# carries the names docs/SLO.md and docs/RUNBOOK.md refer to.
alerts="$(printf '%s' "$PROD_FULL" | yq 'select(.kind=="PrometheusRule") | .spec.groups[].rules[].alert')"
for a in High5xxRate HighP99Latency DeadLetterQueueGrowing JobsQueueBacklog DbPoolSaturationHigh ApiTargetDown; do
    printf '%s\n' "$alerts" | grep -qx "$a" || fail "PrometheusRule lost the alert $a (docs/SLO.md names it)"
done
[ "$(printf '%s' "$PROD_FULL" | yq 'select(.kind=="ServiceMonitor") | .metadata.name' | grep -v '^---$' | grep -c . || true)" = "1" ] ||
    fail "serviceMonitor.enabled=true did not render exactly one ServiceMonitor"

# 7e. The NetworkPolicy of the API pods is part of the production floor:
#     deploy/values-prod.yaml enables it, and its egress must keep Postgres.
np_count="$(printf '%s' "$PROD_RENDERED" | yq 'select(.kind=="NetworkPolicy") | .metadata.name' | grep -v '^---$' | grep -c . || true)"
[ "$np_count" = "1" ] ||
    fail "deploy/values-prod.yaml: no NetworkPolicy rendered for the API pods — networkPolicy.enabled must stay true (got $np_count)."
printf '%s' "$PROD_RENDERED" | yq 'select(.kind=="NetworkPolicy") | .spec.egress[].ports[].port' | grep -x 5432 >/dev/null ||
    fail "deploy/values-prod.yaml: the NetworkPolicy egress has no rule for Postgres (5432) — the pods could not reach the database."

# 8. …but that env check can only ever see the plain `.value` path, and every
#    credential in these charts reaches the container via secretKeyRef — the
#    plaintext lands in the rendered Secret's data/stringData, where nothing
#    was looking. That blind spot is how a working placeholder JWT key sat in
#    the umbrella's values with CI green until a downstream release inherited
#    it in production (cyber-accountant e19985b). Assert on the Secret payload
#    itself, over every TRACKED deployable values file, so a credential
#    re-entering values.yaml or a deploy/values-*prod.yaml fails here.
#
#    values-ci.yaml is deliberately absent from the list: its `ci-*` strings
#    are render fixtures whose whole job is to make the secretKeyRef path
#    render at all, and nothing is ever deployed from that file.
CRED_KEYS='^(password|database-password|redis-password|jwt-secret|mail-smtp-password|s3-secret-key)$'

assert_no_secret_credentials() {
    local label="$1" manifests="$2" leaked
    # Both shapes: leaf charts b64 into .data, the umbrella writes .stringData.
    leaked="$(printf '%s' "$manifests" |
        yq 'select(.kind=="Secret") | (.data // {}, .stringData // {}) | to_entries | .[] | select(.value != null and .value != "") | .key' |
        grep -E "$CRED_KEYS" | sort -u | paste -sd' ' - || true)"
    [ -z "$leaked" ] ||
        fail "$label: rendered Secret carries a committed credential (${leaked}) — tracked overlays must leave these empty and take them via --set / external-secrets"
}

assert_no_secret_credentials "life-os-cpp + deploy/values-prod.yaml" "$PROD_RENDERED"
assert_no_secret_credentials "life-os-cpp-worker + deploy/values-worker-prod.yaml" "$WORKER_RENDERED"
assert_no_secret_credentials "cpp-frontend + deploy/values-frontend-prod.yaml" "$FRONTEND_RENDERED"

# 9. The prod worker must wait for the schema of its own version: the API
#    release migrates, and a worker started first would run new code against
#    the old schema. The init container runs the API image (the worker image
#    has no API binary) at the worker's tag.
worker_tag="$(printf '%s' "$WORKER_RENDERED" |
    yq 'select(.kind=="Deployment") | .spec.template.spec.containers[0].image' | sed 's/.*://')"
wait_image="$(printf '%s' "$WORKER_RENDERED" |
    yq 'select(.kind=="Deployment") | .spec.template.spec.initContainers[] | select(.name=="wait-for-schema") | .image')"
[ "$wait_image" = "ghcr.io/moveeeax/life-os-cpp:$worker_tag" ] ||
    fail "deploy/values-worker-prod.yaml: wait-for-schema init container image is '$wait_image', want the API image at the worker tag (ghcr.io/moveeeax/life-os-cpp:$worker_tag)"

# 10. The ani worker release (life-os-ani-worker): the worker chart with
#     deploy/values-worker-prod.yaml and deploy/values-ani-worker-prod.yaml on
#     top, as docs/RUNBOOK.md installs it. The only release with a
#     ServiceAccount token, a Role on pods and secrets, a Namespace, a
#     NetworkPolicy and a ResourceQuota, so it gets its own assertions.
PROD_ANI_VALUES="$ROOT/deploy/values-ani-worker-prod.yaml"
[ -f "$PROD_ANI_VALUES" ] || fail "prod values file $PROD_ANI_VALUES is gone — update the list in this script"
echo "==> helm lint + template (prod ani worker: values-worker-prod + values-ani-worker-prod) + assertions"
helm lint "$WORKER_CHART" -f "$PROD_WORKER_VALUES" -f "$PROD_ANI_VALUES" >/dev/null ||
    fail "helm lint life-os-cpp-worker with deploy/values-worker-prod.yaml + deploy/values-ani-worker-prod.yaml"
ANI_RENDERED="$(helm template life-os-ani-worker "$WORKER_CHART" -n life-os -f "$PROD_WORKER_VALUES" -f "$PROD_ANI_VALUES")"
aq() { printf '%s' "$ANI_RENDERED" | yq "$1" | grep -v '^---$' || true; }

# The worker pod is the one pod that talks to the Kubernetes API: its token is
# mounted, through its own ServiceAccount, and nothing else in the render
# mounts one (the pre-puller's DaemonSet included).
[ "$(aq 'select(.kind=="Deployment") | .spec.template.spec.automountServiceAccountToken')" = "true" ] ||
    fail "life-os-ani-worker: the Deployment does not mount its ServiceAccount token (it starts the agent pods through the Kubernetes API)"
[ "$(aq 'select(.kind=="Deployment") | .spec.template.spec.serviceAccountName')" = "$(aq 'select(.kind=="ServiceAccount") | .metadata.name')" ] ||
    fail "life-os-ani-worker: the Deployment does not run as the chart's ServiceAccount"
[ "$(aq 'select(.kind=="DaemonSet") | .spec.template.spec.automountServiceAccountToken')" = "false" ] ||
    fail "life-os-ani-worker: the pre-puller DaemonSet mounts a ServiceAccount token"
[ "$(aq 'select(.kind=="NetworkPolicy" and .metadata.name=="ani-agents") | .metadata.name')" = "ani-agents" ] ||
    fail "life-os-ani-worker: no NetworkPolicy ani-agents for the agent pods (they would reach anything)"

# The quota: whole Mi, above zero. A unit slip once rendered "0Mi" and every
# agent pod was refused admission without a word.
for k in requests.memory limits.memory; do
    v="$(aq "select(.kind==\"ResourceQuota\") | .spec.hard.\"$k\"")"
    case "$v" in
    '' | 0Mi | *[!0-9]*Mi | *[!0-9Mi]*) fail "life-os-ani-worker: ResourceQuota $k is '$v', want a positive whole number of Mi" ;;
    esac
done
pods="$(aq 'select(.kind=="ResourceQuota") | .spec.hard.pods')"
max_pods="$(printf '%s' "$(helm show values "$WORKER_CHART")" | yq '.aniAgents.maxPods')"
ani_max="$(yq '.aniAgents.maxPods // ""' "$PROD_ANI_VALUES")"
[ -n "$ani_max" ] && max_pods="$ani_max"
[ "$pods" -ge "$max_pods" ] 2>/dev/null ||
    fail "life-os-ani-worker: ResourceQuota pods is '$pods', below aniAgents.maxPods ($max_pods)"

# aniAgents.maxPods is the backstop of ANI_AGENT_MAX_RUNNING, and the
# pre-puller must warm the image the pods run (ANI_AGENT_IMAGE). Both pairs
# are copied by hand (and renovate bumps one image at a time), so compare the
# chart with the defaults in config/config.json and src/agent/Settings.hpp.
cfg_default() { sed -nE "s/.*\"\\\$\\{$1:-([^}]*)\\}\".*/\\1/p" "$ROOT/config/config.json" | head -1; }
cfg_max="$(cfg_default ANI_AGENT_MAX_RUNNING)"
src_max="$(sed -nE 's/.*"ANI_AGENT_MAX_RUNNING", ([0-9]+)\).*/\1/p' "$ROOT/src/agent/Settings.hpp" | head -1)"
[ "$max_pods" = "$cfg_max" ] && [ "$max_pods" = "$src_max" ] ||
    fail "aniAgents.maxPods ($max_pods) must equal ANI_AGENT_MAX_RUNNING (config/config.json: '$cfg_max', src/agent/Settings.hpp: '$src_max')"
prepull_image="$(aq 'select(.kind=="DaemonSet") | .spec.template.spec.initContainers[] | select(.name=="pull") | .image')"
cfg_image="$(cfg_default ANI_AGENT_IMAGE)"
src_image="$(sed -nE 's/.*"ANI_AGENT_IMAGE", "([^"]+)"\).*/\1/p' "$ROOT/src/agent/Settings.hpp" | head -1)"
[ -n "$prepull_image" ] && [ "$prepull_image" = "$cfg_image" ] && [ "$prepull_image" = "$src_image" ] ||
    fail "aniAgents.prepull.image ('$prepull_image') must equal ANI_AGENT_IMAGE (config/config.json: '$cfg_image', src/agent/Settings.hpp: '$src_image'), or the pre-puller warms an image the pods do not run"

# Every job the agent registers (src/agent/jobs/Register.cpp) is pulled by
# this release; a missing type sits in its queue forever.
ani_types="$(aq 'select(.kind=="Deployment") | .spec.template.spec.containers[0].env[] | select(.name=="WORKER_TYPES") | .value')"
[ -n "$ani_types" ] || fail "life-os-ani-worker: WORKER_TYPES is not rendered"
handlers="$(grep -oE 'register_handler\(([A-Za-z]+::)?k[A-Za-z]+Job' "$ROOT/src/agent/jobs/Register.cpp" | sed -E 's/.*(k[A-Za-z]+Job)$/\1/' | sort -u)"
[ -n "$handlers" ] || fail "no register_handler(k...Job) found in src/agent/jobs/Register.cpp — the layout changed, update this script"
for c in $handlers; do
    t="$(grep -hoE "$c = \"[^\"]+\"" "$ROOT"/src/agent/*.hpp | sed -E 's/.*"(.*)"/\1/' | head -1)"
    [ -n "$t" ] || fail "cannot resolve the job name of $c in src/agent/*.hpp — update this script"
    case ",$ani_types," in
    *",$t,"*) ;;
    *) fail "deploy/values-ani-worker-prod.yaml: worker.types lacks $t (registered in src/agent/jobs/Register.cpp)" ;;
    esac
done

echo "==> helm template (umbrella, chart defaults) + committed-credential assertion"
DEFAULT_RENDERED="$(helm template env-smoke "$CHART")"
assert_no_secret_credentials "umbrella defaults (values.yaml)" "$DEFAULT_RENDERED"

# values-minimal.yaml is the only other tracked umbrella overlay (values-ci.yaml
# is excluded above; the stage overlay is gone, there is no stage environment).
echo "==> helm template (umbrella, values-minimal.yaml) + committed-credential assertion"
MINIMAL_ONLY="$(helm template env-smoke "$CHART" -f "$CHART/values-minimal.yaml")"
assert_no_secret_credentials "umbrella + values-minimal.yaml" "$MINIMAL_ONLY"

echo "==> helm-render: all assertions passed"
