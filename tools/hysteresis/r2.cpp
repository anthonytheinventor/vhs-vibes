// Round 2 harness for the table engine (rt2.h): table generation from the carrier reference,
// playback-filter design, accuracy against the 12-layer carrier simulation, aliasing, and
// configuration export for the arm32 cost count.
//   r2 [nu=17] [step=0.05] [nph=8] [sc=1] [os=4] [J=3] [lin] [iir] <acc|alias|under|tapesat|save=file|gen> [speed=ep] [bias=100]
#include "model.h"
#include "rt2.h"
#include <string>
#include <memory>
#include <array>
#include <map>
using namespace hy;

static const double kHbRight = 4.7377, kK = 2.19036;    // RAW calibration (step3_4_validation.txt)
static Par gPar;
static std::unique_ptr<Density> gD;
static std::unique_ptr<ETable> gTab;
static Eval gEv;

struct TOpt { int J = 3, sub = 12, nu = 17, nph = 8, os = 4; double Hmax = 6.0, step = 0.05, sc = 1.0, umax = 0.95; bool cubic = true, iir = false, adaa = false; };

// ---------------------------------------------------------------- carrier reference element
struct RawPath
{
    double y, v; int n, nPh; double dx, X = 25.0;
    std::vector<double> g, x; std::vector<std::vector<double>> car;
    RawPath(double y_, double v_, int nph) : y(y_), v(v_), nPh(nph)
    {
        dx = v / (gPar.fb * 256); n = (int)std::lround(2 * X / dx) + 1;
        g.resize(n); x.resize(n);
        for (int i = 0; i < n; ++i) { x[i] = -X + i * dx; g[i] = karlqvist(x[i], y, gPar.gap); }
        car.assign(nPh, std::vector<double>(n));
        for (int p = 0; p < nPh; ++p) for (int i = 0; i < n; ++i) car[p][i] = std::sin(2 * kPi * gPar.fb * x[i] / v + 2 * kPi * p / nPh);
    }
    template <class S> double element(double Hb, S sig, std::vector<double>& U) const
    {
        U.resize(n);
        double m = 0;
        for (int p = 0; p < nPh; ++p)
        {
            for (int i = 0; i < n; ++i) U[i] = g[i] * (sig(x[i] / v) + Hb * car[p][i]);
            m += recordPath(gEv, U.data(), U.data(), n);
        }
        return m / nPh;
    }
};

// ---------------------------------------------------------------- planes (tables for one bias)
struct Plane { int J = 0, nh = 0, nu = 0; std::vector<std::vector<float>> T; std::vector<double> d0; };
static std::string planeName(bool ep, double bp, const TOpt& t)
{
    char b[160]; std::snprintf(b, 160, "planes/p_%s_b%.1f_J%d_nu%d_st%.3f_H%.1f_sc%.2f_um%.2f_ph%d.bin", ep ? "ep" : "sp", bp, t.J, t.nu, t.step, t.Hmax, t.sc, t.umax, t.nph);
    return b;
}
static bool loadPlane(Plane& P, const std::string& fn)
{
    FILE* f = fopen(fn.c_str(), "rb"); if (!f) return false;
    bool ok = fread(&P.J, 4, 1, f) == 1 && fread(&P.nh, 4, 1, f) == 1 && fread(&P.nu, 4, 1, f) == 1;
    P.d0.resize(P.J); P.T.assign(P.J, std::vector<float>((size_t)P.nh * P.nu));
    for (int j = 0; j < P.J && ok; ++j) ok = fread(&P.d0[j], 8, 1, f) == 1 && fread(P.T[j].data(), 4, P.T[j].size(), f) == P.T[j].size();
    fclose(f); return ok;
}
static void savePlane(const Plane& P, const std::string& fn)
{
    FILE* f = fopen(fn.c_str(), "wb"); if (!f) return;
    fwrite(&P.J, 4, 1, f); fwrite(&P.nh, 4, 1, f); fwrite(&P.nu, 4, 1, f);
    for (int j = 0; j < P.J; ++j) { fwrite(&P.d0[j], 8, 1, f); fwrite(P.T[j].data(), 4, P.T[j].size(), f); }
    fclose(f);
}
static double uToSigma(double u, const TOpt& t) { return t.sc * u / (1 - std::fabs(u)); }

