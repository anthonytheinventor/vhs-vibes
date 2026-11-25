---
name: mpc-plugin-optimize
description: Find and cut CPU cost in Akai MPC plugin DSP on the 32-bit (ARM Cortex-A17) models, with measured costs and scripts. Use when a plugin is too slow, drops out, or fails a CPU bench.
license: MIT
---

# Optimizing MPC plugin DSP for the 32-bit hardware

The 32-bit MPCs (MPC X, Live, Live II, One, One+, Key 61, Key 37, Force) run plugins on a Cortex-A17
at 1.6 GHz. One percent of an audio block is about 365 processor cycles per frame. This skill is a
measured method for making a plugin built with the MPC Plugin SDK cheaper on that core without
changing what it does.

Everything factual is in the guide, [docs/mpc-gen1-dsp-optimization.md](../../../docs/mpc-gen1-dsp-optimization.md),
called the reference below. Each figure there is tagged with where it came
from: (device), (counted), (compiled), (documented), (checked) or (inferred). Treat (inferred) items
as hypotheses to test. Read the section a step names before acting on it; do not work from memory of
other ARM cores, since several rules here differ from the usual advice.

## Before you start

Ask the owner, or find out from the project, and write the answers down:

1. **May the output change?** If the plugin is released and projects depend on it, the usual answer is
   that existing settings must produce the same bits. That limits you to bit-exact changes (reference:
   "Choosing what to change"). Never trade away bit-identity without being told to.
2. **What is the complaint?** Dropouts and bench failures are worst-block problems. A high meter with
   several instances is a steady-cost problem. They have different fixes.
3. **What must never change?** Parameter ids, parameter order and the plugin's `uid` are permanent in
   any SDK plugin. Leave them alone.

## The loop

Work in this order and keep a short log of every measurement.

1. **Baseline.** Keep an untouched copy of the source. Count instructions per frame in steady state
   and for each event that costs extra (a mode switch, a program change, a knob sweep), and scan the
   assembly. Use the scripts below. Record the numbers before changing anything.
2. **Read the source for cheap wins.** Before any profile, read every file that `process()` reaches,
   plus the parameter, program, reset and mode-switch code, against the checklist in reference
   section 17 and this list. These are mistakes that are quick to see in source, quick to fix, and
   easy to miss in a profile because each hides inside a larger function or runs only on an event:
   - a parameter read with `get(i)` per sample or per block when it has not changed (section 8);
   - `pow`, `exp`, `log`, `sin`, `tanh` or `sqrt` inside the sample loop, or recomputed every block
     from values that only change with a parameter (sections 3 and 4);
   - a divide whose divisor does not change inside the loop (section 4);
   - double literals or double functions that drag float math into double (`1.0` for `1.0f`,
     `std::pow` on a float) (section 3);
   - `floor`, `round` or `lround` calls where a conversion would do (section 4);
   - a whole buffer or state block cleared on reset or a mode switch when only part can be read
     back (sections 7 and 8);
   - a table or filter redesigned on any parameter change, where only one parameter feeds it;
   - a per-sample test of something that only changes per block or per control tick (section 10);
   - a small function called from the sample loop that is not inlined, or a virtual or
     `std::function` call there (section 10);
   - scalar code beside vector code doing the same work on the other channel (section 5).
   Put every hit on the candidate list of step 3 with its estimate. Take the clear ones first, one
   at a time through steps 5 to 8 like any other change: a cheap win is still proven and counted.
3. **Survey the whole plugin before choosing anything.** The most expensive block or function is
   where the instructions are, which is not the same as where the waste is: it may already be tight,
   while cheap fixes sit elsewhere. So look everywhere first:
   - Profile steady state (`profile.sh` with `k` 0) and read the median column: where a typical block
     spends its instructions.
   - Profile several costly blocks from the random-parameter run in one call (the largest, and a few
     from around the 99th and 90th percentiles in `counts.txt`), and note whether they share a cause
     or are different mode combinations. Look for rebuilds and resets (sections 8 and 14).
   - Read the scan for every function that showed up in either profile: divides and square roots,
     double math, library calls, scalar compares, spills and reloads (reference section 13). A
     function with many instructions and few of these patterns is probably already tight.
   - Add to the candidate list started in step 2. For each: the function, the pattern, which scenario it helps (steady,
     high percentile, worst block), the estimated saving in instructions per frame from the cost
     table in section 1 and the profile figures, how often it runs, and how much code it touches.
4. **Pick one change from the list by saving for the effort,** not by where the biggest number is.
   Do the cheap, certain ones first (a library call or divide that can move out of a loop, work
   repeated per sample that changes per block, a clear that covers more than it needs to), wherever
   they are. Among candidates of similar yield, follow "Choosing what to change" in the reference:
   the worst block, then the high percentile under moving controls, then the steady cost, with the
   owner's complaint deciding ties. Skip a candidate whose estimate is under about 2 % of the
   instructions per frame of the scenario it helps, unless it lowers the worst block. If a function
   resists two attempts, leave it and take the next candidate; come back only when the list is empty.
