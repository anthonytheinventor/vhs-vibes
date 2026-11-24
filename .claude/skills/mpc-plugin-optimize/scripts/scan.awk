/^[A-Za-z_][A-Za-z0-9_.$]*:/ { f = $1; next }
f == "" || $1 ~ /^[.@]/ || NF == 0 { next }
{ n[f]++ }
$1 ~ /^(vdiv|vsqrt)\./ { div[f]++ }
$1 ~ /\.f64$/ && $1 !~ /^vmov/ { dbl[f]++ }
$1 ~ /^bl?$/ && $2 ~ /^(sin|cos|tan|exp|pow|log|tanh|floor|ceil|round|lrint|fmin|fmax|sqrt)[fl]?$|^__aeabi_/ { lib[f]++ }
$1 == "vmrs" { cmp[f]++ }
/Spill/ { sp[f]++ }
/Reload/ { rl[f]++ }
END { for (f in n) if (n[f] >= 100)
        printf "%s insns %d div/sqrt %d f64 %d libcalls %d compares %d spills %d reloads %d\n",
               f, n[f], div[f], dbl[f], lib[f], cmp[f], sp[f], rl[f] }
