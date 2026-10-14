#!/usr/bin/env bash
#
# Scaffold a feature-module master switch end to end, the way the habits and
# notes modules are wired:
#
#   config/config.json + config.sample.json   {"<name>": {"enabled": "${<NAME>_ENABLED:-false}"}}
#   src/core/Modules.hpp                      Core::<name>_enabled()
#   docker/docker-compose.yml                 <NAME>_ENABLED on the app and worker services
#   helm/life-os-cpp                          values key + deployment env
#   helm/life-os-cpp-worker                   values key + deployment env
#   docs/CONFIG.md                            a row in the module switch table
#
# The switch ships OFF everywhere. Routes stay statically registered; every
# handler of the module opens with
#
#   API_MODULE_USER_GUARD(Core::<name>_enabled(), "<name>", req, callback, owner);
#
# (api/Guards.hpp): 404 "<name>" while the module is off, then 401 without an
# identity, then 403 no_user_account for a principal that is not a user.
# config/config.production.json and the deploy/ overlays are not touched:
# turn the module on there when it is ready.
#
# Safe to re-run: every patch is anchor-checked and skipped when present.
#
# Usage:
#   ./scripts/new-module.sh <name>       # lower snake_case, e.g. reading
set -euo pipefail

die() {
    echo "ERROR: $*" >&2
    exit 1
}

