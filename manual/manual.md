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
    ::: anatomy                                  the labeled screen: region | text
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
- Every noise is synthesized: no samples, nothing to load.
- Six parts are built from published figures: the tape of the VHS linear track, its head out of line (Azimuth), the FM sound of the Hi-Fi track, the noise reduction circuit of early stereo decks, the four modeled microphones of the INPUT page, and the record level control of an early VHS deck. The chapter ＩＮＳＩＤＥ　ＴＨＥ　ＭＡＣＨＩＮＥ opens them up.
- With **Visual Vibes** on, the picture on the screen shows what the sound is doing.
- This manual describes version {{version}}.

[[screenshot TAPE | The first of the five pages, as the MPC shows it.]]

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

## First sound

1. Load VHS Vibes as an insert effect on a track and play something through it.
2. The plugin opens on **Home Recording**: a family tape in good shape, with gentle wobble, some hiss and a level control that rides a little. The reels on the screen turn while sound passes.
3. There is hiss and hum even when nothing plays. That is the tape. The **Power** switch on the NOISE page turns it off.
4. Step through the programs to hear what the machine can do. They are listed in the chapter ＴＡＰＥＳ　ＩＮ　ＴＨＥ　ＢＯＸ.
5. To build a sound from nothing, choose the program **Init**: a blank tape, everything neutral, the sound passing through unchanged. Then turn up one thing at a time.
6. If the effect is too much, turn **Mix** down on the TAPE page to let the untouched input back in, or start from **Subtle Glue**.

::: chapter
id: screen
title: ＳＣＲＥＥＮ
jp: スクリーン
word: SCREEN
toc: How the screen and the Q-Links are organized
epigraph: A cassette with a window. Behind the window, two wheels and all the time in the world.
:::

## The five pages

[[pages]]

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
| Two, three or four buttons | A selector | The lit button is the current choice. |

The name printed above a control is sometimes shorter than the name the MPC uses for the same parameter elsewhere, for example in automation: the knob printed ^RATE^ in the FLUTTER block is the parameter `Flut Rate`. This manual gives both for every control.

The screens shown here have every control at its default.

**Type** on the TAPE page also recolors controls. With Type on VHS, the knobs that work for the tape model turn mint green. With Type on Hi-Fi, the ten that work for the Hi-Fi track turn sunset orange, on whichever page they are. The lit Type button wears the same shade. The **Azimuth** knob on the DECK page is there only while Type is VHS.

## The Q-Links

Each block of four controls is one Q-Link group. **Touch a control and the four Q-Link knobs follow that block**, left to right. The MPC marks the selected block with a Q in its top left corner, which is why the block titles start a little to the right.

[[qlink-map]]

A block with fewer than four controls leaves the remaining Q-Links of its group unused.

## The Q-Links in Program mode

In the MPC's Program Q-Link mode the sixteen Q-Links do not follow the page. They hold this fixed set:

[[program-qlinks]]

## It moves

The two reels turn while sound comes in or the song is playing, coast to a stop about two seconds after, and lurch with the wobble. The neon grid on the page sticker rolls with them, and the sun glows with the output level. The **Animation** switch on the DECK page turns the movement off.

Two extra read-only parameters, **Reel** and **Level**, drive the movement; the plugin updates them up to 20 times a second. They are not settings, cannot be automated and are best left alone.

## The picture shows the sound

With **Visual Vibes** on (DECK page, SCREEN block), the picture on the page sticker plays like a worn video tape of what the plugin is doing:

