// Round 3: fitted HF self-erasure correction, FAST versus SLOPE, Bias knob sweeps, fixed latency.
//   r3 fit            fit the correction for both engines and speeds, report errors with and without
//   r3 sweep          Bias sweeps 25 -> 150 % in 0.5 s: discontinuities and noise floor per update rate
//   r3 save=<file> nu=..  configuration (with fixed latency) for the arm32 cost harness
#define main r2_main
#include "r2.cpp"
#undef main
#include <functional>

static const double kLfix = 10.0;   // bulk delay removed before the FIR center: the same for both engines (samples)
static const double kBiasSet[] = {25, 50, 75, 100, 150};

// playback FIR with a fixed total latency (kLfix + 31.5 samples) whatever the engine
static void designFirFixed(rt2::Cfg& c, const TOpt& t, bool ep, double* lfitOut = nullptr)
{
    const int N = rt2::kFir, nb = N / 2;
    double L;
    Design D = measureTargets(c, t, ep, N, L);
    if (lfitOut) *lfitOut = L;
    const double tau0 = 0.5 * (N - 1);
    for (int j = 0; j < t.J; ++j)
    {
        std::vector<cd> H(N);
        for (int b = 0; b <= nb; ++b)
        {
            const double f = b == 0 ? 0.0 : 44100.0 * b / N, w = 2 * kPi * f / 44100.0, fm = D.f[b];
            // D.T had the measured delay L removed; put back the difference to the fixed one
            const cd val = b == nb ? cd(0) : D.T[j][b] * std::polar(1.0, 2 * kPi * fm / 44100.0 * (L - kLfix)) * std::polar(1.0, -w * tau0);
            H[b] = val; if (b > 0 && b < nb) H[N - b] = std::conj(val);
        }
        for (int k = 0; k < N; ++k) { cd s = 0; for (int b = 0; b < N; ++b) s += H[b] * std::polar(1.0, 2 * kPi * b * k / N); c.g[j].fir[k] = (float)(s.real() / N); }
    }
}
static void baseFixed(rt2::Cfg& c, const TOpt& t, bool ep, double* lfit = nullptr)
{
    build(c, t, ep, 100, nullptr);
    c.lat = 14;
    designFirFixed(c, t, ep, lfit);
}

struct HfP { double fc = 2000, e0 = 1e9, c = 0; };
static rt2::HfCorr makeCorr(const HfP& p, bool ep)
{
    rt2::HfCorr h;
    h.kSig = (float)(44100.0 / (ep ? kEP : kSP));
    h.e0 = (float)p.e0; h.c = (float)p.c;
    h.aAtt = (float)(1 - std::exp(-1.0 / (0.0005 * 44100))); h.aRel = (float)(1 - std::exp(-1.0 / (0.030 * 44100)));
    h.hpA = (float)std::exp(-2 * kPi * p.fc / 44100.0);
    return h;
}
static std::vector<float> applyCorr(const HfP& p, bool ep, const std::vector<float>& hs, const std::vector<float>& y)
{
    rt2::HfCorr h = makeCorr(p, ep);
    std::vector<float> o(y.size());
    for (size_t i = 0; i < y.size(); ++i) { const rt2::v2 r = h.run(rt2::v2{hs[i], 0.0f}, rt2::v2{y[i], 0.0f}); o[i] = r[0]; }
    return o;
}

