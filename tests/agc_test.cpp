// AGC Model (1.6): the Modeled AGC against the figures it was built to (ModeledAgc in the
// source, tools/agc_fit.py), and the switch in the plugin. Ends with PASS or FAIL.
//
// Parts 1 to 4 run ModeledAgc on its own, in the loop the plugin runs it in (a control tick
// every 16 samples, the detector every sample), so levels far above full scale can be measured.
// Parts 5 to 9 go through the whole plugin, from the Init program with the deck switched on.
#include "../src/VHSVibes.cpp"

#include <cstdio>
#include <string>

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) { return opcode == audioMasterVersion ? 2400 : 0; }
static double db(double x) { return 20.0 * std::log10(x > 1e-30 ? x : 1e-30); }

const int kInit = 20;

// ModeledAgc in the plugin's loop. A 1 kHz sine, the same in both channels.
struct Loop
{
    ModeledAgc m; float sr; long n = 0;
    explicit Loop(float rate) : sr(rate) { m.prepare(sr); m.reset(); }
    // `sec` seconds of a sine of peak `amp` at `amount`; each(in, out, gain) per sample
    template <class F> void run(double sec, float amp, float amount, F&& each)
    {
        run(sec, amp, amp, amount, each);
    }
    // the same with the peak gliding from amp to amp2 (in equal dB steps)
    template <class F> void run(double sec, float amp, float amp2, float amount, F&& each)
    {
        const long end = (long)(sec * sr);
        for (long k = 0; k < end; ++k, ++n)
        {
            if ((n & (kCtrl - 1)) == 0) m.tick(amount);
            const float a = amp2 == amp ? amp : amp * (float)std::pow((double)amp2 / amp, (double)k / (double)end);
            const float x = a * (float)std::sin(6.283185307179586 * 1000.0 * (double)n / (double)sr);
            const float g = m.step(stereo(x, x));
            each(x, x * g, g);
        }
    }
    void run(double sec, float amp, float amount = 1.0f) { run(sec, amp, amount, [](float, float, float) {}); }
    // the steady gain on a tone, in dB: output peak against input peak over 0.2 s. The tone comes
    // in at the knee and rises to its level over 10 s (a loud tone switched on at once overshoots
    // the detector, and C80 takes a minute to give that back), then 20 s more.
    double steadyDb(float amp, double settle = 20.0)
    {
        const float knee = kRefLevel * 0.316228f;
        if (amp > knee) run(10.0, knee, amp, 1.0f, [](float, float, float) {});
        run(settle, amp);
        double pi = 0, po = 0;
        run(0.2, amp, 1.0f, [&](float in, float out, float) { pi = std::fmax(pi, std::fabs(in)); po = std::fmax(po, std::fabs(out)); });
        return db(po / pi);
    }
};

struct Fx
{
    AEffect* e; long pos = 0;
    explicit Fx(int program = kInit, float sr = 44100.0f)
    {
        e = VSTPluginMain(hostCb);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, sr);
        e->dispatcher(e, effSetBlockSize, 0, 512, nullptr, 0.0f);
        set(kHq, 0.0f);
        e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
        e->dispatcher(e, effSetProgram, 0, program, nullptr, 0.0f);
    }
    ~Fx() { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f); e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f); }
    mpc::Plugin* plug() { return static_cast<mpc::Plugin*>(e->object); }
    void set(int i, float plain) { plug()->set(i, plain); }
    // `seconds` of a sine (left amp, right ampR, or amp when ampR < 0) in 128-frame calls
    void play(float seconds, float amp, float hz, std::vector<float>* keepL, float ampR = -1.0f, std::vector<float>* keepR = nullptr)
    {
        static float L[128], R[128]; float* io[2] = { L, R };
        const long end = pos + (long)(seconds * 44100.0f);
        while (pos < end)
        {
            for (int i = 0; i < 128; ++i)
            {
                const float s = (float)std::sin(6.283185307179586 * (double)hz * (double)(pos + i) / 44100.0);
                L[i] = amp * s; R[i] = (ampR < 0.0f ? amp : ampR) * s;
            }
            pos += 128;
            e->processReplacing(e, io, io, 128);
            if (keepL) keepL->insert(keepL->end(), L, L + 128);
            if (keepR) keepR->insert(keepR->end(), R, R + 128);
        }
    }
    static Fx* deck(int model, float agc) { Fx* f = new Fx; f->set(kDeckOn, 1); f->set(kNoiseOn, 0); f->set(kAgcModel, (float)model); f->set(kAgc, agc); return f; }
};
static double peak(const std::vector<float>& v, size_t from = 0)
{
    double m = 0; for (size_t i = from; i < v.size(); ++i) m = std::fmax(m, std::fabs((double)v[i])); return m;
}

