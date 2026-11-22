# Writing fast DSP for first-generation MPC hardware

A guide for plugin developers targeting the 32-bit standalone MPCs. These machines have a fraction of a
laptop's processing power, and a plugin shares one audio block with the user's whole project. Code that
is comfortable on a desktop can cause dropouts here.

Every figure below carries its origin:

- **(measured)**: measured on an MPC One+ while optimizing a real effect plugin.
- **(documented)**: stated by ARM, the chip's datasheet or the compiler's source.
- **(model)**: from GCC's scheduling tables for this core. They are the best public per-instruction
  figures, but they are a model. ARM publishes no optimization guide for this core.
- **(inferred)**: reasoning from the above that has not been measured.

## 1. Know the machine

| | |
|---|---|
| Chip | Rockchip RK3288: four identical cores at 1.6 GHz (documented) |
| Core | Cortex-A17 (the silicon reports Cortex-A12; ARM renamed it, and the two perform the same) |
| Instruction set | 32-bit ARMv7-A with NEON and VFPv4, fused multiply-add, hardware integer divide |
| Pipeline | out of order, two instructions per cycle, about 11 stages |
| L1 cache | 32 KB data and 32 KB instruction per core, 4-way, 64-byte lines |
| L2 cache | 1 MB, shared by all four cores |
| Audio | 44.1 kHz, blocks of 32 to 512 frames |

Applies to the MPC Live, Live II, One, One+, X, Key 61 and Force.

**The budget.** A 128-frame block lasts 2902 µs, which is about 4.6 million processor cycles. One
percent of that block is about 46,000 cycles, or roughly 360 cycles per frame. A stereo effect that
spends 1,000 instructions per frame uses about 3 % of a core. Aim for a few percent per instance.

## 2. Measure before you optimize

- **Count instructions for precision.** Build for ARM and run the plugin under an emulator that counts
  executed instructions (qemu with a counting plugin). The count is exact and repeatable, so it can prove
  that a change did not slow a path down, to a fraction of an instruction per frame.
- **Counts do not see time.** A divide is one instruction and about 18 cycles. A table read that misses
  the cache is one instruction. So is a mispredicted branch. In practice (measured):
  - Replacing 14 scalar divides per frame with reciprocal estimates added 28 instructions per frame and
    made the plugin faster on the device.
  - An 81-entry lookup table cost more on the device than its count suggested. A closed-form curve was
    faster.
  - Code that reads a lot of table memory got about 10 % less work done per cycle than code that
    computes.
- **The device has the last word.** Time candidates on the hardware, alternating A, B, A, B. Results
  drift by about half a percentage point between sessions, so two builds timed an hour apart cannot be
  compared.
- **Watch the worst block, not the average.** A feature that costs 3 % on average still causes a dropout
  if one block in a thousand costs 40 %. Script the events that cost extra (a knob sweep, a mode switch,
  several controls at once) and look at the heaviest blocks.
- **Test control moves at the smallest block size.** Bugs in control handling hide at 128 frames and
  appear at 32.

## 3. Arithmetic

- **There is no vector divide and no vector square root** on ARMv7 NEON (documented). Writing `/` on a
  vector type compiles to one scalar divide per lane.
- **The divider handles one value at a time.** A float divide or square root takes about 18 cycles and
  blocks the divider for 10 (model). Many divides per frame queue up behind each other.
- **Use reciprocal estimates for bulk divides.** `vrecpeq_f32` followed by one Newton step
  (`vrecpsq_f32`) gives about 1.5e-5 relative error, which is inaudible in most audio math. Use
  `vrsqrteq_f32` and `vrsqrtsq_f32` for 1/sqrt. Take two Newton steps where the result feeds a small
  difference. The estimate takes slightly longer than one divide per result; it wins because it works on
  2 or 4 lanes at once in pipelined units. A single isolated divide can stay a plain divide.

  ```c
  static inline float32x4_t recip(float32x4_t x)   // x must be positive and not tiny
  {
      float32x4_t r = vrecpeq_f32(x);
      return vmulq_f32(r, vrecpsq_f32(x, r));      // one Newton step
  }
  ```

- **Guard the input** of every estimate: zero, negative and very large values give infinities or NaN.
- **Hoist divides out of the frame loop.** A coefficient that changes once per block is divided once per
  block.
