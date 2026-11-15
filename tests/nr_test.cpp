// VHS Vibes 1.5: the B-type noise reduction (NR), measured against the figures the chip makers
// published for it, and against itself (decode must undo encode). Ends with PASS or FAIL.
#include "../src/VHSVibes.cpp"

#include <cstdio>

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) { return opcode == audioMasterVersion ? 2400 : 0; }

static double rmsOf(const std::vector<float>& v, size_t from) { double s = 0; for (size_t i = from; i < v.size(); ++i) s += (double)v[i] * v[i]; return std::sqrt(s / (double)(v.size() - from)); }
static double dB(double x) { return 20.0 * std::log10(x > 1e-30 ? x : 1e-30); }

// a sine at `hz`, `levelDb` relative to Dolby level, through one side of the NR for a second;
// returns the gain over the last 0.3 s in dB
static double gainOf(float sr, bool decode, float hz, float levelDb)
{
    NoiseRed nr; nr.prepare(sr, decode);
    const int n = (int)sr;
    std::vector<float> in((size_t)n), out((size_t)n);
    const float amp = kRefLevel * std::pow(10.0f, levelDb / 20.0f);
    for (int i = 0; i < n; ++i)
    {
        in[(size_t)i] = amp * (float)std::sin(6.283185307179586 * hz * i / sr);
        const Stereo x = splat(in[(size_t)i]);
        out[(size_t)i] = (decode ? nr.decode(x) : nr.encode(x))[0];
    }
    return dB(rmsOf(out, (size_t)(0.7f * sr)) / rmsOf(in, (size_t)(0.7f * sr)));
}

// something like music: tones and noise whose level keeps changing, peaks around Dolby level
static std::vector<float> programme(int n, float sr)
{
    std::vector<float> v((size_t)n);
    uint32_t seed = 99;
    for (int i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const double t = i / (double)sr, noise = (double)(seed >> 8) / 8388608.0 - 1.0;
        const double swell = 0.02 + 0.98 * std::pow(0.5 + 0.5 * std::sin(6.283185307179586 * 0.7 * t), 4.0);
        const double hit = std::fmod(t, 0.37) < 0.004 ? 1.0 : 0.0;   // sudden jumps: the clipper has to work
        v[(size_t)i] = (float)(kRefLevel * (swell * (0.4 * std::sin(6.283185307179586 * 330.0 * t) + 0.3 * std::sin(6.283185307179586 * 2700.0 * t)
                                                     + 0.2 * std::sin(6.283185307179586 * 9100.0 * t) + 0.1 * noise)
                                            + 0.9 * hit * std::sin(6.283185307179586 * 6000.0 * t)));
    }
    return v;
}

struct Fx
{
    AEffect* e;
    Fx()
    {
        e = VSTPluginMain(hostCb);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
        e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
        static_cast<mpc::Plugin*>(e->object)->set(kHq, 0);   // HQ is on in a new instance since 1.5; the tests start without it
        e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
        e->dispatcher(e, effSetProgram, 0, 20, nullptr, 0.0f);   // Init
    }
    ~Fx() { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f); e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f); }
    void set(int i, float plain) { static_cast<mpc::Plugin*>(e->object)->set(i, plain); }
    float get(int i) { return static_cast<mpc::Plugin*>(e->object)->get(i); }
    // level in dB of a sine after two seconds through the plugin
    double level(float hz, float levelDb)
    {
        float L[128], R[128]; float* io[2] = { L, R };
        const float amp = kRefLevel * std::pow(10.0f, levelDb / 20.0f);
        std::vector<float> out; long pos = 0;
        for (int b = 0; b < 2 * 44100 / 128; ++b)
        {
            for (int i = 0; i < 128; ++i, ++pos) L[i] = R[i] = amp * (float)std::sin(6.283185307179586 * hz * pos / 44100.0);
            e->processReplacing(e, io, io, 128);
            if (b > 44100 / 128) out.insert(out.end(), L, L + 128);
        }
        return dB(rmsOf(out, 0));
    }
};

