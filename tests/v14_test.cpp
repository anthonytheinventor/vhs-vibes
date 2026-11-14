// What changed in VHS Vibes 1.4, measured. Starts every case from the Init program (neutral).
// Ends with PASS or FAIL.
#include "../src/VHSVibes.cpp"

#include <cstdio>
#include <mutex>

static std::mutex g_m;
static int g_hostCalls = 0;
static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterVersion) return 2400;
    std::lock_guard<std::mutex> l(g_m);
    ++g_hostCalls;
    return 0;
}
static int hostCalls() { std::lock_guard<std::mutex> l(g_m); return g_hostCalls; }

const int kInit = 20;   // "Init" has this number in every version

struct Fx
{
    AEffect* e; long pos = 0;
    Fx()
    {
        e = VSTPluginMain(hostCb);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
        e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
        e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
        e->dispatcher(e, effSetProgram, 0, kInit, nullptr, 0.0f);
    }
    ~Fx() { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f); e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f); }
    void set(int i, float plain) { static_cast<mpc::Plugin*>(e->object)->set(i, plain); }
    float get(int i) { return static_cast<mpc::Plugin*>(e->object)->get(i); }
    // plays `seconds` of a sine at `hz` and `amp` (the right channel `ampR`, if given); appends the output to keepL / keepR
    void play(float seconds, float amp, std::vector<float>* keepL, float hz = 1000.0f, std::vector<float>* keepR = nullptr, float ampR = -1.0f)
    {
        float L[128], R[128]; float* io[2] = { L, R };
        const long end = pos + (long)(seconds * 44100.0f);
        while (pos < end)
        {
            for (int i = 0; i < 128; ++i)
            {
                const float s = (float)std::sin(6.283185307179586 * (double)hz * (double)(pos + i) / 44100.0);
                L[i] = amp * s; R[i] = (ampR < 0.0f ? amp : ampR) * s;
            }
            pos += 128;
            e->processReplacing(e, io, io, 128);
            if (keepL) keepL->insert(keepL->end(), L, L + 128);
            if (keepR) keepR->insert(keepR->end(), R, R + 128);
        }
    }
};

// lowest and highest pitch (cents from `hz`) seen in `out`, from its zero crossings
static void pitchRange(const std::vector<float>& out, double hz, double& lo, double& hi)
{
    double last = -1; lo = 0; hi = 0; float pk = 0;
    for (size_t i = 1; i < out.size(); ++i)
    {
        const float a = out[i - 1], b = out[i];
        pk = std::fmax(pk, std::fabs(b));
        if (a < 0.0f && b >= 0.0f)
        {
            const double t = (double)(i - 1) + (double)(-a) / (double)(b - a);
            const bool loud = pk > 0.02f; pk = 0;
            if (!loud) { last = -1; continue; }
            if (last >= 0) { const double c = 1200.0 * std::log2((44100.0 / (t - last)) / hz); lo = std::fmin(lo, c); hi = std::fmax(hi, c); }
            last = t;
        }
    }
}
static double rms(const std::vector<float>& v, size_t from = 0, size_t to = (size_t)-1)
{
    if (to > v.size()) to = v.size();
    double s = 0; for (size_t i = from; i < to; ++i) s += (double)v[i] * v[i];
    return std::sqrt(s / (double)(to > from ? to - from : 1));
}
static double db(double x) { return 20.0 * std::log10(x > 1e-12 ? x : 1e-12); }
// lowest RMS of any 5 ms window
static double quietest(const std::vector<float>& v)
{
    double lo = 1e9;
    for (size_t i = 0; i + 220 <= v.size(); i += 110) lo = std::fmin(lo, rms(v, i, i + 220));
    return lo;
}
// what is left of `v` after the best-fitting sine at `hz` is taken out, against that sine (dB)
static double residualDb(const std::vector<float>& v, double hz)
{
    double ss = 0, cc = 0, sc = 0, vs = 0, vc = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double w = 6.283185307179586 * hz * (double)i / 44100.0, s = std::sin(w), c = std::cos(w);
        ss += s * s; cc += c * c; sc += s * c; vs += v[i] * s; vc += v[i] * c;
    }
    const double det = ss * cc - sc * sc, a = (vs * cc - vc * sc) / det, b = (vc * ss - vs * sc) / det;
    double res = 0, sig = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double w = 6.283185307179586 * hz * (double)i / 44100.0, fit = a * std::sin(w) + b * std::cos(w);
        res += (v[i] - fit) * (v[i] - fit); sig += fit * fit;
    }
    return db(std::sqrt(res / (sig > 1e-30 ? sig : 1e-30)));
}

