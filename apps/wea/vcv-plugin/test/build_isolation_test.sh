#!/usr/bin/env bash
# Build and run the wvengine two-instance isolation test on the host compiler.
# No VCV Rack SDK required — fw_engine.cpp never includes rack.hpp.
#
# Usage (from vcv-plugin/):  test/build_isolation_test.sh
set -euo pipefail

cd "$(dirname "$0")/.."   # -> vcv-plugin/
mkdir -p build

# The shim and forgevcv headers live in vcvlib/ at the root of this monorepo.
# Override FORGEVCV to point elsewhere.
FORGEVCV="${FORGEVCV:-../../../vcvlib}"

CXX="${CXX:-g++}"
"$CXX" -std=c++17 -g -O0 \
    -Isrc -I"$FORGEVCV/shim" -I"$FORGEVCV/include" -I../lib -I../../../core \
    src/engine/fw_engine.cpp \
    test/isolation_test.cpp \
    -o build/isolation_test

echo "── running ─────────────────────────────────────────────"
exec ./build/isolation_test
