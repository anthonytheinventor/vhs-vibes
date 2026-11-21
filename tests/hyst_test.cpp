// VHS Vibes 1.8: Hysteresis (Sat Type VHS with the Saturator on): the tape hysteresis recorder of
// src/hysteresis.h (one table per group: the engine that was called Lite), in place of TapeSat's curve.
// Checked here:
//   1. the parameter (appended last, Off / On, Off in a new instance, in every program and in older states;
//      saved and loaded with a state; a state of the three-position switch loads Lite and Full as On)
//   2. the recorder against the prototype it was built from (tools/hysteresis/rt2.h, the same tables,
//      the same correction): a grid of tones, levels, bias 25 / 50 / 100 / 150 %, SP and EP
//   3. the recorder's own figures at correct bias, SP (441 Hz and 1 kHz, through the record and playback
//      emphasis: the values the three-position version's Lite gave), and the whole plugin at reference
//      level: about 3 % third harmonic, with HQ off and on alike
//   4. silence stays silent, a hot signal stays finite and comes back to silence
//   5. the latency it reports and the delay it has; the dry copy at Mix 50 % stays lined up
//   6. a Bias sweep, switching Off / On and SP / EP while a tone plays: no click; a new speed glides
// Ends with PASS or FAIL.
//   hyst_test [older states ...]   (run.sh passes the states 1.6.0, 1.4.0 and 1.7.0 saved)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#define private public   // the latency the plugin wants to report, and the recorder itself
#include "../src/VHSVibes.cpp"
#undef private
#include "../tools/hysteresis/rt2.h"

static VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float) { return opcode == audioMasterVersion ? 2400 : 0; }
static const double kPiD = 3.141592653589793, kTwoPiD = 2.0 * kPiD;
static double dB(double x) { return 20.0 * std::log10(x > 1e-30 ? x : 1e-30); }
static const int kInit = 20;
static const float kRecZero = 100.0f * 10.0f / 18.0f;   // Lo Sat (Rec Level) at 0 dB

struct Fx
{
    AEffect* e; mpc::Plugin* p; float sr; long pos = 0;
    // Init, noise off, the Saturator on in VHS at Rec Level 0 dB, SP, bias right; then the changes
    Fx(int hq, int mode, const std::vector<std::pair<int, float>>& ch = {}, float rate = 44100.0f) : sr(rate)
    {
        e = VSTPluginMain(hostCb); p = static_cast<mpc::Plugin*>(e->object);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, rate);
        e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0.0f);
        p->set(kHq, (float)hq);
        e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
        e->dispatcher(e, effSetProgram, 0, kInit, nullptr, 0.0f);
        p->set(kNoiseOn, 0); p->set(kSatOn, 1); p->set(kSatType, 1); p->set(kLoSat, kRecZero); p->set(kHiSat, 0); p->set(kSplit, 2500);
        p->set(kHysteresis, (float)mode);
        for (const auto& c : ch) p->set(c.first, c.second);
    }
    Fx() : Fx(0, 0) {}
    ~Fx() { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0.0f); e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f); }
    VHSVibes* vv() { return static_cast<VHSVibes*>(p); }
    float get(int i) { return p->get(i); }
    // `sec` seconds of sig(sample index) in both channels, 128-frame calls; `each` runs before each call
    std::vector<float> play(double sec, const std::function<double(long)>& sig, const std::function<void(long)>& each = nullptr, std::vector<float>* right = nullptr)
    {
        static float L[128], R[128]; float* io[2] = { L, R };
        std::vector<float> out;
        const long n = (long)(sec * sr);
        for (long k = 0; k < n; k += 128)
        {
            if (each) each(pos);
            const int nb = (int)std::min<long>(128, n - k);
            for (int i = 0; i < nb; ++i) L[i] = R[i] = (float)sig(pos + i);
            e->processReplacing(e, io, io, nb);
            pos += nb;
            out.insert(out.end(), L, L + nb);
            if (right) right->insert(right->end(), R, R + nb);
        }
        return out;
    }
    std::vector<uint8_t> chunk()
    {
        void* data = nullptr;
        const VstIntPtr size = e->dispatcher(e, effGetChunk, 0, 0, &data, 0.0f);
        const uint8_t* d = static_cast<const uint8_t*>(data);
        return std::vector<uint8_t>(d, d + size);
    }
};

