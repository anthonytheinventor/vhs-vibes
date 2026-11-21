# VHS Vibes: the Hysteresis recorder, as shipped in 1.8

Status: built into the plugin. The engine is `src/hysteresis.h`, its tables `src/hysteresis_tables.h`
(made by `r3.cpp`, mode `plugin`), its wiring the `hy...` functions in `src/VHSVibes.cpp`, its checks
`tests/hyst_test.cpp`. The rest of this folder is the offline side: the carrier simulation (`hyst.h`,
`model.h`), the table generator (`r2.cpp`, `r3.cpp`) and the prototype engine (`rt2.h`).

- Ground truth: the 12-layer carrier simulation (`hyst.h`, mode RAW).
- Figures marked (m) are measured, figures marked (i) are inferred.

## 0. What it is, and what it is not

At run time there is no hysteretic state. The engine holds no magnetization that a later sample could
depend on through a loop: per group of layers it reads a fixed curve (remanence against record field),
then filters. The hysteresis is in the offline simulation: each bit of tape arrives erased, crosses the
record head once in the fading bias field and freezes, and a Preisach model of the particles works out
what it keeps. The tables hold the results of that simulation; the engine looks them up.

The accuracy figures below compare the engine with that simulation, on steady tones. They are not a
comparison with a measured deck.

## 1. What it replaces: one engine

With Sat Type = VHS, the Saturator on and Hysteresis On, the recorder takes the place of TapeSat's
static curve (`s * curve(w / s)`). Rec Level, pre-emphasis, de-emphasis, band edge and make-up stay. The
Bias tilt (`gBias`, `biasLp`) and the Bias-dependent ceiling `s` are left out: the recorder makes those
effects itself.

