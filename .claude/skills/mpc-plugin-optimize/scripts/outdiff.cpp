// outdiff.cpp: sizes the difference between two output files written by render.cpp, so a change that
// alters the output bits can be judged by a figure. bitcheck.sh builds and runs it when the files differ.
//   outdiff <old.bin> <new.bin> <old.log>
// For every phase of the run it prints the level of the old output, the RMS and the peak of the
// difference (dB relative to full scale, 1.0), how far the difference is below the output, and the
// share of samples that differ, and how far into the phase the first difference is. For a held phase (MPC_OPT_HOLD) it also prints the difference in the
// first and the last quarter, which shows whether an error grows with time.
// Samples where both files hold the same non-finite value are left out; any other disagreement that
// involves a non-finite value is counted on its own line, since it has no size.
// A second table says where in the spectrum the difference sits, since one level cannot: for each
// phase that differs, how far the difference is below the output in three bands (under 2 kHz, 2 to
// 8 kHz, above 8 kHz), and the strongest narrow line of the difference with how far it stands above
// the difference on either side of it. A line that the old or the new output does not have is marked
// A TONE: the change added, removed or moved a steady tone. A band where the difference comes close
// to the output is a hiss or a dullness the overall figure hides. The spectra are averages over
// half-overlapping frames of 8192 samples (5.4 Hz apart); lines are searched from 43 Hz up. The phase
// of bad input values is left out, since its huge samples swamp every figure.
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct Phase { std::string name; long start; };
struct Acc
{
    double sa = 0.0, sd = 0.0, peak = 0.0;
    long n = 0, differ = 0, peakAt = -1, nonFinite = 0, firstAt = -1;
    void add(float a, float b, long byteAt)
    {
        if (!std::isfinite(a) || !std::isfinite(b))
        {
            uint32_t ua, ub;
            std::memcpy(&ua, &a, 4);
            std::memcpy(&ub, &b, 4);
            if (ua != ub && !(std::isnan(a) && std::isnan(b))) { ++nonFinite; if (firstAt < 0) firstAt = byteAt; }
            return;
        }
        const double d = std::fabs((double)a - (double)b);
        sa += (double)a * (double)a;
        sd += d * d;
        ++n;
        if (d != 0.0) { ++differ; if (firstAt < 0) firstAt = byteAt; }
        if (d > peak) { peak = d; peakAt = byteAt; }
    }
    double rmsA() const { return n > 0 ? std::sqrt(sa / (double)n) : 0.0; }
    double rmsD() const { return n > 0 ? std::sqrt(sd / (double)n) : 0.0; }
};