static Plane genPlane(bool ep, double bp, const TOpt& t)
{
    Plane P;
    const std::string fn = planeName(ep, bp, t);
    if (loadPlane(P, fn)) return P;
    const double v = ep ? kEP : kSP, Hb = kHbRight * bp / 100, tr = 1.0 / v;
    Geometry Gs(gPar, v, t.sub);
    const int nhh = (int)std::lround(t.Hmax / t.step);
    P.J = t.J; P.nh = 2 * nhh + 1; P.nu = t.nu;
    P.T.assign(t.J, std::vector<float>((size_t)P.nh * P.nu, 0.0f)); P.d0.assign(t.J, 0.0);
    for (int j = 0; j < t.J; ++j)
    {
        std::vector<std::unique_ptr<RawPath>> paths;
        for (int q = j * t.sub / t.J; q < (j + 1) * t.sub / t.J; ++q) paths.push_back(std::make_unique<RawPath>(Gs.y[q], v, t.nph));
        const double ws = 1.0 / paths.size();
        // group delay at the level where the group is steepest (the small-signal delay, or in a
        // dead zone the delay where the curve starts)
        const double As[] = {0.0, 0.1, 0.25, 0.5, 1.0, 1.5, 2.0, 3.0};
        double best = 0, d0 = 0;
        for (double A : As)
        {
            double m0 = 0, m1 = 0; const double dH = 0.01, e1 = 0.005 / tr;
#pragma omp parallel for reduction(+ : m0, m1)
            for (int q = 0; q < (int)paths.size(); ++q)
            {
                std::vector<double> U;
                m0 += ws * (paths[q]->element(Hb, [&](double) { return A + dH; }, U) - paths[q]->element(Hb, [&](double) { return A - dH; }, U)) / (2 * dH);
                m1 += ws * (paths[q]->element(Hb, [&](double tt) { return A + e1 * tt; }, U) - paths[q]->element(Hb, [&](double tt) { return A - e1 * tt; }, U)) / (2 * e1);
            }
            if (std::fabs(m0) > best * 1.05) { best = std::fabs(m0); d0 = m1 / m0; }
        }
        P.d0[j] = d0;
        const int tasks = (nhh + 1) * t.nu * (int)paths.size();
        std::vector<double> acc((size_t)(nhh + 1) * t.nu, 0.0);
#pragma omp parallel for schedule(dynamic)
        for (int k = 0; k < tasks; ++k)
        {
            const int q = k / ((nhh + 1) * t.nu), r = k % ((nhh + 1) * t.nu), i = r / t.nu, ju = r % t.nu;
            const double A = i * t.step;
            const double u = t.nu == 1 ? 0.0 : -t.umax + 2 * t.umax * ju / (t.nu - 1);
            const double s = uToSigma(u, t) / tr;     // field per second
            std::vector<double> U;
            const double m = paths[q]->element(Hb, [&](double tt) { return A + std::clamp(s * (tt - d0), -3.0, 3.0); }, U);
#pragma omp atomic
            acc[(size_t)i * t.nu + ju] += ws * m;
        }
        for (int i = 0; i <= nhh; ++i)
            for (int ju = 0; ju < t.nu; ++ju)
            {
                const double m = acc[(size_t)i * t.nu + ju];
                P.T[j][(size_t)ju * P.nh + nhh + i] = (float)m;
                P.T[j][(size_t)(t.nu - 1 - ju) * P.nh + nhh - i] = (float)-m;   // odd: M(-A, -s) = -M(A, s)
            }
    }
    std::system("mkdir -p planes");
    savePlane(P, fn);
    return P;
}

// ---------------------------------------------------------------- core configuration
struct GP { const Geometry* g; int sub, J; double play(int j, double f) const { double s = 0; for (int q = j * sub / J; q < (j + 1) * sub / J; ++q) s += g->play(q, f); return s; } };

static void setPlane(rt2::Cfg& c, const Plane& P, const TOpt& t, bool ep)
{
    const double v = ep ? kEP : kSP, fs = 44100.0 * t.os;
    c.os = t.os; c.J = P.J; c.nh = P.nh; c.nu = P.nu;
    c.h0 = (float)-t.Hmax; c.invh = (float)(1.0 / t.step); c.sc = (float)t.sc; c.umax = (float)t.umax;
    c.tauS = (float)(fs / v); c.cubicA = t.cubic; c.useIir = t.iir; c.adaa = t.adaa;
    int lat = 8;
    for (int j = 0; j < P.J; ++j)
    {
        rt2::Group& G = c.g[j];
        G.T = P.T[j];
        // integral along A from A = 0, Simpson-like (trapezoid on the fine table)
        G.Gi.assign(G.T.size(), 0.0f);
        const int mid = P.nh / 2;
        for (int iu = 0; iu < P.nu; ++iu)
        {
            const float* T = &G.T[(size_t)iu * P.nh]; float* Gi = &G.Gi[(size_t)iu * P.nh];
            double acc = 0; Gi[mid] = 0;
            for (int i = mid + 1; i < P.nh; ++i) { acc += 0.5 * t.step * (T[i] + T[i - 1]); Gi[i] = (float)acc; }
            acc = 0;
            for (int i = mid - 1; i >= 0; --i) { acc -= 0.5 * t.step * (T[i] + T[i + 1]); Gi[i] = (float)acc; }
        }
        const double p = P.d0[j] * fs, fl = std::floor(p), tt = p - fl;
        G.io = (int)fl;
        const double cv[4] = {-tt * (tt - 1) * (tt - 2) / 6, (tt + 1) * (tt - 1) * (tt - 2) / 2, -(tt + 1) * tt * (tt - 2) / 2, (tt + 1) * tt * (tt - 1) / 6};
        const double cs[4] = {-(3 * tt * tt - 6 * tt + 2) / 6, (3 * tt * tt - 4 * tt - 1) / 2, -(3 * tt * tt - 2 * tt - 2) / 2, (3 * tt * tt - 1) / 6};
        for (int k = 0; k < 4; ++k) { G.cv[k] = (float)cv[k]; G.cs[k] = (float)cs[k]; }
        lat = std::max(lat, G.io + 4);
    }
    c.lat = lat;
}

static std::complex<double> project(const std::vector<float>& v, size_t off, size_t n, double f, double sr = 44100.0)
{
    std::complex<double> s = 0; double ws = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * (double)i / (double)n);
        s += (double)v[off + i] * w * std::polar(1.0, -2 * kPi * f * (double)(off + i) / sr);
        ws += w;
    }
    return s * (2.0 / ws);
}
template <class Sig> static std::vector<float> runCore(const rt2::Cfg& c, size_t n, Sig sig)
{
    static rt2::Core core; core.init(c);
    std::vector<float> L(n), R(n, 0.0f);
    for (size_t i = 0; i < n; ++i) L[i] = (float)sig((long)i);
    for (size_t i = 0; i < n; i += 128) core.process(L.data() + i, R.data() + i, (int)std::min<size_t>(128, n - i));
    return L;
}

