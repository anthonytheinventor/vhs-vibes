// VHS Vibes 1.8: the tape hysteresis recorder (Sat Type VHS, Hysteresis On).
//
// SPDX-License-Identifier: MIT
//
// Included by VHSVibes.cpp inside its namespace, after HalfBand (it uses Stereo, Quad and HalfBand from
// there). The tables come from src/hysteresis_tables.h, made by tools/hysteresis/r3.cpp (mode "plugin")
// from a carrier simulation of the linear track: 12 layers of the coating, AC bias at 62.5 kHz, a
// Preisach model of the particles. What a group of layers keeps (its remanence) is tabulated against
// the record field A where the element leaves the gap, from A = -12 to +12 (in units of the coercivity;
// up to 1.8's review the planes stopped at 6, where the deepest group is far from saturated at low bias:
// past the end the engine held the end value, a hard knee in the treble). (Up to the three-position switch
// this file also had a Full engine with the slope of the field as a second axis; only the engine called
// Lite there is left, and the tables keep only the rows it reads: u = 0.)
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
// The latency is kLatency at both speeds. The Bias knob (25 to 150 %, the planes' range) picks two
// neighboring bias planes and their blend; the working tables (T, Gi, the group delays and the interpolation taps) are worked out again
// every 32 samples while the bias moves (setBias). Nothing here allocates, locks or does I/O.
#pragma once
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace hys {

using namespace hysttab;
constexpr int   kRing = 128;                 // record field history at twice the rate (the reads reach lat + 3 back)
constexpr float kLatency = 41.5f;            // samples at the session rate, both speeds (by design)
constexpr float kInvField = 1.0f / kFieldPerUnit;
// The reads' arithmetic: the place fa = (A - kA0) kInvStep, its whole part the table index less kOff. kA0 is
// where the planes started before they reached 12: every read inside the old range is worked out as it was
// (the same fa, the same rounding: kept finer than a place counted from -12, which loses a bit of fa past
// A = 0.8, and with it of the integral's reads where the step is small).
constexpr float kA0 = -6.0f;
constexpr int   kOff = (int)((kA0 - kH0) * kInvStep + 0.5f);    // A = kA0's place in the planes (120)
constexpr float kFaMid = -kA0 * kInvStep;                          // fa at A = 0
// The integral's table range, the same on both sides (odd, as the planes): out to the last A whose four
// Catmull-Rom points are all in the planes (at the top), mirrored. Past it, gInt continues the integral
// with the end's value and slope (by then every group is saturated: within 3e-5 of 1 at 25 % bias).
constexpr float kAEdge = kA0 + (float)(kNh - 3 - kOff) / kInvStep;   // 11.9
static_assert(kOff >= 3 && kNh - 3 - kOff > 0, "the planes reach past kA0 on both sides");
// the fallback read's clamp, likewise odd: fa from kFaLo to kFaHi
constexpr float kFaHi = (float)(kNh - 3 - kOff) - 0.001f, kFaLo = 2.0f * kFaMid - kFaHi;
// The difference quotient (G(A) - G(A_prev)) / (A - A_prev) loses digits as A barely moves: the integral G
// grows with |A| (to about 12), so its float rounding, divided by a small step, grows as the step shrinks.
// Below a threshold the engine reads T at the middle of the step instead. The threshold grows with |A|:
// kQ0 + kQk |A| (where it matters the step is small, so |A_prev| is about |A|); near A = 0 it is the old
// fixed 0.005. Measured offline against the quotient worked out in double over every plane and step (1.8
// review): the float quotient's worst error where it is used fell from 6.8e-4 of full scale (the fixed
// 0.005) to 1.1e-4. (T at the middle and the quotient's limit differ by up to 1e-3 where T bends most, as
// they did before: the slope of G's own curve there would agree with the quotient, but it bends in corners
// and put a fifth harmonic on quiet tones at low bias, -28 dBc where the simulation has -43.)
constexpr float kQ0 = 0.005f, kQk = 0.004f;
// 1 / x four at a time without a divide (32-bit ARM has none in its vector unit, so x / y was four scalar
// divides): the estimate and two Newton steps, within a few units in the last place; elsewhere a divide.
static inline Quad rcp4(Quad x)
{
#if defined(__ARM_NEON)
    const float32x4_t v = (float32x4_t)x;
    float32x4_t r = vrecpeq_f32(v);
    r = vmulq_f32(r, vrecpsq_f32(v, r));
    r = vmulq_f32(r, vrecpsq_f32(v, r));
    return (Quad)r;
#else
    return 1.0f / x;
#endif
}
static inline Stereo rcp2(Stereo x)
{
#if defined(__ARM_NEON)
    const float32x2_t v = (float32x2_t)x;
    float32x2_t r = vrecpe_f32(v);
    r = vmul_f32(r, vrecps_f32(v, r));
    r = vmul_f32(r, vrecps_f32(v, r));
    return (Stereo)r;
#else
    return 1.0f / x;
#endif
}
// The high-frequency correction, fitted to the carrier simulation: [speed] = {fc Hz, e0, c}
constexpr float kCorr[2][3] = { {700.0f, 14.0f, 0.08f}, {1000.0f, 4.0f, 0.02f} };

// Each plane's integral along A from A = 0 (trapezoid rule, in double), made once, the first time a recorder
// is made (not on the audio thread). The working integral for a bias is then the blend of two of these, as T is
// the blend of two planes (the integral of a blend is the blend of the integrals): a bias update blends four
// rows at a time instead of summing along the row one point after another. (In float, the running sum the
// working tables used to be made with differed from these by a rounding.)
struct Integrals
{
    float g[2][kNb][kJ][kNh];   // [speed][bias plane][group][A]
    Integrals()
    {
        constexpr int mid = kNh / 2;
        for (int e = 0; e < 2; ++e)
            for (int b = 0; b < kNb; ++b)
                for (int j = 0; j < kJ; ++j)
                {
                    const float* t = (e ? kTEp : kTSp)[b][j];
                    float* o = g[e][b][j];
                    const double h = 0.5 / (double)kInvStep;
                    double acc = 0.0;
                    o[mid] = 0.0f;
                    for (int i = mid + 1; i < kNh; ++i) { acc += h * ((double)t[i] + t[i - 1]); o[i] = (float)acc; }
                    acc = 0.0;
                    for (int i = mid - 1; i >= 0; --i) { acc -= h * ((double)t[i] + t[i + 1]); o[i] = (float)acc; }
                }
    }
};
inline const Integrals& integrals() { static const Integrals I; return I; }

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
    // working table: the integral of T along A from A = 0. (1.8 review) T itself is read from the planes the
    // bias (and, while the speed glides, the speed) sits between, where it is needed (the fallback of a step
    // that barely moved, and the ends of the integral): a bias update blends only the integral rows.
    float Gi[kJ][kNh] = {};
    const float* tp[kJ][4] = {};              // the planes: [SP or the speed's] k, k + 1, and while gliding EP's k, k + 1
    float tf = 0.0f, te = 0.0f;               // the blend along the bias, and along the speed (gliding: tn 4)
    int   tn = 2;
    // (lite) the integral and T at the two ends where gInt continues them (gInt's own reads, made with the tables)
    float gLo[kJ] = {}, tLo[kJ] = {}, gHi[kJ] = {}, tHi[kJ] = {};

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
        const float (*G)[kJ][kNh] = integrals().g[e];
        const float (*D)[kJ] = e ? kD0Ep : kD0Sp;
        tn = 2; tf = f;
        for (int j = 0; j < kJ; ++j)
        {
            blendG(G[k][j], G[k + 1][j], f, Gi[j]);
            tp[j][0] = P[k][j]; tp[j][1] = P[k + 1][j];
            taps(j, (1.0 - f) * D[k][j] + (double)f * D[k + 1][j]);
        }
    }
    // T at a (Catmull-Rom) from the planes: along the bias, and while gliding along the speed
    float tRead(int j, float a) const
    {
        const float x = look(tp[j][0], a), y = x + tf * (look(tp[j][1], a) - x);
        if (tn == 2) return y;
        const float u = look(tp[j][2], a), z = u + tf * (look(tp[j][3], a) - u);
        return y + te * (z - y);
    }
    Quad tRead4(int j, Quad a) const
    {
        const Quad x = look4<true>(tp[j][0], a), y = x + tf * (look4<true>(tp[j][1], a) - x);
        if (tn == 2) return y;
        const Quad u = look4<true>(tp[j][2], a), z = u + tf * (look4<true>(tp[j][3], a) - u);
        return y + te * (z - y);
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
        gLo[j] = look(Gi[j], -kAEdge); tLo[j] = tRead(j, -kAEdge); gHi[j] = look(Gi[j], kAEdge); tHi[j] = tRead(j, kAEdge);
        gp[j] = Stereo{ gEnds(j, ap[j][0]), gEnds(j, ap[j][1]) };
    }
    // the integral at a, continued past +-kAEdge with the end's value and slope (gInt, with the ends made above)
    float gEnds(int j, float a) const
    {
        if (a >= -kAEdge && a <= kAEdge) return look(Gi[j], a);
        return a < -kAEdge ? gLo[j] + tLo[j] * (a + kAEdge) : gHi[j] + tHi[j] * (a - kAEdge);
    }
    // (lite) setBias while the speed glides: the integral as the blend of the four planes' integrals around the
    // bias at the two speeds, along the bias (f) and then along the speed (es), in one pass; T read likewise; the
    // delays as setBias has them
    void setGlide(int k, float f, float)
    {
        const Integrals& I = integrals();
        const Quad fq = vsplat<Quad>(f), eq = vsplat<Quad>(es);
        tn = 4; tf = f; te = es;
        for (int j = 0; j < kJ; ++j)
        {
            const float *a = I.g[0][k][j], *c = I.g[0][k + 1][j], *a2 = I.g[1][k][j], *c2 = I.g[1][k + 1][j];
            float* g = Gi[j];
            // (set 1) the lower half and the middle (see mirrorRow), then the mirror, then the last point as
            // before (the scalar expression)
            for (int i = 0; i <= kNh / 2; i += 4)
            {
                Quad qa, qc, qa2, qc2;
                std::memcpy(&qa, a + i, sizeof(Quad)); std::memcpy(&qc, c + i, sizeof(Quad));
                std::memcpy(&qa2, a2 + i, sizeof(Quad)); std::memcpy(&qc2, c2 + i, sizeof(Quad));
                const Quad x = qa + fq * (qc - qa), y = qa2 + fq * (qc2 - qa2), gq = x + eq * (y - x);
                std::memcpy(g + i, &gq, sizeof(Quad));
            }
            mirrorRow(g);
            { constexpr int i = kNh - 1; const float x = a[i] + f * (c[i] - a[i]), y = a2[i] + f * (c2[i] - a2[i]); g[i] = x + es * (y - x); }
            tp[j][0] = kTSp[k][j]; tp[j][1] = kTSp[k + 1][j]; tp[j][2] = kTEp[k][j]; tp[j][3] = kTEp[k + 1][j];
            const double ds = (1.0 - f) * kD0Sp[k][j] + (double)f * kD0Sp[k + 1][j], de = (1.0 - f) * kD0Ep[k][j] + (double)f * kD0Ep[k + 1][j];
            taps(j, (1.0 - es) * ds + (double)es * de);
        }
    }
    // the integral row of a bias: the two planes' integrals blended, four at a time (lerp4)
    static void blendG(const float* ga, const float* gc, float f, float* g)
    {
        static_assert(kNh % 4 == 1, "the row: whole quads and one more point");
        const Quad fq = { f, f, f, f };
        for (int i = 0; i <= kNh / 2; i += 4)   // (set 1) points 0 to 243, then the mirror (see mirrorRow)
        {
            Quad qga, qgc;
            std::memcpy(&qga, ga + i, sizeof(Quad)); std::memcpy(&qgc, gc + i, sizeof(Quad));
            const Quad gq = lerp4(qga, qgc, fq);
            std::memcpy(g + i, &gq, sizeof(Quad));
        }
        mirrorRow(g);
        constexpr int l = kNh - 1;
        g[l] = ga[l] + f * (gc[l] - ga[l]);
    }
    // (set 1) Every integral row is exactly even: the planes are exactly odd (T[mid - k] == -T[mid + k]),
    // and Integrals builds both halves outwards from the middle with exactly negated operands, so
    // g[mid - k] == g[mid + k] to the bit (tests check every row). A blend works out each point from that
    // point's values alone, the same operations in every lane, so the blended row is even to the bit as
    // well: blendG and setGlide blend points 0 to 243 (the lower half and the quad over the middle) and
    // this copies 236 down to 1 into 244 up to 479, the values the blend would give there. The last point
    // (480, the scalar expression, which may round differently from a lane) is still blended by itself.
    static void mirrorRow(float* g)
    {
        constexpr int mid = kNh / 2;
        static_assert(kNh == 2 * mid + 1 && mid % 4 == 0, "the middle on a quad's first point");
        for (int i = mid + 4; i + 4 <= kNh - 1; i += 4)
        {
            Quad q;
            std::memcpy(&q, g + (2 * mid - i - 3), sizeof(Quad));   // the points mirroring i .. i + 3, upwards
            q = __builtin_shufflevector(q, q, 3, 2, 1, 0);
            std::memcpy(g + i, &q, sizeof(Quad));
        }
    }
    // a + f (c - a) four at a time (blendG), rounded as the compiler rounds the scalar
    // expression on this target: where the hardware has a fused multiply-add clang fuses the scalar one (one
    // rounding), but on 32-bit ARM not the vector one (NEON's vmul and vadd: two roundings), so it is asked
    // to. Another compiler gets the scalar expression itself, lane by lane.
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

    // A table read at A = a: Catmull-Rom along A (fa clamped to the planes, the same on both sides)
    static inline float look(const float* tv, float a)
    {
        float fa = (a - kA0) * kInvStep;
        fa = fa < kFaLo ? kFaLo : fa;
        fa = fa > kFaHi ? kFaHi : fa;
        int ia = (int)fa;
        if ((float)ia > fa) --ia;   // (rounded down: fa < 0 past kA0)
        const float ta = fa - (float)ia;
        const float* q = tv + kOff + ia;
        const float p0 = q[-1], p1 = q[0], p2 = q[1], p3 = q[2];
        return p1 + 0.5f * ta * (p2 - p0 + ta * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + ta * (3.0f * (p1 - p2) + p3 - p0)));
    }
    // the integral along A, continued past +-kAEdge with the end value's slope
    static inline float gInt(const float* gi, const float* tv, float a)
    {
        if (a >= -kAEdge && a <= kAEdge) return look(gi, a);
        const float ac = a < -kAEdge ? -kAEdge : kAEdge;
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
    // allSet: every lane of a compare's result true (all ones). On the MPC one NEON AND and pairwise minimum and
    // one lane moved out, where taking the four lanes out one by one cost about twice as many instructions.
    typedef int32_t QuadI __attribute__((vector_size(16)));
    static inline bool allSet(QuadI m)
    {
#if defined(__ARM_NEON)
        const uint32x4_t u = (uint32x4_t)m;
        const uint32x2_t h = vand_u32(vget_low_u32(u), vget_high_u32(u));
        return vget_lane_u32(vpmin_u32(h, h), 0) != 0;
#else
        return (m[0] & m[1] & m[2] & m[3]) != 0;
#endif
    }
    static inline bool anyOut(Quad a, float lo, float hi)
    {
        const QuadI in = (a >= vsplat<Quad>(lo)) & (a <= vsplat<Quad>(hi));
        return !allSet(in);
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
        Quad fa = (a - kA0) * kInvStep;
        if constexpr (clamp)
        {
            const Quad lo = vsplat<Quad>(kFaLo), hi = vsplat<Quad>(kFaHi);
            fa = fa < lo ? lo : fa;
            fa = fa > hi ? hi : fa;
        }
        QuadI ia = __builtin_convertvector(fa, QuadI);
        ia += (QuadI)(__builtin_convertvector(ia, Quad) > fa);   // rounded down (a true lane is -1)
        const Quad ta = fa - __builtin_convertvector(ia, Quad);
        tv += kOff - 1;
        Quad r0, r1, r2, r3;   // p0 .. p3 of each lane
        std::memcpy(&r0, tv + ia[0], sizeof(Quad)); std::memcpy(&r1, tv + ia[1], sizeof(Quad));
        std::memcpy(&r2, tv + ia[2], sizeof(Quad)); std::memcpy(&r3, tv + ia[3], sizeof(Quad));
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
    // Out of line: written into the recorder's copies of the sample loop it crowded their registers, and as a
    // call once a sample each copy came out shorter (32-bit ARM build, counted); the arithmetic is the same.
    __attribute__((noinline)) Stereo step(const Stereo* ring, uint32_t w0, Stereo hs)
    {
        Stereo y = splat(0.0f);
        fp = (fp - 1) & (kFir - 1);
        for (int j = 0; j < kJ; ++j)
        {
            // (lite) both samples at twice the rate at once, as four lanes (the first sample's left and right,
            // then the second's): every lane does what the two-lane code did for its sample, in the same
            // order, so every value is the same to the bit
            // (unsigned: w0 wraps at 2^32, a multiple of kRing, so every index is the one it would be without the
            // wrap; a group's io can be -1)
            const uint32_t b = w0 - 1u - (uint32_t)lat + (uint32_t)io[j];
            const Stereo y0 = ring[(b - 1u) & (kRing - 1)], y1 = ring[b & (kRing - 1)], y2 = ring[(b + 1u) & (kRing - 1)], y3 = ring[(b + 2u) & (kRing - 1)], y4 = ring[(b + 3u) & (kRing - 1)];
            const Quad A = cv[j][0] * quad(y0, y1) + cv[j][1] * quad(y1, y2) + cv[j][2] * quad(y2, y3) + cv[j][3] * quad(y3, y4);
            const Quad pa = quad(ap[j], lo2(A));
            ap[j] = hi2(A);
            const Quad d = A - pa;
            // the last sample's integral is still valid: one lookup per lane; past the table's ends, as gInt
            // continues it (the end's value and slope: a lane inside keeps its own read, a lane outside takes
            // the continuation, each what gInt gives it)
            constexpr float lo = -kAEdge, hi = kAEdge;
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
            // the difference quotient, or where A has hardly moved, T at the middle (read only when a lane
            // needs it; see kQ0)
            const auto moved = vabs4(d) > fmad(vsplat<Quad>(kQk), vabs4(A), vsplat<Quad>(kQ0));
            const Quad q = (g - g0) * rcp4(d);
            Quad r = q;
            if (__builtin_expect(!allSet(moved), 0)) r = moved ? q : tRead4(j, 0.5f * (A + pa));
            const Stereo v = dn[j].down(lo2(r), hi2(r));
            fh[j][fp] = v; fh[j][fp + kFir] = v;
        }
        // The three playback FIRs, written out and side by side: each group's sum is its own chain of
        // multiply-adds in tap order, as one loop per group had it, and y adds the groups in order, so every
        // value is the same to the bit; the three chains no longer wait on each other.
        static_assert(kJ == 3, "three groups");
        Quad acc0 = {}, acc1 = {}, acc2 = {};
        const float *h0 = (const float*)&fh[0][fp], *h1 = (const float*)&fh[1][fp], *h2 = (const float*)&fh[2][fp];
        const float *c0 = (const float*)fir[0], *c1 = (const float*)fir[1], *c2 = (const float*)fir[2];
#pragma clang loop unroll(full)
        for (int k = 0; k < kFir / 2; ++k)
        {
            Quad q0, q1, q2, f0, f1, f2;
            std::memcpy(&q0, h0, sizeof(Quad)); std::memcpy(&f0, c0, sizeof(Quad));
            std::memcpy(&q1, h1, sizeof(Quad)); std::memcpy(&f1, c1, sizeof(Quad));
            std::memcpy(&q2, h2, sizeof(Quad)); std::memcpy(&f2, c2, sizeof(Quad));
            acc0 += f0 * q0; acc1 += f1 * q1; acc2 += f2 * q2;
            h0 += 4; h1 += 4; h2 += 4; c0 += 4; c1 += 4; c2 += 4;
        }
        y += lo2(acc0) + hi2(acc0);
        y += lo2(acc1) + hi2(acc1);
        y += lo2(acc2) + hi2(acc2);
        // the high-frequency self-erasure: the record field's slope (field per micrometer of tape) through
        // a peak follower; above the knee e0 the band above fc is turned down by 1 / (1 + c (env - e0)).
        // The side chain is the record field, so there is no feedback.
        const Stereo sg = vabs(hs - hPrev) * kSig;
        hPrev = hs;
        for (int k = 0; k < 2; ++k) env[k] += (sg[k] > env[k] ? aAtt : aRel) * (sg[k] - env[k]);
        lpY += lpA * (y - lpY);
        const Stereo over = vmax(env - e0, splat(0.0f));
        return lpY + (y - lpY) * rcp2(1.0f + cc * over);   // (the divisor is at least 1)
    }
};

// What the plugin keeps of the recorder: the ring, the two rate changers, the engine and how far the
// crossfade against TapeSat's curve is. (One engine: a change of speed glides its tables, see Engine and
// VHSVibes::hyTick, so that no block ever pays for two.)
// The two running counters (w, dw) are unsigned and wrap at 2^32 (after 13.5 hours at 44.1 kHz for w, which
// moves by two a sample): every use masks them to a power of two that divides 2^32, so the wrap changes
// nothing. (As ints they overflowed after 6.8 hours, which C++ leaves undefined.)
struct Stage
{
    Stereo ring[kRing] = {};
    uint32_t w = 0;               // index of the newest sample (modulo kRing)
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
    uint32_t dw = 0;              // its write position (modulo 128)
    Stereo thX = {}, thY = {};
    // how late the recorder makes the sound at this rate (session samples), and so the dry copy's delay
    // (whole samples, and whether a half more) and the playback side's lag (the stage's own samples)
    float  late = kLatency;
    int    dInt = 41, lagN = 42;
    bool   dHalf = true;

    Stage() { (void)integrals(); }   // (the planes' integrals made now, with the recorder: not on the audio thread)
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
        ring[(w + 1u) & (kRing - 1)] = a;
        ring[(w + 2u) & (kRing - 1)] = b;
        w += 2u;
        return mix(hs);
    }
    // HQ on: two samples at twice the rate in (a the earlier), two out
    __attribute__((always_inline)) void run2(Stereo ha, Stereo hb, Stereo& ya, Stereo& yb)
    {
        ring[(w + 1u) & (kRing - 1)] = ha;
        ring[(w + 2u) & (kRing - 1)] = hb;
        w += 2u;
        outUp.up(mix(hb), ya, yb);
    }
    __attribute__((always_inline)) Stereo mix(Stereo hs) { return run ? eng.step(ring, w, hs) : splat(0.0f); }
};

} // namespace hys
