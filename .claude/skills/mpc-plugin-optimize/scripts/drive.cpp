// drive.cpp: includes the plugin's source and drives it the way a host does.
//   drive [k] [blocks]   k = parameters set to random values before each block (0 = steady state)
//   drive list           print every parameter's index, name and current value text, then exit
// Events: set MPC_OPT_EVENTS to a text file to force settings, mode switches or notes. One per line,
//   <block> <parameter> <value 0..1>     set a parameter (normalized) before that block; the parameter
//                                        is its index or its name as "drive list" prints it, with _ for
//                                        spaces (case does not matter)
//   <block> program <number>             select a program before that block
//   <block> note <pitch>                 note on, velocity 100         <block> off <pitch>   note off
//   <block> chord <count>                count notes on, spread over three octaves from pitch 36, as
//                                        the Open MPC bench plays them
//   <block> chordoff <count>             the matching notes off
//   <block> restrike <period>            from this block on, every <period> blocks: the last chord off
//                                        and on again
// Lines starting with # are skipped. Block 0 events set the state the run starts in. What each event
// did is written to <events file>.applied with the value text the plugin reports, so a setting that
// did not land where intended shows up there: read it before trusting the run.
// An instrument makes no sound without notes: give it a chord at block 0, or the run counts silence.
// Random parameters (k above 0) leave out parameters the plugin reports as momentary triggers (opcode
// 0x7F01, as the Open MPC bench asks) and those named in MPC_OPT_SKIP (indexes or names, commas between).
// MPC_OPT_MARK=all counts and times the whole host block: events, parameter writes and the process
// call, as the Open MPC bench does. The default is the process call alone.
// Timing (meaningful on a device only): each block is timed with the thread's own CPU clock, so time
// taken by other threads on the core, the plugin's own worker threads included, does not count. The
// "process CPU time" line adds the plugin's other threads: where it is well above the thread's own
// time, the plugin does work on a thread of its own, and that line is its cost. The wall-clock line
// includes everything on the core; a large gap to the others means the core is shared.
// MPC_OPT_SILENT=1 feeds silence in place of noise: the idle scenario of an effect.
// The harness sets flush-to-zero on arm32, as the 32-bit MPC host does; MPC_OPT_FZ=0 leaves it off.
// It hands the plugin the same buffers for input and output, as the MPC host does; MPC_OPT_INPLACE=0
// gives it separate ones.
// A plugin that is not one SDK source file: compile this file without SRC and link the plugin's own
// objects (see SKILL.md, "Ports"). -DMPC_OPT_ABI='"<its VST2 header>"' uses the plugin's header;
// without it the harness uses its own vst2_min.h.
#ifdef SRC
#include SRC
#else
#ifdef MPC_OPT_ABI
#include MPC_OPT_ABI
#else
#include "vst2_min.h"
#endif
extern "C" AEffect* VSTPluginMain(audioMasterCallback);
#endif
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <time.h>
#include <vector>

extern "C" __attribute__((noinline, used)) void icMark(int b) { asm volatile("" :: "r"(b) : "memory"); }
extern "C" __attribute__((noinline, used)) void icEnd(int b)  { asm volatile("" :: "r"(b) : "memory"); }
// The same layouts as the VST2 structures, declared here so any VST2 header will do.
struct HostTime { double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStart, cycleEnd;
                  int32_t tsNum, tsDen, smpte, smpteRate, toNextClock, flags; };
struct HostMidi { int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset; char midi[4]; char detune, offVelocity, r1, r2; };
struct HostEvents { int32_t numEvents; intptr_t reserved; HostMidi* ev[64]; };
static HostTime hostTime;
static intptr_t hostCb(AEffect*, int32_t op, int32_t, intptr_t, void*, float)
{
    if (op == 1) return 2400;                       // audioMasterVersion
    if (op == 7) return (intptr_t)&hostTime;        // audioMasterGetTime: 120 BPM, playing
    return 0;
}
// The 32-bit MPC host runs plugins with flush-to-zero on; a fresh process starts with it off. Set it
// as the host does, so a plugin that does not set it itself is counted, timed and rendered in the
// mode it runs in. MPC_OPT_FZ=0 leaves the mode as the process started. (The 64-bit host does not
// flush, so nothing is set there.)
static void hostFpuMode()
{
#if defined(__arm__)
    const char* fz = getenv("MPC_OPT_FZ");
    if (fz != nullptr && fz[0] == '0') return;
    unsigned v;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(v));
    v |= 1u << 24;
    __asm__ volatile("vmsr fpscr, %0" : : "r"(v));