// ---- playback filter design: FIR by frequency sampling of play_g / R_tot (as round 1), and an IIR
// alternative fitted to the same targets (Steiglitz-McBride, biquads)
struct Design { std::vector<std::vector<cd>> T; std::vector<double> f; };
static Design measureTargets(rt2::Cfg& c, const TOpt& t, bool ep, int N, double& Lfit)
{
    const double v = ep ? kEP : kSP;
    Geometry Gs(gPar, v, t.sub);
    GP G{&Gs, t.sub, t.J};
    const int nb = N / 2;
    const double h = 0.01;
    std::vector<std::vector<cd>> R(t.J, std::vector<cd>(nb + 1));
    rt2::Cfg m = c; m.useIir = false;
    for (int j = 0; j < t.J; ++j)
    {
        for (int jj = 0; jj < t.J; ++jj) std::fill(std::begin(m.g[jj].fir), std::end(m.g[jj].fir), 0.0f);
        m.g[j].fir[0] = 1.0f;
        for (int b = 0; b <= nb; ++b)
        {
            const double f = b == 0 ? 30.0 : (b == nb ? 21000.0 : 44100.0 * b / N);
            const size_t n = f < 200 ? 32768 : 8192, settle = 2048;
            auto y = runCore(m, n + settle, [&](long i) { return h * std::sin(2 * kPi * f * i / 44100.0); });
            R[j][b] = project(y, settle, n, f) / (cd(0, -1) * h);
        }
    }
    // bulk delay from the low-frequency phase
    double prev = 0, acc = 0;
    for (int b = 1; b <= 3; ++b)
    {
        const double f = 44100.0 * b / N;
        cd tot = 0; for (int jj = 0; jj < t.J; ++jj) tot += G.play(jj, f) * R[jj][b];
        double ph = std::arg(tot);
        while (ph - prev > kPi) ph -= 2 * kPi;
        while (ph - prev < -kPi) ph += 2 * kPi;
        prev = ph; acc += -ph / (2 * kPi * f / 44100.0);
    }
    Lfit = acc / 3;
    Design D; D.T.assign(t.J, std::vector<cd>(nb + 1)); D.f.resize(nb + 1);
    for (int b = 0; b <= nb; ++b)
    {
        const double fm = b == 0 ? 30.0 : (b == nb ? 21000.0 : 44100.0 * b / N);
        D.f[b] = fm;
        cd tot = 0; for (int jj = 0; jj < t.J; ++jj) tot += G.play(jj, fm) * R[jj][b];
        for (int j = 0; j < t.J; ++j) D.T[j][b] = G.play(j, fm) / (tot * std::polar(1.0, 2 * kPi * fm / 44100.0 * Lfit));   // latency removed
    }
    return D;
}
static void designFir(rt2::Cfg& c, const TOpt& t, bool ep)
{
    const int N = rt2::kFir, nb = N / 2;
    double L;
    Design D = measureTargets(c, t, ep, N, L);
    const double tau0 = 0.5 * (N - 1);
    for (int j = 0; j < t.J; ++j)
    {
        std::vector<cd> H(N);
        for (int b = 0; b <= nb; ++b)
        {
            const double f = b == 0 ? 0.0 : 44100.0 * b / N, w = 2 * kPi * f / 44100.0;
            const cd val = b == nb ? cd(0) : D.T[j][b] * std::polar(1.0, -w * tau0);
            H[b] = val; if (b > 0 && b < nb) H[N - b] = std::conj(val);
        }
        for (int k = 0; k < N; ++k) { cd s = 0; for (int b = 0; b < N; ++b) s += H[b] * std::polar(1.0, 2 * kPi * b * k / N); c.g[j].fir[k] = (float)(s.real() / N); }
    }
}
// Least-squares IIR (order 2*nbq) with a bulk delay D, fitted to T_j(f) e^{-i w D} on a dense grid
// by Steiglitz-McBride; returned as biquads.
static bool solve(std::vector<std::vector<double>> A, std::vector<double> b, std::vector<double>& x)
{
    const int n = (int)b.size();
    for (int i = 0; i < n; ++i)
    {
        int p = i; for (int r = i + 1; r < n; ++r) if (std::fabs(A[r][i]) > std::fabs(A[p][i])) p = r;
        std::swap(A[i], A[p]); std::swap(b[i], b[p]);
        if (std::fabs(A[i][i]) < 1e-14) return false;
        for (int r = i + 1; r < n; ++r) { const double f = A[r][i] / A[i][i]; for (int k = i; k < n; ++k) A[r][k] -= f * A[i][k]; b[r] -= f * b[i]; }
    }
    x.assign(n, 0);
    for (int i = n - 1; i >= 0; --i) { double s = b[i]; for (int k = i + 1; k < n; ++k) s -= A[i][k] * x[k]; x[i] = s / A[i][i]; }
    return true;
}
static std::vector<cd> polyRoots(std::vector<double> a)   // a[0] = 1, monic in z^-1: roots of z^n + a1 z^{n-1} + ...
{
    const int n = (int)a.size() - 1;
    std::vector<cd> r(n);
    for (int i = 0; i < n; ++i) r[i] = std::polar(0.9, 2 * kPi * (i + 0.25) / n);
    for (int it = 0; it < 500; ++it)
        for (int i = 0; i < n; ++i)
        {
            cd p = 1, q = 1;
            for (int k = 1; k <= n; ++k) p = p * r[i] + a[k];
            for (int k = 0; k < n; ++k) if (k != i) q *= (r[i] - r[k]);
            r[i] -= p / q;
        }
    return r;
}
static double fitIir(const std::vector<double>& fr, const std::vector<cd>& Tt, int order, int D, std::vector<double>& B, std::vector<double>& A)
{
    // weights: equal in log frequency between 20 Hz and 20 kHz
    std::vector<cd> tgt(fr.size()); std::vector<double> w(fr.size());
    for (size_t k = 0; k < fr.size(); ++k) { tgt[k] = Tt[k] * std::polar(1.0, -2 * kPi * fr[k] / 44100.0 * D); w[k] = 1.0 / std::max(std::norm(tgt[k]), 1e-12); }
    A.assign(order + 1, 0.0); A[0] = 1; B.assign(order + 1, 0.0);
    for (int it = 0; it < 30; ++it)
    {
        // minimize sum w |B(z) - T A(z)|^2 / |A_prev(z)|^2 (linear in b0..bn, a1..an)
        const int nu = 2 * order + 1;
        std::vector<std::vector<double>> M(nu, std::vector<double>(nu, 0.0)); std::vector<double> rhs(nu, 0.0);
        for (size_t k = 0; k < fr.size(); ++k)
        {
            const double om = 2 * kPi * fr[k] / 44100.0;
            cd Ap = 0; for (int i = 0; i <= order; ++i) Ap += A[i] * std::polar(1.0, -om * i);
            const double wk = w[k] / std::max(std::norm(Ap), 1e-12);
            std::vector<cd> row(nu);
            for (int i = 0; i <= order; ++i) row[i] = std::polar(1.0, -om * i);
            for (int i = 1; i <= order; ++i) row[order + i] = -tgt[k] * std::polar(1.0, -om * i);
            const cd y = tgt[k];
            for (int a = 0; a < nu; ++a) { for (int b = 0; b < nu; ++b) M[a][b] += wk * std::real(std::conj(row[a]) * row[b]); rhs[a] += wk * std::real(std::conj(row[a]) * y); }
        }
        std::vector<double> x;
        if (!solve(M, rhs, x)) break;
        for (int i = 0; i <= order; ++i) B[i] = x[i];
        for (int i = 1; i <= order; ++i) A[i] = x[order + i];
        // reflect unstable poles
        auto r = polyRoots(A);
        bool changed = false;
        for (auto& z : r) if (std::abs(z) >= 0.999) { cd zr = 1.0 / std::conj(z); if (std::abs(zr) > 0.995) zr *= 0.995 / std::abs(zr); z = zr; changed = true; }
        if (changed)
        {
            std::vector<cd> p{1};
            for (auto& z : r) { std::vector<cd> q(p.size() + 1, 0); for (size_t i = 0; i < p.size(); ++i) { q[i] += p[i]; q[i + 1] -= p[i] * z; } p = q; }
            for (int i = 1; i <= order; ++i) A[i] = p[i].real();
        }
    }
    double err = 0;
    for (size_t k = 0; k < fr.size(); ++k)
    {
        const double om = 2 * kPi * fr[k] / 44100.0;
        cd Bz = 0, Az = 0; for (int i = 0; i <= order; ++i) { Bz += B[i] * std::polar(1.0, -om * i); Az += A[i] * std::polar(1.0, -om * i); }
        if (fr[k] <= 16000) err = std::max(err, std::abs(Bz / Az / tgt[k] - 1.0));
    }
    return err;
}
static void designIir(rt2::Cfg& c, const TOpt& t, bool ep, int order)
{
    // dense targets
    double L;
    Design D = measureTargets(c, t, ep, 128, L);
    for (int j = 0; j < t.J; ++j)
    {
        std::vector<double> fr; std::vector<cd> tt;
        for (size_t b = 0; b < D.f.size(); ++b) if (D.f[b] >= 20 && D.f[b] <= 20000) { fr.push_back(D.f[b]); tt.push_back(D.T[j][b]); }
        double bestErr = 1e9; std::vector<double> bB, bA; int bD = 0;
        for (int Dl = 0; Dl <= 4; ++Dl)
        {
            std::vector<double> B, A;
            const double e = fitIir(fr, tt, order, Dl, B, A);
            if (e < bestErr) { bestErr = e; bB = B; bA = A; bD = Dl; }
        }
        // split into biquads (pairs of poles and zeros)
        rt2::Group& G = c.g[j];
        G.bqDelay = bD;
        auto zr = polyRoots(std::vector<double>(bB.begin(), bB.end())); // zeros of B/b0
        std::vector<double> Bn(bB.size()); for (size_t i = 0; i < bB.size(); ++i) Bn[i] = bB[i] / bB[0];
        zr = polyRoots(Bn);
        auto pr = polyRoots(bA);
        auto pairUp = [](std::vector<cd> r) {
            std::vector<std::pair<double, double>> q;   // (c1, c2) of 1 + c1 z^-1 + c2 z^-2
            std::vector<bool> used(r.size(), false);
            for (size_t i = 0; i < r.size(); ++i)
            {
                if (used[i]) continue; used[i] = true;
                size_t bj = i; double bd = 1e9;
                for (size_t k = 0; k < r.size(); ++k) if (!used[k]) { const double d = std::abs(r[k] - std::conj(r[i])); if (d < bd) { bd = d; bj = k; } }
                if (bj != i) used[bj] = true;
                const cd a = r[i], b = bj != i ? r[bj] : cd(0);
                q.push_back({-(a + b).real(), (a * b).real()});
            }
            return q;
        };
        auto zq = pairUp(zr), pq = pairUp(pr);
        G.nbq = (int)std::max(zq.size(), pq.size());
        for (int q = 0; q < G.nbq; ++q)
        {
            const auto z = q < (int)zq.size() ? zq[q] : std::pair<double, double>{0, 0};
            const auto p = q < (int)pq.size() ? pq[q] : std::pair<double, double>{0, 0};
            G.bq[q] = { 1.0f, (float)z.first, (float)z.second, (float)p.first, (float)p.second };
        }
        G.bqGain = (float)bB[0];
        std::printf("# group %d IIR order %d delay %d: worst |H/T - 1| to 16 kHz %.4f (%.2f dB)\n", j, order, bD, bestErr, dB(1 + bestErr));
    }
    c.useIir = true;
}

