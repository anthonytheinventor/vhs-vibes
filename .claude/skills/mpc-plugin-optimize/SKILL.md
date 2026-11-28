---
name: mpc-plugin-optimize
description: Find and cut CPU cost in Akai MPC plugin DSP on the 32-bit (ARM Cortex-A17) models, with measured costs and scripts. Use when a plugin is too slow, drops out, or fails a CPU bench.
license: MIT
---

# Optimizing MPC plugin DSP for the 32-bit hardware

The 32-bit MPCs (MPC X, Live, Live II, One, One+, Key 61, Key 37, Force) run plugins on a Cortex-A17
at 1.6 GHz. One percent of an audio block is about 365 processor cycles per frame. This skill is a
measured method for making a plugin built with the MPC Plugin SDK cheaper on that core without
changing what it does. A plugin built another way (a port with its own build, several source files,
another compiler) can use the same method; "Ports and other builds" below says what changes.

Everything factual is in the guide, [docs/mpc-gen1-dsp-optimization.md](../../../docs/mpc-gen1-dsp-optimization.md),
called the reference below. Each figure there is tagged with where it came
from: (device), (counted), (compiled), (documented), (checked) or (inferred). Treat (inferred) items
as hypotheses to test. Read the section a step names before acting on it; do not work from memory of
other ARM cores, since several rules here differ from the usual advice.

## Before you start

Ask the owner, or find out from the project, and write the answers down:

1. **May the output change?** There are three answers; ask which applies.
   - **Same bits, always.** Only bit-exact changes qualify (reference: "Choosing what to change").
   - **Same bits unless I agree to a change.** The usual answer for a released plugin that projects
     depend on, and the one to assume when the owner cannot be asked. Bit-exact changes are made as
     they are found. A change that alters the output is measured and brought to the owner with its
     saving and the size of the difference (step 8), and is kept only on a yes to that change.
   - **The output may change.** A plugin that is not released yet. Still measure every difference and
     report it.
   Under every answer, a change that alters the output goes on the candidate list with its estimate,
   so the owner sees what is on offer. Never keep one without the owner's yes to that change.
2. **What is the complaint?** Dropouts and bench failures are worst-block problems. A high meter with
   several instances is a steady-cost problem. They have different fixes.
3. **What must never change?** Parameter ids, parameter order and the plugin's `uid` are permanent in
   any SDK plugin. Leave them alone.
4. **Which code may be edited?** Read the project's own rules (README, contributing notes, agent
   instructions) for code it keeps unmodified from another project. If the cost sits in such code,
   ask before editing it, and record what was changed in the way the project asks.

## The loop

Work in this order and keep a short log of every measurement.

**Say where you are.** The owner cannot see a run under the emulator, and some take an hour. Before
each step that runs longer than about two minutes, say which step of the loop this is, what is about
to run, and how long you expect it to take; start it in the background. A count of 1000 blocks of
an effect that takes 3 % of a block on the device runs in about half a minute; an instrument with
every voice sounding takes many times that. `count.sh` and `profile.sh`
keep `status.txt` in their output folder current with the block reached and the time left: read it
when asked, and say so when a run goes well past its estimate. Time one scenario before starting
many, so the estimates for the rest are real.
Keep `progress.md` in the work folder as well, from the first step: one line at the top for the
step you are on, what is running and its estimate, then one line per finished step with its key
figure ("baseline steady: 1102 per frame, 3.02 % on the device"). When you run as a background agent
the owner sees none of your messages, and this file is the only way they can follow the run. When
you start such an agent yourself, tell the owner where the file is and pass on each update.

**Two measures, two jobs.** Time on the device is the score: a change is a win only if it lowers the
time there. Instruction counts under the emulator are the tool for finding candidates and for catching
a regression to a fraction of an instruction. A plugin whose audio code depends on a timer thread
(meters pushed from a thread, say) gives counts that differ by a few dozen instructions in some
blocks from one run to the next; compare medians there, and treat differences that small as none.
Counts can mislead in both directions: removing
instructions the processor was already running side by side saves little time (in one plugin 15 %
fewer instructions changed the time by under 1 %), while removing a divide or shortening a chain of dependent
operations saves more than the count shows (reference section 2). When no device is available, say
so in the report and call every result a count, not a speedup.