struct Tone { bool ep; double f, lv, bp, h; std::vector<float> hs, y; std::array<double, 3> tr; };
static const size_t kN = 16384, kSettle = 4096;
static void harm(const std::vector<float>& y, double f, double h, double m[3])
{
    const size_t n = f < 200 ? 32768 : kN;
    for (int i = 0; i < 3; ++i) { const int hn = 2 * i + 1; m[i] = hn * f <= 22050 ? std::abs(project(y, kSettle, n, hn * f)) / h : 0.0; }
}
static std::vector<Tone> collect(const TOpt& t)
{
    std::vector<Tone> v;
    for (int ep = 0; ep <= 1; ++ep)
    {
        rt2::Cfg base; baseFixed(base, t, ep);
        for (double bp : kBiasSet)
        {
            rt2::Cfg c; build(c, t, ep, bp, &base); c.lat = 14;
            for (double f : {100.0, 1000.0, 3000.0, 5000.0, 10000.0})
                for (double lv : {-20.0, -10.0, 0.0, 6.0})
                {
                    Tone T; T.ep = ep; T.f = f; T.lv = lv; T.bp = bp;
                    T.h = kK * kRef * std::pow(10.0, lv / 20) * std::abs(preEmph(f, ep));
                    const size_t n = (f < 200 ? 32768 : kN) + kSettle;
                    T.hs.resize(n);
                    for (size_t i = 0; i < n; ++i) T.hs[i] = (float)(T.h * std::sin(2 * kPi * f * i / 44100.0));
                    T.y = runCore(c, n, [&](long i) { return (double)T.hs[(size_t)i]; });
                    T.tr = truth(ep, f, lv, bp);
                    v.push_back(std::move(T));
                }
        }
    }
    return v;
}
struct Err { double out, h3, h5; bool okOut, ok3, ok5; };
static Err errOf(const Tone& T, const std::vector<float>& y)
{
    double m[3]; harm(y, T.f, T.h, m);
    const double rOut = dB(T.tr[0] / T.h), rH3 = dB(T.tr[1] / T.tr[0]), rH5 = dB(T.tr[2] / T.tr[0]);
    const double outDbfs = rOut + dB(T.h / kK) - dB(std::abs(preEmph(T.f, T.ep)));
    Err e{dB(m[0]) - rOut, dB(m[1] / m[0]) - rH3, dB(m[2] / m[0]) - rH5, outDbfs > -80, outDbfs > -80 && rH3 > -60 && 3 * T.f <= 20000, outDbfs > -80 && rH5 > -60 && 5 * T.f <= 20000};
    return e;
}
static void summary(const char* name, const std::vector<Tone>& v, const std::vector<std::vector<float>>& ys)
{
    for (int ep = 0; ep <= 1; ++ep)
    {
        std::printf("  %s %s:\n", name, ep ? "EP" : "SP");
        for (double bp : kBiasSet)
        {
            double w[3] = {}, s[3] = {}; int n[3] = {};
            for (size_t i = 0; i < v.size(); ++i)
            {
                if (v[i].ep != (bool)ep || v[i].bp != bp) continue;
                const Err e = errOf(v[i], ys[i]);
                const double d[3] = {e.out, e.h3, e.h5}; const bool ok[3] = {e.okOut, e.ok3, e.ok5};
                for (int q = 0; q < 3; ++q) if (ok[q]) { w[q] = std::max(w[q], std::fabs(d[q])); s[q] += d[q] * d[q]; n[q]++; }
            }
            std::printf("    bias %3.0f%%: out worst %5.2f rms %4.2f | H3 worst %5.2f rms %4.2f | H5 worst %5.2f rms %4.2f\n", bp,
                        w[0], std::sqrt(s[0] / std::max(1, n[0])), w[1], std::sqrt(s[1] / std::max(1, n[1])), w[2], std::sqrt(s[2] / std::max(1, n[2])));
        }
    }
}

