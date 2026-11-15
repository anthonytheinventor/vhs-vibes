#!/bin/bash
# The Open MPC release zips (armv7, aarch64, sdk) of a built plugin folder, and the checks that must pass
# before they are uploaded. Run in Linux or WSL from anywhere:
#
#     OMP_REPO=<a clone of open-mpc/mpc-vst-plugins> BENCH=<bench output file> tools/release_openmpc.sh <version>
#
# BENCH is the output of Open MPC's CPU bench run with -j (its JSON line goes into the manifest).
# The zips are written to dist/ only if every check passes.
#
# Why the checks: the zips of 1.4.0 to 1.5.2 were first built from a Windows checkout of Open MPC's tools,
# where git had turned plugin_list.awk into CRLF. The MPC's awk refuses such a file ("Unexpected token"), so
# every install through the Plugin Manager failed, while a check run against that same checkout saw nothing
# wrong. So this script never uses the checkout's files: it takes the tools straight out of git, with the
# line endings the repository has, and then looks at the bytes in the zips.
set -euo pipefail
V=${1:?usage: release_openmpc.sh <version>}
: "${OMP_REPO:?set OMP_REPO to a clone of open-mpc/mpc-vst-plugins}"
: "${BENCH:?set BENCH to the bench output (run with -j)}"
REF=${OMP_REF:-origin/main}
P=$(cd "$(dirname "$0")/.." && pwd)
F="anthonytheinventor - VST - VHS Vibes"
[ -d "$P/build/$F" ] || { echo "no build folder: run the SDK build first" >&2; exit 1; }
grep -q "\"version\": \"$V\"" "$P/build/$F/plugin.json" || { echo "the build folder is not version $V" >&2; exit 1; }

W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
mkdir -p "$W/omp" "$W/out"
# the tools as the repository has them, whatever the checkout on disk looks like
git -C "$OMP_REPO" -c core.autocrlf=false -c safe.directory='*' archive "$REF" tools docs | tar -x -C "$W/omp"
T=${OMP_TOOLS:-$W/omp/tools}          # (OMP_TOOLS: only to prove that the checks below catch a bad copy)