1. **Baseline.** Keep an untouched copy of the source. Count instructions per frame in steady state
   and for each event that costs extra (a mode switch, a program change, a knob sweep), and scan the
   assembly. Use an events file to force each heavy mode on and count it in steady state too.
   Count idle as a scenario of its own (an effect with silent input, which `MPC_OPT_SILENT=1`
   gives, or an instrument with no notes): idle instances multiply in a project. Where the plugin
   compiles several copies of its sample loop, one per combination of stages, give each copy that
   users reach a scenario, or it has no row in the survey. An instrument makes no sound without notes, so give it
   scenarios with `chord` events: one voice, and every voice sounding with a `restrike`. Take the
   number of voices from the engine's code and its voice setting, not from the README: the limit the
   code allows is the one a project can reach. If a
   device is available, time the same scenarios there now (step 9 says how), since those are the
   figures the result is judged against. Record everything before changing anything.
2. **Read the source for cheap wins.** Before any profile, read every file that `process()` reaches,
   plus the parameter, program, reset and mode-switch code, against the checklist in reference
   section 17 and this list. Code that runs once at start-up or only draws the screen can be
   skimmed; say in the report what was read in full. These are mistakes that are quick to see in source, quick to fix, and
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
   at a time through steps 5 to 8 like any other change: a cheap win is still proven and measured.
3. **Survey the whole plugin before choosing anything.** The most expensive block or function is
   where the instructions are, which is not the same as where the waste is: it may already be tight,
   while cheap fixes sit elsewhere. So look everywhere first:
   - Profile steady state (`profile.sh` with `k` 0) and read the median column: where a typical block
     spends its instructions.
   - Profile several costly blocks from the random-parameter run in one call (the largest, and a few
     from around the 99th and 90th percentiles in `counts.txt`), and note whether they share a cause
     or are different mode combinations. Look for rebuilds and resets (sections 8 and 14).
   - Where one function holds most of a block because everything was inlined into it, the function
     profile says little. Run `lineprof.sh` on that scenario: it gives instructions per frame by
     source line, and the instruction mix. A large share of loads from the stack in a hot loop is
     a finding of its own (reference section 10).
   - Run `scan.sh` once and then `coverage.sh`, naming every scenario's output folder ("Scripts"
     below says how to lay the folders out). `coverage.md` lists every
     function that executed, with its instruction counts and its costly patterns (divides and square
     roots, double math, library calls, scalar compares, spills and reloads; reference section 13).
     Fill in the Verdict of **every** row: "candidate: what, about how many instructions per frame" or
     "read, nothing: why". A function with many instructions and few costly patterns is probably
     already tight; say so in its row. An empty row means part of the plugin was not looked at.
   - The candidates, from steps 2 and 3 together, are the list you choose from. For each note which
     scenario it helps (steady, high percentile, worst block), how often it runs, how much code it
     touches, and whether its saving is the kind the device will show (see "Two measures" above).
     Mark each one "same bits" or "changes the output" (the reference lists which kinds do, under
     "Choosing what to change"). Do not drop a candidate because it changes the output.
4. **Pick one change from the list by saving for the effort,** not by where the biggest number is.
   Do the cheap, certain ones first (a library call or divide that can move out of a loop, work
   repeated per sample that changes per block, a clear that covers more than it needs to), wherever
   they are. Among candidates of similar yield, follow "Choosing what to change" in the reference:
   the worst block, then the high percentile under moving controls, then the steady cost, with the
   owner's complaint deciding ties. Skip a candidate whose estimate is under about 2 % of the
   instructions per frame of the scenario it helps most (idle counts as one), unless it lowers the
   worst block. If a function
   resists two attempts, leave it and take the next candidate; come back only when the list is empty.
   Take the same-bits candidates first. A candidate that changes the output is worth trying when its
   estimate is about 5 % or more of the scenario it helps, or when it brings the worst block under
   the limit; below that, list it in the report with its estimate and leave it.
