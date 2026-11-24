#!/bin/bash
# profile.sh <sdk-dir> <plugin.cpp> [k] [blocks] [out-dir] [block]
#
# Says which functions the instructions of a block were executed in. It runs count.sh, takes the
# median block and the largest block from block 200 on (or the block number given), runs the same
# program again under qemu-arm and attributes every instruction of those two blocks to a function.
# Prints the 20 functions with the most instructions in either block, side by side,
# so the difference shows what a costly block spent its extra instructions on.
# The run is repeatable (fixed random seed), so block numbers match those in counts.txt.
# Code the compiler inlined is counted under the function it was inlined into.
# Needs: what count.sh needs, plus c++filt.
set -euo pipefail
[ $# -ge 2 ] || { sed -n '2,11p' "$0"; exit 2; }
SDK=$1; SRC=$2; K=${3:-0}; BLOCKS=${4:-}; OUT=${5:-mpc-opt-out}; PICK=${6:-}
HERE=$(cd "$(dirname "$0")" && pwd)
bash "$HERE/count.sh" "$SDK" "$SRC" "$K" "$BLOCKS" "$OUT"
BLOCKS=$(wc -l < "$OUT/counts.txt")
MED=$(awk '$1 >= 200' "$OUT/counts.txt" | sort -k2 -n | awk '{ b[n++] = $1 } END { print b[int(n / 2)] }')
[ -n "$PICK" ] || PICK=$(awk '$1 >= 200 && $2 >= mx { mx = $2; mb = $1 } END { print mb }' "$OUT/counts.txt")
M=$(nm "$OUT/drive" | awk '$3 == "icMark" { print $1 }')
E=$(nm "$OUT/drive" | awk '$3 == "icEnd" { print $1 }')
nm -n "$OUT/drive" | awk '$2 ~ /^[TtWw]$/ && $3 !~ /^[$]/ { print $1, $3 }' > "$OUT/symbols.txt"
qemu-arm -one-insn-per-tb -d exec,nochain -D /dev/stdout "$OUT/drive" "$K" "$BLOCKS" 2>/dev/null \
  | LC_ALL=C grep -F "Trace 0:" \
  | LC_ALL=C awk -F/ -v M="00000000$M" -v E="00000000$E" -v A="$MED" -v B="$PICK" -v SYMS="$OUT/symbols.txt" '
  function hex(s,   i, v) { v = 0; for (i = 1; i <= length(s); ++i) v = v * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1; return v }
  function find(a,   lo, hi, mid) {
    lo = 1; hi = ns
    while (lo < hi) { mid = int((lo + hi + 1) / 2); if (addr[mid] <= a) lo = mid; else hi = mid - 1 }
    return name[lo]
  }
  BEGIN { while ((getline line < SYMS) > 0) { split(line, p, " "); addr[++ns] = hex(p[1]); name[ns] = p[2] }
          blk = -1; on = 0 }
  $2 == M { ++blk; on = (blk == A || blk == B); next }
  $2 == E { on = 0; next }
  on { ++pc[blk "," $2] }
  END {
    for (key in pc) { split(key, q, ","); f = find(hex(q[2])); n[q[1] "," f] += pc[key]; tot[q[1]] += pc[key]; seen[f] = 1 }
    for (f in seen) { x = n[B "," f] + 0; y = n[A "," f] + 0; printf "%d %d %d %s\n", (x > y ? x : y), x, y, f }
    printf "%d %d %d TOTAL\n", tot[B] + tot[A], tot[B], tot[A]
  }' | sort -k1 -n -r | cut -d" " -f2- > "$OUT/profile_k$K.txt"
echo "k=$K: block $PICK against the median block $MED (instructions per block, 128 frames each)"
printf "%10s %10s %10s  %s\n" "block $PICK" "median" "extra" "function"
head -21 "$OUT/profile_k$K.txt" | c++filt | awk '{ f = $3; for (i = 4; i <= NF; ++i) f = f " " $i
  printf "%10d %10d %10d  %s\n", $1, $2, $1 - $2, substr(f, 1, 110) }'
echo "full list: $OUT/profile_k$K.txt"
