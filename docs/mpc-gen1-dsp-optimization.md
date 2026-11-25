# Writing fast DSP for first-generation MPC hardware

For plugin developers targeting the 32-bit standalone MPCs. These have a fraction of a laptop's
processing power, and a plugin shares one audio block with the user's whole project, so code that is
comfortable on a desktop can cause dropouts here.

Every figure below carries its origin:

- **(device)**: timed on an MPC One+, while optimizing a real effect plugin or in a micro-benchmark of
  single instructions pinned to one core and calibrated against a chain of integer adds (one cycle
  each), so time taken by other threads on that core cancels out.
- **(counted)**: executed instructions counted under an emulator while optimizing the same plugin.
- **(compiled)**: seen in assembly built with the SDK's own flags (Clang 22 as bundled with zig 0.17,
  arm32 `-mcpu=cortex_a17 -O2` unless stated).
- **(documented)**: stated by ARM, the chip's datasheet, the compiler's source or the SDK's docs.
- **(checked)**: confirmed by a test run on the stated platform, or by a numeric sweep.
- **(inferred)**: reasoning from the above that has not been measured.

"Frame" means one sample period at 44.1 kHz, all channels. "Sample" is used for the oversampled rate.
Figures introduced with "in one plugin" show the scale of an effect in one code base. Use them to judge
whether a check is worth running, never as targets.

See also the SDK's `docs/host-contract.md`, `docs/dsp-cookbook.md`, `docs/building.md` and
`framework/mpc_plugin.h` for the host's behavior and the framework this guide assumes.

## Biggest wins first

1. No divide, square root or transcendental function per frame that could run per block; reciprocal
   estimates for the divides that remain, where the output need not match an earlier release bit for bit.
2. Stereo processed as one vector.
3. No `double` and no library math calls in the frame loop.
4. The data touched per block fits in about 24 KB, inside the 32 KB L1 data cache.
5. No table rebuild, stage start-up or doubled stage inside one block. Resets, program changes and mode
   switches count as rebuilds: under a random-parameter bench they set the worst block (section 14).
6. Optional stages compiled out when they are off.
7. Only then the micro-tuning below.

## Choosing what to change

The order below is a recommendation; the measurements behind it are in the sections it cites.

1. **Decide first whether the output may change.** If a release must match the previous one bit for
   bit, only bit-exact changes qualify: hoisting, caches, flags set per tick, clearing only reachable
   state, mirrored exact symmetry, the square-root builtin, inlining and member layout (sections 3, 7,
   8, 10). These change the bits and need the owner's decision: reciprocal estimates, a reordered or
   vectorized sum, fused against non-fused multiply-add, a new or removed floor, a detector moved to
   another rate, a different oversampling factor (sections 4, 5, 11, 12).
   Put a figure on such a change before deciding: render the old and the new build over the same
   input and express the RMS and the peak of the difference in dB, against full scale and against
   the output level. Weigh that figure against the time saved, one change at a time. An error
   inside a feedback path can grow, so measure a run of many seconds as well and listen to it.
2. **Then fix the worst block.** One block over the limit is a dropout, whatever the average. Resets,
   rebuilds and stage start-ups come before any frame-loop work (sections 8, 14).
3. **Then the high-percentile block under moving controls,** which is what a bench with random
   parameters reports (section 14).
4. **Then the steady cost per frame,** which matters most when several instances run.
5. **Where speed and sound conflict,** prefer the change whose error is bounded and below the noise
   the stage already has. An estimate with a relative error under 1e-5 is acceptable on a new path;
   keep the exact operation where the result feeds a small difference or a long feedback path
   (section 4). Gliding one copy's coefficients in place of a two-copy crossfade changes how a
   transition sounds, never the steady sound (section 8).
6. **Stop** when the worst block is under the limit with margin, the steady cost is a few percent of
   a core, and the next candidate saves under about 2 % of the instructions per frame: a saving of that
   size did not show on the device (section 2). Stop earlier if the next change would cost
   bit-identity that the release needs.

## 1. Know the machine

| | |
|---|---|
| Chip | Rockchip RK3288: four identical cores, rated to 1.8 GHz (documented), 1.608 GHz here (device) |
| Core | Cortex-A17. The silicon reports Cortex-A12 (part 0xC0D in `/proc/cpuinfo`); renamed, same speed |
| Instruction set | 32-bit ARMv7-A with NEON and VFPv4, fused multiply-add, hardware integer divide |
| Pipeline | out of order, two instructions per cycle, about 11 stages |
| L1 cache | 32 KB data and 32 KB instruction per core, 4-way, 64-byte lines |
| L2 cache | 1 MB, shared by all four cores |
| Audio | 44.1 kHz only, blocks of 32 to 512 frames |

Applies to the 32-bit products: MPC X, Live, Live II, One, One+, Key 61, Key 37 and Force (the
products whose plugins load the `file32` binary).

**The budget.** All four cores ran at a fixed 1.608 GHz on an MPC One+: the governor is `performance`,
and the frequency did not change under load or after idling (device). A 128-frame block lasts 2902 µs,
about 4.67 million processor cycles. One percent of that block is about 46,700 cycles, or roughly 365
cycles per frame, at any block size. A stereo effect that spends 1,000 instructions per frame uses about
3 % of a core, assuming about one instruction per cycle (inferred).

**Costs at a glance.** Cycles on the device, with the section that explains each. "Every" is how often
a new independent operation can start. One percent of a block is about 365 cycles per frame.

| Operation | Latency | Every | Section |
|---|---|---|---|
| NEON float multiply, two or four lanes | 4 | 1 (two lanes), 2 (four) | 5 |
| NEON float add, two or four lanes | 5 | 1 (two lanes), 2 (four) | 5 |
| NEON `vmla.f32` or `vfma.f32`, four lanes | 4 via the accumulator, 8 via the multiplicand | 2 | 5 |
| NEON `vmin` or `vmax`, four lanes | 5 | 2 | 3 |
| Scalar float multiply | 6 (5 with FZ set) | | 5, 11 |
| Scalar float add | 5 | | 5 |
| Scalar compare, flag transfer and conditional move | 9 | | 3 |
| Mispredicted branch | about 12 | | 4 |
| Float divide, full mantissa | 18 | 16 | 4 |
| Float square root, full mantissa | 17 | 15 | 4 |
| Float divide or root with a few-bit result | 9 | 7 | 4 |
| Double divide | 32 | 30 | 3 |
| Four divides from a vector `/` | | 64 | 4 |
| Four square roots; four roots with divides | | 60; 124 | 4 |
| Reciprocal estimate and one Newton step, four lanes | 17 | 6 | 4 |
| Reciprocal square-root estimate and one step, four lanes | 21 | 8 | 4 |
| Integer divide | 8 to 19, by quotient size | same | 4 |
| NEON-to-core register move | 7 for a round trip | 4 | 5 |
| Random read within 24 KB; at 32 KB | 4; 10 | | 7 |
| Random read at 64 KB; 256 to 512 KB; 1 MB and beyond | 23; 32 to 44; 130 to 200 | | 7 |
| Sequential read per 64-byte line: L1; L2; main memory | 7; 14 to 18; 22 | | 7 |
| Scalar load across a cache line | 7 | 2.2 | 7 |
| 81-entry table with interpolation; rational tanh, per four lanes | 45; 34 | | 4 |
| `clock_gettime` | about 760 | | 2 |

**Targets.** Aim for a few percent of one core per instance on average. Keep the worst block of the whole
plugin under 15 %, the limit in the SDK's `dsp-cookbook.md`. The Open MPC catalog's bench is looser on
the worst block: it passes a plugin whose 99th-percentile block is at most 15 % and whose worst block is
at most 50 % (documented, bench source; section 14). Several instances multiply these figures.

## 2. Measure before you optimize

- **Count instructions for precision.** Run the ARM build under an emulator that counts executed
  instructions (qemu with a counting plugin). The count is exact and repeatable, so it can prove that a
  change did not add instructions to a path, to a fraction of an instruction per frame.
