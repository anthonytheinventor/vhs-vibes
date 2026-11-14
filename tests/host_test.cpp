// How VHS Vibes 1.3 behaves towards the host: when the helper thread speaks, what it says
// after a program change, the meters and the Animation switch, and the latency report.
#include "../src/VHSVibes.cpp"
#include <cstdio>
#include <mutex>

static std::mutex g_m;
static int g_settings = 0, g_meter = 0, g_io = 0;
static float g_last[kNumParams];
static bool g_seen[kNumParams];

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32 index, VstIntPtr, void*, float opt)
{
    if (opcode == audioMasterVersion) return 2400;
    std::lock_guard<std::mutex> l(g_m);
    if (opcode == audioMasterIOChanged) { ++g_io; return 1; }
    if (opcode == audioMasterAutomate && index >= 0 && index < kNumParams)
    {
        if (index == kVisReel || index == kVisLevel) ++g_meter; else ++g_settings;
        g_last[index] = opt; g_seen[index] = true;
    }
    return 0;
}
static void clearCounts() { std::lock_guard<std::mutex> l(g_m); g_settings = g_meter = g_io = 0; for (bool& b : g_seen) b = false; }
static void ms(int n) { std::this_thread::sleep_for(std::chrono::milliseconds(n)); }

// n blocks of a 220 Hz tone (amp 0 = silence), paced in real time so the helper thread runs alongside
static void play(AEffect* e, int n, float amp, bool paced = true)
{
    static long pos = 0;
    float L[128], R[128]; float* io[2] = { L, R };
    for (int b = 0; b < n; ++b)
    {
        for (int i = 0; i < 128; ++i) L[i] = R[i] = amp * std::sin(6.2831853f * 220.0f * (float)(pos + i) / 44100.0f);
        pos += 128;
        e->processReplacing(e, io, io, 128);
        if (paced) std::this_thread::sleep_for(std::chrono::microseconds(2900));
    }
}

int main()
{
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("   %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };
    mpc::Plugin* plug = nullptr;

    std::printf("1. Before the host starts the plugin\n");
    AEffect* e = VSTPluginMain(hostCb);
    plug = static_cast<mpc::Plugin*>(e->object);
    ms(300);
    { std::lock_guard<std::mutex> l(g_m); std::printf("   host calls in the first 300 ms after creation: %d\n", g_settings + g_meter + g_io); }
    check(g_settings + g_meter + g_io == 0, "the helper thread stays silent until the plugin is started");

    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
    play(e, 8, 0.0f);
    ms(150);

    std::printf("2. Program changes\n");
    for (int prog : {2, 7, 13, 20, 0})
    {
        clearCounts();
        e->dispatcher(e, effSetProgram, 0, prog, nullptr, 0.0f);
        play(e, 4, 0.0f);
        ms(150);
        std::lock_guard<std::mutex> l(g_m);
        int seen = 0, wrong = 0;
        for (int i = 0; i < kNumParams; ++i) { if (g_seen[i]) ++seen; if (g_seen[i] && isSetting(i) && g_last[i] != e->getParameter(e, i)) ++wrong; }
        std::printf("   program %2d: %d of %d parameters told, %d wrong\n", prog, seen, (int)kNumParams, wrong);
        if (seen != kNumParams || wrong) ++fails;
    }

    std::printf("3. Quiet when nothing changes\n");
    clearCounts(); play(e, 60, 0.0f, false); ms(200);
    check(g_settings == 0, "no setting notifications while idle");
    clearCounts(); e->setParameter(e, kWarp, 0.9f); play(e, 10, 0.0f, false); ms(200);
    check(g_settings == 0, "a knob turned by the host is not echoed back");
    plug->set(kWarp, 35.0f);

    std::printf("4. Meters and the Animation switch\n");
    clearCounts(); play(e, 700, 0.4f);
    { std::lock_guard<std::mutex> l(g_m); std::printf("   tone for about 2 s with Animation on: %d meter notifications\n", g_meter); }
    check(g_meter >= 20 && g_meter <= 90, "between 20 and 90 meter notifications in 2 s (20 a second at most, two meters)");
    plug->set(kAnimate, 0.0f);
    play(e, 20, 0.4f);
    clearCounts(); play(e, 350, 0.4f);
    { std::lock_guard<std::mutex> l(g_m); std::printf("   tone for about 1 s with Animation off: %d meter notifications\n", g_meter); }
    check(g_meter == 0, "no meter notifications with Animation off");
    e->dispatcher(e, effSetProgram, 0, 3, nullptr, 0.0f);
    play(e, 8, 0.0f); ms(150);
    check(e->getParameter(e, kAnimate) < 0.5f, "choosing a program leaves the Animation switch alone");
    plug->set(kAnimate, 1.0f);

    std::printf("5. Latency report\n");
    e->dispatcher(e, effSetProgram, 0, 0, nullptr, 0.0f);     // Home Recording
    play(e, 400, 0.2f);                                       // > 1 s
    std::printf("   Home Recording: host told %d samples (%.1f ms)\n", (int)e->initialDelay, e->initialDelay / 44.1);
    check(e->initialDelay > 60 && e->initialDelay < 400, "a plausible latency is reported for Home Recording");
    const int homeLat = (int)e->initialDelay;
    clearCounts();
    for (int b = 0; b < 350; ++b) { e->setParameter(e, kWarp, 0.2f + 0.6f * (float)b / 350.0f); play(e, 1, 0.2f); }   // a 1 s knob sweep
    { std::lock_guard<std::mutex> l(g_m); std::printf("   latency updates sent during a one-second Warp sweep: %d\n", g_io); }
    check(g_io == 0, "no latency updates while a knob is moving");
    play(e, 1200, 0.2f);                                      // the delay glides to its new length (about 2 s), then half a second of quiet
    { std::lock_guard<std::mutex> l(g_m); std::printf("   after the sweep settles: %d update(s), now %d samples (%.1f ms)\n", g_io, (int)e->initialDelay, e->initialDelay / 44.1); }
    check(g_io == 1 && (int)e->initialDelay > homeLat, "one update after the knob stops, and the latency grew with Warp");
    e->dispatcher(e, effSetProgram, 0, 20, nullptr, 0.0f);    // Init: no wobble
    play(e, 400, 0.2f);
    std::printf("   Init: host told %d samples\n", (int)e->initialDelay);
    check((int)e->initialDelay == 12, "Init reports the 12-sample minimum");

    e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f);
    e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);
    std::printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
