// Offline Preisach model of AC-biased recording on the linear audio track (prototype, double).
// Units: fields in units of the loop coercivity Hc (h = H/Hc), lengths in micrometers, time in s.
//
// Model: density P(hc, hu) = LogNormal(hc; hc0, sc) x Normal(hu; 0, si); hysteron up threshold
// alpha = hu + hc, down threshold beta = hu - hc. Each tape element arrives AC-erased (state +1 iff
// hu < 0), crosses the head field once, and keeps the state set by its LAST threshold crossing.
// Head field: Karlqvist g(x, y); field on the element H = g (Hs(t) + Hb sin(2 pi fb t + phi)).
//
// The final state of every hysteron is set by the backward records of the field along the path:
// A_k = max_{j>=k} U_j, B_k = min_{j>=k} L_j (raw carrier: U = L = H). Hysteron (alpha, beta) ends
// +1 iff its last up-crossing is later than its last down-crossing. With the cumulative mass
// E(a, b) = mass{alpha <= a, beta < b} this is a sum over the record staircase (see recordPath).
#pragma once
#include <cmath>
#include <vector>
#include <complex>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <functional>

namespace hy {

constexpr double kPi = 3.14159265358979323846;
using cd = std::complex<double>;

struct Par
{
    double hc0 = 1.05, sc = 0.15, si = 0.20;  // density (hc0 rescaled so the loop coercivity is 1: see normalize)
    double gap = 1.5, spacing = 0.3, coat = 3.5;
    int nl = 12;                              // sub-layers through the coating (reference)
    double fb = 62500.0;                      // bias frequency
};

inline double Phi(double z) { return 0.5 * std::erfc(-z / std::sqrt(2.0)); }

inline double karlqvist(double x, double y, double gap)
{
    return (std::atan((x + 0.5 * gap) / y) - std::atan((x - 0.5 * gap) / y)) / kPi;
}

// Gauss-Legendre nodes on [-1, 1]
struct GL
{
    std::vector<double> x, w;
    explicit GL(int n)
    {
        x.resize(n); w.resize(n);
        for (int i = 0; i < n; ++i)
        {
            double z = std::cos(kPi * (i + 0.75) / (n + 0.5)), pp = 0;
            for (int it = 0; it < 100; ++it)
            {
                double p1 = 1, p2 = 0;
                for (int j = 0; j < n; ++j) { double p3 = p2; p2 = p1; p1 = ((2 * j + 1) * z * p2 - j * p3) / (j + 1); }
                pp = n * (z * p1 - p2) / (z * z - 1);
                double z1 = z; z = z1 - p1 / pp;
                if (std::fabs(z - z1) < 1e-15) break;
            }
            x[i] = z; w[i] = 2 / ((1 - z * z) * pp * pp);
        }
    }
};

struct Density
{
    Par p;
    double ulo, uhi;           // ln hc range
    GL gl{48};
    explicit Density(const Par& q) : p(q)
    {
        ulo = std::log(p.hc0) - 6.5 * p.sc; uhi = std::log(p.hc0) + 6.5 * p.sc;
    }
    // integral over u = ln hc in [u0, u1] of N(u) f(hc)
    template <class F> double integ(double u0, double u1, F f) const
    {
        if (u1 <= u0) return 0;
        const double m = 0.5 * (u0 + u1), r = 0.5 * (u1 - u0), mu = std::log(p.hc0);
        double s = 0;
        for (size_t i = 0; i < gl.x.size(); ++i)
        {
            const double u = m + r * gl.x[i], d = (u - mu) / p.sc;
            s += gl.w[i] * std::exp(-0.5 * d * d) * f(std::exp(u));
        }
        return s * r / (p.sc * std::sqrt(2 * kPi));
    }
    template <class F> double integSplit(std::vector<double> cuts, F f) const   // cuts in hc
    {
        std::vector<double> us{ulo};
        for (double c : cuts) if (c > 0) { double u = std::log(c); if (u > ulo && u < uhi) us.push_back(u); }
        us.push_back(uhi);
        std::sort(us.begin(), us.end());
        double s = 0;
        for (size_t i = 0; i + 1 < us.size(); ++i) s += integ(us[i], us[i + 1], f);
        return s;
    }
    // E(a, b) = mass{alpha <= a, beta < b}
    double E(double a, double b) const
    {
        const double si = p.si;
        return integSplit({0.5 * (a - b)}, [&](double hc) { return Phi(std::min(a - hc, b + hc) / si); });
    }
    // Er(a, b) = mass{alpha > a, beta < b, hu < 0}
    double Er(double a, double b) const
    {
        const double si = p.si;
        return integSplit({0.5 * (a - b), -b, a}, [&](double hc) {
            const double up = std::min(b + hc, 0.0), lo = a - hc;
            return up > lo ? Phi(up / si) - Phi(lo / si) : 0.0;
        });
    }
    // density of beta (descending branch from + saturation: dM/dh = 2 f_beta(h))
    double fbeta(double be) const
    {
        const double si = p.si;
        return integSplit({}, [&](double hc) { const double z = (be + hc) / si; return std::exp(-0.5 * z * z) / (si * std::sqrt(2 * kPi)); });
    }
    double massBetaAbove(double h) const   // mass{beta >= h}
    {
        const double si = p.si;
        return integSplit({}, [&](double hc) { return 1.0 - Phi((h + hc) / si); });
    }
};

// Loop figures: coercivity of the major loop (descending branch M = 0), SFD = FWHM of dM/dh over Hc,
// squareness of the irreversible part M(0)/Msat.
struct LoopFig { double hc, sfd, sq; };
inline LoopFig loopFigures(const Par& p)
{
    Density d(p);
    double lo = -5, hi = 0;
    for (int i = 0; i < 80; ++i) { double m = 0.5 * (lo + hi); if (d.massBetaAbove(m) > 0.5) lo = m; else hi = m; }
    const double hcl = -0.5 * (lo + hi);
    double pk = 0, xp = 0;
    for (double x = -4; x <= 0.5; x += 0.0005) { double f = d.fbeta(x); if (f > pk) { pk = f; xp = x; } }
    auto edge = [&](double dir) { double x = xp; while (d.fbeta(x) > 0.5 * pk) x += dir * 0.0005; return x; };
    const double fw = edge(+1) - edge(-1);
    return { hcl, fw / hcl, 1.0 - 2.0 * d.massBetaAbove(0.0) };
}
// rescale hc0 (and si with it) so the loop coercivity is exactly 1
inline Par normalize(Par p)
{
    for (int it = 0; it < 3; ++it) { LoopFig f = loopFigures(p); p.hc0 /= f.hc; p.si /= f.hc; }
    return p;
}

// Bicubic table of E and Er on a grid (for the envelope form and the sweeps)
struct ETable
{
    double a0 = -1.0, a1 = 4.6, b0 = -4.6, b1 = 1.0, st = 0.01;
    int na = 0, nb = 0;
    std::vector<double> e, er;
    void build(const Density& d, double step = 0.01)
    {
        st = step; na = (int)std::lround((a1 - a0) / st) + 1; nb = (int)std::lround((b1 - b0) / st) + 1;
        e.assign((size_t)na * nb, 0); er.assign((size_t)na * nb, 0);
#pragma omp parallel for schedule(dynamic)
        for (int i = 0; i < na; ++i)
            for (int j = 0; j < nb; ++j)
            {
                const double a = a0 + i * st, b = b0 + j * st;
                e[(size_t)i * nb + j] = d.E(a, b);
                er[(size_t)i * nb + j] = d.Er(a, b);
            }
    }
    static double cr(double p0, double p1, double p2, double p3, double t)
    {
        return p1 + 0.5 * t * (p2 - p0 + t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (3 * (p1 - p2) + p3 - p0)));
    }
    double look(const std::vector<double>& T, double a, double b) const
    {
        a = std::clamp(a, a0, a1); b = std::clamp(b, b0, b1);
        const double fa = (a - a0) / st, fbv = (b - b0) / st;
        int ia = std::min((int)fa, na - 2), ib = std::min((int)fbv, nb - 2);
        const double ta = fa - ia, tb = fbv - ib;
        double r[4];
        for (int q = 0; q < 4; ++q)
        {
            const int ii = std::clamp(ia - 1 + q, 0, na - 1);
            const double* row = &T[(size_t)ii * nb];
            const int j0 = std::clamp(ib - 1, 0, nb - 1), j1 = ib, j2 = ib + 1, j3 = std::clamp(ib + 2, 0, nb - 1);
            r[q] = cr(row[j0], row[j1], row[j2], row[j3], tb);
        }
        return cr(r[0], r[1], r[2], r[3], ta);
    }
    double E(double a, double b) const { return look(e, a, b); }
    double Er(double a, double b) const { return look(er, a, b); }
};