There is one engine, the one called FAST in this folder's tools and Lite during development. It runs at
twice the session rate with HQ off and with HQ on alike (HQ on hands it the doubled signal it already has;
HQ off doubles it with the plugin's HalfBand). The second engine, Full (SLOPE here), with the slope of the
field as a second table axis, was removed before release: `FULL_ENGINE.md` says what it was, what it cost
and how to bring it back.

## 2. Signal flow (per channel; L and R are the two lanes of one vector)

```
w (pre-emphasized, Rec Level applied, at the session rate)
-> Hs = kFieldPerUnit * w                       field in Hc (coercivity units)
-> x2: HQ off, the plugin's HalfBand (5 sections); HQ on, already at x2
-> ring of 128 samples at x2
-> per group g = 0..2, per x2 sample, element time e = newest - lag:
     p = d0_g * fs2; io = floor(p); t = p - io
     A = sum_k cv_k(t) Hs[e+io-1+k]                         (k = 0..3, Lagrange cubic)
     M_g = ADAA along A of T_g:
       (Gi(A_n) - Gi(A_{n-1})) * 1 / (A_n - A_{n-1})        if |A_n - A_{n-1}| > 0.005 + 0.004 |A_n|
       T_g((A_n + A_{n-1}) / 2)                              otherwise
       Gi(A_{n-1}) is the previous sample's value, reused.
       Past |A| = 11.9 the integral continues with the end's value and slope (the same at both ends).
-> per group: /2 (HalfBand) -> 64-tap playback FIR
-> y = sum of the groups
-> high-frequency self-erasure correction (section 6), at the session rate
-> y / kFieldPerUnit -> de-emphasis, band edge, make-up (TapeSat's code)
```

- The two x2 samples and both channels go through the table reads as four lanes at once.
- `1 / x` is the reciprocal estimate and two Newton steps on ARM (its vector unit has no divide; written as a
  divide it was four scalar divides per group and x2 pair); a true divide elsewhere. The correction's division
  is done the same way.
- The threshold grows with |A| because the integral grows with |A| (to about 12), so its float rounding over a
  small step grows too. Measured over every plane and step against the quotient worked out in double (m): the
  float quotient's worst error where it is used is 1.1e-4 of full scale, against 6.8e-4 with a fixed 0.005.
  T at the middle and the quotient's limit differ by up to 1e-3 where T bends most, as before.
- The lag: `kLat` = 14 samples at x2 at 44.1 kHz. At a higher rate the groups' delays (fixed in seconds) are
  more samples, and the lag grows with them, so that no read reaches past the newest sample:
  `lag = max(14, ceil(14 * sr / 44100))` (`Engine::lagOf`). The ring reaches lag + io + 3 back.

Lagrange taps for the points -1..2: `cv = { -t(t-1)(t-2)/6, (t+1)(t-1)(t-2)/2, -(t+1)t(t-2)/2, (t+1)t(t-1)/6 }`.

## 3. Tables (generator: `r3.cpp`, mode `plugin`)

**Groups:** 12 sub-layers over the 3.5 um coating (0.3 to 3.8 um deep), merged in threes. A group's T is the
mean of its sub-layers' remanence, each computed at its own depth.

**T_g(A):** the carrier simulation's remanence (-1..1), averaged over 8 carrier phases, for a field that holds
still while the element freezes (the slope axis' middle row, u = 0). d0_g: the group's small-signal delay,
taken where the group is steepest.

**Grid:**
- A: -12..12 in 0.05 steps (481 points). Up to 1.8's review the planes stopped at 6, and past the edge the
  engine held the end value. At 25 % bias the deepest group is far from saturated there (T = 0.384 at 6, 0.90 at
  8, 0.99997 at 12), so pre-emphasized treble, which reaches about 9 at the reference level and more when
  driven (EP, with its stronger emphasis, past 30), met a hard knee. Out to 6 the planes are the cached grid's
  values unchanged; past 6 the generator makes the same element runs at each A (`extendRow`). Past 11.9 the
  engine continues the integral with the end's slope; every group is within 3e-5 of saturation there.
- Bias planes at 25, 30, 35, 40, 45, 50, 60, 70, 80, 90, 100, 110, 120, 135, 150 % of correct (the Bias knob's
  range; the simulation's 0 to 20 % planes were shipped and never read until the review).
- SP and EP separate. Odd symmetry: `T(-A) = -T(A)`.

**Shipped (m):** `src/hysteresis_tables.h`, 472 KB of text, 173 KB of floats (planes), plus the delays and FIRs.

**At load:** each plane's integral along A from A = 0 (trapezoid rule, in double, kept as float), made once
when the first recorder is made (173 KB). A bias update blends two of those, as T is the blend of two planes.

**Working memory per engine (m):** the blended integral, 3 x 481 floats (5.8 KB); T is read from the planes.

## 4. Bias knob: 25-150 % of correct, and plane blending

- **Mapping:** knob -> bias % (25..150) -> neighboring planes P_b, P_b+1 and blend f.
  - The working integral: `Gi = (1-f) G_b + f G_b+1`, four points at a time; T read as `(1-f) T_b + f T_b+1`
    where it is needed. d0 is blended too, then the taps (cv, io) are made again.
  - After every update the reused value `Gi(A_prev)` is made again, or the next sample clicks.
- The bias glides towards the knob (the whole range in 0.5 s at most) and the tables follow every 32 samples
  while it moves.
- **Sweep test (m, `hyst_test`):** a reference-level 1 kHz tone with the Bias knob swept from 25 to 150 % in
  0.5 s: the highest line off the harmonics is -81.9 dBFS (-100.2 at rest).

## 5. Tape speed (Hi Sat): SP below half way, EP from there

A change of speed while the recorder is heard glides: the planes, delays, FIRs and the correction's values
move from one speed's to the other's in about 10 ms (a step every 32 samples). One engine does it; no block
runs two. Unheard (the recorder still coming in), the new speed is set at once.

## 6. HF self-erasure correction (fitted to the ground truth)

```
sigma_n = |Hs_n - Hs_{n-1}| * kSig          kSig = sr / v[um/s]   (SP 1.3223, EP 3.9658 at 44.1 kHz)
env    += (sigma > env ? aAtt : aRel) * (sigma - env)      aAtt: 0.5 ms, aRel: 30 ms (one-pole)
g       = 1 / (1 + c * max(0, env - e0))
lo     += (1 - hpA) * (y - lo)              hpA = exp(-2 pi fc / sr)
y'      = lo + g * (y - lo)
```

The side chain is the record field, so there is no feedback. Parameters (m): SP fc 700 Hz, e0 14, c 0.08;
EP fc 1000 Hz, e0 4, c 0.02.

## 7. Latency

- 41.5 samples at 44.1 kHz (0.94 ms) with HQ off, 41.0 with HQ on, both speeds (m, `hyst_test`: 41.489 and
  40.989). The reported latency grows by that much, and the dry copy for Mix is delayed to match.
- At 48 and 96 kHz the lag grows with the rate (section 2): 42.3 and 48.5 samples (m).
- **Hysteresis on and off:** a 10 ms crossfade between the curve and the recorder, the recorder started from
  rest unheard first.

## 8. Calibration constants

| Constant | Value |
|---|---|
| kHbRight | 4.7377 Hc |
| kFieldPerUnit | 2.19036 |
| Density | hc0 0.9966, sigma_c 0.1054, sigma_i 0.1334 (SFD 0.40) |
| Geometry | gap 1.5 um, spacing 0.3 um, coat 3.5 um, bias 62.5 kHz |
| Speeds | SP 33.35 mm/s, EP 11.12 mm/s |

- **Correct bias:** SP, 6.3 kHz at -20 dB, 2 dB past its peak.
- **Calibration:** 441 Hz at the reference level (0.4) gives 3.00 % third harmonic in the simulation.

## 9. Cost

Device (m, before the review's fixes): Hysteresis On took about 6.6 % of a core steady on the MPC One+.

After the fixes, by instruction count only (m: arm32, qemu, per stereo sample, VHS type; i: what that means
on the device):

| | HQ off | HQ on |
|---|---|---|
| Hysteresis Off | 779 | 1,136 |
| On, still (before the review) | 1,946 | 2,223 |
| On, still (now) | 1,973 | 2,290 |
| On, Bias knob moving (before / now) | 2,151 / 2,169 | 2,430 / 2,488 |

- The count rose a little: the threshold of section 2 and the reciprocal's steps are more instructions than
  the divides they replace. The divides themselves are gone (14 per stereo sample, scalar, each many cycles on
  this core): the device should spend less, not more (i, not measured).
- Per-group 64-tap FIRs are about 300 instructions of the total.
- A replica of the public CPU bench by instruction count (m: its stages, its random knob sweep, the default
  build with Hysteresis Off by default): the sweep's p99 block is 537.2 k instructions after the fixes against
  537.1 k before, the worst block 710 k against 706 k.

## 10. Accuracy against the ground truth (m), with the correction

Grid: 100 Hz to 10 kHz, -20 to +6 dB re reference, steady tones. Each cell is the output-level error as
worst / rms in dB, and in brackets the third harmonic's rms error in dB.

| Speed | 25 % | 50 % | 100 % | 150 % |
|---|---|---|---|---|
| SP | 7.4 / 2.3 (5.4) | 1.8 / 0.6 (6.2) | 0.3 / 0.14 (1.3) | 1.3 / 0.4 (1.1) |
| EP | 8.7 / 3.4 (7.7) | 1.8 / 0.7 (5.1) | 1.4 / 0.4 (1.2) | 2.3 / 0.9 (1.3) |

- The review's fixes moved these by less than 0.1 dB.
- Hotter treble, where the wider planes matter (m, against the simulation, output level error before / after):
  SP 3 kHz +12 dB at 25 %: -1.16 / -0.62 dB; SP 5 kHz +12 dB at 25 %: +0.22 / +0.64 dB; at 100 % bias and at EP
  the change is under 0.1 dB. The deep group's treble is mostly filtered off by its own playback response,
  so the old knee showed little at the output.

## 11. Test figures (m, `hyst_test`)

The engine at correct bias, SP, through the record and playback emphasis. Gain is out - in in dB; H3 and H5
are % at the output.

| Tone | Level | Gain | H3 % | H5 % |
|---|---|---|---|---|
| 441 Hz | -20 | +0.01 | 0.079 | 0.005 |
| 441 Hz | 0 | -0.71 | 2.93 | 0.017 |
| 441 Hz | +8 | -4.12 | 11.40 | 2.08 |
| 1 kHz | 0 | -1.13 | 2.95 | 0.096 |

- The whole plugin at the reference level: 441 Hz -0.74 dB, 2.91 % third harmonic; 1 kHz -1.20 dB, 2.82 %
  (HQ on alike within 0.05 dB).
- Silence stays exactly silent; a full-scale square and a tone 6 dB over full scale stay finite.
- A tone and its negative come out each other's negative within 1e-5 of the peak, at field 14, past both ends.

## 12. Numerics

- The ring and dry-line counters are unsigned and wrap at 2^32 (every use masks them to a power of two that
  divides 2^32): the wrap changes nothing (checked by presetting them just short of it). As ints they
  overflowed after 6.8 hours at 44.1 kHz.
- Table reads are Catmull-Rom; the place fa = (A + 6) * 20 is rounded down, so reads on both sides of A = -6
  work alike, and clamped the same distance from both ends.