int main()
{
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("   %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };
    double lo, hi;

    std::printf("1. A knob jump bends the pitch no further than the wobble does (1 kHz tone)\n");
    {
        Fx fx; fx.set(kWow, 100); fx.set(kWowRate, 4.0f);
        std::vector<float> steady; fx.play(3.0f, 0.25f, nullptr); fx.play(3.0f, 0.25f, &steady);
        pitchRange(steady, 1000, lo, hi);
        std::printf("   Wow 100 %% at 4 Hz, steady:            %+6.0f .. %+5.0f cents\n", lo, hi);
        fx.set(kWowRate, 0.1f);
        std::vector<float> after; fx.play(3.0f, 0.25f, &after);
        pitchRange(after, 1000, lo, hi);
        std::printf("   then Wow Rate jumps to 0.1 Hz:        %+6.0f .. %+5.0f cents in the next 3 s (1.3: -762)\n", lo, hi);
        check(lo > -150 && hi < 150, "Wow Rate 4 -> 0.1 Hz stays within 150 cents");
    }
    {
        Fx fx; fx.set(kRpm, 45.0f);
        fx.play(2.0f, 0.25f, nullptr);
        fx.set(kWarp, 90);
        std::vector<float> after; fx.play(1.0f, 0.25f, &after);
        pitchRange(after, 1000, lo, hi);
        std::printf("   Warp jumps 0 -> 90 %% at 45 rpm:       %+6.0f .. %+5.0f cents in the next 1 s (1.3: -287)\n", lo, hi);
        check(lo > -100 && hi < 100, "Warp 0 -> 90 % stays within 100 cents while the delay grows");
        std::vector<float> steady; fx.play(8.0f, 0.25f, nullptr); fx.play(4.0f, 0.25f, &steady);
        pitchRange(steady, 1000, lo, hi);
        std::printf("   Warp 90 %%, settled:                   %+6.0f .. %+5.0f cents\n", lo, hi);
        check(hi - lo > 80, "and the full wobble is there once it has settled");
    }
    {
        Fx fx; fx.set(kChorusOn, 1); fx.set(kChoMix, 100); fx.set(kChoDelay, 30.0f); fx.set(kChoSpeed, 0.05f);
        fx.play(2.0f, 0.25f, nullptr);
        fx.set(kChoDelay, 1.0f);
        std::vector<float> after; fx.play(0.5f, 0.25f, &after);
        pitchRange(after, 1000, lo, hi);
        std::printf("   Cho Delay jumps 30 -> 1 ms:           %+6.0f .. %+5.0f cents in the next 0.5 s (1.3: +3118)\n", lo, hi);
        check(lo > -100 && hi < 100, "Cho Delay 30 -> 1 ms stays within 100 cents");
    }

    std::printf("2. AGC 100 %% and Comp 100 %%: a quiet passage, then a loud hit (input peak 0.80)\n");
    for (int satOn = 0; satOn < 2; ++satOn)
    {
        Fx fx; fx.set(kDeckOn, 1); fx.set(kAgc, 100); fx.set(kComp, 100);
        if (satOn) { fx.set(kSatOn, 1); fx.set(kLoSat, 35); fx.set(kHiSat, 35); }
        fx.play(10.0f, 0.01f, nullptr);
        std::vector<float> hit; fx.play(0.5f, 0.8f, &hit);
        double pk = 0; for (float v : hit) pk = std::fmax(pk, std::fabs((double)v));
        std::printf("   saturator %s: loudest output sample %.2f (1.3: %s)\n", satOn ? "at 35 %" : "off   ", pk, satOn ? "1.3" : "5.8");
        check(pk < 2.0, "the hit stays below 2.0 (the clip is at 1.5; the filters after it overshoot a little)");
    }
    {
        Fx fx;   // Init: nothing may clip, whatever comes in
        std::vector<float> out; fx.play(0.5f, 3.0f, nullptr); fx.play(0.2f, 3.0f, &out);
        double pk = 0; for (float v : out) pk = std::fmax(pk, std::fabs((double)v));
        std::printf("   Init with a tone of peak 3.0: output peak %.2f\n", pk);
        check(pk > 2.9, "Init passes a signal far over full scale untouched");
    }

    std::printf("3. Mix and Chorus switched in while playing: no dip\n");
    {
        Fx fx; fx.set(kWarp, 35); fx.set(kWow, 25);   // some resting delay, so the dry copy has to wait
        fx.play(2.0f, 0.25f, nullptr, 100.0f);
        fx.set(kMix, 50);
        std::vector<float> after; fx.play(0.1f, 0.25f, &after, 100.0f);
        double pk = 1e9;   // lowest peak of any 10 ms (one cycle)
        for (size_t i = 0; i + 441 <= after.size(); i += 441) { double p = 0; for (size_t k = i; k < i + 441; ++k) p = std::fmax(p, std::fabs((double)after[k])); pk = std::fmin(pk, p); }
        std::printf("   Mix 100 -> 50 %%: lowest cycle peak in the next 100 ms %.3f of 0.250 (1.3: about 0.18)\n", pk);
        check(pk > 0.235, "Mix turned down: the level holds");
    }
    {
        Fx fx; fx.set(kChoMix, 100); fx.set(kChoDelay, 30.0f);
        fx.play(2.0f, 0.25f, nullptr, 100.0f);
        fx.set(kChorusOn, 1);
        std::vector<float> after; fx.play(0.1f, 0.25f, &after, 100.0f);
        double pk = 1e9;
        for (size_t i = 0; i + 441 <= after.size(); i += 441) { double p = 0; for (size_t k = i; k < i + 441; ++k) p = std::fmax(p, std::fabs((double)after[k])); pk = std::fmin(pk, p); }
        std::printf("   Chorus on (30 ms):  lowest cycle peak in the next 100 ms %.3f of 0.250\n", pk);
        check(pk > 0.2, "Chorus switched on: no drop to half while its delay fills (the chorus itself moves the level a little)");
    }

    std::printf("4. Dropouts take the highs first (Dropouts 100 %%, 60 s)\n");
    {
        double q[2];
        for (int k = 0; k < 2; ++k)
        {
            Fx fx; fx.set(kDropouts, 100);
            std::vector<float> out; fx.play(1.0f, 0.25f, nullptr, k ? 6000.0f : 200.0f); fx.play(60.0f, 0.25f, &out, k ? 6000.0f : 200.0f);
            q[k] = db(quietest(out) / (0.25 / std::sqrt(2.0)));
        }
        std::printf("   deepest 5 ms dip: 200 Hz %+.1f dB, 6 kHz %+.1f dB\n", q[0], q[1]);
        check(q[0] > -14.0 && q[0] < -3.0, "the lows dip by 3 to 14 dB");
        check(q[1] < q[0] - 12.0, "the highs dip at least 12 dB further");
    }

    std::printf("5. Tracking: the right channel crackles alone more often than the whole track is lost\n");
    {
        Fx fx; fx.set(kDeckOn, 1); fx.set(kTracking, 60);
        std::vector<float> L, R; fx.play(1.0f, 0.25f, nullptr); fx.play(120.0f, 0.25f, &L, 1000.0f, &R, 0.0f);   // tone on the left only
        // The right input is silent: sound in it is static (or the mono backup track). A burst
        // in the right channel with the left one still a clean tone is "right alone".
        int rightAlone = 0, whole = 0; bool in = false; int quiet = 0; bool sawLeft = false;
        for (size_t i = 0; i + 64 <= L.size(); i += 64)
        {
            const double r = rms(R, i, i + 64), l = rms(L, i, i + 64);
            const bool leftChanged = std::fabs(l - 0.25 / std::sqrt(2.0)) > 0.02;
            if (r > 0.01)
            {
                if (!in) { in = true; sawLeft = false; }
                sawLeft = sawLeft || leftChanged; quiet = 0;
            }
            else if (in && ++quiet > 8) { in = false; if (sawLeft) ++whole; else ++rightAlone; }
        }
        std::printf("   in 120 s at Tracking 60 %%: right channel alone %d times, the whole track %d times\n", rightAlone, whole);
        check(rightAlone > whole && whole > 20, "right alone is the commoner fault, and fall-backs still happen");
    }

    std::printf("6. Scrape: roughness around a 3 kHz tone (what is left when the tone is taken out)\n");
    {
        double r[4]; const float amount[4] = {0, 30, 60, 100};
        for (int k = 0; k < 4; ++k)
        {
            Fx fx; fx.set(kScrape, amount[k]);
            std::vector<float> out; fx.play(1.0f, 0.25f, nullptr, 3000.0f); fx.play(1.0f, 0.25f, &out, 3000.0f);
            r[k] = residualDb(out, 3000.0);
            std::printf("   Scrape %3.0f %%: %+.1f dB\n", amount[k], r[k]);
        }
        check(r[0] < -80.0, "nothing at 0 %");
        check(r[1] > -60.0 && r[1] < r[2] && r[2] < r[3] && r[3] > -24.0 && r[3] < -8.0, "rising with the knob to between -24 and -8 dB at 100 %");
        Fx fx; fx.set(kScrape, 100);
        std::vector<float> out; fx.play(1.0f, 0.0f, nullptr); fx.play(0.5f, 0.0f, &out);
        check(rms(out) == 0.0, "and silent in silence");
    }

    std::printf("7. Whistle\n");
    {
        Fx fx; fx.set(kWhistle, 50); fx.set(kNoise, 50); fx.set(kNoiseOn, 1);
        std::vector<float> out; fx.play(1.0f, 0.0f, nullptr); fx.play(1.0f, 0.0f, &out);
        int crossings = 0; for (size_t i = 1; i < out.size(); ++i) if (out[i - 1] < 0.0f && out[i] >= 0.0f) ++crossings;
        const double hz = crossings * 44100.0 / (double)out.size(), level = db(rms(out) * std::sqrt(2.0));
        std::printf("   Whistle 50 %%, Noise 50 %%: %.0f Hz at %+.1f dBFS peak\n", hz, level);
        check(std::fabs(hz - 15734.0) < 5.0, "15734 Hz");
        check(std::fabs(level + 45.0) < 1.0, "-45 dBFS at 50 %");
        fx.set(kNoiseOn, 0);
        std::vector<float> off; fx.play(0.5f, 0.0f, nullptr); fx.play(0.2f, 0.0f, &off);
        check(rms(off) == 0.0, "off with Noise On off");
    }

    std::printf("8. A NaN from the host does not stay\n");
    {
        Fx fx; fx.set(kHiCut, 9000); fx.set(kWarp, 30);
        fx.play(0.5f, 0.25f, nullptr);
        float L[128], R[128]; float* io[2] = { L, R };
        for (int i = 0; i < 128; ++i) L[i] = R[i] = 0.1f;
        L[5] = std::nanf("");
        fx.e->processReplacing(fx.e, io, io, 128);
        bool finite = true; for (int i = 0; i < 128; ++i) finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]);
        std::vector<float> after; fx.play(0.5f, 0.25f, &after);
        for (float v : after) finite = finite && std::isfinite(v);
        check(finite, "every output sample is finite");
        check(rms(after, after.size() / 2) > 0.1, "and the sound is back within a quarter of a second");
    }

    std::printf("9. The older programs do not use the new knobs; a suspended plugin is quiet\n");
    {
        Fx fx; bool clean = true; char name[64] = {0};
        for (int p = 0; p <= kInit; ++p)
        {
            fx.e->dispatcher(fx.e, effSetProgram, 0, p, nullptr, 0.0f);
            clean = clean && fx.get(kScrape) == 0.0f && fx.get(kWhistle) == 0.0f;
        }
        fx.e->dispatcher(fx.e, effGetProgramNameIndexed, kInit, 0, name, 0.0f);
        check(clean, "programs 0 to 20 leave Scrape and Whistle at 0");
        check(std::strcmp(name, "Init") == 0, "program 20 is Init");
        fx.play(0.1f, 0.1f, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        fx.e->dispatcher(fx.e, effSetProgram, 0, 3, nullptr, 0.0f);   // something for the helper thread to say
        fx.play(0.01f, 0.1f, nullptr);
        fx.e->dispatcher(fx.e, effMainsChanged, 0, 0, nullptr, 0.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));  // a call already on its way may land
        const int before = hostCalls();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        check(hostCalls() == before, "no calls to the host while suspended");
        fx.e->dispatcher(fx.e, effMainsChanged, 0, 1, nullptr, 0.0f);
        fx.play(0.05f, 0.1f, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        check(hostCalls() > before, "and it speaks again after a resume");
    }

    std::printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
