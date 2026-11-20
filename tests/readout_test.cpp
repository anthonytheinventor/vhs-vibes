// VHS Vibes 1.8: five read-outs went from percent to what the knob sets. Drive, and Lo Sat and Hi Sat
// with Sat Type on Simple, read the gain into their clipper in dB; Comp its threshold in dBFS; Noise
// what every noise level gets against its 50 %, in dB. Whistle stays in percent: a level at the output
// would move with Noise, Output and Mix, and the MPC does not always draw the screen again for that.
// Only the read-outs changed: each parameter (id, name, range, default, mapping), what getParameter
// gives, the saved state and every program are as in 1.7.0, so old projects and automation load as
// before, and every other read-out (Whistle, and Lo Sat and Hi Sat in VHS and Hi-Fi, too) reads as in
// 1.7.0. The same program built against the 1.7.0 source (-DWRITE170, run.sh takes it from git) writes
// what 1.7.0 gives; built against this source it compares, then checks the new read-outs. Ends with
// PASS or FAIL.
//   readout_test <values file> <texts file>   (WRITE170: writes both; otherwise reads them)
#ifdef SRC
#include SRC
#else
#include "../src/VHSVibes.cpp"
#endif
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) { return opcode == audioMasterVersion ? 2400 : 0; }
#ifndef WRITE170
// 1.8 saves one thing more after the parameters (the B-Type NR clip level, see nrclip_test.cpp): eight
// bytes of extra state, which 1.7.0 passes over. They are checked and taken off before the comparison.
static int g_extraWrong = 0;
static std::vector<uint8_t> withoutExtra(std::vector<uint8_t> s)
{
    static const uint8_t tail[12] = { 8, 0, 0, 0, 'V', 'V', 'X', 1, 1, 0, 0, 0 };   // extraLen 8, a new instance's flag
    if (s.size() < 12 || std::memcmp(s.data() + s.size() - 12, tail, 12) != 0) { ++g_extraWrong; return s; }
    s.resize(s.size() - 8);
    std::memset(s.data() + s.size() - 4, 0, 4);
    return s;
}
#endif

struct Fx
{
    AEffect* e;
    mpc::Plugin* p;
    Fx()
    {
        e = VSTPluginMain(hostCb);
        p = static_cast<mpc::Plugin*>(e->object);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
        e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
    }
    ~Fx() { e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f); }
    std::vector<uint8_t> chunk()
    {
        void* data = nullptr;
        const VstIntPtr size = e->dispatcher(e, effGetChunk, 0, 0, &data, 0.0f);
        const uint8_t* d = static_cast<const uint8_t*>(data);
#ifndef WRITE170
        return withoutExtra(std::vector<uint8_t>(d, d + size));
#else
        return std::vector<uint8_t>(d, d + size);
#endif
    }
    std::string text(int i) { char b[64] = {0}; e->dispatcher(e, effGetParamDisplay, i, 0, b, 0.0f); return b; }
};

// The five whose read-out changed, and Whistle, which keeps 1.7.0's percent.
static const int kWatched[] = { kDrive, kLoSat, kHiSat, kComp, kNoise, kWhistle };

static void put(std::vector<uint8_t>& out, const void* v, size_t n) { const uint8_t* b = static_cast<const uint8_t*>(v); out.insert(out.end(), b, b + n); }
static void putF(std::vector<uint8_t>& out, float f) { put(out, &f, 4); }
static void putS(std::vector<uint8_t>& out, const std::string& s) { const uint32_t n = (uint32_t)s.size(); put(out, &n, 4); put(out, s.data(), n); }

