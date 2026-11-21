// VHS Vibes 1.8: Clipper, how hard the B-Type NR's overshoot suppressor works (see kNrClipLaw and
// DiodeCurve in the source). Up to 1.7 the suppressor was a hard clamp at a level that clipped loud
// steady tones; since 1.8 it is a pair of silicon diodes' curve at every setting, and the knob sets
// its level: 0 % (a new instance, every program but Stereo Rental) acts only on sudden jumps, 50 % is the amount of
// clipping of every version before (an older state), 100 % well below. Checked here:
//   1. the knob in the state: a new instance has 0 %, a state saved by 1.7.0 loads at 50 % and keeps
//      it, a state saved by the first 1.8 builds (eight bytes of extra state, no Clipper) loads as any
//      other, at 50 %, every program has 0 % but Stereo Rental (50 %), a state that cannot be loaded leaves it as it was;
//   2. the sound: with NR off, HQ off, 1.7.0's to the bit, and the knob changes nothing at all; with
//      NR in use at 50 % (as an old project) what differs from 1.7.0 and by how much;
//   3. the encoder's distortion at 0 % on steady tones: at most 0.15 % at 1 kHz at NR reference
//      level and 0.3 % at 10 kHz 10 dB above it (the figures encoders are specified to), HQ off and on;
//   4. a jump from -60 dB to reference level: how far the first cycles overshoot the steady level;
//   5. the knob's law and its three points: 50 % against the old clamp, 100 % plainly dirty;
//   6. NR On (encode, then decode, nothing between) gives back the input at 0, 50 and 100 %;
//   7. the record-side figures nr_test checks still hold at 0 and 50 % (the suppressor does not move them).
// The same program built against the 1.7.0 source (-DWRITE170, run.sh takes it from git) first loads
// a state this one saved (NR On, Clipper 73 %), then saves a state of its own and five renders.
// Ends with PASS or FAIL.
//   nrclip_test save <state file>                         (this source: a state for 1.7.0 to load)
//   nrclip_test <1.7.0 state> <1.7.0 renders>             (this source: the checks)
//   WRITE170: nrclip_test <1.8 state> <state out> <renders out>
#ifdef SRC
#include SRC
#else
#include "../src/VHSVibes.cpp"
#endif
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) { return opcode == audioMasterVersion ? 2400 : 0; }
static double dB(double x) { return 20.0 * std::log10(x > 1e-30 ? x : 1e-30); }
static const double kTwoPiD = 6.283185307179586;
static const int kInit = 20;

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
static bool spit(const char* path, const std::vector<uint8_t>& v)
{
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    const bool ok = std::fwrite(v.data(), 1, v.size(), f) == v.size();
    std::fclose(f);
    return ok;
}

// An instance on Init with HQ as asked, NOISE off. With `state` it loads that state after the
// resume, as a host does when it opens a project (and then sets HQ again).
struct Fx
{
    AEffect* e; float sr; long pos = 0;
    Fx(int hq, const std::vector<uint8_t>* state = nullptr, float rate = 44100.0f) : sr(rate)
    {
        e = VSTPluginMain(hostCb);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, rate);
        e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
        set(kHq, (float)hq);
        e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
        if (state != nullptr) load(*state);
        else e->dispatcher(e, effSetProgram, 0, kInit, nullptr, 0.0f);
        set(kHq, (float)hq);
        set(kNoiseOn, 0);
    }
    ~Fx() { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f); e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f); }
    mpc::Plugin* p() { return static_cast<mpc::Plugin*>(e->object); }
    void set(int i, float plain) { p()->set(i, plain); }
    float get(int i) { return p()->get(i); }
    std::vector<uint8_t> chunk()
    {
        void* data = nullptr;
        const VstIntPtr size = e->dispatcher(e, effGetChunk, 0, 0, &data, 0.0f);
        const uint8_t* d = static_cast<const uint8_t*>(data);
        return std::vector<uint8_t>(d, d + size);
    }
    VstIntPtr load(std::vector<uint8_t> s) { return e->dispatcher(e, effSetChunk, 0, (VstIntPtr)s.size(), s.data(), 0.0f); }
    // `sec` seconds of sig(sample index) in both channels, 128-frame calls; appends the left channel
    void play(double sec, const std::function<double(long)>& sig, std::vector<float>* out = nullptr)
    {
        float L[128], R[128]; float* io[2] = { L, R };
        const long n = (long)(sec * sr);
        for (long k = 0; k < n; k += 128)
        {
            const int nb = (int)std::min<long>(128, n - k);
            for (int i = 0; i < nb; ++i) L[i] = R[i] = (float)sig(pos + i);
            e->processReplacing(e, io, io, nb);
            if (out != nullptr) out->insert(out->end(), L, L + nb);
            pos += nb;
        }
    }
};

