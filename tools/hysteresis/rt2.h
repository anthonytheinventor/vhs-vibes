// Round 2 real-time hysteresis recorder: the table engine with a slope axis ("SLOPE"), stereo in
// 2-lane vectors (L, R), as the plugin is written. FAST is the same engine with one slope point.
//
// Per channel, per 44.1 kHz sample:
//   Hs (record field) -> x os (the plugin's half-band, cascaded) -> history ring
//   per oversampled sample, per playback group g (sub-layers of the coating merged):
//     A = Hs(t_e + d_g), S = Hs'(t_e + d_g)            (4-tap cubic, taps fixed per group and plane)
//     u = s / (sc + |s|),  s = S * tauRef               (slope compressed into -1..1)
//     M_g = T_g(A, u)                                   (table: cubic in A, linear in u)
//   per group: / os (half-band) -> playback filter (group's playback loss x playback EQ) -> sum
// T_g is the carrier reference's remanence for a signal that is a straight ramp through the head
// field, value A and slope S at the time t_e + d_g, averaged over the group's sub-layers.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>

namespace rt2 {

typedef float v2 __attribute__((vector_size(8)));
typedef float v4 __attribute__((vector_size(16)));
inline v2 sp2(float x) { return v2{x, x}; }

constexpr float kHb[5] = { 0.0863437797f, 0.2952210021f, 0.5320824744f, 0.7393734977f, 0.9145070455f };
struct HB
{
    v2 s[5][2] = {};
    inline v2 ap(int i, v2 x) { const v2 y = kHb[i] * (x - s[i][1]) + s[i][0]; s[i][0] = x; s[i][1] = y; return y; }
    inline v2 even(v2 x) { return ap(4, ap(2, ap(0, x))); }
    inline v2 odd(v2 x) { return ap(3, ap(1, x)); }
    inline void up(v2 x, v2& a, v2& b) { a = even(x); b = odd(x); }
    inline v2 down(v2 a, v2 b) { return 0.5f * (even(b) + odd(a)); }
};
struct Up
{
    int st = 0; HB h[3];
    void run(v2 x, v2* out)
    {
        out[0] = x; int n = 1; v2 tmp[8];
        for (int s = 0; s < st; ++s)
        {
            for (int i = 0; i < n; ++i) h[s].up(out[i], tmp[2 * i], tmp[2 * i + 1]);
            n *= 2;
            for (int i = 0; i < n; ++i) out[i] = tmp[i];
        }
    }
};
struct Down
{
    int st = 0; HB h[3];
    v2 run(v2* in)
    {
        int n = 1 << st;
        for (int s = st - 1; s >= 0; --s) { n /= 2; for (int i = 0; i < n; ++i) in[i] = h[s].down(in[2 * i], in[2 * i + 1]); }
        return in[0];
    }
};

constexpr int kMaxJ = 4, kFir = 64, kRing = 512, kMaxBq = 4;

// biquad cascade on both channels (playback filter as an IIR at 44.1 kHz)
struct BqC { float b0, b1, b2, a1, a2; };
struct BqS { v2 z1 = {}, z2 = {}; };

struct Group
{
    // where and how the element reads the history: taps for samples e+io-1 .. e+io+2
    int io = 0; float cv[4] = {}, cs[4] = {};
    std::vector<float> T;          // [nu][nh], the current plane (or the blend of two)
    std::vector<float> Gi;         // [nu][nh], its integral along A from A = 0 (first-order ADAA)
    float fir[kFir] = {};          // playback filter as FIR (reference implementation)
    int nbq = 0; BqC bq[kMaxBq] = {}; float bqGain = 1.0f; int bqDelay = 0;   // or as IIR
    float rp[3] = {}, rq[3] = {}; float rAmax = 6.0f;   // fitted odd rational M = A(p0+p1x+p2x^2)/(1+q1x+q2x^2+q3x^3), x = A^2
};
struct Cfg
{
    int os = 4, J = 3, nh = 0, nu = 1, lat = 8;
    float h0 = -6.0f, invh = 1.0f / 0.05f, sc = 1.0f, tauS = 1.0f, umax = 0.95f;   // tauS: seconds-per-micrometer x fs (slope scale)
    bool useIir = false, cubicA = true, adaa = false, rational = false;
    Group g[kMaxJ];
};

struct Core
{
    const Cfg* c = nullptr;
    Up up; Down dn[kMaxJ];
    v2 ring[kRing] = {};
    int w = 0;
    v2 fh[kMaxJ][2 * kFir] = {}; int fp = 0;
    BqS bs[kMaxJ][kMaxBq];
    v2 dl[kMaxJ][8] = {}; int dp = 0;
    v2 Ap[kMaxJ] = {}, Gp[kMaxJ] = {};
    void init(const Cfg& cfg)
    {
        c = &cfg;
        const int st = cfg.os == 1 ? 0 : cfg.os == 2 ? 1 : cfg.os == 4 ? 2 : 3;
        up = Up(); up.st = st;
        for (auto& d : dn) { d = Down(); d.st = st; }
        std::memset(ring, 0, sizeof(ring)); w = 0; std::memset(fh, 0, sizeof(fh)); fp = 0;
        for (auto& a : bs) for (auto& b : a) b = BqS();
        std::memset(dl, 0, sizeof(dl)); dp = 0; std::memset(Ap, 0, sizeof(Ap)); std::memset(Gp, 0, sizeof(Gp));
    }
    // after the working table changed (Bias knob): the stored integral of the last sample is stale
    void onTableChange()
    {
        const Cfg& C = *c;
        for (int j = 0; j < C.J; ++j) Gp[j] = v2{gInt(C.g[j], Ap[j][0], 0.0f), gInt(C.g[j], Ap[j][1], 0.0f)};
    }
    // table lookup for one lane
    inline float look(const Group& G, float a, float u) const { return lookT(G.T, a, u); }
    // the integral along A, continued past the table's ends with the end value's slope
    inline float gInt(const Group& G, float a, float u) const
    {
        const Cfg& C = *c;
        const float lo = C.h0 + 2.0f / C.invh, hi = -C.h0 - 3.0f / C.invh;
        if (a >= lo && a <= hi) return lookT(G.Gi, a, u);
        const float ac = a < lo ? lo : hi;
        return lookT(G.Gi, ac, u) + lookT(G.T, ac, u) * (a - ac);
    }
    inline float lookT(const std::vector<float>& Tv, float a, float u) const
    {
        const Cfg& C = *c;
        float fa = (a - C.h0) * C.invh;
        fa = std::min(std::max(fa, 1.0f), (float)(C.nh - 3) - 0.001f);
        const int ia = (int)fa; const float ta = fa - (float)ia;
        float fu = 0.0f; int iu = 0;
        if (C.nu > 1)
        {
            fu = (u + C.umax) * (0.5f * (float)(C.nu - 1) / C.umax);
            fu = std::min(std::max(fu, 0.0f), (float)(C.nu - 1) - 0.001f);
            iu = (int)fu; fu -= (float)iu;
        }
        const float* r0 = &Tv[(size_t)iu * C.nh + ia];
        auto row = [&](const float* r) {
            if (!C.cubicA) return r[0] + ta * (r[1] - r[0]);
            // Catmull-Rom through r[-1..2]
            const float p0 = r[-1], p1 = r[0], p2 = r[1], p3 = r[2];
            return p1 + 0.5f * ta * (p2 - p0 + ta * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + ta * (3.0f * (p1 - p2) + p3 - p0)));
        };
        const float v0 = row(r0);
        if (C.nu == 1) return v0;
        const float v1 = row(r0 + C.nh);
        return v0 + fu * (v1 - v0);
    }
    void process(float* L, float* R, int n)
    {
        const Cfg& C = *c;
        const int os = C.os, J = C.J;
        v2 buf[8], mj[kMaxJ][8];
        for (int i = 0; i < n; ++i)
        {
            up.run(v2{L[i], R[i]}, buf);
            for (int o = 0; o < os; ++o)
            {
                ring[w & (kRing - 1)] = buf[o];
                const int e = w - C.lat;
                ++w;
                for (int j = 0; j < J; ++j)
                {
                    const Group& G = C.g[j];
                    const int b = e + G.io;
                    const v2 y0 = ring[(b - 1) & (kRing - 1)], y1 = ring[b & (kRing - 1)], y2 = ring[(b + 1) & (kRing - 1)], y3 = ring[(b + 2) & (kRing - 1)];
                    const v2 A = G.cv[0] * y0 + G.cv[1] * y1 + G.cv[2] * y2 + G.cv[3] * y3;
                    v2 u = sp2(0.0f);
                    if (C.nu > 1)
                    {
                        const v2 s = (G.cs[0] * y0 + G.cs[1] * y1 + G.cs[2] * y2 + G.cs[3] * y3) * C.tauS;
                        const v2 as = v2{std::fabs(s[0]), std::fabs(s[1])};
                        u = s / (C.sc + as);
                    }
                    if (C.rational)
                    {
                        // closed form in both lanes at once (no table, no gather)
                        v2 a = A;
                        a = a > G.rAmax ? sp2(G.rAmax) : a;
                        a = a < -G.rAmax ? sp2(-G.rAmax) : a;
                        const v2 x = a * a;
                        const v2 num = a * (G.rp[0] + x * (G.rp[1] + x * G.rp[2]));
                        const v2 den = 1.0f + x * (G.rq[0] + x * (G.rq[1] + x * G.rq[2]));
                        mj[j][o] = num / den;
                    }
                    else if (!C.adaa) mj[j][o] = v2{look(G, A[0], u[0]), look(G, A[1], u[1])};
                    else
                    {
                        // first-order antiderivative anti-aliasing along A (u held at this sample's)
                        const v2 ap = Ap[j]; Ap[j] = A;
                        v2 r;
                        if (C.nu == 1)
                        {
                            // no slope axis: the previous sample's integral is still valid, one lookup per lane
                            const v2 g = v2{gInt(G, A[0], 0.0f), gInt(G, A[1], 0.0f)}, gp = Gp[j]; Gp[j] = g;
                            const v2 d = A - ap;
                            for (int k = 0; k < 2; ++k)
                                r[k] = std::fabs(d[k]) > 0.005f ? (g[k] - gp[k]) / d[k] : look(G, 0.5f * (A[k] + ap[k]), 0.0f);
                            mj[j][o] = r;
                            continue;
                        }
                        for (int k = 0; k < 2; ++k)
                        {
                            const float d = A[k] - ap[k];
                            r[k] = std::fabs(d) > 0.005f ? (gInt(G, A[k], u[k]) - gInt(G, ap[k], u[k])) / d
                                                         : look(G, 0.5f * (A[k] + ap[k]), u[k]);
                        }
                        mj[j][o] = r;
                    }
                }
            }
            v2 y = sp2(0.0f);
            if (!C.useIir)
            {
                fp = (fp - 1) & (kFir - 1);
                for (int j = 0; j < J; ++j)
                {
                    const v2 v = dn[j].run(mj[j]);
                    fh[j][fp] = v; fh[j][fp + kFir] = v;
                    const float* f = C.g[j].fir; const v2* hh = &fh[j][fp];
                    v2 a0 = sp2(0.0f), a1 = sp2(0.0f);
                    for (int t = 0; t < kFir; t += 2) { a0 += f[t] * hh[t]; a1 += f[t + 1] * hh[t + 1]; }
                    y += a0 + a1;
                }
            }
            else
            {
                dp = (dp + 1) & 7;
                for (int j = 0; j < J; ++j)
                {
                    const Group& G = C.g[j];
                    v2 v = dn[j].run(mj[j]) * G.bqGain;
                    dl[j][dp] = v;
                    v = dl[j][(dp - G.bqDelay) & 7];
                    for (int q = 0; q < G.nbq; ++q)
                    {
                        const BqC& k = G.bq[q]; BqS& s = bs[j][q];
                        const v2 out = k.b0 * v + s.z1;
                        s.z1 = k.b1 * v - k.a1 * out + s.z2;
                        s.z2 = k.b2 * v - k.a2 * out;
                        v = out;
                    }
                    y += v;
                }
            }
            L[i] = y[0]; R[i] = y[1];
        }
    }
};


