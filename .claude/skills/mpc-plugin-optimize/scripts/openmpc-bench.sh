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
# With a device address, the bench and a copy of the plugin go to a new folder under /tmp on the MPC,
# which is deleted afterwards. Nothing is installed and the MPC app keeps running. Get the owner's
# agreement first. The device's host key is kept in <OUT>/openmpc/known_hosts, not in your own file.
# Environment: OUT (default ./mpc-opt-out), ZIG (default zig), BENCH_CPUMASK (default 2 = core 1,
#   the core the catalog's own launcher uses on these models, so the verdict matches the catalog's),
#   BENCH_DATA (a folder whose contents are copied beside the plugin on the device, for a plugin
#   that loads banks or other files from its own folder),
#   OPENMPC_REF (branch, tag or commit to fetch, default main), OPENMPC_DIR (a local checkout of the
#   catalog to take the three files from, with no fetch).
# Needs: curl, zig (or ZIG="python3 -m ziglang"), and for a device run ssh and scp. Linux or WSL.
set -euo pipefail
[ $# -ge 1 ] || { sed -n '2,20p' "$0"; exit 2; }
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
  if [ -n "${OPENMPC_DIR:-}" ]; then
    cp "$OPENMPC_DIR/$f" "$OUT/openmpc/$(basename "$f")" || { echo "no $f in $OPENMPC_DIR"; exit 2; }
    continue
  fi
  curl -fsSL "$RAW/$f" -o "$OUT/openmpc/$(basename "$f").new" \
    || { echo "could not fetch $f from Open MPC ($RAW)"; exit 2; }
  mv "$OUT/openmpc/$(basename "$f").new" "$OUT/openmpc/$(basename "$f")"
done
echo "Open MPC bench (${OPENMPC_DIR:-$REF}): $(wc -c < "$OUT/openmpc/bench.c") bytes, sha256 $(sha256sum "$OUT/openmpc/bench.c" | cut -c1-12)"
# Same compiler floor as their Docker image for the 32-bit models: glibc 2.31, dynamic (it loads the .so).
$ZIG cc -target arm-linux-gnueabihf.2.31 -mcpu=cortex_a17 -O2 -Wall \
  -o "$OUT/openmpc/bench-armhf" "$OUT/openmpc/bench.c" -ldl -lm
echo "built $OUT/openmpc/bench-armhf; the rules are in $OUT/openmpc/BENCH.md"
if [ -z "$IP" ]; then
  echo "no device address given: stopping after the build"
  exit 0
fi
OPTS=(-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=accept-new -o LogLevel=ERROR
      -o "UserKnownHostsFile=$OUT/openmpc/known_hosts")
ARCH=$(ssh "${OPTS[@]}" "root@$IP" uname -m) || { echo "cannot reach the MPC at $IP"; exit 2; }
case "$ARCH" in
  arm*) ;;
  *) echo "the device is $ARCH, not a 32-bit MPC: use Open MPC's own tools/bench.sh for it"; exit 2 ;;
esac
MASK=${BENCH_CPUMASK:-2}
B=$(basename "$SO")
D=/tmp/mpcopt-bench.$$.$RANDOM
ssh "${OPTS[@]}" "root@$IP" "mkdir -m 700 '$D'" || { echo "cannot make $D on the MPC"; exit 2; }
clean() { ssh "${OPTS[@]}" "root@$IP" "rm -rf '$D'" || true; }
trap clean EXIT
# some devices take only the older copy protocol (-O)
copy() { scp -q "${OPTS[@]}" "$1" "$2" 2>/dev/null || scp -q -O "${OPTS[@]}" "$1" "$2"; }
copy "$OUT/openmpc/bench-armhf" "root@$IP:$D/vstbench"
copy "$SO" "root@$IP:$D/$B"
if [ -n "${BENCH_DATA:-}" ]; then
  tar -C "$BENCH_DATA" -cf - . | ssh "${OPTS[@]}" "root@$IP" "tar -C '$D' -xf -" || { echo "could not copy $BENCH_DATA to the MPC"; exit 2; }
fi
set +e
ssh "${OPTS[@]}" "root@$IP" "chmod +x '$D/vstbench'; cd '$D' && taskset $MASK ./vstbench '$D/$B' $*" | tee "$OUT/openmpc/result.txt"
R=${PIPESTATUS[0]}
set -e
echo "bench exit code: $R (1 = FAIL); output saved to $OUT/openmpc/result.txt"
exit "$R"
