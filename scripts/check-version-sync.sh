#!/usr/bin/env bash
# Verify every baked/pinned application version matches the newest RELEASED
# CHANGELOG heading.
#
# project(VERSION ...) in CMakeLists.txt renders into generated/version.hpp and
# is what Core::version(), GET /health and GET / report. Nothing compared it to
# anything, so it sat at 1.4.0 while three releases (1.5.x) shipped. Release
# builds override it from the git tag (docker/Dockerfile ARG APP_VERSION ->
# -DPROJECT_VERSION_OVERRIDE), but that leaves every non-tag build — including
# the runtime images CI smoke-tests — reporting the baked value, so the baked
# value has to stay honest.
#
# The helm side drifts the same way and is checked the same way:
#   - helm/cpp-env/values.yaml pins the api/worker/frontend image tags (3 pins).
#     A since-removed stage overlay sat on v1.4.0 pins — GHCR tags that never
#     existed — through two releases before #29 caught it.
#   - deploy/values-prod.yaml, deploy/values-worker-prod.yaml and
#     deploy/values-frontend-prod.yaml are what prod actually runs (one image
#     tag each); a stale pin there deploys the previous release.
#   - helm/*/Chart.yaml appVersion is the default image tag for a standalone
#     chart install and the app.kubernetes.io/version label; it sat at 1.4.0
#     while 1.5.x shipped.
#
# Everything is compared against the newest RELEASED heading — '## [Unreleased]'
# is skipped, so a master that carries unreleased work on top of the last
# release stays green. scripts/release.sh bumps all of these in one command.
set -euo pipefail

REPO="${REPO_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"

cmake_version="$(sed -n 's/^project(.*VERSION[[:space:]]\{1,\}\([0-9][0-9.]*\).*/\1/p' \
    "$REPO/CMakeLists.txt" | head -1)"
changelog_version="$(sed -n 's/^## \[\([0-9][0-9.]*\)\].*/\1/p' "$REPO/CHANGELOG.md" | head -1)"

if [ -z "$cmake_version" ]; then
    echo "✗ could not parse project(... VERSION x.y.z ...) from CMakeLists.txt" >&2
    exit 1
fi
if [ -z "$changelog_version" ]; then
    echo "✗ could not parse a released '## [x.y.z]' heading from CHANGELOG.md" >&2
    exit 1
fi

fail=0

if [ "$cmake_version" != "$changelog_version" ]; then
    {
        echo "✗ version drift:"
        echo "    CMakeLists.txt  project(VERSION $cmake_version)"
        echo "    CHANGELOG.md    newest release heading [$changelog_version]"
        echo ""
        echo "Bump project(VERSION ...) in CMakeLists.txt to $changelog_version in the"
        echo "same commit as the changelog entry. Leave vcpkg.json's version alone:"
        echo "the Dockerfile keys the whole dependency-install layer on that"
        echo "manifest, so touching it rebuilds ~29 packages from source and blows"
        echo "the CI job timeouts (downstream bump: tarassov.me 044fa73)."
    } >&2
    fail=1
fi

# --- helm image-tag pins (cpp-env umbrella + prod values) --------------------
# Each entry is <path>:<expected pin count>. The umbrella pins 3 app image tags
# (life-os-cpp, life-os-cpp-worker, cpp-frontend); each prod values file under
# deploy/ pins the one image of its release. The count is asserted so a
# restructure that stops the parser from matching fails loudly instead of
# degrading into a green no-op.
pin_total=0
for entry in helm/cpp-env/values.yaml:3 deploy/values-prod.yaml:1 \
    deploy/values-worker-prod.yaml:1 deploy/values-frontend-prod.yaml:1; do
    path="${entry%:*}"
    want="${entry##*:}"
    pin_total=$((pin_total + want))
    if [ ! -f "$REPO/$path" ]; then
        echo "✗ $path is gone — update the pin list in this script" >&2
        fail=1
        continue
    fi
    tags="$(sed -n 's/^[[:space:]]*tag:[[:space:]]*"\([0-9][0-9.]*\)".*/\1/p' "$REPO/$path")"
    count=0
    [ -n "$tags" ] && count="$(printf '%s\n' "$tags" | wc -l | tr -d ' ')"
    if [ "$count" -ne "$want" ]; then
        echo "✗ $path: expected exactly $want pinned image tag(s), parsed $count —" >&2
        echo "    the file layout changed under the parser; fix the pins or" >&2
        echo "    re-anchor this check." >&2
        fail=1
        continue
    fi
    while IFS= read -r tag; do
        if [ "$tag" != "$changelog_version" ]; then
            {
                echo "✗ helm image tag drift: $path pins tag \"$tag\""
                echo "    but the newest CHANGELOG release heading is [$changelog_version]."
                echo "    A stale pin deploys an old image (or one that never existed:"
                echo "    a stage overlay once carried v1.4.0 GHCR tags for two releases)."
            } >&2
            fail=1
        fi
    done <<EOF_TAGS
$tags
EOF_TAGS
done

# --- Chart.yaml appVersion ---------------------------------------------------
# Default image tag for standalone chart installs and the source of the
# app.kubernetes.io/version label on every rendered object.
for chart in life-os-cpp life-os-cpp-worker cpp-frontend cpp-env; do
    path="helm/$chart/Chart.yaml"
    app_version="$(sed -n 's/^appVersion:[[:space:]]*"\([^"]*\)".*/\1/p' "$REPO/$path" | head -1)"
    if [ -z "$app_version" ]; then
        echo "✗ $path: could not parse appVersion — expected appVersion: \"x.y.z\"" >&2
        fail=1
    elif [ "$app_version" != "$changelog_version" ]; then
        {
            echo "✗ appVersion drift: $path has appVersion \"$app_version\""
            echo "    but the newest CHANGELOG release heading is [$changelog_version]."
        } >&2
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    {
        echo ""
        echo "scripts/release.sh <version> bumps every one of these points in one"
        echo "command (CHANGELOG heading content stays human-written)."
    } >&2
    exit 1
fi

echo "✓ version in sync: $cmake_version (CMakeLists.txt == newest CHANGELOG heading" \
    "== $pin_total helm image-tag pins == 4 Chart.yaml appVersions)"
