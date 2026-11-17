# VHS Vibes 1.7: the Hi-Fi mode

A plan, not yet built. Sources: `hifi_research.md` and `hifi_research2.md` (in `C:\Claude\build-tmp\vhs-vibes-test\`).
Every number below carries the label it has in those reports: **[R]** read in a primary document,
**[S]** secondary, **[D]** derived, **[E]** estimate.

## 1. What it is

A third position of **Sat Type**: 2-Band, VHS, **Hi-Fi**.

VHS mode models the tape of the linear track, which saturates. Hi-Fi mode models the FM sound that the
rotating heads record under the picture. FM does not saturate. It stays clean, and what you hear instead is:

- a hard ceiling when the level is too hot (the deviation limiter);
- noise that rises and falls with the music, and pumping, when record and playback do not agree (the 2:1 compander);
- clicks and a tearing crackle when the signal off the tape gets weak (FM threshold);
- a buzz that rides on high notes (the two heads handing over 59.94 times a second);
- a short hold, then a drop to the dull mono linear track, when the signal is lost.

While Hi-Fi is selected, the model also takes over seven knobs on the NOISE, DECK and OUTPUT pages, so those
artifacts come from one model instead of separate imitations.

## 2. Signal path in Hi-Fi mode

The whole Hi-Fi path sits where the saturator sits today, as one block. Everything before it (Chorus, Mic,
AGC, Preamp, Slew, Comp) and after it (Hi Cut, the wobbles, Hum, Buzz, Whistle, Mix) stays as it is.

```
in -> NR emphasis -> 2:1 compressor -> FM emphasis -> limiter
   -> FM CHANNEL (noise, clicks, head switch, signal dips, hold)
   -> FM de-emphasis -> 1:2 expander -> NR de-emphasis -> out
                                  \-> when the signal is lost: crossfade to the linear track
```

| Block | Values | Label |
|---|---|---|
| NR emphasis | shelf 240 µs / 56 µs | [S], reproduces the chip's measured 10 kHz figures to 0.1 dB [D] |
| Compressor | 2:1, unity at reference level; detector weighted 240 µs / 24 µs; attack 5 ms, recovery 70 ms | [R] two chip datasheets; weighting [R/S] |
| FM emphasis | shelf 56 µs / 20 µs | [S], checked as above |
| Limiter | ±150 kHz deviation = 9.5 dB over reference, after the compressor and emphasis. At the input: about +19 dB at 1 kHz, +11.5 dB at 10 kHz | level and position [R]; **hard clip is [E]** |
| Channel noise | rises 6 dB per octave; 55 dB under reference at full signal; 1 dB more noise per dB of lost signal | shape [R]; the 55 dB point [R] 54-60 dB on the bench |
| Clicks | rate = (B / 2√3) · erfc(√CNR), B = 300 kHz | formula [D] from Rice 1948; B [R]; **CNR at full signal = 25 dB is [E]** (17-27 dB implied by a 1983 spectrum) |
| Head switch | every 1/59.94 s; the deck hides it with a 6 µs hold plus a correction; residual -73 to -76 dB | timing, hold, residual [R]; the size of the unhidden spike [D] |
| Signal lost | hold the sound when the signal is 21 dB down; switch to the linear track after 0.3 ms at 17 dB down; come back after 33-267 ms | [R] chip datasheets |
| Expander | 1:2, same detector and times as the compressor | [R] |
| Linear track during a loss | the plugin's own **VHS-mode tape model** of the same input, in mono | design choice: it is the track the deck really falls back to |

Reference level is the plugin's reference (a peak of -8 dBFS), the same as in VHS mode.

With every impairment at zero the path must be transparent: what goes in comes out, apart from the limiter.

## 3. The knobs

### The three Sat Type knobs

| Knob | 2-Band | VHS | **Hi-Fi** | Read-out | Evidence |
|---|---|---|---|---|---|
| Lo Sat | level of lows | Rec Level | **Rec Level**: how hard the limiter is hit. 0 dB at the same knob position as in VHS mode | -10 to +16 dB | limiter level [R] |
| Hi Sat | level of highs | Tape Speed | **Carrier**: how much signal comes off the tape. Down = more noise, then clicks, then holds and drops to the linear track | 0 to -30 dB | thresholds [R]; overwrite loss 12-14 dB [R]; absolute scale [E] |
| Split | crossover | Bias | **Cal**: playback level error at the expander. Off centre = the compander mistracks: pumping, noise that breathes | -3 to +3 dB, centre is right | deck tolerances ±1 to ±2 dB [R] |

### The seven knobs the model takes over

They keep their names, because each still means the same thing to the ear. What changes is that the model
produces the sound.

| Knob | Today | In Hi-Fi mode | Evidence |
|---|---|---|---|
| Hiss | hiss added to the output | FM noise inside the channel, before the expander, so it breathes by itself | noise law [R] |
| Crackle | random crackle | one head weaker than the other: noise and clicks alternate with each head, 0 to 12 dB | 3-5 dB within service tolerance [R]; more than that [E] |
| Head Buzz | a rasp plus a short hold | how badly the head handover is hidden: from the documented -73 dB residual up to no concealment | [R] at the bottom; the top [D] |
| Breathing | hiss follows the signal | compander mismatch: expander times 0.8 to 1.2 of the right value, and a different detector | ±20 % [S]; peak vs RMS detectors both documented |
| Tracking | static, then the backup track | signal dips of 10-30 dB lasting one or more head passes: clicks, holds, drops to the linear track with the documented timing | timing [R]; how often and how deep [E] |
| Drift | quieter and duller | slow wander of the signal strength: the noise swells and clicks come and go. Not duller: FM does not dull | noise law [R]; swing [E] |
| Dropouts | dips that take the highs | short signal losses: the deck holds the sound; a click at the edges | hold behaviour [R]; 2-8.5 per minute on new tape, up to 80 after 100 passes [R] on the video signal; lengths [E] |

### Left alone

- **Tape speed** has no knob in Hi-Fi mode. One deck measured EP 4 dB noisier than SP [R]; the Carrier knob covers that.
- **B-Type NR** belongs to the linear track. In Hi-Fi mode it is ignored.
- **Wobble, Hi Cut, Hum, Buzz, Whistle** work as before, after the Hi-Fi block. The Hi-Fi track itself has no
  measurable wow or flutter [R], so for a faithful sound leave the wobbles low.

## 4. Screen

- Sat Type becomes three buttons.
- Each mode has a colour, and the knobs it changes wear it: 2-Band as today, **VHS turquoise-green**, **Hi-Fi sunset
  orange**. In Hi-Fi mode the three TAPE knobs and the seven taken-over knobs turn orange, on whichever page they are.
- Captions change on the TAPE page: REC LEVEL, CARRIER, CAL.

## 5. Compatibility

- No existing parameter id, order or program number changes. Sat Type gains a third value; no new parameter is planned.
- 2-Band and VHS must be bit-identical to 1.6 on 32-bit ARM.
- Saved projects store each value both ways and the plugin trusts the plain one, so a project saved with VHS loads as VHS.
- **Accepted (owner, 2026-11-16):** anything that stores only a knob position (a Q-Link assignment or recorded
  automation of Sat Type) may read "VHS" as "Hi-Fi" once there are three positions. Sat Type simply gains the
  third value; no check and no separate switch.
- New programs are appended: about five "Hi-Fi - ..." programs.

## 6. What the manual will call an estimate

The absolute signal-to-noise scale of the Carrier knob; the limiter being hard; the level of the unhidden head-switch
spike; how often and how long tracking dips and dropouts are; which detector type a given deck used.
The VHS Hi-Fi standard itself (IEC 60774-2) could not be read; its values come through chip datasheets.

## 7. Build stages

| Stage | What | Proof |
|---|---|---|
| 1 | Emphasis, compressor, limiter, expander: the clean path | transparent to 0.1 dB; 2:1 and 1:2 static curves; 5 ms / 70 ms; the chip's 10 kHz figures (+3.9 dB record, -7.7 dB playback); limiter at +19 dB (1 kHz) and +11.5 dB (10 kHz) |
| 2 | The channel: noise, clicks, the signal-strength variable | noise 55 dB under reference at full signal and 1 dB per dB; click rate against the formula |
| 3 | Head switch, hold, drop to the linear track | residual -73 dB at 10 kHz with Head Buzz at 0; 0.3 ms on, 33-267 ms off |
| 4 | Knob take-over, captions, read-outs, colours | read-out tests; a rendered picture of each page |
| 5 | Programs, manual chapter, README | PDF rebuilt |
| 6 | CPU pass and bench on the MPC | no slower than VHS mode is the target |

A listening build can go on the MPC after stages 1, 3 and 4.

## 8. Decisions (made 2026-11-16)

1. The seven taken-over knobs keep their names.
2. The linear track during a loss is the VHS-mode model, in mono.
3. B-Type NR is ignored in Hi-Fi mode.
4. Carrier reads in dB, 0 to -30.