| On the picture | What moves it |
|---|---|
| The sun takes the Type's shade: pink for Simple, mint for VHS, orange for Hi-Fi | Type |
| Snow, in four strengths | the hiss as it is heard; in Hi-Fi it rises as the Carrier comes down |
| A strip of noise along the bottom, in three strengths | Head Buzz |
| A white streak for a moment | each dropout |
| A band of noise rolling up the picture | the tracking lost: Tracking in Simple and VHS, every fall to the linear track in Hi-Fi |
| The grid bends, in three steps | the four wobble depths together (WOBBLE's power off straightens it) |
| A haze, in five steps | Hi Cut below 7 kHz, thickest at 1 kHz |

- Visual Vibes is off in a new instance and in a project from an older version.
- **Animation** is the master switch: with it off, neither moves.
- Choosing a program changes neither, and neither changes the sound.
- Five more read-only parameters, **Snow**, **Head**, **Streak**, **Grid** and **Haze**, carry the picture. Like Reel and Level, they are not settings and are best left alone.

::: chapter
id: path
title: ＳＩＧＮＡＬ　ＰＡＴＨ
jp: 信号経路
word: PATH
toc: The signal path as a diagram
epigraph: A copy of a copy wobbles twice.
:::

The sound goes through the machine in this order. Each box is one stage; its color says which page its controls are on.

::: signal
Noise | NOISE | if Position is Pre | optional
Chorus | INPUT | modulated delay |
Mic | INPUT | period microphone |
AGC | DECK | level control, seconds |
Tilt | TAPE | lows against highs |
NR record | DECK | lifts quiet highs |
Drive | TAPE | preamp soft clip | hq
Slew | TAPE | rate limit | hq
Comp | TAPE | compressor | hq
Vibe | TAPE | Simple, VHS or Hi-Fi | hq
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
- **The Whistle comes late**, after every filter and both wobbles, so it never wavers. Dropouts dip it with the rest of the sound, unless the NR playback side is on: then it comes after that, at the very end.
- **NR is in two halves.** The record side sits between Tilt and Drive, the playback side just before Mono. Everything between them is the tape, and whatever the tape does there makes the playback side mistrack.
- **Breathing is not a stage.** It makes the hiss rise and fall with the signal and holds the head buzz down in the pauses.
- **Hi-Fi gathers the deck's faults into one place.** With Type on Hi-Fi and VIBE's power on, the Hi-Fi track in the Vibe box makes the hiss, crackle, head buzz, breathing, tracking, drift and dropouts itself, inside the track, and their own boxes further down rest. B-Type NR rests too. Everything else stays where it is.
- **Azimuth sits inside the VHS saturator**, between the tape and the playback head, so the B-Type NR playback side hears what the tilted head lost.
- **Mix blends the untouched input back in** at the end. The dry signal is delayed by the resting length of the two wobble delays so that it lines up with the tape path. At 100 % the dry path is not computed at all.

::: page TAPE
toc: Preamp, output and the VIBE stage
epigraph: What the tape did to the sound.
:::

[[controls TAPE]]

[[pagebreak]]

## The three knobs that change their name

The **Type** switch in the VIBE block changes what the VIBE stage is, and with it what three of its knobs mean. It has three buttons, stacked: SIMPLE, VHS and HI-FI.

- **The captions change.** SPLIT, LO SAT and HI SAT in Simple become BIAS, REC LEVEL and TAPE SPEED in VHS, and CAL, REC LEVEL and CARRIER in Hi-Fi.
- **The read-outs change.** REC LEVEL reads in dB in both. TAPE SPEED reads SP, LP or EP at those three speeds, the names of other formats' speeds where they fall (PAL SP, Beta II, PAL Beta, Beta III, PAL LP), and the speed in mm/s between them. BIAS reads "right", or a percentage: minus is too little bias, plus too much. CARRIER reads 0 dB down to -20 dB. CAL reads "right", or the error in dB.
- **The shade changes.** In VHS the three knobs, their captions and the VHS button turn mint green. In Hi-Fi the three knobs and the HI-FI button turn sunset orange, and so do seven more on other pages, which the Hi-Fi track takes over (below). So you can see which controls the mode has taken.
- **The knobs stay.** They are the same knobs on the same Q-Links, and the MPC still lists them as Split, Lo Sat and Hi Sat (the Type switch as Sat Type, and VIBE's power switch as Saturator).
- **Lows and Highs** keep their names but change their job as well.

| Knob | Type = Simple | Type = VHS | Type = Hi-Fi |
|---|---|---|---|
| ^SPLIT^ | **Split.** The frequency at which the signal is divided into a low band and a high band. | **Bias.** How well the deck is set up for the tape. 2.5 kHz, where the knob rests, is right. Lower is too little bias: brighter, grittier, distorting sooner. Higher is too much: duller, the highs giving way sooner. | **Cal.** Whether playback is set to the same level as the record side: right where the knob rests, -3 dB at the bottom, +3 dB at the top. The error comes out doubled and grows with the level, so loud and quiet parts move apart or together and the sound pumps. |
| ^LO SAT^ | **Lo Sat.** How hard the low band is clipped. Reads as the gain into its clipper, 0.0 to +20.0 dB. | **Rec Level.** The record level, from 10 dB under to 8 dB over the deck's reference level, which is at 56 %: there a tone at -8 dBFS has 3 % distortion, as on a real deck. Quiet sound comes back at the level it went in whatever the setting; what changes is how much of the loud sound the tape bends. | **Rec Level.** From -10 dB to +28 dB, 0 dB at 56 %. How hard the sound meets the Hi-Fi track's hard ceiling. Nothing bends below it; above it the peaks are cut flat. |
| ^HI SAT^ | **Hi Sat.** How hard the high band is clipped. Reads as the gain into its clipper, 0.0 to +20.0 dB. | **Tape Speed.** The tape speed, from SP (33.35 mm/s) through LP (16.67 mm/s, at 63 % of the knob) to EP (11.12 mm/s). The slower, the sooner the highs give way and the lower the top of the band. | **Carrier.** How strong the FM signal off the tape is, 0 to -20 dB. Every dB down is a dB more hiss; from about -14 dB the deck falls to the linear track now and then, and from about -19 dB it hardly comes back. |
| ^LOWS^ | Level of the low band into its clipper. | Trims the result below 1 kHz. | Trims the result below 1 kHz. |
| ^HIGHS^ | Level of the high band into its clipper. | Trims the result above 1 kHz. | Trims the result above 1 kHz. |

- **Simple** is the two-band saturator, a crossover with a soft clipper on each side. Up to version 1.6 this choice was called 2-Band.
- **VHS** is a model of what the tape of a VHS deck's linear track does. Quiet sound passes unchanged; loud sound loses its highs first and goes soft, the way slow tape does. The programs SP Tape and EP Tape show it at its two speeds. The programs whose names begin with VHS are older programs with this saturator in place of the Simple one.
- **Hi-Fi** is the FM sound the rotating heads write under the picture. It does not saturate at all: it stays clean up to a hard ceiling. What gives it away is everything around the sound, below.

### What Hi-Fi takes over

With Type on Hi-Fi and VIBE's power on, seven controls on other pages stop driving their own stages and drive the Hi-Fi track instead. They keep their names and their places, and turn orange.

| Control | Page | In Hi-Fi |
|---|---|---|
| ^HISS^ | NOISE | The FM channel's noise. It rises and falls with the music by itself. 9 % at Master 0.0 dB is a real deck's. |
| ^CRACKLE^ | NOISE | How many clicks: how close the deck sits to the point where the FM signal breaks up. It does not move with Carrier. |
| ^HEAD BUZZ^ | NOISE | How badly the switch from one head to the other is hidden. 0 % is hidden as a deck does it. |
| ^BREATHING^ | DECK | The tape played on another deck, whose playback side follows the sound its own way: it pumps. |
| ^TRACKING^ | DECK | One head reading weaker, and dips of the signal, one to eight head passes long. |
| ^DRIFT^ | DECK | The signal's strength wandering slowly, up to 22 dB. |
| ^DROPOUTS^ | TAPE | Short losses of the signal: bursts of noise and clicks, holds, falls to the linear track. |

**B-Type NR** is ignored: it belongs to the linear track.

With VIBE's power off, all of these are their own again, and Hi-Fi sounds exactly as Simple does with VIBE's power off.

NOISE's power and Master work on Hiss, Crackle and Head Buzz in Hi-Fi as they do everywhere. **Deck Power** switches Breathing, Tracking and Drift.

### The fall to the linear track

Besides the Hi-Fi sound, every tape also carries the mono linear track along its edge. When the FM signal gets too weak, for a dip, a dropout or a low Carrier, the deck crossfades to it. The sound goes duller, 3 dB quieter and mono, with a hiss of its own (set by Hiss, twice as loud as the other Types' hiss).

The Hi-Fi sound comes back about a quarter of a second after the signal does, so each fall is heard as a short dull, hissy patch. When the signal is lost deeper still, the deck holds the sound for a moment, fading toward silence.

::: note Where the two tape models come from
**VHS** is built on the published figures of the VHS linear track: its tape speeds, its record pre-emphasis, its reference level and its 3 % distortion at that level. Driven hard, the stage clips highs it has just lifted; without HQ part of that folds back into the band. Keep HQ on. **Hi-Fi** is a real Hi-Fi deck's machinery, from the chips' datasheets, driven far out of spec, with the click amount given its own knob. How both work, what they measure and what is estimated is in the chapter ＩＮＳＩＤＥ　ＴＨＥ　ＭＡＣＨＩＮＥ.
:::

::: page WOBBLE
toc: Two pitch wobbles, scrape and two filters
epigraph: The motor is tired.
:::

[[controls WOBBLE]]

::: page NOISE
toc: Hum, buzz, hiss, crackle and the whistle
epigraph: Everything the tape adds when nobody is talking.
:::

[[controls NOISE]]

## Noise and silence

- The effect makes noise even with no input. Turn Master down to off, or switch NOISE's power off, for a silent background.
- With Position on Pre and Comp or AGC turned up, the noise swells whenever the music stops. That is how a real recorder's level control behaves; turn Master down if it is too much.
- While the Mic is on Modeled, the microphone adds its own noise: the capsule's hiss, or the phone line's. NOISE's power silences it; Master does not change it.

::: page INPUT
toc: Chorus and microphone
epigraph: What it was recorded through.
:::

[[controls INPUT]]

## The microphones: Simple and Modeled

The Mic plays the sound into a period microphone before the recorder hears it, as if it had been re-recorded through one. **Mic Type** chooses one of four: Camcorder, Dynamic, Phone or Boombox. **Mic Model** chooses which set of the four it is, Simple or Modeled. Switching either fades the mic out and back in.

### Simple

**Simple** is the set of the versions up to 1.5. Each type is one fixed tone curve, in stereo, with no noise and no level control of its own; Mic Gain is a level after it. Projects saved before 1.6 open on it, and the five older programs with the Mic on use it.

| Type | The curve |
|---|---|
| ^CAMCORDER^ | 180 Hz to 7.5 kHz, a 5 dB peak at 3.2 kHz |
| ^DYNAMIC^ | 90 Hz to 12 kHz, a 4 dB peak at 5 kHz |
| ^PHONE^ | 350 Hz to 3.4 kHz, a 6 dB peak at 1.8 kHz |
| ^BOOMBOX^ | 120 Hz to 6 kHz, a 4 dB peak at 900 Hz |

### Modeled

On **Modeled**, each type is a model of one kind of microphone people recorded through in the 1980s, built from its published figures: the response, how it overloads, how much it hisses, and for the Camcorder and the Boombox the level control of the recorder it sat in.

A new instance starts on Modeled, and the five programs named Mic - ... use it. The Camcorder, the Phone and the Boombox are mono, as the originals were; the Dynamic is in stereo.

| Type | Built on | Its sound |
|---|---|---|
| ^CAMCORDER^ | a camcorder's record chain: the electret capsule, the mic amp and the record ALC, from service manuals, datasheets and one measured camera | the camera across the room: it hears the source 16 dB quieter (estimate); a low cut near 50 Hz and a soft top |
| ^DYNAMIC^ | the Shure 515 moving-coil stage microphone's published curve | a gentle low cut (8 dB down at 100 Hz), a 5 dB presence peak at 5 kHz, a smaller one at 8 kHz, falling away above 10 kHz |
| ^PHONE^ | CCITT P.48, the reference telephone of the phone companies' standards body | 300 Hz to 3.4 kHz with steep edges, a carbon microphone's grit, the line's faint noise |
| ^BOOMBOX^ | the same capsule, a radio cassette's record ALC and its tape | the cassette's 50 Hz to 10 kHz, with its hiss |

What each one does to loud and quiet sound:

| Type | Its level | After a loud passage |
|---|---|---|
| ^CAMCORDER^ | the record level control holds everything from 61 to 83 dB SPL at the microphone within 1.2 dB of one level; louder than that, loud sounds and sudden onsets crunch. It catches a sound in 20 ms (estimate) | the gain comes back at 8 dB a second, the whole 21 dB in about 2.5 seconds (estimate), and the hiss in the pause rises by as much; after a sound louder than its range it first stays down longer, ten seconds or more |
| ^DYNAMIC^ | no level control: Mic Gain is a plain level | nothing changes |
| ^PHONE^ | no level control, but the carbon grows less sensitive as the sound gets quieter, so quiet words come through weaker still | nothing changes |
| ^BOOMBOX^ | the record level control (ALC) turns everything from a whisper to a shout into nearly one level | the gain comes back over about seven seconds; the hiss, the room and every quiet sound come up with it |

Sources, measurements and every estimate are in the chapter ＩＮＳＩＤＥ　ＴＨＥ　ＭＡＣＨＩＮＥ.

### Mic Gain: two meanings

On Simple, **Mic Gain** is a level after the curve.

On Modeled it sets how loud the sound is at the microphone, as if the source were louder or closer: a sound peaking at -8 dBFS counts as 104 dB SPL. That mapping is an assumption. Every type's response is 0 dB at 1 kHz.

The Camcorder and the Boombox then have a record level control, so what comes out of them is the level they record at. On those two, Mic Gain changes how hard they work more than how loud they are.

::: page DECK
toc: The machine's faults, B-Type NR and HQ
epigraph: The tape got older. So did the machine.
:::

The machine's own faults. Each does something no other control can.

[[controls DECK]]

## B-Type NR: the four positions

NR is B-type noise reduction. On the record side it lifts quiet highs by up to 10 dB; on the playback side it cuts them again, and the hiss with them. Loud sounds pass untouched. The switch chooses which of the two sides is in.

| Position | The tape was | What you hear |
|---|---|---|
| ^OFF^ | recorded without, played without | No noise reduction. |
| ^REC^ | recorded with it, played without | Bright, airy, a little squashed, all the hiss. |
| ^PLAY^ | recorded without, played with it | Dull, the highs ducking in quiet passages. |
| ^ON^ | recorded with it, played with it | On a clean tape it only removes hiss. The more the tape has lost in between (Hi Cut, Slew, Dropouts, Drift), the more the playback side mistracks, and the duller and more unsteady the top end gets. |

The programs Wrong Switch (REC) and Stereo Rental (ON) are built on it. Switching NR fades it in or out over 8 ms. With Type on Hi-Fi and VIBE's power on, NR is ignored: it belongs to the linear track.

::: note Where NR comes from
It is a part-by-part model of the circuit early stereo VHS decks had on their linear tracks. The circuit, the datasheet figures it meets and why it mistracks are in the chapter ＩＮＳＩＤＥ　ＴＨＥ　ＭＡＣＨＩＮＥ.
:::

## Azimuth: a tape from another deck

The ALIGNMENT block holds **Azimuth** while Type is VHS. It tilts the playback head of the linear track against the recording, from 0' to 60' (minutes of arc).

The treble cancels across the tilted head: first a dulling of the very top, then a hollow, notched top end that swishes as the tape wanders. The knob is fine at the bottom, where real tapes live. A tape from another deck is 3' to 8' off, 22 to 37 % of the knob; 60' is a head knocked out of line.

| Tilt | Knob | SP: 3 dB down / first notch | EP: 3 dB down / first notch |
|---|---|---|---|
| 3.75' | 25 % | 13.5 kHz / 30.6 kHz | 4.5 kHz / 10.2 kHz |
| 7.5' | 35 % | 6.8 kHz / 15.3 kHz | 2.3 kHz / 5.1 kHz |
| 15' | 50 % | 3.4 kHz / 7.6 kHz | 1.1 kHz / 2.5 kHz |
| 60' | 100 % | 850 Hz / 1.9 kHz | 280 Hz / 640 Hz |

- EP is three times as touchy as SP, because the tape runs three times slower.
- With B-Type NR on ^ON^ the loss grows: the decoder takes the dulled highs for quieter ones and cuts them further.
- It rests at 0', where it is not there at all. Outside VHS it does nothing.
- The programs VHS - Borrowed Tape and VHS - Wrong Deck EP use it.

## HQ

HQ runs the stages that distort (Drive, Slew, Comp, the VIBE stage) at twice the sample rate. Since 1.7 that includes the modeled microphones (Camcorder, Phone, Boombox) and the recording side of B-Type NR.

- **Off:** bright sounds that hit those stages fold back into the midrange as tones that were never played. The plugin sounds as it did before HQ existed.
- **On:** that is gone for highs at ordinary levels. A full-level high tone that squares off in the clippers still folds back, 6 to 20 dB quieter.
- On takes about a quarter more CPU.
- Switching it fades the sound out and back in at the other rate.
- HQ is on in a new instance. Choosing a program does not change it.

::: chapter
id: inside
title: ＩＮＳＩＤＥ　ＴＨＥ　ＭＡＣＨＩＮＥ
jp: 内部
word: INSIDE
toc: How the VHS and Hi-Fi tracks, Azimuth, B-Type NR, the microphones and the AGC are modeled
epigraph: Somebody took the lid off and wrote down what was there.
:::

Six parts of VHS Vibes are built from what was published about the machines: a format's own figures, a tape recording handbook's formula, the datasheets of the chips inside Hi-Fi decks, a chip's circuit diagram, the datasheets, standards and measurements of four microphones, and a deck's service manual. This chapter says what each is made of, what was measured, and what had to be estimated.

## The tape itself: Type = VHS

A VHS deck without Hi-Fi records its sound on a thin track along the edge of the tape, the linear track. The tape crawls past the head at 33 mm a second, slower than an audio cassette, on a coating made for pictures. **Type = VHS** is a model of what that track does to sound.

### The figures it is built on

| Figure | Value | Where it is from |
|---|---|---|
| Tape speed, SP | 33.35 mm/s | Audio Engineering Society tutorial on videotape duplication, table of audio recording formats, column VHS |
| Record pre-emphasis | 120 microseconds: the highs are lifted 6 dB per octave from 1.33 kHz | the same table |
| Reference level | 100 nWb/m, taken here as -8 dBFS | the same table |
| Distortion | 3 % third harmonic at reference level | the same table |
| Top of the band, SP | about 10 kHz | the same tutorial |
| Bias | too much sounds dull, too little bright and distorted | the same tutorial |
| Tape speed, EP | 11.12 mm/s | deck specification sheets |
| Top of the band, EP | 4 to 6 kHz | deck specification sheets |

The fourth line is the heart of it. An audio cassette reaches 3 % distortion 7 dB above its reference level and a studio machine 10 dB above. The VHS linear track is there at reference level. It has no headroom at all.

### What happens to the sound, step by step

1. **Record level.** The sound is made louder or quieter by the ^REC LEVEL^ knob, from 10 dB under to 8 dB over the deck's reference level.
2. **Pre-emphasis.** The highs are lifted, as the format prescribes, before they reach the tape. In SP the lift starts at 1.33 kHz; a 5 kHz sound arrives at the tape about 11 dB hotter than a bass note of the same level.
3. **The tape.** A smooth curve that bends more the harder it is driven and goes level at its ceiling. It is set so that a tone at reference level comes out with 3 % third harmonic.
4. **De-emphasis.** The playback side cuts the highs again, by exactly what step 2 lifted.
5. **Playback level.** Whatever step 1 added is taken off again, so the knob sets how hard the tape is driven and leaves the level of the result where it was.
6. **The top of the band.** A gentle roll-off, 3 dB down at 10 kHz in SP and at 4.5 kHz in EP.
7. **Bias.** A shelf that brightens or dulls the highs, depending on ^BIAS^.

### Why the highs go first

Steps 2 and 4 are exact opposites. For a quiet sound the tape in between is a straight line, so the two cancel and the sound comes back as it went in. A loud sound meets the curve with its highs already lifted. They hit the ceiling first; the lows pass under it. The cut of step 4 then lowers what is left of them. So the louder the music, the more top it loses, and bass and voice stay.

At the slow speed everything happens at a third of the frequency: the lift starts at 440 Hz, so in EP even the midrange runs out of tape.

### What comes out, measured

| Test | Result |
|---|---|
| A quiet sound (-40 dB), SP | unchanged at 100 Hz, 0.05 dB down at 1 kHz, 3 dB down at 10 kHz |
| A quiet sound (-40 dB), EP | unchanged at 100 Hz, 3 dB down at 4.5 kHz |
| 441 Hz at reference level, Rec Level 0 dB | 3.0 % third harmonic |
| The same tone 10 dB under reference | 0.3 % |
| The same tone recorded 8 dB over | 12 %, and it comes back 4.8 dB quieter |
| 10 dB under reference, SP | 441 Hz loses 0.1 dB, 7 kHz loses 2.5 dB |
| 2.5 kHz, 10 dB under reference | SP loses 0.5 dB, EP 2.3 dB |
| A low tone 4 dB under reference | 4.7 % with too little bias, 1.3 % with the bias right |
| 6 kHz played quietly | 0.6 dB up with too little bias, 5.4 dB down with too much |

The figures hold at every sample rate from 44.1 to 192 kHz.

### The three knobs, in the model's terms

- ^REC LEVEL^ is step 1. At 0 dB a tone at -8 dBFS sits exactly at the deck's reference level.
- ^TAPE SPEED^ moves steps 2, 4 and 6 together, from SP through LP to EP.
- ^BIAS^ at "right" does nothing. Toward too little, the tape's ceiling drops by up to 6 dB and the highs come up by up to 3 dB: bright and gritty. Toward too much, the highs lose headroom sooner and are cut by up to 6 dB: dull.

### What is estimated

No figures were found for EP's equalization or headroom; it is taken to behave like SP at a third of every frequency. Also estimated: that the lift of step 2 levels off 20 dB up, the shape of the curve beyond the 3 % point, and the sizes of the bias effects. There is no head bump: at these tape speeds it lies below 20 Hz.

::: note Hear it
Load **SP Tape** and play something with cymbals and a bass line. Turn ^REC LEVEL^ up: the cymbals soften and sink, the bass stays where it was. Now turn ^TAPE SPEED^ to EP and listen to the voice go the same way. Keep **HQ** on while you do: driven hard, the stage clips highs it has just lifted (see the note on the TAPE page).
:::

::: aside
Made for pictures. It carried the music anyway.
:::

[[pagebreak]]

## The head out of line: Azimuth

The head that reads the linear track has a narrow gap that should stand exactly square across the tape. On a tape recorded by another deck it rarely does. Tilted, the gap meets one edge of the track a moment before the other, and the head hears the average of the sound over that moment. Bass does not notice. Treble whose waves are about as short as that moment cancels itself. **Azimuth** is that loss, on the VHS tape model.

### The figures it is built on

| Figure | Value | Where it is from |
|---|---|---|
| The loss | 20 log of the size of sin x / x, with x = π f W tan(a) / v: 3 dB down at 0.443 of the first notch, notches at every multiple of it, faint lobes between | a 1967 tape recording handbook |
| Track width W | 1.0 mm, the mono linear track | an excerpt of the VHS standard, quoted elsewhere |
| Tape speed v | 33.35 mm/s in SP, a third of it in EP | as for Type = VHS |
| A well-set deck | 0' to 3' off | worked out from the service procedure (peaking an alignment tape) |
| A tape from another deck | 3' to 8' off | worked out from the tolerances; the spread is an estimate |
| The tilt wandering as the tape plays | about 2' peak to peak on a good professional machine; a slow wander from the tape's slitting, 0.3 to 0.9 m of tape long | measurements of professional tape machines, borrowed |

### What happens to the sound

1. **The tape.** Steps 1 to 3 of Type = VHS: the record level, the lift, the tape's curve.
2. **The tilted head.** The sound is averaged over the time the tilt takes to cross the track: W tan(a) / v. At 7.5' in SP that is 65 microseconds.
3. **The playback side.** Steps 4 to 7 of Type = VHS, and, much later, the B-Type NR decoder, which sees the dulled highs.
4. **The wander.** The tilt moves by up to 1' for the knob's position plus a tenth of the tilt (2' either way at 50 %): seven parts a slow drift, 0.03 to 0.3 Hz in SP and slower in EP, three parts a faster one, 1 to 3 Hz. That is the swish.

### What comes out, measured

| Test | Result |
|---|---|
| First notch and 3 dB point against the formula, 3.75' to 60', SP, LP and EP, 44.1 to 192 kHz | within 0.5 % while the notch lies below a fifth of the sample rate; within 2.5 % up to 0.4 of it |
| The response against the formula, down to -20 dB, below 20 kHz | within 0.6 dB |
| The first lobe past the notch (30', SP, 192 kHz) | -13.3 dB, as the formula has it |
| 10 kHz at SP, 5', over a minute | 2.9 dB down on average, swinging between 2.1 and 4.0 dB down |
| 10 kHz at -30 dB, SP, 5', B-Type NR off / on | 2.9 dB down / 4.1 dB down |
| At 0' | the stage is not there; outside Type = VHS, Azimuth at 60' changes nothing, bit for bit |

### What is estimated

No VHS deck's tolerance for this tilt was found, and no measurement of one tape played on many decks. The ranges for a well-set deck and for a tape from another deck are worked out from the service procedure and from tape-standard tolerances; 60' at the top of the knob is a choice, a head knocked far out of line. The wander is borrowed from professional machines and audio cassettes, and its two speeds and their shares are estimates.

::: note Hear it
Load **VHS - Borrowed Tape** and play something with cymbals, then turn ^AZIMUTH^ back to 0' and up again: the shimmer dulls and starts to swish. Load **VHS - Wrong Deck EP** for the same at EP, where the first notch falls inside the band. Switch B-Type NR to ^ON^ and the top sinks further.
:::

::: aside
Two machines that never agreed on where up was.
:::

[[pagebreak]]

## The FM track: Type = Hi-Fi

A VHS deck marked Hi-Fi writes its sound a second way: as FM, laid deep into the tape by the spinning video heads, under the picture. It is far quieter than the linear track, and it fails in its own ways. **Type = Hi-Fi** is a real Hi-Fi deck's machinery, driven far out of spec, with the click amount given its own knob. At the bottom of every knob it is a deck in good order; the rest of each knob goes well past what a working deck does, because a deck in order is too clean to hear.

### The figures it is built on

The VHS Hi-Fi standard itself could not be read. Its values come through the datasheets of the chips that did the work inside the decks.

| Figure | Value | Where it is from |
|---|---|---|
| Reference level | 50 kHz of FM deviation at 1 kHz, taken here as a tone peaking at -8 dBFS | the chips' datasheets |
| Compander | 2:1 on record, 1:2 on playback, unity at reference level | the chips' datasheets |
| Its timing | 5 ms attack, 70 ms recovery (one chip: 4 and 65 ms) | the chips' datasheets |
| Its detector | a peak detector, the signal weighted toward the highs (240 and 24 microseconds) | one chip's datasheet and a secondary source |
| The emphasis | a noise-reduction lift (240 and 56 microseconds) and an FM lift (56 and 20 microseconds) | secondary sources, checked against the chips' figures at 10 kHz: +3.9 dB recording, -7.7 dB playback |
| The ceiling | 150 kHz of deviation, 9.5 dB over reference | the chips' datasheets |
| Channel noise | 55 dB under reference at full signal, rising 6 dB per octave; 1 dB more for every dB of signal lost | the 55 dB chosen inside the 54 to 60 dB measured on a bench; the rest is FM theory |
| Clicks | (B / 2√3) erfc(√CNR) a second, over a band B of 300 kHz | worked out from S. O. Rice's 1948 paper on FM noise; the band from a 1983 paper on VHS Hi-Fi |
| Head switch | 59.94 times a second, hidden by a 6 microsecond hold; what is left: 73 to 76 dB under a 10 kHz tone | the chips' datasheets |
| A weak signal | the sound held below -21 dB; the linear track after 0.3 ms below -17 dB; back after 7 to 8 turns of the head drum (233 to 267 ms) | the chips' datasheets |
| One head weaker | 3 to 5 dB is within service tolerance | a deck's service manual |
| Playback tolerances | level within 1 to 2 dB; the expander's slope 1.93 to 2.07 | deck documentation; the chips' datasheets |
| Two decks' detectors | times up to 20 % apart; peak or average detectors both in use | secondary sources |
| Dropouts | counted from 20 dB deep on the picture and from 15 microseconds; 1 to 10 a minute on a good tape, 80 on a poor one after 100 plays; the FM sound loses about half the picture's dB | tape patents; the share worked out from a recording formula |

### What happens to the sound, step by step

1. **Record level.** ^REC LEVEL^ makes the sound louder or quieter going in.
2. **The record side.** A lift of the highs, then the 2:1 compressor, which halves every distance in dB from the reference level: a sound 40 dB under reference goes onto the tape 20 dB under. Then the FM lift of the highs.
3. **The ceiling.** Past 9.5 dB over reference the FM deviation is cut off, hard. Through the compressor and the lifts, a steady 1 kHz tone reaches it 19 dB over reference, a 10 kHz tone 11.5 dB over.
4. **The channel.** What the FM receiver gives back: the sound, plus noise (^HISS^), plus clicks when the signal is weak (^CRACKLE^: each one the receiver slipping a whole cycle), plus, 59.94 times a second, the handover from one head to the other (^HEAD BUZZ^). The noise and the clicks follow the signal's strength: ^CARRIER^, a small wander of its own (1 dB, never more than 3.5), ^DRIFT^, ^TRACKING^'s dips and weaker head, and ^DROPOUTS^.
5. **The playback side.** The FM lift taken off, the ^CAL^ error, the 1:2 expander, which doubles every dB again and so pushes the noise down in quiet passages and lets it up under loud ones, the noise lift taken off, and the level brought back down by what step 1 added.
6. **A weak signal.** Below -21 dB the deck holds the sound, which fades toward silence. Below -17 dB for 0.3 ms it crossfades, in 3 ms, to the linear track: the VHS tape model of the same input, in mono, at SP, 3 dB quieter, with its own hiss at twice the other Types' level. It comes back 7 to 8 drum turns after the signal does.

With every knob at rest the whole path is transparent, apart from the ceiling and a residue of the head switch 74 dB down.

### Each knob, from a deck in order to far past it

| Knob | A deck in order | At the top, past it |
|---|---|---|
| ^REC LEVEL^ | 0 dB at 56 % | +28 dB: well past the +16 dB the chips are tested to |
| ^CARRIER^ | 0 dB | -20 dB; the scale of the knob is an estimate |
| ^CAL^ | right | ±3 dB, with the expander's slope 2 ± 0.25: past real tolerances |
| ^HISS^ | 9 % at Master 0.0 dB: 55 dB under reference | 30 dB more than that scale |
| ^CRACKLE^ | the first 7 %: no clicks, the margin a 1983 measurement implies | 150 clicks a second on each channel |
| ^HEAD BUZZ^ | 0 %: hidden as the datasheets say | 20 dB past the largest spike a head switch can make |
| ^BREATHING^ | 0 %: two matched decks | another deck: times 20 % off and an average detector |
| ^TRACKING^ | 0 % | 4 dips a second, 14 to 30 dB deep; one head 4 dB weaker |
| ^DRIFT^ | 0 % | a slow wander 22 dB deep |
| ^DROPOUTS^ | 6 to 18 %: a good tape's 1 to 10 a minute | 300 a minute, up to 80 ms long |

### Things the format does that you will hear

- **Hiss that moves.** The expander pushes the noise down with the music, so the hiss is quiet in pauses and swells under loud passages, without Breathing.
- **The hard ceiling.** Nothing softens on the way up. And because the compressor has turned its gain up during a silence and needs 5 ms to bring it down, the first moments of a drum hit after silence reach the ceiling even with Rec Level low.
- **Cal pumps.** A wrong Cal comes out doubled, and with the expander's slope off it grows with the level: at the top of the knob a tone at reference level comes out 6.75 dB louder, one 40 dB quieter only 1.75 dB louder.
- **Falling back.** A dull, mono, hissy quarter of a second, each time the signal gives way.

### What comes out, measured

At 44.1 kHz, and where it says so at 48, 88.2 and 96 kHz as well.

| Test | Result |
|---|---|
| Every knob at rest, 20 Hz to 20 kHz, -60 to +10 dB, every rate | within 0.001 dB of the input, apart from the two settings that reach the ceiling |
| The compressor, -60 to +16 dB | 2:1 within 0.004 dB; the expander undoes it |
| Its detector, every rate | attack 5.0 ms, recovery 70.0 ms |
| 10 kHz at reference, against 1 kHz | recording +3.8 dB, playback -7.7 dB (datasheet: +3.9 and -7.7) |
| Where a steady tone starts to clip | 1 kHz at +19.1 dB, 10 kHz at +11.4 dB over reference |
| Channel noise | 55.0 dB under reference at full signal, 50.0 at -5 dB, 45.0 at -10 dB; 6 dB per octave |
| Clicks a second on each channel, Crackle 10 / 25 / 50 / 75 / 100 % | 0 / 1.3 / 4.9 / 31 / 145, the same at Carrier 0 and -12 dB |
| Clicks in a dip, against Rice's formula | 3493 counted, 3495 expected; 24838 counted, 24820 expected |
| Head switch, 10 kHz tone, what is left below 3 kHz | -75.1 dB at Head Buzz 0 % (-75.0 to -74.5 at the other rates); -46 dB at 100 % |
| The fall to the linear track | 0.3 ms after the signal drops below -17 dB; back 235 to 260 ms after it returns |
| Cal at the top / the bottom | a tone at reference +6.75 / -5.25 dB, one 40 dB under it +1.75 / -0.25 dB |
| Breathing 100 % | steady tones within 0.01 dB; bursts up to 4.5 dB off |
| Silence on the linear track, Hiss 50 % | -32.5 dBFS, against -69.4 on the Hi-Fi track and -38.5 on Simple |
| The most clicks before the deck falls back | about 970 a second on each channel |

### What is estimated

- **Deliberately past a real deck:** the top halves of Rec Level, Hiss (30 dB), Head Buzz (20 dB), Cal (its level and its slope), Crackle, Tracking, Drift and Dropouts. They are there to be heard; the bottom of each knob is the deck in order.
- **Estimates:** that the ceiling cuts hard; the compander's range (30 dB either way); that the noise stops rising 10 dB under reference however weak the signal; the scale of the Carrier knob; the click margin past the first 7 % of Crackle; the signal's own small wander; the point (5 dB of carrier to noise) where the deck gives up on clicks and falls back; where the hold sits and how it fades; the 3 ms crossfade; how often, how deep and how long Tracking's dips, Drift and Dropouts are past the counted figures above.
- **Worked out rather than read:** the click rate (from Rice's formula), the size of a click, the shape of what the head switch leaves.
- **Left out:** the extra clicks of loud passages (not confirmed), a difference between the left and right channels' signals, the tape speed (one deck measured EP 4 dB noisier, which Carrier covers), the level control some decks had on the Hi-Fi sound (the AGC knob is a different one).

::: note Hear it
Load **Hi-Fi - Clean Deck** and play something with quiet passages. Turn ^CARRIER^ down: the hiss comes up, and past about -14 dB the sound starts to drop to the dull linear track and back. Bring it back to 0 dB and turn ^CRACKLE^ up past half, then ^DRIFT^: the clicks come in bursts at the low points. Load **Hi-Fi - Wrong Deck** and set ^CAL^ back to right: the pumping stops.
:::

::: aside
Clean until the signal goes. Then somewhere else entirely.
:::

[[pagebreak]]

## The switch on the back: B-Type NR

The linear track hisses. When stereo came to VHS, that track was split in two, each half narrower and noisier, so those decks were given the B-type noise reduction of the audio cassette. **B-Type NR** is that circuit.

### The idea

Hiss is a high sound, and it is only heard when the music is quiet. So the recorder lifts the quiet highs before they reach the tape, by up to 10 dB, and the player cuts them by the same amount afterward. The music comes back as it was. The hiss, which joined in between, has only been cut. Loud highs are left alone on both sides: they cover the hiss by themselves, and lifting them would overload the tape.

### The circuit

The model is the application circuit of the Signetics NE545, a chip made for this job, with the part values printed in its datasheet:

| Part | Value | What it does |
|---|---|---|
| C301 | 5600 pF | with C302 and R301, the fixed half of the filter |
| C302 | 27 nF | |
| R301 | 3.3 kOhm | |
| R302 | 47 kOhm | with C303 and F, the sliding half |
| C303 | 4700 pF | |
| F | 700 kOhm at rest, down to 70 Ohm | a resistance the circuit itself controls |

The signal goes two ways. The main path passes it untouched. Beside it runs a side chain, a filter that lets only the highs through, and its output is added to the main path. That sum is the 10 dB lift.

The side chain also listens to itself. Its output is rectified into a control voltage, and that voltage turns F down. As F conducts, the filter's corner slides upwards, so less and less passes: the louder the highs, the less is added, until at the circuit's reference level nothing is. This is the sliding band.

The control voltage has two speeds. It follows the music with a time of about 100 ms, slow enough not to distort the sound. A sudden loud sound would get through boosted before that, so for a large jump the time drops to about 2 ms, and a clipper in the side chain holds the overshoot down for the instant in between.

The player is the same side chain turned around: connected in a loop, subtracting from the signal where the recorder added.

### Solved exactly

A loop of this kind is hard to compute, because the output depends on a filter whose setting depends on the output. The model solves the whole network for every sample, with its two capacitor charges and with F included, so nothing in the loop lags by a sample. One result is that the playback side undoes the record side almost perfectly when nothing lies between them: on music, what remains is about 130 dB below the signal.

### Against the datasheets

The datasheets of the NE545 and of a second chip of the same kind, the TEA0665, give the record side's lift at eight points. Three things they do not give as numbers (one amplifier's gain, another's rise with frequency, and how F follows the control voltage) were fitted to those eight points.

| Tone | Level under reference | Datasheet | Model |
|---|---|---|---|
| 1.4 kHz | 20 dB | +4.4 dB | +4.3 dB |
| 1.4 kHz | 30 dB | +7.5 dB | +7.6 dB |
| 2 kHz | 25 dB | +7.0 dB | +7.2 dB |
| 5 kHz | 20 dB | +3.2 dB | +2.9 dB |
| 5 kHz | 30 dB | +8.2 dB | +7.9 dB |
| 5 kHz | 40 dB | +10.3 dB | +10.3 dB |
| 10 kHz | 20 dB | +2.6 dB | +2.9 dB |
| 10 kHz | 30 dB | +6.5 dB | +6.8 dB |

That is at 44.1 kHz. At every sample rate up to 192 kHz the model stays within 0.6 dB of all eight; the chips themselves are specified to 2 dB.

Other things measured on the model:

- **Very quiet sound** is lifted 0.6 dB at 200 Hz, 3.7 dB at 600 Hz, 9.3 dB at 2.4 kHz and 10.3 dB at 5 kHz.
- **At reference level** the lift is under 0.2 dB.
- **Hiss** at 8 kHz, through the playback side alone, drops by 10.5 dB.
- **A sudden jump** from quiet to reference level overshoots by 0.9 dB and has settled within 5 ms. After the loud sound stops, the full lift is back in about 155 ms.

Two values are estimates: the level at which the fast smoothing takes over, and the level of the clipper. The reference level of the circuit is taken as -8 dBFS, the same level the VHS saturator takes as the deck's.

### Why it mistracks

The playback side decides how much to cut from the level it sees. If the tape gives back less than was recorded, it sees a quieter sound than the recorder did, takes it for one that was lifted more, and cuts more. Every loss of the tape is made larger.

| Between the two sides | Without NR | With NR on both sides |
|---|---|---|
| The tape plays 2 dB quiet (5 kHz, 30 dB under reference) | 2 dB down | 4.3 dB down |
| The tape has lost its highs from 4 kHz (8 kHz, 25 dB under reference) | 6.5 dB down | 12.2 dB down |

In the plugin everything from Drive to Dropouts lies between the two sides. So with NR on ^ON^, Hi Cut, Slew, the VIBE stage, Drift and Dropouts all become stronger in the highs, and unsteady, because the cut moves with the music. That is the sound of a worn tape in a deck with the switch on.

### What it means for the knobs

- **It works on quiet highs.** Sound near -8 dBFS and above passes almost untouched on both sides. To hear NR, play something with quiet passages, or turn the input down.
- **Hiss is only reduced if it is on the tape.** With the noise's Position on Post, the hiss joins between the two sides and ^ON^ or ^PLAY^ cuts it. With Position on Pre it goes in before the record side, is lifted and cut like the music, and comes out as loud as before.
- ^REC^ alone is the lift without the cut: bright, airy, a little squashed, because quiet highs come up and loud ones do not.
- ^PLAY^ alone is the cut without the lift: dull, and duller the quieter it gets.

::: note Hear it
Load **Wrong Switch**, a tape recorded with the switch on and played with it off, and compare it with NR on ^OFF^. Then load **Stereo Rental**, with NR on both sides of an aged tape, and switch NR between ^ON^ and ^OFF^ during a quiet passage: the top end ducks and comes back with the music.
:::

::: aside
Built to take something away. Set wrong, it adds.
:::

[[pagebreak]]

## Four microphones: Mic Model = Modeled

This section is about the Modeled set. The Simple one, the curves of the versions up to 1.5, is listed on the INPUT page.

Before the recorder hears the sound, the Mic stage plays it into a microphone. Each of the four types is built from published figures for one kind of microphone of the period. Where a figure was missing, the value is an estimate, and is called one below.

### One level for all four

A microphone overloads and hisses at a sound pressure, and a plugin only has digital levels, so the stage has to decide how loud a digital level is in the air. It takes a sound peaking at -8 dBFS, the level the rest of the plugin treats as the recorder's reference, to be 104 dB SPL: a loud voice close to the microphone, or a band heard from the crowd. 94 dB SPL (1 pascal), the pressure microphones are specified at, then peaks at -18 dBFS.

**This mapping is an assumption.** No document can fix it for a plugin. It was chosen so that the electret's documented overload lies 6 dB above reference: music played in at reference level passes cleanly, and Mic Gain +6 dB reaches the overload. The same mapping serves all four types. Mic Gain moves the sound along it, as a louder or a closer source would.

The Camcorder stands further off. The mapping is the same for it, but its camera is taken to be across the room, 16 dB further from the source than a close microphone (about six times the distance in free field). So it hears everything 16 dB quieter, before Mic Gain's own change.

Its level control works from 60.8 to 82.8 dB SPL at the microphone. With the distance, that is -35 to -13 dBFS peak coming in at Mic Gain 0. Here is what Mic Gain does to ordinary material: music at -18 dBFS rms, and a sine at that level.

- **-12 to -6 dB: inside the range.** Loud and quiet parts 15 dB apart come out 3 to 4 dB apart. In a gap the hiss rises to within 28 dB of the music.
- **0 dB: at the top of the range.** The sine sits 1.8 dB inside it; a tone peaking at -12 dBFS sits 1.2 dB past it. The music's loud passages hold the level control at its lowest, so they keep their 15 dB over the quiet ones, with a little crunch. After the music stops, the hiss comes up some seconds later (7 seconds after this music).
- **+6 to +12 dB: past the range.** Loud sounds crunch. The sine has 0.7 % distortion at +6 dB and 3.1 % at +12 dB; the music's peaks come out 4 to 9 dB lower against the rest. After the music stops, the gap stays quiet for about 22 seconds before the hiss comes up.

Mic - Camcorder 89 (Mic Gain +2 dB) and Mic - Wedding Video (+1 dB) put that -12 dBFS tone 3 and 2 dB past the top of the range: squashed, with a little crunch (0.6 and 0.5 % distortion).

### The figures they are built on

| Figure | Value | Where it is from |
|---|---|---|
| Electret: the capsule and the gate of its transistor | a first-order low cut; 100 MOhm and 30 pF: 53 Hz | J. Kean, "Design and Performance of Electret Condenser Microphones", Radio World |
| Electret: overload | above 110 dB SPL; negative-going peaks clip first | the same article |
| Electret: the highs | flat to 5 kHz, +1 dB at 8 kHz, +1.5 to +3 dB at 13 to 17 kHz | ten Panasonic WM-61A capsules measured by J. Neal, read off his plot |
| Electret: self-noise | 26 dB SPL(A) | a figure quoted for the WM-61A |
| Electret: sensitivity | -35 dB re 1 V/Pa | WM-61A |
| Camcorder: the record chain | microphone, mic amp, a voltage-controlled amplifier, an ALC detector on the output feeding back to it | JVC GR-C1 (1984) service manual, audio block diagram |
| Camcorder: microphone input level | -66 dBs (0.39 mV) | Sony CCD-M8U service manual (1985) and CCD-TR4 instruction manual; the JVC GR-C1's manual says -68 dBs |
| Camcorder: mic amp band | 20 Hz -12.8, 50 Hz -3.1, 100 Hz -0.6, 200 Hz to 1 kHz 0, 2 kHz -0.2, 5 kHz -0.6, 10 kHz -1.7, 15 kHz -3.6, 20 kHz -4.9 dB | Sony DCR-TRV900E (1998), the microphone input measured by D. Vrančič |
| Camcorder: ALC curve | full gain up to the knee, then 0.75 dB more output for 14 dB more input (1 : 19) | the same camera and measurements |
| Camcorder: ALC range | 22 dB with the microphone | Sanyo VM-RZ1P training manual; the Sony CCD-M8U's service manual checks at least 20 dB |
| Camcorder: mic amp overload | 0.95 V out at 21.5 dB of gain; 1.4 V at 24.3 dB | ROHM BA7780KV and Sanyo LA7471M datasheets, camcorder microphone amplifiers |
| Dynamic: response at 0.6 m | 50 Hz -16, 100 Hz -8, 200 Hz -4, 500 Hz -1.5, 2 kHz +1, 4 kHz +4, 5 kHz +5, 6.5 kHz +2, 8 kHz +3, 10 kHz 0, 15 kHz -10 dB | Shure 515BSM user guide, figure 1 (the Unidyne B family of the 515SA and 515SB), read off the plot |
| Phone: sending response | 24 points from 100 Hz to 8 kHz | CCITT Recommendation P.48 (1988), Table 1: the intermediate reference system, "based on the mean characteristics of many commercial circuits" |
| Phone: carbon microphone | the second harmonic is the main distortion; sensitivity rises with sound pressure | patents on carbon transmitters; White and Louie, The Audio Dictionary |
| Phone: line noise | at most -65 dBm0p | CCITT G.712 |
| Boombox: record ALC | 45 dB of input, from -60 dBm, give 3 dB of output; a peak detector on the output | Sanyo LA3220 datasheet, a radio-cassette record amplifier |
| Boombox: distortion under ALC | 0.5 % typical, 2 % at most; 3 % at the end of the range | LA3220; ROHM BA3308 |
| Boombox: ALC recovery | set by 22 uF and 1 MOhm | ROHM BA3308 application circuit |
| Boombox: the cassette | 50 Hz to 10 kHz, S/N 45 dB | Sharp QT-27 radio cassette service manual |
| Boombox: the built-in microphone | mono | the same manual |

### Each one, step by step

**Camcorder**

A camcorder's built-in microphone was a decent electret. What gave its sound away was the record chain behind it: a mic amp, then a level control with a great deal of fixed gain in front of a short range. The model follows the JVC GR-C1's chain. It is mono: one built-in microphone, one audio track, so left and right are summed first.

1. **The distance.** The source is heard 16 dB quieter: a camera across the room.
2. **The capsule.** The electret's highs: one peak filter gives the WM-61A's rise, flat to 5 kHz, a little up at 8 kHz, about 2.5 dB up around 15 kHz. Its low cut: the capsule and the gate of its transistor form a first-order low cut at 53 Hz. Its self-noise: white noise at 26 dB SPL(A). Its overload: the transistor gives way gently above 110 dB SPL, the negative half of the wave first, reaching 1 % distortion there, second harmonic as well as third.
3. **The mic amp's band.** A second-order low cut and a first-order top cut, fitted to the TRV900's measured microphone input: 3 dB down at 50 Hz on top of the capsule's own cut, and a soft top, 1.7 dB down at 10 kHz and 3.6 dB at 15 kHz.
4. **The mic amp's overload.** Both halves alike, 1 % distortion at 107 dB SPL: the level at which the two datasheets' microphone amplifiers reach their maximum output with this capsule. It gives way before the capsule's transistor does.
5. **The ALC.** A fixed record gain of 42 dB. The camera's nominal microphone level, -66 dBs, is taken as the knee: from this capsule that is 60.8 dB SPL at the microphone, -35 dBFS peak coming in at Mic Gain 0, and the gain brings it to 1.2 dB under reference. Above the knee a peak detector on the output turns the gain down by 18 dB for every dB it sees: 22 dB of input give 1.2 dB of output, so the end of the range, 82.8 dB SPL, comes out at reference. There the gain has come down by 20.8 dB, as far as it goes; past it the output follows the input up again.
6. **The gain element's distortion.** The Boombox's, after the ALC: 0.3 % at the end of the range, rising quickly past it.

The level control's timing is an estimate. No camcorder of the period has its ALC's time constants in a document that could be read: the JVC's are on its schematic, but not legibly.

- **Attack.** The detector charges with 20 ms, slower than the Boombox's, because a camcorder's level control is reported to let a sudden loud sound distort before it reacts.
- **Recovery.** It discharges with 19 seconds, which brings the gain back at 8.2 dB a second, the whole 20.8 dB in about 2.5 seconds. That is faster than the Boombox, because the Sanyo manual says the recovery is shortened for the microphone, and slow enough for the "several seconds" reviewers describe.
- **After a sound louder than the range,** the detector holds more than it needs and the gain stays at its lowest until that has drained away. At Mic Gain 0 the gain starts back 2 seconds after the -12 dBFS tone stops and 7 seconds after the music; at +12 dB, 22 to 24 seconds after.

There is no mechanism noise. A camcorder's microphone does pick up its own motors, the makers say so, but no level or spectrum was found for it. The only noise is the capsule's, and the ALC lifts it in every pause: 17 dB down right after a sound in its range, then up with the gain, to the full 42 dB of record gain.

**Dynamic**

Four filters fitted to the twelve read points: a gentle low cut, the presence peak, the smaller peak above the dip at 6.5 kHz, and the fall above 10 kHz. A moving coil does not overload at these levels and its own noise is far below a recorder's, so there is neither. There is no proximity lift: the guide gives it only at 1 cm, and no distance in between is documented.

**Phone**

1. **The carbon's sensitivity.** Below reference level a carbon microphone grows less sensitive the quieter the sound: here by a quarter of a dB for every dB, by 10 dB at most.
2. **Its distortion.** A second harmonic of 3 % at reference level.
3. **The reference telephone.** Eight filters fitted to all 24 points of the P.48 table: the steep low cut, the broad rise to 2.5 to 3 kHz, and the cliff above 3.4 kHz.
4. **The line.** Noise at -65 dBm0 in the band from 300 Hz to 3.4 kHz.

A phone line carries one channel, so the Phone sums left and right before step 1 and is mono.

**Boombox**

1. **The capsule.** The Camcorder's electret, its step 2 above, and only one: the built-in microphone of a radio cassette is mono, so the Boombox sums left and right first.
2. **The ALC.** A fixed record gain of 37 dB brings the bottom of the ALC's range (-60 dBm from the capsule, 66.8 dB SPL, -45 dBFS here) up to reference level. Above it, a peak detector on the output turns the gain down by 14 dB for every dB it sees above reference: 45 dB of input give 3 dB of output, to the end of the range 45 dB up. The detector charges within milliseconds and lets go through 22 uF and 1 MOhm: a time of 22 seconds, of which the loop needs only the first third, so the gain comes back at 5.5 dB a second.
3. **The gain element's distortion.** 0.5 % at the level of the LA3220's measurement, a little less at the start of the range.
4. **The tape.** Its hiss, 45 dB under the level the ALC records at, and its band, 50 Hz to 10 kHz.

### What comes out, measured

At 44.1 kHz; the responses hold at every rate from 44.1 to 192 kHz.

| Test | Result |
|---|---|
| Phone against the P.48 table, 100 Hz to 3.5 kHz | within 0.2 dB at all 20 points |
| Phone at 4 and 5 kHz | -33.6 and -49.1 dB (table: -33.5 and -48.5) |
| Phone at 6.3 and 8 kHz | -76 and -92 dB (table: -70 and -86): a little deeper |
| Phone, 1 kHz at reference level | second harmonic 4.7 % (3 % at the microphone, lifted 4 dB by the curve at 2 kHz) |
| Phone, 20 dB under reference | comes out 25 dB lower; never more than 10 dB under a plain microphone |
| Phone, silence | line noise -65.0 dBm0 |
| Dynamic against the Shure points | within 0.8 dB at all twelve, worst at 500 Hz |
| Camcorder, the capsule | 3.0 dB down at 53 Hz; +0.3 dB at 5 kHz, +0.7 dB at 8 kHz, +2.5 dB at 15 kHz |
| Camcorder, the mic amp against the TRV900 | within 0.2 dB at every point from 20 Hz to 20 kHz |
| Camcorder, the capsule's transistor at 110 dB SPL | 1.0 % distortion, the negative peak the lower |
| Camcorder, the mic amp at 107 dB SPL | 1.0 % distortion, both halves alike |
| Camcorder, 1 kHz from 20 dB under the knee to the end of the range | on the curve within 0.13 dB; above the knee 0.053 dB more output per dB of input (1 : 19) |
| Camcorder, the end of the range | out at reference (+0.1 dB), 0.3 % distortion |
| Camcorder, 9 dB past it | 8.3 dB more output, 2.3 % distortion |
| Camcorder, a sound 20 dB louder than the one before | 11.4 dB over at once, back within 1 dB after 8 ms |
| Camcorder, after a loud passage | the gain comes back at 8.23 dB a second (the 19 s detector gives 8.23) ; from 18.9 dB down it is within 1 dB of full gain 2.1 s later |
| Camcorder, silence | the capsule's hiss through the full record gain: -48 dB(A), 37 dB under a reference sine |
| Camcorder, silence right after a sound in its range | 17 dB lower, rising with the gain |
| Camcorder, music at -18 dBFS rms, Mic Gain -12 dB | the loud and the quiet part 3 dB apart instead of 15; the hiss in a 2 s gap 28 dB under the loud part |
| Camcorder, the same at Mic Gain 0 | 15 dB apart; the peaks 0.5 dB lower against the rest; the hiss in the gap 54 dB under |
| Camcorder, the same at Mic Gain +12 | past the range: 13 dB apart, the peaks 9 dB lower against the rest; the hiss in the gap 64 dB under |
| Camcorder, a sine at -18 dBFS rms | 0.28 % distortion at Mic Gain 0, 0.7 % at +6 dB, 3.1 % at +12 dB |
| Boombox, 1 kHz from -70 to -45 dBFS | 37.2 dB of gain, straight |
| Boombox, 1 kHz from -45 to 0 dBFS | out at -8.0 to -5.0 dBFS: 45 dB in, 3.05 dB out |
| Boombox, under ALC at -20 dBm | 0.9 % distortion, the capsule's share included |
| Boombox, a loud sound from rest | held within about 5 ms, after a short burst |
| Boombox, after a loud passage | the gain comes back at 5.53 dB a second (the 22 uF and 1 MOhm give 5.53) and is back in full 6.5 s later |
| Boombox, silence right after a sound | the tape's hiss, -56 dB(A) |
| Boombox, the same silence 11 s later | -51 dB(A): the capsule's hiss at full gain on top |

### What is estimated

- **The level mapping** above.
- **Camcorder and Boombox:** that "begins overloading" means 1 % distortion; that the positive half gives way 6 dB later than the negative (no figure was found); the middle of the spread of the ten measured capsules.
- **Camcorder:** its distance from the source (16 dB, a camera across the room: set by ear); the timing of its level control, both its attack (20 ms) and its recovery (19 s, 8.2 dB a second); that the camera's nominal microphone level is the ALC's knee; that the camera's capsule is as sensitive as the WM-61A (with a less sensitive one the knee moves up by as much); that the mic amp overloads on both halves alike; the distortion after the gain element, taken from the Boombox's radio-cassette IC; and that a 1980s camera's microphone input had the band measured on a 1998 one. The ALC's knee is hard; the TRV900's is soft, so just above the knee the model comes out up to 1.7 dB lower than that camera.
- **Phone:** both carbon effects are documented only in kind, so their amounts are estimates, kept modest: 3 % second harmonic, a quarter of a dB per dB down to 10 dB, following the level within 1 ms up and 50 ms down. For the line noise: the limit taken as the level, the reference junction taken as 0 dBr in 600 Ohm, the band edges as gentle ones, no psophometric weighting.
- **Boombox:** the ALC's attack (no charging current is published): 5 ms; that its range starts at the capsule level the WM-61A's sensitivity gives; that it starts where the output reaches -8 dBFS; the cassette's band edges taken as -3 dB points; its hiss measured in that band without weighting.
- **Left out, for want of figures:** a camcorder's wind filter, its motor noise, how a microphone's housing colors it, the proximity effect at ordinary distances, the carbon's own noise, and the 8-bit coding of digital phone lines.

::: note Hear it
Load **Mic - Bootleg Concert** and stop the music: within seconds the hiss and the room climb up as the ALC lets go, and the next note slams them down. Load **Mic - Answer Machine** and play a voice that goes from loud to soft: the soft words sink further than they should. In **Mic - Camcorder 89**, turn ^MIC GAIN^ up and listen to kicks and loud chords crunch; stop the music and, some seconds later, the hiss and the room come up behind the camera's level control. For the Dynamic, choose Init, switch the Mic on and pick it: a presence lift, nothing more.
:::

::: aside
The cheapest part in the machine, and the nearest to the sound.
:::

[[pagebreak]]

## The deck's level control: AGC Model = Modeled

In an early VHS deck the record level was set by an automatic gain control. **AGC Model = Modeled** is the record level circuit of one early VHS deck, rebuilt from its service manual: the schematic, the parts list and the description of the circuit. The Simple AGC, the one up to version 1.5, is described on the DECK page.

### The circuit

The deck's audio chip has a gain element at its input and an output amplifier after it. The output does two jobs: it is the deck's audio out, and it feeds a detector that sets the gain. So the control is a loop: the detector hears the output, after the gain has already turned it down.

| Part | Value | What it does |
|---|---|---|
| C70 | 4.7 uF | couples the output into the detector |
| R103 | 1 kOhm | in series with C70 |
| R104 | 22 kOhm | to ground: the path that lets C70's charge drain |
| D21 | germanium diode | passes the positive half of the wave only |
| C80 | 47 uF | holds the rectified peak: its voltage sets the gain |
| R107 + R108 | 1.2 MOhm + 560 kOhm | drain C80 slowly; their junction carries the control voltage to the chip |
| C81 | 0.01 uF | at the control input: a lag of 3.8 ms |
| a switch | | shorts C80 in playback, so every recording starts at full gain |

The values are read from the schematic and checked against the parts list.

### How it moves

**Down, in two steps.** When a loud sound arrives, C70 and C80 charge together through the diode. C70 is ten times smaller, so it fills ten times faster and then blocks: within a few cycles C80 has taken about 9 % of the excess. Because the gain law is steep, that alone is a cut of 10 dB or more, reached in about 11 ms. The rest follows as C70's charge drains through R104, over about half a second.

**Back up, slowly.** When the sound stops, the diode stops conducting, and C80 can only drain through R107 and R108. That takes over a minute to empty it, and the gain comes back at about 1 dB per second. Nothing holds the gain down on purpose; the slow drain does it.

### The gain law and the levels

The chip's own datasheet was not found. The replacement part's datasheet gives two points at 1 kHz: 0.55 V out for 0.25 mV in, and 0.8 V out for 25 mV in, "AGC 40 dB". That is 40 dB more input for 3.25 dB more output, about 12 : 1. It gives no curve and no timing.

So the model takes the simplest law that meets those points: above a knee, the gain falls in a straight line in dB as C80's voltage rises (as a transistor junction used as a variable resistance would make it), down to at most 40 dB. Two numbers of that law were fitted on the circuit itself, with a steady 1 kHz tone: where it starts (0.769 V on C80) and how steep it is (88.3 dB per volt).

The level comes from the service manual's adjustment: the reference input is set to give 2.2 V peak to peak at the output, 1.1 V peak. The plugin's reference level, a peak of -8 dBFS, is taken to be that input.

Where the knee lies is not printed. The manual's adjustment procedure checks a tone 10 dB below the reference and expects it back near full level, which only works if the AGC is already at work there. So the knee is taken to be 10 dB below the reference, peaks of about -18 dBFS.

### In the plugin

- **Stereo.** The deck was mono. The detector hears the louder of the two channels, and both get the same gain.
- **Unity at reference.** At the reference level the circuit cuts by 9.1 dB. The plugin lifts everything by that much, so a steady tone peaking at -8 dBFS passes at its own level, quieter sound comes up by 9.1 dB, and louder sound is cut.
- **The knob.** The detector always works on the circuit's full gain, so its timing never changes. The knob sets how much of its gain change you hear, in dB: at 50 % half of it, at 0 % none, and then the stage is skipped.
- **The start.** C80 is empty after a program change, as the deck's was at the start of each recording. For most of the first second, loud sound comes through lifted.

### What comes out, measured

A 1 kHz tone, AGC at 100 %. The same at 44.1, 48 and 96 kHz within 0.01 dB, and within 0.1 ms.

| Test | Result |
|---|---|
| A steady tone at the knee, 10 dB under reference | no cut: lifted by 9.08 dB |
| At reference | cut by 9.08 dB: out at its own level |
| 10 dB over reference | cut by 18.23 dB (the circuit simulated alone: 18.2) |
| 30 dB over reference, 40 dB over the knee | cut by 36.75 dB: 3.25 dB louder out, as the datasheet gives |
| A jump of 20 dB from the knee | 10 dB of cut after 10.9 ms, 13.0 dB after 50 ms, 17.6 dB after half a second (of 18.2) |
| After that sound stops | the gain comes back at 0.98 dB per second; all of it in about 19 seconds |
| From rest, a tone at reference | within 1 dB of its level after 0.85 s |

### What is estimated

- **The gain law:** its shape. Only the two datasheet points are known; the straight line in dB is an assumption, and its steepness and starting point follow from it and from where the knee is put.
- **Where the knee lies:** 10 dB under the reference, read from the adjustment procedure. The evidence is not all one way, and anything from 20 dB under the reference to the reference itself is possible.
- **The diode:** as a germanium diode, 0.15 V and 100 Ohm; the resistance of the chip's output, 100 Ohm; that the output clips at 4.5 V either side (it decides only how hard a sudden loud sound charges the detector).
- **The control range:** 40 dB, the datasheet's test condition. The datasheet gives no maximum.
- **That the chip's gain follows its control input at once.** Only C81's lag is in the model.
- **Left out, for want of figures:** any leakage of the germanium diode and any current drawn by the chip's control input, which could only make the recovery faster (up to about 2.5 dB per second); any load on the audio output that would speed up the second step of the attack.

::: note Hear it
Choose **Init**, switch **Deck Power** on, set **AGC** to 100 % and **AGC Model** to Modeled. Play a drum loop with a quiet break: the hits are held down, and in the break the room and the hiss rise slowly, a dB a second, long after a Simple AGC would have come back. Then set AGC to 50 % for half of that.
:::

::: aside
It listened to itself. It took a long time to forget.
:::

::: chapter
id: switches
title: ＳＷＩＴＣＨＥＳ
jp: スイッチ
word: SWITCHES
toc: The master switches
epigraph: One click, and a small red light in the dark.
:::

Every section of the machine has its own switch, so a part can be taken out without touching its knobs. These are all of them.

[[switches]]

**Animation**, **Visual Vibes** and **HQ** are not part of the programs: choosing a program leaves them as you set them.

## What a switch leaves in

- **VIBE's power** off: Hi Cut stays in. With Type on Hi-Fi, the seven controls the Hi-Fi track had taken go back to their own stages, and B-Type NR works again.
- **WOBBLE's power** off: the two filters of the WOBBLE page stay in.
- **Noise** off: silences every noise, the Whistle, the head-switching buzz and the modeled microphones' own noise included. In Hi-Fi it also silences the FM channel's noise, clicks and head switch, and the linear track's hiss; the holds and the falls to the linear track stay, as they are part of the deck.
- **Deck Power** off: B-Type NR, HQ, Azimuth, Animation and Visual Vibes are not affected. In Hi-Fi it switches off Breathing, Tracking and Drift there too; Dropouts is not part of the deck.
- **Animation** off: Visual Vibes shows nothing either.

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

## Four recipes

- **A slow-speed (EP) tape:** set Hi Cut near 6 kHz, Lo Cut near 100 Hz and raise Hiss. Or set Type to VHS and turn TAPE SPEED to EP.
- **A standard-speed (SP) tape:** Hi Cut about 10 kHz, Lo Cut about 50 Hz. Or set Type to VHS with TAPE SPEED at SP.
- **A clean Hi-Fi sound:** start from **Hi-Fi - Clean Deck**. Or keep the wobbles and the noise low on any Type: the rotating heads have no wow or flutter to speak of.
- **A tape from another deck:** set Type to VHS and turn ^AZIMUTH^ to 3' to 8'.

::: chapter
id: notes
title: ＮＯＴＥＳ
jp: ノート
word: NOTES
toc: How it behaves, and what comes from real machines
epigraph: In pencil, on the inside of the sleeve.
:::

## How it behaves

- **Turning a wobble knob while sound plays.** A wobble depth or rate changes the plugin's own delay. A small move is followed at a limited pace, so the pitch bends by a couple of percent at most. After a big jump, once the knob rests, the sound crossfades to the new delay in 25 ms (a brief blur) and the bend is over.
- **Timing.** The wobble delays hold the sound back by their resting length, a few milliseconds on most programs. That length is reported to the host as the plugin's latency, half a second after it stops changing, so a host that compensates keeps the track in time.
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

These rows are what Head Buzz, Tracking, Breathing and the others do in Simple and VHS. No figures were found for their other times and depths, which are set by ear.

What is published and what is estimated in **Type = VHS**, in **Azimuth**, in **Type = Hi-Fi** (which makes its own head buzz, tracking, breathing, drift and dropouts from the chips' figures), in **B-Type NR**, in the four modeled microphones of **Mic Type** and in the Modeled **AGC** is said in the chapter ＩＮＳＩＤＥ　ＴＨＥ　ＭＡＣＨＩＮＥ.

::: chapter
id: compat
title: ＯＬＤ　ＴＡＰＥＳ
jp: 互換性
word: OLD TAPES
toc: Projects made with older versions
epigraph: Nothing you recorded is lost. It only waits.
:::

**Projects made with an older version load with every setting in place, and projects saved with 1.3 to 1.6 sound as they did, with two changes made on purpose: HQ now reaches further, and Whistle is louder above 60 %.**

- New controls are only ever added after the existing ones, and each rests where the plugin sounds as it did without it: Scrape and Whistle at 0 %, B-Type NR at Off, Type at Simple, Azimuth at 0', Visual Vibes off. Mic Model and AGC Model rest at Modeled in a new instance; a project saved before they existed opens with both at Simple.
- No older program uses the newer controls: the five with the Mic on keep the Simple microphones, every program with the AGC up keeps the Simple AGC, and none of the 39 programs from before 1.7 uses Hi-Fi or Azimuth. They are unchanged.
- The newer programs come after Init and after each other, so every older program keeps its number.
- **Type** gained a third position in 1.7. A project stores the choice itself, so VHS stays VHS. The one exception: automation of Type recorded in 1.5 or 1.6, which stores only a knob position, now reads the old VHS position as Hi-Fi, because the same travel is now shared by three positions. Set it again to VHS where that happens.
- **HQ** is on in a new instance. A project keeps the HQ setting it was saved with; one saved before HQ existed opens with HQ off, so it sounds as it did.
- **HQ reaches further in 1.7.** It now also covers the modeled microphones and the recording side of B-Type NR, which used to fold bright, loud sounds back into the midrange even with HQ on. A project that uses them with HQ on keeps its level and tone and loses those tones; the programs that use them stay within a third of a dB of 1.6 below 10 kHz. With HQ off, and wherever neither is in use, nothing changes.
- **Whistle** is as it was up to 60 %. Above that it is louder than in 1.6, by 15 dB at 100 %.
- **Mic Model** is on Modeled in a new instance. A project keeps the Mic Model it was saved with; one saved before 1.6 opens on Simple, the microphones it was made with, so it sounds as it did.
- **AGC Model** rests on Modeled, but every program with the AGC up uses Simple, Home Recording included. A project saved before 1.6 opens on Simple, so it sounds as it did.

## What arrived when

| Version | Controls | Programs |
|---|---|---|
| 1.1 | The Wobble and Noise switches | |
| 1.2 | The deck: Power, AGC, Drift, Tracking, Breathing. Mix | Bad Tracking, Six Hour Tape, Wedding Video, Hi-Fi Master, Edge Damage, Bootleg Concert, Public Access |
| 1.3 | Animation | |
| 1.4 | Scrape, Whistle, HQ | Sticky Tape, Tube TV, Worn Heads, Dirty Heads |
| 1.5 | B-Type NR, Sat Type | Stereo Rental, Wrong Switch, SP Tape, EP Tape, and five older programs again with the VHS saturator: VHS - 3rd Gen Copy, VHS - Training Video, VHS - Six Hour Tape, VHS - Sticky Tape, VHS - Dirty Heads |
| 1.6 | Mic Model: the four Mic Types built from published figures (Modeled), beside the ones before (Simple). AGC Model: the record level control of an early VHS deck (Modeled), beside the AGC before (Simple) | the five programs with the Mic on again, on the modeled microphones: Mic - Camcorder 89, Mic - Answer Machine, Mic - Wedding Video, Mic - Bootleg Concert, Mic - Tube TV |
| 1.7 | Sat Type Hi-Fi: the FM track, taking over Hiss, Crackle, Head Buzz, Breathing, Tracking, Drift and Dropouts; the first choice renamed Simple. Azimuth. Visual Vibes. Tape Speed names five more formats' speeds; Whistle louder above 60 %. The blocks SATURATOR, HI-FI TRACK, TRACKING (WOBBLE page) and DECK renamed VIBE, ALIGNMENT, WARP and AGC; the power switches of NOISE and WOBBLE first in their rows, Drift in ALIGNMENT | Hi-Fi - Clean Deck, Hi-Fi - Hot Signal, Hi-Fi - Worn Rental, Hi-Fi - Tracking Lost, Hi-Fi - Wrong Deck; two VHS programs with Azimuth: VHS - Borrowed Tape, VHS - Wrong Deck EP |

::: chapter
id: trouble
title: ＡＤＪＵＳＴ　ＴＲＡＣＫＩＮＧ
jp: トラブルシューティング
word: TROUBLE
toc: Troubleshooting
epigraph: The picture rolls. Somewhere a small wheel wants turning.
:::

| What happens | Why, and what to do |
|---|---|
| VHS Vibes is not in the plugin browser. | Check that the MPC runs MPC OS 3.10.1 or later; that **Enable Enhancements** and **External Plugin Support** are both on under Preferences > Enhancements (Beta); that the MPC was restarted afterward; and that the folder `{{vendor}} - VST - VHS Vibes` sits inside a folder named `VST2 Plugins` at the top of the drive. Look under **External Plugins**. |
| It hisses and hums with nothing playing. | That is the tape noise. Switch **Power** off on the NOISE page, or turn **Master** down to off. |
| The noise swells when the music stops. | **Position** is on Pre and **Comp** or **AGC** is turned up. Turn **Master** down, or set Position to Post. Or the **Mic** is on Modeled with Type **Camcorder** or **Boombox**: its level control turns the gain up in every pause, as the real one did. |
| The Mic sounds different in a new instance from an older project. | A new instance starts on **Mic Model** Modeled; a project saved before 1.6 opens on Simple. Set Mic Model to the one you want. |
| The AGC sounds different from one program to another, or after turning it up on Init. | **AGC Model**: every program with the AGC up, and a project saved before 1.6, use Simple; Init and the programs with the AGC at 0 % are on Modeled. Set AGC Model to the one you want. |
| With Mix below 100 % it sounds like a chorus. | The dry input is steady and the tape path wobbles. Switch **Wobble** off for a clean blend, or keep Mix at 100 %. |
| The pitch bends or blurs when I turn a wobble knob. | The knob changes the plugin's own delay. It settles as soon as the knob rests. |
| The sound dips for a moment when I choose a program, or switch HQ or Sat Type. | Each of these fades the sound out and back in. |
| AGC, Drift, Tracking and Breathing do nothing. | **Deck Power** on the DECK page is off. The programs Init and Clean Widener switch it off. |
| Split, Lows, Lo Sat, Highs and Hi Sat do nothing. | VIBE's **power** is off. |
| Split, Lo Sat and Hi Sat do not do what their names say. | **Type** is on VHS, where they are Bias, Rec Level and Tape Speed, or on Hi-Fi, where they are Cal, Rec Level and Carrier, and are captioned so on the screen. See the table on the TAPE page. |
| Hiss, Crackle, Head Buzz, Breathing, Tracking, Drift or Dropouts behave differently, and are orange. | **Type** is on Hi-Fi with VIBE's power on: the Hi-Fi track has taken them over. See *What Hi-Fi takes over* on the TAPE page. |
| B-Type NR does nothing. | **Type** is on Hi-Fi with VIBE's power on: NR belongs to the linear track and is ignored. |
| Now and then the sound turns dull, quieter and mono for a moment. | In Hi-Fi: the deck has fallen to the linear track, because the signal off the tape got too weak. Raise **Carrier**, or lower Tracking, Drift or Dropouts. |
| There is no Azimuth knob on the DECK page. | It is there only while **Type** is on VHS. |
| The picture on the sticker does not move with the sound. | **Visual Vibes** is off (it is off in a new instance), or **Animation** is off. |
| Head Buzz, Whistle or Breathing cannot be heard. | Head Buzz and Whistle follow **Master** and the **Noise** switch. The Whistle is at 15.7 kHz; not everyone can hear it. Breathing shapes Hiss and Head Buzz, so it needs one of them turned up (in Hi-Fi it works on the music, best heard on drums). |
| A new program did not change HQ, Animation or Visual Vibes. | Those three switches are not part of the programs. |
| The reels stand still. | They turn only while sound comes in or the song is playing. Or **Animation** is off. |
| The MPC is short of CPU. | Switch **HQ** off: on, it takes about a quarter more. **Animation** off saves the work of updating the screen. |

::: chapter
id: rewind
title: ＲＥＷＩＮＤ
jp: 巻き戻し
word: REWIND
toc: Credits and license
epigraph: The counter runs back to zero. The tape remembers anyway.
:::

## Credits

**VHS Vibes** {{version}}, by **{{vendor}}**.

Inspired by the idea of James Peck's "VHS Audio Degradation Suite" for Reaktor: the order of the stages and what each one is for. All code is original. Nothing is taken from the Reaktor ensemble and no samples are used: every noise is synthesized.

## License

VHS Vibes is released under the **MIT License**. Copyright (c) 2026 {{vendor}}. The full text is in the file `LICENSE` of the project.

Project: `{{url}}`

## Colophon

The captions and titles on the plugin's screen, and in this manual, are set in MS Gothic, Times New Roman and Brush Script MT; the running text is Roboto. The cassette, the suns and the grids are drawn by script.

::: aside
ｂｅ　ｋｉｎｄ　・　ｒｅｗｉｎｄ
:::