5. **Make that one change.** One change per measurement, so a result can be traced to its cause.
6. **Prove the output.** For a change meant to be bit-exact, run `bitcheck.sh` with the untouched
   source and the changed one. It must print IDENTICAL for arm32; agreement on the desktop build is
   not proof (section 2). The script first runs the old build twice. If it prints NOT REPEATABLE,
   the plugin's output depends on something outside the run (a trigger debounced against the clock,
   a thread, uninitialized memory): find it, or leave that parameter out with `MPC_OPT_SKIP`, before
   any comparison. Once per plugin, confirm the check can fail: change one constant in a copy and
   see DIFFERENT. If the plugin has behavior the standard run cannot reach (a mode only one hidden
   setting enables, a side input), add cases for it to a copy of `render.cpp` first. Then run the
   SDK's host simulator test, or the plugin's own tests where it is not an SDK plugin; they must
   still pass.
   The script also compares what the plugin shows and saves: every parameter's value text and unit,
   the program names, and the saved state, at fixed points of the run and after a state is loaded
   back. It must say IDENTICAL for those too, unless the change was meant to alter a read-out.
   IDENTICAL is as fine as the samples the plugin hands the host. An engine that computes in 16-bit
   integers gives float samples in whole steps of 1/32768, so the proof has 16-bit precision there,
   and a difference shows in whole steps (about -90 dB each). Check the engine's sample format and
   say which precision was proven.
   The proof holds for the compiler and flags it was run with. Where the release is built with
   another compiler, or with flags that let the compiler reorder float operations (`-ffast-math`,
   `-funsafe-math-optimizations`, `-fassociative-math`), prove it with that exact compiler version
   and those flags ("Ports and other builds"). Under such flags a hoist, a cache, even an `if` put
   around an unchanged loop can change the bits of the function it is in, so treat nothing as
   bit-exact until the check says so.
   For a change that alters the output, run `bitcheck.sh` with `MPC_OPT_HOLD=20`. It prints
   DIFFERENT and a table: for each phase the level of the output, the RMS and the peak of the
   difference in dB, and how far the difference is below the output. Record the largest peak, the
   smallest "diff below out", and the line counting samples where one build is not finite and the
   other disagrees, which must be 0. For scale, the rounding noise of a 16-bit file is about 98 dB
   below a full-scale sine, and of a 24-bit one about 146 dB. In the held phases compare the
   difference in the first and the last quarter: a figure that rises through the run means the error
   feeds back and grows, and the short phases understate it. The held programs must use the changed
   code. If the table says that no held phase reaches the difference, choose settings that do, with
   `MPC_OPT_HOLD_PROGRAMS` and `MPC_OPT_HOLD_SET`, and run it again; until then nothing is known
   about growth. A difference that sits in a few
   samples (a high peak with a low RMS and a small differing share) is a decision that flipped, such
   as a threshold or a mode switch landing one sample apart; find it before calling it small.
   One level cannot say what a difference sounds like, so the script prints a second table: for
   each phase, how far the difference is below the output under 2 kHz, from 2 to 8 kHz and above
   8 kHz, and the strongest narrow line of the difference. Read it for what the overall figure
   hides:
   - A line marked A TONE is a steady tone the change added or removed. A value computed once
     every N samples and held in between puts one at the sample rate divided by N and at its
     multiples (every 4 samples at 44.1 kHz: 11,025 Hz). Interpolate the value between updates, or
     raise its rate, and run the check again.
   - A band where the difference comes much closer to the output than it does overall is a hiss,
     a whistle or a loss of highs under a signal that is mostly low. Report the figure of the
     worst band beside the overall one.
   - The "bursts and silence" phase plays short sounds with silence between. A difference that is
     larger there than in the steady phases, or whose peak stands far above its RMS, is a click at
     an onset or a changed tail. Sustained material shows neither, so never judge an output change
     on sustained material alone.
   A change to an internal rate (an oversampling factor, a control rate, a stage run at a lower
   rate) also changes the meaning of every constant written per sample: filter coefficients, time
   constants and delays counted in samples, envelope and phase increments, and the level of any
   noise source, whose power is spread over a band that is now wider or narrower. Search the
   changed stage for each one and derive it again from the new rate. A missed one shows in the
   tables as a level, tuning or timing change.
   These figures size a change only while the two builds stay in step. A plugin with random or
   chaotic parts (noise, random modulation, voices that free themselves, triggers) turns one flipped
   decision into two unrelated signals, and a free-running oscillator does the same with no
   randomness at all, since the smallest change of pitch makes the two builds drift apart in phase: peaks near 0 dB and most samples differing, which says the
   builds parted and nothing about how large the change is. Read the "first diff" column there:
   the phases that stay identical, and how far into a phase the first difference comes, tell which
   settings reach the changed code. To size the change itself, compare over settings with the random
   parts off and oscillators held or reset, in a copy of `render.cpp`, or measure the changed
   function alone.