int main()
{
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("   %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };

    std::printf("1. Record side against the datasheets (NE545, TEA0665): boost in dB\n");
    {
        struct { float hz, level, want; } pts[] = { {1400, -20, 4.4f}, {1400, -30, 7.5f}, {2000, -25, 7.0f}, {5000, -20, 3.2f},
                                                    {5000, -30, 8.2f}, {5000, -40, 10.3f}, {10000, -20, 2.6f}, {10000, -30, 6.5f} };
        for (float sr : {44100.0f, 48000.0f, 88200.0f, 96000.0f, 192000.0f})
        {
            double worst = 0.0;
            std::printf("   %5.0f Hz rate:", sr);
            for (auto& p : pts)
            {
                const double got = gainOf(sr, false, p.hz, p.level);
                std::printf(" %.1f(%.1f)", got, p.want);
                worst = std::fmax(worst, std::fabs(got - p.want));
            }
            std::printf("  worst error %.2f dB\n", worst);
            check(worst < (sr == 44100.0f ? 0.5 : 0.7), "all eight published figures met (the chips are specified to +/- 2 dB)");
        }
        std::printf("   low level (-60 dB):");
        for (float hz : {200.0f, 600.0f, 1200.0f, 2400.0f, 5000.0f, 10000.0f, 15000.0f}) std::printf(" %.0f Hz %+.1f", hz, gainOf(44100.0f, false, hz, -60.0f));
        std::printf("\n");
        check(std::fabs(gainOf(44100.0f, false, 5000.0f, -60.0f) - 10.3) < 0.3 && gainOf(44100.0f, false, 200.0f, -60.0f) < 1.0, "10 dB at 5 kHz, next to nothing at 200 Hz");
        double most = 0.0;
        for (float hz : {1400.0f, 5000.0f, 10000.0f}) most = std::fmax(most, std::fabs(gainOf(44100.0f, false, hz, 0.0f)));
        std::printf("   at Dolby level: at most %.2f dB\n", most);
        check(most < 0.5, "a signal at Dolby level passes unchanged");
    }

    std::printf("2. Playback side: the mirror image\n");
    {
        double worst = 0.0;
        // a tone that left the encoder at level + boost comes back at level
        for (float hz : {1400.0f, 5000.0f, 10000.0f})
            for (float level : {-20.0f, -30.0f, -40.0f})
            {
                const double boost = gainOf(44100.0f, false, hz, level);
                const double cut = gainOf(44100.0f, true, hz, level + (float)boost);
                worst = std::fmax(worst, std::fabs(boost + cut));
            }
        std::printf("   boost + cut over nine tones: at most %.3f dB apart\n", worst);
        check(worst < 0.05, "what the record side adds to a tone the playback side takes off");
        const double hiss = gainOf(44100.0f, true, 8000.0f, -50.0f);
        std::printf("   hiss-level 8 kHz through the playback side alone: %+.1f dB\n", hiss);
        check(hiss < -9.0 && hiss > -11.5, "quiet highs, and so the hiss, come down by about 10 dB");
    }

    std::printf("3. Decode undoes encode on programme material\n");
    for (float sr : {44100.0f, 96000.0f})
    {
        const std::vector<float> in = programme((int)(6 * sr), sr);
        NoiseRed enc, dec; enc.prepare(sr, false); dec.prepare(sr, true);
        std::vector<float> diff(in.size()), coded(in.size());
        int clipped = 0;
        for (size_t i = 0; i < in.size(); ++i)
        {
            const Stereo y = enc.encode(splat(in[i]));
            coded[i] = y[0];
            if (std::fabs(y[0] - in[i]) >= kNrClip * 0.9999f) ++clipped;
            diff[i] = dec.decode(y)[0] - in[i];
        }
        const double left = dB(rmsOf(diff, 0) / rmsOf(in, 0));
        float worstSample = 0.0f; for (float v : diff) worstSample = std::fmax(worstSample, std::fabs(v));
        std::printf("   %5.0f Hz rate: what is left after encode + decode %.0f dB below the signal (largest single sample %.0f dBFS); encoded %.1f dB above it; clipper worked on %d samples\n",
                    sr, -left, dB(worstSample), dB(rmsOf(coded, 0) / rmsOf(in, 0)), clipped);
        check(left < -95.0, "the pair is transparent (better than -95 dB)");
        check(dB(worstSample) < -95.0, "and no single sample is further off than -95 dBFS");
        check(clipped > 0, "also while the overshoot clipper is working");
    }

    std::printf("4. A sudden jump in level (5 kHz, -40 dB to Dolby level and back)\n");
    {
        const float sr = 44100.0f;
        NoiseRed nr; nr.prepare(sr, false);
        const int n1 = (int)sr, n2 = (int)sr, n3 = (int)sr;
        std::vector<float> in, out;
        for (int i = 0; i < n1 + n2 + n3; ++i)
        {
            const float amp = kRefLevel * (i >= n1 && i < n1 + n2 ? 1.0f : 0.01f);
            in.push_back(amp * (float)std::sin(6.283185307179586 * 5000.0 * i / sr));
            out.push_back(nr.encode(splat(in.back()))[0]);
        }
        auto gainAt = [&](int from, int len) { double a = 0, b = 0; for (int i = from; i < from + len; ++i) { a += (double)out[(size_t)i] * out[(size_t)i]; b += (double)in[(size_t)i] * in[(size_t)i]; } return dB(std::sqrt(a / b)); };
        float peak = 0.0f; for (int i = n1; i < n1 + n2; ++i) peak = std::fmax(peak, std::fabs(out[(size_t)i]));
        const double over = dB(peak / kRefLevel), first = gainAt(n1, 44), at5 = gainAt(n1 + 221, 88), settled = gainAt(n1 + n2 - 4410, 4410);
        std::printf("   up:   largest output sample %+.2f dB over Dolby level; gain in the first ms %+.2f dB, after 5 ms %+.2f dB, settled %+.2f dB\n", over, first, at5, settled);
        check(over < 1.2, "the overshoot clipper holds the jump: no more than about 1 dB over");
        check(std::fabs(at5 - settled) < 1.0, "within 1 dB of its final gain after 5 ms");
        int back = -1;
        for (int ms = 0; ms < 900 && back < 0; ms += 5) if (gainAt(n1 + n2 + (int)(ms * 0.001f * sr), 221) > 9.3) back = ms;
        std::printf("   down: the full boost is back (within 1 dB) after %d ms\n", back);
        check(back >= 40 && back <= 500, "recovery takes of the order of 100 ms, not an instant");
    }

    std::printf("5. Mistracking: what the tape does between the two sides\n");
    {
        const float sr = 44100.0f;
        auto chain = [&](float hz, float level, float tapeGainDb, float tapeLpHz)
        {
            NoiseRed enc, dec; enc.prepare(sr, false); dec.prepare(sr, true);
            const float tg = std::pow(10.0f, tapeGainDb / 20.0f), lp = tapeLpHz > 0.0f ? 1.0f - std::exp(-6.2831853f * tapeLpHz / sr) : 1.0f;
            const int n = (int)sr;
            std::vector<float> in((size_t)n), out((size_t)n), tape((size_t)n);
            float z = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                in[(size_t)i] = kRefLevel * std::pow(10.0f, level / 20.0f) * (float)std::sin(6.283185307179586 * hz * i / sr);
                z += lp * (enc.encode(splat(in[(size_t)i]))[0] * tg - z);
                tape[(size_t)i] = z;
                out[(size_t)i] = dec.decode(splat(z))[0];
            }
            const size_t from = (size_t)(0.7f * sr);
            struct R { double total, tapeAlone; };
            // the tape's own loss on a plain signal, for comparison
            float z2 = 0.0f; std::vector<float> plain((size_t)n);
            for (int i = 0; i < n; ++i) { z2 += lp * (in[(size_t)i] * tg - z2); plain[(size_t)i] = z2; }
            return R{ dB(rmsOf(out, from) / rmsOf(in, from)), dB(rmsOf(plain, from) / rmsOf(in, from)) };
        };
        const auto level2 = chain(5000.0f, -30.0f, -2.0f, 0.0f);
        std::printf("   tape plays 2 dB quiet, 5 kHz at -30 dB: comes back %+.2f dB (the 2 dB, and %.2f dB more from the decoder)\n", level2.total, -(level2.total + 2.0));
        check(level2.total < -2.2 && level2.total > -5.0, "a level error is made larger, by a small amount (datasheet figure 18)");
        const auto dull = chain(8000.0f, -25.0f, 0.0f, 4000.0f);
        std::printf("   tape has lost its highs (6 dB/octave from 4 kHz), 8 kHz at -25 dB: tape alone %+.1f dB, with NR %+.1f dB\n", dull.tapeAlone, dull.total);
        check(dull.total < dull.tapeAlone - 1.0, "a dull tape comes back duller through the decoder");
    }

    std::printf("6. In the plugin\n");
    {
        Fx off, rec, both;
        rec.set(kNr, 1); both.set(kNr, 3);
        const double lOff = off.level(5000.0f, -30.0f), lRec = rec.level(5000.0f, -30.0f), lBoth = both.level(5000.0f, -30.0f);
        std::printf("   Init, 5 kHz at -30 dB: NR Rec %+.2f dB against Off; NR On %+.2f dB against Off\n", lRec - lOff, lBoth - lOff);
        check(std::fabs(lRec - lOff - 8.2) < 0.6, "Rec: the record side's boost, alone");
        check(std::fabs(lBoth - lOff) < 0.5, "On: both sides, and Init between them changes next to nothing");
        Fx play; play.set(kNr, 2);
        const double lPlay = play.level(8000.0f, -50.0f) - off.level(8000.0f, -50.0f);
        std::printf("   Init, 8 kHz at -50 dB: NR Play %+.2f dB against Off\n", lPlay);
        check(lPlay < -9.0, "Play: the playback side's cut, alone");

        // switching NR while a tone plays: the largest step from one sample to the next, against the tone's own
        for (float hz : {100.0f, 5000.0f})
        {
            const float amp = hz < 1000.0f ? 0.8f : kRefLevel * 0.0316f;
            Fx sw; float L[128], R[128]; float* io[2] = { L, R }; long pos = 0; float last = 0.0f, own = 0.0f, worst = 0.0f;
            const int modes[] = { 0, 3, 0, 2, 1, 0, 1, 3, 2, 0 };
            for (int seg = 0; seg < 10; ++seg)
            {
                sw.set(kNr, (float)modes[seg]);
                for (int b = 0; b < 44100 / 128; ++b)
                {
                    for (int i = 0; i < 128; ++i, ++pos) L[i] = R[i] = amp * (float)std::sin(6.283185307179586 * hz * pos / 44100.0);
                    sw.e->processReplacing(sw.e, io, io, 128);
                    for (int i = 0; i < 128; ++i)
                    {
                        const float stepSize = std::fabs(L[i] - last); last = L[i];
                        if (seg == 0 && b > 100) own = std::fmax(own, stepSize);
                        if (seg > 0 && b < 20) worst = std::fmax(worst, stepSize);   // the 58 ms after each switch
                    }
                }
            }
            std::printf("   %4.0f Hz tone, NR switched nine times: largest step %.4f (the tone's own %.4f)\n", hz, worst, own);
            check(worst < (hz < 1000.0f ? 1.3f : 3.6f) * own, hz < 1000.0f ? "switching NR under a loud low tone makes no click" : "under a quiet 5 kHz tone the step stays what the boost itself makes it");
        }

        // the TV's whistle is not on the tape: the playback side must leave it alone
        {
            Fx a, b; double lv[2]; int k = 0;
            for (Fx* f : {&a, &b})
            {
                f->set(kWhistle, 100); f->set(kNoise, 100); f->set(kNoiseOn, 1);
                if (f == &b) f->set(kNr, 2);
                lv[k++] = f->level(1000.0f, -200.0f);   // silence in: only the whistle comes out
            }
            std::printf("   Whistle alone: %.1f dBFS with NR off, %.1f dBFS with NR on Play\n", lv[0], lv[1]);
            check(lv[0] > -40.0 && std::fabs(lv[0] - lv[1]) < 0.1, "the playback side does not turn the whistle down");
        }

        Fx fx; bool clean = true;
        for (int p = 0; p < 25; ++p) { fx.e->dispatcher(fx.e, effSetProgram, 0, p, nullptr, 0.0f); clean = clean && fx.get(kNr) == 0.0f; }
        check(clean, "programs 0 to 24 leave NR off");

        // a state saved by 1.4 has no NR in it: it must come up Off, also in an instance that had it on
        Fx saver; void* data = nullptr;
        const VstIntPtr size = saver.e->dispatcher(saver.e, effGetChunk, 0, 0, &data, 0.0f);
        const uint8_t* d = static_cast<const uint8_t*>(data);
        std::vector<uint8_t> old(d, d + 12); size_t pos = 12; uint32_t kept = 0;
        const uint32_t count = (uint32_t)d[8] | (uint32_t)d[9] << 8 | (uint32_t)d[10] << 16 | (uint32_t)d[11] << 24;
        for (uint32_t i = 0; i < count; ++i)
        {
            const size_t len = 1 + (size_t)d[pos] + 8;
            if (std::string(reinterpret_cast<const char*>(d) + pos + 1, d[pos]) != "nr") { old.insert(old.end(), d + pos, d + pos + len); ++kept; }
            pos += len;
        }
        old.insert(old.end(), d + pos, d + size);
        for (int i = 0; i < 4; ++i) old[8 + (size_t)i] = (uint8_t)(kept >> (8 * i));
        Fx loader; loader.set(kNr, 3);
        loader.e->dispatcher(loader.e, effSetChunk, 0, (VstIntPtr)old.size(), old.data(), 0.0f);
        std::printf("   a 1.4 state (%u of %u values) loaded into an instance with NR On: NR reads %.0f\n", kept, count, loader.get(kNr));
        check(kept == count - 1 && loader.get(kNr) == 0.0f, "a project saved with 1.4 loads with NR off");

        // HQ: on in a new instance; a state keeps the HQ it was saved with; one from before HQ existed gets none
        AEffect* raw = VSTPluginMain(hostCb);
        const bool freshOn = raw->getParameter(raw, kHq) > 0.5f;
        raw->dispatcher(raw, effClose, 0, 0, nullptr, 0.0f);
        check(freshOn, "a new instance has HQ on");
        Fx hqOn; hqOn.set(kHq, 1);
        hqOn.e->dispatcher(hqOn.e, effSetChunk, 0, (VstIntPtr)old.size(), old.data(), 0.0f);   // saved above with HQ off
        check(hqOn.get(kHq) == 0.0f, "a state saved with HQ off loads with HQ off");
        std::vector<uint8_t> older(d, d + 12); pos = 12; kept = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            const size_t len = 1 + (size_t)d[pos] + 8;
            const std::string id(reinterpret_cast<const char*>(d) + pos + 1, d[pos]);
            if (id != "nr" && id != "hq" && id != "scrape" && id != "whistle") { older.insert(older.end(), d + pos, d + pos + len); ++kept; }
            pos += len;
        }
        older.insert(older.end(), d + pos, d + size);
        for (int i = 0; i < 4; ++i) older[8 + (size_t)i] = (uint8_t)(kept >> (8 * i));
        Fx from13; from13.set(kHq, 1);
        from13.e->dispatcher(from13.e, effSetChunk, 0, (VstIntPtr)older.size(), older.data(), 0.0f);
        check(kept == count - 4 && from13.get(kHq) == 0.0f, "a project saved with 1.3 (no HQ in it) loads with HQ off, as it sounded");
    }

    std::printf(fails ? "\nFAIL (%d)\n" : "\nPASS\n", fails);
    return fails ? 1 : 0;
}