static void fit()
{
    loadCache();
    TOpt tf; tf.nu = 1; tf.adaa = true; tf.os = 2;
    TOpt ts = tf; ts.nu = 17;
    std::vector<Tone> vF = collect(tf), vS = collect(ts);
    std::printf("# collected %zu tones per engine (2x, ADAA, fixed latency)\n", vF.size());
    const double fcs[] = {500, 700, 1000, 1500, 2000, 3000, 4500, 6000, 9000};
    const double e0s[] = {0, 2, 4, 6, 8, 10, 14, 18, 24};
    const double cs[] = {0.005, 0.01, 0.02, 0.03, 0.05, 0.08, 0.12, 0.2, 0.3, 0.45};
    HfP best[2][2];
    for (int eng = 0; eng < 2; ++eng)
        for (int ep = 0; ep <= 1; ++ep)
        {
            const std::vector<Tone>& v = eng ? vS : vF;
            std::vector<HfP> grid;
            for (double fc : fcs) for (double e0 : e0s) for (double c : cs) grid.push_back({fc, e0, c});
            std::vector<double> cost(grid.size());
#pragma omp parallel for schedule(dynamic)
            for (int g = 0; g < (int)grid.size(); ++g)
            {
                double s = 0; int n = 0;
                for (const Tone& T : v)
                {
                    if (T.ep != (bool)ep) continue;
                    const Err e = errOf(T, applyCorr(grid[g], ep, T.hs, T.y));
                    if (e.okOut) { s += e.out * e.out; ++n; }
                    if (e.ok3 && T.tr[1] / T.tr[0] > 0.01) { s += 0.25 * e.h3 * e.h3; ++n; }   // H3 above -40 dBc counts a quarter
                }
                cost[g] = s / std::max(1, n);
            }
            int bi = 0; for (int g = 1; g < (int)grid.size(); ++g) if (cost[g] < cost[bi]) bi = g;
            best[eng][ep] = grid[bi];
            std::printf("# best correction %s %s: fc %.0f Hz, knee e0 %.1f, slope c %.3f (cost %.3f; none %.3f)\n", eng ? "SLOPE" : "FAST", ep ? "EP" : "SP",
                        grid[bi].fc, grid[bi].e0, grid[bi].c, cost[bi], 0.0);
        }
    std::vector<std::vector<float>> yF0, yF1, yS0, yS1;
    for (const Tone& T : vF) { yF0.push_back(T.y); yF1.push_back(applyCorr(best[0][T.ep], T.ep, T.hs, T.y)); }
    for (const Tone& T : vS) { yS0.push_back(T.y); yS1.push_back(applyCorr(best[1][T.ep], T.ep, T.hs, T.y)); }
    std::printf("\n== errors against the 12-layer truth (dB), 100 Hz - 10 kHz, -20..+6 dB re reference\n");
    summary("FAST without", vF, yF0); summary("FAST with   ", vF, yF1);
    summary("SLOPE without", vS, yS0); summary("SLOPE with   ", vS, yS1);
    // the correction at low level: change of the output at -20 dB
    double lowMax = 0;
    for (size_t i = 0; i < vS.size(); ++i) if (vS[i].lv <= -20) { double a[3], b[3]; harm(yS0[i], vS[i].f, vS[i].h, a); harm(yS1[i], vS[i].f, vS[i].h, b); lowMax = std::max(lowMax, std::fabs(dB(b[0] / a[0]))); }
    std::printf("# largest change the correction makes at -20 dB (SLOPE): %.3f dB\n", lowMax);
    // FAST against SLOPE (both corrected)
    std::printf("\n== FAST against SLOPE, both with the correction: worst / rms differences (dB), per speed\n");
    for (int ep = 0; ep <= 1; ++ep)
    {
        double w[3] = {}, s[3] = {}; int n[3] = {}; char wh[3][64] = {};
        for (size_t i = 0; i < vF.size(); ++i)
        {
            const Tone& T = vF[i]; if (T.ep != (bool)ep) continue;
            double a[3], b[3]; harm(yF1[i], T.f, T.h, a); harm(yS1[i], T.f, T.h, b);
            const double outDbfs = dB(T.tr[0] / kK) - dB(std::abs(preEmph(T.f, ep)));
            if (outDbfs < -80) continue;
            const double d[3] = {dB(a[0]) - dB(b[0]), dB(a[1] / a[0]) - dB(b[1] / b[0]), dB(a[2] / a[0]) - dB(b[2] / b[0])};
            const bool ok[3] = {true, b[1] / b[0] > 0.001 && 3 * T.f <= 20000, b[2] / b[0] > 0.001 && 5 * T.f <= 20000};
            for (int q = 0; q < 3; ++q) if (ok[q]) { s[q] += d[q] * d[q]; n[q]++; if (std::fabs(d[q]) > w[q]) { w[q] = std::fabs(d[q]); std::snprintf(wh[q], 64, "%.0f Hz %+.0f dB bias %.0f%%", T.f, T.lv, T.bp); } }
        }
        std::printf("  %s: out %.2f (%s) rms %.2f | H3 %.2f (%s) rms %.2f | H5 %.2f (%s) rms %.2f  [harmonics above -60 dBc]\n", ep ? "EP" : "SP",
                    w[0], wh[0], std::sqrt(s[0] / std::max(1, n[0])), w[1], wh[1], std::sqrt(s[1] / std::max(1, n[1])), w[2], wh[2], std::sqrt(s[2] / std::max(1, n[2])));
    }
    // side effects: two-tone intermodulation and a burst's attack, SLOPE SP and EP at correct bias
    std::printf("\n== side effects of the correction (SLOPE, correct bias)\n");
    for (int ep = 0; ep <= 1; ++ep)
    {
        rt2::Cfg c; baseFixed(c, ts, ep);
        const double f1 = 1000, f2 = 7300, a1 = kK * kRef * std::abs(preEmph(f1, ep)), a2 = kK * kRef * 0.5 * std::abs(preEmph(f2, ep));
        const size_t n = 32768 + kSettle;
        std::vector<float> hs(n);
        for (size_t i = 0; i < n; ++i) hs[i] = (float)(a1 * std::sin(2 * kPi * f1 * i / 44100.0) + a2 * std::sin(2 * kPi * f2 * i / 44100.0));
        auto y0 = runCore(c, n, [&](long i) { return (double)hs[(size_t)i]; });
        auto y1 = applyCorr(best[1][ep], ep, hs, y0);
        std::printf("  %s two-tone 1 kHz (0 dB) + 7.3 kHz (-6 dB): component dB re the 7.3 kHz tone, without / with correction:\n   ", ep ? "EP" : "SP");
        for (double fx : {f2 - 2 * f1, f2 - f1, f2, f2 + f1, f2 + 2 * f1, f1})
        {
            const double r0 = std::abs(project(y0, kSettle, 32768, fx)) / std::abs(project(y0, kSettle, 32768, f2));
            const double r1 = std::abs(project(y1, kSettle, 32768, fx)) / std::abs(project(y1, kSettle, 32768, f2));
            std::printf(" %.1f kHz %6.1f / %6.1f |", fx / 1000, dB(r0), dB(r1));
        }
        std::printf("\n   7.3 kHz level change from the correction: %.2f dB; 1 kHz: %.3f dB\n",
                    dB(std::abs(project(y1, kSettle, 32768, f2)) / std::abs(project(y0, kSettle, 32768, f2))),
                    dB(std::abs(project(y1, kSettle, 32768, f1)) / std::abs(project(y0, kSettle, 32768, f1))));
        // burst: 5 kHz at +6 dB, 20 ms on / 80 ms off, over a steady 100 Hz at 0 dB (pumping check)
        const double fb = 5000, ab = kK * kRef * 2.0 * std::abs(preEmph(fb, ep)), al = kK * kRef * std::abs(preEmph(100, ep));
        const size_t nb = 44100;
        std::vector<float> hb(nb);
        for (size_t i = 0; i < nb; ++i)
        {
            const double tt = (double)i / 44100.0, ph = std::fmod(tt, 0.1);
            hb[i] = (float)(al * std::sin(2 * kPi * 100 * tt) + (ph < 0.02 ? ab * std::sin(2 * kPi * fb * tt) : 0.0));
        }
        auto z0 = runCore(c, nb, [&](long i) { return (double)hb[(size_t)i]; });
        auto z1 = applyCorr(best[1][ep], ep, hb, z0);
        // onset: high-band peak in the first 1 ms against the last 5 ms of the burst (with / without correction)
        auto hp = [&](const std::vector<float>& z, size_t a, size_t b) { double m = 0; for (size_t i = a + 1; i < b; ++i) m = std::max(m, std::fabs((double)z[i] - z[i - 1])); return m; };
        double on0 = 0, on1 = 0, st0 = 0, st1 = 0;
        for (int k = 2; k < 9; ++k)
        {
            const size_t s0 = (size_t)(k * 0.1 * 44100);
            on0 = std::max(on0, hp(z0, s0, s0 + 44)); on1 = std::max(on1, hp(z1, s0, s0 + 44));
            st0 = std::max(st0, hp(z0, s0 + 660, s0 + 880)); st1 = std::max(st1, hp(z1, s0 + 660, s0 + 880));
        }
        // 100 Hz modulation: its level in each 10 ms window, spread over the bursts
        double lmin = 1e9, lmax = 0;
        for (size_t s0 = 8820; s0 + 4410 <= nb; s0 += 441)
        {
            std::complex<double> acc = 0; for (size_t i = s0; i < s0 + 4410; ++i) acc += (double)(z1[i] - z0[i]) * std::polar(1.0, -2 * kPi * 100 * i / 44100.0);
            const double l = std::abs(acc) * 2 / 4410 / al; lmin = std::min(lmin, l); lmax = std::max(lmax, l);
        }
        std::printf("   burst 5 kHz +6 dB, 20 ms on/80 ms off: onset/steady slope ratio without %.2f, with %.2f (onset overshoot from the correction %.2f dB); 100 Hz altered by at most %.1f dB re itself\n",
                    on0 / st0, on1 / st1, dB((on1 / st1) / (on0 / st0)), dB(lmax + 1e-12));
    }
    FILE* f = fopen("r3_hfcorr_params.txt", "w");
    for (int eng = 0; eng < 2; ++eng) for (int ep = 0; ep <= 1; ++ep) std::fprintf(f, "%s %s fc %.0f e0 %.2f c %.4f\n", eng ? "SLOPE" : "FAST", ep ? "EP" : "SP", best[eng][ep].fc, best[eng][ep].e0, best[eng][ep].c);
    fclose(f);
}

