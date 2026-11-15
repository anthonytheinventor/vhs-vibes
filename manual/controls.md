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
what: Soft clipping in the preamp, the recorder's input stage. 0 % is clean; the further you turn it, the harder the sound is pushed into the clipper.
listen: Peaks round off and the sound thickens. Sound around -8 dBFS keeps its loudness; quieter sound comes up, louder sound is held down.
try: 25 % is Home Recording. Subtle Glue uses 20 %, Bootleg Concert 60 %.

## Tone
what: A tilt in front of the Drive clipper. Left of centre is darker, right of centre is brighter, about 6 dB each way. 0 is flat.
try: Training Video sits at -15, Dream Sequence at +10.

## Slew
what: A slew limiter: it limits how fast the signal can move. The number under the knob is the shortest time, in microseconds, the limit leaves for a swing from silence to full level. Sound that moves more slowly than that passes untouched; sound that tries to move faster is straightened into a ramp. That dulls and smears loud, bright sound and leaves quiet sound alone, which a filter cannot do.
listen: The top end dulls and the detail smears, more on loud passages than on quiet ones.
try: 32 µs is Home Recording: a full-level tone passes clean up to about 5 kHz. 3rd Gen Copy uses 113 µs (1.4 kHz), Chewed Tape 146 µs (1.1 kHz). The longest, 1890 µs, holds a full-level tone clean only up to 84 Hz. A tone 12 dB quieter passes four times as high.
note: Turned fully down the read-out says off and the stage is skipped. The MPC lists the control from 0 to 100 %; the read-out turns that into the time.
range: off, then 12 to 1890 µs

## Comp
what: A one-knob compressor with make-up gain. Turning it up lowers the threshold (down to -36 dB at 100 %, ratio 4:1) and raises the make-up. Past about a third it squashes and brings quiet sound, and noise, up.
listen: Quiet passages rise. With Position on Pre (NOISE page) the noise rises with them whenever the music stops.
note: At 0 % the threshold is at full scale: only peaks above 0 dBFS are still compressed. Comp works in milliseconds. For a level control that works over seconds, use AGC on the DECK page.

## Dropouts
what: Short random dips, as when the tape lifts off the head. The highs go first and furthest; the lows only sag. The knob sets both how often they come and how deep they go. 0 % is off.
listen: Dips of a few milliseconds in which the sound goes dull rather than silent.
try: Bad Tracking uses 15 %, Edge Damage 30 %, Chewed Tape 80 %.
note: Dropouts are short. For a slow fading in and out, use Drift on the DECK page.

## Mono
what: Sums left and right into one. It sits near the end of the path, after everything except Mix and Output.

## Mix
what: Dry / wet. 100 % is all effect; lower lets the untouched input back in. The dry signal is delayed to line up with the tape path.
listen: With the wobbles off the blend is clean. With them on, dry and wet drift against each other and it sounds like a chorus. That is the nature of mixing a wobbling signal with a steady one.

## Output
what: Output level. The last stage.

## Saturator
what: Switches the saturator stage on or off, whichever Type is chosen. Off, the controls Split, Lows, Lo Sat, Highs, Hi Sat and Type do nothing.
note: Hi Cut is drawn in the same block but is a stage of its own: it stays in when Power is off.

## Split
what: In **2-Band**: the frequency at which the signal is divided into a low band and a high band. In **VHS** the caption reads BIAS: how well the deck is set up for the tape. 2.5 kHz, where the knob rests, is right. Lower is too little bias: brighter, grittier, distorting sooner. Higher is too much: duller, the highs giving way sooner.
note: In VHS the read-out shows "right" at 2.5 kHz, a minus percentage for too little bias and a plus percentage for too much.

## Lows
what: In **2-Band**: the level of the low band going into its clipper. In **VHS**: trims the result below 1 kHz.

## Lo Sat
what: In **2-Band**: how hard the low band is clipped. In **VHS** the caption reads REC LEVEL: the record level, from 10 dB under to 8 dB over the deck's reference level, which is at 56 %: there a tone at -8 dBFS has 3 % distortion, as on a real deck. Quiet sound comes back at the level it went in whatever the setting; what changes is how much of the loud sound the tape bends.
try: In VHS, SP Tape uses 45 % and EP Tape 50 %.

## Highs
what: In **2-Band**: the level of the high band going into its clipper. In **VHS**: trims the result above 1 kHz.
try: In 2-Band, lower Highs and raise Hi Sat for "copied again". 3rd Gen Copy has Highs at -8 dB and Hi Sat at 60 %.

