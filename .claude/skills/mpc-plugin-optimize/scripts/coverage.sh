#!/bin/bash
# coverage.sh [out-dir ...]
#
# Writes <out-dir>/coverage.md: one row for every function that executed instructions in any profile
# in the folders given (profile_k*.txt from profile.sh), largest first, with its counts from the
# assembly scan (scan_all.txt from scan.sh) and an empty Verdict column. With one folder per scenario,
# name them all: the table goes in the first, and the scan is taken from the first that has one.
# Fill in every row before choosing what to change: "candidate: <what>, about <n> per frame" or
# "read, nothing: <why>". The filled table goes in the final report, so a part of the plugin that
# nobody looked at shows as an empty row.
# Run profile.sh for steady state (k 0), for each heavy mode held on and for the random-parameter
# run first, and scan.sh once. Library and harness functions are listed without scan counts.
# Needs: awk, sort, c++filt.
set -euo pipefail
OUT=${1:-mpc-opt-out}
[ $# -gt 0 ] || set -- "$OUT"
PROFILES=(); SCAN=
for D in "$@"; do
  for F in "$D"/profile_k*.txt; do
    if [ -s "$F" ]; then PROFILES+=("$F"); fi
  done
  if [ -z "$SCAN" ] && [ -s "$D/scan_all.txt" ]; then SCAN="$D/scan_all.txt"; fi
done
[ ${#PROFILES[@]} -gt 0 ] || { echo "no profile_k*.txt in $*: run profile.sh first"; exit 2; }
[ -n "$SCAN" ] || { echo "no scan_all.txt in $*: run scan.sh into one of the folders first"; exit 2; }
awk '
  FNR == 1 { file++ }
  FILENAME ~ /scan_all\.txt$/ { sub(/:$/, "", $1); s[$1] = $3 " | " $5 " | " $7 " | " $9 " | " $11 " | " $13 " | " $15; next }
  $4 == "TOTAL" || $4 == "main" || $4 == "icMark" || $4 == "icEnd" || $4 ~ /hostCb/ || $4 ~ /sendNotes/ { next }
  { f = $4; if ($2 + 0 > big[f]) big[f] = $2 + 0; if ($3 + 0 > med[f]) med[f] = $3 + 0; seen[f] = 1 }
  END { for (f in seen) printf "%d\t%d\t%s\t%s\n", (big[f] > med[f] ? big[f] : med[f]), med[f], (f in s ? s[f] : " |  |  |  |  |  | "), f }
' "$SCAN" "${PROFILES[@]}" | sort -k1,1nr | c++filt | awk -F'\t' '
  BEGIN { print "| Function | Costly block | Median block | Size | Div/sqrt | f64 | Lib calls | Compares | Spills | Reloads | Verdict |"
          print "|---|---|---|---|---|---|---|---|---|---|---|" }
  { name = $4; gsub(/\|/, "\\|", name); printf "| `%s` | %d | %d | %s |  |\n", substr(name, 1, 120), $1, $2, $3; n++ }
  END { printf "\n%d functions executed. Counts are instructions in one 128-frame block: the largest over the profiled blocks, and the median block.\n", n }
' > "$OUT/coverage.md"
echo "wrote $OUT/coverage.md from ${#PROFILES[@]} profiles: $(grep -c '^| `' "$OUT/coverage.md") rows to fill in"
