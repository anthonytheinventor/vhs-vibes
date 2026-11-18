// Stress test for the threads a host really uses: one audio thread calling process with
// changing block sizes, two "UI" threads turning knobs, choosing programs, saving and loading
// state and reading value text, and the plugin's own helper thread calling back into the host.
// It ends by creating and destroying the plugin many times in a row.
//
// On its own it only checks that nothing crashes and the output stays finite. Its real use is
// under the checkers: build it with -fsanitize=thread, or -fsanitize=address,undefined.
#include "../src/VHSVibes.cpp"

#include <atomic>
#include <cstdio>
#include <thread>

static std::atomic<long> g_automate{0}, g_redraw{0}, g_io{0};

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterVersion) return 2400;
    if (opcode == audioMasterAutomate) g_automate.fetch_add(1, std::memory_order_relaxed);
    if (opcode == audioMasterUpdateDisplay) g_redraw.fetch_add(1, std::memory_order_relaxed);
    if (opcode == audioMasterIOChanged) g_io.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

static uint32_t rnd(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

int main(int argc, char** argv)
{
    const double seconds = argc > 1 ? std::atof(argv[1]) : 6.0;
    AEffect* e = VSTPluginMain(hostCb);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher(e, effSetBlockSize, 0, 512, nullptr, 0.0f);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);

    std::atomic<bool> stop{false};
    std::atomic<long> blocks{0}, bad{0};
    const auto t0 = std::chrono::steady_clock::now();

    std::thread audio([&] {
        static float L[512], R[512];
        uint32_t s = 0x1234567u;
        long pos = 0;
        while (!stop.load())
        {
            static const int sizes[] = {32, 64, 96, 128, 192, 256, 512, 1, 7, 33};
            const int n = sizes[rnd(s) % 10];
            for (int i = 0; i < n; ++i)
            {
                L[i] = 0.5f * std::sin(6.2831853f * 220.0f * (float)(pos + i) / 44100.0f) + 0.2f * ((float)(rnd(s) >> 8) / 8388608.0f - 1.0f);
                R[i] = 0.5f * std::sin(6.2831853f * 331.0f * (float)(pos + i) / 44100.0f);
            }
            pos += n;
            float* io[2] = { L, R };
            e->processReplacing(e, io, io, n);      // in place, like the MPC
            for (int i = 0; i < n; ++i)
                if (!std::isfinite(L[i]) || !std::isfinite(R[i]) || std::fabs(L[i]) > 50.0f || std::fabs(R[i]) > 50.0f) bad.fetch_add(1);
            blocks.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::microseconds(150));
        }
    });

    std::thread knobs([&] {                          // a hand on the knobs: any parameter, any value
        uint32_t s = 0xbeef01u;
        while (!stop.load())
        {
            const int p = (int)(rnd(s) % (uint32_t)e->numParams);
            const float v = (rnd(s) % 8 == 0) ? (float)(rnd(s) % 2) : (float)(rnd(s) >> 8) / 16777216.0f;
            e->setParameter(e, p, v);
            if (rnd(s) % 16 == 0) (void)e->getParameter(e, (int)(rnd(s) % (uint32_t)e->numParams));
            std::this_thread::sleep_for(std::chrono::microseconds(400));
        }
    });

    std::thread ui([&] {                             // the host's UI: programs, state, value text, latency
        uint32_t s = 0xc0de02u;
        std::vector<unsigned char> saved;
        char text[64];
        int k = 0;
        while (!stop.load())
        {
            ++k;
            if (k % 5 == 0) e->dispatcher(e, effSetProgram, 0, (VstIntPtr)(rnd(s) % (uint32_t)e->numPrograms), nullptr, 0.0f);
            if (k % 7 == 0)
            {
                void* chunk = nullptr;
                const VstIntPtr n = e->dispatcher(e, effGetChunk, 0, 0, &chunk, 0.0f);
                if (n > 0 && chunk != nullptr) saved.assign((unsigned char*)chunk, (unsigned char*)chunk + n);
            }
            if (k % 11 == 0 && !saved.empty()) e->dispatcher(e, effSetChunk, 0, (VstIntPtr)saved.size(), saved.data(), 0.0f);
            const int p = (int)(rnd(s) % (uint32_t)e->numParams);
            e->dispatcher(e, effGetParamDisplay, p, 0, text, 0.0f);
            e->dispatcher(e, effGetParamName, p, 0, text, 0.0f);
            (void)e->dispatcher(e, effGetProgram, 0, 0, nullptr, 0.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    });

    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < seconds)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop.store(true);
    audio.join(); knobs.join(); ui.join();
    e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f);
    e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);
    std::printf("stress: %ld blocks, %ld bad samples, %ld automate calls, %ld redraw requests, %ld latency updates\n",
                blocks.load(), bad.load(), g_automate.load(), g_redraw.load(), g_io.load());

    // start-up and shutdown ordering: make and destroy the plugin over and over, at every stage
    for (int i = 0; i < 40; ++i)
    {
        AEffect* x = VSTPluginMain(hostCb);
        if (i % 4 >= 1)
        {
            x->dispatcher(x, effSetSampleRate, 0, 0, nullptr, 44100.0f);
            x->dispatcher(x, effSetBlockSize, 0, 128, nullptr, 0.0f);
            x->dispatcher(x, effMainsChanged, 0, 1, nullptr, 0.0f);
        }
        if (i % 4 >= 2)
        {
            float L[128] = {0}, R[128] = {0}; float* io[2] = { L, R };
            for (int b = 0; b < 3; ++b) x->processReplacing(x, io, io, 128);
        }
        if (i % 4 == 3) std::this_thread::sleep_for(std::chrono::milliseconds(60));   // let the helper thread speak
        x->dispatcher(x, effClose, 0, 0, nullptr, 0.0f);
    }
    std::printf("create / destroy: 40 rounds done\n");
    std::printf(bad.load() == 0 ? "PASS\n" : "FAIL\n");
    return bad.load() == 0 ? 0 : 1;
}