grep '^{"plugin"' "$BENCH" | tail -1 > "$W/bench.json"
[ -s "$W/bench.json" ] || { echo "no JSON line in $BENCH: run the bench with -j" >&2; exit 1; }
# The plugin folder for these zips gets a second copy of each binary at its top, beside the one in VST/.
# After an install on MPC OS 3.10.1 the Plugin Manager rewrites plugin.json, and up to at least its 1.3.0 it
# names the binary by its bare file name (open-mpc/plugin-manager#12), which for the SDK layout points at
# nothing: the plugin installs and is not listed. With a copy at the top that name is a real file, and a
# manager that keeps the path works as well. (Loaded from there on an MPC One+, 3.10.1.12, 2026-11-15.)
mkdir -p "$W/src" && cp -a "$P/build/$F" "$W/src/" && cp "$W/src/$F/VST/"*.so "$W/src/$F/"
(cd "$W/src" && zip -q -r "$W/in.zip" "$F")
python3 "$T/sdk_release.py" "$W/in.zip" --version "$V" --id vhs-vibes --repo anthonytheinventor/vhs-vibes --license MIT -o "$W/out" -- --bench "$W/bench.json" | tail -1
python3 "$T/sdk_zip.py" "$W/out/VHS-Vibes-$V-mpc-sdk.zip" "$W/out"/*-mpc-armv7.zip "$W/out"/*-mpc-aarch64.zip

fail=0
# 1. Open MPC's own checker, run from the clean tools: no errors and no warnings
for z in "$W/out"/*-mpc-armv7.zip "$W/out"/*-mpc-aarch64.zip; do
    r=$(python3 "$W/omp/tools/catalog_check.py" --catalog "$z" 2>&1 | tail -1)
    echo "catalog_check $(basename "$z"): $r"
    case $r in "OK: vhs-vibes $V (0 warnings)") ;; *) fail=1 ;; esac
done
# 2. the bytes: nothing the MPC runs or reads may hold a carriage return, and the files copied from
#    Open MPC's tools must be the repository's own, byte for byte
python3 - "$W/out" "$W/omp/tools" "$V" <<'EOF' || fail=1
import os, sys, zipfile
out, tools, v = sys.argv[1:4]
bad = 0
canon = {}
for root, _, files in os.walk(tools):
    for f in files:
        if f in ("plugin_list.awk",) and os.path.basename(root) == "release":
            canon[f] = open(os.path.join(root, f), "rb").read()
if "plugin_list.awk" not in canon:
    print("FAIL  plugin_list.awk not found in Open MPC's tools"); bad = 1
for kind in ("armv7", "aarch64"):
    name = "VHS-Vibes-%s-mpc-%s.zip" % (v, kind)
    zf = zipfile.ZipFile(os.path.join(out, name))
    for n in zf.namelist():
        if "/portable/" in n or n.endswith("/"):
            continue                                   # the plugin folder itself: the MPC app reads it, not a shell
        data, base = zf.read(n), n.rsplit("/", 1)[-1]
        if b"\r" in data:
            print("FAIL  %s: %s has %d carriage returns" % (name, base, data.count(b"\r"))); bad = 1
        if base in canon and data != canon[base]:
            print("FAIL  %s: %s is not Open MPC's own file" % (name, base)); bad = 1
        if base.endswith(".sh") and not data.startswith(b"#!/bin/sh\n"):
            print("FAIL  %s: %s does not start with #!/bin/sh" % (name, base)); bad = 1
print("bytes: %s" % ("FAIL" if bad else "ok (no carriage returns, helper script is Open MPC's own)"))
sys.exit(bad)
EOF
# 3. the scripts parse, and the helper runs: with the MPC's own tools (BusyBox) when this machine has them
mkdir -p "$W/x" && unzip -q -o "$W/out/VHS-Vibes-$V-mpc-armv7.zip" -d "$W/x"
X="$W/x/VHS-Vibes-$V"
SH=sh; AWK=awk; which busybox >/dev/null 2>&1 && { SH="busybox sh"; AWK="busybox awk"; }
for s in install.sh uninstall.sh; do $SH -n "$X/$s" || { echo "FAIL  $s does not parse ($SH)"; fail=1; }; done
printf '<VALUE name="pluginList-arm">\n</VALUE>\n' > "$W/settings"
: > "$W/entry"
$AWK -v mode=add -v file=/x/a.so -v alt=/x/b.so -v uid=41596c4d -v entryfile="$W/entry" -v listkey=pluginList-arm -f "$X/plugin_list.awk" "$W/settings" > /dev/null \
    || { echo "FAIL  plugin_list.awk does not run ($AWK)"; fail=1; }
echo "scripts: parsed with '$SH', helper run with '$AWK'"

# 4. what the Plugin Manager does after an install: its script rewrites plugin.json, and the binary it names
#    must be in the folder, on both kinds of MPC
for a in armv7:armv7l:file32 aarch64:aarch64:file64; do
    zipname=${a%%:*}; rest=${a#*:}; arch=${rest%%:*}; key=${rest#*:}
    rm -rf "$W/m" && mkdir -p "$W/m/z" "$W/m/root/nvme/VST2 Plugins"
    unzip -q "$W/out/VHS-Vibes-$V-mpc-$zipname.zip" -d "$W/m/z"
    cp -a "$W/m/z/VHS-Vibes-$V/portable/$F" "$W/m/root/nvme/VST2 Plugins/"
    ARCH=$arch ROOT="$W/m/root" sh "$W/omp/tools/vst2_migrate.sh" --json "/nvme/VST2 Plugins/$F" > /dev/null || { echo "FAIL  the manager's step did not run ($zipname)"; fail=1; continue; }
    so=$(sed -n "s/.*\"$key\": \"\(.*\)\".*/\1/p" "$W/m/root/nvme/VST2 Plugins/$F/plugin.json")
    if [ -n "$so" ] && [ -f "$W/m/root/nvme/VST2 Plugins/$F/$so" ]; then echo "manager step ($zipname): $key = $so, in the folder"
    else echo "FAIL  manager step ($zipname): $key = \"$so\" is not in the folder"; fail=1; fi
done

[ $fail = 0 ] || { echo "NOT RELEASED: a check failed, nothing was written to dist/" >&2; exit 1; }
mkdir -p "$P/dist" && cp "$W/out"/*.zip "$P/dist/"
echo "ok: $(cd "$W/out" && ls *.zip | tr '\n' ' ')written to dist/"
