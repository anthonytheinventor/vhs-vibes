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
for t in host_test mix_test v14_test nr_test tape_test v152_test mic_test agc_test deck_test levels_test; do
  echo "===== $t ====="
  if ! zig c++ -O2 -std=c++17 -pthread -I "$FRAMEWORK" "$HERE/$t.cpp" -o "$OUT/$t" 2> "$OUT/$t.log"; then
    grep -E "error" -A6 "$OUT/$t.log" | head -30
    status=1
    continue
  fi
  "$OUT/$t" || status=1
done

# The 1.4 comparison (see ab14_test.cpp): the same test program built against the 1.4.0 source,
# taken from git, and against this one; the first one's output goes straight into the second.
echo "===== ab14_test ====="
REPO="$HERE/.."
if ! git -C "$REPO" rev-parse -q --verify "v1.4.0^{commit}" > /dev/null 2>&1; then
  git -C "$REPO" fetch -q --tags origin 2> "$OUT/ab14_test.log"
fi
if ! git -C "$REPO" show "v1.4.0:src/VHSVibes.cpp" > "$OUT/VHSVibes_v140.cpp" 2>> "$OUT/ab14_test.log"; then
  echo "cannot get src/VHSVibes.cpp of v1.4.0 from git (see $OUT/ab14_test.log)"
  echo "FAIL"
  status=1
elif ! zig c++ -O2 -std=c++17 -pthread -I "$FRAMEWORK" "-DSRC=\"$OUT/VHSVibes_v140.cpp\"" "$HERE/ab14_test.cpp" -o "$OUT/ab14_v140" 2>> "$OUT/ab14_test.log" \
  || ! zig c++ -O2 -std=c++17 -pthread -I "$FRAMEWORK" "-DSRC=\"$HERE/../src/VHSVibes.cpp\"" "$HERE/ab14_test.cpp" -o "$OUT/ab14_test" 2>> "$OUT/ab14_test.log"; then
  grep -E "error" -A6 "$OUT/ab14_test.log" | head -30
  status=1
else
  "$OUT/ab14_v140" write - | "$OUT/ab14_test" compare - || status=1
fi
# Sat Type = Hi-Fi (1.7, see hifi_test.cpp). It is given a state saved by 1.6.0 with Sat Type on VHS:
# the same test program built against the 1.6.0 source, taken from git, writes it.
echo "===== hifi_test ====="
if ! git -C "$REPO" rev-parse -q --verify "v1.6.0^{commit}" > /dev/null 2>&1; then
  git -C "$REPO" fetch -q --tags origin 2> "$OUT/hifi_test.log"
fi
if ! git -C "$REPO" show "v1.6.0:src/VHSVibes.cpp" > "$OUT/VHSVibes_v160.cpp" 2>> "$OUT/hifi_test.log"; then
  echo "cannot get src/VHSVibes.cpp of v1.6.0 from git (see $OUT/hifi_test.log)"
  echo "FAIL"
  status=1
elif ! zig c++ -O2 -std=c++17 -pthread -I "$FRAMEWORK" -DWRITE160 "-DSRC=\"$OUT/VHSVibes_v160.cpp\"" "$HERE/hifi_test.cpp" -o "$OUT/hifi_write160" 2>> "$OUT/hifi_test.log" \
  || ! "$OUT/hifi_write160" "$OUT/state160.bin" \
  || ! zig c++ -O2 -std=c++17 -pthread -I "$FRAMEWORK" "$HERE/hifi_test.cpp" -o "$OUT/hifi_test" 2>> "$OUT/hifi_test.log"; then
  grep -E "error" -A6 "$OUT/hifi_test.log" | head -30
  echo "FAIL"
  status=1
else
  "$OUT/hifi_test" "$OUT/state160.bin" || status=1
fi
# Visual Vibes and the picture effects' meters (1.7, see visual_test.cpp); it loads the same 1.6.0 state.
echo "===== visual_test ====="
if ! zig c++ -O2 -std=c++17 -pthread -I "$FRAMEWORK" "$HERE/visual_test.cpp" -o "$OUT/visual_test" 2> "$OUT/visual_test.log"; then
  grep -E "error" -A6 "$OUT/visual_test.log" | head -30
  echo "FAIL"
  status=1
else
  "$OUT/visual_test" "$OUT/state160.bin" || status=1
fi
# Azimuth (1.7, see azimuth_test.cpp): it loads the same 1.6.0 state and one written by 1.4.0 (its source from the 1.4 comparison).
echo "===== azimuth_test ====="
if ! zig c++ -O2 -std=c++17 -pthread -I "$FRAMEWORK" -DWRITE_STATE "-DSRC=\"$OUT/VHSVibes_v140.cpp\"" "$HERE/azimuth_test.cpp" -o "$OUT/azimuth_write140" 2> "$OUT/azimuth_test.log"   || ! "$OUT/azimuth_write140" "$OUT/state140.bin"   || ! zig c++ -O2 -std=c++17 -pthread -I "$FRAMEWORK" "$HERE/azimuth_test.cpp" -o "$OUT/azimuth_test" 2>> "$OUT/azimuth_test.log"; then
  grep -E "error" -A6 "$OUT/azimuth_test.log" | head -30
  echo "FAIL"
  status=1
else
  "$OUT/azimuth_test" "$OUT/state160.bin" "$OUT/state140.bin" || status=1
fi
# host_test, mix_test, v14_test, nr_test, tape_test, v152_test, mic_test, agc_test, ab14_test, hifi_test, visual_test and azimuth_test end with PASS or FAIL;
# deck_test and levels_test print measurements to read.
exit $status
