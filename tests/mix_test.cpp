// Checks the Mix knob: with the Init program (neutral) any Mix setting must leave a tone
// unchanged (dry and wet line up); with a preset, 0 % must return the input (delayed), and the
// level must move smoothly between.
#include "../src/VHSVibes.cpp"

#include <cstdio>

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    return opcode == audioMasterVersion ? 2400 : 0;
}

static AEffect* make(int program)
{
    AEffect* e = VSTPluginMain(hostCb);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
    e->dispatcher(e, effSetProgram, 0, program < 0 ? 20 : program, nullptr, 0.0f);
    return e;
}
static void run(AEffect* e, std::vector<float>& L, std::vector<float>& R)
{
    for (int pos = 0; pos < (int)L.size(); pos += 128)
    {
        const int n = (int)L.size() - pos < 128 ? (int)L.size() - pos : 128;
        float* io[2] = { L.data() + pos, R.data() + pos };
        e->processReplacing(e, io, io, n);
    }
}
static double rms(const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double)v[i] * v[i]; return std::sqrt(s / (double)(b - a)); }
static double db(double x) { return x > 1e-9 ? 20.0 * std::log10(x) : -180.0; }

int main()
{
    const int N = 44100 * 4;
    std::vector<float> in((size_t)N);
    for (int i = 0; i < N; ++i) in[(size_t)i] = 0.25f * (float)std::sin(6.283185307179586 * 1000.0 * i / 44100.0) + 0.1f * (float)std::sin(6.283185307179586 * 5300.0 * i / 44100.0);
    int fails = 0;

    std::printf("Init program (neutral): output level against the input at each Mix setting\n");
    for (float mix : {0.0f, 25.0f, 50.0f, 75.0f, 100.0f})
    {
        AEffect* e = make(-1);
        static_cast<mpc::Plugin*>(e->object)->set(kMix, mix);
        std::vector<float> L = in, R = in;
        run(e, L, R);
        const double d = db(rms(L, 44100, N)) - db(rms(in, 44100, N));
        std::printf("   mix %3.0f %%: %+.2f dB\n", mix, d);
        if (std::fabs(d) > 0.3) ++fails;
        e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);
    }

    std::printf("Home Recording at Mix 0 %%: the output must be the input, only later\n");
    {
        AEffect* e = make(0);
        static_cast<mpc::Plugin*>(e->object)->set(kMix, 0.0f);
        static_cast<mpc::Plugin*>(e->object)->set(kOutput, 0.0f);
        std::vector<float> low((size_t)N);   // low notes, so a whole-sample lag can match closely
        for (int i = 0; i < N; ++i) low[(size_t)i] = 0.25f * (float)std::sin(6.283185307179586 * 200.0 * i / 44100.0) + 0.1f * (float)std::sin(6.283185307179586 * 310.0 * i / 44100.0);
        const std::vector<float>& in = low;
        std::vector<float> L = in, R = in;
        run(e, L, R);
        // find the lag with the smallest difference
        double best = 1e9; int lag = -1;
        for (int k = 0; k < 4000; ++k)
        {
            double s = 0; for (int i = 88200; i < 88200 + 4410; ++i) { const double d = (double)L[(size_t)i] - in[(size_t)(i - k)]; s += d * d; }
            if (s < best) { best = s; lag = k; }
        }
        const double resid = db(std::sqrt(best / 4410.0)) - db(rms(in, 88200, 88200 + 4410));
        std::printf("   best match at %d samples (%.1f ms) later, difference %.1f dB below the signal\n", lag, lag / 44.1, resid);
        if (resid > -30.0) ++fails;   // a whole-sample lag cannot match closer than about -36 dB
        e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);
    }

    std::printf("Home Recording, Mix swept 100 -> 0 -> 100 %% while playing: largest step between samples\n");
    {
        AEffect* e = make(0);
        static_cast<mpc::Plugin*>(e->object)->set(kNoiseOn, 0.0f);
        std::vector<float> L((size_t)N), R((size_t)N);
        for (int i = 0; i < N; ++i) L[(size_t)i] = R[(size_t)i] = 0.25f * (float)std::sin(6.283185307179586 * 100.0 * i / 44100.0);
        double maxStep = 0;
        for (int pos = 0; pos < N; pos += 128)
        {
            const float t = (float)pos / (float)N;
            static_cast<mpc::Plugin*>(e->object)->set(kMix, 100.0f * std::fabs(1.0f - 2.0f * t));
            float* io[2] = { L.data() + pos, R.data() + pos };
            e->processReplacing(e, io, io, N - pos < 128 ? N - pos : 128);
        }
        for (int i = 44100; i < N; ++i) maxStep = std::fmax(maxStep, std::fabs((double)L[(size_t)i] - L[(size_t)i - 1]));
        std::printf("   %.4f (a clean 100 Hz tone at this level steps about 0.004)\n", maxStep);
        if (maxStep > 0.03) ++fails;
        e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);
    }
    std::printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
