// render.cpp: includes the plugin's source and writes its output for a fixed, seeded run that visits
// every program, seven block sizes, random parameters, every parameter at once, many settled random
// settings, and bad input values (NaN, infinities, huge and denormal). Two builds whose output is the
// same to the bit write byte-identical files. bitcheck.sh builds and compares them.
//   render <out.bin>
// Phase boundaries are printed as byte offsets, so a difference can be traced to its phase.
#include SRC
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

static VstIntPtr hostCb(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float) { return op == audioMasterVersion ? 2400 : 0; }
static uint32_t rs = 0x1234567u;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static float rf() { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }

enum { MaxB = 512 };
static float L[MaxB], R[MaxB], oL[MaxB], oR[MaxB];
static AEffect* fx;
static FILE* out;
static long frames;
static bool bad;

static void block(int n)
{
    float* in[2] = { L, R };
    float* o[2] = { oL, oR };
    for (int i = 0; i < n; ++i)
    {
        const float a = 0.6f * std::sin(0.031f * (float)(frames + i)) + 0.3f * (rf() - 0.5f);
        L[i] = a;
        R[i] = 0.8f * a + 0.2f * (rf() - 0.5f);
    }
    if (bad && rnd() % 23 == 0)
    {
        static const float v[6] = { NAN, INFINITY, -INFINITY, 3e38f, -3e38f, 1e-40f };
        ((rnd() & 1) ? L : R)[rnd() % (uint32_t)n] = v[rnd() % 6];
    }
    fx->processReplacing(fx, in, o, n);
    fwrite(oL, sizeof(float), (size_t)n, out);
    fwrite(oR, sizeof(float), (size_t)n, out);
    frames += n;
}
static void phase(const char* name) { std::printf("phase %-28s starts at byte %ld\n", name, frames * 8); }

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    fx = VSTPluginMain(hostCb);
    out = fopen(argv[1], "wb");
    if (fx == nullptr || out == nullptr) return 1;
    fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    fx->dispatcher(fx, effSetBlockSize, 0, MaxB, nullptr, 0.0f);
    fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0.0f);
    static const int sizes[7] = { 32, 128, 512, 64, 96, 192, 256 };
    const uint32_t np = (uint32_t)(fx->numParams > 0 ? fx->numParams : 1);
    const int npr = fx->numPrograms > 0 ? fx->numPrograms : 1;

    phase("programs");                       // every program, two parameters moved partway through
    for (int p = 0; p < npr; ++p)
    {
        fx->dispatcher(fx, effSetProgram, 0, p, nullptr, 0.0f);
        for (int b = 0; b < 40; ++b)
        {
            if (b == 20) { fx->setParameter(fx, (int)(rnd() % np), rf()); fx->setParameter(fx, (int)(rnd() % np), rf()); }
            block(sizes[p % 7]);
        }
    }
    phase("4 random parameters");            // before every block, block size varying
    for (int b = 0; b < 3000; ++b)
    {
        for (int i = 0; i < 4; ++i) fx->setParameter(fx, (int)(rnd() % np), rf());
        block(sizes[rnd() % 7]);
    }
    phase("every parameter random");         // on every block
    for (int b = 0; b < 600; ++b)
    {
        for (uint32_t i = 0; i < np; ++i) fx->setParameter(fx, (int)i, rf());
        block(sizes[rnd() % 3]);
    }
    phase("settled random settings");        // the steady state of many combinations
    for (int c = 0; c < 120; ++c)
    {
        for (uint32_t i = 0; i < np; ++i) fx->setParameter(fx, (int)i, rf());
        for (int b = 0; b < 30; ++b) block(128);
    }
    phase("bad input values");               // with program changes and moving parameters
    bad = true;
    for (int b = 0; b < 3000; ++b)
    {
        if (b % 100 == 0) fx->dispatcher(fx, effSetProgram, 0, (b / 100) % npr, nullptr, 0.0f);
        if (b % 3 == 0) fx->setParameter(fx, (int)(rnd() % np), rf());
        block(sizes[rnd() % 7]);
    }
    fclose(out);
    std::printf("%d parameters, %d programs, %ld frames, %ld bytes\n", fx->numParams, fx->numPrograms, frames, frames * 8);
    return 0;
}