// Fitted high-frequency self-erasure correction (round 3), 44.1 kHz, both channels in the lanes.
// Side-chain: the record field's slope, sigma = |dHs/dt| * tauRef (field per micrometer of tape travel),
// through a peak follower (attack, release). Above the knee e0 the high band (first-order high-pass
// at fc) of the played-back signal is scaled by g = 1 / (1 + c (env - e0)): a level-dependent shelf.
struct HfCorr
{
    float kSig = 1.0f, e0 = 4.0f, c = 0.1f, aAtt = 0.05f, aRel = 0.001f;   // kSig = 44100 * tauRef
    float hpA = 0.9f;                                                       // one-pole high-pass coefficient
    v2 hPrev = {}, env = {}, lpY = {};
    void reset() { hPrev = env = lpY = v2{0.0f, 0.0f}; }
    inline v2 run(v2 hs, v2 y)
    {
        const v2 d = (hs - hPrev) * kSig; hPrev = hs;
        for (int k = 0; k < 2; ++k)
        {
            const float s = std::fabs(d[k]);
            env[k] += (s > env[k] ? aAtt : aRel) * (s - env[k]);
        }
        // low band by a one-pole low-pass; the high band is the rest (complementary, no phase seam)
        lpY += (1.0f - hpA) * (y - lpY);
        const v2 hi = y - lpY;
        v2 x = env - e0;
        x = x > 0.0f ? x : v2{0.0f, 0.0f};
        const v2 g = 1.0f / (1.0f + c * x);
        return lpY + g * hi;
    }
};

// blend two planes into the working tables (when the Bias knob moves): T = (1-f) P0 + f P1
inline void blendPlanes(Cfg& c, const std::vector<std::vector<float>>& p0, const std::vector<std::vector<float>>& p1, float f)
{
    for (int j = 0; j < c.J; ++j)
    {
        float* t = c.g[j].T.data(); const float* a = p0[j].data(); const float* b = p1[j].data();
        const size_t n = c.g[j].T.size();
        for (size_t i = 0; i < n; ++i) t[i] = a[i] + f * (b[i] - a[i]);
    }
}

} // namespace rt2