// Bias sweeps 25 -> 150 % in 0.5 s
static void sweep(int nu)
{
    TOpt t; t.nu = nu; t.adaa = true; t.os = 2;
    const double grid[] = {0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 60, 70, 80, 90, 100, 110, 120, 135, 150};
    const int nb = sizeof(grid) / sizeof(grid[0]);
    std::vector<rt2::Cfg> P(nb);
    rt2::Cfg base; baseFixed(base, t, false);
    for (int b = 0; b < nb; ++b) build(P[b], t, false, grid[b], &base);
    int lat = 14; for (auto& p : P) for (int j = 0; j < t.J; ++j) lat = std::max(lat, p.g[j].io + 4);
    std::vector<double> d0(nb * t.J);
    for (int b = 0; b < nb; ++b) { Plane pl = genPlane(false, grid[b], t); for (int j = 0; j < t.J; ++j) d0[b * t.J + j] = pl.d0[j]; }
    std::printf("# Bias sweep 25 -> 150 %% in 0.5 s, %s, SP, 2x; fixed lat %d\n", nu > 1 ? "SLOPE" : "FAST", lat);
    auto setBias = [&](rt2::Cfg& c, double bp) {
        int b = 0; while (b + 2 < nb && grid[b + 1] <= bp) ++b;
        const double f = std::clamp((bp - grid[b]) / (grid[b + 1] - grid[b]), 0.0, 1.0);
        for (int j = 0; j < t.J; ++j)
        {
            rt2::Group& G = c.g[j]; const rt2::Group& A = P[b].g[j]; const rt2::Group& B = P[b + 1].g[j];
            for (size_t i = 0; i < G.T.size(); ++i) { G.T[i] = (float)((1 - f) * A.T[i] + f * B.T[i]); G.Gi[i] = (float)((1 - f) * A.Gi[i] + f * B.Gi[i]); }
            const double dd = (1 - f) * d0[b * t.J + j] + f * d0[(b + 1) * t.J + j];
            const double p = dd * 44100.0 * t.os, fl = std::floor(p), tt = p - fl;
            G.io = (int)fl;
            const double cv[4] = {-tt * (tt - 1) * (tt - 2) / 6, (tt + 1) * (tt - 1) * (tt - 2) / 2, -(tt + 1) * tt * (tt - 2) / 2, (tt + 1) * tt * (tt - 1) / 6};
            const double cs[4] = {-(3 * tt * tt - 6 * tt + 2) / 6, (3 * tt * tt - 4 * tt - 1) / 2, -(3 * tt * tt - 2 * tt - 2) / 2, (3 * tt * tt - 1) / 6};
            for (int k = 0; k < 4; ++k) { G.cv[k] = (float)cv[k]; G.cs[k] = (float)cs[k]; }
        }
    };
    const size_t n = 44100, s0 = 11025, s1 = s0 + 22050;
    for (int sig = 0; sig < 2; ++sig)
    {
        const double h = sig ? kK * kRef * std::abs(preEmph(1000, false)) : 0.0;
        std::printf(" %s\n", sig ? "1 kHz at reference level" : "silence");
        for (int upd : {0, 32, 64, 128, 256, 512})
        {
            rt2::Cfg c = P[0]; c.lat = lat;
            setBias(c, upd ? 25.0 : 100.0);
            static rt2::Core core; core.init(c);
            std::vector<float> L(n), R(n, 0.0f);
            for (size_t i = 0; i < n; ++i) L[i] = (float)(h * std::sin(2 * kPi * 1000 * i / 44100.0));
            for (size_t i = 0; i < n; i += 32)
            {
                if (upd && i >= s0 && i < s1 && (i % upd) == 0)
                {
                    setBias(c, 25.0 + 125.0 * (double)(i - s0) / (double)(s1 - s0));
                    core.onTableChange();
                }
                if (upd && i == s1) { setBias(c, 150.0); core.onTableChange(); }
                core.process(L.data() + i, R.data() + i, (int)std::min<size_t>(32, n - i));
            }
            // largest second difference in the sweep window, and the highest spectral line off the 1 kHz harmonics (+-60 Hz)
            double d2 = 0; for (size_t i = s0; i < s1 + 2048; ++i) d2 = std::max(d2, std::fabs((double)L[i] - 2.0 * L[i - 1] + L[i - 2]));
            const int N = 16384; std::vector<cd> a(N); double ws = 0;
            for (int i = 0; i < N; ++i) { const double w = 0.5 - 0.5 * std::cos(2 * kPi * i / N); a[i] = L[s0 + 2048 + i] * w; ws += w; }
            fftc(a);
            double floorMax = 0; const double bin = 44100.0 / N;
            for (int i = (int)(40 / bin); i < N / 2; ++i) { const double f = i * bin; if (sig && std::fabs(f - std::round(f / 1000) * 1000) < 60) continue; floorMax = std::max(floorMax, std::abs(a[i]) * 2 / ws); }
            std::printf("   update every %3d samples%s: largest second difference %.3e (output units, field), highest line off the harmonics %.1f dBFS-equivalent\n",
                        upd, upd ? "" : " (static, 100 %)", d2, dB(floorMax / kK));
        }
    }
}

