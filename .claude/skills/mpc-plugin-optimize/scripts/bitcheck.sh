#!/bin/bash
# bitcheck.sh <sdk-dir> <old-plugin.cpp> <new-plugin.cpp> [out-dir]
#
# Proves, or disproves, that two versions of a plugin produce the same output bits, and when they do
# not, measures how large the difference is. It builds render.cpp around each source as a static
# arm32 program (the same build as count.sh), runs both under qemu-arm and compares the output files
# byte for byte. The run visits every program, seven block sizes, random parameters, every parameter
# at once, many settled random settings and bad input values. It then does the same for arm64 when
# qemu-aarch64 is installed.
#   old-plugin.cpp   the untouched copy of the source kept in step 1 (its folder holds its headers)
#   new-plugin.cpp   the changed source
# Exit code 0 = identical on every target run, 1 = a difference, 2 = could not run.
# On a difference it prints the first differing byte with its phase, then a table from outdiff.cpp:
# per phase, the level of the output, the RMS and peak of the difference in dB, and how far the
# difference is below the output. Set MPC_OPT_HOLD=<seconds> to add long held programs to the run;
# the table then shows the difference early and late in each, which is how an error that grows in a
# feedback path is seen.
# A pass covers what the run visits. A plugin with behavior the run cannot reach (tempo, MIDI, side
# inputs, a sample rate other than 44.1 kHz) needs its own cases added to a copy of render.cpp.
# Check that the comparison can fail: change one constant in a copy of the source and run it once.
# Needs: zig (or ZIG="python3 -m ziglang"), qemu-arm, cmp. Linux or WSL.
set -uo pipefail
[ $# -ge 3 ] || { sed -n '2,21p' "$0"; exit 2; }
SDK=$1; OLD=$2; NEW=$3; OUT=${4:-mpc-opt-out}
HERE=$(cd "$(dirname "$0")" && pwd)
ZIG=${ZIG:-zig}
mkdir -p "$OUT/bits"
RESULT=0
run_target() {  # name, zig target, extra flags, emulator
  local T=$1 TRIPLE=$2 FLAGS=$3 EMU=$4 W
  for W in old new; do
    local SRC=$OLD; [ "$W" = new ] && SRC=$NEW
    $ZIG c++ -O2 -std=c++17 -pthread -target "$TRIPLE" $FLAGS -static \
      -I"$SDK/framework" -I"$(dirname "$SRC")" "-DSRC=\"$(basename "$SRC")\"" "$HERE/render.cpp" \
      -o "$OUT/bits/render-$T-$W" || { echo "$T: build of the $W source failed"; RESULT=2; return; }
    $EMU "$OUT/bits/render-$T-$W" "$OUT/bits/$T-$W.bin" > "$OUT/bits/$T-$W.log" \
      || { echo "$T: run of the $W build failed"; RESULT=2; return; }
  done
  if cmp -s "$OUT/bits/$T-old.bin" "$OUT/bits/$T-new.bin"; then
    echo "$T: IDENTICAL, $(stat -c %s "$OUT/bits/$T-old.bin") bytes ($(tail -1 "$OUT/bits/$T-old.log"))"
  else
    local AT
    AT=$(cmp "$OUT/bits/$T-old.bin" "$OUT/bits/$T-new.bin" | sed 's/.*byte \([0-9]*\).*/\1/')
    echo "$T: DIFFERENT, first at byte ${AT:-?}"
    awk -v at="${AT:-0}" '$1 == "phase" && $NF + 0 < at { p = $0 } END { if (p != "") print "  in " p }' "$OUT/bits/$T-old.log"
    if [ -x "$OUT/bits/outdiff" ] || $ZIG c++ -O2 -std=c++17 "$HERE/outdiff.cpp" -o "$OUT/bits/outdiff"; then
      "$OUT/bits/outdiff" "$OUT/bits/$T-old.bin" "$OUT/bits/$T-new.bin" "$OUT/bits/$T-old.log" | tee "$OUT/bits/$T-diff.txt"
    else
      echo "  could not build outdiff.cpp, so the difference was not measured"
    fi
    [ "$RESULT" = 2 ] || RESULT=1
  fi
}
run_target arm32 arm-linux-musleabihf "-mcpu=cortex_a17" qemu-arm
if command -v qemu-aarch64 > /dev/null; then
  run_target arm64 aarch64-linux-musl "" qemu-aarch64
else
  echo "arm64: skipped (qemu-aarch64 not installed)"
fi
exit "$RESULT"
