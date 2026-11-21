// VHS Vibes 1.8: the tape hysteresis recorder (Sat Type VHS, Hysteresis Lite or Full).
//
// SPDX-License-Identifier: MIT
//
// Included by VHSVibes.cpp inside its namespace, after HalfBand (it uses Stereo, Quad and HalfBand from
// there). The tables come from src/hysteresis_tables.h, made by tools/hysteresis/r3.cpp (mode "plugin")
// from a carrier simulation of the linear track: 12 layers of the coating, AC bias at 62.5 kHz, a
// Preisach model of the particles. What a group of layers keeps (its remanence) is tabulated against
// the record field A where the element leaves the gap and, for Full, the slope of the field there.
//
// Per channel (left and right are the two lanes of one Stereo):
//
//   Hs = kFieldPerUnit * w            w: the pre-emphasized record signal (TapeSat's), Rec Level applied
//   -> twice the rate (HalfBand)      HQ on: the signal is at twice the rate already
//   -> ring of the last kRing samples
//   -> per group g = 0..2, per sample at twice the rate, at the element time e = newest - kLat:
//        A = cubic (Lagrange) value of Hs at e + d0_g, S = its slope there (Full only)
//        u = s / (kSc + |s|), s = S * tauS                   (Full only; Lite has u = 0)
//        M_g = the mean of T_g(., u) between the last A and this one (first-order antiderivative
//              anti-aliasing: (Gi(A) - Gi(A_prev)) / (A - A_prev), Gi the integral of T along A)
//   -> per group: back to the session rate (HalfBand) -> 64-tap playback FIR -> sum of the groups
//   -> high-frequency self-erasure correction (HfCorr, at the session rate)
//   -> / kFieldPerUnit                (then TapeSat's de-emphasis, band edge and make-up, in the plugin)
//
// Lite (FAST) and Full (SLOPE) have the same latency, kLatency, at both speeds. The Bias knob picks
// two neighboring bias planes and their blend; the working tables (T, Gi, the group delays and the
// interpolation taps) are worked out again when the bias has moved (setBias): Lite every 32 samples,
// Full every 128 while it moves. A Full update reads the two planes once and writes T and Gi once (about
// 200 KB) in about 82,000 instructions (arm32; it was about 250,000), a sixth of what a steady Full block
// costs the whole plugin. Nothing here allocates, locks or does I/O.
#pragma once

namespace hys {

using namespace hysttab;
constexpr int   kRing = 128;                 // record field history at twice the rate (the reads reach lat + 3 back)
constexpr int   kPlane = kNu * kNh;          // one group's table, Full (Lite reads its middle row)
constexpr int   kUMid = kNu / 2;             // the row of u = 0
constexpr float kLatency = 41.5f;            // samples at the session rate, both engines, both speeds (by design)
constexpr float kInvField = 1.0f / kFieldPerUnit;
// The high-frequency correction, fitted to the carrier simulation: [engine][speed] = {fc Hz, e0, c}
constexpr float kCorr[2][2][3] = { { {700.0f, 14.0f, 0.08f}, {1000.0f, 4.0f, 0.02f} },
                                   { {9000.0f, 10.0f, 0.30f}, {1000.0f, 6.0f, 0.02f} } };

// Bias as a percentage of correct bias from the plugin's bias value (-1 too little .. 0 right .. +1 too
// much, TapeSat::set's): 25 % at the bottom, 100 % at "right", 150 % at the top, in a straight line on
// either side.
inline float biasPercent(float bias) { return bias < 0.0f ? 100.0f + 75.0f * bias : 100.0f + 50.0f * bias; }

// One engine (Lite or Full) at one tape speed, with its own working tables.
struct Engine
{
    int   slope = 0, ep = 0;                  // Full (the slope axis) or Lite; EP or SP
    int   nu = 1;                             // slope points read: 1 (Lite) or kNu (Full)
    float bp = -1.0f;                         // the bias (%) its working tables hold; -1: none yet
    float tauS = 1.0f;                        // slope scale: samples at twice the rate per micrometer
    float uScale = 0.0f;                      // u to the table's row (from kUMid)
    float fs2 = 88200.0f;                     // twice the session rate
    int   lat = kLat;                         // the element lag, samples at twice the rate (see configure)
    int   io[kJ] = {};                        // where each group reads the ring, and its taps
    float cv[kJ][4] = {}, cs[kJ][4] = {};
    Stereo ap[kJ] = {}, gp[kJ] = {};          // last A, and (Lite) the integral there
    HalfBand dn[kJ];
    Quad  fir[kJ][kFir / 2] = {};             // the playback FIRs, tap pairs {f0, f0, f1, f1}
    Stereo fh[kJ][2 * kFir] = {};             // their histories, twice over (no wrap in the sum)
    int   fp = 0;
    // high-frequency correction
    float kSig = 1.0f, e0 = 0.0f, cc = 0.0f, aAtt = 0.0f, aRel = 0.0f, lpA = 0.0f;
    Stereo hPrev = {}, env = {}, lpY = {};
    // working tables: T (Full: [u][A]; Lite: the row of u = 0 only) and its integral along A from A = 0
    float T[kJ][kPlane] = {}, Gi[kJ][kPlane] = {};

