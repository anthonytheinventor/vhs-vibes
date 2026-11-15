// VHS Vibes 1.6: the four microphones (Mic Type), measured through the plugin against the
// published figures they are built from (see MicModel in the source, and the manual's chapter
// on them). Every case starts from the Init program (neutral), HQ off, and compares the output
// with the Mic on against the same instance's output with it off, so only the mic is measured.
// Only the plugin's parameters are used, so the file builds against older versions too (1.5.3
// fails most of it). Ends with PASS or FAIL.
//
// The level mapping the models use (an assumption, stated in the manual): a sine peaking at
// kRefLevel (-8 dBFS) is 104 dB SPL, so 94 dB SPL peaks at -18 dBFS.
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

    std::printf("3. Camcorder: the electret capsule. Gate highpass (Kean: 100 Mohm, 30 pF, first order), WM-61A HF rise (read off Neal's plot), no lowpass\n");
    {
        // first order at 53.05 Hz: -3.0 dB there, -9.1 dB at 20 Hz, -0.5 dB at 150 Hz; the
        // rise: flat within 0.3 dB from 300 Hz to 5 kHz, +1 at 8 kHz, +1.5 to +3 at 13 to 17 kHz
        // (the units' spread: +2.25 +/- 0.75), +1 to -1 at 21.5 kHz
        const double pts[11][3] = { {20,-9.1,0.5},{53.05,-3.0,0.5},{150,-0.5,0.5},{300,0,0.3},{3000,0,0.3},{5000,0,0.3},{8000,1.0,0.5},{13000,2.25,0.75},{15000,2.25,0.75},{17000,2.25,0.75},{21500,0,1.0} };
        for (float sr : rates)
        {
            double worst = 0.0; std::string line; bool ok = true;
            for (const auto& p : pts)
            {
                if (p[0] > 0.45 * sr) continue;
                const double got = respDb(sr, 0, p[0], 0.1);
                char b[40]; std::snprintf(b, sizeof(b), " %g:%+.2f/%+.2f", p[0], got, p[1]); line += b;
                worst = std::fmax(worst, std::fabs(got - p[1]));
                if (std::fabs(got - p[1]) > p[2]) ok = false;
            }
            std::printf("   %6.0f Hz:%s\n", sr, line.c_str());
            char what[160]; std::snprintf(what, sizeof(what), "%.0f Hz: within 0.5 dB at the highpass and at 8 kHz, 0.3 dB in the flat part, inside the spread at 13 to 17 kHz, 1 dB at 21.5 kHz", sr);
            check(ok, what);
        }
    }

    std::printf("4. Camcorder: the JFET's overload (Kean: above 110 dB SPL, negative peaks first), 1 kHz\n");
    {
        double thd104 = 0, thd110 = 0;
        for (double spl : {104.0, 107.0, 110.0, 113.0, 116.0})
        {
            Fx fx(44100.0f, 0, true);
            const std::vector<float> in = sine(44100.0f, 1000.0, splPeak(spl), 0.75);
            const std::vector<float> out = fx.run(in);
            double h2 = 0, h3 = 0;
            const double t = thd(out, 44100.0f, 1000.0, 17640, out.size(), &h2, &h3);
            // the waveform's two peaks around its mean (the stage's DC is left in)
            double mean = 0; for (size_t i = 22050; i < out.size(); ++i) mean += out[i];
            mean /= (double)(out.size() - 22050);
            double hi = 0, lo = 0; for (size_t i = 22050; i < out.size(); ++i) { hi = std::fmax(hi, out[i] - mean); lo = std::fmin(lo, out[i] - mean); }
            std::printf("   %.0f dB SPL (peak %.3f): THD %.2f %% (2nd %.2f %%, 3rd %.2f %%), positive peak %.4f, negative %.4f\n", spl, splPeak(spl), 100 * t, 100 * h2, 100 * h3, hi, -lo);
            if (spl == 104.0) thd104 = t;
            if (spl == 110.0) { thd110 = t; check(-lo < hi, "the negative half gives way first"); check(h2 > 0.5 * h3, "so there is second harmonic as well as third"); }
        }
        check(std::fabs(thd110 - 0.01) < 0.0015, "1 % THD at 110 dB SPL (taken as where overload begins)");
        check(thd104 < 0.003, "clean at reference level (104 dB SPL)");
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
                const double want = splRms(26.0);
                std::printf("   %.0f Hz Camcorder: %.1f dB(A) re full scale, %.1f dB SPL(A); the WM-61A figure 26 dB SPL(A) is %.1f\n", sr, dB(a), 104.0 + dB(a / splRms(104.0)), dB(want));
                check(std::fabs(dB(a / want)) < 1.0, "the capsule's self-noise within 1 dB of 26 dB SPL(A)");
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

    std::printf("11. With the Mic off nothing changes; with the Noise switch off silence stays silent\n");
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

    std::printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
