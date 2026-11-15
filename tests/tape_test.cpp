// VHS Vibes 1.5: Sat Type = VHS, the linear track's tape, measured against the figures it is
// built to (see TapeSat in the source). Ends with PASS or FAIL.
#include "../src/VHSVibes.cpp"

#include <cstdio>

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) { return opcode == audioMasterVersion ? 2400 : 0; }
static double dB(double x) { return 20.0 * std::log10(x > 1e-30 ? x : 1e-30); }

// amplitude of the `harmonic`-th harmonic of `hz` in v (whole periods, by correlation)
static double partial(const std::vector<float>& v, float sr, float hz, int harmonic)
{
    double a = 0, b = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double ph = 6.283185307179586 * hz * harmonic * (double)i / sr;
        a += v[i] * std::sin(ph); b += v[i] * std::cos(ph);
    }
    return 2.0 * std::sqrt(a * a + b * b) / (double)v.size();
}
struct Result { double gainDb, hd3; };
// a sine at `hz`, `levelDb` relative to reference level, through the tape stage recording at `recDb`
// (0 = the deck's reference recording level); gain and third harmonic
static Result tone(float sr, float hz, float levelDb, float recDb, float speed, float bias)
{
    TapeSat t; t.set(sr, (recDb - kTapeLevelMin) / kTapeLevelSpan, speed, bias);
    const float amp = kRefLevel * std::pow(10.0f, levelDb / 20.0f);
    const int settle = (int)sr / 2, n = (int)(std::floor(0.4f * hz) * sr / hz + 0.5f);   // whole periods
    std::vector<float> out;
    for (int i = 0; i < settle + n; ++i)
    {
        const float y = t.process(splat(amp * (float)std::sin(6.283185307179586 * hz * i / sr)))[0];
        if (i >= settle) out.push_back(y);
    }
    // the correlation starts at sample `settle`: the phase offset does not matter for a magnitude
    const double f1 = partial(out, sr, hz, 1);
    return { dB(f1 / amp), 3 * hz < 0.45f * sr ? partial(out, sr, hz, 3) / f1 : 0.0 };
}

struct Fx
{
    AEffect* e;
    Fx()
    {
        e = VSTPluginMain(hostCb);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
        e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
        static_cast<mpc::Plugin*>(e->object)->set(kHq, 0);
        e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
        e->dispatcher(e, effSetProgram, 0, 20, nullptr, 0.0f);   // Init
    }
    ~Fx() { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f); e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f); }
    void set(int i, float plain) { static_cast<mpc::Plugin*>(e->object)->set(i, plain); }
    float get(int i) { return static_cast<mpc::Plugin*>(e->object)->get(i); }
};

