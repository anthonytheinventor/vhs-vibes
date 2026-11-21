// VHS Vibes 1.7: Azimuth, the playback head's gap tilted against the recording (see Azimuth in the
// source). Measured against the loss law it is built on, and checked for what must not change.
//   azimuth_test [state160.bin [state140.bin]]   states saved by 1.6.0 and 1.4.0 (see run.sh)
// Built against an older source with -DWRITE_STATE it only writes such a state. Ends with PASS or FAIL.
#ifndef SRC
#define SRC "../src/VHSVibes.cpp"
#endif
#include SRC

#include <algorithm>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) { return opcode == audioMasterVersion ? 2400 : 0; }

#ifdef WRITE_STATE
// The older side: a state with a few knobs off their defaults.
int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    AEffect* e = VSTPluginMain(hostCb);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
    mpc::Plugin* p = static_cast<mpc::Plugin*>(e->object);
    p->set(kHiss, 12.0f); p->set(kDrive, 40.0f);
    void* data = nullptr;
    const size_t n = (size_t)e->dispatcher(e, effGetChunk, 0, 0, &data, 0.0f);
    FILE* f = std::fopen(argv[1], "wb");
    if (f == nullptr || std::fwrite(data, 1, n, f) != n) return 1;
    std::fclose(f);
    e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f); e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);
    return 0;
}
#else

static const double kTau = 6.283185307179586;
static double dB(double x) { return 20.0 * std::log10(x > 1e-30 ? x : 1e-30); }

// The window's impulse response at a fixed tilt, no wander (stage rate sr, perTick samples per tick).
static std::vector<double> impulse(float sr, int perTick, float tilt, float speed)
{
    Azimuth a;
    a.set(sr, perTick, tilt, 0.0f, speed);
    a.awT = 0.0f;
    a.reset(1);
    for (int t = 0; t < 2 * kAzLen / perTick + 4; ++t) { a.tick(); for (int k = 0; k < perTick; ++k) a.process(splat(0.0f)); }
    std::vector<double> h;
    for (int i = 0; i < kAzLen; ++i)
    {
        if (i % perTick == 0) a.tick();
        h.push_back(a.process(splat(i == 0 ? 0.25f : 0.0f))[0] / 0.25);
    }
    return h;
}
static double mag(const std::vector<double>& h, double sr, double f)
{
    std::complex<double> s = 0;
    for (size_t n = 0; n < h.size(); ++n) s += h[n] * std::polar(1.0, -kTau * f * (double)n / sr);
    return std::abs(s);
}

struct Fx
{
    AEffect* e;
    Fx(float sr = 44100.0f, int hq = 1)
    {
        e = VSTPluginMain(hostCb);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, sr);
        e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
        plug()->set(kHq, (float)hq);
        e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
        e->dispatcher(e, effSetProgram, 0, 20, nullptr, 0.0f);   // Init
    }
    ~Fx() { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f); e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f); }
    mpc::Plugin* plug() { return static_cast<mpc::Plugin*>(e->object); }
    void set(int i, float plain) { plug()->set(i, plain); }
    float get(int i) { return plug()->get(i); }
    std::string text(int i) { char b[64] = {0}; e->dispatcher(e, effGetParamDisplay, i, 0, b, 0.0f); return b; }
    // the tape and nothing else: quiet, linear, no noise, wobble or deck
    void plainTape()
    {
        for (auto kv : { std::pair<int, float>{kSatType, 1}, {kSatOn, 1}, {kNoiseOn, 0}, {kWobbleOn, 0}, {kDeckOn, 0},
                         {kDrive, 0}, {kWear, 0}, {kComp, 0}, {kHiCut, 18000}, {kHiCut2, 18000}, {kLoCut, 20},
                         {kLoSat, 0}, {kHighs, 0}, {kLows, 0}, {kMix, 100}, {kTone, 0}, {kHiSat, 0} })
            set(kv.first, kv.second);
    }
    // renders `seconds` of gen(i) (mono into both sides); `move(i)` is called before each 128-frame block
    template <typename G, typename M> std::vector<float> render(double seconds, float sr, G gen, M move)
    {
        const long total = (long)(seconds * sr);
        std::vector<float> out((size_t)total);
        float L[128], R[128]; float* io[2] = { L, R };
        for (long pos = 0; pos < total; pos += 128)
        {
            move(pos);
            const int n = (int)std::min<long>(128, total - pos);
            for (int i = 0; i < n; ++i) L[i] = R[i] = gen(pos + i);
            e->processReplacing(e, io, io, n);
            for (int i = 0; i < n; ++i) out[(size_t)(pos + i)] = L[i];
        }
        return out;
    }
};
static void noMove(long) {}

