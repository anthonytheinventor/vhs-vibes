<!--
manual.md - the text of the VHS Vibes user manual, in reading order.

Rebuild the PDF from the plugin folder with:      python tools/make_manual.py

What lives where
    manual.md      this file: chapters and everything that is not one control or one program
    controls.md    one entry per control      (the generator fails if a parameter has none)
    programs.md    one line per program       (the generator fails if a program has none)
    manual.css     the look

What is filled in by the generator (do not type these by hand)
    {{version}} {{controls}} {{pages}} {{programs}} {{vendor}} {{url}}
    [[toc]]                 the contents list, with page numbers
    [[controls TAPE]]       every control of that page, in screen order, with its printed name,
                            block, Q-Link, range and default (from build/params.json,
                            src/*.cpp, tools/make_skin.py, skins/Q-Links.json)
    [[programs]]            the program list, in the MPC's order
    [[switches]]            every on/off switch
    [[qlink-map]] [[program-qlinks]]                 figure and table of the SCREEN chapter
    [[screenshot TAPE | caption]]                    a page's picture, whole and unmarked
    [[pages]]                                        the table of pages and their blocks
    [[pagebreak]]

Blocks
    ::: cover / ::: chapter / ::: page NAME      "key: value" lines, closed by :::
    ::: signal                                   the signal path: Name | PAGE | remark | flags
    ::: anatomy                                  the labelled screen: region | text
    ::: note Title ... :::                       a boxed note
    ::: aside ... :::                            one cryptic line between sections

Text is a small Markdown: ## and ### headings, paragraphs, "- " and "1. " lists, | tables |,
**bold**, *italic*, `code`, ^NAME^ for a name as printed on the screen. The cryptic voice belongs in titles, epigraphs and asides. The
explanation of a control is always plain and exact.
-->

::: cover
kana: ビデオカセット　・　ａｅｓｔｈｅｔｉｃ　ｔａｐｅ　ｄｅｇｒａｄａｔｉｏｎ　・　Ｔ－１２０
manual: ＵＳＥＲ　ＭＡＮＵＡＬ　取扱説明書
line: an effect plugin for Akai MPC standalone hardware, by {{vendor}}
epigraph: You found this tape in a box with no label. Somebody recorded over it, twice. The picture is gone. The sound stayed. It is not the sound that went in.
:::

::: chapter
id: contents
title: ＣＯＮＴＥＮＴＳ
jp: 目次
word: INDEX
epigraph: Fast forward to the part you came for.
toc: -
:::

[[toc]]

::: chapter
id: play
title: ＰＬＡＹ
jp: 再生
word: PLAY
toc: What it is
epigraph: Press it. What comes back is almost your song.
:::

**VHS Vibes makes clean audio sound like it lived on a video cassette.** Put it on a track and the pitch starts to swim, the highs soften, the hiss comes up in the quiet parts and, if you let it, the machine loses the tape for a moment and finds it again.

- An insert effect, stereo in, stereo out, for Akai MPC standalone hardware.
- {{controls}} controls on {{pages}} pages, {{programs}} built-in programs.
- A hot pink cassette on the MPC's screen, with reels that turn when sound passes.
- Every noise is synthesised: no samples, nothing to load.
- This manual describes version {{version}}.

[[screenshot TAPE | The first of the five pages, as the MPC shows it.]]

## The five pages

[[pages]]

::: chapter
id: insert
title: ＩＮＳＥＲＴ　ＴＡＰＥ
jp: 挿入
word: INSERT
toc: Install and first sound
epigraph: The machine takes the tape out of your hand. It has done this before.
:::

## Install

VHS Vibes needs **MPC OS 3.10.1 or later**.

1. Take the zip from the Releases page of the project: `{{url}}/releases`
2. Copy the folder `{{vendor}} - VST - VHS Vibes` into a folder named `VST2 Plugins` at the top of a USB drive, an SD card or the MPC's internal drive.
3. On the MPC open **Preferences > Enhancements (Beta)** and switch on both **Enable Enhancements** and **External Plugin Support**.
4. Restart the MPC.
5. VHS Vibes is in the plugin browser under **External Plugins**.

`INSTALL.txt` in the zip says the same at more length.

::: note The two switches
Both preference switches must be on: **Enable Enhancements** and **External Plugin Support**. Restart the MPC after switching them.
:::

## First sound

1. Load VHS Vibes as an insert effect on a track and play something through it.
2. The plugin opens on **Home Recording**: a family tape in good shape, with gentle wobble, some hiss and a level control that rides a little. The reels on the screen turn while sound passes.
3. There is hiss and hum even when nothing plays. That is the tape. The Noise switch on the NOISE page turns it off.
4. Step through the programs to hear what the machine can do. They are listed in the chapter ＴＡＰＥＳ　ＩＮ　ＴＨＥ　ＢＯＸ.
5. To build a sound from nothing, choose the program **Init**: a blank tape, everything neutral, the sound passing through unchanged. Then turn up one thing at a time.
6. If the effect is too much, turn **Mix** down on the TAPE page to let the untouched input back in, or start from **Subtle Glue**.

::: chapter
id: screen
title: ＳＣＲＥＥＮ
jp: スクリーン
word: SCREEN
toc: How the screen and the Q-Links are organised
epigraph: A cassette with a window. Behind the window, two wheels and all the time in the world.
:::

## The cassette

Every page is the same cassette with a different set of controls on its face.

::: anatomy
label | **Title label.** The name of the plugin.
window | **Reel window.** The two reels turn while sound passes.
sticker | **Page sticker.** The name of the page, its number out of five, the sun and the grid.
panel | **Face label.** The dark panel that carries the controls: two rows of eight places.
block | **A block.** Four places under one title. One block is one Q-Link group.
caption | **The name of a control**, printed above it.
value | **The value window** under a knob shows the current value.
:::

There are three kinds of control:

| On screen | What it is | How it reads |
|---|---|---|
| A knob with a window under it | A continuous value | The window shows the value with its unit, for example `25 %` or `9.0 kHz`. |
| A pill that says ON or OFF | A switch | Lit pink when on. |
| Two or four buttons | A selector | The lit button is the current choice. |

The name printed above a control is sometimes shorter than the name the MPC uses for the same parameter elsewhere, for example in automation: the knob printed ^RATE^ in the FLUTTER block is the parameter `Flut Rate`. This manual gives both for every control.

The screens in this manual are drawn by the plugin's skin script with every control at its default; they are not photos of an MPC.

## The Q-Links

Each block of four controls is one Q-Link group. **Touch a control and the four Q-Link knobs follow that block**, left to right. The MPC marks the selected block with a Q in its top left corner, which is why the block titles start a little to the right.

[[qlink-map]]

A block with fewer than four controls leaves the remaining Q-Links of its group unused.

## The Q-Links in Program mode

In the MPC's Program Q-Link mode the sixteen Q-Links do not follow the page. They hold this fixed set:

[[program-qlinks]]

## It moves

- The two reels turn while sound comes in or the song is playing, coast to a stop about two seconds after, and lurch with the wobble.
- The neon grid on the page sticker rolls with the reels.
- The sun glows with the output level.

These are driven by two extra read-only parameters, **Reel** and **Level**, which the plugin updates up to 20 times a second. They are not settings, cannot be automated and are best left alone. The **Animation** switch on the DECK page turns the movement off.

::: chapter
id: path
title: ＳＩＧＮＡＬ　ＰＡＴＨ
jp: 信号経路
word: PATH
toc: The signal path as a diagram
epigraph: Two wobbles, because a copy of a copy wobbles twice.
:::

The sound goes through the machine in this order. Each box is one stage; its colour says which page its controls are on.

::: signal
Noise | NOISE | if Position is Pre | optional
Chorus | INPUT | modulated delay |
Mic | INPUT | cheap microphone |
AGC | DECK | level control, seconds |
Tone | TAPE | tilt |
NR record | DECK | lifts quiet highs |
Drive | TAPE | preamp soft clip | hq
Wear | TAPE | slew limit | hq
Comp | TAPE | compressor | hq
Saturator | TAPE | 2-Band or VHS | hq
Hi Cut | TAPE | 4-pole lowpass |
Wobble 1 | WOBBLE | Warp, Flutter, Scrape |
Noise | NOISE | if Position is Post | optional
Wobble 2 | WOBBLE | Wow, Flutter |
Lo Cut, Hi Cut | WOBBLE | the FILTER block |
Drift | DECK | comes and goes |
Tracking | DECK | static, backup track |
Head Buzz | NOISE | holds the signal |
Whistle | NOISE | 15.7 kHz |
Dropouts | TAPE | short dips |
NR playback | DECK | cuts quiet highs |
Mono | TAPE | sums left and right |
Mix | TAPE | dry / wet |
Output | TAPE | level |
:::

## What the order means

- **Noise has two doors.** With Position on Pre the noise goes in at the very start and is compressed, clipped and wobbled with the music. On Post it is added after the first wobble and stays clean.
- **The stages marked HQ are the ones that distort.** With the HQ switch on they run at twice the sample rate.
- **Two wobbles.** Wobble 1 is the tape. Wobble 2 is the tape after one more copy, with its own two filters behind it.
- **The Whistle comes late**, so that no filter and no wobble touches it. It never wavers.
- **NR is in two halves.** The record side sits between Tone and Drive, the playback side just before Mono. Everything between them is the tape, and whatever the tape does there makes the playback side mistrack.
- **Breathing is not a stage.** It makes the hiss rise and fall with the signal and holds the head buzz down in the pauses.
- **Mix blends the untouched input back in** at the end. The dry signal is delayed by the resting length of the two wobble delays so that it lines up with the tape path. At 100 % the dry path is not computed at all.

::: page TAPE
toc: Preamp, output and the saturator
epigraph: What the tape did to the sound.
:::

[[controls TAPE]]

[[pagebreak]]

## The three knobs that change their name

The **Type** switch changes what the saturator stage is, and with it what three of its knobs mean. Their captions on the screen change with it: SPLIT, LO SAT and HI SAT in 2-Band become BIAS, REC LEVEL and TAPE SPEED in VHS. They stay the same three knobs, on the same Q-Links, and the MPC still lists them as Split, Lo Sat and Hi Sat. Lows and Highs keep their names but change their job as well.

| Knob | Type = 2-Band | Type = VHS |
|---|---|---|
| ^SPLIT^ | **Split.** The frequency at which the signal is divided into a low band and a high band. | **Bias.** How well the deck is set up for the tape. 2.5 kHz, where the knob rests, is right. Lower is too little bias: brighter, grittier, distorting sooner. Higher is too much: duller, the highs giving way sooner. The read-out stays in Hz. |
| ^LO SAT^ | **Lo Sat.** How hard the low band is clipped. | **Rec Level.** The record level, from 10 dB under to 8 dB over the deck's reference level, which is at 56 %: there a tone at -8 dBFS has 3 % distortion, as on a real deck. Quiet sound comes back at the level it went in whatever the setting; what changes is how much of the loud sound the tape bends. |
| ^HI SAT^ | **Hi Sat.** How hard the high band is clipped. | **Tape Speed.** The tape speed, from SP at 0 % to EP at 100 %. The slower, the sooner the highs give way and the lower the top of the band. |
| ^LOWS^ | Level of the low band into its clipper. | Trims the result below 1 kHz. |
| ^HIGHS^ | Level of the high band into its clipper. | Trims the result above 1 kHz. |

**2-Band** is a crossover with a soft clipper on each side. **VHS** is a model of what the tape of a VHS deck's linear track does (the Hi-Fi track does not saturate this way): quiet sound passes unchanged; loud sound loses its highs first and goes soft, the way slow tape does. The programs SP Tape and EP Tape show it at its two speeds.

::: note What is documented and what is estimated in Type = VHS
It follows the figures an Audio Engineering Society tutorial on videotape duplication gives for the VHS linear track in SP: 33.35 mm/s, a record pre-emphasis of 120 microseconds, reference level 100 nWb/m (taken as -8 dBFS) and 3 % third-harmonic distortion at that reference level, a band that reaches about 10 kHz, and the direction of the bias effects. Deck specification sheets give EP's speed (11.12 mm/s) and band (4 to 6 kHz).

Estimates: that EP behaves like SP at a third of every frequency, how far the pre-emphasis keeps rising, the shape of the curve past the 3 % point and the sizes of the bias effects. There is no head bump: at these tape speeds it lies below 20 Hz. Driven hard, the stage clips highs it has just lifted, which folds back into the band without HQ; HQ is on in a new instance for that reason.
:::

::: page WOBBLE
toc: Two pitch wobbles, scrape and two filters
epigraph: The motor is tired.
:::

[[controls WOBBLE]]

## Turning a wobble knob while sound plays

Turning a wobble depth or rate changes the plugin's own delay. A small move is followed at a limited pace, so the pitch bends by a couple of percent at most. After a big jump, once the knob rests, the sound crossfades to the new delay in 25 ms (a brief blur) and the bend is over.

::: page NOISE
toc: Hum, buzz, hiss, crackle and the whistle
epigraph: Everything the tape adds when nobody is talking.
:::

[[controls NOISE]]

## Noise and silence

- The effect makes noise even with no input. Set Master to 0 %, or switch Noise off, for a silent background.
- With Position on Pre and Comp or AGC turned up, the noise swells whenever the music stops. That is how a real recorder's level control behaves; turn Master down if it is too much.

::: page INPUT
toc: Chorus and microphone
epigraph: What it was recorded through.
:::

[[controls INPUT]]

::: page DECK
toc: The machine's faults, NR and HQ
epigraph: The tape got older. So did the machine.
:::

Faults of the machine rather than the tape. Each does something no other control can.

[[controls DECK]]

## NR: the four positions

NR is B-type noise reduction. On the record side it lifts quiet highs by up to 10 dB; on the playback side it cuts them again, and the hiss with them. Loud sounds pass untouched. The switch chooses which of the two sides is in.

| Position | The tape was | What you hear |
|---|---|---|
| ^OFF^ | recorded without, played without | No noise reduction. |
| ^REC^ | recorded with it, played without | Bright, airy, a little squashed, all the hiss. |
| ^PLAY^ | recorded without, played with it | Dull, the highs ducking in quiet passages. |
| ^ON^ | recorded with it, played with it | On a clean tape it only removes hiss. The more the tape has lost in between (Hi Cut, Wear, Dropouts, Drift), the more the playback side mistracks, and the duller and more unsteady the top end gets. |

The programs Wrong Switch (REC) and Stereo Rental (ON) are built on it. Switching NR fades it in or out over 8 ms.

::: note Where NR comes from
Early stereo VHS decks had Dolby B-type noise reduction on their linear tracks. NR is a model of that circuit, not of its effect: the Signetics NE545 application circuit, with the side-chain filter and its real part values, the sliding band, the overshoot clipper and the two-speed smoothing.

What the datasheets do not give as numbers was fitted to the figures they publish. The record side meets all eight of them (1.4, 2, 5 and 10 kHz at -20 to -40 dB) within 0.6 dB at every sample rate up to 192 kHz, where the chips are specified to 2 dB. The playback side undoes the record side to about -100 dB when nothing lies between them. The reference level of the circuit is taken as -8 dBFS.
:::

## HQ

HQ runs the stages that distort (Drive, Wear, Comp, Saturator) at twice the sample rate.

- **Off:** bright sounds that hit those stages fold back into the midrange as tones that were never played. The plugin sounds as it did before HQ existed.
- **On:** that is gone for highs at ordinary levels. A full-level high tone that squares off in the clippers still folds back, 6 to 20 dB quieter.
- On takes about a quarter more CPU.
- Switching it fades the sound out and back in at the other rate.
- HQ is on in a new instance. Choosing a program does not change it.

::: chapter
id: switches
title: ＳＷＩＴＣＨＥＳ
jp: スイッチ
word: SWITCHES
toc: The master switches
epigraph: Some things are simply on. Or they are not.
:::

Every section of the machine has its own switch, so a part can be taken out without touching its knobs. These are all of them.

[[switches]]

## What a switch leaves in

- **Saturator Power** off: Hi Cut stays in.
- **Wobble** off: the two filters of the WOBBLE page stay in.
- **Noise** off: silences every noise, the Whistle and the head-switching buzz included.
- **Deck Power** off: NR, HQ and Animation are not affected.

## Two switches the programs leave alone

**Animation** and **HQ** are not part of the programs. Choosing a program does not change either; they stay as you set them.

::: chapter
id: programs
title: ＴＡＰＥＳ　ＩＮ　ＴＨＥ　ＢＯＸ
jp: プログラム
word: TAPES
toc: All {{programs}} programs
epigraph: {{programs}} tapes in a box. The labels are handwritten.
:::

Choosing a program fades the sound out, starts the new tape and fades it in.

[[programs]]

## Three recipes

- **A slow-speed (EP) tape:** set Hi Cut near 6 kHz, Lo Cut near 100 Hz and raise Hiss.
- **A standard-speed (SP) tape:** Hi Cut about 10 kHz, Lo Cut about 50 Hz.
- **A clean Hi-Fi sound:** keep the wobbles and the noise low.

::: chapter
id: notes
title: ＮＯＴＥＳ
jp: ノート
word: NOTES
toc: Notes from the back of the box
epigraph: Written in pencil on the back of the box, by someone who knew the machine.
:::

## How it behaves

- **Program changes.** Choosing a program fades the sound out, starts the new tape and fades it in.
- **Wobble knobs.** Turning a wobble depth or rate changes the plugin's own delay. A small move bends the pitch by a couple of percent at most; after a big jump the sound crossfades to the new delay in 25 ms.
- **Timing.** The wobble delays hold the sound back by their resting length, a few milliseconds on most programs. That length is reported to the host as the plugin's latency, half a second after it stops changing, so a host that compensates keeps the track in time.
- **Mix below 100 %.** The dry signal is delayed to line up with the tape path. With the wobbles off the blend is clean; with them on, dry and wet drift against each other and it sounds like a chorus.
- **Noise with no input.** The effect makes noise even with nothing playing. Set Master to 0 %, or switch Noise off, for a silent background.
- **Noise that swells.** With Position on Pre and Comp or AGC turned up, the noise swells whenever the music stops. Turn Master down if it is too much.
- **Skipped stages.** A stage whose amount is zero is skipped.

## What is taken from real machines

| From VCR manuals and servicing books | In the plugin |
|---|---|
| The heads switch once per video field, 59.94 times a second, and the join is audible as a buzz on high notes. | Head Buzz |
| A Hi-Fi deck that loses its carrier makes static and falls back to the mono linear track. The right Hi-Fi channel fails before the left. | Tracking |
| Camcorder level controls duck fast, hold for seconds and creep back up. | AGC |
| Hi-Fi noise reduction breathes: 5 ms up, 70 ms back. | Breathing |
| A dropout is the tape lifting off the head, which costs the highs far more than the lows. | Dropouts |
| The line whistle of a TV set sits at 15.734 kHz. | Whistle |

The other times and depths are not documented anywhere the author could find, and are set by ear. What is published and what is estimated in **Type = VHS** and in **NR** is said in the chapters ＴＡＰＥ and ＤＥＣＫ.

::: chapter
id: compat
title: ＯＬＤ　ＴＡＰＥＳ
jp: 互換性
word: OLD TAPES
toc: Projects made with older versions
epigraph: Nothing you recorded is lost. It only waits.
:::

**Projects made with 1.3 or 1.4 load with every setting in place.**

- New controls are only ever added after the existing ones, and each rests where the plugin sounds as it did without it: Scrape and Whistle at 0 %, NR at Off, Sat Type at 2-Band.
- No older program uses the newer controls.
- The newer programs come after Init, so every older program keeps its number.
- **HQ** is on in a new instance. A project keeps the HQ setting it was saved with; one saved before HQ existed opens with HQ off, so it sounds as it did.

## What arrived when

| Version | Controls | Programs |
|---|---|---|
| 1.1 | The Wobble and Noise switches | |
| 1.2 | The deck: Power, AGC, Drift, Tracking, Breathing. Mix | Bad Tracking, Six Hour Tape, Wedding Video, Hi-Fi Master, Edge Damage, Bootleg Concert, Public Access |
| 1.3 | Animation | |
| 1.4 | Scrape, Whistle, HQ | Sticky Tape, Tube TV, Worn Heads, Dirty Heads |
| 1.5 | NR, Sat Type | Stereo Rental, Wrong Switch, SP Tape, EP Tape |

::: chapter
id: trouble
title: ＡＤＪＵＳＴ　ＴＲＡＣＫＩＮＧ
jp: トラブルシューティング
word: TROUBLE
toc: Troubleshooting
epigraph: The picture rolls. Somewhere a small wheel wants turning.
:::

**Troubleshooting:** what can happen, why, and what to do.

| What happens | Why, and what to do |
|---|---|
| VHS Vibes is not in the plugin browser. | Check that the MPC runs MPC OS 3.10.1 or later; that **Enable Enhancements** and **External Plugin Support** are both on under Preferences > Enhancements (Beta); that the MPC was restarted afterwards; and that the folder `{{vendor}} - VST - VHS Vibes` sits inside a folder named `VST2 Plugins` at the top of the drive. Look under **External Plugins**. |
| It hisses and hums with nothing playing. | That is the tape noise. Switch **Noise** off on the NOISE page, or set **Master** to 0 %. |
| The noise swells when the music stops. | **Position** is on Pre and **Comp** or **AGC** is turned up. Turn **Master** down, or set Position to Post. |
| With Mix below 100 % it sounds like a chorus. | The dry input is steady and the tape path wobbles. Switch **Wobble** off for a clean blend, or keep Mix at 100 %. |
| The pitch bends or blurs when I turn a wobble knob. | The knob changes the plugin's own delay. It settles as soon as the knob rests. |
| The sound dips for a moment when I choose a program, or switch HQ or Sat Type. | Each of these fades the sound out and back in. |
| AGC, Drift, Tracking and Breathing do nothing. | **Power** on the DECK page is off. The programs Init and Clean Widener switch it off. |
| Split, Lows, Lo Sat, Highs and Hi Sat do nothing. | **Power** in the SATURATOR block is off. |
| Split, Lo Sat and Hi Sat do not do what their names say. | **Type** is on VHS, where they are Bias, Level and Speed. See the table in the chapter ＴＡＰＥ. |
| Head Buzz, Whistle or Breathing cannot be heard. | Head Buzz and Whistle follow **Master** and the **Noise** switch. The Whistle is at 15.7 kHz; not everyone can hear it. Breathing shapes Hiss and Head Buzz, so it needs one of them turned up. |
| A new program did not change HQ or Animation. | Those two switches are not part of the programs. |
| The reels stand still. | They turn only while sound comes in or the song is playing. Or **Animation** is off. |
| The MPC is short of CPU. | Switch **HQ** off: on, it takes about a quarter more. Switching **Animation** off saves the work of updating the screen. |

::: chapter
id: rewind
title: ＲＥＷＩＮＤ
jp: 巻き戻し
word: REWIND
toc: Credits and licence
epigraph: The counter runs back to zero. The tape remembers anyway.
:::

## Credits

**VHS Vibes** {{version}}, by **{{vendor}}**.

Inspired by the idea of James Peck's "VHS Audio Degradation Suite" for Reaktor: the order of the stages and what each one is for. All code is original. Nothing is taken from the Reaktor ensemble and no samples are used: every noise is synthesised.

## Licence

VHS Vibes is released under the **MIT licence**. Copyright (c) 2026 {{vendor}}. The full text is in the file `LICENSE` of the project.

Project: `{{url}}`

## Colophon

The captions and titles on the plugin's screen, and in this manual, are set in MS Gothic, Times New Roman and Brush Script MT; the running text is Roboto. The cassette, the suns and the grids are drawn by script.

::: aside
ｂｅ　ｋｉｎｄ　・　ｒｅｗｉｎｄ
:::
