// What changed in VHS Vibes 1.5.2, measured. Every case starts from the Init program (neutral)
// unless it says otherwise. Ends with PASS or FAIL.
#include "../src/VHSVibes.cpp"

#include <chrono>
#include <cstdio>
#include <string>

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) { return opcode == audioMasterVersion ? 2400 : 0; }

const int kInit = 20;   // "Init" has this number in every version

struct Fx
{
    AEffect* e; long pos = 0;
    int block;                    // frames per call; 0 = a random number from 1 to 512 for each call
    uint32_t seed = 0x2545f491u;
    Fx(int program = kInit, bool hq = false, int blockSize = 128) : block(blockSize)
    {
        e = VSTPluginMain(hostCb);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
        e->dispatcher(e, effSetBlockSize, 0, 512, nullptr, 0.0f);
        set(kHq, hq ? 1.0f : 0.0f);
        e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
        e->dispatcher(e, effSetProgram, 0, program, nullptr, 0.0f);
    }
    ~Fx() { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f); e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f); }
    mpc::Plugin* plug() { return static_cast<mpc::Plugin*>(e->object); }
    void set(int i, float plain) { plug()->set(i, plain); }
    float get(int i) { return plug()->get(i); }
    int nextBlock()
    {
        if (block > 0) return block;
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        return 1 + (int)(seed % 512u);
    }
    // plays `seconds` of a sine at `hz` (amp 0: silence; the right channel at ampR if given) and
    // appends the output to keepL / keepR. Whole calls only: the last one may run past `seconds`.
    void play(float seconds, float amp, float hz, std::vector<float>* keepL, std::vector<float>* keepR = nullptr, float ampR = -1.0f)
    {
        static float L[512], R[512]; float* io[2] = { L, R };
        const long end = pos + (long)(seconds * 44100.0f);
        while (pos < end)
        {
            const int n = nextBlock();
            for (int i = 0; i < n; ++i)
            {
                const float s = (float)std::sin(6.283185307179586 * (double)hz * (double)(pos + i) / 44100.0);
                L[i] = amp * s; R[i] = (ampR < 0.0f ? amp : ampR) * s;
            }
            pos += n;
            e->processReplacing(e, io, io, n);
            if (keepL) keepL->insert(keepL->end(), L, L + n);
            if (keepR) keepR->insert(keepR->end(), R, R + n);
        }
    }
    std::vector<uint8_t> state()
    {
        void* data = nullptr;
        const size_t n = (size_t)e->dispatcher(e, effGetChunk, 0, 0, &data, 0.0f);
        return std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + n);
    }
    // loads a state, or only its first `size` bytes
    VstIntPtr load(const std::vector<uint8_t>& s, size_t size = (size_t)-1)
    {
        std::vector<uint8_t> copy(s.begin(), s.begin() + (long)std::min(size, s.size()));
        return e->dispatcher(e, effSetChunk, 0, (VstIntPtr)copy.size(), copy.data(), 0.0f);
    }
};

static float largestStep(const std::vector<float>& v, size_t from = 1, size_t to = (size_t)-1)
{
    float m = 0.0f;
    if (from < 1) from = 1;
    for (size_t i = from; i < v.size() && i < to; ++i) m = std::fmax(m, std::fabs(v[i] - v[i - 1]));
    return m;
}
static std::string blocks(int b) { return b == 0 ? "1 to 512 frames per call" : std::to_string(b) + " frames per call"; }