// The plugin's table header (src/hysteresis_tables.h): the remanence planes the plugin's engine (FAST: the
// one that was called Lite) reads, the group delays per bias plane, the FAST playback FIRs with the fixed
// latency for both speeds, and the constants. Each float is written with 9 significant digits, so it reads
// back as exactly the value computed here.
// The planes: the bias values the Bias knob reaches (25 to 150 %: the 0 to 20 % planes of the simulation
// were shipped up to 1.8's review and never read), along the record field A from -kHmaxPlugin to
// +kHmaxPlugin. Up to |A| = 6 (the simulation's cached grid, TOpt::Hmax) a plane is the middle row (u = 0)
// of the SLOPE plane (17 slope points; that row is exactly the FAST plane: the two come from the same
// element runs); past it the same element runs are made at each A (extendRow). At 25 % bias the deepest
// group is far from saturated at 6 (about 0.38, rising to 0.9 at 8 and 1 by 12), so the old edge at 6,
// past which the engine held the end value, was a hard knee that the pre-emphasized treble reaches.
static const double kHmaxPlugin = 12.0;
// The plugin row of a plane (u = 0) out to kHmaxPlugin: the cached plane up to its own edge, element runs
// (s = 0, the group's paths averaged in path order) past it; odd in A.
static std::vector<float> extendRow(bool ep, double bp, const TOpt& t, const Plane& P, int j)
{
    const int nhh = (P.nh - 1) / 2, nx = (int)std::lround(kHmaxPlugin / t.step), row = (P.nu / 2) * P.nh;
    std::vector<float> pos((size_t)nx + 1);
    for (int i = 0; i <= nhh; ++i) pos[(size_t)i] = P.T[j][(size_t)(row + nhh + i)];
    if (nx > nhh)
    {
        const double v = ep ? kEP : kSP, Hb = kHbRight * bp / 100;
        Geometry Gs(gPar, v, t.sub);
        std::vector<std::unique_ptr<RawPath>> paths;
        for (int q = j * t.sub / t.J; q < (j + 1) * t.sub / t.J; ++q) paths.push_back(std::make_unique<RawPath>(Gs.y[q], v, t.nph));
        const int np = (int)paths.size(), tasks = (nx - nhh) * np;
        std::vector<double> m((size_t)tasks);
#pragma omp parallel for schedule(dynamic)
        for (int k = 0; k < tasks; ++k)
        {
            const double A = (nhh + 1 + k / np) * t.step;
            std::vector<double> U;
            m[(size_t)k] = paths[(size_t)(k % np)]->element(Hb, [&](double) { return A; }, U);
        }
        for (int i = nhh + 1; i <= nx; ++i)
        {
            double a = 0; for (int q = 0; q < np; ++q) a += m[(size_t)((i - nhh - 1) * np + q)] / np;
            pos[(size_t)i] = (float)a;
        }
    }
    std::vector<float> r((size_t)(2 * nx + 1));
    for (int i = 0; i <= nx; ++i) { r[(size_t)(nx + i)] = pos[(size_t)i]; r[(size_t)(nx - i)] = -pos[(size_t)i]; }
    return r;
}
// A float literal for v as a float: 9 significant digits (it reads back as exactly that float), with a
// point or an exponent so that "f" may follow.
static std::string ff(double v)
{
    char b[48];
    std::snprintf(b, sizeof(b), "%.9g", (double)(float)v);
    std::string s = b;
    if (s.find_first_of(".en") == std::string::npos) s += ".0";
    return s + "f";
}