// A loud steady tone with loud bursts on top, for the comparison with 1.7.0: 997 Hz at -3 dBFS and,
// every 0.4 s for 50 ms, 5 kHz at -6 dBFS from silence (the suppressor's own work).
static double loud(long i)
{
    const double t = i / 44100.0;
    const double burst = std::fmod(t, 0.4) < 0.05 ? 0.5 * std::sin(kTwoPiD * 5000.0 * t) : 0.0;
    return 0.708 * std::sin(kTwoPiD * 997.0 * t) * (t < 1.0 ? 1.0 : 0.0) + burst;
}
// The five renders 1.7.0 writes, HQ off, 2 s of `loud` each: NR Off as Init has it, NR Off with Drive
// 60, the Saturator and Comp 50 (stages that distort), then NR Rec, Play and On.
static const int kRenders = 5;
static const char* const kRenderName[kRenders] = { "NR Off", "NR Off, Drive 60, Comp 50", "NR Rec", "NR Play", "NR On" };
static std::vector<float> render(int k, const std::function<Fx*()>& make)
{
    Fx* f = make();
    f->set(kNr, k < 2 ? 0.0f : (float)(k - 1));
    if (k == 1) { f->set(kDrive, 60.0f); f->set(kSatOn, 1.0f); f->set(kComp, 50.0f); }
    std::vector<float> out;
    f->play(2.0, loud, &out);
    delete f;
    return out;
}

#ifdef WRITE170
int main(int argc, char** argv)
{
    if (argc < 4) { std::printf("usage: nrclip_test <1.8 state> <state out> <renders out>\nFAIL\n"); return 1; }
    bool ok = true;
    // A state saved by 1.8 (NR On, Clipper 73 %, Drive 37): 1.7.0 loads it, every setting it knows as saved.
    {
        const std::vector<uint8_t> s18 = slurp(argv[1]);
        Fx f(0);
        const VstIntPtr r = f.load(s18);
        const bool same = r == 1 && f.get(kNr) == 3.0f && f.get(kDrive) == 37.0f;
        std::printf("1.7.0 loads a state saved by 1.8 (%zu bytes): returns %d, NR %.0f, Drive %.0f: %s\n", s18.size(), (int)r,
                    (double)f.get(kNr), (double)f.get(kDrive), same ? "every setting as saved" : "NOT as saved");
        ok = ok && same && !s18.empty();
        std::vector<float> out;
        f.play(1.0, loud, &out);
        double peak = 0.0; bool finite = true;
        for (float v : out) { finite = finite && std::isfinite(v); peak = std::fmax(peak, std::fabs(v)); }
        std::printf("  and plays it: peak %.3f, %s\n", peak, finite ? "no NaN or Inf" : "NaN or Inf");
        ok = ok && finite && peak > 0.1 && peak < 4.0;
    }
    // 1.7.0's own state: NR On, HQ off, on Init.
    {
        Fx f(0);
        f.set(kNr, 3.0f);
        if (!spit(argv[2], f.chunk())) { std::printf("cannot write %s\nFAIL\n", argv[2]); return 1; }
    }
    std::vector<uint8_t> all;
    for (int k = 0; k < kRenders; ++k)
    {
        const std::vector<float> r = render(k, [] { return new Fx(0); });
        const uint8_t* b = reinterpret_cast<const uint8_t*>(r.data());
        all.insert(all.end(), b, b + r.size() * 4);
    }
    if (!spit(argv[3], all)) { std::printf("cannot write %s\nFAIL\n", argv[3]); return 1; }
    std::printf("1.7.0 wrote its state and %d renders\n", kRenders);
    if (!ok) std::printf("FAIL\n");
    return ok ? 0 : 1;
}
#else

// The state's extra part: "VVX" version flags, or "" when there is none (as this version saves).
static std::string extraOf(const std::vector<uint8_t>& s)
{
    if (s.size() < 12) return "?";
    auto u32 = [&](size_t at) { uint32_t x = 0; for (int i = 0; i < 4; ++i) x |= (uint32_t)s[at + (size_t)i] << (8 * i); return x; };
    const uint32_t version = u32(4), n = u32(8);
    size_t pos = 12;
    for (uint32_t k = 0; k < n && pos < s.size(); ++k) pos += 1 + s[pos] + (version >= 2 ? 8 : 4);
    if (pos + 4 > s.size()) return "?";
    const uint32_t len = u32(pos);
    if (pos + 4 + len != s.size()) return "?";
    if (len == 0) return "";
    char b[64];
    if (len == 8 && std::memcmp(&s[pos + 4], "VVX", 3) == 0) std::snprintf(b, sizeof(b), "VVX%u:%u", (unsigned)s[pos + 7], (unsigned)u32(pos + 8));
    else std::snprintf(b, sizeof(b), "%u bytes", (unsigned)len);
    return b;
}
// A state as the first 1.8 builds saved it: no Clipper, and their eight bytes of extra state after the
// parameters ('V' 'V' 'X' 1, then the flag for their NR clip level).
static std::vector<uint8_t> asFirst18(const std::vector<uint8_t>& s, uint32_t flag)
{
    auto u32 = [&](size_t at) { uint32_t x = 0; for (int i = 0; i < 4; ++i) x |= (uint32_t)s[at + (size_t)i] << (8 * i); return x; };
    std::vector<uint8_t> out(s.begin(), s.begin() + 12);
    uint32_t n = u32(8), kept = 0;
    size_t pos = 12;
    for (uint32_t k = 0; k < n; ++k)
    {
        const size_t len = 1 + s[pos] + 8;
        if (std::string(reinterpret_cast<const char*>(&s[pos + 1]), s[pos]) != "clipper") { out.insert(out.end(), s.begin() + (long)pos, s.begin() + (long)(pos + len)); ++kept; }
        pos += len;
    }
    std::memcpy(&out[8], &kept, 4);
    const uint8_t tail[12] = { 8, 0, 0, 0, 'V', 'V', 'X', 1, (uint8_t)flag, 0, 0, 0 };
    out.insert(out.end(), tail, tail + 12);
    return out;
}

