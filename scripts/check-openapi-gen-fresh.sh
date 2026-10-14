#!/usr/bin/env bash
# Freshness gate for tests/e2e/openapi.gen.json.
#
# The e2e binary validates response bodies against tests/e2e/openapi.gen.json,
# a committed JSON conversion of docs/openapi.yaml that scripts/gen-openapi-json.sh
# (and release.sh) produce. The e2e suite re-hashes the YAML at runtime and
# fails when the stamp is stale, but that only runs when the e2e bucket runs.
# This gate makes the check a plain CI step: regenerate into a scratch dir and
# require the result to be byte-identical to the committed file. Nothing is
# ignored in the comparison — the x-generated block carries no timestamp,
# only the generator name, the source path and the FNV-1a-64 of the YAML,
# all of which are reproducible.
#
# Inputs: REPO_ROOT (the selftest points it at a copy). Needs python3 with
# pyyaml, like the generator; without pyyaml the gate exits 2 (not a pass).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="${REPO_ROOT:-$(cd "$SCRIPT_DIR/.." && pwd)}"
SPEC="$REPO/docs/openapi.yaml"
COMMITTED="$REPO/tests/e2e/openapi.gen.json"
GEN="$SCRIPT_DIR/gen-openapi-json.sh"

for f in "$SPEC" "$COMMITTED" "$GEN"; do
    if [[ ! -f "$f" ]]; then
        echo "check-openapi-gen-fresh: $f not found" >&2
        exit 2
    fi
done

if ! python3 -c 'import yaml' >/dev/null 2>&1; then
    echo "check-openapi-gen-fresh: python3 has no 'yaml' module (pyyaml) — the generator cannot run," >&2
    echo "  so the freshness of tests/e2e/openapi.gen.json cannot be checked. This is exit 2, not a" >&2
    echo "  pass: pip install pyyaml and re-run." >&2
    exit 2
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/openapi-gen-fresh.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/docs" "$TMP/tests/e2e"
cp "$SPEC" "$TMP/docs/openapi.yaml"
if ! gen_out="$(REPO_ROOT="$TMP" "$GEN" 2>&1)"; then
    echo "check-openapi-gen-fresh: scripts/gen-openapi-json.sh failed on docs/openapi.yaml:" >&2
    printf '%s\n' "$gen_out" | sed 's/^/    /' >&2
    exit 2
fi

if diff_out="$(diff -u "$COMMITTED" "$TMP/tests/e2e/openapi.gen.json" --label committed/tests/e2e/openapi.gen.json --label regenerated/tests/e2e/openapi.gen.json)"; then
    echo "✓ tests/e2e/openapi.gen.json is fresh (regenerated from docs/openapi.yaml byte-for-byte)"
    exit 0
fi

echo "✗ tests/e2e/openapi.gen.json is STALE: docs/openapi.yaml changed after it was generated."
echo "  Regenerate and commit it: run scripts/gen-openapi-json.sh"
echo "  First lines of the difference (committed vs regenerated):"
printf '%s\n' "$diff_out" | head -n 40 | sed 's/^/    /'
exit 1
