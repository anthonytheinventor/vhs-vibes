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
    // the helper thread only ever makes these two calls (the audio thread asks for the time)
    if (opcode != audioMasterAutomate && opcode != audioMasterIOChanged) return 0;
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
        static_cast<mpc::Plugin*>(e->object)->set(kHq, 0);   // HQ is on in a new instance since 1.5; the tests start without it
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
// Everything in the first second of `v` that is neither the tone at `hz` nor one of its
// harmonics below half the sample rate, as an RMS level: with the noise off, that is what folded back.
static double toneLevel(const std::vector<float>& v, double hz);
static double foldBack(const std::vector<float>& v, double hz)
{
    double mean = 0, power = 0;
    for (size_t i = 0; i < 44100; ++i) mean += v[i];
    mean /= 44100.0;
    for (size_t i = 0; i < 44100; ++i) power += (v[i] - mean) * (v[i] - mean);
    power /= 44100.0;
    for (double f = hz; f < 22050.0; f += hz) { const double a = toneLevel(v, f); power -= 0.5 * a * a; }
    return std::sqrt(power > 0.0 ? power : 0.0);
}
// amplitude of the component at `hz` (a whole number of Hz) in the first second of `v`
static double toneLevel(const std::vector<float>& v, double hz)
{
    double re = 0, im = 0;
    for (size_t i = 0; i < 44100 && i < v.size(); ++i)
    {
        const double w = 6.283185307179586 * hz * (double)i / 44100.0;
        re += v[i] * std::cos(w); im += v[i] * std::sin(w);
    }
    return 2.0 * std::sqrt(re * re + im * im) / 44100.0;
}
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

