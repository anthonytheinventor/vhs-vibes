// VHS Vibes 1.6: the four microphones (Mic Type), measured through the plugin against the
// published figures they are built from (see MicModel in the source, and the manual's chapter
// on them). Every case starts from the Init program (neutral), HQ off, and compares the output
// with the Mic on against the same instance's output with it off, so only the mic is measured.
// Only the plugin's parameters are used, so the file builds against older versions too (1.5.3
// fails most of it). Ends with PASS or FAIL.
//
// The level mapping the models use (an assumption, stated in the manual): a sine peaking at
// kRefLevel (-8 dBFS) is 104 dB SPL, so 94 dB SPL peaks at -18 dBFS.
//
// The Camcorder is the camera's record chain (the camcorder report, 2026-11-15): capsule, mic
// amp, record ALC. Its sections (3, 4, 13 to 18) check it against the report's numbers,
// worked out here from the documents' own figures, not from the source's constants. Section 16
// measures how far each model is from bypass on a music-like signal, for the Camcorder that is
// replaced (the capsule alone, still in the source as MicModel::capsule) and the new one.
#include "../src/VHSVibes.cpp"

#include <algorithm>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) { return opcode == audioMasterVersion ? 2400 : 0; }
static double dB(double x) { return 20.0 * std::log10(x > 1e-30 ? x : 1e-30); }
static double fromDb(double d) { return std::pow(10.0, d / 20.0); }
const double kPi2 = 6.283185307179586;
const int kInit = 20;
static const char* kNames[4] = { "Camcorder", "Dynamic", "Phone", "Boombox" };
static const double kRef = 0.4;                                   // kRefLevel, as a number (1.5.3 has it too)
static double splPeak(double spl) { return kRef * fromDb(spl - 104.0); }   // the peak of a sine of this SPL
static double splRms(double spl) { return splPeak(spl) / std::sqrt(2.0); }   // and the rms of a noise of this SPL

// The Camcorder's ALC from the documents (camcorder report, section 3): knee at -66 dBs (0.388 mV)
// from a -35 dB V/Pa capsule; 1 : 19 above it over 22 dB; the end of the range on kRefLevel.
static const double kKneeSpl  = 94.0 + 20.0 * std::log10(0.7746e-3 * std::pow(10.0, -6.0 / 20.0) / std::pow(10.0, -35.0 / 20.0));
static const double kKneePk   = splPeak(kKneeSpl);                         // a sine at the knee: its peak
static const double kKneeOut  = kRef * std::pow(10.0, -22.0 / 19.0 / 20.0);  // the output there
static const double kCamGain  = kKneeOut / kKneePk;                          // the record gain before the ALC
static const double kAttDb    = 22.0 * 18.0 / 19.0;                          // its largest attenuation
static const double kRecDbS   = 18.0 * 20.0 * std::log10(std::exp(1.0)) / 19.0;   // recovery, dB/s (19 s detector, 18 dB per dB)
// The Camcorder hears the source kCamDistanceDb quieter (the camera across the room), so a sine
// that reaches the microphone d dB above the knee comes into the plugin this much louder.
static const double kDist     = fromDb(kCamDistanceDb);
static double knee(double d) { return kKneePk * fromDb(d) * kDist; }          // the input (Mic Gain 0) that puts a sine d dB above the knee
// the static curve the report gives, output in dB re the knee's output
static double lawDb(double d) { return d <= 0.0 ? d : d <= 22.0 ? d / 19.0 : 22.0 / 19.0 + (d - 22.0); }

struct Fx
{
    AEffect* e; float sr;
    Fx(float rate, int type, bool micOn, bool noiseOn = false, float micGain = 0.0f) : sr(rate)
    {
        e = VSTPluginMain(hostCb);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, rate);
        e->dispatcher(e, effSetBlockSize, 0, 512, nullptr, 0.0f);
        set(kHq, 0);
        e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
        e->dispatcher(e, effSetProgram, 0, kInit, nullptr, 0.0f);
        set(kNoiseOn, noiseOn ? 1.0f : 0.0f);
        set(kMicOn, micOn ? 1.0f : 0.0f); set(kMicType, (float)type); set(kMicGain, micGain);
    }
    ~Fx() { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f); e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f); }
    void set(int i, float plain) { static_cast<mpc::Plugin*>(e->object)->set(i, plain); }
    // renders `in` (mono, sent to both channels) in 128-frame calls, returns the left channel
    std::vector<float> run(const std::vector<float>& in)
    {
        std::vector<float> out(in.size());
        static float L[128], R[128]; float* io[2] = { L, R };
        for (size_t pos = 0; pos < in.size(); pos += 128)
        {
            const size_t n = std::min<size_t>(128, in.size() - pos);
            for (size_t i = 0; i < n; ++i) L[i] = R[i] = in[pos + i];
            e->processReplacing(e, io, io, (VstInt32)n);
            std::copy(L, L + n, out.begin() + (long)pos);
        }
        return out;
    }
};

static std::vector<float> sine(float sr, double hz, double amp, double seconds, double phase = 0.0)
{
    std::vector<float> v((size_t)(seconds * sr));
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float)(amp * std::sin(kPi2 * hz * (double)i / sr + phase));
    return v;
}
// amplitude of `hz` in v[from, to), Hann-weighted
static double partial(const std::vector<float>& v, float sr, double hz, size_t from, size_t to)
{
    std::complex<double> acc = 0.0; double wsum = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(kPi2 * (double)(i - from) / (double)(to - from));
        acc += w * (double)v[i] * std::polar(1.0, -kPi2 * hz * (double)i / sr);
        wsum += w;
    }
    return 2.0 * std::abs(acc) / wsum;
}
// The mic's gain at hz in dB (mic on against mic off, same input), from 0.4 s on
static double gainDb(float sr, int type, double hz, double amp)
{
    const std::vector<float> in = sine(sr, hz, amp, 0.75);
    Fx on(sr, type, true), off(sr, type, false);
    const std::vector<float> a = on.run(in), b = off.run(in);
    const size_t from = (size_t)(0.4 * sr), to = in.size();
    return dB(partial(a, sr, hz, from, to) / partial(b, sr, hz, from, to));
}
// relative to 1 kHz
static double respDb(float sr, int type, double hz, double amp) { return gainDb(sr, type, hz, amp) - gainDb(sr, type, 1000.0, amp); }