// The spectrum of one phase: the power of the old output, the new output and the difference per
// frequency bin, averaged over half-overlapping frames of N samples (Hann window, which then weighs
// every sample the same), both channels added. The end of a phase is padded with zeros.
enum { N = 8192, Hop = N / 2, Bins = N / 2 + 1, Reach = 40, Gap = 4, FirstBin = 8 };
struct Spec
{
    std::vector<double> pa, pb, pd;
    long frames = 0;
    Spec() : pa(Bins, 0.0), pb(Bins, 0.0), pd(Bins, 0.0) {}
};
static void fft(std::vector<std::complex<double>>& x)
{
    const size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<double> w = std::polar(1.0, -2.0 * 3.14159265358979323846 / (double)len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> u(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k, u *= w)
            {
                const std::complex<double> t = u * x[i + k + len / 2];
                x[i + k + len / 2] = x[i + k] - t;
                x[i + k] += t;
            }
        }
    }
}
static void addFrame(Spec& s, const double* a, const double* b)   // N samples of one channel, old and new
{
    static std::vector<double> win;
    if (win.empty()) { win.resize(N); for (int i = 0; i < N; ++i) win[(size_t)i] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / N); }
    std::vector<std::complex<double>> x(N);
    for (int pass = 0; pass < 3; ++pass)
    {
        for (int i = 0; i < N; ++i) x[(size_t)i] = (pass == 0 ? a[i] : pass == 1 ? b[i] : a[i] - b[i]) * win[(size_t)i];
        fft(x);
        std::vector<double>& p = pass == 0 ? s.pa : pass == 1 ? s.pb : s.pd;
        for (int k = 0; k < Bins; ++k) p[(size_t)k] += std::norm(x[(size_t)k]);
    }
}
// How far bin k stands above the bins on either side of it, as a ratio of power: against the higher
// of the two medians, left and right, so the edge of a falling spectrum is no line. The reach
// shrinks toward low frequencies. 0 when a side has too few bins or nothing to compare with.
static double standsOut(const std::vector<double>& p, int k)
{
    const int reach = std::min((int)Reach, std::max(Gap + 2, k / 2));
    double m = 0.0;
    for (int side = -1; side <= 1; side += 2)
    {
        std::vector<double> near;
        for (int j = Gap; j <= reach; ++j)
        {
            const int at = k + side * j;
            if (at >= 2 && at < Bins) near.push_back(p[(size_t)at]);
        }
        if (near.size() < 3) return 0.0;
        std::nth_element(near.begin(), near.begin() + (long)(near.size() / 2), near.end());
        m = std::max(m, near[near.size() / 2]);
    }
    return m > 0.0 ? p[(size_t)k] / m : 0.0;
}
static std::string bandBelow(const Spec& s, double f0, double f1)
{
    double a = 0.0, d = 0.0;
    for (int k = 1; k < Bins; ++k)
    {
        const double f = k * 44100.0 / N;
        if (f >= f0 && f < f1) { a += s.pa[(size_t)k]; d += s.pd[(size_t)k]; }
    }
    if (!(d > 0.0)) return "none here";
    if (!(a > 0.0)) return "no output";
    char t[32];
    std::snprintf(t, sizeof t, "%.1f dB", 10.0 * std::log10(a / d));
    return t;
}

static std::string db(double x)
{
    if (!(x > 0.0)) return "none";
    char t[32];
    std::snprintf(t, sizeof t, "%.1f", 20.0 * std::log10(x));
    return t;
}
static std::string below(const Acc& c)
{
    if (!(c.rmsD() > 0.0)) return "identical";
    if (!(c.rmsA() > 0.0)) return "silent output";
    char t[32];
    std::snprintf(t, sizeof t, "%.1f dB", 20.0 * std::log10(c.rmsA() / c.rmsD()));
    return t;
}

