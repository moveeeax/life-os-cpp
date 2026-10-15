#!/usr/bin/env bash
# Pre-deployment gate on the production profile the cluster actually runs.
#
# Production has exactly one config description: helm/life-os-cpp rendered
# with deploy/values-prod.yaml (the pods load the ConfigMap's config.json and
# the Deployment's env; the image's config/config.json only supplies the
# ${ENV:-default} skeleton underneath). This script renders that release and
# asserts the semantic floor on the result: auth on, cookies secure, limiter
# fail-closed, docs UI off, HSTS on, no public-path override, credentials
# only from a Secret. It replaces the former config/config.production.json,
# a file nothing deployed and that drifted from the chart (its
# jobs.visibility_timeout_sec never reached the worker; its api.public_paths
# override dropped the PayPal webhook).
#
# Usage:
#   ./scripts/prod-check.sh [deploy/values-prod.yaml]
#   make prod-check
#
# Needs helm and mikefarah yq (same tools as scripts/check-helm-render.sh,
# which asserts the wider deploy-path floor in CI; run both before shipping).
# The credential strength checks run only when JWT_SECRET / DATABASE_PASSWORD
# are present in the shell: in the cluster they live in the Secret
# life-os-app, which this script cannot read.
#
# Exit: 0 when every check passes, 1 otherwise, 2 on a missing tool or file.

set -euo pipefail

ROOT="${REPO_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
CHART="$ROOT/helm/life-os-cpp"
VALUES="${1:-$ROOT/deploy/values-prod.yaml}"
RELEASE="life-os-cpp"
NAMESPACE="life-os"
FAILURES=0

[[ -f "$VALUES" ]] || {
    echo "error: $VALUES not found" >&2
    exit 2
}
[[ -d "$CHART" ]] || {
    echo "error: chart $CHART not found" >&2
    exit 2
}
command -v helm >/dev/null 2>&1 || {
    echo "error: helm is required (brew install helm)" >&2
    exit 2
}
command -v yq >/dev/null 2>&1 || {
    echo "error: yq is required (brew install yq)" >&2
    exit 2
}
yq --version 2>/dev/null | grep -q mikefarah || {
    echo "error: yq is not mikefarah/yq (got: $(yq --version 2>&1 | head -1)) — the Python yq wrapper has another syntax" >&2
    exit 2
}

fail() {
    printf '\e[1;31m✗ %s\e[0m\n' "$1"
    FAILURES=$((FAILURES + 1))
}
pass() { printf '\e[1;32m✓ %s\e[0m\n' "$1"; }
note() { printf '\e[1;33m· %s\e[0m\n' "$1"; }

echo "== prod-check: helm template $RELEASE $CHART -n $NAMESPACE -f $VALUES =="

RENDERED="$(helm template "$RELEASE" "$CHART" -n "$NAMESPACE" -f "$VALUES")" || {
    echo "error: helm template failed" >&2
    exit 2
}

# The ConfigMap's config.json (a JSON document; yq reads it as YAML).
CFG="$(printf '%s' "$RENDERED" | yq 'select(.kind=="ConfigMap" and .data."config.json" != null) | .data."config.json"')"
[[ -n "$CFG" && "$CFG" != "null" ]] || {
    echo "error: no ConfigMap with config.json in the render — the chart layout changed, update this script" >&2
    exit 2
}
# The api container's env: plain values, and the names that come from a
# Secret (valueFrom), kept apart so a plaintext credential is visible.
ENV_PLAIN="$(printf '%s' "$RENDERED" |
    yq 'select(.kind=="Deployment") | .spec.template.spec.containers[0].env[] | select(.value != null) | .name + "=" + .value')"
ENV_FROM_SECRET="$(printf '%s' "$RENDERED" |
    yq 'select(.kind=="Deployment") | .spec.template.spec.containers[0].env[] | select(.valueFrom.secretKeyRef != null) | .name')"
ENVFROM_SECRETS="$(printf '%s' "$RENDERED" |
    yq 'select(.kind=="Deployment") | .spec.template.spec.containers[0].envFrom[]? | .secretRef.name // ""' | grep -v -e '^$' -e '^---$' || true)"

cfg() { printf '%s' "$CFG" | yq "$1"; }
penv() { printf '%s\n' "$ENV_PLAIN" | sed -n "s/^$1=//p" | head -1; }