7. **Count again.** Compare with the baseline for the same scenario. Check the assembly for the
   pattern you meant to remove, and check that no other hot function grew (inlining can change,
   section 10). A change that does not lower the count for the scenario it was meant for is reverted
   here, unless it is the kind counts cannot see (a divide removed, a dependent chain split).
8. **Time it on the device** when one is available: the old and new builds of the scenario the
   change was meant for, alternating, three runs each (step 9 says how). Do this for each kept
   change, or for a batch of two or three small ones together; a long list of changes timed only at
   the end cannot be untangled. **Keep** a change the device shows as faster and that passed step 6.
   A change that lowers the count but not the time is not a win: keep it only if the code is no
   harder to read and nothing else got slower, and report it as "no measured gain". Revert anything
   that is slower, and note in the log that it was tried.
   A change that adds or enlarges a lookup table, or spreads state over more memory, needs a second
   test before it is kept. `drive` runs blocks back to back, so the table stays in the cache and
   the change looks good; where parameters jump or other work ran on the core in between, the table
   is read cold and the same change can lose (reference section 7). Time it with random parameters
   too (`drive 4`) and compare the worst block of the Open MPC bench, old against new. In one plugin
   such a change cut 13 % of the instructions of the worst block and 2.6 % of the steady time, and
   made that bench's result worse.
   **A change that alters the output is the owner's decision.** Keep it apart from the others (its
   own commit or patch), and once it is timed, stop and ask, with the figures side by side: the time
   saved on the device in the scenario it helps, the largest peak difference and the smallest
   "diff below out" from step 6, whether the difference grows in the held run, and in one sentence
   what in the signal changes (a detector that reacts a sample later, a gain off by one part in
   100,000). A small figure is strong evidence, not proof, that nobody will hear it: say so, and
   offer a build to listen to when the change sits in a feedback path or moves a decision. Where
   the plugin has a place for it, offer the third way too: put the change behind a new setting whose
   default keeps the old bits, so saved projects sound the same. Revert the change unless the answer
   is yes.
9. **How to time on the device.** Do not copy files to an MPC or restart its app without the
   owner's agreement. `devtime.sh` does it: give it the MPC's address and the old and the new
   `drive` program (`count.sh` leaves each as `<out>/drive`), with the same events file as the count
   to force the mode under test. It copies both to a new folder under `/tmp` on the MPC, runs them
   alternately pinned to one core, deletes the folder, and prints the thread CPU time of every run
   and the mean of each build. Give it the same build twice first to learn the run-to-run spread; a
   difference inside that spread is no difference. For a plugin that is, or will be, listed in the Open MPC catalog,
   finish the whole job with that catalog's own bench on the built `.so`, old and new:
   `openmpc-bench.sh` fetches the current copy and runs it on the device. Its verdict (PASS, WARN or
   FAIL) is the one that counts there.
10. **Stop** when the candidate list holds nothing above the 2 % rule. Reaching the limit with
   margin is a reason to check with the owner, not to stop early: cheap savings still on the list
   are worth taking, since several instances multiply them.

**When the list is empty and the plugin is still too slow.** Small changes have a floor. Two kinds
of larger step remain. Both go to the owner with an estimate before any code is written.

