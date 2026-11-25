// render.cpp: includes the plugin's source and writes its output for a fixed, seeded run that visits
// every program, seven block sizes, random parameters, every parameter at once, many settled random
// settings, and bad input values (NaN, infinities, huge and denormal). Two builds whose output is the
// same to the bit write byte-identical files. bitcheck.sh builds and compares them.
//   render <out.bin>
// Phase boundaries are printed as byte offsets, so a difference can be traced to its phase.
// An instrument is played: chords of one to six notes go on and off through every phase.
// Parameters the plugin reports as momentary triggers (opcode 0x7F01) and those named in MPC_OPT_SKIP
// (indexes or names as "drive list" prints them, commas between) are left alone; a trigger that the
// plugin debounces against the clock would make two runs of the same build differ.
// Beside the audio it writes <out.bin>.txt: what the plugin shows and saves at fixed points of the
// run (every parameter's value, value text and unit, program names, the size and a hash of the saved
// state). bitcheck.sh compares that file too, so a change to a read-out or to the saved state shows.
// The harness sets flush-to-zero on arm32, as the 32-bit MPC host does; MPC_OPT_FZ=0 leaves it off.
// With MPC_OPT_HOLD=<seconds> in the environment the run ends with long held programs (see below).
// MPC_OPT_HOLD_PROGRAMS=<n,n,...> chooses which programs, and MPC_OPT_HOLD_SET=<name=value,...>
// (values 0..1) sets parameters on top of each, so the held run reaches the code under test.
// It hands the plugin the same buffers for input and output, as the MPC host does; MPC_OPT_INPLACE=0
// gives it separate ones.
// A plugin that is not one SDK source file: compile without SRC and link the plugin's own objects
// (see SKILL.md, "Ports"). -DMPC_OPT_ABI='"<its VST2 header>"' uses the plugin's header; without it
// the harness uses its own vst2_min.h.
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
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// The same layouts as the VST2 structures, declared here so any VST2 header will do.
struct HostTime { double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStart, cycleEnd;
                  int32_t tsNum, tsDen, smpte, smpteRate, toNextClock, flags; };