if [[ $# -ne 1 || "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    echo "Usage: $0 <name>   (lower snake_case, e.g. reading)" >&2
    exit 2
fi

NAME="$1"
[[ "$NAME" =~ ^[a-z][a-z0-9_]*$ ]] || die "module name must be lower snake_case [a-z0-9_], got '$NAME'"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# A module may not reuse a top-level config key: it would shadow that
# subsystem's settings.
if python3 - "$ROOT/config/config.json" "$NAME" <<'PY'; then
import json, sys
sys.exit(0 if sys.argv[2] in json.load(open(sys.argv[1], encoding="utf-8")) else 1)
PY
    if grep -qF "inline bool ${NAME}_enabled()" "$ROOT/src/core/Modules.hpp"; then
        echo "==> '${NAME}' is already a module; re-checking its wiring"
    else
        die "'$NAME' is already a top-level key in config/config.json — pick another module name"
    fi
fi

NAME_UPPER="$(printf '%s' "$NAME" | tr '[:lower:]' '[:upper:]')"
PASCAL=""
for part in ${NAME//_/ }; do
    first="$(printf '%s' "$part" | cut -c1 | tr '[:lower:]' '[:upper:]')"
    rest="$(printf '%s' "$part" | cut -c2-)"
    PASCAL+="${first}${rest}"
done

python3 - "$ROOT" "$NAME" "$NAME_UPPER" "$PASCAL" <<'PY'
import pathlib
import re
import sys

root, name, upper, pascal = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]


def die(msg):
    sys.exit(f"ERROR: {msg}")


def patch(rel, present, edit):
    path = root / rel
    if not path.exists():
        die(f"{rel} not found")
    text = path.read_text(encoding="utf-8")
    if present in text:
        print(f"==> {rel}: already wired, skipped")
        return
    new = edit(text)
    if new is None or present not in new:
        die(f"{rel}: the anchor is gone; wire {upper}_ENABLED there by hand")
    path.write_text(new, encoding="utf-8")
    print(f"==> {rel}: patched")


def before(anchor, block, count=1):
    def edit(text):
        if anchor not in text:
            return None
        return text.replace(anchor, block + anchor, count)
    return edit


def before_each_line(pattern, make):
    rx = re.compile(pattern, re.M)

    def edit(text):
        if not rx.search(text):
            return None
        return rx.sub(lambda m: make(m.group("ind")) + m.group(0), text)
    return edit


def before_yaml_block(heading, block):
    """Insert @p block before the '# ---' separator that opens @p heading."""
    def edit(text):
        at = text.find("\n" + heading)
        if at < 0:
            return None
        sep = text.rfind("\n# ---", 0, at)
        if sep < 0:
            return None
        return text[: sep + 1] + block + text[sep + 1 :]
    return edit


# 1. config skeletons: before the worker block.
for rel in ("config/config.json", "config/config.sample.json"):
    patch(rel, f'"{name}": {{',
          before('  "worker": {', f'  "{name}": {{\n    "enabled": "${{{upper}_ENABLED:-false}}"\n  }},\n'))

# 2. Core::<name>_enabled() at the end of core/Modules.hpp.
patch("src/core/Modules.hpp", f"inline bool {name}_enabled()",
      before("}  // namespace Core",
             f"/// {pascal} module master switch — same pattern as notes_enabled().\n"
             f"inline bool {name}_enabled() {{\n"
             f"    if (!Config::is_initialized())\n"
             f"        return false;\n"
             f'    return Config::get().get<bool>("{name}.enabled", "{upper}_ENABLED", false);\n'
             f"}}\n\n"))

# 3. docker-compose: the app and the worker service, beside the other switches.
patch("docker/docker-compose.yml", f"{upper}_ENABLED:",
      before_each_line(r"^(?P<ind>[ ]+)REGISTRATION_OPEN: \$\{REGISTRATION_OPEN:-true\}$",
                       lambda ind: f"{ind}{upper}_ENABLED: ${{{upper}_ENABLED:-false}}\n"))

# 4. Both charts: the values key and the deployment env.
values_block = ("# ---------------------------------------------------------------\n"
                f"# {name} module. Env var set: {upper}_ENABLED\n"
                "# ---------------------------------------------------------------\n"
                f"{name}:\n  enabled: false\n\n")
for chart in ("life-os-cpp", "life-os-cpp-worker"):
    patch(f"helm/{chart}/values.yaml", f"\n{name}:\n", before_yaml_block("# Self sign-up", values_block))
    patch(f"helm/{chart}/templates/deployment.yaml", f"name: {upper}_ENABLED",
          before_each_line(r"^(?P<ind>[ ]+)# self sign-up: false = invite-only$",
                           lambda ind: f"{ind}# {name} module — master switch\n"
                                       f"{ind}- name: {upper}_ENABLED\n"
                                       f"{ind}  value: {{{{ .Values.{name}.enabled | quote }}}}\n"))

# 5. docs/CONFIG.md: after the last module switch row before the mail section.
def config_row(text):
    mail = text.find("## Mail (SMTP)")
    if mail < 0:
        return None
    rows = [m for m in re.finditer(r"^\| `[A-Z_]+_ENABLED` \|[^\n]*Master switch for the [^\n]*\n", text[:mail], re.M)]
    if not rows:
        return None
    end = rows[-1].end()
    row = (f"| `{upper}_ENABLED` | `{name}.enabled` | bool | `false` | Master switch for the {name} module "
           f"(`Core::{name}_enabled()`). Routes stay registered, handlers 404 while off. |\n")
    return text[:end] + row + text[end:]


patch("docs/CONFIG.md", f"`{upper}_ENABLED`", config_row)
PY

cat <<EOF

==> Module '${NAME}' switched in (off everywhere). Next steps:
  1. Endpoints: ./scripts/new-endpoint.sh ${PASCAL}Controller Get /api/v1/${NAME} --with-test
  2. First line of every handler of the module (api/Guards.hpp):
         API_MODULE_USER_GUARD(Core::${NAME}_enabled(), "${NAME}", req, callback, owner);
     Request bodies: parse_object(req, body, callback); deletes: no_content(callback)
     (api/HandlerSupport.hpp).
  3. Enable locally: ${NAME_UPPER}_ENABLED=true make up
     Enable on prod: ${NAME}.enabled: true in deploy/values-prod.yaml (and
     deploy/values-worker-prod.yaml if the worker runs its jobs).
  4. Run ./scripts/check-config-sync.sh and ./scripts/check-helm-render.sh.
EOF
