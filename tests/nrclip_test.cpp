// VHS Vibes 1.8: the B-Type NR overshoot clipper's level (see kNrClip18 in the source). Up to 1.7 it
// clipped loud steady tones; 1.8 raised it. It is not a parameter: the state carries it (the extra
// part the framework saves after the parameters). Checked here:
//   1. the state: a new instance has 1.8's level, a state saved by 1.7.0 loads with the old one and
//      keeps it when saved and loaded again, a state saved by 1.8 loads as it was saved, a program
//      leaves it as it is, a state that cannot be loaded leaves it as it was; with the old level and
//      HQ off the sound is 1.7.0's to the bit, with NR off the level changes nothing at all;
//   2. the encoder's distortion with the new level on steady tones: at most 0.15 % at 1 kHz at NR
//      reference level and 0.3 % at 10 kHz 10 dB above it (the figures encoders are specified to),
//      HQ off and on;
//   3. a jump from -60 dB to reference level: how far the first cycles overshoot the steady level;
//   4. NR On (encode, then decode, nothing between) gives back what NR Off gives;
//   5. the record-side figures nr_test checks still hold with the new level.
// The same program built against the 1.7.0 source (-DWRITE170, run.sh takes it from git) first loads
// a state this one saved (with the new level, NR On), then saves a state of its own and the sound of
// three renders. Ends with PASS or FAIL.
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