5. **Make that one change.** One change per measurement, so a result can be traced to its cause.
6. **Prove the output.** For a change meant to be bit-exact, compare the output of the old and new
   arm32 builds under the emulator, with NaN and infinity in the test signal (section 2). Agreement
   on the desktop build is not proof. Then run the SDK's host simulator test; it must still pass.
7. **Count again.** Compare with the baseline for the same scenario. Check the assembly for the
   pattern you meant to remove, and check that no other hot function grew (inlining can change,
   section 10).
8. **Keep or revert.** Keep a change that lowers the count or the worst block and passes step 6.
   Revert anything else, and note in the log that it was tried.
9. **Confirm on the device** when one is available, old and new builds alternating (section 2).
   Instruction counts do not see divide time, cache misses or mispredicted branches, so the device has
   the last word. Do not copy files to an MPC or restart its app without the owner's agreement.
   For a plugin that is, or will be, listed in the Open MPC catalog, finish with that catalog's own
   bench: `scripts/openmpc-bench.sh` fetches the current copy and runs it on the device. Its verdict
   (PASS, WARN or FAIL) is the one that counts there; report it with the figures for old and new.
10. **Stop** when the candidate list holds nothing above the 2 % rule. Reaching the limit with
   margin is a reason to check with the owner, not to stop early: cheap savings still on the list
   are worth taking, since several instances multiply them.

Report the before and after numbers for every scenario, what was changed, what was tried and
reverted, and which claims are measured and which are reasoned.

## Scripts

They are in `scripts/` beside this file (in Claude Code, `${CLAUDE_SKILL_DIR}/scripts`). They need a
Linux shell (WSL on Windows) with `zig`, `qemu-arm` 8.1 or later, `nm`, `awk` and `c++filt`. If `zig`
is not on the path, set `ZIG="python3 -m ziglang"` with the SDK's zig environment active.

| File | Use |
|---|---|
| `scripts/count.sh <sdk> <plugin.cpp> [k] [blocks] [out]` | Instructions per block for the arm32 build under the emulator. `k` is the number of parameters set to random values before each block (0 = steady state). Prints the median, the 90th and 99th percentiles per frame and the largest block; writes `counts.txt`. Runs 400 blocks in steady state and 1000 with `k` above 0; the first 200 are warm-up. With 1000 blocks a heavy plugin takes several minutes, so run it in the background. |
| `scripts/profile.sh <sdk> <plugin.cpp> [k] [blocks] [out] [block ...]` | Attributes every instruction of the largest block (or of each block number given) to a function and prints the 15 largest, each with its share of the block and the difference from the median block. Name several costly blocks from `counts.txt` in one call: each call is one more full pass under the emulator. It reuses the build and counts from `count.sh` in the same `out` folder while `k`, the block count and the sources are unchanged. Inlined code counts under the function it was inlined into. |
| `scripts/scan.sh <sdk> <plugin.cpp> [out]` | Compiles to arm32 assembly with the SDK's flags and prints, per function, the counts of the costly patterns. Leaves `out.s` for reading. |
| `scripts/drive.cpp` | The harness both use: includes the plugin's source and drives it like a host. Copy it and add your own events (a program change at block 300, a mode switch) inside the block loop to measure them. To compare two builds bit for bit, write `oL` and `oR` to a file and compare the files. Run on a device, the same program is a random-parameter CPU bench: `taskset 4 ./drive 4`. Try each core first and use the quietest (reference section 14). |
| `scripts/scan.awk` | The pattern counter behind `scan.sh`. |
| `scripts/openmpc-bench.sh <plugin-arm32.so> [device-ip] [bench args]` | Fetches the current CPU bench of the Open MPC catalog (`tools/bench.c` from [open-mpc/mpc-vst-plugins](https://github.com/open-mpc/mpc-vst-plugins), MIT license), builds it unchanged for the 32-bit models, and with a device address runs it there on the built `.so`: both files go to `/tmp` on the MPC and are deleted afterwards. Without an address it only fetches and builds. `-p` sweeps every parameter, `-j` adds a JSON line. The rules it applies are in the `BENCH.md` it fetches; read that file for the current limits, since the catalog can change them. Needs `curl`, and `ssh` and `scp` for a device run. |

`<plugin.cpp>` is the one source file that ends with the SDK's entry macro. The harness needs the
standard entry point (`VSTPluginMain`), which every SDK plugin has.

## Rules that hold throughout

- No allocation, locks, logging or file access in `process()`.
- Never rebuild a table or start an expensive stage inside one block; never run two copies of a stage
  to crossfade (section 8).
- Write vector code yourself: Clang does not vectorize float loops on this target with the SDK's
  flags (section 5). Every NEON path needs a fallback that builds for the desktop test (section 6).
- Do not add `-ffast-math`, `-ffp-contract=fast` or `-fno-math-errno` to an existing plugin: each can
  change its output bits (sections 3 and 5).
- A reciprocal or square-root estimate changes the output bits (section 4). Use it only where the owner's
  answer to the first question allows.
- Figures in the reference given as "in one plugin" show scale. They are not targets.
- The reference covers the 32-bit models. For the 64-bit models only section 15 applies, and no cycle
  count carries over.