struct HostMidi { int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset; char midi[4]; char detune, offVelocity, r1, r2; };
struct HostEvents { int32_t numEvents; intptr_t reserved; HostMidi* ev[8]; };
static HostTime hostTime;
static intptr_t hostCb(AEffect*, int32_t op, int32_t, intptr_t, void*, float)
{
    if (op == 1) return 2400;                       // audioMasterVersion
    if (op == 7) return (intptr_t)&hostTime;        // audioMasterGetTime: 120 BPM, playing
    return 0;
}
static uint32_t rs = 0x1234567u;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static float rf() { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
// A second generator for the notes, so an effect's run draws the same numbers as it always did.
static uint32_t ns = 0x2468aceu;
static uint32_t nrnd() { ns ^= ns << 13; ns ^= ns >> 17; ns ^= ns << 5; return ns; }

enum { MaxB = 512 };
static float L[MaxB], R[MaxB], oL[MaxB], oR[MaxB];
static AEffect* fx;
static FILE* out;
static FILE* dump;
static long frames, blocks;
static bool bad, synth, inplace = true;
static bool skipParam[4096];
static int noteBase, noteCount;

static void notes(bool on)
{
    static HostMidi mev[8];
    static HostEvents evs;
    evs.numEvents = 0;
    for (int k = 0; k < noteCount; ++k)
    {
        HostMidi& m = mev[k];
        std::memset(&m, 0, sizeof m);
        m.type = 1; m.byteSize = sizeof m;
        m.midi[0] = (char)(on ? 0x90 : 0x80);
        m.midi[1] = (char)(noteBase + k * 7);
        m.midi[2] = (char)(on ? 100 : 0);
        evs.ev[evs.numEvents++] = &m;
    }
    if (evs.numEvents > 0) fx->dispatcher(fx, 25 /* effProcessEvents */, 0, 0, &evs, 0.0f);
}
static uint32_t fnv(const void* p, size_t n, uint32_t h = 2166136261u)
{
    const unsigned char* c = (const unsigned char*)p;
    for (size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 16777619u; }
    return h;
}
static void text(int op, int i, char* buf) { std::memset(buf, 0, 256); fx->dispatcher(fx, op, i, 0, buf, 0.0f); }
static bool sameName(const char* a, const char* b)   // ignores case, and treats _ as a space
{
    for (; *a != 0 && *b != 0; ++a, ++b)
    {
        const char x = *a == ' ' ? '_' : (char)std::tolower((unsigned char)*a);
        const char y = *b == ' ' ? '_' : (char)std::tolower((unsigned char)*b);
        if (x != y) return false;
    }
    return *a == *b;
}
static int paramIndex(const char* key)       // an index, or a name as "drive list" prints it; -1 when neither
{
    char* end = nullptr;
    const long i = std::strtol(key, &end, 10);
    if (*end == 0 && end != key) return i >= 0 && i < fx->numParams ? (int)i : -1;
    char name[256];
    for (int j = 0; j < fx->numParams; ++j) { text(8 /* effGetParamName */, j, name); if (sameName(name, key)) return j; }
    return -1;
}
static std::vector<std::string> listOf(const char* env)   // the comma-separated items of an environment variable
{
    std::vector<std::string> v;
    const char* s = std::getenv(env);
    if (s == nullptr) return v;
    std::string list(s);
    for (size_t at = 0; at <= list.size();)
    {
        const size_t stop = list.find(',', at) == std::string::npos ? list.size() : list.find(',', at);
        if (stop > at) v.push_back(list.substr(at, stop - at));
        at = stop + 1;
    }
    return v;
}
// What the plugin shows and saves, written beside the audio: every parameter's value, value text and
// unit, the program's name, and the size and a hash of the state the plugin hands the host.
static void dumpState(const char* what, int n)
{
    if (dump == nullptr) return;
    char buf[256];
    text(5 /* effGetProgramName */, 0, buf);
    std::fprintf(dump, "== %s %d, program name \"%s\"\n", what, n, buf);
    for (int i = 0; i < fx->numParams; ++i)
    {
        char label[256];
        text(7 /* effGetParamDisplay */, i, buf);
        text(6 /* effGetParamLabel */, i, label);
        std::fprintf(dump, "%d %.9g \"%s\" \"%s\"\n", i, (double)fx->getParameter(fx, i), buf, label);
    }
    if ((fx->flags & (1 << 5)) != 0)                 // the plugin saves its state as a chunk
        for (int kind = 0; kind < 2; ++kind)
        {
            void* p = nullptr;
            const long len = (long)fx->dispatcher(fx, 23 /* effGetChunk */, kind, 0, &p, 0.0f);
            std::fprintf(dump, "%s state: %ld bytes, hash %08x\n", kind == 0 ? "bank" : "program", len,
                         p != nullptr && len > 0 ? fnv(p, (size_t)len) : 0u);
        }
}
static void setParam(int i, float v) { if (!skipParam[i & 4095]) fx->setParameter(fx, i, v); }

static void block(int n)
{
    float* in[2] = { L, R };
    float* o[2] = { inplace ? L : oL, inplace ? R : oR };
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
    if (synth)                                    // a chord for 16 blocks, then 8 blocks of release
    {
        if (blocks % 24 == 0) { noteBase = 36 + (int)(nrnd() % 24); noteCount = 1 + (int)(nrnd() % 6); notes(true); }
        if (blocks % 24 == 16) notes(false);
    }
    hostTime.samplePos = (double)frames;
    hostTime.ppqPos = hostTime.samplePos / 44100.0 * 2.0;
    fx->processReplacing(fx, in, o, n);
    fwrite(o[0], sizeof(float), (size_t)n, out);
    fwrite(o[1], sizeof(float), (size_t)n, out);
    frames += n;
    ++blocks;
}
static void phase(const char* name) { std::printf("phase %-28s starts at byte %ld\n", name, frames * 8); }

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
#if defined(__arm__)
    {
        const char* fz = std::getenv("MPC_OPT_FZ");   // flush-to-zero on, as the 32-bit MPC host has it
        if (fz == nullptr || fz[0] != '0')
        {
            unsigned v;
            __asm__ volatile("vmrs %0, fpscr" : "=r"(v));
            v |= 1u << 24;
            __asm__ volatile("vmsr fpscr, %0" : : "r"(v));
        }
    }
#endif
    fx = VSTPluginMain((audioMasterCallback)hostCb);
    out = fopen(argv[1], "wb");
    if (fx == nullptr || out == nullptr) return 1;
    dump = fopen((std::string(argv[1]) + ".txt").c_str(), "w");
    hostTime.sampleRate = 44100.0; hostTime.tempo = 120.0; hostTime.tsNum = 4; hostTime.tsDen = 4;
    hostTime.flags = (1 << 1) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 13);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    fx->dispatcher(fx, effSetBlockSize, 0, MaxB, nullptr, 0.0f);
    fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0.0f);
    static const int sizes[7] = { 32, 128, 512, 64, 96, 192, 256 };
    const uint32_t np = (uint32_t)(fx->numParams > 0 ? fx->numParams : 1);
    const int npr = fx->numPrograms > 0 ? fx->numPrograms : 1;
    synth = (fx->flags & (1 << 8)) != 0;
    int skipped = 0;
    for (int i = 0; i < fx->numParams && i < 4096; ++i)
        if (fx->dispatcher(fx, 0x7F01, i, 0, nullptr, 0.0f) == 1) { skipParam[i] = true; ++skipped; }
    for (const std::string& key : listOf("MPC_OPT_SKIP"))
    {
        const int i = paramIndex(key.c_str());
        if (i < 0 || i >= 4096) { std::fprintf(stderr, "MPC_OPT_SKIP: no such parameter: %s\n", key.c_str()); continue; }
        if (!skipParam[i]) { skipParam[i] = true; ++skipped; }
    }
    if (const char* ip = std::getenv("MPC_OPT_INPLACE")) inplace = ip[0] != '0';

    phase("programs");                       // every program, two parameters moved partway through
    for (int p = 0; p < npr; ++p)
    {
        fx->dispatcher(fx, effSetProgram, 0, p, nullptr, 0.0f);
        for (int b = 0; b < 40; ++b)
        {
            if (b == 20) { const int i = (int)(rnd() % np); setParam(i, rf()); const int j = (int)(rnd() % np); setParam(j, rf()); }
            block(sizes[p % 7]);
        }
        dumpState("program", p);
    }
    phase("4 random parameters");            // before every block, block size varying
    for (int b = 0; b < 3000; ++b)
    {
        for (int i = 0; i < 4; ++i) { const int j = (int)(rnd() % np); setParam(j, rf()); }
        block(sizes[rnd() % 7]);
    }
    phase("every parameter random");         // on every block
    for (int b = 0; b < 600; ++b)
    {
        for (uint32_t i = 0; i < np; ++i) setParam((int)i, rf());
        block(sizes[rnd() % 3]);
    }
    phase("settled random settings");        // the steady state of many combinations
    for (int c = 0; c < 120; ++c)
    {
        for (uint32_t i = 0; i < np; ++i) setParam((int)i, rf());
        for (int b = 0; b < 30; ++b) block(128);
        if (c % 4 == 0) dumpState("settled setting", c);
    }
    phase("bad input values");               // with program changes and moving parameters
    bad = true;
    for (int b = 0; b < 3000; ++b)
    {
        if (b % 100 == 0) fx->dispatcher(fx, effSetProgram, 0, (b / 100) % npr, nullptr, 0.0f);
        if (b % 3 == 0) { const int j = (int)(rnd() % np); setParam(j, rf()); }
        block(sizes[rnd() % 7]);
    }
    // A saved state loaded back: for a plugin that saves a chunk, random settings are saved, another
    // program is selected, the saved state is loaded, and the sound that follows is written. A change
    // to how state is saved or loaded shows here and in the text file.
    bad = false;
    if ((fx->flags & (1 << 5)) != 0)
    {
        phase("state saved and loaded");
        for (int c = 0; c < 8; ++c)
        {
            fx->dispatcher(fx, effSetProgram, 0, c % npr, nullptr, 0.0f);
            for (uint32_t i = 0; i < np; ++i) setParam((int)i, rf());
            for (int b = 0; b < 10; ++b) block(128);
            void* p = nullptr;
            const long len = (long)fx->dispatcher(fx, 23 /* effGetChunk */, 0, 0, &p, 0.0f);
            std::vector<char> saved;
            if (p != nullptr && len > 0) saved.assign((const char*)p, (const char*)p + len);
            fx->dispatcher(fx, effSetProgram, 0, (c + 1) % npr, nullptr, 0.0f);
            for (int b = 0; b < 4; ++b) block(128);
            if (!saved.empty()) fx->dispatcher(fx, 24 /* effSetChunk */, 0, (VstIntPtr)saved.size(), saved.data(), 0.0f);
            dumpState("loaded state", c);
            for (int b = 0; b < 30; ++b) block(128);
        }
    }
    // Optional: MPC_OPT_HOLD=<seconds> holds up to six programs spread over the list for that long
    // each, with clean input. A difference that grows over such a run comes from a feedback path.
    bad = false;
    const char* hold = std::getenv("MPC_OPT_HOLD");
    const int holdBlocks = hold != nullptr ? (int)(std::atof(hold) * 44100.0 / 128.0) : 0;
    std::vector<int> held;
    for (const std::string& s : listOf("MPC_OPT_HOLD_PROGRAMS")) held.push_back(std::atoi(s.c_str()) % npr);
    if (held.empty())
        for (int k = 0; k < (npr < 6 ? npr : 6); ++k) held.push_back(npr <= 6 ? k : (int)((long)k * (npr - 1) / 5));
    const std::vector<std::string> holdSet = listOf("MPC_OPT_HOLD_SET");
    for (size_t k = 0; holdBlocks > 0 && k < held.size(); ++k)
    {
        char name[32];
        std::snprintf(name, sizeof name, "held program %d", held[k]);
        phase(name);
        fx->dispatcher(fx, effSetProgram, 0, held[k], nullptr, 0.0f);
        for (const std::string& s : holdSet)
        {
            const size_t eq = s.find('=');
            const int i = eq == std::string::npos ? -1 : paramIndex(s.substr(0, eq).c_str());
            if (i < 0) { if (k == 0) std::fprintf(stderr, "MPC_OPT_HOLD_SET: no such parameter: %s\n", s.c_str()); continue; }
            fx->setParameter(fx, i, (float)std::atof(s.c_str() + eq + 1));
        }
        for (int b = 0; b < holdBlocks; ++b) block(128);
    }
    fclose(out);
    if (dump != nullptr) fclose(dump);
    std::printf("%d parameters (%d left alone), %d programs, %s, %ld frames, %ld bytes\n", fx->numParams, skipped,
                fx->numPrograms, synth ? "instrument" : "effect", frames, frames * 8);
    return 0;
}