- **Counts do not see time.** Each of these is one instruction: a divide takes 18 cycles, a
  mispredicted branch about 12, and a table read that misses the cache up to about 200 (all device). In
  practice:
  - Replacing 14 scalar divides per frame with reciprocal estimates added 28 instructions per frame
    (counted) yet made the plugin faster on the device (device).
  - An 81-entry lookup table cost more on the device than its count suggested; a closed-form curve was
    faster (device).
  - Code that reads a lot of table memory got about 10 % fewer instructions done per cycle than code
    that computes (device, against counts).
  - Counts also overstate. Rewriting three 64-tap filters to run side by side and moving a large
    inlined function out of its loops cut 15 to 16 % of the instructions per frame of one mode of an
    effect plugin (counted) and changed its time by under 1 %, inside the run-to-run spread (device).
    The instructions removed were ones the processor already ran side by side. Expect time from a
    change that removes a divide, a library call, a cache miss or a link in a chain of dependent
    operations; expect little from one that only removes independent loads, moves and loop overhead
    (inferred from the above).
- **The device has the last word, so time is the score.** A change is a win when it lowers the time
  on the hardware; a lower count alone is a lead to follow up. Time candidates alternating A, B, A, B.
  Results drift by about half a percentage point between sessions (device), so two builds timed an
  hour apart cannot be compared. Time with the thread's own CPU clock, not the wall clock (section 14).
- **Watch the worst block, not only the average.** A feature that costs 3 % on average still causes a
  dropout if one block in a thousand costs 40 %. Script the events that cost extra (a knob sweep, a mode
  switch, several controls at once) and look at the heaviest blocks.
- **Test control moves at the smallest block size.** Bugs in control handling hide at 128 frames and
  appear at 32.
- **Desktop timings are a first estimate.** The SDK's host simulator times the desktop build; the
  cookbook's rough factor to the A17 is 15 to 25 (documented).
- **A timing bench cannot see a small steady saving.** Eight changes to one effect plugin cut its steady
  instructions by 1 to 2.4 % per frame (counted), and its idle mean and 99th percentile read 2.9 % and
  3.5 % of a 128-frame block before and after (device). Judge changes of that size by instruction counts.
- **Prove bit-identity on the target.** When a change must leave the output unchanged, compare the arm32
  build's output under the emulator, with NaN and infinity in the test signal (a huge finite input can
  overflow into them). Agreement on x86 is not proof: a rewrite that matched on x86 changed the output on
  ARM when one lane held a NaN (section 3). For a change that relies on a bound, also run a control that
  breaks the bound slightly; it must fail.

**A counting and bench harness.** One small program includes the plugin's source, drives it the way a
host does and marks each block. It serves three jobs: instruction counts per block under the emulator,
bit-identity checks (write `oL` and `oR` to a file and compare two builds), and the random-parameter
bench of section 14 when run on the device. An events file named in `MPC_OPT_EVENTS` sets parameters
or selects programs at given blocks, by index or by name, which is how to hold a mode on or to place a
mode switch; `drive list` prints the names. The file `<events>.applied` then shows what each event set,
in the plugin's own value text. Read it: a setting that lands on the wrong parameter measures the
wrong mode without any other sign.

```cpp
// drive.cpp: includes the plugin's source and drives it the way a host does.
//   drive [k] [blocks]   k = parameters set to random values before each block (0 = steady state)
//   drive list           print every parameter's index, name and current value text, then exit
// Events: set MPC_OPT_EVENTS to a text file to force settings or mode switches. One event per line,
//   <block> <parameter> <value 0..1>     set a parameter (normalized) before that block; the parameter
//                                        is its index or its name as "drive list" prints it, with _ for
//                                        spaces (case does not matter)
//   <block> program <number>             select a program before that block
// Lines starting with # are skipped. Block 0 events set the state the run starts in. What each event
// did is written to <events file>.applied with the value text the plugin reports, so a setting that
// did not land where intended shows up there: read it before trusting the run.
// Timing (meaningful on a device only): each block is timed with the thread's own CPU clock, so time
// taken by other threads on the core, the plugin's own worker threads included, does not count. The
// wall-clock line beside it does include them; a large gap between the two means the core is shared.
#include SRC
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <time.h>
#include <vector>

extern "C" __attribute__((noinline, used)) void icMark(int b) { asm volatile("" :: "r"(b) : "memory"); }
extern "C" __attribute__((noinline, used)) void icEnd(int b)  { asm volatile("" :: "r"(b) : "memory"); }
static VstIntPtr hostCb(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float) { return op == audioMasterVersion ? 2400 : 0; }
static int64_t nowNs(clockid_t c) { timespec t; clock_gettime(c, &t); return int64_t(t.tv_sec) * 1000000000 + t.tv_nsec; }
struct Event { int block, index; float value; };
static std::string paramText(AEffect* e, int i, VstInt32 op)
{
    char buf[256] = {};
    e->dispatcher(e, op, i, 0, buf, 0.0f);
    return buf;
}
static bool sameName(const std::string& a, const char* b)   // ignores case, and treats _ as a space
{
    size_t n = strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i)
    {
        const char x = a[i] == ' ' ? '_' : (char)tolower((unsigned char)a[i]);
        const char y = b[i] == ' ' ? '_' : (char)tolower((unsigned char)b[i]);
        if (x != y) return false;
    }
    return true;
}

static void report(const char* what, std::vector<int64_t>& ns, int Warm, int Blocks, int N)
{
    std::sort(ns.begin() + Warm, ns.end());
    const double block = N * 1e9 / 44100.0, n = Blocks - Warm;
    double sum = 0.0;
    for (int b = Warm; b < Blocks; ++b) sum += (double)ns[b];
    std::printf("%s: mean %.2f %%  p99 %.2f %%  worst %.2f %% of a %d-frame block\n", what, 100.0 * sum / n / block,
                100.0 * (double)ns[Warm + (int)(n * 0.99)] / block, 100.0 * (double)ns[Blocks - 1] / block, N);
}

int main(int argc, char** argv)
{
    enum { N = 128, Warm = 200 };
    AEffect* e = VSTPluginMain(hostCb);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher(e, effSetBlockSize, 0, N, nullptr, 0.0f);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0.0f);
    if (argc > 1 && strcmp(argv[1], "list") == 0)
    {
        for (int i = 0; i < e->numParams; ++i)
            std::printf("%3d  %-24s %s\n", i, paramText(e, i, effGetParamName).c_str(), paramText(e, i, effGetParamDisplay).c_str());
        std::printf("%d parameters, %d programs\n", e->numParams, e->numPrograms);
        return 0;
    }
    const int k = argc > 1 ? atoi(argv[1]) : 0;
    const int Blocks = argc > 2 ? std::max(atoi(argv[2]), Warm + 100) : 4000;
    std::vector<Event> events;
    FILE* applied = nullptr;
    if (const char* path = getenv("MPC_OPT_EVENTS"))
    {
        FILE* f = fopen(path, "r");
        if (f == nullptr) { std::fprintf(stderr, "cannot open events file %s\n", path); return 2; }
        applied = fopen((std::string(path) + ".applied").c_str(), "w");
        char line[256], key[128];
        while (fgets(line, sizeof line, f))
        {
            Event ev;
            if (line[0] == '#' || sscanf(line, "%d %127s %f", &ev.block, key, &ev.value) != 3) continue;
            char* end = nullptr;
            const long idx = strtol(key, &end, 10);
            if (*end == 0) ev.index = (int)idx;
            else if (sameName("program", key)) ev.index = -1;
            else
            {
                ev.index = -2;
                for (int i = 0; i < e->numParams; ++i)
                    if (sameName(paramText(e, i, effGetParamName), key)) { ev.index = i; break; }
            }
            if (ev.index < -1 || ev.index >= e->numParams)
            {
                if (applied) std::fprintf(applied, "NOT APPLIED, no such parameter: %s", line);
                std::fprintf(stderr, "events: no such parameter: %s", line);
                continue;
            }
            events.push_back(ev);
        }
        fclose(f);
    }
    static float L[N], R[N], oL[N], oR[N];
    float* in[2] = { L, R };
    float* out[2] = { oL, oR };
    std::vector<int64_t> cpu(Blocks), wall(Blocks);
    unsigned seed = 12345;
    for (int b = 0; b < Blocks; ++b)
    {
        for (int i = 0; i < k; ++i)
        {
            const int p = rand_r(&seed) % e->numParams;
            e->setParameter(e, p, rand_r(&seed) / (float)RAND_MAX);
        }
        for (const Event& ev : events)
        {
            if (ev.block != b) continue;
            if (ev.index < 0)
            {
                e->dispatcher(e, effSetProgram, 0, (VstIntPtr)ev.value, nullptr, 0.0f);
                if (applied) std::fprintf(applied, "block %d: program %d\n", b, (int)ev.value);
            }
            else
            {
                e->setParameter(e, ev.index, ev.value);
                if (applied) std::fprintf(applied, "block %d: %d %s = %s (%.4f)\n", b, ev.index,
                                          paramText(e, ev.index, effGetParamName).c_str(),
                                          paramText(e, ev.index, effGetParamDisplay).c_str(), ev.value);
            }
        }
        for (int i = 0; i < N; ++i) L[i] = R[i] = rand_r(&seed) / (float)RAND_MAX - 0.5f;
        const int64_t w0 = nowNs(CLOCK_MONOTONIC), c0 = nowNs(CLOCK_THREAD_CPUTIME_ID);
        icMark(b);
        e->processReplacing(e, in, out, N);
        icEnd(b);
        cpu[b] = nowNs(CLOCK_THREAD_CPUTIME_ID) - c0;
        wall[b] = nowNs(CLOCK_MONOTONIC) - w0;
    }
    if (applied) fclose(applied);
    report("thread CPU time", cpu, Warm, Blocks, N);
    report("wall clock     ", wall, Warm, Blocks, N);
    return 0;
}
```

