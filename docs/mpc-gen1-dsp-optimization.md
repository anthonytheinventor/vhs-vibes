# Writing fast DSP for first-generation MPC hardware

A guide for plugin developers targeting the 32-bit standalone MPCs. These machines have a fraction of a
laptop's processing power, and a plugin shares one audio block with the user's whole project. Code that
is comfortable on a desktop can cause dropouts here.

Every figure below carries its origin:

- **(device)**: timed on an MPC One+, either while optimizing a real effect plugin or in a
  micro-benchmark of single instructions, pinned to one core and calibrated against a chain of integer
  adds (one cycle each), so time taken by other threads on that core cancels out.
- **(counted)**: executed instructions counted under an emulator while optimizing the same plugin.
- **(compiled)**: seen in assembly built with the SDK's own flags (Clang 22 as bundled with zig 0.17,
  arm32 `-mcpu=cortex_a17 -O2` unless stated).
- **(documented)**: stated by ARM, the chip's datasheet, the compiler's source or the SDK's docs.
- **(inferred)**: reasoning from the above that has not been measured.

"Frame" means one sample period at 44.1 kHz, all channels. "Sample" is used for the oversampled rate.

See also the SDK's `docs/host-contract.md`, `docs/dsp-cookbook.md`, `docs/building.md` and
`framework/mpc_plugin.h`. They cover the host's behavior and the framework this guide assumes.

## Biggest wins first

1. No divide, square root or transcendental function per frame that could run per block; reciprocal
   estimates for the divides that remain.
2. Stereo processed as one vector.
3. No `double` and no library math calls in the frame loop.
4. The data touched per block fits in the 32 KB L1 data cache.
5. No table rebuild, stage start-up or doubled stage inside one block.
6. Optional stages compiled out when they are off.
7. Only then the micro-tuning below.

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

**Targets.** Aim for a few percent of one core per instance on average. Keep the worst block of the whole
plugin under 15 %, the limit in the SDK's `dsp-cookbook.md`. Several instances multiply both figures.

## 2. Measure before you optimize

- **Count instructions for precision.** Build for ARM and run the plugin under an emulator that counts
  executed instructions (qemu with a counting plugin). The count is exact and repeatable, so it can prove
  that a change did not slow a path down, to a fraction of an instruction per frame.
- **Counts do not see time.** A divide is one instruction and 18 cycles (device). A table read that
  misses the cache is one instruction and up to about 200 cycles (device). So is a mispredicted branch,
  about 12 cycles (device). In practice:
  - Replacing 14 scalar divides per frame with reciprocal estimates added 28 instructions per frame
    (counted) and made the plugin faster on the device (device).
  - An 81-entry lookup table cost more on the device than its count suggested. A closed-form curve was
    faster (device).
  - Code that reads a lot of table memory got about 10 % fewer instructions done per cycle than code
    that computes (device, against counts).
- **The device has the last word.** Time candidates on the hardware, alternating A, B, A, B. Results
  drift by about half a percentage point between sessions (device), so two builds timed an hour apart
  cannot be compared.
- **Watch the worst block, not only the average.** A feature that costs 3 % on average still causes a
  dropout if one block in a thousand costs 40 %. Script the events that cost extra (a knob sweep, a mode
  switch, several controls at once) and look at the heaviest blocks.
- **Test control moves at the smallest block size.** Bugs in control handling hide at 128 frames and
  appear at 32.
- **Desktop timings are a first estimate.** The SDK's host simulator times the desktop build. The
  cookbook's rough factor to the A17 is 15 to 25 (documented).

**A timing recipe.** Time every `process()` call into a preallocated buffer, and write it out only after
processing has stopped.

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
  small sections. If you can, record the core frequency
  (`/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq`) from a non-audio thread.
- The MPC's own CPU meter is a coarse cross-check. Reading the ARM cycle counter from user space raised
  an illegal-instruction signal (device).

## 3. Float hygiene

- **Suffix every float literal.** `x * 0.1` with a float `x` became a convert to double, a double
  multiply and a convert back (compiled). `x * 0.5` and `x / 3.0` stayed single precision (compiled),
  since the compiler could prove the float result unchanged. Do not rely on that: write `0.1f`.
- **Keep `double` out of the frame loop.** NEON has no double lanes on ARMv7, and a double divide takes
  32 cycles, with a new one every 30, against 18 and 16 for float (device). `std::sin(x * 2.0 * pi)`
  with a float `x` called the double `sin` (compiled). Build with `-Wdouble-promotion` to see every
  promotion.
