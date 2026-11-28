#!/bin/bash
# count.sh <sdk-dir> <plugin.cpp> [k] [blocks] [out-dir]
#
# Builds drive.cpp around the plugin's source as a static arm32 program (Cortex-A17, -O2, the SDK's
# optimization level) and counts the instructions each block executes under qemu-arm.
#   k        parameters set to random values before each block (default 0 = steady state)
#   blocks   blocks to run (default 400 in steady state, 1000 with k above 0; minimum 300). The first
#            200 are warm-up and are left out of the summary. The emulator runs a few million
#            instructions a second, so the time grows with the plugin's cost: seconds for a light
#            effect, over an hour for 400 blocks of an instrument with every voice sounding. For a
#            heavy steady scenario 300 blocks are enough.
# While it runs, <out-dir>/status.txt says which block it has reached and about how long is left.
#   out-dir  where drive, counts.txt and the summary go (default ./mpc-opt-out)
# Set MPC_OPT_EVENTS to an events file (format at the top of drive.cpp) to force settings, program
# changes or mode switches at given blocks, e.g. to count one mode in steady state.
# MPC_OPT_MARK=all counts the whole host block (events and parameter writes too), MPC_OPT_SKIP names
# parameters the random sweep leaves alone; both are explained at the top of drive.cpp.
# MPC_OPT_SILENT=1 feeds silence in place of noise (the idle scenario of an effect).
# DRIVE_BIN names a drive program that is already built; <sdk-dir> and <plugin.cpp> are then not used
# (give - for each). Use it to build once for many scenarios: count the first scenario, then pass
# DRIVE_BIN=<its out-dir>/drive to the others. A plugin that is not one SDK source file always needs
# it: build drive.cpp yourself with the compiler and flags of the release (static, arm32).
# Needs: zig (or ZIG="python3 -m ziglang"), qemu-arm 8.1 or later, nm, readelf, awk. Linux or WSL.
# MPC_OPT_THREADS=all counts the plugin's other threads too, for a plugin that does its work on a
# thread of its own (the default counts the thread that calls the plugin).
# The result is an instruction count, exact, and repeatable for a plugin whose audio code does not
# depend on a timer thread; one that does (meters pushed from a thread, say) can differ by a few
# dozen instructions in some blocks from run to run. It is a proxy for time on the device.
set -euo pipefail
[ $# -ge 2 ] || { sed -n '2,28p' "$0"; exit 2; }
SDK=$1; SRC=$2; K=${3:-0}; BLOCKS=${4:-}; OUT=${5:-mpc-opt-out}
[ -n "$BLOCKS" ] || { if [ "$K" -gt 0 ]; then BLOCKS=1000; else BLOCKS=400; fi; }
HERE=$(cd "$(dirname "$0")" && pwd)
ZIG=${ZIG:-zig}
mkdir -p "$OUT"
TAG="k$K"; [ -z "${MPC_OPT_EVENTS:-}" ] || TAG="${TAG}_$(basename "${MPC_OPT_EVENTS%.*}")"
rm -f "$OUT/counts.stamp"
if [ -n "${DRIVE_BIN:-}" ]; then
  [ "$DRIVE_BIN" -ef "$OUT/drive" ] || cp "$DRIVE_BIN" "$OUT/drive"
else
  $ZIG c++ -O2 -std=c++17 -pthread -target arm-linux-musleabihf -mcpu=cortex_a17 -static \
    -I"$SDK/framework" -I"$(dirname "$SRC")" "-DSRC=\"$(basename "$SRC")\"" "$HERE/drive.cpp" -o "$OUT/drive"
fi
M=$(nm "$OUT/drive" | awk '$3 == "icMark" { print $1 }')
E=$(nm "$OUT/drive" | awk '$3 == "icEnd" { print $1 }')
[ -n "$M" ] && [ -n "$E" ] || { echo "icMark/icEnd not found in the binary"; exit 1; }
# A Thumb function's symbol has bit 0 set; the trace shows the even address.
M=$(printf '%08x' $(( 0x$M & ~1 ))); E=$(printf '%08x' $(( 0x$E & ~1 )))
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
rm -f "$OUT/progress"; echo "count ($TAG): starting" > "$OUT/status.txt"
progress count > /dev/null 2>&1 &
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
command -v readelf > /dev/null || { echo "readelf is needed (binutils)"; exit 2; }
if readelf -h "$PROGRAM" | grep -q 'Type:.*DYN'; then
  ! readelf -l "$PROGRAM" | grep -q 'INTERP' || { echo "this program is linked against shared libraries: link the harness statically"; exit 2; }
  LOGS=$LOGS,page; PIE=(-n "$(readelf -h "$PROGRAM" | awk '/Entry point/ { print $NF }')")
fi
if [ "$FAST" = 1 ]; then
  qemu-arm -d "$LOGS" -D /dev/stdout "$OUT/drive" "$K" "$BLOCKS" 2>/dev/null \
    | "$TB" -c "$OUT/counts.txt" -s "$OUT/modes.txt" -p "$OUT/progress" ${THREADS[@]+"${THREADS[@]}"} ${PIE[@]+"${PIE[@]}"} "$M" "$E" || FAST=0
fi
[ "$FAST" = 1 ] || [ ${#PIE[@]} -eq 0 ] || { echo "this program loads at a movable address, which only the fast counter can follow, and the fast counter did not run: link the harness at a fixed address (-no-pie)"; exit 2; }
if [ "$FAST" = 0 ]; then
  qemu-arm -one-insn-per-tb -d exec,nochain -D /dev/stdout "$OUT/drive" "$K" "$BLOCKS" 2>/dev/null \
    | LC_ALL=C grep -F "Trace 0:" | LC_ALL=C grep --line-buffered -n -e "/00000000$M/" -e "/00000000$E/" \
    | awk -F: -v P="$OUT/progress" 'NR % 2 == 1 { p = $1 }
        NR % 2 == 0 { print (NR - 2) / 2, $1 - p - 1; if (NR % 10 == 0) { print NR / 2 > P; close(P) } }' > "$OUT/counts.txt"
fi
kill $PW 2>/dev/null || true
echo "count ($TAG): done, $BLOCKS blocks in $(( (SECONDS - T0 + 59) / 60 )) min" > "$OUT/status.txt"
sort -k2 -n "$OUT/counts.txt" | awk -v k="$K" '
  $1 >= 200 { v[n++] = $2; if ($2 >= mx) { mx = $2; mb = $1 } }
  END { if (n == 0) { print "no blocks counted"; exit 1 }
        printf "k=%d: median %.1f instructions per frame, largest block %d (block %d, %.1f per frame), %d blocks from block 200\n",
               k, v[int(n / 2)] / 128, mx, mb, mx / 128, n
        printf "      per frame: 90th percentile %.1f, 99th percentile %.1f", v[int(n * 0.90)] / 128, v[int(n * 0.99)] / 128
        if (n < 500) printf " (only %d blocks: the 99th percentile is rough)", n
        printf "\n" }' | tee "$OUT/summary_$TAG.txt"
[ -z "${MPC_OPT_EVENTS:-}" ] || { echo "events applied:"; sed 's/^/  /' "$MPC_OPT_EVENTS.applied"; }
# What a run depends on, for the stamp that lets profile.sh reuse a count: the settings, the events
# file, the program that ran, and the sources it was built from ("-" for a prebuilt program).
SETTINGS="$K $BLOCKS|${MPC_OPT_MARK:-} ${MPC_OPT_SKIP:-} ${MPC_OPT_SILENT:-} ${MPC_OPT_FZ:-} ${MPC_OPT_INPLACE:-} ${MPC_OPT_THREADS:-}|$(sha256sum < "${MPC_OPT_EVENTS:-/dev/null}" | cut -c1-16)"
SOURCES=-
[ -n "${DRIVE_BIN:-}" ] || SOURCES=$(cat "$(dirname "$SRC")"/*.[ch]* "$HERE/drive.cpp" | sha256sum | cut -c1-16)
echo "$SETTINGS|$(sha256sum < "$OUT/drive" | cut -c1-16)|$SOURCES" > "$OUT/counts.stamp"
