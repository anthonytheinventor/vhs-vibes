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
for t in host_test mix_test v14_test nr_test tape_test v152_test mic_test agc_test hq_test deck_test levels_test; do
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
# The read-outs that changed unit in 1.8 (see readout_test.cpp): the same test program built against the
# 1.7.0 source, taken from git, writes the six parameters, their values and saved states, and every other
# read-out; this one must give the same, then checks the new read-outs.
echo "===== readout_test ====="
if ! git -C "$REPO" rev-parse -q --verify "v1.7.0^{commit}" > /dev/null 2>&1; then
  git -C "$REPO" fetch -q --tags origin 2> "$OUT/readout_test.log"
fi
if ! git -C "$REPO" show "v1.7.0:src/VHSVibes.cpp" > "$OUT/VHSVibes_v170.cpp" 2>> "$OUT/readout_test.log"; then
  echo "cannot get src/VHSVibes.cpp of v1.7.0 from git (see $OUT/readout_test.log)"
  echo "FAIL"
  status=1
elif ! zig c++ -O2 -std=c++17 -pthread -I "$FRAMEWORK" -DWRITE170 "-DSRC=\"$OUT/VHSVibes_v170.cpp\"" "$HERE/readout_test.cpp" -o "$OUT/readout_write170" 2>> "$OUT/readout_test.log" \
  || ! "$OUT/readout_write170" "$OUT/readout170.bin" "$OUT/readout170.txt" > /dev/null \
  || ! zig c++ -O2 -std=c++17 -pthread -I "$FRAMEWORK" "$HERE/readout_test.cpp" -o "$OUT/readout_test" 2>> "$OUT/readout_test.log"; then
  grep -E "error" -A6 "$OUT/readout_test.log" | head -30
  echo "FAIL"
  status=1
else
  "$OUT/readout_test" "$OUT/readout170.bin" "$OUT/readout170.txt" || status=1
fi
# The B-Type NR clip level of 1.8, carried by the state (see nrclip_test.cpp): this source saves a
# state, the same test program built against the 1.7.0 source (taken from git above) loads it, then
# writes a state and three renders of its own, which this source must load and match.
echo "===== nrclip_test ====="
if [ ! -s "$OUT/VHSVibes_v170.cpp" ]; then
  echo "no 1.7.0 source (see readout_test above)"
  echo "FAIL"
  status=1
elif ! zig c++ -O2 -std=c++17 -pthread -I "$FRAMEWORK" "$HERE/nrclip_test.cpp" -o "$OUT/nrclip_test" 2> "$OUT/nrclip_test.log" \
  || ! zig c++ -O2 -std=c++17 -pthread -I "$FRAMEWORK" -DWRITE170 "-DSRC=\"$OUT/VHSVibes_v170.cpp\"" "$HERE/nrclip_test.cpp" -o "$OUT/nrclip_write170" 2>> "$OUT/nrclip_test.log" \
  || ! "$OUT/nrclip_test" save "$OUT/nrclip18.bin" \
  || ! "$OUT/nrclip_write170" "$OUT/nrclip18.bin" "$OUT/nrclip170.bin" "$OUT/nrclip170.hash"; then
  grep -E "error" -A6 "$OUT/nrclip_test.log" | head -30
  echo "FAIL"
  status=1
else
  "$OUT/nrclip_test" "$OUT/nrclip170.bin" "$OUT/nrclip170.hash" || status=1
fi
# Hysteresis (1.8, see hyst_test.cpp): it loads the states saved by 1.6.0, 1.4.0 and 1.7.0 above.
echo "===== hyst_test ====="
if ! zig c++ -O2 -std=c++17 -pthread -I "$FRAMEWORK" "$HERE/hyst_test.cpp" -o "$OUT/hyst_test" 2> "$OUT/hyst_test.log"; then
  grep -E "error" -A6 "$OUT/hyst_test.log" | head -30
  echo "FAIL"
  status=1
else
  olds=""
  for s in state160.bin state140.bin nrclip170.bin; do [ -s "$OUT/$s" ] && olds="$olds $OUT/$s"; done
  "$OUT/hyst_test" $olds || status=1
fi
# host_test, mix_test, v14_test, nr_test, tape_test, v152_test, mic_test, agc_test, hq_test, ab14_test, hifi_test, visual_test, azimuth_test, readout_test, nrclip_test and hyst_test end with PASS or FAIL;
# deck_test and levels_test print measurements to read.
exit $status