- **ARMv7 has no rounding instruction.** `floorf` and `lrintf` compiled to library calls, even with
  `-fno-math-errno` (compiled). Use a truncating convert with a correction:

  ```c
  static inline float32x4_t floor4(float32x4_t x)    // |x| below 2^31
  {
      float32x4_t t = vcvtq_f32_s32(vcvtq_s32_f32(x)); // truncate toward zero
      uint32x4_t up = vcgtq_f32(t, x);                 // truncation rounded up
      return vbslq_f32(up, vsubq_f32(t, vdupq_n_f32(1.0f)), t);
  }
  ```

- **ARMv7 has no scalar float min or max.** `fminf` compiled to a library call. `std::clamp` and
  `a < b ? a : b` compiled to `vcmp`, a flag transfer (`vmrs`) and a conditional move, once per bound
  (compiled). Branchless scalar code still pays the compare and transfer: each compare, transfer and
  conditional move took 9 cycles (device). Clamp in vectors with `vminq_f32` and `vmaxq_f32`: 5 cycles
  of latency, and a new four-lane one every 2 cycles (device).
- **`sqrtf` carries an error check.** With Clang's default `-fmath-errno`, `sqrtf(x)` compiled to
  `vsqrt.f32`, a compare and a branch to the library `sqrtf` (compiled). `-fno-math-errno` leaves the
  bare `vsqrt.f32`. The SDK build does not pass that flag; `__builtin_elementwise_sqrt(x)` also gave a
  bare `vsqrt.f32` (compiled).

## 4. Arithmetic

- **There is no vector divide and no vector square root** on ARMv7 NEON (documented). Writing `/` on a
  vector type compiled to four scalar `vdiv.f32` (compiled).
- **The divider handles one value at a time.** A float divide takes 18 cycles, and a new one can start
  only every 16 (device). A float square root takes about 17 with a full mantissa, less for simple
  values (9 for 1.0), and a new one starts every 15 (device). Many divides per frame queue up behind
  each other.
- **Use reciprocal estimates for bulk divides.** `vrecpeq_f32` followed by one Newton step
  (`vrecpsq_f32`) gives about 1.5e-5 relative error, which is inaudible in most audio math. Use
  `vrsqrteq_f32` and `vrsqrtsq_f32` for 1/sqrt. Take two Newton steps where the result feeds a small
  difference. One estimate plus one Newton step has a latency of 17 cycles, against 18 for one divide,
  but it handles four lanes and a new vector can start every 6 cycles; the four scalar divides that a
  vector `/` compiles to take 64 (device). That is about 10 times faster per vector. For 1/sqrt, the
  estimate and one step take 21 cycles and one vector per 8, against 56 for four square roots with
  divides, or 28 for four square roots alone (device). A single isolated divide can stay a plain divide.

  ```c
  static inline float32x4_t recip(float32x4_t x)   // x must not be zero or tiny
  {
      float32x4_t r = vrecpeq_f32(x);
      return vmulq_f32(r, vrecpsq_f32(x, r));      // one Newton step
  }
  ```

- **Guard the input of every estimate.** For the reciprocal, zero and denormal inputs give infinity,
  inputs above about 1e38 give 0, and negative inputs are fine. For the reciprocal square root, negative
  inputs give NaN. Zero and denormal inputs give infinity from the estimate alone, and NaN once the usual
  Newton step multiplies x by the estimate. Forming sqrt(x) as x times rsqrt(x) gives NaN at x = 0 too
  (all checked under an emulator). Clamp the input away from zero, and away from negatives for rsqrt,
  before the estimate.
- **Hoist divides out of the frame loop.** A coefficient that changes once per block is divided once per
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
  oversampled code). The gain is half the instructions. The arithmetic itself is no faster per lane: a
  four-lane float operation runs as two halves. A two-lane add or multiply can start every cycle, a
  four-lane one every 2 cycles; either way a NEON multiply takes 4 cycles and an add 5, against 6 and 5
  for scalar code (device). Do not add shuffles just to reach four lanes.
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
- **Clang never fuses a vector multiply-add on this target** (documented, LLVM source). A vector
  `a * b + c`, and also `vaddq_f32(vmulq_f32(a, b), c)`, compiled to one non-fused `vmla.f32`; `vfmaq_f32`
  compiled to one `vfma.f32` (compiled). The two run at the same speed: 4 cycles through the
  accumulator, 8 through the multiplicand, one four-lane op every 2 cycles (device). A separate `vmul`
  and `vadd` took 5 and 9 cycles and half the throughput (device). Write `vfmaq_f32` only for its
  rounding.
- **Clang does fuse scalar expressions.** A scalar `a * b + c` in one expression compiled to the fused
  `vfma.f32`. The same math split into two statements compiled to the non-fused `vmla.f32` (compiled).
  So a vector body and its scalar tail can round differently: the pragma-vectorized loop above used
  `vmla.f32` in its body and `vfma.f32` in its tail (compiled). Where a scalar path and a vector path
  must agree to the bit, write the rounding explicitly in both: `vfmaq_f32` with `std::fma` (which
  compiled to `vfma.f32`), or `vmla` math with the multiply and the add in separate statements.