    // sr: the session rate
    void configure(int slopeAxis, int speedEp, float sr)
    {
        slope = slopeAxis; ep = speedEp; nu = slope ? kNu : 1;
        fs2 = 2.0f * sr;
        // kLat at 44.1 kHz; at a higher rate the groups' delays (in seconds) are more samples, and the lag
        // grows with them so that no read reaches past the newest sample (lagOf)
        lat = lagOf(sr);
        tauS = fs2 / kSpeed[ep];
        uScale = (float)kUMid / kUmax;
        const float (*f)[kFir] = slope ? (ep ? kFirSlopeEp : kFirSlopeSp) : (ep ? kFirFastEp : kFirFastSp);
        for (int j = 0; j < kJ; ++j)
            for (int k = 0; k < kFir / 2; ++k) fir[j][k] = Quad{ f[j][2 * k], f[j][2 * k], f[j][2 * k + 1], f[j][2 * k + 1] };
        const float* p = kCorr[slope][ep];
        kSig = sr / kSpeed[ep];
        e0 = p[1]; cc = p[2];
        aAtt = 1.0f - std::exp(-1.0f / (0.0005f * sr));
        aRel = 1.0f - std::exp(-1.0f / (0.030f * sr));
        lpA = 1.0f - std::exp(-kTwoPi * p[0] / sr);
        bp = -1.0f;
        reset();
    }
    static int lagOf(float sr)
    {
        const int l = (int)std::ceil((double)kLat * (double)sr / 44100.0 - 1e-6);
        return l < kLat ? kLat : l;
    }
    void reset()
    {
        for (int j = 0; j < kJ; ++j) { ap[j] = gp[j] = splat(0.0f); dn[j].reset(); for (Stereo& v : fh[j]) v = splat(0.0f); }
        fp = 0;
        hPrev = env = lpY = splat(0.0f);
    }