// A saved state as an older version wrote it: this version's state without the parameters
// added in 1.4 and since. (Format: "MPCP", version, count, then per parameter: id length, id, two floats.)
static std::vector<uint8_t> stateBefore14(AEffect* e)
{
    void* data = nullptr;
    const size_t size = (size_t)e->dispatcher(e, effGetChunk, 0, 0, &data, 0.0f);
    const uint8_t* d = static_cast<const uint8_t*>(data);
    std::vector<uint8_t> out(d, d + 12);
    const uint32_t n = (uint32_t)d[8] | (uint32_t)d[9] << 8 | (uint32_t)d[10] << 16 | (uint32_t)d[11] << 24;
    uint32_t kept = 0;
    size_t pos = 12;
    for (uint32_t k = 0; k < n; ++k)
    {
        const size_t len = 1 + (size_t)d[pos] + 8;
        const std::string id(reinterpret_cast<const char*>(d) + pos + 1, d[pos]);
        if (id != "scrape" && id != "whistle" && id != "hq" && id != "nr" && id != "sat_type" && id != "mic_model" && id != "agc_model") { out.insert(out.end(), d + pos, d + pos + len); ++kept; }
        pos += len;
    }
    out.insert(out.end(), d + pos, d + size);
    for (int i = 0; i < 4; ++i) out[8 + (size_t)i] = (uint8_t)(kept >> (8 * i));
    return out;
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
        std::vector<float> steady; fx.play(8.0f, 0.25f, nullptr); fx.play(8.0f, 0.25f, &steady);
        pitchRange(steady, 1000, lo, hi);
        std::printf("   Warp 90 %%, settled:                   %+6.0f .. %+5.0f cents\n", lo, hi);
        // the same settings chosen before the start, where nothing has to glide
        Fx ref; ref.set(kRpm, 45.0f); ref.set(kWarp, 90);
        ref.e->dispatcher(ref.e, effMainsChanged, 0, 0, nullptr, 0.0f); ref.e->dispatcher(ref.e, effMainsChanged, 0, 1, nullptr, 0.0f);
        std::vector<float> refOut; ref.play(8.0f, 0.25f, nullptr); ref.play(8.0f, 0.25f, &refOut);
        double rlo, rhi; pitchRange(refOut, 1000, rlo, rhi);
        std::printf("   Warp 90 %% from the start:             %+6.0f .. %+5.0f cents\n", rlo, rhi);
        check(std::fabs((hi - lo) - (rhi - rlo)) < 0.25 * (rhi - rlo), "once settled the wobble is as deep as when it was set from the start");
    }
    {
        // the longest glide there is: the slowest, deepest second wobble switched in at once
        Fx fx; fx.set(kWowRate, 0.1f);
        fx.play(1.0f, 0.25f, nullptr);
        fx.set(kWow, 100);
        std::vector<float> glide; fx.play(0.5f, 0.25f, nullptr); fx.play(7.5f, 0.25f, &glide);
        pitchRange(glide, 1000, lo, hi);
        std::printf("   Wow 0 -> 100 %% at 0.1 Hz, from 0.5 s to 8 s after: %+6.0f .. %+5.0f cents\n", lo, hi);
        check(lo > -70 && hi < 70, "half a second after the jump only the wobble itself is left, which is 52 cents");
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
    for (int model = 0; model < 2; ++model)   // AGC Model: Simple (as in 1.4), Modeled (1.6)
    for (int satOn = 0; satOn < 2; ++satOn)
    {
        Fx fx; fx.set(kDeckOn, 1); fx.set(kAgcModel, (float)model); fx.set(kAgc, 100); fx.set(kComp, 100);
        if (satOn) { fx.set(kSatOn, 1); fx.set(kLoSat, 35); fx.set(kHiSat, 35); }
        fx.play(10.0f, 0.01f, nullptr);
        std::vector<float> hit; fx.play(0.5f, 0.8f, &hit);
        double pk = 0; for (float v : hit) pk = std::fmax(pk, std::fabs((double)v));
        std::printf("   AGC %s, saturator %s: loudest output sample %.2f%s\n", model ? "Modeled" : "Simple ", satOn ? "at 35 %" : "off   ", pk,
                    model ? "" : satOn ? " (1.3: 1.3)" : " (1.3: 5.8)");
        check(pk < 2.0, "the hit stays below 2.0 (the clip is at 1.5; the filters after it overshoot a little)");
    }
    {
        // The clip belongs to the AGC and the make-up gain only: Init has neither.
        Fx fx; fx.set(kComp, 0);
        std::vector<float> out; fx.play(0.5f, 0.9f, nullptr); fx.play(0.2f, 0.9f, &out);
        double pk = 0; for (float v : out) pk = std::fmax(pk, std::fabs((double)v));
        std::printf("   Init with a tone of peak 0.90: output peak %.3f\n", pk);
        check(std::fabs(pk - 0.9) < 0.005, "Init passes a tone just under full scale untouched");
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
        Fx fx; fx.set(kChoMix, 100); fx.set(kChoDelay, 30.0f); fx.set(kChoSpeed, 0.05f); fx.set(kChoWidth, 0);
        fx.play(2.0f, 0.25f, nullptr, 100.0f);
        fx.set(kChorusOn, 1);
        std::vector<float> after; fx.play(0.1f, 0.25f, &after, 100.0f);
        double pk = 1e9;
        for (size_t i = 0; i + 441 <= after.size(); i += 441) { double p = 0; for (size_t k = i; k < i + 441; ++k) p = std::fmax(p, std::fabs((double)after[k])); pk = std::fmin(pk, p); }
        std::printf("   Chorus on (30 ms):  lowest cycle peak in the next 100 ms %.3f of 0.250\n", pk);
        check(pk > 0.24, "Chorus switched on: the level holds while its delay fills");
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
    for (int program : {4, 8, 20, 22})   // Camcorder 89 and Training Video (mic, Slew, tone), Init, Tube TV
    {
        Fx fx; fx.e->dispatcher(fx.e, effSetProgram, 0, program, nullptr, 0.0f);
        fx.set(kNoiseOn, 0);
        std::vector<float> before; fx.play(1.0f, 0.25f, nullptr); fx.play(0.5f, 0.25f, &before);
        float L[128], R[128]; float* io[2] = { L, R };
        for (int i = 0; i < 128; ++i) L[i] = R[i] = 0.1f;
        L[5] = std::nanf("");
        fx.e->processReplacing(fx.e, io, io, 128);
        bool finite = true; for (int i = 0; i < 128; ++i) finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]);
        std::vector<float> after; fx.play(1.0f, 0.25f, nullptr); fx.play(0.5f, 0.25f, &after);
        for (float v : after) finite = finite && std::isfinite(v);
        std::printf("   program %2d: level before %.3f, a second after the NaN %.3f\n", program, rms(before), rms(after));
        check(finite, "every output sample is finite");
        check(std::fabs(db(rms(after) / rms(before))) < 3.0, "and the sound is back at its level");
    }
    {
        Fx fx;
        fx.play(0.2f, 0.25f, nullptr);
        float L[128], R[128]; float* io[2] = { L, R };
        for (int i = 0; i < 128; ++i) { L[i] = 0.1f; R[i] = 0.1f; }
        R[100] = INFINITY;
        fx.e->processReplacing(fx.e, io, io, 128);
        bool finite = true; for (int i = 0; i < 128; ++i) finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]);
        check(finite, "an infinity in one channel is caught too");
    }

    {
        // a program chosen while bad input keeps arriving is still reported to the host
        Fx fx; fx.play(0.2f, 0.1f, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const int before = hostCalls();
        fx.e->dispatcher(fx.e, effSetProgram, 0, 2, nullptr, 0.0f);
        float L[128], R[128]; float* io[2] = { L, R };
        for (int b = 0; b < 40; ++b)
        {
            for (int i = 0; i < 128; ++i) L[i] = R[i] = 0.1f;
            L[3] = std::nanf("");
            fx.e->processReplacing(fx.e, io, io, 128);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        std::printf("   program chosen while a NaN arrives in every block: %d values sent to the host\n", hostCalls() - before);
        check(hostCalls() - before >= 50, "the host still hears about the program");
    }

    std::printf("9. The older programs do not use the new knobs; a suspended plugin is quiet\n");
    {
        Fx fx; bool clean = true; char name[64] = {0};
        for (int p = 0; p <= kInit; ++p)
        {
            fx.e->dispatcher(fx.e, effSetProgram, 0, p, nullptr, 0.0f);
            clean = clean && fx.get(kScrape) == 0.0f && fx.get(kWhistle) == 0.0f && fx.get(kHq) == 0.0f;
        }
        fx.e->dispatcher(fx.e, effGetProgramNameIndexed, kInit, 0, name, 0.0f);
        check(clean, "programs 0 to 20 leave Scrape, Whistle and HQ at 0");
        check(std::strcmp(name, "Init") == 0, "program 20 is Init");
        fx.play(0.1f, 0.1f, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        fx.e->dispatcher(fx.e, effSetProgram, 0, 3, nullptr, 0.0f);   // something for the helper thread to say
        fx.play(0.01f, 0.1f, nullptr);
        fx.e->dispatcher(fx.e, effMainsChanged, 0, 0, nullptr, 0.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));  // a call already on its way may land
        const int before = hostCalls();
        for (int k = 0; k < 6; ++k) { fx.play(0.01f, 0.1f, nullptr); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
        check(hostCalls() == before, "no calls to the host while suspended, even if audio keeps coming");
        fx.e->dispatcher(fx.e, effMainsChanged, 0, 1, nullptr, 0.0f);
        fx.play(0.05f, 0.1f, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        check(hostCalls() > before, "and it speaks again after a resume");
    }

    std::printf("10. A state saved before 1.4 sounds as it was saved, whatever the instance was doing\n");
    {
        Fx a; a.e->dispatcher(a.e, effSetProgram, 0, 0, nullptr, 0.0f); a.set(kWarp, 70);
        const std::vector<uint8_t> old = stateBefore14(a.e);
        for (int first : {-1, 21, 22})   // a fresh instance, then one on Sticky Tape and one on Tube TV, both with HQ on
        {
            Fx b;
            if (first >= 0) { b.e->dispatcher(b.e, effSetProgram, 0, first, nullptr, 0.0f); b.set(kHq, 1); }
            b.play(0.05f, 0.1f, nullptr);
            b.e->dispatcher(b.e, effSetChunk, 0, (VstIntPtr)old.size(), const_cast<uint8_t*>(old.data()), 0.0f);
            b.play(0.05f, 0.1f, nullptr);
            bool same = true;
            for (int i = 0; i < kScrape; ++i) if (isSetting(i)) same = same && b.get(i) == a.get(i);
            bool rest = true;
            for (int i = kScrape; i < kNumParams; ++i) rest = rest && b.get(i) == 0.0f;
            std::printf("   loaded into %s: Scrape %.0f, Whistle %.0f\n", first < 0 ? "a fresh instance" : first == 21 ? "Sticky Tape" : "Tube TV", b.get(kScrape), b.get(kWhistle));
            check(same, "every older parameter is as saved");
            check(rest, "the parameters added in 1.4 and since are at rest");
        }
        a.set(kScrape, 40); a.set(kWhistle, 25);
        void* data = nullptr;
        const VstIntPtr size = a.e->dispatcher(a.e, effGetChunk, 0, 0, &data, 0.0f);
        Fx b;
        b.e->dispatcher(b.e, effSetChunk, 0, size, data, 0.0f);
        check(b.get(kScrape) == 40.0f && b.get(kWhistle) == 25.0f, "a 1.4 state brings its own Scrape and Whistle");
    }

    std::printf("11. HQ: the distorting stages at twice the sample rate\n");
    {
        // Everything that folds back, not one line of it (noise, wobble and deck off).
        //  - highs at an ordinary level: only the soft part of the distortion is at work, its
        //    third harmonic lands below twice the rate, and HQ removes it;
        //  - a full-level high tone: the clippers and Slew square it off, the harmonics run far
        //    beyond twice the rate, and HQ can only take some of it away.
        struct Case { const char* name; int program; float hz, amp; double atLeast; };
        for (const Case& c : { Case{"Home Recording, 12 kHz at -15 dBFS", 0, 12000.0f, 0.18f, 30.0},
                               Case{"3rd Gen Copy, 14 kHz at -3 dBFS   ", 2, 14000.0f, 0.70f, 6.0},
                               Case{"Training Video, 15 kHz at -3 dBFS ", 8, 15000.0f, 0.70f, 6.0} })
        {
            double fold[2];
            for (int hq = 0; hq < 2; ++hq)
            {
                Fx fx; fx.e->dispatcher(fx.e, effSetProgram, 0, c.program, nullptr, 0.0f);
                fx.set(kNoiseOn, 0); fx.set(kWobbleOn, 0); fx.set(kDeckOn, 0); fx.set(kDropouts, 0); fx.set(kHq, (float)hq);
                std::vector<float> out; fx.play(2.0f, c.amp, nullptr, c.hz); fx.play(1.1f, c.amp, &out, c.hz);
                fold[hq] = db(foldBack(out, c.hz));
            }
            std::printf("   %s: fold-back HQ off %+.1f dBFS, HQ on %+.1f dBFS\n", c.name, fold[0], fold[1]);
            check(fold[1] < fold[0] - c.atLeast, c.atLeast > 20.0 ? "highs at an ordinary level: HQ takes more than 30 dB off" : "a full-level high tone: HQ takes at least 6 dB off");
        }
    }
    {
        // Init, HQ off against HQ on: the same level at every pitch, and at Mix 50 % too
        // (there the dry signal has to be lined up with the HQ path, 1.4 samples late).
        bool flat = true; double mixed[2] = {0, 0};
        for (float hz : {100.0f, 1000.0f, 10000.0f, 15000.0f, 18000.0f})
        {
            double g[2];
            for (int hq = 0; hq < 2; ++hq)
            {
                Fx fx; fx.set(kHq, (float)hq);
                std::vector<float> out; fx.play(0.5f, 0.25f, nullptr, hz); fx.play(1.1f, 0.25f, &out, hz);
                g[hq] = db(toneLevel(out, hz) / 0.25);
            }
            std::printf("   Init, %5.0f Hz: HQ off %+.3f dB, HQ on %+.3f dB\n", hz, g[0], g[1]);
            flat = flat && std::fabs(g[1] - g[0]) < 0.05;
        }
        check(flat, "HQ alone changes no level from 100 Hz to 18 kHz");
        bool lined = true;
        for (float hz : {5000.0f, 12000.0f, 15000.0f, 17000.0f, 19000.0f})
        {
            for (int hq = 0; hq < 2; ++hq)
            {
                Fx fx; fx.set(kHq, (float)hq); fx.set(kMix, 50);
                std::vector<float> out; fx.play(0.5f, 0.25f, nullptr, hz); fx.play(1.1f, 0.25f, &out, hz);
                mixed[hq] = db(toneLevel(out, hz) / 0.25);
            }
            std::printf("   Init, Mix 50 %%, %5.0f Hz: HQ off %+.3f dB, HQ on %+.3f dB\n", hz, mixed[0], mixed[1]);
            lined = lined && std::fabs(mixed[1] - mixed[0]) < 0.1;
        }
        check(lined, "at Mix 50 % the dry signal is lined up with the HQ path from 5 to 19 kHz");
    }
    {
        Fx fx;
        fx.play(1.0f, 0.25f, nullptr);
        std::vector<float> out; fx.play(0.1f, 0.25f, &out);   // the samples before the switch are kept too
        fx.set(kHq, 1);
        fx.play(0.5f, 0.25f, &out);
        bool finite = true; float step = 0.0f;
        for (size_t i = 1; i < out.size(); ++i) { finite = finite && std::isfinite(out[i]); step = std::fmax(step, std::fabs(out[i] - out[i - 1])); }
        std::printf("   HQ switched on while a 1 kHz tone plays: largest step between samples %.3f (the tone itself steps 0.036)\n", step);
        check(finite && step < 0.04, "switching HQ makes no click: no step larger than the tone makes anyway");
        check(rms(out, out.size() / 2) > 0.17, "and the sound is back within a quarter of a second");
        fx.e->dispatcher(fx.e, effSetProgram, 0, 3, nullptr, 0.0f);
        check(fx.get(kHq) > 0.5f, "choosing a program leaves HQ on");
        fx.set(kHq, 0);
        fx.e->dispatcher(fx.e, effSetProgram, 0, 5, nullptr, 0.0f);
        check(fx.get(kHq) < 0.5f, "and leaves it off");
    }

    std::printf("12. No clicks: a program chosen while sound plays, and a knob jump that moves the delay far\n");
    {
        // largest step between two samples of a 1 kHz tone at 0.25 (the tone itself steps 0.036)
        auto largestStep = [](const std::vector<float>& v) { float m = 0.0f; for (size_t i = 1; i < v.size(); ++i) m = std::fmax(m, std::fabs(v[i] - v[i - 1])); return m; };
        {
            Fx fx; fx.set(kNoiseOn, 0);
            std::vector<float> out; fx.play(1.0f, 0.25f, nullptr); fx.play(0.1f, 0.25f, &out);
            fx.e->dispatcher(fx.e, effSetProgram, 0, 12, nullptr, 0.0f);   // Subtle Glue: no noise of its own
            fx.play(0.5f, 0.25f, &out);
            const std::vector<float> tail(out.end() - 4410, out.end());   // Subtle Glue once it has settled
            std::printf("   Init -> Subtle Glue while playing: largest step %.3f (Subtle Glue itself steps %.3f, Init 0.036)\n", largestStep(out), largestStep(tail));
            check(largestStep(out) < 1.3f * largestStep(tail), "a program change fades out and in: no step much beyond the new program's own");
            check(rms(out, out.size() - 4410) > 0.1, "and the new program is playing a moment later");
        }
        {
            // a change between two programs that differ in almost every knob: the new settings
            // must not land on the old sound before it has faded out
            Fx fx; fx.e->dispatcher(fx.e, effSetProgram, 0, 2, nullptr, 0.0f);   // 3rd Gen Copy
            fx.set(kNoiseOn, 0); fx.set(kWobbleOn, 0);
            std::vector<float> out; fx.play(1.0f, 0.5f, nullptr, 300.0f); fx.play(0.1f, 0.5f, &out, 300.0f);
            const float own = largestStep(out);
            fx.e->dispatcher(fx.e, effSetProgram, 0, 20, nullptr, 0.0f);          // Init
            fx.play(0.5f, 0.5f, &out, 300.0f);
            const std::vector<float> tail(out.end() - 4410, out.end());
            std::printf("   3rd Gen Copy -> Init while playing: largest step %.3f (3rd Gen Copy steps %.3f, Init %.3f)\n", largestStep(out), own, largestStep(tail));
            check(largestStep(out) < 1.3f * std::fmax(own, largestStep(tail)), "a change between very different programs does not click either");
        }
        // quietest 2 ms of a signal, against its usual level (dB)
        auto lowest = [](const std::vector<float>& v, double usual)
        {
            double lo = 1e9;
            for (size_t i = 0; i + 88 <= v.size(); i += 44) lo = std::fmin(lo, rms(v, i, i + 88));
            return db(lo / usual);
        };
        // What a crossfade costs at worst: it runs between the sound at the old delay and at
        // the new one, and where the two copies of a steady tone are out of phase the level
        // dips for a few ms in the middle. (Printed for the record, not judged: it depends on
        // the pitch of the tone. At Mix below 100 % dry and wet drift against each other
        // whenever a wobble runs, by design, so a dip there says nothing about the crossfade.)
        for (float hz : {1000.0f, 300.0f})
        {
            Fx fx; fx.set(kWowRate, 0.1f);
            std::vector<float> out; fx.play(1.0f, 0.25f, nullptr, hz); fx.play(0.1f, 0.25f, &out, hz);
            const double usual = rms(out);
            fx.set(kWow, 100); fx.play(1.0f, 0.25f, &out, hz);
            fx.set(kWow, 0);   fx.play(3.0f, 0.25f, &out, hz);
            std::printf("   Wow 0 -> 100 -> 0 %% at 0.1 Hz, %4.0f Hz tone: quietest 2 ms %+.1f dB\n", hz, lowest(out, usual));
        }
        {
            // a knob that is still turning is glided after: no crossfades, so no dips, during the sweep
            Fx fx; fx.set(kWowRate, 0.1f);
            std::vector<float> out; fx.play(1.0f, 0.25f, nullptr, 300.0f); fx.play(0.1f, 0.25f, &out, 300.0f);
            const double usual = rms(out);
            std::vector<float> sweep;
            for (int b = 0; b < 345; ++b) { fx.set(kWow, 60.0f * (float)b / 344.0f); fx.play(128.0f / 44100.0f, 0.25f, &sweep, 300.0f); }
            std::printf("   Wow swept 0 -> 60 %% over 1 s (300 Hz tone): quietest 2 ms during the sweep %+.1f dB\n", lowest(sweep, usual));
            check(lowest(sweep, usual) > -1.5, "a knob that is still turning is glided after: no crossfade, so no dip, during the sweep");
        }
        {
            // a jump right after a program change: the delay line is still filling
            Fx fx; fx.set(kWowRate, 0.1f); fx.set(kNoiseOn, 0);
            fx.play(1.0f, 0.25f, nullptr, 300.0f);
            fx.e->dispatcher(fx.e, effSetProgram, 0, 20, nullptr, 0.0f);
            fx.set(kWowRate, 0.1f);
            std::vector<float> out; fx.play(0.02f, 0.25f, &out, 300.0f);
            fx.set(kWow, 100);
            fx.play(2.0f, 0.25f, &out, 300.0f);
            std::printf("   Wow 0 -> 100 %% 20 ms after a program change: largest step %.3f (the 300 Hz tone steps 0.011)\n", largestStep(out));
            check(largestStep(out) < 0.02f, "a jump right after a reset does not click");
        }
        for (int mixed = 0; mixed < 2; ++mixed)
        {
            Fx fx; fx.set(kWowRate, 0.1f); if (mixed) fx.set(kMix, 50);
            std::vector<float> out; fx.play(1.0f, 0.25f, nullptr); fx.play(0.1f, 0.25f, &out);
            fx.set(kWow, 100);            // the resting delay goes from 6 to about 2000 samples
            fx.play(1.0f, 0.25f, &out);
            fx.set(kWow, 0);              // and back
            fx.play(3.0f, 0.25f, &out);
            std::printf("   Wow 0 -> 100 -> 0 %% at 0.1 Hz, Mix %s: largest step %.3f\n", mixed ? "50 %" : "100 %", largestStep(out));
            check(largestStep(out) < 0.05f, "the delay crossfades to its new length and back without a click");
        }
        {
            // how soon the full wobble is there after the jump: by half a second, not after seconds
            Fx fx; fx.set(kRpm, 45.0f);
            fx.play(1.0f, 0.25f, nullptr);
            fx.set(kWarp, 90);
            std::vector<float> early; fx.play(0.5f, 0.25f, nullptr); fx.play(2.0f, 0.25f, &early);
            pitchRange(early, 1000, lo, hi);
            std::printf("   Warp 0 -> 90 %% at 45 rpm, from 0.5 to 2.5 s after: %+.0f .. %+.0f cents (settled: about -98 .. +98)\n", lo, hi);
            check(hi - lo > 150, "the full wobble is there half a second after the jump");
        }
        char name[64] = {0};
        Fx fx;
        fx.e->dispatcher(fx.e, effGetProgramNameIndexed, 23, 0, name, 0.0f);
        const bool worn = std::strcmp(name, "Worn Heads") == 0;
        fx.e->dispatcher(fx.e, effGetProgramNameIndexed, 24, 0, name, 0.0f);
        check(worn && std::strcmp(name, "Dirty Heads") == 0 && fx.e->numPrograms >= 25, "Worn Heads and Dirty Heads are programs 23 and 24");
    }

    std::printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
