<!--
controls.md - one entry for every control of VHS Vibes. Part of the user manual.

    ## <parameter name, exactly as in build/params.json>
    what:   what the control does, in one or two plain sentences        (required)
    listen: what to listen for                                          (optional)
    try:    a starting value, or which programs use it                  (optional)
    note:   anything else worth knowing                                 (optional)
    range:  only if tools/make_manual.py cannot read the range from src/*.cpp

A value may run over several lines; a new "key:" or "##" ends it. **bold**, *italic* and
`code` work. The name printed on the screen, the page, the block, the Q-Link, the range and
the default are NOT written here: make_manual.py takes them from build/params.json,
src/*.cpp, tools/make_skin.py and skins/Q-Links.json, so they cannot go stale.

make_manual.py stops with an error if a parameter in build/params.json has no entry here,
and warns about entries for parameters that no longer exist. The order of the entries does
not matter (the manual follows the screen), but keeping them in screen order helps.
-->

<!-- ======================================================================== TAPE -->

## Drive
what: Soft clipping in the preamp, the recorder's input stage. The read-out is the gain into the clipper, from 0.0 dB (clean) to +24.1 dB; the further you turn it, the harder the sound is pushed into the clipper.
listen: Peaks round off and the sound thickens. Sound around -8 dBFS keeps its loudness; quieter sound comes up, louder sound is held down.
try: +5.7 dB is Home Recording. Subtle Glue uses +4.1 dB, Bootleg Concert +16.1 dB.
note: Below about +3.1 dB (the first 17 % of the knob), the knob also blends the clean signal with the clipped one, so the dB figure is the gain into the clipper.
range: 0.0 to +24.1 dB

## Tilt
what: Tilts the balance of lows against highs, in front of the Drive clipper. The sound is split at 800 Hz: to the right the highs come up and the lows go down by the same amount, to the left the reverse. The read-out is what the highs get, up to 6 dB either way; 0.0 dB is flat.
listen: To the left, darker and heavier; to the right, brighter and thinner. With Drive up it also changes what the clipper bites on.
try: Training Video sits at -0.9 dB, Dream Sequence at +0.6 dB.
range: -6.0 to +6.0 dB

## Slew
what: A slew limiter: it limits how fast the signal can move. The number under the knob is the shortest time, in microseconds, the limit leaves for a swing from silence to full level. Sound that moves more slowly than that passes untouched; sound that tries to move faster is straightened into a ramp. So it dulls and smears loud, bright sound and leaves quiet sound alone, which a filter cannot do.
listen: The top end dulls and the detail smears, more on loud passages than on quiet ones.
try: The figure in parentheses is how high a full-level tone passes clean. A tone 12 dB quieter passes four times as high.
- Home Recording: 32 µs (about 5 kHz).
- 3rd Gen Copy: 113 µs (1.4 kHz).
- Chewed Tape: 146 µs (1.1 kHz).
- The longest, 1890 µs: only up to 84 Hz.
note: Turned fully down the read-out says off and the stage is skipped. The MPC lists the control from 0 to 100 %; the read-out turns that into the time.
range: off, then 12 to 1890 µs

## Comp
what: A one-knob compressor with make-up gain. The read-out is the threshold, from 0 dBFS down to -36 dBFS; the ratio is always 4:1. Turning it up lowers the threshold and raises the make-up. Below about -12 dBFS it squashes and brings quiet sound, and noise, up.
listen: Quiet passages rise. With Position on Pre (NOISE page) the noise rises with them whenever the music stops.
note: At 0 dBFS, the bottom of the knob, only peaks above full scale are still compressed. Comp works in milliseconds. For a level control that works over seconds, use AGC on the DECK page.
range: 0 to -36 dBFS

## Dropouts
what: Short random dips, as when the tape lifts off the head. The highs go first and furthest; the lows only sag. The knob sets both how often they come and how deep they go. 0 % is off.
listen: Dips of a few milliseconds in which the sound goes dull. It does not drop to silence.
try: Bad Tracking uses 15 %, Edge Damage 30 %, Chewed Tape 80 %.
note: Dropouts are short. For a slow fading in and out, use Drift on the DECK page. With Type on **Hi-Fi** and VIBE's power on, the Hi-Fi track makes the dropouts instead, and the knob turns orange.
- Each one weakens the FM signal off the tape for 15 µs to 2 ms (up to 80 ms at 100 %) and brings a burst of noise and clicks.
- A deep one makes the deck hold the sound for a moment. A long deep one sends it to the dull linear track for a quarter of a second.
- How many: up to 300 a minute at 100 %; 80 a minute (a poor tape after a hundred plays) at 52 %; a good tape's 1 to 10 a minute at 6 to 18 %.

## Mono
what: Sums left and right into one. It sits near the end of the path, after everything except Mix and Output.

## Mix
what: Dry / wet. 100 % is all effect; lower lets the untouched input back in. The dry signal is delayed to line up with the tape path.
listen: With the wobbles off the blend is clean. With them on, dry and wet drift against each other and it sounds like a chorus.

## Output
what: Output level. The last stage.

## Saturator
what: The VIBE block's POWER switch. It turns the VIBE stage on or off, whichever Type is chosen. Off, the controls Split, Lows, Lo Sat, Highs, Hi Sat, Type, Azimuth and Hysteresis do nothing.
note: Hi Cut is drawn in the same block but is a stage of its own: it stays in when VIBE's power is off. With Type on Hi-Fi, VIBE's power off also hands Hiss, Crackle, Head Buzz, Breathing, Tracking, Drift, Dropouts and B-Type NR back to their own stages: the plugin then sounds exactly as it does on Simple with VIBE's power off.

## Split
what: Its job depends on **Type** (see *The three knobs that change their name*).
- **Simple**: the frequency at which the signal is divided into a low band and a high band.
- **VHS**, captioned BIAS: how well the deck is set up for the tape. 2.5 kHz, where the knob rests, is right. Lower is too little bias: brighter, grittier, distorting sooner. Higher is too much: duller, the highs giving way sooner.
- **Hi-Fi**, captioned CAL: whether the playback side is set to the same level as the record side. At 2.5 kHz, where the knob rests, it is right. Off it, the error comes out doubled and grows with the level, so the sound pumps.
note: In VHS the read-out shows "right" at 2.5 kHz, a minus percentage for too little bias and a plus percentage for too much. With **Hysteresis** on (DECK page) the knob is a real bias control: the bottom is a quarter of the right bias, the top one and a half times it (see *Hysteresis*, after the DECK page's controls). In Hi-Fi it shows "right" at 2.5 kHz and the error in dB elsewhere, from -3.0 dB at the bottom to +3.0 dB at the top.
try: In Hi-Fi, Hi-Fi - Wrong Deck has it at the bottom.

## Lows
what: In **Simple**: the level of the low band going into its clipper. In **VHS** and **Hi-Fi**: trims the result below 1 kHz.

## Lo Sat
what: Its job depends on **Type**.
- **Simple**: how hard the low band is clipped. The read-out is the gain into the band's clipper, from 0.0 dB (clean) to +20.0 dB.
- **VHS**, captioned REC LEVEL: the record level, from 10 dB under to 8 dB over the deck's reference level, which is at 56 %. There a tone at -8 dBFS has 3 % distortion, as on a real deck. Quiet sound comes back at the level it went in whatever the setting; what changes is how much of the loud sound the tape bends.
- **Hi-Fi**, captioned REC LEVEL too: the record level, from -10 dB to +28 dB, 0 dB at the same 56 %. It decides how hard the sound meets the Hi-Fi track's hard ceiling, which a steady 1 kHz tone reaches 19 dB over reference level and a 10 kHz tone 11.5 dB over. Under the ceiling nothing distorts and the sound comes back at its own level. Turning it up also lowers the noise and the clicks against the sound.
listen: In Hi-Fi, loud hits and held loud notes flatten against a ceiling, hard, with no softening on the way. The first milliseconds of a drum hit after silence reach it even low on the knob: the record side has turned its gain up in the silence and needs 5 ms to bring it down. That is part of the format.
try: In Simple, 3rd Gen Copy uses +6.5 dB and Bootleg Concert +9.0 dB. In VHS, SP Tape uses 45 % and EP Tape 50 %. In Hi-Fi, Hi-Fi - Clean Deck uses 56 % (0 dB) and Hi-Fi - Hot Signal 95 % (+24.9 dB).
note: In Simple, below about +1.9 dB (the first 17 % of the knob) the knob also blends the clean band with the clipped one, as Drive does.

## Highs
what: In **Simple**: the level of the high band going into its clipper. In **VHS** and **Hi-Fi**: trims the result above 1 kHz.
try: In Simple, lower Highs and raise Hi Sat for "copied again". 3rd Gen Copy has Highs at -8 dB and Hi Sat at +12.5 dB.

## Hi Sat
what: Its job depends on **Type**.
- **Simple**: how hard the high band is clipped. The read-out is the gain into the band's clipper, from 0.0 dB (clean) to +20.0 dB.
- **VHS**, captioned TAPE SPEED: the tape speed, from SP (33.35 mm/s, at 0 %) through LP (16.67 mm/s, at 63 %) to EP (11.12 mm/s, at 100 %). The read-out names those three and the speeds of other formats inside the range (see the note), and gives the speed in mm/s between them. The slower the tape, the sooner the highs give way and the lower the top of the band: it reaches 10 kHz in SP, about 6 kHz in LP and about 4.5 kHz in EP.
- **Hi-Fi**, captioned CARRIER: how strong the FM signal off the tape is, from 0 dB (a good tape on a good deck) down to -20 dB, 0.2 dB for every percent. Every dB lost is a dB more hiss. From about -14 dB the deck starts to fall to the dull mono linear track now and then, more often further down, and from about -19 dB it hardly ever comes back. Carrier does not change how many clicks there are: that is Crackle's job.
listen: In Hi-Fi, the hiss comes up, and lower down the sound drops now and then to a duller, quieter, mono version of itself with its own hiss, for about a quarter of a second each time.
try: In Simple, 3rd Gen Copy uses +12.5 dB and Bootleg Concert +10.2 dB. In VHS, 0 % is the SP Tape program and 100 % the EP Tape program. In Hi-Fi, Hi-Fi - Worn Rental has the carrier at -12 dB.
note: In Simple, below about +1.9 dB (the first 17 % of the knob) the knob also blends the clean band with the clipped one, as Drive does. The other formats' names are on the VHS read-out only: each position sounds as it did in 1.6, the VHS tape at that speed.
- PAL SP (23.39 mm/s) at 32 %
- Beta II (20.00 mm/s) at 47 %
- PAL Beta (18.73 mm/s) at 53 %
- LP at 63 %
- Beta III (13.33 mm/s) at 83 %
- PAL LP (11.70 mm/s) at 95 %

## Hi Cut
what: A 4-pole lowpass straight after the VIBE stage. It works with every Type, and with VIBE's power off.
try: About 10 kHz for a standard-speed (SP) tape, near 6 kHz for a slow-speed (EP) tape.
note: Not the HI CUT on the WOBBLE page, which is a second, later filter. With Visual Vibes on, a haze comes over the picture as it comes down below 7 kHz.

## Sat Type
what: Chooses what the VIBE stage is: three buttons, stacked.
- **Simple** is the two-band saturator, as it always was: the signal is split in two and each band gets its own level and soft clip.
- **VHS** is a model of what the tape of a VHS deck's linear track does. Quiet sound passes unchanged; loud sound loses its highs first and goes soft, the way slow tape does.
- **Hi-Fi** is a model of the FM sound the rotating heads write under the picture. It does not saturate. It stays clean up to a hard ceiling; what you hear instead is hiss that rises and falls with the music, clicks when the signal off the tape is weak, a buzz from the heads switching, and drops to the dull mono linear track when the signal is lost.
note: In VHS and Hi-Fi three knobs change their meaning and their caption (see the table after this list). In Hi-Fi, with VIBE's power on, the Hi-Fi track also makes Hiss, Crackle, Head Buzz, Breathing, Tracking, Drift and Dropouts, and B-Type NR is ignored; those seven knobs and the three keep their place and turn orange. Switching Type fades the sound out and back in.

<!-- ====================================================================== WOBBLE -->

## Warp
what: Depth of the slow pitch wobble, up to about 5 % of pitch at 100 %. The low end of the knob is fine: small settings are small.
listen: The pitch swims slowly up and down, at the speed Warp Rate sets.
try: 35 % is Home Recording. Fresh Rental uses 15 %, Chewed Tape 90 %.

## Warp Rate
what: Speed of the slow wobble, in swings per second. With Warp high, the slowest speeds hold the depth back so the swing fits the tape delay: at Warp 100 % below about 0.38 Hz, at 70 % below about 0.18 Hz. Below Warp 50 % it never happens.
try: 0.55 Hz is Home Recording. Vaporwave Mall slows to 0.33 Hz, Tracking Error runs at 1.50 Hz.
range: 0.10 to 5.00 Hz

## Shape
what: The form of the slow wobble. 0 % is a smooth, even wobble; 100 % is a lumpy, uneven lurch.
try: Chewed Tape uses 100 %.

## Flutter
what: Depth of the fast pitch wobble, up to about 1.2 % of pitch at 100 %.
listen: A fast quiver on held notes.

## Flut Rate
what: Speed of the fast pitch wobble.

## Scrape
what: Scrape flutter: the tape sticking and slipping along its path, far faster than any wobble. 0 % is off.
listen: A roughness that clings to bright sounds and is silent in silence.
try: Dirty Heads uses 30 %, Sticky Tape 65 %.

## Wobble On
what: The WOBBLE page's POWER switch. It turns both wobbles and Scrape off or on together. The two filters of this page stay in.
note: With Wobble off, Mix below 100 % blends cleanly.

## Wow
what: Depth of a second, independent slow pitch wobble: the tape after one more copy. Up to about 3 % of pitch at 100 %.

## Wow Rate
what: Speed of the second slow wobble. With Wow high, the slowest rates hold the depth back so the swing fits the tape delay: at Wow 100 % below about 0.23 Hz, at 80 % below about 0.15 Hz. Below Wow 65 % it never happens.

## Flutter 2
what: Depth of the second fast pitch wobble, up to about 1 % of pitch at 100 %.

## Flut2 Rate
what: Speed of the second fast pitch wobble.

## Lo Cut
what: Removes the lows after the second wobble.
try: About 50 Hz for a standard-speed (SP) tape, near 100 Hz for a slow-speed (EP) tape. Answer Machine goes to 300 Hz.

## Hi Cut 2
what: A lowpass after the second wobble. It is gentler than the Hi Cut on the TAPE page (2 poles against 4).
note: The host lists this parameter as Hi Cut 2; on the screen it is HI CUT in the FILTER block.

<!-- ======================================================================= NOISE -->

## Hum
what: 60 Hz mains hum.

## Buzz
what: 120 Hz buzz.

## Head Buzz
what: The video heads switching, 59.94 times a second. It makes two things: a steady rasp, and a buzz that rides on bright sounds and vanishes on bass, because the signal is held for a few samples at each switch.
note: Both follow Master and NOISE's power. Breathing (DECK page) pushes the rasp down in the pauses. With Type on **Hi-Fi** (the knob turns orange) it is how badly the deck hides the switch from one head to the other.
- At 0 % it hides it as well as the chips' datasheets say a deck does, about 75 dB under a 10 kHz tone: nothing to hear.
- Turned up, each switch leaves a spike that rides on the music and sinks with it in the pauses.
- At 100 % it is a deck whose heads are far out of line, louder than a real fault could be.

## Hiss
what: Tape hiss, different in left and right.
try: 35 % is Home Recording. Hi-Fi Master uses 12 %, Six Hour Tape 65 %. For a slow-speed (EP) tape, raise it.
note: With Type on **Hi-Fi** (the knob turns orange) it is the noise of the FM channel instead.
- It goes in before the playback side, which pushes it down in quiet passages and lets it up under loud ones, so it rises and falls with the music on its own.
- At Master 0.0 dB, 9 % is a real deck's noise, 55 dB under reference level (the program Hi-Fi - Clean Deck); 50 % is 25 dB under.
- Whenever the deck falls to the linear track, this knob also sets that track's hiss, twice as loud as the other Types' hiss.

## Crackle
what: Random clicks. The knob sets both how many and how loud.
note: With Type on **Hi-Fi** (the knob turns orange) it sets only how many: how close the deck sits to the point where the FM signal breaks into clicks.
- About 0 clicks a second on each channel at 10 %, 1 at 25 %, 5 at 50 %, 30 at 75 %, 150 at 100 %, single clicks even at the top.
- Their size follows Master. The Carrier knob does not change the count.
- Whatever weakens the signal for a moment (Drift, Tracking's dips, Dropouts) brings a burst of them. Before they could turn into a static louder than the music, the deck falls to the linear track.

## Noise
what: Level of all noise together. The read-out is the gain on every noise against the nominal level, which sits at the middle of the knob: 0.0 dB is nominal, -12.0 dB is a quarter of the way up, +12.0 dB is the top. Fully down it reads off and the noise is silent.
try: Most programs leave it at 0.0 dB. Wedding Video uses -3.9 dB, Bootleg Concert -6.2 dB.
note: The host lists this parameter as Noise; on the screen it is MASTER. The read-out stays when NOISE's power is off, as every knob's does.
range: off, then up to +12.0 dB

## Noise Pos
what: Where the noise enters. **Post**: after the first wobble, so the noise is clean. **Pre**: at the input, so the noise is compressed, clipped and wobbled with the music.
listen: On Pre with Comp or AGC turned up, the noise swells whenever the music stops. That is how a real recorder's level control behaves; turn Master down if it is too much.

## Noise On
what: The NOISE page's POWER switch. It turns all noise off or on: Hum, Buzz, Head Buzz, Hiss, Crackle and Whistle.
note: The effect makes noise even with no input. Switch this off, or turn Master down to off, for a silent background. In Hi-Fi it also silences the FM channel's noise, its clicks and the head switch, and the linear track's hiss; the deck still holds the sound and falls to the linear track when the signal is lost, as that is part of the machine.

## Whistle
what: The line whistle of the TV set, 15.7 kHz, steady, never wobbling. At 50 % it is about where a real deck has it. Up to 60 % it is as it always was; above that it climbs faster, to 15 dB more at 100 % than before 1.7: a set that screams.
listen: A very high, thin tone. Not everyone can hear it.
note: It is added late in the path, after every filter and both wobbles. Dropouts dip it with the rest of the sound, unless the NR playback side is on: then it is added after that, at the very end. It follows Master and NOISE's power like the other noises.

<!-- ======================================================================= INPUT -->

## Chorus
what: Switches the chorus on or off. The chorus is a short modulated delay in front of everything else, with left and right out of phase for width.
try: Clean Widener uses only the chorus, as a stereo widener.

## Cho Mix
what: How much of the chorus is blended in.

## Cho Speed
what: How fast the chorus delay moves.

## Cho Delay
what: The length of the chorus delay.

## Cho Width
what: Spreads the chorus left and right. 0 % moves both sides together; 100 % moves them half a cycle apart.

## Mic
what: Plays the sound into one of four period microphones before it reaches the recorder, as if it had been re-recorded through one. Mic Model chooses which set of four. The mic fades in and out when switched.
note: On Modeled the mic adds its own noise (the capsule's hiss, or the phone line's), at the level the model gives it. NOISE's power silences it; the Master knob does not change it. The Simple ones add none.

## Mic Type
what: Which of the four microphones the Mic plays the sound into. What each one is depends on **Mic Model**: on Simple a tone curve and nothing more, on Modeled a real kind of microphone with the recorder it sat in.
- **Camcorder.** Simple: thin and forward. Modeled: a camcorder across the room. Loud and quiet are pulled to one level, the hiss swells in every pause, loud hits crunch.
- **Dynamic.** Simple: the widest, with a presence lift. Modeled: a stage microphone. A gentle low cut and a presence lift, nothing more.
- **Phone.** Simple: narrow and nasal. Modeled: a telephone line. No bass, no top, and quiet words fade more than loud ones.
- **Boombox.** Simple: dull and boxy. Modeled: a radio cassette's built-in microphone. A whisper and a shout come out at nearly one level, with tape hiss and a rounded top.
listen: On Modeled, stop the music and the hiss and the room climb back up.
- Boombox: over about seven seconds.
- Camcorder: over about two and a half, or ten or more after a sound too loud for it.
- Phone: nothing climbs, but quiet words sink further than loud ones.
try: On Simple, Camcorder 89 and Wedding Video use Camcorder, Answer Machine uses Phone, Bootleg Concert and Tube TV use Boombox. The five programs named Mic - ... are the same on Modeled.
note: Changing the type fades the mic out, puts the new one in and fades it back in. The figures for each microphone are in *The microphones: Simple and Modeled*, after this page's controls.

## Mic Gain
what: On **Simple**, the level of the microphone, after its curve. On **Modeled**, how loud the sound is at the microphone, as if the source were louder or closer: it comes before the microphone, so it decides how hard the mic is driven. It works only while Mic is on.
listen: On Modeled, each microphone answers differently.
- Camcorder: turned down, everything sits at one level and the hiss swells in the gaps. Turned up, loud sounds crunch and the hiss waits longer to come back. The level that comes out changes little.
- Boombox: the level hardly moves, only how close the hiss sits.
- Phone: turned down, quiet words sink.
- Dynamic: a plain level.
try: Mic - Answer Machine uses +3 dB, Mic - Camcorder 89 and Mic - Bootleg Concert +2 dB, Mic - Wedding Video +1 dB.
note: The microphone comes before AGC and the preamp, so more gain here also drives those harder. On Modeled a sound peaking at -8 dBFS counts as 104 dB SPL at the microphone; this mapping is an assumption. What each step of Mic Gain does to the Camcorder is in the chapter ＩＮＳＩＤＥ　ＴＨＥ　ＭＡＣＨＩＮＥ.

## Mic Model
what: Which set of four microphones Mic Type chooses from.
- **Simple**: the microphones as they were up to version 1.5, each one fixed tone curve, in stereo, with no noise of its own. Mic Gain is a level after it.
- **Modeled**: the microphones built from published figures, from version 1.6: their response, how they overload, their noise, and for the Camcorder and the Boombox the level control of the recorder they sat in.
listen: Simple colors the sound and leaves its loud and quiet parts as they were. Modeled also moves them: the level controls pull them together and lift the hiss in the pauses.
try: Camcorder 89, Answer Machine, Wedding Video, Bootleg Concert and Tube TV use Simple; the five programs named Mic - ... are the same on Modeled.
note: A new instance starts on Modeled. A project saved before version 1.6 opens on Simple, so it sounds as it did. Switching fades the mic out, puts the other one in and fades it back in.

<!-- ======================================================================== DECK -->

## Deck On
what: Switches AGC, Drift, Tracking and Breathing off or on together, in every Type.
note: NR, NR Clipper, HQ, Azimuth, Hysteresis, Animation and Visual Vibes are not affected; each has its own switch or rests at 0. The Init and Clean Widener programs switch the deck off.

## AGC
what: The recorder's automatic level control, in one of two kinds (**AGC Model**). The knob sets how much of it you hear: 100 % is the whole effect, 0 % off; on Modeled, 50 % is half of every change in dB.
- **Simple.** A loud sound pulls the gain down at once; it stays down for a second and a half, then creeps back up over a few seconds and brings the background up with it, by up to 14 dB.
- **Modeled.** An early VHS deck's record level circuit. Sound peaking at -8 dBFS passes at its own level. Quieter sound is lifted by 9.1 dB, up to the point where the circuit starts to work, 10 dB below that (peaks around -18 dBFS). Louder sound is cut: every 10 dB more going in comes out only about 1 dB louder. A sudden loud sound is cut by 10 dB in about 11 ms, and most of the rest within half a second. When it stops, the gain comes back at about 1 dB per second: after a big hit, 18 dB takes about 19 seconds.
listen: After a loud hit, the quiet that follows slowly swells, noise included. On Modeled it swells for far longer, and the first moments of a loud entry come through louder before the cut lands.
try: 25 % is Home Recording. Camcorder 89 uses 80 %, Wedding Video 90 %. All of them use Simple.
note: Comp works in milliseconds; this works in seconds. On Modeled the AGC starts from full gain after a program change, the way the deck started each recording: for most of the first second, loud sound comes through lifted (the clip after Comp keeps it in bounds).

## Drift
what: The sound slowly comes and goes: quieter and duller for a moment, then back, like the audio track wandering off the head.
try: 10 % is Home Recording. Chewed Tape uses 85 %, Edge Damage 95 %.
note: Dropouts are short dips; this is slow. With Type on **Hi-Fi** (the knob turns orange) it is the strength of the FM signal off the tape wandering slowly, up to 22 dB down at 100 %, one slow swing every several seconds. The Hi-Fi track does not go duller: the hiss swells, clicks come in bursts at the low points, and the deepest ones drop the deck to the linear track.

## AGC Model
what: Which automatic level control the AGC knob turns up. **Simple**: the AGC as it was up to version 1.5. **Modeled**: an early VHS deck's record level circuit, rebuilt part by part from its service manual, from version 1.6. It ducks fast, lifts quiet sound by 9.1 dB and comes back at about 1 dB per second (the numbers are under AGC).
listen: Simple pumps: down, a pause, back up within seconds. Modeled holds a loud passage down and lets the quiet after it rise slowly, over tens of seconds.
try: Every program with the AGC up uses Simple. Turn the AGC up on Init with the deck on to hear Modeled.
note: It rests on Modeled. Init and every program that leaves the AGC at 0 % are on Modeled, so turning the AGC up there gives the Modeled one.
- Every program with the AGC up, Home Recording included, uses Simple.
- A project saved before version 1.6 opens on Simple, so it sounds as it did.
- Switching while sound plays does not click: the new AGC starts from rest and settles unheard (Modeled for a second, Simple for a moment), then the sound crossfades to it in 0.1 s.
- How the Modeled one was built is in the chapter ＩＮＳＩＤＥ　ＴＨＥ　ＭＡＣＨＩＮＥ.

## Tracking
what: Its job depends on **Type**.
- **Simple** and **VHS**: the Hi-Fi track loses lock now and then: a burst of static, a moment on the dull, quieter mono backup track, another burst, and back. More often only the right channel crackles for a moment, as it is the first to go on a real deck. Higher is more often.
- **Hi-Fi** (the knob turns orange): the Hi-Fi track itself mistracks. One head reads weaker than the other, up to 4 dB at 100 %, so noise and clicks change with every head pass. The signal dips, 14 to 30 dB deep, for one to eight head passes: up to 4 dips a second at 100 %, about one every two seconds at 25 %. A deep dip brings clicks, a hold or a drop to the linear track.
try: Fresh Rental uses 6 %, Bad Tracking 65 %, Tracking Error 85 %. In Hi-Fi, Hi-Fi - Tracking Lost uses 45 %.

## Breathing
what: Its job depends on **Type**.
- **Simple** and **VHS**: the Hi-Fi noise reduction at work. The hiss rises and falls with the signal, and Head Buzz is pushed down in the pauses.
- **Hi-Fi** (the knob turns orange): the hiss already does that by itself. Breathing plays the tape on another deck, whose playback side follows the sound its own way: it reacts up to a fifth faster or slower than the record side did, and it measures the average level of the sound where the record side measured its peaks. Steady tones keep their level; moving sound pumps, by up to about 4.5 dB at 100 %.
listen: Hiss that swells behind each note and sinks after it. In Hi-Fi, drums and sharp attacks lift and duck the level around them.
try: Fresh Rental uses 15 %, Hi-Fi Master 35 %, Worn Heads 45 %. In Hi-Fi, Hi-Fi - Wrong Deck uses 100 %.
note: In Simple and VHS it shapes Hiss and Head Buzz, so it needs some of either on the NOISE page. In Hi-Fi it works on the music itself.

## Azimuth
what: The tilt of the playback head against the recording, as when a tape recorded on one deck is played on another, in minutes of arc (a sixtieth of a degree each). The head then hears each moment a little smeared across its tilted gap, so the treble cancels: the top turns hollow and notched, and it swishes slowly as the tilt wanders with the tape. On the DECK page only while Type is **VHS**; it needs VIBE's power on.
listen: Highs that dull and swish. Turned further, a hollow top with notches and faint highs between them. At EP everything happens three times lower, as the tape runs three times slower.
try: The knob gives 60' times the square of its position: 3.8' at 25 %, 15' at 50 %, 34' at 75 %. A tape from another deck is 3' to 8' off, at 22 to 37 %; 60' is a head knocked out of line. VHS - Borrowed Tape uses 6.5', VHS - Wrong Deck EP 7.5'.
note: It rests at 0', where the stage is not there at all. TAPE SPEED sets the speed the tape is read at. With B-Type NR on ^ON^ the dulling grows, because the decoder mistracks on it. How it is built is in the chapter ＩＮＳＩＤＥ　ＴＨＥ　ＭＡＣＨＩＮＥ.
range: 0' to 60'

## Animation
what: The moving reels, grid and sun on or off. Off saves the work of updating the screen.
note: Choosing a program does not change it. It makes no difference to the sound. It is also the master switch of Visual Vibes: off, the picture effects stop too.

## Visual Vibes
what: The picture shows the sound. On the page sticker, the picture behaves like a worn video tape playing what the plugin is doing: the sun in the Type's own shade (pink for Simple, mint for VHS, orange for Hi-Fi), snow that thickens with the hiss, a strip of noise along the bottom with Head Buzz, a white streak at each dropout, a band of noise that rolls up the picture when the tracking is lost, the grid bending with the wobble, and a haze as Hi Cut comes down.
note: Off in a new instance and in a project from an older version. It needs Animation on. Choosing a program does not change it, and it makes no difference to the sound. The plugin updates the picture up to 20 times a second through five more read-only parameters (see *The picture shows the sound*, in the chapter ＳＣＲＥＥＮ).

## HQ
what: Runs the stages that distort (Drive, Slew, Comp, the VIBE stage, the modeled microphones and the recording side of B-Type NR) at twice the sample rate, so bright sounds that hit them do not fold back into the midrange as tones that were never played.
listen: With HQ off, loud bright sounds grow extra, unrelated tones underneath. With it on, they are gone for highs at ordinary levels.
note: On takes more CPU. Switching it fades the sound out and back in. Choosing a program does not change it. More in the HQ section after this list.

## Hysteresis
what: With Type on **VHS**, records the tape through a model of tape hysteresis in place of the plain tape curve. **Off**: the tape as before. **On (CPU+)**: the Preisach model of the chapter ＩＮＳＩＤＥ　ＴＨＥ　ＭＡＣＨＩＮＥ. With it on, BIAS (the Split knob) becomes a real bias control, and TAPE SPEED also picks how the tape records: as at SP below half way, as at EP from there.
listen: Turn BIAS down: the sound gets gritty, quiet sounds break up or vanish as if gated, and the top gets brighter. Turn it up: duller and cleaner.
try: VHS - Factory Aligned has the bias right; VHS - Whisper Gate has almost none.
note: On the DECK page only while Type is VHS; it needs VIBE's power on. The ON button carries a small bar: it costs CPU (on an MPC One+ about 6.5 % of one core, against about 2.9 % with it off). With it on the sound comes about 1 ms later, and the plugin tells the host. Switching crossfades over 10 ms. Off in a new instance, in an older project and in every program but the six from VHS - Factory Aligned to VHS - Six Hour Starved.

## B-Type NR
what: B-type noise reduction, as on the linear tracks of early stereo decks. **Off**: none. **Rec**: recorded with it, played without. **Play**: recorded without, played with. **On**: both.
note: The four positions and what each sounds like are in the B-Type NR section after this list. Switching fades it in or out over 8 ms. It belongs to the linear track, so with Type on Hi-Fi and VIBE's power on it is ignored.

## NR Clipper
what: A diode clipper on the NR side chain, the control path that decides how much the NR lifts and cuts. 0 % is clean: the diodes only catch sudden jumps. 50 % is the amount of every version before 1.8. Above 50 % the diodes are driven hard and it gets dirty.
listen: The grit follows the music: it shows mostly on and around quiet passages, and changes with what goes in. Higher up, loud passages get dirty too, and the sound gets brighter and louder.
try: NR - Fuzz In The Gaps uses 80 %, NR - Blown Encoder 100 %. Stereo Rental sits at 50 %.
note: It does nothing while B-Type NR is Off, or while NR is ignored in Hi-Fi. 0 % in a new instance; a project saved with an older version loads at 50 %. More in the NR Clipper section after this list.