## Hi Sat
what: In **2-Band**: how hard the high band is clipped. In **VHS** the caption reads TAPE SPEED: the tape speed, from SP (33.35 mm/s, at 0 %) through LP (16.67 mm/s, at 63 %) to EP (11.12 mm/s, at 100 %). The read-out names those three and gives the speed in mm/s between them. The slower the tape, the sooner the highs give way and the lower the top of the band: it reaches 10 kHz in SP, about 6 kHz in LP and about 4.5 kHz in EP.
try: In VHS, 0 % is the SP Tape program and 100 % the EP Tape program.

## Hi Cut
what: A 4-pole lowpass straight after the saturator. It works with either Type, and with the saturator switched off.
try: About 10 kHz for a standard-speed (SP) tape, near 6 kHz for a slow-speed (EP) tape.
note: Not the HI CUT on the WOBBLE page, which is a second, later filter.

## Sat Type
what: Chooses what the saturator stage is. **2-Band**: the signal is split in two and each band gets its own level and soft clip. **VHS**: a model of what the tape of a VHS deck's linear track does (the Hi-Fi track does not saturate this way). Quiet sound passes unchanged; loud sound loses its highs first and goes soft, the way slow tape does.
note: In VHS three knobs change their meaning and their caption (see the table after this list). Switching Type fades the sound out and back in.

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
listen: A quiver on held notes rather than a swim.

## Flut Rate
what: Speed of the fast pitch wobble.

## Scrape
what: Scrape flutter: the tape sticking and slipping along its path, far faster than any wobble. 0 % is off.
listen: A roughness that clings to bright sounds and is silent in silence.
try: Dirty Heads uses 30 %, Sticky Tape 65 %.

## Wobble On
what: Turns both wobbles and Scrape off or on together. The two filters of this page stay in.
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
note: Both follow Master and the Noise switch. Breathing (DECK page) pushes the rasp down in the pauses.

## Hiss
what: Tape hiss, different in left and right.
try: 35 % is Home Recording. Hi-Fi Master uses 12 %, Six Hour Tape 65 %. For a slow-speed (EP) tape, raise it.

## Crackle
what: Random clicks. The knob sets both how many and how loud.

## Noise
what: Level of all noise together. 50 % is nominal, 0 % is silent, 100 % is 12 dB above nominal.
note: The host lists this parameter as Noise; on the screen it is MASTER.

## Noise Pos
what: Where the noise enters. **Post**: after the first wobble, so the noise is clean. **Pre**: at the input, so the noise is compressed, clipped and wobbled with the music.
listen: On Pre with Comp or AGC turned up, the noise swells whenever the music stops. That is how a real recorder's level control behaves; turn Master down if it is too much.

## Noise On
what: Turns all noise off or on: Hum, Buzz, Head Buzz, Hiss, Crackle and Whistle.
note: The effect makes noise even with no input. Switch this off, or set Master to 0 %, for a silent background.

## Whistle
what: The line whistle of the TV set, 15.7 kHz, steady, never wobbling. It is added late in the path, after every filter and both wobbles. Dropouts dip it with the rest of the sound, unless the NR playback side is on: then it is added after that, at the very end. At 50 % it is about where a real deck has it.
listen: A very high, thin tone. Not everyone can hear it.
note: It follows Master and the Noise switch like the other noises.

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
what: Plays the sound into one of four period microphones before it reaches the recorder, as if it had been re-recorded through one. Each is built from published figures: the chapter ＩＮＳＩＤＥ　ＴＨＥ　ＭＡＣＨＩＮＥ lists them. The mic fades in and out when switched.
note: While the Mic is on it adds its own noise (the capsule's hiss, or the phone line's), at the level the model gives it. The Noise switch silences it; the Master knob does not change it.