// The engine evaluating E: exact quadrature or table
struct Eval
{
    const Density* d = nullptr;
    const ETable* t = nullptr;
    double E(double a, double b) const { return t ? t->E(a, b) : d->E(a, b); }
    double Er(double a, double b) const { return t ? t->Er(a, b) : d->Er(a, b); }
};

// Final magnetization (-1..1) of an element from its upper/lower envelope samples U[k], L[k] along
// the path in time order (raw carrier: U = L = H). Trapezoid in the steps where both records move
// (envelope form: the crossing times are interpolated linearly within the step); exact where one
// record moves at a time (raw carrier).
inline double recordPath(const Eval& ev, const double* U, const double* L, int n)
{
    double A = U[n - 1], B = L[n - 1];
    double mass = ev.E(A, B);                 // hysterons reached only at the very end (negligible)
    double runA = A;                          // start of the current run of A-records at constant B
    for (int k = n - 2; k >= 0; --k)
    {
        const double nA = std::max(A, U[k]), nB = std::min(B, L[k]);
        if (nB != B)
        {
            if (nA != A || runA != A)
            {
                // flush the run of A-records at the old B, then this step's (trapezoid) move
                if (runA != A) mass += ev.E(A, B) - ev.E(runA, B);
                if (nA != A) mass += 0.5 * (ev.E(nA, nB) + ev.E(nA, B) - ev.E(A, nB) - ev.E(A, B));
            }
            runA = nA;
        }
        A = nA; B = nB;
    }
    if (runA != A) mass += ev.E(A, B) - ev.E(runA, B);
    mass += ev.Er(A, B);                      // never reached: AC-erased state
    return 2.0 * mass - 1.0;
}

