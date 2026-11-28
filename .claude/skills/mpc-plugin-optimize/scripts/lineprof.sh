#!/bin/bash
# lineprof.sh <sdk-dir> <plugin.cpp> [k] [blocks] [out-dir] [block ...]
#
# Says which source lines the instructions were executed in, where profile.sh says which functions.
# Use it when one large function holds most of the cost because the compiler inlined everything
# into it. It prints the 40 lines with the most instructions per frame, then the instruction mix:
# how many instructions per frame were loads from the stack, stores to it, and each other kind.
# Many loads from the stack in a hot loop mean the compiler ran out of registers or could not
# reach members from its base register (reference sections 10 and 13).
#   block ...   the blocks to add up (default: every block from 200 on). Name one costly block from
#               counts.txt to see its lines.
# A line is the innermost one: code inlined from a helper counts under the helper's line.
# The program is built like count.sh's, with debug information added (drive-g in <out-dir>); the
# script checks that its code is the same as the counted program's. DRIVE_BIN names a prebuilt
# program instead, which must carry debug information (-g). MPC_OPT_EVENTS, MPC_OPT_SILENT,
# MPC_OPT_MARK and MPC_OPT_SKIP work as in count.sh.
# Each call is one more pass under the emulator; <out-dir>/status.txt shows how far it is.
# Needs: what count.sh needs, plus an addr2line that reads 32-bit ARM programs (llvm-addr2line, or
# arm-linux-gnueabihf-addr2line from binutils-arm-linux-gnueabihf; ADDR2LINE names another). The
# instruction mix also needs llvm-objdump or arm-linux-gnueabihf-objdump, and is left out without.
set -euo pipefail
[ $# -ge 2 ] || { sed -n '2,20p' "$0"; exit 2; }
SDK=$1; SRC=$2; K=${3:-0}; BLOCKS=${4:-}; OUT=${5:-mpc-opt-out}
shift $(( $# < 5 ? $# : 5 ))
PICKS="$*"
HERE=$(cd "$(dirname "$0")" && pwd)
ZIG=${ZIG:-zig}
TAG="k$K"; [ -z "${MPC_OPT_EVENTS:-}" ] || TAG="${TAG}_$(basename "${MPC_OPT_EVENTS%.*}")"
[ -n "$BLOCKS" ] || { if [ "$K" -gt 0 ]; then BLOCKS=1000; else BLOCKS=300; fi; }
AWK=awk; if command -v mawk > /dev/null; then AWK=mawk; fi
mkdir -p "$OUT"
G="$OUT/drive-g"
if [ -n "${DRIVE_BIN:-}" ]; then
  [ "$DRIVE_BIN" -ef "$G" ] || cp "$DRIVE_BIN" "$G"
else
  $ZIG c++ -O2 -g -std=c++17 -pthread -target arm-linux-musleabihf -mcpu=cortex_a17 -static \
    -I"$SDK/framework" -I"$(dirname "$SRC")" "-DSRC=\"$(basename "$SRC")\"" "$HERE/drive.cpp" -o "$G"
  if [ -x "$OUT/drive" ] && $ZIG objcopy -O binary -j .text "$G" "$OUT/text-g.bin" 2>/dev/null \
     && $ZIG objcopy -O binary -j .text "$OUT/drive" "$OUT/text.bin" 2>/dev/null; then
    cmp -s "$OUT/text-g.bin" "$OUT/text.bin" || echo "note: the code of drive-g differs from $OUT/drive (other sources or settings?)"
    rm -f "$OUT/text-g.bin" "$OUT/text.bin"
  fi
fi
M=$(nm "$G" | awk '$3 == "icMark" { print $1 }')
E=$(nm "$G" | awk '$3 == "icEnd" { print $1 }')
[ -n "$M" ] && [ -n "$E" ] || { echo "icMark/icEnd not found in the binary"; exit 1; }
M=$(printf '%08x' $(( 0x$M & ~1 ))); E=$(printf '%08x' $(( 0x$E & ~1 )))
A2L=
for C in "${ADDR2LINE:-}" llvm-addr2line arm-linux-gnueabihf-addr2line addr2line; do
  [ -n "$C" ] && command -v "$C" > /dev/null || continue
  if echo "0x$M" | "$C" -e "$G" 2>/dev/null | grep -q ':[0-9]'; then A2L=$C; break; fi
done
[ -n "$A2L" ] || { echo "no addr2line that reads this program (install llvm or binutils-arm-linux-gnueabihf, or set ADDR2LINE); a prebuilt program needs -g"; exit 2; }
progress() {
  local T0=$SECONDS N
  while sleep 15; do
    N=$(cat "$OUT/progress" 2>/dev/null || true); N=${N:-0}
    if [ "$N" -gt 0 ]; then
      echo "lines ($TAG): block $N of $BLOCKS, $(( (SECONDS - T0) / 60 )) min so far, about $(( ((SECONDS - T0) * (BLOCKS - N) / N + 59) / 60 )) min left" > "$OUT/status.txt"
    else
      echo "lines ($TAG): starting, $(( (SECONDS - T0) / 60 )) min so far" > "$OUT/status.txt"
    fi
  done
}
rm -f "$OUT/progress"; echo "lines ($TAG): starting" > "$OUT/status.txt"
progress > /dev/null 2>&1 &
PW=$!
trap 'kill $PW 2>/dev/null || true' EXIT
T0=$SECONDS
# one line per address: "<hex address> <count>", and the number of blocks added up in the first line
PROGRAM="$G"
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
command -v readelf > /dev/null || { echo "readelf is needed (binutils)"; exit 2; }
if readelf -h "$PROGRAM" | grep -q 'Type:.*DYN'; then
  ! readelf -l "$PROGRAM" | grep -q 'INTERP' || { echo "this program is linked against shared libraries: link the harness statically"; exit 2; }
  LOGS=$LOGS,page; PIE=(-n "$(readelf -h "$PROGRAM" | awk '/Entry point/ { print $NF }')")
fi
if [ "$FAST" = 1 ]; then
  qemu-arm -d "$LOGS" -D /dev/stdout "$G" "$K" "$BLOCKS" 2>/dev/null \
    | "$TB" -u "$OUT/hist_$TAG.txt" -s "$OUT/modes.txt" -p "$OUT/progress" ${THREADS[@]+"${THREADS[@]}"} ${PIE[@]+"${PIE[@]}"} "$M" "$E" $PICKS || FAST=0
fi
[ "$FAST" = 1 ] || [ ${#PIE[@]} -eq 0 ] || { echo "this program loads at a movable address, which only the fast counter can follow, and the fast counter did not run: link the harness at a fixed address (-no-pie)"; exit 2; }
if [ "$FAST" = 0 ]; then
  qemu-arm -one-insn-per-tb -d exec,nochain -D /dev/stdout "$G" "$K" "$BLOCKS" 2>/dev/null \
    | LC_ALL=C grep -F "Trace 0:" \
    | LC_ALL=C $AWK -F/ -v M="00000000$M" -v E="00000000$E" -v P="$PICKS" -v PROG="$OUT/progress" '
    BEGIN { np = split(P, pick, " "); for (i = 1; i <= np; ++i) want[pick[i]] = 1; blk = -1; on = 0 }
    $2 == M { ++blk; on = np > 0 ? (blk in want) : (blk >= 200); if (on) ++nb
              if (blk % 5 == 0) { print blk > PROG; close(PROG) } next }
    $2 == E { on = 0; next }
    on { ++pc[$2] }
    END { print "blocks", nb + 0; for (a in pc) print substr(a, 9), pc[a] }' > "$OUT/hist_$TAG.txt"
fi
kill $PW 2>/dev/null || true
echo "lines ($TAG): done, $BLOCKS blocks in $(( (SECONDS - T0 + 59) / 60 )) min" > "$OUT/status.txt"
NB=$(awk 'NR == 1 { print $2 }' "$OUT/hist_$TAG.txt")
[ "${NB:-0}" -gt 0 ] || { echo "no blocks were added up (block numbers out of range?)"; exit 1; }
tail -n +2 "$OUT/hist_$TAG.txt" > "$OUT/hist.tmp"
cut -d' ' -f1 "$OUT/hist.tmp" | sed 's/^/0x/' | "$A2L" -e "$G" | sed 's/ (discriminator [0-9]*)//' > "$OUT/where.tmp"
paste -d' ' "$OUT/hist.tmp" "$OUT/where.tmp" | awk -v n="$NB" '
  { w = $3; sub(/^.*\//, "", w); c[w] += $2; t += $2 }
  END { printf "%.2f TOTAL\n", t / n / 128; for (w in c) printf "%.2f %s\n", c[w] / n / 128, w }' \
  | sort -k1,1nr > "$OUT/lines_$TAG.txt"
echo "k=$K: instructions per frame by source line, over $NB blocks (the 40 largest)"
head -n 41 "$OUT/lines_$TAG.txt" | awk '{ printf "%10.2f  %s\n", $1, $2 }'
OBJ=
for C in "${OBJDUMP:-}" llvm-objdump arm-linux-gnueabihf-objdump; do
  [ -n "$C" ] && command -v "$C" > /dev/null || continue
  OBJ=$C; break
done
if [ -n "$OBJ" ]; then
  "$OBJ" -d --no-show-raw-insn "$G" 2>/dev/null \
    | awk -F'\t' '$1 ~ /^ *[0-9a-f]+: *$/ { a = $1; gsub(/[ :]/, "", a); printf "%s\t%s\t%s\n", a, $2, $3 }' > "$OUT/dis.tmp"
  echo
  echo "k=$K: instruction mix per frame (the 15 largest kinds)"
  awk -F'\t' -v n="$NB" 'FNR == NR { m[$1] = $2; o[$1] = $3; next }
    { split($0, p, " "); a = p[1]; sub(/^0+/, "", a); k = m[a]; sub(/ +$/, "", k); if (k == "") k = "?"
      if (k ~ /^v?ldr/ && o[a] ~ /\[sp/) k = "load from the stack (" k ")"
      else if (k ~ /^v?str/ && o[a] ~ /\[sp/) k = "store to the stack (" k ")"
      c[k] += p[2] }
    END { for (k in c) printf "%.2f\t%s\n", c[k] / n / 128, k }' "$OUT/dis.tmp" "$OUT/hist.tmp" \
    | sort -k1,1nr > "$OUT/mix_$TAG.txt"
  head -n 15 "$OUT/mix_$TAG.txt" | awk -F'\t' '{ printf "%10.2f  %s\n", $1, $2 }'
  rm -f "$OUT/dis.tmp"
fi
rm -f "$OUT/hist.tmp" "$OUT/where.tmp"
echo
echo "full lists: $OUT/lines_$TAG.txt${OBJ:+ and $OUT/mix_$TAG.txt}"
