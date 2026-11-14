// Measurements of the VHS Vibes deck section (AGC, Drift, Tracking, Breathing) and of the
// part of Head Buzz that rides on the music. Starts from the Init program (everything neutral)
// and switches on one feature at a time.
#include "../src/VHSVibes.cpp"

#include <cstdio>

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    return opcode == audioMasterVersion ? 2400 : 0;
}

struct Fx
{
    AEffect* e;
    Fx()
    {
        e = VSTPluginMain(hostCb);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
        e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
        e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
        e->dispatcher(e, effSetProgram, 0, 20, nullptr, 0.0f);   // Init (program 20 in every version)
        char name[64] = {0};
        e->dispatcher(e, effGetProgramNameIndexed, 20, 0, name, 0.0f);
        if (std::strcmp(name, "Init") != 0) std::printf("!! program 20 is %s, not Init\n", name);
    }
    ~Fx() { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f); e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f); }
    void set(int i, float plain) { static_cast<mpc::Plugin*>(e->object)->set(i, plain); }
    void run(std::vector<float>& L, std::vector<float>& R)
    {
        const int total = (int)L.size();
        for (int pos = 0; pos < total; pos += 128)
        {
            const int n = total - pos < 128 ? total - pos : 128;
            float* io[2] = { L.data() + pos, R.data() + pos };
            e->processReplacing(e, io, io, n);
        }
    }
};

static std::vector<float> tone(float amp, float hz, float seconds)
{
    std::vector<float> v((size_t)(seconds * 44100.0f));
    for (size_t i = 0; i < v.size(); ++i) v[i] = amp * (float)std::sin(6.283185307179586 * hz * (double)i / 44100.0);
    return v;
}
static double rms(const std::vector<float>& v, double fromSec, double toSec)
{
    const size_t a = (size_t)(fromSec * 44100), b = (size_t)(toSec * 44100) < v.size() ? (size_t)(toSec * 44100) : v.size();
    double s = 0; for (size_t i = a; i < b; ++i) s += (double)v[i] * v[i];
    return std::sqrt(s / (double)(b - a));
}
static double db(double x) { return x > 1e-9 ? 20.0 * std::log10(x) : -180.0; }