// ---------------------------------------------------------------- references (shared cache with rt_build)
static std::map<std::string, std::array<double, 3>> gCache;
static std::string keyStr(int form, int J, int ep, double f, double lv, double bp) { char b[128]; std::snprintf(b, 128, "%d %d %d %.0f %.0f %.0f", form, J, ep, f, lv, bp); return b; }
static void loadCache()
{
    FILE* f = fopen("ref_cache.txt", "r"); if (!f) return;
    int fo, J, ep; double fr, lv, bp, a, b, cc;
    while (fscanf(f, "%d %d %d %lf %lf %lf %lf %lf %lf", &fo, &J, &ep, &fr, &lv, &bp, &a, &b, &cc) == 9) gCache[keyStr(fo, J, ep, fr, lv, bp)] = {a, b, cc};
    fclose(f);
}
static void saveCache() { FILE* f = fopen("ref_cache.txt", "w"); for (auto& [k, v] : gCache) std::fprintf(f, "%s %.6f %.6f %.6f\n", k.c_str(), v[0], v[1], v[2]); fclose(f); }
static std::array<double, 3> truth(bool ep, double f, double lv, double bp)
{
    const std::string k = keyStr(0, 12, (int)ep, f, lv, bp);
    auto it = gCache.find(k); if (it != gCache.end()) return it->second;
    Geometry G(gPar, ep ? kEP : kSP, 12);
    PathSpec ps;
    static std::map<std::pair<int, long>, cd> Dc;
    auto D = [&](double fr) {
        auto kk = std::make_pair((int)ep, std::lround(fr * 10));
        if (!Dc.count(kk)) { ToneSpec T{fr, 0.01, kHbRight, 32}; auto H = recordTone(G, gEv, RAW, T, ps, 1); Dc[kk] = playback(G, H, fr, 1) / 0.01; }
        return Dc[kk];
    };
    const double h = kK * kRef * std::pow(10.0, lv / 20) * std::abs(preEmph(f, ep));
    ToneSpec T{f, h, kHbRight * bp / 100, 64};
    auto H = recordTone(G, gEv, RAW, T, ps, 5);
    std::array<double, 3> r{};
    for (int i = 0; i < 3; ++i) { const int n = 2 * i + 1; r[i] = n * f <= 22050 ? std::abs(playback(G, H, f, n) / D(n * f)) : 0.0; }
    gCache[k] = r; saveCache();
    return r;
}

