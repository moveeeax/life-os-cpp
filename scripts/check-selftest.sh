#!/usr/bin/env bash
#
# Proves the check-* gates bite.
#
# A gate that is only ever run against healthy trees is indistinguishable from
# `exit 0`, and this project's own history has both failure modes on record:
# check-test-buckets.sh read only its first directory argument (tests/api was
# never scanned), and a sibling service shipped a gitleaks config that scanned
# with zero rules — green for months, checking nothing. So: for each of the ten
# gate scripts, copy the subtree it reads into a scratch dir, plant a known
# breakage, and require the gate to FAIL and to NAME what it found. Two planted
# breakages per gate, with four exceptions: five for check-version-sync.sh,
# which also guards the helm image pins, Chart.yaml appVersions and the
# openapi.yaml / frontend package versions; eight for check-helm-render.sh,
# whose production floor has one case per assertion (fail-open limiter,
# committed credential, open registration, dev APP_ENV, docs UI on, worker
# lease off, missing automountServiceAccountToken, NetworkPolicy off); four
# for check-config-sync.sh (an unregistered key, a stale doc row, one knob
# read with two defaults, a chart env missing from compose); three for check-module-deps.sh (two forbidden
# edges, one declared include cycle); two for check-openapi-breaking.sh (a
# removed path, a required response property made optional); one each for
# check-openapi-gen-fresh.sh (a stale tests/e2e/openapi.gen.json),
# assemble-changelog.sh --check (the changelog-fragment format gate) and
# prod-check.sh (the rendered production profile). Thirty-four in all.
#
# Every case follows the same discipline (borrowed from the cyber-accountant
# service's check-render-selftest.sh, which learned it the expensive way):
#
#   1. CONTROL — run the gate on the UNTOUCHED copy first and require a PASS.
#      Without the control, a case could "pass" because the gate rejects
#      everything, or because the copy was incomplete.
#   2. FINGERPRINT — the file list is fixed BEFORE the mutation; each file's
#      cksum+size is taken before and after. If the digests match, the mutator
#      changed nothing and the case would have re-tested the healthy tree.
#   3. COUNTED MUTATORS — every mutator is python3 and counts its own
#      substitutions, exiting nonzero when the count is wrong. A `sed` whose
#      pattern silently stopped matching is exactly how a selftest rots into a
#      green rubber stamp.
#   4. NEEDLES — the gate must exit nonzero AND its output must contain every
#      expected substring, so a gate that fails for the WRONG reason (crash,
#      missing file, unrelated assertion) does not count as a catch.
#
# The gates run against the scratch copies via the REPO_ROOT override each
# check script supports. The check-helm-render.sh and prod-check.sh cases
# need helm + yq (same tools the helm-charts CI job installs); they are
# REQUIRED, not skipped — a skipped case reports success, which is the exact
# disease this script exists to cure. Two tools are not required: oasdiff
# (check-openapi-breaking.sh) and pyyaml (check-openapi-gen-fresh.sh). Their
# cases print a SELFTEST SKIP line and are counted apart, never as caught;
# the verdict names the skipped count so a run without them cannot read as
# a full proof.
#
# Usage: ./scripts/check-selftest.sh
set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd -- "$SCRIPT_DIR/.." && pwd)"

for tool in python3 git; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "check-selftest: $tool required" >&2
        exit 1
    }
done
# helm + yq are hard requirements, not a skip: the check-helm-render.sh and
# prod-check.sh cases are the only proof those gates bite, and a silently
# skipped case is a green rubber stamp ("skipped job reports success").
for tool in helm yq; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "check-selftest: $tool required — the check-helm-render.sh cases cannot run" >&2
        echo "  without it, and skipping them would report success while testing nothing." >&2
        echo "  Install it (brew install $tool / see the helm-charts CI job) and re-run." >&2
        exit 1
    }
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
failures=0
cases=0
skipped=0

# skip_case <label> <reason> — a case whose tool is not installed here. Loud
# on purpose: the line is printed, the count is reported in the verdict, and
# the case is never counted among the caught ones.
skip_case() {
    skipped=$((skipped + 1))
    echo "SELFTEST SKIP [$1]: $2"
}

# --- plumbing ----------------------------------------------------------------

# copy_tree <dest> <path>... — copy the files git sees under the given repo
# paths into <dest>, preserving layout: tracked files plus untracked ones
# that are not ignored (a file added on the branch and not yet committed is
# what CI will see after the commit). git ls-files, not cp -R: ignored
# litter (vendored charts/*.tgz, editor droppings) must not leak into the
# control run, or the control stops proving what CI would see.
copy_tree() {
    local dest="$1" f
    shift
    while IFS= read -r f; do
        mkdir -p "$dest/$(dirname "$f")"
        cp "$REPO/$f" "$dest/$f"
    done < <(git -C "$REPO" ls-files --cached --others --exclude-standard -- "$@" | LC_ALL=C sort -u)
}

# tree_files <dir> — every regular file under <dir>, one per line, sorted.
tree_files() {
    find "$1" -type f | LC_ALL=C sort
}

