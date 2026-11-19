// How VHS Vibes behaves towards the host: when the helper thread speaks, what it says after a
// program change, a change of Sat Type, the meters and the Animation switch, and the latency
// report. The helper thread reports the meters one by one (audioMasterAutomate) and asks for no
// redraw, except while the host is moving a knob: then it reports nothing and asks for one redraw
// per look (audioMasterUpdateDisplay). After a load or a change of Sat Type it asks for one redraw
// at once; it never reports a setting. The test host writes every report straight back from
// inside it, as the MPC does.
#include "../src/VHSVibes.cpp"
#include <cstdio>
#include <mutex>

static std::mutex g_m;
static int g_automate = 0, g_redraw = 0, g_io = 0, g_other = 0;   // g_other: any other call but the time
static int g_setting = 0;                                         // reports of anything but a meter (never)
static int g_echoes = 0;                                          // reports written back

static VstIntPtr hostCb(AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr, void*, float opt)
{
    if (opcode == audioMasterVersion) return 2400;
    {
        std::lock_guard<std::mutex> l(g_m);
        if (opcode == audioMasterIOChanged) { ++g_io; return 1; }
        if (opcode == audioMasterAutomate) { ++g_automate; if (index < 0 || index >= kNumParams || isSetting(index)) ++g_setting; }
        else if (opcode == audioMasterUpdateDisplay) ++g_redraw;
        else if (opcode != audioMasterGetTime) ++g_other;
    }
    // the MPC writes every report straight back, from inside it (setParameter, same index and value)
    if (opcode == audioMasterAutomate && e != nullptr)
    {
        e->setParameter(e, index, opt);
        std::lock_guard<std::mutex> l(g_m); ++g_echoes;
    }
    return 0;
}
static void clearCounts() { std::lock_guard<std::mutex> l(g_m); g_automate = g_redraw = g_io = g_other = g_setting = g_echoes = 0; }
static int count(const int& c) { std::lock_guard<std::mutex> l(g_m); return c; }
static void ms(int n) { std::this_thread::sleep_for(std::chrono::milliseconds(n)); }
static double msSince(std::chrono::steady_clock::time_point t) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); }

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
// blocks of the tone (paced) until `untilMs` after t
static void playUntil(AEffect* e, std::chrono::steady_clock::time_point t, double untilMs, float amp)
{
    while (msSince(t) < untilMs) play(e, 1, amp);
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
        const auto t = std::chrono::steady_clock::now();
        e->dispatcher(e, effSetProgram, 0, prog, nullptr, 0.0f);
        playUntil(e, t, 150.0, 0.0f);
        const int early = count(g_redraw);
        playUntil(e, t, 650.0, 0.0f);
        std::lock_guard<std::mutex> l(g_m);
        std::printf("   program %2d: %d redraw request(s) in the first 150 ms, %d by 650 ms; %d report(s) of a setting\n",
                    prog, early, g_redraw, g_setting);
        if (early != 1 || g_redraw != 1 || g_setting != 0 || g_other != 0) ++fails;
    }
    // a data wheel turned through five programs, one step every 100 ms: one redraw per program, each
    // within its step, and none later
    {
        clearCounts();
        int perStep[5], worst = 0, total = 0;
        auto t = std::chrono::steady_clock::now();
        for (int k = 0; k < 5; ++k)
        {
            t = std::chrono::steady_clock::now();
            const int before = count(g_redraw);
            e->dispatcher(e, effSetProgram, 0, 3 + k, nullptr, 0.0f);
            playUntil(e, t, 100.0, 0.0f);
            perStep[k] = count(g_redraw) - before;
            worst = std::max(worst, perStep[k]); total += perStep[k];
        }
        playUntil(e, t, 150.0, 0.0f);
        const int by150 = count(g_redraw);
        playUntil(e, t, 800.0, 0.0f);
        std::printf("   five programs 100 ms apart: %d %d %d %d %d redraw request(s) in each step; %d by 150 ms after the last step, %d by 800 ms\n",
                    perStep[0], perStep[1], perStep[2], perStep[3], perStep[4], by150, count(g_redraw));
        check(worst <= 1 && by150 <= 5, "at most one redraw request per program");
        check(count(g_redraw) == by150, "none later than 150 ms after the last step");
        check(count(g_setting) == 0, "and no setting reported");
    }

    std::printf("3. Quiet when nothing changes; Sat Type; a knob sweep\n");
    clearCounts(); play(e, 60, 0.0f, false); ms(200);
    check(count(g_automate) + count(g_redraw) == 0, "nothing sent while idle with Animation off");
    clearCounts(); play(e, 350, 0.4f);
    check(count(g_automate) + count(g_redraw) == 0, "nothing sent while a tone plays with Animation off");
    clearCounts(); e->setParameter(e, kWarp, 0.9f); play(e, 10, 0.0f, false); ms(200);
    check(count(g_automate) == 0, "a knob turned by the host is not echoed back");
    plug->set(kWarp, 35.0f);
    play(e, 8, 0.0f); ms(400);
    for (float type : {1.0f, 2.0f, 0.0f})   // VHS, Hi-Fi, Simple: the three read-outs (Lo Sat, Hi Sat, Split) change each time
    {
        clearCounts();
        plug->set(kSatType, type);
        play(e, 60, 0.0f); ms(150);
        std::printf("   Sat Type %s: %d redraw request(s), %d report(s)\n", type == 1.0f ? "VHS" : type == 2.0f ? "Hi-Fi" : "Simple",
                    count(g_redraw), count(g_automate));
        check(count(g_redraw) == 1 && count(g_automate) == 0, "a change of Sat Type asks for one redraw, and reports nothing");
    }
    // a knob swept with Animation on: no meter report, one redraw per look at most, and the picture keeps moving
    plug->set(kAnimate, 1.0f);
    play(e, 60, 0.4f); ms(150);
    int reelMoves = 0; float lastReel = e->getParameter(e, kVisReel);
    const auto tSweep = std::chrono::steady_clock::now();
    // Counted from just after the first write. One report may still come then: the helper
    // thread looks at the knob state for each meter just before it reports that meter, so a
    // meter whose look came just before the first write landed can still go out (one meter, as
    // every later meter sees the write).
    e->setParameter(e, kFlutter, 0.2f); clearCounts();
    for (int k = 1; k < 350; ++k)
    {
        e->setParameter(e, kFlutter, 0.2f + 0.6f * (float)(k & 63) / 63.0f); play(e, 1, 0.4f);
        const float r = e->getParameter(e, kVisReel); if (r != lastReel) { ++reelMoves; lastReel = r; }
    }
    const double sweepSecs = msSince(tSweep) / 1000.0;
    std::printf("   a knob swept for %.2f s with Animation on: %d redraw request(s), the reel moved %d times, %d meter report(s)\n",
                sweepSecs, count(g_redraw), reelMoves, count(g_automate));
    check(count(g_automate) <= 1, "no meter report while a knob is swept (one already on its way at the first write excepted)");
    check(reelMoves >= 5 && count(g_redraw) >= 5, "the picture keeps moving during the sweep");
    check(count(g_redraw) <= (int)(20.0 * sweepSecs) + 1, "20 redraw requests a second at most");
    // the reports come back about 300 ms after the last write (6 looks, and up to one look's wait)
    clearCounts();
    const auto tLast = std::chrono::steady_clock::now();
    while (count(g_automate) == 0 && msSince(tLast) < 1500.0) play(e, 1, 0.4f);
    const double resumedMs = msSince(tLast);
    const int redrawsUntil = count(g_redraw);
    clearCounts(); play(e, 350, 0.4f);
    std::printf("   meter reports back %.0f ms after the last write (%d redraw request(s) until then); then in about 1 s: %d report(s), %d redraw request(s)\n",
                resumedMs, redrawsUntil, count(g_automate), count(g_redraw));
    check(resumedMs >= 250.0 && resumedMs <= 450.0, "meter reports resume about 300 ms after the knob stops");
    check(count(g_redraw) == 0 && count(g_automate) > 0, "and from then on reports alone, no redraw requests");
    plug->set(kFlutter, 30.0f);

    std::printf("4. Meters and the Animation switch\n");
    clearCounts();
    const auto t0 = std::chrono::steady_clock::now();
    play(e, 700, 0.4f);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    { std::lock_guard<std::mutex> l(g_m); std::printf("   tone for %.2f s with Animation on: %d redraw request(s), %d meter report(s) (%d written back), %d of a setting\n",
                                                      secs, g_redraw, g_automate, g_echoes, g_setting); }
    check(count(g_automate) >= 10 && count(g_redraw) == 0, "no knob moving: the meters are reported, no redraw requests");
    check(count(g_setting) == 0, "only meters are reported");
    check(count(g_echoes) == count(g_automate), "the host writing each report back does not count as a knob moving");
    plug->set(kAnimate, 0.0f);
    play(e, 20, 0.4f); ms(120);
    clearCounts(); play(e, 350, 0.4f);
    { std::lock_guard<std::mutex> l(g_m); std::printf("   tone for about 1 s with Animation off: %d redraw request(s), %d report(s)\n", g_redraw, g_automate); }
    check(count(g_redraw) + count(g_automate) == 0, "nothing sent with Animation off");
    e->dispatcher(e, effSetProgram, 0, 3, nullptr, 0.0f);
    play(e, 8, 0.0f); ms(150);
    check(e->getParameter(e, kAnimate) < 0.5f, "choosing a program leaves the Animation switch alone");
    plug->set(kAnimate, 1.0f);

    std::printf("4a. A program change with sound and Animation on\n");
    play(e, 200, 0.4f);
    clearCounts();
    const auto tProg = std::chrono::steady_clock::now();
    e->dispatcher(e, effSetProgram, 0, 5, nullptr, 0.0f);
    playUntil(e, tProg, 150.0, 0.4f);
    const int early = count(g_redraw);
    playUntil(e, tProg, 650.0, 0.4f);
    { std::lock_guard<std::mutex> l(g_m); std::printf("   %d redraw request(s) in the first 150 ms, %d by 650 ms; %d meter report(s), %d of a setting\n",
                                                      early, g_redraw, g_automate, g_setting); }
    check(early == 1 && count(g_redraw) == 1, "exactly one redraw request, within 150 ms");
    check(count(g_automate) >= 5 && count(g_setting) == 0, "the meter reports go on, and no setting is reported");

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
    std::printf("   after the resume: %d redraw request(s), %d meter report(s), %d of a setting\n", count(g_redraw), count(g_automate), count(g_setting));
    check(count(g_redraw) == 1 && count(g_setting) == 0, "one redraw request after the resume, and no setting reported");

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
