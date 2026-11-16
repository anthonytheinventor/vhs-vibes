// Test harness for VHS Vibes: drives the plugin through its VST entry point and measures
// level, noise floor, pitch deviation and speed. Not part of the plugin.
#include "../src/VHSVibes.cpp"

#include <chrono>
#include <cstdio>

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    return opcode == audioMasterVersion ? 2400 : 0;
}

static AEffect* make()
{
    AEffect* e = VSTPluginMain(hostCb);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
    static_cast<mpc::Plugin*>(e->object)->set(kHq, 0);   // HQ is on in a new instance since 1.5; the tests start without it
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
    return e;
}

struct Result { double rmsL, rmsR, peak, fMin, fMax, fMean; };

// Runs `seconds` of a sine (amp, hz) through the plugin, measuring after the first second.
static Result run(AEffect* e, float amp, float hz, float seconds, std::vector<float>* keep = nullptr)
{
    const int total = (int)(seconds * 44100.0f);
    std::vector<float> L((size_t)total), R((size_t)total);
    for (int i = 0; i < total; ++i) L[(size_t)i] = R[(size_t)i] = amp * std::sin(6.283185307179586 * hz * i / 44100.0);
    for (int pos = 0; pos < total; pos += 128)
    {
        const int n = total - pos < 128 ? total - pos : 128;
        float* io[2] = { L.data() + pos, R.data() + pos };
        e->processReplacing(e, io, io, n);   // in place, like the MPC
    }
    Result r{};
    const int start = 44100;
    double sl = 0, sr = 0, pk = 0;
    for (int i = start; i < total; ++i)
    {
        sl += (double)L[(size_t)i] * L[(size_t)i]; sr += (double)R[(size_t)i] * R[(size_t)i];
        pk = std::fmax(pk, std::fabs((double)L[(size_t)i]));
    }
    r.rmsL = std::sqrt(sl / (total - start)); r.rmsR = std::sqrt(sr / (total - start)); r.peak = pk;
    // instantaneous frequency from rising zero crossings (linear interpolation)
    double last = -1, fmin = 1e9, fmax = 0, fsum = 0; int count = 0;
    for (int i = start + 1; i < total; ++i)
    {
        const float a = L[(size_t)i - 1], b = L[(size_t)i];
        if (a < 0.0f && b >= 0.0f)
        {
            const double t = (i - 1) + (double)(-a) / (double)(b - a);
            if (last >= 0) { const double f = 44100.0 / (t - last); fmin = std::fmin(fmin, f); fmax = std::fmax(fmax, f); fsum += f; ++count; }
            last = t;
        }
    }
    r.fMin = fmin; r.fMax = fmax; r.fMean = count ? fsum / count : 0;
    if (keep) *keep = L;
    return r;
}

static double db(double x) { return x > 1e-9 ? 20.0 * std::log10(x) : -180.0; }
static double cents(double f, double ref) { return 1200.0 * std::log2(f / ref); }