## Mic Type
what: Which microphone. **Camcorder**: a camcorder's built-in microphone and the record chain behind it. Mono, as the camera was. The electret capsule (3 dB down at 53 Hz, a rise of about 2.5 dB around 15 kHz, its own faint hiss), the mic amp's band (a further low cut near 40 Hz, 3.6 dB down at 15 kHz) and its overload above about 107 dB SPL, then the camera's record level control: 42 dB of gain, everything from 61 to 83 dB SPL held within 1.2 dB of one level, and in every pause the hiss lifted by up to 21 dB. Louder than that range, the level control is at its lowest and loud sounds and sudden onsets crunch. Its timing (20 ms to catch a sound, 8 dB a second back) is an estimate. **Dynamic**: a moving-coil stage microphone of the Shure 515 kind: a gentle low cut (8 dB down at 100 Hz), a presence peak of 5 dB at 5 kHz, a smaller one at 8 kHz, falling away above 10 kHz. **Phone**: the CCITT P.48 reference telephone: 300 Hz to 3.4 kHz with a broad rise to 3 kHz and steep edges, the second harmonic of a carbon microphone, its sensitivity that falls with the level (quiet sound comes through weaker still), and the line's faint noise. Mono, as a phone line is. **Boombox**: the same electret, a record level control (ALC) that turns everything from a whisper to a shout into nearly the same level, and the cassette's 50 Hz to 10 kHz with its hiss. Mono, as a radio cassette's built-in microphone was.
listen: Boombox: after a loud passage the ALC lets go over about seven seconds and the room, the hiss and every quiet sound come up with it. Camcorder: the same in about two and a half seconds, but after a sound louder than its range it first holds the gain down for longer, ten seconds or more. Phone: quiet words sink further than loud ones.
try: Camcorder 89 and Wedding Video use Camcorder, Answer Machine uses Phone, Bootleg Concert and Tube TV use Boombox.
note: Changing the type fades the mic out, puts the new one in and fades it back in.

## Mic Gain
what: How loud the sound is at the microphone: as if the source were louder or closer. It is applied before the microphone, so it decides how hard the mic itself is driven. It works only while Mic is on.
listen: Camcorder: its level control works from 61 to 83 dB SPL, which a sound peaking near -8 dBFS is well past even at -12 dB. Lower means less crunch on loud sounds and the hiss coming up sooner after them; higher means more crunch. The level that comes out changes little. Phone: down, and quiet words sink. Boombox: the ALC keeps the level almost the same; what changes is how close the noise sits. Dynamic: a plain level.
note: The microphone comes before AGC and the preamp, so more gain here also drives those harder. A sound peaking at -8 dBFS counts as 104 dB SPL at the microphone; this mapping is an assumption (see the chapter ＩＮＳＩＤＥ　ＴＨＥ　ＭＡＣＨＩＮＥ).

<!-- ======================================================================== DECK -->

## Deck On
what: Switches AGC, Drift, Tracking and Breathing off or on together.
note: NR, HQ and Animation are not affected; each has its own switch. The Init and Clean Widener programs switch the deck off.

## AGC
what: The recorder's automatic level control. A loud sound pulls the gain down at once; it stays down for a second and a half, then creeps back up over a few seconds and brings the background up with it, by up to 14 dB.
listen: After a loud hit, the quiet that follows slowly swells, noise included.
try: 25 % is Home Recording. Camcorder 89 uses 80 %, Wedding Video 90 %.
note: Comp works in milliseconds; this works in seconds.

## Drift
what: The sound slowly comes and goes: quieter and duller for a moment, then back, like the audio track wandering off the head.
try: 10 % is Home Recording. Chewed Tape uses 85 %, Edge Damage 95 %.
note: Dropouts are short dips; this is slow.

## Tracking
what: The Hi-Fi track loses lock now and then: a burst of static, a moment on the dull, quieter mono backup track, another burst, and back. More often only the right channel crackles for a moment, as it is the first to go on a real deck. Higher is more often.
try: Fresh Rental uses 6 %, Bad Tracking 65 %, Tracking Error 85 %.

## Breathing
what: The Hi-Fi noise reduction at work: the hiss rises and falls with the signal, and Head Buzz is pushed down in the pauses.
listen: Hiss that swells behind each note and sinks after it.
try: Fresh Rental uses 15 %, Hi-Fi Master 35 %, Worn Heads 45 %.
note: It shapes Hiss and Head Buzz, so it needs some of either on the NOISE page.

## Animation
what: The moving reels, grid and sun on or off. Off saves the work of updating the screen.
note: Choosing a program does not change it. It makes no difference to the sound.

## HQ
what: Runs the stages that distort (Drive, Slew, Comp, Saturator) at twice the sample rate, so bright sounds that hit them do not fold back into the midrange as tones that were never played.
listen: With HQ off, loud bright sounds grow extra, unrelated tones underneath. With it on, they are gone for highs at ordinary levels.
note: On takes about a quarter more CPU. Switching it fades the sound out and back in. Choosing a program does not change it. More in the HQ section after this list.

## B-Type NR
what: B-type noise reduction, as on the linear tracks of early stereo decks. **Off**: none. **Rec**: recorded with it, played without. **Play**: recorded without, played with. **On**: both.
note: The four positions and what each sounds like are in the B-Type NR section after this list. Switching fades it in or out over 8 ms.