// FFT (radix 2, in place)
static void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<double> wl = std::polar(1.0, -kPi2 / (double)len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w = 1.0;
            for (size_t k = 0; k < len / 2; ++k) { const std::complex<double> u = a[i + k], t = w * a[i + k + len / 2]; a[i + k] = u + t; a[i + k + len / 2] = u - t; w *= wl; }
        }
    }
}
// IEC 61672 A-weighting, 0 dB at 1 kHz
static double aWeight(double f)
{
    const double f2 = f * f;
    const double ra = 12194.0 * 12194.0 * f2 * f2 / ((f2 + 20.6 * 20.6) * std::sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
    return ra * fromDb(2.0);
}
// rms of v[from, to) between lo and hi Hz, A-weighted or not (Hann-windowed periodograms, averaged)
static double bandRms(const std::vector<float>& v, float sr, size_t from, size_t to, double lo, double hi, bool aw)
{
    const size_t n = 8192;
    double sum = 0.0; int segs = 0;
    for (size_t s = from; s + n <= to; s += n / 2, ++segs)
    {
        std::vector<std::complex<double>> a(n);
        double w2 = 0.0;
        for (size_t i = 0; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos(kPi2 * (double)i / (double)n); a[i] = w * (double)v[s + i]; w2 += w * w; }
        fft(a);
        for (size_t k = 1; k < n / 2; ++k)
        {
            const double f = (double)k * sr / (double)n;
            if (f < lo || f > hi) continue;
            const double g = aw ? aWeight(f) : 1.0;
            sum += 2.0 * std::norm(a[k]) / (w2 * (double)n) * g * g;
        }
    }
    return segs ? std::sqrt(sum / segs) : 0.0;
}
static double thd(const std::vector<float>& v, float sr, double hz, size_t from, size_t to, double* h2 = nullptr, double* h3 = nullptr)
{
    const double f1 = partial(v, sr, hz, from, to);
    double s = 0.0;
    for (int h = 2; h <= 9 && h * hz < 0.45 * sr; ++h)
    {
        const double a = partial(v, sr, hz * h, from, to);
        if (h == 2 && h2) *h2 = a / f1;
        if (h == 3 && h3) *h3 = a / f1;
        s += a * a;
    }
    return std::sqrt(s) / f1;
}

// The capsule's 26 dB SPL(A) (white) at the camcorder's output with the ALC at full gain: through
// the record gain and the mic amp's band, measured A-weighted between 20 Hz and 20 kHz.
static double camNoise(float sr)
{
    MicModel m; m.set(0, sr);
    double num = 0.0, den = 0.0;
    for (double f = 20.0; f < 20000.0 && f < 0.5 * sr; f += 1.0)
    {
        const double h = m.gainAt(2, 4, sr, (float)f) / m.gainAt(2, 4, sr, 1000.0f), a = aWeight(f);
        num += h * h * a * a; den += a * a;
    }
    return splRms(26.0) * kCamGain * std::sqrt(num / den);
}
// the music-like test signal (16): a chord, a kick on every beat, a hi-hat on every half beat;
// loud for 2 s, 15 dB quieter for 2 s, 2 s of silence, loud 2 s, quiet 2 s; -18 dBFS rms over
// the parts that play
static std::vector<float> music(float sr)
{
    const double secs = 10.0;
    std::vector<double> v((size_t)(secs * sr), 0.0);
    uint32_t rng = 0x9e3779b9u;
    const double chord[3] = { 220.0, 277.18, 329.63 };
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double t = (double)i / sr;
        if (t >= 4.0 && t < 6.0) continue;
        const bool loud = t < 2.0 || (t >= 6.0 && t < 8.0);
        double x = 0.0;
        for (double f : chord) for (int k = 1; k <= 6; ++k) x += 0.08 / k * std::sin(kPi2 * f * k * t + k);
        const double tb = std::fmod(t, 0.5), th = std::fmod(t, 0.25);
        x += 0.9 * std::exp(-tb / 0.08) * std::sin(kPi2 * (50.0 + 60.0 * std::exp(-tb / 0.02)) * tb);
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        x += 0.25 * std::exp(-th / 0.015) * ((double)rng / 2147483648.0 - 1.0);
        v[i] = x * (loud ? 1.0 : fromDb(-15.0));
    }
    double e = 0.0; size_t n = 0;
    for (size_t i = 0; i < v.size(); ++i) { const double t = (double)i / sr; if (t < 4.0 || t >= 6.0) { e += v[i] * v[i]; ++n; } }
    const double k = fromDb(-18.0) / std::sqrt(e / (double)n);
    std::vector<float> out(v.size());
    for (size_t i = 0; i < v.size(); ++i) out[i] = (float)(v[i] * k);
    return out;
}
struct Dyn { double crest, loudQuiet, spread, gap; };
// crest factor (peak / rms over the parts that play), loud against quiet (rms of 0.5-2 s against
// 2.5-4 s), level over time (standard deviation of the 50 ms rms in dB, over the parts that play,
// after the first 0.2 s), and the gap's level against the loud part (rms of its last half second,
// 5.4-5.9 s)
static Dyn dynamics(const std::vector<float>& v, float sr)
{
    auto rms = [&](double a, double b) { double e = 0; size_t n = 0; for (size_t i = (size_t)(a * sr); i < (size_t)(b * sr); ++i, ++n) e += (double)v[i] * v[i]; return std::sqrt(e / (double)n); };
    double pk = 0, e = 0; size_t n = 0;
    for (size_t i = (size_t)(0.2 * sr); i < v.size(); ++i) { const double t = (double)i / sr; if (t >= 4.0 && t < 6.0) continue; pk = std::fmax(pk, std::fabs(v[i])); e += (double)v[i] * v[i]; ++n; }
    std::vector<double> lv;
    for (double t = 0.2; t + 0.05 <= 10.0; t += 0.05) if (t + 0.05 <= 4.0 || t >= 6.0) lv.push_back(dB(rms(t, t + 0.05)));
    double m = 0; for (double x : lv) m += x;
    m /= (double)lv.size();
    double sd = 0; for (double x : lv) sd += (x - m) * (x - m);
    Dyn d;
    d.crest = dB(pk / std::sqrt(e / (double)n));
    d.loudQuiet = dB(rms(0.5, 2.0) / rms(2.5, 4.0));
    d.spread = std::sqrt(sd / (double)lv.size());
    d.gap = dB(rms(5.4, 5.9) / rms(0.5, 2.0));
    return d;
}
// MicModel alone, as the plugin calls it (Mic Gain in front); old = the capsule alone (the
// Camcorder before this model)
static std::vector<float> micOnly(const std::vector<float>& in, float sr, double gainDb, bool noise, bool old, MicModel* keep = nullptr)
{
    MicModel m; m.set(0, sr); m.reset();
    const float g = (float)fromDb(gainDb);
    std::vector<float> out(in.size());
    for (size_t i = 0; i < in.size(); ++i)
        out[i] = old ? m.capsule(in[i] * g, noise ? 1.0f : 0.0f) : m.process(splat(in[i] * g), noise ? 1.0f : 0.0f)[0];
    if (keep) *keep = m;
    return out;
}