# digest_of — reads paths on stdin, emits "<path> <cksum> <bytes>" per path,
# or "<path> GONE" for one that no longer exists. The path list is captured
# BEFORE the mutation on purpose: only a change to a file the gate actually
# reads may count as a mutation, so litter a broken mutator leaves behind
# cannot masquerade as a successful one. POSIX tools only (cksum), because
# this runs on ubuntu CI runners and on the maintainer's macOS.
digest_of() {
    local file
    while IFS= read -r file; do
        if [[ -f "$file" ]]; then
            printf '%s %s\n' "$file" "$(cksum <"$file")"
        else
            printf '%s GONE\n' "$file"
        fi
    done
}

# run_case <label> <gate-script> <mutator> <copy-paths> <needle>...
#   label      — scratch-dir name and log tag
#   gate       — script under scripts/ to run with REPO_ROOT=<copy>; may carry
#                arguments ("assemble-changelog.sh --check"), word-split below
#   mutator    — shell function: takes the copy root, plants the breakage
#   copy-paths — space-separated repo paths the gate reads (word-split on
#                purpose; none of them contain spaces)
#   needles    — literal substrings the failing gate's output must contain
run_case() {
    local label="$1" gate="$2" mutator="$3" paths="$4"
    shift 4
    cases=$((cases + 1))
    local tree="$TMP/$label"
    mkdir -p "$tree"
    # shellcheck disable=SC2086 # deliberate word-split of the path list
    copy_tree "$tree" $paths

    local -a gate_cmd
    read -r -a gate_cmd <<<"$gate"
    gate_cmd[0]="$SCRIPT_DIR/${gate_cmd[0]}"

    local output status
    # Control: the untouched copy must PASS, or the case proves nothing.
    output="$(REPO_ROOT="$tree" "${gate_cmd[@]}" 2>&1)"
    status=$?
    if [[ "$status" -ne 0 ]]; then
        echo "SELFTEST FAIL [$label]: $gate fails on a HEALTHY tree (exit $status)," >&2
        echo "  so a failure on the broken one would prove nothing:" >&2
        printf '%s\n' "$output" | sed 's/^/    /' >&2
        failures=$((failures + 1))
        return
    fi

    # Now break it — and prove the breakage happened. A mutator whose anchor
    # rotted away would otherwise hand the gate the SAME tree it just passed,
    # and the case would report OK while testing nothing at all.
    local tracked digest_before digest_after
    tracked="$(tree_files "$tree")"
    digest_before="$(printf '%s\n' "$tracked" | digest_of)"
    if ! "$mutator" "$tree"; then
        echo "SELFTEST FAIL [$label]: the mutator $mutator itself failed — its anchor" >&2
        echo "  no longer matches the tree. Re-point the mutator at the shape the" >&2
        echo "  file has NOW; do not delete the case." >&2
        failures=$((failures + 1))
        return
    fi
    digest_after="$(printf '%s\n' "$tracked" | digest_of)"
    if [[ "$digest_before" == "$digest_after" ]]; then
        echo "SELFTEST FAIL [$label]: mutator changed NOTHING — every file under the" >&2
        echo "  copy is byte-identical, so this case would have re-tested the healthy" >&2
        echo "  tree and reported OK. A mutator that stops mutating is a gate that" >&2
        echo "  stops gating." >&2
        failures=$((failures + 1))
        return
    fi

    output="$(REPO_ROOT="$tree" "${gate_cmd[@]}" 2>&1)"
    status=$?
    if [[ "$status" -eq 0 ]]; then
        echo "SELFTEST FAIL [$label]: $gate PASSED the planted breakage:" >&2
        printf '%s\n' "$output" | sed 's/^/    /' >&2
        failures=$((failures + 1))
        return
    fi

    local missing=0 needle
    for needle in "$@"; do
        if ! printf '%s\n' "$output" | grep -qF -- "$needle"; then
            echo "SELFTEST FAIL [$label]: $gate failed, but never said: $needle" >&2
            missing=1
        fi
    done
    if [[ "$missing" -ne 0 ]]; then
        printf '%s\n' "$output" | sed 's/^/    /' >&2
        failures=$((failures + 1))
        return
    fi
    echo "SELFTEST OK   [$label]: $gate exit $status, named the planted breakage"
}

# --- the breakages -----------------------------------------------------------
# Each mutator takes the copy root as $1, edits in place, and COUNTS. An
# anchor that stops matching must fail the mutator loudly (see run_case), not
# no-op — every anchor below is a real line in this repo today.

