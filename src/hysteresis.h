// VHS Vibes 1.8: the tape hysteresis recorder (Sat Type VHS, Hysteresis On).
//
// SPDX-License-Identifier: MIT
//
// Included by VHSVibes.cpp inside its namespace, after HalfBand (it uses Stereo, Quad and HalfBand from
// there). The tables come from src/hysteresis_tables.h, made by tools/hysteresis/r3.cpp (mode "plugin")
// from a carrier simulation of the linear track: 12 layers of the coating, AC bias at 62.5 kHz, a
// Preisach model of the particles. What a group of layers keeps (its remanence) is tabulated against
// the record field A where the element leaves the gap. (Up to the three-position switch this file also had
// a Full engine with the slope of the field as a second axis; only the engine called Lite there is left,
// and the tables keep only the rows it reads: u = 0.)
//
// Per channel (left and right are the two lanes of one Stereo):
//
//   Hs = kFieldPerUnit * w            w: the pre-emphasized record signal (TapeSat's), Rec Level applied
//   -> twice the rate (HalfBand)      HQ on: the signal is at twice the rate already
//   -> ring of the last kRing samples
//   -> per group g = 0..2, per sample at twice the rate, at the element time e = newest - kLat:
//        A = cubic (Lagrange) value of Hs at e + d0_g
//        M_g = the mean of T_g between the last A and this one (first-order antiderivative
//              anti-aliasing: (Gi(A) - Gi(A_prev)) / (A - A_prev), Gi the integral of T along A)
//   -> per group: back to the session rate (HalfBand) -> 64-tap playback FIR -> sum of the groups
//   -> high-frequency self-erasure correction (HfCorr, at the session rate)
//   -> / kFieldPerUnit                (then TapeSat's de-emphasis, band edge and make-up, in the plugin)
//
// The latency is kLatency at both speeds. The Bias knob picks two neighboring bias planes and their
// blend; the working tables (T, Gi, the group delays and the interpolation taps) are worked out again
// every 32 samples while the bias moves (setBias). Nothing here allocates, locks or does I/O.
#pragma once

namespace hys {

using namespace hysttab;
constexpr int   kRing = 128;                 // record field history at twice the rate (the reads reach lat + 3 back)
constexpr float kLatency = 41.5f;            // samples at the session rate, both speeds (by design)
constexpr float kInvField = 1.0f / kFieldPerUnit;
// The high-frequency correction, fitted to the carrier simulation: [speed] = {fc Hz, e0, c}
constexpr float kCorr[2][3] = { {700.0f, 14.0f, 0.08f}, {1000.0f, 4.0f, 0.02f} };

// Bias as a percentage of correct bias from the plugin's bias value (-1 too little .. 0 right .. +1 too
// much, TapeSat::set's): 25 % at the bottom, 100 % at "right", 150 % at the top, in a straight line on
// either side.
inline float biasPercent(float bias) { return bias < 0.0f ? 100.0f + 75.0f * bias : 100.0f + 50.0f * bias; }

// One engine at one tape speed, with its own working tables. (lite) When the speed changes while it is
// heard, its tables and filters glide from the one speed's to the other's (es), in about kHyFadeSec.
struct Engine
{
    int   ep = 0;                             // EP or SP (where es goes)
    float es = 0.0f;                          // the speed its tables and filters are made for: 0 SP, 1 EP (between: gliding)
    float bp = -1.0f;                         // the bias (%) its working tables hold; -1: none yet
    float fs2 = 88200.0f;                     // twice the session rate
    float sr = 0.0f;                          // the session rate it was configured for (0: not yet)
    int   lat = kLat;                         // the element lag, samples at twice the rate (see configure)
    int   io[kJ] = {};                        // where each group reads the ring, and its taps
    float cv[kJ][4] = {};
    Stereo ap[kJ] = {}, gp[kJ] = {};          // last A, and the integral there
    HalfBand dn[kJ];
    Quad  fir[kJ][kFir / 2] = {};             // the playback FIRs, tap pairs {f0, f0, f1, f1}
    Stereo fh[kJ][2 * kFir] = {};             // their histories, twice over (no wrap in the sum)
    int   fp = 0;
    // high-frequency correction
    float kSig = 1.0f, e0 = 0.0f, cc = 0.0f, aAtt = 0.0f, aRel = 0.0f, lpA = 0.0f;
    Stereo hPrev = {}, env = {}, lpY = {};
    // working tables: T along A and its integral along A from A = 0
    float T[kJ][kNh] = {}, Gi[kJ][kNh] = {};
    // (lite) the integral and T at the two ends where gInt continues them (gInt's own reads, made with the tables)
    float gLo[kJ] = {}, tLo[kJ] = {}, gHi[kJ] = {}, tHi[kJ] = {};
    // (lite) for the speed glide: both speeds' tables at bias gb (setGlide)
    float Ts[2][kJ][kNh] = {}, Gs[2][kJ][kNh] = {};
    float gb = -1.0f;

