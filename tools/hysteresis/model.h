// Tone-level wrapper around hyst.h: speeds, pre-emphasis, playback equalization, calibration.
#pragma once
#include "hyst.h"
#include <map>
#include <string>

namespace hy {

constexpr double kSP = 33350.0, kEP = 11120.0;   // um/s
constexpr double kRef = 0.4;                      // plugin kRefLevel (digital amplitude, -8 dBFS)

// The plugin's record pre-emphasis (analog prototype): zero at fz, pole at 10 fz; fz = 1326.3 Hz in
// SP, a third of it in EP (TapeSat::set)
inline cd preEmph(double f, bool ep)
{
    const double fz = 1326.3 / (ep ? 3.0 : 1.0);
    return cd(1.0, f / fz) / cd(1.0, f / (10 * fz));
}

struct Model
{
    Par p;
    Density d;
    ETable tab;
    Eval ev;
    Geometry sp, ep;
    mutable PathSpec ps;
    Mode mode = ENV2;
    explicit Model(const Par& q, int nl = 12, double tabStep = 0.01)
        : p(q), d(q), sp(q, kSP, nl), ep(q, kEP, nl)
    {
        tab.build(d, tabStep);
        ev = Eval{nullptr, &tab};
    }
    const Geometry& geo(bool isEp) const { return isEp ? ep : sp; }
    // played-back complex harmonics 1..nh of a sine of field amplitude h at f
    std::vector<cd> tone(bool isEp, double f, double h, double Hb, int nE = 64, Mode m = (Mode)-1, int nh = 7) const
    {
        const Geometry& G = geo(isEp);
        ToneSpec T{f, h, Hb, nE};
        auto H = recordTone(G, ev, m == (Mode)-1 ? mode : m, T, ps, nh);
        std::vector<cd> y(nh + 1);
        for (int n = 1; n <= nh; ++n) y[n] = playback(G, H, f, n);
        return y;
    }
};

// Calibration state: correct bias, small-signal response at correct bias (for the playback EQ),
// field per digital unit
struct Cal
{
    double HbRight = 0, k = 0;        // field per unit of the pre-emphasized digital signal
    double hSmall = 0.01;             // field amplitude used for small-signal responses
    std::map<std::pair<int, long>, cd> D;   // (speed, Hz) -> small-signal response at correct bias
    Mode mode = RAW; int nShift = 3;        // the form the EQ is taken from (fixed at calibration)
};

inline cd smallSignal(const Model& M, bool isEp, double f, double Hb, double hs, Mode md = (Mode)-1)
{
    return M.tone(isEp, f, hs, Hb, 32, md, 1)[1] / hs;
}

inline cd Dresp(const Model& M, Cal& C, bool isEp, double f)
{
    auto key = std::make_pair((int)isEp, (long)std::lround(f * 10));
    auto it = C.D.find(key);
    if (it != C.D.end()) return it->second;
    const int keep = M.ps.nShift;
    M.ps.nShift = C.nShift;
    const cd v = smallSignal(M, isEp, f, C.HbRight, C.hSmall, C.mode);
    M.ps.nShift = keep;
    C.D[key] = v;
    return v;
}

// Output (digital) harmonic amplitudes 1..nh of a digital input tone of amplitude a at f, through
// pre-emphasis, the recorder, playback, playback EQ (inverse of the small-signal response at correct
// bias) and de-emphasis.
inline std::vector<double> outTone(const Model& M, Cal& C, bool isEp, double f, double a, double Hb, int nh = 5, int nE = 64, Mode md = (Mode)-1)
{
    const double h = C.k * a * std::abs(preEmph(f, isEp));
    auto y = M.tone(isEp, f, h, Hb, nE, md, nh);
    std::vector<double> o(nh + 1, 0.0);
    for (int n = 1; n <= nh; ++n)
    {
        if (n * f > 22050) continue;
        o[n] = std::abs(y[n] / Dresp(M, C, isEp, n * f) / preEmph(n * f, isEp)) / C.k;
    }
    return o;
}

inline double dB(double x) { return 20 * std::log10(std::max(x, 1e-30)); }

// scale sc and si together (keeping their ratio) until the normalized loop gives the target SFD
inline Par fitSfd(Par p, double target)
{
    double lo = 0.2, hi = 2.0;
    for (int it = 0; it < 40; ++it)
    {
        const double m = 0.5 * (lo + hi);
        Par q = p; q.sc *= m; q.si *= m;
        if (loopFigures(normalize(q)).sfd > target) hi = m; else lo = m;
    }
    Par q = p; q.sc *= 0.5 * (lo + hi); q.si *= 0.5 * (lo + hi);
    return normalize(q);
}

// Calibration. Correct bias: the deck rule used here is "SP, a 6.3 kHz tone well below reference
// level; raise the bias past the 6.3 kHz output peak until that output has fallen overDb below the
// peak" (6.3 kHz at 33.35 mm/s is a 5.3 um wavelength, close to the 4.8 um of the 10 kHz tone used
// the same way at 47.6 mm/s). Then k (field per digital unit) so that a tone of fCal at the plugin's
// reference level (0.4) gives hd3 third harmonic at the output.
struct CalRule { double fBias = 6300, overDb = 2.0, fCal = 441, hd3 = 0.03; };
inline Cal calibrate(const Model& M, const CalRule& R = CalRule(), bool verbose = true)
{
    Cal C;
    C.mode = M.mode; C.nShift = M.ps.nShift;
    auto sens = [&](double Hb) { return dB(std::abs(smallSignal(M, false, R.fBias, Hb, C.hSmall))); };
    // peak by golden section on [1, 8]
    double a = 1.0, b = 8.0;
    for (int i = 0; i < 26; ++i)
    {
        const double m1 = b - 0.618 * (b - a), m2 = a + 0.618 * (b - a);
        if (sens(m1) > sens(m2)) b = m2; else a = m1;
    }
    const double hbPk = 0.5 * (a + b), sPk = sens(hbPk);
    double lo = hbPk, hi = 3 * hbPk;
    for (int i = 0; i < 24; ++i) { const double m = 0.5 * (lo + hi); if (sens(m) > sPk - R.overDb) lo = m; else hi = m; }
    C.HbRight = 0.5 * (lo + hi);
    // k
    double klo = 0.01, khi = 100;
    for (int i = 0; i < 34; ++i)
    {
        C.k = std::sqrt(klo * khi);
        auto o = outTone(M, C, false, R.fCal, kRef, C.HbRight, 3);
        if (o[3] / o[1] > R.hd3) khi = C.k; else klo = C.k;
    }
    C.k = std::sqrt(klo * khi);
    if (verbose) std::printf("calibration: %.0f Hz peak at Hb %.4f (%.2f dB); correct bias Hb %.4f; k %.5f (field at ref level, %.0f Hz: %.4f)\n",
                             R.fBias, hbPk, sPk, C.HbRight, C.k, R.fCal, C.k * kRef * std::abs(preEmph(R.fCal, false)));
    return C;
}

// The plugin's present curve (TapeSat, Bias right, Rec Level 0 dB): s curve(w / s), s = 0.4 / 0.746,
// pre-emphasis before and de-emphasis after; harmonics of the static curve by DFT
inline std::vector<double> pluginTone(bool isEp, double f, double a, int nh = 5, double biasKnob = 0.0)
{
    const double s = (0.4 / 0.746) * (biasKnob < 0 ? std::pow(10.0, 6.0 * biasKnob / 20) : 1.0);
    const double A = a * std::abs(preEmph(f, isEp));
    const int N = 512;
    std::vector<double> o(nh + 1, 0.0);
    for (int n = 1; n <= nh; ++n)
    {
        if (n * f > 22050) continue;
        cd acc = 0;
        for (int i = 0; i < N; ++i)
        {
            double t = A * std::sin(2 * kPi * i / N) / s;
            t = std::clamp(t, -3.0, 3.0);
            const double c = s * t * (27 + t * t) / (27 + 9 * t * t);
            acc += c * std::polar(1.0, -2 * kPi * n * i / N);
        }
        o[n] = std::abs(acc) * 2.0 / N / std::abs(preEmph(n * f, isEp));
    }
    return o;
}

} // namespace hy
