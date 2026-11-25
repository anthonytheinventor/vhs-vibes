// tbcount.cpp: the fast counter behind count.sh, profile.sh and lineprof.sh.
// It reads the log of   qemu-arm -d in_asm,exec,nochain   on stdin. In that mode the emulator runs
// whole translated blocks of code and logs one line per block executed, where the older method
// (-one-insn-per-tb) logged one line per instruction. Each block's instructions are known from the
// listing the emulator prints when it first translates it, so the counts are the same and the log is
// several times shorter.
//   tbcount [options] <icMark address> <icEnd address> [block ...]      (addresses in hex, even)
//   -c <file>   write "<block> <instructions>" per audio block: the instructions executed after the
//               first instruction of icMark and before icEnd, as the older method counted them
//   -s <file>   mapping symbols of the program, "<hex address> a|t" per line, sorted: where ARM code
//               ($a) and Thumb code ($t) start. Without it every block is taken as ARM code.
//   -e <file>   for each block named: "<block> <hex address> <count>" per instruction address
//   -u <file>   summed over the blocks named, or over every block from 200 on when none is named:
//               a first line "blocks <n>", then "<hex address> <count>" per instruction address
//   -p <file>   keep the number of the audio block reached in this file (for status.txt)
//   -t          count the plugin's other threads too (every "Trace <n>:" line between the markers)
// Exit code 3 means the log was not in a form it can count exactly (the scripts then fall back to
// the older method): no block listings, or a block whose bytes do not fit its instruction set.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

struct Tb
{
    std::vector<uint8_t> len;          // the byte length of each instruction
    std::vector<uint32_t> each;        // executions inside each named block (-e), one per named block
    uint64_t sum = 0;                  // executions inside the summed blocks (-u)
    bool known = false;
};
static std::unordered_map<uint32_t, Tb> tbs;
static std::vector<std::pair<uint32_t, char>> modes;     // mapping symbols, sorted by address

static bool thumbAt(uint32_t pc)
{
    auto it = std::upper_bound(modes.begin(), modes.end(), std::make_pair(pc, (char)127));
    return it != modes.begin() && (it - 1)->second == 't';
}
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