int main()
{
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("   %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };
    const float SP = 0.0f, EP = 1.0f;

    std::printf("1. A quiet sound passes; the band ends where the decks' specifications put it\n");
    for (float sr : {44100.0f, 48000.0f, 88200.0f, 96000.0f, 192000.0f})
    {
        std::printf("   %5.0f Hz rate, -40 dB: SP", sr);
        for (float hz : {100.0f, 1000.0f, 3000.0f, 10000.0f}) std::printf("  %.0f Hz %+.2f", hz, tone(sr, hz, -40, 0, SP, 0).gainDb);
        std::printf("   EP");
        for (float hz : {100.0f, 1000.0f, 4500.0f}) std::printf("  %.0f Hz %+.2f", hz, tone(sr, hz, -40, 0, EP, 0).gainDb);
        std::printf("\n");
        check(std::fabs(tone(sr, 100, -40, 0, SP, 0).gainDb) < 0.2 && std::fabs(tone(sr, 1000, -40, 0, SP, 0).gainDb) < 0.25, "flat at 100 Hz and 1 kHz");
        const double sp10 = tone(sr, 10000, -40, 0, SP, 0).gainDb, ep45 = tone(sr, 4500, -40, 0, EP, 0).gainDb;
        check(std::fabs(sp10 + 3.0) < 0.4 && std::fabs(ep45 + 3.0) < 0.4, "3 dB down at 10 kHz in SP and at 4.5 kHz in EP");
        check(std::fabs(tone(sr, 1000, -40, 8, SP, 0).gainDb) < 0.25, "and a quiet sound passes at the hottest record level too");
    }

    std::printf("2. The distortion ceiling: 3 %% third harmonic at reference level (100 nWb/m), as the format has it\n");
    {
        const Result at = tone(44100.0f, 441.0f, 0.0f, 0, SP, 0), under = tone(44100.0f, 441.0f, -10.0f, 0, SP, 0), sat = tone(44100.0f, 441.0f, 24.0f, 0, SP, 0);
        const Result at96 = tone(96000.0f, 441.0f, 0.0f, 0, SP, 0);
        std::printf("   441 Hz: 10 dB under reference %.2f %%, at reference level %.2f %% (gain %+.2f dB; at 96 kHz %.2f %%), 24 dB over it the output sits %+.1f dB over reference\n",
                    100 * under.hd3, 100 * at.hd3, at.gainDb, 100 * at96.hd3, 24.0 + sat.gainDb);
        check(at.hd3 > 0.027 && at.hd3 < 0.033 && at96.hd3 > 0.027 && at96.hd3 < 0.033, "3 % at reference level");
        check(under.hd3 < 0.005, "clean 10 dB below it");
        check(24.0 + sat.gainDb > 2.0 && 24.0 + sat.gainDb < 6.5, "and it cannot go much past 4 dB over reference: saturation");
    }

    std::printf("3. The highs give way first, and sooner at the slow speed\n");
    {
        // tones 10 dB under reference level, against the same tones played very quietly
        auto squash = [&](float sr, float hz, float speed) { return tone(sr, hz, -10, 0, speed, 0).gainDb - tone(sr, hz, -50, 0, speed, 0).gainDb; };
        const double lo = squash(44100.0f, 441.0f, SP), sp7 = squash(44100.0f, 7000.0f, SP), sp7hq = squash(88200.0f, 7000.0f, SP);
        const double sp25 = squash(44100.0f, 2500.0f, SP), ep25 = squash(44100.0f, 2500.0f, EP);
        std::printf("   10 dB under reference level: 441 Hz %+.2f dB; 7 kHz SP %+.2f dB (%+.2f dB at twice the rate); 2.5 kHz SP %+.2f dB, EP %+.2f dB\n", lo, sp7, sp7hq, sp25, ep25);
        check(std::fabs(lo) < 0.2, "a low tone 10 dB under reference is not squashed");
        check(sp7 < -1.0, "a 7 kHz tone at the same level is");
        check(std::fabs(sp7 - sp7hq) < 0.5, "and by the same amount at twice the sample rate (HQ)");
        check(ep25 < sp25 - 1.0, "in EP the same happens much lower down");
        // loud highs on top of a tone must not pull the tone down differently at another rate
        double drop[2];
        for (int r = 0; r < 2; ++r)
        {
            const float sr = r ? 192000.0f : 44100.0f;
            TapeSat t; t.set(sr, (0.0f - kTapeLevelMin) / kTapeLevelSpan, EP, 0);
            uint32_t seed = 7; double a = 0, b2 = 0; const int n = (int)sr;
            for (int i = 0; i < 2 * n; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                const float noise = 0.0316f * ((float)(seed >> 8) / 8388608.0f - 1.0f);
                const double ph = 6.283185307179586 * 1000.0 * i / sr;
                const float y = t.process(splat(kRefLevel * (float)std::sin(ph) + noise))[0];
                if (i >= n) { a += y * std::sin(ph); b2 += y * std::cos(ph); }
            }
            drop[r] = dB(2.0 * std::sqrt(a * a + b2 * b2) / n / kRefLevel);
        }
        std::printf("   1 kHz at reference level with full-band noise under it, EP: the tone comes out %+.1f dB at 44.1 kHz and %+.1f dB at 192 kHz\n", drop[0], drop[1]);
        check(std::fabs(drop[0] - drop[1]) < 2.0, "noise above the audio band does not change what the tape does to the sound");
    }

    std::printf("4. Bias and record level\n");
    {
        const Result under = tone(44100.0f, 441.0f, -4, 0, SP, -1), right = tone(44100.0f, 441.0f, -4, 0, SP, 0), over = tone(44100.0f, 441.0f, -4, 0, SP, 1);
        const double hUnder = tone(44100.0f, 6000.0f, -50, 0, SP, -1).gainDb, hRight = tone(44100.0f, 6000.0f, -50, 0, SP, 0).gainDb, hOver = tone(44100.0f, 6000.0f, -50, 0, SP, 1).gainDb;
        std::printf("   distortion of a low tone 4 dB under reference: too little bias %.2f %%, right %.2f %%, too much %.2f %%\n", 100 * under.hd3, 100 * right.hd3, 100 * over.hd3);
        std::printf("   6 kHz played quietly: too little bias %+.2f dB, right %+.2f dB, too much %+.2f dB\n", hUnder, hRight, hOver);
        check(under.hd3 > 1.5 * right.hd3, "too little bias distorts more");
        check(hUnder > hRight + 1.0 && hOver < hRight - 2.0, "too little bias is brighter, too much is duller");
        const Result cold = tone(44100.0f, 441.0f, 0, -10, SP, 0), hot = tone(44100.0f, 441.0f, 0, 8, SP, 0);
        std::printf("   reference-level tone recorded 10 dB under and 8 dB over: %.2f %% / %+.2f dB and %.2f %% / %+.2f dB\n", 100 * cold.hd3, cold.gainDb, 100 * hot.hd3, hot.gainDb);
        check(hot.hd3 > 5 * cold.hd3 && hot.gainDb < cold.gainDb - 2.0, "a hotter recording distorts more, and its loud parts come back quieter");
    }

    std::printf("5. In the plugin\n");
    {
        Fx fx; bool clean = fx.get(kSatType) == 0.0f;
        for (int p = 0; p < 27; ++p) { fx.e->dispatcher(fx.e, effSetProgram, 0, p, nullptr, 0.0f); clean = clean && fx.get(kSatType) == 0.0f; }
        check(clean, "a new instance and programs 0 to 26 use the 2-band saturator");
        char name[64] = {0};
        fx.e->dispatcher(fx.e, effGetProgramNameIndexed, 27, 0, name, 0.0f);
        fx.e->dispatcher(fx.e, effSetProgram, 0, 27, nullptr, 0.0f);
        check(std::strcmp(name, "SP Tape") == 0 && fx.get(kSatType) == 1.0f && fx.e->numPrograms == 29, "29 programs; SP Tape is number 27 and uses the tape");

        // switching the type while a tone plays, with the saturator on and both HQ settings
        for (int hq = 0; hq < 2; ++hq)
        {
            Fx sw; sw.set(kSatOn, 1); sw.set(kHq, (float)hq);
            float L[128], R[128]; float* io[2] = { L, R }; long pos = 0; float last = 0.0f, own = 0.0f, worst = 0.0f;
            for (int seg = 0; seg < 6; ++seg)
            {
                sw.set(kSatType, (float)(seg & 1));
                for (int b = 0; b < 44100 / 128; ++b)
                {
                    for (int i = 0; i < 128; ++i, ++pos) L[i] = R[i] = 0.5f * (float)std::sin(6.283185307179586 * 300.0 * pos / 44100.0);
                    sw.e->processReplacing(sw.e, io, io, 128);
                    for (int i = 0; i < 128; ++i)
                    {
                        const float stepSize = std::fabs(L[i] - last); last = L[i];
                        if (seg == 0 && b > 100) own = std::fmax(own, stepSize);
                        if (seg > 0 && b < 20) worst = std::fmax(worst, stepSize);
                    }
                }
            }
            std::printf("   HQ %d, 300 Hz tone, type switched five times: largest step %.4f (the tone's own %.4f)\n", hq, worst, own);
            check(worst < 1.4f * own, "switching the type makes no click");
        }

        // a state saved by 1.4 has no Sat Type: 2-Band
        Fx saver; void* data = nullptr;
        const VstIntPtr size = saver.e->dispatcher(saver.e, effGetChunk, 0, 0, &data, 0.0f);
        const uint8_t* d = static_cast<const uint8_t*>(data);
        std::vector<uint8_t> old(d, d + 12); size_t pos = 12; uint32_t kept = 0;
        const uint32_t count = (uint32_t)d[8] | (uint32_t)d[9] << 8 | (uint32_t)d[10] << 16 | (uint32_t)d[11] << 24;
        for (uint32_t i = 0; i < count; ++i)
        {
            const size_t len = 1 + (size_t)d[pos] + 8;
            const std::string id(reinterpret_cast<const char*>(d) + pos + 1, d[pos]);
            if (id != "nr" && id != "sat_type") { old.insert(old.end(), d + pos, d + pos + len); ++kept; }
            pos += len;
        }
        old.insert(old.end(), d + pos, d + size);
        for (int i = 0; i < 4; ++i) old[8 + (size_t)i] = (uint8_t)(kept >> (8 * i));
        Fx loader; loader.set(kSatType, 1);
        loader.e->dispatcher(loader.e, effSetChunk, 0, (VstIntPtr)old.size(), old.data(), 0.0f);
        check(kept == count - 2 && loader.get(kSatType) == 0.0f, "a project saved with 1.4 loads with the 2-band saturator");
    }

    std::printf(fails ? "\nFAIL (%d)\n" : "\nPASS\n", fails);
    return fails ? 1 : 0;
}
