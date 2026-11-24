// drive.cpp: includes the plugin's source and drives it the way a host does.
//   drive [k] [blocks]   k = parameters set to random values before each block (0 = steady state)
#include SRC
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <time.h>
#include <vector>

extern "C" __attribute__((noinline, used)) void icMark(int b) { asm volatile("" :: "r"(b) : "memory"); }
extern "C" __attribute__((noinline, used)) void icEnd(int b)  { asm volatile("" :: "r"(b) : "memory"); }
static VstIntPtr hostCb(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float) { return op == audioMasterVersion ? 2400 : 0; }
static int64_t nowNs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return int64_t(t.tv_sec) * 1000000000 + t.tv_nsec; }

int main(int argc, char** argv)
{
    enum { N = 128, Warm = 200 };
    const int k = argc > 1 ? atoi(argv[1]) : 0;
    const int Blocks = argc > 2 ? std::max(atoi(argv[2]), Warm + 100) : 4000;
    AEffect* e = VSTPluginMain(hostCb);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher(e, effSetBlockSize, 0, N, nullptr, 0.0f);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
    static float L[N], R[N], oL[N], oR[N];
    float* in[2] = { L, R };
    float* out[2] = { oL, oR };
    std::vector<int64_t> ns(Blocks);
    unsigned seed = 12345;
    for (int b = 0; b < Blocks; ++b)
    {
        for (int i = 0; i < k; ++i)
        {
            const int p = rand_r(&seed) % e->numParams;
            e->setParameter(e, p, rand_r(&seed) / (float)RAND_MAX);
        }
        for (int i = 0; i < N; ++i) L[i] = R[i] = rand_r(&seed) / (float)RAND_MAX - 0.5f;
        const int64_t t0 = nowNs();
        icMark(b);
        e->processReplacing(e, in, out, N);
        icEnd(b);
        ns[b] = nowNs() - t0;
    }
    std::sort(ns.begin() + Warm, ns.end());
    const double block = N * 1e9 / 44100.0, n = Blocks - Warm;
    double sum = 0.0;
    for (int b = Warm; b < Blocks; ++b) sum += (double)ns[b];
    std::printf("mean %.2f %%  p99 %.2f %%  worst %.2f %% of a %d-frame block\n", 100.0 * sum / n / block,
                100.0 * (double)ns[Warm + (int)(n * 0.99)] / block, 100.0 * (double)ns[Blocks - 1] / block, N);
    return 0;
}