// Everything about the six except their text, as bytes: each one's description, then each one set
// five ways (plain 0 / 25 / 50 / 75 / 100), and by a normalised write as automation makes it, with its
// plain value, getParameter and the whole saved state; then every program, with the six's values and
// the saved state. Kept in `chunks` as well, in that order.
static std::vector<uint8_t> record(std::vector<std::vector<uint8_t>>& chunks)
{
    std::vector<uint8_t> out;
    Fx f;
    const uint32_t head[] = { (uint32_t)f.p->numParams(), (uint32_t)f.p->numPrograms() };
    put(out, head, sizeof(head));
    auto state = [&] { chunks.push_back(f.chunk()); putS(out, std::string(chunks.back().begin(), chunks.back().end())); };
    for (int i : kWatched)
    {
        const mpc::Param& d = f.p->param(i);
        const uint32_t meta[] = { (uint32_t)i, (uint32_t)d.kind, (uint32_t)d.logarithmic, (uint32_t)d.automatable };
        put(out, meta, sizeof(meta));
        putS(out, d.id); putS(out, d.name);
        putF(out, d.min); putF(out, d.max); putF(out, d.def); putF(out, d.skew);
        for (float n : { 0.0f, 0.1f, 0.25f, 0.5f, 0.77f, 1.0f }) putF(out, d.toPlain(n));
        putF(out, f.e->getParameter(f.e, i));   // a new instance: the default
    }
    for (int i : kWatched)
    {
        const float was = f.p->get(i);
        for (float v : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f }) { f.p->set(i, v); putF(out, f.p->get(i)); putF(out, f.e->getParameter(f.e, i)); state(); }
        f.e->setParameter(f.e, i, 0.37f); putF(out, f.p->get(i)); putF(out, f.e->getParameter(f.e, i)); state();
        f.p->set(i, was);
    }
    for (int prog = 0; prog < f.p->numPrograms(); ++prog)
    {
        f.e->dispatcher(f.e, effSetProgram, 0, prog, nullptr, 0.0f);
        for (int i : kWatched) { putF(out, f.p->get(i)); putF(out, f.e->getParameter(f.e, i)); }
        state();
    }
    return out;
}

// The read-outs that must not change, as text: in every program every setting's read-out but the
// five (Lo Sat and Hi Sat are kept where Sat Type is VHS or Hi-Fi); Lo Sat and Hi Sat at 0 / 25 / 50 /
// 75 / 100 % in VHS and in Hi-Fi; and Whistle at those points.
static bool changed(int i, int satType)
{
    if (i == kDrive || i == kComp || i == kNoise) return true;
    return (i == kLoSat || i == kHiSat) && satType == 0;
}
static std::string texts()
{
    std::string out;
    Fx f;
    for (int prog = 0; prog < f.p->numPrograms(); ++prog)
    {
        f.e->dispatcher(f.e, effSetProgram, 0, prog, nullptr, 0.0f);
        out += "program " + std::to_string(prog) + ":";
        const int type = (int)std::lround(f.p->get(kSatType));
        for (int i = 0; i < kNumParams; ++i) if (isSetting(i) && !changed(i, type)) out += " " + std::to_string(i) + "=" + f.text(i);
        out += "\n";
    }
    for (int type : { 1, 2 })
    {
        f.p->set(kSatType, (float)type);
        out += "type " + std::to_string(type) + ":";
        for (int i : { (int)kLoSat, (int)kHiSat })
            for (float v : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f }) { f.p->set(i, v); out += " " + f.text(i); }
        out += "\n";
    }
    out += "whistle:";
    for (float v : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f }) { f.p->set(kWhistle, v); out += " " + f.text(kWhistle); }
    out += "\n";
    return out;
}

#ifndef WRITE170
static std::vector<uint8_t> slurp(const char* path)
{
    std::vector<uint8_t> v;
    if (FILE* f = std::fopen(path, "rb"))
    {
        uint8_t b[4096]; size_t n;
        while ((n = std::fread(b, 1, sizeof(b), f)) > 0) v.insert(v.end(), b, b + n);
        std::fclose(f);
    }
    return v;
}
#endif