    // sr: the session rate
    void configure(int speedEp, float sr)
    {
        ep = speedEp; es = (float)speedEp; this->sr = sr;
        fs2 = 2.0f * sr;
        // kLat at 44.1 kHz; at a higher rate the groups' delays (in seconds) are more samples, and the lag
        // grows with them so that no read reaches past the newest sample (lagOf)
        lat = lagOf(sr);
        aAtt = 1.0f - std::exp(-1.0f / (0.0005f * sr));
        aRel = 1.0f - std::exp(-1.0f / (0.030f * sr));
        setSpeed();
        bp = -1.0f;
        reset();
    }
    bool gliding() const { return es != (float)ep; }
    // (lite) one step of the glide towards ep, and the filters for it (the caller then makes the tables: setBias)
    void glideStep(float d)
    {
        es = ep ? (es + d < 1.0f ? es + d : 1.0f) : (es - d > 0.0f ? es - d : 0.0f);
        setSpeed();
    }
    // the playback FIRs and the correction at speed es: the speed's own at 0 (SP) and 1 (EP), between them a
    // blend of the two (while the speed glides)
    void setSpeed()
    {
        if (es == 0.0f || es == 1.0f)
        {
            const int e = (int)es;
            const float (*f)[kFir] = e ? kFirFastEp : kFirFastSp;
            for (int j = 0; j < kJ; ++j)
                for (int k = 0; k < kFir / 2; ++k) fir[j][k] = Quad{ f[j][2 * k], f[j][2 * k], f[j][2 * k + 1], f[j][2 * k + 1] };
            const float* p = kCorr[e];
            kSig = sr / kSpeed[e];
            e0 = p[1]; cc = p[2];
            lpA = 1.0f - std::exp(-kTwoPi * p[0] / sr);
            return;
        }
        const float a = 1.0f - es;
        for (int j = 0; j < kJ; ++j)
            for (int k = 0; k < kFir / 2; ++k)
            {
                const float f0 = a * kFirFastSp[j][2 * k] + es * kFirFastEp[j][2 * k], f1 = a * kFirFastSp[j][2 * k + 1] + es * kFirFastEp[j][2 * k + 1];
                fir[j][k] = Quad{ f0, f0, f1, f1 };
            }
        kSig = a * (sr / kSpeed[0]) + es * (sr / kSpeed[1]);
        e0 = a * kCorr[0][1] + es * kCorr[1][1]; cc = a * kCorr[0][2] + es * kCorr[1][2];
        lpA = 1.0f - std::exp(-kTwoPi * (a * kCorr[0][0] + es * kCorr[1][0]) / sr);
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
    // trapezoid rule from A = 0, the groups' delays blended and their taps. The reused integral of the
    // last A is kept valid (otherwise the next sample would click). While the speed glides: the same for
    // a blend of the two speeds' (setGlide).
    void setBias(float b)
    {
        bp = b;
        b = clamp(b, kBiasPct[0], kBiasPct[kNb - 1]);
        int k = 0;
        while (k + 2 < kNb && kBiasPct[k + 1] <= b) ++k;
        const float f = clamp((b - kBiasPct[k]) / (kBiasPct[k + 1] - kBiasPct[k]), 0.0f, 1.0f);
        if (__builtin_expect(gliding() && es != 0.0f && es != 1.0f, 0)) { setGlide(k, f, b); return; }
        const int e = (int)es;
        const float (*P)[kJ][kNh] = e ? kTEp : kTSp;
        const float (*D)[kJ] = e ? kD0Ep : kD0Sp;
        for (int j = 0; j < kJ; ++j)
        {
            blendRow(P[k][j], P[k + 1][j], f, T[j], Gi[j]);
            taps(j, (1.0 - f) * D[k][j] + (double)f * D[k + 1][j]);
        }
    }
    // the group's delay d (seconds) and its Lagrange taps for the samples -1 .. 2 around it, in double:
    // worked out in float they differed from the prototype's by a rounding, and where A moves by just over
    // the difference quotient's threshold (0.005) that showed at -74 dB on single samples
    void taps(int j, double d)
    {
        const double p = d * (double)fs2, fl = std::floor(p), tt = p - fl;
        io[j] = (int)fl;
        cv[j][0] = (float)(-tt * (tt - 1) * (tt - 2) / 6);
        cv[j][1] = (float)((tt + 1) * (tt - 1) * (tt - 2) / 2);
        cv[j][2] = (float)(-(tt + 1) * tt * (tt - 2) / 2);
        cv[j][3] = (float)((tt + 1) * tt * (tt - 1) / 6);
        gp[j] = Stereo{ gInt(Gi[j], T[j], ap[j][0]), gInt(Gi[j], T[j], ap[j][1]) };
        constexpr float lo = kH0 + 2.0f / kInvStep, hi = -kH0 - 3.0f / kInvStep;
        gLo[j] = look(Gi[j], lo); tLo[j] = look(T[j], lo); gHi[j] = look(Gi[j], hi); tHi[j] = look(T[j], hi);
    }
    // (lite) setBias while the speed glides: each speed's own tables at this bias (made as setBias makes them,
    // once for the bias: gb), blended along the speed (es), four at a time; the delays as setBias has them.
    // (The integral of the blend is the blend of the integrals.)
    void setGlide(int k, float f, float b)
    {
        if (gb != b)
        {
            for (int j = 0; j < kJ; ++j)
            {
                blendRow(kTSp[k][j], kTSp[k + 1][j], f, Ts[0][j], Gs[0][j]);
                blendRow(kTEp[k][j], kTEp[k + 1][j], f, Ts[1][j], Gs[1][j]);
            }
            gb = b;
        }
        const Quad eq = vsplat<Quad>(es);
        for (int j = 0; j < kJ; ++j)
        {
            const float *t0 = Ts[0][j], *t1 = Ts[1][j], *g0 = Gs[0][j], *g1 = Gs[1][j];
            float *t = T[j], *g = Gi[j];
            int i = 0;
            for (; i + 4 <= kNh; i += 4)
            {
                Quad a, c, ga, gc;
                std::memcpy(&a, t0 + i, sizeof(Quad)); std::memcpy(&c, t1 + i, sizeof(Quad));
                std::memcpy(&ga, g0 + i, sizeof(Quad)); std::memcpy(&gc, g1 + i, sizeof(Quad));
                const Quad tq = a + eq * (c - a), gq = ga + eq * (gc - ga);
                std::memcpy(t + i, &tq, sizeof(Quad)); std::memcpy(g + i, &gq, sizeof(Quad));
            }
            for (; i < kNh; ++i) { t[i] = t0[i] + es * (t1[i] - t0[i]); g[i] = g0[i] + es * (g1[i] - g0[i]); }
            const double ds = (1.0 - f) * kD0Sp[k][j] + (double)f * kD0Sp[k + 1][j], de = (1.0 - f) * kD0Ep[k][j] + (double)f * kD0Ep[k + 1][j];
            taps(j, (1.0 - es) * ds + (double)es * de);
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

    // A table read at A = a: Catmull-Rom along A
    static inline float look(const float* tv, float a)
    {
        float fa = (a - kH0) * kInvStep;
        fa = fa < 1.0f ? 1.0f : fa;
        fa = fa > (float)(kNh - 3) - 0.001f ? (float)(kNh - 3) - 0.001f : fa;
        const int ia = (int)fa;
        const float ta = fa - (float)ia;
        const float* q = tv + ia;
        const float p0 = q[-1], p1 = q[0], p2 = q[1], p3 = q[2];
        return p1 + 0.5f * ta * (p2 - p0 + ta * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + ta * (3.0f * (p1 - p2) + p3 - p0)));
    }
    // the integral along A, continued past the table's ends with the end value's slope
    static inline float gInt(const float* gi, const float* tv, float a)
    {
        constexpr float lo = kH0 + 2.0f / kInvStep, hi = -kH0 - 3.0f / kInvStep;
        if (a >= lo && a <= hi) return look(gi, a);
        const float ac = a < lo ? lo : hi;
        return look(gi, ac) + look(tv, ac) * (a - ac);
    }
    // (lite) look() for four values at once (lanes: step's two samples, left and right), value for value
    // what look() gives each lane. a * b + c as the compiler makes look()'s scalar expression on this target:
    // the C++ front end marks every "a * b + c" of the source (and "a * b - c": the product's partner
    // negated) as fusable, and where the hardware has a fused multiply-add the scalar one is fused; on
    // 32-bit ARM the vector one is not (see lerp4), so it is asked to be, at the same places.
    // clamp: the fallback read (clamped at both ends, as look()) or the integral's (the caller has checked the
    // range, and there the clamps would leave every lane as it is).
    // anyOut: any lane outside lo .. hi (or a NaN)
    static inline bool anyOut(Quad a, float lo, float hi)
    {
        typedef int32_t QuadI __attribute__((vector_size(16)));
        const QuadI in = (a >= vsplat<Quad>(lo)) & (a <= vsplat<Quad>(hi));
        return (in[0] & in[1] & in[2] & in[3]) == 0;
    }
    static inline Quad vabs4(Quad a) { return __builtin_elementwise_abs(a); }
    static inline Quad fmad(Quad a, Quad b, Quad c)
    {
#if defined(__clang__) && (defined(__ARM_FEATURE_FMA) || defined(__FMA__))
        return __builtin_elementwise_fma(a, b, c);
#else
        return a * b + c;
#endif
    }
    template <bool clamp> static inline Quad look4(const float* tv, Quad a)
    {
        typedef int32_t QuadI __attribute__((vector_size(16)));
        Quad fa = (a - kH0) * kInvStep;
        if constexpr (clamp)
        {
            const Quad lo = { 1.0f, 1.0f, 1.0f, 1.0f }, hi = vsplat<Quad>((float)(kNh - 3) - 0.001f);
            fa = fa < lo ? lo : fa;
            fa = fa > hi ? hi : fa;
        }
        const QuadI ia = __builtin_convertvector(fa, QuadI);
        const Quad ta = fa - __builtin_convertvector(ia, Quad);
        Quad r0, r1, r2, r3;   // p0 .. p3 of each lane
        std::memcpy(&r0, tv + ia[0] - 1, sizeof(Quad)); std::memcpy(&r1, tv + ia[1] - 1, sizeof(Quad));
        std::memcpy(&r2, tv + ia[2] - 1, sizeof(Quad)); std::memcpy(&r3, tv + ia[3] - 1, sizeof(Quad));
        const Quad t0 = __builtin_shufflevector(r0, r1, 0, 4, 1, 5), t1 = __builtin_shufflevector(r2, r3, 0, 4, 1, 5);
        const Quad t2 = __builtin_shufflevector(r0, r1, 2, 6, 3, 7), t3 = __builtin_shufflevector(r2, r3, 2, 6, 3, 7);
        const Quad p0 = __builtin_shufflevector(t0, t1, 0, 1, 4, 5), p1 = __builtin_shufflevector(t0, t1, 2, 3, 6, 7);
        const Quad p2 = __builtin_shufflevector(t2, t3, 0, 1, 4, 5), p3 = __builtin_shufflevector(t2, t3, 2, 3, 6, 7);
        // p1 + 0.5 ta (p2 - p0 + ta (2 p0 - 5 p1 + 4 p2 - p3 + ta (3 (p1 - p2) + p3 - p0))), as look() has it
        const Quad z = fmad(vsplat<Quad>(3.0f), p1 - p2, p3) - p0;
        const Quad u = fmad(vsplat<Quad>(4.0f), p2, fmad(vsplat<Quad>(2.0f), p0, -(5.0f * p1))) - p3;
        const Quad y = fmad(ta, z, u);
        const Quad x = fmad(ta, y, p2 - p0);
        return fmad(0.5f * ta, x, p1);
    }

    // Two samples at twice the rate (ring indices w0 - 1 and w0, the second the newest) to one at the
    // session rate, in field units; hs: the record field at the session rate, for the correction.
    __attribute__((always_inline)) Stereo step(const Stereo* ring, int w0, Stereo hs)
    {
        Stereo y = splat(0.0f);
        fp = (fp - 1) & (kFir - 1);
        for (int j = 0; j < kJ; ++j)
        {
            // (lite) both samples at twice the rate at once, as four lanes (the first sample's left and right,
            // then the second's): every lane does what the two-lane code did for its sample, in the same
            // order, so every value is the same to the bit
            const int b = w0 - 1 - lat + io[j];
            const Stereo y0 = ring[(b - 1) & (kRing - 1)], y1 = ring[b & (kRing - 1)], y2 = ring[(b + 1) & (kRing - 1)], y3 = ring[(b + 2) & (kRing - 1)], y4 = ring[(b + 3) & (kRing - 1)];
            const Quad A = cv[j][0] * quad(y0, y1) + cv[j][1] * quad(y1, y2) + cv[j][2] * quad(y2, y3) + cv[j][3] * quad(y3, y4);
            const Quad pa = quad(ap[j], lo2(A));
            ap[j] = hi2(A);
            const Quad d = A - pa;
            // the last sample's integral is still valid: one lookup per lane; past the table's ends, as gInt
            // continues it (the end's value and slope: a lane inside keeps its own read, a lane outside takes
            // the continuation, each what gInt gives it)
            constexpr float lo = kH0 + 2.0f / kInvStep, hi = -kH0 - 3.0f / kInvStep;
            Quad g;
            if (__builtin_expect(anyOut(A, lo, hi), 0))
            {
                // the ends' reads are the tables' own (gLo .. tHi): the continuation is one multiply-add a lane
                const Quad vlo = vsplat<Quad>(lo), vhi = vsplat<Quad>(hi);
                const auto in = A >= vlo && A <= vhi, below = A < vlo;
                const Quad ac = below ? vlo : (in ? A : vhi);
                const Quad ge = below ? vsplat<Quad>(gLo[j]) : vsplat<Quad>(gHi[j]), te = below ? vsplat<Quad>(tLo[j]) : vsplat<Quad>(tHi[j]);
                g = in ? look4<false>(Gi[j], ac) : fmad(te, A - ac, ge);
            }
            else g = look4<false>(Gi[j], A);
            const Quad g0 = quad(gp[j], lo2(g));
            gp[j] = hi2(g);
            // the difference quotient, or where A has hardly moved, T at the middle (read only when a lane needs it)
            const auto moved = vabs4(d) > vsplat<Quad>(0.005f);
            const Quad q = (g - g0) / d;
            Quad r = q;
            if (__builtin_expect(!(moved[0] & moved[1] & moved[2] & moved[3]), 0)) r = moved ? q : look4<true>(T[j], 0.5f * (A + pa));
            const Stereo v = dn[j].down(lo2(r), hi2(r));
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

// What the plugin keeps of the recorder: the ring, the two rate changers, the engine and how far the
// crossfade against TapeSat's curve is. (One engine: a change of speed goes out to the curve and in again,
// see VHSVibes::hyTick, so that no block ever pays for two.)
struct Stage
{
    Stereo ring[kRing] = {};
    int    w = 0;                 // index of the newest sample
    HalfBand up, outUp;           // HQ off: the record field to twice the rate; HQ on: the result back up
    Engine eng;
    bool   run = false;           // the engine runs
    int    warm = 0;              // control ticks it still runs unheard (from rest)
    float  f = 0.0f, fInc = 0.0f;     // share of the recorder against TapeSat's curve (Hysteresis on/off)
    float  fTo = 0.0f;                // where f is going
    int    tick = 0;              // control ticks since the engine's tables were last brought to the bias
    float  bpNow = 100.0f, bpTo = 100.0f, bpStep = 0.1f;   // the bias the tables follow, its target, its fastest move per tick
    int    wantEp = 0;            // the speed asked for
    bool   feeding = false;       // the plugin passes it the record signal (the engine runs)
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
    __attribute__((always_inline)) Stereo mix(Stereo hs) { return run ? eng.step(ring, w, hs) : splat(0.0f); }
};

} // namespace hys