- **Build settings,** tried last, once the code changes are in, each one behind the bit check and
  timed on the device. A plugin made of several source files or libraries can gain from link-time
  optimization, which lets the compiler inline across files; an SDK plugin is one source file and
  gains nothing. Profile-guided optimization can gain a little more: build an instrumented harness,
  run the scenarios users reach, and rebuild with the profile. Either one changes how the release
  is built, and the profile run becomes a step of every release, so the owner decides.
- **A different structure.** Size each idea from the profile first: it can save at most the share
  of the block that the moved or skipped work takes now. Compute slow-moving values (a filter
  design, a shape, an envelope target) at a lower rate and interpolate between them. Move work
  that may arrive a few milliseconds late (analysis, table design) to a background thread that
  hands its results over without a lock, with lookahead where the audio has to wait for them.
  Skip a stage whose input and state are both silent. Lower the voice limit, or add a quality
  setting for the 32-bit models. Most of these change the output, the latency or what the plugin
  offers, and step 6 applies to each. A background thread's time still comes out of the same four
  cores the MPC app uses, so report it with the rest.

Report, for every scenario, the device time before and after (or say plainly that there was no
device), the instruction counts before and after, what was changed, what was tried and reverted, the
filled `coverage.md`, how bit-identity was proven, and which claims are measured and which are
reasoned. List every candidate that changes the output on its own: the saving (measured or
estimated), the size of the difference where it was measured, and the owner's answer or "not yet
asked".

## Scripts

They are in `scripts/` beside this file (in Claude Code, `${CLAUDE_SKILL_DIR}/scripts`). They need a
Linux shell (WSL on Windows) with `zig`, `qemu-arm` 8.1 or later, `nm`, `awk` and `c++filt`. If `zig`
is not on the path, set `ZIG="python3 -m ziglang"` with the SDK's zig environment active. When WSL
is started from Git Bash, put `MSYS_NO_PATHCONV=1` in front of `wsl`, or Git Bash rewrites the
`/mnt/c/...` paths.

