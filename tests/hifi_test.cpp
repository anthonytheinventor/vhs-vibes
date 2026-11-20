// VHS Vibes 1.7: Sat Type = Hi-Fi, the FM track, measured against the figures it is built to (see
// HiFiPath in the source). Ends with PASS or FAIL.
//   hifi_test [state file]   the state file holds a state saved by VHS Vibes 1.6.0 with Sat Type on
//                            VHS: run.sh writes it with this same file built against the 1.6.0
//                            source with -DWRITE160 (then main() only writes that state).
#ifndef SRC
#define SRC "../src/VHSVibes.cpp"
#endif
#include SRC

#include <algorithm>
#include <chrono>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) { return opcode == audioMasterVersion ? 2400 : 0; }

#ifdef WRITE160
// The 1.6.0 side: a state with Sat Type = VHS and its three knobs off their defaults.
int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    AEffect* e = VSTPluginMain(hostCb);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
    mpc::Plugin* p = static_cast<mpc::Plugin*>(e->object);
    p->set(kSatType, 1.0f); p->set(kSatOn, 1.0f); p->set(kLoSat, 70.0f); p->set(kHiSat, 25.0f); p->set(kSplit, 3000.0f); p->set(kHiss, 12.0f);
    void* data = nullptr;
    const size_t n = (size_t)e->dispatcher(e, effGetChunk, 0, 0, &data, 0.0f);
    FILE* f = std::fopen(argv[1], "wb");
    if (f == nullptr || std::fwrite(data, 1, n, f) != n) return 1;
    std::fclose(f);
    e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f); e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);
    return 0;
}
#else

static double dB(double x) { return 20.0 * std::log10(x > 1e-30 ? x : 1e-30); }
static const double kTau = 6.283185307179586;

// amplitude of `hz` in v (by correlation; v should hold whole periods); v[0] is sample `first`
static double amp(const std::vector<float>& v, double sr, double hz, long first = 0)
{
    double a = 0, b = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double ph = kTau * hz * (double)((long)i + first) / sr;
        a += v[i] * std::sin(ph); b += v[i] * std::cos(ph);
    }
    return 2.0 * std::sqrt(a * a + b * b) / (double)v.size();
}
static int wholePeriods(double sr, double hz, double seconds) { return (int)std::lround(std::floor(seconds * hz) * sr / hz); }

static HiFiPath::Settings clean() { HiFiPath::Settings s; s.headGate = 1.0f; return s; }   // the plugin with every Hi-Fi knob at 0, Noise on
static void start(HiFiPath& h, float sr, const HiFiPath::Settings& s) { h.prepare(sr); h.set(s); h.reset(); }
// the channel alone (with its control tick), for a modulation y
static Stereo chan(HiFiPath& h, Stereo y) { if (h.n == 0) h.tick(); h.n = (h.n + 1) & (kCtrl - 1); return h.channel(y); }

struct Tone { double gainDb, yDb, yPeak; };
// a sine at `hz`, `levelDb` re reference, through the whole path: gain, the level on the tape (re reference), its peak
static Tone pathTone(float sr, const HiFiPath::Settings& s, double hz, double levelDb, double settle = 0.6, double meas = 0.4)
{
    HiFiPath h; start(h, sr, s);
    const double a = kRefLevel * std::pow(10.0, levelDb / 20.0);
    const long ns = (long)(settle * sr), nm = wholePeriods(sr, hz, meas);
    std::vector<float> out, y; out.reserve((size_t)nm); y.reserve((size_t)nm);
    double peak = 0.0;
    for (long i = 0; i < ns + nm; ++i)
    {
        const float x = (float)(a * std::sin(kTau * hz * (double)i / sr));
        const Stereo o = h.process(splat(x));
        if (i >= ns) { out.push_back(o[0]); y.push_back(h.yPrev[0]); peak = std::max(peak, (double)std::fabs(h.yPrev[0])); }
    }
    return { dB(amp(out, sr, hz, ns) / a), dB(amp(y, sr, hz, ns) / kRefLevel), peak };
}

// power spectral density (Welch, Hann, 4096 points, 50 % overlap): one value per bin, power per bin
static std::vector<double> psd(const std::vector<float>& v)
{
    const int N = 4096;
    std::vector<double> acc(N / 2 + 1, 0.0), win(N);
    double wsum = 0.0;
    for (int i = 0; i < N; ++i) { win[(size_t)i] = 0.5 - 0.5 * std::cos(kTau * i / N); wsum += win[(size_t)i] * win[(size_t)i]; }
    int frames = 0;
    std::vector<std::complex<double>> buf(N);
    for (size_t at = 0; at + N <= v.size(); at += N / 2, ++frames)
    {
        for (int i = 0; i < N; ++i) buf[(size_t)i] = v[at + (size_t)i] * win[(size_t)i];
        for (int i = 1, j = 0; i < N; ++i) { int bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(buf[(size_t)i], buf[(size_t)j]); }
        for (int len = 2; len <= N; len <<= 1)
        {
            const std::complex<double> wl = std::polar(1.0, -kTau / len);
            for (int i = 0; i < N; i += len)
            {
                std::complex<double> w = 1.0;
                for (int j = 0; j < len / 2; ++j, w *= wl)
                {
                    const std::complex<double> u = buf[(size_t)(i + j)], t = w * buf[(size_t)(i + j + len / 2)];
                    buf[(size_t)(i + j)] = u + t; buf[(size_t)(i + j + len / 2)] = u - t;
                }
            }
        }
        for (int k = 0; k <= N / 2; ++k) acc[(size_t)k] += std::norm(buf[(size_t)k]) * (k == 0 || k == N / 2 ? 1.0 : 2.0);
    }
    for (double& a : acc) a /= (double)frames * wsum * N;   // so that the bins add up to the mean square
    return acc;
}
static double bandPower(const std::vector<double>& p, double sr, double lo, double hi)
{
    const double df = sr / 4096.0; double s = 0.0;
    for (size_t k = 0; k < p.size(); ++k) if (k * df >= lo && k * df <= hi) s += p[k];
    return s;
}
static double bandDensity(const std::vector<double>& p, double sr, double lo, double hi)
{
    const double df = sr / 4096.0; double s = 0.0; int n = 0;
    for (size_t k = 0; k < p.size(); ++k) if (k * df >= lo && k * df <= hi) { s += p[k]; ++n; }
    return s / std::max(n, 1);
}

static float largestStep(const std::vector<float>& v, size_t from = 1)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t>(from, 1); i < v.size(); ++i) m = std::fmax(m, std::fabs(v[i] - v[i - 1]));
    return m;
}

struct Fx
{
    AEffect* e; long pos = 0; float sr;
    Fx(float rate = 44100.0f, bool hq = false, int program = 20) : sr(rate)
    {
        e = VSTPluginMain(hostCb);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, sr);
        e->dispatcher(e, effSetBlockSize, 0, 512, nullptr, 0.0f);
        set(kHq, hq ? 1.0f : 0.0f);
        e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
        e->dispatcher(e, effSetProgram, 0, program, nullptr, 0.0f);
    }
    ~Fx() { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f); e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f); }
    mpc::Plugin* plug() { return static_cast<mpc::Plugin*>(e->object); }
    void set(int i, float plain) { plug()->set(i, plain); }
    float get(int i) { return plug()->get(i); }
    // Init with the Saturator on Hi-Fi at Rec Level 0 dB: every Hi-Fi knob at 0
    void hifi() { set(kSatOn, 1); set(kSatType, 2); set(kLoSat, 100.0f * 10.0f / 18.0f); }
    // `seconds` of a sine (amp 0: silence) in calls of `block` frames; the output appended to keep (left channel)
    void play(float seconds, double ampl, double hz, std::vector<float>* keep, int block = 128, std::vector<float>* keepR = nullptr)
    {
        static float L[512], R[512]; float* io[2] = { L, R };
        const long end = pos + (long)(seconds * sr);
        while (pos < end)
        {
            const int n = (int)std::min<long>(block, end - pos);
            for (int i = 0; i < n; ++i) L[i] = R[i] = (float)(ampl * std::sin(kTau * hz * (double)(pos + i) / sr));
            pos += n;
            e->processReplacing(e, io, io, n);
            if (keep) keep->insert(keep->end(), L, L + n);
            if (keepR) keepR->insert(keepR->end(), R, R + n);
        }
    }
    std::vector<uint8_t> state()
    {
        void* data = nullptr;
        const size_t n = (size_t)e->dispatcher(e, effGetChunk, 0, 0, &data, 0.0f);
        return std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + n);
    }
    VstIntPtr load(const std::vector<uint8_t>& s) { std::vector<uint8_t> c = s; return e->dispatcher(e, effSetChunk, 0, (VstIntPtr)c.size(), c.data(), 0.0f); }
    std::string text(int i) { char b[64] = {0}; e->dispatcher(e, effGetParamDisplay, i, 0, b, 0.0f); return b; }
};