# Resolve a knob the way utils/Config.hpp does: the env var wins, else the
# JSON value, else empty (the caller knows the code default).
resolved() {
    local json_path="$1" env_var="$2" v
    v="$(penv "$env_var")"
    if [[ -n "$v" ]]; then
        printf '%s' "$v"
        return
    fi
    v="$(cfg "$json_path")"
    [[ "$v" == "null" ]] && v=""
    printf '%s' "$v"
}

# 1. The boot-time production guard must be armed.
APP_ENV_V="$(resolved '.app.env' 'APP_ENV')"
if [[ "$APP_ENV_V" == "production" || "$APP_ENV_V" == "prod" ]]; then
    pass "app.env = $APP_ENV_V"
else
    fail "app.env = '$APP_ENV_V' (must be production: it arms the boot-time guard). Set app.env=production."
fi

# 2. Auth must not be 'none'.
MODE="$(resolved '.auth.mode' 'AUTH_MODE')"
case "$MODE" in
jwt | bearer) pass "auth.mode = $MODE" ;;
*) fail "auth.mode = '$MODE' (must be jwt or bearer in production). Set auth.mode=jwt." ;;
esac

# 3. Credentials come from a Secret, never as plain env or JSON. JWT_SECRET
#    and DATABASE_PASSWORD may be rendered as secretKeyRef, or arrive through
#    an envFrom secretRef (life-os-app in production).
for s in JWT_SECRET DATABASE_PASSWORD; do
    if [[ -n "$(penv "$s")" ]]; then
        fail "$s is a plaintext env value in the render — it must come from a Secret (extraEnvFrom secretRef or secretKeyRef)"
    elif printf '%s\n' "$ENV_FROM_SECRET" | grep -qx "$s"; then
        pass "$s comes from a Secret (secretKeyRef)"
    elif [[ -n "$ENVFROM_SECRETS" ]]; then
        pass "$s is expected from the envFrom Secret(s): $(printf '%s' "$ENVFROM_SECRETS" | paste -sd' ' -)"
    else
        fail "$s reaches the pod from nowhere: no secretKeyRef env and no extraEnvFrom secretRef"
    fi