int main()
{
    const double ref = db(0.25 / std::sqrt(2.0));

    std::printf("0. Init with the deck switched on but every deck knob at 0: gain at 100 Hz / 1 kHz / 10 kHz:");
    for (float f : {100.0f, 1000.0f, 10000.0f})
    {
        Fx fx; fx.set(kDeckOn, 1);
        std::vector<float> L = tone(0.25f, f, 2.0f), R = L;
        fx.run(L, R);
        std::printf(" %+.2f dB", db(rms(L, 1.0, 2.0)) - ref);
    }
    std::printf("\n");

    std::printf("\n1. AGC at 100 %%: a quiet tone, a loud one for 2 s, then quiet again\n");
    {
        Fx fx; fx.set(kDeckOn, 1); fx.set(kAgc, 100);
        std::vector<float> L = tone(0.01f, 440, 30.0f);                  // -40 dBFS
        for (size_t i = (size_t)(12 * 44100); i < (size_t)(14 * 44100); ++i) L[i] *= 50.0f;   // 2 s at -6 dBFS
        std::vector<float> R = L, inL = L;
        fx.run(L, R);
        for (double t : {0.2, 2.0, 5.0, 11.5, 12.01, 13.5, 14.5, 15.4, 17.0, 20.0, 29.5})
            std::printf("   t=%5.2f s  in %6.1f dBFS  gain %+5.1f dB\n", t, db(rms(inL, t, t + 0.2)),
                        db(rms(L, t, t + 0.2)) - db(rms(inL, t, t + 0.2)));
        double pk = 0; for (size_t i = (size_t)(12 * 44100); i < (size_t)(12.05 * 44100); ++i) pk = std::fmax(pk, std::fabs((double)L[i]));
        std::printf("   peak in the first 50 ms of the loud part: %.2f (input peak 0.50)\n", pk);
    }

    std::printf("\n2. Tracking: fall-backs to the dull mono backup track in 60 s of a stereo signal\n");
    for (float tr : {6.0f, 25.0f, 50.0f, 85.0f})
    {
        Fx fx; fx.set(kDeckOn, 1); fx.set(kTracking, tr);
        std::vector<float> L = tone(0.25f, 700, 60.0f), R = tone(0.25f, 1100, 60.0f);
        fx.run(L, R);
        int events = 0; bool in = false; double monoTime = 0, pk = 0;
        for (size_t i = 0; i + 441 < L.size(); i += 441)
        {
            double diff = 0; for (size_t k = i; k < i + 441; ++k) { diff += std::fabs((double)L[k] - R[k]); pk = std::fmax(pk, std::fabs((double)L[k])); }
            const bool mono = diff / 441.0 < 0.02;
            if (mono) monoTime += 0.01;
            if (mono && !in) ++events;
            in = mono;
        }
        std::printf("   tracking %2.0f %%: %2d times, %.1f s in all, loudest sample %.2f (signal peak 0.25)\n", tr, events, monoTime, pk);
    }

    std::printf("\n3. Head Buzz, the part that rides on the music: residual against Head Buzz 0, by pitch of the tone\n");
    std::printf("   (Noise on, master 50 %%, every other noise at 0; the steady rasp is subtracted out)\n");
    for (float hb : {10.0f, 30.0f, 60.0f, 100.0f})
    {
        std::printf("   head buzz %3.0f %%:", hb);
        for (float f : {100.0f, 500.0f, 1000.0f, 3000.0f, 6000.0f})
        {
            std::vector<float> out[3];
            for (int k = 0; k < 3; ++k)   // 0: clean tone, 1: tone + head buzz, 2: silence + head buzz (the rasp alone)
            {
                Fx fx; fx.set(kHeadBuzz, k == 0 ? 0.0f : hb);
                std::vector<float> L = tone(k == 2 ? 0.0f : 0.25f, f, 5.0f), R = L;
                fx.run(L, R);
                out[k] = L;
            }
            std::vector<float> d(out[0].size());
            for (size_t i = 0; i < d.size(); ++i) d[i] = out[1][i] - out[0][i] - out[2][i];
            std::printf("  %4.0f Hz %6.1f dB", f, db(rms(d, 1.0, 5.0)) - db(rms(out[0], 1.0, 5.0)));
        }
        std::printf("\n");
    }

    std::printf("\n4. Drift: level of a 1 kHz and a 5 kHz tone over 60 s, in 100 ms steps\n");
    for (float amt : {30.0f, 60.0f, 100.0f})
        for (float f : {1000.0f, 5000.0f})
        {
            Fx fx; fx.set(kDeckOn, 1); fx.set(kDrift, amt);
            std::vector<float> L = tone(0.25f, f, 60.0f), R = L;
            fx.run(L, R);
            double lo = 1e9, hi = -1e9, sum = 0; int n = 0;
            for (double t = 1.0; t < 59.8; t += 0.1) { const double v = db(rms(L, t, t + 0.1)) - ref; lo = std::fmin(lo, v); hi = std::fmax(hi, v); sum += v; ++n; }
            std::printf("   drift %3.0f %%, %4.0f Hz: loudest %+5.1f dB, quietest %+5.1f dB, average %+5.1f dB\n", amt, f, hi, lo, sum / n);
        }

    std::printf("\n5. Breathing: hiss level with no signal and under a 100 Hz tone at -12 dBFS (Hiss 50 %%)\n");
    for (float br : {0.0f, 50.0f, 100.0f})
    {
        double level[2];
        for (int withTone = 0; withTone < 2; ++withTone)
        {
            std::vector<float> out[2];
            for (int noiseOn = 0; noiseOn < 2; ++noiseOn)
            {
                Fx fx; fx.set(kDeckOn, 1); fx.set(kBreathing, br); fx.set(kHiss, 50); fx.set(kNoiseOn, (float)noiseOn);
                std::vector<float> L = tone(withTone ? 0.25f : 0.0f, 100, 6.0f), R = L;
                fx.run(L, R);
                out[noiseOn] = L;
            }
            std::vector<float> d(out[0].size());
            for (size_t i = 0; i < d.size(); ++i) d[i] = out[1][i] - out[0][i];
            level[withTone] = db(rms(d, 1.0, 6.0));
        }
        std::printf("   breathing %3.0f %%: hiss %6.1f dBFS in silence, %6.1f dBFS under the tone (%+.1f dB)\n", br, level[0], level[1], level[1] - level[0]);
    }
    return 0;
}