// ---------------------------------------------------------------- tests
struct Worst { double v[3] = {}; char w[3][64] = {}; double rms[3] = {}; int n[3] = {}; };
static void build(rt2::Cfg& c, const TOpt& t, bool ep, double bp, const rt2::Cfg* firFrom)
{
    Plane P = genPlane(ep, bp, t);
    setPlane(c, P, t, ep);
    if (firFrom) { for (int j = 0; j < t.J; ++j) { std::memcpy(c.g[j].fir, firFrom->g[j].fir, sizeof(c.g[j].fir)); c.g[j].nbq = firFrom->g[j].nbq; std::memcpy(c.g[j].bq, firFrom->g[j].bq, sizeof(c.g[j].bq)); c.g[j].bqGain = firFrom->g[j].bqGain; c.g[j].bqDelay = firFrom->g[j].bqDelay; } c.useIir = firFrom->useIir; }
}
static void baseCfg(rt2::Cfg& c, const TOpt& t, bool ep)
{
    build(c, t, ep, 100, nullptr);
    designFir(c, t, ep);
    if (t.iir) designIir(c, t, ep, 4);
}
static void accuracy(const TOpt& t, const std::vector<double>& biases, const std::vector<double>& freqs, const std::vector<double>& levels, const char* title)
{
    loadCache();
    std::printf("# %s: engine %s nu=%d step=%.3f nph=%d sc=%.2f os=%d J=%d %s %s\n", title, t.nu > 1 ? "SLOPE" : "FAST", t.nu, t.step, t.nph, t.sc, t.os, t.J, t.cubic ? "cubic-A" : "linear-A", t.iir ? "IIR playback" : "FIR playback");
    std::printf("# speed f level bias | truth: out dB re in, H3 dBc, H5 dBc | engine - truth: d out, d H3, d H5\n");
    Worst W[2][2];   // [speed][bias group: 0 >= 50 %, 1 < 50 %]
    for (int ep = 0; ep <= 1; ++ep)
    {
        rt2::Cfg base; baseCfg(base, t, ep);
        for (double bp : biases)
        {
            rt2::Cfg c; build(c, t, ep, bp, &base);
            for (double f : freqs)
                for (double lv : levels)
                {
                    const double h = kK * kRef * std::pow(10.0, lv / 20) * std::abs(preEmph(f, ep));
                    const size_t n = f < 200 ? 32768 : 16384, settle = 4096;
                    auto y = runCore(c, n + settle, [&](long i) { return h * std::sin(2 * kPi * f * i / 44100.0); });
                    double m[3];
                    for (int i = 0; i < 3; ++i) { const int hn = 2 * i + 1; m[i] = hn * f <= 22050 ? std::abs(project(y, settle, n, hn * f)) / h : 0.0; }
                    auto r = truth(ep, f, lv, bp);
                    const double rOut = dB(r[0] / h), rH3 = dB(r[1] / r[0]), rH5 = dB(r[2] / r[0]);
                    const double d[3] = {dB(m[0]) - rOut, dB(m[1] / m[0]) - rH3, dB(m[2] / m[0]) - rH5};
                    const double outDbfs = rOut + dB(h / kK) - dB(std::abs(preEmph(f, ep)));
                    std::printf("%s %5.0f %+3.0f %3.0f | %7.2f %7.2f %7.2f | %6.2f %6.2f %6.2f\n", ep ? "EP" : "SP", f, lv, bp, rOut, rH3, rH5, d[0], d[1], d[2]);
                    if (outDbfs < -80) continue;
                    const bool ok[3] = {true, rH3 > -60 && 3 * f <= 20000, rH5 > -60 && 5 * f <= 20000};
                    Worst& w = W[ep][bp >= 50 ? 0 : 1];
                    for (int q = 0; q < 3; ++q)
                        if (ok[q])
                        {
                            w.rms[q] += d[q] * d[q]; w.n[q]++;
                            if (std::fabs(d[q]) > w.v[q]) { w.v[q] = std::fabs(d[q]); std::snprintf(w.w[q], 64, "%.0f Hz %+.0f dB bias %.0f%%", f, lv, bp); }
                        }
                    std::fflush(stdout);
                }
        }
    }
    for (int ep = 0; ep <= 1; ++ep)
        for (int g = 0; g < 2; ++g)
        {
            const Worst& w = W[ep][g];
            if (!w.n[0]) continue;
            std::printf("# %s bias %s: out worst %.2f (%s) rms %.2f | H3 worst %.2f (%s) rms %.2f | H5 worst %.2f (%s) rms %.2f\n", ep ? "EP" : "SP", g ? "<  50%" : ">= 50%",
                        w.v[0], w.w[0], std::sqrt(w.rms[0] / std::max(1, w.n[0])), w.v[1], w.w[1], std::sqrt(w.rms[1] / std::max(1, w.n[1])), w.v[2], w.w[2], std::sqrt(w.rms[2] / std::max(1, w.n[2])));
        }
}