int main(int argc, char** argv)
{
    const char *countsPath = nullptr, *symPath = nullptr, *eachPath = nullptr, *sumPath = nullptr, *progPath = nullptr;
    bool allThreads = false;
    int a = 1;
    for (; a < argc && argv[a][0] == '-'; ++a)
    {
        const char o = argv[a][1];
        if (o == 't') { allThreads = true; continue; }
        if (a + 1 >= argc) return 2;
        const char* v = argv[++a];
        if (o == 'c') countsPath = v; else if (o == 's') symPath = v; else if (o == 'e') eachPath = v;
        else if (o == 'u') sumPath = v; else if (o == 'p') progPath = v; else return 2;
    }
    if (a + 1 >= argc) { std::fprintf(stderr, "tbcount [options] <icMark hex> <icEnd hex> [block ...]\n"); return 2; }
    const uint32_t mark = (uint32_t)std::strtoul(argv[a], nullptr, 16) & ~1u, end = (uint32_t)std::strtoul(argv[a + 1], nullptr, 16) & ~1u;
    std::unordered_map<long, int> pick;                  // block number -> its place in the list
    std::vector<long> picks;
    for (a += 2; a < argc; ++a) { pick[std::atol(argv[a])] = (int)picks.size(); picks.push_back(std::atol(argv[a])); }
    if (symPath != nullptr)
        if (FILE* f = std::fopen(symPath, "r"))
        {
            char ad[64], k[8];
            while (std::fscanf(f, "%63s %7s", ad, k) == 2) modes.push_back({ (uint32_t)std::strtoul(ad, nullptr, 16) & ~1u, k[0] });
            std::fclose(f);
            std::sort(modes.begin(), modes.end());
        }
    FILE* fc = countsPath != nullptr ? std::fopen(countsPath, "w") : nullptr;

    static char line[1 << 16];
    bool listing = false, bad = false;
    uint32_t first = 0;
    bool haveFirst = false;
    std::string bytes;                 // the hex of the block being listed
    std::vector<uint32_t> perLine;     // instruction addresses, when the emulator lists one per line
    uint64_t total = 0, markAt = 0, listed = 0;
    long blk = -1, summed = 0;
    int cur = -1;                      // the place of the current audio block in the named list, or -1
    bool inSum = false, open = false;

    auto closeListing = [&]()
    {
        if (!listing) return;
        listing = false;
        if (!haveFirst) return;
        Tb& t = tbs[first];
        t.len.clear();
        if (!bytes.empty())
        {
            const size_t n = bytes.size() / 2;
            // Thumb where the mapping symbols say so, and for code outside them whose length is no
            // multiple of four (the emulator's own signal-return stub, say).
            if (thumbAt(first) || n % 4 != 0)
                for (size_t i = 0; i < n;)
                {
                    if (i + 1 >= n) { bad = true; break; }
                    const int hi = hexv(bytes[2 * i + 2]) * 16 + hexv(bytes[2 * i + 3]);   // the second byte: the high half
                    const int l = (hi & 0xf8) >= 0xe8 ? 4 : 2;
                    if (i + (size_t)l > n) { bad = true; break; }
                    t.len.push_back((uint8_t)l);
                    i += (size_t)l;
                }
            else
                t.len.assign(n / 4, 4);
        }
        else                            // one instruction per listed line
            for (size_t i = 0; i < perLine.size(); ++i)
                t.len.push_back((uint8_t)(i + 1 < perLine.size() ? perLine[i + 1] - perLine[i] : 4));
        t.known = !t.len.empty();
        if (t.known) ++listed;
    };

    while (std::fgets(line, sizeof line, stdin) != nullptr)
    {
        if (line[0] == 'T' && std::strncmp(line, "Trace ", 6) == 0)
        {
            closeListing();
            const int cpu = std::atoi(line + 6);
            if (cpu != 0 && !(allThreads && open)) continue;
            const char* b = std::strchr(line, '[');
            const char* s = b != nullptr ? std::strchr(b, '/') : nullptr;
            if (s == nullptr) continue;
            const uint32_t pc = (uint32_t)std::strtoull(s + 1, nullptr, 16);
            auto it = tbs.find(pc);
            if (it == tbs.end() || !it->second.known) { bad = true; continue; }
            Tb& t = it->second;
            const uint64_t k = t.len.size();
            if (cpu == 0 && pc == mark)
            {
                ++blk;
                markAt = total + 1;                       // the first instruction of icMark is not counted
                open = true;
                auto p = pick.find(blk);
                cur = p != pick.end() ? p->second : -1;
                inSum = sumPath != nullptr && (picks.empty() ? blk >= 200 : cur >= 0);
                if (inSum) ++summed;
                if (progPath != nullptr && blk % 5 == 0)
                    if (FILE* fp = std::fopen(progPath, "w")) { std::fprintf(fp, "%ld\n", blk); std::fclose(fp); }
            }
            else if (cpu == 0 && pc == end)
            {
                if (fc != nullptr && open) std::fprintf(fc, "%ld %llu\n", blk, (unsigned long long)(total - markAt));
                open = false; cur = -1; inSum = false;
                continue;
            }
            total += k;
            if (open)
            {
                if (inSum) ++t.sum;
                if (cur >= 0 && eachPath != nullptr)
                {
                    if (t.each.empty()) t.each.assign(picks.size(), 0);
                    ++t.each[(size_t)cur];
                }
            }
            continue;
        }
        if (std::strncmp(line, "IN:", 3) == 0) { closeListing(); listing = true; haveFirst = false; bytes.clear(); perLine.clear(); continue; }
        if (!listing) continue;
        if (line[0] == '0' && line[1] == 'x')
        {
            const uint32_t ad = (uint32_t)std::strtoul(line + 2, nullptr, 16);
            if (!haveFirst) { first = ad; haveFirst = true; }
            const char* c = std::strchr(line, ':');
            if (c != nullptr) { ++c; while (*c == ' ') ++c; if (*c != '\n' && *c != '\r' && *c != 0) perLine.push_back(ad); }
            continue;
        }
        if (std::strncmp(line, "OBJD-", 5) == 0)
        {
            const char* h = std::strchr(line, ':');
            if (h == nullptr) continue;
            ++h;
            while (*h == ' ') ++h;
            bytes.append(h, std::strcspn(h, " \r\n"));
            continue;
        }
        if (line[0] == '-') closeListing();
    }
    if (fc != nullptr) std::fclose(fc);
    if (listed == 0 || bad) { std::fprintf(stderr, "tbcount: the log could not be counted exactly (%s)\n", listed == 0 ? "no block listings" : "a block did not fit"); return 3; }

    // Per instruction address. The instruction at icMark itself is left out, as in the counts.
    auto emit = [&](FILE* f, long label, const Tb& t, uint32_t pc, uint64_t n)
    {
        uint32_t ad = pc;
        for (size_t i = 0; i < t.len.size(); ad += t.len[i], ++i)
        {
            if (pc == mark && i == 0) continue;
            if (label >= 0) std::fprintf(f, "%ld %08x %llu\n", label, ad, (unsigned long long)n);
            else std::fprintf(f, "%08x %llu\n", ad, (unsigned long long)n);
        }
    };
    if (eachPath != nullptr)
        if (FILE* f = std::fopen(eachPath, "w"))
        {
            for (const auto& e : tbs)
                for (size_t i = 0; i < e.second.each.size(); ++i)
                    if (e.second.each[i] != 0) emit(f, picks[i], e.second, e.first, e.second.each[i]);
            std::fclose(f);
        }
    if (sumPath != nullptr)
        if (FILE* f = std::fopen(sumPath, "w"))
        {
            std::fprintf(f, "blocks %ld\n", summed);
            for (const auto& e : tbs)
                if (e.second.sum != 0) emit(f, -1, e.second, e.first, e.second.sum);
            std::fclose(f);
        }
    return 0;
}