Build it static for arm32, then count the instructions between the two marks of every block
(checked with qemu 8.2 on an SDK example plugin; older qemu spells `-one-insn-per-tb` as `-singlestep`):

```
zig c++ -O2 -std=c++17 -pthread -target arm-linux-musleabihf -mcpu=cortex_a17 -static \
  -I<sdk>/framework -I<project>/src '-DSRC="MyPlugin.cpp"' drive.cpp -o drive
M=$(nm drive | awk '$3 == "icMark" { print $1 }'); E=$(nm drive | awk '$3 == "icEnd" { print $1 }')
qemu-arm -one-insn-per-tb -d exec,nochain -D /dev/stdout drive 2>/dev/null | grep -F "Trace 0:" \
  | grep -n -e "/00000000$M/" -e "/00000000$E/" | cut -d: -f1 \
  | awk 'NR % 2 == 1 { p = $1 } NR % 2 == 0 { print (NR - 2) / 2, $1 - p - 1 }' > counts.txt
```

`counts.txt` holds one line per block: its index and its instruction count. Divide a steady block by
the frame count for instructions per frame; for an event, read the largest block after it. The printed
timing lines mean nothing under the emulator. For a total over the whole run, qemu's `libinsn.so`
plugin (`qemu-arm -plugin <path>/libinsn.so -d plugin drive`) is faster.

**A timing recipe for inside the host.** Time every `process()` call into a preallocated buffer; write
it out only after processing has stopped.

```cpp
#include <stdint.h>
#include <time.h>

static inline int64_t nowNs()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return int64_t(t.tv_sec) * 1000000000 + t.tv_nsec;
}

struct BlockLog                                   // a member of the plugin, so no allocation in process()
{
    enum { N = 16384 };                           // a power of two
    int32_t ns[N];
    int16_t frames[N];
    uint32_t count = 0;
    void add(int64_t t0, int n)
    { uint32_t i = count++ & (N - 1); ns[i] = int32_t(nowNs() - t0); frames[i] = int16_t(n); }
};
// In process():  int64_t t0 = nowNs();  /* render */  blockLog.add(t0, numFrames);
```

- Percent of the block is `ns * 44100 / (frames * 1e7)`. Report the median, the 99th percentile and the
  maximum over thousands of blocks, after discarding a few seconds of warm-up.
- Judge the maximum only inside the host's real-time audio thread. In an ordinary-priority test process,
  the worst single block was a preemption spike of about a quarter of a millisecond (device).
- Write the buffer to a file in the destructor, which runs on the UI thread after the host has stopped
  processing. Keep the project, the block size and the playback state fixed between runs.
- Time an empty `process()` the same way and subtract it. `clock_gettime` is a system call on the
  device and costs about 0.5 µs, some 760 cycles (device). With the recipe compiled unchanged, an empty
  `process()` read 583 ns, 0.02 % of a 128-frame block (device): good for whole blocks, too coarse for
  small sections. If you can, record the core frequency from a non-audio thread
  (`/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq`).
- The MPC's own CPU meter is a coarse cross-check. Reading the ARM cycle counter (`PMCCNTR`) from user
  space raised an illegal-instruction signal; the virtual timer count (`CNTVCT`) was readable, and its
  frequency register (`CNTFRQ`) held no usable value (device).

## 3. Float hygiene

- **Suffix every float literal.** `x * 0.1` with a float `x` became a convert to double, a double
  multiply and a convert back (compiled). `x * 0.5` and `x / 3.0` stayed single precision (compiled),
  since the compiler could prove the float result unchanged. Do not rely on that: write `0.1f`.
- **Keep `double` out of the frame loop.** NEON has no double lanes on ARMv7, and a double divide takes
  32 cycles, with a new one every 30, against 18 and 16 for float (device). `std::sin(x * 2.0 * pi)`
  with a float `x` called the double `sin` (compiled). `-Wdouble-promotion` shows only implicit
  promotions: an explicit `(double)x` and arithmetic between `double` members raised no warning
  (compiled), and in one plugin all 64 of its warnings were in set-up code (compiled).
  Confirm with the assembly of the hot functions (section 13).
- **ARMv7 cannot round a float to a whole-number float, and NEON cannot round at all.** `floorf` and
  `lrintf` compiled to library calls, even with `-fno-math-errno` (compiled). The scalar
  `vcvtr.s32.f32` does round to an integer in the current rounding mode; Clang did not emit it for
  `lrintf` (compiled). The `vrint` instructions arrived with ARMv8 (documented). Use a truncating
  convert with a correction:

  ```c
  // |x| below 2^31. NaN and negative denormals give 0; -0 gives +0.
  static inline float32x4_t floor4(float32x4_t x)
  {
      float32x4_t t = vcvtq_f32_s32(vcvtq_s32_f32(x)); // truncate toward zero
      uint32x4_t up = vcgtq_f32(t, x);                 // truncation rounded up
      return vbslq_f32(up, vsubq_f32(t, vdupq_n_f32(1.0f)), t);
  }
  ```

- **ARMv7 has no scalar float min or max.** `fminf` compiled to a library call. `std::clamp` and
  `a < b ? a : b` compiled to `vcmp`, a flag transfer (`vmrs`) and a conditional move, once per bound
  (compiled). Though branchless, each compare, transfer and conditional move took 9 cycles (device).
  Clamp in vectors with `vminq_f32` and `vmaxq_f32`: 5 cycles
  of latency, and a new four-lane one every 2 cycles (device).
- **NEON min and max propagate NaN; a compare-and-select does not.** `vmaxq_f32` and `vminq_f32` return
  NaN when either input is NaN (documented, ARM), while `a > b ? a : b` returns one of its inputs. In one
  plugin, a NEON all-below test in place of a scalar maximum matched on x86 and changed the output on ARM
  (checked under an emulator).
