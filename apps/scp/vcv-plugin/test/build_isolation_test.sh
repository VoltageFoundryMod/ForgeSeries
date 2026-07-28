#!/usr/bin/env bash
# Build and run the fvengine two-instance isolation + render test on the host
# compiler. No VCV Rack SDK required — fw_engine.cpp never includes rack.hpp.
#
# Usage (from vcv-plugin/):  test/build_isolation_test.sh
set -euo pipefail

cd "$(dirname "$0")/.." # -> vcv-plugin/
mkdir -p build

# The shim and forgevcv headers live in the shared library (a sibling repo /
# submodule). Override FORGEVCV to point elsewhere.
FORGEVCV="${FORGEVCV:-../../ForgeSeries-VCVLib}"

CXX="${CXX:-g++}"
"$CXX" -std=c++17 -g -O0 \
    -Isrc -I"$FORGEVCV/shim" -I"$FORGEVCV/include" -I../lib \
    src/engine/fw_engine.cpp \
    test/isolation_test.cpp \
    -o build/isolation_test

echo "── running ─────────────────────────────────────────────"
exec ./build/isolation_test
