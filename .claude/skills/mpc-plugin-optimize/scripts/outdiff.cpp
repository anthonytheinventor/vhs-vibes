// outdiff.cpp: sizes the difference between two output files written by render.cpp, so a change that
// alters the output bits can be judged by a figure. bitcheck.sh builds and runs it when the files differ.
//   outdiff <old.bin> <new.bin> <old.log>
// For every phase of the run it prints the level of the old output, the RMS and the peak of the
// difference (dB relative to full scale, 1.0), how far the difference is below the output, and the
// share of samples that differ. For a held phase (MPC_OPT_HOLD) it also prints the difference in the
// first and the last quarter, which shows whether an error grows with time.
// Samples where both files hold the same non-finite value are left out; any other disagreement that
// involves a non-finite value is counted on its own line, since it has no size.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct Phase { std::string name; long start; };
struct Acc
{
    double sa = 0.0, sd = 0.0, peak = 0.0;
    long n = 0, differ = 0, peakAt = -1, nonFinite = 0;
    void add(float a, float b, long byteAt)
    {
        if (!std::isfinite(a) || !std::isfinite(b))
        {
            uint32_t ua, ub;
            std::memcpy(&ua, &a, 4);
            std::memcpy(&ub, &b, 4);
            if (ua != ub && !(std::isnan(a) && std::isnan(b))) ++nonFinite;
            return;
        }
        const double d = std::fabs((double)a - (double)b);
        sa += (double)a * (double)a;
        sd += d * d;
        ++n;
        if (d != 0.0) ++differ;
        if (d > peak) { peak = d; peakAt = byteAt; }
    }
    double rmsA() const { return n > 0 ? std::sqrt(sa / (double)n) : 0.0; }
    double rmsD() const { return n > 0 ? std::sqrt(sd / (double)n) : 0.0; }
};

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

    std::printf("  %-28s %10s %10s %10s %14s %9s\n", "phase", "output RMS", "diff RMS", "diff peak", "diff below out", "differing");
    for (size_t i = 0; i <= ph.size(); ++i)
    {
        const Acc& c = i < ph.size() ? acc[i] : all;
        std::printf("  %-28s %10s %10s %10s %14s %8.2f%%\n", i < ph.size() ? ph[i].name.c_str() : "whole run",
                    db(c.rmsA()).c_str(), db(c.rmsD()).c_str(), db(c.peak).c_str(), below(c).c_str(),
                    c.n > 0 ? 100.0 * (double)c.differ / (double)c.n : 0.0);
        if (i < ph.size() && ph[i].name.compare(0, 4, "held") == 0)
            std::printf("  %-28s diff RMS in the first quarter %s, in the last quarter %s\n", "",
                        db(first[i].rmsD()).c_str(), db(last[i].rmsD()).c_str());
    }
    std::printf("  levels are dB relative to full scale (1.0)\n");
    if (all.peakAt >= 0)
    {
        size_t q = 0;
        while (q + 1 < ph.size() && all.peakAt >= ph[q + 1].start) ++q;
        std::printf("  largest difference %s dB at byte %ld, in phase %s\n", db(all.peak).c_str(), all.peakAt, ph[q].name.c_str());
    }
    std::printf("  samples where one build is not finite and the other disagrees: %ld\n", all.nonFinite);
    return 0;
}
