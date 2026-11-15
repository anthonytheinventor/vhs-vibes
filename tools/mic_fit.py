"""The numbers behind the four microphones of VHS Vibes 1.6 (MicModel in src/VHSVibes.cpp).

Pure Python, no packages. RBJ cookbook biquads evaluated at a sample rate, fitted with
Nelder-Mead to the published points the source comments name.

    python tools/mic_fit.py check              the filters in the source against their targets, every rate
    python tools/mic_fit.py consts             the constants that need a little numerical work
    python tools/mic_fit.py fit phone          refit the P.48 curve (minutes)
    python tools/mic_fit.py fit dynamic RATE   refit the Shure curve for 44100, 88200 or 192000
    python tools/mic_fit.py fit capsule RATE   refit the WM-61A rise for 44100, 48000, 88200 or 192000
    python tools/mic_fit.py fit camamp RATES   refit the camcorder mic amp's band (44100; 48000; 88200 96000; 192000)
"""
import cmath
import math
import random
import sys

RATES = (44100.0, 48000.0, 88200.0, 96000.0, 192000.0)


def rbj(kind, f0, q, gdb, fs):
    A = 10 ** (gdb / 40)
    w = 2 * math.pi * min(f0, 0.49 * fs) / fs
    c, s = math.cos(w), math.sin(w)
    al = s / (2 * q) if q else 0
    if kind == 'h': return [(1 + c) / 2, -(1 + c), (1 + c) / 2], [1 + al, -2 * c, 1 - al]
    if kind == 'l': return [(1 - c) / 2, 1 - c, (1 - c) / 2], [1 + al, -2 * c, 1 - al]
    if kind == 'p': return [1 + al * A, -2 * c, 1 - al * A], [1 + al / A, -2 * c, 1 - al / A]
    if kind == 'n': return [1, -2 * c, 1], [1 + al, -2 * c, 1 - al]
    if kind == '1':   # first-order highpass, bilinear, exact at its corner
        k = math.tan(math.pi * f0 / fs); m = 1 / (1 + k)
        return [m, -m, 0], [1, (k - 1) * m, 0]
    if kind == 'L':   # first-order lowpass: its pole matched to f0, its zero at -q (q = 1: at half the rate)
        return [1, q, 0], [1, -math.exp(-2 * math.pi * f0 / fs), 0]
    raise ValueError(kind)


def mag(secs, f, fs):
    z = cmath.exp(-2j * math.pi * f / fs)
    h = 1
    for kind, f0, q, g in secs:
        b, a = rbj(kind, f0, q, g, fs)
        h *= (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)
    return 20 * math.log10(abs(h))


def rel(secs, f, fs):
    return mag(secs, f, fs) - mag(secs, 1000.0, fs)


def band(fs):   # the rate bands of the source
    return 3 if fs >= 130000 else 2 if fs >= 70000 else 1 if fs >= 46000 else 0


# ---- targets -----------------------------------------------------------------------------
# CCITT P.48 (1988) Table 1/P.48, sending, dB V/Pa (re 1 kHz: + 3.7)
P48 = [(100, -45.8), (125, -36.1), (160, -25.6), (200, -19.2), (250, -14.3), (300, -11.3), (315, -10.8), (400, -8.4),
       (500, -6.9), (600, -6.3), (630, -6.1), (800, -4.9), (1000, -3.7), (1250, -2.3), (1600, -0.6), (2000, 0.3),
       (2500, 1.8), (3000, 1.5), (3150, 1.8), (3500, -7.3), (4000, -37.2), (5000, -52.2), (6300, -73.6), (8000, -90.0)]
P48 = [(f, d + 3.7) for f, d in P48]
# Shure 515BSM user guide, figure 1 at 0.6 m, read off the plot (dB re 1 kHz)
SHURE = [(50, -16), (100, -8), (200, -4), (500, -1.5), (1000, 0), (2000, 1), (4000, 4), (5000, 5), (6500, 2),
         (8000, 3), (10000, 0), (15000, -10)]
# WM-61A (J. Neal's plot of ten capsules): flat to 5 kHz, +1 at 8 kHz, +1.5..+3 at 13-17 kHz, ~0 at 21.5 kHz.
# (frequency, target, weight)
WM61 = [(300, 0, 4), (1000, 0, 4), (2000, 0, 4), (3000, 0, 4), (4000, 0, 4), (5000, 0, 4), (6000, 0.3, 1),
        (8000, 1.0, 2), (13000, 2.25, 1), (15000, 2.25, 1), (17000, 2.25, 1), (21500, 0, 0.5)]