// The tape and the head for one speed
struct Geometry
{
    Par p;
    double v;                     // um/s
    std::vector<double> y, w;     // sub-layer center depth and thickness share
    std::vector<double> ya, yb;   // sub-layer limits
    Geometry(const Par& q, double v_um_s, int nl) : p(q), v(v_um_s)
    {
        for (int j = 0; j < nl; ++j)
        {
            const double a = q.spacing + q.coat * j / nl, b = q.spacing + q.coat * (j + 1) / nl;
            ya.push_back(a); yb.push_back(b); y.push_back(0.5 * (a + b)); w.push_back(1.0 / nl);
        }
    }
    // playback weight of sub-layer j at frequency f: mean of exp(-k y) over the sub-layer, times its share
    double play(int j, double f) const
    {
        const double k = 2 * kPi * f / v;
        if (k * (yb[j] - ya[j]) < 1e-9) return w[j] * std::exp(-k * y[j]);
        return w[j] * (std::exp(-k * ya[j]) - std::exp(-k * yb[j])) / (k * (yb[j] - ya[j]));
    }
};

// A periodic tone recorded: per-layer harmonics (complex, 1..nh) of M over one signal period.
struct ToneSpec { double f, h, Hb; int nE = 64; };
// RAW: explicit carrier. ENV: the plain envelope rule (continuous last-crossing times).
// ENV2: the envelope rule corrected for the finite bias period. With positive peaks at phi + nT and
// negative ones half a period later, and the carrier phase uniform (the playback head averages over
// many bias wavelengths), the last negative peak before the continuous down-crossing t_dn lies
// s ~ U[0, T) before it, and the element ends +1 iff the next (positive) peak N + T/2 still reaches
// alpha: P(+1 | dt = t_up - t_dn) = clamp(1/2 + dt / T, 0, 1). That is the plain envelope rule
// averaged over a shift of the lower envelope uniform in [-T/2, T/2]; done here with nShift
// Gauss-Legendre points of that uniform distribution.
// FAST: the signal taken as constant through the head field, at its value d_j after the element
// passes the gap center (ps.delay, per layer), each element then recorded by ps.fastBase.
enum Mode { RAW, ENV, ENV2, FAST };
struct PathSpec { double X = 25.0; double dxEnv = 0.05; int cyc = 256; int nPhase = 8; int nShift = 3;
                  Mode fastBase = RAW; const std::vector<double>* delay = nullptr; };