// The distortion (and noise) of a steady tone: what is left of `out` once the tone itself (its sine
// and cosine at hz, and DC) is taken off, against the tone, over a whole second of whole cycles.
static double thdOf(const std::vector<float>& out, size_t from, double hz, double sr)
{
    const size_t n = (size_t)sr;
    double c = 0, s = 0, m = 0;
    for (size_t i = 0; i < n; ++i) { const double w = kTwoPiD * hz * (double)i / sr, v = out[from + i]; c += v * std::cos(w); s += v * std::sin(w); m += v; }
    c *= 2.0 / (double)n; s *= 2.0 / (double)n; m /= (double)n;
    double r = 0;
    for (size_t i = 0; i < n; ++i) { const double w = kTwoPiD * hz * (double)i / sr, d = out[from + i] - m - c * std::cos(w) - s * std::sin(w); r += d * d; }
    return std::sqrt(r / (double)n) / std::sqrt(0.5 * (c * c + s * s));
}
// The level of the 3rd harmonic of hz in `out` (from `from`, a second), in dBFS.
static double h3Of(const std::vector<float>& out, size_t from, double hz, double sr)
{
    const size_t n = (size_t)sr;
    double c = 0, s = 0;
    for (size_t i = 0; i < n; ++i) { const double w = kTwoPiD * 3.0 * hz * (double)i / sr; c += out[from + i] * std::cos(w); s += out[from + i] * std::sin(w); }
    return dB(2.0 * std::hypot(c, s) / (double)n);
}

// The encoder with the suppressor at knob position `knob` (0 to 1; below 0: up to 1.7, a hard clamp at
// 0.12 kRefLevel, worked out here as 1.7.0 did it; above 1: none). HQ: encode<true>, as inside the
// doubled section.
struct Enc
{
    NoiseRed nr; Stereo cp = {}; float knob; bool hq;
    Enc(float sr, float k, bool h) : knob(k), hq(h)
    {
        nr.prepare(sr, false);
        if (k > 1.0f) { nr.setClip(1.0e6f); return; }
        float t, g, tr; nrClipParams(k < 0.0f ? 0.5f : k, t, g, tr); nr.setClip(t, g, tr);
    }
    Stereo run(Stereo x)
    {
        if (knob >= 0.0f) return hq ? nr.encode<true>(x, 1.0f, &cp) : nr.encode<false>(x);
        const Stereo vB = (nr.c1 * (nr.sq + nr.kap * x) - nr.c2 * nr.su) * nr.rec;
        nr.step(x, vB);
        const Stereo e = kNrGain * vB;
        nr.control(e);
        return x + vclamp(e, 0.12f * kRefLevel);
    }
};
static std::vector<float> encTone(float sr, float knob, bool hq, double hz, double levelDb, double sec = 2.0)
{
    Enc en(sr, knob, hq);
    const double amp = kRefLevel * std::pow(10.0, levelDb / 20.0);
    std::vector<float> out;
    for (long i = 0; i < (long)(sec * sr); ++i) out.push_back(en.run(splat((float)(amp * std::sin(kTwoPiD * hz * (double)i / sr))))[0]);
    return out;
}
static double encThd(float sr, float knob, bool hq, double hz, double levelDb) { return thdOf(encTone(sr, knob, hq, hz, levelDb), (size_t)sr, hz, sr); }

// A tone at -60 dB re reference level for a second, then at reference level: the largest sample in
// the first 20 ms after the jump against the largest once it has settled (0.8 to 1 s after), in dB.
static double overshoot(float sr, float knob, bool hq, double hz)
{
    Enc en(sr, knob, hq);
    const long n0 = (long)sr;
    double pk = 0, pks = 0;
    for (long i = 0; i < 2 * n0; ++i)
    {
        const double a = kRefLevel * (i < n0 ? 0.001 : 1.0);
        const float y = en.run(splat((float)(a * std::sin(kTwoPiD * hz * (double)i / sr))))[0];
        if (i >= n0 && i < n0 + (long)(0.02 * sr)) pk = std::fmax(pk, std::fabs(y));
        if (i >= n0 + (long)(0.8 * sr)) pks = std::fmax(pks, std::fabs(y));
    }
    return dB(pk / pks);
}

// The record side's boost of a sine (as nr_test's gainOf), the suppressor at `knob`.
static double gainOf(float sr, float knob, float hz, float levelDb)
{
    Enc en(sr, knob, false);
    const int n = (int)sr;
    const float amp = kRefLevel * std::pow(10.0f, levelDb / 20.0f);
    double si = 0, so = 0;
    for (int i = 0; i < n; ++i)
    {
        const float x = amp * (float)std::sin(kTwoPiD * hz * i / sr);
        const float y = en.run(splat(x))[0];
        if (i >= (int)(0.7f * sr)) { si += (double)x * x; so += (double)y * y; }
    }
    return 10.0 * std::log10(so / si);
}