**One output folder per scenario.** A scenario is one combination of `k`, block count and events
file: steady state, each heavy mode held on, the random-parameter run. The build and `counts.txt` in
a folder belong to one scenario, so give each its own folder (`out/steady`, `out/hq-on`, `out/k4`)
and pass that folder to `count.sh` and `profile.sh` for that scenario. `scan.sh` does not depend on
the scenario; run it once into any of them. `coverage.sh` takes all the folders at once. Scenarios in
separate folders can run at the same time, one emulator per processor core. Build once for all of
them: count the first scenario, then give the others `DRIVE_BIN=<first folder>/drive` (with `-` for
the two arguments the script then does not use), so nothing compiles while the others run. Start each as a
background job and wait for the jobs to exit (the shell's `wait`); do not write a loop that polls
for an output file, since a run that failed never writes it and the loop never ends.

| File | Use |
|---|---|
| `scripts/count.sh <sdk> <plugin.cpp> [k] [blocks] [out]` | Instructions per block for the arm32 build under the emulator. `k` is the number of parameters set to random values before each block (0 = steady state). Prints the median, the 90th and 99th percentiles per frame and the largest block; writes `counts.txt`. Runs 400 blocks in steady state and 1000 with `k` above 0; the first 200 are warm-up. The time grows with the plugin's cost: seconds for a light effect, minutes for an instrument with every voice sounding, so run a heavy scenario in the background. `MPC_OPT_THREADS=all` counts the plugin's other threads as well as the one that calls it. `status.txt` in the output folder says which block it has reached and about how long is left. Set `MPC_OPT_EVENTS` to an events file to force settings. |
| `scripts/profile.sh <sdk> <plugin.cpp> [k] [blocks] [out] [block ...]` | Attributes every instruction of the largest block (or of each block number given) to a function and prints the 15 largest, each with its share of the block and the difference from the median block. Name several costly blocks from `counts.txt` in one call: each call is one more full pass under the emulator. It reuses the build and counts from `count.sh` in the same `out` folder while `k`, the block count, the events file and the sources are unchanged, and saves the profile as `profile_k<k>.txt`, or `profile_k<k>_<events file name>.txt` with an events file. Inlined code counts under the function it was inlined into. The pass takes about as long as the count did and keeps `status.txt` current the same way. |
| `scripts/lineprof.sh <sdk> <plugin.cpp> [k] [blocks] [out] [block ...]` | Instructions per frame by source line, for the blocks named or every block from 200 on, and the instruction mix (loads from the stack, stores to it, each other kind). For a plugin whose cost sits in one large function. One more pass under the emulator. Needs an `addr2line` that reads 32-bit ARM programs (`llvm-addr2line`, or the one in `binutils-arm-linux-gnueabihf`). |
| `scripts/scan.sh <sdk> <plugin.cpp> [out]` | Compiles to arm32 assembly with the SDK's flags and prints the counts of the costly patterns for the 20 largest functions. Leaves `out.s` for reading and the counts for every function in `scan_all.txt`. |
| `scripts/coverage.sh [out ...]` | Joins the profiles and the scan in the folders named into `coverage.md` in the first: one row per function that executed, with an empty Verdict to fill in (step 3). With several scenarios, the two count columns hold the largest value over the scenarios; the per-scenario figures are in each folder's profile. |
| `scripts/bitcheck.sh <sdk> <old.cpp> <new.cpp> [out]` | Builds `render.cpp` around the old and the new source for arm32 (and arm64 when `qemu-aarch64` is installed), runs both under the emulator and compares the output byte for byte: every program, seven block sizes, random parameters, every parameter at once, many settled random settings, short bursts with silence between, and NaN, infinity, huge and denormal input. It runs the old build twice first and stops with NOT REPEATABLE if the two runs differ. Prints IDENTICAL, or DIFFERENT with the phase of the first difference and a table of how large the difference is (also saved as `bits/arm32-diff.txt`). `MPC_OPT_HOLD=<seconds>` adds long held programs, to show an error that grows; `MPC_OPT_HOLD_PROGRAMS` and `MPC_OPT_HOLD_SET` choose which programs and settings. It also compares what the plugin shows and saves (value texts, units, program names, the saved state, and the sound after a state is loaded back) and says IDENTICAL or DIFFERENT for those. Exit code 0, 1, or 2 when it could not run. |
| `scripts/outdiff.cpp` | The measuring program behind that table: per phase the output level, the RMS and peak of the difference in dB relative to full scale, how far the difference is below the output, and the share of samples that differ. A second table says where the difference sits: how far it is below the output in three bands, and its strongest narrow line, marked A TONE when the output has no line there. |
| `scripts/drive.cpp` | The harness behind `count.sh` and `profile.sh`: includes the plugin's source and drives it like a host, answering the host's time request (120 BPM, playing). An events file named in `MPC_OPT_EVENTS` acts at given blocks (block 0 sets the starting state): `<block> <parameter index or name> <value 0..1>`, `<block> program <n>`, and for instruments `<block> note <pitch>`, `off <pitch>`, `chord <count>`, `chordoff <count>` and `restrike <period>` (the last chord off and on again every so many blocks). That is how to hold a mode on, place a mode switch, or keep voices sounding. `drive list` prints the parameters and marks momentary ones; the random sweep leaves those out, and any named in `MPC_OPT_SKIP`. `MPC_OPT_MARK=all` counts and times parameter writes and events with the process call, as the Open MPC bench does. Run on a device it is a timing bench: `taskset 4 ./drive 0` for steady state, `taskset 4 ./drive 4` for four random parameters per block. It prints the thread's own CPU time, which leaves out other threads on the core, the CPU time of the whole process, which adds the plugin's own other threads, and the wall-clock time (reference section 14). On arm32 it sets flush-to-zero before calling the plugin, as the 32-bit MPC host does (reference section 11); `MPC_OPT_FZ=0` leaves it off. It hands the plugin the same buffers for input and output, as the MPC host does (`MPC_OPT_INPLACE=0` for separate ones), and `MPC_OPT_SILENT=1` feeds silence. |
| `scripts/devtime.sh <mpc-ip> <old drive> <new drive> [k] [blocks] [rounds]` | Times two `drive` programs on an MPC, alternating, from a folder under `/tmp` that it deletes afterwards, and prints each run and the mean of each build. Step 9. |
| `scripts/render.cpp` | The fixed, seeded run behind `bitcheck.sh`. It plays chords into an instrument through every phase, leaves momentary parameters and those named in `MPC_OPT_SKIP` alone, sets flush-to-zero as `drive.cpp` does, and writes the plugin's read-outs and saved state to a text file beside the audio. Copy it and add cases when the plugin has behavior the standard run cannot reach. |
| `scripts/vst2_min.h` | The few VST2 declarations the harness needs, used when it is built around a plugin that has no VST2 header of its own to give it ("Ports and other builds"). |
| `scripts/scan.awk` | The pattern counter behind `scan.sh`. |
| `scripts/tbcount.cpp` | The fast counter behind `count.sh`, `profile.sh` and `lineprof.sh`. The emulator runs whole translated blocks of code and logs one line for each; this program works out the instructions from the listing of each block, for ARM and Thumb code. The scripts build it once. `MPC_OPT_SLOW=1` uses the older method of one log line per instruction instead: the same counts, several times slower. |
| `scripts/openmpc-bench.sh <plugin-arm32.so> [device-ip] [bench args]` | Fetches the current CPU bench of the Open MPC catalog (`tools/bench.c` from [open-mpc/mpc-vst-plugins](https://github.com/open-mpc/mpc-vst-plugins), MIT license), builds it unchanged for the 32-bit models, and with a device address runs it there on the built `.so`: both files go to a new folder under `/tmp` on the MPC, which is deleted afterwards. `BENCH_DATA=<folder>` copies that folder's contents beside the plugin, for one that loads banks or other files from its own folder. It runs on core 1, as the catalog's own launcher does on these models. Without an address it only fetches and builds. `-p` sweeps every parameter, `-j` adds a JSON line. The rules it applies are in the `BENCH.md` it fetches; read that file for the current limits, since the catalog can change them. Needs `curl`, and `ssh` and `scp` for a device run. |

`<plugin.cpp>` is the one source file that ends with the SDK's entry macro. The harness needs the
standard entry point (`VSTPluginMain`), which every SDK plugin has.

## Ports and other builds

The scripts build an SDK plugin themselves: one source file, the SDK's compiler and flags. For a
plugin built any other way, you build the harness and hand the result to the scripts.

- **Match the release build exactly.** Counts, the assembly scan and above all the bit check are
  only true for the compiler version and flags that build the shipped binary. Read them from the
  project's build files and use the same ones. Look in the project's CI files as well: a release
  made there may pin a toolchain image, or a commit of a wrapper or build repository, that the
  build files in the checkout do not name. Check what the toolchain adds without being asked:
  some distributions turn on stack protection and fortified library calls by default, which puts
  code in the counts that the release does not have (`-fno-stack-protector -U_FORTIFY_SOURCE` takes
  it out). If the exact compiler version is not available, say in the report that bit-identity is
  not proven for the release.
- **Read the real target.** The reference is written for the SDK's build: ARM instructions, VFPv4,
  NEON and a hardware integer divide. Run `readelf -A` on the shipped arm32 binary and read the
  first lines of its assembly (`.arch`, `.fpu`, `.thumb`). Without `Tag_Advanced_SIMD_arch` there
  is no NEON, so sections 5 and 6 do not apply and nothing flushes denormals except the FZ bit.
  Without `Tag_DIV_use` every integer divide and remainder is a library call (`__aeabi_idiv` and
  its relatives, which the scan counts as library calls). An FPU named `-d16` has 16 double
  registers, not 32. Section 13's notes on what the compiler does describe Clang; for another
  compiler, read its assembly before assuming any of them. The cycle costs of section 1 belong to
  the processor and hold either way.
- **Build the harness around the plugin.** Compile `drive.cpp` and `render.cpp` without `SRC` and
  link them statically for arm32 with the plugin's own objects, which must provide
  `VSTPluginMain`. Add `-DMPC_OPT_ABI='"<the plugin's VST2 header>"'` to use the plugin's header;
  without it the harness uses its own `vst2_min.h`. Name the results in `DRIVE_BIN` (for
  `count.sh` and `profile.sh`) and in `RENDER_OLD` and `RENDER_NEW` (for `bitcheck.sh`), and give
  `-` for the arguments those scripts then do not use. For `scan.sh`, write the assembly with the
  release's compiler and name the file in `SCAN_ASM`. Thumb builds work.
- **An engine in another language.** The harness needs `VSTPluginMain` from the plugin and nothing
  else, so an engine written in Rust, C or generated code links in as its static library beside the
  wrapper's objects, built with the release's own target and optimization settings. A harness that
  the build links as a position-independent executable works too: the fast counter reads from the
  emulator where the program was loaded (`MPC_OPT_SLOW=1` cannot follow one).
- **Put the plugin's data files where it looks.** A plugin that loads banks, tables or samples
  from its own folder needs them where it will look from the program the scripts run:
  `<out>/drive` for `count.sh` and `profile.sh`, `<out>/bits/render-arm32-old` and `-new` for
  `bitcheck.sh`. A plugin that finds nothing usually runs on
  silently with an empty state, and every figure is then for the wrong plugin, so check before any
  measurement that `drive list` shows the real programs and values. Under the emulator, a 32-bit
  program linked against an older C library cannot list a folder on the Linux disk or on a Windows
  drive (the call fails with "value too large", checked), and the plugin sees an empty folder. Put
  the data on a memory file system (`/dev/shm`) and link or copy it into place. That folder is
  emptied when WSL shuts down, so copy the data in at the start of each script.
- **Flush-to-zero.** The harness sets it on arm32, as the 32-bit MPC host does. A wrapper that sets
  the mode itself overrides that, as it would on the device. Check that the release's wrapper and
  the one you build against do the same thing here.
- **A plugin that works on a thread of its own.** Some engines render on their own thread and only
  hand finished audio to the process call, so the counts and the thread's CPU time show almost
  nothing. `drive` prints a "process CPU time" line beside the thread's own: where it is well above,
  the work is on another thread. Count with `MPC_OPT_THREADS=all` and time with `DEV_LINE=process`
  in `devtime.sh`, and say in the report that the figures cover every thread. The emulator runs
  threads at a different pace from the device, so an engine that renders ahead of the process call
  may count unevenly from block to block: use long runs and means there. The Open MPC bench times
  the thread that calls the plugin and shows the plugin's other threads in a column of their own
  (`threads%`), which its verdict leaves out. That time is still taken from the device, so report
  it beside the verdict. With such threads the bench's `peak` column, the output level, can differ
  between two runs of one build, so it cannot show that two builds sound the same; `bitcheck.sh`
  is the proof.
- **Count what the host block costs.** In some wrappers a parameter write does real work at once
  (formatting text, searching a table). Set `MPC_OPT_MARK=all` so the counts and times include it.
  A wrapper that reads the clock in that path can make the same block differ between two passes
  (by about 500 instructions in one port); treat counts that close as equal.
- **A second opinion.** `bitcheck.sh` checks arm64 only for SDK builds. For a port, run it again
  with programs built by a second compiler into another output folder; a change that is exact
  under both is less likely to rest on one compiler's choices.
- The scan's spill and reload columns come from comments only Clang writes; they read 0 for other
  compilers.

## Rules that hold throughout

- No allocation, locks, logging or file access in `process()`.
- Never rebuild a table or start an expensive stage inside one block; never run two copies of a stage
  to crossfade (section 8).
- Write vector code yourself: Clang does not vectorize float loops on this target with the SDK's
  flags (section 5). Every NEON path needs a fallback that builds for the desktop test (section 6).
- Do not add `-ffast-math`, `-ffp-contract=fast` or `-fno-math-errno` to an existing plugin: each can
  change its output bits (sections 3 and 5).
- A reciprocal or square-root estimate changes the output bits (section 4), as does every kind of
  change the reference lists with it. Each is measured and put to the owner (steps 6 and 8).
- Figures in the reference given as "in one plugin" show scale. They are not targets.
- The reference covers the 32-bit models. For the 64-bit models only section 15 applies, and no cycle
  count carries over.