- **No `exp`, `pow`, `tanh`, `sin` or `log` per frame.** Compute them when a parameter changes, or
  replace them with a polynomial or rational shape that behaves the same where it matters.
- **Avoid branches on float compares in the frame loop.** A scalar float compare takes about 9 cycles
  before its branch can resolve (model). Use `vminq_f32`, `vmaxq_f32` and `vbslq_f32`.
- **Prefer a closed form to a lookup table for a smooth curve.** A short formula stays in registers and
  vectorizes. A table costs index math, a clamp and memory reads.
- **Keep an exactly linear region if the curve has one.** If most frames fall where a saturator is a
  straight line, test once per vector and skip the curve.
- **Start an iterative solve from the previous frame's answer.** Audio moves smoothly, so two Newton
  steps usually suffice. Cap the step count so the cost is bounded.
- **Name the core in the build** (`-mcpu=cortex-a17`). Integer divide is then one hardware instruction
  of about 20 cycles. With a plain `-march=armv7-a` it becomes a library call.

## 4. NEON

- **Two channels are two lanes.** Treat a stereo frame as one vector everywhere. Running a mono path
  twice is the slowest way to process stereo.
- **Fill four lanes when the work is there** (two channels times two samples, for example in
  oversampled code). The gain is half the instructions. The arithmetic itself is no faster per lane: a
  four-lane float operation runs as two halves (model). Do not add shuffles just to reach four lanes.
- **There are no gathers.** A table read per lane is a scalar load per lane. Vectorize the index math
  and the interpolation around the reads.
- **Moving a lane into an integer register costs about 3 cycles** (model). Fetch two indices with one
  two-register move.
- **Write the vector code yourself.** Clang does not vectorize float loops on 32-bit ARM without
  fast-math flags, and GCC needs `-funsafe-math-optimizations`, because NEON's handling of denormals is
  not IEEE-compliant.
- **Use `vfmaq_f32` for a vector multiply-add.** The fused form is about 9 cycles against 11 for a
  multiply followed by an add (model), and compilers do not produce it from a vector expression on this
  target.
- **Fused multiply-add changes rounding.** The compiler fuses scalar expressions and leaves vector ones
  alone. If a scalar path and a vector path must agree to the bit, write the fused operation explicitly
  in both.
- **Load with `vld1q_f32(ptr)`.** Casting a `float*` to a vector pointer lets the compiler assume
  alignment, and the load can fault.
- **Do not split a tight loop to "vectorize harder".** Splitting a 64-tap filter loop by channel made
  it slower: the compiler stopped unrolling it (measured). Try it, count it, keep what is faster.
- **Keep hot vectors in q0 to q3 and q8 to q15.** Registers q4 to q7 are saved by the callee, so using
  them costs a push and a pop. A function call inside the loop spills every live scratch register.

## 5. Memory

- **32 KB of L1 data cache is the fast budget** for everything a block touches: delay lines, tables,
  filter state, the audio buffers themselves.
- **Past it, reads hit the 1 MB L2,** which is shared with the other three cores and everything else in
  the project. Slower, and less predictable.
- **Assume the L1 is cold at the start of each block.** The host and other plugins ran on the core in
  between (inferred). Compact state pays off on every block.
- **Mind the 8 KB spacing.** The L1 is 4-way with 8 KB per way, so buffers that sit a multiple of 8 KB
  apart compete for the same cache sets. More than four streams at that spacing evict each other.
  Offset parallel delay lines by a cache line or two (inferred from the documented geometry).
- **Align hot tables to 64 bytes** so a vector load never spans two cache lines.
- **Prefetch hints do not help** small tables or a filter's history. They are already in L1.
- **Precompute off the audio thread.** Build tables in the constructor or `prepare()`. A parameter change
  should blend or select ready-made data.
- **Read each row once.** When updating a table, blend and accumulate in a single pass, with running
  sums in registers.
- **Ship only what can be reached.** Table data that no control setting uses is binary size and cache
  pressure for nothing.

## 6. Spread the work across blocks

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

## 7. Code shape

- **Skip stages at compile time.** Make the frame loop a template with one flag per optional stage, and
  choose the right instantiation once per block. A stage that is off then costs nothing. Checking "is
  this stage on" from memory in every frame costs up to 19 instructions per frame (measured).