# Camcorder mic amp: the Sony DCR-TRV900E's mic input (D. Vrancic's table, manual gain 3, dB re
# 1 kHz, as the camcorder report reads it). (frequency, target)
CAMAMP = [(20, -12.8), (50, -3.1), (100, -0.6), (200, 0.0), (1000, 0.0), (2000, -0.2), (5000, -0.6),
          (10000, -1.7), (15000, -3.6), (20000, -4.9)]

# ---- the filters in the source (keep in step with src/VHSVibes.cpp) ----------------------
PHONE = [('h', 325.3, 1.026, 0), ('h', 169.6, 1.433, 0), ('p', 1045.7, 0.253, 13.34), ('l', 1861.7, 1.308, 0),
         ('l', 2773.4, 3.496, 0), ('l', 3330.4, 13.709, 0), ('n', 4135.1, 4.234, 0), ('n', 6971.6, 0.534, 0)]
PHONE_REPORT = [('h', 342.3, 1.11, 0), ('h', 175.4, 1.52, 0), ('p', 1541.6, 0.28, 13.38), ('p', 11881.7, 1.63, 2.25),
                ('l', 2718.8, 1.6, 0), ('l', 3346.1, 9.29, 0), ('n', 3904.9, 1.23, 0), ('n', 4885.9, 1.14, 0)]
DYN = [
    [('h', 101.86, 0.370, 0), ('p', 4597.4, 2.145, 5.29), ('p', 8172.8, 5.749, 2.25), ('l', 10932.9, 0.823, 0)],
    [('h', 102.16, 0.370, 0), ('p', 4578.7, 2.34, 5.17), ('p', 8171.8, 8.0, 2.01), ('l', 10325.8, 0.9, 0)],
    [('h', 101.88, 0.370, 0), ('p', 4579.5, 2.422, 5.17), ('p', 8170.3, 8.937, 2.08), ('l', 11448.6, 1.122, 0), ('l', 11907.9, 0.640, 0)],
    [('h', 101.90, 0.371, 0), ('p', 4578.1, 2.550, 5.04), ('p', 8174.7, 8.928, 2.15), ('l', 11879.7, 1.469, 0), ('l', 8810.5, 0.688, 0)],
]
CAP = [(15307.6, 0.590, 2.50), (15035.1, 0.711, 2.49), (14106.2, 1.200, 2.47), (13867.4, 1.352, 2.47)]
CAMHP = ('h', 38.8, 0.569, 0)
CAMLP = [(19660.6, 0.234), (16080.6, 0.185), (13431.8, 0.091), (13299.6, 0.0)]


def check():
    print('Phone against Table 1/P.48 (points to 3.5 kHz; 4 and 5 kHz; 6.3 and 8 kHz need only be as deep):')
    for name, secs in (('report', PHONE_REPORT), ('source', PHONE)):
        for fs in RATES:
            near = max(abs(rel(secs, f, fs) - d) for f, d in P48 if f <= 3500)
            mid = max(abs(rel(secs, f, fs) - d) for f, d in P48 if 4000 <= f <= 5000)
            deep = ' '.join('%d: %+.1f (%+.1f)' % (f, rel(secs, f, fs), d) for f, d in P48 if f > 5000)
            print('  %-6s %6.0f: %.2f dB, %.2f dB, %s' % (name, fs, near, mid, deep))
    print('Dynamic against the Shure points:')
    for fs in RATES:
        secs = DYN[band(fs)]
        print('  %6.0f: worst %.2f dB' % (fs, max(abs(rel(secs, f, fs) - d) for f, d in SHURE)))
    print('Capsule rise (dB re 1 kHz):')
    for fs in RATES:
        secs = [('p',) + CAP[band(fs)]]
        print('  %6.0f: %s' % (fs, ' '.join('%g: %+.2f' % (f, rel(secs, f, fs)) for f, d, w in WM61 if f < 0.45 * fs)))
    print('Camcorder mic amp against the TRV900 points (dB re 1 kHz, got/read):')
    for fs in RATES:
        secs = [CAMHP, ('L',) + CAMLP[band(fs)] + (0,)]
        pts = [(f, d) for f, d in CAMAMP if f < 0.45 * fs]
        print('  %6.0f: worst %.2f dB; %s' % (fs, max(abs(rel(secs, f, fs) - d) for f, d in pts),
                                            ' '.join('%g: %+.2f/%+.1f' % (f, rel(secs, f, fs), d) for f, d in pts)))


