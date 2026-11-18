// VHS Vibes 1.7: Visual Vibes and the picture effects' meters (Snow, Head, Streak, Grid, Haze, and
// the sun's colour in Level). Ends with PASS or FAIL.
//   visual_test [state file]   the state file holds a state saved by VHS Vibes 1.6.0 (run.sh writes it
//                              for hifi_test); without it that check is skipped.
// The meters are set by the plugin's helper thread, so the sound is played in real time here.
#include "../src/VHSVibes.cpp"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

static const int kSunFrames = kSunColours * kLevelFrames;
static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) { return opcode == audioMasterVersion ? 2400 : 0; }
static const int kFxMeters[] = { kVisSnow, kVisHead, kVisStreak, kVisGrid, kVisHaze };
static const int kFxFrames[] = { kSnowFrames, kHeadFrames, kStreakFrames, kGridFrames, kHazeFrames };

struct Fx
{
    AEffect* e; long pos = 0;
    explicit Fx(int program = 20, bool start = true)
    {
        e = VSTPluginMain(hostCb);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
        e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
        e->dispatcher(e, effSetProgram, 0, program, nullptr, 0.0f);
        set(kHq, 0);
        if (start) set(kVisualVibes, 1);   // (it rests off: the tests that play want the effects)
        if (start) e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
    }
    ~Fx() { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f); e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f); }
    mpc::Plugin* plug() const { return static_cast<mpc::Plugin*>(e->object); }
    void set(int i, float plain) { plug()->set(i, plain); }
    float get(int i) const { return plug()->get(i); }
    int frame(int i, int frames) const { return (int)std::lround(plug()->getNormalised(i) * (float)(frames - 1)); }
    std::vector<uint8_t> state() const
    {
        void* data = nullptr;
        const size_t n = (size_t)e->dispatcher(e, effGetChunk, 0, 0, &data, 0.0f);
        const uint8_t* d = static_cast<const uint8_t*>(data);
        return std::vector<uint8_t>(d, d + n);
    }
    VstIntPtr load(std::vector<uint8_t> s) { return e->dispatcher(e, effSetChunk, 0, (VstIntPtr)s.size(), s.data(), 0.0f); }
    // `seconds` of a 220 Hz tone with a little noise; paced in real time when `paced`, so the helper thread
    // runs alongside. With `seen`, the largest frame each meter showed (after each block) is kept there.
    void play(float seconds, bool paced = true, int* seen = nullptr, std::vector<float>* out = nullptr)
    {
        float L[128], R[128]; float* io[2] = { L, R };
        uint32_t s = 1;
        for (int b = 0; b < (int)(seconds * 44100.0f / 128.0f); ++b)
        {
            for (int i = 0; i < 128; ++i)
            {
                s = s * 1664525u + 1013904223u;
                L[i] = R[i] = 0.3f * std::sin(6.2831853f * 220.0f * (float)(pos + i) / 44100.0f) + 0.02f * ((float)(s >> 8) * (2.0f / 16777216.0f) - 1.0f);
            }
            pos += 128;
            e->processReplacing(e, io, io, 128);
            if (out) out->insert(out->end(), L, L + 128);
            if (seen) for (int k = 0; k < 5; ++k) seen[k] = std::max(seen[k], frame(kFxMeters[k], kFxFrames[k]));
            if (paced) std::this_thread::sleep_for(std::chrono::microseconds(2900));
        }
    }
};