// level of `hz` in v[first .. first + n) (whole periods), by correlation
static double level(const std::vector<float>& v, double sr, double hz, long first, long n)
{
    double a = 0, b = 0;
    for (long i = first; i < first + n; ++i) { a += v[(size_t)i] * std::sin(kTau * hz * i / sr); b += v[(size_t)i] * std::cos(kTau * hz * i / sr); }
    return 2.0 * std::sqrt(a * a + b * b) / (double)n;
}

int main(int argc, char** argv)
{
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("   %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };

    std::printf("1. The window against 20 log |sin x / x|, x = pi f W tan(a) / v (W 1.0 mm), first null and -3 dB point\n");
    std::printf("   stage rate   tilt  speed   null: formula  measured  error    -3 dB: formula  measured  error   worst dev. above -20 dB\n");
    {
        double worstNull = 0, worstNullLow = 0, worst3 = 0, worstDev = 0;
        for (float sr : { 44100.0f, 96000.0f, 192000.0f })   // (the latter two: 48 and 96 kHz with HQ)
            for (float speed : { 0.0f, 0.6309f, 1.0f })
                for (float tilt : { 3.75f, 7.5f, 15.0f, 30.0f, 60.0f })
                {
                    const double v = 33.35 * std::pow(3.0, -(double)speed);
                    const double f0 = v / (1.0 * std::tan(tilt * kTau / 21600.0));
                    if (f0 > 0.4 * sr) continue;
                    const auto h = impulse(sr, sr > 100000.0f ? 2 * kCtrl : kCtrl, tilt, speed);
                    double fn = 0, best = 1e9;
                    for (double f = 0.8 * f0; f < std::min(1.2 * f0, 0.49 * (double)sr); f += f0 * 0.0005)
                    { const double m = mag(h, sr, f); if (m < best) { best = m; fn = f; } }
                    double lo = 0.2 * f0, hi = 0.7 * f0;   // -3 dB: bisection
                    for (int k = 0; k < 40; ++k) { const double mid = 0.5 * (lo + hi); (mag(h, sr, mid) > std::sqrt(0.5) ? lo : hi) = mid; }
                    double dev = 0;
                    for (double f = 0.01 * f0; f < std::min(0.9 * f0, 20000.0); f += 0.01 * f0)
                    {
                        const double x = kTau / 2 * f / f0, want = std::fabs(std::sin(x) / x);
                        if (want > 0.1) dev = std::max(dev, std::fabs(dB(mag(h, sr, f)) - dB(want)));
                    }
                    const double eN = fn / f0 - 1.0, e3 = lo / (0.4429 * f0) - 1.0;
                    worstNull = std::max(worstNull, std::fabs(eN)); if (f0 < 0.2 * sr) worstNullLow = std::max(worstNullLow, std::fabs(eN)); worst3 = std::max(worst3, std::fabs(e3)); worstDev = std::max(worstDev, dev);
                    std::printf("   %6.0f Hz  %5.2f'  %s   %8.0f Hz %8.0f Hz %+5.2f %%   %8.0f Hz %8.0f Hz %+5.2f %%   %.2f dB\n", sr, tilt,
                                speed == 0.0f ? "SP" : speed == 1.0f ? "EP" : "LP", f0, fn, 100 * eN, 0.4429 * f0, lo, 100 * e3, dev);
                }
        check(worstNull < 0.025 && worstNullLow < 0.005, "the first null within 0.5 % of where the formula puts it while it lies below a fifth of the rate, within 2.5 % up to 0.4 of it (a window of under four samples)");
        check(worst3 < 0.03, "the -3 dB point within 3 % of 0.443 x the null");
        check(worstDev < 0.6, "the response within 0.6 dB of the formula down to -20 dB, below 20 kHz");
        // past the null: the first side lobe, inverted, -13.3 dB
        const auto h = impulse(192000.0f, 2 * kCtrl, 30.0f, 0.0f);
        const double f0 = 33.35 / std::tan(30.0 * kTau / 21600.0);
        double side = 0; for (double f = 1.1 * f0; f < 1.9 * f0; f += 0.002 * f0) side = std::max(side, mag(h, 192000.0, f));
        std::printf("   30' SP at 192 kHz: first side lobe %.1f dB (sin x / x: -13.3 dB)\n", dB(side));
        check(std::fabs(dB(side) + 13.3) < 0.5, "the side lobe past the null is there, as the formula has it");
    }

    std::printf("2. At rest it is not there; outside Sat Type = VHS with the Saturator on it does nothing\n");
    {
        uint32_t s = 9;
        auto gen = [&](long i) { s = s * 1664525u + 1013904223u; return 0.2f * (float)std::sin(kTau * 3000.0 * i / 44100.0) + 0.05f * ((float)(s >> 8) / 8388608.0f - 1.0f); };
        bool same = true;
        for (int mode = 0; mode < 4; ++mode)   // Simple, Hi-Fi, VHS with the Saturator off, VHS at 0 (against 30' turned back to 0 before playing)
            for (int hq = 0; hq < 2; ++hq)
            {
                Fx a(44100.0f, hq), b(44100.0f, hq);
                for (Fx* f : { &a, &b })
                {
                    f->plainTape();
                    f->set(kSatType, mode == 0 ? 0.0f : mode == 1 ? 2.0f : 1.0f);
                    f->set(kSatOn, mode == 2 ? 0.0f : 1.0f);
                }
                b.set(kAzimuth, mode == 3 ? 30.0f : 60.0f);
                if (mode == 3) b.set(kAzimuth, 0.0f);
                s = 9; const auto ya = a.render(1.0, 44100.0f, gen, noMove);
                s = 9; const auto yb = b.render(1.0, 44100.0f, gen, noMove);
                same = same && ya == yb;
            }
        check(same, "Simple, Hi-Fi and VHS with the Saturator off: Azimuth 60' is bit-identical to 0 (HQ off and on)");
        Azimuth a; a.set(44100.0f, kCtrl, 0.0f, 0.0f, 0.0f); a.reset(1);
        double worst = 0;
        for (int i = 0; i < 1000; ++i) { if (i % kCtrl == 0) a.tick(); const float x = 0.5f * (float)std::sin(0.1 * i); worst = std::max(worst, (double)std::fabs(a.process(splat(x))[0] - x)); }
        std::printf("   the shortest window passes the signal within %.2g\n", worst);
        check(worst <= 1.0 / kAzQuant && !a.live(), "the window at its shortest is the signal itself (to the 2^-21 step), and at 0 the stage stops");
    }

    std::printf("3. The read-out: the tilt in arc-minutes\n");
    {
        Fx fx;
        struct { float knob; const char* want; } cases[] = { {0.0f, "0'"}, {0.02f, "0'"}, {0.25f, "3.8'"}, {0.4f, "9.6'"}, {0.5f, "15'"}, {0.75f, "34'"}, {1.0f, "60'"} };
        bool ok = true;
        for (const auto& c : cases)
        {
            fx.plug()->setNormalised(kAzimuth, c.knob);
            const std::string t = fx.text(kAzimuth);
            std::printf("   knob %3.0f %%: %-5s (%.2f')\n", 100 * c.knob, t.c_str(), fx.get(kAzimuth));
            ok = ok && t == c.want;
        }
        const mpc::Param& p = fx.plug()->param(kAzimuth);
        check(ok, "0', 3.8', 9.6', 15', 34', 60' along the knob (60' x the knob squared)");
        check(kAzimuth == 64 && kNumParams == 67 && p.id == "azimuth" && p.name == "Azimuth" && p.def == 0.0f && isSetting(kAzimuth)
              && !isSetting(kVisHaze) && groupOf(kAzimuth) == gAz && fx.e->dispatcher(fx.e, effCanBeAutomated, kAzimuth, 0, nullptr, 0.0f) == 1,
              "number 64, after the meters: azimuth, \"Azimuth\", resting at 0, a setting, automatable");
    }

    std::printf("4. The wander\n");
    {
        Azimuth a; a.set(44100.0f, kCtrl, 15.0f, 0.5f, 0.0f); a.reset(3);
        double mx = 0, ss = 0; long n = 0;
        for (long t = 0; t < (long)(600.0 * 44100.0 / kCtrl); ++t) { a.tick(); const double e = a.tilt - 15.0; mx = std::max(mx, std::fabs(e)); ss += e * e; ++n; }
        std::printf("   15' (knob 50 %%), 10 minutes: aw %.2f', largest excursion %.2f', RMS %.2f'\n", a.aw, mx, std::sqrt(ss / n));
        check(std::fabs(a.aw - 2.0f) < 1e-4f && mx <= 2.0 + 1e-4 && mx > 1.2 && std::sqrt(ss / n) > 0.25 * 2.0 && std::sqrt(ss / n) < 0.4 * 2.0,
              "aw = 1' x knob + 0.1 x tilt (2' at 50 %): within +-aw, RMS about a third of it");
        Azimuth z; z.set(44100.0f, kCtrl, 0.0f, 0.0f, 1.0f); z.reset(3);
        bool zero = true;
        for (int t = 0; t < 100000; ++t) { z.tick(); zero = zero && z.tilt == 0.0f && z.d == kAzMinD; }
        check(zero && !z.live(), "at knob 0 the tilt is exactly 0, wander and all");
        // through the plugin: a 10 kHz tone at SP, 5' (knob 28.9 %), against Azimuth 0
        Fx a0, a5; a0.plainTape(); a5.plainTape(); a5.set(kAzimuth, 5.0f);
        auto gen = [](long i) { return 0.02f * (float)std::sin(kTau * 10000.0 * i / 44100.0); };
        const auto y0 = a0.render(60.0, 44100.0f, gen, noMove), y5 = a5.render(60.0, 44100.0f, gen, noMove);
        const long w = 2205;   // 50 ms
        double lo = 1e9, hi = -1e9, sum = 0; int k = 0;
        for (long i = 44100; i + w <= (long)y0.size(); i += w, ++k)
        {
            const double g = dB(level(y5, 44100.0, 10000.0, i, w) / level(y0, 44100.0, 10000.0, i, w));
            lo = std::min(lo, g); hi = std::max(hi, g); sum += g;
        }
        const auto loss = [](double arcMin) { const double x = kTau / 2 * 10000.0 * std::tan(arcMin * kTau / 21600.0) / 33.35; return dB(std::fabs(std::sin(x) / x)); };
        std::printf("   10 kHz at SP, 5': %.2f dB on average (formula %.2f dB), from %.2f to %.2f dB over a minute (formula at 5' -+ 0.79': %.2f to %.2f dB)\n",
                    sum / k, loss(5.0), lo, hi, loss(5.79), loss(4.21));
        check(std::fabs(sum / k - loss(5.0)) < 0.5 && hi - lo > 0.3 && lo > loss(5.79) - 0.5 && hi < loss(4.21) + 0.5,
              "in the plugin the treble loss is the formula's and swishes within the wander's range");
    }

    std::printf("5. Older states and programs leave it at 0\n");
    {
        bool states = true; int loaded = 0;
        for (int k = 1; k < argc; ++k)
        {
            FILE* f = std::fopen(argv[k], "rb");
            if (f == nullptr) { states = false; continue; }
            std::vector<uint8_t> st; int c; while ((c = std::fgetc(f)) != EOF) st.push_back((uint8_t)c); std::fclose(f);
            Fx fx; fx.set(kAzimuth, 30.0f);
            const VstIntPtr r = fx.e->dispatcher(fx.e, effSetChunk, 0, (VstIntPtr)st.size(), st.data(), 0.0f);
            states = states && r != 0 && fx.get(kAzimuth) == 0.0f && fx.get(kHiss) == 12.0f;
            ++loaded;
        }
        std::printf("   %d older state(s) loaded over Azimuth 30'\n", loaded);
        check(states && loaded == argc - 1 && loaded >= 2, "a state from 1.6.0 and one from 1.4.0 load with Azimuth at 0");
        Fx fx; bool progs = fx.e->numPrograms == 46; char name[64];
        for (int p = 0; p < fx.e->numPrograms; ++p)
        {
            fx.set(kAzimuth, 30.0f);
            fx.e->dispatcher(fx.e, effSetProgram, 0, p, nullptr, 0.0f);
            const float want = p == 44 ? 6.5f : p == 45 ? 7.5f : 0.0f;
            progs = progs && std::fabs(fx.get(kAzimuth) - want) < 1e-4f;
            if (p >= 44) { fx.e->dispatcher(fx.e, effGetProgramNameIndexed, p, 0, name, 0.0f); std::printf("   program %d: %s, Azimuth %s, Sat Type %.0f, Hi Sat %.0f\n", p, name, fx.text(kAzimuth).c_str(), fx.get(kSatType), fx.get(kHiSat));
                           progs = progs && fx.get(kSatType) == 1.0f && std::strncmp(name, "VHS - ", 6) == 0; }
        }
        check(progs, "46 programs: 0 to 43 set Azimuth to 0, 44 and 45 are VHS programs with 6.5' and 7.5'");
    }

    std::printf("6. No clicks when Azimuth or the tape speed is turned while sound plays\n");
    for (int hq = 0; hq < 2; ++hq)
    {
        auto gen = [](long i) { return 0.3f * (float)std::sin(kTau * 300.0 * i / 44100.0); };
        // the largest second difference after the first second: a click stands out of a 300 Hz sine
        auto rough = [](const std::vector<float>& y) { double m = 0; for (size_t i = 44100; i < y.size(); ++i) m = std::max(m, (double)std::fabs(y[i] - 2 * y[i - 1] + y[i - 2])); return m; };
        double steady = 0;
        for (float az : { 0.0f, 60.0f })
            for (float spd : { 0.0f, 100.0f })
            { Fx f(44100.0f, hq); f.plainTape(); f.set(kAzimuth, az); f.set(kHiSat, spd); steady = std::max(steady, rough(f.render(2.0, 44100.0f, gen, noMove))); }
        Fx base(44100.0f, hq), moved(44100.0f, hq); base.plainTape(); moved.plainTape();
        auto speedMoves = [&](Fx& f, long pos) { if (pos == 128 * 400) f.set(kHiSat, 100.0f); if (pos == 128 * 700) f.set(kHiSat, 0.0f); };
        const double rb = rough(base.render(8.0, 44100.0f, gen, [&](long pos) { speedMoves(base, pos); }));
        const double rm = rough(moved.render(8.0, 44100.0f, gen, [&](long pos)
        {
            speedMoves(moved, pos);
            if (pos == 128 * 500) moved.set(kAzimuth, 60.0f);   // jumps
            if (pos == 128 * 900) moved.set(kAzimuth, 2.0f);
            if (pos >= 128 * 1100 && pos < 128 * 1600) moved.set(kAzimuth, 60.0f * (float)((pos / 128) % 50) / 49.0f);   // a Q-Link swept to and fro
            if (pos == 128 * 1700) moved.set(kAzimuth, 0.0f);
            if (pos == 128 * 2200) moved.set(kSatOn, 0.0f);
            if (pos == 128 * 2400) { moved.set(kAzimuth, 30.0f); moved.set(kSatOn, 1.0f); }
        }));
        std::printf("   HQ %s: largest second difference %.5f with the moves, %.5f with the speed moves alone, %.5f steady at either end\n", hq ? "on " : "off", rm, rb, steady);
        check(rm < 1.2 * std::max(rb, steady), "Azimuth jumps, sweeps, the tape speed and the Saturator switched: no step stands out");
    }

    std::printf("7. Five minutes: the running sum stays exact, nothing subnormal\n");
    {
        const float sr = 96000.0f;   // 48 kHz with HQ
        Azimuth a; a.set(sr, 2 * kCtrl, 60.0f, 1.0f, 1.0f); a.reset(5);   // the longest windows, wandering
        std::vector<int32_t> q(kAzLen); uint32_t s = 77;
        double worst = 0; long subn = 0; bool exact = true;
        const long total = (long)(300.0 * sr);
        for (long i = 0; i < total; ++i)
        {
            if (i % (2 * kCtrl) == 0) a.tick();
            s = s * 1664525u + 1013904223u;
            const float x = 0.3f + 0.2f * ((float)(s >> 8) / 8388608.0f - 1.0f);   // a DC offset under noise: the sum climbs for ever
            q[(size_t)(i & (kAzLen - 1))] = (int32_t)(x * kAzQuant);
            const float y = a.process(splat(x))[0];
            if (std::fpclassify(y) == FP_SUBNORMAL) ++subn;
            if (i > total - (long)sr)
            {
                const int di = (int)a.d;
                int64_t whole = 0; for (int k = 0; k < di; ++k) whole += q[(size_t)((i - k) & (kAzLen - 1))];
                const double ref = ((double)whole + (a.d - di) * q[(size_t)((i - di) & (kAzLen - 1))]) / a.d / kAzQuant;
                worst = std::max(worst, std::fabs(y - ref));
                exact = exact && (int32_t)(a.sum[a.w] - a.sum[(a.w - di) & (kAzLen - 1)])[0] == whole;
            }
        }
        for (float v : { a.s1, a.s2, a.sl, a.f1, a.f2, a.fl, a.d, a.r, a.tilt }) if (std::fpclassify(v) == FP_SUBNORMAL) ++subn;
        std::printf("   after 5 minutes (60' at EP, 96 kHz, a DC offset): window sums exact: %s, largest error against a fresh sum %.2g, subnormals %ld\n",
                    exact ? "yes" : "no", worst, subn);
        check(exact && worst < 1e-6 && subn == 0, "no drift, no subnormals");
    }

    std::printf("8. B-type NR decodes after it and mistracks on the dulled highs\n");
    {
        // 10 kHz, 30 dB under NR reference level, SP 5': the loss with NR Off, Play and On, each against Azimuth 0 in the same mode
        auto gen = [](long i) { return kRefLevel * 0.0316f * (float)std::sin(kTau * 10000.0 * i / 44100.0); };
        double loss[3];
        const int modes[3] = { 0, 2, 3 };
        for (int m = 0; m < 3; ++m)
        {
            Fx a0, a5; a0.plainTape(); a5.plainTape(); a0.set(kNr, (float)modes[m]); a5.set(kNr, (float)modes[m]); a5.set(kAzimuth, 5.0f);
            const auto y0 = a0.render(20.0, 44100.0f, gen, noMove), y5 = a5.render(20.0, 44100.0f, gen, noMove);
            loss[m] = dB(level(y5, 44100.0, 10000.0, 44100, 19 * 44100) / level(y0, 44100.0, 10000.0, 44100, 19 * 44100));
        }
        std::printf("   10 kHz at -30 dB, SP, 5': NR Off %.2f dB, Play %.2f dB, On %.2f dB\n", loss[0], loss[1], loss[2]);
        check(loss[2] < loss[0] - 0.5 && loss[1] < loss[0] + 0.1, "recorded and played with it (On), the same tilt costs more: the decoder mistracks");
    }

    std::printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
#endif