static void fftc(std::vector<cd>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * kPi / (double)len; const cd wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) { cd w(1); for (size_t k = 0; k < len / 2; ++k) { auto u = a[i + k], tt = a[i + k + len / 2] * w; a[i + k] = u + tt; a[i + k + len / 2] = u - tt; w *= wl; } }
    }
}
static double aliasDb(const std::vector<float>& v, double grid, double* at = nullptr)
{
    const int N = 65536; std::vector<cd> a(N); double ws = 0; const size_t off = v.size() - N;
    for (int i = 0; i < N; ++i) { const double tt = 2 * kPi * i / N, w = 0.35875 - 0.48829 * std::cos(tt) + 0.14128 * std::cos(2 * tt) - 0.01168 * std::cos(3 * tt); a[(size_t)i] = v[off + (size_t)i] * w; ws += w; }
    fftc(a);
    const double bin = 44100.0 / N; double worst = 0, wf = 0;
    for (int i = (int)(20.0 / bin); i < N / 2 - 1; ++i)
    {
        const double f = i * bin;
        if (std::fabs(f - std::round(f / grid) * grid) <= 10 * bin) continue;
        const double m = std::abs(a[(size_t)i]) * 2.0 / ws;
        if (m >= std::abs(a[(size_t)i - 1]) * 2.0 / ws && m >= std::abs(a[(size_t)i + 1]) * 2.0 / ws && m > worst) { worst = m; wf = f; }
    }
    if (at) *at = wf;
    return dB(worst);
}
struct PreEmph
{
    float b0, b1, a1, invB0, uP = 0, wP = 0, vP = 0, yP = 0;
    explicit PreEmph(bool ep)
    {
        const float sr = 44100.0f, fz = 1326.3f * (ep ? 1.0f / 3.0f : 1.0f);
        const float kz = std::tan((float)kPi * fz / sr), kp = std::tan((float)kPi * std::min(10.0f * fz, 0.45f * sr) / sr);
        const float norm = 1.0f / (1.0f + 1.0f / kp);
        b0 = (1.0f + 1.0f / kz) * norm; b1 = (1.0f - 1.0f / kz) * norm; a1 = (1.0f - 1.0f / kp) * norm; invB0 = 1.0f / b0;
    }
    float pre(float u) { const float w = b0 * u + b1 * uP - a1 * wP; uP = u; wP = w; return w; }
    float de(float v) { const float y = (v + a1 * vP - b1 * yP) * invB0; vP = v; yP = y; return y; }
};
static void aliasing(const TOpt& t, bool ep)
{
    std::printf("# aliasing %s nu=%d step=%.3f nph=%d os=%d %s %s, %s: worst off-grid component, dBFS at the output\n", t.nu > 1 ? "SLOPE" : "FAST", t.nu, t.step, t.nph, t.os, t.cubic ? "cubic-A" : "linear-A", t.iir ? "IIR" : "FIR", ep ? "EP" : "SP");
    struct S { const char* name; double f1, f2, amp; };
    const S ss[] = { {"1 kHz -1 dBFS", 1000, 0, 0.891}, {"5 kHz -1 dBFS", 5000, 0, 0.891}, {"10 kHz -1 dBFS", 10000, 0, 0.891}, {"14 kHz -1 dBFS", 14000, 0, 0.891}, {"1 + 15 kHz -7 dBFS each", 1000, 15000, 0.447} };
    rt2::Cfg base; baseCfg(base, t, ep);
    double worst = -999;
    for (double recDb : {0.0, 8.0})
    {
        const double gRec = std::pow(10.0, recDb / 20);
        std::printf("Rec Level %+.0f dB      bias 0%%  25%%  50%%  100%%  150%%\n", recDb);
        std::vector<rt2::Cfg> cs;
        for (double bp : {0.0, 25.0, 50.0, 100.0, 150.0}) { rt2::Cfg c; build(c, t, ep, bp, &base); cs.push_back(c); }
        for (const S& s : ss)
        {
            std::printf("  %-24s", s.name);
            for (const rt2::Cfg& c : cs)
            {
                const size_t n = 65536 + 16384;
                PreEmph pe(ep);
                std::vector<float> x(n), r(n, 0.0f);
                for (size_t i = 0; i < n; ++i)
                {
                    const double tt = (double)i / 44100.0;
                    x[i] = (float)(kK * pe.pre((float)(gRec * s.amp * (std::sin(2 * kPi * s.f1 * tt) + (s.f2 > 0 ? std::sin(2 * kPi * s.f2 * tt) : 0.0)))));
                }
                static rt2::Core core; core.init(c);
                for (size_t i = 0; i < n; i += 128) core.process(x.data() + i, r.data() + i, 128);
                for (size_t i = 0; i < n; ++i) x[i] = (float)(pe.de((float)(x[i] / kK)) / gRec);
                double at = 0; const double a = aliasDb(x, s.f1, &at);
                worst = std::max(worst, a);
                std::printf(" %6.1f(%5.0f)", a, at);
            }
            std::printf("\n"); std::fflush(stdout);
        }
    }
    std::printf("# worst %.1f dBFS\n", worst);
}

