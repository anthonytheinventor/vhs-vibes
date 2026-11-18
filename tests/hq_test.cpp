// VHS Vibes 1.7: HQ for the modelled microphones and the B-type NR encoder. With HQ on, the
// Camcorder, Phone and Boombox models run at twice the rate up to their last stage that distorts,
// and the NR encoder runs inside the oversampled section, so their clippers no longer fold back.
// Checked here: the aliasing comes down, the level and response stay those of HQ off in the
// linear range, the latency the plugin wants to report and the dry copy follow the extra delay
// in every combination, and switching while a tone plays does not click. Ends with PASS or FAIL.
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#define private public   // the latency the plugin wants to report, and its resting delay
#include "../src/VHSVibes.cpp"
#undef private

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) { return opcode == audioMasterVersion ? 2400 : 0; }
static double dB(double x) { return 20.0 * std::log10(x > 1e-30 ? x : 1e-30); }
static const double kTwoPiD = 6.283185307179586;
static const int kInit = 20;

struct Fx
{
    AEffect* e; float sr;
    Fx(float rate, int hq, const std::vector<std::pair<int, float>>& ch) : sr(rate)
    {
        e = VSTPluginMain(hostCb);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, rate);
        e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
        set(kHq, (float)hq);
        e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
        e->dispatcher(e, effSetProgram, 0, kInit, nullptr, 0.0f);
        set(kNoiseOn, 0);
        for (const auto& c : ch) set(c.first, c.second);
    }
    ~Fx() { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f); e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f); }
    VHSVibes* vv() { return static_cast<VHSVibes*>(static_cast<mpc::Plugin*>(e->object)); }
    void set(int i, float plain) { static_cast<mpc::Plugin*>(e->object)->set(i, plain); }
    // `sec` seconds of sig(sample index) in both channels, 128-frame calls; appends the left channel
    void play(double sec, const std::function<double(long)>& sig, std::vector<float>* out = nullptr)
    {
        static float L[128], R[128]; float* io[2] = { L, R };
        const long n = (long)(sec * sr);
        for (long k = 0; k < n; k += 128)
        {
            const int nb = (int)std::min<long>(128, n - k);
            for (int i = 0; i < nb; ++i) L[i] = R[i] = (float)sig(pos + i);
            e->processReplacing(e, io, io, nb);
            pos += nb;
            if (out) out->insert(out->end(), L, L + nb);
        }
    }
    long pos = 0;
};

// a sine's complex amplitude at hz over the last n samples (Hann window), the phase against the input's
static std::complex<double> toneAt(const std::vector<float>& v, float sr, double hz, long firstIndex, size_t n)
{
    std::complex<double> s = 0; double ws = 0; const size_t off = v.size() - n;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(kTwoPiD * (double)i / (double)n);
        s += (double)v[off + i] * w * std::polar(1.0, -kTwoPiD * hz * (double)(firstIndex + (long)(off + i)) / sr);
        ws += w;
    }
    return s * (2.0 / ws);
}
static void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -kTwoPiD / (double)len; const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w(1); for (size_t k = 0; k < len / 2; ++k) { auto u = a[i + k], t = a[i + k + len / 2] * w; a[i + k] = u + t; a[i + k + len / 2] = u - t; w *= wl; } }
    }
}
// the largest spectral peak (dBFS, a full-scale sine = 0) of the last 65536 samples that is not a
// harmonic of `grid`: what folded back
static double aliasDb(const std::vector<float>& v, float sr, double grid)
{
    const int N = 65536; std::vector<std::complex<double>> a(N); double ws = 0; const size_t off = v.size() - N;
    for (int i = 0; i < N; ++i)
    {
        const double t = kTwoPiD * i / N, w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
        a[(size_t)i] = v[off + (size_t)i] * w; ws += w;
    }
    fft(a);
    const double bin = sr / N; double worst = 0;
    for (int i = (int)(20.0 / bin); i < N / 2 - 1; ++i)
    {
        const double f = i * bin;
        if (std::fabs(f - std::round(f / grid) * grid) <= 10 * bin) continue;
        const double m = std::abs(a[(size_t)i]) * 2.0 / ws;
        if (m >= std::abs(a[(size_t)i - 1]) * 2.0 / ws && m >= std::abs(a[(size_t)i + 1]) * 2.0 / ws) worst = std::fmax(worst, m);
    }
    return dB(worst);
}
// the largest sample-to-sample step against the largest a 300 Hz sine of the local peak (one cycle
// each side) can make; 1.0 for a clean tone, and a click stands out above what the steady sound gives
static double stepRatio(const std::vector<float>& v, float sr, size_t* at = nullptr)
{
    const double sineStep = 2.0 * std::sin(3.141592653589793 * 300.0 / sr);
    const size_t half = (size_t)(sr / 300.0);
    double worst = 0;
    for (size_t i = 1; i < v.size(); ++i)
    {
        double pk = 0;
        for (size_t j = i < half ? 0 : i - half; j < v.size() && j <= i + half; ++j) pk = std::fmax(pk, std::fabs((double)v[j]));
        const double r = pk > 1e-4 ? std::fabs((double)v[i] - v[i - 1]) / (sineStep * pk) : 0.0;
        if (r > worst) { worst = r; if (at) *at = i; }
    }
    return worst;
}