def consts():
    def aw(f):
        f2 = f * f
        ra = 12194.0 ** 2 * f2 * f2 / ((f2 + 20.6 ** 2) * math.sqrt((f2 + 107.7 ** 2) * (f2 + 737.9 ** 2)) * (f2 + 12194.0 ** 2))
        return ra * 10 ** (2.0 / 20)
    n = 200000
    ja = sum(aw(20 + 19980 * (i + 0.5) / n) ** 2 for i in range(n)) * 19980 / n
    print('A-weighted noise bandwidth of white noise, 20 Hz-20 kHz: %.0f Hz' % ja)

    def sc(t):
        t = max(-1.5, min(1.5, t)); return t - 4.0 / 27.0 * t ** 3

    def harm(fn, amp, N=4096, H=9):
        y = [fn(amp * math.sin(2 * math.pi * i / N)) for i in range(N)]
        out = []
        for h in range(1, H + 1):
            a = sum(y[i] * math.sin(2 * math.pi * h * i / N) for i in range(N)) * 2 / N
            b = sum(y[i] * math.cos(2 * math.pi * h * i / N) for i in range(N)) * 2 / N
            out.append(math.hypot(a, b))
        return out

    def thd(fn, amp):
        h = harm(fn, amp); return math.sqrt(sum(v * v for v in h[1:])) / h[0]

    def solve(f, lo, hi, want, rising):
        for _ in range(60):
            mid = math.sqrt(lo * hi) if lo > 0 else 0.5 * (lo + hi)
            if (f(mid) > want) == rising: hi = mid
            else: lo = mid
        return 0.5 * (lo + hi)
    ref = 0.4
    p110 = ref * 10 ** (6 / 20.0)
    sn = solve(lambda s: thd(lambda x: s * 1.9953 * sc(x / (s * 1.9953)) if x > 0 else s * sc(x / s), p110), 0.05, 5.0, 0.01, False)
    print('JFET: the negative half scale that gives a 110 dB SPL sine 1 %% THD: %.3f times its peak' % (sn / p110))
    a = ref * 10 ** ((40.0 / 15.0) / 20)
    c = solve(lambda c: thd(lambda y: y - c * y ** 3, a), 0.0, 1.0, 0.005, True)
    print('ALC cubic: %.5f (0.5 %% THD at the output level of VI = -20 dBm); its turning point 1/sqrt(3c) = %.4f' % (c, 1 / math.sqrt(3 * c)))
    onset = 0.7746e-3 / 10 ** (-35 / 20.0)
    print('ALC onset: -60 dBm from -35 dB V/Pa = %.4f Pa = %.1f dB SPL = %.1f dBFS peak; record gain %.1f dB'
          % (onset, 94 + 20 * math.log10(onset), 20 * math.log10(onset * ref / math.sqrt(10)), -20 * math.log10(onset / math.sqrt(10))))
    ss = solve(lambda s: thd(lambda x: s * sc(x / s), 1.0), 0.05, 5.0, 0.01, False)
    print('Camcorder mic amp: the symmetric cubic scale that gives a sine 1 %% THD: %.3f times its peak' % ss)
    knee = 0.7746e-3 * 10 ** (-6 / 20.0) / 10 ** (-35 / 20.0)
    thr = 10 ** (-22 / 19 / 20.0)
    print('Camcorder ALC: knee -66 dBs from -35 dB V/Pa = %.4f Pa = %.1f dB SPL, %.1f dB below kRefLevel; threshold %.5f x kRefLevel'
          ' (%.2f dB); record gain %.2f dB; at most %.6f (%.2f dB)'
          % (knee, 94 + 20 * math.log10(knee), 10 - 20 * math.log10(knee), thr, 20 * math.log10(thr),
             20 * math.log10(thr * math.sqrt(10) / knee), 10 ** (-22 * 18 / 19 / 20.0), -22 * 18 / 19))
    print('line noise: -65 dBm0, 1 Pa = %.2f dBm0: %.4e of the 1 Pa rms' % (20 * math.log10(10 ** (-3.7 / 20) / 0.7746), 10 ** ((-65 - 20 * math.log10(10 ** (-3.7 / 20) / 0.7746)) / 20)))


def nm(E, x0, it=4000, step=0.3):
    n = len(x0); pts = [x0] + [[x0[j] + (step if j == i else 0) for j in range(n)] for i in range(n)]
    vals = [E(p) for p in pts]
    for _ in range(it):
        o = sorted(range(n + 1), key=lambda i: vals[i]); pts = [pts[i] for i in o]; vals = [vals[i] for i in o]
        c = [sum(p[j] for p in pts[:-1]) / n for j in range(n)]
        xr = [c[j] + (c[j] - pts[-1][j]) for j in range(n)]; fr = E(xr)
        if fr < vals[0]:
            xe = [c[j] + 2 * (c[j] - pts[-1][j]) for j in range(n)]; fe = E(xe)
            pts[-1], vals[-1] = (xe, fe) if fe < fr else (xr, fr)
        elif fr < vals[-2]: pts[-1], vals[-1] = xr, fr
        else:
            xc = [c[j] + 0.5 * (pts[-1][j] - c[j]) for j in range(n)]; fc = E(xc)
            if fc < vals[-1]: pts[-1], vals[-1] = xc, fc
            else:
                pts = [pts[0]] + [[pts[0][j] + 0.5 * (p[j] - pts[0][j]) for j in range(n)] for p in pts[1:]]
                vals = [vals[0]] + [E(p) for p in pts[1:]]
    i = min(range(n + 1), key=lambda i: vals[i]); return pts[i], vals[i]