done
# Strength checks on the values themselves, when the shell has them (the
# cluster Secret is out of reach here; see docs/RUNBOOK.md for the kubectl way).
if [[ -n "${JWT_SECRET:-}" ]]; then
    if [[ ${#JWT_SECRET} -lt 32 ]]; then
        fail "JWT_SECRET (shell) is shorter than 32 chars (have ${#JWT_SECRET}) — use 'openssl rand -hex 32'"
    elif [[ "$JWT_SECRET" == *dev* || "$JWT_SECRET" == *test* || "$JWT_SECRET" == *secret* || "$JWT_SECRET" == *change* ]]; then
        fail "JWT_SECRET (shell) looks like a dev placeholder"
    else
        pass "JWT_SECRET (shell) is ${#JWT_SECRET} chars and not a placeholder"
    fi
else
    note "JWT_SECRET not in the shell: its length is checked by the binary at boot (>= 32 chars), not here"
fi
if [[ -n "${DATABASE_PASSWORD:-}" ]]; then
    case "$DATABASE_PASSWORD" in
    postgres | password | changeme | admin | root | 123456) fail "DATABASE_PASSWORD (shell) is a known-weak default" ;;
    *) pass "DATABASE_PASSWORD (shell) is not a known-weak default" ;;
    esac
fi
if [[ "$(penv DATABASE_REQUIRE_SECURE_PASSWORD)" == "true" || "${DATABASE_REQUIRE_SECURE_PASSWORD:-}" == "true" ]]; then
    pass "DATABASE_REQUIRE_SECURE_PASSWORD=true (a weak DB password aborts boot)"
else
    note "DATABASE_REQUIRE_SECURE_PASSWORD is not set in the render: a weak DB password only logs a warning at boot. Put DATABASE_REQUIRE_SECURE_PASSWORD=true into the Secret life-os-app to make it fatal."
fi

# 4. Cookies secure when enabled (the __Host- prefix needs it).
if [[ "$(resolved '.auth.cookies.enabled' 'AUTH_COOKIES_ENABLED')" == "true" ]]; then
    if [[ "$(resolved '.auth.cookies.secure' 'AUTH_COOKIE_SECURE')" == "true" ]]; then
        pass "auth.cookies.secure = true"
    else
        fail "auth.cookies.secure must be true in production (cookies over https only). Set auth.cookies.secure=true."
    fi
    if [[ "$(resolved '.security.csrf.enabled' 'SECURITY_CSRF_ENABLED')" == "true" ]]; then
        pass "security.csrf.enabled = true"
    else
        fail "security.csrf.enabled must be true with cookie auth. Set auth.csrf.enabled=true."
    fi
fi

# 5. Rate limiter: on and fail-closed.
if [[ "$(resolved '.rate_limit.enabled' 'RATE_LIMIT_ENABLED')" == "true" ]]; then
    pass "rate_limit.enabled = true"
else
    fail "rate_limit.enabled must be true in production. Set rateLimit.enabled=true."
fi
if [[ "$(resolved '.rate_limit.fail_open' 'RATE_LIMIT_FAIL_OPEN')" == "false" ]]; then
    pass "rate_limit.fail_open = false"
else
    fail "rate_limit.fail_open must be false (Redis outage must not disable the limiter). Set rateLimit.failOpen=false."
fi
# The public auth surface is throttled by rate_limit.protected_* with the
# code defaults (docs/CONFIG.md). A full override of the path lists in the
# rendered JSON is the footgun that once dropped the PayPal webhook from
# api.public_paths: refuse it.
for k in '.api.public_paths' '.rate_limit.protected_paths'; do
    if [[ "$(cfg "$k")" == "null" ]]; then
        pass "$k not overridden (the built-in list applies)"
    else
        fail "$k is overridden in the rendered config.json — a full override silently drops routes (the PayPal webhook, /ready). Remove it; extend the list in src/utils/Strings.hpp instead."
    fi
done

# 6. Swagger UI off.
DOCS="$(resolved '.docs.enabled' 'DOCS_ENABLED')"
if [[ "$DOCS" == "false" || -z "$DOCS" ]]; then
    pass "docs.enabled = false"
else
    fail "docs.enabled must be false in production (Swagger UI exposes the surface). Set docs.enabled=false."
fi

# 7. HSTS on: TLS ends at the ingress, and the API's own answers carry the
#    header so a proxied response never downgrades it.
if [[ "$(resolved '.security.hsts' 'SECURITY_HSTS')" == "true" ]]; then
    pass "security.hsts = true"
else
    fail "security.hsts must be true in production. Set security.hsts=true."
fi

# 8. JSON log format (aggregator-ready).
if [[ "$(resolved '.logging.format' 'LOG_FORMAT')" == "json" ]]; then
    pass "logging.format = json"
else
    fail "logging.format should be json in production. Set logging.format=json."
fi

# 9. Every ${VAR} placeholder without a default in the rendered config.json
#    must be fed by the Deployment: a secretKeyRef env, or an envFrom Secret.
#    (The role env-check.sh plays for config/config.json.)
unsatisfied=0
while IFS= read -r var; do
    [[ -z "$var" ]] && continue
    if printf '%s\n' "$ENV_FROM_SECRET" | grep -qx "$var" || [[ -n "$(penv "$var")" ]]; then
        continue
    fi
    if [[ -n "$ENVFROM_SECRETS" ]]; then
        continue # expected from the envFrom Secret(s); cannot be verified here
    fi
    note "placeholder \${$var} in config.json has no default and nothing in the Deployment feeds it"
    unsatisfied=$((unsatisfied + 1))
done < <(printf '%s' "$CFG" | grep -oE '\$\{[A-Z_][A-Z0-9_]*\}' | tr -d '${}' | sort -u)
if [[ $unsatisfied -eq 0 ]]; then
    pass "every required \${VAR} placeholder in config.json is fed by env or an envFrom Secret"
else
    fail "$unsatisfied placeholder(s) in the rendered config.json have no source — add them to the Secret or the chart env"
fi

echo
echo "NOTE: scripts/check-helm-render.sh (make helm-validate) asserts the wider"
echo "      deploy-path floor: registration closed, worker lease, NetworkPolicy,"
echo "      automountServiceAccountToken, no committed credentials."
echo
if [[ $FAILURES -gt 0 ]]; then
    printf '\e[1;31mprod-check FAILED: %d issue(s).\e[0m\n' "$FAILURES"
    exit 1
fi
echo "prod-check passed — the rendered production profile is production-shaped."