static void writePluginHeader(const std::string& fn)
{
    const double grid[] = {25, 30, 35, 40, 45, 50, 60, 70, 80, 90, 100, 110, 120, 135, 150};
    const int nb = sizeof(grid) / sizeof(grid[0]);
    TOpt t; t.nu = 17; t.adaa = true; t.os = 2;
    const int nh = 2 * (int)std::lround(kHmaxPlugin / t.step) + 1;
    FILE* f = fopen(fn.c_str(), "w");
    if (!f) { std::printf("cannot write %s\n", fn.c_str()); return; }
    std::fprintf(f, "// VHS Vibes: tables of the tape hysteresis recorder (Sat Type VHS, Hysteresis On; see src/hysteresis.h).\n"
                    "// Generated by tools/hysteresis/r3.cpp, mode \"plugin\" (the Lite engine's rows only). Do not edit.\n"
                    "// SPDX-License-Identifier: MIT\n#pragma once\nnamespace hysttab {\n");
    std::fprintf(f, "constexpr int kJ = %d, kNh = %d, kNb = %d, kFir = %d, kLat = 14;   // kLat: element lag at twice the rate\n",
                 t.J, nh, nb, rt2::kFir);
    std::fprintf(f, "constexpr float kH0 = %s, kInvStep = %s;   // the planes start at A = kH0, a point every 1 / kInvStep\n", ff(-kHmaxPlugin).c_str(), ff(1.0 / t.step).c_str());
    std::fprintf(f, "constexpr float kHbRight = %s;        // correct bias, in units of the coercivity (deep-gap field)\n", ff(kHbRight).c_str());
    std::fprintf(f, "constexpr float kFieldPerUnit = %s;   // record field per unit of the pre-emphasized signal\n", ff(kK).c_str());
    std::fprintf(f, "constexpr float kSpeed[2] = { %s, %s };   // tape speed, micrometers per second: SP, EP\n", ff(kSP).c_str(), ff(kEP).c_str());
    std::fprintf(f, "constexpr float kBiasPct[kNb] = {");
    for (int b = 0; b < nb; ++b) std::fprintf(f, "%s%s", b ? ", " : " ", ff(grid[b]).c_str());
    std::fprintf(f, " };   // the planes' bias, percent of correct\n");
    for (int ep = 0; ep <= 1; ++ep)
    {
        std::vector<Plane> Ps;
        for (int b = 0; b < nb; ++b) Ps.push_back(genPlane(ep, grid[b], t));
        std::fprintf(f, "// %s: each group's small-signal delay after the gap center, seconds, per bias plane\nconstexpr float kD0%s[kNb][kJ] = {\n",
                     ep ? "EP" : "SP", ep ? "Ep" : "Sp");
        for (int b = 0; b < nb; ++b) { std::fprintf(f, " {"); for (int j = 0; j < t.J; ++j) std::fprintf(f, "%s%s", j ? ", " : "", ff(Ps[b].d0[j]).c_str()); std::fprintf(f, "},\n"); }
        std::fprintf(f, "};\n");
        for (int n2 : {1})
        {
            TOpt tf = t; tf.nu = n2; rt2::Cfg c; baseFixed(c, tf, ep);
            std::fprintf(f, "// %s playback FIRs of the %s engine at 44.1 kHz (taps, newest first), per group; total latency %.1f samples\n"
                            "constexpr float kFir%s%s[kJ][kFir] = {\n", ep ? "EP" : "SP", n2 > 1 ? "SLOPE" : "FAST", kLfix + 31.5, n2 > 1 ? "Slope" : "Fast", ep ? "Ep" : "Sp");
            for (int j = 0; j < t.J; ++j) { std::fprintf(f, " {"); for (int k = 0; k < rt2::kFir; ++k) std::fprintf(f, "%s%s", k ? "," : "", ff(c.g[j].fir[k]).c_str()); std::fprintf(f, "},\n"); }
            std::fprintf(f, "};\n");
        }
        // T(0, 0) is 0 by symmetry; the element runs leave about 1e-10 there, which would turn silence into
        // a faint offset. It is written as 0.
        std::fprintf(f, "// %s remanence planes, -1..1: [bias][group][A], A = kH0 + i / kInvStep (u = 0: no slope axis)\nconstexpr float kT%s[kNb][kJ][kNh] = {\n",
                     ep ? "EP" : "SP", ep ? "Ep" : "Sp");
        for (int b = 0; b < nb; ++b)
        {
            std::fprintf(f, " {\n");
            for (int j = 0; j < t.J; ++j)
            {
                std::fprintf(f, "  {");
                const std::vector<float> r = extendRow(ep, grid[b], t, Ps[b], j);
                for (int k = 0; k < nh; ++k) std::fprintf(f, "%s%s", k ? "," : "", ff(k == nh / 2 ? 0.0 : (double)r[(size_t)k]).c_str());
                std::fprintf(f, "},\n");
            }
            std::fprintf(f, " },\n");
        }
        std::fprintf(f, "};\n");
        std::fflush(f);
    }
    std::fprintf(f, "} // namespace hysttab\n");
    fclose(f);
    std::printf("wrote %s\n", fn.c_str());
}