- **`sqrtf` and `std::sqrt` carry an error check.** With Clang's default `-fmath-errno`, both compiled to
  `vsqrt.f32`, a self-compare (`vcmp.f32 s2, s2`), `vmrs` and a path to the library `sqrtf` taken on NaN
  (in a loop, `bvs` to a `bl sqrtf`) (compiled). `__builtin_elementwise_sqrt(x)` gives the bare
  `vsqrt.f32` (compiled) and the same bits, since a square root is correctly rounded (documented, IEEE
  754). The SDK build does not pass `-fno-math-errno`; that flag also lets Clang change other library
  calls (inferred), so where the output must stay bit-identical, prefer the builtin.

## 4. Arithmetic

- **There is no vector divide and no vector square root** on ARMv7 NEON (documented). Writing `/` on a
  vector type compiled to four scalar `vdiv.f32` (compiled).
- **The divider handles one value at a time.** With full-mantissa operands a float divide takes 18
  cycles and a new one can start only every 16; a float square root takes about 17 and a new one starts
  every 15 (device). Both are faster when the result has few mantissa bits (1.0, one power of two divided by another, the root
  of 4 or 16): 9 cycles, one every 7 (device). Budget for the full-mantissa figures; audio data has them. Many divides per frame queue up
  behind each other.
- **Use reciprocal estimates for bulk divides.** `vrecpeq_f32` followed by one Newton step
  (`vrecpsq_f32`) gives under 1e-5 relative error (8.3e-6 at worst, checked), which is inaudible in most audio math. Use
  `vrsqrteq_f32` and `vrsqrtsq_f32` for 1/sqrt. Take two Newton steps where the result feeds a small
  difference. One estimate plus one Newton step has a latency of 17 cycles, against 18 for one divide,
  but handles four lanes, and a new vector can start every 6 cycles, against 64 for the four scalar
  divides a vector `/` compiles to (device): about 10 times faster per vector. For 1/sqrt, the estimate
  and one step take 21 cycles and one vector per 8, against 124 for four square roots with divides, or 60
  for four square roots alone, all with full-mantissa inputs (device). A single isolated divide can stay
  a plain divide.
- **Estimates change the output bits.** A divide and a square root are correctly rounded (documented,
  IEEE 754); an estimate is not, even after Newton steps. On a path that must stay bit-identical to an
  earlier release, a divide stays a divide, and a root can only become another correctly rounded root
  (the builtin in section 3). Use estimates on new paths, or where a release may change the sound
  slightly. Section 6 has a `recip()` with one Newton step and a portable fallback.

- **Guard the input of every estimate.** For the reciprocal, zero and denormal inputs give infinity,
  inputs above about 1e38 give 0, and negative inputs are fine. For the reciprocal square root, negative
  inputs give NaN. Zero and denormal inputs give infinity from the estimate alone, and NaN once the usual
  Newton step multiplies x by the estimate. Forming sqrt(x) as x times rsqrt(x) gives NaN at x = 0 too
  (all checked under an emulator). Clamp the input away from zero, and away from negatives for rsqrt,
  before the estimate.
- **Hoist divides out of the frame loop.** Divide a coefficient that changes once per block once per
  block.
- **No `exp`, `pow`, `tanh`, `sin` or `log` per frame.** Compute them when a parameter changes, or
  replace them with a polynomial or rational shape that behaves the same where it matters.
- **Avoid branches on float compares in the frame loop.** A scalar float compare must pass its flags to
  the integer core: a compare, transfer and conditional move took 9 cycles (device), and a mispredicted
  branch costs about 12 more (device). Use `vminq_f32`, `vmaxq_f32` and `vbslq_f32`.
- **Prefer a closed form to a lookup table for a smooth curve.** A short formula stays in registers and
  vectorizes. A table costs index math, a clamp and memory reads. With both hot in L1, an 81-entry
  table with linear interpolation took 45 cycles per four-lane vector, and a rational tanh with a
  reciprocal estimate took 34 (device).
- **Keep an exactly linear region if the curve has one.** If most frames fall where a saturator is a
  straight line, test once per vector and skip the curve.
- **Start an iterative solve from the previous frame's answer.** Audio moves smoothly, so two Newton
  steps usually suffice. Cap the step count so the cost is bounded.
- **Integer divide is one instruction.** With the SDK's `-mcpu=cortex_a17`, `a / b` on `int` compiled
  to `sdiv` (compiled). Its time depends on the data: 8 cycles for 1000 / 7, 12 for a 15-bit quotient,
  19 for a 29-bit one, and the next divide waits as long (device). See section 13 for the flag.

## 5. NEON

- **Two channels are two lanes.** Treat a stereo frame as one vector everywhere. Running a mono path
  twice is the slowest way to process stereo.
- **Fill four lanes when the work is there** (two channels times two samples, for example in
  oversampled code). This halves the instructions, though the arithmetic is no faster per lane: a
  four-lane float operation runs as two halves. A two-lane add or multiply can start every cycle, a
  four-lane one every 2 cycles; either way a NEON multiply takes 4 cycles and an add 5, against 6 and 5
  for scalar code with flush-to-zero off, or 5 and 5 with it on (device, section 11). Do not add shuffles just to reach four lanes.
- **There are no gathers.** A table read per lane is a scalar load per lane. Vectorize the index math
  and the interpolation around the reads.
- **Moving a lane into an integer register is slow.** A NEON-to-core move can start only every 4
  cycles, whether it moves one lane or a whole d register into two core registers (`vmov r, r, d`)
  (device). Fetch two indices with one two-register move: it is twice as fast per index. Moves the other
  way start every cycle, and a round trip takes 7 cycles (device).
- **Write the vector code yourself.** Clang does not vectorize float loops on 32-bit ARM unless every
  fast-math flag is on, because NEON is not IEEE-compliant (documented, LLVM source). A plain
  multiply-add loop was not vectorized, even with `-fno-math-errno -ffp-contract=fast` (compiled).
  `#pragma clang loop vectorize(enable)` before one loop overrides the check: it vectorized that loop and
  a float sum (compiled), reordering the sum. That loop then flushes denormals, and the reordered sum
  rounds differently.
- **With the SDK's flags, Clang does not fuse a vector multiply-add** (documented, LLVM source). A vector
  `a * b + c`, and also `vaddq_f32(vmulq_f32(a, b), c)`, compiled to one non-fused `vmla.f32`; `vfmaq_f32`
  compiled to one `vfma.f32` (compiled). With `-ffp-contract=fast` or `-ffast-math`, both of the first
  two compiled to the fused `vfma.f32`, even with the multiply and the add in separate statements
  (compiled): adding either flag changes the output bits of existing NEON code. `vmla.f32` and `vfma.f32` run at the same speed: 4 cycles through the
  accumulator, 8 through the multiplicand, one four-lane op every 2 cycles (device). A separate `vmul`
  and `vadd` took 5 and 9 cycles and half the throughput (device). Write `vfmaq_f32` only for its
  rounding.
- **Clang does fuse scalar expressions.** A scalar `a * b + c` in one expression compiled to the fused
  `vfma.f32`. The same math split into two statements compiled to the non-fused `vmla.f32` (compiled).
  So a vector body and its scalar tail can round differently: the pragma-vectorized loop above used
  `vmla.f32` in its body and `vfma.f32` in its tail (compiled). Where a scalar path and a vector path
  must agree to the bit, write the rounding explicitly in both: `vfmaq_f32` with `std::fma` (which
  compiled to `vfma.f32`), or `vmla` math with the multiply and the add in separate statements (default flags only; the two
  flags above fuse across statements). The same
  holds when moving existing scalar code into NEON: if the scalar version compiled to `vfma.f32`, the
  vector version needs `vfmaq_f32` (or `vfma_f32` for two lanes) to keep its bits.
- **Load with `vld1q_f32(ptr)`.** Casting a `float*` to a vector pointer lets the compiler assume
  alignment, and the load can fault.
- **Do not split a tight loop to "vectorize harder".** Splitting a 64-tap filter loop by channel made
  it slower: the compiler stopped unrolling it (counted). Try it, count it, keep what is faster.
- **Keep hot vectors in q0 to q3 and q8 to q15.** Registers q4 to q7 are saved by the callee, so a
  function that uses them pays one push and one pop per call (documented): negligible around a block
  loop, worth avoiding in a small function called per frame. A function call inside the loop spills
  every live scratch register.