int main()
{
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { std::printf("   %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) ++fails; };
    char what[240];
    typedef std::vector<std::pair<int, float>> Ch;
    const Ch cam0 = { {kMicOn, 1}, {kMicModel, 1}, {kMicType, 0}, {kMicGain, 0} };
    const Ch phone12 = { {kMicOn, 1}, {kMicModel, 1}, {kMicType, 2}, {kMicGain, 12} };
    const Ch boom0 = { {kMicOn, 1}, {kMicModel, 1}, {kMicType, 3}, {kMicGain, 0} };
    const Ch nrRec = { {kNr, 1} };

    std::printf("1. Aliasing, 44.1 kHz: the largest component off the tone's harmonics, HQ off -> on\n");
    {
        // N: what each must gain at least (measured when this was written: 44, 36, 44 and 18 dB)
        struct C { const char* name; Ch ch; double amp, need; };
        const C cs[] = { { "Camcorder, Mic Gain 0, 14 kHz at -8 dBFS", cam0, 0.4, 30.0 },
                         { "Phone, Mic Gain +12, 14 kHz at -1 dBFS", phone12, 0.89, 25.0 },
                         { "Boombox, Mic Gain 0, 14 kHz at -8 dBFS", boom0, 0.4, 30.0 },
                         { "NR Rec, 14 kHz at -1 dBFS", nrRec, 0.89, 12.0 } };
        for (const C& c : cs)
        {
            double a[2];
            for (int hq = 0; hq < 2; ++hq)
            {
                Fx fx(44100.0f, hq, c.ch);
                std::vector<float> out;
                fx.play(2.0, [&](long i) { return c.amp * std::sin(kTwoPiD * 14000.0 * (double)i / 44100.0); }, &out);
                a[hq] = aliasDb(out, 44100.0f, 14000.0);
            }
            std::snprintf(what, sizeof(what), "%s: %.1f -> %.1f dBFS, at least %.0f dB lower", c.name, a[0], a[1], c.need);
            check(a[1] <= a[0] - c.need, what);
        }
    }

    std::printf("2. HQ on against off in the linear range: tone level (dB) at 100 Hz to 15 kHz, at 44.1 and 96 kHz\n");
    {
        struct C { const char* name; Ch ch; double amp, tol10, tol15; };
        // the mics at -30 dBFS (the Camcorder and Boombox ALCs hold a steady gain), NR at -40 dB re Dolby level
        const C cs[] = { { "Camcorder", { {kMicOn, 1}, {kMicModel, 1}, {kMicType, 0}, {kMicGain, 0} }, 0.0316, 0.2, 0.2 },
                         { "Phone", { {kMicOn, 1}, {kMicModel, 1}, {kMicType, 2}, {kMicGain, 0} }, 0.0316, 0.2, 0.2 },
                         { "Boombox", { {kMicOn, 1}, {kMicModel, 1}, {kMicType, 3}, {kMicGain, 0} }, 0.0316, 0.2, 0.2 },
                         { "NR Rec", { {kNr, 1} }, 0.004, 0.05, 1.0 },
                         { "NR On", { {kNr, 3} }, 0.004, 0.05, 1.0 } };
        for (float sr : { 44100.0f, 96000.0f })
            for (const C& c : cs)
            {
                double worst10 = 0, worst15 = 0;
                std::string row;
                for (double hz : { 100.0, 1000.0, 3000.0, 5000.0, 8000.0, 10000.0, 15000.0 })
                {
                    double lvl[2];
                    for (int hq = 0; hq < 2; ++hq)
                    {
                        Fx fx(sr, hq, c.ch);
                        std::vector<float> out;
                        fx.play(1.5, [&](long i) { return c.amp * std::sin(kTwoPiD * hz * (double)i / sr); }, &out);
                        lvl[hq] = dB(std::abs(toneAt(out, sr, hz, 0, (size_t)(0.5 * sr))) / c.amp);
                    }
                    // where the response is more than 40 dB down (the Phone's band edges) the difference is not looked at
                    const double d = lvl[1] - lvl[0];
                    char cell[48]; std::snprintf(cell, sizeof(cell), " %.0f %+.2f", hz, d); row += cell;
                    if (lvl[0] < -40.0) { row += "(off band)"; continue; }
                    if (hz <= 10000.0) worst10 = std::fmax(worst10, std::fabs(d)); else worst15 = std::fmax(worst15, std::fabs(d));
                }
                std::snprintf(what, sizeof(what), "%3.0f kHz %-9s:%s; within %.2f dB to 10 kHz, %.2f dB at 15 kHz", sr / 1000.0, c.name, row.c_str(), c.tol10, c.tol15);
                check(worst10 <= c.tol10 && worst15 <= c.tol15, what);
            }
    }

    std::printf("3. Latency and the dry copy, every combination (Init, 44.1 kHz; a 1 kHz tone at -20 dBFS)\n");
    {
        // The plugin wants to report the wobble delays' resting length, + kHqDelay with HQ, + kHqDelay
        // more while a Camcorder, Phone or Boombox model runs under HQ (rounded). The wet path's delay
        // at 1 kHz must move by those amounts against HQ off (the mic's own filters at twice the rate
        // have a slightly different phase: within 0.2 samples), and the dry copy (Mix 0 %) by exactly
        // those amounts (within 0.02).
        struct M { const char* name; float on, model, type; bool hqMic; };
        const M ms[] = { { "Mic off", 0, 1, 0, false }, { "Simple Camcorder", 1, 0, 0, false }, { "Modeled Camcorder", 1, 1, 0, true },
                         { "Modeled Dynamic", 1, 1, 1, false }, { "Modeled Phone", 1, 1, 2, true }, { "Modeled Boombox", 1, 1, 3, true } };
        const float sr = 44100.0f;
        bool okLat = true, okWet = true, okDry = true;
        double worstWet = 0, worstDry = 0;
        std::string lats;
        for (const M& m : ms)
            for (int nr = 0; nr < 4; ++nr)
            {
                double delay[2][2];   // [hq][dry]
                int lat[2] = { 0, 0 };
                for (int hq = 0; hq < 2; ++hq)
                    for (int dry = 0; dry < 2; ++dry)
                    {
                        Fx fx(sr, hq, { {kMicOn, m.on}, {kMicModel, m.model}, {kMicType, m.type}, {kNr, (float)nr}, {kMix, dry ? 0.0f : 100.0f} });
                        std::vector<float> out;
                        fx.play(1.5, [&](long i) { return 0.1 * std::sin(kTwoPiD * 1000.0 * (double)i / sr); }, &out);
                        delay[hq][dry] = -std::arg(toneAt(out, sr, 1000.0, 0, (size_t)(0.5 * sr))) / (kTwoPiD * 1000.0) * sr;
                        VHSVibes* v = fx.vv();
                        const float want = v->wf1_.base + v->wf2_.base + (hq ? kHqDelay : 0.0f) + (hq && m.hqMic ? kHqDelay : 0.0f);
                        if (dry == 0) { lat[hq] = v->wantLatency_.load(); okLat = okLat && lat[hq] == (int)(want + 0.5f); }
                    }
                const double extra = m.hqMic ? 2.0 * kHqDelay : kHqDelay;
                auto wrap = [](double d) { const double p = 44.1; while (d > p / 2) d -= p; while (d < -p / 2) d += p; return d; };
                const double dw = wrap(delay[1][0] - delay[0][0]) - extra, dd = wrap(delay[1][1] - delay[0][1]) - extra;
                worstWet = std::fmax(worstWet, std::fabs(dw)); worstDry = std::fmax(worstDry, std::fabs(dd));
                okWet = okWet && std::fabs(dw) < 0.2; okDry = okDry && std::fabs(dd) < 0.02;
                if (nr == 0 || nr == 1) { char c[80]; std::snprintf(c, sizeof(c), " %s%s %d/%d;", m.name, nr ? " + NR Rec" : "", lat[0], lat[1]); lats += c; }
            }
        std::printf("   wanted latency, HQ off / on:%s\n", lats.c_str());
        check(okLat, "the latency it wants to report is the resting delay + kHqDelay with HQ + kHqDelay more with a model mic under HQ, in all 48");
        std::snprintf(what, sizeof(what), "the tape path's delay moves by that (within %.3f samples of it; NR adds none)", worstWet);
        check(okWet, what);
        std::snprintf(what, sizeof(what), "the dry copy (Mix 0 %%) moves by the same (within %.4f samples)", worstDry);
        check(okDry, what);
    }

    std::printf("4. Switched while a tone plays, HQ on: no click (step ratio against the steady sound before and after)\n");
    {
        // a 300 Hz tone at -14 dBFS. Each change is made once and then undone (or followed by the next);
        // the steps it makes must be no larger than 1.1 times the largest step of the steady sound on
        // either side.
        struct S { const char* name; Ch start; Ch moves; };
        const S ss[] = {
            { "Mic on, off (Modeled Camcorder)", { {kMicModel, 1}, {kMicType, 0}, {kMicOn, 0} }, { {kMicOn, 1}, {kMicOn, 0} } },
            { "Mic on, off (Modeled Phone), Mix 50 %", { {kMicModel, 1}, {kMicType, 2}, {kMicOn, 0}, {kMix, 50} }, { {kMicOn, 1}, {kMicOn, 0} } },
            { "Mic Model Simple -> Modeled -> Simple (Boombox)", { {kMicModel, 0}, {kMicType, 3}, {kMicOn, 1} }, { {kMicModel, 1}, {kMicModel, 0} } },
            { "Mic Type Camcorder, Phone, Boombox, Dynamic, Camcorder, Mix 50 %", { {kMicModel, 1}, {kMicType, 0}, {kMicOn, 1}, {kMix, 50} },
              { {kMicType, 2}, {kMicType, 3}, {kMicType, 1}, {kMicType, 0} } },
            { "HQ off, on (Modeled Camcorder)", { {kMicModel, 1}, {kMicType, 0}, {kMicOn, 1} }, { {kHq, 0}, {kHq, 1} } },
            { "NR Off, Rec, On, Play, Off", { }, { {kNr, 1}, {kNr, 3}, {kNr, 2}, {kNr, 0} } },
        };
        const float sr = 44100.0f;
        auto sig = [&](long i) { return 0.2 * std::sin(kTwoPiD * 300.0 * (double)i / sr); };
        for (const S& s : ss)
        {
            Fx fx(sr, 1, s.start);
            std::vector<float> steady, during;
            fx.play(1.0, sig);
            fx.play(0.5, sig, &steady);
            double base = stepRatio(steady, sr);
            for (const auto& mv : s.moves)
            {
                fx.set(mv.first, mv.second);
                fx.play(0.6, sig, &during);
                std::vector<float> st;
                fx.play(0.3, sig, &st);       // settled in the new setting: the steady sound there
                base = std::fmax(base, stepRatio(st, sr));
                during.insert(during.end(), st.begin(), st.end());
            }
            size_t at = 0;
            // (one cycle of the sound before the first change in front: the local peak needs it on both sides)
            const size_t cyc = (size_t)(sr / 300.0f);
            during.insert(during.begin(), steady.end() - (long)cyc, steady.end());
            const double r = stepRatio(during, sr, &at);
            at = at > cyc ? at - cyc : 0;
            std::snprintf(what, sizeof(what), "%s: step ratio %.3f (%.0f ms after the first change), steady %.3f", s.name, r, 1000.0 * (double)at / sr, base);
            check(r <= 1.1 * base, what);
        }
    }

    std::printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