- **Load with `vld1q_f32(ptr)`.** Casting a `float*` to a vector pointer lets the compiler assume
  alignment, and the load can fault.
- **Do not split a tight loop to "vectorize harder".** Splitting a 64-tap filter loop by channel made
  it slower: the compiler stopped unrolling it (counted). Try it, count it, keep what is faster.
- **Keep hot vectors in q0 to q3 and q8 to q15.** Registers q4 to q7 are saved by the callee, so using
  them costs a push and a pop (documented). A function call inside the loop spills every live scratch
  register.

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

- **32 KB of L1 data cache is the fast budget** for everything a block touches: delay lines, tables,
  filter state, the audio buffers themselves.
- **Count bytes touched per block.** A 2048-float table is 8 KB, a quarter of the L1. A 64-tap stereo
  FIR history is 512 bytes. One second of stereo float delay is about 345 KB, a third of the shared L2;
  only the lines a delay reads and writes in one block count toward that block (inferred).
- **Past it, reads hit the 1 MB L2,** which is shared with the other three cores and everything else in
  the project. A random read takes 4 cycles within 24 KB, about 23 at 64 KB, 32 to 44 at 256 to 512 KB
  and 130 to 200 (90 to 140 ns, TLB misses included) from main memory at 1 MB and beyond (device): an
  L2 hit costs 6 to 11 times an L1 hit. The hardware prefetcher follows sequential reads, so they stay
  cheap: per 64-byte line, about 7 cycles in L1, 14 to 18 in L2 and 22 at 4 to 16 MB (device).
- **Assume the L1 is cold at the start of each block.** The host and other plugins ran on the core in
  between (inferred). The MPC app pins a real-time audio worker to each of the four cores, so a plugin
  can run on any of them, and the app's own threads took about 8 to 14 % of each core while the
  micro-benchmark ran (device).
  Compact state pays off on every block.
- **Mind the 8 KB spacing.** The L1 is 4-way with 8 KB per way. At exactly 8 KB spacing up to four
  streams always fit; with five or more, an access rose from 4 cycles to about 9 to 26 in most
  placements, and in one placement only at six or eight streams (device). At 4 KB spacing eight streams
  usually fit and twelve never did (device). The placement dependence is consistent with a physically
  indexed cache, so the program cannot control conflicts exactly. **Offset parallel delay lines by one
  cache line:** at 8 KB + 64 bytes or 4 KB + 64 bytes, twelve streams never conflicted (device).
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

## 8. Blocks, controls and threads

- **Never rebuild in one block.** Recomputing a table or starting an expensive stage inside one block
  produces a spike. Split the work across several blocks, or run it at a fixed, short cadence while a
  control moves.
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
  if it has. Smooth the final coefficient only when that is cheaper than recomputing it.
- **Parameter writes can arrive during `process()`.** Changes from automation, project load and preset
  load can come from the UI thread while the block runs (documented). Read each parameter once per block
  from its atomic. The SDK framework stores them that way.
- **Keep the audio thread real-time.** No allocation, locks, logging or file access in `process()`.
  Audio threads on some products have a 1 MB stack (documented), so put large arrays on the heap,
  allocated in `prepare()`. Swap heavy state (tables, samples) in with a lock-free handoff.

## 9. Instruments and polyphony

- **The worst block is every voice sounding plus a burst of note-ons.** Cost per voice times the maximum
  voice count is the figure to budget.
