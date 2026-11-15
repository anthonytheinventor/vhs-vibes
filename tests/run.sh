#!/bin/bash
# Builds and runs the VHS Vibes test programs on this computer (Linux or WSL, needs zig on PATH).
# Run from anywhere:  bash "plugins/VHS Vibes/tests/run.sh"
# The project must sit in the MPC Plugin SDK at plugins/VHS Vibes (the tests use its framework/).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
FRAMEWORK="$HERE/../../../framework"
OUT="${TMPDIR:-/tmp}/vhs-vibes-tests"
mkdir -p "$OUT"
if [ ! -f "$FRAMEWORK/mpc_plugin.h" ]; then
  echo "run.sh: cannot find the SDK framework at $FRAMEWORK" >&2
  exit 2
fi
status=0
for t in host_test mix_test v14_test nr_test tape_test v152_test deck_test levels_test; do
  echo "===== $t ====="
  if ! zig c++ -O2 -std=c++17 -pthread -I "$FRAMEWORK" "$HERE/$t.cpp" -o "$OUT/$t" 2> "$OUT/$t.log"; then
    grep -E "error" -A6 "$OUT/$t.log" | head -30
    status=1
    continue
  fi
  "$OUT/$t" || status=1
done
# host_test, mix_test, v14_test, nr_test, tape_test and v152_test end with PASS or FAIL; deck_test and levels_test print measurements to read.
exit $status