# 1. A route registered in Api::get_endpoints() that the OpenAPI spec never
#    heard of — the "added the endpoint, forgot the spec" drift.
break_registry_ghost_route() {
    python3 - "$1/src/api/Endpoints.hpp" <<'PY'
import sys
path = sys.argv[1]
anchor = '{"GET", "/api/v1/jobs", "List jobs"},'
with open(path, encoding="utf-8") as fh:
    text = fh.read()
n = text.count(anchor)
if n != 1:
    sys.exit("break_registry_ghost_route: expected exactly 1 anchor row %r in %s, found %d"
             % (anchor, path, n))
text = text.replace(
    anchor,
    anchor + '\n        {"GET", "/api/v1/selftest-ghost", "Selftest: planted ghost route"},')
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 2. The spec's method flipped get -> put on an existing path: drift in BOTH
#    directions at once (code has a get the spec lost, spec has a put the code
#    never registered). This is the case the old path-only comparison missed.
break_spec_method_flip() {
    python3 - "$1/docs/openapi.yaml" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
text, n = re.subn(r"(?m)^(  /api/v1/jobs:\n)    get:", r"\1    put:", text)
if n != 1:
    sys.exit("break_spec_method_flip: flipped %d get: operations under /api/v1/jobs "
             "in %s — expected exactly 1" % (n, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 3. A hand-written ADD_METHOD_TO the registry never saw: the route would
#    work, invisibly to --print-routes and the OpenAPI drift check.
break_unlinked_controller() {
    python3 - "$1/src/api/Api.hpp" <<'PY'
import sys
path = sys.argv[1]
line = '#include "api/TasksController.hpp"\n'
with open(path, encoding="utf-8") as fh:
    text = fh.read()
if line not in text:
    sys.exit("break_unlinked_controller: %s does not include TasksController.hpp" % path)
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text.replace(line, ""))
PY
}

break_rogue_controller_route() {
    python3 - "$1/src/api/HealthController.hpp" <<'PY'
import sys
path = sys.argv[1]
planted = 'ADD_METHOD_TO(HealthController::selftest_ghost, "/api/v1/selftest-ghost", Get);'
with open(path, encoding="utf-8") as fh:
    text = fh.read()
if planted in text:
    sys.exit("break_rogue_controller_route: %s already contains the planted route" % path)
# Appended after the class body — never compiled, only grepped by the gate.
with open(path, "a", encoding="utf-8") as fh:
    fh.write("\n// planted by scripts/check-selftest.sh\n" + planted + "\n")
PY
}

# 4. An unversioned /api route re-entering the registry — the docs/ARCHITECTURE.md §7
#    violation the versioning lint exists to stop.
break_unversioned_registry_row() {
    python3 - "$1/src/api/Endpoints.hpp" <<'PY'
import sys
path = sys.argv[1]
anchor = '{"GET", "/api/v1/jobs", "List jobs"},'
with open(path, encoding="utf-8") as fh:
    text = fh.read()
n = text.count(anchor)
if n != 1:
    sys.exit("break_unversioned_registry_row: expected exactly 1 anchor row %r in %s, found %d"
             % (anchor, path, n))
text = text.replace(
    anchor,
    anchor + '\n        {"GET", "/api/selftest-unversioned", "Selftest: planted unversioned route"},')
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 5/6. A suite name duplicated across buckets. Two cases, one per non-unit
#    bucket, because the gate USED to read only its first directory argument —
#    tests/api was silently never scanned. Case 6 is the regression test for
#    that exact bug.
_break_bucket_dup() {
    # $1 = copy root, $2 = unit suite to duplicate, $3 = file to plant it in
    python3 - "$1" "$2" "$3" <<'PY'
import glob, os, re, sys
root, suite, target = sys.argv[1], sys.argv[2], sys.argv[3]
pat = re.compile(r"TEST(_F)?\(%s," % re.escape(suite))
hits = sum(bool(pat.search(open(f, encoding="utf-8").read()))
           for f in glob.glob(os.path.join(root, "tests/unit/*.cpp")))
if hits < 1:
    sys.exit("bucket-dup mutator: suite %s no longer exists in tests/unit — "
             "pick a suite that does" % suite)
path = os.path.join(root, target)
if not os.path.isfile(path):
    sys.exit("bucket-dup mutator: %s is gone — re-point the case at a file that exists" % target)
with open(path, "a", encoding="utf-8") as fh:
    fh.write("\n// planted by scripts/check-selftest.sh — collides with tests/unit\n"
             "TEST(%s, SelftestPlantedCollision) {}\n" % suite)
PY
}
break_bucket_dup_integration() { _break_bucket_dup "$1" AuthTest tests/integration/test_account_flow.cpp; }
break_bucket_dup_api() { _break_bucket_dup "$1" Base64Test tests/api/test_api_endpoints.cpp; }

# 7. A phantom release heading on top of the CHANGELOG: the baked
#    project(VERSION) is now stale and the gate must say so.
break_changelog_phantom_release() {
    python3 - "$1/CHANGELOG.md" <<'PY'
import sys
path = sys.argv[1]
anchor = "## [Unreleased]"
with open(path, encoding="utf-8") as fh:
    text = fh.read()
n = text.count(anchor)
if n != 1:
    sys.exit("break_changelog_phantom_release: expected exactly 1 %r heading in %s, found %d"
             % (anchor, path, n))
text = text.replace(anchor, anchor + "\n\n## [99.99.99] — 2099-01-01")
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 8. Every release heading loses its brackets. The parser now finds NOTHING —
#    and finding nothing must be a hard failure, not a shrugged-off pass:
#    "could not parse" is the difference between a gate and a no-op.
break_changelog_heading_format() {
    python3 - "$1/CHANGELOG.md" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
text, n = re.subn(r"(?m)^## \[(\d[0-9.]*)\]", r"## \1", text)
if n < 1:
    sys.exit("break_changelog_heading_format: de-bracketed %d release heading(s) "
             "in %s — expected at least 1" % (n, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 8b. One image pin left behind on the release bump — the failure mode that
#     once shipped GHCR pins for tags that never existed through two releases.
#     Flips the single pin of the prod worker release (deploy/values-worker-prod.yaml).
# The OpenAPI spec's info.version left behind by a release (it sat at 1.0.0
# for 26 releases before check-version-sync.sh learned to read it).
break_openapi_version_drift() {
    python3 - "$1/docs/openapi.yaml" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
text, n = re.subn(r"(?m)^(  version:[ \t]*)[0-9][0-9.]*$", r"\g<1>9.9.9", text)
if n != 1:
    sys.exit("break_openapi_version_drift: flipped %d info.version line(s) in %s — expected exactly 1"
             % (n, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

break_helm_tag_drift() {
    python3 - "$1/deploy/values-worker-prod.yaml" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
pat = re.compile(r'(?m)^([ \t]*tag:[ \t]*")[0-9][0-9.]*(")')
if len(pat.findall(text)) != 1:
    sys.exit("break_helm_tag_drift: expected exactly 1 pinned image tag in %s, found %d"
             % (path, len(pat.findall(text))))
text, n = pat.subn(r"\g<1>9.9.9\g<2>", text, count=1)
if n != 1:
    sys.exit("break_helm_tag_drift: flipped %d pin(s) in %s — expected exactly 1" % (n, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 8c. A Chart.yaml appVersion frozen at an old release — exactly how all four
#     charts sat on 1.4.0 while 1.5.x shipped (appVersion is the default image
#     tag for standalone installs and the app.kubernetes.io/version label).
break_chart_appversion_drift() {
    python3 - "$1/helm/life-os-cpp/Chart.yaml" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
text, n = re.subn(r'(?m)^(appVersion:[ \t]*")[^"]*(")', r"\g<1>0.0.1\g<2>", text)
if n != 1:
    sys.exit("break_chart_appversion_drift: rewrote %d appVersion line(s) in %s — "
             "expected exactly 1" % (n, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 9. A location added to the compose nginx.conf only — the drift that 404s
#    part of the public surface in exactly one environment.
break_nginx_location_drift() {
    python3 - "$1/frontend/nginx.conf" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
text, n = re.subn(r"(?m)^(\s*location = /healthz .*)$",
                  r"\1\n    location = /selftest-drift { return 404; }", text)
if n != 1:
    sys.exit("break_nginx_location_drift: inserted after %d healthz location(s) in %s "
             "— expected exactly 1" % (n, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 10. client_max_body_size raised in the compose copy only — uploads would
#     413 in exactly one environment.
break_nginx_body_cap() {
    python3 - "$1/frontend/nginx.conf" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
text, n = re.subn(r"client_max_body_size\s+[0-9]+[kKmMgG]?", "client_max_body_size 999m", text)
if n < 1:
    sys.exit("break_nginx_body_cap: no client_max_body_size directive in %s" % path)
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 11. The real prod values' rate limiter flipped to fail-open: a Redis blip
#     would silently disable the login brute-force throttle. Flips an explicit
#     rateLimit.failOpen in deploy/values-prod.yaml; if the key is absent
#     (inherited chart default), plants it under the top-level rateLimit block.
break_helm_fail_open() {
    python3 - "$1/deploy/values-prod.yaml" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
text, n = re.subn(r"(?m)^(\s+)failOpen: false\b", r"\1failOpen: true", text)
if n == 0 and re.search(r"(?m)^\s+failOpen:", text) is None:
    text, n = re.subn(r"(?m)^rateLimit:[ \t]*\n", "rateLimit:\n  failOpen: true\n", text)
if n != 1:
    sys.exit("break_helm_fail_open: set %d failOpen key(s) in %s — expected exactly 1"
             % (n, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 12. A working JWT secret committed into the umbrella's tracked values: the
#     credential lands in the rendered Secret's data, where the env-value
#     check cannot see it — the cyber-accountant e19985b incident class. The
#     planted string is a deliberately silly non-credential; the point is
#     that the key is non-empty at all.
break_helm_committed_jwt() {
    python3 - "$1/helm/cpp-env/values.yaml" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
# All three: credentials.jwtSecret plus the life-os-cpp / life-os-cpp-worker mirrors.
text, n = re.subn(r'(?m)^(\s*)jwtSecret: ""', r'\1jwtSecret: "planted-by-check-selftest"', text)
if n != 3:
    sys.exit("break_helm_committed_jwt: planted %d jwtSecret value(s) in %s — expected "
             "exactly 3 (credentials + life-os-cpp + life-os-cpp-worker mirrors)" % (n, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 12b. The production floor of check-helm-render.sh, one case per assertion
#      added in the wave-2 review. Each flips the exact key in the tracked
#      prod overlay (deploy/values-*prod.yaml) or the chart template that the
#      gate reads, so a case also breaks when that key moves.
break_helm_registration_open() {
    python3 - "$1/deploy/values-prod.yaml" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
text, n = re.subn(r"(?m)^registration:\n  open: false$", "registration:\n  open: true", text)
if n != 1:
    sys.exit("break_helm_registration_open: flipped %d registration.open key(s) in %s — expected exactly 1" % (n, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

break_helm_app_env_dev() {
    python3 - "$1/deploy/values-prod.yaml" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
if re.search(r"(?m)^app:\n(?:  .*\n)*?  env:", text):
    text, n = re.subn(r"(?m)^(app:\n(?:  .*\n)*?)  env: .*$", r"\1  env: development", text, count=1)
elif re.search(r"(?m)^app:$", text):
    # The prod values carry an app block (baseUrl, name): plant env inside it.
    text, n = re.subn(r"(?m)^app:$", "app:\n  env: development  # planted by scripts/check-selftest.sh", text, count=1)
else:
    text += "\n# planted by scripts/check-selftest.sh\napp:\n  env: development\n"
    n = 1
if n != 1:
    sys.exit("break_helm_app_env_dev: could not plant app.env in %s" % path)
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

break_helm_docs_enabled() {
    python3 - "$1/deploy/values-prod.yaml" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
text, n = re.subn(r"(?m)^docs:\n  enabled: false$", "docs:\n  enabled: true", text)
if n != 1:
    sys.exit("break_helm_docs_enabled: flipped %d docs.enabled key(s) in %s — expected exactly 1" % (n, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

break_helm_worker_lease_off() {
    python3 - "$1/deploy/values-worker-prod.yaml" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
text, n = re.subn(r"(?m)^(\s+visibilityTimeoutSec:\s*)[1-9][0-9]*$", r"\g<1>0", text)
if n != 1:
    sys.exit("break_helm_worker_lease_off: flipped %d visibilityTimeoutSec key(s) in %s — expected exactly 1" % (n, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

break_helm_automount_token() {
    python3 - "$1/helm/life-os-cpp/templates/deployment.yaml" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
text, n = re.subn(r"(?m)^\s*automountServiceAccountToken: false\n", "", text)
if n != 1:
    sys.exit("break_helm_automount_token: removed %d automountServiceAccountToken line(s) from %s — expected exactly 1" % (n, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

break_helm_networkpolicy_off() {
    python3 - "$1/deploy/values-prod.yaml" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
text, n = re.subn(r"(?m)^networkPolicy:\n  enabled: true$", "networkPolicy:\n  enabled: false", text)
if n != 1:
    sys.exit("break_helm_networkpolicy_off: flipped %d networkPolicy.enabled key(s) in %s — expected exactly 1" % (n, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 12c. prod-check.sh reads the same overlay through `helm template`; the docs
#      UI switched on is the planted breakage (same mutator as the helm
#      floor case, different gate and diagnostic).
break_prod_check_docs_on() { break_helm_docs_enabled "$1"; }

# 13. The webhooks -> email include edge resurrected — the exact edge whose
#     removal broke the email/jobs/webhooks cycle in the phase-1 hub split.
break_module_dep_cycle_edge() {
    python3 - "$1/src/webhooks/Webhooks.hpp" <<'PY'
import sys
path = sys.argv[1]
planted = '#include "email/Mailer.hpp"'
with open(path, encoding="utf-8") as fh:
    text = fh.read()
if planted in text:
    sys.exit("break_module_dep_cycle_edge: %s already includes email/Mailer.hpp — "
             "the healthy tree is not healthy" % path)
# Appended at EOF — never compiled, only grepped by the gate.
with open(path, "a", encoding="utf-8") as fh:
    fh.write("\n// planted by scripts/check-selftest.sh\n" + planted + "\n")
PY
}

# 14. A guard header quietly re-including the composition root — the include
#     that used to make every controller TU transitively pull Kafka/PayPal/
#     OTel through Guards.hpp -> Core.hpp.
break_module_dep_core_hub() {
    python3 - "$1/src/api/Guards.hpp" <<'PY'
import sys
path = sys.argv[1]
planted = '#include "core/Core.hpp"'
with open(path, encoding="utf-8") as fh:
    text = fh.read()
if planted in text:
    sys.exit("break_module_dep_core_hub: %s already includes core/Core.hpp — "
             "the healthy tree is not healthy" % path)
with open(path, "a", encoding="utf-8") as fh:
    fh.write("\n// planted by scripts/check-selftest.sh\n" + planted + "\n")
PY
}

# 14b. A cycle closed in the DECLARED graph: security -> api legalized in
#      docs/module-deps.txt (api -> security is real) — the shape the old
#      membership-only gate accepted for a year (email <-> jobs).
break_module_dep_declared_cycle() {
    python3 - "$1/docs/module-deps.txt" <<'PY'
import sys
path = sys.argv[1]
planted = "security -> api"
with open(path, encoding="utf-8") as fh:
    text = fh.read()
if planted in text:
    sys.exit("break_module_dep_declared_cycle: %s already declares %r — the healthy tree is not healthy"
             % (path, planted))
if "api -> security" not in text:
    sys.exit("break_module_dep_declared_cycle: %s no longer declares 'api -> security' — pick another"
             " edge whose reverse closes a cycle" % path)
with open(path, "a", encoding="utf-8") as fh:
    fh.write("\n# planted by scripts/check-selftest.sh\n" + planted + "\n")
PY
}

# 14c. The spec loses a whole path: every client of GET/POST /api/v1/jobs
#      breaks. The additive-only rule (docs/ARCHITECTURE.md §7) as a gate.
break_openapi_removed_path() {
    python3 - "$1/docs/openapi.yaml" <<'PY'
import sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    lines = fh.read().split("\n")
out, removed, skipping = [], 0, False
for line in lines:
    if line == "  /api/v1/jobs:":
        skipping, removed = True, removed + 1
        continue
    if skipping and line.startswith("  ") and not line.startswith("   ") and line.strip():
        skipping = False
    if not skipping:
        out.append(line)
if removed != 1:
    sys.exit("break_openapi_removed_path: removed %d '/api/v1/jobs:' path block(s) in %s — expected exactly 1"
             % (removed, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write("\n".join(out))
PY
}

# 14d. A required response property quietly becomes optional (Job.id): the
#      (method, path) gates see nothing, a typed client loses a guarantee.
break_openapi_optional_field() {
    python3 - "$1/docs/openapi.yaml" <<'PY'
import sys
path = sys.argv[1]
anchor = "    Job:\n      type: object\n      required: [id, type, status, created_at]\n"
with open(path, encoding="utf-8") as fh:
    text = fh.read()
n = text.count(anchor)
if n != 1:
    sys.exit("break_openapi_optional_field: expected exactly 1 Job schema anchor in %s, found %d" % (path, n))
text = text.replace(anchor, anchor.replace("[id, type, status, created_at]", "[type, status, created_at]"))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 14e. docs/openapi.yaml edited, tests/e2e/openapi.gen.json not regenerated:
#      the e2e schema checks would validate against the old spec.
break_openapi_gen_stale() {
    python3 - "$1/docs/openapi.yaml" <<'PY'
import sys
path = sys.argv[1]
anchor = "      description: W3C Trace Context header.\n"
with open(path, encoding="utf-8") as fh:
    text = fh.read()
n = text.count(anchor)
if n != 1:
    sys.exit("break_openapi_gen_stale: expected exactly 1 anchor line %r in %s, found %d" % (anchor, path, n))
text = text.replace(anchor, "      description: W3C Trace Context header (planted by check-selftest).\n")
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 15. A config knob read in code that config.json (and every other copy)
#     never heard of — the "added the key, forgot the skeletons and docs"
#     drift the config-sync gate exists to stop.
break_config_unregistered_key() {
    python3 - "$1/src/core/Core.cpp" <<'PY'
import sys
path = sys.argv[1]
planted = 'cfg.get<int>("selftest.planted_key", "SELFTEST_PLANTED_KEY", 1);'
with open(path, encoding="utf-8") as fh:
    text = fh.read()
if planted in text:
    sys.exit("break_config_unregistered_key: %s already contains the planted read" % path)
# Appended at EOF — never compiled, only parsed by the gate's extractor.
with open(path, "a", encoding="utf-8") as fh:
    fh.write("\n// planted by scripts/check-selftest.sh\n"
             "static void selftest_planted_(Config::AppConfig& cfg) { " + planted + " }\n")
PY
}

# 16. A documented env row deleted from CONFIG.md while the code still reads
#     the knob — the "full table" quietly stops being full.
break_config_doc_row_removed() {
    python3 - "$1/docs/CONFIG.md" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
text, n = re.subn(r"(?m)^\|\s*`MAIL_TIMEOUT_SEC`.*\n", "", text)
if n != 1:
    sys.exit("break_config_doc_row_removed: removed %d MAIL_TIMEOUT_SEC row(s) in %s "
             "— expected exactly 1" % (n, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 16c. An env the worker chart sets dropped from the compose worker service:
#      `make up` would run without a knob prod has (the review of 2026-10-18
#      found three such envs on the worker and twenty on the app).
break_config_compose_missing_env() {
    python3 - "$1/docker/docker-compose.yml" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
text, n = re.subn(r"(?m)^\s+JOBS_VISIBILITY_TIMEOUT_SEC:.*\n", "", text)
if n != 1:
    sys.exit("break_config_compose_missing_env: removed %d JOBS_VISIBILITY_TIMEOUT_SEC line(s) "
             "in %s — expected exactly 1" % (n, path))
with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

# 16b. The same knob read twice with two different literal defaults — the
#      worker.id drift (worker_main.cpp once read it in two places): the
#      gate must name the key AND both sites. The planted read uses the
#      literal-triple form against a site that reads the registry form
#      (Config::Keys::worker_concurrency), so both parsers are exercised.
break_config_default_mismatch() {
    python3 - "$1/src/core/Core.cpp" <<'PY'
import sys
path = sys.argv[1]
planted = 'cfg.get<int>("worker.concurrency", "WORKER_CONCURRENCY", 99);'
with open(path, encoding="utf-8") as fh:
    text = fh.read()
if planted in text:
    sys.exit("break_config_default_mismatch: %s already contains the planted read" % path)
# Appended at EOF — never compiled, only parsed by the gate's extractor.
with open(path, "a", encoding="utf-8") as fh:
    fh.write("\n// planted by scripts/check-selftest.sh\n"
             "static void selftest_planted_default_(Config::AppConfig& cfg) { " + planted + " }\n")
PY
}

# 17. A changelog fragment whose type is not a keep-a-changelog section —
#     assemble-changelog.sh --check must reject it BY NAME, or a typo'd
#     fragment silently never reaches the CHANGELOG at release time. The
#     mutator RENAMES the tracked README (renaming, not creating: the
#     fingerprint only watches files that existed before the mutation, so a
#     merely-added file would read as "mutator changed NOTHING").
break_changelog_fragment_type() {
    python3 - "$1" <<'PY'
import os, sys
root = sys.argv[1]
src = os.path.join(root, "changelog.d/README.md")
dst = os.path.join(root, "changelog.d/selftest-planted.badtype.md")
if not os.path.isfile(src):
    sys.exit("break_changelog_fragment_type: changelog.d/README.md is gone — "
             "re-point the mutator at a file that exists")
if os.path.exists(dst):
    sys.exit("break_changelog_fragment_type: %s already exists" % dst)
os.rename(src, dst)
PY
}

# --- run them ----------------------------------------------------------------
# Needles are LITERAL substrings of each gate's real diagnostics (grep -F).
# When a gate's wording changes, the needle changes in the same PR — that is
# the point: the selftest pins the gates to diagnostics a human can act on.

run_case registry-ghost-route check-openapi-drift.sh break_registry_ghost_route \
    "src/api/Endpoints.hpp docs/openapi.yaml" \
    'missing from openapi.yaml' \
    'get /api/v1/selftest-ghost'

run_case spec-method-flip check-openapi-drift.sh break_spec_method_flip \
    "src/api/Endpoints.hpp docs/openapi.yaml" \
    'missing from openapi.yaml' \
    'get /api/v1/jobs' \
    'not registered in Api::get_endpoints()' \
    'put /api/v1/jobs'

# check-openapi-breaking.sh diffs the copy against a baseline FILE here (the
# copies are not git trees): the untouched spec, so the control compares the
# spec with itself and the planted edit is the only difference.
if command -v oasdiff >/dev/null 2>&1; then
    export OPENAPI_BASE_SPEC="$REPO/docs/openapi.yaml"
    run_case openapi-breaking-removed-path check-openapi-breaking.sh break_openapi_removed_path \
        "docs/openapi.yaml" \
        'BREAKING change(s) in docs/openapi.yaml' \
        'api-path-removed-without-deprecation' \
        'GET /api/v1/jobs' \
        'additive-only'
    run_case openapi-breaking-optional-field check-openapi-breaking.sh break_openapi_optional_field \
        "docs/openapi.yaml" \
        'BREAKING change(s) in docs/openapi.yaml' \
        'response-property-became-optional' \
        'GET /api/v1/jobs'
    unset OPENAPI_BASE_SPEC
else
    skip_case openapi-breaking-removed-path 'oasdiff is not installed (version + sha256 in the Install oasdiff step of the openapi-drift CI job)'
    skip_case openapi-breaking-optional-field 'oasdiff is not installed (version + sha256 in the Install oasdiff step of the openapi-drift CI job)'
fi

if python3 -c 'import yaml' >/dev/null 2>&1; then
    run_case openapi-gen-stale check-openapi-gen-fresh.sh break_openapi_gen_stale \
        "docs/openapi.yaml tests/e2e/openapi.gen.json" \
        'tests/e2e/openapi.gen.json is STALE' \
        'run scripts/gen-openapi-json.sh'
else
    skip_case openapi-gen-stale 'python3 has no yaml module (pyyaml), the generator cannot run here'
fi

run_case rogue-controller-route check-routes-registered.sh break_rogue_controller_route \
    "src/api" \
    'MISSING from Api::get_endpoints() (Endpoints.hpp): GET /api/v1/selftest-ghost' \
    'Add the missing route(s)'

run_case unlinked-controller check-routes-registered.sh break_unlinked_controller \
    "src/api" \
    'not included by src/api/Api.hpp' \
    'api/TasksController.hpp'

run_case unversioned-registry-row check-routes-registered.sh break_unversioned_registry_row \
    "src/api" \
    'UNVERSIONED API route(s)' \
    '/api/selftest-unversioned'

run_case bucket-dup-integration check-test-buckets.sh break_bucket_dup_integration \
    "tests/unit tests/integration tests/api" \
    'appear in BOTH unit and integration buckets' \
    'AuthTest' \
    'Rename one of the conflicting suites.'

run_case bucket-dup-api check-test-buckets.sh break_bucket_dup_api \
    "tests/unit tests/integration tests/api" \
    'appear in BOTH unit and integration buckets' \
    'Base64Test'

# check-version-sync.sh reads these; every case copies all of them so the
# control run on the untouched copy passes.
VERSION_SYNC_PATHS="CMakeLists.txt CHANGELOG.md helm deploy project.env docs/openapi.yaml frontend/package.json frontend/package-lock.json"

run_case changelog-phantom-release check-version-sync.sh break_changelog_phantom_release \
    "$VERSION_SYNC_PATHS" \
    'version drift' \
    'newest release heading [99.99.99]'

run_case changelog-heading-format check-version-sync.sh break_changelog_heading_format \
    "$VERSION_SYNC_PATHS" \
    "could not parse a released '## [x.y.z]' heading"

run_case helm-tag-drift check-version-sync.sh break_helm_tag_drift \
    "$VERSION_SYNC_PATHS" \
    'helm image tag drift: deploy/values-worker-prod.yaml pins tag "9.9.9"' \
    'A stale pin deploys an old image'

run_case chart-appversion-drift check-version-sync.sh break_chart_appversion_drift \
    "$VERSION_SYNC_PATHS" \
    'appVersion drift: helm/life-os-cpp/Chart.yaml has appVersion "0.0.1"'

run_case openapi-version-drift check-version-sync.sh break_openapi_version_drift \
    "$VERSION_SYNC_PATHS" \
    'version drift: docs/openapi.yaml info.version is "9.9.9"'

run_case nginx-location-drift check-frontend-nginx-sync.sh break_nginx_location_drift \
    "frontend/nginx.conf helm/cpp-frontend/templates/configmap.yaml" \
    'location sets drifted' \
    'only in frontend/nginx.conf' \
    'location = /selftest-drift'

run_case nginx-body-cap check-frontend-nginx-sync.sh break_nginx_body_cap \
    "frontend/nginx.conf helm/cpp-frontend/templates/configmap.yaml" \
    'client_max_body_size drifted' \
    'will 413 in one environment only'

run_case helm-fail-open check-helm-render.sh break_helm_fail_open \
    "helm deploy" \
    'rate limiter is fail-OPEN' \
    'Set rateLimit.failOpen=false.'

run_case helm-committed-jwt check-helm-render.sh break_helm_committed_jwt \
    "helm deploy" \
    'rendered Secret carries a committed credential (jwt-secret)'

run_case helm-registration-open check-helm-render.sh break_helm_registration_open \
    "helm deploy" \
    'REGISTRATION_OPEN != false' \
    'Set registration.open=false.'

run_case helm-app-env-dev check-helm-render.sh break_helm_app_env_dev \
    "helm deploy" \
    'APP_ENV != production' \
    'Set app.env=production.'

run_case helm-docs-enabled check-helm-render.sh break_helm_docs_enabled \
    "helm deploy" \
    'docs.enabled != false' \
    'Set docs.enabled=false.'

run_case helm-worker-lease-off check-helm-render.sh break_helm_worker_lease_off \
    "helm deploy" \
    'JOBS_VISIBILITY_TIMEOUT_SEC is 0' \
    'Set jobs.visibilityTimeoutSec=300.'

run_case helm-automount-token check-helm-render.sh break_helm_automount_token \
    "helm deploy" \
    'without automountServiceAccountToken: false in the pod spec: life-os-cpp'

run_case helm-networkpolicy-off check-helm-render.sh break_helm_networkpolicy_off \
    "helm deploy" \
    'no NetworkPolicy rendered for the API pods' \
    'networkPolicy.enabled must stay true'

run_case prod-check-docs-on prod-check.sh break_prod_check_docs_on \
    "helm/life-os-cpp deploy/values-prod.yaml" \
    'docs.enabled must be false in production' \
    'prod-check FAILED'

# check-module-deps.sh reads these; the cycle allowlist may be absent once
# the last tolerated cycle is gone (git ls-files then copies nothing for it).
MODULE_DEPS_PATHS="src docs/module-deps.txt docs/module-cycles-allowlist.txt"

run_case module-dep-cycle-edge check-module-deps.sh break_module_dep_cycle_edge \
    "$MODULE_DEPS_PATHS" \
    'forbidden edge webhooks -> email' \
    'src/webhooks/Webhooks.hpp' \
    'utils/CurlInit.hpp'

run_case module-dep-core-hub check-module-deps.sh break_module_dep_core_hub \
    "$MODULE_DEPS_PATHS" \
    'forbidden include of the composition root: src/api/Guards.hpp includes "core/Core.hpp"' \
    'include "utils/Modules.hpp"'

run_case module-dep-declared-cycle check-module-deps.sh break_module_dep_declared_cycle \
    "$MODULE_DEPS_PATHS" \
    'include cycle between src/ directories' \
    'security -> api' \
    'docs/module-cycles-allowlist.txt'

run_case changelog-broken-fragment 'assemble-changelog.sh --check' break_changelog_fragment_type \
    "changelog.d" \
    'changelog.d/selftest-planted.badtype.md' \
    "unknown type 'badtype'"

CONFIG_SYNC_PATHS="src config/config.json config/config.sample.json docs/CONFIG.md docs/config-sync-allowlist.txt docker/docker-compose.yml helm/life-os-cpp/templates/deployment.yaml helm/life-os-cpp-worker/templates/deployment.yaml"

run_case config-unregistered-key check-config-sync.sh break_config_unregistered_key \
    "$CONFIG_SYNC_PATHS" \
    "config key 'selftest.planted_key'" \
    'missing from config/config.json' \
    'missing from config/config.sample.json' \
    "env 'SELFTEST_PLANTED_KEY'"

run_case config-stale-doc-row check-config-sync.sh break_config_doc_row_removed \
    "$CONFIG_SYNC_PATHS" \
    "env 'MAIL_TIMEOUT_SEC' is read in src/ but not documented in docs/CONFIG.md"

run_case config-default-mismatch check-config-sync.sh break_config_default_mismatch \
    "$CONFIG_SYNC_PATHS" \
    "default-mismatch: config key 'worker.concurrency' (env WORKER_CONCURRENCY)" \
    '2 at src/worker_main.cpp:' \
    '99 at src/core/Core.cpp:'

run_case config-compose-missing-env check-config-sync.sh break_config_compose_missing_env \
    "$CONFIG_SYNC_PATHS" \
    "sets env 'JOBS_VISIBILITY_TIMEOUT_SEC' but docker/docker-compose.yml service 'worker' does not"

# --- verdict -----------------------------------------------------------------

if [[ "$failures" -gt 0 ]]; then
    echo "check-selftest: $failures of $cases case(s) failed — the gates are NOT proven" >&2
    echo "  to catch anything. Fix this before trusting any green check-* run." >&2
    exit 1
fi
if [[ "$skipped" -gt 0 ]]; then
    echo "check-selftest: $cases deliberate breakages, all planted, all caught, all named; $skipped case(s) SKIPPED for a missing tool (see the SELFTEST SKIP lines) — not a full proof"
else
    echo "check-selftest: $cases deliberate breakages, all planted, all caught, all named"
fi