- **Skip idle voices and steal without allocating.** Stop processing a voice once its envelope is done
  and its output is below a level gate (the SDK's `ADSR` has `isActive()`). Keep voice state in a fixed
  array (the SDK's `VoiceAllocator<N>`).
- **Voices can be lanes.** Four voices of mono state make one vector. This is an alternative to stereo
  as lanes, and it fills all four lanes (inferred).
- **Mind event splits.** Instruments get MIDI with sample-accurate offsets (documented). Splitting the
  block at each offset adds a loop start per event, which adds up when many events land in one block
  (inferred).

## 10. Code shape

- **Skip stages at compile time.** Make the frame loop a template with one flag per optional stage, and
  choose the right instantiation once per block. A stage that is off then costs nothing. Checking "is
  this stage on" from memory in every frame cost up to 19 instructions per frame (counted).
- **Watch inlining when a function gains a second caller.** Clang may stop inlining it, which slows a
  path you did not touch. Force the inline or give the new caller its own copy.
- **Member order affects speed.** `vldr` reaches only 1020 bytes from its base register and NEON `vld1`
  takes no offset at all, while integer `ldr` reaches 4095 bytes (documented). A float member at offset
  2000 needed an extra `add` before its `vldr`; one at 3208 needed a `movw` and an `add`; an `int` at
  2004 loaded directly (compiled). Keep hot float members in the first kilobyte of the object. Add new
  members at the end, so the offsets of hot fields do not move.
- **Link-time optimization changes inlining decisions.** Recheck the hot paths after turning it on or
  off.

## 11. Denormals and the FPU mode

- **NEON arithmetic always flushes denormals to zero** on 32-bit ARM, whatever the mode bit says
  (documented).
- **Scalar float code honors the FZ bit in FPSCR.** Do not assume its state. A fresh process starts
  with FZ clear, a new thread inherits FZ from the thread that created it, set or clear, `fork` keeps
  it and `exec` clears it (device). The host may have set it.
- **Only arithmetic flushes.** Loads, stores, moves and bit-selects pass a denormal through unchanged.
- **The mode on each platform.** On the 32-bit MPCs the host runs with flush-to-zero on; on the 64-bit
  MPCs it does not (both documented by the SDK, not observed here). The SDK framework's
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
  (filter and envelope tails) a tiny floor.
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
  float quotient's error near 4.0 was about 1000 times that near 0.1 (checked numerically).
- **Design the filter that follows with the anti-aliasing in the loop,** so its half-sample delay and
  high-frequency droop are corrected once.
- **Run level detectors at the rate their time constants assume.** A detector whose coefficients are set
  per sample behaves differently at twice the rate. Rescale its time constants for the oversampled rate,
  or feed it the base-rate signal.
- **Keep the reported latency constant.** The host reads latency once at load (documented). Do not let
  it depend on an oversampling switch. Compensate internally for paths that change it.

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

  In the hot functions, look for:
  - `vdiv.f32` and `vsqrt.f32` (divides and roots), and `vrecpe`/`vrsqrte` where you expected them;
  - any `.f64` instruction (double creep);
  - `bl` or `b` to `sinf`, `floorf`, `fminf`, `lrintf`, `sqrtf` or an `__aeabi_` helper (library calls);
  - `vcmp` followed by `vmrs APSR_nzcv` inside a loop (scalar compares);
  - `vmla.f32` against `vfma.f32` (rounding);
  - `vpush {d8` (callee-saved registers in use);
  - an `add` into a base register right before a `vldr` (a member offset out of reach).

## 14. Passing a CPU bench

Community catalogs may run a CPU bench on submitted plugins. A common design sets several randomly
chosen parameters to random values on every block, then passes or fails the plugin on two limits: one
for a high-percentile block and one for the worst block. Build the same test yourself: a host loop that
randomizes a few parameters per block, records every block's time and reports both figures. Things to
know about that kind of test:

- **Switching cost dominates.** Every mode switch flips many times a second, and each flip lands on the
  previous one's crossfade. A feature that is cheap when left alone can fail on what it costs to turn on.
- **The result is the sum of everything that can be on together.** A saving in any heavy stage counts.
- **Adding a parameter can move the result** with no change to the DSP, because the random sequence
  depends on the parameter count.

## 15. What changes on the 64-bit MPCs

The SDK describes the 64-bit models (MPC Live III, MPC XL and later) as Cortex-A55/A76 class and builds
for `cortex_a55`, the lowest common core (documented). Their chip is not otherwise described here.

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

These were not measured, so measure them before relying on them: the FPU mode the MPC host sets on its
audio threads (the SDK documents FZ on for the 32-bit models; not observed here); whether the MPC
compensates for, or re-reads, a plugin's reported latency; whether the L1 really is cold at the start of
each block; whether the other 32-bit models
run at the same clock as the MPC One+ measured here; instruction timings the micro-benchmark did not
cover; and everything about the 64-bit models' hardware.

## 17. Checklist

- [ ] No allocation, locks, logging or file access in `process()`; each parameter read once per block.
- [ ] No divides, square roots or transcendental functions per frame that could be per block.
- [ ] No `double` or library math calls in the frame loop, checked in the assembly.
- [ ] Stereo processed as one vector.
- [ ] NEON code has a portable fallback and passes the host simulator test.
- [ ] Steady-state data touched per block fits in 32 KB.
- [ ] No table rebuild or stage start-up inside a single block; optional stages cost nothing when off.
- [ ] Frame counts from 32 to 512 handled, including vector tails.
- [ ] FPU mode set or confirmed, so scalar and vector paths flush alike.
- [ ] Reported latency constant for the life of the instance.
- [ ] Instruments: worst block checked with every voice sounding.
- [ ] Worst block checked during knob sweeps and mode switches, at 32-frame and 128-frame blocks.
- [ ] Timed on the device, A and B alternating.