// Every setting the plugin had before 1.4 (as v14_test writes such a state): this version's state
// without the parameters added since.
static std::vector<uint8_t> stateBefore14(const std::vector<uint8_t>& d)
{
    std::vector<uint8_t> out(d.begin(), d.begin() + 12);
    const uint32_t n = (uint32_t)d[8] | (uint32_t)d[9] << 8 | (uint32_t)d[10] << 16 | (uint32_t)d[11] << 24;
    uint32_t kept = 0;
    size_t pos = 12;
    for (uint32_t k = 0; k < n; ++k)
    {
        const size_t len = 1 + (size_t)d[pos] + 8;
        const std::string id(reinterpret_cast<const char*>(d.data()) + pos + 1, d[pos]);
        static const char* const added[] = { "scrape", "whistle", "hq", "nr", "sat_type", "mic_model", "agc_model",
                                             "visual_vibes", "vis_snow", "vis_head", "vis_streak", "vis_grid", "vis_haze" };
        bool skip = false;
        for (const char* a : added) skip = skip || id == a;
        if (!skip) { out.insert(out.end(), d.begin() + (long)pos, d.begin() + (long)(pos + len)); ++kept; }
        pos += len;
    }
    out.insert(out.end(), d.begin() + (long)pos, d.end());
    for (int i = 0; i < 4; ++i) out[8 + (size_t)i] = (uint8_t)(kept >> (8 * i));
    return out;
}