// signal: Hs(t) for the tone h sin(2 pi f t) plus an optional second component (two-tone tests)
inline std::vector<std::vector<cd>> recordTone(const Geometry& G, const Eval& ev, Mode mode0, const ToneSpec& T,
                                               const PathSpec& ps, int nh = 7, const std::vector<int>* layers = nullptr)
{
    const bool fast = mode0 == FAST;
    const Mode mode = fast ? ps.fastBase : mode0;
    std::vector<int> js;
    if (layers) js = *layers; else for (int j = 0; j < (int)G.y.size(); ++j) js.push_back(j);
    std::vector<std::vector<cd>> out(G.y.size(), std::vector<cd>(nh + 1, 0.0));
    const int nE = T.nE;
    std::vector<double> Mv((size_t)G.y.size() * nE, 0.0);
    // path sampling
    const double dx = mode == RAW ? G.v / (G.p.fb * ps.cyc) : ps.dxEnv;
    const int n = (int)std::lround(2 * ps.X / dx) + 1;
    std::vector<std::vector<double>> g(G.y.size());
    for (int j : js) { g[j].resize(n); for (int i = 0; i < n; ++i) g[j][i] = karlqvist(-ps.X + i * dx, G.y[j], G.p.gap); }
    const int tasks = (int)js.size() * nE;
#pragma omp parallel
    {
        std::vector<double> U(n), L(n), hs(n);
#pragma omp for schedule(dynamic)
        for (int q = 0; q < tasks; ++q)
        {
            const int j = js[q / nE], e = q % nE;
            const double t0 = (double)e / (nE * T.f);
            const double hc = fast ? T.h * std::sin(2 * kPi * T.f * (t0 + (ps.delay ? (*ps.delay)[j] : 0.0))) : 0.0;
            auto sig = [&](double x) { return fast ? hc : T.h * std::sin(2 * kPi * T.f * (t0 + x / G.v)); };
            for (int i = 0; i < n; ++i) hs[i] = sig(-ps.X + i * dx);
            double m = 0;
            if (mode == RAW)
            {
                for (int ph = 0; ph < ps.nPhase; ++ph)
                {
                    const double phi = 2 * kPi * ph / ps.nPhase;
                    for (int i = 0; i < n; ++i)
                        U[i] = g[j][i] * (hs[i] + T.Hb * std::sin(2 * kPi * G.p.fb * (-ps.X + i * dx) / G.v + phi));
                    m += recordPath(ev, U.data(), U.data(), n);
                }
                m /= ps.nPhase;
            }
            else if (mode == ENV)
            {
                for (int i = 0; i < n; ++i) { U[i] = g[j][i] * (hs[i] + T.Hb); L[i] = g[j][i] * (hs[i] - T.Hb); }
                m = recordPath(ev, U.data(), L.data(), n);
            }
            else
            {
                const double hx = G.v * 0.5 / G.p.fb;      // half a bias wavelength
                GL q(ps.nShift);
                for (int i = 0; i < n; ++i) U[i] = g[j][i] * (hs[i] + T.Hb);
                for (int s = 0; s < ps.nShift; ++s)
                {
                    for (int i = 0; i < n; ++i)
                    {
                        const double x = -ps.X + i * dx - q.x[s] * hx;
                        L[i] = karlqvist(x, G.y[j], G.p.gap) * (sig(x) - T.Hb);
                    }
                    m += 0.5 * q.w[s] * recordPath(ev, U.data(), L.data(), n);
                }
            }
            Mv[(size_t)j * nE + e] = m;
        }
    }
    for (int j : js)
        for (int hn = 1; hn <= nh; ++hn)
        {
            cd s = 0;
            for (int e = 0; e < nE; ++e) s += Mv[(size_t)j * nE + e] * std::polar(1.0, -2 * kPi * hn * e / nE);
            out[j][hn] = s * (2.0 / nE);     // complex amplitude: M = Re(c e^{i w t}), sin -> c = -i
        }
    return out;
}

// sum of the layers as played back (no equalization): complex amplitude of harmonic hn
inline cd playback(const Geometry& G, const std::vector<std::vector<cd>>& H, double f, int hn)
{
    cd s = 0;
    for (size_t j = 0; j < G.y.size(); ++j) s += G.play((int)j, hn * f) * H[j][hn];
    return s;
}

} // namespace hy