    // The working tables for bias b (%): the two planes around it blended, the integral along A by the
    // trapezoid rule from A = 0, the groups' delays blended and their taps. Lite keeps its reused
    // integral of the last A valid (otherwise the next sample would click).
    void setBias(float b)
    {
        bp = b;
        b = clamp(b, kBiasPct[0], kBiasPct[kNb - 1]);
        int k = 0;
        while (k + 2 < kNb && kBiasPct[k + 1] <= b) ++k;
        const float f = clamp((b - kBiasPct[k]) / (kBiasPct[k + 1] - kBiasPct[k]), 0.0f, 1.0f);
        const float (*P)[kJ][kPlane] = ep ? kTEp : kTSp;
        const float (*D)[kJ] = ep ? kD0Ep : kD0Sp;
        const int off = slope ? 0 : kUMid * kNh;
        for (int j = 0; j < kJ; ++j)
        {
            for (int u = 0; u < nu; ++u)
                blendRow(&P[k][j][off + u * kNh], &P[k + 1][j][off + u * kNh], f, T[j] + u * kNh, Gi[j] + u * kNh);
            // the group's delay and its Lagrange taps for the samples -1 .. 2 around it (value and slope), in
            // double: worked out in float they differed from the prototype's by a rounding, and where A moves by
            // just over the difference quotient's threshold (0.005) that showed at -74 dB on single samples
            const double d = (1.0 - f) * D[k][j] + (double)f * D[k + 1][j];
            const double p = d * (double)fs2, fl = std::floor(p), tt = p - fl;
            io[j] = (int)fl;
            cv[j][0] = (float)(-tt * (tt - 1) * (tt - 2) / 6);
            cv[j][1] = (float)((tt + 1) * (tt - 1) * (tt - 2) / 2);
            cv[j][2] = (float)(-(tt + 1) * tt * (tt - 2) / 2);
            cv[j][3] = (float)((tt + 1) * tt * (tt - 1) / 6);
            cs[j][0] = (float)(-(3 * tt * tt - 6 * tt + 2) / 6);
            cs[j][1] = (float)((3 * tt * tt - 4 * tt - 1) / 2);
            cs[j][2] = (float)(-(3 * tt * tt - 2 * tt - 2) / 2);
            cs[j][3] = (float)((3 * tt * tt - 1) / 6);
            if (!slope) gp[j] = Stereo{ gInt(Gi[j], T[j], ap[j][0], 0.0f), gInt(Gi[j], T[j], ap[j][1], 0.0f) };
        }
    }
    // One row of the working tables: the two planes' rows blended (a + f (c - a)) and the integral of the
    // blend along A by the trapezoid rule from A = 0 (the middle of the row) out to either end. Operation
    // for operation what 1.8 first did in two passes, so the values are the same to the bit (checked for
    // 2,000 bias values, both engines and speeds, on x86-64, 32-bit and 64-bit ARM), in one pass and a third
    // of the instructions: four blends at a time, and the running sum taken from them while they are still
    // in registers (the row is read once and written once, never read back).
    // a + f (c - a) four at a time, rounded as the compiler rounds the scalar expression on this target: where
    // the hardware has a fused multiply-add clang fuses the scalar one (one rounding), but on 32-bit ARM not
    // the vector one (NEON's vmul and vadd: two roundings), so it is asked to. Another compiler gets the
    // scalar expression itself, lane by lane (the same rounding as the scalar code by construction).
    static inline Quad lerp4(Quad a, Quad c, Quad f)
    {
#if defined(__clang__) && (defined(__ARM_FEATURE_FMA) || defined(__FMA__))
        return __builtin_elementwise_fma(f, c - a, a);
#elif defined(__clang__)
        return a + f * (c - a);
#else
        Quad r;
        for (int l = 0; l < 4; ++l) r[l] = a[l] + f[l] * (c[l] - a[l]);
        return r;
#endif
    }
    static void blendRow(const float* a, const float* c, float f, float* t, float* g)
    {
        constexpr int mid = kNh / 2;
        static_assert(kNh == 2 * mid + 1 && mid % 4 == 0, "the row: a middle and two halves of whole quads");
        const float h = 0.5f / kInvStep;
        const Quad fq = { f, f, f, f };
        const float tm = a[mid] + f * (c[mid] - a[mid]);
        t[mid] = tm; g[mid] = 0.0f;
        float prev = tm, acc = 0.0f;
        for (int i = mid + 1; i < kNh; i += 4)
        {
            Quad qa, qc, gq;
            std::memcpy(&qa, a + i, sizeof(Quad)); std::memcpy(&qc, c + i, sizeof(Quad));
            const Quad tq = lerp4(qa, qc, fq);
            std::memcpy(t + i, &tq, sizeof(Quad));
            for (int l = 0; l < 4; ++l) { acc += h * (tq[l] + prev); gq[l] = acc; prev = tq[l]; }
            std::memcpy(g + i, &gq, sizeof(Quad));
        }
        prev = tm; acc = 0.0f;
        for (int i = mid - 4; i >= 0; i -= 4)
        {
            Quad qa, qc, gq;
            std::memcpy(&qa, a + i, sizeof(Quad)); std::memcpy(&qc, c + i, sizeof(Quad));
            const Quad tq = lerp4(qa, qc, fq);
            std::memcpy(t + i, &tq, sizeof(Quad));
            for (int l = 3; l >= 0; --l) { acc -= h * (tq[l] + prev); gq[l] = acc; prev = tq[l]; }
            std::memcpy(g + i, &gq, sizeof(Quad));
        }
    }

