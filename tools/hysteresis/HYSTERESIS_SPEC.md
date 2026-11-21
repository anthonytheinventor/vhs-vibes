# VHS Vibes: Hysteresis recorder, implementation spec (prototype, round 3)

Status: offline prototype, nothing built into the plugin. Everything is in
`C:\Claude\build-tmp\vhs-vibes-test\v18\hyst\`.

- Ground truth: the 12-layer carrier simulation (`hyst.h`, mode RAW).
- Figures marked (m) are measured. Figures marked (i) are inferred.

## 1. What it replaces, and the two engines

With Sat Type = VHS and Hysteresis on, `TapeSat::record` (the static `s * curve(w / s)`) is replaced by the
recorder below. Rec Level, pre-emphasis, de-emphasis, band edge and make-up stay. The Bias tilt
(`gBias`, `biasLp`) and the Bias-dependent ceiling `s` are bypassed: the recorder makes those effects itself.

| HQ | Engine | Table | Rate |
|---|---|---|---|
| off | FAST + ADAA | T_g(A) | 2x |
| on | SLOPE + ADAA | T_g(A, u) | 2x |

Both engines use the same latency, the same group delays and the same playback filters per speed, so
toggling HQ does not move timing (section 6).

## 2. Signal flow (per channel; L and R are the two lanes of one vector)

```
w (pre-emphasised, Rec Level applied, 44.1 kHz)
-> Hs = kFieldPerUnit * w                       field in Hc
-> x2 (plugin HalfBand, 5 sections)              88.2 kHz -> ring (>= 32 samples)
-> per group g = 0..2, per 88.2 kHz sample, element time e = newest - kLat (kLat = 14):
     p = d0_g * 88200; io = floor(p); t = p - io
     A = sum_k cv_k(t) Hs[e+io-1+k],  S = sum_k cs_k(t) Hs[e+io-1+k]     (k = 0..3, Lagrange cubic)
     SLOPE only: s = S * kTauS[speed]; u = s / (kSc + |s|), clamped to +-kUmax
     M_g = ADAA along A of T_g:
       (Gi(A_n,u_n) - Gi(A_{n-1},u_n)) / (A_n - A_{n-1})    if |A_n - A_{n-1}| > 0.005
       T_g((A_n + A_{n-1})/2, u_n)                          otherwise
       Gi continues past +-6 with slope T(+-6).
       FAST may reuse Gi(A_{n-1}) from the previous sample; SLOPE must not.