// A loud steady tone with loud bursts on top, for the bit comparison with 1.7.0: 997 Hz at -3 dBFS
// and, every 0.4 s for 50 ms, 5 kHz at -6 dBFS from silence (the clipper's own work).
static double loud(long i)
{
    const double t = i / 44100.0;
    const double burst = std::fmod(t, 0.4) < 0.05 ? 0.5 * std::sin(kTwoPiD * 5000.0 * t) : 0.0;
    return 0.708 * std::sin(kTwoPiD * 997.0 * t) * (t < 1.0 ? 1.0 : 0.0) + burst;
}
static uint64_t fnv(const std::vector<float>& v)
{
    uint64_t h = 1469598103934665603ull;
    const uint8_t* b = reinterpret_cast<const uint8_t*>(v.data());
    for (size_t i = 0; i < v.size() * 4; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}
// The three renders 1.7.0 writes: NR Rec, Play and On, HQ off, 2 s of `loud`.
static std::vector<uint64_t> renders(const std::function<Fx*()>& make)
{
    std::vector<uint64_t> h;
    for (int nr : { 1, 2, 3 })
    {
        Fx* f = make();
        f->set(kNr, (float)nr);
        std::vector<float> out;
        f->play(2.0, loud, &out);
        h.push_back(fnv(out));
        delete f;
    }
    return h;
}

#ifdef WRITE170
int main(int argc, char** argv)
{
    if (argc < 4) { std::printf("usage: nrclip_test <1.8 state> <state out> <renders out>\nFAIL\n"); return 1; }
    bool ok = true;
    // A state saved by 1.8 (NR On, 1.8's clip level, Drive 37): 1.7.0 loads it, every setting as saved.
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
    const std::vector<uint64_t> h = renders([] { return new Fx(0); });
    std::vector<uint8_t> hb(h.size() * 8);
    std::memcpy(hb.data(), h.data(), hb.size());
    if (!spit(argv[3], hb)) { std::printf("cannot write %s\nFAIL\n", argv[3]); return 1; }
    std::printf("1.7.0 wrote its state and %zu renders\n", h.size());
    if (!ok) std::printf("FAIL\n");
    return ok ? 0 : 1;
}
#else

// The state's extra part: "VVX" version flags (see saveExtraState), or "" when there is none.
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
static bool newLevel(Fx& f) { return static_cast<VHSVibes*>(f.p())->nrClip18(); }

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

// One sample through the encoder: HQ (encode<true>, as inside the doubled section) or not, 1.8's clip
// level (c18) or the old one; and through the decoder.
static Stereo enc(NoiseRed& nr, Stereo x, bool hq, bool c18, Stereo* cp)
{
    if (hq) return c18 ? nr.encode<true, true>(x, 1.0f, cp) : nr.encode<true, false>(x, 1.0f, cp);
    return c18 ? nr.encode<false, true>(x) : nr.encode<false, false>(x);
}
static Stereo dec(NoiseRed& nr, Stereo y, bool hq, bool c18, Stereo* cp)
{
    if (hq) return c18 ? nr.decode<true, true>(y, cp) : nr.decode<true, false>(y, cp);
    return c18 ? nr.decode<false, true>(y) : nr.decode<false, false>(y);
}

// The encoder alone at `sr` (HQ: encode<true>, as inside the doubled section): a sine at `hz`, `levelDb`
// re NR reference level, settled for a second; its distortion over the next second.
static double encThd(float sr, bool hq, bool c18, double hz, double levelDb)
{
    NoiseRed nr; nr.prepare(sr, false);
    Stereo cp = {};
    const double amp = kRefLevel * std::pow(10.0, levelDb / 20.0);
    std::vector<float> out;
    for (long i = 0; i < 2 * (long)sr; ++i)
    {
        const Stereo x = splat((float)(amp * std::sin(kTwoPiD * hz * (double)i / sr)));
        out.push_back(enc(nr, x, hq, c18, &cp)[0]);
    }
    return thdOf(out, (size_t)sr, hz, sr);
}

// A tone at -60 dB re reference level for a second, then at reference level: the largest sample in
// the first 20 ms after the jump against the largest once it has settled (0.8 to 1 s after), in dB.
static double overshoot(float sr, bool hq, bool c18, double hz)
{
    NoiseRed nr; nr.prepare(sr, false);
    Stereo cp = {};
    const long n0 = (long)sr;
    double pk = 0, pks = 0;
    for (long i = 0; i < 2 * n0; ++i)
    {
        const double a = kRefLevel * (i < n0 ? 0.001 : 1.0);
        const Stereo x = splat((float)(a * std::sin(kTwoPiD * hz * (double)i / sr)));
        const float y = enc(nr, x, hq, c18, &cp)[0];
        if (i >= n0 && i < n0 + (long)(0.02 * sr)) pk = std::fmax(pk, std::fabs(y));
        if (i >= n0 + (long)(0.8 * sr)) pks = std::fmax(pks, std::fabs(y));
    }
    return dB(pk / pks);
}

// The record side's boost of a sine (as nr_test's gainOf), with a given clip level.
static double gainOf(float sr, bool c18, float hz, float levelDb)
{
    NoiseRed nr; nr.prepare(sr, false);
    const int n = (int)sr;
    const float amp = kRefLevel * std::pow(10.0f, levelDb / 20.0f);
    double si = 0, so = 0;
    for (int i = 0; i < n; ++i)
    {
        const float x = amp * (float)std::sin(kTwoPiD * hz * i / sr);
        const float y = enc(nr, splat(x), false, c18, nullptr)[0];
        if (i >= (int)(0.7f * sr)) { si += (double)x * x; so += (double)y * y; }
    }
    return 10.0 * std::log10(so / si);
}

int main(int argc, char** argv)
{
    if (argc >= 3 && std::strcmp(argv[1], "save") == 0)
    {
        Fx f(0);
        f.set(kNr, 3.0f); f.set(kDrive, 37.0f);
        if (!spit(argv[2], f.chunk())) { std::printf("cannot write %s\nFAIL\n", argv[2]); return 1; }
        std::printf("wrote a 1.8 state (NR On, Drive 37, %s)\n", extraOf(f.chunk()).c_str());
        return 0;
    }
    if (argc < 3) { std::printf("usage: nrclip_test <1.7.0 state> <1.7.0 renders>  |  nrclip_test save <file>\nFAIL\n"); return 1; }
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("   %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };
    const std::vector<uint8_t> s170 = slurp(argv[1]), r170 = slurp(argv[2]);

    std::printf("1. The clip level in the state\n");
    {
        Fx fresh(0);
        const std::vector<uint8_t> sNew = fresh.chunk();
        std::printf("   a new instance: %s level, saves extra \"%s\"\n", newLevel(fresh) ? "1.8's" : "the old", extraOf(sNew).c_str());
        check(newLevel(fresh) && extraOf(sNew) == "VVX1:1", "a new instance has 1.8's level and saves it");
        std::printf("   1.7.0's state: %zu bytes, extra \"%s\"\n", s170.size(), extraOf(s170).c_str());
        Fx old(0, &s170);
        const std::vector<uint8_t> sOld = old.chunk();
        check(!s170.empty() && extraOf(s170) == "" && !newLevel(old) && old.get(kNr) == 3.0f, "a state saved by 1.7.0 loads with the old level (NR On as saved)");
        check(extraOf(sOld) == "VVX1:0", "saved again, it says the old level");
        Fx again(1, &sOld);
        check(!newLevel(again), "and loads with the old level again (here with HQ on)");
        Fx back(0, &sNew);
        check(newLevel(back), "a state saved with 1.8's level loads with it");
        check(old.load(sNew) == 1 && newLevel(old), "loaded over an instance on the old level, too");
        check(old.load(s170) == 1 && !newLevel(old), "and a 1.7.0 state over one on 1.8's level gives the old one");
        // a program leaves it as it is
        old.e->dispatcher(old.e, effSetProgram, 0, 5, nullptr, 0.0f);
        fresh.e->dispatcher(fresh.e, effSetProgram, 0, 5, nullptr, 0.0f);
        Fx viaProg(0);
        for (int prog = 0; prog < viaProg.p()->numPrograms(); ++prog) viaProg.e->dispatcher(viaProg.e, effSetProgram, 0, prog, nullptr, 0.0f);
        check(!newLevel(old) && newLevel(fresh) && newLevel(viaProg), "choosing a program leaves the level as it is (old stays old, new stays new)");
        // a state that cannot be loaded leaves it as it was
        std::vector<uint8_t> cut = sNew; cut.resize(cut.size() / 2);
        std::vector<uint8_t> bad = sOld; bad[0] = 'X';
        check(old.load(cut) == 0 && !newLevel(old) && old.load(bad) == 0 && !newLevel(old), "a state cut short or damaged leaves the old level as it was");
        check(fresh.load(cut) == 0 && newLevel(fresh), "and 1.8's level as it was");
        bool tail = true;
        for (size_t k : { 1, 3, 5, 8, 11 })
        {
            std::vector<uint8_t> t = sNew; t.resize(t.size() - k);
            tail = tail && old.load(t) == 0 && !newLevel(old);
        }
        check(tail, "a 1.8 state cut short in its last 12 bytes (the part after the parameters) is turned down too");
        // the extra part with another tag or a later version
        std::vector<uint8_t> later = sNew; later[later.size() - 5] = 2;   // version 2, flag bit 0 still set
        std::vector<uint8_t> other = sNew; other[other.size() - 8] = 'Q';
        check(old.load(later) == 1 && newLevel(old) && old.load(other) == 1 && !newLevel(old), "a later version's flag is read; an extra part that is not ours means the old level");
    }
    {
        // the sound: the old level with HQ off is 1.7.0's to the bit
        std::vector<uint64_t> h170(r170.size() / 8);
        if (!h170.empty()) std::memcpy(h170.data(), r170.data(), h170.size() * 8);
        const std::vector<uint64_t> hOld = renders([&] { return new Fx(0, &s170); });
        const std::vector<uint64_t> hNew = renders([] { return new Fx(0); });
        int same = 0, differ = 0;
        for (size_t i = 0; i < hOld.size(); ++i) { same += i < h170.size() && hOld[i] == h170[i]; differ += hNew[i] != hOld[i]; }
        std::printf("   NR Rec, Play, On, HQ off, a loud tone and bursts: %d of 3 renders 1.7.0's to the bit on the old level; %d of 3 differ on 1.8's\n", same, differ);
        check(h170.size() == 3 && same == 3, "with the old level and HQ off the sound is 1.7.0's to the bit");
        check(differ == 3, "1.8's level changes the sound of a loud tone with NR in use");
        // NR off: the level changes nothing
        bool off = true;
        for (int hq : { 0, 1 })
        {
            Fx a(hq), b(hq, &s170);
            b.set(kNr, 0.0f);
            for (Fx* f : { &a, &b }) { f->set(kDrive, 60.0f); f->set(kSatOn, 1.0f); f->set(kComp, 50.0f); }
            std::vector<float> oa, ob;
            a.play(1.5, loud, &oa); b.play(1.5, loud, &ob);
            off = off && oa == ob;
        }
        check(off, "with NR off the two levels sound the same to the bit (HQ off and on)");
    }

    std::printf("2. Distortion of a steady tone through the record side (THD + noise)\n");
    {
        struct { double hz, level, limit; } pts[] = { { 1000, 0, 0.0015 }, { 10000, 10, 0.003 } };
        for (const auto& pt : pts)
        {
            double worst = 0;
            for (int hq : { 0, 1 })
            {
                const float sr = hq ? 88200.0f : 44100.0f;
                const double eNew = encThd(sr, hq != 0, true, pt.hz, pt.level), eOld = encThd(sr, hq != 0, false, pt.hz, pt.level);
                // and through the plugin, on Init with NR Rec (and Off, for what the rest adds)
                double plug[3];
                for (int k = 0; k < 3; ++k)
                {
                    Fx f(hq, k == 1 ? &s170 : nullptr);
                    f.set(kNr, k == 2 ? 0.0f : 1.0f);
                    const double amp = kRefLevel * std::pow(10.0, pt.level / 20.0);
                    std::vector<float> out;
                    f.play(2.0, [&](long i) { return amp * std::sin(kTwoPiD * pt.hz * (double)i / 44100.0); }, &out);
                    plug[k] = thdOf(out, 44100, pt.hz, 44100.0);
                }
                std::printf("   %5.0f Hz %+3.0f dB re ref, HQ %s: encoder %.3f %% (old level %.3f %%); plugin NR Rec %.3f %% (old level %.3f %%), NR Off %.4f %%\n",
                            pt.hz, pt.level, hq ? "on " : "off", 100 * eNew, 100 * eOld, 100 * plug[0], 100 * plug[1], 100 * plug[2]);
                worst = std::fmax(worst, std::fmax(eNew, plug[0]));
            }
            char what[128];
            std::snprintf(what, sizeof(what), "%.0f Hz at %+.0f dB re reference level: at most %.2f %% with 1.8's level, HQ off and on", pt.hz, pt.level, 100 * pt.limit);
            check(worst <= pt.limit, what);
        }
    }

    std::printf("3. Overshoot of a jump from -60 dB to reference level (first 20 ms against the settled peak)\n");
    {
        double worst = 0;
        for (double hz : { 1000.0, 5000.0 })
        {
            const double o0 = overshoot(44100.0f, false, true, hz), o1 = overshoot(88200.0f, true, true, hz);
            const double c0 = overshoot(44100.0f, false, false, hz), c1 = overshoot(88200.0f, true, false, hz);
            std::printf("   %4.0f Hz: %+.2f dB HQ off, %+.2f dB HQ on (old level %+.2f and %+.2f dB)\n", hz, o0, o1, c0, c1);
            worst = std::fmax(worst, std::fmax(o0, o1));
        }
        check(worst < 3.5, "the overshoot stays near 3 dB (without the clipper it is 5 to 9 dB)");
    }

    std::printf("4. NR On gives back the input (the difference against the tone, over the second second)\n");
    {
        // The two sides alone, nothing between: HQ off both at 44.1 kHz; HQ on the encoder at twice
        // the rate (encode<true>, on the tone made at that rate, every other sample kept: a steady
        // tone that is not clipped needs no filter for that) and the decoder at 44.1 kHz (decode<true>).
        auto direct = [](bool hq, bool c18, double hz, double dbfs)
        {
            NoiseRed en, de; en.prepare(hq ? 88200.0f : 44100.0f, false); de.prepare(44100.0f, true);
            Stereo cpE = {}, cpD = {};
            const double amp = std::pow(10.0, dbfs / 20.0);
            double d = 0, r = 0;
            for (long i = 0; i < 88200; ++i)
            {
                const double x = amp * std::sin(kTwoPiD * hz * (double)i / 44100.0);
                Stereo y;
                if (hq)
                {
                    enc(en, splat((float)(amp * std::sin(kTwoPiD * hz * (i - 0.5) / 44100.0))), true, c18, &cpE);
                    y = enc(en, splat((float)x), true, c18, &cpE);
                    y = dec(de, y, true, c18, &cpD);
                }
                else y = dec(de, enc(en, splat((float)x), false, c18, nullptr), false, c18, nullptr);
                if (i >= 44100) { d += (y[0] - x) * (y[0] - x); r += x * x; }
            }
            return dB(std::sqrt(d / r));
        };
        // Through the plugin on Init, NR On against NR Off: its 4-pole Hi Cut (at 18 kHz) and Lo Cut
        // (at 20 Hz) sit between the two sides, so the decoder never gets back exactly what the
        // encoder made.
        auto plugin = [&](int hq, bool old, double hz, double dbfs)
        {
            const std::vector<uint8_t>* st = old ? &s170 : nullptr;
            Fx on(hq, st), off(hq, st);
            on.set(kNr, 3.0f); off.set(kNr, 0.0f);
            const double amp = std::pow(10.0, dbfs / 20.0);
            auto sig = [&](long i) { return amp * std::sin(kTwoPiD * hz * (double)i / 44100.0); };
            std::vector<float> a, b;
            on.play(2.0, sig, &a); off.play(2.0, sig, &b);
            double d = 0, r = 0;
            for (size_t i = 44100; i < a.size(); ++i) { d += (double)(a[i] - b[i]) * (a[i] - b[i]); r += (double)b[i] * b[i]; }
            return dB(std::sqrt(d / r));
        };
        double worst[2] = { -999, -999 };
        bool better = true;
        for (int hq : { 0, 1 })
            for (double hz : { 313.0, 997.0 })
                for (double dbfs : { -3.0, -12.0 })
                {
                    const double dOld = direct(hq != 0, false, hz, dbfs), dNew = direct(hq != 0, true, hz, dbfs);
                    const double pOld = plugin(hq, true, hz, dbfs), pNew = plugin(hq, false, hz, dbfs);
                    std::printf("   HQ %s %4.0f Hz %+4.0f dBFS: the two sides alone: old level %6.1f dB, 1.8's %6.1f dB;  the plugin: old %6.1f dB, 1.8's %6.1f dB\n",
                                hq ? "on " : "off", hz, dbfs, dOld, dNew, pOld, pNew);
                    worst[hq] = std::fmax(worst[hq], dNew);
                    if (hq) better = better && dNew <= dOld + 2.0;   // (HQ off both are at the float floor, see above)
                }
        check(worst[0] < -110.0, "HQ off, the two sides alone: within -110 dB with 1.8's level");
        check(worst[1] < -45.0, "HQ on, the two sides alone (encoder at twice the rate, decoder at the session's): within -45 dB with 1.8's level");
        // (HQ on, what is left is the two rates' networks not quite matching; where the old level
        // clipped both sides, the clamp hid a little of that, up to 1.4 dB of it at -12 dBFS.)
        check(better, "the two sides alone come out as close with 1.8's level as with the old one (HQ on within 2 dB)");
    }

    std::printf("5. The record-side figures with 1.8's level (as nr_test 1)\n");
    {
        struct { float hz, level, want; } pts[] = { {1400, -20, 4.4f}, {1400, -30, 7.5f}, {2000, -25, 7.0f}, {5000, -20, 3.2f},
                                                    {5000, -30, 8.2f}, {5000, -40, 10.3f}, {10000, -20, 2.6f}, {10000, -30, 6.5f} };
        bool ok = true; double moved = 0;
        for (float sr : {44100.0f, 48000.0f, 88200.0f, 96000.0f, 176400.0f, 192000.0f, 352800.0f, 384000.0f})
        {
            double worst = 0;
            for (auto& pt : pts)
            {
                const double g = gainOf(sr, true, pt.hz, pt.level);
                worst = std::fmax(worst, std::fabs(g - pt.want));
                moved = std::fmax(moved, std::fabs(g - gainOf(sr, false, pt.hz, pt.level)));
            }
            std::printf("   %6.0f Hz rate: worst error %.2f dB\n", sr, worst);
            ok = ok && worst < (sr == 44100.0f ? 0.5 : 0.7);
        }
        double most = 0;
        for (float hz : {1400.0f, 5000.0f, 10000.0f}) most = std::fmax(most, std::fabs(gainOf(44100.0f, true, hz, 0.0f)));
        std::printf("   the eight against the old level: at most %.4f dB apart; at reference level at most %.2f dB\n", moved, most);
        check(ok && moved < 0.01, "all eight published figures met as before (the clipper does not work there)");
        check(most < 0.5, "a signal at NR reference level passes unchanged");
    }

    std::printf(fails == 0 ? "PASS\n" : "FAIL\n");
    return fails == 0 ? 0 : 1;
}
#endif
