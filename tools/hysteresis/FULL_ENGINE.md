# The Full hysteresis engine (removed in 1.8, kept here for the record)

Hysteresis shipped in 1.8 as one switch, Off / On. During development it had three positions,
Off / Lite / Full. On is the engine that was called Lite. This note says what Full was, why it was
taken out, and how to bring it back.

## Where the working code is

The last commit with both engines is `c47ee86` on `dev-1.8` (the feature arrived in `ab40681`, the
cheaper control moves and the CPU bars in `07755ee`). Everything needed is in that commit:

- `src/hysteresis.h`: `hys::Engine` with both table reads, `hys::Stage` with two engines for
  crossfades.
- `src/hysteresis_tables.h`: the full planes (20 bias planes x 2 speeds, 1.97 MB of floats).
- `tools/hysteresis/`: the generator and the prototype (`rt2.h` has both engines, called FAST and
  SLOPE there).
- `tests/hyst_test.cpp`: the checks for both.

To look at it: `git show c47ee86:src/hysteresis.h`. To build it: `git worktree add ../vv-full c47ee86`.

`HYSTERESIS_SPEC.md` beside this file is the specification both engines were built from: equations,
signal flow, tables, calibration, latency, accuracy and test figures.

## What the two engines share

Both are the same model: a Preisach description of AC-biased recording in the moving field of the
record head. Each bit of tape arrives erased, crosses the head once and freezes. The bias is treated
as a fading envelope, never as a carrier. A brute-force simulation of that process with the real
bias carrier and 12 depths of coating is the ground truth; its results are stored in tables, one
plane per bias setting and speed, merged into 3 depth groups for playback.

Both run at twice the session rate with first-order antiderivative anti-aliasing along the field
axis, both use the same playback filters and the same level-dependent high-frequency correction,
and both have the same latency (41.5 samples at 44.1 kHz).

## What Full adds

Lite (now On) assumes the signal holds still while a bit of tape freezes. Its table has one axis:
the record field. That is exact for slow signals.

Full adds a second axis: the local slope of the signal, taken as a ramp through the head field.
The table is read at (field, slope), so the result depends on how fast the signal is moving as the
tape leaves the head. Lite's table is the middle row of Full's (slope = 0), bit for bit.

## What that bought, measured against the ground truth

Third harmonic error, rms over the test grid (100 Hz to 10 kHz, -20 to +6 dB), SP:

| Bias | Lite | Full |
|---|---|---|
| Correct (100 %) | 1.3 dB | 1.3 dB |
| 25 to 50 % | 5 to 7.7 dB | 1.7 to 3.7 dB |

Output level is the same in both (about 0.5 dB rms). So Full is more exact only in the level of
individual harmonics when the bias is well below correct.

## What it cost

On the MPC One+ (32-bit Cortex-A17, block 128 at 44.1 kHz), share of one core:

| | Steady | Worst block while controls move |
|---|---|---|
| Lite at `c47ee86` | 9.4 % | 24.5 % |
| Full at `c47ee86` | 15.8 % | 35.1 % |
| On in 1.8 (Lite, reads made four at a time) | 6.6 % | 17.3 % |

- Instructions per stereo sample at `c47ee86`: Lite 2,741, Full 4,066 (HQ off).
- Full's working table is 98 KB per engine, three times that processor's 32 KB data cache. Lite's
  is under 6 KB.
- A Bias move rebuilds the working table: about 82k instructions for Full, every 128 samples while
  the knob moves.
- The plugin file was 2.4 MB with Full's planes and is about 640 KB without them.
- With Full in the plugin, the public CPU bench gave a warning (sweep p99 27.6 %). Without it the
  bench passes (12.7 %).

## Why it was removed

On the device, by ear, Lite and Full were hard to tell apart, and Full cost the bench its pass. The
accuracy it adds is real on paper and small in the sound.

## If it comes back

- A faster processor changes the picture: the newer MPCs have more and larger-cached cores.
- Lite's reads were later made four lanes at a time, which cut its cost by about 28 % with the same
  output. Full's read was never given that treatment and likely has similar room.
- One engine at a time: 1.8 changes speed by gliding one engine's tables, where `c47ee86` ran two
  engines and crossfaded. Full should adopt the same.
- A fitted smooth function in place of the table was tried and rejected (unstable fits, no cheap
  antiderivative). A table of the antiderivative shipped as constants would add about 2 MB.
- Full's cost was mostly its second table axis: 98 KB of working table per engine. A compact stand-in
  for that axis is worth a try before bringing the table back: a small neural network trained on the
  ground truth, or a low-order fit of how the table changes with slope. The idea comes from a tape
  plugin that trained small "state transition" networks to stand in for its hysteresis solver (about
  three times faster than the solver, with a sound of its own at extreme settings). For On's
  one-axis table a network would be slower and less exact than the lookup; it is only the slope axis
  that might gain.
- The parameter is a two-position choice now. A third position would have to be appended as the
  last choice so that saved projects keep their meaning: Off, On, then Full.
