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
# The profile is saved as profile_k<k>.txt, or profile_k<k>_<events file name>.txt with an events
# file, so the scenarios of one job do not overwrite each other's profiles. The build and counts.txt
# belong to one scenario at a time: give each scenario its own out-dir to keep them all.
# Only instructions between the block's start and end markers count: the plugin's process call.
# Code the compiler inlined is counted under the function it was inlined into.
# DRIVE_BIN, MPC_OPT_MARK and MPC_OPT_SKIP work as in count.sh.
# The profile pass takes about as long as the count did. While it runs, <out-dir>/status.txt says
# which block it has reached and about how long is left.
# Needs: what count.sh needs, plus c++filt.
set -euo pipefail
[ $# -ge 2 ] || { sed -n '2,20p' "$0"; exit 2; }
SDK=$1; SRC=$2; K=${3:-0}; BLOCKS=${4:-}; OUT=${5:-mpc-opt-out}
shift $(( $# < 5 ? $# : 5 ))
PICKS="$*"
HERE=$(cd "$(dirname "$0")" && pwd)
TAG="k$K"; [ -z "${MPC_OPT_EVENTS:-}" ] || TAG="${TAG}_$(basename "${MPC_OPT_EVENTS%.*}")"
[ -n "$BLOCKS" ] || { if [ "$K" -gt 0 ]; then BLOCKS=1000; else BLOCKS=400; fi; }
# What a run depends on, for the stamp that lets profile.sh reuse a count: the settings, the events
# file, the program that ran, and the sources it was built from ("-" for a prebuilt program).
SETTINGS="$K $BLOCKS|${MPC_OPT_MARK:-} ${MPC_OPT_SKIP:-} ${MPC_OPT_SILENT:-} ${MPC_OPT_FZ:-} ${MPC_OPT_INPLACE:-} ${MPC_OPT_THREADS:-}|$(sha256sum < "${MPC_OPT_EVENTS:-/dev/null}" | cut -c1-16)"
SOURCES=-
[ -n "${DRIVE_BIN:-}" ] || SOURCES=$(cat "$(dirname "$SRC")"/*.[ch]* "$HERE/drive.cpp" | sha256sum | cut -c1-16)
# Reuse the count when the settings match and the program is the one that was counted: the prebuilt
# one named now, or the one these sources built.
REUSE=no
if [ -x "$OUT/drive" ] && [ -s "$OUT/counts.txt" ] && [ -s "$OUT/counts.stamp" ]; then
  IFS='|' read -r S1 S2 S3 SBIN SSRC < "$OUT/counts.stamp" || true
  if [ "$S1|$S2|$S3" = "$SETTINGS" ]; then
    if [ -n "${DRIVE_BIN:-}" ]; then
      [ "$(sha256sum < "$DRIVE_BIN" | cut -c1-16)" != "$SBIN" ] || REUSE=yes
    else
      [ "$SOURCES" != "$SSRC" ] || REUSE=yes
    fi
  fi
fi
AWK=awk; if command -v mawk > /dev/null; then AWK=mawk; fi   # mawk is several times faster on the trace
if [ "$REUSE" = yes ]; then
  echo "reusing the build and counts in $OUT (k=$K, $BLOCKS blocks, same program and settings)"
else
  bash "$HERE/count.sh" "$SDK" "$SRC" "$K" "$BLOCKS" "$OUT"
fi
MED=$(awk '$1 >= 200' "$OUT/counts.txt" | sort -k2 -n | awk '{ b[n++] = $1 } END { print b[int(n / 2)] }')
[ -n "$PICKS" ] || PICKS=$(awk '$1 >= 200 && $2 >= mx { mx = $2; mb = $1 } END { print mb }' "$OUT/counts.txt")
M=$(nm "$OUT/drive" | awk '$3 == "icMark" { print $1 }')
E=$(nm "$OUT/drive" | awk '$3 == "icEnd" { print $1 }')
# A Thumb function's symbol has bit 0 set; the trace shows the even address.
M=$(printf '%08x' $(( 0x$M & ~1 ))); E=$(printf '%08x' $(( 0x$E & ~1 )))
nm -n "$OUT/drive" | awk '$2 ~ /^[TtWw]$/ && $3 !~ /^[$]/ { print $1, $3 }' > "$OUT/symbols.txt"
# Progress: the pass below writes the block it has reached to <out-dir>/progress, and this loop turns
# that into <out-dir>/status.txt with the time left, every 15 seconds.
progress() {
  local T0=$SECONDS N
  while sleep 15; do
    N=$(cat "$OUT/progress" 2>/dev/null || true); N=${N:-0}
    if [ "$N" -gt 0 ]; then
      echo "$1 ($TAG): block $N of $BLOCKS, $(( (SECONDS - T0) / 60 )) min so far, about $(( ((SECONDS - T0) * (BLOCKS - N) / N + 59) / 60 )) min left" > "$OUT/status.txt"
    else
      echo "$1 ($TAG): starting, $(( (SECONDS - T0) / 60 )) min so far" > "$OUT/status.txt"
    fi
  done
}
rm -f "$OUT/progress"; echo "profile ($TAG): starting" > "$OUT/status.txt"
progress profile > /dev/null 2>&1 &
PW=$!
trap 'kill $PW 2>/dev/null || true' EXIT
T0=$SECONDS
PROGRAM="$OUT/drive"
# The fast counter: the emulator runs whole translated blocks and tbcount works out the instructions
# from the listing of each (see tbcount.cpp). MPC_OPT_SLOW=1, or a log tbcount cannot count exactly,
# falls back to the older method of one log line per instruction: the same counts, several times slower.
# It is built once per version of its source and shared by every scenario folder.
TB="${TMPDIR:-/tmp}/mpc-opt-tbcount-$(cksum < "$HERE/tbcount.cpp" | cut -d' ' -f1)"
FAST=1; [ "${MPC_OPT_SLOW:-0}" = 0 ] || FAST=0
if [ "$FAST" = 1 ] && [ ! -x "$TB" ]; then
  if ${ZIG:-zig} c++ -O2 -std=c++17 "$HERE/tbcount.cpp" -o "$TB.$$" 2>/dev/null; then mv -f "$TB.$$" "$TB"
  else echo "could not build tbcount.cpp: using the slower method"; FAST=0; fi
fi
# where ARM code and Thumb code start, from the program's mapping symbols
{ nm --special-syms "$PROGRAM" 2>/dev/null || nm "$PROGRAM"; } | awk '$3 ~ /^[$][at]/ { print $1, substr($3, 2, 1) }' > "$OUT/modes.txt"
THREADS=(); [ "${MPC_OPT_THREADS:-}" != all ] || THREADS=(-t)
# A program built to load at a movable address (a position-independent executable) runs somewhere
# other than its file addresses say. tbcount reads where from the emulator's log ("page").
LOGS=in_asm,exec,nochain; PIE=()
if readelf -h "$PROGRAM" 2>/dev/null | grep -q 'Type:.*DYN'; then
  LOGS=$LOGS,page; PIE=(-n "$(readelf -h "$PROGRAM" | awk '/Entry point/ { print $NF }')")
fi
# First: "<block> <address> <count>" for every instruction address executed in the blocks wanted.
if [ "$FAST" = 1 ]; then
  qemu-arm -d "$LOGS" -D /dev/stdout "$OUT/drive" "$K" "$BLOCKS" 2>/dev/null \
    | "$TB" -e "$OUT/each.tmp" -s "$OUT/modes.txt" -p "$OUT/progress" ${THREADS[@]+"${THREADS[@]}"} ${PIE[@]+"${PIE[@]}"} "$M" "$E" $PICKS "$MED" || FAST=0
fi
[ "$FAST" = 1 ] || [ ${#PIE[@]} -eq 0 ] || { echo "this program loads at a movable address, which only the fast counter can follow: link it at a fixed address (-no-pie), or leave MPC_OPT_SLOW unset"; exit 2; }
if [ "$FAST" = 0 ]; then
  qemu-arm -one-insn-per-tb -d exec,nochain -D /dev/stdout "$OUT/drive" "$K" "$BLOCKS" 2>/dev/null \
    | LC_ALL=C grep -F "Trace 0:" \
    | LC_ALL=C $AWK -F/ -v M="00000000$M" -v E="00000000$E" -v A="$MED" -v P="$PICKS" -v PROG="$OUT/progress" '
    BEGIN { np = split(P, pick, " "); for (i = 1; i <= np; ++i) want[pick[i]] = 1; want[A] = 1; blk = -1; on = 0 }
    $2 == M { ++blk; on = (blk in want); if (blk % 5 == 0) { print blk > PROG; close(PROG) } next }
    $2 == E { on = 0; next }
    on { ++pc[blk "," $2] }
    END { for (key in pc) { split(key, q, ","); print q[1], substr(q[2], 9), pc[key] } }' > "$OUT/each.tmp"
fi
# Then: each address to its function, and the blocks side by side with the median block.
LC_ALL=C $AWK -v A="$MED" -v P="$PICKS" -v SYMS="$OUT/symbols.txt" '
  function hex(s,   i, v) { v = 0; for (i = 1; i <= length(s); ++i) v = v * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1; return v }
  function even(v) { return v - v % 2 }     # a Thumb symbol has bit 0 set
  function find(a,   lo, hi, mid) {
    lo = 1; hi = ns
    while (lo < hi) { mid = int((lo + hi + 1) / 2); if (addr[mid] <= a) lo = mid; else hi = mid - 1 }
    return name[lo]
  }
  BEGIN { while ((getline line < SYMS) > 0) { split(line, p, " "); addr[++ns] = even(hex(p[1])); name[ns] = p[2] }
          np = split(P, pick, " ") }
  { f = find(hex($2)); n[$1 "," f] += $3; tot[$1] += $3; seen[f] = 1 }
  END {
    for (i = 1; i <= np; ++i) { b = pick[i]
      printf "%d 999999999999 %d %d TOTAL\n", i, tot[b], tot[A]
      for (f in seen) { x = n[b "," f] + 0; y = n[A "," f] + 0; if (x + y > 0) printf "%d %d %d %d %s\n", i, (x > y ? x : y), x, y, f }
    }
  }' "$OUT/each.tmp" | sort -k1,1n -k2,2nr | cut -d" " -f1,3- > "$OUT/profile_$TAG.txt"
rm -f "$OUT/each.tmp"
kill $PW 2>/dev/null || true
echo "profile ($TAG): done, $BLOCKS blocks in $(( (SECONDS - T0 + 59) / 60 )) min" > "$OUT/status.txt"
I=0
for B in $PICKS; do
  I=$((I + 1))
  echo
  echo "k=$K: block $B against the median block $MED (instructions per block of 128 frames)"
  printf "%10s %7s %10s %10s  %s\n" "block $B" "share" "median" "extra" "function"
  awk -v i="$I" '$1 == i' "$OUT/profile_$TAG.txt" | head -16 | c++filt | awk 'NR == 1 { t = $2 }
    { f = $4; for (j = 5; j <= NF; ++j) f = f " " $j
      printf "%10d %6.1f%% %10d %10d  %s\n", $2, 100 * $2 / t, $3, $2 - $3, substr(f, 1, 110) }'
done
echo
echo "full list (block index in the order given, block count, median count, function): $OUT/profile_$TAG.txt"