def pack(secs): return sum(([math.log(f), math.log(q)] + ([g] if k == 'p' else []) for k, f, q, g in secs), [])


def unpack(kinds, x):
    out, i = [], 0
    for k in kinds:
        if k == 'p': out.append((k, math.exp(x[i]), math.exp(x[i + 1]), x[i + 2])); i += 3
        else: out.append((k, math.exp(x[i]), math.exp(x[i + 1]), 0)); i += 2
    return out


def fit(secs0, error, rounds=8, seed=5):
    kinds = [s[0] for s in secs0]
    random.seed(seed); best = None
    for r in range(rounds):
        x = pack(secs0) if r == 0 else [v + random.gauss(0, 0.08) for v in best[0]]
        x, e = nm(lambda x: error(unpack(kinds, x)), x)
        if best is None or e < best[1]: best = (x, e)
    return unpack(kinds, best[0])


def show(secs):
    print('  ' + ', '.join("{ '%s', %.1ff, %.3ff, %.2ff }" % s for s in secs))


def bad(secs, fs, qmax):
    return any(not (10 < f < 0.49 * fs) or not (0.2 < q < qmax) or abs(g) > 20 for k, f, q, g in secs)


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else 'check'
    if what == 'check': check()
    elif what == 'consts': consts()
    elif what == 'fit' and sys.argv[2] == 'phone':
        # the report's eight filters with its 11.9 kHz peak as a lowpass, at 48 kHz, all 24 points;
        # points below -30 dB need only be at least as deep
        start = PHONE_REPORT[:3] + [('l', 5000, 0.7, 0)] + PHONE_REPORT[4:]
        def err(s):
            if bad(s, 48000.0, 30): return 1e12
            e = 0
            for f, d in P48:
                m = rel(s, f, 48000.0)
                if not (d < -30 and m < d): e += (m - d) ** 2 * (4 if f >= 6000 else 1)
            return e
        show(fit(start, err, rounds=10))
    elif what == 'fit' and sys.argv[2] == 'dynamic':
        fs = float(sys.argv[3])
        # the report's 48 kHz curve between the read points, and the points themselves three times over
        dense = []
        for (f1, d1), (f2, d2) in zip(SHURE, SHURE[1:]):
            dense += [(math.exp(math.log(f1) + k / 6 * (math.log(f2) - math.log(f1))), 0) for k in range(6)]
        target = [(f, rel(DYN[1], f, 48000.0)) for f, _ in dense] + SHURE * 3
        start = DYN[0] if fs < 46000 else DYN[2]
        show(fit(start, lambda s: 1e12 if bad(s, fs, 10) else sum((rel(s, f, fs) - d) ** 2 for f, d in target)))
    elif what == 'fit' and sys.argv[2] == 'capsule':
        fs = float(sys.argv[3])
        pts = [p for p in WM61 if p[0] < 0.47 * fs]
        show(fit([('p', 14000, 1.0, 2.3)], lambda s: 1e12 if bad(s, fs, 5) else sum(w * (rel(s, f, fs) - d) ** 2 for f, d, w in pts), rounds=4))
    elif what == 'fit' and sys.argv[2] == 'camamp':
        # second-order highpass and first-order lowpass (the report's starting point: 45 Hz, Q 0.6;
        # 14 kHz) to the TRV900 points below 0.45 fs, at every rate of the band together
        rates = [float(r) for r in sys.argv[3:]]
        def err(s):
            if any(bad(s[:1], fs, 5) or not (10 < s[1][1] < 0.49 * fs) for fs in rates) or not (0 <= s[1][2] <= 1): return 1e12
            return sum((rel(s, f, fs) - d) ** 2 for fs in rates for f, d in CAMAMP if f < 0.45 * fs)
        best = fit([('h', 45.0, 0.6, 0), ('L', 14000.0, 0.9, 0)], err, rounds=6)
        show(best)
        for fs in rates:
            print('  %6.0f: %s' % (fs, ' '.join('%g: %+.2f/%+.1f' % (f, rel(best, f, fs), d) for f, d in [(10, -28.1)] + CAMAMP if f < 0.45 * fs)))
    else: print(__doc__)
