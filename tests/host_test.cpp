// How VHS Vibes behaves towards the host: when the helper thread speaks, what it says after a
// program change, a change of Sat Type, the meters and the Animation switch, and the latency
// report. Since 1.7 the helper thread never reports a parameter as changed (audioMasterAutomate);
// it sets the meters itself and asks for one redraw per look (audioMasterUpdateDisplay).
#include "../src/VHSVibes.cpp"
#include <cstdio>
#include <mutex>

static std::mutex g_m;
static int g_automate = 0, g_redraw = 0, g_io = 0, g_other = 0;   // g_other: any other call but the time

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterVersion) return 2400;
    std::lock_guard<std::mutex> l(g_m);
    if (opcode == audioMasterIOChanged) { ++g_io; return 1; }
    if (opcode == audioMasterAutomate) ++g_automate;
    else if (opcode == audioMasterUpdateDisplay) ++g_redraw;
    else if (opcode != audioMasterGetTime) ++g_other;
    return 0;
}
static void clearCounts() { std::lock_guard<std::mutex> l(g_m); g_automate = g_redraw = g_io = g_other = 0; }
static int count(const int& c) { std::lock_guard<std::mutex> l(g_m); return c; }
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
    { std::lock_guard<std::mutex> l(g_m); std::printf("   host calls in the first 300 ms after creation: %d\n", g_automate + g_redraw + g_io + g_other); }
    check(g_automate + g_redraw + g_io + g_other == 0, "the helper thread stays silent until the plugin is started");

    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
    static_cast<mpc::Plugin*>(e->object)->set(kHq, 0);   // HQ is on in a new instance since 1.5; the tests start without it
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
    play(e, 8, 0.0f);
    ms(150);

    // Animation off for 2 and 3: then nothing on the screen moves by itself, so every redraw
    // request comes from what the test did
    plug->set(kAnimate, 0.0f);
    play(e, 8, 0.0f); ms(150);

    std::printf("2. Program changes (Animation off)\n");
    for (int prog : {2, 7, 13, 20, 0})
    {
        clearCounts();
        e->dispatcher(e, effSetProgram, 0, prog, nullptr, 0.0f);
        play(e, 4, 0.0f);
        ms(150);
        std::lock_guard<std::mutex> l(g_m);
        std::printf("   program %2d: %d redraw request(s), %d parameter-changed reports\n", prog, g_redraw, g_automate);
        if (g_redraw < 1 || g_redraw > 4 || g_automate != 0 || g_other != 0) ++fails;
    }

    std::printf("3. Quiet when nothing changes; Sat Type; a knob sweep\n");
    clearCounts(); play(e, 60, 0.0f, false); ms(200);
    check(count(g_automate) + count(g_redraw) == 0, "nothing sent while idle with Animation off");
    clearCounts(); play(e, 350, 0.4f);
    check(count(g_automate) + count(g_redraw) == 0, "nothing sent while a tone plays with Animation off");
    clearCounts(); e->setParameter(e, kWarp, 0.9f); play(e, 10, 0.0f, false); ms(200);
    check(count(g_automate) == 0, "a knob turned by the host is not echoed back");
    plug->set(kWarp, 35.0f);
    play(e, 8, 0.0f); ms(150);
    for (float type : {1.0f, 2.0f, 0.0f})   // VHS, Hi-Fi, Simple: the three read-outs (Lo Sat, Hi Sat, Split) change each time
    {
        clearCounts();
        plug->set(kSatType, type);
        play(e, 60, 0.0f); ms(150);
        std::printf("   Sat Type %s: %d redraw request(s), %d parameter-changed reports\n", type == 1.0f ? "VHS" : type == 2.0f ? "Hi-Fi" : "Simple",
                    count(g_redraw), count(g_automate));
        check(count(g_redraw) >= 1 && count(g_automate) == 0, "a change of Sat Type asks for a redraw, and reports nothing");
    }
    // a knob swept with Animation on: no parameter-changed report, and the picture keeps moving
    plug->set(kAnimate, 1.0f);
    play(e, 60, 0.4f); ms(150);
    clearCounts();
    int reelMoves = 0; float lastReel = e->getParameter(e, kVisReel);
    for (int k = 0; k < 350; ++k)
    {
        e->setParameter(e, kFlutter, 0.2f + 0.6f * (float)(k & 63) / 63.0f); play(e, 1, 0.4f);
        const float r = e->getParameter(e, kVisReel); if (r != lastReel) { ++reelMoves; lastReel = r; }
    }
    std::printf("   a knob swept for about 1 s with Animation on: %d redraw request(s), the reel moved %d times, %d parameter-changed reports\n",
                count(g_redraw), reelMoves, count(g_automate));
    check(count(g_automate) == 0, "no parameter-changed report while a knob is swept");
    check(reelMoves >= 5 && count(g_redraw) >= 5, "the picture keeps moving during the sweep");
    plug->set(kFlutter, 30.0f);

    std::printf("4. Meters and the Animation switch\n");
    clearCounts();
    const auto t0 = std::chrono::steady_clock::now();
    play(e, 700, 0.4f);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    { std::lock_guard<std::mutex> l(g_m); std::printf("   tone for %.2f s with Animation on: %d redraw request(s), %d parameter-changed reports\n", secs, g_redraw, g_automate); }
    check(count(g_redraw) >= 10 && count(g_redraw) <= (int)(20.0 * secs) + 2, "at least 10 redraw requests, and 20 a second at most");
    check(count(g_automate) == 0, "the meters are never reported as changed parameters");
    plug->set(kAnimate, 0.0f);
    play(e, 20, 0.4f); ms(120);
    clearCounts(); play(e, 350, 0.4f);
    { std::lock_guard<std::mutex> l(g_m); std::printf("   tone for about 1 s with Animation off: %d redraw request(s)\n", g_redraw); }
    check(count(g_redraw) + count(g_automate) == 0, "no redraw requests with Animation off");
    e->dispatcher(e, effSetProgram, 0, 3, nullptr, 0.0f);
    play(e, 8, 0.0f); ms(150);
    check(e->getParameter(e, kAnimate) < 0.5f, "choosing a program leaves the Animation switch alone");
    plug->set(kAnimate, 1.0f);

    std::printf("4b. Suspended\n");
    play(e, 60, 0.4f); ms(100);
    e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f);
    ms(120);   // a call already on its way may land
    clearCounts();
    e->dispatcher(e, effSetProgram, 0, 2, nullptr, 0.0f);
    play(e, 200, 0.4f); ms(150);
    check(count(g_automate) + count(g_redraw) + count(g_io) == 0, "nothing sent while suspended, with audio, Animation on and a program change");
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
    play(e, 60, 0.4f); ms(100);
    check(count(g_redraw) >= 1 && count(g_automate) == 0, "redraw requests again after a resume");

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
