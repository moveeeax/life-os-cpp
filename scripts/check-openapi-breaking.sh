#!/usr/bin/env bash
# Breaking-change gate for docs/openapi.yaml.
#
# check-openapi-drift.sh compares (method, path) tuples only; nothing checked
# that a PR keeps the request and response SCHEMAS it inherited. This gate
# turns docs/ARCHITECTURE.md §7's additive-only rule into a CI verdict: the
# spec on the current tree is diffed against the spec on the merge base with
# master, and any breaking change (a removed path or operation, a response
# property that is no longer required, a tightened type, a removed enum
# value, a new required request property, ...) fails the job. Additive
# changes pass, a new value of a response enum among them: oasdiff rates it
# a warning, scripts/oasdiff-severity-levels.txt lowers it to info (the file
# takes "<rule id> <level>" lines and no comments). A request change oasdiff
# rates as breaking but the server still accepts (a request enum widened to a
# plain string while every old value is still taken) is listed in
# scripts/oasdiff-err-ignore.txt: the accepted widenings as exact oasdiff
# messages, "<METHOD> <path> <message>", one per line, no comments. A line
# that no longer matches anything does nothing. The diff engine is
# oasdiff (https://github.com/oasdiff/oasdiff),
# pinned by version and sha256 in the `openapi-drift` job of
# .github/workflows/ci.yml.
#
# Inputs (all optional):
#   REPO_ROOT          — repo to check (the selftest points it at a copy)
#   OASDIFF            — path to the oasdiff binary (default: on PATH)
#   OPENAPI_BASE_REF   — git ref whose merge base with HEAD is the baseline
#                        (default origin/master, then master)
#   OPENAPI_BASE_SPEC  — a baseline spec FILE; skips git entirely. Used by
#                        check-selftest.sh (its copies are not git trees) and
#                        handy for "compare against this release's spec".
#
# Exit codes: 0 no breaking change, 1 breaking change found, 2 the gate could
# not run (oasdiff missing, no baseline). 2 is never a pass: a missing tool
# must not look like a green gate.
set -euo pipefail

REPO="${REPO_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
SPEC_REL="docs/openapi.yaml"
SPEC="$REPO/$SPEC_REL"
OASDIFF_BIN="${OASDIFF:-oasdiff}"
SEVERITY_LEVELS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/oasdiff-severity-levels.txt"
ERR_IGNORE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/oasdiff-err-ignore.txt"

if [[ ! -f "$SPEC" ]]; then
    echo "check-openapi-breaking: $SPEC not found" >&2
    exit 2
fi

if ! command -v "$OASDIFF_BIN" >/dev/null 2>&1; then
    echo "check-openapi-breaking: oasdiff is NOT installed — the breaking-change gate cannot run." >&2
    echo "  This is exit 2, not a pass. Install the pinned release (version + sha256 in the" >&2
    echo "  'Install oasdiff' step of the openapi-drift job in .github/workflows/ci.yml) or" >&2
    echo "  point OASDIFF at a binary, then re-run." >&2
    exit 2
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/openapi-breaking.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

base_label=""
if [[ -n "${OPENAPI_BASE_SPEC:-}" ]]; then
    if [[ ! -f "$OPENAPI_BASE_SPEC" ]]; then
        echo "check-openapi-breaking: OPENAPI_BASE_SPEC=$OPENAPI_BASE_SPEC is not a file" >&2
        exit 2
    fi
    cp "$OPENAPI_BASE_SPEC" "$TMP/openapi.base.yaml"
    base_label="$OPENAPI_BASE_SPEC"
else
    if ! git -C "$REPO" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        echo "check-openapi-breaking: $REPO is not a git work tree and OPENAPI_BASE_SPEC is unset —" >&2
        echo "  no baseline to compare against." >&2
        exit 2
    fi
    base_ref="${OPENAPI_BASE_REF:-}"
    if [[ -z "$base_ref" ]]; then
        if git -C "$REPO" rev-parse --verify -q origin/master >/dev/null; then
            base_ref=origin/master
        elif git -C "$REPO" rev-parse --verify -q master >/dev/null; then
            base_ref=master
        else
            echo "check-openapi-breaking: neither origin/master nor master is known here; in CI the" >&2
            echo "  checkout needs fetch-depth: 0 (the merge base is computed from history). Set" >&2
            echo "  OPENAPI_BASE_REF or OPENAPI_BASE_SPEC to choose the baseline by hand." >&2
            exit 2
        fi
    fi
    base_sha="$(git -C "$REPO" merge-base HEAD "$base_ref")" || {
        echo "check-openapi-breaking: git merge-base HEAD $base_ref failed (shallow clone?)." >&2
        exit 2
    }
    # On master itself the merge base is HEAD: compare the commit with its
    # first parent, so a direct push still gets a verdict for what it changed.
    if [[ "$base_sha" == "$(git -C "$REPO" rev-parse HEAD)" ]]; then
        if ! base_sha="$(git -C "$REPO" rev-parse -q --verify HEAD^ 2>/dev/null)"; then
            echo "✓ OpenAPI breaking-change check: HEAD is $base_ref and has no parent — nothing to compare"
            exit 0
        fi
    fi
    if ! git -C "$REPO" show "$base_sha:$SPEC_REL" >"$TMP/openapi.base.yaml" 2>/dev/null; then
        echo "✓ OpenAPI breaking-change check: $SPEC_REL does not exist at ${base_sha:0:12} — a new spec has nothing to break"
        exit 0
    fi
    base_label="$base_ref @ ${base_sha:0:12}"
fi

# --fail-on WARN: every breaking change counts, including the ones oasdiff
# rates as warnings. Additive changes are "info" and never fail.
set +e
output="$("$OASDIFF_BIN" breaking --fail-on WARN --severity-levels "$SEVERITY_LEVELS" --err-ignore "$ERR_IGNORE" --format text \
    "$TMP/openapi.base.yaml" "$SPEC" 2>&1)"
status=$?
set -e

case "$status" in
0)
    echo "✓ OpenAPI breaking-change check: no breaking change in $SPEC_REL vs $base_label"
    exit 0
    ;;
1)
    echo "✗ BREAKING change(s) in $SPEC_REL vs $base_label — the API contract is additive-only (docs/ARCHITECTURE.md §7):"
    printf '%s\n' "$output" | sed 's/^/    /'
    echo "  A removed path, operation, required response property, enum value or a tightened type"
    echo "  breaks every client built from the published spec. Deprecate instead of removing, add"
    echo "  instead of renaming, or widen instead of tightening. (oasdiff breaking --fail-on WARN)"
    exit 1
    ;;
*)
    echo "check-openapi-breaking: oasdiff exited $status (could not compare):" >&2
    printf '%s\n' "$output" | sed 's/^/    /' >&2
    exit 2
    ;;
esac
