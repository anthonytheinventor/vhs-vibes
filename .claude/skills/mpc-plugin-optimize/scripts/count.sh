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
# Needs: zig (or ZIG="python3 -m ziglang"), qemu-arm 8.1 or later, nm, awk. Linux or WSL.
# The result is an instruction count, exact and repeatable. It is a proxy for time on the device.
set -euo pipefail
[ $# -ge 2 ] || { sed -n '2,12p' "$0"; exit 2; }
SDK=$1; SRC=$2; K=${3:-0}; BLOCKS=${4:-}; OUT=${5:-mpc-opt-out}
[ -n "$BLOCKS" ] || { if [ "$K" -gt 0 ]; then BLOCKS=1000; else BLOCKS=400; fi; }
HERE=$(cd "$(dirname "$0")" && pwd)
ZIG=${ZIG:-zig}
mkdir -p "$OUT"
rm -f "$OUT/counts.stamp"
$ZIG c++ -O2 -std=c++17 -pthread -target arm-linux-musleabihf -mcpu=cortex_a17 -static \
  -I"$SDK/framework" -I"$(dirname "$SRC")" "-DSRC=\"$(basename "$SRC")\"" "$HERE/drive.cpp" -o "$OUT/drive"
M=$(nm "$OUT/drive" | awk '$3 == "icMark" { print $1 }')
E=$(nm "$OUT/drive" | awk '$3 == "icEnd" { print $1 }')
[ -n "$M" ] && [ -n "$E" ] || { echo "icMark/icEnd not found in the binary"; exit 1; }
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
        printf "\n" }' | tee "$OUT/summary_k$K.txt"
# Lets profile.sh reuse this build and counts.txt while k, the block count and the sources are unchanged.
echo "$K $BLOCKS $(cat "$(dirname "$SRC")"/*.[ch]* "$HERE/drive.cpp" | sha256sum | cut -c1-16)" > "$OUT/counts.stamp"