int main()
{
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { std::printf("   %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) ++fails; };
    const float rates[5] = { 44100.0f, 48000.0f, 88200.0f, 96000.0f, 192000.0f };

    std::printf("1. Phone: the CCITT P.48 IRS sending curve, Table 1/P.48 (dB re 1 kHz), at reference level\n");
    {
        const double tbl[24][2] = { {100,-45.8},{125,-36.1},{160,-25.6},{200,-19.2},{250,-14.3},{300,-11.3},{315,-10.8},{400,-8.4},{500,-6.9},{600,-6.3},{630,-6.1},{800,-4.9},
                                    {1000,-3.7},{1250,-2.3},{1600,-0.6},{2000,0.3},{2500,1.8},{3000,1.5},{3150,1.8},{3500,-7.3},{4000,-37.2},{5000,-52.2},{6300,-73.6},{8000,-90.0} };
        for (float sr : rates)
        {
            double worst = 0.0, worstAt = 0.0; bool deep = true; std::string line;
            for (const auto& p : tbl)
            {
                const double want = p[1] + 3.7, got = respDb(sr, 2, p[0], kRef);
                char b[48]; std::snprintf(b, sizeof(b), " %g:%+.1f/%+.1f", p[0], got, want); line += b;
                if (want < -40.0) { if (got > want + 1.0) deep = false; continue; }   // 5, 6.3 and 8 kHz: within 1 dB or deeper
                const double err = std::fabs(got - want) / (want < -30.0 ? 2.0 : 1.0);   // 4 kHz (-33.5): half the weight
                if (err > worst) { worst = err; worstAt = p[0]; }
            }
            std::printf("   %6.0f Hz:%s\n", sr, line.c_str());
            char what[160];
            std::snprintf(what, sizeof(what), "%.0f Hz: within 0.5 dB of the table to 3.5 kHz and 1 dB at 4 kHz (worst %.2f at %.0f Hz); 5 to 8 kHz within 1 dB or deeper", sr, worst, worstAt);
            check(worst <= 0.5 && deep, what);
        }
    }

    std::printf("2. Dynamic: the Shure 515 (Unidyne B) curve at 0.6 m, read off figure 1 of the 515BSM guide (dB re 1 kHz)\n");
    {
        const double pts[12][2] = { {50,-16},{100,-8},{200,-4},{500,-1.5},{1000,0},{2000,1},{4000,4},{5000,5},{6500,2},{8000,3},{10000,0},{15000,-10} };
        for (float sr : rates)
        {
            double worst = 0.0, at = 0.0; std::string line;
            for (const auto& p : pts)
            {
                const double got = respDb(sr, 1, p[0], 0.1);
                char b[40]; std::snprintf(b, sizeof(b), " %g:%+.1f/%+.0f", p[0], got, p[1]); line += b;
                if (std::fabs(got - p[1]) > worst) { worst = std::fabs(got - p[1]); at = p[0]; }
            }
            std::printf("   %6.0f Hz:%s\n", sr, line.c_str());
            char what[120]; std::snprintf(what, sizeof(what), "%.0f Hz: within 1 dB of every read point (the plot reads to about 1 dB; worst %.2f at %.0f Hz)", sr, worst, at);
            check(worst <= 1.0, what);
        }
        Fx a(44100.0f, 1, true, false, 6.0f), b(44100.0f, 1, true, false, 0.0f);
        const std::vector<float> in = sine(44100.0f, 1000.0, 0.05, 0.75);
        const double g = dB(partial(a.run(in), 44100.0f, 1000.0, 17640, in.size()) / partial(b.run(in), 44100.0f, 1000.0, 17640, in.size()));
        std::printf("   Mic Gain +6 dB: %+.2f dB at 1 kHz\n", g);
        check(std::fabs(g - 6.0) < 0.05, "Mic Gain is a plain gain in front of it");
    }

    std::printf("3. Camcorder: the band, below the ALC's knee. The capsule (gate highpass: Kean, 100 Mohm, 30 pF; HF rise: WM-61A, Neal's plot)\n"
                "   times the mic amp (Sony DCR-TRV900E mic input, Vrancic's table); through the plugin against the two parts\n");
    {
        // the capsule as before: first order at 53.05 Hz, the units' spread at 13 to 17 kHz
        const double cap[11][3] = { {20,-9.1,0.5},{53.05,-3.0,0.5},{150,-0.5,0.5},{300,0,0.3},{3000,0,0.3},{5000,0,0.3},{8000,1.0,0.5},{13000,2.25,0.75},{15000,2.25,0.75},{17000,2.25,0.75},{21500,0,1.0} };
        // the mic amp, read (dB re 1 kHz)
        const double amp[10][2] = { {20,-12.8},{50,-3.1},{100,-0.6},{200,0.0},{1000,0.0},{2000,-0.2},{5000,-0.6},{10000,-1.7},{15000,-3.6},{20000,-4.9} };
        for (float sr : rates)
        {
            MicModel m; m.set(0, sr);
            auto part = [&](int from, int to, double hz) { return dB(m.gainAt(from, to, sr, (float)hz) / m.gainAt(from, to, sr, 1000.0f)); };
            double worstCap = 0.0, worstAmp = 0.0, worstAll = 0.0; bool okCap = true; std::string lc, la;
            for (const auto& p : cap)
            {
                if (p[0] > 0.45 * sr) continue;
                const double got = part(0, 2, p[0]);
                char b[40]; std::snprintf(b, sizeof(b), " %g:%+.2f/%+.2f", p[0], got, p[1]); lc += b;
                worstCap = std::fmax(worstCap, std::fabs(got - p[1]));
                if (std::fabs(got - p[1]) > p[2]) okCap = false;
            }
            for (const auto& p : amp)
            {
                if (p[0] > 0.45 * sr) continue;
                const double got = part(2, 4, p[0]);
                char b[40]; std::snprintf(b, sizeof(b), " %g:%+.2f/%+.1f", p[0], got, p[1]); la += b;
                worstAmp = std::fmax(worstAmp, std::fabs(got - p[1]));
            }
            // through the plugin, 20 dB under the knee (the ALC at full gain, both clips far off)
            for (double hz : {20.0, 50.0, 100.0, 300.0, 3000.0, 8000.0, 15000.0})
                worstAll = std::fmax(worstAll, std::fabs(respDb(sr, 0, hz, knee(-20.0)) - part(0, 4, hz)));
            std::printf("   %6.0f Hz capsule:%s\n", sr, lc.c_str());
            std::printf("   %6.0f Hz mic amp:%s\n", sr, la.c_str());
            char what[200];
            std::snprintf(what, sizeof(what), "%.0f Hz capsule: within 0.5 dB at the highpass and at 8 kHz, 0.3 dB in the flat part, inside the spread at 13 to 17 kHz, 1 dB at 21.5 kHz", sr);
            check(okCap, what);
            std::snprintf(what, sizeof(what), "%.0f Hz mic amp: within 0.3 dB of every read point to 0.45 times the rate (worst %.2f)", sr, worstAmp);
            check(worstAmp <= 0.3, what);
            std::snprintf(what, sizeof(what), "%.0f Hz: the plugin's response is the two together, within 0.1 dB (worst %.3f)", sr, worstAll);
            check(worstAll <= 0.1, what);
        }
        std::printf("   (Vrancic's table also gives -28.1 dB at 10 Hz; the fitted highpass gives %.1f there, the capsule adds %.1f)\n",
                    [] { MicModel m; m.set(0, 48000.0f); return dB(m.gainAt(2, 4, 48000.0f, 10.0f) / m.gainAt(2, 4, 48000.0f, 1000.0f)); }(),
                    [] { MicModel m; m.set(0, 48000.0f); return dB(m.gainAt(0, 2, 48000.0f, 10.0f) / m.gainAt(0, 2, 48000.0f, 1000.0f)); }());
    }

    std::printf("4. Camcorder: the two overloads ahead of the ALC, 1 kHz, each stage alone\n"
                "   the capsule's JFET (Kean: above 110 dB SPL, negative peaks first); the mic amp (derived: 107 dB SPL from the BA7780 and LA7471, symmetric)\n");
    {
        const float sr = 44100.0f;
        auto stage = [&](float (*fn)(float), double spl, double* h2, double* h3, double* hi, double* lo) {
            const std::vector<float> in = sine(sr, 1000.0, splPeak(spl), 0.2);
            std::vector<float> out(in.size());
            for (size_t i = 0; i < in.size(); ++i) out[i] = fn(in[i]);
            double mean = 0; for (float v : out) mean += v;
            mean /= (double)out.size();
            *hi = 0; *lo = 0; for (float v : out) { *hi = std::fmax(*hi, v - mean); *lo = std::fmin(*lo, v - mean); }
            return thd(out, sr, 1000.0, 0, out.size(), h2, h3);
        };
        double jf[6] = {0}, ma[6] = {0}; int k = 0;
        for (double spl : {101.0, 104.0, 107.0, 110.0, 113.0, 116.0})
        {
            double h2, h3, hi, lo, m2, m3, mhi, mlo;
            jf[k] = stage(MicModel::jfet, spl, &h2, &h3, &hi, &lo);
            ma[k] = stage(MicModel::micAmp, spl, &m2, &m3, &mhi, &mlo);
            std::printf("   %.0f dB SPL (peak %.3f): JFET THD %.2f %% (2nd %.2f %%, 3rd %.2f %%, peaks +%.4f/-%.4f); mic amp THD %.2f %% (2nd %.3f %%, 3rd %.2f %%, peaks +%.4f/-%.4f)\n",
                        spl, splPeak(spl), 100 * jf[k], 100 * h2, 100 * h3, hi, -lo, 100 * ma[k], 100 * m2, 100 * m3, mhi, -mlo);
            if (spl == 110.0) { check(-lo < hi, "JFET: the negative half gives way first"); check(h2 > 0.5 * h3, "so there is second harmonic as well as third"); }
            if (spl == 107.0) check(m2 < 0.0001 && std::fabs(mhi + mlo) < 1e-4, "mic amp: both halves alike (no second harmonic)");
            ++k;
        }
        check(std::fabs(jf[3] - 0.01) < 0.0015, "JFET: 1 % THD at 110 dB SPL (taken as where overload begins)");
        check(std::fabs(ma[2] - 0.01) < 0.0015, "mic amp: 1 % THD at 107 dB SPL (taken as where overload begins)");
        check(ma[2] > jf[2], "so of the two the mic amp gives way first");
    }

    std::printf("5. Noise floors (silence in, Noise switch on, the mic's own noise only)\n");
    {
        for (float sr : { 44100.0f, 96000.0f })
        {
            std::vector<float> in((size_t)(3.0f * sr), 0.0f);
            {
                Fx fx(sr, 0, true, true);
                const std::vector<float> out = fx.run(in);
                const double a = bandRms(out, sr, (size_t)(0.5f * sr), out.size(), 20.0, 20000.0, true);
                const double want = camNoise(sr);
                std::printf("   %.0f Hz Camcorder: %.1f dB(A) re full scale, %.1f dB re a kRefLevel sine; the WM-61A's 26 dB SPL(A) through the full record gain (%+.1f dB) and the mic amp's band: %.1f\n",
                            sr, dB(a), dB(a / (kRef / std::sqrt(2.0))), dB(kCamGain), dB(want));
                check(std::fabs(dB(a / want)) < 1.0, "silence in: the capsule's self-noise at the full record gain, within 1 dB");
            }
            {
                Fx fx(sr, 2, true, true);
                const std::vector<float> out = fx.run(in);
                const double a = bandRms(out, sr, (size_t)(0.5f * sr), out.size(), 5.0, 0.49 * sr, false);
                // -65 dBm0; 1 Pa at the mouth = -1.48 dBm0 at the junction, and comes out as a 1 kHz tone of 1 Pa
                const double want = splRms(94.0) * fromDb(-65.0 + 1.48);
                const double inBand = bandRms(out, sr, (size_t)(0.5f * sr), out.size(), 300.0, 3400.0, false);
                std::printf("   %.0f Hz Phone: line noise %.1f dB re full scale = %.1f dBm0 (%.1f dBm0 between 300 and 3400 Hz); G.712 allows -65 dBm0p\n", sr, dB(a), -65.0 + dB(a / want), -65.0 + dB(inBand / want));
                check(std::fabs(dB(a / want)) < 1.0, "the line's idle noise within 1 dB of -65 dBm0");
            }
            {
                Fx fx(sr, 1, true, true);
                const std::vector<float> out = fx.run(in);
                double m = 0; for (float s : out) m = std::fmax(m, std::fabs(s));
                std::printf("   %.0f Hz Dynamic: loudest sample %.1e\n", sr, m);
                check(m == 0.0, "the dynamic has no noise of its own");
            }
        }
    }

    std::printf("6. Boombox: the record ALC's static curve (LA3220: 45 dB of input from -60 dBm give 3 dB of output), 1 kHz\n");
    {
        // -60 dBm from a -35 dB V/Pa capsule = 66.8 dB SPL = -45.2 dBFS here; the range ends 45 dB higher
        const double onset = dB(splPeak(94.0 + dB(0.7746e-3 / fromDb(-35.0))));
        double outAt[2] = { 0, 0 }; std::string line;
        for (double in : { -70.0, -60.0, -50.0, onset, -35.0, -25.0, -15.0, -5.0, onset + 45.0, 3.0 })
        {
            Fx fx(44100.0f, 3, true);
            const std::vector<float> s = sine(44100.0f, 1000.0, fromDb(in), 1.5);
            const std::vector<float> out = fx.run(s);
            const double o = dB(partial(out, 44100.0f, 1000.0, 44100, out.size()));
            char b[40]; std::snprintf(b, sizeof(b), " %.1f->%.1f", in, o); line += b;
            if (in == onset) outAt[0] = o;
            if (in == onset + 45.0) outAt[1] = o;
        }
        std::printf("   dBFS in -> dBFS out:%s\n", line.c_str());
        std::printf("   at the start of the range %.2f dBFS (kRefLevel is -7.96), 45 dB higher %.2f: %.2f dB more\n", outAt[0], outAt[1], outAt[1] - outAt[0]);
        check(std::fabs(outAt[0] - dB(kRef)) < 0.5, "the ALC holds the output at reference level where its range begins");
        check(std::fabs(outAt[1] - outAt[0] - 3.0) < 0.5, "45 dB more input give 3 dB more output");
    }

    std::printf("7. Boombox: distortion under ALC (LA3220 0.5 %% typical, 2 %% max at VI = -20 dBm; BA3308 3 %% at the end of its range)\n");
    {
        const double vi20 = dB(splPeak(94.0 + dB(0.7746e-3 / fromDb(-35.0)))) + 40.0;   // -20 dBm
        Fx fx(44100.0f, 3, true);
        const std::vector<float> out = fx.run(sine(44100.0f, 1000.0, fromDb(vi20), 1.5));
        double h2 = 0, h3 = 0;
        const double t = thd(out, 44100.0f, 1000.0, 44100, out.size(), &h2, &h3);
        std::printf("   VI = -20 dBm (%.1f dBFS): THD %.2f %% (2nd %.2f %%, 3rd %.2f %%), with the capsule's own share\n", vi20, 100 * t, 100 * h2, 100 * h3);
        check(t >= 0.005 && t <= 0.03, "between 0.5 and 3 %");
    }

    std::printf("8. Boombox: attack and recovery (BA3308: C7 22 uF, R7 1 Mohm: 22 s; the loop's 42 dB need 3 dB of it)\n");
    {
        // 0.5 s of silence, 2 s of a loud tone (-5 dBFS), then a quiet probe at -55 dBFS (below the
        // range: 37 dB of record gain once the ALC has let go). The probe's level over time.
        const float sr = 44100.0f;
        std::vector<float> in((size_t)(0.5f * sr), 0.0f);
        const std::vector<float> loud = sine(sr, 1000.0, fromDb(-5.0), 2.0), probe = sine(sr, 1000.0, fromDb(-55.0), 12.0);
        in.insert(in.end(), loud.begin(), loud.end());
        const size_t end = in.size();
        in.insert(in.end(), probe.begin(), probe.end());
        Fx fx(sr, 3, true);
        const std::vector<float> out = fx.run(in);
        auto level = [&](double t0) { const size_t a = (size_t)(t0 * sr); return dB(partial(out, sr, 1000.0, a, a + (size_t)(0.05 * sr))); };
        // attack: from the loud tone's start until every 1 ms period after it peaks within 1 dB
        // of the steady peak
        auto periodPeak = [&](double t0) { double m = 0; for (size_t i = (size_t)(t0 * sr), k = 0; k < 44; ++i, ++k) m = std::fmax(m, std::fabs(out[i])); return m; };
        const double steady = level(1.5), steadyPk = periodPeak(1.5);
        double attack = 0.0, burst = 0.0;
        for (double t = 0.5; t < 1.0; t += 0.001)
        {
            burst = std::fmax(burst, periodPeak(t));
            if (std::fabs(dB(periodPeak(t) / steadyPk)) > 1.0) attack = t + 0.001 - 0.5;
        }
        const double t0 = (double)end / sr;
        const double l1 = level(t0 + 1.0), l4 = level(t0 + 4.0), fin = level(t0 + 11.0);
        double back = -1.0;
        for (double t = t0; t < t0 + 11.5; t += 0.01) if (level(t) > fin - 1.0) { back = t - t0; break; }
        const double rate = (l4 - l1) / 3.0;
        const double wantRate = 14.0 * 20.0 * std::log10(std::exp(1.0)) / 22.0;           // 14 dB per dB of detector, 8.69 dB per 22 s
        const double attStart = steady - (-5.0 + 20.0 * std::log10(kRef / splPeak(94.0 + dB(0.7746e-3 / fromDb(-35.0)))));   // the attenuation the loud tone had
        const double wantBack = (-attStart - 1.0) / wantRate;
        std::printf("   attack (from rest, until within 1 dB): %.0f ms, with a burst up to %.2f (steady peak %.2f; estimate: a 5 ms detector)\n", 1000.0 * attack, burst, steadyPk);
        std::printf("   recovery: %.2f dB/s between 1 and 4 s after (from the RC: %.2f), within 1 dB of full gain after %.2f s (from the RC: %.2f s for %.1f dB)\n",
                    rate, wantRate, back, wantBack, -attStart);
        check(attack > 0.0 && attack < 0.02, "a fast attack: within 20 ms");
        check(std::fabs(rate / wantRate - 1.0) < 0.05, "the gain comes back at the rate the 22 uF / 1 Mohm detector sets, within 5 %");
        check(std::fabs(back / wantBack - 1.0) < 0.1, "and has come back when that rate says, within 10 %");
    }

    std::printf("9. Boombox: the noise comes up in a pause (2 s of a tone at -10 dBFS, then silence, Noise switch on)\n");
    {
        const float sr = 44100.0f;
        std::vector<float> in = sine(sr, 440.0, fromDb(-10.0), 2.0);
        in.resize(in.size() + (size_t)(12.0f * sr), 0.0f);
        double early = 0, late = 0, cam = 0;
        for (int t : {3, 0})
        {
            Fx fx(sr, t, true, true);
            const std::vector<float> out = fx.run(in);
            const size_t p = (size_t)(2.0f * sr);
            if (t == 3)
            {
                early = bandRms(out, sr, p + (size_t)(0.1f * sr), p + (size_t)(0.7f * sr), 20.0, 20000.0, true);
                late  = bandRms(out, sr, p + (size_t)(11.0f * sr), out.size(), 20.0, 20000.0, true);
            }
            else cam = bandRms(out, sr, p + (size_t)(11.0f * sr), out.size(), 20.0, 20000.0, true);
        }
        // what it should come to: the capsule's 26 dB SPL(A) at the 37.2 dB record gain through the
        // tape band (second-order 50 Hz and 10 kHz), plus the tape's hiss 45 dB under reference level
        auto bandGain = [](double f, bool a) {
            const double s = f / 10000.0, h = f / 50.0;
            const double lp = 1.0 / std::sqrt(1.0 + s * s * s * s), hp = h * h / std::sqrt(1.0 + h * h * h * h);
            return lp * hp * (a ? aWeight(f) : 1.0); };
        double aw = 0, ab = 0, flat = 0;
        for (double f = 20.0; f < 20000.0; f += 1.0) { aw += aWeight(f) * aWeight(f); ab += bandGain(f, true) * bandGain(f, true); }
        for (double f = 1.0; f < 22050.0; f += 1.0) flat += bandGain(f, false) * bandGain(f, false);
        const double recGain = kRef / splPeak(94.0 + dB(0.7746e-3 / fromDb(-35.0)));
        const double capsule = splRms(26.0) * recGain * std::sqrt(ab / aw);
        const double hiss = kRef / std::sqrt(2.0) * fromDb(-45.0) * std::sqrt(ab / flat);
        const double want = std::sqrt(capsule * capsule + hiss * hiss);
        std::printf("   just after the tone %.1f dB(A), 11 s later %.1f dB(A) (capsule noise through the full record gain %.1f, plus tape hiss: %.1f); the Camcorder in the same pause %.1f\n",
                    dB(early), dB(late), dB(capsule), dB(want), dB(cam));
        std::printf("   (just after the tone the ALC still holds the capsule's noise about 30 dB down: what is heard is the tape's hiss, %.1f)\n", dB(hiss));
        check(std::fabs(dB(early / hiss)) < 1.5, "just after the tone: the tape's hiss, within 1.5 dB");
        check(dB(late / early) > 4.0 && std::fabs(dB(late / want)) < 1.5, "once the ALC has let go: the capsule's noise at full record gain on top of it, within 1.5 dB");
    }

    std::printf("10. Phone: the carbon transmitter (2nd harmonic and sensitivity rising with level: amounts are estimates)\n");
    {
        Fx fx(44100.0f, 2, true);
        const std::vector<float> out = fx.run(sine(44100.0f, 1000.0, kRef, 0.75));
        double h2 = 0, h3 = 0;
        thd(out, 44100.0f, 1000.0, 17640, out.size(), &h2, &h3);
        // 3 % at the transmitter; the P.48 curve then lifts 2 kHz by 4.0 dB against 1 kHz
        const double want = 0.03 * fromDb(4.0);
        std::printf("   1 kHz at reference level: 2nd harmonic %.2f %% (3 %% at the transmitter, +4.0 dB by the curve at 2 kHz: %.2f %%), 3rd %.3f %%\n", 100 * h2, 100 * want, 100 * h3);
        check(std::fabs(h2 / want - 1.0) < 0.1 && h3 < 0.25 * h2, "second harmonic as set, and the main product");
        const double g0 = gainDb(44100.0f, 2, 1000.0, kRef), g20 = gainDb(44100.0f, 2, 1000.0, kRef * 0.1), g60 = gainDb(44100.0f, 2, 1000.0, kRef * 0.001);
        std::printf("   gain at reference level %+.2f dB, 20 dB below %+.2f dB, 60 dB below %+.2f dB\n", g0, g20, g60);
        check(std::fabs(g20 - g0 + 5.0) < 0.5, "20 dB below reference it comes out 25 dB lower (1 : 1.25)");
        check(std::fabs(g60 - g0 + 10.0) < 0.5, "and never more than 10 dB down");
    }

    std::printf("11. The Camcorder (one built-in mic, one linear track), the Phone (one line) and the Boombox (Sharp QT-27: \"BUILT-IN MIC (MONO)\") are mono\n");
    for (int t : {0, 2, 3})
    {
        Fx fx(44100.0f, t, true, true);
        std::vector<float> L(44100), R(44100, 0.0f);
        for (size_t i = 0; i < L.size(); ++i) L[i] = (float)(0.3 * std::sin(kPi2 * 700.0 * (double)i / 44100.0));
        for (size_t pos = 0; pos < L.size(); pos += 128) { float* io[2] = { L.data() + pos, R.data() + pos }; fx.e->processReplacing(fx.e, io, io, (VstInt32)std::min<size_t>(128, L.size() - pos)); }
        double l = 0, r = 0, d = 0;
        for (size_t i = 22050; i < L.size(); ++i) { l += (double)L[i] * L[i]; r += (double)R[i] * R[i]; d = std::fmax(d, std::fabs(L[i] - R[i])); }
        std::printf("   %s, a tone in the left channel only: left %.1f dB, right %.1f dB, largest difference %.1e\n", kNames[t], 10 * std::log10(l / 22050), 10 * std::log10(r / 22050), d);
        check(d < 1e-6, std::string(kNames[t]) + ": both channels the same (to the rounding of the mic's crossfade)");
    }

    std::printf("12. With the Mic off nothing changes; with the Noise switch off silence stays silent\n");
    {
        const std::vector<float> in = sine(44100.0f, 300.0, 0.5, 1.0);
        Fx a(44100.0f, 0, false), b(44100.0f, 3, false, false, 12.0f);
        check(a.run(in) == b.run(in), "Mic off: Mic Type and Mic Gain make no difference, bit for bit");
        for (int t = 0; t < 4; ++t)
        {
            Fx fx(44100.0f, t, true, false, 12.0f);
            const std::vector<float> out = fx.run(std::vector<float>(44100 * 3, 0.0f));
            double m = 0; for (float s : out) m = std::fmax(m, std::fabs(s));
            check(m == 0.0, std::string(kNames[t]) + ", Mic Gain +12 dB, Noise off: silence in, silence out");
        }
    }

    std::printf("13. Camcorder: the ALC's static curve, 1 kHz (TRV900 measured 1 : 19 above its knee; range 22 dB, Sanyo VM-RZ1P;\n"
                "    knee at -66 dBs = %.1f dB SPL = %.1f dBFS peak at the microphone, %.1f dBFS peak in at Mic Gain 0 with the %.0f dB distance; record gain %+.2f dB; the range ends at %.1f dB SPL)\n",
                kKneeSpl, dB(kKneePk), dB(knee(0.0)), (double)kCamDistanceDb, dB(kCamGain), kKneeSpl + 22.0);
    {
        const double ds[] = { -20.0, -10.0, 0.0, 1.6, 3.5, 6.0, 9.5, 12.0, 14.0, 20.0, 22.0, 25.0, 28.0, 31.0 };
        const double trv[][2] = { {1.6,1.1},{3.5,1.6},{6.0,2.0},{9.5,2.3},{12.0,2.4},{14.0,2.5},{20.0,2.8} };
        for (float sr : rates)
        {
            double out[14], dist[14]; std::string line;
            for (int i = 0; i < 14; ++i)
            {
                Fx fx(sr, 0, true);
                const std::vector<float> o = fx.run(sine(sr, 1000.0, knee(ds[i]), 1.5));
                const size_t a = (size_t)(1.0 * sr), b = o.size();
                out[i] = dB(partial(o, sr, 1000.0, a, b) / kKneeOut);
                dist[i] = thd(o, sr, 1000.0, a, b);
                char c[40]; std::snprintf(c, sizeof(c), " %+.1f->%+.2f", ds[i], out[i]); line += c;
            }
            std::printf("   %6.0f Hz, dB re the knee in -> out:%s\n", sr, line.c_str());
            double worst = 0.0;
            for (int i = 0; i < 11; ++i) worst = std::fmax(worst, std::fabs(out[i] - lawDb(ds[i])));
            const double slope = (out[9] - out[5]) / 14.0, past = out[13] - out[10];
            char what[200];
            std::snprintf(what, sizeof(what), "%.0f Hz: on the report's curve within 0.25 dB from 20 dB under the knee to the end of the range (worst %.2f)", sr, worst);
            check(worst <= 0.25, what);
            std::snprintf(what, sizeof(what), "%.0f Hz: 1 : 19 above the knee (%.4f dB per dB from +6 to +20; 1/19 = %.4f; the TRV900's table %.4f)", sr, slope, 1.0 / 19.0, (2.8 - 2.0) / 14.0);
            check(std::fabs(slope - 1.0 / 19.0) < 0.006, what);
            std::snprintf(what, sizeof(what), "%.0f Hz: the end of the range (+22 dB) comes out at kRefLevel within 0.25 dB (%+.2f dB)", sr, out[10] + dB(kKneeOut / kRef));
            check(std::fabs(out[10] + dB(kKneeOut / kRef)) < 0.25, what);
            std::snprintf(what, sizeof(what), "%.0f Hz: past it the output follows the input again (9 dB more in: %+.2f dB out), and distorts: THD %.2f %% at the end, %.2f %% 9 dB past",
                          sr, past, 100 * dist[10], 100 * dist[13]);
            check(past > 8.0 && dist[10] < 0.005 && dist[13] > 2.0 * dist[10], what);
            if (sr == 44100.0f)
            {
                std::string t;
                for (const auto& p : trv) for (int i = 0; i < 14; ++i) if (ds[i] == p[0]) { char c[48]; std::snprintf(c, sizeof(c), " %+.1f: %+.2f/%+.1f", p[0], out[i], p[1]); t += c; }
                std::printf("   against the TRV900's table (in re the knee: model/measured, dB re the knee's output):%s\n", t.c_str());
                std::printf("   (the TRV900's knee is soft: near it the hard knee lies up to 1.7 dB under the table, as the report says; above +6 dB the slopes agree)\n");
            }
        }
    }

    std::printf("14. Camcorder: attack and recovery (both estimates: a 20 ms detector charge, a 19 s discharge: %.2f dB/s back with 18 dB per dB)\n", kRecDbS);
    {
        for (float sr : rates)
        {
            // 1 s at the knee, 2 s 20 dB above it (inside the range), then 6 s of a probe 10 dB under
            // the knee (full gain once the ALC has let go). Noise off.
            std::vector<float> in = sine(sr, 1000.0, knee(0.0), 1.0);
            const std::vector<float> loud = sine(sr, 1000.0, knee(20.0), 2.0, kPi2 * 1000.0 * 1.0), probe = sine(sr, 1000.0, knee(-10.0), 6.0, kPi2 * 1000.0 * 3.0);
            in.insert(in.end(), loud.begin(), loud.end());
            in.insert(in.end(), probe.begin(), probe.end());
            Fx fx(sr, 0, true);
            const std::vector<float> out = fx.run(in);
            // the same in the report's terms, worked out in double: record gain, the cubic after
            // the gain element, a peak detector charging with 20 ms and discharging with 19 s,
            // gain (vc / threshold) ^ -18 at least -20.84 dB
            std::vector<double> ref(in.size());
            {
                const double atk = 1.0 - std::exp(-1.0 / (0.020 * sr)), rel = std::exp(-1.0 / (19.0 * sr)), floor = fromDb(-kAttDb), c = 0.06665;
                double vc = 0.0, g = 1.0;
                for (size_t i = 0; i < in.size(); ++i)
                {
                    double y = std::fmax(-2.2364, std::fmin(2.2364, (double)in[i] / kDist * kCamGain * g));
                    y -= c * y * y * y;
                    vc *= rel;
                    if (std::fabs(y) > vc) vc += atk * (std::fabs(y) - vc);
                    g = vc > kKneeOut ? std::fmax(std::pow(vc / kKneeOut, -18.0), floor) : 1.0;
                    ref[i] = y;
                }
            }
            const size_t ms = (size_t)(0.001 * sr);
            auto pkOut = [&](double t0) { double m = 0; for (size_t i = (size_t)(t0 * sr), k = 0; k < ms; ++i, ++k) m = std::fmax(m, std::fabs(out[i])); return m; };
            auto pkRef = [&](double t0) { double m = 0; for (size_t i = (size_t)(t0 * sr), k = 0; k < ms; ++i, ++k) m = std::fmax(m, std::fabs(ref[i])); return m; };
            const double steady = pkOut(2.8), steadyRef = pkRef(2.8);
            double attack = 0.0, attackRef = 0.0, burst = 0.0, burstRef = 0.0;
            for (double t = 1.0; t < 1.5; t += 0.001)
            {
                burst = std::fmax(burst, pkOut(t)); burstRef = std::fmax(burstRef, pkRef(t));
                if (std::fabs(dB(pkOut(t) / steady)) > 1.0) attack = t + 0.001 - 1.0;
                if (std::fabs(dB(pkRef(t) / steadyRef)) > 1.0) attackRef = t + 0.001 - 1.0;
            }
            auto level = [&](double t0) { const size_t a = (size_t)(t0 * sr); return dB(partial(out, sr, 1000.0, a, a + (size_t)(0.05 * sr))); };
            const double t0 = 3.0, l1 = level(t0 + 0.5), l2 = level(t0 + 1.5), fin = level(t0 + 5.5);
            double back = -1.0;
            for (double t = t0; t < t0 + 5.5; t += 0.01) if (level(t) > fin - 1.0) { back = t - t0; break; }
            const double rate = l2 - l1, att = 20.0 * 18.0 / 19.0, wantBack = (att - 1.0) / kRecDbS;
            std::printf("   %6.0f Hz: onset 20 dB: back within 1 dB after %.0f ms (the report's detector: %.0f ms), overshoot to %+.1f dB (%+.1f); recovery %.2f dB/s, within 1 dB of full gain after %.2f s (%.2f s for %.1f dB)\n",
                        sr, 1000.0 * attack, 1000.0 * attackRef, dB(burst / steady), dB(burstRef / steadyRef), rate, back, wantBack, att);
            char what[160];
            std::snprintf(what, sizeof(what), "%.0f Hz: the onset as the report's 20 ms detector gives it, within 15 %% (and 1 dB of overshoot)", sr);
            check(std::fabs(attack / attackRef - 1.0) < 0.15 && std::fabs(dB(burst / steady) - dB(burstRef / steadyRef)) < 1.0, what);
            std::snprintf(what, sizeof(what), "%.0f Hz: the gain comes back at the rate the 19 s detector sets, within 5 %%, and has come back when that rate says, within 10 %%", sr);
            check(std::fabs(rate / kRecDbS - 1.0) < 0.05 && std::fabs(back / wantBack - 1.0) < 0.1, what);
        }
    }

    std::printf("15. Camcorder: the capsule's noise comes up in a gap (2 s of a tone 20 dB above the knee, then silence, Noise switch on)\n");
    {
        for (float sr : rates)
        {
            std::vector<float> in = sine(sr, 1000.0, knee(20.0), 2.0);
            in.resize(in.size() + (size_t)(6.0f * sr), 0.0f);
            Fx fx(sr, 0, true, true);
            const std::vector<float> out = fx.run(in);
            const size_t p = (size_t)(2.0f * sr);
            // 0.1 to 0.3 s after the tone (middle 0.2 s: the ALC still 18.95 - 0.2 x 8.23 dB down), 5 to 6 s after (let go)
            const double early = bandRms(out, sr, p + (size_t)(0.1f * sr), p + (size_t)(0.3f * sr), 20.0, 20000.0, true);
            const double late  = bandRms(out, sr, p + (size_t)(5.0f * sr), out.size(), 20.0, 20000.0, true);
            const double full = camNoise(sr), held = 20.0 * 18.0 / 19.0 - 0.2 * kRecDbS;
            std::printf("   %6.0f Hz: 0.2 s after the tone %.1f dB(A) (the noise at full gain %.1f, %.1f dB down: %.1f), 5 to 6 s after %.1f; it rose %.1f dB; the tone came out at %.1f dBFS rms\n",
                        sr, dB(early), dB(full), held, dB(full) - held, dB(late), dB(late / early), dB(kKneeOut * fromDb(20.0 / 19.0) / std::sqrt(2.0)));
            char what[160];
            std::snprintf(what, sizeof(what), "%.0f Hz: held down by what the ALC's gain says, within 1.5 dB, then at the full record gain within 1 dB", sr);
            check(std::fabs(dB(early / full) + held) < 1.5 && std::fabs(dB(late / full)) < 1.0, what);
        }
    }

    std::printf("16. How far from bypass: a music-like signal at -18 dBFS rms (chord, kick, hi-hat; loud and 15 dB quieter parts; a 2 s gap), the mic alone\n"
                "    crest = peak / rms; loud-quiet = the level difference of the two parts; spread = standard deviation of the 50 ms level;\n"
                "    gap = noise in the gap's last half second re the loud part (Noise on); THD = a 1 kHz sine at -18 dBFS rms at the same setting.\n"
                "    The new one hears the source %.0f dB quieter (the camera across the room) before Mic Gain's own change\n", (double)kCamDistanceDb);
    {
        const float sr = 44100.0f;
        const std::vector<float> in = music(sr);
        const Dyn by = dynamics(in, sr);
        std::printf("   %-34s crest %5.1f dB, loud-quiet %5.1f dB, spread %4.1f dB, gap %s\n", "bypass (the input)", by.crest, by.loudQuiet, by.spread, "silent");
        auto row = [&](const char* name, double gainDb, bool old) {
            Dyn d = dynamics(micOnly(in, sr, gainDb, false, old), sr);
            d.gap = dynamics(micOnly(in, sr, gainDb, true, old), sr).gap;
            const std::vector<float> t = micOnly(sine(sr, 1000.0, fromDb(-15.0), 2.0), sr, gainDb, false, old);
            const double dist = thd(t, sr, 1000.0, (size_t)sr, t.size());
            std::printf("   %-34s crest %5.1f dB, loud-quiet %5.1f dB, spread %4.1f dB, gap %6.1f dB, THD %5.2f %%; change from bypass: crest %+5.1f, loud-quiet %+5.1f, spread %+5.1f\n",
                        name, d.crest, d.loudQuiet, d.spread, d.gap, 100 * dist, d.crest - by.crest, d.loudQuiet - by.loudQuiet, d.spread - by.spread);
            return d;
        };
        const Dyn o0 = row("replaced (capsule alone), 0 dB", 0.0, true);
        row("replaced (capsule alone), -12 dB", -12.0, true);
        const Dyn p12 = row("new, Mic Gain +12 dB", 12.0, false);
        row("new, Mic Gain +6 dB", 6.0, false);
        row("new, Mic Gain 0 dB", 0.0, false);
        row("new, Mic Gain -6 dB", -6.0, false);
        row("new, Mic Gain -12 dB", -12.0, false);
        // where this music is held inside the range: 26 dB under its level at the microphone
        // without the distance (its peaks stand 18 dB over its rms), wherever the distance puts that
        const double inside = (double)kCamDistanceDb - 26.0;
        char nm[48]; std::snprintf(nm, sizeof(nm), "new, Mic Gain %+.0f dB (held)", inside);
        const Dyn in26 = row(nm, inside, false);
        const double oldMove = std::fmax(std::fabs(o0.loudQuiet - by.loudQuiet), std::fabs(o0.spread - by.spread));
        check(oldMove < 0.5 && std::fabs(o0.crest - by.crest) < 1.5,
              "the model it replaces leaves the level over time within 0.5 dB and the crest within 1.5 dB (its 53 Hz highpass on the kick) (the owner: \"You can barely tell it's on\")");
        check(p12.crest < by.crest - 2.0, "the new one at Mic Gain +12 dB: past the ALC's range, the peaks come down by at least 2 dB");
        check(in26.loudQuiet < by.loudQuiet - 6.0 && in26.spread < 0.6 * by.spread && in26.gap > -50.0,
              "with the music 26 dB under its level at the microphone, inside the range: at least 6 dB less between loud and quiet, under 0.6 of the spread, the noise lifted in the gap to within 50 dB of the loud part");
    }

    std::printf("17. The two programs with the Camcorder (Camcorder 89, Wedding Video): where typical material lands (the mic alone, 1 kHz)\n"
                "    the range ends at %.1f dBFS peak in at Mic Gain 0 (%.1f dB SPL at the microphone, %.0f dB of distance); typical: the levels_test tone (-12 dBFS peak) and a sine at -18 dBFS rms\n",
                dB(knee(22.0)), kKneeSpl + 22.0, (double)kCamDistanceDb);
    {
        const float sr = 44100.0f;
        auto at = [&](double gainDb, double peakDbfs, double* past, double* g, double* dist, double* opk) {
            MicModel m;
            const std::vector<float> o = micOnly(sine(sr, 1000.0, fromDb(peakDbfs), 1.5), sr, gainDb, false, false, &m);
            *past = peakDbfs + gainDb - dB(knee(22.0));   // knee() has the distance in
            *g = dB(m.alcG);
            *dist = thd(o, sr, 1000.0, (size_t)sr, o.size());
            *opk = 0; for (size_t i = (size_t)sr; i < o.size(); ++i) *opk = std::fmax(*opk, std::fabs(o[i]));
        };
        for (double gainDb : {12.0, 6.0, 2.0, 1.0, 0.0, -6.0, -12.0})
            for (double pk : {-12.0, -15.0})
            {
                double past, g, dist, opk;
                at(gainDb, pk, &past, &g, &dist, &opk);
                std::printf("   Mic Gain %+5.1f dB, %s: %+5.1f dB past the end of the range, ALC gain %+6.2f dB, out peak %.2f, THD %5.2f %%\n",
                            gainDb, pk == -12.0 ? "-12 dBFS peak" : "-18 dBFS rms ", past, g, opk, 100 * dist);
            }
        std::printf("   a -18 dBFS rms sine stays inside the range up to Mic Gain %+.1f dB, the levels_test tone up to %+.1f dB\n", dB(knee(22.0)) - (-15.0), dB(knee(22.0)) - (-12.0));
        for (int prog : {4, 15})
        {
            Fx fx(sr, 0, true);
            fx.e->dispatcher(fx.e, effSetProgram, 0, prog, nullptr, 0.0f);
            char name[64] = {0};
            fx.e->dispatcher(fx.e, effGetProgramNameIndexed, prog, 0, name, 0.0f);
            mpc::Plugin* pl = static_cast<mpc::Plugin*>(fx.e->object);
            const double g = pl->get(kMicGain);
            double past1, g1, d1, p1, past2, g2, d2, p2;
            at(g, -12.0, &past1, &g1, &d1, &p1);
            at(g, -15.0, &past2, &g2, &d2, &p2);
            std::printf("   %-14s Mic %s, type %d, Mic Gain %+.1f dB, AGC %.0f %%: the tone %+.1f dB past the range (THD %.2f %%), the sine %+.1f dB past (THD %.2f %%)\n",
                        name, pl->getBool(kMicOn) ? "on" : "off", pl->getInt(kMicType), g, pl->get(kAgc), past1, 100 * d1, past2, 100 * d2);
            check(pl->getInt(kMicType) == 0 && std::fabs(g - (prog == 4 ? 2.0 : 1.0)) < 0.01 && past1 <= 4.0 && past2 <= 4.0 && d1 < 0.01 && d2 < 0.01 && p1 < 1.0,
                  std::string(name) + ": its original Mic Gain; with the distance, typical material at most 4 dB past the top of the ALC's range, not clipping (THD under 1 %)");
        }
    }

    std::printf("18. Camcorder: its ALC starts from rest unheard (it settles for 100 ms before the mic fades in), switched on or chosen while a tone plays\n");
    {
        for (float sr : { 44100.0f, 192000.0f })
        {
            std::string line; double worst = -100.0;
            for (double d : {1.0, 2.2, 5.0, 10.0, 20.0, 30.0})
                for (int how = 0; how < 2; ++how)
                {
                    // 0.5 s with the Mic off (or on Dynamic), then the Mic on (or Camcorder chosen); the
                    // loudest millisecond after that against the steady level 2 s later
                    Fx fx(sr, how ? 1 : 0, how == 1);
                    const std::vector<float> a = sine(sr, 1000.0, knee(d), 0.5), b = sine(sr, 1000.0, knee(d), 2.5, kPi2 * 1000.0 * 0.5);
                    fx.run(a);
                    if (how) fx.set(kMicType, 0.0f); else fx.set(kMicOn, 1.0f);
                    const std::vector<float> o = fx.run(b);
                    const size_t ms = (size_t)(0.001 * sr);
                    double steady = 0, most = 0;
                    for (size_t i = (size_t)(2.0 * sr); i < (size_t)(2.0 * sr) + 20 * ms; ++i) steady = std::fmax(steady, std::fabs(o[i]));
                    for (size_t i = 0; i < (size_t)(2.0 * sr); ++i) most = std::fmax(most, std::fabs(o[i]));
                    worst = std::fmax(worst, dB(most / steady));
                    char c[48]; std::snprintf(c, sizeof(c), " %+.0f%s:%+.2f", d, how ? "t" : "m", dB(most / steady)); line += c;
                }
            std::printf("   %6.0f Hz, dB above the knee (m: Mic switched on, t: type chosen), loudest after it re steady:%s\n", sr, line.c_str());
            char what[120]; std::snprintf(what, sizeof(what), "%.0f Hz: no start-up burst: never more than 1 dB above the steady level (worst %+.2f)", sr, worst);
            check(worst < 1.0, what);
        }
    }

    std::printf("19. Camcorder: after a sound louder than the ALC's range its detector holds more than full attenuation needs, so the gain\n"
                "    starts to come back only once that has drained (19 s discharge). How long after the sound ends, the mic alone, then 40 s of silence\n");
    {
        const float sr = 44100.0f;
        auto after = [&](std::vector<float> in, double gainDb, double* start, double* full, double* endG) {
            const size_t end = in.size();
            in.resize(end + (size_t)(40.0f * sr), 0.0f);
            MicModel m; m.set(0, sr); m.reset();
            const float g = (float)fromDb(gainDb);
            double lo = 1e9; *start = -1.0; *full = -1.0;
            for (size_t i = 0; i < in.size(); ++i)
            {
                m.process(splat(in[i] * g), 0.0f);
                if (i == end) *endG = dB(m.alcG);
                if (i < end) continue;
                const double gd = dB(m.alcG);
                lo = std::fmin(lo, gd);
                if (*start < 0.0 && gd > lo + 1.0) *start = std::fmax(0.0, (double)(i - end) / sr - 1.0 / kRecDbS);   // back to where it rose its first dB
                if (*full < 0.0 && gd > -1.0) *full = (double)(i - end) / sr;
            }
        };
        struct Case { const char* name; std::vector<float> in; double gain; };
        const std::vector<float> mus = music(sr), tone = sine(sr, 1000.0, 0.25, 4.0);
        const Case cases[] = { {"music, Mic Gain 0", mus, 0.0}, {"music, Mic Gain +12", mus, 12.0},
                               {"levels_test tone, Mic Gain 0", tone, 0.0}, {"levels_test tone, Mic Gain +12", tone, 12.0} };
        for (const Case& c : cases)
        {
            double start, full, endG;
            after(c.in, c.gain, &start, &full, &endG);
            std::printf("   %-31s gain at the end %+6.2f dB; it starts to come back %5.2f s after, is within 1 dB of full gain %5.2f s after\n", c.name, endG, start, full);
            check(full > 0.0 && full < 40.0, std::string(c.name) + ": the gain comes all the way back");
        }
    }

    std::printf("20. The mono types run their filters as MonoBiquad (the state in the two halves of one vector): each filter of the\n"
                "    Camcorder, the Phone and the Boombox gives PairBiquad's output bit for bit (noise, a loud tone, then silence)\n");
    {
        int filters = 0, differ = 0;
        for (float sr : rates)
            for (int t : { (int)MicModel::kCamcorder, (int)MicModel::kPhone, (int)MicModel::kBoombox })
            {
                MicModel m;
                m.set(t, sr);
                m.reset();
                for (int i = 0; i < m.n; ++i, ++filters)
                {
                    PairBiquad p = m.f[i];
                    MonoBiquad q = m.mf[i];
                    uint32_t s = 1;
                    bool same = true;
                    for (int k = 0; k < 30000; ++k)
                    {
                        s = s * 1664525u + 1013904223u;
                        const float x = k < 10000 ? (float)((s >> 8) * (2.0 / 16777216.0) - 1.0)
                                      : k < 20000 ? (float)(0.9 * std::sin(kPi2 * 3000.0 * k / sr)) : 0.0f;
                        const float a = p.process(splat(x))[0], b = q.process(x);
                        if (std::memcmp(&a, &b, sizeof(float)) != 0) same = false;   // (tells -0 from +0 too)
                    }
                    if (!same) ++differ;
                }
            }
        std::printf("   %d filters, %d differ\n", filters, differ);
        check(differ == 0 && filters == 5 * (4 + 8 + 4), "MonoBiquad is PairBiquad on a mono signal, to the bit");
    }

    std::printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
