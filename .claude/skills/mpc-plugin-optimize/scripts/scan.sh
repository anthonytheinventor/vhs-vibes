#!/bin/bash
# scan.sh <sdk-dir> <plugin.cpp> [out-dir]
#
# Compiles the plugin to arm32 assembly with the SDK's flags and prints, for the 20 largest functions,
# the counts of the patterns that cost time on a Cortex-A17: divides and square roots, double math,
# library math calls, scalar float compares, spills and reloads. Reference section 13 explains each.
# Needs: zig (or ZIG="python3 -m ziglang"), awk, c++filt. The assembly is left in <out-dir>/out.s and the
# counts for every function in <out-dir>/scan_all.txt.
set -euo pipefail
[ $# -ge 2 ] || { sed -n '2,8p' "$0"; exit 2; }
SDK=$1; SRC=$2; OUT=${3:-mpc-opt-out}
HERE=$(cd "$(dirname "$0")" && pwd)
ZIG=${ZIG:-zig}
mkdir -p "$OUT"
$ZIG c++ -target arm-linux-gnueabihf.2.31 -mcpu=cortex_a17 -std=c++17 -O2 -fPIC \
  -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections \
  -Wno-unused-command-line-argument \
  -I"$SDK/framework" -I"$(dirname "$SRC")" -S -o "$OUT/out.s" "$SRC"
# Every function, for coverage.sh; then the 20 largest for reading.
awk -v min=1 -f "$HERE/scan.awk" "$OUT/out.s" | sort -k3 -n -r > "$OUT/scan_all.txt"
awk '$3 >= 100' "$OUT/scan_all.txt" | head -20 | c++filt
