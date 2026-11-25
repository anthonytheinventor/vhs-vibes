#!/bin/bash
# profile.sh <sdk-dir> <plugin.cpp> [k] [blocks] [out-dir] [block ...]
#
# Says which functions the instructions of a block were executed in. For each block named (default:
# the largest block from block 200 on) it prints the 15 functions with the most instructions, each
# with its share of the block and the difference from the median block, so the difference shows what
# a costly block spent its extra instructions on. Name several blocks to profile them in one pass:
# every pass costs a full run under the emulator.
# It reuses the build and counts.txt left in <out-dir> by count.sh or by an earlier profile when k,
# the block count and the sources are unchanged, and runs count.sh otherwise.
# The run is repeatable (fixed random seed), so block numbers match those in counts.txt.
# Only instructions between the block's start and end markers count: the plugin's process call.
# Code the compiler inlined is counted under the function it was inlined into.
# Needs: what count.sh needs, plus c++filt.
set -euo pipefail
[ $# -ge 2 ] || { sed -n '2,14p' "$0"; exit 2; }
SDK=$1; SRC=$2; K=${3:-0}; BLOCKS=${4:-}; OUT=${5:-mpc-opt-out}
shift $(( $# < 5 ? $# : 5 ))
PICKS="$*"
HERE=$(cd "$(dirname "$0")" && pwd)
[ -n "$BLOCKS" ] || { if [ "$K" -gt 0 ]; then BLOCKS=1000; else BLOCKS=400; fi; }
STAMP="$K $BLOCKS $(cat "$(dirname "$SRC")"/*.[ch]* "$HERE/drive.cpp" | sha256sum | cut -c1-16)"
if [ -x "$OUT/drive" ] && [ -s "$OUT/counts.txt" ] && [ "$(cat "$OUT/counts.stamp" 2>/dev/null)" = "$STAMP" ]; then
  echo "reusing the build and counts in $OUT (k=$K, $BLOCKS blocks, sources unchanged)"
else
  bash "$HERE/count.sh" "$SDK" "$SRC" "$K" "$BLOCKS" "$OUT"
fi
MED=$(awk '$1 >= 200' "$OUT/counts.txt" | sort -k2 -n | awk '{ b[n++] = $1 } END { print b[int(n / 2)] }')
[ -n "$PICKS" ] || PICKS=$(awk '$1 >= 200 && $2 >= mx { mx = $2; mb = $1 } END { print mb }' "$OUT/counts.txt")
M=$(nm "$OUT/drive" | awk '$3 == "icMark" { print $1 }')
E=$(nm "$OUT/drive" | awk '$3 == "icEnd" { print $1 }')
nm -n "$OUT/drive" | awk '$2 ~ /^[TtWw]$/ && $3 !~ /^[$]/ { print $1, $3 }' > "$OUT/symbols.txt"
qemu-arm -one-insn-per-tb -d exec,nochain -D /dev/stdout "$OUT/drive" "$K" "$BLOCKS" 2>/dev/null \
  | LC_ALL=C grep -F "Trace 0:" \
  | LC_ALL=C awk -F/ -v M="00000000$M" -v E="00000000$E" -v A="$MED" -v P="$PICKS" -v SYMS="$OUT/symbols.txt" '
  function hex(s,   i, v) { v = 0; for (i = 1; i <= length(s); ++i) v = v * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1; return v }
  function find(a,   lo, hi, mid) {
    lo = 1; hi = ns
    while (lo < hi) { mid = int((lo + hi + 1) / 2); if (addr[mid] <= a) lo = mid; else hi = mid - 1 }
    return name[lo]
  }
  BEGIN { while ((getline line < SYMS) > 0) { split(line, p, " "); addr[++ns] = hex(p[1]); name[ns] = p[2] }
          np = split(P, pick, " "); for (i = 1; i <= np; ++i) want[pick[i]] = 1; want[A] = 1
          blk = -1; on = 0 }
  $2 == M { ++blk; on = (blk in want); next }
  $2 == E { on = 0; next }
  on { ++pc[blk "," $2] }
  END {
    for (key in pc) { split(key, q, ","); f = find(hex(q[2])); n[q[1] "," f] += pc[key]; tot[q[1]] += pc[key]; seen[f] = 1 }
    for (i = 1; i <= np; ++i) { b = pick[i]
      printf "%d 999999999999 %d %d TOTAL\n", i, tot[b], tot[A]
      for (f in seen) { x = n[b "," f] + 0; y = n[A "," f] + 0; if (x + y > 0) printf "%d %d %d %d %s\n", i, (x > y ? x : y), x, y, f }
    }
  }' | sort -k1,1n -k2,2nr | cut -d" " -f1,3- > "$OUT/profile_k$K.txt"
I=0
for B in $PICKS; do
  I=$((I + 1))
  echo
  echo "k=$K: block $B against the median block $MED (instructions per block of 128 frames)"
  printf "%10s %7s %10s %10s  %s\n" "block $B" "share" "median" "extra" "function"
  awk -v i="$I" '$1 == i' "$OUT/profile_k$K.txt" | head -16 | c++filt | awk 'NR == 1 { t = $2 }
    { f = $4; for (j = 5; j <= NF; ++j) f = f " " $j
      printf "%10d %6.1f%% %10d %10d  %s\n", $2, 100 * $2 / t, $3, $2 - $3, substr(f, 1, 110) }'
done
echo
echo "full list (block index in the order given, block count, median count, function): $OUT/profile_k$K.txt"
