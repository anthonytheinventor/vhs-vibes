// drive.cpp: includes the plugin's source and drives it the way a host does.
//   drive [k] [blocks]   k = parameters set to random values before each block (0 = steady state)
//   drive list           print every parameter's index, name and current value text, then exit
// Events: set MPC_OPT_EVENTS to a text file to force settings or mode switches. One event per line,
//   <block> <parameter> <value 0..1>     set a parameter (normalized) before that block; the parameter
//                                        is its index or its name as "drive list" prints it, with _ for
//                                        spaces (case does not matter)
//   <block> program <number>             select a program before that block
// Lines starting with # are skipped. Block 0 events set the state the run starts in. What each event
// did is written to <events file>.applied with the value text the plugin reports, so a setting that
// did not land where intended shows up there: read it before trusting the run.
// Timing (meaningful on a device only): each block is timed with the thread's own CPU clock, so time
// taken by other threads on the core, the plugin's own worker threads included, does not count. The
// wall-clock line beside it does include them; a large gap between the two means the core is shared.
#include SRC
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <time.h>
#include <vector>

extern "C" __attribute__((noinline, used)) void icMark(int b) { asm volatile("" :: "r"(b) : "memory"); }
extern "C" __attribute__((noinline, used)) void icEnd(int b)  { asm volatile("" :: "r"(b) : "memory"); }
static VstIntPtr hostCb(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float) { return op == audioMasterVersion ? 2400 : 0; }
static int64_t nowNs(clockid_t c) { timespec t; clock_gettime(c, &t); return int64_t(t.tv_sec) * 1000000000 + t.tv_nsec; }
struct Event { int block, index; float value; };
static std::string paramText(AEffect* e, int i, VstInt32 op)
{
    char buf[256] = {};
    e->dispatcher(e, op, i, 0, buf, 0.0f);
    return buf;
}
static bool sameName(const std::string& a, const char* b)   // ignores case, and treats _ as a space
{
    size_t n = strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i)
    {
        const char x = a[i] == ' ' ? '_' : (char)tolower((unsigned char)a[i]);
        const char y = b[i] == ' ' ? '_' : (char)tolower((unsigned char)b[i]);
        if (x != y) return false;
    }
    return true;
}

static void report(const char* what, std::vector<int64_t>& ns, int Warm, int Blocks, int N)
{
    std::sort(ns.begin() + Warm, ns.end());
    const double block = N * 1e9 / 44100.0, n = Blocks - Warm;
    double sum = 0.0;
    for (int b = Warm; b < Blocks; ++b) sum += (double)ns[b];
    std::printf("%s: mean %.2f %%  p99 %.2f %%  worst %.2f %% of a %d-frame block\n", what, 100.0 * sum / n / block,
                100.0 * (double)ns[Warm + (int)(n * 0.99)] / block, 100.0 * (double)ns[Blocks - 1] / block, N);
}

int main(int argc, char** argv)
{
    enum { N = 128, Warm = 200 };
    AEffect* e = VSTPluginMain(hostCb);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher(e, effSetBlockSize, 0, N, nullptr, 0.0f);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
    if (argc > 1 && strcmp(argv[1], "list") == 0)
    {
        for (int i = 0; i < e->numParams; ++i)
            std::printf("%3d  %-24s %s\n", i, paramText(e, i, effGetParamName).c_str(), paramText(e, i, effGetParamDisplay).c_str());
        std::printf("%d parameters, %d programs\n", e->numParams, e->numPrograms);
        return 0;
    }
    const int k = argc > 1 ? atoi(argv[1]) : 0;
    const int Blocks = argc > 2 ? std::max(atoi(argv[2]), Warm + 100) : 4000;
    std::vector<Event> events;
    FILE* applied = nullptr;
    if (const char* path = getenv("MPC_OPT_EVENTS"))
    {
        FILE* f = fopen(path, "r");
        if (f == nullptr) { std::fprintf(stderr, "cannot open events file %s\n", path); return 2; }
        applied = fopen((std::string(path) + ".applied").c_str(), "w");
        char line[256], key[128];
        while (fgets(line, sizeof line, f))
        {
            Event ev;
            if (line[0] == '#' || sscanf(line, "%d %127s %f", &ev.block, key, &ev.value) != 3) continue;
            char* end = nullptr;
            const long idx = strtol(key, &end, 10);
            if (*end == 0) ev.index = (int)idx;
            else if (sameName("program", key)) ev.index = -1;
            else
            {
                ev.index = -2;
                for (int i = 0; i < e->numParams; ++i)
                    if (sameName(paramText(e, i, effGetParamName), key)) { ev.index = i; break; }
            }
            if (ev.index < -1 || ev.index >= e->numParams)
            {
                if (applied) std::fprintf(applied, "NOT APPLIED, no such parameter: %s", line);
                std::fprintf(stderr, "events: no such parameter: %s", line);
                continue;
            }
            events.push_back(ev);
        }
        fclose(f);
    }
    static float L[N], R[N], oL[N], oR[N];
    float* in[2] = { L, R };
    float* out[2] = { oL, oR };
    std::vector<int64_t> cpu(Blocks), wall(Blocks);
    unsigned seed = 12345;
    for (int b = 0; b < Blocks; ++b)
    {
        for (int i = 0; i < k; ++i)
        {
            const int p = rand_r(&seed) % e->numParams;
            e->setParameter(e, p, rand_r(&seed) / (float)RAND_MAX);
        }
        for (const Event& ev : events)
        {
            if (ev.block != b) continue;
            if (ev.index < 0)
            {
                e->dispatcher(e, effSetProgram, 0, (VstIntPtr)ev.value, nullptr, 0.0f);
                if (applied) std::fprintf(applied, "block %d: program %d\n", b, (int)ev.value);
            }
            else
            {
                e->setParameter(e, ev.index, ev.value);
                if (applied) std::fprintf(applied, "block %d: %d %s = %s (%.4f)\n", b, ev.index,
                                          paramText(e, ev.index, effGetParamName).c_str(),
                                          paramText(e, ev.index, effGetParamDisplay).c_str(), ev.value);
            }
        }
        for (int i = 0; i < N; ++i) L[i] = R[i] = rand_r(&seed) / (float)RAND_MAX - 0.5f;
        const int64_t w0 = nowNs(CLOCK_MONOTONIC), c0 = nowNs(CLOCK_THREAD_CPUTIME_ID);
        icMark(b);
        e->processReplacing(e, in, out, N);
        icEnd(b);
        cpu[b] = nowNs(CLOCK_THREAD_CPUTIME_ID) - c0;
        wall[b] = nowNs(CLOCK_MONOTONIC) - w0;
    }
    if (applied) fclose(applied);
    report("thread CPU time", cpu, Warm, Blocks, N);
    report("wall clock     ", wall, Warm, Blocks, N);
    return 0;
}