## 6. NEON code that builds everywhere

The SDK builds every plugin for arm32, arm64 and the desktop computer that runs its host simulator, and
the simulator test must pass. `arm_neon.h` exists on both ARM targets and not on x86.

**Guard intrinsics with `__ARM_NEON` and give each one a generic fallback.** Write most of the math with
generic vector types (`__attribute__((vector_size(16)))` or a small wrapper class), and keep intrinsics
for the few operations that need them, such as the estimates.

```c
#if defined(__ARM_NEON)
#include <arm_neon.h>
typedef float32x4_t f4;
static inline f4 recip(f4 x)                    // x must not be zero or tiny
{
    f4 r = vrecpeq_f32(x);
    return vmulq_f32(r, vrecpsq_f32(x, r));     // one Newton step
}
#else
typedef float f4 __attribute__((vector_size(16)));
static inline f4 recip(f4 x) { return 1.0f / x; }
#endif
```

This compiled to `vrecpe`/`vrecps`/`vmul` on arm32, `frecpe`/`frecps`/`fmul` on arm64 and `divps` on
x86-64 (compiled). The desktop result differs in the last bits; test it against a tolerance.

## 7. Memory

- **About 24 KB is the fast budget** for everything a block touches: delay lines, tables, filter
  state, the audio buffers themselves. The L1 data cache holds 32 KB, and the stack and the host's own
  data share it: a random read took 4 cycles within 24 KB and 10 at 32 KB (device).
- **Count bytes touched per block.** A 2048-float table is 8 KB, a third of that budget. A 64-tap stereo
  FIR history is 512 bytes. One second of stereo float delay is about 345 KB, a third of the shared L2;
  only the lines a delay reads and writes in one block count toward that block (inferred).
- **Past it, reads hit the 1 MB L2,** which is shared with the other three cores and everything else in
  the project. A random read takes 4 cycles within 24 KB, about 23 at 64 KB, 32 to 44 at 256 to 512 KB
  and 130 to 200 (80 to 125 ns, TLB misses included) from main memory at 1 MB and beyond (device): an
  L2 hit costs 6 to 11 times an L1 hit. The hardware prefetcher follows sequential reads, so they stay
  cheap: per 64-byte line, about 7 cycles in L1, 14 to 18 in L2 and 22 at 4 to 16 MB (device).
- **Assume the L1 is cold at the start of each block.** The host and other plugins ran on the core in
  between (inferred). The MPC app pins a real-time audio worker to each of the four cores, so a plugin
  can run on any of them, and the app's own threads took between under 1 % and 14 % of each core,
  depending on the run (device). Compact state pays off on every block.
- **A larger table can win warm and lose cold.** A harness that runs blocks back to back keeps the
  plugin's tables in the cache. A bench that jumps between settings, or a host that ran other work on
  the core in between, reads them cold. In one plugin a change that traded computation for a larger
  lookup table cut 13 % of the instructions of the worst block (counted) and 2.6 % of the steady
  time (device), and still made the result of the Open MPC bench worse (device). After a change that
  adds or enlarges a table, compare the worst block under changing parameters as well as the steady
  time (section 14).
- **Mind the 8 KB spacing.** The L1 is 4-way with 8 KB per way. At exactly 8 KB spacing up to four
  streams always fit; with five or more, an access rose from 4 cycles to about 9 to 26 in most
  placements, in one placement only at six or eight streams, and in one only at twelve (device). At 4 KB spacing eight streams
  usually fit and twelve never did (device). The placement dependence is consistent with a physically
  indexed cache, so the program cannot control conflicts exactly. **Offset parallel delay lines by one
  cache line:** at 8 KB + 64 bytes or 4 KB + 64 bytes, twelve streams never conflicted (device).
  Power-of-two `std::vector` delay lines allocated one after another start a few bytes apart modulo
  4 KB (inferred); allocate them as one block, each 64 × k bytes past a 4 KB boundary. Their accesses
  are sequential, so the cost is probably smaller than the random-read figures above (inferred).
- **Unaligned loads cost little.** A 128-bit `vld1q` at 8-byte alignment cost nothing measurable, even
  across a cache line or page boundary; at a 4-byte offset it managed one load per 2.3 cycles against
  1.75 (device). A scalar `ldr` across a line takes 7 cycles instead of 4, one per 2.2 cycles instead of
  per cycle (device). Aligning vector data to 16 bytes is free, so do it anyway. **Align hot tables to 64
  bytes** so they occupy the fewest lines: a 256-byte table then touches 4 lines instead of up to 5.
- **Prefetch hints do not help** small tables or a filter's history. They are already in L1.
- **Precompute off the audio thread.** Build tables in the constructor or `prepare()`. A parameter change
  should blend or select ready-made data.
- **Read each row once.** When updating a table, blend and accumulate in a single pass, with running
  sums in registers.
- **Ship only what can be reached.** Table data that no control setting uses is binary size and cache
  pressure for nothing.
- **Use exact symmetry.** If every row of a table is exactly even or odd, half of each rebuild or blend
  can be a mirrored copy (negated for odd), and the stored table can be half the size. The integral of
  an exactly odd row, built outward from the middle with negated operands, is exactly even (inferred).
  Verify the symmetry bitwise on every row, never from the formula alone, and compare the mirrored result
  with the full computation in a test. In one plugin, blending half of each row and mirroring it matched
  the full loop on 4.75 billion values (checked on x86-64, and on arm32 and arm64 under an emulator).

## 8. Blocks, controls and threads

- **Never rebuild in one block.** Recomputing a table or starting an expensive stage inside one block
  makes a spike. Split the work across blocks, or run it at a fixed, short cadence while a control
  moves.
- **Resets and switches are rebuilds.** A mode switch, program change or state load that clears state,
  redesigns filters and recomputes constants is one block of rebuild work, and under a random-parameter
  bench it sets the worst block (section 14). Three rules cut it without changing a bit:
  - *Clear only what a reader can reach.* After a reset a delay line's writer restarts and its reader
    sits at the resting delay. If the delay cannot grow faster than one sample per sample, only the last
    (largest resting delay plus the interpolation margin) entries before the write position can be read
    before they are overwritten (inferred). Prove the bound in a test build: fill the uncleared part
    with NaN at every reset and check that the output is unchanged and free of NaN, then clear a few
    entries too few and check that NaN appears.
  - *Compute per-rate constants once per rate.* A value that depends on the sample rate alone, such as a
    noise power summed over a 50 ms impulse response or an integral over 800 sines, belongs in a cache
    filled once per rate.
  - *Skip a redesign whose input did not change.* Moving a gain control of a stage that is not heard, or
    choosing the same type again, should not redesign that stage.

  With these, and with half of a symmetric table row mirrored (section 7), single blocks fell from 290,144
  to 152,890 instructions for a quality switch (a reset), 203,354 to 101,253 for a program change,
  124,753 to 96,286 for a gain move on an unheard stage and 154,103 to 94,467 for choosing a model a
  second time (counted). The first block after load fell from 11.5, 10.8 and 12.4 % to 10.2, 9.2 and
  9.3 % of a 128-frame block in three runs (device).
- **Never run two copies of an expensive stage to crossfade.** Glide one copy's coefficients to the new
  setting.
- **Do each control's work once per block,** however many times the host sets it during that block.
- **Compare a control's plain value, not its raw float,** so a knob turned across a switch position does
  not restart a fade on every step.
- **A delayed update must not restart its own delay.** If each change restarts a countdown, a control
  that moves in every block never takes effect until it stops.
- **Handle any frame count.** The user can choose 32, 64, 96, 128, 192, 256 or 512 frames, and the
  announced size is only a maximum (documented). All of these are multiples of 32, so 32-frame
  control-rate subblocks divide them evenly. Keep a scalar tail in vector loops anyway.
