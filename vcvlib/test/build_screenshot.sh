#!/usr/bin/env bash
# Build and run the OLED screenshot tool for one module, on the host compiler.
# No VCV Rack SDK required — fw_engine.cpp never includes rack.hpp.
#
# Usage (from the repo root):
#   vcvlib/test/build_screenshot.sh wea [-- tool args...]
#
# Prefer the Makefile wrapper, which knows the repo layout:
#   make screen-wea
#   make screen-wea ARGS="--ms 2100 --turn 3"
set -euo pipefail

APP="${1:-}"
if [ -z "$APP" ]; then
    echo "usage: $0 <app> [-- tool args...]" >&2
    exit 2
fi
shift
[ "${1:-}" = "--" ] && shift

cd "$(dirname "$0")/../.."   # -> repo root

PLUGIN="apps/$APP/vcv-plugin"
if [ ! -f "$PLUGIN/src/engine/fw_engine.cpp" ]; then
    echo "no VCV engine for app '$APP' ($PLUGIN/src/engine/fw_engine.cpp)" >&2
    exit 2
fi

# Each module's VcvEngine lives in its own namespace — the firmwares all define
# the same global names, so the Rack plugin keeps them apart that way. The tool
# is generic over forgevcv::IEngine and needs only the namespace to reach it, so
# read it out of the module's own header rather than keeping a table here that
# would go stale the day someone renames one.
NS=$(sed -n 's/^} \/\/ namespace \([a-z_][a-z0-9_]*\)$/\1/p' \
     "$PLUGIN/src/engine/fw_engine.hpp" | tail -n1)
if [ -z "$NS" ]; then
    echo "could not find the engine namespace in $PLUGIN/src/engine/fw_engine.hpp" >&2
    exit 2
fi

# Most modules wrap their engine in a VcvEngine (forgevcv::IEngine). ScopeForge
# predates that and exposes free functions plus a per-sample feedSample(), so the
# tool carries an adapter for each shape and picks by what the header actually
# declares — not by a hardcoded list of which module is which.
FREEFN=""
if ! grep -q 'class VcvEngine' "$PLUGIN/src/engine/fw_engine.hpp"; then
    FREEFN="-DFORGE_SCREENSHOT_FREEFN=1"
fi

mkdir -p "$PLUGIN/build"
OUT="$PLUGIN/build/screenshot"

CXX="${CXX:-g++}"
"$CXX" -std=c++17 -g -O0 \
    -DFORGE_SCREENSHOT_NS="$NS" $FREEFN \
    -I"$PLUGIN/src" -Ivcvlib/shim -Ivcvlib/include -I"apps/$APP/lib" -Icore \
    "$PLUGIN/src/engine/fw_engine.cpp" \
    vcvlib/test/screenshot.cpp \
    -o "$OUT"

exec "./$OUT" "$@"