int main()
{
    const double inRms = 0.25 / std::sqrt(2.0);
    std::printf("Input: 1 kHz sine, peak 0.25 (-12 dBFS), rms %.1f dBFS\n\n", db(inRms));
    std::printf("%-21s %8s %8s %8s | %9s | %7s %7s (cents, noise off)\n", "program", "rmsL", "rmsR", "peak", "silence", "min", "max");
    // the first eight, the five that use the mic (Camcorder 89 is one of the eight), and the same
    // five on the modelled microphones
    for (int p : {0, 1, 2, 3, 4, 5, 6, 7, 10, 15, 18, 22, 34, 35, 36, 37, 38})
    {
        char name[64] = {0};
        AEffect* e = make();
        e->dispatcher(e, effSetProgram, 0, p, nullptr, 0.0f);
        e->dispatcher(e, effGetProgramNameIndexed, p, 0, name, 0.0f);
        const Result a = run(e, 0.25f, 1000.0f, 6.0f);
        e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);

        e = make();
        e->dispatcher(e, effSetProgram, 0, p, nullptr, 0.0f);
        const Result s = run(e, 0.0f, 1000.0f, 4.0f);
        e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);

        e = make();
        e->dispatcher(e, effSetProgram, 0, p, nullptr, 0.0f);
        e->setParameter(e, kNoise, 0.0f);
        e->setParameter(e, kDropouts, 0.0f);
        const Result w = run(e, 0.25f, 1000.0f, 20.0f);
        e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);

        std::printf("%-21s %8.1f %8.1f %8.1f | %9.1f | %7.1f %7.1f\n", name, db(a.rmsL), db(a.rmsR), db(a.peak),
                    db(s.rmsL), cents(w.fMin, 1000.0), cents(w.fMax, 1000.0));
    }

    // Level at different input levels for the default program (shows the compressor / clipper curve).
    std::printf("\nDefault program, level in -> out (1 kHz, noise off):\n");
    for (float amp : {0.01f, 0.03f, 0.1f, 0.25f, 0.5f, 1.0f})
    {
        AEffect* e = make();
        e->setParameter(e, kNoise, 0.0f);
        const Result a = run(e, amp, 1000.0f, 4.0f);
        e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);
        std::printf("  in %6.1f dBFS peak -> out rms %6.1f dBFS, peak %6.1f dBFS\n", db(amp), db(a.rmsL), db(a.peak));
    }

    // Everything neutral: how transparent is the chain?
    {
        AEffect* e = make();
        e->dispatcher(e, effSetProgram, 0, 7, nullptr, 0.0f);   // Clean Widener
        e->setParameter(e, kChorusOn, 0.0f);
        for (float hz : {100.0f, 1000.0f, 5000.0f, 10000.0f, 15000.0f})
        {
            const Result a = run(e, 0.25f, hz, 3.0f);
            std::printf("  neutral %7.0f Hz: out/in %+.2f dB\n", hz, db(a.rmsL) - db(inRms));
        }
        e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);
    }

    // Switch clicks: toggle the on/off switches while a low sine plays, look for jumps.
    {
        AEffect* e = make();
        e->setParameter(e, kNoise, 0.0f);
        e->setParameter(e, kWarp, 0.0f); e->setParameter(e, kFlutter, 0.0f);
        e->setParameter(e, kWow, 0.0f);  e->setParameter(e, kFlutter2, 0.0f);
        const int total = 44100 * 4;
        std::vector<float> L((size_t)total), R((size_t)total);
        for (int i = 0; i < total; ++i) L[(size_t)i] = R[(size_t)i] = 0.25f * std::sin(6.283185307179586 * 100.0 * i / 44100.0);
        int flip = 0;
        for (int pos = 0; pos < total; pos += 128)
        {
            if (pos > 44100 && (pos / 128) % 40 == 0)
            {
                const int which[4] = { kMono, kChorusOn, kMicOn, kSatOn };
                const int idx = which[(flip / 2) % 4];
                e->setParameter(e, idx, e->getParameter(e, idx) > 0.5f ? 0.0f : 1.0f);
                ++flip;
            }
            const int n = total - pos < 128 ? total - pos : 128;
            float* io[2] = { L.data() + pos, R.data() + pos };
            e->processReplacing(e, io, io, n);
        }
        double maxJump = 0;
        for (int i = 44100; i < total; ++i) maxJump = std::fmax(maxJump, std::fabs((double)L[(size_t)i] - L[(size_t)i - 1]));
        std::printf("\nSwitch toggling over a 100 Hz sine: largest sample-to-sample step %.4f (a clean 100 Hz sine at this level steps ~0.004-0.02)\n", maxJump);
        e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);
    }

    // Speed: time processReplacing only (input prepared beforehand), 60 s of audio.
    {
        const int total = 44100 * 60;
        std::vector<float> src((size_t)total);
        for (int i = 0; i < total; ++i) src[(size_t)i] = 0.25f * (float)std::sin(6.283185307179586 * 220.0 * i / 44100.0);
        struct Case { const char* name; int program; std::initializer_list<std::pair<int, float>> set; };
        const Case cases[] = {
            { "default", 0, {} },
            { "default, noise off", 0, { {kNoise, 0.0f} } },
            { "default, noise+sat off", 0, { {kNoise, 0.0f}, {kSatOn, 0.0f} } },
            { "default + chorus + mic", 0, { {kChorusOn, 1.0f}, {kMicOn, 1.0f} } },
            { "3rd Gen Copy", 2, {} },
            { "Clean Widener", 7, {} },
        };
        for (const Case& c : cases)
        {
            AEffect* e = make();
            e->dispatcher(e, effSetProgram, 0, c.program, nullptr, 0.0f);
            for (const auto& kv : c.set) e->setParameter(e, kv.first, kv.second);
            std::vector<float> L = src, R = src;
            const auto t0 = std::chrono::steady_clock::now();
            for (int pos = 0; pos < total; pos += 128)
            {
                const int n = total - pos < 128 ? total - pos : 128;
                float* io[2] = { L.data() + pos, R.data() + pos };
                e->processReplacing(e, io, io, n);
            }
            const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("%-26s %6.1f ns/sample  = %.3f %% of real time on this PC\n", c.name, sec / total * 1e9, sec / 60.0 * 100.0);
            e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);
        }
    }
    return 0;
}
