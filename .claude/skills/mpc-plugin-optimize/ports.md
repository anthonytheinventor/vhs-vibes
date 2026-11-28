# Ports and other builds

Read this before measuring a plugin that the MPC Plugin SDK's tools do not build: a port with its own
build system, several source files, an engine in another language, another compiler. It belongs to
[SKILL.md](SKILL.md), whose loop and rules apply unchanged; "section n" means that section of
the guide, [docs/mpc-gen1-dsp-optimization.md](../../../docs/mpc-gen1-dsp-optimization.md).

The scripts build an SDK plugin themselves: one source file, the SDK's compiler and flags. For any
other plugin, you build the harness and hand the result to the scripts.

- **Match the release build exactly.** Counts, the scan and above all the bit check are only true
  for the compiler version and flags that build the shipped binary. Read them from the project's
  build files, and from its CI files too: a release made there may pin a toolchain image, or a
  commit of a wrapper or build repository, that the checkout's build files do not name. Check what
  the toolchain adds unasked: some distributions turn on stack protection and fortified library
  calls by default, which puts code in the counts that the release lacks
  (`-fno-stack-protector -U_FORTIFY_SOURCE` takes it out). Without the exact compiler version, say
  in the report that bit-identity is not proven for the release.
- **Read the real target.** The reference assumes the SDK's build: ARM instructions, VFPv4, NEON
  and a hardware integer divide. Run `readelf -A` on the shipped arm32 binary and read the first
  lines of its assembly (`.arch`, `.fpu`, `.thumb`). Without `Tag_Advanced_SIMD_arch` there is no
  NEON, so sections 5 and 6 do not apply and only the FZ bit flushes denormals. Without
  `Tag_DIV_use` every integer divide and remainder is a library call (`__aeabi_idiv` and its
  relatives, which the scan counts as library calls). An FPU named `-d16` has 16 double registers,
  not 32. Section 13 describes what Clang does; for another compiler, read its assembly before
  assuming any of it. The scan's spill and reload columns come from comments only Clang writes and
  read 0 for other compilers. The cycle costs of section 1 belong to the processor and hold either
  way.
- **Build the harness around the plugin.** Compile `drive.cpp` and `render.cpp` without `SRC` and
  link them statically for arm32 with the plugin's own objects, which must provide
  `VSTPluginMain`. Add `-DMPC_OPT_ABI='"<the plugin's VST2 header>"'` to use the plugin's header;
  without it the harness uses its own `vst2_min.h`. Name the results in `DRIVE_BIN` (for
  `count.sh` and `profile.sh`) and in `RENDER_OLD` and `RENDER_NEW` (for `bitcheck.sh`), with `-`
  for the arguments those scripts then do not use. For `scan.sh`, write the assembly with the
  release's compiler and name the file in `SCAN_ASM`. Thumb builds work.
- **An engine in another language.** The harness needs only `VSTPluginMain` from the plugin, so an
  engine written in Rust, C or generated code links in as its static library beside the wrapper's
  objects, built with the release's own target and optimization settings. A harness linked as a
  static position-independent executable works too: the fast counter reads from the emulator where
  the program was loaded (`MPC_OPT_SLOW=1` cannot follow one).
- **Put the plugin's data files where it looks.** A plugin that loads banks, tables or samples
  from its own folder needs them beside the program the scripts run: `<out>/drive` for `count.sh`
  and `profile.sh`, `<out>/bits/render-arm32-old` and `-new` for `bitcheck.sh`. A plugin that finds
  nothing usually runs on silently with an empty state, and every figure is then for the wrong
  plugin, so check before any measurement that `drive list` shows the real programs and values.
  Under the emulator, a 32-bit program linked against an older C library cannot list a folder on
  the Linux disk or a Windows drive (the call fails with "value too large", checked), and the
  plugin sees an empty folder. Put the data on a memory file system (`/dev/shm`) and link or copy
  it into place. WSL empties that folder when it shuts down, so copy the data in at the start of
  each script.
- **Flush-to-zero.** The harness sets it on arm32, as the 32-bit MPC host does. A wrapper that sets
  the mode itself overrides that, as it would on the device. Check that the release's wrapper and
  the one you build against do the same thing here.
- **A plugin that works on a thread of its own.** Some engines render on their own thread and only
  hand finished audio to the process call, so the counts and the thread's CPU time show almost
  nothing. Where `drive`'s "process CPU time" line is well above the thread's own, the work is on
  another thread. Count with `MPC_OPT_THREADS=all`, time with `DEV_LINE=process` in `devtime.sh`,
  and say in the report that the figures cover every thread. The emulator runs threads at a
  different pace from the device, so an engine that renders ahead of the process call may count
  unevenly from block to block: use long runs and means. The Open MPC bench times the calling
  thread and shows the plugin's other threads in their own column (`threads%`), outside its
  verdict; that time is still taken from the device, so report it beside the verdict. With such
  threads the bench's `peak` column, the output level, can differ between two runs of one build,
  so it cannot show that two builds sound the same; `bitcheck.sh` is the proof.
- **Count what the host block costs.** In some wrappers a parameter write does real work at once
  (formatting text, searching a table). Set `MPC_OPT_MARK=all` so the counts and times include it.
  A wrapper that reads the clock in that path can make the same block differ between two passes
  (by about 500 instructions in one port); treat counts that close as equal.
- **A second opinion.** `bitcheck.sh` checks arm64 only for SDK builds. For a port, run it again
  with programs built by a second compiler into another output folder; a change that is exact
  under both is less likely to rest on one compiler's choices.
