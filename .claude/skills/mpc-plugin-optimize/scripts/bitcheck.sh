#!/bin/bash
# bitcheck.sh <sdk-dir> <old-plugin.cpp> <new-plugin.cpp> [out-dir]
#
# Proves, or disproves, that two versions of a plugin produce the same output bits, and when they do
# not, measures how large the difference is. It builds render.cpp around each source as a static
# arm32 program (the same build as count.sh), runs both under qemu-arm and compares the output files
# byte for byte. The run visits every program, seven block sizes, random parameters, every parameter
# at once, many settled random settings and bad input values, and plays notes into an instrument. It
# then does the same for arm64 when qemu-aarch64 is installed (BITCHECK_ARM64=0 leaves that out).
#   old-plugin.cpp   the untouched copy of the source kept in step 1 (its folder holds its headers)
#   new-plugin.cpp   the changed source
# Exit code 0 = identical on every target run, 1 = a difference, 2 = could not run or not repeatable.
# Control: the old build is run twice first. If its two outputs differ, the plugin does not repeat
# itself (a parameter debounced against the clock, a thread, uninitialized memory), no comparison
# means anything, and the script stops with exit code 2. Find the cause, or leave the parameter out
# with MPC_OPT_SKIP, before going on.
# On a difference it prints the first differing byte with its phase, then a table from outdiff.cpp:
# per phase, the level of the output, the RMS and peak of the difference in dB, how far the
# difference is below the output, and where in the phase the first difference is. Set
# MPC_OPT_HOLD=<seconds> to add long held programs to the run; the table then shows the difference
# early and late in each, which is how an error that grows in a feedback path is seen.
# A pass covers what the run visits. A plugin with behavior the run cannot reach (side inputs, a
# sample rate other than 44.1 kHz) needs its own cases added to a copy of render.cpp.
# Check that the comparison can fail: change one constant in a copy of the source and run it once.
# What the plugin shows and saves is compared as well: render.cpp writes every parameter's value,
# value text and unit, the program names and a hash of the saved state at fixed points of the run,
# and the script says whether the two builds agree on them (a difference counts as a difference).
# Whatever differs between the two runs of the old build is left out of that comparison and named:
# a parameter (a meter the plugin updates from a timer, say), the saved state when it holds such a
# value, or the program names. The sound after a state is loaded back is compared either way.
# IDENTICAL means the float samples the plugin handed the host are the same. Where the engine works
# in 16-bit integers inside, that is the precision the proof has.
# Data files the plugin loads (banks, tables, samples) must be where it looks for them: the programs
# run from <out-dir>/bits, so put the files there as the plugin expects them beside its binary.
# A plugin that is not one SDK source file: build render.cpp yourself with the compiler and flags of
# the release (static, arm32) around the old and the new source, and name the two programs in
# RENDER_OLD and RENDER_NEW; the first three arguments are then not used (give - for each).
# Needs: zig (or ZIG="python3 -m ziglang"), qemu-arm, cmp. Linux or WSL.
set -uo pipefail
[ $# -ge 3 ] || { sed -n '2,38p' "$0"; exit 2; }
SDK=$1; OLD=$2; NEW=$3; OUT=${4:-mpc-opt-out}
HERE=$(cd "$(dirname "$0")" && pwd)
ZIG=${ZIG:-zig}
mkdir -p "$OUT/bits"
RESULT=0
measure() {  # target, file a, file b, where to save the table
  if [ -x "$OUT/bits/outdiff" ] || $ZIG c++ -O2 -std=c++17 "$HERE/outdiff.cpp" -o "$OUT/bits/outdiff"; then
    "$OUT/bits/outdiff" "$2" "$3" "$OUT/bits/$1-old.log" | tee "$4"
  else
    echo "  could not build outdiff.cpp, so the difference was not measured"
  fi
}
run_target() {  # name, zig target, extra flags, emulator
  local T=$1 TRIPLE=$2 FLAGS=$3 EMU=$4 W TEXT=none
  for W in old new; do
    local SRC=$OLD PRE=${RENDER_OLD:-}
    [ "$W" = new ] && { SRC=$NEW; PRE=${RENDER_NEW:-}; }
    if [ -n "$PRE" ]; then
      cp "$PRE" "$OUT/bits/render-$T-$W" || { echo "$T: cannot copy $PRE"; RESULT=2; return; }
    else
      $ZIG c++ -O2 -std=c++17 -pthread -target "$TRIPLE" $FLAGS -static \
        -I"$SDK/framework" -I"$(dirname "$SRC")" "-DSRC=\"$(basename "$SRC")\"" "$HERE/render.cpp" \
        -o "$OUT/bits/render-$T-$W" || { echo "$T: build of the $W source failed"; RESULT=2; return; }
    fi
    $EMU "$OUT/bits/render-$T-$W" "$OUT/bits/$T-$W.bin" > "$OUT/bits/$T-$W.log" \
      || { echo "$T: run of the $W build failed"; RESULT=2; return; }
    if [ "$W" = old ]; then
      $EMU "$OUT/bits/render-$T-old" "$OUT/bits/$T-old2.bin" > /dev/null \
        || { echo "$T: second run of the old build failed"; RESULT=2; return; }
      if ! cmp -s "$OUT/bits/$T-old.bin" "$OUT/bits/$T-old2.bin"; then
        echo "$T: NOT REPEATABLE: two runs of the old build differ, so no comparison means anything"
        measure "$T" "$OUT/bits/$T-old.bin" "$OUT/bits/$T-old2.bin" "$OUT/bits/$T-control.txt"
        RESULT=2; return
      fi
      TEXT=yes
      [ -s "$OUT/bits/$T-old.bin.txt" ] || TEXT=none
      : > "$OUT/bits/$T-unstable.txt"
      if [ "$TEXT" = yes ] && ! cmp -s "$OUT/bits/$T-old.bin.txt" "$OUT/bits/$T-old2.bin.txt"; then
        if [ "$(wc -l < "$OUT/bits/$T-old.bin.txt")" = "$(wc -l < "$OUT/bits/$T-old2.bin.txt")" ]; then
          # one line per kind that differs anywhere: "P <index>" a parameter, "S" the saved state,
          # "H" the program names
          awk 'NR == FNR { a[FNR] = $0; next }
               a[FNR] != $0 { k = $1 ~ /^[0-9]+$/ ? "P " $1 : ($2 == "state:" ? "S" : "H"); if (!(k in s)) { s[k] = 1; print k } }' \
            "$OUT/bits/$T-old.bin.txt" "$OUT/bits/$T-old2.bin.txt" > "$OUT/bits/$T-unstable.txt"
        else
          TEXT=unstable
        fi
      fi
      rm -f "$OUT/bits/$T-old2.bin" "$OUT/bits/$T-old2.bin.txt"
    fi
  done
  if cmp -s "$OUT/bits/$T-old.bin" "$OUT/bits/$T-new.bin"; then
    echo "$T: IDENTICAL, $(stat -c %s "$OUT/bits/$T-old.bin") bytes ($(tail -1 "$OUT/bits/$T-old.log")); the old build repeats itself"
  else
    local AT
    AT=$(cmp "$OUT/bits/$T-old.bin" "$OUT/bits/$T-new.bin" | sed 's/.*byte \([0-9]*\).*/\1/')
    echo "$T: DIFFERENT, first at byte ${AT:-?}"
    awk -v at="${AT:-0}" '$1 == "phase" && $NF + 0 < at { p = $0 } END { if (p != "") print "  in " p }' "$OUT/bits/$T-old.log"
    measure "$T" "$OUT/bits/$T-old.bin" "$OUT/bits/$T-new.bin" "$OUT/bits/$T-diff.txt"
    [ "$RESULT" = 2 ] || RESULT=1
  fi
  case "$TEXT" in
    none) echo "$T: read-outs and saved state not compared (these render programs do not write them)" ;;
    unstable) echo "$T: read-outs and saved state not compared (two runs of the old build wrote a different number of lines)" ;;
    *) local U W2 LEFT=""
       U=$(awk '$1 == "P" { n = n " " $2 } $1 == "S" { o = o ", the saved state" } $1 == "H" { o = o ", the program names" }
                END { if (n != "") n = "parameters" n; else o = substr(o, 3); printf "%s%s", n, o }' "$OUT/bits/$T-unstable.txt")
       for W2 in old new; do   # the same kinds out of both files
         awk -v U="$OUT/bits/$T-unstable.txt" '
              BEGIN { while ((getline x < U) > 0) { split(x, q, " "); if (q[1] == "P") p[q[2]] = 1; else k[q[1]] = 1 } }
              $1 ~ /^[0-9]+$/ { if (!($1 in p)) print; next }
              $2 == "state:" { if (!("S" in k)) print; next }
              { if (!("H" in k)) print }' \
           "$OUT/bits/$T-$W2.bin.txt" > "$OUT/bits/$T-$W2.cmp.txt"
       done
       [ -z "$U" ] || LEFT="; left out, since two runs of the old build differ there: $U"
       if cmp -s "$OUT/bits/$T-old.cmp.txt" "$OUT/bits/$T-new.cmp.txt"; then
         echo "$T: read-outs and saved state IDENTICAL ($(wc -l < "$OUT/bits/$T-old.cmp.txt") lines$LEFT)"
       else
         echo "$T: read-outs or saved state DIFFERENT (old <, new >; full files in $OUT/bits/$T-old.bin.txt and $T-new.bin.txt$LEFT):"
         diff "$OUT/bits/$T-old.cmp.txt" "$OUT/bits/$T-new.cmp.txt" | head -n 12 | sed 's/^/  /'
         [ "$RESULT" = 2 ] || RESULT=1
       fi ;;
  esac
}
run_target arm32 arm-linux-musleabihf "-mcpu=cortex_a17" "${BITCHECK_EMU:-qemu-arm}"
if [ -n "${RENDER_OLD:-}${RENDER_NEW:-}" ]; then
  echo "arm64: skipped (prebuilt programs were given)"
elif [ "${BITCHECK_ARM64:-1}" = 0 ]; then
  echo "arm64: skipped (BITCHECK_ARM64=0)"
elif command -v qemu-aarch64 > /dev/null; then
  run_target arm64 aarch64-linux-musl "" qemu-aarch64
else
  echo "arm64: skipped (qemu-aarch64 not installed)"
fi
exit "$RESULT"