-> per group: /2 (HalfBand) -> 64-tap FIR (hyst_firs_fixed.h)
-> y = sum of groups
-> HF self-erasure correction (section 5), 44.1 kHz
-> y / kFieldPerUnit -> de-emphasis, band edge, make-up (existing code)
```

Lagrange taps for the points -1..2:
- Value: `cv = { -t(t-1)(t-2)/6, (t+1)(t-1)(t-2)/2, -(t+1)t(t-2)/2, (t+1)t(t-1)/6 }`
- Slope: `cs = { -(3t^2-6t+2)/6, (3t^2-4t-1)/2, -(3t^2-2t-2)/2, (3t^2-1)/6 }`

## 3. Tables (generator: `r2.cpp`, mode `header`; FIRs: `r3.cpp`, mode `firs`)

**Groups:** 12 sub-layers over the 3.5 um coating (0.3 to 3.8 um deep), merged in threes.
- A group's T is the mean of its sub-layers' remanence, each computed at its own depth.
- Three single depths fail badly under bias (m).

**T_g(A, u):** the carrier simulation's remanence (-1..1), averaged over 8 carrier phases.
- The element sees `Hs(tau) = A + clamp(s (tau - d0_g), -3, 3)`, where tau is the time after the gap center and s = u-derived slope / tauRef.
- FAST: u = 0 only.
- d0_g: the group's small-signal delay, taken where the group is steepest.

**Grid:**
- A: -6..6 in 0.05 steps (241 points).
- SLOPE u: 17 points over -0.95..0.95, kSc = 1.
- Bias planes at 0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 60, 70, 80, 90, 100, 110, 120, 135, 150 % of correct.
- SP and EP separate.
- Odd symmetry: `T(-A, -u) = -T(A, u)`.

**Generated files (m):**
- `hyst_tables_fast_os2.h`: 459 KB of text, 116 KB of floats.
- `hyst_tables_slope_os2.h`: 7.6 MB of text, 1.97 MB of floats.
- Both compile. Use the FIRs in `hyst_firs_fixed.h`; the FIRs inside the table headers are from round 2 and have the old latency.
- Gi is not shipped: compute it at load by a trapezoid along A from A = 0.

**Working memory (m):**
- FAST: plane + Gi = 5.8 KB, fits the A17's 32 KB L1.
- SLOPE: 98 KB, does not fit. The instruction counts do not include its cache misses.

**Coarser A steps (FAST, m):**
- 0.1, 0.2 and 0.4 leave output level and aliasing unchanged.
- They save memory only, no instructions.

## 4. Bias knob: 25-150 % of correct, and plane blending

- **Mapping:** knob -> bias % (25..150) -> neighbouring planes P_b, P_b+1 and blend f.
  - Working tables: `T = (1-f) T_b + f T_b+1`, the same for Gi.
  - d0 is blended too, then recompute the taps (cv, cs, io).
  - kLat stays fixed at 14.
- **After every update:** recompute the FAST reuse value `Gp = Gi(A_prev)`, or the next sample clicks.
- **Sweep test (m), 25 -> 150 % in 0.5 s, SP, 2x.** The highest spectral line off the 1 kHz harmonics while sweeping a reference-level tone, against -98 dBFS static:

  | Update every | Off-harmonic line |
  |---|---|
  | 32 samples | -81 dBFS |
  | 64 samples | -75 |
  | 128 samples | -70 |
  | 256 samples | -64 |
  | 512 samples | -58 |

  - The largest second difference rose from 0.023 static (at 100 %) to 0.033-0.042 for update intervals of 32-256 samples, and to 0.059-0.070 at 512 samples. Most of that rise is the 25 % setting's own distortion, not steps (i).
  - Silence stays exactly zero at every rate.
  - Both engines behave alike.
- **Update cost (m)**, T + Gi, per update (independent of os): about 6 k instructions for FAST, about 172 k for SLOPE.
- **Settled rate:**
  - FAST: update every 32 samples while the knob moves (about +320 instr per sample (i): T alone measured +160).
  - SLOPE: every 128 samples (+1,340 per sample while moving (i)), giving about -70 dBFS of zipper on a full-range 0.5 s sweep (m). Slower sweeps are lower (i).
  - Alternative for SLOPE, not built: look up both planes and blend the outputs per sample. No zipper, about +1,500-2,000 while moving (i).

## 5. HF self-erasure correction (fitted to the ground truth)

```
sigma_n = |Hs_n - Hs_{n-1}| * kSig          kSig = 44100 / v[um/s]   (SP 1.3223, EP 3.9658)
env    += (sigma > env ? aAtt : aRel) * (sigma - env)      aAtt: 0.5 ms, aRel: 30 ms (one-pole)
g       = 1 / (1 + c * max(0, env - e0))
lo     += (1 - hpA) * (y - lo)              hpA = exp(-2 pi fc / 44100)
y'      = lo + g * (y - lo)
```

The side-chain is the record field, so there is no feedback.

Fitted parameters (m):

| Engine | Speed | fc | e0 | c |
|---|---|---|---|---|
| FAST | SP | 700 Hz | 14 | 0.08 |
| FAST | EP | 1000 Hz | 4 | 0.02 |
| SLOPE | SP | 9000 Hz | 10 | 0.30 |
| SLOPE | EP | 1000 Hz | 6 | 0.02 |

- The optimum is flat: fits on a narrower grid scored the same.
- Cost: +47 instructions per stereo sample (m).
- At -20 dB re reference it changes nothing: 0.000 dB (m).

## 6. Latency (both engines)

- kLat = 14 (88.2 kHz samples).
- The FIR target removes a fixed bulk delay of 10 samples rather than the measured one. Measured bulk delays: SP 7.8 (FAST) and 7.8 (SLOPE), EP 4.9 and 4.8.
- Total: 10 + 31.5 = **41.5 samples at 44.1 kHz (0.94 ms)** for both engines and both speeds (by construction; not measured end to end).
- **Switching engines:** crossfade the two outputs over about 10 ms, running both during the fade.
  - At correct bias the engines differ by about 0.5 dB rms in level on SP (m), at the same timing. No click is expected (i, not tested).

## 7. Calibration constants

| Constant | Value |
|---|---|
| kHbRight | 4.7377 Hc |
| kFieldPerUnit | 2.19036 |
| kTauS at 2x | SP 2.6447, EP 7.9317 (samples per um) |
| Density | hc0 0.9966, sigma_c 0.1054, sigma_i 0.1334 (SFD 0.40) |
| Geometry | gap 1.5 um, spacing 0.3 um, coat 3.5 um, bias 62.5 kHz |
| Speeds | SP 33.35 mm/s, EP 11.12 mm/s |

- **Correct bias:** SP, 6.3 kHz at -20 dB, 2 dB past its peak.
- **Calibration:** 441 Hz at the reference level (0.4) gives 3.00 % third harmonic at the output.

## 8. Cost (m), arm32 instructions per stereo sample, block 128, 2x

| | Still | Bias knob moving (i) |
|---|---|---|
| FAST + ADAA + correction | 2,899 | about 3,200 |
| SLOPE + ADAA + correction | 4,881 | about 6,200 (128-sample updates) |

Per-group 64-tap FIRs are about 840 of each. A 4th-order IIR fit was 2-3 dB off and was rejected.

## 9. Accuracy against the ground truth (m), 2x, with the correction

Grid: 100 Hz to 10 kHz, -20 to +6 dB re reference. Each cell is the output-level error as worst / rms in dB;
values in brackets are without the correction.

| Engine, speed | 25 % | 50 % | 100 % | 150 % |
|---|---|---|---|---|
| SLOPE SP | 4.7 / 1.5 | 0.8 / 0.4 | 0.4 / 0.13 (2.3 / 0.5) | 1.2 / 0.3 |
| SLOPE EP | 4.6 / 1.6 | 2.9 / 0.8 | 1.5 / 0.5 (6.5 / 2.2) | 2.3 / 0.8 |
| FAST SP | 7.4 / 2.3 | 1.8 / 0.6 | 0.3 / 0.14 | 1.3 / 0.4 |
| FAST EP | 8.7 / 3.4 | 1.8 / 0.7 | 1.4 / 0.4 | 2.3 / 0.9 |

Harmonic rms errors (the correction leaves them unchanged):

| Engine, speed | H3 at 100 % | H3 at 25 % | H3 at 50 % |
|---|---|---|---|
| SLOPE SP | 1.3 | 3.5 | 1.7 |
| SLOPE EP | 1.7 | 3.7 | 3.4 |
| FAST SP | 1.3 | 5.4 | 6.2 |
| FAST EP | 1.1 | 7.7 | 5.0 |

## 10. Test figures to reproduce (m)

SLOPE at correct bias, SP. Gain is out - in in dB; H3 and H5 are % at the output.

| Tone | Level | Gain | H3 % | H5 % |
|---|---|---|---|---|
| 441 Hz | -20 | +0.03 | 0.06 | 0.006 |
| 441 Hz | 0 | -0.69 | 2.98 | 0.067 |
| 441 Hz | +8 | -4.10 | 11.4 | 2.13 |
| 1 kHz | 0 | -1.15 | 3.16 | 0.146 |

- **EP 1 kHz at 0 dB:** -5.56 dB, 8.10 % (before the correction; the correction changes 1 kHz by less than 0.6 dB).
- **Small signal:** flat within 0.05 dB from 50 Hz to 12.5 kHz.
- **Aliasing at 2x:** `r2_alias_fast_st0.05_os2.txt` and `r2_alias_slope17_os2.txt`.