// a sine's complex amplitude at hz over the last n samples (Hann window)
static std::complex<double> toneAt(const std::vector<float>& v, double sr, double hz, size_t n = 32768)
{
    std::complex<double> s = 0; double ws = 0; const size_t off = v.size() - n;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(kTwoPiD * (double)i / (double)n);
        s += (double)v[off + i] * w * std::polar(1.0, -kTwoPiD * hz * (double)(off + i) / sr);
        ws += w;
    }
    return s * (2.0 / ws);
}
static void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -kTwoPiD / (double)len; const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w(1); for (size_t k = 0; k < len / 2; ++k) { auto u = a[i + k], t = a[i + k + len / 2] * w; a[i + k] = u + t; a[i + k + len / 2] = u - t; w *= wl; } }
    }
}
// the highest spectral line (dBFS, Hann, n samples from `from`) further than 60 Hz from every harmonic of `grid`
static double offLine(const std::vector<float>& v, size_t from, double sr, double grid, int n = 16384)
{
    std::vector<std::complex<double>> a((size_t)n); double ws = 0;
    for (int i = 0; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos(kTwoPiD * i / n); a[(size_t)i] = v[from + (size_t)i] * w; ws += w; }
    fft(a);
    double mx = 0; const double bin = sr / n;
    for (int i = (int)(40 / bin); i < n / 2; ++i)
    {
        const double f = i * bin;
        if (std::fabs(f - std::round(f / grid) * grid) < 60.0) continue;
        mx = std::fmax(mx, std::abs(a[(size_t)i]) * 2.0 / ws);
    }
    return dB(mx);
}
// the largest sample-to-sample step against the largest a sine of `hz` at the local peak can make
// (one cycle each side): 1.0 for a clean tone; a click stands out above it
static double stepRatio(const std::vector<float>& v, double sr, double hz, size_t from = 0)
{
    const double sineStep = 2.0 * std::sin(kPiD * hz / sr);
    const size_t half = (size_t)(sr / hz);
    double worst = 0;
    for (size_t i = std::max<size_t>(from, 1); i < v.size(); ++i)
    {
        double pk = 0;
        for (size_t j = i < half ? 0 : i - half; j < v.size() && j <= i + half; ++j) pk = std::fmax(pk, std::fabs((double)v[j]));
        if (pk > 1e-4) worst = std::fmax(worst, std::fabs((double)v[i] - v[i - 1]) / (sineStep * pk));
    }
    return worst;
}
// The plugin's record emphasis (TapeSat::set without the bias term) and its exact inverse, at 44.1 kHz
struct Emph
{
    float b0, b1, a1, invB0, uP = 0, wP = 0, vP = 0, yP = 0;
    explicit Emph(int ep)
    {
        const float sr = 44100.0f, fz = 1326.3f * (ep ? 1.0f / 3.0f : 1.0f);
        const float kz = std::tan(kPi * fz / sr), kp = std::tan(kPi * minf(10.0f * fz, 0.45f * sr) / sr);
        const float norm = 1.0f / (1.0f + 1.0f / kp);
        b0 = (1.0f + 1.0f / kz) * norm; b1 = (1.0f - 1.0f / kz) * norm; a1 = (1.0f - 1.0f / kp) * norm; invB0 = 1.0f / b0;
    }
    float pre(float u) { const float w = b0 * u + b1 * uP - a1 * wP; uP = u; wP = w; return w; }
    float de(float v) { const float y = (v + a1 * vP - b1 * yP) * invB0; vP = v; yP = y; return y; }
    static double mag(double f, int ep)   // the analog emphasis' lift at f
    {
        const double fz = 1326.3 * (ep ? 1.0 / 3.0 : 1.0);
        return std::abs(std::complex<double>(1.0, f / fz) / std::complex<double>(1.0, f / (10.0 * fz))) / 1.0;
    }
};