int main()
{
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("   %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };
    const float knee = kRefLevel * 0.316228f;   // 10 dB below the nominal input

    std::printf("1. Steady state on a 1 kHz tone, AGC 100 %% (gain reduction = %.2f dB lift - the gain)\n", (double)ModeledAgc::kBoostDb);
    {
        // the AGC report, 5.4: input re knee 0 / +10 / +20 / +40 dB -> 0 / 9.1 / 18.2 / 36.8 dB of gain reduction
        const double reNominal[4] = { -10, 0, 10, 30 }, table[4] = { 0.0, 9.1, 18.2, 36.8 };
        for (float sr : { 44100.0f, 48000.0f, 96000.0f })
            for (int k = 0; k < 4; ++k)
            {
                Loop l(sr);
                const double gr = ModeledAgc::kBoostDb - l.steadyDb(kRefLevel * (float)std::pow(10.0, reNominal[k] / 20.0));
                char what[140];
                std::snprintf(what, sizeof(what), "%6.0f Hz, input %+3.0f dB re nominal (%+3.0f re knee): gain reduction %6.2f dB (table %4.1f)",
                              (double)sr, reNominal[k], reNominal[k] + 10.0, gr, table[k]);
                check(std::fabs(gr - table[k]) < 0.3, what);
            }
        Loop l(44100.0f);
        const double g = l.steadyDb(kRefLevel);
        char what[120];
        std::snprintf(what, sizeof(what), "at the nominal level the gain is %+.3f dB (unity)", g);
        check(std::fabs(g) < 0.1, what);
        Loop q(44100.0f);
        const double lift = q.steadyDb(kRefLevel * 0.01f);
        std::snprintf(what, sizeof(what), "40 dB below nominal (30 below the knee) the gain is %+.2f dB (the lift, %.2f)", lift, (double)ModeledAgc::kBoostDb);
        check(std::fabs(lift - ModeledAgc::kBoostDb) < 0.01, what);
    }

    std::printf("2. Attack: a +20 dB step from the knee (settled there for 10 s)\n");
    double attackMs[3] = { 0, 0, 0 };
    for (int r = 0; r < 3; ++r)
    {
        const float sr = r == 0 ? 44100.0f : r == 1 ? 48000.0f : 96000.0f;
        Loop l(sr);
        l.run(10.0, knee);
        float before = 0.0f;
        l.run(0.001, knee, 1.0f, [&](float, float, float g) { before = g; });
        long at = -1, k = 0;
        double gr50 = 0, gr500 = 0;
        l.run(2.0, knee * 10.0f, 1.0f, [&](float, float, float g) {
            const double grNow = db(before) - db(g);
            if (at < 0 && grNow >= 10.0) at = k;
            if (k == (long)(0.05 * sr)) gr50 = grNow;
            if (k == (long)(0.5 * sr)) gr500 = grNow;
            ++k;
        });
        attackMs[r] = 1000.0 * (double)at / (double)sr;
        char what[140];
        std::snprintf(what, sizeof(what), "%6.0f Hz: 10 dB of gain reduction after %.1f ms (10 to 50); %.1f dB after 50 ms, %.1f dB after 0.5 s",
                      (double)sr, attackMs[r], gr50, gr500);
        check(at > 0 && attackMs[r] >= 10.0 && attackMs[r] <= 50.0, what);
    }

    std::printf("3. Recovery after the sound stops (a +20 dB tone over the knee for 2 s, then silence)\n");
    for (float sr : { 44100.0f, 48000.0f, 96000.0f })
    {
        Loop l(sr);
        l.run(10.0, knee * 10.0f);
        double gr2 = 0, gr8 = 0, gr20 = 0; long k = 0;
        l.run(20.5, 0.0f, 1.0f, [&](float, float, float g) {
            const double gr = ModeledAgc::kBoostDb - db(g);
            if (k == (long)(2.0 * sr)) gr2 = gr;
            if (k == (long)(8.0 * sr)) gr8 = gr;
            if (k == (long)(20.0 * sr)) gr20 = gr;
            ++k;
        });
        const double rate = (gr2 - gr8) / 6.0;
        char what[140];
        std::snprintf(what, sizeof(what), "%6.0f Hz: %.2f dB/s from 2 to 8 s after (0.8 to 1.3); gain reduction %.1f dB at 2 s, %.1f dB at 20 s",
                      (double)sr, rate, gr2, gr20);
        check(rate >= 0.8 && rate <= 1.3, what);
    }

    std::printf("4. Long silence, and a NaN, do not leave it stuck\n");
    {
        Loop l(44100.0f);
        l.run(5.0, 1.0f);
        l.run(600.0, 0.0f);
        const bool clean = std::fpclassify(l.m.vK) != FP_SUBNORMAL && std::fpclassify(l.m.v70) != FP_SUBNORMAL && std::fpclassify(l.m.vc) != FP_SUBNORMAL;
        char what[160];
        std::snprintf(what, sizeof(what), "after 10 minutes of silence: C80 %.3g V, C70 %.3g V, control %.3g V, none denormal; gain %+.2f dB",
                      (double)l.m.vK, (double)l.m.v70, (double)l.m.vc, db(l.m.a));
        check(clean && std::fabs(db(l.m.a) - ModeledAgc::kBoostDb) < 0.01, what);
        l.m.step(stereo(std::nanf(""), 0.0f));
        l.run(0.01, 0.0f);
        std::snprintf(what, sizeof(what), "one NaN sample in: back at full gain within 10 ms (gain %+.2f dB)", db(l.m.a));
        check(std::isfinite(l.m.a) && std::fabs(db(l.m.a) - ModeledAgc::kBoostDb) < 0.01, what);
    }

    std::printf("5. In the plugin: the AGC knob, the nominal level, the lift\n");
    {
        const double reNominal[3] = { -30, 0, 6 };
        for (int k = 0; k < 3; ++k)
            for (float agc : { 50.0f, 100.0f })
            {
                const float amp = kRefLevel * (float)std::pow(10.0, reNominal[k] / 20.0);
                Fx* on = Fx::deck(1, agc); Fx* off = Fx::deck(1, 0.0f);
                std::vector<float> a, b;
                on->play(20.0f, amp, 1000.0f, nullptr); on->play(0.5f, amp, 1000.0f, &a);
                off->play(20.0f, amp, 1000.0f, nullptr); off->play(0.5f, amp, 1000.0f, &b);
                const double g = db(peak(a) / peak(b));
                // what the model gives at that level, scaled by the knob
                Loop l(44100.0f);
                const double want = 0.01 * agc * l.steadyDb(amp);
                char what[140];
                std::snprintf(what, sizeof(what), "AGC %3.0f %%, tone %+3.0f dB re nominal: %+6.2f dB (the model at 100 %% x the knob: %+6.2f)", (double)agc, reNominal[k], g, want);
                check(std::fabs(g - want) < 0.1 && (k != 1 || std::fabs(g) < 0.1), what);
                delete on; delete off;
            }
    }

    std::printf("6. At 0 %% the Modeled AGC is off: the same output as the Simple one at 0 %%, bit for bit\n");
    {
        Fx* a = Fx::deck(0, 0.0f); Fx* b = Fx::deck(1, 0.0f);
        std::vector<float> oa, ob;
        a->play(1.0f, 0.3f, 440.0f, &oa, 0.1f); a->set(kAgc, 60); a->play(1.0f, 0.3f, 440.0f, nullptr, 0.1f); a->set(kAgc, 0); a->play(1.0f, 0.3f, 440.0f, &oa, 0.1f);
        b->play(1.0f, 0.3f, 440.0f, &ob, 0.1f); b->set(kAgc, 60); b->play(1.0f, 0.3f, 440.0f, nullptr, 0.1f); b->set(kAgc, 0); b->play(1.0f, 0.3f, 440.0f, &ob, 0.1f);
        // the second second of each 1 s stretch: after the knob has come back down (and both
        // AGCs have let go) the two are the same again
        bool same = true;
        for (size_t i = 0; i < 44100; ++i) same = same && oa[i] == ob[i];
        double tail = 0; for (size_t i = oa.size() - 22050; i < oa.size(); ++i) tail = std::fmax(tail, std::fabs((double)oa[i] - ob[i]));
        char what[140];
        std::snprintf(what, sizeof(what), "the first second identical; after AGC 60 %% -> 0 %%, the last 0.5 s within %.1e of each other", tail);
        check(same && tail < 1e-6, what);
        delete a; delete b;
    }

    std::printf("7. AGC Model switched while a tone plays: no click\n");
    {
        // 300 Hz, the right channel at half the left, at three levels; Simple -> Modeled and back,
        // as plain values and as a Q-Link sends them (every normalised value on the way). The new
        // AGC settles unheard (the Modeled one for a second), then the sound crossfades to it: the
        // level moves, smoothly. A click is a step that is large for the level around it, so the
        // measure is each sample-to-sample step against the largest a 300 Hz sine of the local
        // peak (over one cycle each side) can make: 1.0 for a clean tone.
        const auto clickRatio = [](const std::vector<float>& v) {
            const double sineStep = 2.0 * std::sin(3.141592653589793 * 300.0 / 44100.0);
            double worst = 0;
            for (size_t i = 1; i < v.size(); ++i)
            {
                double pk = 0;
                for (size_t j = i < 147 ? 0 : i - 147; j < v.size() && j <= i + 147; ++j) pk = std::fmax(pk, std::fabs((double)v[j]));
                if (pk > 0) worst = std::fmax(worst, std::fabs((double)v[i] - v[i - 1]) / (sineStep * pk));
            }
            return worst;
        };
        const auto levelDb = [](const std::vector<float>& v) { return db(peak(v, v.size() - 4410)); };
        for (float lvl : { 0.02f, 0.4f, 0.9f })
            for (int from = 0; from < 2; ++from)
                for (int qlink = 0; qlink < 2; ++qlink)
                {
                    Fx* fx = Fx::deck(from, 100.0f);
                    std::vector<float> before, during, after;
                    fx->play(3.0f, lvl, 300.0f, &before, 0.5f * lvl);
                    if (qlink)
                        for (int k = 0; k < 64; ++k)
                        {
                            const float u = (float)k / 63.0f;
                            fx->e->setParameter(fx->e, kAgcModel, from ? 1.0f - u : u);
                            fx->play(128.0f / 44100.0f, lvl, 300.0f, &during, 0.5f * lvl);
                        }
                    else fx->set(kAgcModel, (float)(1 - from));
                    fx->play(1.5f, lvl, 300.0f, &during, 0.5f * lvl);
                    fx->play(0.5f, lvl, 300.0f, &after, 0.5f * lvl);
                    double lo = 1e9, hi = -1e9;
                    for (size_t b = 0; b + 441 <= during.size(); b += 441)
                    {
                        const double l = db(peak(std::vector<float>(during.begin() + (long)b, during.begin() + (long)b + 441)));
                        lo = std::fmin(lo, l); hi = std::fmax(hi, l);
                    }
                    const double r = clickRatio(during);
                    char what[200];
                    std::snprintf(what, sizeof(what), "tone %.2f, %s -> %s%s: step ratio %.3f; level %+.1f dB before, %+.1f .. %+.1f during, %+.1f after",
                                  (double)lvl, from ? "Modeled" : "Simple ", from ? "Simple " : "Modeled", qlink ? " (Q-Link)" : "         ", r,
                                  levelDb(before) - db(lvl), lo - db(lvl), hi - db(lvl), levelDb(after) - db(lvl));
                    check(r < 1.1 && fx->plug()->getInt(kAgcModel) == 1 - from, what);
                    delete fx;
                }
        // flipped back while the new one settles, and half way through the crossfade
        for (float back : { 0.5f, 1.05f })
        {
            Fx* fx = Fx::deck(0, 100.0f);
            fx->play(3.0f, 0.4f, 300.0f, nullptr, 0.2f);
            std::vector<float> out;
            fx->set(kAgcModel, 1); fx->play(back, 0.4f, 300.0f, &out, 0.2f);
            fx->set(kAgcModel, 0); fx->play(1.0f, 0.4f, 300.0f, &out, 0.2f);
            char what[120];
            std::snprintf(what, sizeof(what), "Simple -> Modeled -> Simple after %.2f s: step ratio %.3f", (double)back, clickRatio(out));
            check(clickRatio(out) < 1.1, what);
            delete fx;
        }
    }
    std::printf("8. The new parameter, the programs, old states\n");
    {
        Fx fx;
        AEffect* raw = VSTPluginMain(hostCb);
        const float fresh = raw->getParameter(raw, kAgcModel);
        raw->dispatcher(raw, effClose, 0, 0, nullptr, 0.0f);
        const mpc::Param& p = fx.plug()->param(kAgcModel);
        check(fx.e->numParams == 66 && kAgcModel == 57 && p.id == "agc_model" && p.name == "AGC Model" && p.choices.size() == 2
              && p.choices[0] == "Simple" && p.choices[1] == "Modeled" && fresh == 1.0f && groupOf(kAgcModel) == groupOf(kAgc),
              "66 parameters; number 57 (the last of 1.6) is agc_model, \"AGC Model\", Simple / Modeled, resting on Modeled, in the deck group");
        int withAgc = 0, simple = 0, others = 0;
        for (int i = 0; i < fx.e->numPrograms; ++i)
        {
            fx.e->dispatcher(fx.e, effSetProgram, 0, i, nullptr, 0.0f);
            if (fx.plug()->get(kAgc) > 0.0f) { ++withAgc; if (fx.plug()->getInt(kAgcModel) == 0) ++simple; }
            else if (fx.plug()->getInt(kAgcModel) == 1) ++others;
        }
        char what[140];
        std::snprintf(what, sizeof(what), "%d programs; the %d with the AGC up are on Simple (%d), the other %d on Modeled (%d)",
                      fx.e->numPrograms, withAgc, simple, fx.e->numPrograms - withAgc, others);
        check(fx.e->numPrograms == 46 && withAgc == 26 && simple == withAgc && others == fx.e->numPrograms - withAgc, what);

        // a state saved before AGC Model existed: this one's values without "agc_model"
        Fx saver; saver.set(kDeckOn, 1); saver.set(kAgc, 40); saver.set(kAgcModel, 1);
        void* data = nullptr;
        const VstIntPtr size = saver.e->dispatcher(saver.e, effGetChunk, 0, 0, &data, 0.0f);
        const uint8_t* d = static_cast<const uint8_t*>(data);
        const std::vector<uint8_t> modeled(d, d + size);
        std::vector<uint8_t> old(d, d + 12); size_t pos = 12; uint32_t kept = 0;
        const uint32_t count = (uint32_t)d[8] | (uint32_t)d[9] << 8 | (uint32_t)d[10] << 16 | (uint32_t)d[11] << 24;
        for (uint32_t i = 0; i < count; ++i)
        {
            const size_t len = 1 + (size_t)d[pos] + 8;
            if (std::string(reinterpret_cast<const char*>(d) + pos + 1, d[pos]) != "agc_model") { old.insert(old.end(), d + pos, d + pos + len); ++kept; }
            pos += len;
        }
        old.insert(old.end(), d + pos, d + size);
        for (int i = 0; i < 4; ++i) old[8 + (size_t)i] = (uint8_t)(kept >> (8 * i));
        Fx loader; loader.set(kAgcModel, 1);
        loader.play(0.1f, 0.0f, 100.0f, nullptr);
        const VstIntPtr r = loader.e->dispatcher(loader.e, effSetChunk, 0, (VstIntPtr)old.size(), old.data(), 0.0f);
        std::snprintf(what, sizeof(what), "a state without it (%u of %u values) loaded into an instance on Modeled: AGC Model %s, AGC %.0f %%",
                      kept, count, loader.plug()->getInt(kAgcModel) ? "Modeled" : "Simple", (double)loader.plug()->get(kAgc));
        check(r == 1 && kept == count - 1 && loader.plug()->getInt(kAgcModel) == 0 && loader.plug()->get(kAgc) == 40.0f, what);
        Fx back; back.set(kAgcModel, 0);
        back.e->dispatcher(back.e, effSetChunk, 0, (VstIntPtr)modeled.size(), const_cast<uint8_t*>(modeled.data()), 0.0f);
        check(back.plug()->getInt(kAgcModel) == 1, "a state saved on Modeled loads on Modeled, also into an instance on Simple");
    }

    std::printf("9. Silence through the plugin, and a loud hit after a quiet passage\n");
    {
        // against the same with the AGC at 0: the filters after it ring out the same way, only
        // lifted by the AGC's 9 dB as the silence goes on
        long sub[2] = { 0, 0 }; double last[2] = { 0, 0 }; bool finite = true;
        for (int on = 0; on < 2; ++on)
        {
            Fx* fx = Fx::deck(1, on ? 100.0f : 0.0f);
            fx->play(2.0f, 0.5f, 1000.0f, nullptr);
            std::vector<float> out; fx->play(120.0f, 0.0f, 1000.0f, &out);
            for (float v : out) { finite = finite && std::isfinite(v); sub[on] += std::fpclassify(v) == FP_SUBNORMAL ? 1 : 0; }
            last[on] = peak(out, out.size() - 44100);
            delete fx;
        }
        char line[200];
        std::snprintf(line, sizeof(line), "two minutes of silence after a tone: all finite; denormal samples out %ld (AGC 0 %%: %ld); last second's peak %.1e (%.1e)",
                      sub[1], sub[0], last[1], last[0]);
        check(finite && sub[1] <= sub[0] && last[1] <= 3.0 * last[0] + 1e-30, line);
        // the lift (9 dB) is in when a loud sound arrives: the clip after the compressor keeps it in bounds
        Fx* h = Fx::deck(1, 100.0f);
        h->play(10.0f, 0.01f, 440.0f, nullptr);
        std::vector<float> hit; h->play(0.5f, 0.9f, 440.0f, &hit);
        char what[160];
        std::snprintf(what, sizeof(what), "a hit of peak 0.90 after 10 s at 0.01: loudest output sample %.2f (the clip is at 1.5; the filters after it overshoot a little)", peak(hit));
        check(peak(hit) < 2.0, what);
        delete h;
    }

    std::printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