int main()
{
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("   %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };

    std::printf("1. The fade-out before a reset ends at 0 whatever the block size\n");
    {
        // A 40 Hz tone at 0.5 steps 0.003 between samples, and the 3 ms fade-out adds 0.004. At
        // 128 frames per call the control ticks line up with the calls; at the other sizes a call
        // can end inside the fade's last step, which must not cut it short. 40 program changes
        // each, at different places in the calls.
        float at128 = 0.0f, worst = 0.0f;
        for (int b : {128, 40, 100, 120, 333, 1, 0})
        {
            Fx fx(kInit, false, b); fx.set(kNoiseOn, 0);
            std::vector<float> out;
            fx.play(0.2f, 0.5f, 40.0f, nullptr);
            for (int k = 0; k < 40; ++k)
            {
                fx.e->dispatcher(fx.e, effSetProgram, 0, kInit, nullptr, 0.0f);
                fx.set(kNoiseOn, 0);
                fx.play(0.06f + 0.0013f * (float)k, 0.5f, 40.0f, &out);
            }
            const float s = largestStep(out);
            std::printf("   %-25s largest step %.4f\n", (blocks(b) + ":").c_str(), s);
            if (b == 128) at128 = s;
            else worst = std::fmax(worst, s);
        }
        check(worst < 1.25f * at128, "no block size makes the fade-out end in a step");
    }

    std::printf("2. After a reset the chorus comes in with the rest of the sound (Cho Width 50, Cho Delay 20 ms)\n");
    for (int hq = 0; hq < 2; ++hq)
    {
        // A state with the chorus on loaded while a 30 Hz tone plays, and HQ switched with the
        // chorus on. At width 50 the right side's delay moves furthest from the left's at once,
        // while the left one hardly moves at first: so the left channel, with the same tone, the
        // same fades and the same chorus, is the measure for the right. Eight start times, so the
        // tone is caught at every phase.
        for (int how = 0; how < 2; ++how)
        {
            float stepL = 0.0f, stepR = 0.0f;
            for (int k = 0; k < 8; ++k)
            {
                Fx fx(kInit, hq != 0); fx.set(kNoiseOn, 0);
                fx.set(kChorusOn, 1); fx.set(kChoWidth, 50); fx.set(kChoDelay, 20); fx.set(kChoMix, 40);
                const std::vector<uint8_t> withChorus = fx.state();
                if (how == 0) fx.set(kChorusOn, 0);
                fx.play(1.0f + 0.004f * (float)k, 0.5f, 30.0f, nullptr);
                if (how == 0) fx.load(withChorus);
                else fx.set(kHq, hq ? 0.0f : 1.0f);
                std::vector<float> L, R;
                fx.play(0.2f, 0.5f, 30.0f, &L, &R);
                stepL = std::fmax(stepL, largestStep(L));
                stepR = std::fmax(stepR, largestStep(R));
            }
            std::printf("   HQ %s, %s: largest step in the 200 ms after it, left %.4f, right %.4f\n",
                        hq ? "on " : "off", how == 0 ? "a state with the chorus on loaded" : "HQ switched with the chorus on", stepL, stepR);
            check(stepR < 1.25f * stepL, "the right channel comes in as smoothly as the left");
        }
    }

    std::printf("3. Sat Type switched while the Saturator is off\n");
    {
        // A and B play the same tone; A switches Sat Type to VHS while the Saturator is off and
        // later switches the Saturator on, B had VHS from the start. Nothing may differ.
        Fx a, b; a.set(kNoiseOn, 0); b.set(kNoiseOn, 0); b.set(kSatType, 1);
        a.set(kLoSat, 90); b.set(kLoSat, 90);
        std::vector<float> oa, ob;
        a.play(0.5f, 0.5f, 300.0f, &oa); b.play(0.5f, 0.5f, 300.0f, &ob);
        a.set(kSatType, 1);
        a.play(0.5f, 0.5f, 300.0f, &oa); b.play(0.5f, 0.5f, 300.0f, &ob);
        a.set(kSatOn, 1); b.set(kSatOn, 1);
        a.play(0.5f, 0.5f, 300.0f, &oa); b.play(0.5f, 0.5f, 300.0f, &ob);
        float diff = 0.0f, quiet = 1.0f;
        for (size_t i = 0; i < oa.size() && i < ob.size(); ++i) diff = std::fmax(diff, std::fabs(oa[i] - ob[i]));
        for (size_t i = 22050; i + 128 <= 44100; i += 128)   // the half second after the switch
        {
            float pk = 0.0f;
            for (size_t k = i; k < i + 128; ++k) pk = std::fmax(pk, std::fabs(oa[k]));
            quiet = std::fmin(quiet, pk);
        }
        std::printf("   quietest 128 samples after the switch %.3f (the tone is at 0.5); largest difference from an instance set to VHS from the start %.2e\n", quiet, diff);
        check(quiet > 0.45f, "the sound plays on: no fade");
        check(diff == 0.0f, "and once the Saturator is switched on it is the VHS one, as if it had been chosen first");
    }

    std::printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
