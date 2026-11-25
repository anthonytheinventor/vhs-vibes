#!/bin/bash
# count.sh <sdk-dir> <plugin.cpp> [k] [blocks] [out-dir]
#
# Builds drive.cpp around the plugin's source as a static arm32 program (Cortex-A17, -O2, the SDK's
# optimization level) and counts the instructions each block executes under qemu-arm.
#   k        parameters set to random values before each block (default 0 = steady state)
#   blocks   blocks to run (default 400 in steady state, 1000 with k above 0; minimum 300). The first
#            200 are warm-up and are left out of the summary. A heavy plugin with 1000 blocks takes
#            several minutes.
#   out-dir  where drive, counts.txt and the summary go (default ./mpc-opt-out)
# Set MPC_OPT_EVENTS to an events file (format at the top of drive.cpp) to force settings, program
# changes or mode switches at given blocks, e.g. to count one mode in steady state.
# MPC_OPT_MARK=all counts the whole host block (events and parameter writes too), MPC_OPT_SKIP names
# parameters the random sweep leaves alone; both are explained at the top of drive.cpp.
# A plugin that is not one SDK source file: build drive.cpp yourself with the compiler and flags of
# the release (static, arm32) and name the program in DRIVE_BIN; <sdk-dir> and <plugin.cpp> are then
# not used (give - for each).
# Needs: zig (or ZIG="python3 -m ziglang"), qemu-arm 8.1 or later, nm, awk. Linux or WSL.
# The result is an instruction count, exact and repeatable. It is a proxy for time on the device.
set -euo pipefail
[ $# -ge 2 ] || { sed -n '2,19p' "$0"; exit 2; }
SDK=$1; SRC=$2; K=${3:-0}; BLOCKS=${4:-}; OUT=${5:-mpc-opt-out}
[ -n "$BLOCKS" ] || { if [ "$K" -gt 0 ]; then BLOCKS=1000; else BLOCKS=400; fi; }
HERE=$(cd "$(dirname "$0")" && pwd)
ZIG=${ZIG:-zig}
mkdir -p "$OUT"
TAG="k$K"; [ -z "${MPC_OPT_EVENTS:-}" ] || TAG="${TAG}_$(basename "${MPC_OPT_EVENTS%.*}")"
rm -f "$OUT/counts.stamp"
if [ -n "${DRIVE_BIN:-}" ]; then
  cp "$DRIVE_BIN" "$OUT/drive"
else
  $ZIG c++ -O2 -std=c++17 -pthread -target arm-linux-musleabihf -mcpu=cortex_a17 -static \
    -I"$SDK/framework" -I"$(dirname "$SRC")" "-DSRC=\"$(basename "$SRC")\"" "$HERE/drive.cpp" -o "$OUT/drive"
fi
M=$(nm "$OUT/drive" | awk '$3 == "icMark" { print $1 }')
E=$(nm "$OUT/drive" | awk '$3 == "icEnd" { print $1 }')
[ -n "$M" ] && [ -n "$E" ] || { echo "icMark/icEnd not found in the binary"; exit 1; }
# A Thumb function's symbol has bit 0 set; the trace shows the even address.
M=$(printf '%08x' $(( 0x$M & ~1 ))); E=$(printf '%08x' $(( 0x$E & ~1 )))
qemu-arm -one-insn-per-tb -d exec,nochain -D /dev/stdout "$OUT/drive" "$K" "$BLOCKS" 2>/dev/null \
  | LC_ALL=C grep -F "Trace 0:" | LC_ALL=C grep -n -e "/00000000$M/" -e "/00000000$E/" | cut -d: -f1 \
  | awk 'NR % 2 == 1 { p = $1 } NR % 2 == 0 { print (NR - 2) / 2, $1 - p - 1 }' > "$OUT/counts.txt"
sort -k2 -n "$OUT/counts.txt" | awk -v k="$K" '
  $1 >= 200 { v[n++] = $2; if ($2 >= mx) { mx = $2; mb = $1 } }
  END { if (n == 0) { print "no blocks counted"; exit 1 }
        printf "k=%d: median %.1f instructions per frame, largest block %d (block %d, %.1f per frame), %d blocks from block 200\n",
               k, v[int(n / 2)] / 128, mx, mb, mx / 128, n
        printf "      per frame: 90th percentile %.1f, 99th percentile %.1f", v[int(n * 0.90)] / 128, v[int(n * 0.99)] / 128
        if (n < 500) printf " (only %d blocks: the 99th percentile is rough)", n
        printf "\n" }' | tee "$OUT/summary_$TAG.txt"
[ -z "${MPC_OPT_EVENTS:-}" ] || { echo "events applied:"; sed 's/^/  /' "$MPC_OPT_EVENTS.applied"; }
# Lets profile.sh reuse this build and counts.txt while k, the block count and the sources are unchanged.
echo "$K $BLOCKS ${MPC_OPT_MARK:-} ${MPC_OPT_SKIP:-} $(cat ${DRIVE_BIN:+"$DRIVE_BIN"} ${DRIVE_BIN:-"$(dirname "$SRC")"/*.[ch]*} "$HERE/drive.cpp" "${MPC_OPT_EVENTS:-/dev/null}" | sha256sum | cut -c1-16)" > "$OUT/counts.stamp"