#ifndef HYST_NO_MAIN
// r3 fit | sweep [nu=..] | firs | lat | plugin [out=<file>]
// Build (Linux or WSL, OpenMP): g++ -O2 -march=native -fopenmp -std=c++17 -o r3 r3.cpp
// Run it in a folder whose planes/ holds the element runs already made (they are cached there by bias,
// speed and grid); a plane that is not there is computed, which takes minutes per plane.
int main(int argc, char** argv)
{
    std::string mode; int nu = 1; std::string out;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a.rfind("nu=", 0) == 0) nu = std::stoi(a.substr(3));
        else if (a.rfind("save=", 0) == 0) { mode = "save"; out = a.substr(5); }
        else if (a.rfind("out=", 0) == 0) out = a.substr(4);
        else mode = a;
    }
    gPar = fitSfd(Par(), 0.40);
    gD = std::make_unique<Density>(gPar);
    gTab = std::make_unique<ETable>(); gTab->build(*gD, 0.01);
    gEv = Eval{nullptr, gTab.get()};
    if (mode == "fit") fit();
    else if (mode == "sweep") sweep(nu);
    else if (mode == "firs")
    {
        // playback FIRs with the fixed latency, for both engines and speeds (replace the header's round-2 FIRs)
        FILE* f = fopen("hyst_firs_fixed.h", "w");
        std::fprintf(f, "// Generated by r3 firs: playback FIRs (64 taps, newest first) with a fixed total latency of %.1f samples.\n#pragma once\nnamespace hysttab {\n", kLfix + 31.5);
        for (int n2 : {1, 17}) for (int ep = 0; ep <= 1; ++ep)
        {
            TOpt t; t.nu = n2; t.adaa = true; t.os = 2; rt2::Cfg c; baseFixed(c, t, ep);
            std::fprintf(f, "constexpr float kFir%s%s[3][64] = {\n", n2 > 1 ? "Slope" : "Fast", ep ? "Ep" : "Sp");
            for (int j = 0; j < 3; ++j) { std::fprintf(f, " {"); for (int k = 0; k < 64; ++k) std::fprintf(f, "%s%.9ef", k ? "," : "", c.g[j].fir[k]); std::fprintf(f, "},\n"); }
            std::fprintf(f, "};\n");
        }
        std::fprintf(f, "constexpr int kLat = 14;   // element lag in 88.2 kHz samples (both engines, both speeds)\n} // namespace hysttab\n");
        fclose(f);
        std::printf("wrote hyst_firs_fixed.h\n");
    }
    else if (mode == "lat")
    {
        for (int ep = 0; ep <= 1; ++ep) for (int n2 : {1, 17})
        {
            TOpt t; t.nu = n2; t.adaa = true; t.os = 2; rt2::Cfg c; double L; baseFixed(c, t, ep, &L);
            std::printf("%s %s: measured bulk delay %.3f samples (fixed to %.1f, total latency %.1f samples = %.3f ms)\n", n2 > 1 ? "SLOPE" : "FAST", ep ? "EP" : "SP", L, kLfix, kLfix + 31.5, (kLfix + 31.5) / 44.1);
        }
    }
    else if (mode == "plugin") writePluginHeader(out.empty() ? "hysteresis_tables.h" : out);
    return 0;
}
#endif