int main(int argc, char** argv)
{
    if (argc >= 3 && std::strcmp(argv[1], "save") == 0)
    {
        Fx f(0);
        f.set(kNr, 3.0f); f.set(kDrive, 37.0f); f.set(kClipper, 73.0f);
        if (!spit(argv[2], f.chunk())) { std::printf("cannot write %s\nFAIL\n", argv[2]); return 1; }
        std::printf("wrote a 1.8 state (NR On, Drive 37, Clipper 73 %%, extra \"%s\")\n", extraOf(f.chunk()).c_str());
        return 0;
    }
    if (argc < 3) { std::printf("usage: nrclip_test <1.7.0 state> <1.7.0 renders>  |  nrclip_test save <file>\nFAIL\n"); return 1; }
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("   %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };
    const std::vector<uint8_t> s170 = slurp(argv[1]), r170 = slurp(argv[2]);
    char what[256];

    std::printf("1. Clipper in the state\n");
    {
        Fx fresh(0);
        // a new instance, before any program
        AEffect* raw = VSTPluginMain(hostCb);
        mpc::Plugin* rp = static_cast<mpc::Plugin*>(raw->object);
        const mpc::Param& pc = rp->param(kClipper);
        char txt[64] = {0};
        rp->set(kClipper, 50.0f); raw->dispatcher(raw, effGetParamDisplay, kClipper, 0, txt, 0.0f);
        const std::string at50 = txt;
        rp->set(kClipper, pc.def);
        std::printf("   kClipper %d of %d: \"%s\" \"%s\", %.0f to %.0f, a new instance %.0f %%; reads \"%s\" at 50 %%\n", (int)kClipper, (int)kNumParams,
                    pc.id.c_str(), pc.name.c_str(), (double)pc.min, (double)pc.max, (double)rp->get(kClipper), at50.c_str());
        check(kClipper == 66 && kNumParams == 67 && raw->numParams == 67 && pc.id == "clipper" && pc.name == "Clipper" && pc.min == 0.0f && pc.max == 100.0f
              && pc.def == 0.0f && isSetting(kClipper) && groupOf(kClipper) == gNr && raw->dispatcher(raw, effCanBeAutomated, kClipper, 0, nullptr, 0.0f) == 1,
              "number 66, the last: clipper, \"Clipper\", 0 to 100, 0 in a new instance, a setting, automatable, in the NR group");
        check(at50 == "50%" || at50 == "50 %", "it reads in % alone");
        raw->dispatcher(raw, effClose, 0, 0, nullptr, 0.0f);

        fresh.set(kClipper, 73.0f);
        const std::vector<uint8_t> sNew = fresh.chunk();
        std::printf("   a state saved now: %zu bytes, extra \"%s\"\n", sNew.size(), extraOf(sNew).c_str());
        check(extraOf(sNew) == "", "a state saved now has nothing after its parameters");
        Fx back(0, &sNew);
        check(back.get(kClipper) == 73.0f, "a state with Clipper 73 % loads at 73 %");
        std::printf("   1.7.0's state: %zu bytes, extra \"%s\"\n", s170.size(), extraOf(s170).c_str());
        Fx old(0, &s170);
        check(!s170.empty() && old.get(kClipper) == kClipperOld && old.get(kNr) == 3.0f, "a state saved by 1.7.0 loads at 50 % (NR On as saved)");
        const std::vector<uint8_t> sOld = old.chunk();
        Fx again(1, &sOld);
        check(again.get(kClipper) == kClipperOld, "saved again and loaded (here with HQ on), still 50 %");
        check(back.load(s170) == 1 && back.get(kClipper) == kClipperOld, "loaded over an instance at 73 %, too");
        // the first 1.8 builds' states
        bool first = true;
        for (uint32_t flag : { 1u, 0u })
        {
            const std::vector<uint8_t> s = asFirst18(sNew, flag);
            Fx f(0); f.set(kClipper, 20.0f);
            const VstIntPtr r = f.load(s);
            first = first && r == 1 && f.get(kClipper) == kClipperOld && f.get(kNr) == 0.0f && extraOf(s) == (flag ? "VVX1:1" : "VVX1:0");
            for (size_t k : { 1, 3, 5, 8, 11 })   // cut short inside its extra part: the parameters are all there
            {
                std::vector<uint8_t> t = s; t.resize(t.size() - k);
                f.set(kClipper, 20.0f); f.set(kDrive, 11.0f);
                first = first && f.load(t) == 1 && f.get(kClipper) == kClipperOld && f.get(kDrive) == back.get(kDrive);
            }
        }
        check(first, "a state of the first 1.8 builds (their extra part, no Clipper) loads at 50 %, whatever its flag; cut short in that part, too");
        // every program at 0 %, as a new instance (and as one the MPC puts on program 0 when it loads it)
        Fx viaProg(0);
        bool progs = true;
        for (int prog = 0; prog < viaProg.p()->numPrograms(); ++prog)
        {
            viaProg.set(kClipper, 9.0f);
            viaProg.e->dispatcher(viaProg.e, effSetProgram, 0, prog, nullptr, 0.0f);
            char nm[64] = {0}; viaProg.e->dispatcher(viaProg.e, effGetProgramNameIndexed, prog, 0, nm, 0.0f);
            progs = progs && viaProg.get(kClipper) == (std::string(nm) == "Stereo Rental" ? kClipperOld : 0.0f);
        }
        AEffect* mpc = VSTPluginMain(hostCb);   // the MPC's load: sample rate, open, program 0
        mpc->dispatcher(mpc, effSetSampleRate, 0, 0, nullptr, 44100.0f);
        mpc->dispatcher(mpc, effOpen, 0, 0, nullptr, 0.0f);
        mpc->dispatcher(mpc, effSetProgram, 0, 0, nullptr, 0.0f);
        const float fresh0 = static_cast<mpc::Plugin*>(mpc->object)->get(kClipper);
        mpc->dispatcher(mpc, effClose, 0, 0, nullptr, 0.0f);
        check(progs && fresh0 == 0.0f, "every program puts Clipper at 0 % but Stereo Rental (50 %, its 1.7.0 sound), and a new instance the MPC puts on program 0 stays at 0 %");
        // a state that cannot be loaded leaves it as it was
        Fx keep(0); keep.set(kClipper, 61.0f);
        std::vector<uint8_t> cut = sNew; cut.resize(cut.size() / 2);
        std::vector<uint8_t> bad = sNew; bad[0] = 'X';
        check(keep.load(cut) == 0 && keep.get(kClipper) == 61.0f && keep.load(bad) == 0 && keep.get(kClipper) == 61.0f, "a state cut short or damaged leaves Clipper as it was");
    }
    {
        std::printf("   the sound, HQ off, 2 s of a loud tone and bursts, against 1.7.0 (as an old project: Clipper 50 %%)\n");
        const size_t len = (size_t)(2.0 * 44100.0);
        bool offSame = r170.size() == kRenders * len * 4, nrDiffer = true, nrClose = true;
        for (int k = 0; k < kRenders; ++k)
        {
            const std::vector<float> mine = render(k, [&] { return new Fx(0, &s170); });
            std::vector<float> theirs(len, 0.0f);
            if (r170.size() == kRenders * len * 4) std::memcpy(theirs.data(), r170.data() + k * len * 4, len * 4);
            double d = 0, r = 0, dHi = 0, rHi = 0; float hp1 = 0, hp2 = 0, dm = 0;
            for (size_t i = 0; i < len && i < mine.size(); ++i)
            {
                const float df = mine[i] - theirs[i];
                d += (double)df * df; r += (double)theirs[i] * theirs[i];
                const float h1 = mine[i] - hp1, h2 = theirs[i] - hp2;   // first differences: the highs
                hp1 = mine[i]; hp2 = theirs[i];
                dHi += (double)(h1 - h2) * (h1 - h2); rHi += (double)h2 * h2;
                dm = std::fmax(dm, std::fabs(df));
            }
            const bool same = mine == theirs;
            std::printf("   %-26s %s", kRenderName[k], same ? "1.7.0's to the bit\n" : "");
            if (!same) std::printf("differs: %.1f dB below the signal (its highs: %.1f dB), largest sample %.1f dBFS\n", dB(std::sqrt(d / r)), dB(std::sqrt(dHi / rHi)), dB(dm));
            if (k < 2) offSame = offSame && same;
            else { nrDiffer = nrDiffer && !same; nrClose = nrClose && dB(std::sqrt(d / r)) < -20.0; }
        }
        check(offSame, "with NR off the sound is 1.7.0's to the bit");
        check(nrDiffer && nrClose, "with NR in use the diode curve differs from 1.7.0's clamp, by less than -20 dB of the sound");
        // NR off: the knob changes nothing
        bool off = true;
        for (int hq : { 0, 1 })
            for (float sr : { 44100.0f, 96000.0f })
            {
                Fx a(hq, nullptr, sr), b(hq, nullptr, sr);
                a.set(kClipper, 0.0f); b.set(kClipper, 100.0f);
                for (Fx* f : { &a, &b }) { f->set(kNr, 0.0f); f->set(kDrive, 60.0f); f->set(kSatOn, 1.0f); f->set(kComp, 50.0f); }
                std::vector<float> oa, ob;
                a.play(1.0, loud, &oa); b.play(0.5, loud, &ob);
                b.set(kClipper, 37.0f);   // moved while it plays
                b.play(0.5, loud, &ob);
                off = off && oa == ob;
            }
        check(off, "with NR off Clipper changes nothing, to the bit (HQ off and on, 44.1 and 96 kHz, moved while it plays)");
    }

    std::printf("2. Distortion of a steady tone through the record side at 0 %% (THD + noise)\n");
    {
        struct { double hz, level, limit; } pts[] = { { 1000, 0, 0.0015 }, { 10000, 10, 0.003 } };
        for (const auto& pt : pts)
        {
            double worst = 0;
            for (int hq : { 0, 1 })
            {
                const float sr = hq ? 88200.0f : 44100.0f;
                const double e0 = encThd(sr, 0.0f, hq != 0, pt.hz, pt.level), e50 = encThd(sr, 0.5f, hq != 0, pt.hz, pt.level), eOld = encThd(sr, -1.0f, hq != 0, pt.hz, pt.level);
                // and through the plugin, on Init with NR Rec (and Off, for what the rest adds)
                double plug[2];
                for (int k = 0; k < 2; ++k)
                {
                    Fx f(hq);
                    f.set(kClipper, 0.0f);
                    f.set(kNr, k == 1 ? 0.0f : 1.0f);
                    const double amp = kRefLevel * std::pow(10.0, pt.level / 20.0);
                    std::vector<float> out;
                    f.play(2.0, [&](long i) { return amp * std::sin(kTwoPiD * pt.hz * (double)i / 44100.0); }, &out);
                    plug[k] = thdOf(out, 44100, pt.hz, 44100.0);
                }
                std::printf("   %5.0f Hz %+3.0f dB re ref, HQ %s: encoder %.3f %% (50 %%: %.3f %%, 1.7's clamp %.3f %%); plugin NR Rec %.3f %%, NR Off %.4f %%\n",
                            pt.hz, pt.level, hq ? "on " : "off", 100 * e0, 100 * e50, 100 * eOld, 100 * plug[0], 100 * plug[1]);
                worst = std::fmax(worst, std::fmax(e0, plug[0]));
            }
            std::snprintf(what, sizeof(what), "%.0f Hz at %+.0f dB re reference level: at most %.2f %% at Clipper 0 %%, HQ off and on", pt.hz, pt.level, 100 * pt.limit);
            check(worst <= pt.limit, what);
        }
    }

    std::printf("3. Overshoot of a jump from -60 dB to reference level (first 20 ms against the settled peak)\n");
    {
        double worst0 = 0, off50 = 0; bool order = true;
        for (double hz : { 1000.0, 5000.0 })
        {
            double o[2][4];
            for (int hq : { 0, 1 })
            {
                const float sr = hq ? 88200.0f : 44100.0f;
                const float ks[4] = { 0.0f, 0.5f, 1.0f, -1.0f };
                for (int j = 0; j < 4; ++j) o[hq][j] = overshoot(sr, ks[j], hq != 0, hz);
                worst0 = std::fmax(worst0, o[hq][0]);
                off50 = std::fmax(off50, std::fabs(o[hq][1] - o[hq][3]));
                order = order && o[hq][0] > o[hq][1];
            }
            std::printf("   %4.0f Hz, HQ off / on: 0 %% %+.2f / %+.2f dB, 50 %% %+.2f / %+.2f, 100 %% %+.2f / %+.2f; 1.7's clamp %+.2f / %+.2f; none %+.2f / %+.2f\n", hz,
                        o[0][0], o[1][0], o[0][1], o[1][1], o[0][2], o[1][2], o[0][3], o[1][3], overshoot(44100.0f, 2.0f, false, hz), overshoot(88200.0f, 2.0f, true, hz));
        }
        check(worst0 < 3.7, "at 0 % the overshoot stays near 3 dB, 3.7 at most (without the suppressor it is 5 to 9 dB)");
        check(off50 < 0.3, "at 50 % within 0.3 dB of 1.7's clamp");
        check(order, "and less at 50 % than at 0 % (above 50 % the side chain is driven: see 4)");
    }

    std::printf("4. The knob: up to 50 %% ln T a parabola in it; above, T rises again and the side chain is driven\n");
    {
        float t0, t50, t100, g, tr, g100, tr100;
        nrClipParams(0.0f, t0, g, tr); nrClipParams(0.5f, t50, g, tr);
        const bool rest = g == 1.0f && tr == 1.0f;
        nrClipParams(1.0f, t100, g100, tr100);
        t0 /= kRefLevel; t50 /= kRefLevel; t100 /= kRefLevel;
        bool mono = true;
        for (int i = 1; i <= 500; ++i) mono = mono && nrClipLevel(i * 0.001f) < nrClipLevel((i - 1) * 0.001f);
        float lt = t50 * kRefLevel, lg = 1.0f;
        for (int i = 501; i <= 1000; ++i) { float t, gg, r; nrClipParams(i * 0.001f, t, gg, r); mono = mono && t >= lt && gg > lg; lt = t; lg = gg; }
        std::printf("   T at 0, 25, 50, 75, 100 %%: %.4f %.4f %.4f %.4f %.4f kRefLevel; drive at 100 %% %.1f, trim %.2f\n", t0, nrClipLevel(0.25f) / kRefLevel, t50,
                    [] { float t, g2, r; nrClipParams(0.75f, t, g2, r); return t / kRefLevel; }(), t100, g100, tr100);
        check(std::fabs(t0 - 0.44f) < 1e-5f && std::fabs(t50 - 0.09f) < 1e-5f && rest && mono && std::fabs(t100 - 0.27f) < 1e-4f && std::fabs(g100 - 30.0f) < 1e-3f,
              "0.44 and 0.09 kRefLevel at 0 and 50 % (no drive there), falling between; above 50 % T rising to 0.27 and the drive to 30");
        // 50 % against 1.7's clamp: the 3rd harmonic of 1 kHz at reference level and at -3 dBFS (HQ: 88.2 kHz)
        double worst = 0;
        for (double dbfs : { -8.0, -3.0 })
        {
            const double lv = dbfs - dB(kRefLevel);
            const std::vector<float> a = encTone(88200.0f, 0.5f, true, 1000.0, lv), b = encTone(88200.0f, -1.0f, false, 1000.0, lv);
            const double ha = h3Of(a, 88200, 1000.0, 88200.0), hb = h3Of(b, 88200, 1000.0, 88200.0);
            std::printf("   1 kHz at %+.0f dBFS: 3rd harmonic at 50 %% %.1f dBFS, 1.7's clamp %.1f dBFS\n", dbfs, ha, hb);
            worst = std::fmax(worst, std::fabs(ha - hb));
        }
        check(worst < 2.0, "at 50 % the 3rd harmonic is 1.7's within 2 dB (the diodes' knee is rounder: at -3 dBFS 1.9 dB less)");
        // above 50 %: dirtier with every step, at every level; at 100 % plainly dirty, loud material too
        bool rising = true; double loud = 1.0, quiet = 1.0;
        for (double hz : { 1000.0, 5000.0 })
            for (double dbfs : { -38.0, -18.0, -8.0, -3.0 })
            {
                double t[6];
                for (int j = 0; j < 6; ++j) t[j] = encThd(88200.0f, 0.5f + 0.1f * j, true, hz, dbfs - dB(kRefLevel));
                std::printf("   %4.0f Hz at %+3.0f dBFS: THD at 50 / 60 / 70 / 80 / 90 / 100 %%: %5.2f %5.2f %5.2f %5.2f %5.2f %5.2f %%\n", hz, dbfs,
                            100 * t[0], 100 * t[1], 100 * t[2], 100 * t[3], 100 * t[4], 100 * t[5]);
                for (int j = 1; j < 6; ++j) rising = rising && t[j] > t[j - 1];
                if (dbfs >= -8.0) loud = std::fmin(loud, t[5]); else quiet = std::fmin(quiet, t[5]);
            }
        check(rising, "above 50 % every step of the knob is dirtier, 1 and 5 kHz from -38 to -3 dBFS");
        check(loud > 0.09 && quiet > 0.2, "at 100 %: over 9 % at reference level and at -3 dBFS, over 20 % at -18 and -38 dBFS");
    }

    std::printf("5. NR On gives back the input (the difference against the tone, over the second second)\n");
    {
        // The two sides alone, nothing between: HQ off both at 44.1 kHz; HQ on the encoder at twice
        // the rate (encode<true>, on the tone made at that rate, every other sample kept: a steady
        // tone that is not clipped needs no filter for that) and the decoder at 44.1 kHz (decode<true>).
        auto direct = [](bool hq, float knob, double hz, double dbfs)
        {
            NoiseRed en, de; en.prepare(hq ? 88200.0f : 44100.0f, false); de.prepare(44100.0f, true);
            float t, g, tr; nrClipParams(knob, t, g, tr);
            en.setClip(t, g, tr); de.setClip(t, g, tr);
            Stereo cpE = {}, cpD = {};
            const double amp = std::pow(10.0, dbfs / 20.0);
            double d = 0, r = 0;
            for (long i = 0; i < 88200; ++i)
            {
                const double x = amp * std::sin(kTwoPiD * hz * (double)i / 44100.0);
                Stereo y;
                if (hq)
                {
                    en.encode<true>(splat((float)(amp * std::sin(kTwoPiD * hz * (i - 0.5) / 44100.0))), 1.0f, &cpE);
                    y = en.encode<true>(splat((float)x), 1.0f, &cpE);
                    y = de.decode<true>(y, &cpD);
                }
                else y = de.decode<false>(en.encode<false>(splat((float)x)));
                if (i >= 44100) { d += (y[0] - x) * (y[0] - x); r += x * x; }
            }
            return dB(std::sqrt(d / r));
        };
        // Through the plugin on Init, NR On against NR Off: its 4-pole Hi Cut (at 18 kHz) and Lo Cut
        // (at 20 Hz) sit between the two sides, so the decoder never gets back exactly what the
        // encoder made.
        auto plugin = [&](int hq, float knob, double hz, double dbfs)
        {
            Fx on(hq), off(hq);
            on.set(kNr, 3.0f); off.set(kNr, 0.0f); on.set(kClipper, 100.0f * knob);
            const double amp = std::pow(10.0, dbfs / 20.0);
            auto sig = [&](long i) { return amp * std::sin(kTwoPiD * hz * (double)i / 44100.0); };
            std::vector<float> a, b;
            on.play(2.0, sig, &a); off.play(2.0, sig, &b);
            double d = 0, r = 0;
            for (size_t i = 44100; i < a.size(); ++i) { d += (double)(a[i] - b[i]) * (a[i] - b[i]); r += (double)b[i] * b[i]; }
            return dB(std::sqrt(d / r));
        };
        // HQ on, the two sides alone, before 1.8's diodes (measured, the last build with a clamp):
        // 0 % against its clamp at 0.40 kRefLevel, 50 % against the clamp of 1.7 (0.12)
        struct Was { double hz, dbfs, at0, at50; };
        const Was was[] = { { 313, -3, -79.7, -76.8 }, { 313, -12, -70.6, -72.0 }, { 997, -3, -60.9, -56.5 }, { 997, -12, -52.7, -54.1 } };
        double worst[2] = { -999, -999 }, worstHiOff = -999, worstHiOn = -999;
        bool asGood = true;
        for (int hq : { 0, 1 })
            for (const Was& w : was)
            {
                double d[5], p[5];
                const float ks[5] = { 0.0f, 0.5f, 0.6f, 0.8f, 1.0f };
                for (int j = 0; j < 5; ++j) { d[j] = direct(hq != 0, ks[j], w.hz, w.dbfs); p[j] = plugin(hq, ks[j], w.hz, w.dbfs); }
                std::printf("   HQ %s %4.0f Hz %+4.0f dBFS: the two sides alone at 0 / 50 / 60 / 80 / 100 %%: %6.1f %6.1f %6.1f %6.1f %6.1f dB;  the plugin: %6.1f %6.1f %6.1f %6.1f %6.1f dB\n",
                            hq ? "on " : "off", w.hz, w.dbfs, d[0], d[1], d[2], d[3], d[4], p[0], p[1], p[2], p[3], p[4]);
                worst[hq] = std::fmax(worst[hq], std::fmax(d[0], d[1]));
                for (int j = 2; j < 5; ++j) (hq ? worstHiOn : worstHiOff) = std::fmax(hq ? worstHiOn : worstHiOff, d[j]);
                if (hq) asGood = asGood && d[0] <= w.at0 + 1.0 && d[1] <= w.at50 + 3.5;
            }
        check(worst[0] < -110.0, "HQ off, the two sides alone: within -110 dB at 0 and 50 %");
        check(asGood, "HQ on, the two sides alone: within 1 dB of the clamp before them at 0 % (or better), within 3.5 dB at 50 % (see NoiseRed::decode)");
        std::snprintf(what, sizeof(what), "above 50 %%, HQ off: worst %.1f dB (at most -100)", worstHiOff);
        check(worstHiOff < -100.0, what);
        std::snprintf(what, sizeof(what), "above 50 %%, HQ on (the encoder at twice the rate, the decoder at the session's): worst %.1f dB (at most -25: what the two rates' networks do not share, the steep curve shows)", worstHiOn);
        check(worstHiOn < -25.0, what);
    }

    std::printf("6. The record-side figures at 0 and 50 %% (as nr_test 1)\n");
    {
        struct { float hz, level, want; } pts[] = { {1400, -20, 4.4f}, {1400, -30, 7.5f}, {2000, -25, 7.0f}, {5000, -20, 3.2f},
                                                    {5000, -30, 8.2f}, {5000, -40, 10.3f}, {10000, -20, 2.6f}, {10000, -30, 6.5f} };
        bool ok = true; double moved[3] = { 0, 0, 0 }, worst50 = 0;
        for (float sr : {44100.0f, 48000.0f, 88200.0f, 96000.0f, 176400.0f, 192000.0f, 352800.0f, 384000.0f})
        {
            double worst = 0;
            for (auto& pt : pts)
            {
                const double g = gainOf(sr, 0.0f, pt.hz, pt.level), none = gainOf(sr, 2.0f, pt.hz, pt.level);
                worst = std::fmax(worst, std::fabs(g - pt.want));
                moved[0] = std::fmax(moved[0], std::fabs(g - none));
                const double g50 = gainOf(sr, 0.5f, pt.hz, pt.level);
                moved[1] = std::fmax(moved[1], std::fabs(g50 - none));
                worst50 = std::fmax(worst50, std::fabs(g50 - pt.want));
                if (sr == 44100.0f) moved[2] = std::fmax(moved[2], std::fabs(gainOf(sr, 1.0f, pt.hz, pt.level) - none));
            }
            std::printf("   %6.0f Hz rate: worst error %.2f dB at 0 %%\n", sr, worst);
            ok = ok && worst < (sr == 44100.0f ? 0.5 : 0.7);
        }
        double most = 0;
        for (float hz : {1400.0f, 5000.0f, 10000.0f}) most = std::fmax(most, std::fabs(gainOf(44100.0f, 0.0f, hz, 0.0f)));
        std::printf("   the eight against no suppressor: at most %.4f dB apart at 0 %%, %.4f at 50 %% (100 %%, 44.1 kHz: %.2f dB); at reference level at most %.2f dB\n",
                    moved[0], moved[1], moved[2], most);
        check(ok && moved[0] < 0.01, "all eight published figures met at 0 % (the suppressor does not work there)");
        std::printf("   at 50 %%: worst error against the published figures %.2f dB, every rate\n", worst50);
        check(moved[1] < 0.7 && worst50 < 1.0, "at 50 % the diodes' knee reaches the -20 dB points (up to 0.7 dB less boost): each figure within 1 dB");
        check(most < 0.5, "a signal at NR reference level passes unchanged");
    }

    std::printf("7. Driven hard (Clipper 100 %%): bounded, no NaN, back to silence (Init, NR Rec, Play, On, HQ off and on)\n");
    {
        bool ok = true; double worst = 0;
        for (int nr : { 1, 2, 3 })
            for (int hq : { 0, 1 })
                for (int in = 0; in < 4; ++in)
                {
                    Fx f(hq);
                    f.set(kNr, (float)nr); f.set(kClipper, 100.0f);
                    uint32_t s = 7;
                    std::vector<float> out;
                    // full-scale noise, DC 0.9, impulses of 1.0, and a 1 kHz tone at -6 dBFS with the knob swept 0 - 100 - 0
                    auto sig = [&](long i) -> double {
                        if (i >= 88200) return 0.0;
                        if (in == 0) { s = s * 1664525u + 1013904223u; return (s >> 8) / 8388608.0 - 1.0; }
                        if (in == 1) return 0.9;
                        if (in == 2) return i % 11025 == 0 ? 1.0 : 0.0;
                        return 0.5 * std::sin(kTwoPiD * 1000.0 * (double)i / 44100.0);
                    };
                    for (int b = 0; b < 88200 / 128 + 200; ++b)
                    {
                        if (in == 3) { const double t = b * 128.0 / 88200.0; f.set(kClipper, (float)(t < 0.5 ? 200.0 * t : t < 1.0 ? 200.0 - 200.0 * t : 0.0)); }
                        f.play(128.0 / 44100.0, sig, &out);
                    }
                    double pk = 0, tail = 0; bool fin = true;
                    for (size_t i = 0; i < out.size(); ++i) { fin = fin && std::isfinite(out[i]); pk = std::fmax(pk, std::fabs(out[i])); if (i + 4410 >= out.size()) tail = std::fmax(tail, std::fabs(out[i])); }
                    worst = std::fmax(worst, pk);
                    ok = ok && fin && pk < 3.0 && tail < 1e-6;
                }
        std::printf("   largest output against inputs up to 1.0: %.2f\n", worst);
        check(ok, "finite, under 3.0 for inputs up to full scale (+9.5 dB), silent again 0.4 s after the input stops");
    }

    std::printf(fails == 0 ? "PASS\n" : "FAIL\n");
    return fails == 0 ? 0 : 1;
}
#endif
