#!/bin/bash
# devtime.sh <device-ip> <old drive> <new drive> [k] [blocks] [rounds]
#
# Times two builds of the harness on an MPC, alternating old, new, old, new, and prints the thread
# CPU time of every run with the mean of each build at the end. This is the measurement that
# decides whether a change is a win (SKILL.md, steps 8 and 9).
#   old drive, new drive   static arm32 drive programs (count.sh leaves one as <out-dir>/drive)
#   k        parameters set to random values before each block (default 0 = steady state)
#   blocks   blocks per run (default 4000, about 12 seconds of audio)
#   rounds   how many times each build runs (default 3)
# MPC_OPT_EVENTS names an events file for the scenario; it is copied along. MPC_OPT_SILENT,
# MPC_OPT_MARK, MPC_OPT_SKIP, MPC_OPT_FZ and MPC_OPT_INPLACE are passed on. DEV_DATA names a folder
# whose contents are copied beside the programs, for a plugin that loads files from its own folder.
# Both programs go to a new folder under /tmp on the MPC, which is deleted afterwards. Nothing is
# installed and the MPC app keeps running. Get the owner's agreement first.
# Environment: OUT (default ./mpc-opt-out; the result goes to <OUT>/devtime.txt and the device's
#   host key to <OUT>/known_hosts), DEV_CPUMASK (default 4 = core 2; try each core once and use the
#   quietest, reference section 14), DEV_LINE (default thread: the calling thread's CPU time;
#   process: the whole process, for a plugin that does its work on a thread of its own).
# Give the same build twice to see the run-to-run spread before trusting a small difference.
# Needs: ssh and scp, and an MPC with developer mode on. Linux or WSL.
set -euo pipefail
[ $# -ge 3 ] || { sed -n '2,21p' "$0"; exit 2; }
IP=$1; OLD=$2; NEW=$3; K=${4:-0}; BLOCKS=${5:-4000}; ROUNDS=${6:-3}
OUT=${OUT:-mpc-opt-out}
MASK=${DEV_CPUMASK:-4}
LINE=${DEV_LINE:-thread}
[ -f "$OLD" ] && [ -f "$NEW" ] || { echo "no such file: $OLD or $NEW"; exit 2; }
mkdir -p "$OUT"
OPTS=(-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=accept-new -o LogLevel=ERROR
      -o "UserKnownHostsFile=$OUT/known_hosts")
ARCH=$(ssh "${OPTS[@]}" "root@$IP" uname -m) || { echo "cannot reach the MPC at $IP"; exit 2; }
case "$ARCH" in arm*) ;; *) echo "the device is $ARCH, not a 32-bit MPC"; exit 2 ;; esac
D=/tmp/mpcopt-time.$$.$RANDOM
ssh "${OPTS[@]}" "root@$IP" "mkdir -m 700 '$D'" || { echo "cannot make $D on the MPC"; exit 2; }
clean() { ssh "${OPTS[@]}" "root@$IP" "rm -rf '$D'" || true; }
trap clean EXIT
# some devices take only the older copy protocol (-O)
copy() { scp -q "${OPTS[@]}" "$1" "$2" 2>/dev/null || scp -q -O "${OPTS[@]}" "$1" "$2"; }
copy "$OLD" "root@$IP:$D/old"
copy "$NEW" "root@$IP:$D/new"
ENVS=""
if [ -n "${MPC_OPT_EVENTS:-}" ]; then
  copy "$MPC_OPT_EVENTS" "root@$IP:$D/events.txt"
  ENVS="MPC_OPT_EVENTS=$D/events.txt"
fi
for V in MPC_OPT_SILENT MPC_OPT_MARK MPC_OPT_SKIP MPC_OPT_FZ MPC_OPT_INPLACE; do
  [ -z "${!V:-}" ] || ENVS="$ENVS $V='${!V}'"
done
if [ -n "${DEV_DATA:-}" ]; then
  tar -C "$DEV_DATA" -cf - . | ssh "${OPTS[@]}" "root@$IP" "tar -C '$D' -xf -" || { echo "could not copy $DEV_DATA to the MPC"; exit 2; }
fi
CMD="cd '$D' && chmod +x old new"
for R in $(seq 1 "$ROUNDS"); do
  for B in old new; do
    CMD="$CMD; echo \"round $R $B \$(env $ENVS taskset $MASK ./$B $K $BLOCKS 2>/dev/null | grep '$LINE CPU')\""
  done
done
ssh "${OPTS[@]}" "root@$IP" "$CMD" | tee "$OUT/devtime.txt"
awk '$3 == "old" || $3 == "new" { for (i = 1; i <= NF; ++i) if ($i == "mean") { s[$3] += $(i + 1); n[$3]++; if (!($3 in lo) || $(i + 1) < lo[$3]) lo[$3] = $(i + 1); if ($(i + 1) > hi[$3]) hi[$3] = $(i + 1) } }
  END { if (n["old"] == 0 || n["new"] == 0) { print "no timings came back"; exit 1 }
        o = s["old"] / n["old"]; w = s["new"] / n["new"]
        printf "mean of %d rounds: old %.3f %% (%.2f to %.2f), new %.3f %% (%.2f to %.2f): %+.1f %%\n", n["old"], o, lo["old"], hi["old"], w, lo["new"], hi["new"], 100 * (w - o) / o
        if (lo["new"] <= hi["old"] && lo["old"] <= hi["new"]) print "the two ranges overlap: no measured difference at this number of rounds" }' "$OUT/devtime.txt" | tee -a "$OUT/devtime.txt"