int main(int argc, char** argv)
{
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("   %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };

    std::printf("1. The parameters\n");
    {
        Fx fx(20, false);
        const mpc::Param& v = fx.plug()->param(kVisualVibes);
        check(fx.e->numParams == 64 && kVisualVibes == 58 && v.id == "visual_vibes" && v.name == "Visual Vibes"
              && v.kind == mpc::Param::Kind::Bool && v.def == 0.0f && v.automatable && isSetting(kVisualVibes) && groupOf(kVisualVibes) == 0,
              "64 parameters; number 58 is visual_vibes, \"Visual Vibes\", a switch, off in a new instance, automatable, no sound group");
        static const char* const ids[] = { "vis_snow", "vis_head", "vis_streak", "vis_grid", "vis_haze" };
        static const char* const names[] = { "Snow", "Head", "Streak", "Grid", "Haze" };
        bool meters = true;
        for (int k = 0; k < 5; ++k)
        {
            const mpc::Param& p = fx.plug()->param(kFxMeters[k]);
            meters = meters && kFxMeters[k] == 59 + k && p.id == ids[k] && p.name == names[k] && !p.automatable && !isSetting(kFxMeters[k])
                     && p.def == 0.0f && fx.e->dispatcher(fx.e, effCanBeAutomated, kFxMeters[k], 0, nullptr, 0.0f) == 0;
        }
        check(meters, "59 to 63: the meters Snow, Head, Streak, Grid, Haze; not automatable, not settings, resting at 0");
        check(fx.e->dispatcher(fx.e, effCanBeAutomated, kVisualVibes, 0, nullptr, 0.0f) == 1
              && fx.e->dispatcher(fx.e, effCanBeAutomated, kAnimate, 0, nullptr, 0.0f) == 1, "Visual Vibes can be automated, as Animation can");
    }

    std::printf("2. Programs do not store the meters or the switch\n");
    {
        Fx fx(20, false);   // not started: the helper thread stays out of it
        fx.set(kVisualVibes, 0.0f);
        for (int k = 0; k < 5; ++k) fx.plug()->setNormalised(kFxMeters[k], 0.5f);
        bool held = true;
        for (int prog = 0; prog < fx.e->numPrograms; ++prog)
        {
            fx.e->dispatcher(fx.e, effSetProgram, 0, prog, nullptr, 0.0f);
            held = held && fx.get(kVisualVibes) == 0.0f;
            for (int k = 0; k < 5; ++k) held = held && fx.plug()->getNormalised(kFxMeters[k]) == 0.5f;
        }
        check(held, "choosing any of the 44 programs leaves Visual Vibes off and every meter where it was");
    }

    std::printf("3. Older states\n");
    {
        Fx a(3, false); a.set(kWarp, 70); a.set(kHiss, 60); a.set(kAnimate, 0);
        const std::vector<uint8_t> old = stateBefore14(a.state());
        Fx b(20, false); b.set(kVisualVibes, 1); b.set(kHq, 1);
        b.load(old);
        bool same = true;
        for (int i = 0; i < kScrape; ++i) if (isSetting(i)) same = same && b.get(i) == a.get(i);
        check(same && b.get(kVisualVibes) == 0.0f, "a state from before 1.4: every setting as saved, Visual Vibes off");
        if (argc > 1)
        {
            std::vector<uint8_t> s;
            if (FILE* f = std::fopen(argv[1], "rb")) { int c; while ((c = std::fgetc(f)) != EOF) s.push_back((uint8_t)c); std::fclose(f); }
            Fx c(20, false); c.set(kVisualVibes, 1);
            const VstIntPtr r = s.empty() ? 0 : c.load(s);
            check(r != 0 && c.get(kSatType) == 1.0f && c.get(kSatOn) == 1.0f && std::fabs(c.get(kLoSat) - 70.0f) < 1e-3f
                  && std::fabs(c.get(kHiSat) - 25.0f) < 1e-3f && std::fabs(c.get(kSplit) - 3000.0f) < 0.5f && std::fabs(c.get(kHiss) - 12.0f) < 1e-3f
                  && c.get(kVisualVibes) == 0.0f, "the 1.6.0 state: Sat Type VHS and its knobs as saved, Visual Vibes off");
        }
        else std::printf("   (no 1.6.0 state given: skipped)\n");
        Fx d(20, false); d.set(kVisualVibes, 1);
        Fx e(20, false); e.load(d.state());
        check(e.get(kVisualVibes) == 1.0f, "a 1.7 state brings its own Visual Vibes");
    }

    std::printf("4. The meters while the sound plays\n");
    {
        auto quiet = [](Fx& f)   // every source at zero
        {
            for (int i : {(int)kHiss, (int)kHeadBuzz, (int)kDropouts, (int)kTracking, (int)kWarp, (int)kFlutter, (int)kWow, (int)kFlutter2}) f.set(i, 0.0f);
            f.set(kHiCut, 18000.0f); f.set(kSatType, 0.0f);
        };
        auto loud = [](Fx& f, int type)   // every source up
        {
            f.set(kSatType, (float)type); f.set(kHiss, 100.0f); f.set(kHeadBuzz, 100.0f); f.set(kDropouts, 100.0f);
            f.set(kWarp, 100.0f); f.set(kHiCut, 1000.0f); f.set(kDeckOn, 1.0f); f.set(kSatOn, 1.0f);   // (Init has both off)
        };
        {
            Fx f; quiet(f);
            int seen[5] = {}; f.play(1.0f, true, seen);
            check(seen[0] == 0 && seen[1] == 0 && seen[2] == 0 && seen[3] < kReelFrames && seen[4] == 0 && f.frame(kVisLevel, kSunFrames) < kLevelFrames,
                  "every source at zero: no snow, head strip, streak or haze, the straight grid, the sun in its first colour");
        }
        {
            Fx f; loud(f, 1);
            int seen[5] = {}; f.play(1.5f, true, seen);
            std::printf("   largest frames: snow %d, head %d, streak %d, grid %d, haze %d; Level %d\n", seen[0], seen[1], seen[2], seen[3], seen[4], f.frame(kVisLevel, kSunFrames));
            check(seen[0] > 2 * kSnowVariants && seen[1] > 2 * kHeadVariants && seen[2] >= 1 && seen[2] <= kStreakVariants
                  && seen[3] >= kWobbleSteps * kReelFrames && seen[4] == kHazeSteps && f.frame(kVisLevel, kSunFrames) / kLevelFrames == 1,
                  "VHS, every source up: heavy snow and head strip, streaks, the deepest bent grid, the thickest haze, the mint sun");
        }
        {
            Fx f; quiet(f); f.set(kDeckOn, 1.0f); f.set(kTracking, 100.0f);
            int seen[5] = {}; f.play(2.0f, true, seen);
            check(seen[2] > kStreakVariants, "Tracking: the band (the deck lost the Hi-Fi track)");
        }
        {
            Fx f; loud(f, 2); f.set(kHiSat, 0.0f); f.set(kHiCut, 18000.0f);
            int seen[5] = {}; f.play(3.0f, true, seen);
            // (a deep enough carrier dropout sends the path to the linear track for a moment: then the band)
            check(seen[0] > 0 && seen[1] > 2 * kHeadVariants && seen[2] >= 1 && f.frame(kVisLevel, kSunFrames) / kLevelFrames == 2,
                  "Hi-Fi: snow from the FM channel's hiss, the head strip, streaks at its carrier dropouts, the orange sun");
            f.set(kDropouts, 0.0f); f.set(kHiSat, 100.0f);   // the carrier 20 dB down: the path stays on the linear track
            int lin[5] = {}; f.play(1.0f, true, lin);
            check(lin[2] > kStreakVariants && lin[0] > 0, "Hi-Fi with the carrier 20 dB down: the band, and the linear track's snow");
        }
        {
            // Hi-Fi's snow on the same scale as the other types' (the same step at the same Hiss and Noise at
            // full carrier), and a weaker carrier pushes it up
            int at[3] = {};
            for (int k = 0; k < 3; ++k)
            {
                Fx f; quiet(f); f.set(kSatOn, 1.0f); f.set(kSatType, k == 0 ? 1.0f : 2.0f); f.set(kHiss, 40.0f); f.set(kHiSat, k == 2 ? 60.0f : 0.0f);
                int seen[5] = {}; f.play(1.0f, true, seen);
                at[k] = (seen[0] + kSnowVariants - 1) / kSnowVariants;   // the step
            }
            std::printf("   Hiss 40 %%: snow step %d in VHS, %d in Hi-Fi at full carrier, %d with the carrier 12 dB down\n", at[0], at[1], at[2]);
            check(at[0] == 2 && at[1] == at[0] && at[2] > at[1], "snow: the same steps in Hi-Fi as in VHS at full carrier, higher at a weak carrier");
        }
        {
            Fx f; loud(f, 1); f.play(0.5f);
            f.set(kAnimate, 0.0f); f.play(0.3f);
            const int grid = f.frame(kVisGrid, kGridFrames);
            int seen[5] = {}; f.play(1.0f, true, seen);
            check(seen[0] == 0 && seen[1] == 0 && seen[2] == 0 && seen[4] == 0 && grid < kReelFrames && f.frame(kVisGrid, kGridFrames) == grid
                  && f.frame(kVisLevel, kSunFrames) < kLevelFrames, "Animation off, every source up: nothing shows, the straight grid stands still, the first colour");
        }
        {
            Fx f; loud(f, 2); f.play(0.5f);
            f.set(kVisualVibes, 0.0f); f.play(0.3f);
            int seen[5] = {}, low = 1 << 30;
            for (int k = 0; k < 20; ++k) { f.play(0.05f, true, seen); low = std::min(low, f.frame(kVisGrid, kGridFrames)); }
            check(seen[0] == 0 && seen[1] == 0 && seen[2] == 0 && seen[4] == 0 && seen[3] < kReelFrames && seen[3] != low
                  && f.frame(kVisLevel, kSunFrames) < kLevelFrames, "Visual Vibes off, every source up: nothing shows, the straight grid scrolls, the first colour");
        }
    }

    std::printf("5. The sound does not change\n");
    {
        bool same = true;
        for (int prog : {5, 13, 40, 41})
        {
            std::vector<float> ref;
            for (int k = 0; k < 4; ++k)
            {
                Fx f(prog);
                f.set(kAnimate, (float)(k & 1)); f.set(kVisualVibes, (float)(k >> 1));
                std::vector<float> o; f.play(1.0f, false, nullptr, &o);
                if (k == 0) ref = o; else same = same && o == ref;
            }
        }
        check(same, "four programs (two Hi-Fi), Animation and Visual Vibes each on and off: the same output, sample for sample");
    }

    std::printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