// The prototype engine (rt2.h), set up from the plugin's tables as the prototype's tools set it up:
// planes and their integrals (per plane, in double) blended, group delays blended, taps in double.
static void protoCfg(rt2::Cfg& c, int ep, double bp)
{
    using namespace hysttab;
    c.os = 2; c.J = kJ; c.nh = kNh; c.nu = 1; c.lat = kLat;
    c.h0 = kH0; c.invh = kInvStep; c.tauS = (float)(88200.0 / kSpeed[ep]);
    c.cubicA = true; c.adaa = true; c.useIir = false; c.rational = false;
    int b = 0; while (b + 2 < kNb && kBiasPct[b + 1] <= bp) ++b;
    const double f = std::clamp((bp - kBiasPct[b]) / (kBiasPct[b + 1] - kBiasPct[b]), 0.0, 1.0);
    const float (*P)[kJ][kNh] = ep ? kTEp : kTSp;
    const float (*D)[kJ] = ep ? kD0Ep : kD0Sp;
    const float (*F)[kFir] = ep ? kFirFastEp : kFirFastSp;
    const int off = 0, n = c.nu * kNh, mid = kNh / 2;
    auto integral = [&](const float* T) {
        std::vector<double> g((size_t)n, 0.0);
        for (int u = 0; u < c.nu; ++u)
        {
            const float* t = T + u * kNh; double* gi = g.data() + u * kNh; double acc = 0;
            for (int i = mid + 1; i < kNh; ++i) { acc += 0.5 * 0.05 * (t[i] + t[i - 1]); gi[i] = acc; }
            acc = 0;
            for (int i = mid - 1; i >= 0; --i) { acc -= 0.5 * 0.05 * (t[i] + t[i + 1]); gi[i] = acc; }
        }
        return g;
    };
    for (int j = 0; j < kJ; ++j)
    {
        rt2::Group& G = c.g[j];
        const float* A = &P[b][j][off]; const float* B = &P[b + 1][j][off];
        const std::vector<double> ga = integral(A), gb = integral(B);
        G.T.resize((size_t)n); G.Gi.resize((size_t)n);
        for (int i = 0; i < n; ++i) { G.T[(size_t)i] = (float)((1 - f) * A[i] + f * B[i]); G.Gi[(size_t)i] = (float)((1 - f) * ga[(size_t)i] + f * gb[(size_t)i]); }
        const double d = (1 - f) * D[b][j] + f * D[b + 1][j];
        const double p = d * 88200.0, fl = std::floor(p), tt = p - fl;
        G.io = (int)fl;
        const double cv[4] = {-tt * (tt - 1) * (tt - 2) / 6, (tt + 1) * (tt - 1) * (tt - 2) / 2, -(tt + 1) * tt * (tt - 2) / 2, (tt + 1) * tt * (tt - 1) / 6};
        const double cs[4] = {-(3 * tt * tt - 6 * tt + 2) / 6, (3 * tt * tt - 4 * tt - 1) / 2, -(3 * tt * tt - 2 * tt - 2) / 2, (3 * tt * tt - 1) / 6};
        for (int k = 0; k < 4; ++k) { G.cv[k] = (float)cv[k]; G.cs[k] = (float)cs[k]; }
        for (int k = 0; k < kFir; ++k) G.fir[k] = F[j][k];
    }
}
static rt2::HfCorr protoCorr(int ep)
{
    rt2::HfCorr h;
    const float* p = hys::kCorr[ep];
    h.kSig = (float)(44100.0 / hysttab::kSpeed[ep]); h.e0 = p[1]; h.c = p[2];
    h.aAtt = (float)(1 - std::exp(-1.0 / (0.0005 * 44100))); h.aRel = (float)(1 - std::exp(-1.0 / (0.030 * 44100)));
    h.hpA = (float)std::exp(-kTwoPiD * p[0] / 44100.0);
    return h;
}
// the record field hs (44.1 kHz) through the prototype, and through the plugin's recorder: field out
static std::vector<float> runProto(int ep, double bp, const std::vector<float>& hs)
{
    static rt2::Cfg c; c = rt2::Cfg(); protoCfg(c, ep, bp);
    static rt2::Core core; core.init(c);
    rt2::HfCorr corr = protoCorr(ep);
    std::vector<float> L(hs), R(hs);
    for (size_t i = 0; i < hs.size(); i += 128)
    {
        const int n = (int)std::min<size_t>(128, hs.size() - i);
        core.process(L.data() + i, R.data() + i, n);
        for (int k = 0; k < n; ++k) { const rt2::v2 r = corr.run(rt2::v2{hs[i + k], hs[i + k]}, rt2::v2{L[i + k], R[i + k]}); L[i + k] = r[0]; }
    }
    return L;
}
static std::vector<float> runMine(int ep, double bp, const std::vector<float>& hs)
{
    static std::unique_ptr<hys::Stage> s(new hys::Stage());
    s->clear();
    s->eng.configure(ep, 44100.0f); s->eng.setBias((float)bp);
    s->run = true; s->warm = 0;
    std::vector<float> y(hs.size());
    for (size_t i = 0; i < hs.size(); ++i) y[i] = s->run1(splat(hs[i]))[0];
    return y;
}