int main(int argc, char** argv)
{
    if (argc < 3) { std::printf("usage: readout_test <values file> <texts file>\nFAIL\n"); return 1; }
    std::vector<std::vector<uint8_t>> chunks;
    const std::vector<uint8_t> now = record(chunks);
    const std::string nowTexts = texts();
#ifdef WRITE170
    FILE* f = std::fopen(argv[1], "wb");
    if (f == nullptr || std::fwrite(now.data(), 1, now.size(), f) != now.size()) { std::printf("cannot write %s\nFAIL\n", argv[1]); return 1; }
    std::fclose(f);
    f = std::fopen(argv[2], "wb");
    if (f == nullptr || std::fwrite(nowTexts.data(), 1, nowTexts.size(), f) != nowTexts.size()) { std::printf("cannot write %s\nFAIL\n", argv[2]); return 1; }
    std::fclose(f);
    std::printf("wrote %zu + %zu bytes (1.7.0: the six parameters, their saved states and the other read-outs)\n", now.size(), nowTexts.size());
    return 0;
#else
    bool ok = true;
    auto check = [&](bool c, const char* what) { std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what); ok = ok && c; };
    const std::vector<uint8_t> old = slurp(argv[1]);
    const std::vector<uint8_t> oldTexts = slurp(argv[2]);
    std::printf("1.7.0 wrote %zu bytes, this source %zu (%zu saved states); read-outs %zu and %zu bytes\n",
                old.size(), now.size(), chunks.size(), oldTexts.size(), nowTexts.size());
    check(!old.empty() && old == now, "Drive, Lo Sat, Hi Sat, Comp, Noise and Whistle: id, name, range, default and mapping, getParameter, every program and the saved states are as in 1.7.0");
    check(g_extraWrong == 0, "(each saved state ends with 1.8's eight bytes of extra state, a new instance's NR clip level; without them it is 1.7.0's)");
    check(!oldTexts.empty() && std::string(oldTexts.begin(), oldTexts.end()) == nowTexts,
          "every other read-out in every program, Whistle, and Lo Sat and Hi Sat in VHS and Hi-Fi, read as in 1.7.0");

    // A state saved by 1.7.0 loads into this one as it was: the one with Drive at 50 % (the third
    // recorded), its bytes found in the 1.7.0 file by length and content.
    {
        const std::vector<uint8_t>& mine = chunks[2];
        bool found = false;
        for (size_t at = 0; !found && at + mine.size() <= old.size(); ++at)
            found = std::memcmp(old.data() + at, mine.data(), mine.size()) == 0;
        Fx f;
        const int loaded = (int)f.e->dispatcher(f.e, effSetChunk, 0, (VstIntPtr)mine.size(), const_cast<uint8_t*>(mine.data()), 0.0f);
        std::printf("  a 1.7.0 state with Drive at 50 %%: loads %d, Drive %.2f, getParameter %.4f\n", loaded, (double)f.p->get(kDrive), (double)f.e->getParameter(f.e, kDrive));
        check(found && loaded == 1 && f.p->get(kDrive) == 50.0f && f.e->getParameter(f.e, kDrive) == 0.5f, "it loads with Drive at 50 % (normalised 0.5)");
    }

    auto row = [](Fx& f, int i, std::initializer_list<float> vs)
    {
        std::string r;
        for (float v : vs) { f.p->set(i, v); r += (r.empty() ? "" : " | ") + f.text(i); }
        return r;
    };
    auto show = [](const char* what, const std::string& r) { std::printf("  %s: \"%s\"\n", what, r.c_str()); };
    const std::initializer_list<float> five = { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f };

    // Drive: 20 log10(1 + 15 a^2), one decimal, as the other dB read-outs.
    {
        Fx f;
        const std::string r = row(f, kDrive, { 0.0f, 25.0f, 50.0f, 100.0f });
        char label[64] = {0};
        f.e->dispatcher(f.e, effGetParamLabel, kDrive, 0, label, 0.0f);
        show("Drive at 0, 25, 50, 100 %", r);
        check(r == "0.0 dB | +5.7 dB | +13.5 dB | +24.1 dB" && label[0] == 0, "Drive reads the gain into the clipper in dB");
        f.p->set(kDrive, 1.0f); const std::string low = f.text(kDrive);
        f.p->set(kDrive, 17.0f); const std::string blend = f.text(kDrive);
        std::printf("  Drive at 1 %% \"%s\", at 17 %% \"%s\"\n", low.c_str(), blend.c_str());
        check(low == "0.0 dB" && blend == "+3.1 dB", "a hair above 0 still reads 0.0 dB; the clean blend ends near +3.1 dB");
    }

    // Lo Sat and Hi Sat: 20 log10(1 + 9 a^2) in Simple; their own read-outs in VHS and Hi-Fi.
    {
        Fx f;
        const char* simple = "0.0 dB | +3.9 dB | +10.2 dB | +15.7 dB | +20.0 dB";
        f.p->set(kSatType, 0.0f);
        const std::string lo = row(f, kLoSat, five), hi = row(f, kHiSat, five);
        show("Simple, Lo Sat at 0 .. 100 %", lo); show("Simple, Hi Sat at 0 .. 100 %", hi);
        check(lo == simple && hi == simple, "in Simple, Lo Sat and Hi Sat read the gain into the band's clipper in dB");
        f.p->set(kSatType, 1.0f);
        const std::string vLo = row(f, kLoSat, five), vHi = row(f, kHiSat, five);
        show("VHS, Lo Sat", vLo); show("VHS, Hi Sat", vHi);
        check(vLo == "-10.0 dB | -5.5 dB | -1.0 dB | +3.5 dB | +8.0 dB" && vHi == "SP | 25 mm/s | 19 mm/s | 15 mm/s | EP",
              "in VHS they keep their record level and tape speed");
        f.p->set(kSatType, 2.0f);
        const std::string hLo = row(f, kLoSat, five), hHi = row(f, kHiSat, five);
        show("Hi-Fi, Lo Sat", hLo); show("Hi-Fi, Hi Sat", hHi);
        f.p->set(kSatType, 0.0f);
        f.p->set(kLoSat, 50.0f);
        check(hHi == "0 dB | -5 dB | -10 dB | -15 dB | -20 dB" && hLo.find("dBFS") == std::string::npos && hLo.rfind("-10.0 dB", 0) == 0
              && f.text(kLoSat) == "+10.2 dB", "in Hi-Fi they keep record level and carrier; back on Simple, the gain again");
    }

    // Comp: the threshold, a whole number of dBFS; 0 dBFS at 0 %.
    {
        Fx f;
        const std::string r = row(f, kComp, five);
        f.p->set(kComp, 1.0f); const std::string low = f.text(kComp);
        show("Comp at 0 .. 100 %", r);
        check(r == "0 dBFS | -9 dBFS | -18 dBFS | -27 dBFS | -36 dBFS" && low == "0 dBFS", "Comp reads its threshold in dBFS (0 dBFS at 0 %, never -0)");
    }

    // Noise: 20 log10(4 n^2) against 50 %; off at 0; the same with NOISE's power off.
    {
        Fx f;
        const std::string r = row(f, kNoise, five);
        show("Noise at 0 .. 100 %", r);
        f.p->set(kNoiseOn, 0.0f);
        const std::string offSw = row(f, kNoise, five);
        f.p->set(kNoise, 0.5f); const std::string tiny = f.text(kNoise);
        f.p->set(kNoise, 0.01f); const std::string tinier = f.text(kNoise);
        std::printf("  Noise at 0.5 %% \"%s\", at 0.01 %% \"%s\"\n", tiny.c_str(), tinier.c_str());
        check(r == "off | -12.0 dB | 0.0 dB | +7.0 dB | +12.0 dB", "Noise reads the noise level against its 50 % in dB, off at 0");
        check(offSw == r, "with NOISE's power off the knob still shows its figure");
        check(tiny == "-80.0 dB" && tinier == "-148 dB" && tinier.size() <= 8, "far down it still fits in 8 characters");
    }
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
#endif
}