// export for the arm32 cost harness: config + two planes (for the knob-move blend)
static void saveCfg(const TOpt& t, bool ep, const char* fn)
{
    rt2::Cfg c; baseCfg(c, t, ep);
    Plane P2 = genPlane(ep, 50, t);
    FILE* f = fopen(fn, "wb");
    fwrite(&c.os, 4, 1, f); fwrite(&c.J, 4, 1, f); fwrite(&c.nh, 4, 1, f); fwrite(&c.nu, 4, 1, f); fwrite(&c.lat, 4, 1, f);
    fwrite(&c.h0, 4, 1, f); fwrite(&c.invh, 4, 1, f); fwrite(&c.sc, 4, 1, f); fwrite(&c.tauS, 4, 1, f); fwrite(&c.umax, 4, 1, f);
    int fl[3] = {(int)c.useIir, (int)c.cubicA, (int)c.adaa}; fwrite(fl, 4, 3, f);
    for (int j = 0; j < c.J; ++j)
    {
        rt2::Group& G = c.g[j];
        fwrite(&G.io, 4, 1, f); fwrite(G.cv, 4, 4, f); fwrite(G.cs, 4, 4, f); fwrite(G.fir, 4, rt2::kFir, f);
        fwrite(&G.nbq, 4, 1, f); fwrite(G.bq, sizeof(rt2::BqC), rt2::kMaxBq, f); fwrite(&G.bqGain, 4, 1, f); fwrite(&G.bqDelay, 4, 1, f);
        fwrite(G.T.data(), 4, G.T.size(), f); fwrite(P2.T[j].data(), 4, P2.T[j].size(), f); fwrite(G.Gi.data(), 4, G.Gi.size(), f);
    }
    fclose(f);
    size_t bytes = 0; for (int j = 0; j < c.J; ++j) bytes += c.g[j].T.size() * 4;
    std::printf("saved %s: nh %d nu %d J %d, one plane %zu bytes, lat %d\n", fn, c.nh, c.nu, c.J, bytes, c.lat);
}

// The generator: every plane on the bias grid for both speeds, the playback FIRs (designed at correct
// bias, per speed) and the constants, written as one C++ header of constant arrays.
static void writeHeader(const TOpt& t, const char* fn)
{
    const double grid[] = {0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 60, 70, 80, 90, 100, 110, 120, 135, 150};
    const int nb = sizeof(grid) / sizeof(grid[0]);
    FILE* f = fopen(fn, "w");
    std::fprintf(f, "// Generated by r2 header (hyst prototype): hysteresis recorder tables. Do not edit.\n#pragma once\n");
    std::fprintf(f, "namespace hysttab {\n");
    std::fprintf(f, "constexpr int kJ = %d, kNh = %d, kNu = %d, kNb = %d, kOs = %d, kFir = %d;\n", t.J, 2 * (int)std::lround(t.Hmax / t.step) + 1, t.nu, nb, t.os, rt2::kFir);
    std::fprintf(f, "constexpr float kH0 = %.9ef, kInvStep = %.9ef, kSc = %.9ef, kUmax = %.9ef;\n", -t.Hmax, 1.0 / t.step, t.sc, t.umax);
    std::fprintf(f, "constexpr float kHbRight = %.9ef, kFieldPerUnit = %.9ef;   // correct bias (Hc units, deep-gap field); k: field per unit of pre-emphasized signal\n", kHbRight, kK);
    std::fprintf(f, "constexpr float kTauS[2] = { %.9ef, %.9ef };   // fs_os / v (samples per micrometer), SP, EP\n", 44100.0 * t.os / kSP, 44100.0 * t.os / kEP);
    std::fprintf(f, "constexpr float kBiasPct[kNb] = {");
    for (int b = 0; b < nb; ++b) std::fprintf(f, "%s%.1ff", b ? ", " : " ", grid[b]);
    std::fprintf(f, " };\n");
    for (int ep = 0; ep <= 1; ++ep)
    {
        rt2::Cfg base; baseCfg(base, t, ep);
        std::fprintf(f, "// %s playback FIRs (taps, newest first), per group\nconstexpr float kFirTaps%s[kJ][kFir] = {\n", ep ? "EP" : "SP", ep ? "Ep" : "Sp");
        for (int j = 0; j < t.J; ++j) { std::fprintf(f, " {"); for (int k = 0; k < rt2::kFir; ++k) std::fprintf(f, "%s%.9ef", k ? "," : "", base.g[j].fir[k]); std::fprintf(f, "},\n"); }
        std::fprintf(f, "};\n");
        std::fprintf(f, "// %s group delays (seconds after the gap center) per bias plane\nconstexpr float kD0%s[kNb][kJ] = {\n", ep ? "EP" : "SP", ep ? "Ep" : "Sp");
        std::vector<Plane> Ps;
        for (int b = 0; b < nb; ++b) { Ps.push_back(genPlane(ep, grid[b], t)); std::fprintf(f, " {"); for (int j = 0; j < t.J; ++j) std::fprintf(f, "%s%.9e", j ? "," : "", Ps.back().d0[j]); std::fprintf(f, "},\n"); }
        std::fprintf(f, "};\n// %s remanence planes: [bias][group][u][A]\nconstexpr float kT%s[kNb][kJ][kNu * kNh] = {\n", ep ? "EP" : "SP", ep ? "Ep" : "Sp");
        for (int b = 0; b < nb; ++b)
        {
            std::fprintf(f, " {\n");
            for (int j = 0; j < t.J; ++j) { std::fprintf(f, "  {"); for (size_t k = 0; k < Ps[b].T[j].size(); ++k) std::fprintf(f, "%s%.7ef", k ? "," : "", Ps[b].T[j][k]); std::fprintf(f, "},\n"); }
            std::fprintf(f, " },\n");
        }
        std::fprintf(f, "};\n");
        std::fflush(f);
    }
    std::fprintf(f, "} // namespace hysttab\n");
    fclose(f);
    std::printf("wrote %s\n", fn);
}