int main(int argc, char** argv)
{
    if (argc < 4) { std::fprintf(stderr, "outdiff <old.bin> <new.bin> <old.log>\n"); return 2; }
    FILE* fa = std::fopen(argv[1], "rb");
    FILE* fb = std::fopen(argv[2], "rb");
    FILE* fl = std::fopen(argv[3], "r");
    if (fa == nullptr || fb == nullptr || fl == nullptr) { std::fprintf(stderr, "outdiff: cannot open the files\n"); return 2; }

    std::vector<Phase> ph;
    char line[256];
    while (std::fgets(line, sizeof line, fl) != nullptr)
    {
        const char* at = std::strstr(line, " starts at byte ");
        if (std::strncmp(line, "phase ", 6) != 0 || at == nullptr) continue;
        std::string name(line + 6, (size_t)(at - (line + 6)));
        while (!name.empty() && name.back() == ' ') name.pop_back();
        ph.push_back({ name, std::atol(at + 16) });
    }
    if (ph.empty()) ph.push_back({ "whole run", 0 });
    std::fseek(fa, 0, SEEK_END);
    const long size = std::ftell(fa);
    std::fseek(fa, 0, SEEK_SET);

    std::vector<Acc> acc(ph.size()), first(ph.size()), last(ph.size());
    Acc all;
    std::vector<Spec> spec(ph.size());
    static double sa2[2][N], sb2[2][N];          // the frame being filled: old and new, left and right
    int fill = 0, fresh = 0;                      // samples in the frame, and how many no frame has used yet
    size_t fillPhase = 0;
    auto frame = [&](size_t q)
    {
        addFrame(spec[q], sa2[0], sb2[0]);
        addFrame(spec[q], sa2[1], sb2[1]);
        ++spec[q].frames;
        for (int ch = 0; ch < 2; ++ch)
        {
            std::memmove(sa2[ch], sa2[ch] + Hop, sizeof(double) * (N - Hop));
            std::memmove(sb2[ch], sb2[ch] + Hop, sizeof(double) * (N - Hop));
        }
        fill = N - Hop;
        fresh = 0;
    };
    auto flush = [&](size_t q)                    // the end of a phase: pad what no frame has used with zeros
    {
        if (fresh > 0)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = fill; i < N; ++i) sa2[ch][i] = sb2[ch][i] = 0.0;
            frame(q);
        }
        fill = fresh = 0;
    };
    enum { Buf = 16384 };
    static float a[Buf], b[Buf];
    long byteAt = 0;
    size_t p = 0;
    for (;;)
    {
        const size_t na = std::fread(a, 4, Buf, fa), nb = std::fread(b, 4, Buf, fb);
        const size_t n = na < nb ? na : nb;
        for (size_t i = 0; i < n; ++i, byteAt += 4)
        {
            while (p + 1 < ph.size() && byteAt >= ph[p + 1].start) ++p;
            {
                // the file holds left and right in turn (render.cpp); a frame is N samples of each
                if (p != fillPhase) { flush(fillPhase); fillPhase = p; }
                const bool ok = std::isfinite(a[i]) && std::isfinite(b[i]);
                const int ch = (int)((byteAt / 4) & 1);
                sa2[ch][fill] = ok ? (double)a[i] : 0.0;
                sb2[ch][fill] = ok ? (double)b[i] : 0.0;
                if (ch == 1) { ++fresh; if (++fill == N) frame(p); }
            }
            acc[p].add(a[i], b[i], byteAt);
            all.add(a[i], b[i], byteAt);
            const long end = p + 1 < ph.size() ? ph[p + 1].start : size;
            const long len = end - ph[p].start;
            if (len > 0)
            {
                const long q = (byteAt - ph[p].start) * 4 / len;
                if (q == 0) first[p].add(a[i], b[i], byteAt);
                if (q == 3) last[p].add(a[i], b[i], byteAt);
            }
        }
        if (na != nb) std::printf("  the files differ in length\n");
        if (n < Buf) break;
    }
    flush(fillPhase);

    std::printf("  %-28s %10s %10s %10s %14s %9s %12s\n", "phase", "output RMS", "diff RMS", "diff peak", "diff below out", "differing", "first diff");
    for (size_t i = 0; i <= ph.size(); ++i)
    {
        const Acc& c = i < ph.size() ? acc[i] : all;
        char when[32] = "none";
        if (c.firstAt >= 0)   // seconds into the phase: 8 bytes per frame at 44.1 kHz
            std::snprintf(when, sizeof when, "%.3f s", (double)(c.firstAt - (i < ph.size() ? ph[i].start : 0)) / 8.0 / 44100.0);
        std::printf("  %-28s %10s %10s %10s %14s %8.2f%% %12s\n", i < ph.size() ? ph[i].name.c_str() : "whole run",
                    db(c.rmsA()).c_str(), db(c.rmsD()).c_str(), db(c.peak).c_str(), below(c).c_str(),
                    c.n > 0 ? 100.0 * (double)c.differ / (double)c.n : 0.0, when);
        if (i < ph.size() && ph[i].name.compare(0, 4, "held") == 0)
            std::printf("  %-28s diff RMS in the first quarter %s, in the last quarter %s\n", "",
                        db(first[i].rmsD()).c_str(), db(last[i].rmsD()).c_str());
    }
    std::printf("  levels are dB relative to full scale (1.0)\n");
    // Where in the spectrum the difference sits.
    {
        bool head = false;
        int tones = 0;
        for (size_t i = 0; i < ph.size(); ++i)
        {
            const Spec& s = spec[i];
            if (acc[i].firstAt < 0) continue;
            if (!head)
            {
                std::printf("  where the difference sits: how far it is below the output in each band, and its strongest narrow line\n");
                std::printf("  %-28s %12s %12s %12s   %s\n", "phase", "under 2 kHz", "2 to 8 kHz", "over 8 kHz", "strongest line of the difference");
                head = true;
            }
            if (ph[i].name.compare(0, 9, "bad input") == 0) { std::printf("  %-28s left out: its huge input samples swamp every figure\n", ph[i].name.c_str()); continue; }
            if (s.frames < 8) { std::printf("  %-28s too short to measure\n", ph[i].name.c_str()); continue; }
            int best = -1;
            double bestOut = 0.0;
            for (int k = FirstBin; k < Bins - FirstBin; ++k)
            {
                if (!(s.pd[(size_t)k] > 0.0)) continue;
                const double o = standsOut(s.pd, k);
                if (o > bestOut) { bestOut = o; best = k; }
            }
            char lineText[200] = "none";
            if (best >= 0)
            {
                // A line of the difference is a tone added, removed or moved when the old or the new
                // output has no line of its own there (under 6 dB).
                auto dbOf = [](double r) { return 10.0 * std::log10(std::max(r, 1e-30)); };
                const double outDb = dbOf(bestOut), oldDb = dbOf(standsOut(s.pa, best)), newDb = dbOf(standsOut(s.pb, best));
                const bool line = outDb >= 15.0, tone = line && std::min(oldDb, newDb) < 6.0;
                if (tone) ++tones;
                std::snprintf(lineText, sizeof lineText, "%.0f Hz, %.0f dB above the difference beside it%s", best * 44100.0 / N, outDb,
                              tone ? "  <-- A TONE" : line ? " (both outputs have a line there)" : "");
            }
            std::printf("  %-28s %12s %12s %12s   %s\n", ph[i].name.c_str(), bandBelow(s, 0.0, 2000.0).c_str(),
                        bandBelow(s, 2000.0, 8000.0).c_str(), bandBelow(s, 8000.0, 1e9).c_str(), lineText);
        }
        if (tones > 0)
            std::printf("  A TONE: the difference has a narrow line that the old or the new output lacks, in %d phase(s): the\n"
                        "  change added, removed or moved a steady tone. A value added to the signal and updated once every\n"
                        "  N samples puts one at 44100 / N Hz and its multiples\n", tones);
    }
    {
        int heldPhases = 0, heldDiffer = 0;
        for (size_t i = 0; i < ph.size(); ++i)
            if (ph[i].name.compare(0, 4, "held") == 0) { ++heldPhases; if (acc[i].firstAt >= 0) ++heldDiffer; }
        if (heldPhases > 0 && heldDiffer == 0 && all.firstAt >= 0)
            std::printf("  NO HELD PHASE REACHES THE DIFFERENCE, so this run says nothing about whether it grows:\n"
                        "  choose held settings that use the changed code (MPC_OPT_HOLD_PROGRAMS, MPC_OPT_HOLD_SET)\n");
    }
    if (all.peakAt >= 0)
    {
        size_t q = 0;
        while (q + 1 < ph.size() && all.peakAt >= ph[q + 1].start) ++q;
        std::printf("  largest difference %s dB at byte %ld, in phase %s\n", db(all.peak).c_str(), all.peakAt, ph[q].name.c_str());
    }
    std::printf("  samples where one build is not finite and the other disagrees: %ld\n", all.nonFinite);
    return 0;
}
