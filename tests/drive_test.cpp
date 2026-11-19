// VHS Vibes 1.8: Drive reads as the gain into the preamp clipper, in dB. Only the read-out changed:
// the parameter (id, name, range, default, mapping), what getParameter gives, the saved state and
// every program are as in 1.7.0, so old projects and automation load as before. The same program
// built against the 1.7.0 source (-DWRITE170, run.sh takes it from git) writes what 1.7.0 gives;
// built against this source it compares, then checks the read-out. Ends with PASS or FAIL.
//   drive_test <file>   (WRITE170: writes the file; otherwise reads it)
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
        return std::vector<uint8_t>(d, d + size);
    }
    std::string text(int i) { char b[64] = {0}; e->dispatcher(e, effGetParamDisplay, i, 0, b, 0.0f); return b; }
};

// Everything about Drive except its text, as bytes: the description, then for Drive set four ways
// (plain 0 / 25 / 50 / 100, a normalised write as automation makes it, every program) the plain
// value, getParameter and the whole saved state.
static void put(std::vector<uint8_t>& out, const void* v, size_t n) { const uint8_t* b = static_cast<const uint8_t*>(v); out.insert(out.end(), b, b + n); }
static void putF(std::vector<uint8_t>& out, float f) { put(out, &f, 4); }
static void putS(std::vector<uint8_t>& out, const std::string& s) { const uint32_t n = (uint32_t)s.size(); put(out, &n, 4); put(out, s.data(), n); }
static std::vector<uint8_t> record(std::vector<std::vector<uint8_t>>& chunks)
{
    std::vector<uint8_t> out;
    Fx f;
    const mpc::Param& d = f.p->param(kDrive);
    const uint32_t meta[] = { (uint32_t)kDrive, (uint32_t)f.p->numParams(), (uint32_t)f.p->numPrograms(), (uint32_t)d.kind, (uint32_t)d.logarithmic, (uint32_t)d.automatable };
    put(out, meta, sizeof(meta));
    putS(out, d.id); putS(out, d.name);
    putF(out, d.min); putF(out, d.max); putF(out, d.def); putF(out, d.skew);
    for (float n : { 0.0f, 0.1f, 0.25f, 0.5f, 0.77f, 1.0f }) putF(out, d.toPlain(n));
    putF(out, f.e->getParameter(f.e, kDrive));   // a new instance: the default
    auto state = [&] { putF(out, f.p->get(kDrive)); putF(out, f.e->getParameter(f.e, kDrive)); chunks.push_back(f.chunk()); putS(out, std::string(chunks.back().begin(), chunks.back().end())); };
    for (float v : { 0.0f, 25.0f, 50.0f, 100.0f }) { f.p->set(kDrive, v); state(); }
    f.e->setParameter(f.e, kDrive, 0.37f); state();
    for (int prog = 0; prog < f.p->numPrograms(); ++prog) { f.e->dispatcher(f.e, effSetProgram, 0, prog, nullptr, 0.0f); state(); }
    return out;
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: drive_test <file>\nFAIL\n"); return 1; }
    std::vector<std::vector<uint8_t>> chunks;
    const std::vector<uint8_t> now = record(chunks);
#ifdef WRITE170
    FILE* f = std::fopen(argv[1], "wb");
    if (f == nullptr || std::fwrite(now.data(), 1, now.size(), f) != now.size()) { std::printf("cannot write %s\nFAIL\n", argv[1]); return 1; }
    std::fclose(f);
    std::printf("wrote %zu bytes (1.7.0: Drive and its saved states)\n", now.size());
    return 0;
#else
    bool ok = true;
    auto check = [&](bool c, const char* what) { std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what); ok = ok && c; };
    std::vector<uint8_t> old;
    if (FILE* f = std::fopen(argv[1], "rb"))
    {
        uint8_t b[4096]; size_t n;
        while ((n = std::fread(b, 1, sizeof(b), f)) > 0) old.insert(old.end(), b, b + n);
        std::fclose(f);
    }
    std::printf("1.7.0 wrote %zu bytes, this source %zu (%zu saved states)\n", old.size(), now.size(), chunks.size());
    check(!old.empty() && old == now, "Drive's id, name, range, default and mapping, getParameter, every program and the saved states are as in 1.7.0");

    // A state saved by 1.7.0 loads into this one as it was: here the 1.7.0 state with Drive at 50 %
    // (the third one recorded), its bytes taken from the 1.7.0 file. The states sit in the file in
    // order; a state's bytes are found by its length and content.
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

    // The read-out: 20 log10(1 + 15 a^2), one decimal, as the other dB read-outs.
    {
        Fx f;
        std::string r;
        for (float v : { 0.0f, 25.0f, 50.0f, 100.0f }) { f.p->set(kDrive, v); r += (r.empty() ? "" : " | ") + f.text(kDrive); }
        char name[64] = {0};
        f.e->dispatcher(f.e, effGetParamName, kDrive, 0, name, 0.0f);
        char label[64] = {0};
        f.e->dispatcher(f.e, effGetParamLabel, kDrive, 0, label, 0.0f);
        std::printf("  %s at 0, 25, 50, 100 %% reads \"%s\" (label \"%s\")\n", name, r.c_str(), label);
        check(r == "0.0 dB | +5.7 dB | +13.5 dB | +24.1 dB" && std::string(name) == "Drive" && label[0] == 0, "Drive reads the gain into the clipper in dB");
        f.p->set(kDrive, 1.0f); const std::string low = f.text(kDrive);
        f.p->set(kDrive, 17.0f); const std::string blend = f.text(kDrive);
        std::printf("  at 1 %% \"%s\", at 17 %% \"%s\"\n", low.c_str(), blend.c_str());
        check(low == "0.0 dB" && blend == "+3.1 dB", "a hair above 0 still reads 0.0 dB; the clean blend ends near +3.1 dB");
    }
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
#endif
}