// Step 5: the engine at correct bias against the plugin's present TapeSat curve (Bias right, Rec Level 0 dB)
static void tapesat(const TOpt& t)
{
    std::printf("# engine %s at correct bias against the present TapeSat curve: gain dB (out - in), H3 %%, H5 %% at the output (after de-emphasis)\n", t.nu > 1 ? "SLOPE" : "FAST");
    for (int ep = 0; ep <= 1; ++ep)
    {
        rt2::Cfg c; baseCfg(c, t, ep);
        for (double f : {441.0, 1000.0, 3000.0})
        {
            std::printf("%s %.0f Hz: level | engine gain H3 H5 | TapeSat gain H3 H5 | truth gain H3 H5\n", ep ? "EP" : "SP", f);
            for (double lv = -20; lv <= 8.01; lv += 4)
            {
                const double a = kRef * std::pow(10.0, lv / 20), h = kK * a * std::abs(preEmph(f, ep));
                const size_t n = 32768, settle = 4096;
                auto y = runCore(c, n + settle, [&](long i) { return h * std::sin(2 * kPi * f * i / 44100.0); });
                double m[3];
                for (int i = 0; i < 3; ++i) { const int hn = 2 * i + 1; m[i] = hn * f <= 22050 ? std::abs(project(y, settle, n, hn * f)) / std::abs(preEmph(hn * f, ep)) / kK : 0.0; }
                auto q = pluginTone(ep, f, a, 5);
                double r3[3] = {0, 0, 0};
                if (f != 3000.0) { auto r = truth(ep, f, lv, 100); for (int i = 0; i < 3; ++i) { const int hn = 2 * i + 1; r3[i] = r[i] * std::abs(preEmph(f, ep)) / std::abs(preEmph(hn * f, ep)) * a; } }
                std::printf("  %+4.0f | %+6.2f %6.2f %6.3f | %+6.2f %6.2f %6.3f | %+6.2f %6.2f %6.3f\n", lv,
                            dB(m[0] / a), 100 * m[1] / m[0], 100 * m[2] / m[0], dB(q[1] / a), 100 * q[3] / q[1], 100 * q[5] / q[1],
                            r3[0] > 0 ? dB(r3[0] / a) : 0.0, r3[0] > 0 ? 100 * r3[1] / r3[0] : 0.0, r3[0] > 0 ? 100 * r3[2] / r3[0] : 0.0);
                std::fflush(stdout);
            }
        }
    }
}

int main(int argc, char** argv)
{
    TOpt t; std::string mode, out; bool ep = false; double bias = 100;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto val = [&](const char* k) { return std::stod(a.substr(std::strlen(k))); };
        if (a.rfind("nu=", 0) == 0) t.nu = (int)val("nu=");
        else if (a.rfind("step=", 0) == 0) t.step = val("step=");
        else if (a.rfind("nph=", 0) == 0) t.nph = (int)val("nph=");
        else if (a.rfind("sc=", 0) == 0) t.sc = val("sc=");
        else if (a.rfind("os=", 0) == 0) t.os = (int)val("os=");
        else if (a.rfind("J=", 0) == 0) t.J = (int)val("J=");
        else if (a.rfind("bias=", 0) == 0) bias = val("bias=");
        else if (a == "lin") t.cubic = false;
        else if (a == "iir") t.iir = true;
        else if (a == "adaa") t.adaa = true;
        else if (a == "speed=ep") ep = true;
        else if (a.rfind("save=", 0) == 0) { mode = "save"; out = a.substr(5); }
        else if (a.rfind("out=", 0) == 0) out = a.substr(4);
        else mode = a;
    }
    gPar = fitSfd(Par(), 0.40);
    gD = std::make_unique<Density>(gPar);
    gTab = std::make_unique<ETable>(); gTab->build(*gD, 0.01);
    gEv = Eval{nullptr, gTab.get()};
    if (mode == "acc") accuracy(t, {25, 50, 75, 100, 150}, {100, 1000, 3000, 5000, 10000}, {-20, -10, 0, 6}, "main grid");
    else if (mode == "under") accuracy(t, {0, 10, 25, 35}, {100, 1000, 5000}, {-20, -10, 0, 6}, "under-bias grid");
    else if (mode == "alias") aliasing(t, ep);
    else if (mode == "save") saveCfg(t, ep, out.c_str());
    else if (mode == "tapesat") { loadCache(); tapesat(t); }
    else if (mode == "header") writeHeader(t, out.empty() ? "hyst_tables.h" : out.c_str());
    else if (mode == "gen") { genPlane(ep, bias, t); std::printf("plane %s\n", planeName(ep, bias, t).c_str()); }
    return 0;
}