#endif
}
static int64_t nowNs(clockid_t c) { timespec t; clock_gettime(c, &t); return int64_t(t.tv_sec) * 1000000000 + t.tv_nsec; }
enum { EvProgram = -1, EvNone = -2, EvNote = -3, EvOff = -4, EvChord = -5, EvChordOff = -6, EvRestrike = -7 };
struct Event { int block, index; float value; };
static std::string paramText(AEffect* e, int i, int32_t op)
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
static int findParam(AEffect* e, const char* key)           // an index or a name; EvNone when neither
{
    char* end = nullptr;
    const long idx = strtol(key, &end, 10);
    if (*end == 0) return idx >= 0 && idx < e->numParams ? (int)idx : EvNone;
    for (int i = 0; i < e->numParams; ++i)
        if (sameName(paramText(e, i, 8 /* effGetParamName */), key)) return i;
    return EvNone;
}
static void sendNotes(AEffect* e, bool on, int count, int single)   // single >= 0: that one pitch
{
    static HostMidi mev[64];
    static HostEvents evs;
    evs.numEvents = 0;
    for (int k = 0; k < count && k < 64; ++k)
    {
        HostMidi& m = mev[k];
        memset(&m, 0, sizeof m);
        m.type = 1; m.byteSize = sizeof m;
        m.midi[0] = (char)(on ? 0x90 : 0x80);
        m.midi[1] = (char)(single >= 0 ? single : 36 + (k * 7) % 36);
        m.midi[2] = (char)(on ? 100 : 0);
        evs.ev[evs.numEvents++] = &m;
    }
    if (evs.numEvents > 0) e->dispatcher(e, 25 /* effProcessEvents */, 0, 0, &evs, 0.0f);
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
    hostFpuMode();
    AEffect* e = VSTPluginMain((audioMasterCallback)hostCb);
    hostTime.sampleRate = 44100.0; hostTime.tempo = 120.0; hostTime.tsNum = 4; hostTime.tsDen = 4;
    hostTime.flags = (1 << 1) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 13);   // playing; ppq, tempo, bars, time signature valid
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher(e, effSetBlockSize, 0, N, nullptr, 0.0f);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
    if (argc > 1 && strcmp(argv[1], "list") == 0)
    {
        for (int i = 0; i < e->numParams; ++i)
            std::printf("%3d  %-24s %s%s\n", i, paramText(e, i, 8).c_str(), paramText(e, i, 7 /* effGetParamDisplay */).c_str(),
                        e->dispatcher(e, 0x7F01, i, 0, nullptr, 0.0f) == 1 ? "   (momentary)" : "");
        std::printf("%d parameters, %d programs%s\n", e->numParams, e->numPrograms, (e->flags & (1 << 8)) ? ", instrument" : "");
        return 0;
    }
    const int k = argc > 1 ? atoi(argv[1]) : 0;
    const int Blocks = argc > 2 ? std::max(atoi(argv[2]), Warm + 100) : 4000;
    const char* markEnv = getenv("MPC_OPT_MARK");
    const char* silentEnv = getenv("MPC_OPT_SILENT");
    const bool silent = silentEnv != nullptr && silentEnv[0] != '0' && silentEnv[0] != 0;
    const char* inplaceEnv = getenv("MPC_OPT_INPLACE");
    const bool inplace = inplaceEnv == nullptr || inplaceEnv[0] != '0';
    const bool markAll = markEnv != nullptr && strcmp(markEnv, "all") == 0;

    // The parameters the random sweep may turn.
    std::vector<int> sweep;
    {
        std::vector<bool> skip((size_t)std::max(e->numParams, 1), false);
        if (const char* s = getenv("MPC_OPT_SKIP"))
        {
            std::string list(s);
            for (size_t at = 0; at <= list.size();)
            {
                const size_t end = std::min(list.find(',', at), list.size());
                const std::string key = list.substr(at, end - at);
                const int p = key.empty() ? (int)EvNone : findParam(e, key.c_str());
                if (p >= 0) skip[(size_t)p] = true;
                else if (!key.empty()) std::fprintf(stderr, "MPC_OPT_SKIP: no such parameter: %s\n", key.c_str());
                at = end + 1;
            }
        }
        for (int i = 0; i < e->numParams; ++i)
            if (!skip[(size_t)i] && e->dispatcher(e, 0x7F01, i, 0, nullptr, 0.0f) != 1) sweep.push_back(i);
        if (k > 0 && (int)sweep.size() < e->numParams)
            std::fprintf(stderr, "random parameters: %d of %d (momentary or skipped ones left out)\n", (int)sweep.size(), e->numParams);
    }

    std::vector<Event> events;
    FILE* applied = nullptr;
    bool notes = false;
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
            if (sameName("program", key)) ev.index = EvProgram;
            else if (sameName("note", key)) ev.index = EvNote;
            else if (sameName("off", key)) ev.index = EvOff;
            else if (sameName("chord", key)) ev.index = EvChord;
            else if (sameName("chordoff", key)) ev.index = EvChordOff;
            else if (sameName("restrike", key)) ev.index = EvRestrike;
            else ev.index = findParam(e, key);
            if (ev.index == EvNone)
            {
                if (applied) std::fprintf(applied, "NOT APPLIED, no such parameter: %s", line);
                std::fprintf(stderr, "events: no such parameter: %s", line);
                continue;
            }
            if (ev.index <= EvNote) notes = true;
            events.push_back(ev);
        }
        fclose(f);
    }
    if ((e->flags & (1 << 8)) != 0 && !notes)
        std::fprintf(stderr, "this is an instrument and the events hold no notes: the run measures silence\n");

    static float L[N], R[N], oL[N], oR[N];
    float* in[2] = { L, R };
    float* out[2] = { inplace ? L : oL, inplace ? R : oR };
    std::vector<int64_t> cpu(Blocks), wall(Blocks), proc(Blocks);
    std::vector<int> pk((size_t)std::max(k, 1));
    std::vector<float> pv((size_t)std::max(k, 1));
    unsigned seed = 12345;
    int restrikeFrom = -1, restrikePeriod = 0, lastChord = 0;
    for (int b = 0; b < Blocks; ++b)
    {
        // Everything random is drawn before the marked region, in a fixed order.
        for (int i = 0; i < k; ++i)
        {
            pk[(size_t)i] = sweep.empty() ? -1 : sweep[(size_t)rand_r(&seed) % sweep.size()];
            pv[(size_t)i] = rand_r(&seed) / (float)RAND_MAX;
        }
        for (int i = 0; i < N; ++i) L[i] = R[i] = rand_r(&seed) / (float)RAND_MAX - 0.5f;   // drawn either way
        if (silent) for (int i = 0; i < N; ++i) L[i] = R[i] = 0.0f;
        hostTime.samplePos = (double)b * N;
        hostTime.ppqPos = hostTime.samplePos / 44100.0 * 2.0;
        int64_t w0 = nowNs(CLOCK_MONOTONIC), p0 = nowNs(CLOCK_PROCESS_CPUTIME_ID), c0 = nowNs(CLOCK_THREAD_CPUTIME_ID);
        if (markAll) icMark(b);
        for (int i = 0; i < k; ++i)
            if (pk[(size_t)i] >= 0) e->setParameter(e, pk[(size_t)i], pv[(size_t)i]);
        for (const Event& ev : events)
        {
            if (ev.block != b) continue;
            const int v = (int)ev.value;
            switch (ev.index)
            {
                case EvProgram:  e->dispatcher(e, effSetProgram, 0, (VstIntPtr)v, nullptr, 0.0f); break;
                case EvNote:     sendNotes(e, true, 1, v); break;
                case EvOff:      sendNotes(e, false, 1, v); break;
                case EvChord:    sendNotes(e, true, v, -1); lastChord = v; break;
                case EvChordOff: sendNotes(e, false, v, -1); break;
                case EvRestrike: restrikeFrom = b; restrikePeriod = v; break;
                default:         e->setParameter(e, ev.index, ev.value); break;
            }
        }
        if (restrikePeriod > 0 && b > restrikeFrom && (b - restrikeFrom) % restrikePeriod == 0)
        {
            sendNotes(e, false, lastChord, -1);
            sendNotes(e, true, lastChord, -1);
        }
        if (!markAll)
        {
            w0 = nowNs(CLOCK_MONOTONIC); p0 = nowNs(CLOCK_PROCESS_CPUTIME_ID); c0 = nowNs(CLOCK_THREAD_CPUTIME_ID);
            icMark(b);
        }
        e->processReplacing(e, in, out, N);
        icEnd(b);
        cpu[b] = nowNs(CLOCK_THREAD_CPUTIME_ID) - c0;
        proc[b] = nowNs(CLOCK_PROCESS_CPUTIME_ID) - p0;
        wall[b] = nowNs(CLOCK_MONOTONIC) - w0;
        if (applied)
            for (const Event& ev : events)
            {
                if (ev.block != b) continue;
                static const char* const names[] = { "note", "off", "chord", "chordoff", "restrike" };
                if (ev.index == EvProgram) std::fprintf(applied, "block %d: program %d\n", b, (int)ev.value);
                else if (ev.index <= EvNote) std::fprintf(applied, "block %d: %s %d\n", b, names[EvNote - ev.index], (int)ev.value);
                else std::fprintf(applied, "block %d: %d %s = %s (%.4f)\n", b, ev.index, paramText(e, ev.index, 8).c_str(),
                                  paramText(e, ev.index, 7).c_str(), ev.value);
            }
    }
    if (applied) fclose(applied);
    report("thread CPU time", cpu, Warm, Blocks, N);
    report("process CPU time", proc, Warm, Blocks, N);
    report("wall clock     ", wall, Warm, Blocks, N);
    return 0;
}
