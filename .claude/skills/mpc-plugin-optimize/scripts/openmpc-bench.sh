#!/bin/bash
# openmpc-bench.sh <plugin-arm32.so> [device-ip] [bench args...]
#
# Fetches the current CPU bench of the Open MPC plugin catalog (tools/bench.c, MIT license, from
# https://github.com/open-mpc/mpc-vst-plugins), builds it unchanged for the 32-bit MPCs, and with a
# device address runs it there. This is the bench that decides PASS, WARN or FAIL for a catalog
# listing; its rules are in the BENCH.md fetched beside it.
#   plugin-arm32.so   the built plugin, e.g. <project>/build/<Manufacturer> - VST - <Name>/VST/<slug>-arm32.so
#   device-ip         the MPC's address (developer mode on). Omit it to fetch and build only.
#   bench args        passed to the bench, e.g. -p (sweep every parameter) -j (add a JSON line)
# With a device address, the bench and a copy of the plugin go to /tmp on the MPC and are deleted
# afterwards. Nothing is installed and the MPC app keeps running. Get the owner's agreement first.
# Environment: OUT (default ./mpc-opt-out), ZIG (default zig), BENCH_CPUMASK (default 2 = core 1),
#   OPENMPC_REF (branch, tag or commit to fetch, default main).
# Needs: curl, zig (or ZIG="python3 -m ziglang"), and for a device run ssh and scp. Linux or WSL.
set -euo pipefail
[ $# -ge 1 ] || { sed -n '2,15p' "$0"; exit 2; }
SO=$1; shift
IP=""
if [ $# -ge 1 ] && [ "${1#-}" = "$1" ]; then IP=$1; shift; fi
OUT=${OUT:-mpc-opt-out}
ZIG=${ZIG:-zig}
REF=${OPENMPC_REF:-main}
RAW=https://raw.githubusercontent.com/open-mpc/mpc-vst-plugins/$REF
[ -f "$SO" ] || { echo "no such file: $SO"; exit 2; }
mkdir -p "$OUT/openmpc"
for f in tools/bench.c docs/BENCH.md LICENSE; do
  curl -fsSL "$RAW/$f" -o "$OUT/openmpc/$(basename "$f").new" \
    || { echo "could not fetch $f from Open MPC ($RAW)"; exit 2; }
  mv "$OUT/openmpc/$(basename "$f").new" "$OUT/openmpc/$(basename "$f")"
done
echo "fetched Open MPC bench ($REF): $(wc -c < "$OUT/openmpc/bench.c") bytes, sha256 $(sha256sum "$OUT/openmpc/bench.c" | cut -c1-12)"
# Same compiler floor as their Docker image for the 32-bit models: glibc 2.31, dynamic (it loads the .so).
$ZIG cc -target arm-linux-gnueabihf.2.31 -mcpu=cortex_a17 -O2 -Wall \
  -o "$OUT/openmpc/bench-armhf" "$OUT/openmpc/bench.c" -ldl -lm
echo "built $OUT/openmpc/bench-armhf; the rules are in $OUT/openmpc/BENCH.md"
if [ -z "$IP" ]; then
  echo "no device address given: stopping after the build"
  exit 0
fi
OPTS=(-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=accept-new -o LogLevel=ERROR)
ARCH=$(ssh "${OPTS[@]}" "root@$IP" uname -m) || { echo "cannot reach the MPC at $IP"; exit 2; }
case "$ARCH" in
  arm*) ;;
  *) echo "the device is $ARCH, not a 32-bit MPC: use Open MPC's own tools/bench.sh for it"; exit 2 ;;
esac
MASK=${BENCH_CPUMASK:-2}
B=$(basename "$SO")
clean() { ssh "${OPTS[@]}" "root@$IP" "rm -f /tmp/vstbench '/tmp/$B'" || true; }
trap clean EXIT
scp -q "${OPTS[@]}" "$OUT/openmpc/bench-armhf" "root@$IP:/tmp/vstbench"
scp -q "${OPTS[@]}" "$SO" "root@$IP:/tmp/$B"
set +e
ssh "${OPTS[@]}" "root@$IP" "chmod +x /tmp/vstbench; taskset $MASK /tmp/vstbench '/tmp/$B' $*" | tee "$OUT/openmpc/result.txt"
R=${PIPESTATUS[0]}
set -e
echo "bench exit code: $R (1 = FAIL); output saved to $OUT/openmpc/result.txt"
exit "$R"
