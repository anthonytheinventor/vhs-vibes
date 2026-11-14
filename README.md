# VHS Vibes

A VHS tape degradation effect for Akai MPC standalone hardware, by anthonytheinventor.
Pitch wobble, saturation, generation loss, hum, hiss and crackle, on a hot pink cassette.

![The TAPE page](docs/page_1_tape.png)

(The pictures in `docs/` are mock-ups drawn by the skin script, not photos of an MPC.)

This repository is the plugin's project folder for the
[Akai MPC Plugin SDK](https://github.com/inmusic-software/mpc-plugin-builder-sdk). To build
it, clone the SDK and put this folder inside it as `plugins/VHS Vibes`. The MPC needs MPC OS
3.10.1 or later with Preferences > Enhancements (Beta) > Enable Enhancements and External
Plugin Support switched on.

Insert effect, stereo in / stereo out, 49 controls on five pages, 21 built-in programs. Version 1.3.

## What it does

The stage order follows the idea of James Peck's "VHS Audio Degradation Suite" for Reaktor.
All code here is original and no samples are used: every noise is synthesised.

```
in -> Chorus -> Mic -> AGC -> Preamp (Tone, Drive) -> Wear -> Comp -> 2-band Saturator -> Hi Cut
   -> Wobble 1 (Warp, Flutter) -> Noise -> Wobble 2 (Wow, Flutter) -> Lo Cut / Hi Cut
   -> Drift -> Tracking -> Head switching -> Dropouts -> Mono -> Mix -> Output
```

## Pages and controls

**TAPE**

| Control | What it does |
|---|---|
| Drive | Preamp soft clipping. 0 % is clean. |
| Tone | Tilt: left is darker, right is brighter (about 6 dB each way). |
| Wear | Limits how fast the signal can change: dulls and smears, like an old or copied tape. |
| Comp | One-knob compressor. Past about a third it squashes and brings quiet sound (and noise) up. |
| Dropouts | Short random level dips. 0 % is off. |
| Mono | Sums left and right. |
| Mix | Dry / wet. 100 % (the default) is all effect; lower lets the untouched input back in. |
| Output | Output level. |
| Saturator Power | Switches the 2-band saturator. |
| Split | Where the signal is divided into lows and highs. |
| Lows / Lo Sat | Level and clipping of the low band. |
| Highs / Hi Sat | Level and clipping of the high band. Lower Highs and raise Hi Sat for "copied again". |
| Hi Cut | 4-pole lowpass after the saturator. |

**WOBBLE**

| Control | What it does |
|---|---|
| Warp | Depth of the slow pitch wobble (up to about 5 % of pitch). |
| RPM | Speed of the slow wobble, as reel turns per minute. Below about 30 rpm the depth is limited. |
| Shape | 0 % is a smooth wobble, 100 % an uneven lurch. |
| Flutter Depth / Rate | Fast pitch wobble. |
| Wow, Rate, Flutter, Rate (Re-record) | A second, independent wobble: the tape after one more copy. |
| Lo Cut / Hi Cut | Filters after the second wobble. |
| Wobble (switch) | Turns both wobbles off or on together. The filters stay in. |

**NOISE**

| Control | What it does |
|---|---|
| Hum | 60 Hz mains hum. |
| Buzz | 120 Hz buzz. |
| Head Buzz | The video heads switching, 59.94 times a second: a steady rasp, plus a buzz that rides on bright sounds and vanishes on bass (the signal is held for a few samples at each switch). |
| Hiss | Tape hiss (different in left and right). |
| Crackle | Random clicks; the knob sets both how many and how loud. |
| Master | Level of all noise together. 50 % is nominal, 0 % is silent. |
| Noise (switch) | Turns all noise off or on. |
| Position | Post: noise is added after the first wobble (clean noise). Pre: noise goes in at the input and is compressed, clipped and wobbled with the music. |

**INPUT**

| Control | What it does |
|---|---|
| Chorus Power, Mix, Speed, Delay, Width | A chorus in front of everything; Width spreads it left / right. |
| Mic Power, Type, Gain | The tone of re-recording through a cheap microphone: Camcorder, Dynamic, Phone or Boombox. |

**DECK**

Faults of the machine rather than the tape. Each does something no other control can.

| Control | What it does |
|---|---|
| Power | The whole deck section on or off. |
| AGC | The recorder's automatic level control. A loud sound pulls the gain down at once; it stays down for a second and a half, then creeps back up over a few seconds and brings the background up with it (up to 14 dB). Comp works in milliseconds; this works in seconds. |
| Drift | The sound slowly comes and goes: quieter and duller for a moment, then back, like the audio track wandering off the head. (Dropouts are short dips; this is slow.) |
| Tracking | The Hi-Fi track loses lock now and then: a burst of static, a moment on the dull mono backup track, another burst, back again. Higher is more often. |
| Breathing | The hiss rises and falls with the signal, like a mistracking noise-reduction circuit. |
| Animation | The moving reels, grid and sun on or off. Off saves the work of updating the screen. Choosing a program does not change it. |

There is no tape-speed or track switch because they would only repeat other controls. For a
slow-speed (EP) tape set Hi Cut near 6 kHz, Lo Cut near 100 Hz and raise Hiss; SP is about
10 kHz and 50 Hz. For a clean Hi-Fi sound keep the wobbles and the noise low.

## Programs

Home Recording (the default), Fresh Rental, 3rd Gen Copy, Vaporwave Mall, Camcorder 89,
Tracking Error, Late Night TV, Clean Widener, Training Video, Chewed Tape, Answer Machine,
Dream Sequence, Subtle Glue, Bad Tracking, Six Hour Tape, Wedding Video, Hi-Fi Master,
Edge Damage, Bootleg Concert, Public Access, Init.

Init is a blank tape: everything neutral, the sound passes through unchanged. It is always
the last program.

## Tips

- With Position on Pre and Comp turned up, the noise swells whenever the music stops. That is
  how a real VCR's automatic level control behaves; turn Master down if it is too much.
- The effect makes noise even with no input. Set Master to 0 % for a silent background.
- Turning a wobble depth or rate changes the plugin's own delay for a moment, which bends
  the pitch while you turn it.
- Mix below 100 %: the dry signal is delayed to line up with the tape path. With the wobbles
  off the blend is clean; with them on, dry and wet drift against each other and it sounds
  like a chorus. That is the nature of mixing a wobbling signal with a steady one.

## Moving parts

- The two reels turn while sound comes in or the song is playing, coast to a stop about two
  seconds after, and lurch with the wobble.
- The neon grid on the page sticker rolls with the reels.
- The sun glows with the output level.

These are driven by two extra read-only parameters, Reel and Level, which the plugin updates
up to 20 times a second. They are not settings, cannot be automated and are best left alone.
The Animation switch on the DECK page turns them off.

## The interface

A hand-made skin: a hot pink cassette with the reel window and two stickers along the top
and the controls on a dark face label, in two rows of eight. Each block of four controls is
one Q-Link group; touch a control and the four knobs follow that block. The block titles
start a little to the right so the MPC's "Q" marker does not cover them.

The skin is generated by a script (needs Python with Pillow, on Windows for the fonts):

```
python "plugins/VHS Vibes/tools/make_skin.py"
```

It reads `build/params.json` (run a build first, `--target native` is enough), draws every picture and writes
`skins/TUI.json` and `skins/Q-Links.json`. `tools/build.py` copies `skins/` into the plugin
as it is. Mock-ups of each page are written to `build/preview/`. `ui.json` is not used
while `skins/` exists; it is a plain fallback layout.

Captions and titles are part of the pictures, because the MPC can only draw live text in
three built-in fonts. They were rendered from fonts installed with Windows and Office
(MS Gothic, Times New Roman, Brush Script MT). No font files are in the plugin. The value
read-outs under the knobs are live text (Nexa).

## Building

```
python3 "plugins/VHS Vibes/tools/build_lto.py" "plugins/VHS Vibes"
```

This is the SDK's `tools/build.py` with link-time optimisation (`-flto`) added; all its
options work. The plain `tools/build.py` also builds the plugin, without LTO.

Left and right run through the effect chain together as one two-float vector, which the
compiler turns into NEON instructions on the MPC. Maths that would be a library call on the
32-bit MPCs (fmax, tanh, sin per sample) is avoided in the per-sample code.

## Tests

```
bash "plugins/VHS Vibes/tests/run.sh"
```

Builds four small programs with zig and runs them on the computer (Linux or WSL):
`host_test` (what the plugin tells the host and when), `mix_test`, `deck_test` and
`levels_test` (measurements of each stage to read through). The first two end in PASS or
FAIL.

## Honest status

- Built for 32-bit and 64-bit MPCs; passes the SDK host simulator (the one warning, sound
  with silent input, is the noise section doing its job).
- Versions 1.0 to 1.2 ran on an MPC One+. The 1.3 changes (a code review's bug fixes and
  speed-ups, the latency report, the Animation switch) are tested on the PC only so far.
- What comes from real VCR data: heads switch every video field (59.94 Hz); a Hi-Fi deck
  that loses its carrier makes static and falls back to the mono linear track; camcorder
  level controls duck fast, hold for seconds and creep back; Hi-Fi noise reduction breathes.
  The times and depths of those effects are not documented anywhere I could find and are
  set by ear.
- CPU: about 0.2 % of one core on a desktop PC for the default program. The 32-bit MPCs are
  much slower; a rough guess is a few percent of one core, not measured. Stages whose amount
  is zero are skipped, so clean settings cost less: Init takes about a third of Home Recording.
- Latency: the two wobble stages hold the sound back by their resting length: 12 samples
  with the wobbles off, about 5 ms on Home Recording, more with slow, deep wobble. The plugin
  reports that length to the host half a second after it stops changing. Whether the MPC
  uses it to keep the track in time has not been checked.
- After a program change the output is shut for the length of the delays and then fades in
  over about 20 ms, so presets switch without a click or a pitch slide.
- The sample rate is not assumed, but only 44.1 kHz has been tested.

## Licence

MIT (see the source file). The Reaktor ensemble that inspired the stage order is not used,
copied or required.