static bool subnormal(float v) { return std::fpclassify(v) == FP_SUBNORMAL; }
static bool subnormal(Stereo v) { return subnormal(v[0]) || subnormal(v[1]); }

int main(int argc, char** argv)
{
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("   %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };
    const float rates[] = { 44100.0f, 48000.0f, 88200.0f, 96000.0f };   // 88.2 kHz: the path inside HQ at 44.1 kHz

    std::printf("1. Transparent with every impairment at 0 (Carrier 0 dB, Rec Level 0 dB, Cal right), -60 to +10 dB re reference, 20 Hz to 20 kHz\n");
    {
        const double freqs[] = { 20, 50, 100, 1000, 5000, 10000, 15000, 20000 };
        double worstAll = 0.0; int skipped = 0;
        for (float sr : rates)
        {
            double worst = 0.0, wf = 0, wl = 0;
            for (double hz : freqs)
                for (int lv = -60; lv <= 10; lv += 10)
                {
                    const Tone t = pathTone(sr, clean(), hz, lv);
                    if (t.yPeak >= 0.999 * kHfClip) { ++skipped; continue; }   // the limiter: not transparent by design
                    if (std::fabs(t.gainDb) > std::fabs(worst)) { worst = t.gainDb; wf = hz; wl = lv; }
                }
            std::printf("   %6.0f Hz rate: largest deviation %+.4f dB (%.0f Hz at %+.0f dB)\n", sr, worst, wf, wl);
            worstAll = std::max(worstAll, std::fabs(worst));
        }
        std::printf("   (%d of %d tone/level pairs reach the limiter and are left out: +10 dB at 20 kHz, where it sits at +9.8 dB)\n", skipped, 4 * 8 * 8);
        check(worstAll < 0.1, "within 0.1 dB at every rate, level and frequency");
        // in the plugin, HQ on at 44.1 kHz and off at 48 and 96 kHz: the Hi-Fi output against the same plugin with the Saturator off
        double worstFx = 0.0;
        for (int r = 0; r < 3; ++r)
        {
            const float sr = r == 0 ? 44100.0f : r == 1 ? 48000.0f : 96000.0f;
            for (double hz : { 100.0, 1000.0, 10000.0, 18000.0 })
                for (double lv : { -50.0, -20.0, 0.0, 8.0 })
                {
                    if (hz >= 15000.0 && lv > 7.0) continue;   // the limiter
                    double a[2];
                    for (int k = 0; k < 2; ++k)
                    {
                        Fx fx(sr, r == 0); fx.hifi(); if (k == 1) fx.set(kSatOn, 0);
                        std::vector<float> o; fx.play(0.7f, kRefLevel * std::pow(10.0, lv / 20.0), hz, nullptr); fx.play((float)(wholePeriods(sr, hz, 0.3) / sr), kRefLevel * std::pow(10.0, lv / 20.0), hz, &o);
                        a[k] = amp(o, sr, hz, fx.pos - (long)o.size());
                    }
                    worstFx = std::max(worstFx, std::fabs(dB(a[0] / a[1])));
                }
        }
        std::printf("   in the plugin (HQ on at 44.1 kHz, off at 48 and 96 kHz), against the Saturator off: largest deviation %.4f dB\n", worstFx);
        check(worstFx < 0.1, "the same in the plugin, with HQ on and off");
    }

    std::printf("2. The 2:1 compressor (level on the tape) and unity overall, 1 kHz\n");
    {
        double worstY = 0.0, worstG = 0.0;
        for (float sr : { 44100.0f, 96000.0f })
            for (int lv = -60; lv <= 16; lv += 4)
            {
                const Tone t = pathTone(sr, clean(), 1000.0, lv);
                worstY = std::max(worstY, std::fabs(t.yDb - lv / 2.0));
                worstG = std::max(worstG, std::fabs(t.gainDb));
                if (sr == 44100.0f && lv % 20 == 0) std::printf("   in %+3d dB: on the tape %+7.2f dB (2:1 gives %+6.1f), out %+.3f dB\n", lv, t.yDb, lv / 2.0, t.gainDb);
            }
        std::printf("   largest error of the 2:1 line from -60 to +16 dB: %.3f dB; of unity overall: %.4f dB\n", worstY, worstG);
        check(worstY < 0.1, "2:1 to 0.1 dB from -60 to +16 dB (the TDA9605H's tested range)");
        check(worstG < 0.05, "and the expander undoes it");
    }

    std::printf("3. The detector's attack and recovery (the one-pole follower: 63 %% of a step in the time constant)\n");
    {
        bool ok = true;
        for (float sr : rates)
        {
            HiFiPath h; start(h, sr, clean());
            HfDetector d = h.recDet; d.d = splat(0.0f);
            int i = 0; while (d.d[0] < 0.6321206f) { d.step(splat(1.0f)); ++i; }
            const double att = 1000.0 * i / sr;
            HfDetector r = h.recDet; r.d = splat(1.0f); int k = 0; while (r.d[0] > 0.3678794f) { r.step(splat(0.0f)); ++k; }
            const double rel = 1000.0 * k / sr;
            std::printf("   %6.0f Hz rate: attack %.3f ms, recovery %.2f ms\n", sr, att, rel);
            ok = ok && std::fabs(att - 5.0) < 1000.0 / sr + 0.01 && std::fabs(rel - 70.0) < 1000.0 / sr + 0.05;
        }
        check(ok, "5 ms and 70 ms to within a sample");
        // what that does to the level in the loop (shown, not judged: the datasheets give the detector's times)
        HiFiPath h; start(h, 44100.0f, clean());
        std::vector<float> y;
        for (long i = 0; i < 44100; ++i)
        {
            const double a = kRefLevel * (i < 22050 ? 0.1 : 1.0);   // -20 dB, then 0 dB
            h.process(splat((float)(a * std::sin(kTau * 1000.0 * i / 44100.0)))); y.push_back(std::fabs(h.yPrev[0]));
        }
        auto envAt = [&](long i) { float m = 0; for (long k = i; k < i + 44; ++k) m = std::fmax(m, y[(size_t)k]); return dB(m / kRefLevel); };
        long t1 = 22050; while (t1 < 44000 && envAt(t1) > 1.0) ++t1;
        std::printf("   in the loop, a 20 dB step up at 1 kHz: the level on the tape overshoots to %+.1f dB and is within 1 dB of its 0 dB after %.1f ms\n", envAt(22050), 1000.0 * (t1 - 22050) / 44100.0);
    }

    std::printf("4. The TDA9605H's 10 kHz figures at reference level (re 1 kHz): record +3.9 dB, playback -7.7 dB\n");
    {
        bool ok = true;
        for (float sr : rates)
        {
            const double rec = pathTone(sr, clean(), 10000.0, 0.0).yDb - pathTone(sr, clean(), 1000.0, 0.0).yDb;
            double pb[2];
            for (int k = 0; k < 2; ++k)
            {
                const double hz = k ? 10000.0 : 1000.0;
                HiFiPath h; start(h, sr, clean());
                const long ns = (long)(0.6 * sr), nm = wholePeriods(sr, hz, 0.4);
                std::vector<float> o;
                for (long i = 0; i < ns + nm; ++i)
                {
                    const Stereo out = h.playOnly(splat((float)(kRefLevel * std::sin(kTau * hz * (double)i / sr))));
                    if (i >= ns) o.push_back(out[0]);
                }
                pb[k] = dB(amp(o, sr, hz, ns));
            }
            std::printf("   %6.0f Hz rate: record %+.2f dB, playback %+.2f dB\n", sr, rec, pb[1] - pb[0]);
            ok = ok && std::fabs(rec - 3.9) <= 0.3 && std::fabs(pb[1] - pb[0] + 7.7) <= 0.3;
        }
        check(ok, "both within 0.3 dB at every rate");
    }

    std::printf("5. The limiter: where a steady tone starts to clip (+19 dB at 1 kHz, +11.5 dB at 10 kHz)\n");
    {
        bool ok = true;
        for (float sr : { 44100.0f, 96000.0f })
        {
            double onset[2];
            for (int k = 0; k < 2; ++k)
            {
                const double hz = k ? 10000.0 : 1000.0;
                double lo = 0.0, hi = 30.0;
                for (int it = 0; it < 14; ++it)
                {
                    const double mid = 0.5 * (lo + hi);
                    (pathTone(sr, clean(), hz, mid, 0.5, 0.2).yPeak >= kHfClip * 0.99999 ? hi : lo) = mid;
                }
                onset[k] = 0.5 * (lo + hi);
            }
            std::printf("   %6.0f Hz rate: 1 kHz clips from %+.2f dB, 10 kHz from %+.2f dB\n", sr, onset[0], onset[1]);
            ok = ok && std::fabs(onset[0] - 19.0) <= 0.5 && std::fabs(onset[1] - 11.5) <= 0.5;
        }
        check(ok, "within 0.5 dB");
    }

    std::printf("6. Channel noise: 55 dB under reference at full carrier, 1 dB more per dB lost, rising 6 dB per octave\n");
    {
        bool ok = true;
        for (float sr : { 44100.0f, 96000.0f })
        {
            double snr[3]; double slope = 0.0;
            const float cs[3] = { 0.0f, -5.0f, -10.0f };
            for (int k = 0; k < 3; ++k)
            {
                HiFiPath::Settings s; s.hiss = 1.0f; s.carrierDb = cs[k];
                HiFiPath h; start(h, sr, s);
                std::vector<float> z; z.reserve((size_t)(8 * sr));
                for (long i = 0; i < (long)(8 * sr); ++i) z.push_back(chan(h, splat(0.0f))[0]);
                const std::vector<double> p = psd(z);
                snr[k] = 10.0 * std::log10(0.5 * kRefLevel * kRefLevel / bandPower(p, sr, 20.0, 20000.0));
                if (k == 0) slope = 10.0 * std::log10(bandDensity(p, sr, 3800.0, 4200.0) / bandDensity(p, sr, 475.0, 525.0)) / 3.0;
            }
            std::printf("   %6.0f Hz rate: S/N %.2f dB at 0 dB carrier, %.2f at -5, %.2f at -10; %.2f dB per octave from 500 Hz to 4 kHz\n", sr, snr[0], snr[1], snr[2], slope);
            ok = ok && std::fabs(snr[0] - 55.0) < 0.3 && std::fabs(snr[0] - snr[1] - 5.0) < 0.2 && std::fabs(snr[0] - snr[2] - 10.0) < 0.2 && std::fabs(slope - 6.02) < 0.3;
        }
        check(ok, "55 dB within 0.3 dB, 1 dB per dB within 0.2 dB, 6 dB per octave within 0.3 dB");
    }

    std::printf("7. Clicks: Rice's rate (B / 2 sqrt 3) erfc(sqrt CNR), B = 300 kHz, per channel; the CNR is Crackle's margin less what moves the carrier\n");
    {
        bool ok = true;
        const float cs[3] = { -16.0f, -17.5f, -19.0f };   // a dropout this deep under a 25 dB margin
        const double secs[3] = { 300.0, 60.0, 30.0 };
        for (int k = 0; k < 3; ++k)
        {
            HiFiPath::Settings s; s.clicks = 1.0f;
            HiFiPath h; start(h, 44100.0f, s);
            h.dropDb = -cs[k]; h.dropLeft = 1 << 30;   // (a dropout that lasts)
            const long n = (long)(secs[k] * 44100.0);
            for (long i = 0; i < n; ++i) chan(h, splat(0.0f));
            const double rho = std::pow(10.0, (25.0 + cs[k]) / 10.0);
            const double want = 300.0e3 / (2.0 * std::sqrt(3.0)) * std::erfc(std::sqrt(rho)) * 2.0 * secs[k];
            const double tol = 4.0 * std::sqrt(want) + 0.002 * want;   // 4 sigma of a Poisson count, and the rate rounded to 32 bits
            std::printf("   a dropout of %+.1f dB (CNR %.1f dB): %u clicks in %.0f s on two channels, Rice: %.0f (+-%.0f), %.2f a second per channel\n",
                        cs[k], 25.0 + cs[k], h.clickCount, secs[k], want, tol, h.clickCount / (2.0 * secs[k]));
            ok = ok && std::fabs((double)h.clickCount - want) <= tol;
        }
        check(ok, "the counts within 4 standard deviations of the formula");
        HiFiPath::Settings s; s.clicks = 1.0f; s.cnr0Db = 9.13f; s.carrierDb = -19.0f;
        HiFiPath h; start(h, 44100.0f, s);
        for (long i = 0; i < 44100L * 60; ++i) chan(h, splat(0.0f));
        std::printf("   Crackle 50 %% (CNR 9.1 dB), the Carrier knob at -19 dB: %.2f clicks a second per channel (Rice at 9.1 dB: about 5)\n", h.clickCount / 120.0);
        check(h.clickCount > 300 && h.clickCount < 900, "the Carrier knob alone does not change the click rate");
    }

    std::printf("8. The head switch: 59.94 times a second; residual of a 10 kHz reference tone below 3 kHz at Head Buzz 0 between -73 and -76 dB\n");
    {
        {
            HiFiPath::Settings s = clean(); s.headBuzz = 1.0f;
            HiFiPath h; start(h, 44100.0f, s);
            std::vector<long> at;
            for (long i = 0; i < 44100L * 100; ++i) { const Stereo z = chan(h, splat(0.0f)); if (z[0] != 0.0f) at.push_back(i); }
            const double gap = at.size() > 1 ? 1000.0 * (double)(at.back() - at.front()) / (double)(at.size() - 1) / 44100.0 : 0.0;
            std::printf("   %u switches in 100 s (the 5994th falls on 100.0 s); at Head Buzz 100 %% a spike at each (%zu), %.4f ms apart on average: %.3f a second\n",
                        h.switchCount, at.size(), gap, 1000.0 / gap);
            check((h.switchCount == 5993 || h.switchCount == 5994) && at.size() == h.switchCount && std::fabs(1000.0 / gap - 59.94) < 0.005, "59.94 a second");
        }
        bool ok = true; double base = 0.0;
        for (float sr : rates)
        {
            double res[4];
            const float hb[4] = { 0.0f, 0.1f, 0.3f, 1.0f };
            for (int k = 0; k < 4; ++k)
            {
                HiFiPath::Settings s = clean(); s.headBuzz = hb[k];
                HiFiPath h; start(h, sr, s);
                PairBiquad lpA, lpB;
                lpA.setCoeffs(BiquadCoeffs::lowpass(sr, 3000.0f, 0.5412f)); lpB.setCoeffs(BiquadCoeffs::lowpass(sr, 3000.0f, 1.3066f));
                double acc = 0.0; long cnt = 0;
                const long n = (long)(20.0 * sr);
                for (long i = 0; i < n; ++i)
                {
                    const float y = (float)(kRefLevel * std::sin(kTau * 10000.0 * (double)i / sr));
                    const Stereo z = chan(h, splat(y));
                    const Stereo r = lpB.process(lpA.process(z - splat(y)));
                    if (i > (long)(0.1 * sr)) { acc += (double)r[0] * r[0]; ++cnt; }
                }
                res[k] = 10.0 * std::log10(acc / cnt / (0.5 * kRefLevel * kRefLevel));
            }
            std::printf("   %6.0f Hz rate: residual %.2f dB at Head Buzz 0 (a plain 6 us hold: %.1f dB); 10 %%: %.1f, 30 %%: %.1f, 100 %%: %.1f dB\n",
                        sr, res[0], res[0] - dB(kHfResidual), res[1], res[2], res[3]);
            ok = ok && res[0] <= -73.0 && res[0] >= -76.0 && res[1] > res[0] && res[2] > res[1] + 3.0 && res[3] > res[2] + 3.0;
            if (sr == 44100.0f) base = res[0];
        }
        (void)base;
        check(ok, "between -73 and -76 dB at Head Buzz 0 at every rate, and it grows with Head Buzz");
    }

    std::printf("9. A lost carrier: held below -21 dB; the linear track after 0.3 ms below -17 dB; back after 7-8 HID periods (233-267 ms, the slow mode)\n");
    {
        bool holdOk = true, onOk = true, backOk = true;
        double onMs = 0.0, backMin = 1e9, backMax = 0.0;
        for (float sr : rates)
        {
            HiFiPath::Settings s = clean();
            HiFiPath h; start(h, sr, s);
            Xorshift32 rng(7);
            long i = 0;
            auto run = [&](long count, int* first = nullptr, bool (*cond)(const HiFiPath&) = nullptr)
            {
                for (long k = 0; k < count; ++k, ++i)
                {
                    h.process(splat((float)(0.3 * kRefLevel * std::sin(kTau * 1000.0 * (double)i / sr))));
                    if (first && *first < 0 && cond(h)) *first = (int)k;
                }
            };
            run((long)(0.3 * sr));
            // a hold: c at -22 dB, the channel stands still
            s.carrierDb = -22.0f; h.set(s);
            run(kCtrl);
            // (it lets go towards silence: never grows, never changes sign)
            float hl = h.held[0]; bool still = true;
            for (int k = 0; k < (int)(0.002 * sr); ++k) { run(1); still = still && std::fabs(h.held[0]) <= std::fabs(hl) && h.held[0] * hl >= 0.0f; hl = h.held[0]; }
            s.carrierDb = -20.5f; h.set(s); run(kCtrl + 2);
            const float h2 = h.held[0]; run(4); const bool moves = h.held[0] != h2;
            holdOk = holdOk && still && moves;
            s.carrierDb = 0.0f; h.set(s); run((long)(0.3 * sr));
            // the switch to the linear track, and back
            for (int trial = 0; trial < 12; ++trial)
            {
                run(1 + (long)(rng.nextFloat() * 0.017 * sr));
                s.carrierDb = -18.0f; h.set(s);
                int lowAt = -1, linAt = -1;
                for (long k = 0; k < (long)(0.01 * sr); ++k) { run(1); if (lowAt < 0 && h.lowCount > 0) lowAt = (int)k; if (linAt < 0 && h.linear) linAt = (int)k; }
                const double ms = 1000.0 * (linAt - lowAt + 1) / sr;
                onOk = onOk && lowAt >= 0 && linAt >= 0 && std::fabs(ms - 0.3) <= 500.0 / sr;
                if (sr == 44100.0f) onMs = ms;
                run((long)(0.05 * sr + rng.nextFloat() * 0.02 * sr));
                s.carrierDb = 0.0f; h.set(s);
                int upAt = -1, offAt = -1;
                for (long k = 0; k < (long)(0.3 * sr); ++k) { run(1); if (upAt < 0 && h.lowCount == 0) upAt = (int)k; if (offAt < 0 && !h.linear) offAt = (int)k; }
                const double back = 1000.0 * (offAt - upAt) / sr;
                backMin = std::min(backMin, back); backMax = std::max(backMax, back);
                backOk = backOk && upAt >= 0 && offAt >= 0 && back >= 7000.0 / 29.97 - 1000.0 / sr && back <= 8000.0 / 29.97 + 1000.0 / sr;
                run((long)(0.02 * sr));
            }
        }
        std::printf("   at -22 dB the channel holds (falling towards silence) and at -20.5 dB it moves: %s\n", holdOk ? "yes" : "no");
        std::printf("   on the linear track %.3f ms after the carrier fell below -17 dB (44.1 kHz; 0.3 ms rounded to a sample)\n", onMs);
        std::printf("   back on Hi-Fi %.1f to %.1f ms after the carrier returned (12 trials at each of 4 rates)\n", backMin, backMax);
        check(holdOk, "held below -21 dB, free above it");
        check(onOk, "the linear track 0.3 ms after the carrier fell below -17 dB, at every rate");
        check(backOk, "back after 233.6 to 266.9 ms");

        // no click at the crossfades: a tone, the carrier dropping to -18 dB (and to -25 dB: hold first) and coming back
        double worst = 0.0;
        for (float drop : { -18.0f, -25.0f })
        {
            HiFiPath::Settings s = clean();
            HiFiPath h; start(h, 44100.0f, s);
            std::vector<float> o; float ownHi = 0.0f, ownLin = 0.0f;
            for (long i = 0; i < 44100L * 4; ++i)
            {
                const long ph = i % 26460;   // 600 ms: on Hi-Fi (back from the last loss after 250 ms), then a loss of 150 ms
                if (ph == 0 || ph == 19845) { s.carrierDb = ph == 0 ? 0.0f : drop; h.set(s); }
                const Stereo out = h.process(splat((float)(0.3 * kRefLevel * std::sin(kTau * 1000.0 * (double)i / 44100.0))));
                if (!o.empty())
                {
                    const float st = std::fabs(out[0] - o.back());
                    if (h.linMix == 0.0f && !h.tapeRun) ownHi = std::fmax(ownHi, st);
                    if (h.linMix == 1.0f) ownLin = std::fmax(ownLin, st);
                }
                o.push_back(out[0]);
            }
            const double ratio = largestStep(o, 4410) / std::fmax(ownHi, ownLin);
            std::printf("   loss to %+.0f dB: largest step %.5f, the Hi-Fi sound's own %.5f, the linear track's %.5f (ratio %.2f)\n", drop, largestStep(o, 4410), ownHi, ownLin, ratio);
            worst = std::max(worst, ratio);
        }
        check(worst < 1.3, "no step at the crossfades larger than 1.3 times the sound's own");
    }

    std::printf("10. Cal: a level error at the expander's input comes out doubled (1:2), at every level inside the compander's range\n");
    {
        bool ok = true;
        for (double lv : { -40.0, -20.0, 0.0, 6.0 })
        {
            double g[3];
            const float cal[3] = { 0.0f, 3.0f, -3.0f };
            for (int k = 0; k < 3; ++k) { HiFiPath::Settings s = clean(); s.calDb = cal[k]; g[k] = pathTone(44100.0f, s, 1000.0, lv).gainDb; }
            std::printf("   in %+3.0f dB: Cal +3 dB gives %+.3f dB, Cal -3 dB %+.3f dB (expected +6.0 and -6.0)\n", lv, g[1] - g[0], g[2] - g[0]);
            ok = ok && std::fabs(g[1] - g[0] - 6.0) < 0.1 && std::fabs(g[2] - g[0] + 6.0) < 0.1;
        }
        HiFiPath::Settings s = clean(); s.calDb = -3.0f;
        std::printf("   (at -60 dB, where the expander reaches its -30 dB end, Cal -3 dB gives %+.2f dB)\n", pathTone(44100.0f, s, 1000.0, -60.0).gainDb - pathTone(44100.0f, clean(), 1000.0, -60.0).gainDb);
        check(ok, "+-6.0 dB to 0.1 dB");
    }

    std::printf("11. Breathing (another deck's expander): steady tones keep their level, moving sound does not\n");
    {
        bool ok = true;
        for (double lv : { -20.0, 0.0 })
        {
            HiFiPath::Settings s = clean(); s.breathing = 1.0f;
            const double g = pathTone(44100.0f, s, 1000.0, lv, 1.0, 0.4).gainDb;
            std::printf("   Breathing 100 %%, 1 kHz steady at %+.0f dB: %+.3f dB\n", lv, g);
            ok = ok && std::fabs(g) < 0.15;
        }
        check(ok, "steady tones within 0.15 dB");
        double err[2];
        for (int k = 0; k < 2; ++k)
        {
            HiFiPath::Settings s = clean(); s.breathing = k ? 1.0f : 0.0f;
            HiFiPath h; start(h, 44100.0f, s);
            double worst = 0.0;
            for (long i = 0; i < 44100L * 2; ++i)
            {
                const double a = kRefLevel * (std::fmod(i / 44100.0, 0.5) < 0.1 ? 0.1 : 0.01);   // -20 dB bursts in -40 dB: under the limiter
                const double x = a * std::sin(kTau * 1000.0 * i / 44100.0);
                const Stereo o = h.process(splat((float)x));
                if (i > 44100 && std::fabs(x) > 0.5 * a) worst = std::max(worst, std::fabs(dB(std::fabs(o[0]) / std::fabs(x))));
            }
            err[k] = worst;
        }
        std::printf("   bursts of -20 dB in -40 dB: largest gain error %.3f dB matched, %.2f dB with Breathing 100 %%\n", err[0], err[1]);
        check(err[0] < 0.1 && err[1] > 0.5, "matched decks track; mismatched ones do not");
    }

    std::printf("12. In the plugin\n");
    {
        Fx fx;
        const mpc::Param& p = fx.plug()->param(kSatType);
        static const char* ids[66] = { "out", "drive", "tone", "wear", "comp", "hicut", "sat_on", "lows", "losat", "split", "highs", "hisat",
            "warp", "rpm", "shape", "flutter", "flut_hz", "hum", "buzz", "headbuzz", "hiss", "crackle", "noise", "noise_pos",
            "wow", "wow_hz", "flutter2", "flut2_hz", "locut", "hicut2", "cho_on", "cho_mix", "cho_hz", "cho_ms", "cho_width",
            "mic_on", "mic_type", "mic_gain", "mono", "dropouts", "wobble_on", "noise_on", "vis_reel", "vis_level",
            "deck_on", "agc", "drift", "tracking", "breathing", "mix", "animate", "scrape", "whistle", "hq", "nr", "sat_type",
            "mic_model", "agc_model", "visual_vibes", "vis_snow", "vis_head", "vis_streak", "vis_grid", "vis_haze", "azimuth", "hysteresis" };
        bool same = fx.e->numParams == 66 && fx.e->numPrograms == 46;
        for (int i = 0; i < 66 && same; ++i) same = fx.plug()->param(i).id == ids[i];
        check(same, "66 parameters (Hysteresis, 1.8, the last) with their ids in their places, 46 programs");
        check(p.choices.size() == 3 && p.choices[0] == "Simple" && p.choices[1] == "VHS" && p.choices[2] == "Hi-Fi" && p.def == 0.0f, "Sat Type: Simple, VHS, Hi-Fi; Simple in a new instance");
        bool none = true;
        for (int k = 0; k < 46; ++k) { fx.e->dispatcher(fx.e, effSetProgram, 0, k, nullptr, 0.0f); none = none && (fx.get(kSatType) == 2.0f) == (k >= 39 && k < 44); }
        check(none, "the 39 older programs do not use Hi-Fi (they keep their sound); the five after them do, the two Azimuth ones after those do not");
        check(fx.e->version == 1800 && fx.e->dispatcher(fx.e, effGetVendorVersion, 0, 0, nullptr, 0.0f) == 1800, "version 1.8.0");

        // a state saved by 1.6.0
        if (argc > 1)
        {
            std::vector<uint8_t> s;
            if (FILE* f = std::fopen(argv[1], "rb")) { int c; while ((c = std::fgetc(f)) != EOF) s.push_back((uint8_t)c); std::fclose(f); }
            Fx ld; ld.set(kSatType, 2);
            const VstIntPtr r = s.empty() ? 0 : ld.load(s);
            std::printf("   a 1.6.0 state (%zu bytes): load %d, Sat Type %.0f (%s), Lo Sat %.1f, Hi Sat %.1f, Split %.0f\n", s.size(), (int)r, ld.get(kSatType),
                        ld.text(kSatType).c_str(), ld.get(kLoSat), ld.get(kHiSat), ld.get(kSplit));
            check(r == 1 && ld.get(kSatType) == 1.0f && std::fabs(ld.get(kLoSat) - 70.0f) < 0.01f && std::fabs(ld.get(kHiSat) - 25.0f) < 0.01f, "a state saved by 1.6.0 with Sat Type on VHS loads as VHS");
        }
        else check(false, "a state saved by 1.6.0 (no file given)");
        {
            Fx a; a.hifi(); a.set(kHiSat, 40); Fx b; const VstIntPtr r = b.load(a.state());
            check(r == 1 && b.get(kSatType) == 2.0f && b.get(kHiSat) == 40.0f, "and a 1.7 state with Hi-Fi loads as Hi-Fi");
        }

        // read-outs
        {
            Fx r; r.hifi();
            const std::string l0 = (r.set(kLoSat, 0), r.text(kLoSat)), l1 = (r.set(kLoSat, 100.0f * 10.0f / 18.0f), r.text(kLoSat)), l2 = (r.set(kLoSat, 100), r.text(kLoSat));
            const std::string h0 = (r.set(kHiSat, 0), r.text(kHiSat)), h1 = (r.set(kHiSat, 50), r.text(kHiSat)), h2 = (r.set(kHiSat, 100), r.text(kHiSat));
            const std::string s0 = (r.set(kSplit, 200), r.text(kSplit)), s1 = (r.set(kSplit, 2500), r.text(kSplit)), s2 = (r.set(kSplit, 8000), r.text(kSplit));
            std::printf("   Rec Level %s / %s / %s, Carrier %s / %s / %s, Cal %s / %s / %s\n", l0.c_str(), l1.c_str(), l2.c_str(), h0.c_str(), h1.c_str(), h2.c_str(), s0.c_str(), s1.c_str(), s2.c_str());
            check(l0 == "-10.0 dB" && l1 == "0.0 dB" && l2 == "+28.0 dB" && h0 == "0 dB" && h1 == "-10 dB" && h2 == "-20 dB" && s0 == "-3.0 dB" && s1 == "right" && s2 == "+3.0 dB",
                  "read-outs: Rec Level -10 to +28 dB (0 dB at 56 %), Carrier 0 to -20 dB, Cal -3 to +3 dB, right at the default");
            r.set(kSatType, 1); r.set(kLoSat, 100.0f * 10.0f / 18.0f);
            check(r.text(kLoSat) == "0.0 dB", "and VHS mode's Rec Level reads 0 dB at the same place");
        }

        // Sat Type turned by a Q-Link: every value on the way, up and down
        for (int satOn = 1; satOn >= 0; --satOn)
        {
            Fx q; q.hifi(); q.set(kSatType, 0); q.set(kSatOn, (float)satOn); q.set(kHiSat, 20); q.set(kHiss, 40);
            std::vector<float> own; q.play(0.5f, 0.5, 300.0, nullptr); q.play(0.3f, 0.5, 300.0, &own);
            float ownStep = largestStep(own);
            for (int t = 1; t <= 2; ++t) { Fx o; o.hifi(); o.set(kSatType, (float)t); o.set(kSatOn, (float)satOn); o.set(kHiSat, 20); o.set(kHiss, 40); std::vector<float> v; o.play(0.5f, 0.5, 300.0, nullptr); o.play(0.3f, 0.5, 300.0, &v); ownStep = std::fmax(ownStep, largestStep(v)); }
            std::vector<float> out; int fades = 0; bool inFade = false;
            for (int b = 0; b < 1000; ++b)
            {
                if (b >= 100 && b < 500) q.e->setParameter(q.e, kSatType, (float)(b - 100) / 399.0f);
                if (b >= 600 && b < 1000) q.e->setParameter(q.e, kSatType, 1.0f - (float)(b - 600) / 399.0f);
                const size_t at = out.size();
                q.play(128.0f / 44100.0f, 0.5, 300.0, &out);
                float pk = 0.0f; for (size_t j = at; j < out.size(); ++j) pk = std::fmax(pk, std::fabs(out[j]));
                const bool quiet = pk < 0.05f;
                if (quiet && !inFade) ++fades;
                inFade = quiet;
            }
            std::vector<float> tail; q.play(0.3f, 0.5, 300.0, &tail);
            float tailPk = 0.0f; for (float v : tail) tailPk = std::fmax(tailPk, std::fabs(v));
            std::printf("   Saturator %s: %d fades, largest step %.4f (the sound's own %.4f), ends on %s with the sound at %.3f\n", satOn ? "on " : "off", fades, largestStep(out), ownStep, q.text(kSatType).c_str(), tailPk);
            char what[96];
            std::snprintf(what, sizeof(what), "Saturator %s: %s, no click, not stuck", satOn ? "on" : "off", satOn ? "one fade at each of the four changes" : "no fade");
            check(fades == (satOn ? 4 : 0) && largestStep(out) < 1.5f * ownStep && q.get(kSatType) == 0.0f && tailPk > 0.3f, what);
        }

        // what Hi-Fi takes over and what it leaves; the switches that gate it
        auto render = [](void (*setup)(Fx&), double ampl) { Fx f; f.hifi(); setup(f); std::vector<float> o; f.play(1.5f, ampl, 440.0, &o); return o; };
        {
            const auto a = render([](Fx& f) { f.set(kSatOn, 0); f.set(kHiss, 60); f.set(kCrackle, 40); f.set(kDeckOn, 1); f.set(kTracking, 50); f.set(kNr, 3); }, 0.2);
            const auto b = render([](Fx& f) { f.set(kSatOn, 0); f.set(kSatType, 0); f.set(kHiss, 60); f.set(kCrackle, 40); f.set(kDeckOn, 1); f.set(kTracking, 50); f.set(kNr, 3); }, 0.2);
            check(a == b, "with the Saturator off, Hi-Fi sounds exactly as Simple does (bit for bit): the seven knobs and B-Type NR their own");
        }
        {
            const auto a = render([](Fx& f) { f.set(kNr, 3); f.set(kHiSat, 30); }, 0.2), b = render([](Fx& f) { f.set(kNr, 0); f.set(kHiSat, 30); }, 0.2);
            check(a == b, "B-Type NR is ignored while Hi-Fi is on");
        }
        {
            const auto a = render([](Fx& f) { f.set(kNoiseOn, 0); f.set(kHiss, 100); f.set(kCrackle, 100); f.set(kHeadBuzz, 100); f.set(kHiSat, 50); }, 0.2);
            const auto b = render([](Fx& f) { f.set(kNoiseOn, 0); f.set(kHiss, 0); f.set(kCrackle, 0); f.set(kHeadBuzz, 0); f.set(kHiSat, 50); }, 0.2);
            check(a == b, "the Noise switch off silences Hiss, Crackle and Head Buzz in Hi-Fi too");
            const auto c = render([](Fx& f) { f.set(kDeckOn, 0); f.set(kTracking, 100); f.set(kDrift, 100); f.set(kBreathing, 100); }, 0.2);
            const auto d = render([](Fx& f) { f.set(kDeckOn, 0); f.set(kTracking, 0); f.set(kDrift, 0); f.set(kBreathing, 0); }, 0.2);
            check(c == d, "and Deck On off, Tracking, Drift and Breathing");
        }
        {
            // the hiss and the crackle of the other types are not added on top: in silence the FM channel's
            // noise, loud as the knobs make it, sits under the expander's cut
            double rms[2];
            for (int k = 0; k < 2; ++k)
            {
                Fx f; f.hifi(); if (k) f.set(kSatType, 0);
                f.set(kHiss, 100); f.set(kCrackle, 100); f.set(kHeadBuzz, 100); f.set(kNoise, 50);
                std::vector<float> o; f.play(0.5f, 0.0, 0.0, nullptr); f.play(2.0f, 0.0, 0.0, &o);
                double s = 0; for (float v : o) s += (double)v * v; rms[k] = dB(std::sqrt(s / o.size()) / kRefLevel);
            }
            std::printf("   silence in, Hiss, Crackle, Head Buzz at 100 %%: Hi-Fi %.1f dB re reference, Simple %.1f dB\n", rms[0], rms[1]);
            check(rms[0] < rms[1] - 10.0, "the other types' noise stages rest while Hi-Fi is on");
        }
    }

    {
        // Crackle at the top with the weaker head at its weakest (Tracking 100 %): that head's share is
        // kept from the noise detector, so it rattles and the deck stays on the Hi-Fi track
        HiFiPath::Settings s = clean(); s.clicks = 1.0f; s.cnr0Db = kHfCnrDb[5]; s.imbalanceDb = kHfImbalanceDb;
        HiFiPath h; start(h, 44100.0f, s);
        long lin = 0;
        for (long i = 0; i < 44100L * 4; ++i) { h.process(splat(0.0f)); lin += h.linear ? 1 : 0; }
        std::printf("   Crackle 100 %% and the weaker head 4 dB down: %u clicks in 4 s on two channels, %ld samples on the linear track\n", h.clickCount, lin);
        check(lin == 0 && h.clickCount > 2000 && h.clickCount < 20000, "Crackle and Tracking at the top: the weaker head clicks hard and the deck never parks on the linear track");
    }

    std::printf("13. Silence for minutes, a full-scale square, a burst of NaNs\n");
    {
        HiFiPath::Settings s = clean(); s.breathing = 0.5f;
        HiFiPath h; start(h, 44100.0f, s);
        for (long i = 0; i < 44100L; ++i) h.process(splat((float)(0.5 * std::sin(kTau * 440.0 * i / 44100.0))));
        for (long i = 0; i < 44100L * 300; ++i) h.process(splat(0.0f));
        const bool sub = subnormal(h.recDet.d) || subnormal(h.playDet.d) || subnormal(h.rmsDet.d) || subnormal(h.nrEmph.x1) || subnormal(h.nrEmph.y1)
                      || subnormal(h.wRec.y1) || subnormal(h.fmEmph.y1) || subnormal(h.fmDe.x1) || subnormal(h.wPlay.y1) || subnormal(h.nrDe.x1) || subnormal(h.held);
        check(!sub, "5 minutes of silence leave no subnormal number in the path");
        Fx f(44100.0f, true); f.hifi(); f.set(kLoSat, 100); f.set(kHiSat, 40); f.set(kHiss, 50); f.set(kCrackle, 50); f.set(kTracking, 40); f.set(kDeckOn, 1); f.set(kDropouts, 100);
        static float L[256], R[256]; float* io[2] = { L, R };
        auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < 44100 * 120 / 256; ++b) { std::memset(L, 0, sizeof(L)); std::memset(R, 0, sizeof(R)); f.e->processReplacing(f.e, io, io, 256); }
        const double first = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        float big = 0.0f; bool finite = true;
        for (int b = 0; b < 44100 * 5 / 256; ++b)
        {
            for (int i = 0; i < 256; ++i) L[i] = R[i] = ((b * 256 + i) / 220) & 1 ? 1.0f : -1.0f;
            f.e->processReplacing(f.e, io, io, 256);
            for (int i = 0; i < 256; ++i) { finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]); big = std::fmax(big, std::fmax(std::fabs(L[i]), std::fabs(R[i]))); }
        }
        std::printf("   in the plugin (HQ, Rec Level +28 dB, every impairment up): 2 minutes of silence took %.2f s; a full-scale 100 Hz square peaks at %.2f\n", first, big);
        check(finite && big < 4.0f, "the square comes out finite and bounded");
        for (int i = 0; i < 256; ++i) L[i] = R[i] = std::nanf("");
        L[10] = INFINITY;
        f.e->processReplacing(f.e, io, io, 256);
        std::vector<float> o; f.pos = 0; f.play(1.0f, 0.3, 1000.0, &o, 256);
        bool fin = true; for (float v : o) fin = fin && std::isfinite(v);
        double late = 0; for (size_t i = o.size() / 2; i < o.size(); ++i) late = std::max(late, (double)std::fabs(o[i]));
        std::printf("   after a block of NaNs: output finite %s, a 1 kHz tone back at %.3f peak in the second half-second\n", fin ? "yes" : "no", late);
        check(fin && late > 0.1, "a burst of NaNs leaves nothing behind");
        HiFiPath g; start(g, 48000.0f, clean());
        for (int i = 0; i < 5; ++i) g.process(splat(std::nanf("")));
        bool ok = true; for (int i = 0; i < 2000; ++i) { const Stereo v = g.process(splat(0.1f)); if (i > 40) ok = ok && std::isfinite(v[0]) && std::isfinite(v[1]); }
        check(ok, "the path on its own clears a NaN at its next control tick");
    }

    std::printf("14. Out of spec: the linear track's hiss, Cal's slope, the noise detector, the hold, separate random streams\n");
    {
        // the linear track brings its own hiss: the hiss stage, twice the Simple type's, on the Hiss knob and the Noise level
        auto hissDb = [](float hiSat, float hiss, float noise, bool on, int type)
        {
            Fx f; f.hifi(); f.set(kSatType, (float)type); f.set(kHiSat, hiSat); f.set(kHiss, hiss); f.set(kNoise, noise); f.set(kNoiseOn, on ? 1.0f : 0.0f);
            std::vector<float> o; f.play(0.5f, 0.0, 0.0, nullptr); f.play(2.0f, 0.0, 0.0, &o);
            double s = 0; for (float v : o) s += (double)v * v; return dB(std::sqrt(s / o.size()) + 1e-30);
        };
        const double lin50 = hissDb(95, 50, 50, true, 2), simple50 = hissDb(0, 50, 50, true, 0), lin25 = hissDb(95, 25, 50, true, 2);
        const double lin50n = hissDb(95, 50, 100, true, 2), lin0 = hissDb(95, 0, 50, true, 2), linOff = hissDb(95, 50, 50, false, 2), hifi50 = hissDb(0, 50, 50, true, 2);
        std::printf("   silence, Carrier -19 dB (on the linear track): Hiss 50 %.1f dBFS (Simple type %.1f, Hi-Fi track %.1f), Hiss 25 %.1f, Noise 100 %.1f, Hiss 0 %.1f, Noise off %.1f\n",
                    lin50, simple50, hifi50, lin25, lin50n, lin0, linOff);
        check(std::fabs(lin50 - simple50 - 6.02) < 1.0 && lin50 > hifi50 + 20.0, "on the linear track the hiss stage's hiss, twice the Simple type's, far over the Hi-Fi track's");
        check(std::fabs(lin50 - lin25 - 12.04) < 0.5 && std::fabs(lin50n - lin50 - 12.04) < 0.5, "it follows the Hiss knob and the Noise level as the other types' hiss does");
        check(lin0 < -120.0 && linOff < -120.0, "Hiss 0 or the Noise switch off: none");

        // Cal: right is transparent (1. above); wrong, the expander's slope is off too, so the error depends on the level
        bool ok = true;
        for (int sg = -1; sg <= 1; sg += 2)
        {
            HiFiPath::Settings s = clean(); s.calDb = 3.0f * sg; s.calSlope = 0.25f * sg;
            const double g0 = pathTone(44100.0f, s, 1000.0, 0.0).gainDb, g40 = pathTone(44100.0f, s, 1000.0, -40.0).gainDb;
            // the expander sees the tone Cal dB off and turns it (2 + slope) times that; 40 dB lower, 20 dB lower on the tape, slope x 20 dB less
            const double want0 = (2.0 + 0.25 * sg) * 3.0 * sg, want40 = want0 - 5.0 * sg;
            std::printf("   Cal %+.0f dB, slope 2 %+.2f: a 1 kHz tone at reference %+.2f dB, 40 dB under it %+.2f dB (expected %+.2f and %+.2f)\n", 3.0 * sg, 0.25 * sg, g0, g40, want0, want40);
            ok = ok && std::fabs(g0 - want0) < 0.1 && std::fabs(g40 - want40) < 0.3;
        }
        double gp[2][2];
        for (int k = 0; k < 2; ++k)
            for (int lv = 0; lv < 2; ++lv)
            {
                Fx f; f.hifi(); f.set(kNoiseOn, 0); if (k) f.set(kSplit, 8000);
                std::vector<float> o; const double a = kRefLevel * (lv ? 0.01 : 1.0);
                f.play(0.8f, a, 1000.0, nullptr); f.play((float)(wholePeriods(44100.0, 1000.0, 0.3) / 44100.0), a, 1000.0, &o);
                gp[k][lv] = dB(amp(o, 44100.0, 1000.0, f.pos - (long)o.size()));
            }
        std::printf("   in the plugin, Split at the top against Cal right: %+.2f dB at reference, %+.2f dB 40 dB under it\n", gp[1][0] - gp[0][0], gp[1][1] - gp[0][1]);
        check(ok && gp[1][0] - gp[0][0] > 5.5 && gp[1][1] - gp[0][1] < 2.0, "Cal wrong: the level error grows with the level (2 +- 0.25 slope at the ends), so the sound pumps");

        // the noise detector: under 5 dB CNR the linear track, never a solid static. Crackle 100 % and a
        // dropout from 0 to 6 dB deep
        {
            double worstRate = 0.0; bool lowOk = true;
            for (float c = 0.0f; c >= -6.0f; c -= 0.125f)
            {
                HiFiPath::Settings s = clean(); s.clicks = 1.0f; s.cnr0Db = hifiCnrDb(1.0f);
                HiFiPath h; start(h, 44100.0f, s);
                h.dropDb = -c; h.dropLeft = 1 << 30;
                long hifi = 0; uint32_t c0 = 0;
                for (long i = 0; i < 22050; ++i) { chan(h, splat(0.0f)); if (i == 2205) c0 = h.clickCount; if (i > 2205 && h.lowCount == 0) ++hifi; }
                if (hifi > 0) worstRate = std::max(worstRate, (h.clickCount - c0) / 2.0 / (19845.0 / 44100.0));
                lowOk = lowOk && (s.cnr0Db + c < kHfNoiseDetDb) == (hifi == 0);
            }
            std::printf("   Crackle 100 %% (CNR %.2f dB at rest) and a dip: the most clicks while on the Hi-Fi track %.0f a second per channel; under 5 dB CNR always the linear track: %s\n",
                        hifiCnrDb(1.0f), worstRate, lowOk ? "yes" : "no");
            check(lowOk && worstRate < 1500.0, "the noise detector: the linear track under 5 dB CNR, at most about 1000 clicks a second above it");
            // at rest Crackle spreads over its knob, the same at every Carrier: about 0, 1, 5, 30 and 150 a second
            bool spread = true;
            const float knob[5] = { 0.1f, 0.25f, 0.5f, 0.75f, 1.0f }, want[5] = { 0.0f, 1.0f, 5.0f, 30.0f, 150.0f };
            std::printf("   Crackle at rest, clicks a second per channel (Carrier 0 dB / -12 dB):");
            for (int k = 0; k < 5; ++k)
            {
                double r[2];
                for (int j = 0; j < 2; ++j)
                {
                    HiFiPath::Settings s = clean(); s.clicks = 1.0f; s.cnr0Db = hifiCnrDb(knob[k]); s.carrierDb = j ? -12.0f : 0.0f;
                    HiFiPath h; start(h, 44100.0f, s);
                    for (long i = 0; i < 44100L * 20; ++i) chan(h, splat(0.0f));
                    r[j] = h.clickCount / 40.0;
                }
                std::printf(" %.0f %%: %.2f / %.2f%s", 100.0 * knob[k], r[0], r[1], k < 4 ? "," : "\n");
                spread = spread && r[0] == r[1] && (k == 0 ? r[0] < 0.1 : r[0] > 0.5 * want[k] && r[0] < 1.6 * want[k]);
            }
            check(spread, "Crackle spread over its knob (about 0, 1, 5, 30 and 150 a second at 10 to 100 %) and the same at every Carrier");
            // in the plugin, every Carrier with Crackle up: the crackle stays under a -18 dBFS tone
            double worstRel = -300.0; float wc = 0, wcr = 0;
            for (float cr : { 25.0f, 50.0f, 75.0f, 100.0f })
                for (float hs = 0.0f; hs <= 100.0f; hs += 5.0f)
                {
                    std::vector<float> o[2];
                    for (int k = 0; k < 2; ++k) { Fx f; f.hifi(); f.set(kHiss, 0); f.set(kHiSat, hs); f.set(kCrackle, k ? cr : 0.0f); f.play(0.3f, 0.126, 440.0, nullptr); f.play(1.0f, 0.126, 440.0, &o[k]); }
                    double d = 0, p = 0; for (size_t i = 0; i < o[0].size(); ++i) { d += (double)(o[1][i] - o[0][i]) * (o[1][i] - o[0][i]); p += (double)o[0][i] * o[0][i]; }
                    const double rel = 10.0 * std::log10(d / p + 1e-30);
                    if (rel > worstRel) { worstRel = rel; wc = hs; wcr = cr; }
                }
            std::printf("   in the plugin, Crackle 25 to 100 %%, Carrier 0 to -20 dB, a -18 dBFS 440 Hz tone: the crackle at most %.1f dB re the tone (Crackle %.0f, Hi Sat %.0f)\n", worstRel, wcr, wc);
            check(worstRel < 0.0, "the crackle stays under the programme at every Crackle and Carrier");
        }

        // the hold: never louder than the programme, no thump when it runs into the crossfade (the change
        // against the same dropout 18 dB deep, which goes to the linear track without a hold)
        {
            double worst = -300.0, worstLf = -300.0;
            for (double hz : { 440.0, 5000.0, 0.0 })   // (0: 100 Hz and 5 kHz together)
                for (float depth : { 23.0f, 30.0f })
                    for (double ms : { 0.1, 0.25, 0.5, 2.0, 20.0 })
                        for (int on = 0; on < 6; ++on)
                        {
                            std::vector<float> o[3];
                            const long at = 22050 + on * 1013;
                            for (int ev = 0; ev < 3; ++ev)
                            {
                                HiFiPath h; start(h, 44100.0f, HiFiPath::Settings());
                                bool done = false;
                                for (long i = 0; i < at + 4410; ++i)
                                {
                                    if (ev && !done && i >= at && h.n == 0) { h.dropDb = ev == 1 ? depth : 18.0f; h.dropLeft = (int)(ms * 44.1 + 0.5); done = true; }
                                    const double t = (double)i / 44100.0;
                                    const double x = hz > 0.0 ? 0.126 * std::sin(kTau * hz * t) : 0.1 * std::sin(kTau * 100.0 * t) + 0.05 * std::sin(kTau * 5000.0 * t);
                                    o[ev].push_back(h.process(splat((float)x))[0]);
                                }
                            }
                            float pk = 0.0f, pp = 0.0f; double lf = 0.0, pr = 0.0, lp1 = 0.0, lp2 = 0.0;
                            for (long i = at - 441; i < at + 4410; ++i)
                            {
                                const float d = o[1][(size_t)i] - o[2][(size_t)i];
                                pk = std::fmax(pk, std::fabs(o[1][(size_t)i])); pp = std::fmax(pp, std::fabs(o[0][(size_t)i]));
                                lp1 += 0.0213 * (d - lp1); lp2 += 0.0213 * (lp1 - lp2);   // under 150 Hz
                                lf += lp2 * lp2; pr += (double)o[0][(size_t)i] * o[0][(size_t)i];
                            }
                            worst = std::max(worst, dB(pk / pp)); worstLf = std::max(worstLf, 10.0 * std::log10(lf / pr + 1e-30));
                        }
            std::printf("   dropouts 23 and 30 dB deep, 0.1 to 20 ms, on 440 Hz, 5 kHz and 100 Hz + 5 kHz: the output's peak at most %+.2f dB re the programme's, the change under 150 Hz %.1f dB re the programme\n", worst, worstLf);
            check(worst <= 0.0 && worstLf < -20.0, "a hold is never louder than the programme and does not thump into the crossfade");
        }

        // Drift, Tracking and Dropouts each draw from their own random stream
        {
            auto events = [](float drop, float track, std::vector<long>& tr, std::vector<long>& dr, std::vector<float>& dt)
            {
                HiFiPath::Settings s = clean(); s.tracking = track; s.drift = 0.6f; s.dropouts = drop;
                HiFiPath h; start(h, 44100.0f, s);
                int tp = 0; uint32_t d0 = 0;
                for (long i = 0; i < 44100L * 20; ++i)
                {
                    h.process(splat(0.0f));
                    if (h.trackPasses > tp) tr.push_back(i);
                    tp = h.trackPasses;
                    if (h.drops != d0) { dr.push_back(i); d0 = h.drops; }
                    if ((i & 1023) == 0) dt.push_back(h.driftN2);
                }
            };
            std::vector<long> t1, d1, t2, d2, t3, d3; std::vector<float> x1, x2, x3;
            events(0.0f, 0.7f, t1, d1, x1); events(1.0f, 0.7f, t2, d2, x2); events(1.0f, 0.3f, t3, d3, x3);
            std::printf("   20 s: %zu tracking dips with Dropouts 0 and %zu with Dropouts 100 %% (%zu dropouts); %zu dropouts with Tracking 30 %%\n", t1.size(), t2.size(), d2.size(), d3.size());
            check(!t1.empty() && t1 == t2 && x1 == x2 && !d2.empty() && d2 == d3 && x2 == x3, "turning Dropouts moves neither Tracking's dips nor Drift; turning Tracking leaves the dropouts where they were");
        }
    }

    std::printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
#endif