- **Smooth cheaply.** A per-frame one-pole smoother per parameter adds up, and the SDK's `Smoother` also
  compares on every frame (documented). Prefer a per-block linear ramp (the SDK's `LinearRamp`) or
  control-rate smoothing. Once per block, check whether a smoother has settled and take a constant path
  if it has. Smooth the final coefficient only when that is cheaper than recomputing it. A control-rate
  loop over many smoothed values never settles under automation, or under a bench that writes parameters
  every block, so its scalar compares run on every tick (inferred): write it in NEON.
- **Parameter writes can arrive during `process()`.** Changes from automation, project load and preset
  load can come from the UI thread while the block runs (documented). Read each parameter once per block
  from its atomic into a snapshot, and have every set-up function read the snapshot, never the atomics
  again. A second read can see a later write, such as part of a preset load that a check at the start of
  the block had ruled out (inferred).
- **The per-block read is a pointer chase.** The SDK framework stores each value as a separately
  allocated `std::atomic<float>` behind a vector of pointers (documented, framework source): each
  parameter is a pointer load followed by a value load. The 60 pairs do not depend on each other, so the
  core overlaps them; the cost is 60 scattered, probably cold cache lines, a fixed cost that weighs most
  at 32-frame blocks (inferred). The framework's `get(i)` also converts the value to its plain range on
  every call: a clamp and a switch, plus a `powf` and a divide for a logarithmic or skewed parameter and
  a library rounding call for a choice or integer one (documented, framework source). Read
  `getNormalised(i)` in one pass, compare each with its last value, and convert only the ones that
  changed.
- **Keep the audio thread real-time.** No allocation, locks, logging or file access in `process()`.
  Audio threads on some products have a 1 MB stack (documented), so put large arrays on the heap,
  allocated in `prepare()`. Swap heavy state (tables, samples) in with a lock-free handoff.

## 9. Instruments and polyphony

The measurements in this guide come from an effect. This section has no timings of its own: its items
are documented or inferred, and the costs in section 1 apply per voice.

- **The worst block is every voice sounding plus a burst of note-ons.** Cost per voice times the maximum
  voice count is the figure to budget.
- **Skip idle voices and steal without allocating.** Stop processing a voice once its envelope is done
  and its output is below a level gate (the SDK's `ADSR` has `isActive()`). Keep voice state in a fixed
  array (the SDK's `VoiceAllocator<N>`).
- **Voices can be lanes.** Four voices of mono state make one vector: an alternative to stereo as
  lanes that fills all four lanes (inferred).
- **Mind event splits.** Instruments get MIDI with sample-accurate offsets (documented). Splitting the
  block at each offset adds a loop start per event, which adds up when many events land in one block
  (inferred).

## 10. Code shape

- **Skip stages at compile time.** Make the frame loop a template with one flag per optional stage and
  choose the instantiation once per block, so a stage that is off costs nothing. Checking "is this
  stage on" from memory in every frame cost up to 19 instructions per frame (counted).
- **Test control-rate state with bools.** A test such as `amount > 0.0f` on state that changes only in
  the control tick costs a `vcmp`, a `vmrs` and a branch in every frame. Set a `bool` once per tick and
  test that. In one plugin this removed 4 to 7 scalar `vcmp` per loop copy (compiled), with identical
  output. Put new bools in existing padding, and confirm with a record-layout dump (add
  `-fsyntax-only -Xclang -fdump-record-layouts` to the build flags) that no other member moved (compiled).
- **Watch inlining when a function gains a second caller.** Clang may stop inlining it, which slows a
  path you did not touch. Force the inline or give the new caller its own copy. Compile-time stage flags
  multiply callers: two loop copies that call the same instantiation of a helper template gave it two
  callers, and Clang made it an out-of-line function (compiled). Giving each copy its own instantiation,
  with an extra template argument the helper ignores, inlined it again (compiled). Check that the `bl` to
  its symbol is gone.
- **Member order affects speed.** `vldr` reaches only 1020 bytes from its base register and NEON `vld1`
  takes no offset at all, while integer `ldr` reaches 4095 bytes (documented). A float member at offset
  2000 needed an extra `add` before its `vldr`; one at 3208 needed a `movw` and an `add`; an `int` at
  2004 loaded directly (compiled). Keep hot float members in the first kilobyte of the object, or of the
  struct that holds them. Add new members at the end, so the offsets of hot fields do not move.
- **A large base class puts every member out of reach.** The SDK's `mpc::Plugin` holds its MIDI event
  buffers inline, about 57 KB, so every member of a derived plugin, an effect's included, starts about
  57 KB from `this` (compiled, record layout). Clang then keeps a few base registers in the loop and
  reloads the addresses of other members from the stack before each access (`ldr r0, [sp, #512]` marked
  `@ 4-byte Reload`, then `vldr s4, [r0]`): 74 such reloads in one plugin's lightest loop copy and 203 in
  its heaviest (compiled, static counts). What counts is the distance from a base register the compiler
  keeps. Gather the per-frame members into one struct under 1 KB, bind a reference to it at the top of the
  loop, and move large arrays (rings, delay lines) to the heap or after it (inferred). Count every loop
  copy afterward.
- **Link-time optimization changes inlining decisions.** Recheck the hot paths after turning it on or
  off.

## 11. Denormals and the FPU mode

- **NEON arithmetic always flushes denormals to zero** on 32-bit ARM, whatever the mode bit says
  (documented).
- **Scalar float code honors the FZ bit in FPSCR.** Do not assume its state. A fresh process starts
  with FZ clear, a new thread inherits FZ from the thread that created it, set or clear, `fork` keeps
  it and `exec` clears it (device). The host may have set it.
- **Only arithmetic flushes.** Loads, stores, moves and bit-selects pass a denormal through unchanged.
- **The mode on each platform.** On the 32-bit MPCs the host runs with flush-to-zero on (documented by
  the SDK). Read at the plugin's entry point on an MPC One+, before any framework code ran, FPSCR had
  FZ set, default-NaN mode clear and round-to-nearest, the same on every one of 10,000 blocks, all on
  one audio thread (device). On the 64-bit MPCs the host does not flush (documented by the SDK, not
  observed here). The SDK framework's
  `ScopedDenormalsOff` sets FZ on ARM, and FTZ with DAZ on x86, around every `process()` call on every
  platform (documented). Outside the framework, set the mode yourself.
- **Set the mode inside `process()` and restore it before returning.** It is thread state that belongs
  to the host. Clang does not treat ordinary float math as reading FPSCR, so after inlining it may move
  that math across the mode write (documented, LLVM). Set the mode in a thin wrapper and call a function
  that is not inlined into it:

  ```c
  struct Ctx;
  __attribute__((noinline)) void renderBlock(Ctx& c);   // defined elsewhere

  #if defined(__arm__)                                  // arm32 only; arm64 uses FPCR
  void process(Ctx& c)
  {
      unsigned old = __builtin_arm_get_fpscr();
      __builtin_arm_set_fpscr(old | (1u << 24));        // FZ
      renderBlock(c);
      __builtin_arm_set_fpscr(old);
  }
  #endif
  ```

  This compiled to `vmrs`, `orr`, `vmsr`, `bl renderBlock`, `vmsr`, in that order (compiled).
- **Denormals cost no time on this core.** With FZ off, scalar VFP multiply and add chains with
  denormal operands and results ran at the same speed as with normal values, and so did a one-pole
  filter stuck at a denormal value (device). The same holds for divide and square root, float and
  double: a denormal dividend, divisor or result, and the square root of a denormal, each matched a
  normal value with the same mantissa to within 0.3 % (device). Setting FZ made a scalar multiply 1
  cycle shorter (6 to 5), whatever the values (device).
- **Flush anyway, for consistency.** With FZ set, scalar code flushes like NEON, so a scalar tail and a
  vector body agree. Denormals may be slow on other processors, such as the desktop x86 test build and
  possibly the 64-bit MPCs (not measured here). Where the mode is not known, give decaying scalar state
  (filter and envelope tails) a tiny floor. In an SDK plugin the framework sets the mode, so a floor buys
  no speed; decide floors for correctness (tails that must reach exact zero, recovery from NaN). Once
  released, a floor is part of the output, and removing it changes the bits.
- **Never rely on a compiler flag to set the FPU mode.** Whether `-ffast-math` sets it in a shared library
  depends on the toolchain and its version; the zig the SDK uses ships no start-up file for it. Set the
  mode explicitly.
- **Expect small differences across platforms.** The device fuses a scalar `a * b + c` into one
  rounding (compiled); an x86-64 baseline build used a separate multiply and add (compiled). The C
  library's `sinf`, `expf` and so on differ. NEON code may have a different fallback on the desktop.
  Flush mode differs unless the framework or your own code sets it on both. Compare against a tolerance.

## 12. Oversampling

- **2x oversampling with first-order antiderivative anti-aliasing** is the affordable standard on this
  hardware. Going to 4x roughly doubles a stage's cost.
- **The antiderivative method needs a memoryless curve.** For a stage with memory, restructure it so the
  nonlinear part has none, then anti-alias that part.
- **The antiderivative quotient loses precision** when consecutive inputs are close or values are large.
  Below a threshold, use the curve at the midpoint of the two inputs, f((x[n] + x[n-1]) / 2): its error
  shrinks with the square of the input step, the current input's only linearly, and on a 1 kHz sine at
  half scale it was about 500 times closer (checked numerically). The current input alone also shifts
  the output by half a sample. Scale the threshold with the size of the input: at the same step, the
  float quotient's error near 4.0 was about 1000 times that near 0.1 (checked numerically). For a
  piecewise polynomial curve, write each piece's mean in closed form instead: for x², the mean over
  [a, b] is (a² + ab + b²) / 3. Nothing large is then subtracted from anything large, and a fixed tiny
  threshold is enough (inferred).
- **Design the filter that follows with the anti-aliasing in the loop,** so its half-sample delay and
  high-frequency droop are corrected once.
- **Run level detectors at the rate their time constants assume.** A detector whose coefficients are set
  per sample behaves differently at twice the rate. Rescale its time constants for the oversampled rate,
  or feed it the base-rate signal. Rescaling keeps it running twice per frame at 2x; feeding the base rate
  halves its cost and changes its output (inferred).
- **A blend at the oversampled rate keeps everything after it there.** A dry/wet mix inside the 2x
  section holds the linear stages that follow at 2x as well. Moving them out needs a second down-filter
  for the other branch; count both versions before choosing (inferred).
- **Keep the reported latency constant.** The host reads latency once at load (documented). Do not let
  it depend on an oversampling switch: report the largest latency any setting can have, and delay the
  shorter paths to match. The framework's `setLatency` signals
  a change to the host (documented, framework source), but whether the MPC acts on it is unknown
  (section 16), so a plugin should not rely on it.

## 13. Compiler

- **The SDK compiles with Clang, through zig.** For arm32, `tools/build.py` passes
  `zig c++ -target arm-linux-gnueabihf.2.31 -mcpu=cortex_a17 -std=c++17 -O2` plus `-fPIC -shared`,
  hidden visibility, `-ffunction-sections -fdata-sections` and `-Wall -Wextra` (documented). There is
  no `-mfpu`: the triple gives hard float, and the CPU name gives NEON and VFPv4.
- **The same target in plain Clang spelling** is
  `--target=arm-linux-gnueabihf -mcpu=cortex-a17 -mfpu=neon-vfpv4 -mfloat-abi=hard -O2`.
- **Name the core.** With `-mcpu=cortex_a17` (zig) or `-mcpu=cortex-a17` (Clang), integer divide is one
  hardware instruction. With a generic ARMv7-A CPU it became a call to `__aeabi_idiv` (compiled).
- **Clang has no scheduling model for this core** and schedules it as a Cortex-A9 (documented, LLVM
  source). Measure instead of trusting its instruction order. Clang's float behavior on this target
  (vectorization, fusion, errno) is in sections 3 and 5.
- **The build runs in ARM state.** The arm32 output was ARM code (`.code 32`), not Thumb (compiled).
- **Check the assembly.** Several rules here can only be confirmed by reading it. With the SDK's zig
  (`python -m ziglang` from `.sdk-tools/zigenv` if zig is not on your path):

  ```
  zig c++ -target arm-linux-gnueabihf.2.31 -mcpu=cortex_a17 -std=c++17 -O2 \
    -I<sdk>/framework -I<project>/src -Rpass-missed=loop-vectorize -S -o out.s <project>/src/MyPlugin.cpp
  ```

  `-Rpass-missed=loop-vectorize` reports only loops the vectorizer tried; add
  `-Rpass-analysis=loop-vectorize` to see why it gave up. Code written with vector types (section 6)
  gives it nothing to report. This scan prints, for every function of 100 instructions or more, the
  counts of the patterns listed below it (checked on an SDK example plugin):

  ```awk
  /^[A-Za-z_][A-Za-z0-9_.$]*:/ { f = $1; next }
  f == "" || $1 ~ /^[.@]/ || NF == 0 { next }
  { n[f]++ }
  $1 ~ /^(vdiv|vsqrt)\./ { div[f]++ }
  $1 ~ /\.f64$/ && $1 !~ /^vmov/ { dbl[f]++ }
  $1 ~ /^bl?$/ && $2 ~ /^(sin|cos|tan|exp|pow|log|tanh|floor|ceil|round|lrint|fmin|fmax|sqrt)[fl]?$|^__aeabi_/ { lib[f]++ }
  $1 == "vmrs" { cmp[f]++ }
  /Spill/ { sp[f]++ }
  /Reload/ { rl[f]++ }
  END { for (f in n) if (n[f] >= 100)
          printf "%s insns %d div/sqrt %d f64 %d libcalls %d compares %d spills %d reloads %d\n",
                 f, n[f], div[f], dbl[f], lib[f], cmp[f], sp[f], rl[f] }
  ```

  ```
  awk -f scan.awk out.s | sort -k3 -n -r | head -20 | c++filt
  ```

  In the hot functions, look for:
  - `vdiv.f32` and `vsqrt.f32` (divides and roots), and `vrecpe`/`vrsqrte` where you expected them;
  - `.f64` arithmetic: `vadd`, `vsub`, `vmul`, `vmla`, `vfma`, `vdiv`, `vcmp` or `vcvt` with `.f64`
    (double creep). A two-lane float vector lives in one d register, so its copies show as
    `vmov.f64 dN, dM` or `vorr dN, dM, dM` (compiled); those move floats and are not double math;
  - `bl` or `b` to `sinf`, `floorf`, `fminf`, `lrintf`, `sqrtf` or an `__aeabi_` helper (library calls);
  - `vcmp` followed by `vmrs APSR_nzcv` inside a loop (scalar compares). Read the control flow to
    separate the ones every frame runs from those in rare branches: one loop copy had 68, most of them
    guarded (compiled);
  - `vmla.f32` against `vfma.f32` (rounding);
  - `vpush {d8` (callee-saved registers in use);
  - an `add` or `movw` into a base register right before a `vldr` (a member offset out of reach);
  - `ldr rN, [sp, #n]` marked `@ 4-byte Reload` and followed by a `vldr` or `vstr` through `rN` (a
    member's address reloaded from the stack inside the loop, section 10);
  - register pressure: count the `Spill` and `Reload` comments and the `vldr` loads from `.LCPI`
    constant pools per hot function. One plugin's loop copies had 150 to 330 spills and 190 to 530
    reloads each, the most common overhead instructions in them (compiled, static counts). A smaller
    loop body, or fewer live values across it, cuts them (inferred).

## 14. Passing a CPU bench

The Open MPC plugin catalog runs a CPU bench on listed plugins: `tools/bench.c` in
[open-mpc/mpc-vst-plugins](https://github.com/open-mpc/mpc-vst-plugins) (MIT license), with its rules in
that repository's `docs/BENCH.md`. It loads the built `.so` on the device, plays it in 128-frame blocks
through several stages (idle, audio or held chords, a sweep that sets 4 random parameters per block, or
every parameter with `-p`, and the release tail), and gives a verdict from the worst stage: PASS up to
15 % at the 99th percentile and 50 % for the worst block, WARN up to 35 % and 80 %, FAIL above
(documented, bench source). It times each block with the thread's own CPU clock, so time taken by the
app's threads does not count (documented, bench source).

**For a plugin headed to that catalog, its bench is the last check, and its verdict is the one that
counts.** Their `tools/bench.sh <plugin.so> <device-ip>` builds it in Docker and runs it. The
`mpc-plugin-optimize` skill's `openmpc-bench.sh` fetches the current `bench.c`, builds it unchanged with
the SDK's zig, and runs it the same way: the bench and a copy of the plugin go to `/tmp` on the MPC and
are deleted afterwards. Run old and new builds alternately: on an MPC One+, two alternating runs of one
effect's builds repeated to within 0.1 points at the 99th percentile and under 1 point at the worst block
(device).

The harness in section 2 is the same kind of test for use while you work, since it builds from source
and also serves the instruction counts: copy the one static file to the device and run it on one core
with the number of parameters to randomize per block.

```
taskset 4 ./drive 0        # steady state
taskset 4 ./drive 4        # four random parameters per block
MPC_OPT_EVENTS=/tmp/mode.txt taskset 4 ./drive 0     # steady state with a mode held on
```

It prints two lines, each with the mean, the 99th percentile and the worst block as percent of a
128-frame block: one timed with the thread's own CPU clock, as the Open MPC bench does, and one with
the wall clock. Run old and new builds alternately (section 2).

**Read the thread CPU time.** The MPC app keeps running beside the harness, and its threads preempt
an ordinary-priority process on the cores they use. The thread clock leaves that out; the wall clock
does not. An SDK example delay, steady state, with the app idle, two runs on each core (device):

| Core (mask) | Thread CPU: mean, 99th percentile, worst | Wall clock: mean, 99th percentile, worst |
|---|---|---|
| 0 (1) | 0.65 %, 1.1 to 1.2 %, 1.6 to 1.7 % | 0.83 to 0.85 %, 8.4 to 8.6 %, 49 % |
| 1 (2) | 0.64 %, 1.0 %, 1.7 to 1.8 % | 0.75 to 0.76 %, 1.6 to 1.8 %, 10 to 13 % |
| 2 (4) | 0.64 %, 1.0 %, 1.4 to 1.5 % | 0.78 %, 1.3 %, 16 % |
| 3 (8) | 0.64 to 0.65 %, 1.1 %, 1.5 to 1.7 % | 0.86 to 0.87 %, 8.2 to 8.3 %, 39 to 40 % |

The thread figures are the same on every core. The wall-clock figures depend on the core, and which
cores are busy changes: a few hours earlier in the same session, before the app was restarted, cores 1
and 3 were the busy ones and core 0 had the worst single blocks (device). A plugin with a worker
thread of its own adds to the gap, since pinning the harness puts that thread on the same core: one
effect plugin read 7.9 % at the 99th percentile by the thread clock and 19 % by the wall clock in
steady state (device). So judge a change by the thread CPU time, pin to one core so the comparison is
like for like, and treat a large gap between the two lines as a sign that the core is shared. The
worst block still varies from run to run, so compare several runs. About that kind of test:

- **Switching cost dominates.** Every mode switch flips many times a second, and each flip lands on the
  previous one's crossfade. A feature that is cheap when left alone can fail on what it costs to turn on.
- **The bench lands on resets often.** If it sets k of N parameters per block, each is picked in k/N of
  the blocks, and a switch with m positions changes on (m − 1)/m of its picks, for uniform random values
  (inferred). At 4 of 60, a two-position switch flips in 4/60 × 1/2 ≈ 3.3 % of blocks, and two such
  switches that each reset the plugin put a reset in about 6.5 % of blocks (inferred). A bench mode that
  sets every parameter on every block (the Open MPC bench's `-p` option) flips a two-position switch in half
  the blocks, so reset cost weighs far more there (documented, bench source). Either rate is far above
  the 1 % of blocks the 99th percentile leaves out, so a design that fades out and then resets turns its
  reset cost into the bench's high-percentile and worst-block figures. In one effect plugin, cheaper
  resets and rebuilds (section 8) lowered the sweep's worst block from 21.5, 19.7 and 21.1 % to 17.3,
  18.3 and 17.3 % in three runs, its mean from 6.7 to 6.5 % and its 99th percentile from 14.5 to 14.2 %,
  while the steady figures did not move (device).
- **The result is the sum of everything that can be on together.** A saving in any heavy stage counts.
- **Adding a parameter can move the result** with no change to the DSP, because the random sequence
  depends on the parameter count.

## 15. What changes on the 64-bit MPCs

The SDK describes the 64-bit models (MPC Live III, MPC XL and later) as Cortex-A55/A76 class and builds
for `cortex_a55`, the lowest common core (documented). Their chip is not otherwise described here, and
nothing below was timed on one: the items are documented or seen in compiler output. No cycle count in
this guide carries over.

Rules that reverse:

- NEON honors the FZ bit there (documented), so vector code no longer flushes denormals by itself. The
  host does not set FZ (documented by the SDK); the SDK framework does.
- Vector divide and square root exist (`vdivq_f32`, `vsqrtq_f32`).
- There are 32 vector registers. Only the low 64 bits of v8 to v15 are saved by the callee (documented).
- Clang vectorized the plain multiply-add loop from section 5 by itself and used the fused `fmla`
  (compiled, `-mcpu=cortex_a55`).
- Float reductions (sums) still need the loop pragma, reassociation flags or manual vectorization.
  Without the pragma, Clang loaded whole vectors but added the lanes one at a time, in order, so the sum
  gained nothing, although its vectorization remark said the loop was vectorized. With
  `#pragma clang loop vectorize(enable)` it kept vector accumulators, and a fused `fmla` for a dot
  product (compiled, `-mcpu=cortex_a55`). The remark alone is not proof: read the assembly.

Rules that carry over: no gathers, the cache budget, spreading work across blocks, no doubled stages,
compile-time stage switches and measuring the worst block. Budget for the Cortex-A55, the core the SDK
targets.

## 16. Known unknowns

These were not measured, so measure them before relying on them: the FPU mode the host sets on the
64-bit models, and on 32-bit models other than the MPC One+ (section 11); whether the MPC
compensates for, or re-reads, a plugin's reported latency; whether the L1 really is cold at the start of
each block; whether the other 32-bit models run at the same clock as the MPC One+ measured here; the cost of a well-predicted scalar float compare
whose result feeds only a branch (the 9 cycles in section 3 are a latency on a chain, and whether
`vmrs APSR_nzcv` stalls the floating-point pipeline is not known); instruction timings the
micro-benchmark did not cover; and everything about the 64-bit models' hardware.

## 17. Checklist

- [ ] No allocation, locks, logging or file access in `process()`; each parameter read once per block
      into a snapshot that all set-up code reads.
- [ ] No divides, square roots or transcendental functions per frame that could be per block.
- [ ] No `double` or library math calls in the frame loop, checked in the assembly, along with spills,
      stack reloads of member addresses and per-frame float tests.
- [ ] Stereo processed as one vector.
- [ ] NEON code has a portable fallback and passes the host simulator test.
- [ ] Steady-state data touched per block fits in about 24 KB.
- [ ] No table rebuild or stage start-up inside a single block; optional stages cost nothing when off.
- [ ] Resets, program changes and mode switches treated as rebuilds: only reachable state cleared,
      per-rate constants cached, unchanged selections not redesigned.
- [ ] Every change meant to be bit-identical proven on the arm32 build, with NaN and infinity in the
      test signal.
- [ ] Frame counts from 32 to 512 handled, including vector tails.
- [ ] FPU mode set or confirmed, so scalar and vector paths flush alike.
- [ ] Reported latency constant for the life of the instance.
- [ ] Instruments: worst block checked with every voice sounding.
- [ ] Worst block checked during knob sweeps and mode switches, at 32-frame and 128-frame blocks.
- [ ] Timed on the device, A and B alternating.