    // A table read at A = a and row position fu (from 0; Lite: 0): Catmull-Rom along A, linear across u
    inline float look(const float* tv, float a, float fu) const
    {
        float fa = (a - kH0) * kInvStep;
        fa = fa < 1.0f ? 1.0f : fa;
        fa = fa > (float)(kNh - 3) - 0.001f ? (float)(kNh - 3) - 0.001f : fa;
        const int ia = (int)fa;
        const float ta = fa - (float)ia;
        int iu = 0;
        if (slope) { iu = (int)fu; fu -= (float)iu; }
        const float* r = tv + iu * kNh + ia;
        const auto row = [ta](const float* q) {
            const float p0 = q[-1], p1 = q[0], p2 = q[1], p3 = q[2];
            return p1 + 0.5f * ta * (p2 - p0 + ta * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + ta * (3.0f * (p1 - p2) + p3 - p0)));
        };
        const float v0 = row(r);
        if (!slope) return v0;
        return v0 + fu * (row(r + kNh) - v0);
    }
    // the integral along A, continued past the table's ends with the end value's slope
    inline float gInt(const float* gi, const float* tv, float a, float fu) const
    {
        constexpr float lo = kH0 + 2.0f / kInvStep, hi = -kH0 - 3.0f / kInvStep;
        if (a >= lo && a <= hi) return look(gi, a, fu);
        const float ac = a < lo ? lo : hi;
        return look(gi, ac, fu) + look(tv, ac, fu) * (a - ac);
    }

    // Two samples at twice the rate (ring indices w0 - 1 and w0, the second the newest) to one at the
    // session rate, in field units; hs: the record field at the session rate, for the correction.
    __attribute__((always_inline)) Stereo step(const Stereo* ring, int w0, Stereo hs)
    {
        Stereo y = splat(0.0f);
        fp = (fp - 1) & (kFir - 1);
        for (int j = 0; j < kJ; ++j)
        {
            Stereo m[2];
            for (int o = 0; o < 2; ++o)
            {
                const int b = w0 - 1 + o - lat + io[j];
                const Stereo y0 = ring[(b - 1) & (kRing - 1)], y1 = ring[b & (kRing - 1)], y2 = ring[(b + 1) & (kRing - 1)], y3 = ring[(b + 2) & (kRing - 1)];
                const Stereo A = cv[j][0] * y0 + cv[j][1] * y1 + cv[j][2] * y2 + cv[j][3] * y3;
                const Stereo pa = ap[j];
                ap[j] = A;
                const Stereo d = A - pa;
                Stereo r;
                if (!slope)
                {
                    // no slope axis: the last sample's integral is still valid, one lookup per lane
                    const Stereo g = Stereo{ gInt(Gi[j], T[j], A[0], 0.0f), gInt(Gi[j], T[j], A[1], 0.0f) }, g0 = gp[j];
                    gp[j] = g;
                    for (int k = 0; k < 2; ++k)
                        r[k] = std::fabs(d[k]) > 0.005f ? (g[k] - g0[k]) / d[k] : look(T[j], 0.5f * (A[k] + pa[k]), 0.0f);
                }
                else
                {
                    const Stereo s = (cs[j][0] * y0 + cs[j][1] * y1 + cs[j][2] * y2 + cs[j][3] * y3) * tauS;
                    const Stereo u = s / (kSc + vabs(s));
                    Stereo fu = (float)kUMid + u * uScale;
                    fu = vmax(vmin(fu, splat((float)(kNu - 1) - 0.001f)), splat(0.0f));
                    for (int k = 0; k < 2; ++k)
                        r[k] = std::fabs(d[k]) > 0.005f ? (gInt(Gi[j], T[j], A[k], fu[k]) - gInt(Gi[j], T[j], pa[k], fu[k])) / d[k]
                                                        : look(T[j], 0.5f * (A[k] + pa[k]), fu[k]);
                }
                m[o] = r;
            }
            const Stereo v = dn[j].down(m[0], m[1]);
            fh[j][fp] = v; fh[j][fp + kFir] = v;
            const Stereo* hh = &fh[j][fp];
            Quad acc = {};
            for (int k = 0; k < kFir / 2; ++k)
            {
                Quad q;
                std::memcpy(&q, hh + 2 * k, sizeof(q));
                acc += fir[j][k] * q;
            }
            y += lo2(acc) + hi2(acc);
        }
        // the high-frequency self-erasure: the record field's slope (field per micrometer of tape) through
        // a peak follower; above the knee e0 the band above fc is turned down by 1 / (1 + c (env - e0)).
        // The side chain is the record field, so there is no feedback.
        const Stereo sg = vabs(hs - hPrev) * kSig;
        hPrev = hs;
        for (int k = 0; k < 2; ++k) env[k] += (sg[k] > env[k] ? aAtt : aRel) * (sg[k] - env[k]);
        lpY += lpA * (y - lpY);
        const Stereo over = vmax(env - e0, splat(0.0f));
        return lpY + (y - lpY) / (1.0f + cc * over);
    }
};