- **Watch inlining when a function gains a second caller.** The compiler may stop inlining it, which
  slows a path you did not touch. Force the inline or give the new caller its own copy.
- **Member order affects speed.** Adding fields in the middle of a class moves everything after them,
  and offsets that no longer fit a short encoding cost instructions on unrelated paths. Add new members
  at the end and keep hot fields together.
- **Link-time optimization changes inlining decisions.** Recheck the hot paths after turning it on or
  off.

## 8. Denormals

- **NEON arithmetic always flushes denormals to zero** on 32-bit ARM, whatever the mode bit says
  (documented).
- **Scalar float code does not,** unless the flush-to-zero bit is set, and Linux starts every thread
  with it clear.
- **Only arithmetic flushes.** Loads, stores, moves and bit-selects pass a denormal through unchanged.
- **For decaying scalar state** (filter and envelope tails), add a tiny floor or set flush-to-zero
  yourself.
- **Set the mode inside `process()` and restore it before returning.** It is thread state that belongs
  to the host.

  ```c
  unsigned old = __builtin_arm_get_fpscr();
  __builtin_arm_set_fpscr(old | (1u << 24));   // FZ
  /* ... process the block ... */
  __builtin_arm_set_fpscr(old);
  ```

- **`-ffast-math` does not set flush mode in a plugin.** The start-up object that does it is not linked
  into a shared library.
- **Desktop test builds flush nothing.** A bit-exact comparison between a desktop build and the device,
  or between scalar and NEON code, can differ for this reason alone.

## 9. Oversampling

- **2x oversampling with first-order antiderivative anti-aliasing** is the affordable standard on this
  hardware. Going to 4x roughly doubles a stage's cost.
- **The antiderivative method needs a memoryless curve.** For a stage with memory, restructure it so the
  nonlinear part has none, then anti-alias that part.
- **The antiderivative quotient loses precision** when consecutive inputs are close or values are large.
  Fall back to the curve itself below a threshold, and scale that threshold with the size of the input.
- **Design the filter that follows with the anti-aliasing in the loop,** so its half-sample delay and
  high-frequency droop are corrected once.
- **Detectors hear oversampled impulses differently.** One sample at twice the rate moves a peak
  detector more, and gives an RMS detector 3 dB more energy, than one sample at the base rate. Feed
  level detectors what they would see without oversampling.
- **Report the added latency** to the host.

## 10. Compiler notes

- **Build flags:** `-mcpu=cortex-a17 -mfpu=neon-vfpv4 -mfloat-abi=hard -O2`.
- **Clang has no scheduling model for this core** and treats it as a Cortex-A9. GCC does have one. Either
  way, measure instead of trusting the compiler's choices.
- **Generic vector types** (`__attribute__((vector_size(...)))` or a small wrapper class) work well and
  keep the code portable to the 64-bit models. Check the generated code for hidden scalar divides.

## 11. Passing a CPU bench

Community catalogues may run a CPU bench on submitted plugins. One in current use sets several randomly
chosen parameters to random values on every block and judges the 99th-percentile block and the worst
block. Things to know about that kind of test:

- **Switching cost dominates.** Every mode switch flips many times a second, and each flip lands on the
  previous one's crossfade. A feature that is cheap when left alone can fail on what it costs to turn on.
- **The result is the sum of everything that can be on together.** A saving in any heavy stage counts.
- **Adding a parameter can move the result** with no change to the DSP, because the random sequence
  depends on the parameter count.

## 12. What changes on the 64-bit MPCs

The newer models use an eight-core 64-bit ARM chip with four fast and four slow cores.

- NEON **keeps** denormals there unless the flush-to-zero bit is set.
- Vector divide and square root exist.
- There are 32 vector registers.
- The compiler fuses vector multiply-adds and vectorizes float loops by itself.
- Budget for the slow cores.

## 13. Checklist

- [ ] No allocation, locks, logging or file access in `process()`.
- [ ] No divides, square roots or transcendental functions per frame that could be per block.
- [ ] Stereo processed as one vector.
- [ ] Steady-state data touched per block fits in 32 KB.
- [ ] No table rebuild or stage start-up inside a single block.
- [ ] Optional stages cost nothing when off.
- [ ] Decaying scalar state cannot go denormal.
- [ ] Worst block checked during knob sweeps and mode switches, at 32-frame and 128-frame blocks.
- [ ] Timed on the device, A and B alternating.