int main(int argc, char** argv)
{
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { std::printf("   %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) ++fails; };
    char what[300];
    std::printf("1. The parameter\n");
    {
        Fx fx;
        const mpc::Param& p = fx.p->param(kHysteresis);
        check(kHysteresis == 65 && kNumParams == 66 && fx.e->numParams == 66 && p.id == "hysteresis" && p.name == "Hysteresis"
              && p.choices.size() == 2 && p.choices[0] == "Off" && p.choices[1] == "On (CPU+)" && p.def == 0.0f
              && isSetting(kHysteresis) && groupOf(kHysteresis) == gHyst && fx.e->dispatcher(fx.e, effCanBeAutomated, kHysteresis, 0, nullptr, 0.0f) == 1,
              "number 65, the last: hysteresis, \"Hysteresis\", Off / On (CPU+), Off by default, a setting, automatable");
        // the read-out the host shows (that it costs CPU) and the text it can set the switch by
        {
            char txt[64] = {};
            bool shown = true;
            const char* want[2] = { "Off", "On (CPU+)" };
            for (int v = 0; v < 2; ++v)
            {
                fx.p->set(kHysteresis, (float)v);
                std::memset(txt, 0, sizeof(txt));
                fx.e->dispatcher(fx.e, effGetParamDisplay, kHysteresis, 0, txt, 0.0f);
                shown = shown && std::strcmp(txt, want[v]) == 0;
            }
            char on[] = "On (CPU+)";
            const bool byText = fx.e->dispatcher(fx.e, effString2Parameter, kHysteresis, 0, on, 0.0f) == 1 && fx.get(kHysteresis) == 1.0f;
            fx.p->set(kHysteresis, 0.0f);
            check(shown && byText, "the read-outs: Off, On (CPU+) (and the host can set it by that text)");
        }
        AEffect* e = VSTPluginMain(hostCb);
        const float fresh = static_cast<mpc::Plugin*>(e->object)->get(kHysteresis);
        e->dispatcher(e, effClose, 0, 0, nullptr, 0.0f);
        check(fresh == 0.0f, "Off in a new instance");
        bool progs = fx.e->numPrograms == 46;
        for (int k = 0; k < fx.e->numPrograms; ++k)
        {
            fx.p->set(kHysteresis, 1.0f);
            fx.e->dispatcher(fx.e, effSetProgram, 0, k, nullptr, 0.0f);
            progs = progs && fx.get(kHysteresis) == 0.0f;
        }
        check(progs, "every one of the 46 programs sets it to Off");
        Fx a(0, 1);
        const std::vector<uint8_t> st = a.chunk();
        Fx b;
        const VstIntPtr r = b.e->dispatcher(b.e, effSetChunk, 0, (VstIntPtr)st.size(), const_cast<uint8_t*>(st.data()), 0.0f);
        check(r != 0 && b.get(kHysteresis) == 1.0f && b.get(kSatType) == 1.0f, "a state saved with On loads with On");
        // a state of the three-position switch (Off / Lite / Full: plain 0 / 1 / 2 at normalised 0 / 0.5 / 1),
        // as that version saved it: this state with its last entry's two values replaced
        {
            uint32_t n; std::memcpy(&n, st.data() + 8, 4);
            size_t pos = 12;
            for (uint32_t k = 0; k + 1 < n; ++k) pos += 1 + (size_t)st[pos] + 8;
            const bool last = st[pos] == 10 && std::memcmp(&st[pos + 1], "hysteresis", 10) == 0;
            bool three = last;
            const float plain[3] = { 0.0f, 1.0f, 2.0f }, norm[3] = { 0.0f, 0.5f, 1.0f }, want[3] = { 0.0f, 1.0f, 1.0f };
            for (int v = 0; v < 3; ++v)
            {
                std::vector<uint8_t> s = st;
                std::memcpy(&s[pos + 11], &plain[v], 4); std::memcpy(&s[pos + 15], &norm[v], 4);
                Fx c; c.p->set(kHysteresis, v ? 0.0f : 1.0f);
                const VstIntPtr rr = c.e->dispatcher(c.e, effSetChunk, 0, (VstIntPtr)s.size(), s.data(), 0.0f);
                three = three && rr != 0 && c.get(kHysteresis) == want[v] && c.get(kSatType) == 1.0f;
            }
            check(three, "a state of the three-position switch loads Off as Off, and Lite and Full as On");
        }
        int loaded = 0; bool old = true;
        for (int k = 1; k < argc; ++k)
        {
            FILE* f = std::fopen(argv[k], "rb");
            if (f == nullptr) { old = false; continue; }
            std::vector<uint8_t> o; int ch; while ((ch = std::fgetc(f)) != EOF) o.push_back((uint8_t)ch); std::fclose(f);
            Fx c; c.p->set(kHysteresis, 1.0f);
            const VstIntPtr rr = c.e->dispatcher(c.e, effSetChunk, 0, (VstIntPtr)o.size(), o.data(), 0.0f);
            old = old && rr != 0 && c.get(kHysteresis) == 0.0f;
            ++loaded;
        }
        // and a state of this version with Hysteresis taken out, as an older one has it
        {
            std::vector<uint8_t> s = st;
            uint32_t n; std::memcpy(&n, s.data() + 8, 4);
            size_t pos = 12;
            for (uint32_t k = 0; k + 1 < n; ++k) pos += 1 + (size_t)s[pos] + 8;
            const bool last = s[pos] == 10 && std::memcmp(&s[pos + 1], "hysteresis", 10) == 0;
            s.erase(s.begin() + (long)pos, s.begin() + (long)(pos + 1 + 10 + 8));
            --n; std::memcpy(s.data() + 8, &n, 4);
            Fx c; c.p->set(kHysteresis, 1.0f);
            const VstIntPtr rr = c.e->dispatcher(c.e, effSetChunk, 0, (VstIntPtr)s.size(), s.data(), 0.0f);
            old = old && last && rr != 0 && c.get(kHysteresis) == 0.0f && c.get(kSatType) == 1.0f;
        }
        std::printf("   %d older state(s) from earlier versions loaded over On\n", loaded);
        check(old && loaded == argc - 1, "older states (and this one without its last entry) load with Hysteresis Off");
    }

    std::printf("2. The recorder against the prototype (tools/hysteresis/rt2.h, the same tables and correction)\n");
    {
        // the record field of a tone at `lv` dB re the reference level, lifted by the emphasis as the prototype's tests do
        double worstLvl = 0, worstH3 = 0, worstSmp = 0; int n3 = 0;
        char at[96] = {};
        {
            for (int ep = 0; ep < 2; ++ep)
                for (double bp : { 25.0, 50.0, 100.0, 150.0 })
                    for (double f : { 100.0, 1000.0, 3000.0, 5000.0, 10000.0 })
                        for (double lv : { -20.0, -10.0, 0.0, 6.0 })
                        {
                            const double h = hysttab::kFieldPerUnit * kRefLevel * std::pow(10.0, lv / 20.0) * Emph::mag(f, ep);
                            std::vector<float> hs(16384 + 8192);
                            for (size_t i = 0; i < hs.size(); ++i) hs[i] = (float)(h * std::sin(kTwoPiD * f * (double)i / 44100.0));
                            const std::vector<float> a = runProto(ep, bp, hs), b = runMine(ep, bp, hs);
                            double pk = 0, df = 0;
                            for (size_t i = 0; i < a.size(); ++i) { pk = std::fmax(pk, std::fabs((double)a[i])); df = std::fmax(df, std::fabs((double)a[i] - b[i])); }
                            const double a1 = std::abs(toneAt(a, 44100.0, f, 16384)), b1 = std::abs(toneAt(b, 44100.0, f, 16384));
                            const double dl = std::fabs(dB(b1 / a1));
                            if (dl > worstLvl) { worstLvl = dl; std::snprintf(at, 96, "%s %.0f Hz %+.0f dB bias %.0f %%", ep ? "EP" : "SP", f, lv, bp); }
                            worstSmp = std::fmax(worstSmp, df / pk);
                            if (3 * f < 20000.0)
                            {
                                const double a3 = std::abs(toneAt(a, 44100.0, 3 * f, 16384)), b3 = std::abs(toneAt(b, 44100.0, 3 * f, 16384));
                                if (a3 / a1 > 1e-3) { worstH3 = std::fmax(worstH3, std::fabs(dB((b3 / b1) / (a3 / a1)))); ++n3; }
                            }
                        }
        }
        std::printf("   160 cases; level within %.4f dB (worst %s), third harmonic within %.4f dB (%d cases above -60 dBc), samples within %.2e of the peak\n",
                    worstLvl, at, worstH3, n3, worstSmp);
        check(worstLvl < 0.01 && worstH3 < 0.05 && worstSmp < 1e-4, "it agrees with the prototype: level within 0.01 dB, third harmonic within 0.05 dB, every sample within 1e-4 of the peak");
    }

    std::printf("3. The recorder's own figures and the plugin at reference level\n");
    {
        // at correct bias, SP, record emphasis -> recorder -> playback emphasis (gain out - in, H3, H5 at the
        // output): the figures the three-position version's Lite gave here (its Full had the specification's
        // SLOPE figures: 441 Hz 0 dB -0.69 dB, H3 2.98 %)
        struct T { double f, lv, gain, h3, h5; };
        const T spec[] = { {441, -20, 0.01, 0.0785, 0.00461}, {441, 0, -0.71, 2.93, 0.0166}, {441, 8, -4.12, 11.4, 2.08}, {1000, 0, -1.13, 2.95, 0.0953} };
        bool ok = true;
        for (const T& t : spec)
        {
            const double a = kRefLevel * std::pow(10.0, t.lv / 20.0);
            Emph em(0);
            std::vector<float> hs(32768 + 8192);
            for (size_t i = 0; i < hs.size(); ++i) hs[i] = (float)(hysttab::kFieldPerUnit * em.pre((float)(a * std::sin(kTwoPiD * t.f * (double)i / 44100.0))));
            std::vector<float> y = runMine(0, 100.0, hs);
            for (float& v : y) v = em.de(v * hys::kInvField);
            const double y1 = std::abs(toneAt(y, 44100.0, t.f)), y3 = std::abs(toneAt(y, 44100.0, 3 * t.f)), y5 = std::abs(toneAt(y, 44100.0, 5 * t.f));
            const double g = dB(y1 / a), h3 = 100 * y3 / y1, h5 = 100 * y5 / y1;
            std::printf("   %4.0f Hz %+3.0f dB: gain %+.2f dB (was %+.2f), H3 %.3f %% (%.2f), H5 %.3f %% (%.3f)\n", t.f, t.lv, g, t.gain, h3, t.h3, h5, t.h5);
            ok = ok && std::fabs(g - t.gain) < 0.1 && std::fabs(h3 / t.h3 - 1) < 0.06 && (t.h5 < 0.01 || std::fabs(h5 / t.h5 - 1) < 0.2);
        }
        check(ok, "at correct bias, SP: gain within 0.1 dB, H3 within 6 % and H5 within 20 % of the figures the three-position version's Lite gave");
        // the whole plugin, Init with the tape at Rec Level 0 dB, SP, bias right, a reference-level tone
        double h3s[2][2][2] = {}, gs[2][2][2] = {};
        for (int hq = 0; hq < 2; ++hq)
            for (int mode = 0; mode < 2; ++mode)
                for (int k = 0; k < 2; ++k)
                {
                    const double f = k ? 1000.0 : 441.0;
                    Fx fx(hq, mode);
                    const std::vector<float> y = fx.play(1.5, [&](long i) { return kRefLevel * std::sin(kTwoPiD * f * (double)i / 44100.0); });
                    const double y1 = std::abs(toneAt(y, 44100.0, f)), y3 = std::abs(toneAt(y, 44100.0, 3 * f));
                    gs[hq][mode][k] = dB(y1 / kRefLevel); h3s[hq][mode][k] = 100 * y3 / y1;
                }
        for (int hq = 0; hq < 2; ++hq)
            std::printf("   HQ %s, 441 Hz | 1 kHz at reference level: Off %+.2f dB %.2f %% | %+.2f dB %.2f %%; On %+.2f dB %.2f %% | %+.2f dB %.2f %%\n",
                        hq ? "on " : "off", gs[hq][0][0], h3s[hq][0][0], gs[hq][0][1], h3s[hq][0][1], gs[hq][1][0], h3s[hq][1][0], gs[hq][1][1], h3s[hq][1][1]);
        bool three = true, same = true, near = true;
        for (int hq = 0; hq < 2; ++hq)
            for (int mode = 1; mode < 2; ++mode)
                for (int k = 0; k < 2; ++k)
                {
                    three = three && h3s[hq][mode][k] > 2.6 && h3s[hq][mode][k] < 3.4;
                    same = same && std::fabs(gs[1][mode][k] - gs[0][mode][k]) < 0.05 && std::fabs(h3s[1][mode][k] / h3s[0][mode][k] - 1) < 0.03;
                    near = near && std::fabs(gs[hq][mode][k] - gs[hq][0][k]) < 0.6;
                }
        check(three, "On: about 3 % third harmonic at reference level (2.6 to 3.4 %), at 441 Hz and 1 kHz");
        check(same, "HQ on leaves them as they are (level within 0.05 dB, H3 within 3 %)");
        check(near, "at bias right they come out within 0.6 dB of TapeSat's curve (Hysteresis Off)");
    }

    std::printf("4. Silence, and a hot signal\n");
    {
        bool zero = true, finite = true, back = true;
        for (int hq = 0; hq < 2; ++hq)
            for (int mode = 1; mode < 2; ++mode)
                for (float bias : { 200.0f, 2500.0f, 8000.0f })
                {
                    Fx fx(hq, mode, { {kSplit, bias}, {kHiSat, 100.0f} });
                    const std::vector<float> y = fx.play(1.0, [](long) { return 0.0; });
                    for (float v : y) zero = zero && v == 0.0f;
                    Fx hot(hq, mode, { {kSplit, bias}, {kLoSat, 100.0f}, {kDrive, 100.0f} });
                    std::vector<float> z = hot.play(1.0, [](long i) { return (i / 50) % 2 ? 1.0 : -1.0; });
                    const std::vector<float> z2 = hot.play(1.0, [](long i) { return 2.0 * std::sin(kTwoPiD * 15000.0 * (double)i / 44100.0); });
                    const std::vector<float> q = hot.play(3.0, [](long) { return 0.0; });
                    for (float v : z) finite = finite && std::isfinite(v);
                    for (float v : z2) finite = finite && std::isfinite(v);
                    for (float v : q) finite = finite && std::isfinite(v);
                    for (size_t i = q.size() - 4410; i < q.size(); ++i) back = back && std::fabs(q[i]) < 1e-9f;
                }
        check(zero, "silence in, exactly silence out (HQ off and on, bias low, right and high, SP and EP)");
        check(finite, "a full-scale square wave and a 15 kHz tone 6 dB over full scale, Rec Level +8 dB, Drive 100 %: never a NaN or an infinity");
        check(back, "and two seconds after the sound stops it is silence again (under -180 dBFS)");
    }

    std::printf("5. Latency and the dry copy\n");
    {
        bool delayOk = true, latOk = true;
        for (int hq = 0; hq < 2; ++hq)
        {
            std::complex<double> t0; int lat0 = 0;
            for (int mode = 0; mode < 2; ++mode)
            {
                Fx fx(hq, mode, { {kLoSat, 0.0f} });
                const std::vector<float> y = fx.play(1.5, [](long i) { return 0.01 * std::sin(kTwoPiD * 2000.0 * (double)i / 44100.0); });
                const std::complex<double> t = toneAt(y, 44100.0, 2000.0);
                const int lat = fx.vv()->wantLatency_.load();
                if (mode == 0) { t0 = t; lat0 = lat; continue; }
                double ph = -std::arg(t / t0); while (ph < 0) ph += kTwoPiD;
                double d = ph / (kTwoPiD * 2000.0) * 44100.0;
                while (d < 30) d += 44100.0 / 2000.0;
                const double want = kHyLate[hq];
                std::printf("   HQ %s: %.3f samples later than Off at 2 kHz (by design %.1f); reported %d against %d\n", hq ? "on " : "off", d, want, lat, lat0);
                delayOk = delayOk && std::fabs(d - want) < 0.1;
                latOk = latOk && std::abs((lat - lat0) - (int)std::lround(want)) <= 1;
            }
        }
        check(delayOk, "the recorder is as late as kHyLate says (41.5 samples, 41.0 with HQ on)");
        check(latOk, "and the latency the plugin reports grows by that much");
        // other rates: the element lag grows with the rate (no read past the newest sample), the lateness with it
        bool rates = true;
        for (float sr : { 48000.0f, 96000.0f })
            for (int hq = 0; hq < 2; ++hq)
            {
                std::complex<double> t[2]; int lat[2] = {0, 0}; float late = 0;
                for (int mode = 0; mode < 2; ++mode)
                {
                    Fx fx(hq, mode, { {kLoSat, 0.0f} }, sr);
                    const std::vector<float> y = fx.play(1.5, [&](long i) { return 0.01 * std::sin(kTwoPiD * 2000.0 * (double)i / sr); });
                    t[mode] = toneAt(y, sr, 2000.0);
                    lat[mode] = fx.vv()->wantLatency_.load();
                    if (mode) late = fx.vv()->hy_->late;
                }
                double ph = -std::arg(t[1] / t[0]); while (ph < 0) ph += kTwoPiD;
                double d = ph / (kTwoPiD * 2000.0) * sr;
                while (d < late - 0.5 * sr / 2000.0) d += sr / 2000.0;
                const double g = dB(std::abs(t[1] / t[0]));
                std::printf("   %.0f Hz, HQ %s: %.3f samples later than Off (lateness %.1f), %+.2f dB at 2 kHz; reported %d against %d\n",
                            sr, hq ? "on " : "off", d, late, g, lat[1], lat[0]);
                rates = rates && std::fabs(d - late) < 1.0 && std::abs((lat[1] - lat[0]) - (int)std::lround(late)) <= 1 && std::fabs(g) < 1.0;
            }
        check(rates, "at 48 and 96 kHz the recorder is as late as it says, within a sample, and passes a quiet 2 kHz tone within 1 dB");
        // Mix 50 %: a quiet tone (nothing clips) at Mix 50 % against Mix 100 %. The tape path's filters
        // (band edge, Hi Cut, Lo Cut) turn its phase against the untouched dry copy, so even with Hysteresis
        // Off the blend dips at the top; with the recorder in, the dry copy is as late as the recorder, so
        // the blend does exactly what it does with Hysteresis Off.
        double worstLo = 0, worstDiff = -1e9;
        for (int hq = 0; hq < 2; ++hq)
            for (double f : { 100.0, 300.0, 1000.0, 3000.0, 6000.0, 10000.0, 15000.0 })
            {
                double r[2];
                for (int mode = 0; mode < 2; ++mode)
                {
                    double l[2];
                    for (int m = 0; m < 2; ++m)
                    {
                        Fx fx(hq, mode, { {kLoSat, 0.0f}, {kMix, m ? 50.0f : 100.0f} });
                        const std::vector<float> y = fx.play(1.5, [&](long i) { return 0.01 * std::sin(kTwoPiD * f * (double)i / 44100.0); });
                        l[m] = std::abs(toneAt(y, 44100.0, f));
                    }
                    r[mode] = dB(l[1] / l[0]);
                }
                std::printf("   HQ %s, %5.0f Hz: Mix 50 %% against 100 %%: Off %+.3f dB, On %+.3f dB\n", hq ? "on " : "off", f, r[0], r[1]);
                if (f <= 6000.0) worstLo = std::fmax(worstLo, std::fabs(r[1] - r[0]));
                worstDiff = std::fmax(worstDiff, r[0] - r[1]);
            }
        check(worstLo < 0.15 && worstDiff < 0.3, "with On, Mix 50 % blends as with Hysteresis Off (within 0.15 dB up to 6 kHz, never 0.3 dB deeper above): the dry copy is as late as the recorder");
    }

    std::printf("6. Moving the knobs while a tone plays\n");
    {
        // a reference-level 1 kHz tone; the Bias knob (Split) swept from the bottom (25 %) to the top (150 %) in 0.5 s
        {
            double line[2];
            for (int sweep = 0; sweep < 2; ++sweep)
            {
                Fx fx(0, 1, { {kSplit, sweep ? 200.0f : 2500.0f} });
                const long s0 = 44100, s1 = s0 + 22050;
                const std::vector<float> y = fx.play(2.0, [](long i) { return kRefLevel * std::sin(kTwoPiD * 1000.0 * (double)i / 44100.0); },
                                                     [&](long pos) { if (sweep && pos >= s0 && pos <= s1 + 128) fx.e->setParameter(fx.e, kSplit, (float)std::min(1.0, (double)(pos - s0) / (double)(s1 - s0))); });
                line[sweep] = offLine(y, (size_t)s0 + 2048, 44100.0, 1000.0);
            }
            const double lim = -75.0;
            std::printf("   highest line off the harmonics %.1f dBFS while the bias sweeps (still at 100 %%: %.1f; the specification's prototype: -81 at 32-sample updates)\n",
                        line[1], line[0]);
            std::snprintf(what, sizeof(what), "the Bias sweep leaves no line above %.0f dBFS", lim);
            check(line[1] < lim, what);
        }
        // switching while a 300 Hz tone at reference level plays: Off -> On, SP -> EP -> SP, On -> Off -> On
        // before the recorder is out, Off, the Saturator off, then On with the Saturator on
        for (int hq = 0; hq < 2; ++hq)
        {
            Fx fx(hq, 0);
            bool glided = false, endsEp = false, heard = true;   // the new speed by a glide of the one engine, heard throughout
            const std::vector<float> y = fx.play(3.0, [](long i) { return kRefLevel * std::sin(kTwoPiD * 300.0 * (double)i / 44100.0); },
                                                 [&](long pos) {
                                                     const hys::Stage& s = *fx.vv()->hy_;
                                                     const long t = pos / 128;
                                                     glided = glided || (t > 200 && t < 300 && s.eng.es > 0.0f && s.eng.es < 1.0f);
                                                     if (t == 299) endsEp = s.eng.es == 1.0f && s.eng.ep == 1;
                                                     if (t > 200 && t < 400) heard = heard && s.run && s.f == 1.0f;
                                                     if (t == 100) fx.p->set(kHysteresis, 1.0f);
                                                     if (t == 200) fx.p->set(kHiSat, 100.0f);
                                                     if (t == 300) fx.p->set(kHiSat, 0.0f);
                                                     if (t == 400) fx.p->set(kHysteresis, 0.0f);
                                                     if (t == 402) fx.p->set(kHysteresis, 1.0f);
                                                     if (t == 600) fx.p->set(kHysteresis, 0.0f);
                                                     if (t == 700) fx.p->set(kSatOn, 0.0f);
                                                     if (t == 800) { fx.p->set(kHysteresis, 1.0f); fx.p->set(kSatOn, 1.0f); }
                                                 });
            // the distorted tone's own steps: the largest in the still stretches (each setting at rest, 0.1 s
            // after each switch to 0.1 s before the next), against the largest anywhere
            double rest = 0;
            for (long b = 0; b < 9; ++b)
            {
                const long from = (b * 100 + (b ? 35 : 20)) * 128, to = (b * 100 + 99) * 128;
                rest = std::fmax(rest, stepRatio(std::vector<float>(y.begin() + from, y.begin() + std::min<long>((long)y.size(), to)), 44100.0, 300.0, 1));
            }
            const double r = stepRatio(y, 44100.0, 300.0, 44100 / 2);
            const bool still = fx.get(kHysteresis) == 1.0f && fx.vv()->hyAny_ && fx.vv()->hy_->f == 1.0f;
            std::printf("   HQ %s: Off, On, EP, SP, Off and On again at once, Off, Saturator off, On with the Saturator on: largest step %.3f of a clean tone's (at rest %.3f)\n", hq ? "on " : "off", r, rest);
            std::snprintf(what, sizeof(what), "HQ %s: no click at any switch (no step 10 %% over the largest at rest), and the recorder is in at the end", hq ? "on" : "off");
            check(r < 1.1 * rest && still, what);
            std::snprintf(what, sizeof(what), "HQ %s: SP to EP and back while it is heard: the one engine glides to the new speed (heard all along) and ends on EP's own tables", hq ? "on" : "off");
            check(glided && endsEp && heard, what);
        }
    }

    std::printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