// What the plugin keeps of the recorder: the ring, the two rate changers, two engines (one heard, the
// other coming in while the engine or the speed changes) and how far each crossfade is.
struct Stage
{
    Stereo ring[kRing] = {};
    int    w = 0;                 // index of the newest sample
    HalfBand up, outUp;           // HQ off: the record field to twice the rate; HQ on: the result back up
    Engine eng[2];
    int    cur = 0;               // the engine heard
    bool   run[2] = {false, false};
    int    warm[2] = {0, 0};      // control ticks it still runs unheard (from rest)
    float  xe = 0.0f, xeInc = 0.0f;   // share of the other engine (eng[1 - cur]) while it crossfades in
    float  f = 0.0f, fInc = 0.0f;     // share of the recorder against TapeSat's curve (Hysteresis on/off)
    float  fTo = 0.0f;                // where f is going
    int    tick[2] = {0, 0};      // control ticks since the engine's tables were last brought to the bias
    float  bpNow = 100.0f, bpTo = 100.0f, bpStep = 0.1f;   // the bias the tables follow, its target, its fastest move per tick
    int    want = 0, wantEp = 0;  // the engine asked for (0 none, 1 Lite, 2 Full) and the speed
    bool   feeding = false;       // the plugin passes it the record signal (some engine runs)
    // TapeSat's playback-side values as the recorder's output meets them (see VHSVibes::hyPlay): a change
    // of Rec Level or speed reaches them as late as the recorder is (lag: samples of the stage still to go)
    float  pA1 = 0.0f, pB1 = 0.0f, pInvB0 = 1.0f, pAEdge = 1.0f, pMakeup = 1.0f;
    float  nA1 = 0.0f, nB1 = 0.0f, nInvB0 = 1.0f, nAEdge = 1.0f, nMakeup = 1.0f;
    int    lag = 0;
    // the dry copy for Mix, as late as the recorder while it is heard (see VHSVibes::hyDry): a delay line
    // of whole samples and, with HQ off, a half sample more (a first-order allpass)
    Stereo dl[128] = {};
    int    dw = 0;
    Stereo thX = {}, thY = {};
    // how late the recorder makes the sound at this rate (session samples), and so the dry copy's delay
    // (whole samples, and whether a half more) and the playback side's lag (the stage's own samples)
    float  late = kLatency;
    int    dInt = 41, lagN = 42;
    bool   dHalf = true;
    int    due = -1;              // an engine whose tables were made this tick: it runs from the next (hyTick)

    void clear()
    {
        for (Stereo& v : ring) v = splat(0.0f);
        w = 0;
        up.reset(); outUp.reset();
    }
    // HQ off: one record field sample (session rate) in, the recorder's output (field units) out
    __attribute__((always_inline)) Stereo run1(Stereo hs)
    {
        Stereo a, b;
        up.up(hs, a, b);
        ring[(w + 1) & (kRing - 1)] = a;
        ring[(w + 2) & (kRing - 1)] = b;
        w += 2;
        return mix(hs);
    }
    // HQ on: two samples at twice the rate in (a the earlier), two out
    __attribute__((always_inline)) void run2(Stereo ha, Stereo hb, Stereo& ya, Stereo& yb)
    {
        ring[(w + 1) & (kRing - 1)] = ha;
        ring[(w + 2) & (kRing - 1)] = hb;
        w += 2;
        outUp.up(mix(hb), ya, yb);
    }
    __attribute__((always_inline)) Stereo mix(Stereo hs)
    {
        const int o = 1 - cur;
        Stereo y = run[cur] ? eng[cur].step(ring, w, hs) : splat(0.0f);
        if (__builtin_expect(run[o], 0))
        {
            const Stereo z = eng[o].step(ring, w, hs);
            if (warm[o] == 0) { xe = minf(xe + xeInc, 1.0f); y += (z - y) * xe; }
        }
        return y;
    }
};

} // namespace hys
