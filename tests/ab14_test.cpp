// The 1.4 comparison: the 25 programs that existed in 1.4.0 must come out exactly as 1.4.0 made
// them, with every switch added since at rest (HQ off, B-Type NR off, Sat Type Simple, Mic Model
// Simple). This file is built twice, against two sources (run.sh takes the 1.4.0 one from git):
//
//   ab14_test write -              renders and writes the raw output to stdout (the 1.4.0 build)
//   ab14_test compare -            renders the same, reads the other build's output from stdin
//                                  and compares them sample by sample; ends with PASS or FAIL
//
// Each program: a fresh instance, 20 s of a test signal (two tones and a little noise per
// channel) in 128-frame calls, the chorus and the mic switched on for every third program.
// The first 100 ms are not compared: since 1.5.2, after a reset with the chorus on, the output
// waits a few ms longer for the chorus swing before it fades in (the fade-in keeps its timing,
// so from about 50 ms on the output is the same again).
#include SRC

#include <cstdio>
#include <cstring>
#include <string>

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    return opcode == audioMasterVersion ? 2400 : 0;
}
// a parameter by its id, -1 if this version has none of that name
static int byId(AEffect* e, const char* id)
{
    mpc::Plugin* p = static_cast<mpc::Plugin*>(e->object);
    for (int i = 0; i < p->numParams(); ++i) if (p->param(i).id == id) return i;
    return -1;
}

int main(int argc, char** argv)
{
    const bool compare = argc > 2 && std::strcmp(argv[1], "compare") == 0;
    if (argc < 3 || (!compare && std::strcmp(argv[1], "write") != 0)) { std::fprintf(stderr, "usage: ab14_test write|compare -\n"); return 2; }
    const int total = 44100 * 20, skip = 4410, programs = 25;
    std::vector<float> srcL((size_t)total), srcR((size_t)total);
    uint32_t seed = 12345;
    for (int i = 0; i < total; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const float n = (float)(seed >> 8) * (2.0f / 16777216.0f) - 1.0f;
        const double t = i / 44100.0;
        srcL[(size_t)i] = (float)(0.3 * std::sin(6.283185307179586 * 220.0 * t) + 0.2 * std::sin(6.283185307179586 * 3300.0 * t)) + 0.05f * n;
        srcR[(size_t)i] = (float)(0.3 * std::sin(6.283185307179586 * 330.0 * t + 1.0) + 0.1 * std::sin(6.283185307179586 * 7000.0 * t)) - 0.05f * n;
    }
    int same = 0;
    std::vector<float> ref((size_t)total);
    for (int prog = 0; prog < programs; ++prog)
    {
        AEffect* e = VSTPluginMain(hostCb);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
        e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
        e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
        e->dispatcher(e, effSetProgram, 0, prog, nullptr, 0.0f);
        e->setParameter(e, kHq, 0.0f);   // on in a new instance since 1.5 (1.4 has the switch, off)
        if (prog % 3 == 1) { e->setParameter(e, kChorusOn, 1.0f); e->setParameter(e, kMicOn, 1.0f); }
        const int model = byId(e, "mic_model");
        if (model >= 0) e->setParameter(e, model, 0.0f);   // the microphones of 1.4 (1.6 added the Modeled ones)
        std::vector<float> L = srcL, R = srcR;
        for (int pos = 0; pos < total; pos += 128)
        {
            const int n = total - pos < 128 ? total - pos : 128;
            float* io[2] = { L.data() + pos, R.data() + pos };
            e->processReplacing(e, io, io, n);
        }
        char name[64] = {0};
        e->dispatcher(e, effGetProgramNameIndexed, prog, 0, name, 0.0f);
        e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f);
        e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);
        if (!compare)
        {
            std::fwrite(L.data(), sizeof(float), L.size(), stdout);
            std::fwrite(R.data(), sizeof(float), R.size(), stdout);
            continue;
        }
        long differ = 0; float worst = 0.0f;
        for (const std::vector<float>* ch : { &L, &R })
        {
            if (std::fread(ref.data(), sizeof(float), ref.size(), stdin) != ref.size()) { std::printf("the 1.4 output ended early\nFAIL\n"); return 1; }
            for (int i = skip; i < total; ++i)
                if (!((*ch)[(size_t)i] == ref[(size_t)i])) { ++differ; worst = std::fmax(worst, std::fabs((*ch)[(size_t)i] - ref[(size_t)i])); }
        }
        if (differ == 0) ++same;
        std::printf("   %2d %-16s %s\n", prog, name, differ == 0 ? "the same" : (std::to_string(differ) + " samples differ, by up to " + std::to_string(worst)).c_str());
    }
    if (!compare) return 0;
    std::printf("   identical to 1.4 on %d of %d programs\n", same, programs);
    std::printf(same == programs ? "PASS\n" : "FAIL\n");
    return same == programs ? 0 : 1;
}
