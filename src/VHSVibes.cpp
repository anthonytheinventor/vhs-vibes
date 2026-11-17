// VHS Vibes - a VHS tape degradation effect for the MPC, by anthonytheinventor
//
// SPDX-License-Identifier: MIT
//
// Inspired by the idea of James Peck's "VHS Audio Degradation Suite" for Reaktor (the
// order of the stages and what each one is for). All code here is original; nothing is
// taken from the Reaktor ensemble and no samples are used: every noise is synthesised.
//
// Signal path (stereo, in place):
//
//   in -> [noise, if Noise Pos = Pre]
//      -> CHORUS      short modulated delay, L/R out of phase for width
//      -> MIC         one of four period microphones ("re-recorded through a mic"), in one of
//                     two sets (Mic Model): Simple, a three-filter curve each, as up to 1.5
//                     (see SimpleMic); Modeled, built from published figures: camcorder
//                     electret with its mic amp and record ALC, dynamic, telephone, boombox
//                     with its record ALC (see MicModel)
//      -> AGC         (deck) the recorder's automatic level control: ducks fast, creeps back up.
//                     Two of them (AGC Model): Simple, as up to 1.5; Modeled, an early VHS
//                     deck's record level circuit (see ModeledAgc)
//      -> PREAMP      tilt tone control into a soft clipper (the VCR's input stage). Between
//                     the two: NR ENCODE, (deck) B-type noise reduction, record side: lifts
//                     quiet highs
//      -> SLEW        slew limiter: the signal cannot change faster than a set rate
//      -> COMP        one-knob compressor with make-up gain
//      -> 2-BAND SAT  (Sat Type = Simple) crossover at Split; lows and highs get their own
//                     level and soft clip.
//                     Or, with Sat Type = VHS, a model of the linear track's tape (see TapeSat).
//                     Or, with Sat Type = Hi-Fi (1.7), the FM track: compander, limiter, the FM
//                     channel's noise, clicks, head switch and losses, the fall-back to the linear
//                     track (see HiFiPath). With the Saturator on it also takes over Hiss, Crackle,
//                     Head Buzz, Breathing, Tracking, Drift and Dropouts (their stages below rest)
//                     and B-Type NR is ignored.
//      -> HI CUT      4-pole lowpass
//      -> WOBBLE 1    pitch wobble (Warp + Flutter): a delay line whose length follows the
//                     integrated tape-speed error. Scrape shakes the same delay with noise.
//      -> [noise, if Noise Pos = Post]
//      -> WOBBLE 2    a second pitch wobble (Wow + Flutter 2), as if the tape was copied again,
//                     followed by its own low cut and high cut
//      -> DRIFT       (deck) the sound slowly comes and goes: quieter and duller, then back
//      -> TRACKING    (deck) the Hi-Fi track loses lock: a burst of static, a moment on the dull
//                     mono backup track, another burst, back again
//      -> HEAD BUZZ   besides its steady rasp (a noise), Head Buzz also holds the signal for a
//                     few samples at every head switch, which makes a buzz that rides on bright
//                     sounds and vanishes on bass
//      -> WHISTLE     the TV set's line whistle, added here, after every filter and wobble, so
//                     it never wavers; only the dropouts still dip it
//      -> DROPOUTS    short random dips that take the highs first
//      -> NR DECODE   (deck) B-type noise reduction, playback side: cuts quiet highs, and the
//                     hiss with them. Whatever the tape did in between makes it mistrack.
//                     (While it runs, the whistle is added after it, not before the dropouts.)
//      -> MONO -> MIX (dry / wet) -> OUTPUT
//
// Breathing (deck) makes the hiss rise and fall with the signal and holds the head buzz down
// in the pauses.
//
// What comes from real VCR data (manuals, servicing books): heads switch once per video field
// (59.94 Hz) and the join is audible as a buzz on high notes; a Hi-Fi deck that loses its
// carrier makes static and falls back to the mono linear track; camcorder level controls duck
// fast, hold for seconds and creep back up; Hi-Fi noise reduction "breathes" (5 ms up, 70 ms
// back); the right Hi-Fi channel fails before the left; a dropout is the tape lifting off the
// head, which costs the highs far more than the lows; the line whistle sits at 15.734 kHz.
// The other times and depths are not documented anywhere I could find and are set by ear.
// Outside Sat Type = VHS tape speed has no control of its own: SP is roughly Hi Cut 10 kHz / Lo Cut 50 Hz, EP
// roughly 6 kHz / 100 Hz with about 3 dB more hiss.
//
// Mix blends the untouched input back in. The dry signal is delayed by the resting length
// of the two wobble delays so that it lines up with the tape path; with the wobbles off the
// blend is clean, with them on dry and wet drift against each other and it sounds like a
// chorus. At 100 % (the default) the dry path is not computed.
//
// HQ (on in a new instance since 1.5) runs the stages that distort, from the preamp clip to the
// saturator, at twice the sample rate between two half-band filters. Without it, what those
// stages add above 22 kHz folds back down as tones that were never in the sound (a loud
// 14 kHz comes back at 2.1 kHz). With it, what the soft part of the distortion adds is
// filtered off almost entirely; what hard clipping and Slew add when a full-level high tone
// drives them goes further up than twice the rate can hold, and that part comes down by
// 6 to 20 dB only. It costs about a quarter more CPU.
//
// Compatibility: parameters are only ever appended, and a new one rests where the plugin
// sounds as it did without it (Scrape and Whistle at 0, NR Off, Sat Type Simple, the 2-band
// saturator; the choice was called 2-Band up to 1.6, with the same stored value). Programs keep
// their numbers: 0 to 20 leave Scrape and Whistle at 0, 0 to 24 (the 1.4 set) leave NR and
// Sat Type at rest, 25 to 33 are the 1.5 programs, 34 to 38 the 1.6 ones. So a project saved
// with an older version loads with every setting in place. 1.6 added the modelled microphones
// beside the old ones (Mic Model): a state saved before it loads on Simple, the old ones, and
// the five older programs with the Mic on keep them; a new instance starts on Modeled. The
// same for the AGC (AGC Model): an older state loads on Simple, every program with the AGC up
// keeps the Simple one (Home Recording too), the parameter itself rests on Modeled (Init and
// the programs with the AGC at 0 have it). 1.7 gave Sat Type a third value, Hi-Fi, after the two:
// a saved state keeps VHS as VHS (it stores the plain value, which wins), and no program uses
// Hi-Fi; only something that stores the bare knob position (recorded automation, a Q-Link's
// position) reads the old VHS position as Hi-Fi now, which was accepted. 1.7 also added Visual
// Vibes, a switch for the picture only (on in a new instance and in an older state), and five more
// meters for it after it; the sound is the same whatever it says.
//
// Timing: the wobble delays hold the sound back by their resting length (a few ms on most
// presets). That length is reported to the host as the plugin's latency, half a second after
// it stops changing, so a host that compensates keeps the track in time.
//
// CPU notes for the MPC (32-bit ARM): everything per sample is plain arithmetic. Sines,
// square roots and all "which way is the knob going" work happen every 16 samples
// (controlTick); exp/pow/biquad coefficients are computed only when a parameter moved
// (readParams). Left and right travel through the chain together as one two-float vector
// (the Stereo type below), which the compiler turns into NEON instructions on the MPC, so
// the two channels cost little more than one. A stage whose amount is zero is skipped, a
// wobble stage with no wobble reads its delay without interpolating, and a knob move only
// recomputes the values of its own group (see groupOf()).

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/biquad.h"
#include "dsp/noise.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

using namespace mpc::dsp;

namespace {

// ---------------------------------------------------------------------------
// Parameters. The order is permanent (saved projects and the skin use the index):
// only ever append new ones at the end.
// ---------------------------------------------------------------------------
enum ParamIndex
{
    kOutput = 0,   // dB    output level. Index 0 is nudged by the MPC on load: harmless here.
    // preamp / tape
    kDrive,        // %     preamp soft clip amount
    kTone,         // -100..100  tilt (shown as "Tilt", read out in dB): negative = darker, positive = brighter
    kWear,         // %     slew limiter amount (shown as "Slew", read out as a swing time in microseconds)
    kComp,         // %     compressor amount (threshold down, make-up up)
    kHiCut,        // Hz    4-pole lowpass
    // 2-band saturator
    kSatOn,        // bool
    kLows,         // dB    low band level into its clipper
    kLoSat,        // %     low band clip amount
    kSplit,        // Hz    crossover
    kHighs,        // dB    high band level into its clipper
    kHiSat,        // %     high band clip amount
    // wobble 1 (tracking / speed instability)
    kWarp,         // %     slow pitch wobble depth
    kRpm,          // rpm   slow wobble rate (shown as "Warp Rate" and read out in Hz; the value itself
                   //       stays in turns per minute, as projects and programs have it)
    kShape,        // %     0 = smooth sine, 100 = lumpy, uneven lurch
    kFlutter,      // %     fast pitch wobble depth
    kFlutRate,     // Hz    fast wobble rate
    // noise
    kHum,          // %     60 Hz mains hum
    kBuzz,         // %     120 Hz buzz
    kHeadBuzz,     // %     59.94 Hz head-switching rasp
    kHiss,         // %     tape hiss
    kCrackle,      // %     random clicks
    kNoise,        // %     master noise level (50 % = nominal)
    kNoisePos,     // choice Post / Pre
    // wobble 2 (the re-recorded copy)
    kWow,          // %
    kWowRate,      // Hz
    kFlutter2,     // %
    kFlut2Rate,    // Hz
    kLoCut,        // Hz
    kHiCut2,       // Hz
    // chorus
    kChorusOn,     // bool
    kChoMix,       // %
    kChoSpeed,     // Hz
    kChoDelay,     // ms
    kChoWidth,     // %
    // mic
    kMicOn,        // bool
    kMicType,      // choice
    kMicGain,      // dB
    // output
    kMono,         // bool
    kDropouts,     // %
    // section switches (added in 1.1)
    kWobbleOn,     // bool  both wobble stages on / off
    kNoiseOn,      // bool  all noise on / off
    // Read-only "meters" for the screen. The plugin writes them, nobody should set them:
    kVisReel,      // 0..1  reel rotation, one tooth-to-tooth step per cycle, in kReelFrames steps
    kVisLevel,     // 0..1  output level, in kLevelFrames steps
    // deck (added in 1.2)
    kDeckOn,       // bool   the whole deck section on / off
    kAgc,          // %      automatic level control: ducks fast, holds, creeps back up over seconds
    kDrift,        // %      the sound slowly comes and goes (the track wandering off the head)
    kTracking,     // %      Hi-Fi track losing lock: static, then a moment on the dull mono track
    kBreathing,    // %      hiss follows the signal
    kMix,          // %      dry / wet, 100 = all effect
    kAnimate,      // bool   moving reels / grid / sun on the screen (added in 1.3). Not part of
                   //        the programs: choosing a program leaves it alone (see holdSwitches).
    // added in 1.4
    kScrape,       // %      scrape flutter: a fast roughness from the tape rubbing along its path
    kWhistle,      // %      the 15.7 kHz line whistle of the TV set leaking into the sound
    kHq,           // bool   the stages that distort run at twice the sample rate (no fold-back).
                   //        Like Animation, not part of the programs.
    // added in 1.5
    kNr,           // choice B-type noise reduction on the linear track: Off, Rec (recorded with it,
                   //        played without), Play (the opposite), On (both)
    kSatType,      // choice the saturator stage: Simple (the 2-band saturator, as before: crossover
                   //        at Split, lows and highs each with their own level and soft clip; shown
                   //        as 2-Band up to 1.6) or VHS (the linear track's tape;
                   //        Lo Sat = record level, Hi Sat = tape speed SP..EP, Split = bias), or
                   //        (1.7) Hi-Fi (the FM track, see HiFiPath; Lo Sat = record level, Hi Sat =
                   //        carrier level, Split = Cal)
    // added in 1.6
    kMicModel,     // choice which set of microphones Mic Type picks from: Simple (as up to 1.5)
                   //        or Modeled (built from published figures). On Modeled in a new
                   //        instance, on Simple in a state saved before it existed.
    kAgcModel,     // choice which AGC the AGC knob sets: Simple (as up to 1.5) or Modeled (an early
                   //        VHS deck's record level circuit, see ModeledAgc). Rests on Modeled;
                   //        Simple in a state saved before it existed and in every program
                   //        with the AGC up.
    // added in 1.7
    kVisualVibes,  // bool   the picture effects on the page sticker (see notifierMain): snow, the head
                   //        switch strip, dropout streaks and the tracking band, the grid bending with
                   //        the wobble, the Hi Cut haze, the sun in the Sat Type's colour. On in a new
                   //        instance and in an older state. Like Animation, not part of the programs;
                   //        the sound never looks at it.
    // Read-only meters for those effects, like Reel and Level. Each one is the frame of a film strip
    // in the skin; frame 0 shows nothing (Grid: the straight grid).
    kVisSnow,      // the snow: hiss level step x random variant
    kVisHead,      // the head switch strip along the bottom: Head Buzz step x random variant
    kVisStreak,    // a dropout streak (random variant) or the tracking band at its height
    kVisGrid,      // the grid: wobble depth step x the scroll frame Reel has
    kVisHaze,      // the Hi Cut haze step
    kNumParams
};

// The meters are not settings.
inline bool isSetting(int i) { return i != kVisReel && i != kVisLevel && i < kVisSnow; }
// Frames in the skin's film strips (tools/make_skin.py reads these lines: one plain number each).
constexpr int kReelFrames  = 24;           // the reel hubs, and the grid's scroll in each wobble step
constexpr int kLevelFrames = 16;           // the sun's level, in each of its colours
constexpr int kSunColours = 3;             // the sun's colours: Simple (and Visual Vibes off), VHS, Hi-Fi
constexpr int kSnowSteps = 3;              // snow: strength steps ...
constexpr int kSnowVariants = 4;           // ... times random variants, after frame 0
constexpr int kHeadSteps = 3;              // the head switch strip: the same
constexpr int kHeadVariants = 4;
constexpr int kStreakVariants = 6;         // Streak: the dropout streaks, then ...
constexpr int kBandSteps = 8;              // ... the tracking band at this many heights, bottom to top
constexpr int kWobbleSteps = 3;            // Grid: bent grids after the straight one
constexpr int kHazeSteps = 5;              // Haze: steps after frame 0
constexpr int kSnowFrames   = 1 + kSnowSteps * kSnowVariants;
constexpr int kHeadFrames   = 1 + kHeadSteps * kHeadVariants;
constexpr int kStreakFrames = 1 + kStreakVariants + kBandSteps;
constexpr int kGridFrames   = (1 + kWobbleSteps) * kReelFrames;
constexpr int kHazeFrames   = 1 + kHazeSteps;
constexpr float kReelStepsPerSec = 3.6f;   // 0.6 turns per second, six teeth per turn
// Peak pitch error of each wobble at 100 % (squared with the knob, see readParams): also how deep
// the grid bends.
constexpr float kWarpDev = 0.05f, kFlutterDev = 0.012f, kWowDev = 0.03f, kFlutter2Dev = 0.010f;

constexpr int   kCtrl      = 16;          // control-rate interval in samples (must be a power of two)
constexpr float kCtrlInv   = 1.0f / (float)kCtrl;
constexpr float kSmooth    = 0.05f;       // per-tick smoothing of control values (~7 ms)
constexpr int   kTapeDelaySamples   = 8192;   // per wobble stage (186 ms at 44.1 kHz)
constexpr int   kChorusDelaySamples = 2048;
constexpr float kMaxExcursionSec    = 0.045f; // largest wobble swing we allow, in seconds of delay
// VHS_VIBES_SWING_ROOM=3.0f and VHS_VIBES_NO_FADE_IN (in doReset) put back 1.2's resting delay
// and its lack of a fade-in after a reset, for builds that are compared with 1.2 (as 1.3 was,
// by a script outside the repo). They are never set in a release; tests/run.sh compares
// against 1.4, with neither set.
#ifndef VHS_VIBES_SWING_ROOM
#define VHS_VIBES_SWING_ROOM 2.2f
#endif
constexpr float kSwingRoom          = VHS_VIBES_SWING_ROOM;   // resting delay = this many times the slow wobble's own swing
constexpr float kGlideStep          = 0.25f;  // most a delay's resting length moves per control tick, in
                                              // samples: a knob move bends the pitch by 1.6 % at most
constexpr float kJumpMin            = 96.0f;  // a resting length further away than this many samples is
                                              // not glided to (the bend would last too long) but
                                              // crossfaded to, in 25 ms
constexpr float kBoostCeil          = 1.5f;   // the AGC and the compressor's make-up never push a sample
                                              // past this (the rail the soft clippers work to)
constexpr float kFbLevel            = 0.71f;  // the backup track plays 3 dB quieter than the Hi-Fi track
constexpr float kLineHz             = 15734.0f;   // NTSC line rate: the whistle of a TV set
// HQ: the half-band filter that doubles and halves the sample rate (see HalfBand), and how
// late the pair makes the sound, in samples
constexpr float kHb[3]              = { 0.1035037879f, 0.3717428742f, 0.7519142908f };
constexpr float kHqDelay            = 1.412f;
constexpr int   kFadeTicks          = 8;      // control ticks of fade-out before a reset while sound plays (3 ms)

// Which values a parameter feeds. A knob move only recomputes its own group in readParams().
enum Group : uint32_t
{
    gRamps = 1u << 0, gPreamp = 1u << 1, gWear = 1u << 2, gComp = 1u << 3, gSat = 1u << 4, gHiCut = 1u << 5,
    gWobble = 1u << 6, gCuts = 1u << 7, gNoise = 1u << 8, gChorus = 1u << 9, gMic = 1u << 10, gDeck = 1u << 11,
    gQuality = 1u << 12,   // the HQ switch: not a group to recompute, the whole plugin restarts
    gNr = 1u << 13,
    gAll = 0xffffffffu
};
inline uint32_t groupOf(int param)
{
    switch (param)
    {
        case kOutput: case kMono: case kMix: case kSatOn: case kChorusOn: case kChoMix: case kMicOn:
        case kDropouts: case kAnimate:                                      return gRamps;
        case kDrive: case kTone:                                            return gPreamp;
        case kWear:                                                         return gWear;
        case kComp:                                                         return gComp;
        case kLows: case kLoSat: case kSplit: case kHighs: case kHiSat:     return gSat;
        case kHiCut:                                                        return gHiCut;
        case kWarp: case kRpm: case kShape: case kFlutter: case kFlutRate:
        case kWow: case kWowRate: case kFlutter2: case kFlut2Rate: case kWobbleOn:
        case kScrape:                                                       return gWobble;
        case kLoCut: case kHiCut2:                                          return gCuts;
        case kHum: case kBuzz: case kHeadBuzz: case kHiss: case kCrackle:
        case kNoise: case kNoisePos: case kNoiseOn: case kWhistle:          return gNoise;
        case kChoSpeed: case kChoDelay: case kChoWidth:                     return gChorus;
        case kMicType: case kMicGain: case kMicModel:                       return gMic;
        case kDeckOn: case kAgc: case kDrift: case kTracking: case kBreathing:
        case kAgcModel:                                                     return gDeck;
        case kHq:                                                           return gQuality;
        case kNr:                                                           return gNr;
        case kSatType:                                                      return gQuality;   // fade out, restart
        default:                                                            return 0;   // the meters, and Visual Vibes (the picture only)
    }
}

// Parabolic sine for a phase in [0,1). Cheap; its slight harmonic content suits mains hum.
inline float psin(float p)
{
    const float x = 1.0f - 2.0f * p;
    return 4.0f * x * (1.0f - std::fabs(x));
}

// Sine for a phase in turns (any value >= 0): parabola plus one correction step,
// within 0.1 % of sin(). Used for every LFO so the control tick needs no libm calls.
inline float lfoSin(float turns)
{
    turns -= (float)(int)turns;
    const float y = psin(turns);
    return 0.225f * (y * std::fabs(y) - y) + y;
}

// Signals around this peak level keep their loudness when a clipper or the compressor
// is turned up; quieter material comes up, louder material is held down.
constexpr float kRefLevel = 0.4f;   // -8 dBFS

// ---------------------------------------------------------------------------
// Stereo: left and right in one two-float vector. +, -, * work on both channels at once
// (a plain float on one side applies to both). On the MPC these become NEON instructions,
// on a PC SSE; no hand-written intrinsics are needed and the same code builds everywhere.
// x[0] is left, x[1] is right.
// ---------------------------------------------------------------------------
typedef float Stereo __attribute__((vector_size(8)));

inline Stereo stereo(float l, float r) { const Stereo v = {l, r}; return v; }
inline Stereo splat(float x) { const Stereo v = {x, x}; return v; }
// min / max as compare-and-select: one or two instructions, where fmin() / fmax() would be
// library calls on the 32-bit MPCs (their NaN rules have no matching instruction there).
inline Stereo vmin(Stereo a, Stereo b) { return a < b ? a : b; }
inline Stereo vmax(Stereo a, Stereo b) { return a > b ? a : b; }
inline Stereo vabs(Stereo a) { return __builtin_elementwise_abs(a); }
inline Stereo vclamp(Stereo x, float limit) { return vmax(vmin(x, splat(limit)), splat(-limit)); }   // to -limit .. limit
inline float  maxf(float a, float b) { return a > b ? a : b; }
inline float  minf(float a, float b) { return a < b ? a : b; }

// The cubic soft clipper of dsp/math.h, for both channels.
inline Stereo softClip(Stereo x)
{
    x = vclamp(x, 1.5f);
    return x - (4.0f / 27.0f) * x * x * x;
}
// Soft clipper with a clean blend: mix 0 = untouched, 1 = softClip(x * g) * makeup.
// (softClip is a cubic: no divide, which matters on the 32-bit MPCs.)
inline Stereo driveSat(Stereo x, float g, float makeup, float mix)
{
    return x + (softClip(x * g) * makeup - x) * mix;
}

// ---------------------------------------------------------------------------
// NR: B-type noise reduction, the Dolby B that early stereo VHS decks had on their linear
// (edge) tracks. A model of the circuit, not of its effect: the Signetics NE545 application
// circuit (datasheet figure 25 and parts list), one processor per channel as in a deck.
//
//   main path ------------------------------------------------(+)---> encoded
//        |                                                      ^
//        +- C301 -+-- R302 || C303 --+--> amp C --+--> clipper -+        the side chain
//                 |                  |            |
//           R301 || C302             F            +--> amp D -> rectifier -> two-stage
//                                    ^                                       smoothing
//                                    +---------------------------------------------+
//
// The side chain is a high-pass filter. At low levels the variable resistance F is open
// and the filter is C301 / C302 / R301 alone (turnover 1.5 kHz): added to the main path it
// lifts the highs by 10 dB. As the side chain's own output grows, F conducts and the
// turnover of the second section (R302, C303, F) slides upwards, so loud highs are left
// alone. The decoder is the same side chain in a feedback loop, subtracting.
//
// From the datasheets: the network and its values, the 10 dB, the clipper that only works
// during a sudden jump in level, time constants "of the order of 100 ms" that shorten to
// "about 2 ms" for a sudden increase. Not given as numbers: the gain of amplifier C, the
// rise of amplifier D with frequency and the control law of F. Those were fitted (F's
// conductance rising in a straight line with the control voltage above a threshold, as a
// junction FET's does) to the record-mode figures of the NE545 and TEA0665 datasheets; the
// model meets all eight within 0.4 dB at 44.1 kHz and within 0.6 dB at every rate up to
// 192 kHz (the chips are specified to +/- 2 dB):
//      1.4 kHz at -20 dB: +4.4 dB, at -30: +7.5      2 kHz at -25 dB: +7.0
//      5 kHz at -20: +3.2, -30: +8.2, -40: +10.3     10 kHz at -20: +2.6, -30: +6.5
// The diode threshold of the smoothing and the clip level are estimates. Levels are relative
// to Dolby level, taken here as kRefLevel.
//
// The network has two memories (the charge on C301 + C302, and on C303) and is solved
// exactly at every sample with the trapezoid rule, F included, so nothing lags and the
// decoder's loop has no delay in it. (The trapezoid rule bends the frequency scale near
// half the sample rate: at 44.1 kHz the network follows the analogue one closely up to
// about 10 kHz.) It costs one reciprocal per sample, followed from the previous one with a
// Newton step while it moves by less than 0.3 % and divided otherwise; with that the
// decoder undoes the encoder to about -100 dB (the largest single sample: about -110 dB
// below full scale).
// ---------------------------------------------------------------------------
constexpr float kNrR301 = 3300.0f, kNrC301 = 5600.0e-12f, kNrC302 = 27.0e-9f;   // the fixed section
constexpr float kNrR302 = 47.0e3f, kNrC303 = 4700.0e-12f;                       // the sliding section
constexpr float kNrGmin   = 1.0f / 700.0e3f;   // F at rest: about 700 k (US patent 4,068,139)
constexpr float kNrGmax   = 1.0f / 70.0f;      // and fully on
constexpr float kNrGain   = 13.8745f;          // amplifier C (fitted: 10.3 dB at 5 kHz, low level)
constexpr float kNrDzero  = 2232.0f;           // amplifier D: gain 1, rising above this frequency (fitted)
constexpr float kNrSlope  = 0.005994f;         // F's conductance per unit of control voltage above...
constexpr float kNrThresh = 0.07719f;          // ...this threshold (both fitted; Dolby level = 1)
constexpr float kNrDiode  = 0.05f;             // a rise larger than this skips the slow smoothing (estimate)
constexpr float kNrClip   = 0.12f * kRefLevel; // overshoot suppressor: just above the largest steady side-chain signal

struct NoiseRed
{
    float kap = 0.0f, c1 = 1.0f, c2 = 1.0f, c2inv = 1.0f, off = 0.0f, gc1 = 0.0f, gc2 = 0.0f;
    float mMin = 1.0f, mK = 0.0f, dMin = 1.0f, dK = 0.0f, tMax = 0.0f;   // m and D as straight lines in the control voltage
    float kd = 0.0f, a1 = 0.0f, a1L = 0.0f, aSlow = 0.0f;
    Stereo sq = {}, su = {};                 // the two memories of the network
    Stereo m = {}, D = {}, rec = {};         // c2 + b1 * (F's conductance), the network's denominator, 1 / (D + off)
    Stereo ePrev = {}, v1 = {}, v2 = {};     // amplifier D's last input; the two smoothing stages

    void prepare(float sr, bool decoder)
    {
        const float k = 0.5f / sr, ct = kNrC301 + kNrC302;
        kap = kNrC301 / ct;
        c1  = 1.0f + k / (kNrR302 * kNrC303);
        c2  = 1.0f + k / (kNrR301 * ct);
        c2inv = 1.0f / c2;
        // F's conductance is g = gmin + slope * t (t: the control voltage above the threshold,
        // up to tMax). The two things that depend on it are straight lines in t as well.
        const float b1 = k / ct, d1 = k * (c2 / kNrC303 + c1 / ct);
        mMin = c2 + b1 * kNrGmin;        mK = b1 * kNrSlope;
        dMin = c1 * c2 + d1 * kNrGmin;   dK = d1 * kNrSlope;
        tMax = (kNrGmax - kNrGmin) / kNrSlope;
        gc1 = kNrGain * c1; gc2 = kNrGain * c2;
        off = decoder ? gc1 * kap : 0.0f;                // the decoder divides by D + this
        kd  = sr / (2.0f * kPi * kNrDzero);
        a1    = 1.0f - std::exp(-1.0f / (0.002f * sr));
        a1L   = a1 / kRefLevel;
        aSlow = 1.0f - std::exp(-1.0f / (0.100f * sr));
        reset();
    }
    void reset()
    {
        sq = su = ePrev = v1 = v2 = splat(0.0f);
        m = splat(mMin);
        D = splat(dMin);
        rec = splat(1.0f / (dMin + off));
    }
    // Starts the network where a steady input x would have left it (no start-up thump).
    void seed(Stereo x) { sq = -kap * x; }

    // The network's memories after this sample, from its input x and its output vB.
    void step(Stereo x, Stereo vB)
    {
        const Stereo u = (sq + kap * x - m * vB) * c2inv;                // the voltage on C303
        sq = 2.0f * (vB - kap * x + u) - sq;
        su = 2.0f * u - su;
    }
    // Side chain output -> F for the next sample.
    void control(Stereo e)
    {
        const Stereo d = e + kd * (e - ePrev);                           // amplifier D
        ePrev = e;
        v1 += a1L * vabs(d) - a1 * v1;                                   // rectifier, first stage (2 ms)
        const Stereo up = v1 - v2;
        v2 += aSlow * up + vmax(up - kNrDiode, splat(0.0f));            // second stage: 100 ms, or at once
        const Stereo t = vmin(vmax(v2 - kNrThresh, splat(0.0f)), splat(tMax));
        m = mMin + mK * t;
        D = dMin + dK * t;
        // 1 / (D + off): one Newton step from the last value while D moves by less than 0.3 %
        // (the error is the square of the change: a millionth), a real division otherwise
        const Stereo den = D + off, p = den * rec, far = vabs(p - 1.0f);
        if (maxf(far[0], far[1]) < 0.003f) rec = rec * (2.0f - p);
        else rec = 1.0f / den;
    }

    // mix: 1 = on; below that the added signal is faded (while NR is switched in or out)
    Stereo encode(Stereo x, float mix = 1.0f)
    {
        const Stereo vB = (c1 * (sq + kap * x) - c2 * su) * rec;         // rec = 1 / D
        step(x, vB);
        const Stereo e = kNrGain * vB;
        control(e);
        return x + vclamp(e, kNrClip) * mix;
    }
    Stereo decode(Stereo y)
    {
        // y = x + gain * vB(x), and vB is a straight line in x: solved for x (rec = 1 / (D + off))
        Stereo x = (y * D - (gc1 * sq - gc2 * su)) * rec;
        Stereo e = y - x;
        const Stereo over = vabs(e);
        if (over[0] > kNrClip || over[1] > kNrClip)                       // the clipper is working: rare
            for (int i = 0; i < 2; ++i)
                if (over[i] > kNrClip)
                {
                    x[i] = y[i] - (e[i] > 0.0f ? kNrClip : -kNrClip);
                    e[i] = kNrGain * (c1 * (sq[i] + kap * x[i]) - c2 * su[i]) / D[i];
                }
        step(x, e * (1.0f / kNrGain));
        control(e);
        return x;
    }
};

// A biquad filter (same maths as dsp/biquad.h) running both channels at once.
struct PairBiquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    Stereo z1 = {0.0f, 0.0f}, z2 = {0.0f, 0.0f};
    void setCoeffs(const BiquadCoeffs& c) { b0 = c.b0; b1 = c.b1; b2 = c.b2; a1 = c.a1; a2 = c.a2; }
    void reset() { z1 = splat(0.0f); z2 = splat(0.0f); }
    Stereo process(Stereo x)
    {
        const Stereo y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// The same biquad on one (mono) signal. PairBiquad would work everything out twice, once in
// each half; here the two halves carry the two halves of the state, z = (z1, z2), and of the
// coefficients, (b1, b2) and (a1, a2), so the state update is one vector expression:
//   y = b0 x + z1,   (z1, z2) = (b1, b2) x + (-a1, -a2) y + (z2, -0)
// That is PairBiquad's arithmetic, operation for operation and in the same order, so the result
// is the same to the bit: adding (-a) y is subtracting a y exactly, and adding -0 changes no
// number, not even the sign of a zero. y is worked out as a vector too (only its first half
// counts) because a compiler may fuse a scalar multiply-add where it does not fuse a vector
// one (the 32-bit ARM build does), which would round differently.
struct MonoBiquad
{
    Stereo b0 = {1.0f, 1.0f}, b = {0.0f, 0.0f}, na = {0.0f, 0.0f}, z = {0.0f, 0.0f};
    void setCoeffs(const PairBiquad& p) { b0 = splat(p.b0); b = stereo(p.b1, p.b2); na = stereo(-p.a1, -p.a2); }
    void reset() { z = splat(0.0f); }
    float process(float x)
    {
        const Stereo y = b0 * x + z;
        z = b * x + na * y[0] + __builtin_shufflevector(z, splat(-0.0f), 1, 2);
        return y[0];
    }
};

// One-pole lowpass coefficient for a cutoff in Hz (one exp: per block only).
inline float lpCoeff(float hz, float sr)
{
    const float a = 1.0f - std::exp(-kTwoPi * hz / sr);
    return clamp(a, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// VHS: what the linear (edge) track's tape does to the sound, in the saturator's place
// (Sat Type = VHS).
//
//   record level -> pre-emphasis -> tape curve -> de-emphasis -> band edge -> bias
//
// From the AES tutorial "Consumer Video Tape Duplication Techniques" (J. Brown), table
// "Standards and performance of selected audio recording formats", column VHS (SP):
// tape speed 33.35 mm/s, pre-emphasis 3180 and 120 microseconds, reference level
// 100 nWb/m, and 3 % third-harmonic distortion AT that reference level (the thin video
// coating; an audio cassette has 7 dB to spare, a studio machine 10). The band reaches
// about 10 kHz. From the text: too much bias sounds dull, too little bright and distorted.
// From deck specification sheets: EP runs at 11.12 mm/s and reaches 4 to 6 kHz.
//
// So in SP the lift before the curve is the format's own 120 microsecond pre-emphasis
// (6 dB per octave from 1.33 kHz), and the curve is set for 3 % at reference level
// (kRefLevel, the same level the NR takes as Dolby level) when the record level stands at
// 0 dB. Estimates: that the lift levels off 20 dB up; that EP behaves like SP at a third
// of every frequency (no figures found for EP's equalisation or headroom); the shape of the
// curve past the 3 % point (saturation 4 dB higher); the sizes of the bias effects.
//
// The lift and the cut after the curve are exact inverses, so a quiet sound passes
// unchanged at any record level; a loud one meets the curve with its highs lifted, and
// they give way first. There is no head bump: at these speeds it lies below 20 Hz.
// Driven hard without HQ the stage folds a good deal back into the band (it clips lifted
// highs); HQ, on in a new instance, is the remedy.
// ---------------------------------------------------------------------------
// The curve alone reaches 3 % a little above 0.636 of its scale; the cut after it lowers the
// third harmonic of a low tone further. Set for 3 % at the output (441 Hz, SP, level 0 dB).
constexpr float kTapeCeil = kRefLevel / 0.746f;
constexpr float kTapeLevelMin = -10.0f, kTapeLevelSpan = 18.0f;   // record level, dB: knob at 0 % and its travel

struct TapeSat
{
    float gRec = 1.0f, makeup = 1.0f, invS = 1.0f, s = 1.0f;
    float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f, invB0 = 1.0f;         // pre-emphasis (b0 + b1 z^-1) / (1 + a1 z^-1), and its inverse
    float aEdge = 1.0f, aBias = 1.0f, gBias = 1.0f;
    Stereo uP = {}, wP = {}, vP = {}, yP = {}, edge = {}, biasLp = {};

    // x (27 + x^2) / (27 + 9 x^2): a soft curve that is level at +/-3 and reaches 1 there
    static Stereo curve(Stereo t)
    {
        t = vclamp(t, 3.0f);
        const Stereo t2 = t * t;
        return t * (27.0f + t2) / (27.0f + 9.0f * t2);
    }
    // One-pole lowpass (y += a (x - y)) coefficient that is exactly 3 dB down at hz.
    static float edgeCoeff(float hz, float sr)
    {
        const float c = 2.0f - std::cos(kTwoPi * minf(hz, 0.45f * sr) / sr);
        return 1.0f - (c - std::sqrt(c * c - 1.0f));
    }
    // level 0..1: record level, -10 to +8 dB. speed 0..1: SP to EP. bias -1..+1: under, right, over.
    void set(float sr, float level, float speed, float bias)
    {
        // 120 microseconds in SP; three times that in EP; too much bias costs the highs more headroom
        const float fz = 1326.3f * std::pow(3.0f, -speed) * (bias > 0.0f ? 1.0f - 0.3f * bias : 1.0f);
        const float kz = std::tan(kPi * fz / sr), kp = std::tan(kPi * minf(10.0f * fz, 0.45f * sr) / sr);
        const float norm = 1.0f / (1.0f + 1.0f / kp);
        b0 = (1.0f + 1.0f / kz) * norm;
        b1 = (1.0f - 1.0f / kz) * norm;
        a1 = (1.0f - 1.0f / kp) * norm;
        invB0 = 1.0f / b0;
        s = kTapeCeil * dbToGain(bias < 0.0f ? 6.0f * bias : 0.0f);      // too little bias: the tape gives way sooner
        invS = 1.0f / s;
        gRec = dbToGain(kTapeLevelMin + kTapeLevelSpan * level);
        makeup = 1.0f / gRec;                                            // played back as much quieter as it was recorded louder
        aEdge = edgeCoeff(10000.0f * std::pow(0.45f, speed), sr);
        aBias = lpCoeff(2.4f * fz, sr);
        gBias = dbToGain(bias < 0.0f ? -3.0f * bias : -6.0f * bias);     // too little: bright; too much: dull
    }
    void reset() { uP = wP = vP = yP = edge = biasLp = splat(0.0f); }
    Stereo process(Stereo x)
    {
        const Stereo u = x * gRec;
        const Stereo w = b0 * u + b1 * uP - a1 * wP;            // pre-emphasis
        uP = u; wP = w;
        const Stereo v = curve(w * invS) * s;
        const Stereo y = (v + a1 * vP - b1 * yP) * invB0;       // de-emphasis: the exact inverse
        vP = v; yP = y;
        edge += aEdge * (y * makeup - edge);
        biasLp += aBias * (edge - biasLp);
        return biasLp + (edge - biasLp) * gBias;
    }
};

// A value that moves in a straight line to its target over one control interval.
struct Ramp
{
    float v = 0.0f, inc = 0.0f;
    void  to(float target) { inc = (target - v) * kCtrlInv; }
    void  snap(float value) { v = value; inc = 0.0f; }
    float next() { v += inc; return v; }
};

// 4-point Hermite interpolation between x0 and x1 (xm before, x2 after), f = 0..1. Cleaner
// highs than linear when the delay time is always moving. T is a float or a Stereo.
template <typename T>
inline T hermite(T xm, T x0, T x1, T x2, float f)
{
    const T c1 = 0.5f * (x1 - xm);
    const T c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const T c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

// Stereo delay line. Left and right are stored side by side, so a pair that shares one delay
// time (the wobble stages) gets each of the four samples the interpolation needs with one
// two-float load. Allocates in prepare() only.
class PairDelay
{
public:
    // size is rounded up to a power of two; delays up to size - 4 can be read (the
    // interpolation looks three samples further back).
    void prepare(int size)
    {
        while (size & (size - 1)) size += size & -size;
        buf_.assign((size_t)size * 2, 0.0f);
        mask_ = size - 1;
        w_ = 0;
    }
    void reset()
    {
        for (float& s : buf_) s = 0.0f;
        w_ = 0;
    }
    void write(Stereo x)
    {
        std::memcpy(&buf_[(size_t)w_ * 2], &x, sizeof(x));
        w_ = (w_ + 1) & mask_;
    }
    // Call after write(). d is the delay in samples, d >= 1.
    Stereo read(float d) const
    {
        const int di = (int)d;
        const int i0 = (w_ - 1 - di) & mask_;
        return hermite(at((i0 + 1) & mask_), at(i0), at((i0 - 1) & mask_), at((i0 - 2) & mask_), d - (float)di);
    }
    // One channel only (0 = left), for a pair whose sides have their own delay times (the chorus).
    float read(float d, int ch) const
    {
        const int di = (int)d;
        const int i0 = (w_ - 1 - di) & mask_;
        const float* b = buf_.data() + ch;
        return hermite(b[(size_t)((i0 + 1) & mask_) * 2], b[(size_t)i0 * 2], b[(size_t)((i0 - 1) & mask_) * 2],
                       b[(size_t)((i0 - 2) & mask_) * 2], d - (float)di);
    }
    // A whole number of samples: one load, no interpolation.
    Stereo tap(int d) const { return at((w_ - 1 - d) & mask_); }
private:
    Stereo at(int i) const { Stereo v; std::memcpy(&v, &buf_[(size_t)i * 2], sizeof(v)); return v; }
    std::vector<float> buf_;
    int mask_ = 0, w_ = 0;
};

// Half-band lowpass for 2x oversampling: two chains of first-order allpass sections that add
// up to a lowpass at a quarter of the doubled rate (the elliptic design of Valenzuela and
// Constantinides; at 88.2 kHz it is 0.1 dB down at 19.8 kHz and 62 dB down from 28.2 kHz).
// One of these doubles the rate, a second one halves it again; the two together are a pure
// allpass: flat at every frequency, kHqDelay samples late in the lows and up to 2 in the highs.
struct HalfBand
{
    Stereo s[3][2] = {};   // per section: its last input and last output
    void reset() { for (auto& v : s) v[0] = v[1] = splat(0.0f); }
    Stereo ap(int i, Stereo x) { const Stereo y = kHb[i] * (x - s[i][1]) + s[i][0]; s[i][0] = x; s[i][1] = y; return y; }
    // one sample in, two out (a is the earlier one)
    void up(Stereo x, Stereo& a, Stereo& b) { a = ap(2, ap(0, x)); b = ap(1, x); }
    // two samples in (a the earlier one), one out
    Stereo down(Stereo a, Stereo b) { return 0.5f * (ap(2, ap(0, b)) + ap(1, a)); }
    // What up() followed by down() does to a sound, without the doubling in between: for a
    // copy that has to stay lined up with the doubled path at every frequency (the dry signal).
    Stereo trip(Stereo x) { return ap(1, ap(2, ap(0, x))); }
};

// The tape transport: turns "how wrong is the tape speed right now" into a delay time.
// Speed error = slow wobble (a shaped sine plus slow random drift) + fast wobble (a sine
// plus random jitter). The delay is the leaky integral of that error, so the pitch
// deviation you hear is the error itself, whatever the rates are.
struct TapeSpeed
{
    Xorshift32 rng;
    float ph1 = 0.0f, ph2 = 0.0f;
    float n1a = 0.0f, n1b = 0.0f, n2a = 0.0f, n2b = 0.0f;
    float x = 0.0f;           // integrated speed error, in samples
    float base = 6.0f;        // resting delay, large enough for the swing
    float delay = 6.0f;       // delay used right now (advanced every sample by inc)
    float inc = 0.0f;
    float err = 0.0f;         // current relative speed error (for the reel animation)
    float baseTarget = 6.0f;  // the resting delay the current settings need
    float jump = 0.0f;        // not 0 while the reader crossfades to a resting delay this far away
    float xf = 0.0f, xfInc = 0.0f;   // how far that crossfade is (0..1), and its step per sample
    float seenTarget = 6.0f;  // baseTarget at the last tick
    int   calm = 0;           // ticks since baseTarget last changed (a knob at rest)
    int   calmTicks = 276;    // ... and how many of them make a tenth of a second
    int   filled = 0;         // ticks of sound written to the delay line since the last reset
    bool  still = false;      // no wobble and settled: the delay is a whole number of samples
                              // and is not moving, so the reader needs no interpolation

    // Values that only change when a knob moves; update() refreshes them.
    float inDev1 = -1.0f, inRate1 = -1.0f, inDev2 = -1.0f, inRate2 = -1.0f, inSr = 0.0f;
    float dev1 = 0.0f, dev2 = 0.0f, ph1Inc = 0.0f, ph2Inc = 0.0f;
    float k1 = 0.001f, k2 = 0.001f, n1Scale = 0.0f, n2Scale = 0.0f, leak = 0.0f;

    // d1 / d2 = peak relative speed error of the slow / fast wobble (0.01 = 1 %), rates in Hz.
    // Cheap when nothing changed (five compares); otherwise it does the divides and square roots
    // that tick() used to do every time.
    void update(float sr, float d1, float rate1, float d2, float rate2)
    {
        if (d1 == inDev1 && rate1 == inRate1 && d2 == inDev2 && rate2 == inRate2 && sr == inSr) return;
        inDev1 = d1; inRate1 = rate1; inDev2 = d2; inRate2 = rate2; inSr = sr;
        const float dt = (float)kCtrl / sr;
        xfInc = 1.0f / (0.025f * sr);
        calmTicks = (int)(0.1f * sr * kCtrlInv);
        // Keep the swing inside the delay line: very slow rates get less depth.
        dev1 = minf(d1, kMaxExcursionSec * kTwoPi * rate1 * (1.0f / kSwingRoom));
        dev2 = d2;
        ph1Inc = rate1 * dt;
        ph2Inc = rate2 * dt;
        k1 = clamp(kTwoPi * rate1 * dt, 0.0005f, 0.5f);
        k2 = clamp(kTwoPi * rate2 * 1.5f * dt, 0.0005f, 0.6f);
        n1Scale = std::sqrt(12.0f / k1);   // brings the smoothed noise to roughly unit RMS
        n2Scale = std::sqrt(12.0f / k2);
        leak = kTwoPi * clamp(0.25f * rate1, 0.02f, 0.5f) * dt;
        const float swing = (dev1 * kSwingRoom / (kTwoPi * rate1) + dev2 * 1.6f / (kTwoPi * rate2)) * sr;
        baseTarget = clamp(swing + 6.0f, 6.0f, (float)kTapeDelaySamples * 0.45f);
    }

    // Start at rest for the current settings (call update() first).
    void reset(uint32_t seed)
    {
        rng.reseed(seed);
        ph1 = ph2 = 0.0f;
        n1a = n1b = n2a = n2b = 0.0f;
        x = 0.0f; base = baseTarget; delay = baseTarget; inc = 0.0f; err = 0.0f;
        jump = 0.0f; xf = 0.0f;
        seenTarget = baseTarget; calm = 0; filled = 0;
        still = false;
    }

    // The resting delay follows what the settings need. A moving delay is a pitch bend, so the
    // target is glided to at a limited rate. To a far one that would take seconds, so there
    // the reader crossfades to the new place instead (blend() below): one crossfade, once the
    // knob has rested for a tenth of a second (a knob still turning is glided after), the swing
    // fits the new resting delay, and the line holds enough sound since the last reset to be
    // read that far back.
    void followBase()
    {
        if (filled < (1 << 20)) ++filled;
        calm = baseTarget != seenTarget ? 0 : calm < (1 << 20) ? calm + 1 : calm;
        seenTarget = baseTarget;
        if (jump != 0.0f) return;   // a crossfade is under way
        const float d = baseTarget - base;
        if (std::fabs(d) < 0.01f) base = baseTarget;
        else if (std::fabs(d) > kJumpMin && calm >= calmTicks && x >= -(baseTarget - 4.0f)
                 && (float)(filled * kCtrl) > baseTarget + x + 8.0f) { jump = d; xf = 0.0f; }
        else base += clamp(0.003f * d, -kGlideStep, kGlideStep);
    }

    // Keeps the swing inside the delay line: at the resting delay there is and, while a
    // crossfade runs, at the one it is going to as well.
    float fit(float v) const
    {
        const float lo = minf(base, base + jump), hi = maxf(base, base + jump);
        return clamp(v, -(lo - 3.0f), (float)kTapeDelaySamples - 8.0f - hi);
    }

    // One sample of the crossfade: a was read at the old place, b at the new one. When it is
    // over, the new place is the resting delay.
    Stereo blend(Stereo a, Stereo b)
    {
        const Stereo y = a + (b - a) * xf;
        xf += xfInc;
        if (xf >= 1.0f) { delay += jump; base += jump; jump = 0.0f; xf = 0.0f; }
        return y;
    }

    // Called every kCtrl samples.
    void tick(float shape)
    {
        if (dev1 <= 0.0f && dev2 <= 0.0f)
        {
            // No wobble asked for. Let what is left die away, then sit on a whole sample so
            // the reader can skip the interpolation.
            err = 0.0f;
            if (still) return;
            x -= clamp(0.0015f * x, -kGlideStep, kGlideStep);
            if (std::fabs(x) < 0.01f) x = 0.0f;
            followBase();
            x = fit(x);
            const float target = base + x;
            if (x == 0.0f && base == baseTarget && std::fabs(target - delay) < 0.01f)
            {
                delay = target; inc = 0.0f; still = true;   // baseTarget is 6 here: a whole sample
            }
            else inc = (target - delay) * kCtrlInv;
            return;
        }
        still = false;

        ph1 += ph1Inc; if (ph1 >= 1.0f) ph1 -= 1.0f;
        ph2 += ph2Inc; if (ph2 >= 1.0f) ph2 -= 1.0f;

        float slow = lfoSin(ph1);
        if (shape > 0.0f) slow += shape * (0.5f * lfoSin(2.0f * ph1 + 0.11f) + 0.25f * lfoSin(3.0f * ph1 + 0.30f));

        // Two-pole smoothed noise, scaled to roughly unit RMS.
        n1a += k1 * (rng.nextBipolar() - n1a);
        n1b += k1 * (n1a - n1b);
        const float drift = clamp(n1b * n1Scale, -2.5f, 2.5f);

        n2a += k2 * (rng.nextBipolar() - n2a);
        n2b += k2 * (n2a - n2b);
        const float jitter = clamp(n2b * n2Scale, -2.5f, 2.5f);

        const float fast = 0.75f * lfoSin(ph2) + 0.5f * jitter;
        err = dev1 * (slow + 0.25f * drift) + dev2 * fast;
        // The swing needs its resting delay. While that is still growing towards a longer
        // one, the wobble gets only the share there is room for, so it never hits the end stop.
        if (base < baseTarget) err *= (base - 6.0f) / (baseTarget - 6.0f);

        // Leaky integrator: without the leak the random part would wander off.
        x += err * (float)kCtrl - leak * x;
        followBase();
        x = fit(x);
        inc = (base + x - delay) * kCtrlInv;
    }
};

// Reads a wobble stage's delay line at the transport's place (`extra` samples further back),
// crossfading to the new resting delay while the transport is on its way to one.
inline Stereo wobbleRead(const PairDelay& line, TapeSpeed& w, float extra)
{
    w.delay += w.inc;
    const Stereo y = line.read(w.delay + extra);
    return w.jump == 0.0f ? y : w.blend(y, line.read(w.delay + w.jump + extra));
}

// Control values that are smoothed once per control tick and then held for 16 samples.
enum CtlIndex
{
    cTiltLo, cTiltHi, cDrvG, cDrvMk, cDrvMix,
    cSlew, cThresh, cMakeup,
    cLowsG, cLoG, cLoMk, cLoMix,
    cHighsG, cHiG, cHiMk, cHiMix,
    cHum, cBuzz, cHead, cHiss, cCrackle,
    cMicGain, cMicNoise,
    cWobbleOn,
    cAgc, cDrift, cBreath,
    cScrape, cWhistle,
    kNumCtl
};

// Values that are additionally ramped every sample (anything that would click).
enum RampIndex { rOut, rMono, rChorus, rMic, rSat, rMix, kNumRamp };

// ---------------------------------------------------------------------------
// MIC, Mic Model = Simple: the four microphones as they were up to 1.5, one curve each (a
// highpass, a presence peak, a lowpass), in stereo, without noise, and Mic Gain a level after
// them. Kept exactly as they were, so a project made with them sounds as it did.
// ---------------------------------------------------------------------------
struct MicCurve { float hpHz, peakHz, peakQ, peakDb, lpHz; };
constexpr MicCurve kMicCurves[4] = {
    { 180.0f, 3200.0f, 1.0f, 5.0f,  7500.0f },   // Camcorder
    {  90.0f, 5000.0f, 0.9f, 4.0f, 12000.0f },   // Dynamic
    { 350.0f, 1800.0f, 1.2f, 6.0f,  3400.0f },   // Phone
    { 120.0f,  900.0f, 0.8f, 4.0f,  6000.0f },   // Boombox
};
struct SimpleMic
{
    PairBiquad hp, pk, lp;
    void set(int type, float sr)
    {
        const MicCurve& mc = kMicCurves[type];
        hp.setCoeffs(BiquadCoeffs::highpass(sr, mc.hpHz, 0.7071f));
        pk.setCoeffs(BiquadCoeffs::peak(sr, mc.peakHz, mc.peakQ, mc.peakDb));
        lp.setCoeffs(BiquadCoeffs::lowpass(sr, mc.lpHz, 0.7071f));
    }
    void reset() { hp.reset(); pk.reset(); lp.reset(); }
    // out of line, like MicModel::process (see there)
    __attribute__((noinline)) Stereo process(Stereo x) { return lp.process(pk.process(hp.process(x))); }
};

// ---------------------------------------------------------------------------
// MIC, Mic Model = Modeled: four period microphones (Mic Type), each built from published
// figures. The research notes behind them ("the report": VHS Vibes mic research, 2026-11-15)
// give every source with its link; the section numbers below are the report's. A value that no
// source gave is marked "estimate", with the reason.
//
//   Camcorder  a camera across the room (kCamDistanceDb) and its record chain: the electret
//              capsule (HF rise -> gate highpass -> + self-noise -> JFET overload) -> the mic
//              amp's band and overload -> the record ALC (42 dB of gain, then 1 : 19 over a
//              22 dB range) -> the cubic after it. Mono: one built-in mic.
//   Dynamic    four filters fitted to the Shure Unidyne B (515) curve at 0.6 m
//   Phone      carbon transmitter: sensitivity rising with level -> 2nd harmonic -> the CCITT
//              P.48 IRS sending curve (eight filters) -> + the line's idle noise. Mono: one line.
//   Boombox    the same capsule -> the record ALC -> + cassette hiss -> the cassette band.
//              Mono: the radio cassette's built-in mic is ("BUILT-IN MIC (MONO)", Sharp QT-27).
//
// Level. Overload and self-noise are sound pressures, so the stage needs a sound pressure for
// each digital level. One mapping for all four, an assumption (nothing can fix it for a
// plugin): a sine peaking at kRefLevel (-8 dBFS), the level the rest of the plugin treats as
// the recorder's reference, is 104 dB SPL, so 94 dB SPL (1 Pa) peaks at -18 dBFS. That is the
// report's suggested convention (0.1). With it the electret's documented overload (110 dB SPL)
// lies 6 dB above reference: sound played in at reference passes the capsule cleanly and Mic
// Gain +6 dB reaches overload; a loud talker at 30 cm (85 to 90 dB SPL) sits 14 to 19 dB below
// reference. Mic Gain scales the sound before the mic (how loud the source was, how close).
//
// Every type is set to 0 dB at 1 kHz, where microphones are specified. Its noise is added only
// while the Mic is on, at the model's own level: the Noise switch silences it, the Noise knob
// does not scale it. Only the selected type's filters run. Everything that follows the level
// (the carbon's sensitivity, the ALC's detector) is worked out at every sample.
// ---------------------------------------------------------------------------
constexpr float kPaPeak = kRefLevel * 0.316228f;   // the peak of a 1 Pa (94 dB SPL) sine: 10 dB under kRefLevel
constexpr float kPaRms  = kPaPeak * 0.707107f;     // and its rms

// Electret capsule (Camcorder, Boombox), report 0.2.
// Gate highpass: J. Kean (Radio World), capsule + JFET gate resistance form a first-order
// highpass; his example R = 100 Mohm, C = 30 pF: 53 Hz.
constexpr float kGateHpHz = 1.0f / (kTwoPi * 100.0e6f * 30.0e-12f);
// HF rise: ten Panasonic WM-61A capsules measured by J. Neal (read off the plot, report 0.2):
// flat to 5 kHz, +1 dB at 8 kHz, +1.5 to +3 dB at 13 to 17 kHz, back to about 0 at 21.5 kHz.
// One peak filter, fitted for each rate band (tools/mic_fit.py) to 0 dB from 300 Hz to 5 kHz,
// +1 dB at 8 kHz, the middle of the spread (+2.25 dB) at 13, 15 and 17 kHz and 0 at 21.5 kHz.
// At every rate: within 0.3 dB to 5 kHz, +0.7 dB at 8 kHz, +1.8 to +2.5 dB at 13 to 17 kHz,
// under +1 dB at 21.5 kHz. No lowpass in the capsule: the band is the recorder's business.
constexpr float kCapPeak[4][3] = {                                  // Hz, Q, dB
    { 15307.6f, 0.590f, 2.50f }, { 15035.1f, 0.711f, 2.49f },      // below 46 kHz; 46-70 kHz
    { 14106.2f, 1.200f, 2.47f }, { 13867.4f, 1.352f, 2.47f },      // 70-130 kHz; 130 kHz up
};
// Overload: Kean, many electrets "begin overloading above 110 dB SPL", probably in the JFET,
// and the clipping is asymmetric: negative-going peaks clip first, positive-going later. Taken
// as 1 % THD at 110 dB SPL (the usual way a mic's maximum level is given; Kean quotes the
// DIN 45500 criterion of 1 %): the negative half is the plugin's cubic soft clip at a scale
// that gives a 110 dB SPL sine 1 % (solved numerically: 1.935 times its peak,
// tools/mic_fit.py). How much later the positive half gives way is not documented: 6 dB later
// is an estimate.
constexpr float kSpl110Peak = kPaPeak * 6.3096f;            // 110 dB SPL = 94 + 16 dB
constexpr float kJfetNeg = 1.935f * kSpl110Peak;
constexpr float kJfetPos = kJfetNeg * 1.9953f;              // + 6 dB (estimate)
// Self-noise: WM-61A "26 +/- 1 dB SPL(A)" (report 0.2, seen in a quoted figure; the datasheet's
// S/N > 62 dB means at most 32 dB(A), and a period capsule mic measured 24 dB(A) in Audio,
// July 1983). 26 dB SPL(A), white, A-weighted between 20 Hz and 20 kHz.
constexpr float kCapNoiseRms = kPaRms * 3.9905e-4f;         // 20 uPa * 10^(26/20) = 3.99e-4 Pa
constexpr float kAWeightBw   = 12468.0f;                    // A-weighted bandwidth of white noise, 20 Hz-20 kHz (computed, tools/mic_fit.py)

// Phone, report 3. The CCITT P.48 (1988) intermediate reference system, sending side: "the
// mean characteristics of many commercial circuits" (handset and local line), Table 1/P.48,
// 100 Hz to 8 kHz. The report's eight-filter fit used 22 of the table's 24 points: it stopped
// at 5 kHz and left 6.3 and 8 kHz 31 and 39 dB above the table. The same kind of fit (RBJ
// filters at 48 kHz, Nelder-Mead; points below -30 dB need only be at least that deep) on all
// 24, with the report's 11.9 kHz peak turned into a lowpass (tools/mic_fit.py): within 0.2 dB
// from 100 Hz to 3.5 kHz at every rate from 44.1 to 192 kHz, 0.3 dB at 4 kHz (-33.5 dB),
// 0.6 dB at 5 kHz (-48.5 dB), and as deep as the table or deeper at 6.3 and 8 kHz. Its gain of +17.3 dB at 1 kHz is taken off
// again (the 1 kHz normalisation below).
struct MicSec { char kind; float hz, q, db; };   // 'h' highpass ('H': worked out in double), 'l' lowpass, 'p' peak, 'n' notch, '1' first-order highpass, 'L' first-order lowpass
constexpr MicSec kPhoneSecs[8] = {
    { 'h',  325.3f, 1.026f,  0.0f  }, { 'h',  169.6f, 1.433f,  0.0f },   // the low cut
    { 'p', 1045.7f, 0.253f, 13.34f },                                    // the broad rise to 2.5-3 kHz
    { 'l', 1861.7f, 1.308f,  0.0f  }, { 'l', 2773.4f, 3.496f,  0.0f },
    { 'l', 3330.4f, 13.709f, 0.0f  },                                    // high Q: holds +5.5 dB at 3.15 kHz before the cliff
    { 'n', 4135.1f, 4.234f,  0.0f  }, { 'n', 6971.6f, 0.534f,  0.0f },   // the cliff, and the depth above it
};
// The carbon transmitter (report 3). Documented: second harmonic is the main distortion
// product ("double-frequency" distortion), and the sensitivity rises with sound pressure
// (favouring the near talker). No amounts were found for either, so both are estimates, kept
// modest: 3 % second harmonic at reference level (x + a x^2), and below reference a
// sensitivity that falls by a quarter of a dB per dB (1 : 1.25), at most 10 dB, following the
// level within 1 ms up and 50 ms down.
constexpr float kCarbonH2    = 2.0f * 0.03f / kRefLevel;   // a: 3 % second harmonic at kRefLevel (estimate)
constexpr float kCarbonClip  = 2.0f;                        // input held inside +/-2, where 1 + 2 a x stays positive
constexpr float kCarbonFloor = 0.316228f;                   // the sensitivity falls by at most 10 dB (estimate)
constexpr float kCarbonQuiet = 0.99f * kCarbonFloor * kCarbonFloor * kCarbonFloor * kCarbonFloor;   // below: at the floor (fourth root 0.3155)
// The line's idle noise: CCITT G.712, PCM channel idle noise at most -65 dBm0p (report 3).
// That limit is used as the level. dBm0 to plugin level: 1 Pa at the mouth gives -3.7 dB V/Pa
// at the IRS junction (Table 1/P.48, 1 kHz) = -1.48 dBm in 600 ohm, the junction taken as a
// 0 dBr point (an assumption); the 1 kHz tone of 1 Pa comes out of this model at kPaRms. Mono
// (one line), white noise through a bandpass with its -3 dB points at 300 and 3400 Hz (the
// channel's nominal band, P.48; G.712's mask itself was not in the text), set to -65 dBm0 in
// total; the psophometric weighting is not applied.
constexpr float kLineNoiseRms = kPaRms * 6.668e-4f;        // 10^((-65 + 1.48) / 20)
constexpr float kLineBpHz = 1010.0f, kLineBpQ = 0.3258f;    // sqrt(300 * 3400), f0 / (3400 - 300)

// Dynamic, report 2. Shure 515BSM user guide (2005, the Unidyne B family of the period 515SA /
// 515SB), figure 1 at 0.6 m, read off the plot: 50 Hz -16, 100 -8, 200 -4, 500 -1.5, 1 kHz 0,
// 2 kHz +1, 4 kHz +4, 5 kHz +5, 6.5 kHz +2, 8 kHz +3, 10 kHz 0, 15 kHz -10 dB. Highpass,
// presence peak, the bump above the 6.5 kHz dip, lowpass: the report's fit at 48 kHz. Its
// lowpass leans on the squeeze of the frequency scale near half the sample rate, so at other
// rates the same filters are refitted (tools/mic_fit.py) to the report's 48 kHz curve between
// the points and to the points themselves: for 44.1 kHz, for 88.2 and 96 kHz, and for 192 kHz
// (the last two with a second lowpass, as the curve falls faster above 10 kHz than one can
// follow without that squeeze). Within 0.82 dB of the read points at every rate, worst at
// 500 Hz, which a highpass of this shape cannot meet along with 100 and 200 Hz.
// No proximity lift: the plot gives it only at 1 cm against 0.6 m, and no distance between is
// documented for this use. No distortion and no noise: a moving coil does not overload at these
// levels, and its own noise is far below the recorder's (report 2).
constexpr MicSec kDynSecs[4][5] = {
    { { 'h', 101.86f, 0.370f, 0.0f }, { 'p', 4597.4f, 2.145f, 5.29f }, { 'p', 8172.8f, 5.749f, 2.25f }, { 'l', 10932.9f, 0.823f, 0.0f }, { 0, 0, 0, 0 } },          // below 46 kHz
    { { 'h', 102.16f, 0.370f, 0.0f }, { 'p', 4578.7f, 2.34f,  5.17f }, { 'p', 8171.8f, 8.0f,   2.01f }, { 'l', 10325.8f, 0.9f,   0.0f }, { 0, 0, 0, 0 } },          // 46-70 kHz: the report's
    { { 'h', 101.88f, 0.370f, 0.0f }, { 'p', 4579.5f, 2.422f, 5.17f }, { 'p', 8170.3f, 8.937f, 2.08f }, { 'l', 11448.6f, 1.122f, 0.0f }, { 'l', 11907.9f, 0.640f, 0.0f } },   // 70-130 kHz
    { { 'h', 101.90f, 0.371f, 0.0f }, { 'p', 4578.1f, 2.550f, 5.04f }, { 'p', 8174.7f, 8.928f, 2.15f }, { 'l', 11879.7f, 1.469f, 0.0f }, { 'l', 8810.5f,  0.688f, 0.0f } },   // 130 kHz up
};

// Boombox, report 4. The capsule above, then the record preamp's ALC, then the cassette.
// ALC (Sanyo LA3220, "2-channel equalizer amplifier with ALC" for radio cassettes): ALC width
// 45 dB typical, "from VI = -60 dBm until VO goes up 3 dB" (45 dB of input give 3 dB of
// output); its control voltage from a voltage doubler, a peak detector on the output. So: a
// loop that attenuates by 14 dB for every dB its detector is above the threshold (45 dB in,
// 3 dB out), at most 42 dB, beyond which the output follows the input again.
// Where the range starts: -60 dBm (0.775 mV) from a -35 dB V/Pa capsule (WM-61A, report 0.2) is
// 66.8 dB SPL, -45.2 dBFS by the mapping above (a derived value). The ALC starts when the
// output reaches kRefLevel (the tape's reference; ROHM BA3308: ALC starts when the output peak
// passes a threshold), so the record gain before it is kRefLevel over that input: +37.2 dB.
constexpr float kAlcOnsetPeak = kPaPeak * (0.7746e-3f / 0.017783f);   // -60 dBm / (-35 dB V/Pa) = 0.0436 Pa
constexpr float kAlcRecGain   = kRefLevel / kAlcOnsetPeak;
constexpr float kAlcMaxAtt    = 0.0079433f;                 // 42 dB: the 45 dB range less its 3 dB
// Distortion under ALC: LA3220 0.5 % typical (2 % max) at VI = -20 dBm, ROHM BA3308 3 % at the
// end of its range. A cubic y - c y^3 after the gain element, set for 0.5 % at the output
// level of VI = -20 dBm (40 dB into the range, 2.7 dB over reference): 0.27 % at the start of
// the range, 0.54 % at its end (tools/mic_fit.py).
constexpr float kAlcCubic = 0.06665f;
constexpr float kAlcRail  = 2.2364f;                         // 1 / sqrt(3 c): the cubic's turning point (output 1.49)
// Recovery: ROHM BA3308 "attack time and recovery time of ALC are set according to C7 and R7",
// application values C7 = 22 uF, R7 = 1 Mohm: the detector discharges with 22 s. Because the
// loop needs only 3 dB of detector voltage for its whole 42 dB, that is 0.35 x 22 = 7.6 s from
// full attenuation back to none (5.5 dB a second). Attack: the charging current is not given
// (a 4.7 k / 22 uF figure for the BA3306 was seen only in a search summary). Estimate: 5 ms,
// fast, as the report proposes (a few ms for the loop to catch a loud onset).
constexpr float kAlcRecoverySec = 22.0e-6f * 1.0e6f;
constexpr float kAlcAttackSec   = 0.005f;                    // estimate
// The cassette: Sharp QT-27 radio cassette service manual, tape frequency response 50 Hz to
// 10 kHz, S/N 45 dB. Taken as the -3 dB points of a second-order highpass and lowpass (the
// tolerance is not given), and the hiss as 45 dB below the level the ALC records at (a
// kRefLevel sine), measured in that band.
constexpr float kTapeHissRms = kRefLevel * 0.707107f * 5.6234e-3f;   // -45 dB

// Camcorder, second report ("the camcorder report": VHS Vibes camcorder mic research,
// 2026-11-15, section 3, its recommended model). The built-in mic of a 1980s camcorder was a
// decent electret; what made it sound like a camcorder was the record chain behind it: a mic
// amp, then an ALC with about 40 dB of fixed gain in front of a short range. The topology is
// the JVC GR-C1's (1984, the first VHS-C camera; its service manual's audio block diagram: mic
// amp -> voltage-controlled amplifier -> line amp, an ALC detector on the output feeding back
// to the VCA). So: the capsule above (unchanged) -> the mic amp's band -> the mic amp's
// overload -> the ALC (gain element, detector on its output) -> the overload after it. Mono:
// one built-in mic, one linear audio track. No mechanism noise (drum, capstan): its level and
// spectrum were not found anywhere, so it is not modelled; the only noise is the capsule's own,
// which the ALC lifts in the gaps.
//
// Mic-amp band: the Sony DCR-TRV900E's mic input, measured by D. Vrancic (signal generator and
// oscilloscope, manual gain 3; the line input is flat, so the roll-offs are the mic path's),
// read from his table: 20 Hz -12.8, 50 Hz -3.1, 100 Hz -0.6, 200 Hz to 1 kHz 0, 2 kHz -0.2,
// 5 kHz -0.6, 10 kHz -1.7, 15 kHz -3.6, 20 kHz -4.9 dB re 1 kHz. A 1998 camera, the only
// measured camcorder mic path found. The report's shapes (second-order highpass near 45 Hz,
// Q about 0.6; first-order lowpass near 14 kHz) refitted to those points for each rate band
// (tools/mic_fit.py fit camamp): the highpass comes to 38.8 Hz, Q 0.569 at every rate; the
// lowpass is a first-order section with its pole matched to its corner and its zero placed by
// the fit (a bilinear one, with its zero at half the sample rate, cannot follow the points at
// 44.1 and 48 kHz). Within 0.2 dB of every point from 20 Hz to 20 kHz (to 0.45 times the rate)
// at all five rates. The table's 10 Hz (-28.1 dB) is not met by a second-order highpass that
// meets 20 and 50 Hz (it gives -23.8); the capsule's own 53 Hz highpass lies on top of it.
constexpr float kCamHpHz = 38.8f, kCamHpQ = 0.569f;                 // fit to the read points
constexpr float kCamLp[4][2] = {                                     // Hz (pole), zero (at -z)
    { 19660.6f, 0.234f }, { 16080.6f, 0.185f },                      // below 46 kHz; 46-70 kHz
    { 13431.8f, 0.091f }, { 13299.6f, 0.000f },                      // 70-130 kHz; 130 kHz up
};
// Mic-amp overload: derived (camcorder report, section 3). ROHM BA7780KV: 0.95 V out at 21.5 dB
// of gain, so -22.0 dBV at its input; Sanyo LA7471M: 1.4 V typical at 24.3 dB, -21.4 dBV. With
// the -35 dB V/Pa capsule both are 107 to 108 dB SPL; the report's range is 104 (BA7780 gain
// high) to 110 dB SPL (gain low), its value 107. Taken as 1 % THD at 107 dB SPL, as for the
// JFET: the plugin's cubic at the scale that gives a sine of that level 1 %. Symmetric (an
// estimate: the datasheets give a level, not a shape), so its scale is 1.953 times the peak
// (tools/mic_fit.py consts), not the JFET's 1.935, which is for a cubic that gives way on one
// half only (the report reused that figure; the difference is 0.08 dB).
constexpr float kCamClip = 1.953f * kPaPeak * 4.4668f;              // 107 dB SPL = 94 + 13 dB
// The ALC's static curve, read from the TRV900's mic-jack measurements (1.2 kHz, peak values):
// full gain up to its knee, then 0.75 dB more output for 14 dB more input, about 1 : 19.
// Its range: 22 dB, read (Sanyo VM-RZ1P training manual, ALC section: "a wide coverage range
// of 22 dB" with the mic), consistent with the Sony CCD-M8U's "at least 20 dB". So the loop
// attenuates 18 dB for every dB its detector is above the threshold, by at most 22 x 18 / 19 =
// 20.84 dB (the report rounds it to 20.8, 0.0912); past that the output follows the input.
//
// Where its range starts: the camera's nominal mic level, -66 dBs (0.388 mV, Sony CCD-M8U and
// CCD-TR4; the JVC GR-C1 says -68), is taken as the knee (an assumption, supported by the TRV900,
// whose knee lies at -63 dBs). From the -35 dB V/Pa capsule that is 0.0218 Pa, 60.8 dB SPL; by
// the plugin's one level mapping (a sine peaking at kRefLevel is 104 dB SPL) a sine of 60.8 dB
// SPL peaks 43.2 dB below kRefLevel. The knee's output is set so that the end of the 22 dB range
// lands on kRefLevel: 22 / 19 = 1.16 dB below it (derived; the report rounds it to -1.2 dB,
// 0.871). The record gain in front of the ALC is that output over the knee's input: +42.0 dB
// (derived). So the knee follows from the mapping: move kRefLevel's SPL and the knee moves with it.
// The range ends 22 dB above the knee, at 82.8 dB SPL (a sine peaking 21.2 dB below kRefLevel);
// above that the camera overloads.
constexpr float kCamKneePeak = kPaPeak * (0.38822e-3f / 0.017783f);   // -66 dBs / (-35 dB V/Pa) = 0.0218 Pa
constexpr float kCamThresh   = kRefLevel * 0.87520f;                  // -1.16 dB: 10 ^ (-22 / 19 / 20)
constexpr float kCamRecGain  = kCamThresh / kCamKneePeak;             // +42.0 dB
constexpr float kCamMaxAtt   = 0.090760f;                             // -20.84 dB: 10 ^ (-22 x 18 / 19 / 20)
// Attack and recovery: no period camera's ALC time constants were found (the GR-C1's ALC filter
// C9, C23, R10 is on its schematic but not legible). Both are estimates, the report's:
// attack, the detector's charge, 20 ms: slower than the Boombox's 5 ms, because a camcorder's
// AGC is documented to let a sudden loud sound distort "before the AGC reacts" (Videomaker,
// "Camcorder Sounds"). Recovery, the detector's discharge, 19 s: with the 18 dB-per-dB law the
// gain comes back at 18 x 8.69 / 19 = 8.2 dB a second, the full 20.8 dB in about 2.5 s; chosen
// from words only: "much lower for several seconds, and then it will gradually increase"
// (Videomaker, "The AGC Conspiracy"), and the Sanyo's "recovery time is shortened" with the mic
// (so faster than the Boombox's 7.6 s).
constexpr float kCamAttackSec   = 0.020f;                    // estimate
constexpr float kCamRecoverySec = 19.0f;                     // estimate
// After the ALC: the Boombox's cubic (kAlcCubic, kAlcRail), the report's choice (an estimate:
// the LA3220's distortion figure belongs to a radio-cassette IC, not a camcorder's).
//
// Distance: the camera stands further from the source than a close microphone would. An
// estimate, set by ear by the owner (to be retuned by ear: change this one number; 20 dB was
// "ever so slightly too clean"). 16 dB is about six times the distance in free field: a
// microphone close to the source against a camera across the room. At Mic Gain 0 it puts a sine
// of ordinary level (-18 dBFS rms) 1.8 dB inside the top of the ALC's range instead of 14.2 dB
// past it. It is applied at the very input of the Camcorder's chain,
// before the capsule, so the capsule and everything after it hear the source that much quieter.
// The level mapping (a sine peaking at kRefLevel is 104 dB SPL) stays as it is, and the same for
// all four types: the distance is a property of the Camcorder, not a change of the mapping. Mic
// Gain still sets how loud the source is, on top of it.
constexpr float kCamDistanceDb = 16.0f;                      // estimate

struct MicModel
{
    enum { kCamcorder = 0, kDynamic, kPhone, kBoombox };
    int type = -1, n = 0;           // the type in the filters; how many filters it has
    PairBiquad f[8];                // the filters (Dynamic runs these; they are also designed and measured here)
    MonoBiquad mf[8];               // the same filters for the mono types (copied from f in set())
    float capNoise = 0.0f, hiss = 0.0f, line = 0.0f;   // noise amplitudes for this rate (uniform white noise)
    float camIn = 0.5f;                                // camcorder: the mono sum's 1/2 times the distance (kCamDistanceDb)
    Biquad lineBp;                                     // the phone line's band (mono)
    Xorshift32 rng[2] = { Xorshift32(0x3c6ef372u), Xorshift32(0xa54ff53au) };
    // phone: the level the carbon's sensitivity follows
    float env = kRefLevel, envAtk = 0.0f, envRel = 0.0f;
    // camcorder, boombox: the ALC's detector voltage (double: its 19 or 22 s discharge is a
    // change of about one part in a million a sample, which a float would round to a few
    // percent), 1 / (vc / threshold), the gain it sets, the detector's charge and discharge per
    // sample
    double vc = 0.0, alcAtk = 0.0, alcRel = 1.0;
    float u = 1.0f, alcG = 1.0f;

    static BiquadCoeffs design(const MicSec& s, float sr)
    {
        switch (s.kind)
        {
            case 'h': return BiquadCoeffs::highpass(sr, s.hz, s.q);
            case 'l': return BiquadCoeffs::lowpass(sr, s.hz, s.q);
            case 'p': return BiquadCoeffs::peak(sr, s.hz, s.q, s.db);
            case 'n': return BiquadCoeffs::notch(sr, s.hz, s.q);
            case 'H':
            {
                // the same highpass as 'h', worked out in double before it is rounded to float:
                // at 192 kHz a 39 Hz corner is a change of under a millionth in cos w, which
                // float arithmetic gets wrong by a few percent (0.35 dB at 20 Hz)
                const double w = 2.0 * 3.14159265358979 * s.hz / sr, c = std::cos(w), al = std::sin(w) / (2.0 * s.q), a0 = 1.0 + al;
                BiquadCoeffs k;
                k.b0 = (float)(0.5 * (1.0 + c) / a0); k.b1 = (float)(-(1.0 + c) / a0); k.b2 = k.b0;
                k.a1 = (float)(-2.0 * c / a0); k.a2 = (float)((1.0 - al) / a0);
                return k;
            }
            case 'L':
            {
                // first-order lowpass: its pole matched to its corner, its zero at -q (q = 1
                // would put it at half the rate, as the bilinear transform does). Its gain is
                // set by the 1 kHz normalisation that follows.
                BiquadCoeffs c; c.b0 = 1.0f; c.b1 = s.q; c.b2 = 0.0f; c.a1 = -std::exp(-kTwoPi * s.hz / sr); c.a2 = 0.0f;
                return c;
            }
            default:
            {
                // first-order highpass by the bilinear transform, exact at its corner
                const float k = std::tan(kPi * minf(s.hz, 0.45f * sr) / sr), m = 1.0f / (1.0f + k);
                BiquadCoeffs c; c.b0 = m; c.b1 = -m; c.b2 = 0.0f; c.a1 = (k - 1.0f) * m; c.a2 = 0.0f;
                return c;
            }
        }
    }
    // |H| of filters [from, to) at hz
    double gainAt(int from, int to, float sr, float hz) const
    {
        const double w = 2.0 * 3.14159265358979 * hz / sr, c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2.0 * w), s2 = std::sin(2.0 * w);
        double g = 1.0;
        for (int i = from; i < to; ++i)
        {
            const PairBiquad& q = f[i];
            const double nr = q.b0 + q.b1 * c1 + q.b2 * c2, ni = -(q.b1 * s1 + q.b2 * s2);
            const double dr = 1.0 + q.a1 * c1 + q.a2 * c2, di = -(q.a1 * s1 + q.a2 * s2);
            g *= std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
        }
        return g;
    }
    // Filters [from, to) together to 0 dB at 1 kHz: the first one's numerator is scaled.
    void normalise(int from, int to, float sr)
    {
        const float k = (float)(1.0 / gainAt(from, to, sr, 1000.0f));
        f[from].b0 *= k; f[from].b1 *= k; f[from].b2 *= k;
    }
    // Uniform white noise in [-a, a] whose rms, measured over `bandwidth` Hz, is `rms`.
    static float noiseAmp(float rms, float bandwidth, float sr) { return rms * std::sqrt(3.0f * 0.5f * sr / bandwidth); }
    // The same, for white noise that goes through filters with this power gain (sum of the
    // squares of their impulse response) and is measured at their output.
    static float noiseAmp(float rms, double power) { return rms * (float)std::sqrt(3.0 / power); }
    template <typename F> static double powerGain(F filter, float sr)
    {
        double sum = 0.0;
        const int len = (int)(0.05f * sr);   // 50 ms: the slowest one here, the 50 Hz highpass, has decayed by 96 dB
        for (int i = 0; i < len; ++i) { const double y = filter(i == 0 ? 1.0f : 0.0f); sum += y * y; }
        return sum;
    }

    void set(int t, float sr)
    {
        type = t;
        const int band = sr >= 130000.0f ? 3 : sr >= 70000.0f ? 2 : sr >= 46000.0f ? 1 : 0;   // the fits are per rate band
        n = 0;
        auto add = [&](const MicSec& s) { f[n++].setCoeffs(design(s, sr)); };
        if (t == kCamcorder || t == kBoombox)
        {
            const float* p = kCapPeak[band];
            add({ 'p', p[0], p[1], p[2] });
            add({ '1', kGateHpHz, 0.0f, 0.0f });
            normalise(0, 2, sr);
            capNoise = noiseAmp(kCapNoiseRms, kAWeightBw, sr);   // A-weighted level of white noise, at the capsule
        }
        if (t == kCamcorder)
        {
            const float* l = kCamLp[band];
            add({ 'H', kCamHpHz, kCamHpQ, 0.0f });
            add({ 'L', l[0], l[1], 0.0f });
            normalise(2, 4, sr);
            alcAtk = 1.0 - std::exp(-1.0 / ((double)kCamAttackSec * sr));
            alcRel = std::exp(-1.0 / ((double)kCamRecoverySec * sr));
            camIn = 0.5f * std::pow(10.0f, -kCamDistanceDb / 20.0f);
        }
        if (t == kBoombox)
        {
            add({ 'h', 50.0f, 0.7071f, 0.0f });
            add({ 'l', 10000.0f, 0.7071f, 0.0f });
            normalise(2, 4, sr);
            PairBiquad a = f[2], b = f[3];
            a.reset(); b.reset();
            hiss = noiseAmp(kTapeHissRms, powerGain([&](float x) { return b.process(a.process(splat(x)))[0]; }, sr));
            alcAtk = 1.0 - std::exp(-1.0 / ((double)kAlcAttackSec * sr));
            alcRel = std::exp(-1.0 / ((double)kAlcRecoverySec * sr));
        }
        if (t == kDynamic)
        {
            for (const MicSec& s : kDynSecs[band]) if (s.kind != 0) add(s);
            normalise(0, n, sr);
        }
        if (t == kPhone)
        {
            for (const MicSec& s : kPhoneSecs) add(s);
            normalise(0, n, sr);
            lineBp.setCoeffs(BiquadCoeffs::bandpass(sr, kLineBpHz, kLineBpQ));
            Biquad bp = lineBp;
            bp.reset();
            line = noiseAmp(kLineNoiseRms, powerGain([&](float x) { return bp.process(x); }, sr));
            envAtk = 1.0f - std::exp(-1.0f / (0.001f * sr));    // estimates (see kCarbonH2)
            envRel = 1.0f - std::exp(-1.0f / (0.050f * sr));
        }
        for (int i = 0; i < n; ++i) mf[i].setCoeffs(f[i]);
    }
    void reset()
    {
        for (PairBiquad& b : f) b.reset();
        for (MonoBiquad& b : mf) b.reset();
        lineBp.reset();
        env = kRefLevel;
        vc = 0.0; u = 1.0f; alcG = 1.0f;
    }
    // The negative half of the capsule's JFET gives way first.
    static float jfet(float x)
    {
        const float s = x < 0.0f ? kJfetNeg : kJfetPos;
        const float t = clamp(x * (x < 0.0f ? 1.0f / kJfetNeg : 1.0f / kJfetPos), -1.5f, 1.5f);
        return s * (t - (4.0f / 27.0f) * t * t * t);
    }
    // The camcorder's mic amp gives way on both halves alike.
    static float micAmp(float x)
    {
        const float t = clamp(x * (1.0f / kCamClip), -1.5f, 1.5f);
        return kCamClip * (t - (4.0f / 27.0f) * t * t * t);
    }
    // one filter on a mono signal
    float mono(int i, float x) { return mf[i].process(x); }
    // The capsule on one mono signal: its two filters, its noise (noise: 1 while the Noise
    // switch is on, 0 off, smoothed), its JFET.
    float capsule(float m, float noise)
    {
        m = mono(1, mono(0, m));
        if (noise > 0.0f) m += rng[0].nextBipolar() * (capNoise * noise);
        return jfet(m);
    }
    // A record ALC (Camcorder, Boombox): the gain element (record gain times the loop's gain),
    // the cubic after it, and the detector on the output, which sets the gain for the next
    // sample. Above the threshold the gain is (vc / threshold) ^ -kLaw, with 1 / r followed by
    // one Newton step a sample (r moves by a few percent a sample at most), at least maxAtt.
    template <int kLaw> float alc(float m, float recGain, float invThresh, float maxAtt)
    {
        m = clamp(m * (recGain * alcG), -kAlcRail, kAlcRail);
        m -= kAlcCubic * m * m * m;
        // the detector: discharges through its resistor, charges from the output's peaks
        const double pk = std::fabs(m);
        vc *= alcRel;
        if (pk > vc) vc += alcAtk * (pk - vc);
        const float r = (float)vc * invThresh;
        if (r > 1.0f)
        {
            u = clamp(u * (2.0f - r * u), 0.0f, 1.0f);
            const float u2 = u * u, u4 = u2 * u2;
            if (kLaw == 14) alcG = maxf(u4 * u4 * u4 * u2, maxAtt);
            else { const float u8 = u4 * u4; alcG = maxf(u8 * u8 * u2, maxAtt); }   // 18
        }
        else { u = 1.0f; alcG = 1.0f; }
        return m;
    }
    // Kept out of line: inlined into the plugin's sample loop, its many values crowded the
    // loop's registers, which cost the whole plugin something even with the Mic off, and made
    // the model itself reload more. A call a sample is cheaper than either.
    __attribute__((noinline)) Stereo process(Stereo x, float noise)
    {
        switch (type)
        {
            case kCamcorder:
            {
                float m = capsule((x[0] + x[1]) * camIn, noise);    // one built-in mic, across the room
                m = micAmp(mono(3, mono(2, m)));                    // the mic amp: its band, its overload
                return splat(alc<18>(m, kCamRecGain, 1.0f / kCamThresh, kCamMaxAtt));
            }
            case kDynamic:   // four filters, five from 70 kHz up (written out: no loop to run)
                x = f[3].process(f[2].process(f[1].process(f[0].process(x))));
                return n == 5 ? f[4].process(x) : x;
            case kPhone:
            {
                float m = 0.5f * (x[0] + x[1]);                // one line
                const float pk = std::fabs(m);
                env += (pk > env ? envAtk : envRel) * (pk - env);
                // sensitivity: r ^ (1/4) below reference (1 : 1.25), at most 10 dB down
                // (the two square roots only where they decide the result: at and above reference
                // the sensitivity is 1, and below kCarbonQuiet the fourth root is surely under the
                // floor, so the floor is what maxf would have given)
                const float r = env * (1.0f / kRefLevel);
                float g = 1.0f;
                if (!(r >= 1.0f)) g = r < kCarbonQuiet ? kCarbonFloor : maxf(std::sqrt(std::sqrt(r)), kCarbonFloor);
                m = clamp(m * g, -kCarbonClip, kCarbonClip);
                m += kCarbonH2 * m * m;                       // its DC goes in the highpasses that follow
                for (int i = 0; i < 8; ++i) m = mono(i, m);
                if (noise > 0.0f) m += lineBp.process(rng[0].nextBipolar()) * (line * noise);
                return splat(m);
            }
            default:   // kBoombox
            {
                float m = capsule(0.5f * (x[0] + x[1]), noise);     // one built-in mic
                m = alc<14>(m, kAlcRecGain, 1.0f / kRefLevel, kAlcMaxAtt);
                if (noise > 0.0f) m += rng[1].nextBipolar() * (hiss * noise);
                return splat(mono(3, mono(2, m)));           // the tape
            }
        }
    }
};

// ---------------------------------------------------------------------------
// AGC, AGC Model = Modeled: the record level circuit of an early VHS deck, built from its
// service manual (schematic and parts list) and the published figures of the equivalent of its
// AGC chip (NTE1263 for the AN262). The research notes behind it ("the AGC report", VHS Vibes
// record AGC, second reading, 2026-11-16) mark each value: [R] read from the source, [D]
// derived from read values, [E] estimated.
//
//   The AGC is a feedback leveller: the chip's line output goes through C70 into a half-wave
//   detector, whose capacitor C80 sets the gain. The detector, per sample (explicit Euler):
//     source (R_src) -> C70 -> R103 -> node A;  A -> R104 -> ground;  A -> diode -> K;
//     K: C80 to ground, and R107 + R108 to ground (the slow discharge).
//   Two stages of attack follow from it: C70 dumps charge into C80 within a few cycles (about
//   9 % of the excess peak, through R103: 4 to 5 ms), then the offset left on C70 bleeds off
//   through R104 (about a second). Recovery is C80 through R107 + R108: about 1 dB a second.
//   The control voltage reaches the chip through C81, which lags it by 3.8 ms [D].
//
//   Gain law: GR = clamp(S * (vK - Vth), 0, 40) dB, linear in dB on C80's voltage [E: the shape;
//   a junction-like control input]. S and Vth are fitted (tools/agc_fit.py fit) so that a steady
//   1 kHz tone gives the chip's typical compression: the knee 10 dB below the nominal input [E],
//   and 40 dB over it comes out 3.25 dB louder [D: NTE1263, 0.55 V out at 0.25 mV in, 0.8 V at
//   25 mV]. The level mapping: the deck's nominal input is kRefLevel (-8 dBFS), which settles to
//   1.1 V peak at the detector [R: 2.2 Vp-p line out at the reference level]; the fit gives the
//   detector volts for a sample of 1.0 at full gain (kVolts). Steady state (tools/agc_fit.py
//   table): 0 dB re knee 0 dB GR, +10 dB (nominal) 9.1 dB, +20 dB 18.2 dB, +40 dB 36.75 dB.
//
//   In the plugin: the deck was mono, so the detector sees the larger of the two channels and
//   one gain goes to both. The gain the detector works with is the full model's; the sound gets
//   Amount x (the model's gain in dB + kBoostDb), where kBoostDb is the gain reduction at the
//   nominal level: a steady tone at nominal passes at unity, quieter sound below the knee is
//   lifted by 9.1 dB, louder sound is cut. The gain is worked out once per control tick and
//   ramped across it (16 samples, 0.36 ms, against the circuit's fastest 4.7 ms).
// ---------------------------------------------------------------------------
struct ModeledAgc
{
    static constexpr float kC70 = 4.7e-6f, kR103 = 1000.0f, kR104 = 22000.0f, kC80 = 47e-6f;   // [R]
    static constexpr float kRdis = 1.2e6f + 560e3f;                 // R107 + R108 [R]
    static constexpr float kRsrc = 100.0f;                          // the chip's output and R106 path [E]
    static constexpr float kVd = 0.15f, kRd = 100.0f;               // germanium diode, piecewise linear [E]
    static constexpr float kLagSec = 0.01e-6f * (1.2e6f * 560e3f / (1.2e6f + 560e3f));   // C81 x R107 || R108: 3.82 ms [D]
    static constexpr float kGrMax = 40.0f;                          // control range, dB [D/E]
    static constexpr float kSwing = 4.5f;                           // the line output clips at +-4.5 V (12 V supply) [E, the
                                                                    // AGC report's attack simulation, 4.1]: only a sudden loud
                                                                    // sound reaches it, before the gain has come down
    // fitted by tools/agc_fit.py (at 192 kHz, to the three figures above) [D]
    static constexpr float kS = 88.297348f;        // dB of gain reduction per volt on C80
    static constexpr float kVth = 0.768954f;       // C80's voltage at the knee
    static constexpr float kVolts = 7.818492f;     // detector volts for a sample of 1.0 at full gain
    static constexpr float kBoostDb = 9.075806f;   // gain reduction at the nominal level: the lift below the knee
    static constexpr float kDbToLn = 0.115129255f; // ln(10) / 20

    float aOff = 0, onE = 0, onK = 0, onN = 0, k70 = 0;   // per sample rate
    double kK = 0, kDis = 0, lag = 0;
    // C80 and the control voltage after C81 (in C80 volts) are doubles: in the slow recovery each
    // moves by about four float steps (ulps) a sample, and rounding that in float would make the
    // recovery up to 9 % slower at some sample rates. C70's voltage is the float v70.
    double vK = 0, vc = 0;
    float v70 = 0;
    float gv = kVolts, gvTo = kVolts, gvInc = 0;    // the model's gain x kVolts, ramped across the tick
    float a = 1.0f, aTo = 1.0f, aInc = 0;           // the gain the sound gets, ramped across the tick

    void prepare(float sr)
    {
        const float dt = 1.0f / sr, rs = kRsrc + kR103;
        aOff = kR104 / (rs + kR104);                // diode off: the divider
        onE = 1.0f / rs; onK = 1.0f / kRd;          // diode on: node A between the three branches
        onN = 1.0f / (1.0f / rs + 1.0f / kR104 + 1.0f / kRd);
        k70 = dt / (kC70 * rs);
        const double dtd = 1.0 / (double)sr;
        kK = dtd / (double)kC80; kDis = dtd / ((double)kC80 * (double)kRdis);
        lag = 1.0 - std::exp(-dtd / (double)kLagSec);
    }
    // C80 and C70 empty, full gain: the deck at the start of a recording (its playback switch
    // shorts C80)
    void reset() { vK = vc = 0.0; v70 = 0.0f; gv = gvTo = kVolts; gvInc = 0.0f; a = aTo = 1.0f; aInc = 0.0f; }

    // Once per control tick: the gains for the next 16 samples. amount 0..1 scales the change in dB.
    void tick(float amount)
    {
        if (!(vK < 1.0e3 && std::fabs(v70) < 1.0e3f && vc < 1.0e3)) reset();     // never stuck on a NaN
        if (vK < 1.0e-20) vK = 0.0;                                               // no denormals in long silence
        if (std::fabs(v70) < 1.0e-20f) v70 = 0.0f;
        if (vc < 1.0e-20) vc = 0.0;
        const float gr = clamp((float)(vc - (double)kVth) * kS, 0.0f, kGrMax);
        gv = gvTo; a = aTo;                                                       // land exactly where the ramps went
        gvTo = kVolts * std::exp(-gr * kDbToLn);
        aTo = std::exp(amount * (kBoostDb - gr) * kDbToLn);
        gvInc = (gvTo - gv) * kCtrlInv; aInc = (aTo - a) * kCtrlInv;
    }

    // One sample: the detector hears the input at the model's gain; returns the sound's gain.
    float step(Stereo x)
    {
        gv += gvInc; a += aInc;
        const float e = clamp(maxf(x[0], x[1]) * gv, -kSwing, kSwing);   // the line output, within its swing
        const float d = e - v70;                       // across R_src + R103 and node A (C70's offset taken off)
        const float on = (float)vK + kVd;
        float va = d * aOff, i = 0.0f;
        if (va > on) { va = (d * onE + on * onK) * onN; i = (va - on) * onK; }
        vK += kK * (double)i - kDis * vK;
        v70 += k70 * (d - va);
        vc += lag * (vK - vc);
        return a;
    }
};

// ---------------------------------------------------------------------------
// HI-FI: Sat Type = Hi-Fi. The FM sound the rotating heads write under the picture (VHS Hi-Fi,
// depth-multiplex audio), in the saturator's place. FM does not saturate; what is heard instead is
// a hard ceiling when the level is too hot (the deviation limiter), noise that comes and goes with
// the music and pumping when record and playback disagree (the 2:1 compander), clicks and a
// tearing crackle when the carrier off the tape gets weak (FM threshold), a buzz riding on high
// notes (the two heads handing over 59.94 times a second), and when the carrier is lost a short
// hold, then the dull mono linear track.
//
// The figures come from the VHS Vibes Hi-Fi research notes of 2026-11-16 ("report 1" and its
// follow-up "report 2"; where they differ report 2 is used). Labels: [R] read in a primary source,
// [S] secondary, [D] derived from read values, [E] estimate. Sources named below: the Philips
// TDA9605H and TDA9610H and the Panasonic AN3664NFB datasheets; MAT83 = S. Hitotsumachi
// (Matsushita), "VHS Hi-Fi VTR", J. ITE Japan 37(12), 1983; FUJ = Fuji tape patents (dropout
// counts); HXV = JVC HR-XV31 service manual; Rice 1948 (BSTJ 27) for the click rate.
//
//   record    Rec Level -> NR emphasis P -> 2:1 compressor (its detector on its output, through
//             the weighting W) -> FM emphasis Q -> deviation limiter
//   channel   the demodulator's output: + noise, + clicks, + the head switch; held while the
//             carrier is lost. All of it follows the carrier level c (dB re nominal).
//   playback  FM de-emphasis 1/Q -> Cal (a level error) -> 1:2 expander (its detector on its
//             input, through W) -> NR de-emphasis 1/P -> down again by the Rec Level
//   loss      c 17 dB down for 0.3 ms: crossfade to the linear track (TapeSat, mono); back two
//             head-drum turns (HID periods) after the carrier returns
//
// Level: the reference, 50 kHz deviation at 1 kHz [R], is a 1 kHz sine peaking at kRefLevel, as
// everywhere in the plugin. The three shelves are scaled to 0 dB at 1 kHz, so such a tone passes
// every block at reference; the compressor is unity there. Its detector reads a steady sine at
// kHfDetSine of its peak, whatever the frequency (computed: 0.8967 to within 0.01 dB from 100 Hz
// to 15 kHz at every rate from 44.1 to 192 kHz), and that is its unity point.
//
// The two compander detectors are the same: a peak detector (AN3664's "peak det." [R]) on the
// weighted signal, read as the standard one-pole follower: the detector moves towards the
// rectified signal with a 5 ms time constant while that is above it (attack) and with 70 ms while
// it is below (recovery). So a step up is 63 % covered in 5 ms, a step down in 70 ms. The
// datasheets give "attack 5 ms, recovery 70 ms" without saying how they are measured; that the
// figures are these time constants is the choice made here. Both gains follow their detector at
// every sample (the compressor's 1 / d by a Newton step from the last sample's value, a division
// only when d jumps), each from the detector as it stood before the sample, so with nothing in
// between the expander undoes the compressor exactly, during an attack too.
//
// With every impairment at zero the path is transparent, apart from the limiter (and the head
// switch's documented residual, 74 dB down). Costs nothing while Sat Type is not Hi-Fi.
// ---------------------------------------------------------------------------
constexpr float kHfNrZero = 240.0e-6f, kHfNrPole = 56.0e-6f;   // NR emphasis P [S, checked D: the TDA's 10 kHz figures]
constexpr float kHfWZero  = 240.0e-6f, kHfWPole  = 24.0e-6f;   // detector weighting W [R AN3664 pin 8/30; S]
constexpr float kHfFmZero = 56.0e-6f,  kHfFmPole = 20.0e-6f;   // FM emphasis Q [S, checked D]; "20 to 240 us" [R TDA9610H]
constexpr float kHfAttackSec   = 0.005f;      // [R] TDA9605H, TDA9610H ("according VHS"); AN3664 4 ms
constexpr float kHfRecoverySec = 0.070f;      // [R] the same; AN3664 65 ms
constexpr float kHfDetSine  = 0.89674f;       // the detector's reading of a steady sine, re its peak [D: computed]
constexpr float kHfGainRange = 31.6228f;      // the compander's gain stays within +-30 dB [E, report 1]
constexpr float kHfClip = 3.0f * kRefLevel;   // limiter: +-150 kHz = 9.5 dB over the 50 kHz reference [R]; hard [E]
constexpr float kHfSnrDb   = 55.0f;           // channel noise under reference at full carrier [E in the bench's 54-60 dB, R]
constexpr float kHfNoiseHz = 20000.0f;        // the band it is measured in, and above which it is cut at high rates
constexpr float kHfCnr0Db  = 25.0f;           // carrier to noise at full carrier, 300 kHz band [E, report 2: 17-27 dB]
// Crackle sets that margin: 27 dB at 0 % (the best of the range: no clicks before the deck gives the
// carrier up), 8 dB at 100 % (a worn-out deck, past the range [E]: clicks at full carrier, about 30 a
// second on each channel). 25 dB is at about 10 %.
constexpr float kHfCnrBestDb = 27.0f, kHfCnrSpanDb = 19.0f;
// The knobs reach well past a deck in good order, which is too clean to hear [E]: Hiss is 30 dB over the
// documented scale (the 55 dB point, at Noise 50 %, is Hiss 9 %; Hiss 50 % is a channel 25 dB under
// reference), and the spike Head Buzz leaves at each switch is 20 dB over the half cycle a phase step
// can make (a deck whose heads are far out of line).
constexpr float kHfHissGain = 31.6228f, kHfBuzzGain = 3.16228f;   // (the spike goes with the square of the amount)
// Hiss goes in a straight line with its knob and the Noise level, not with their squares as the other
// types' hiss: the expander doubles every dB of it, so what is heard still follows the square. And the
// channel's noise stops rising 10 dB under reference, however weak the carrier [E].
constexpr float kHfNoiseTopDb = 10.0f;
constexpr float kHfBandHz  = 300.0e3f;        // that band [R MAT83 fig. 7]
constexpr float kHfRefDevHz = 50.0e3f;        // reference deviation [R]: a click (one cycle) has the area kRefLevel / 50 kHz
constexpr float kHfFieldHz = 59.94f;          // head switches per second [R TDA: 29.97 Hz HID]
constexpr float kHfHoldSec = 6.0e-6f;         // the switch's sample-and-hold [R TDA9605H, TDA9610H]
// The concealed switch leaves a pulse of the area a plain hold would (x' T^2 / 2 [D]) times this:
// set so that the residual of a 10 kHz reference tone, below 3 kHz (4th-order lowpass, as the TDA's
// test), comes to -74.5 dB at 96 kHz, between the TDA9605H's -73 and the TDA9610H's -76 dB [R]; at
// 44.1 kHz it measures -75.1 dB (the slope is taken from one sample's step). A plain 6 us hold
// measures -63.4 dB this way [D; report 1 derives about -63]. How the TDA measured its figure (at a
// 500 Hz HID, corrected by 24 dB to 29.97 Hz) cannot be repeated exactly: the residual's shape is [D].
constexpr float kHfResidual = 0.279f;
constexpr float kHfDocDb    = -21.0f;         // dropout canceller holds below this [R TDA: 4 dB under the hi-fi detector]
constexpr float kHfNormalDb = -17.0f;         // hi-fi detector: the linear track below this [R TDA 3.3 mV of 24 mV]
constexpr float kHfNormalSec = 300.0e-6f;     // ... after this long [R TDA "switch-on delay", 150-500 us]
constexpr float kHfDocReleaseSec = 9.0e-6f;   // the hold lets go this long after the carrier is back [R TDA]
constexpr int   kHfBackEdges = 2;             // back on the second HID rising edge: 1-2 HID periods, 33-67 ms [R TDA fast mode]
constexpr float kHfXfadeSec  = 0.003f;        // the crossfade to and from the linear track [E: the switch's shape is not given]
constexpr float kHfImbalanceDb = 4.0f;        // Tracking at 100 %: one head this much weaker [E; 3-5 dB is within service tolerance, R HXV]
constexpr float kHfDriftDb  = 15.0f;          // Drift at 100 %: the deepest slow wander [E]
constexpr float kHfDriftSec = 1.5f;           // ... and its pace [E]
constexpr float kHfTrackRate = 4.0f;          // Tracking at 100 %: dips per second [E]
constexpr float kHfTrackMinDb = 10.0f, kHfTrackSpanDb = 20.0f;   // their depth, 10 to 30 dB [E, the plan]
constexpr int   kHfTrackPasses = 8;           // their length: 1 to this many head passes [E]
constexpr float kHfDropRate = 80.0f / 60.0f;  // Dropouts at 100 %: 80 a minute, a poor tape after 100 passes [R FUJ, video carrier]
constexpr float kHfDropVideoDb = 20.0f;       // a counted dropout is at least 20 dB deep on the video carrier [R FUJ] ...
constexpr float kHfDropTailDb = 10.0f;        // ... and deeper by an exponential share of this mean [E]
constexpr float kHfDropShare = 0.45f;         // the Hi-Fi carriers lose 0.4-0.5 of the video's dB [D, report 2: Wallace]
constexpr float kHfDropMinSec = 15.0e-6f;     // and last 15 us [R FUJ: counted from 15 us] ...
constexpr float kHfDropOctaves = 7.0f;        // ... to 2 ms, evenly in octaves [E]
constexpr float kHfRecMaxDb = 16.0f;          // Rec Level at the top of Lo Sat [the plan; TDA tests 2:1 to +16 dB, R]
constexpr float kHfCarrierMinDb = -30.0f;     // Carrier at the top of Hi Sat [the plan; the absolute scale is E]
constexpr float kHfCalDb = 3.0f;              // Cal at either end of Split [the plan; deck tolerances +-1 to +-2 dB, R]

// Rec Level from Lo Sat (0..1): -10 dB at 0, 0 dB where VHS mode has it (10 / 18, 55.6 %), +16 dB at 1.
inline float hifiRecDb(float n)
{
    const float n0 = -kTapeLevelMin / kTapeLevelSpan;
    return n <= n0 ? kTapeLevelMin + kTapeLevelSpan * n : kHfRecMaxDb * (n - n0) / (1.0f - n0);
}

// First-order shelf (1 + s tz) / (1 + s tp), digital with its zero and pole where the analogue
// ones map (z = e^(-1 / (t sr))), scaled to 0 dB at 1 kHz. Against the analogue curve (re 1 kHz):
// within 0.04 dB to 10 kHz at 44.1 kHz and 0.01 dB from 88.2 kHz up. inv() is the exact inverse,
// with its own memories: a shelf and its inverse together pass the sound unchanged.
struct HfShelf
{
    float k = 1.0f, kInv = 1.0f, zz = 0.0f, zp = 0.0f;
    Stereo x1 = {}, y1 = {};
    void set(float tz, float tp, float sr)
    {
        zz = std::exp(-1.0f / (tz * sr));
        zp = std::exp(-1.0f / (tp * sr));
        const float c = std::cos(kTwoPi * 1000.0f / sr);
        k = std::sqrt((1.0f - 2.0f * zp * c + zp * zp) / (1.0f - 2.0f * zz * c + zz * zz));
        kInv = 1.0f / k;
    }
    void reset() { x1 = y1 = splat(0.0f); }
    void flush() { const Stereo t = splat(1.0e-20f), z = splat(0.0f); x1 = vabs(x1) < t ? z : x1; y1 = vabs(y1) < t ? z : y1; }
    Stereo fwd(Stereo x) { const Stereo y = k * (x - zz * x1) + zp * y1; x1 = x; y1 = y; return y; }
    Stereo inv(Stereo y) { const Stereo x = (y - zp * y1) * kInv + zz * x1; x1 = x; y1 = y; return x; }
};

// The compander's detector (see above): a one-pole follower, attack while rising, recovery while falling.
struct HfDetector
{
    float att = 0.0f, rel = 0.0f;
    Stereo d = {};
    void set(float atkSec, float relSec, float sr) { att = 1.0f - std::exp(-1.0f / (atkSec * sr)); rel = 1.0f - std::exp(-1.0f / (relSec * sr)); }
    void step(Stereo a) { d += (a > d ? splat(att) : splat(rel)) * (a - d); }
};

struct HiFiPath
{
    // What the knobs ask for (see VHSVibes::setHiFi): levels in dB, amounts 0..1.
    struct Settings
    {
        float recDb = 0.0f, carrierDb = 0.0f, calDb = 0.0f;
        float hiss = 0.0f;          // channel noise, x the 55 dB point
        float clicks = 0.0f;        // click size, x the documented area
        float cnr0Db = kHfCnr0Db;   // carrier to noise at full carrier (Crackle)
        float noiseTop = 1.0e9f;    // the most the channel noise rises to, x the 55 dB point (the plugin sets it)
        float imbalanceDb = 0.0f;   // one head this much weaker
        float headBuzz = 0.0f;      // 0 = the documented concealment, 1 = none, more: out of spec
        float headGate = 0.0f;      // the head switch at all (the Noise switch and level)
        float breathing = 0.0f, tracking = 0.0f, drift = 0.0f, dropouts = 0.0f;
    };
    // The channel at one carrier level, worked out per control tick.
    struct Carrier
    {
        float c = 1.0e9f;           // dB re nominal (1e9: not yet worked out)
        float noise = 0.0f;         // noise amplitude
        uint32_t clickThr = 0;      // a click in a sample when a random 32-bit number is below this
        bool doc = false, low = false;   // held; below the hi-fi detector's threshold
    };

    float sr = 0.0f;
    Settings s;
    HfShelf nrEmph, wRec, fmEmph, fmDe, wPlay, nrDe;
    HfDetector recDet, playDet, rmsDet;
    PairBiquad noiseLp;
    TapeSat tape;                   // the linear track
    Xorshift32 rngL{0x48f1a1u}, rngR{0x2c0ffeeu}, rngC{0x1c11c4u}, rngE{0xe7e47u};

    // per rate
    static constexpr float kDRef = kHfDetSine * kRefLevel;   // the compressor's unity point
    float pRef = kDRef, pRefInv = 1.0f, rmsCal = 1.0f;     // the expander's (Breathing moves it)
    float noiseUnit = 0.0f, clickUnit = 0.0f, residK = 0.0f, hsInc = 0.0f, smooth = 0.05f, xfInc = 0.0f;
    float driftK = 0.0f, driftScale = 0.0f, dropP = 0.0f, trackP = 0.0f;
    int   nOn = 13, nRelease = 0;
    bool  lpOn = false, rmsOn = false;
    float blend = 0.0f;

    // smoothed controls and the gains ramped across a tick
    float recT = 1.0f, recS = 1.0f, calT = 1.0f, calS = 1.0f;
    float rec = 1.0f, recTo = 1.0f, recInc = 0.0f, mk = 1.0f, mkTo = 1.0f, mkInc = 0.0f, cal = 1.0f, calTo = 1.0f, calInc = 0.0f;
    Stereo cg = {1.0f, 1.0f};       // the compressor's gain, Dref / d (kept from sample to sample)
    Stereo rmsS = {}, rmsTo = {}, rmsInc = {};   // Breathing: the RMS detector's reading, ramped across a tick
    int   n = 0;                    // samples into the control tick

    // channel
    Carrier st[2];                  // [0] the carrier as it is, [1] during a dropout
    Stereo wPrev = {}, yPrev = {}, held = {};
    float hsPh = 0.0f;
    bool  headB = false;            // the second head is reading (the weaker one, with Tracking)
    int   dropLeft = 0, relLeft = 0, lowCount = 0, back = 0, trackPasses = 0;
    float dropDb = 0.0f, trackDb = 0.0f, trackDepth = 0.0f, driftN1 = 0.0f, driftN2 = 0.0f;
    bool  linear = false, tapeRun = false;
    float linMix = 0.0f;
    uint32_t clickCount = 0, switchCount = 0;   // for the tests
    uint32_t drops = 0;                         // dropouts so far, for the picture

    void prepare(float rate)
    {
        sr = rate;
        nrEmph.set(kHfNrZero, kHfNrPole, sr); nrDe.set(kHfNrZero, kHfNrPole, sr);
        wRec.set(kHfWZero, kHfWPole, sr);     wPlay.set(kHfWZero, kHfWPole, sr);
        fmEmph.set(kHfFmZero, kHfFmPole, sr); fmDe.set(kHfFmZero, kHfFmPole, sr);
        recDet.set(kHfAttackSec, kHfRecoverySec, sr);
        // Channel noise: white noise, differenced (rising 6 dB per octave), at high rates cut above
        // 20 kHz (the deck's playback filter). Scaled so that its power from 20 Hz to 20 kHz lies
        // kHfSnrDb under a reference sine's: worked out by summing the filters' response.
        lpOn = sr > 50000.0f;
        if (lpOn) noiseLp.setCoeffs(BiquadCoeffs::lowpass(sr, kHfNoiseHz, 0.7071f));
        double pw = 0.0;
        const int steps = 400;
        for (int i = 0; i <= steps; ++i)
        {
            const double f = 20.0 + (kHfNoiseHz - 20.0) * i / steps, w = 6.283185307179586 * f / sr;
            double g = 4.0 * std::sin(0.5 * w) * std::sin(0.5 * w);
            if (lpOn)
            {
                const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
                const double nr = noiseLp.b0 + noiseLp.b1 * c1 + noiseLp.b2 * c2, ni = -(noiseLp.b1 * s1 + noiseLp.b2 * s2);
                const double dr = 1.0 + noiseLp.a1 * c1 + noiseLp.a2 * c2, di = -(noiseLp.a1 * s1 + noiseLp.a2 * s2);
                g *= (nr * nr + ni * ni) / (dr * dr + di * di);
            }
            pw += g * (i == 0 || i == steps ? 1.0 : (i & 1) ? 4.0 : 2.0);
        }
        pw *= (kHfNoiseHz - 20.0) / steps / 3.0 * (2.0 / sr) / 3.0;   // Simpson; white of [-1, 1) has the power 1/3 over sr / 2
        noiseUnit = (float)(kRefLevel * 0.70710678 * std::pow(10.0, -kHfSnrDb / 20.0) / std::sqrt(pw));
        clickUnit = kRefLevel * sr / kHfRefDevHz;                       // one cycle in one sample
        residK = -kHfResidual * 0.5f * sq(kHfHoldSec * sr);             // x' T^2 / 2 as one sample, from a sample's step
        hsInc = kHfFieldHz / sr;
        smooth = 1.0f - std::exp(-(float)kCtrl / (0.007f * sr));
        xfInc = 1.0f / (kHfXfadeSec * sr);
        nOn = (int)(kHfNormalSec * sr + 0.5f);
        nRelease = (int)(kHfDocReleaseSec * sr + 0.5f);
        driftK = 1.0f - std::exp(-(float)kCtrl / (kHfDriftSec * sr));
        driftScale = std::sqrt(12.0f / driftK);
        set(s);
    }

    void set(const Settings& next)
    {
        const bool rmsWas = rmsOn;
        s = next;
        if (sr <= 0.0f) return;                                        // (prepare() comes back here)
        recT = dbToGain(s.recDb);
        calT = dbToGain(s.calDb);
        tape.set(sr, clamp((s.recDb - kTapeLevelMin) / kTapeLevelSpan, 0.0f, 1.0f), 0.0f, 0.0f);   // SP, bias right
        // Breathing: a tape from another deck. Its expander's attack 0.8 and recovery 1.2 times the
        // right value at 100 % (+-20 % [S]), and its detector an RMS one instead of peak ([S]; both
        // kinds are documented). Each detector's reading of a steady sine (fitted to the computed
        // values to 0.01 %) is its unity point, so a steady tone still comes out at its level: the
        // mismatch is in the movement.
        const float b = s.breathing;
        playDet.set(kHfAttackSec * (1.0f - 0.2f * b), kHfRecoverySec * (1.0f + 0.2f * b), sr);
        rmsDet.att = playDet.att; rmsDet.rel = playDet.rel;
        const float peakRead = kHfDetSine + 0.02184f * b - 0.00004f * b * b;
        const float rmsRead  = 0.91911f + 0.01887f * b - 0.00218f * b * b;
        pRef = peakRead * kRefLevel; pRefInv = 1.0f / pRef;
        rmsCal = peakRead / rmsRead;
        blend = b;
        rmsOn = b > 0.0f;
        if (rmsOn && !rmsWas)                                          // starts where the peak one is
        {
            rmsDet.d = (playDet.d / rmsCal) * (playDet.d / rmsCal);
            rmsS = rmsTo = playDet.d; rmsInc = splat(0.0f);
        }
        dropP  = kHfDropRate * sq(s.dropouts) * (float)kCtrl / sr;
        trackP = kHfTrackRate * sq(s.tracking) / kHfFieldHz;
        st[0].c = st[1].c = 1.0e9f;                                    // work the channel out again
    }

    void reset()
    {
        nrEmph.reset(); wRec.reset(); fmEmph.reset(); fmDe.reset(); wPlay.reset(); nrDe.reset();
        recDet.d = splat(kDRef); playDet.d = splat(pRef);               // at the unity point: no start-up overshoot
        rmsDet.d = splat(sq(pRef / rmsCal));
        noiseLp.reset(); tape.reset();
        rngL.reseed(0x48f1a1u); rngR.reseed(0x2c0ffeeu); rngC.reseed(0x1c11c4u); rngE.reseed(0xe7e47u);
        recS = recT; calS = calT;
        rec = recTo = recS; mk = mkTo = 1.0f / recS; cal = calTo = calS; recInc = mkInc = calInc = 0.0f;
        cg = splat(1.0f);
        rmsS = rmsTo = splat(pRef); rmsInc = splat(0.0f);
        n = 0;
        st[0] = st[1] = Carrier();
        wPrev = yPrev = held = splat(0.0f);
        hsPh = 0.0f; headB = false;
        dropLeft = relLeft = lowCount = back = trackPasses = 0;
        dropDb = trackDb = trackDepth = driftN1 = driftN2 = 0.0f;
        linear = tapeRun = false; linMix = 0.0f;
    }

    // The channel at carrier level c.
    void carrierAt(Carrier& k, float c) const
    {
        if (c == k.c) return;
        k.c = c;
        k.doc = c < kHfDocDb;
        k.low = c < kHfNormalDb;
        // noise: 1 dB more for every dB of carrier lost [R, FM theory]
        k.noise = s.hiss > 0.0f ? noiseUnit * minf(s.hiss * dbToGain(-c), s.noiseTop) : 0.0f;
        // clicks: Rice's rate for an unmodulated carrier, (B / 2 sqrt 3) erfc(sqrt(CNR)) per channel
        // [D from Rice 1948]. The extra rate with modulation (report 1) is not verified and left out.
        const float cnr = s.cnr0Db + c;
        k.clickThr = 0;
        if (s.clicks > 0.0f && cnr < 16.0f)                            // above 16 dB: under one click a century
        {
            const float rate = kHfBandHz * (0.5f / 1.7320508f) * std::erfc(std::sqrt(std::pow(10.0f, 0.1f * cnr)));
            const double p = (double)rate / sr * 4294967296.0;
            k.clickThr = p >= 4294967295.0 ? 0xffffffffu : (uint32_t)p;
        }
    }

    // Once per control tick: Rec Level and Cal, the RMS detector, the carrier and its events.
    void tick()
    {
        // never stuck on a NaN; no denormals in long silence
        const Stereo dd = recDet.d + playDet.d + rmsDet.d;
        if (!(maxf(dd[0], dd[1]) < 1.0e6f && minf(dd[0], dd[1]) >= 0.0f && std::fabs(held[0] + held[1]) < 1.0e6f)) reset();
        nrEmph.flush(); wRec.flush(); fmEmph.flush(); fmDe.flush(); wPlay.flush(); nrDe.flush();
        { const Stereo t = splat(1.0e-20f), z = splat(0.0f);
          recDet.d = recDet.d < t ? z : recDet.d; playDet.d = playDet.d < t ? z : playDet.d; rmsDet.d = rmsDet.d < t * t ? z : rmsDet.d; }

        // Rec Level and Cal: smoothed, then ramped. The level comes back down after the expander
        // by as much as it went up before the compressor.
        rec = recTo; mk = mkTo; cal = calTo;                           // land exactly where the ramps went
        if (recS != recT) { recS += smooth * (recT - recS); if (std::fabs(recS - recT) < 1.0e-6f * recT) recS = recT; mkTo = 1.0f / recS; }
        if (calS != calT) { calS += smooth * (calT - calS); if (std::fabs(calS - calT) < 1.0e-6f * calT) calS = calT; }
        recTo = recS; calTo = calS;
        recInc = (recTo - rec) * kCtrlInv; mkInc = (mkTo - mk) * kCtrlInv; calInc = (calTo - cal) * kCtrlInv;

        // Breathing: the RMS detector's reading (a square root, so per tick), ramped
        if (rmsOn)
        {
            rmsS = rmsTo;
            rmsTo = stereo(std::sqrt(rmsDet.d[0]), std::sqrt(rmsDet.d[1])) * rmsCal;
            rmsInc = (rmsTo - rmsS) * kCtrlInv;
        }

        // The carrier: Carrier knob + slow wander (Drift) + the weaker head and the dips (Tracking).
        float c = s.carrierDb + trackDb + (headB ? -s.imbalanceDb : 0.0f);
        if (s.drift > 0.0f)
        {
            // two-pole smoothed noise, about unit size; on the track most of the time, deeper now and then
            driftN1 += driftK * (rngE.nextBipolar() - driftN1);
            driftN2 += driftK * (driftN1 - driftN2);
            const float m = clamp(0.4f + 0.5f * driftN2 * driftScale, 0.0f, 1.0f);
            c -= kHfDriftDb * s.drift * m * m;
        }
        carrierAt(st[0], c);
        // Dropouts: the tape lifts off the head. Poisson, evenly in time; deep and long as above.
        if (dropLeft == 0 && dropP > 0.0f && rngE.nextFloat() < dropP)
        {
            const float video = kHfDropVideoDb - kHfDropTailDb * std::log(1.0f - rngE.nextFloat());
            dropDb = kHfDropShare * video;
            dropLeft = 1 + (int)(kHfDropMinSec * std::exp2(kHfDropOctaves * rngE.nextFloat()) * sr);
            ++drops;
        }
        if (dropLeft > 0) carrierAt(st[1], c - dropDb);
    }

    // At each head switch: the weaker head takes over or hands back; a tracking dip starts, goes
    // on to its next pass, or ends; the return from the linear track counts drum turns.
    void headSwitch()
    {
        headB = !headB;
        ++switchCount;
        if (trackPasses > 0)
        {
            if (--trackPasses > 0) trackDb = -trackDepth * (0.6f + 0.4f * rngE.nextFloat());
            else trackDb = 0.0f;
        }
        else if (trackP > 0.0f && rngE.nextFloat() < trackP)
        {
            trackDepth = kHfTrackMinDb + kHfTrackSpanDb * s.tracking * rngE.nextFloat();
            const float u = rngE.nextFloat();
            trackPasses = 1 + (int)((float)(kHfTrackPasses - 1) * u * u + 0.5f);
            trackDb = -trackDepth * (0.6f + 0.4f * rngE.nextFloat());
        }
        if (linear && lowCount == 0 && !headB && ++back >= kHfBackEdges) linear = false;
    }

    // The demodulator's output for the (limited) modulation y.
    Stereo channel(Stereo y)
    {
        const bool dip = dropLeft > 0;
        if (dip) --dropLeft;
        const Carrier& k = st[dip ? 1 : 0];
        hsPh += hsInc;
        const bool sw = hsPh >= 1.0f;
        if (sw) { hsPh -= 1.0f; headSwitch(); }
        // the hi-fi detector: 0.3 ms under its threshold and the deck goes to the linear track
        if (k.low)
        {
            back = 0;
            if (lowCount < nOn)
            {
                if (lowCount == 0 && !tapeRun) { tape.reset(); tapeRun = true; }   // starts in time to be settled
                if (++lowCount == nOn) linear = true;
            }
        }
        else lowCount = 0;

        Stereo z;
        if (k.doc) { relLeft = nRelease; z = held; }                    // the dropout canceller holds
        else if (relLeft > 0) { --relLeft; z = held; }
        else
        {
            z = y;
            if (k.noise > 0.0f)
            {
                const Stereo w = { rngL.nextBipolar(), rngR.nextBipolar() };
                Stereo dn = w - wPrev;
                wPrev = w;
                if (lpOn) dn = noiseLp.process(dn);
                z += dn * k.noise;
            }
            if (k.clickThr != 0)
            {
                // a 2 pi phase slip: one cycle's area in one sample, either sign; each carrier its own
                const uint32_t uL = rngC.next(), uR = rngC.next();
                const float a = clickUnit * s.clicks;
                if (uL < k.clickThr) { z[0] += (uL & 1u) ? a : -a; ++clickCount; }
                if (uR < k.clickThr) { z[1] += (uR & 1u) ? a : -a; ++clickCount; }
            }
            if (sw && s.headGate > 0.0f)
            {
                // the switch: the concealed residual (a pulse that follows the signal's slope), and
                // what Head Buzz leaves unconcealed: the carrier's phase step, up to half a cycle
                // either way (a step wraps at half a cycle), random
                z += (y - yPrev) * (residK * s.headGate);
                if (s.headBuzz > 0.0f)
                {
                    const float a = 0.5f * clickUnit * sq(s.headBuzz);
                    z += stereo(rngC.nextBipolar(), rngC.nextBipolar()) * a;
                }
            }
            held = z;
        }
        yPrev = y;
        return z;
    }

    // The playback side, from the demodulator's output z. The expander's gain d / Dref, d within
    // +-30 dB, from its detector before this sample (as the compressor's).
    Stereo playback(Stereo z)
    {
        const Stereo a = fmDe.inv(z) * cal;
        Stereo dp = playDet.d;
        if (rmsOn) { rmsS += rmsInc; dp += (rmsS - dp) * blend; }
        const Stereo e = vmin(vmax(dp * pRefInv, splat(1.0f / kHfGainRange)), splat(kHfGainRange));
        const Stereo w = wPlay.fwd(a);
        playDet.step(vabs(w));
        if (rmsOn) rmsDet.step(w * w);
        return nrDe.inv(a * e) * mk;
    }
    // The playback side alone, fed at the demodulator's output (where the TDA9605H's playback
    // figures are measured). For the tests.
    Stereo playOnly(Stereo z)
    {
        if (n == 0) tick();
        n = (n + 1) & (kCtrl - 1);
        rec += recInc; mk += mkInc; cal += calInc;
        return playback(z);
    }

    Stereo process(Stereo x)
    {
        if (n == 0) tick();
        n = (n + 1) & (kCtrl - 1);
        rec += recInc; mk += mkInc; cal += calInc;
        // record. The compressor's gain Dref / d (d within +-30 dB): one Newton step from the last
        // sample's value while d moves by less than 2 % (the error is the square of the move: under
        // 0.004 dB), a division otherwise
        {
            const Stereo u = vmin(vmax(recDet.d * (1.0f / kDRef), splat(1.0f / kHfGainRange)), splat(kHfGainRange));
            const Stereo p = u * cg, far = vabs(p - 1.0f);
            if (maxf(far[0], far[1]) < 0.02f) cg = cg * (2.0f - p);
            else cg = 1.0f / u;
        }
        const Stereo v = nrEmph.fwd(x * rec) * cg;
        recDet.step(vabs(wRec.fwd(v)));
        const Stereo y = vclamp(fmEmph.fwd(v), kHfClip);
        // the tape, then playback
        Stereo out = playback(channel(y));
        // the linear track, while it is heard or about to be
        if (tapeRun)
        {
            const Stereo lin = tape.process(splat(0.5f * (x[0] + x[1]))) * kFbLevel;
            linMix = linear ? minf(linMix + xfInc, 1.0f) : maxf(linMix - xfInc, 0.0f);
            out += (lin - out) * linMix;
            if (!linear && linMix == 0.0f && lowCount == 0) tapeRun = false;
        }
        return out;
    }
};

} // namespace

class VHSVibes : public mpc::Plugin
{
public:
    VHSVibes() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        addParam(floatParam ("out",       "Output",     -24.0f, 12.0f, 0.0f, "dB", 1));

        addParam(floatParam ("drive",     "Drive",        0.0f, 100.0f, 25.0f, "%", 0));
        addParam(floatParam ("tone",      "Tilt",      -100.0f, 100.0f,  0.0f, "",  0));
        addParam(floatParam ("wear",      "Slew",         0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam ("comp",      "Comp",         0.0f, 100.0f, 40.0f, "%", 0));
        addParam(logParam   ("hicut",     "Hi Cut",    1000.0f, 18000.0f, 9000.0f, "Hz", 0));

        addParam(boolParam  ("sat_on",    "Saturator",  true));
        addParam(floatParam ("lows",      "Lows",       -12.0f, 12.0f,  0.0f, "dB", 1));
        addParam(floatParam ("losat",     "Lo Sat",       0.0f, 100.0f, 20.0f, "%", 0));
        addParam(logParam   ("split",     "Split",      200.0f, 8000.0f, 2500.0f, "Hz", 0));
        addParam(floatParam ("highs",     "Highs",      -24.0f, 12.0f, -3.0f, "dB", 1));
        addParam(floatParam ("hisat",     "Hi Sat",       0.0f, 100.0f, 35.0f, "%", 0));

        addParam(floatParam ("warp",      "Warp",         0.0f, 100.0f, 35.0f, "%", 0));
        addParam(logParam   ("rpm",       "Warp Rate",    6.0f, 300.0f, 33.0f, "rpm", 0));
        addParam(floatParam ("shape",     "Shape",        0.0f, 100.0f, 30.0f, "%", 0));
        addParam(floatParam ("flutter",   "Flutter",      0.0f, 100.0f, 30.0f, "%", 0));
        addParam(logParam   ("flut_hz",   "Flut Rate",    2.0f, 30.0f,  9.0f, "Hz", 1));

        addParam(floatParam ("hum",       "Hum",          0.0f, 100.0f, 15.0f, "%", 0));
        addParam(floatParam ("buzz",      "Buzz",         0.0f, 100.0f,  0.0f, "%", 0));
        addParam(floatParam ("headbuzz",  "Head Buzz",    0.0f, 100.0f, 10.0f, "%", 0));
        addParam(floatParam ("hiss",      "Hiss",         0.0f, 100.0f, 35.0f, "%", 0));
        addParam(floatParam ("crackle",   "Crackle",      0.0f, 100.0f, 15.0f, "%", 0));
        addParam(floatParam ("noise",     "Noise",        0.0f, 100.0f, 50.0f, "%", 0));
        addParam(choiceParam("noise_pos", "Noise Pos",  {"Post", "Pre"}, 0));

        addParam(floatParam ("wow",       "Wow",          0.0f, 100.0f, 25.0f, "%", 0));
        addParam(logParam   ("wow_hz",    "Wow Rate",     0.1f, 4.0f,   0.8f, "Hz", 2));
        addParam(floatParam ("flutter2",  "Flutter 2",    0.0f, 100.0f, 20.0f, "%", 0));
        addParam(logParam   ("flut2_hz",  "Flut2 Rate",   3.0f, 40.0f, 14.0f, "Hz", 1));
        addParam(logParam   ("locut",     "Lo Cut",      20.0f, 1000.0f, 60.0f, "Hz", 0));
        addParam(logParam   ("hicut2",    "Hi Cut 2",  1000.0f, 18000.0f, 11000.0f, "Hz", 0));

        addParam(boolParam  ("cho_on",    "Chorus",     false));
        addParam(floatParam ("cho_mix",   "Cho Mix",      0.0f, 100.0f, 40.0f, "%", 0));
        addParam(logParam   ("cho_hz",    "Cho Speed",    0.05f, 5.0f,  0.6f, "Hz", 2));
        addParam(logParam   ("cho_ms",    "Cho Delay",    1.0f, 30.0f, 12.0f, "ms", 1));
        addParam(floatParam ("cho_width", "Cho Width",    0.0f, 100.0f, 70.0f, "%", 0));

        addParam(boolParam  ("mic_on",    "Mic",        false));
        addParam(choiceParam("mic_type",  "Mic Type",   {"Camcorder", "Dynamic", "Phone", "Boombox"}, 0));
        addParam(floatParam ("mic_gain",  "Mic Gain",   -12.0f, 12.0f, 0.0f, "dB", 1));

        addParam(boolParam  ("mono",      "Mono",       false));
        addParam(floatParam ("dropouts",  "Dropouts",     0.0f, 100.0f, 0.0f, "%", 0));

        addParam(boolParam  ("wobble_on", "Wobble On",  true));
        addParam(boolParam  ("noise_on",  "Noise On",   true));

        // Meters for the screen: not automatable, so the MPC never records them.
        Param reel  = floatParam("vis_reel",  "Reel",  0.0f, 1.0f, 0.0f, "", 2);
        Param level = floatParam("vis_level", "Level", 0.0f, 1.0f, 0.0f, "", 2);
        reel.automatable = false;
        level.automatable = false;
        addParam(reel);
        addParam(level);

        addParam(boolParam  ("deck_on",    "Deck On",    true));
        addParam(floatParam ("agc",        "AGC",          0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam ("drift",      "Drift",        0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam ("tracking",   "Tracking",     0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam ("breathing",  "Breathing",    0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam ("mix",        "Mix",          0.0f, 100.0f, 100.0f, "%", 0));
        addParam(boolParam  ("animate",    "Animation",  true));

        // 1.4. Both rest at 0, where the plugin sounds as it did before they existed.
        addParam(floatParam ("scrape",     "Scrape",       0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam ("whistle",    "Whistle",      0.0f, 100.0f, 0.0f, "%", 0));
        addParam(boolParam  ("hq",         "HQ",         true));    // on in a new instance since 1.5
        addParam(choiceParam("nr",         "B-Type NR",  {"Off", "Rec", "Play", "On"}, 0));
        addParam(choiceParam("sat_type",   "Sat Type",   {"Simple", "VHS", "Hi-Fi"}, 0));   // Hi-Fi added in 1.7; Simple was 2-Band up to 1.6
        addParam(choiceParam("mic_model",  "Mic Model",  {"Simple", "Modeled"}, 1));   // Simple in a state from before 1.6
        addParam(choiceParam("agc_model",  "AGC Model",  {"Simple", "Modeled"}, 1));   // the same

        // 1.7: the picture effects switch, then their meters (not automatable, as Reel and Level)
        addParam(boolParam  ("visual_vibes", "Visual Vibes", true));
        const char* const meters[][2] = { {"vis_snow", "Snow"}, {"vis_head", "Head"}, {"vis_streak", "Streak"},
                                          {"vis_grid", "Grid"}, {"vis_haze", "Haze"} };
        for (const auto& m : meters)
        {
            Param meter = floatParam(m[0], m[1], 0.0f, 1.0f, 0.0f, "", 2);
            meter.automatable = false;
            addParam(meter);
        }

        if (numParams() != kNumParams) mpc::log("VHSVibes: parameter count mismatch");

        // The MPC shows only the value text, never the unit, so the unit is written into the
        // text by formatValue() below. Remember each unit and clear the separate label so no
        // host shows it twice.
        for (int i = 0; i < numParams() && i < kNumParams; ++i)
        {
            const std::string& u = params_[(size_t)i].unit;
            units_[i] = u == "%" ? 1 : u == "dB" ? 2 : u == "Hz" ? 3 : u == "ms" ? 4 : u == "rpm" ? 5 : 0;
            params_[(size_t)i].unit.clear();
        }

        addPrograms();

        // The delay lines have fixed sizes, so they are made here once and never again.
        wf1Delay_.prepare(kTapeDelaySamples);   // the longest delay read is kTapeDelaySamples - 7
        wf2Delay_.prepare(kTapeDelaySamples);
        dryDelay_.prepare(kTapeDelaySamples);
        choDelay_.prepare(2 * kChorusDelaySamples);   // the delay time plus its swing

        // See notifierMain(): tells the host about program changes, the meters and the latency.
        // If the system cannot start a thread, the plugin runs without it (the sound is the
        // same; the host is then not told about those three) rather than throwing into the host.
        try { notifier_ = std::thread(&VHSVibes::notifierMain, this); }
        catch (...) { mpc::log("VHSVibes: could not start the helper thread; no meters or latency reports"); }
    }

    ~VHSVibes() override
    {
        goQuiet();          // first: no new calls into a host that is closing us
        {
            // under the lock, so the notifier cannot miss the wake-up between its look at quit_
            // and its wait: it wakes at once instead of finishing its 50 ms
            std::lock_guard<std::mutex> lock(quitMutex_);
            quit_.store(true);
        }
        quitCv_.notify_all();
        if (notifier_.joinable()) notifier_.join();
    }

    // Suspended (or closing): the notifier thread stops talking to the host until the plugin
    // is resumed and has been reset. Any thread.
    void goQuiet() { suspended_.store(true); started_.store(false); }
    void resume()  { suspended_.store(false); resetPending_.store(true); }
    // The host is about to write a whole program or state, and has finished (see process()).
    // loadState_ holds two counts in one word, so the audio thread reads both at once: in the
    // low 8 bits the loads under way, above them one that moves at every start and every end.
    // A load that took (`took`) asks for its reset before it counts as finished.
    void beginLoad()
    {
        loadState_.fetch_add(kLoadEvent + 1u, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);   // before any value of the load is written
    }
    void endLoad(bool took)
    {
        if (took) reset();
        loadState_.fetch_add(kLoadEvent - 1u, std::memory_order_release);
    }

    // A program is about to be chosen: it takes over the Animation, HQ and Visual Vibes switches
    // as they stand, so they do not move. The picture effects' meters (1.7) likewise: a program
    // never moves what the picture shows. (The host's thread, the same one that then loads the program.)
    void holdSwitches(int program)
    {
        if (program < 0 || program >= (int)programs_.size()) return;
        std::vector<float>& v = programs_[(size_t)program].plainValues;
        for (int i : {(int)kAnimate, (int)kHq, (int)kVisualVibes}) v[(size_t)i] = get(i);
        for (int i = kVisSnow; i < kNumParams; ++i) v[(size_t)i] = get(i);
    }

    // The host announces the sample rate here, on its own thread and possibly while audio is
    // running. Nothing is worked out here: the audio thread picks the rate up at its next reset.
    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        srWanted_.store((float)sampleRate);
        tellHost_.store(true);
        resetPending_.store(true);
    }

    // The host asks for a reset after a program change or a state load (and on resume), from
    // whichever thread it likes. The audio thread does the work between two blocks, so it never
    // runs while process() is in the middle of one; and since sound may be playing, it fades
    // the output out first (see process() and fadeOut_). prepare() and a resume ask for theirs
    // through resetPending_, which is done at once.
    void reset() override { tellHost_.store(true); resetWanted_.store(true); }

private:
    // Everything that depends on the sample rate alone.
    void setRate()
    {
        // Hi-Fi noise reduction, per control tick: 5 ms up, 70 ms back (the VHS specification)
        breathAtk_  = 1.0f - std::exp(-(float)kCtrl / (0.005f * sr_));
        breathRel_  = 1.0f - std::exp(-(float)kCtrl / (0.070f * sr_));
        scrapeLo_   = lpCoeff(600.0f, sr_);     // scrape flutter lives between these two
        scrapeHi_   = lpCoeff(3000.0f, sr_);
        // line whistle: a sine from two state values (see process). No whistle at sample
        // rates that cannot carry it.
        whistleE_   = kLineHz < 0.45f * sr_ ? 2.0f * std::sin(kPi * kLineHz / sr_) : 0.0f;
        whistleNorm_ = std::sqrt(1.0f - 0.25f * whistleE_ * whistleE_);
        tiltCoeff_  = lpCoeff(800.0f, sr_);
        humInc_  = 60.0f / sr_;
        buzzInc_ = 120.0f / sr_;
        headInc_ = 59.94f / sr_;
        hsInc_   = 59.94f / sr_;
        agcHoldTicks_ = (int)(1.5f * sr_ * kCtrlInv);
        micHoldTicks_ = (int)(0.020f * sr_ * kCtrlInv) + 1;
        micHoldCamTicks_ = (int)(0.100f * sr_ * kCtrlInv) + 1;
        agcRelease_   = 1.0f / (2.5f * sr_ * kCtrlInv);
        agcm_.prepare(sr_);
        // AGC Model switched: the new one starts from rest and settles unheard, then the sound
        // crossfades to it in 100 ms. The Modeled one takes a second (from rest, on a 1 kHz
        // tone, the gain comes within 1 dB of its steady value after 0.73 s at 8 dB over the
        // nominal level, 0.85 s at it, 1.3 s at 5 dB under it), the Simple one 20 ms (its
        // attack is at once).
        agcSettleTicks_ = (int)(1.0f * sr_ * kCtrlInv) + 1;
        agcSettleSimpleTicks_ = (int)(0.020f * sr_ * kCtrlInv) + 1;
        agcXfStep_      = (float)kCtrl / (0.100f * sr_);
        edgeCoeff_    = lpCoeff(1200.0f, sr_);
        fbLp_.setCoeffs(BiquadCoeffs::lowpass(sr_, 5000.0f, 0.7071f));   // the backup (linear) track is dull
        fbHpCoeff_    = lpCoeff(100.0f, sr_);                            // and thin
        nrEnc_.prepare(sr_, false);
        nrDec_.prepare(sr_, true);
        nrFade_ = 1.0f / (0.008f * sr_);                                 // NR comes in and goes out over 8 ms
    }

    // HQ on or off: the rate the distorting stages run at, and everything that follows from it.
    void setQuality()
    {
        hq_   = getBool(kHq);
        nlSr_ = hq_ ? 2.0f * sr_ : sr_;
        envAttack_  = 1.0f - std::exp(-1.0f / (0.003f * nlSr_));   // the compressor detector
        envRelease_ = 1.0f - std::exp(-1.0f / (0.150f * nlSr_));
        compSmooth_ = hq_ ? 0.01005f : 0.02f;                      // and its gain smoothing, per sample
        if (hifiPath_.sr != nlSr_) hifiPath_.prepare(nlSr_);       // Sat Type = Hi-Fi runs in the saturator's place
    }

    // Clears every memory and jumps all smoothed values to the current settings.
    void doReset()
    {
        const float sr = srWanted_.load(std::memory_order_relaxed);
        if (sr <= 0.0f) return;
        if (sr != sr_) { sr_ = sr; setRate(); }
        setQuality();
        hbUp_.reset(); hbDown_.reset(); hbDry_.reset();
        fadeOut_ = 0; resetNextTick_ = false;
        // All four lines are emptied, not only rewound: in the first moments after a reset the
        // readers reach back past what has been written since (the resting length, the swing,
        // the dry copy's crossfade a knob move can start), and the filters after them carry
        // whatever they read on into the fade-in. Left in place, the old sound would be heard.
        wf1Delay_.reset(); wf2Delay_.reset(); choDelay_.reset(); dryDelay_.reset();
        simple_.reset(); mic_.reset();
        lpfA_.reset(); lpfB_.reset(); hiCut2_.reset();
        tiltLp_ = slew_ = splitLp_ = loCutLp_ = hissLp_ = click_ = splat(0.0f);
        env_ = 0.0f; compGain_ = 1.0f; compTarget_ = 1.0f;
        humPh_ = buzzPh_ = headPh_ = 0.0f;
        buzzLp_ = headLp_ = headHp_ = 0.0f;
        choPhase_ = 0.0f;
        dropGain_ = 1.0f; dropTarget_ = 1.0f; dropTicks_ = 0;
        ctrlCount_ = 0;
        inPeak_ = outPeak_ = inEnv_ = outEnv_ = breathEnv_ = 0.0f;
        inMax_ = splat(0.0f);
        scrapeA_ = scrapeB_ = 0.0f;
        whistleS_ = 0.0f; whistleC_ = 1.0f;
        fbLp_.reset();
        fbHp_ = edgeLp_ = dropLp_ = hsHeld_ = splat(0.0f);
        agcEnv_ = kRefLevel; agcGain_ = agcTarget_ = 1.0f; agcHold_ = 0; agcAmt_ = 0.0f; agcActive_ = false;
        agcm_.reset(); agcMRun_ = agcBlend_ = false; agcSettle_ = 0; agcBInc_ = 0.0f;   // (agcB_: see readParams)
        edgeN1_ = edgeN2_ = edgeMuffle_ = 0.0f; edgeGain_ = 1.0f; edgeActive_ = false;
        hsPhase_ = 0.0f; hsLeft_ = 0; hsFrac_ = 0.0f; hsOn_ = false;
        fbState_ = 0; fbTicks_ = 0; staticAmp_ = 0.0f; fbMono_ = 0.0f;
        nrEnc_.reset(); nrDec_.reset();
        tapeSat_.reset();

        for (int i = 0; i < kNumParams; ++i) lastNorm_[i] = getNormalised(i);
        readParams(gAll);
        nrEncMix_ = nrEncOn_ ? 1.0f : 0.0f; nrDecMix_ = nrDecOn_ ? 1.0f : 0.0f;
        nrEncRun_ = nrEncOn_; nrDecRun_ = nrDecOn_;
        linAmt_ = linTarget_ = 0.0f;
        hissDyn_ = 1.0f;
        headAmp_ = tgt_[cHead];
        for (int i = 0; i < kNumCtl; ++i) cur_[i] = tgt_[i];
        for (int i = 0; i < kNumRamp; ++i) { rampS_[i] = rampT_[i]; ramp_[i].snap(rampT_[i]); }

        // Every delay starts at the resting length its settings need, so nothing glides (and
        // bends the pitch) after a program change.
        const float wob = tgt_[cWobbleOn];
        wf1_.update(sr_, dev1Slow_ * wob, rate1Slow_, dev1Fast_ * wob, rate1Fast_);
        wf2_.update(sr_, dev2Slow_ * wob, rate2Slow_, dev2Fast_ * wob, rate2Fast_);
        wf1_.reset(0x1234abcdu);
        wf2_.reset(0x0badf00du);
        dryD_.snap(wf1_.base + wf2_.base);
        dryActive_ = rampT_[rMix] < 1.0f;
        dryStill_ = false;
        dryHold_ = choHold_ = 0;
        dryJump_ = dryXf_ = 0.0f; dryFill_ = 1 << 20;
        choBase_ = choDelayTarget_;
        choD_[0].snap(choBase_); choD_[1].snap(choBase_);
        choActive_ = rampT_[rChorus] > 0.0f;
        micRun_ = rampT_[rMic] > 0.0f; micHold_ = 0;
        hifiRun_ = hifiType_ && rampT_[rSat] > 0.0f;   // (Hi-Fi: from rest, see controlTick)
        hifiPath_.reset();

        // The delays were just emptied. Keep the output shut until the sound has come through
        // them (their resting length), then fade in over about 20 ms instead of letting it cut in.
#ifndef VHS_VIBES_NO_FADE_IN   // (set for comparison builds only: see VHS_VIBES_SWING_ROOM)
        rampS_[rOut] = 0.0f;
        ramp_[rOut].snap(0.0f);
        fadeHold_ = (int)((wf1_.base + wf2_.base + (choActive_ ? choBase_ : 0.0f)) * kCtrlInv) + 1;
        if (micRun_ && !micSimple_) fadeHold_ += micHoldFor();   // the model starts from rest too: it settles unheard (see controlTick)
        // The chorus also swings: on the first tick one side's delay moves up to its depth (the
        // one controlTick gives it) further back, and until the sound has got there that side
        // would come in with a step in the middle of the fade-in. So the output waits those few
        // ticks longer (choWait_) and then rises to the fade-in in 8 ticks (gate_). The fade-in
        // itself keeps its own time, so once the gate is open the sound is as it always was.
        if (choActive_)
        {
            const float swing = minf(0.4f * choBase_, 0.004f * sr_);
            choWait_ = (int)((wf1_.base + wf2_.base + choBase_ + swing) * kCtrlInv) + 1 - fadeHold_;
        }
        else choWait_ = 0;
        gateOn_ = choWait_ > 0; gateS_ = 0.0f; gate_.snap(0.0f);
#else
        fadeHold_ = 0; choWait_ = 0; gateOn_ = false;
#endif

        // A reset follows the host starting the plugin and every program change and state
        // load: from now on the notifier thread may talk to the host, and after a reset the
        // host asked for it should send the new values. (Only flags here.) The host may suspend
        // us on its own thread at this very moment (goQuiet: suspended_, then started_). So
        // started_ is set first and suspended_ looked at after: a goQuiet that comes in between
        // is seen and undone here, one that comes later clears started_ itself. (Looking first
        // and setting after could set started_ again just after goQuiet had cleared it.)
        started_.store(true);
        if (suspended_.load()) started_.store(false);
        if (tellHost_.exchange(false)) pushPending_.store(true, std::memory_order_relaxed);
    }

    // The stages that distort: preamp clip, Slew, the compressor with the clip on its boost,
    // and the 2-band saturator. Once per sample, or twice per sample in HQ (their per-sample
    // values are then set for the doubled rate: see setQuality, slewStep_, splitCoeff_).
    __attribute__((always_inline)) Stereo shape(Stereo x, float satMix)
    {
        if (driveOn_) x = driveSat(x, cur_[cDrvG], cur_[cDrvMk], cur_[cDrvMix]);

        // ----- wear: slew limiter ----------------------------------------
        if (slewOn_)
        {
            const Stereo step = splat(slewStep_);
            slew_ += vmax(vmin(x - slew_, step), -step);
            x = slew_;
        }

        // ----- compressor (gain target is set in controlTick) ------------
        const Stereo mag = vabs(x);
        const float peak = maxf(mag[0], mag[1]);
        env_ += (peak > env_ ? envAttack_ : envRelease_) * (peak - env_);
        compGain_ += compSmooth_ * (compTarget_ - compGain_);
        x *= compGain_ * cur_[cMakeup];
        // The AGC and the make-up gain turn quiet sound up; a sudden loud hit gets that gain
        // for a few milliseconds before they react. It is clipped here, as the recorder's
        // own input would.
        if (boost_) x = vclamp(x, kBoostCeil);

        // ----- 2-band saturator ------------------------------------------
        if (satMix > 0.0f && tape_)
        {
            const Stereo y = tapeSat_.process(x);
            splitLp_ += splitCoeff_ * (y - splitLp_);
            x += (splitLp_ * cur_[cLowsG] + (y - splitLp_) * cur_[cHighsG] - x) * satMix;
        }
        else if (satMix > 0.0f && !hifiRun_)
        {
            splitLp_ += splitCoeff_ * (x - splitLp_);
            Stereo lo = splitLp_ * cur_[cLowsG];
            Stereo hi = (x - splitLp_) * cur_[cHighsG];
            if (loSatOn_) lo = driveSat(lo, cur_[cLoG], cur_[cLoMk], cur_[cLoMix]);
            if (hiSatOn_) hi = driveSat(hi, cur_[cHiG], cur_[cHiMk], cur_[cHiMix]);
            x += (lo + hi - x) * satMix;
        }
        else if (satMix > 0.0f) x = hifiShape(x, satMix);
        return x;
    }

    // Sat Type = Hi-Fi for one sample (at the rate the stage runs at): Lows and Highs trim the two
    // halves around 1 kHz before the deck (EQ in front of the recorder), then the FM path. Out of
    // line, like agcModeled below.
    __attribute__((noinline)) Stereo hifiShape(Stereo x, float satMix)
    {
        splitLp_ += splitCoeff_ * (x - splitLp_);
        const Stereo e = splitLp_ * cur_[cLowsG] + (x - splitLp_) * cur_[cHighsG];
        return x + (hifiPath_.process(e) - x) * satMix;
    }

    // The Modeled AGC for one sample, with the Simple one beside it while AGC Model is switched
    // (agcB_: how much of the Modeled one is heard; the Simple one's lines as in process()). Out
    // of line, like MicModel::process: the sample loop stays as it was for the Simple AGC.
    __attribute__((noinline)) Stereo agcModeled(Stereo x)
    {
        Stereo simple = x;
        if (agcActive_)
        {
            const Stereo a = vabs(x);
            const float pk = maxf(a[0], a[1]);
            if (pk > agcEnv_)
            {
                agcEnv_ = pk;
                agcHold_ = agcHoldTicks_;
                agcTarget_ = 1.0f + agcAmt_ * (clamp(kRefLevel / pk, 0.25f, 5.0f) - 1.0f);
            }
            agcGain_ += (agcTarget_ < agcGain_ ? 0.25f : 0.002f) * (agcTarget_ - agcGain_);
            simple = x * agcGain_;
        }
        const float g = agcMRun_ ? agcm_.step(x) : 1.0f;
        if (agcBInc_ == 0.0f && agcB_ == 1.0f) return x * g;
        agcB_ += agcBInc_;
        return simple + (x * g - simple) * agcB_;
    }

public:
    void process(mpc::ProcessContext& ctx) override
    {
        // Read first: a load that has finished by now has also asked for its reset (endLoad),
        // and the acquire makes that request visible to the check just below.
        const uint32_t load = loadState_.load(std::memory_order_acquire);

        if (resetPending_.load(std::memory_order_relaxed) && resetPending_.exchange(false))
        {
            resetWanted_.store(false);   // this one serves a waiting request too
            doReset();
        }
        else if (resetWanted_.load(std::memory_order_relaxed) && resetWanted_.exchange(false))
            fadeOut_ = kFadeTicks;       // a program change or state load: fade out, then reset

        // Once per block: recompute the values of whichever groups had a parameter move. Not
        // while the host is writing a program or a state, nor while the output fades out for
        // the reset that follows: the old sound keeps its old settings until then, and the
        // reset reads every parameter.
        //
        // The host's thread may begin or finish writing a program or a state at any moment,
        // also while this loop reads the values. So what the loop saw counts only if no load
        // was under way when it began and none began or ended while it ran (loadState_ is the
        // same after it: see beginLoad / endLoad). Otherwise nothing is taken in and the values
        // stay "unseen", for the next block or for the reset that follows every load. Without
        // that, a block could apply half a program, or a whole one, to the old sound before its
        // fade-out had begun.
        if (fadeOut_ == 0 && !resetNextTick_ && (load & (kLoadEvent - 1u)) == 0)
        {
            float seen[kNumParams];
            uint32_t dirty = 0;
            for (int i = 0; i < kNumParams; ++i)
            {
                const float v = seen[i] = getNormalised(i);
                // A Q-Link turned across a switch sends every value on the way: only a change
                // of the position counts (or Type and HQ would restart the sound at each one).
                if (v != lastNorm_[i] && param(i).toPlain(v) != param(i).toPlain(lastNorm_[i])) dirty |= groupOf(i);
            }
            // If any value read above was written after a load began, this fence makes that
            // load's start visible below (it pairs with the fence in beginLoad).
            std::atomic_thread_fence(std::memory_order_acquire);
            if (loadState_.load(std::memory_order_relaxed) == load)
            {
                const auto plain = [&](int i) { return param(i).toPlain(seen[i]); };
                // Sat Type alone, while the Saturator is off and has faded out completely: none
                // of the stage is heard, so the type changes in place (the stage starts from rest
                // when it comes back in) and the sound plays on.
                if ((dirty & gQuality) && plain(kHq) == param(kHq).toPlain(lastNorm_[kHq])
                    && plain(kSatOn) < 0.5f && rampT_[rSat] == 0.0f && ramp_[rSat].v == 0.0f)
                {
                    tape_ = std::lround(plain(kSatType)) == 1;
                    hifiType_ = std::lround(plain(kSatType)) == 2;   // (not heard with the Saturator off: see readParams)
                    hifiRun_ = false;                                // (silent now; controlTick starts it from rest)
                    tapeSat_.reset();
                    splitLp_ = splat(0.0f);
                    dirty = (dirty & ~gQuality) | gSat;
                    pushPending_.store(true);   // the three read-outs change: have the host ask again
                }
                std::memcpy(lastNorm_, seen, sizeof(lastNorm_));
                if (dirty & gQuality) fadeOut_ = kFadeTicks;   // HQ or Sat Type switched: fade out, then start again
                if (dirty) readParams(dirty);
            }
        }
        playing_ = ctx.transport.valid && ctx.transport.playing;

        const float* inL = (ctx.inputs != nullptr && ctx.numInputs > 0) ? ctx.inputs[0] : nullptr;
        const float* inR = (ctx.inputs != nullptr && ctx.numInputs > 1) ? ctx.inputs[1] : inL;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;

        Stereo inSum = splat(0.0f);
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            const bool tick = ctrlCount_ == 0;
            if (tick) controlTick();
            ctrlCount_ = (ctrlCount_ + 1) & (kCtrl - 1);

            // Read this frame's input before anything is written (in-place buffers).
            const float l = inL != nullptr ? inL[i] : 0.0f;
            const float r = inR != nullptr ? inR[i] : l;
            if (tick) inPeak_ = std::fabs(l) + std::fabs(r);   // the meters look at one sample in 16
            Stereo x = stereo(l, r);
            inSum += x;                                        // see the end of process()
            if (breathOn_) inMax_ = vmax(inMax_, vabs(x));     // what the noise reduction hears
            float dryLen = 0.0f;
            if (dryActive_) { dryDelay_.write(hq_ ? hbDry_.trip(x) : x); dryLen = dryD_.next(); }   // kept for Mix below 100 %

            float outGain = ramp_[rOut].next();
            if (gateOn_) outGain *= gate_.next();   // a moment after a reset only (the chorus swing)
            const float mix     = ramp_[rMix].next();
            const float monoMix = ramp_[rMono].next();
            const float choMix  = ramp_[rChorus].next();
            const float micMix  = ramp_[rMic].next();
            const float satMix  = ramp_[rSat].next();

            // ----- noise generators (each one only while its own level is up) ------
            Stereo noise = splat(0.0f);
            if (noiseActive_)
            {
                float mono = 0.0f;
                if (humOn_)
                {
                    humPh_ += humInc_; if (humPh_ >= 1.0f) humPh_ -= 1.0f;
                    mono = psin(humPh_) * cur_[cHum];
                }
                if (buzzOn_)
                {
                    buzzPh_ += buzzInc_; if (buzzPh_ >= 1.0f) buzzPh_ -= 1.0f;
                    buzzLp_ += 0.22f * ((2.0f * buzzPh_ - 1.0f) - buzzLp_);
                    mono += buzzLp_ * cur_[cBuzz];
                }
                if (headOn_)
                {
                    // Head switching: one tick per video field, the two heads slightly unequal.
                    headPh_ += headInc_;
                    float pulse = 0.0f;
                    if (headPh_ >= 1.0f) { headPh_ -= 1.0f; headFlip_ = !headFlip_; pulse = headFlip_ ? 4.0f : -2.8f; }
                    headLp_ += 0.30f * (pulse - headLp_);
                    headHp_ += 0.05f * (headLp_ - headHp_);
                    mono += (headLp_ - headHp_) * headAmp_;
                }
                noise = splat(mono);
                if (hissOn_)
                {
                    // hiss: different in left and right
                    const Stereo white = stereo(hissRng_[0].nextBipolar(), hissRng_[1].nextBipolar());
                    hissLp_ += 0.1f * (white - hissLp_);
                    noise += (white - hissLp_) * (cur_[cHiss] * hissDyn_);
                }
                if (crackleOn_)
                {
                    // crackle: a click now and then, mostly on one side
                    Stereo imp = splat(0.0f);
                    const uint32_t u = crackleRng_.next();
                    if (u < crackleThresh_)
                    {
                        float a = crackleRng_.nextBipolar();
                        a = a * a * a * 2.5f;
                        imp = (u & 1u) ? stereo(a, a * 0.4f) : stereo(a * 0.4f, a);
                    }
                    click_ += 0.4f * (imp - click_);
                    noise += click_ * cur_[cCrackle];
                }
            }
            if (noisePre_) x += noise;

            // ----- chorus (each side has its own delay time) -----------------
            if (choActive_)
            {
                choDelay_.write(x);
                const float dL = choD_[0].next(), dR = choD_[1].next();
                if (choMix > 0.0f)
                {
                    const Stereo wet = stereo(choDelay_.read(dL, 0), choDelay_.read(dR, 1));
                    x += (wet - x) * (0.5f * choMix);
                }
            }

            // ----- mic ---------------------------------------------------------
            // (Simple: Mic Gain after the curve, and its filters only while the mic is heard, as
            // up to 1.5. Modeled: Mic Gain is how loud the sound reaches the microphone.)
            if (micRun_ && (!micSimple_ || micMix > 0.0f))
            {
                const Stereo m = micSimple_ ? simple_.process(x) * cur_[cMicGain] : mic_.process(x * cur_[cMicGain], cur_[cMicNoise]);
                x += (m - x) * micMix;
            }

            // ----- deck: automatic level control -----------------------------
            // Simple: a new peak pulls the gain down at once; controlTick() holds it there and
            // then lets it creep back up. Modeled: see ModeledAgc and agcModeled().
            if (__builtin_expect(agcBlend_, 0)) x = agcModeled(x);   // (the Simple one alone: below, as up to 1.5)
            else if (agcActive_)
            {
                const Stereo a = vabs(x);
                const float pk = maxf(a[0], a[1]);
                if (pk > agcEnv_)
                {
                    agcEnv_ = pk;
                    agcHold_ = agcHoldTicks_;
                    agcTarget_ = 1.0f + agcAmt_ * (clamp(kRefLevel / pk, 0.25f, 5.0f) - 1.0f);
                }
                agcGain_ += (agcTarget_ < agcGain_ ? 0.25f : 0.002f) * (agcTarget_ - agcGain_);
                x *= agcGain_;
            }

            // ----- preamp: tilt tone, then soft clip (each only when it does something) -----
            if (tiltOn_)
            {
                tiltLp_ += tiltCoeff_ * (x - tiltLp_);
                x = tiltLp_ * cur_[cTiltLo] + (x - tiltLp_) * cur_[cTiltHi];
            }
            else tiltLp_ = x;   // stays with the sound, so Tilt can come in without a click
            if (nrEncRun_)
            {
                if (nrEncSeed_) { nrEnc_.seed(x); nrEncSeed_ = false; }
                if (nrEncMix_ != nrEncGoal_)
                {
                    nrEncMix_ = clamp(nrEncMix_ + (nrEncOn_ ? nrFade_ : -nrFade_), 0.0f, 1.0f);
                    nrEncRun_ = nrEncOn_ || nrEncMix_ > 0.0f;      // faded out: done
                }
                x = nrEnc_.encode(x, nrEncMix_);
            }
            // ----- drive, wear, compressor, saturator: the stages that distort ----
            if (hq_)
            {
                // HQ: they run at twice the sample rate, so what they add above 22 kHz is
                // filtered off instead of folding back down into the sound.
                Stereo a, b;
                hbUp_.up(x, a, b);
                a = shape(a, satMix);
                b = shape(b, satMix);
                x = hbDown_.down(a, b);
            }
            else x = shape(x, satMix);

            // ----- 4-pole high cut -------------------------------------------
            x = lpfB_.process(lpfA_.process(x));

            // ----- wobble 1 --------------------------------------------------
            // With no wobble the delay sits on a whole sample: one load instead of the
            // four-point interpolation.
            wf1Delay_.write(x);
            if (scrapeOn_)
            {
                // Scrape flutter: the tape sticks and slips along its path far faster than any
                // wobble. Noise between 600 Hz and 3 kHz shakes the read position by a fraction
                // of a sample, which puts a roughness around every bright sound.
                scrapeA_ += scrapeHi_ * (scrapeRng_.nextBipolar() - scrapeA_);
                scrapeB_ += scrapeLo_ * (scrapeA_ - scrapeB_);
                x = wobbleRead(wf1Delay_, wf1_, (scrapeA_ - scrapeB_) * cur_[cScrape]);
            }
            else if (wf1_.still) x = wf1Delay_.tap((int)wf1_.delay);
            else x = wobbleRead(wf1Delay_, wf1_, 0.0f);

            if (!noisePre_) x += noise;

            // ----- wobble 2 and its filters ----------------------------------
            wf2Delay_.write(x);
            if (wf2_.still) x = wf2Delay_.tap((int)wf2_.delay);
            else x = wobbleRead(wf2Delay_, wf2_, 0.0f);

            loCutLp_ += loCutCoeff_ * (x - loCutLp_);
            x -= loCutLp_;
            x = hiCut2_.process(x);

            // ----- deck: drift -----------------------------------------------
            // The sound slowly comes and goes: quieter and duller, then back (controlTick moves
            // edgeMuffle_ and edgeGain_).
            if (edgeActive_)
            {
                edgeLp_ += edgeCoeff_ * (x - edgeLp_);
                x = (x + (edgeLp_ - x) * edgeMuffle_) * edgeGain_;
            }

            // ----- deck: tracking, the backup track --------------------------
            // linAmt_ jumps to 1 while a mistracking Hi-Fi deck falls back to its linear track,
            // which is dull and thin (and mono, see fbMono_ below).
            if (linAmt_ > 0.0f || linTarget_ > 0.0f)
            {
                linAmt_ += 0.01f * (linTarget_ - linAmt_);
                Stereo y = fbLp_.process(x);
                fbHp_ += fbHpCoeff_ * (y - fbHp_);
                y -= fbHp_;
                x += (y * kFbLevel - x) * linAmt_;
            }

            // ----- head switching (the part of Head Buzz that rides on the music) -----
            // Once per video field the next head takes over and the signal is held for a few
            // samples. The step that leaves is as big as the music is fast, so the buzz rides
            // on bright sounds and vanishes on bass, as on a real deck.
            if (hsOn_)
            {
                hsPhase_ += hsInc_;
                if (hsPhase_ >= 1.0f)
                {
                    hsPhase_ -= 1.0f;
                    hsFlip_ = !hsFlip_;
                    const float len = hsLen_[hsFlip_ ? 1 : 0];     // in samples, e.g. 2.4
                    hsLeft_ = (int)len + 1;
                    hsFrac_ = len - (float)(int)len;               // weight of the last, partial sample
                }
                if (hsLeft_ > 0)
                {
                    x += (hsHeld_ - x) * (hsLeft_ == 1 ? hsFrac_ : 1.0f);
                    --hsLeft_;
                }
                else hsHeld_ = x;
            }
            // static while the Hi-Fi carrier drops out (staticL_ is 0 when only the right one does)
            if (staticAmp_ > 0.0f)
                x += stereo(staticRng_.nextBipolar() * staticL_, staticRng_.nextBipolar()) * staticAmp_;

            // ----- line whistle ----------------------------------------------
            // A steady sine from two state values that turn around each other: no table, no sin().
            // With the NR playback side running it is added after that instead (below): the
            // set is not on the tape, so the decoder has no business turning it down. Without
            // NR it stays here, as in 1.4.
            const bool whistleLate = nrDecRun_;
            float whistle = 0.0f;
            if (whistleOn_)
            {
                whistleS_ += whistleE_ * whistleC_;
                whistleC_ -= whistleE_ * whistleS_;
                whistle = whistleS_ * cur_[cWhistle];
                if (!whistleLate) x += whistle;
            }

            // ----- dropouts, mono, mix, output -------------------------------
            // A dropout is the tape lifting off the head: the highs go first and furthest
            // (to nothing), the lows only dip.
            if (dropOn_)
            {
                dropGain_ += 0.012f * (dropTarget_ - dropGain_);
                dropLp_ += edgeCoeff_ * (x - dropLp_);
                x = dropLp_ * (0.25f + 0.75f * dropGain_) + (x - dropLp_) * (dropGain_ * dropGain_);
            }
            if (nrDecRun_)
            {
                if (nrDecSeed_) { nrDec_.seed(x); nrDecSeed_ = false; }
                const Stereo plain = nrDec_.decode(x);
                if (nrDecMix_ == 1.0f && nrDecOn_) x = plain;
                else
                {
                    nrDecMix_ = clamp(nrDecMix_ + (nrDecOn_ ? nrFade_ : -nrFade_), 0.0f, 1.0f);
                    nrDecRun_ = nrDecOn_ || nrDecMix_ > 0.0f;
                    x += (plain - x) * nrDecMix_;
                }
            }
            if (whistleLate) x += whistle;
            const float monoAmt = maxf(monoMix, fbMono_);
            if (monoAmt > 0.0f) x += (splat(0.5f * (x[0] + x[1])) - x) * monoAmt;
            if (mix < 1.0f && dryActive_)
            {
                // dry / wet: the dry signal waits as long as the tape path does at rest
                // (a whole number of samples when nothing wobbles: no interpolation then)
                Stereo dry = dryStill_ ? dryDelay_.tap((int)dryLen) : dryDelay_.read(dryLen);
                if (dryJump_ != 0.0f)
                {
                    dry += (dryDelay_.read(dryLen + dryJump_) - dry) * dryXf_;
                    dryXf_ += wf1_.xfInc;
                    if (dryXf_ >= 1.0f) { dryD_.snap(dryD_.v + dryJump_); dryJump_ = 0.0f; }
                }
                x = dry + (x - dry) * mix;
            }
            x *= outGain;

            outL[i] = x[0];
            if (outR != nullptr) outR[i] = x[1];
            if (tick) { const Stereo outMag = vabs(x); outPeak_ = 0.5f * (outMag[0] + outMag[1]); }
        }

        // A NaN or an infinity from the host would go round in the filters for ever (and Slew
        // can turn one into a stuck silence that looks healthy). The sum of the input shows
        // either: such a block comes out silent and the next one starts clean.
        if (!(std::fabs(inSum[0] + inSum[1]) < 1.0e30f))
        {
            std::memset(outL, 0, sizeof(float) * (size_t)ctx.numFrames);
            if (outR != nullptr) std::memset(outR, 0, sizeof(float) * (size_t)ctx.numFrames);
            resetPending_.store(true);   // (our own reset: nothing the host needs to hear about)
        }

        // Hand the meter values to the notifier thread.
        visReel_.store(reelPhase_, std::memory_order_relaxed);
        visLevel_.store(outEnv_, std::memory_order_relaxed);
        // And what the picture effects follow (1.7; the notifier turns it into frames): the hiss as
        // it is made (its RMS: the hiss stage's with Breathing, or the FM channel's at the carrier
        // it has now), how far the head switch shows (0..1, 1 at Head Buzz 100 % and Noise 50 %;
        // nothing on the linear track), how many dropouts so far, and how many times the Hi-Fi
        // track was lost so far (x 2, + 1 while it is lost). The counters are kept where the events
        // start; nothing here changes the sound. (0.533 and 0.816: the RMS of the noise each hiss
        // level scales, the hiss stage's high-passed white noise and the channel's differenced one.)
        visHiss_.store(hifiOn_ ? 0.816f * hifiPath_.st[0].noise : 0.533f * cur_[cHiss] * hissDyn_, std::memory_order_relaxed);
        visHead_.store(hifiOn_ ? (hifiPath_.linear ? 0.0f : hifiPath_.s.headBuzz * (1.0f / kHfBuzzGain))
                               : (hsOn_ ? hsLen_[0] * (1.0f / 6.0f) : 0.0f), std::memory_order_relaxed);
        const bool linear = hifiOn_ && hifiPath_.linear;   // (lasts at least 33 ms: longer than a block)
        if (linear && !visLinear_) ++lostEvents_;
        visLinear_ = linear;
        visDrops_.store(dropEvents_ + hifiPath_.drops, std::memory_order_relaxed);
        visLost_.store(lostEvents_ << 1 | (linear || (fbState_ >= 1 && fbState_ <= 3) ? 1u : 0u), std::memory_order_relaxed);
    }

    // The Split knob as bias: 0 at its default (2.5 kHz, normalised 0.68468), -1 at the bottom
    // (too little), +1 at the top (too much).
    static float tapeBias(float n)
    {
        const float n0 = 0.68468f;
        return n < n0 ? (n - n0) / n0 : (n - n0) / (1.0f - n0);
    }

    // Value text with its unit, at most 8 characters: "25 %", "+3.0 dB", "9.0 kHz", "0.55 Hz".
    bool formatValue(int index, float v, std::string& out) override
    {
        if (index < 0 || index >= kNumParams || !isSetting(index) || param(index).kind != mpc::Param::Kind::Float) return false;
        if (units_[index] == 0 && index != kTone) return false;
        char buf[32];
        // Slew reads as what it sets: the shortest time the limit leaves for a swing from silence
        // to full level (the step per sample of readParams, gWear, turned into microseconds; the
        // micro sign is written as its two UTF-8 bytes).
        if (index == kWear)
        {
            const float sr = srWanted_.load(std::memory_order_relaxed);
            const float us = 1.0e6f / (2.0f * std::pow(0.006f, 0.01f * v) * (sr > 0.0f ? sr : 44100.0f));
            if (v < 0.05f) std::snprintf(buf, sizeof(buf), "off");
            else std::snprintf(buf, sizeof(buf), "%.0f \xC2\xB5s", (double)us);
            out = buf;
            return true;
        }
        // With Sat Type on Hi-Fi the same three read Rec Level (dB, 0 where VHS mode has it), the
        // carrier (0 to -30 dB) and Cal (the playback level error, "right" at the knob's default).
        if ((index == kLoSat || index == kHiSat || index == kSplit) && getInt(kSatType) == 2)
        {
            float dB = index == kLoSat ? hifiRecDb(0.01f * v)
                     : index == kHiSat ? kHfCarrierMinDb * 0.01f * v : kHfCalDb * tapeBias(param(kSplit).toNormalised(v));
            if (index == kHiSat) std::snprintf(buf, sizeof(buf), "%.0f dB", (double)(dB > -0.5f ? 0.0f : dB));
            else if (std::fabs(dB) >= 0.05f) std::snprintf(buf, sizeof(buf), "%+.1f dB", (double)dB);
            else std::snprintf(buf, sizeof(buf), index == kSplit ? "right" : "0.0 dB");
            out = buf;
            return true;
        }
        // With Sat Type on VHS three knobs mean something else (see TapeSat), and say so:
        // record level in dB, tape speed from SP to EP, bias as right / too little / too much.
        if ((index == kLoSat || index == kHiSat || index == kSplit) && getInt(kSatType) == 1)
        {
            if (index == kLoSat)
            {
                const float dB = kTapeLevelMin + kTapeLevelSpan * 0.01f * v;
                if (std::fabs(dB) < 0.05f) std::snprintf(buf, sizeof(buf), "0.0 dB");
                else std::snprintf(buf, sizeof(buf), "%+.1f dB", (double)dB);
            }
            else if (index == kHiSat)
            {
                // The knob runs the tape speed from SP (33.35 mm/s) down to EP, a third of it, in equal
                // ratios (TapeSat::set: 3^-speed). LP, half of SP, therefore lies at log 2 / log 3 of
                // the way: 63.1 %. The three speeds are named; between them the speed is given.
                // Named as well (1.7; the read-out only, the sound at each position is as it was): the
                // other formats' speeds that lie inside the range, each at 100 log(33.35 / speed) / log 3 %:
                // PAL VHS SP 23.39 and LP 11.70 mm/s, Beta II 20.00 and III 13.33 mm/s, PAL Beta 18.73 mm/s.
                // (Beta I, 40 mm/s, and PAL VHS EP, 7.80 mm/s, lie outside it.)
                static const struct { float at; const char* name; } kSpeedMarks[] = {
                    { 32.29f, "PAL SP" }, { 46.54f, "Beta II" }, { 52.51f, "PAL Beta" }, { 63.09f, "LP" },
                    { 83.47f, "Beta III" }, { 95.35f, "PAL LP" } };
                const char* mark = v < 0.5f ? "SP" : v > 99.5f ? "EP" : nullptr;
                for (const auto& m : kSpeedMarks) if (mark == nullptr && std::fabs(v - m.at) < 0.75f) mark = m.name;
                if (mark != nullptr) std::snprintf(buf, sizeof(buf), "%s", mark);
                else std::snprintf(buf, sizeof(buf), "%.0f mm/s", 33.35 * std::pow(3.0, -0.01 * (double)v));
            }
            else
            {
                const float b = 100.0f * tapeBias(param(kSplit).toNormalised(v));
                if (std::fabs(b) < 1.0f) std::snprintf(buf, sizeof(buf), "right");
                else std::snprintf(buf, sizeof(buf), "%+.0f %%", (double)b);        // minus: too little bias, plus: too much
            }
            out = buf;
            return true;
        }
        switch (units_[index])
        {
            case 1: std::snprintf(buf, sizeof(buf), "%.0f %%", (double)v); break;
            case 2:
                if (std::fabs(v) < 0.05f) std::snprintf(buf, sizeof(buf), "0.0 dB");
                else std::snprintf(buf, sizeof(buf), "%+.1f dB", (double)v);
                break;
            case 3:
                if (v >= 1000.0f)     std::snprintf(buf, sizeof(buf), "%.1f kHz", (double)v * 0.001);
                else if (v >= 100.0f) std::snprintf(buf, sizeof(buf), "%.0f Hz", (double)v);
                else if (v >= 10.0f)  std::snprintf(buf, sizeof(buf), "%.1f Hz", (double)v);
                else                  std::snprintf(buf, sizeof(buf), "%.2f Hz", (double)v);
                break;
            case 4: std::snprintf(buf, sizeof(buf), "%.1f ms", (double)v); break;
            case 5: std::snprintf(buf, sizeof(buf), "%.2f Hz", (double)v * (1.0 / 60.0)); break;   // Warp Rate: 0.10 to 5.00 Hz
            default:   // Tilt: what the highs get, in dB (the lows get the opposite): 6 dB at either end
                if (std::fabs(v) < 0.5f) std::snprintf(buf, sizeof(buf), "0.0 dB");
                else std::snprintf(buf, sizeof(buf), "%+.1f dB", 0.06 * (double)v);
                break;
        }
        out = buf;
        return true;
    }

private:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name         = "VHS Vibes";
        info.vendor       = "anthonytheinventor";   // must match plugin.json "manufacturer"
        info.uniqueId     = MPC_FOURCC(kUid[0], kUid[1], kUid[2], kUid[3]);   // must match plugin.json "uid"
        info.version      = 1700;           // 1.7.0
        info.isInstrument = false;
        info.numInputs    = 2;
        info.numOutputs   = 2;
        info.tailSamples  = 2 * kTapeDelaySamples;   // the two wobble delays ring out
        return info;
    }

    static constexpr char kUid[5] = "AYlM";

    // ----- parameters -> targets and filter coefficients ------------------------
    // Called with the groups whose parameters moved (gAll from reset()). All the pow / exp / sin
    // / cos work of the plugin is in here, so a group that did not change costs nothing.
    void readParams(uint32_t groups)
    {
        const float sr = sr_;
        auto pct = [this](int i) { return get(i) * 0.01f; };

        // Sat Type = Hi-Fi (1.7). The type itself changes as Sat Type always has (at a restart, or in
        // place while the Saturator is off and silent). "Hi-Fi on" is the type with the Saturator on:
        // then the FM path takes over Hiss, Crackle, Head Buzz, Breathing, Tracking, Drift and
        // Dropouts (their own stages are left at rest so nothing is doubled; see setHiFi) and B-Type
        // NR is ignored. With the Saturator off the plugin is as it is in the other types.
        if (groups == gAll)
        {
            const bool hifi = getInt(kSatType) == 2;
            if (hifi != hifiType_) pushPending_.store(true);   // the three read-outs change: have the host ask again
            hifiType_ = hifi;
        }
        {
            const bool on = hifiType_ && getBool(kSatOn);
            if (on != hifiOn_) { hifiOn_ = on; groups |= gRamps | gSat | gNoise | gDeck | gNr; }
        }

        if (groups & gRamps)
        {
            rampT_[rOut]    = dbToGain(get(kOutput));
            rampT_[rMono]   = getBool(kMono) ? 1.0f : 0.0f;
            rampT_[rChorus] = getBool(kChorusOn) ? pct(kChoMix) : 0.0f;
            micOn_          = getBool(kMicOn);
            rampT_[rMic]    = micOn_ && !micSwap_ ? 1.0f : 0.0f;   // (out while it changes type: see gMic)
            rampT_[rSat]    = getBool(kSatOn) ? 1.0f : 0.0f;
            rampT_[rMix]    = pct(kMix);
            dropAmount_     = hifiOn_ ? 0.0f : pct(kDropouts);
            animate_.store(getBool(kAnimate), std::memory_order_relaxed);
        }
        if (groups & gPreamp)
        {
            const float tiltDb = pct(kTone) * 6.0f;
            tgt_[cTiltLo] = dbToGain(-tiltDb);
            tgt_[cTiltHi] = dbToGain(tiltDb);
            setDrive(pct(kDrive), 15.0f, cDrvG, cDrvMk, cDrvMix);
        }
        if (groups & gWear)
        {
            // allowed change per sample, from "anything goes" (2.0, the stage is skipped) down to very dull
            tgt_[cSlew] = 2.0f * std::pow(0.006f, pct(kWear));
        }
        if (groups & gComp)
        {
            // threshold 0 .. -36 dB at 4:1; make-up undoes the gain reduction a reference-level
            // signal gets, so turning Comp up brings quiet material (and noise) up
            const float thr = std::pow(10.0f, -1.8f * pct(kComp));
            tgt_[cThresh] = thr;
            tgt_[cMakeup] = thr < kRefLevel ? std::pow(kRefLevel / thr, 0.75f) : 1.0f;
        }
        if (groups & gSat)
        {
            tgt_[cLowsG]  = dbToGain(get(kLows));
            tgt_[cHighsG] = dbToGain(get(kHighs));
            setDrive(pct(kLoSat), 9.0f, cLoG, cLoMk, cLoMix);
            setDrive(pct(kHiSat), 9.0f, cHiG, cHiMk, cHiMix);
            // The type itself only changes at a restart (a switch of it fades out first), or in
            // place while the stage is off (see process): until then the stage that is sounding
            // keeps its own settings.
            if (groups == gAll)
            {
                const bool tape = getInt(kSatType) == 1;
                if (tape != tape_) pushPending_.store(true);   // the three read-outs change: have the host ask again
                tape_ = tape;
            }
            if (tape_)
            {
                // Lo Sat: record level, -10 to +8 dB (0 dB, where a reference-level tone has
                // its 3 %, at 56 %). Hi Sat: tape speed. Split: bias, right at its default
                // (2.5 kHz), too little below, too much above. Lows and Highs trim the two
                // halves of the result around 1 kHz.
                tapeSat_.set(nlSr_, pct(kLoSat), pct(kHiSat), tapeBias(getNormalised(kSplit)));
                splitCoeff_ = lpCoeff(1000.0f, nlSr_);
            }
            else if (hifiType_) splitCoeff_ = lpCoeff(1000.0f, nlSr_);   // Hi-Fi: Lows and Highs around 1 kHz, as in VHS mode
            else splitCoeff_ = lpCoeff(get(kSplit), nlSr_);   // the 2-band crossover
        }
        if (groups & gHiCut)
        {
            // 4-pole Butterworth high cut = two biquads with these Qs
            const float hiCut = get(kHiCut);
            lpfA_.setCoeffs(BiquadCoeffs::lowpass(sr, hiCut, 0.5412f));
            lpfB_.setCoeffs(BiquadCoeffs::lowpass(sr, hiCut, 1.3066f));
        }
        if (groups & gWobble)
        {
            // depths are peak pitch error (squared for fine control at the low end)
            dev1Slow_ = kWarpDev     * sq(pct(kWarp));
            rate1Slow_ = get(kRpm) * (1.0f / 60.0f);
            shape1_   = pct(kShape);
            dev1Fast_ = kFlutterDev  * sq(pct(kFlutter));
            rate1Fast_ = get(kFlutRate);
            dev2Slow_ = kWowDev      * sq(pct(kWow));
            rate2Slow_ = get(kWowRate);
            dev2Fast_ = kFlutter2Dev * sq(pct(kFlutter2));
            rate2Fast_ = get(kFlut2Rate);
            tgt_[cWobbleOn] = getBool(kWobbleOn) ? 1.0f : 0.0f;
            // scrape: how far the read position is shaken. The noise it scales stays inside
            // +-2, and the wobble delay is never shorter than 3 samples, so 1 is the limit here.
            tgt_[cScrape] = sq(pct(kScrape)) * tgt_[cWobbleOn];
        }
        if (groups & gCuts)
        {
            loCutCoeff_ = lpCoeff(get(kLoCut), sr);
            hiCut2_.setCoeffs(BiquadCoeffs::lowpass(sr, get(kHiCut2), 0.7071f));
        }
        if (groups & gNoise)
        {
            // master is nominal (x1) at 50 %, +12 dB at 100 %
            const float master = getBool(kNoiseOn) ? 4.0f * sq(pct(kNoise)) : 0.0f;
            tgt_[cHum]     = sq(pct(kHum))      * 0.12f * master;
            tgt_[cBuzz]    = sq(pct(kBuzz))     * 0.10f * master;
            // (Hi-Fi on: the FM path makes these three; their stages here rest)
            tgt_[cHead]    = hifiOn_ ? 0.0f : sq(pct(kHeadBuzz)) * 0.50f * master;
            tgt_[cHiss]    = hifiOn_ ? 0.0f : sq(pct(kHiss))     * 0.10f * master;
            tgt_[cCrackle] = hifiOn_ ? 0.0f : sq(pct(kCrackle))  * 0.60f * master;
            // about -45 dBFS at 50 %, where a real deck has it; the factor evens out the oscillator.
            // Up to 60 % as it always was; from there (1.7) it climbs faster, to 15 dB more at 100 %
            // (-18 dBFS at Noise 50 %): a set that screams, and a tone this high needs the level to be heard.
            tgt_[cWhistle] = sq(pct(kWhistle))  * 0.022f * whistleNorm_ * master;
            if (pct(kWhistle) > 0.6f) tgt_[cWhistle] *= dbToGain(15.0f * (pct(kWhistle) - 0.6f) / 0.4f);
            const float clicksPerSec = 2.0f + 60.0f * pct(kCrackle);
            crackleThresh_ = (uint32_t)(clicksPerSec / sr * 4294967296.0f);
            noisePre_ = getInt(kNoisePos) == 1;
            tgt_[cMicNoise] = getBool(kNoiseOn) ? 1.0f : 0.0f;   // the mic's own noise: the switch only (see MicModel)

            // head switching: how long the signal is held at each switch, in samples. It follows
            // the Head Buzz knob and the noise master too, but not as the rasp does: in a straight
            // line with the knob (the rasp goes with its square), and with the master only up to
            // 50 %, where it reaches its full length (the rasp goes on rising, to +12 dB at 100 %).
            const float headHold = getBool(kNoiseOn) && !hifiOn_ ? pct(kHeadBuzz) * minf(2.0f * pct(kNoise), 1.0f) : 0.0f;
            hsLen_[0] = headHold * 6.0f;
            hsLen_[1] = headHold * 3.75f;   // the two heads are not quite alike
        }
        if (groups & gChorus)
        {
            choSpeed_ = get(kChoSpeed);
            choDelayTarget_ = clamp(get(kChoDelay) * 0.001f * sr, 4.0f, (float)kChorusDelaySamples - 8.0f);
            choWidth_ = pct(kChoWidth);
        }
        if (groups & gMic)
        {
            // A new Mic Type or Mic Model while the mic is heard would swap its filters under the
            // running sound (a click at each position a Q-Link passes). So the mic fades out
            // first, the new one goes in while it is out, and it fades back in from rest (see
            // controlTick). At a reset, or while the mic is off and silent, it goes in at once.
            micWant_ = clamp(getInt(kMicType), 0, 3) + 4 * clamp(getInt(kMicModel), 0, 1);
            if (groups == gAll || (!micSwap_ && rampT_[rMic] == 0.0f && ramp_[rMic].v == 0.0f)) { micSwap_ = false; setMic(micWant_); }
            else micSwap_ = micWant_ != micType_;   // (turned back to the type that is in: no fade)
            rampT_[rMic] = micOn_ && !micSwap_ ? 1.0f : 0.0f;
            tgt_[cMicGain] = dbToGain(get(kMicGain));
        }
        if (groups & gDeck)
        {
            const bool deck = getBool(kDeckOn);
            tgt_[cAgc]    = deck ? pct(kAgc) : 0.0f;
            tgt_[cDrift]  = deck && !hifiOn_ ? pct(kDrift) : 0.0f;       // (Hi-Fi on: these three are the FM path's)
            tgt_[cBreath] = deck && !hifiOn_ ? pct(kBreathing) : 0.0f;
            trackErr_     = deck && !hifiOn_ ? pct(kTracking) : 0.0f;
            // AGC Model: at a reset straight in; a switch while sound plays goes through
            // controlTick (the new one settles unheard first, then a crossfade)
            const bool wantM = getInt(kAgcModel) == 1;
            if (groups == gAll) { agcB_ = agcBTo_ = wantM ? 1.0f : 0.0f; agcBInc_ = 0.0f; agcSettle_ = 0; }
            else if (wantM != agcWantM_) agcSettle_ = wantM ? agcSettleTicks_ : agcSettleSimpleTicks_;
            agcWantM_ = wantM;
        }
        if (groups & gNr)
        {
            // a side that is switched in starts from rest (unless it was still fading out)
            // and fades in; one that is switched out fades out (see process)
            const int mode = hifiOn_ ? 0 : getInt(kNr);                  // ignored while Hi-Fi is on (it is the linear track's)
            const bool enc = mode == 1 || mode == 3, dec = mode >= 2;
            if (enc && !nrEncOn_ && nrEncMix_ == 0.0f) { nrEnc_.reset(); nrEncSeed_ = true; }
            if (dec && !nrDecOn_ && nrDecMix_ == 0.0f) { nrDec_.reset(); nrDecSeed_ = true; }
            nrEncOn_ = enc; nrDecOn_ = dec;
            nrEncGoal_ = enc ? 1.0f : 0.0f;
            nrEncRun_ = enc || nrEncMix_ > 0.0f; nrDecRun_ = dec || nrDecMix_ > 0.0f;
        }
        if (hifiType_ && (groups & (gRamps | gSat | gNoise | gDeck))) setHiFi();
    }

    // The knobs, as the Hi-Fi path reads them. Lo Sat, Hi Sat and Split: Rec Level, Carrier, Cal.
    // The seven it takes over are switched as their own stages are: Hiss, Crackle and Head Buzz by
    // the Noise switch and level (Hiss, the clicks and Head Buzz's spike as levels, x1 at Noise 50 %
    // and +12 dB at 100 %, as every noise; the head switch's concealed residual as an amount, in full from
    // Noise 50 %, as the head-switch hold is), Breathing, Tracking and Drift by Deck On, Dropouts by
    // nothing. The Noise switch off silences the channel's noise, clicks and head switch; the
    // holds and the linear track (the deck's behaviour, not noise) stay.
    void setHiFi()
    {
        const auto pct = [this](int i) { return get(i) * 0.01f; };
        const bool noise = getBool(kNoiseOn), deck = getBool(kDeckOn);
        const float master = noise ? 4.0f * sq(pct(kNoise)) : 0.0f;
        const float gate = noise ? minf(2.0f * pct(kNoise), 1.0f) : 0.0f;
        HiFiPath::Settings s;
        s.recDb       = hifiRecDb(pct(kLoSat));
        s.carrierDb   = kHfCarrierMinDb * pct(kHiSat);
        s.calDb       = kHfCalDb * tapeBias(getNormalised(kSplit));
        s.hiss        = noise ? kHfHissGain * 4.0f * pct(kHiss) * pct(kNoise) : 0.0f;
        s.noiseTop    = dbToGain(kHfSnrDb - kHfNoiseTopDb);
        s.clicks      = master;
        s.cnr0Db      = kHfCnrBestDb - kHfCnrSpanDb * pct(kCrackle);   // how near the deck sits to the click threshold
        s.headBuzz    = noise ? kHfBuzzGain * pct(kHeadBuzz) * 2.0f * pct(kNoise) : 0.0f;   // its spike, squared, goes with the Noise level as every noise
        s.headGate    = gate;
        s.breathing   = deck ? pct(kBreathing) : 0.0f;
        s.tracking    = deck ? pct(kTracking) : 0.0f;
        s.imbalanceDb = kHfImbalanceDb * s.tracking;                   // off the track, one head reads worse than the other
        s.drift       = deck ? pct(kDrift) : 0.0f;
        s.dropouts    = pct(kDropouts);
        hifiPath_.set(s);
    }

    // How long the mic settles unheard. The Camcorder's ALC, with its 20 ms attack, takes up to
    // about 80 ms from rest to come within 1 dB of its steady level (worst just above its knee,
    // where the start overshoots by 2 dB and the loop closes slowly; well above the knee it takes
    // under 20 ms), so it waits 100 ms. The others: 20 ms.
    int micHoldFor() const { return mic_.type == MicModel::kCamcorder ? micHoldCamTicks_ : micHoldTicks_; }

    // mic: Mic Type + 4 x Mic Model (0 to 3 the Simple ones, 4 to 7 the Modeled ones)
    void setMic(int mic)
    {
        micSimple_ = mic < 4;
        if (micSimple_) simple_.set(mic, sr_); else mic_.set(mic - 4, sr_);
        micType_ = mic;
    }

    // amount 0..1 -> clipper gain, make-up and clean/driven blend.
    void setDrive(float amount, float maxExtraGain, int gIdx, int mkIdx, int mixIdx)
    {
        const float g = 1.0f + maxExtraGain * amount * amount;
        tgt_[gIdx]   = g;
        tgt_[mkIdx]  = kRefLevel / softClip(g * kRefLevel);
        tgt_[mixIdx] = clamp(amount * 6.0f, 0.0f, 1.0f);
    }

    // controlTick's part for the Modeled AGC and the crossfade between the two, out of line so the
    // control tick (inlined into the sample loop) stays as small as it was.
    __attribute__((noinline)) void agcModelTick(bool agcOn, float agcBWant)
    {
        // The Modeled one: off (and free) at 0 %. It starts from rest, as the deck does at the
        // start of a recording; at 0 % its gain is exactly 1, so it stops without a step.
        const bool agcM = agcOn && (agcB_ > 0.0f || agcBWant > 0.0f);
        if (agcM && !agcMRun_) agcm_.reset();
        agcMRun_ = agcM;
        if (agcMRun_) agcm_.tick(agcAmt_);
        // AGC Model switched while an AGC is heard: the new one settles unheard (agcSettle_,
        // from readParams), then the sound crossfades to it. With neither heard, it switches at once.
        agcB_ = agcBTo_;
        if (!agcActive_ && !agcMRun_) { agcB_ = agcBTo_ = agcBWant; agcSettle_ = 0; }
        else if (agcBTo_ != agcBWant)
        {
            if (agcSettle_ > 0) --agcSettle_;
            else agcBTo_ = agcBWant > agcBTo_ ? minf(agcBTo_ + agcXfStep_, 1.0f) : maxf(agcBTo_ - agcXfStep_, 0.0f);
        }
        agcBInc_ = (agcBTo_ - agcB_) * kCtrlInv;
        agcBlend_ = agcMRun_ || agcBInc_ != 0.0f || (agcB_ > 0.0f && agcB_ < 1.0f);
    }

    // ----- every 16 samples: smoothing, modulation, compressor gain -----------
    void controlTick()
    {
        // The fade-out before a reset reached 0 over the interval that ends here: only now may
        // the reset come (at the start of the next block). Asked for a tick earlier, a block
        // that ended inside the fade's last step would have cut it short, with a step.
        if (resetNextTick_) { resetNextTick_ = false; resetPending_.store(true); }
        // Mic Type or Mic Model: the mic has faded out for the new one (see readParams). Put it
        // in, from rest, and fade the mic back in. (A Simple one is cleared here, as up to 1.5;
        // a model starts from rest when it starts to run, below, and settles unheard first.)
        if (micSwap_ && ramp_[rMic].v == 0.0f)
        {
            const bool fromSimple = micSimple_;
            setMic(micWant_);
            if (micSimple_) { simple_.reset(); micHold_ = 0; }
            else if (fromSimple) micRun_ = false;
            micSwap_ = false;
            rampT_[rMic] = micOn_ ? 1.0f : 0.0f;
        }

        // Smoothed control values land exactly on their targets, so "is this stage doing
        // anything" below is a plain compare.
        for (int i = 0; i < kNumCtl; ++i)
        {
            const float d = tgt_[i] - cur_[i];
            cur_[i] = std::fabs(d) < 1.0e-5f ? tgt_[i] : cur_[i] + kSmooth * d;
        }
        for (int i = 0; i < kNumRamp; ++i)
        {
            rampS_[i] += kSmooth * (rampT_[i] - rampS_[i]);
            if (std::fabs(rampT_[i] - rampS_[i]) < 1.0e-4f) rampS_[i] = rampT_[i];   // lands exactly on 0 / 1
            if (ramp_[i].v == rampS_[i]) ramp_[i].inc = 0.0f; else ramp_[i].to(rampS_[i]);
        }
        // A ramp that has arrived must sit exactly on its target so "== 0" skips work.
        for (int i = 0; i < kNumRamp; ++i)
            if (rampS_[i] == rampT_[i] && std::fabs(ramp_[i].v - rampT_[i]) < 1.0e-4f) ramp_[i].snap(rampT_[i]);

        // just after a reset: the output stays shut until the emptied delays have filled
        if (fadeHold_ > 0) { --fadeHold_; rampS_[rOut] = 0.0f; ramp_[rOut].snap(0.0f); }
        // and, with the chorus on, a few ticks longer (see doReset); the fade-in goes on meanwhile
        else if (gateOn_)
        {
            if (choWait_ > 0) --choWait_;
            else if (gateS_ < 1.0f) { gateS_ = minf(gateS_ + 0.125f, 1.0f); gate_.to(gateS_); }
            else { gate_.snap(1.0f); gateOn_ = false; }   // open: from here the output is as without it
        }
        // Before a reset the host asked for (a program change, a state load) and when HQ is
        // switched: the output goes down in a straight line over 3 ms and stays shut, then the
        // reset brings the sound back with its fade-in.
        if (fadeOut_ > 0)
        {
            --fadeOut_;
            rampS_[rOut] = ramp_[rOut].v * (float)fadeOut_ / (float)(fadeOut_ + 1);
            ramp_[rOut].to(rampS_[rOut]);
            if (fadeOut_ == 0) { fadeHold_ = 1 << 20; resetNextTick_ = true; }   // shut; the reset follows
        }

        // Which stages have anything to do for the next 16 samples. (live: the amount is up, or
        // on its way up or down.)
        const auto live = [this](int c) { return cur_[c] > 0.0f || tgt_[c] > 0.0f; };
        humOn_     = live(cHum);
        buzzOn_    = live(cBuzz);
        headOn_    = live(cHead);
        hissOn_    = live(cHiss);
        crackleOn_ = live(cCrackle);
        noiseActive_ = humOn_ || buzzOn_ || headOn_ || hissOn_ || crackleOn_;
        whistleOn_ = live(cWhistle);
        scrapeOn_  = live(cScrape);
        tiltOn_    = cur_[cTiltLo] != 1.0f || cur_[cTiltHi] != 1.0f;
        driveOn_   = cur_[cDrvMix] > 0.0f;
        slewOn_    = cur_[cSlew] < 2.0f;
        slewStep_  = cur_[cSlew] * (hq_ ? 0.5f : 1.0f);   // per sample at the rate the stage runs at
        loSatOn_   = cur_[cLoMix] > 0.0f;
        hiSatOn_   = cur_[cHiMix] > 0.0f;

        // compressor: 4:1 above the threshold. (t ^ 0.75 with two square roots)
        const float thr = cur_[cThresh];
        if (env_ > thr) { const float s = std::sqrt(thr / env_); compTarget_ = s * std::sqrt(s); }
        else compTarget_ = 1.0f;

        // wobble: the switch fades both stages in and out
        const float wob = cur_[cWobbleOn];
        wf1_.update(sr_, dev1Slow_ * wob, rate1Slow_, dev1Fast_ * wob, rate1Fast_);
        wf2_.update(sr_, dev2Slow_ * wob, rate2Slow_, dev2Fast_ * wob, rate2Fast_);
        wf1_.tick(shape1_);
        wf2_.tick(0.0f);
        wantLatency_.store((int)(wf1_.base + wf2_.base + (hq_ ? kHqDelay : 0.0f) + 0.5f), std::memory_order_relaxed);   // settles when the glide ends

        // Mix: the dry copy is only kept while Mix is below 100 %. It waits as long as the tape
        // path does at rest; with both wobbles still that is a whole number of samples. When
        // Mix is first turned down the copy is still empty (or holds old sound), so the blend
        // waits until it has filled: no dip.
        const bool dryWant = rampT_[rMix] < 1.0f || ramp_[rMix].v < 1.0f;
        // It follows the tape path: glided along with it, and crossfaded together with a wobble
        // stage that crossfades (their jump is part of the target from the first tick of the
        // crossfade), once it holds enough sound to be read that far back.
        const float dryTarget = wf1_.base + wf1_.jump + wf2_.base + wf2_.jump;
        if (dryWant && !dryActive_)
        {
            dryD_.snap(dryTarget); dryJump_ = 0.0f; dryFill_ = 0;
            dryHold_ = (int)(dryTarget * kCtrlInv) + 2;
        }
        else if (dryWant && dryJump_ == 0.0f)
        {
            const float off = dryTarget - dryD_.v;
            if (dryFill_ < (1 << 20)) ++dryFill_;
            if (std::fabs(off) < 0.01f) dryD_.snap(dryTarget);
            else if (std::fabs(off) <= kJumpMin) dryD_.to(dryTarget);
            else if ((float)(dryFill_ * kCtrl) > dryTarget + 8.0f) { dryD_.inc = 0.0f; dryJump_ = off; dryXf_ = 0.0f; }
        }
        dryStill_ = wf1_.still && wf2_.still && dryD_.inc == 0.0f && dryJump_ == 0.0f;
        if (dryHold_ > 0) { --dryHold_; rampS_[rMix] = 1.0f; ramp_[rMix].snap(1.0f); }
        dryActive_ = dryWant;

        // ----- what the screen shows -------------------------------------------
        // The reels turn while sound comes in or the song plays, coast to a stop about two
        // seconds after, and lurch with the wobble (the speed error is exaggerated 8 times
        // so it can be seen). The level follows the output with a slow fall.
        if (animate_.load(std::memory_order_relaxed))
        {
            inEnv_  += (inPeak_  > inEnv_  ? 0.5f : 0.0008f) * (inPeak_  - inEnv_);
            outEnv_ += (outPeak_ > outEnv_ ? 0.5f : 0.0020f) * (outPeak_ - outEnv_);
            spin_ += 0.0015f * (((inEnv_ > 0.003f || playing_) ? 1.0f : 0.0f) - spin_);
            const float speed = clamp(1.0f + 8.0f * (wf1_.err + wf2_.err), 0.15f, 2.5f);
            reelPhase_ += kReelStepsPerSec * speed * spin_ * (float)kCtrl / sr_;
            if (reelPhase_ >= 1.0f) reelPhase_ -= 1.0f;
        }

        // chorus: only while it is on. Switched on, it starts at the right delay time, and its
        // blend waits until the delay (and its swing) has filled with new sound.
        const bool choWant = rampT_[rChorus] > 0.0f || ramp_[rChorus].v > 0.0f;
        if (choWant && !choActive_)
        {
            choBase_ = choDelayTarget_;
            choD_[0].snap(choBase_); choD_[1].snap(choBase_);
            choHold_ = (int)((choBase_ + 0.004f * sr_) * kCtrlInv) + 2;
        }
        if (choHold_ > 0) { --choHold_; rampS_[rChorus] = 0.0f; ramp_[rChorus].snap(0.0f); }
        choActive_ = choWant;

        // mic: the model runs while the mic is heard or on its way in or out. Coming in from
        // silence (switched on, or a new type) it starts from rest and runs unheard for 20 ms
        // before the fade-in: long enough for the filters to settle and for the Boombox's ALC
        // to catch a loud sound (which takes it about 5 ms from rest, with a burst). A Simple
        // one carries on from where it stopped and fades in at once, as up to 1.5.
        const bool micWant = rampT_[rMic] > 0.0f;
        if (micWant && !micRun_) { micRun_ = true; if (!micSimple_) { mic_.reset(); micHold_ = micHoldFor(); } }
        else if (!micWant && ramp_[rMic].v == 0.0f) micRun_ = false;
        if (micHold_ > 0) { --micHold_; rampS_[rMic] = 0.0f; ramp_[rMic].snap(0.0f); }
        if (choActive_)
        {
            // L and R LFOs up to half a cycle apart. A jump of Cho Delay is followed at a
            // limited rate, like the wobble delays.
            choPhase_ += choSpeed_ * (float)kCtrl / sr_;
            if (choPhase_ >= 1.0f) choPhase_ -= 1.0f;
            choBase_ += clamp(kSmooth * (choDelayTarget_ - choBase_), -kGlideStep, kGlideStep);
            const float depth = minf(0.4f * choBase_, 0.004f * sr_);
            choD_[0].to(choBase_ + depth * lfoSin(choPhase_));
            choD_[1].to(choBase_ + depth * lfoSin(choPhase_ + 0.5f * choWidth_));
        }

        // dropouts: now and then the sound dips for a few milliseconds
        if (dropTicks_ > 0) { if (--dropTicks_ == 0) dropTarget_ = 1.0f; }
        else if (dropAmount_ > 0.0f)
        {
            const float perTick = 8.0f * dropAmount_ * dropAmount_ * (float)kCtrl / sr_;
            if (dropRng_.nextFloat() < perTick)
            {
                const float ms = 4.0f + 70.0f * dropRng_.nextFloat() * dropRng_.nextFloat();
                dropTicks_  = 1 + (int)(ms * 0.001f * sr_ * kCtrlInv);
                dropTarget_ = 1.0f - dropAmount_ * (0.4f + 0.6f * dropRng_.nextFloat());
                ++dropEvents_;   // (for the picture: see the end of process())
            }
        }
        else dropTarget_ = 1.0f;
        if (dropTarget_ == 1.0f && dropGain_ > 0.9999f) dropGain_ = 1.0f;   // a dip is over
        dropOn_ = dropGain_ != 1.0f || dropTarget_ != 1.0f;                  // only then is there work

        // ----- deck ------------------------------------------------------------
        // AGC: after the last peak the gain holds for a second and a half, then creeps back
        // up over a few seconds and brings the background up with it.
        // Each AGC runs while it is heard, or on its way in or out after AGC Model was switched
        // (agcB_: how much of the Modeled one is heard; agcBTo_: where it is at the next tick).
        // The Simple one runs exactly as up to 1.5 while it is the one in.
        agcAmt_ = cur_[cAgc];
        const bool agcOn = agcAmt_ > 0.0f || tgt_[cAgc] > 0.0f;
        const float agcBWant = agcWantM_ ? 1.0f : 0.0f;
        agcActive_ = (agcB_ < 1.0f || agcBWant < 1.0f) && (agcOn || (agcB_ < 1.0f && std::fabs(agcGain_ - 1.0f) > 1.0e-3f));
        if (agcActive_)
        {
            if (agcHold_ > 0) --agcHold_;
            else agcEnv_ = maxf(agcEnv_ - agcRelease_ * agcEnv_, 1.0e-4f);
            agcTarget_ = 1.0f + agcAmt_ * (clamp(kRefLevel / agcEnv_, 0.25f, 5.0f) - 1.0f);
        }
        else { agcGain_ = agcTarget_ = 1.0f; agcEnv_ = kRefLevel; }
        agcModelTick(agcOn, agcBWant);
        // The clip after the compressor is needed while something may turn the sound up, unless
        // the 2-band saturator, whose clippers have the same rail, is fully in. (With Sat Type on
        // VHS those clippers are not in the path, and Lo Sat and Hi Sat mean something else.)
        boost_ = (agcActive_ || agcMRun_ || cur_[cMakeup] > 1.0f)
              && !(!tape_ && !hifiType_ && ramp_[rSat].v == 1.0f && cur_[cLoMix] == 1.0f && cur_[cHiMix] == 1.0f);
        // Hi-Fi: the path runs while it is heard or fading; coming in from silence it starts from rest
        if (hifiType_)
        {
            const bool run = rampT_[rSat] > 0.0f || ramp_[rSat].v > 0.0f;
            if (run && !hifiRun_) hifiPath_.reset();
            hifiRun_ = run;
        }

        // Tracking: now and then the Hi-Fi carrier drops. A burst of static, the deck falls
        // back to the dull mono linear track for a moment, another short burst, Hi-Fi again.
        // More often it is only the right channel that crackles for a moment: its carrier is
        // the higher of the two and gives up first.
        if (fbTicks_ > 0)
        {
            if (--fbTicks_ == 0)
            {
                if (fbState_ == 1)      { fbState_ = 2; fbTicks_ = fbLinearTicks_; staticAmp_ = 0.0f; }
                else if (fbState_ == 2) { fbState_ = 3; fbTicks_ = 1 + (int)(0.004f * sr_ * kCtrlInv); staticAmp_ = fbStatic_ * 0.6f; }
                else                    { fbState_ = 0; staticAmp_ = 0.0f; }
            }
        }
        else if (trackErr_ > 0.0f)
        {
            const float perTick = 4.0f * trackErr_ * trackErr_ * (float)kCtrl / sr_;
            const float u = fbRng_.nextFloat();
            if (u < 2.5f * perTick)
            {
                const bool whole = u < perTick;   // the whole track; otherwise the right channel alone
                fbState_ = whole ? 1 : 4;
                if (whole) ++lostEvents_; else ++dropEvents_;   // (the picture: the band, or a streak)
                staticL_ = whole ? 1.0f : 0.0f;
                fbTicks_ = 1 + (int)((0.003f + 0.02f * fbRng_.nextFloat()) * sr_ * kCtrlInv);
                fbStatic_ = 0.08f + 0.35f * trackErr_ * fbRng_.nextFloat();
                staticAmp_ = fbStatic_;
                fbLinearTicks_ = 1 + (int)((0.12f + 0.8f * fbRng_.nextFloat() * fbRng_.nextFloat()) * sr_ * kCtrlInv);
            }
        }
        const bool fallback = fbState_ == 2;
        linTarget_ = fallback ? 1.0f : 0.0f;
        if (!fallback && linAmt_ != 0.0f && linAmt_ < 1.0e-4f)
        {
            // Back on the Hi-Fi track: switch the backup-track stage off and empty its filters,
            // so the next fall-back starts clean.
            linAmt_ = 0.0f;
            fbLp_.reset();
            fbHp_ = splat(0.0f);
        }
        fbMono_ += 0.25f * ((fallback ? 1.0f : 0.0f) - fbMono_);
        if (fbMono_ < 1.0e-4f) fbMono_ = 0.0f;
        hsOn_ = hsLen_[0] > 0.0f && !fallback;   // no Hi-Fi heads in play on the linear track
        if (!hsOn_) { hsLeft_ = 0; hsPhase_ = 0.0f; }   // an interrupted hold must not carry on later,
                                                        // and the next switch is a whole field away

        // Breathing is the Hi-Fi noise reduction at work: the hiss rises and falls with the
        // sound coming in (5 ms up, 70 ms back), and the head buzz is pushed down in the
        // pauses, back to its set level when the sound is at reference level. On the backup
        // track the hiss is twice as loud.
        const float breath = cur_[cBreath];
        breathOn_ = live(cBreath);
        const float lvl = maxf(inMax_[0], inMax_[1]);
        inMax_ = splat(0.0f);
        breathEnv_ += (lvl > breathEnv_ ? breathAtk_ : breathRel_) * (lvl - breathEnv_);
        hissDyn_ = (fallback ? 2.0f : 1.0f) * (1.0f + breath * 6.0f * minf(breathEnv_, 1.0f));
        headAmp_ = cur_[cHead] * (1.0f + breath * (minf(breathEnv_ * (1.0f / kRefLevel), 1.5f) - 1.0f));

        // Drift: the audio track on the tape's edge wanders off the head. A slow random
        // movement, on the head most of the time with a deeper excursion now and then.
        const float edge = cur_[cDrift];
        edgeActive_ = edge > 0.0f;
        if (edgeActive_)
        {
            edgeN1_ += 0.0028f * (edgeRng_.nextBipolar() - edgeN1_);
            edgeN2_ += 0.0028f * (edgeN1_ - edgeN2_);
            float m = clamp(0.4f + 0.5f * edgeN2_ * 65.0f, 0.0f, 1.0f);   // 65 brings the drift to about unit size
            m *= m;
            edgeMuffle_ += 0.05f * (edge * m - edgeMuffle_);
            edgeGain_   += 0.05f * ((1.0f - 0.65f * edge * m) - edgeGain_);
        }
    }

    // ----- telling the host what changed inside the plugin -----------------------
    // The MPC does not read values back by itself. Three things change from the inside:
    // choosing a built-in program (all the knobs), the meters that drive the reels and the
    // sun, and how late the output is. The host has to be told, and those calls must not come
    // from the audio thread, so this small thread makes them, 20 times a second at most and
    // only when something changed. It says nothing until the host has started the plugin.
    //
    // The picture effects (1.7) are decided here too, from what the audio thread published at the
    // end of its last block (see process()) and from the knobs, and shown through the meters:
    //   Snow    the hiss as it is made, in steps of its level; a new random pattern at every look,
    //           so it flickers as video noise does
    //   Head    the head switch strip, in steps of Head Buzz as the sound has it; it jitters the same way
    //   Streak  a white streak for kStreakHold looks (100 ms) whenever a dropout started since the
    //           last look; while the Hi-Fi track is lost, and kBandHold looks after, a band of noise
    //           that climbs the picture one height per look (it wins over a streak)
    //   Grid    the grid bent by the wobble's depth (its knobs), at the scroll frame Reel has
    //   Haze    steps of Hi Cut below 7 kHz
    //   Level   besides the sun's level, its colour: Sat Type's
    // All of them show nothing (frame 0, the straight grid, the first colour) unless Animation and
    // Visual Vibes are both on; with Animation off nothing moves.
    static constexpr float kSnowDb[kSnowSteps]    = { -38.0f, -28.0f, -18.0f };   // hiss RMS re a reference sine's
    static constexpr float kHeadAt[kHeadSteps]    = { 0.04f, 0.3f, 0.6f };        // head switch, 0..1
    static constexpr float kWobbleAt[kWobbleSteps] = { 0.003f, 0.012f, 0.03f };   // summed peak pitch error
    static constexpr float kHazeTopHz = 7000.0f;                                  // no haze from here up
    static constexpr int   kStreakHold = 2, kBandHold = 3;                        // looks (50 ms each)

    void notifierMain()
    {
        int lastReel = -1, lastLevel = -1;
        int latCandidate = 0, latStable = 0, latReported = 0;
        // the picture effects: the frames last shown (Snow, Head, Streak, Grid, Haze), the reel's and
        // the sun's frames as last seen with Animation on, and what the effects remember between looks
        int lastFx[kNumParams - kVisSnow];
        for (int& l : lastFx) l = -1;
        int reel = 0, level = 0;
        int snowVar = 0, headVar = 0, streakVar = 0, streakHold = 0, bandHold = 0, bandPos = 0;
        uint32_t seenDrops = 0, seenLost = 0;
        Xorshift32 rng{0x5ee7ab1eu};
        const auto another = [&](int v, int n) { return (v + 1 + (int)(rng.next() % (uint32_t)(n - 1))) % n; };   // never the same twice
        const auto stepOf = [](float v, const float* at, int n) { int s = 0; while (s < n && v >= at[s]) ++s; return s; };
        // Every call into the host checks first that the plugin is still running: the host may
        // have suspended or started closing it since the last look. (suspended_ as well as
        // started_: doReset can set started_ for a moment while a suspend comes in.)
        auto speaking = [this] { return started_.load() && !suspended_.load(); };
        auto tell = [&](int i) { if (speaking()) notifyHostParameterChanged(i); };
        auto show = [&](int i, int frame, int frames, int& last)
        {
            if (frame == last) return;
            last = frame;
            setNormalised(i, (float)frame / (float)(frames - 1));
            tell(i);
        };
        for (;;)
        {
            {
                // 50 ms between looks, cut short when the plugin closes (see the destructor)
                std::unique_lock<std::mutex> lock(quitMutex_);
                if (quitCv_.wait_for(lock, std::chrono::milliseconds(50), [this] { return quit_.load(); })) break;
            }
            if (!speaking()) continue;

            if (pushPending_.exchange(false))
            {
                for (int i = 0; i < numParams(); ++i) tell(i);
                lastReel = lastLevel = -1;
                for (int& l : lastFx) l = -1;
            }

            const bool anim = animate_.load(std::memory_order_relaxed);
            if (anim)
            {
                reel = clamp((int)(visReel_.load(std::memory_order_relaxed) * (float)kReelFrames), 0, kReelFrames - 1);
                show(kVisReel, reel, kReelFrames, lastReel);
                // -42 dBFS .. 0 dBFS across the frames
                const float env = visLevel_.load(std::memory_order_relaxed);
                const float db = 20.0f * std::log10(env > 1.0e-6f ? env : 1.0e-6f);
                level = clamp((int)((db + 42.0f) * (1.0f / 42.0f) * (float)(kLevelFrames - 1) + 0.5f), 0, kLevelFrames - 1);
            }

            const uint32_t drops = visDrops_.load(std::memory_order_relaxed), lost = visLost_.load(std::memory_order_relaxed);
            int snow = 0, head = 0, streak = 0, wobble = 0, haze = 0, colour = 0;
            if (anim && getBool(kVisualVibes))
            {
                const float hiss = visHiss_.load(std::memory_order_relaxed);
                const int s = stepOf(20.0f * std::log10(maxf(hiss, 1.0e-9f) * (1.4142136f / kRefLevel)), kSnowDb, kSnowSteps);
                if (s > 0) { snowVar = another(snowVar, kSnowVariants); snow = 1 + (s - 1) * kSnowVariants + snowVar; }
                const int h = stepOf(visHead_.load(std::memory_order_relaxed), kHeadAt, kHeadSteps);
                if (h > 0) { headVar = another(headVar, kHeadVariants); head = 1 + (h - 1) * kHeadVariants + headVar; }

                if ((lost >> 1) != (seenLost >> 1) || (lost & 1u)) { if (bandHold == 0) bandPos = 0; bandHold = kBandHold; }
                if (drops != seenDrops) { streakVar = another(streakVar, kStreakVariants); streakHold = kStreakHold; }
                if (bandHold > 0) { --bandHold; streak = 1 + kStreakVariants + bandPos; bandPos = (bandPos + 1) % kBandSteps; }
                else if (streakHold > 0) { --streakHold; streak = 1 + streakVar; }

                const auto pct = [this](int i) { return get(i) * 0.01f; };
                const float dev = getBool(kWobbleOn) ? kWarpDev * sq(pct(kWarp)) + kFlutterDev * sq(pct(kFlutter))
                                                     + kWowDev * sq(pct(kWow)) + kFlutter2Dev * sq(pct(kFlutter2)) : 0.0f;
                wobble = stepOf(dev, kWobbleAt, kWobbleSteps);
                // 0 at 7 kHz, 1 at the knob's bottom (1 kHz), in octaves
                const float cut = std::log(kHazeTopHz / get(kHiCut)) / std::log(kHazeTopHz / param(kHiCut).min);
                haze = cut > 0.0f ? clamp(1 + (int)(cut * (float)kHazeSteps), 1, kHazeSteps) : 0;
                colour = clamp(getInt(kSatType), 0, kSunColours - 1);
            }
            else streakHold = bandHold = 0;
            seenDrops = drops; seenLost = lost;
            show(kVisLevel, colour * kLevelFrames + level, kSunColours * kLevelFrames, lastLevel);
            show(kVisGrid, wobble * kReelFrames + reel, kGridFrames, lastFx[kVisGrid - kVisSnow]);
            show(kVisSnow, snow, kSnowFrames, lastFx[kVisSnow - kVisSnow]);
            show(kVisHead, head, kHeadFrames, lastFx[kVisHead - kVisSnow]);
            show(kVisStreak, streak, kStreakFrames, lastFx[kVisStreak - kVisSnow]);
            show(kVisHaze, haze, kHazeFrames, lastFx[kVisHaze - kVisSnow]);

            // Latency: the output is late by the resting length of the two wobble delays. Tell
            // the host once that length has been steady for half a second and is more than
            // 32 samples (0.7 ms) away from what it was told last, so turning a knob does not
            // send a stream of updates.
            const int want = wantLatency_.load(std::memory_order_relaxed);
            if (std::abs(want - latCandidate) > 8) { latCandidate = want; latStable = 0; }
            else if (latStable < 10) ++latStable;
            if (latStable >= 10 && std::abs(latCandidate - latReported) > 32 && speaking())
            {
                latReported = latCandidate;
                setLatency(latCandidate);
            }
        }
    }

    // ----- built-in programs ---------------------------------------------------
    void addPrograms()
    {
        // A program lists every parameter. The Animation and HQ switches are not meant to
        // follow a program: see holdSwitches().
        std::vector<float> defaults;
        for (int i = 0; i < kNumParams; ++i) defaults.push_back(param(i).def);
        auto program = [&](const char* name, std::initializer_list<std::pair<int, float>> changes)
        {
            std::vector<float> v = defaults;
            for (const std::pair<int, float>& c : changes) v[(size_t)c.first] = c.second;
            addProgram(name, v);
        };

        program("Home Recording", { {kAgcModel, 0}, {kAgc, 25}, {kDrift, 10} });
        // A new rental tape: clean, a trace of breathing, loses lock once in a long while.
        program("Fresh Rental", {
            {kDrive, 15}, {kWear, 8}, {kComp, 15}, {kHiCut, 12000}, {kHiSat, 20},
            {kWarp, 15}, {kFlutter, 15}, {kWow, 10}, {kFlutter2, 10}, {kHiCut2, 14000},
            {kHum, 5}, {kHeadBuzz, 5}, {kHiss, 20}, {kCrackle, 5},
            {kTracking, 6}, {kBreathing, 15} });
        program("3rd Gen Copy", {
            {kDrive, 45}, {kWear, 45}, {kComp, 45}, {kHiCut, 6000}, {kHighs, -8}, {kHiSat, 60}, {kLoSat, 35},
            {kWarp, 50}, {kFlutter, 45}, {kWow, 45}, {kFlutter2, 40}, {kLoCut, 100}, {kHiCut2, 7000},
            {kHum, 25}, {kHeadBuzz, 25}, {kHiss, 55}, {kCrackle, 30},
            {kAgcModel, 0}, {kAgc, 40}, {kDrift, 35} });
        program("Vaporwave Mall", {
            {kDrive, 30}, {kComp, 35}, {kHiCut, 7000}, {kWarp, 60}, {kRpm, 20}, {kShape, 50}, {kFlutter, 20},
            {kWow, 40}, {kWowRate, 0.4f}, {kHiss, 25}, {kCrackle, 10},
            {kChorusOn, 1}, {kChoMix, 55}, {kChoSpeed, 0.35f}, {kChoDelay, 18}, {kChoWidth, 90},
            {kDrift, 20} });
        // The camcorder's level control does the squashing here.
        program("Camcorder 89", {
            {kDrive, 35}, {kComp, 30}, {kHiCut, 8000}, {kWarp, 20}, {kFlutter, 35},
            {kHiss, 45}, {kHeadBuzz, 30}, {kNoisePos, 1}, {kNoise, 35},
            {kMicOn, 1}, {kMicModel, 0}, {kMicType, 0}, {kMicGain, 2},
            {kAgcModel, 0}, {kAgc, 80}, {kDrift, 10} });
        // A tape that will not track: buzz on the music, static, drops to the backup track.
        program("Tracking Error", {
            {kWarp, 70}, {kRpm, 90}, {kShape, 80}, {kFlutter, 60}, {kFlutRate, 16}, {kWow, 30},
            {kHeadBuzz, 60}, {kHiss, 40}, {kCrackle, 40}, {kDropouts, 60},
            {kTracking, 85}, {kBreathing, 30} });
        program("Late Night TV", {
            {kDrive, 40}, {kWear, 35}, {kComp, 50}, {kHiCut, 5000}, {kWarp, 25}, {kLoCut, 180},
            {kHum, 40}, {kBuzz, 25}, {kHiss, 45}, {kMono, 1},
            {kAgcModel, 0}, {kAgc, 30}, {kDrift, 15} });
        program("Clean Widener", {
            {kDrive, 0}, {kWear, 0}, {kComp, 0}, {kHiCut, 18000}, {kSatOn, 0},
            {kWarp, 0}, {kFlutter, 0}, {kWow, 0}, {kFlutter2, 0}, {kLoCut, 20}, {kHiCut2, 18000}, {kNoise, 0},
            {kChorusOn, 1}, {kChoMix, 50}, {kChoSpeed, 0.5f}, {kChoDelay, 10}, {kChoWidth, 100},
            {kDeckOn, 0} });

        // A worn-out 80s corporate training tape: mono, narrow, a level control that pumps the
        // hiss up in every pause, slow warble, head buzz, sound that comes and goes, the odd dropout.
        program("Training Video", {
            {kDrive, 40}, {kTone, -15}, {kWear, 40}, {kComp, 35}, {kHiCut, 4500},
            {kHighs, -6}, {kHiSat, 55}, {kLoSat, 30}, {kSplit, 2000},
            {kWarp, 45}, {kRpm, 34}, {kShape, 60}, {kFlutter, 35}, {kFlutRate, 7},
            {kWow, 40}, {kWowRate, 0.5f}, {kFlutter2, 30}, {kFlut2Rate, 12}, {kLoCut, 150}, {kHiCut2, 6000},
            {kHum, 30}, {kBuzz, 15}, {kHeadBuzz, 35}, {kHiss, 50}, {kCrackle, 20}, {kNoise, 40}, {kNoisePos, 1},
            {kMono, 1}, {kDropouts, 25},
            {kAgcModel, 0}, {kAgc, 75}, {kDrift, 40} });
        // The machine ate the tape.
        program("Chewed Tape", {
            {kDrive, 35}, {kWear, 50}, {kComp, 45}, {kHiCut, 5500},
            {kWarp, 90}, {kRpm, 45}, {kShape, 100}, {kFlutter, 70}, {kFlutRate, 5},
            {kWow, 60}, {kWowRate, 1.5f}, {kFlutter2, 45}, {kFlut2Rate, 9}, {kHiCut2, 7000},
            {kHiss, 40}, {kCrackle, 50}, {kHeadBuzz, 20}, {kDropouts, 80},
            {kAgcModel, 0}, {kAgc, 20}, {kDrift, 85} });
        // A message left on a micro-cassette answering machine.
        program("Answer Machine", {
            {kDrive, 50}, {kWear, 30}, {kComp, 60}, {kHiCut, 3500}, {kHiSat, 50},
            {kWarp, 25}, {kRpm, 60}, {kFlutter, 40}, {kFlutRate, 11}, {kWow, 20}, {kLoCut, 300}, {kHiCut2, 4000},
            {kHum, 20}, {kHeadBuzz, 0}, {kHiss, 45}, {kCrackle, 10}, {kNoise, 40},
            {kMicOn, 1}, {kMicModel, 0}, {kMicType, 2}, {kMicGain, 3}, {kMono, 1},
            {kAgcModel, 0}, {kAgc, 60} });
        // Wide, slow and hazy, with hardly any noise.
        program("Dream Sequence", {
            {kDrive, 15}, {kTone, 10}, {kWear, 10}, {kComp, 30}, {kHiCut, 11000}, {kHiSat, 25},
            {kWarp, 55}, {kRpm, 33}, {kShape, 20}, {kFlutter, 10}, {kWow, 55}, {kWowRate, 0.25f}, {kFlutter2, 10},
            {kHum, 0}, {kHeadBuzz, 0}, {kHiss, 15}, {kCrackle, 5}, {kNoise, 35},
            {kChorusOn, 1}, {kChoMix, 70}, {kChoSpeed, 0.2f}, {kChoDelay, 22}, {kChoWidth, 100},
            {kDrift, 15} });
        // Just a little tape: gentle clipping and the faintest wobble, no noise.
        program("Subtle Glue", {
            {kDrive, 20}, {kWear, 5}, {kComp, 35}, {kHiCut, 15000}, {kLoSat, 15}, {kHighs, -1}, {kHiSat, 20},
            {kWarp, 10}, {kFlutter, 10}, {kWow, 8}, {kFlutter2, 8}, {kLoCut, 30}, {kHiCut2, 16000},
            {kNoiseOn, 0} });

        // Added in 1.2, built around the deck section.
        // A good tape in a deck that cannot quite follow it.
        program("Bad Tracking", {
            {kDrive, 20}, {kComp, 25}, {kHiCut, 14000}, {kWarp, 10}, {kFlutter, 10}, {kWow, 5}, {kFlutter2, 5},
            {kHiCut2, 15000}, {kHum, 5}, {kHeadBuzz, 45}, {kHiss, 20}, {kCrackle, 10}, {kDropouts, 15},
            {kTracking, 65}, {kBreathing, 25} });
        // Recorded at the slowest speed to fit six hours on the tape: about 100 Hz to 6 kHz,
        // more hiss and more wobble.
        program("Six Hour Tape", {
            {kDrive, 30}, {kWear, 30}, {kComp, 40}, {kHiCut, 6000},
            {kWarp, 50}, {kFlutter, 50}, {kWow, 35}, {kFlutter2, 30}, {kLoCut, 100}, {kHiCut2, 8000},
            {kHum, 20}, {kHiss, 65}, {kCrackle, 20},
            {kAgcModel, 0}, {kAgc, 35}, {kDrift, 25} });
        // A camcorder at the back of the hall: the level control rides everything.
        program("Wedding Video", {
            {kDrive, 30}, {kComp, 20}, {kHiCut, 8500}, {kWarp, 20}, {kFlutter, 25},
            {kHum, 25}, {kHeadBuzz, 15}, {kHiss, 40}, {kNoise, 40}, {kNoisePos, 1},
            {kMicOn, 1}, {kMicModel, 0}, {kMicType, 0}, {kMicGain, 1},
            {kAgcModel, 0}, {kAgc, 90}, {kDrift, 20} });
        // The best VHS could do, the Hi-Fi track on a good day: wide, steady, quiet, breathing a little.
        program("Hi-Fi Master", {
            {kDrive, 15}, {kWear, 0}, {kComp, 25}, {kHiCut, 17000}, {kLoSat, 15}, {kHighs, 0}, {kHiSat, 15},
            {kWarp, 5}, {kFlutter, 5}, {kWow, 0}, {kFlutter2, 0}, {kLoCut, 20}, {kHiCut2, 18000},
            {kHum, 0}, {kHeadBuzz, 8}, {kHiss, 12}, {kCrackle, 0},
            {kBreathing, 35} });
        // The top edge of the tape is creased: the sound comes and goes.
        program("Edge Damage", {
            {kDrive, 25}, {kHiCut, 8000}, {kWarp, 30}, {kShape, 60}, {kFlutter, 30},
            {kHiss, 40}, {kCrackle, 25}, {kDropouts, 30},
            {kAgcModel, 0}, {kAgc, 15}, {kDrift, 95} });
        // Taped from the crowd on a boombox and copied a few times.
        program("Bootleg Concert", {
            {kDrive, 60}, {kWear, 25}, {kComp, 50}, {kHiCut, 7000}, {kLoSat, 45}, {kHiSat, 50},
            {kWarp, 30}, {kFlutter, 35}, {kHum, 15}, {kHiss, 50}, {kNoise, 35}, {kNoisePos, 1},
            {kMicOn, 1}, {kMicModel, 0}, {kMicType, 3}, {kMicGain, 2}, {kMono, 1},
            {kAgcModel, 0}, {kAgc, 65}, {kDrift, 30} });
        // Local cable studio: hum, buzz and a level control doing its best.
        program("Public Access", {
            {kDrive, 35}, {kComp, 40}, {kHiCut, 7000}, {kWarp, 20}, {kFlutter, 20}, {kLoCut, 120},
            {kHum, 50}, {kBuzz, 35}, {kHeadBuzz, 25}, {kHiss, 40}, {kMono, 1},
            {kAgcModel, 0}, {kAgc, 50}, {kDrift, 10} });

        // Everything neutral: the sound passes through unchanged. A blank tape to start from.
        program("Init", {
            {kDrive, 0}, {kTone, 0}, {kWear, 0}, {kComp, 0}, {kHiCut, 18000},
            {kSatOn, 0}, {kLows, 0}, {kLoSat, 0}, {kHighs, 0}, {kHiSat, 0},
            {kWarp, 0}, {kShape, 0}, {kFlutter, 0}, {kWow, 0}, {kFlutter2, 0}, {kLoCut, 20}, {kHiCut2, 18000},
            {kHum, 0}, {kBuzz, 0}, {kHeadBuzz, 0}, {kHiss, 0}, {kCrackle, 0}, {kDropouts, 0},
            {kDeckOn, 0} });

        // Added in 1.4, after Init: the programs above keep their numbers, and none of them
        // uses Scrape or Whistle, so a project saved with an older version sounds as it did.
        // A tape gone sticky with age, dragging and scraping through the machine.
        program("Sticky Tape", {
            {kDrive, 30}, {kWear, 45}, {kComp, 40}, {kHiCut, 6500}, {kHiSat, 45},
            {kWarp, 45}, {kRpm, 40}, {kShape, 70}, {kFlutter, 50}, {kFlutRate, 13}, {kScrape, 65},
            {kWow, 25}, {kFlutter2, 25}, {kHiCut2, 8000},
            {kHum, 10}, {kHiss, 45}, {kCrackle, 20}, {kDropouts, 45},
            {kAgcModel, 0}, {kAgc, 20}, {kDrift, 35} });
        // The living-room set: a small speaker, mains hum and the whistle of the picture tube.
        program("Tube TV", {
            {kDrive, 30}, {kWear, 15}, {kComp, 45}, {kHiCut, 7500},
            {kWarp, 15}, {kFlutter, 20}, {kWow, 10}, {kFlutter2, 10}, {kLoCut, 140}, {kHiCut2, 9000},
            {kHum, 30}, {kBuzz, 25}, {kHeadBuzz, 0}, {kHiss, 35}, {kCrackle, 5}, {kWhistle, 60},
            {kMicOn, 1}, {kMicModel, 0}, {kMicType, 3}, {kMono, 1},
            {kAgcModel, 0}, {kAgc, 25}, {kDrift, 10} });
        // A good Hi-Fi deck with tired heads: buzz that rides on the music and sinks in the
        // pauses, the right channel crackling, now and then a moment on the backup track.
        program("Worn Heads", {
            {kDrive, 15}, {kWear, 5}, {kComp, 25}, {kHiCut, 15000}, {kHiSat, 20},
            {kWarp, 8}, {kFlutter, 8}, {kWow, 0}, {kFlutter2, 0}, {kLoCut, 25}, {kHiCut2, 16000},
            {kHum, 0}, {kHeadBuzz, 55}, {kHiss, 20}, {kCrackle, 5},
            {kTracking, 45}, {kBreathing, 45} });
        // Oxide on the heads: dull, and the highs keep falling away.
        program("Dirty Heads", {
            {kDrive, 30}, {kWear, 35}, {kComp, 40}, {kHiCut, 5000}, {kHighs, -6}, {kHiSat, 45},
            {kWarp, 30}, {kFlutter, 30}, {kScrape, 30}, {kWow, 20}, {kFlutter2, 15}, {kLoCut, 80}, {kHiCut2, 7000},
            {kHum, 15}, {kHiss, 50}, {kCrackle, 15}, {kDropouts, 75},
            {kAgcModel, 0}, {kAgc, 25}, {kDrift, 45} });

        // Added in 1.5, after the others: no older program uses NR or Sat Type = VHS.
        // An early stereo rental: the narrow linear tracks with noise reduction on both
        // sides. The tape has aged in between, so the decoder mistracks: dull, the highs
        // ducking in the quiet parts.
        program("Stereo Rental", {
            {kDrive, 20}, {kWear, 30}, {kComp, 25}, {kHiCut, 7000}, {kHiSat, 25},
            {kWarp, 15}, {kFlutter, 15}, {kWow, 10}, {kFlutter2, 10}, {kLoCut, 60}, {kHiCut2, 9000},
            {kHum, 10}, {kHiss, 60}, {kCrackle, 5}, {kDropouts, 30},
            {kDrift, 20}, {kNr, 3} });
        // The same kind of tape on a deck without the switch: recorded with noise reduction,
        // played without. Bright, airy, a little squashed, all of the hiss.
        program("Wrong Switch", {
            {kDrive, 15}, {kComp, 20}, {kHiCut, 11000},
            {kWarp, 10}, {kFlutter, 10}, {kWow, 5}, {kFlutter2, 5}, {kLoCut, 50}, {kHiCut2, 12000},
            {kHum, 5}, {kHiss, 50}, {kCrackle, 5},
            {kNr, 1} });
        // The tape model at its two speeds. SP: a rental played a little hot. The loud parts
        // lose their top and soften; the quiet parts stay clear.
        program("SP Tape", {
            {kDrive, 10}, {kComp, 15}, {kHiCut, 12000}, {kSatType, 1}, {kLoSat, 45}, {kHiSat, 0}, {kHighs, 0},
            {kWarp, 10}, {kFlutter, 12}, {kWow, 5}, {kFlutter2, 5}, {kLoCut, 60}, {kHiCut2, 12000},
            {kHum, 5}, {kHiss, 40}, {kCrackle, 5} });
        // EP: six hours on one cassette. A third of the speed, so the highs give way three
        // times sooner and the top of the band is gone.
        program("EP Tape", {
            {kDrive, 10}, {kComp, 20}, {kHiCut, 8000}, {kSatType, 1}, {kLoSat, 50}, {kHiSat, 100}, {kHighs, 0},
            {kWarp, 20}, {kFlutter, 25}, {kWow, 10}, {kFlutter2, 10}, {kLoCut, 90}, {kHiCut2, 8000},
            {kHum, 8}, {kHiss, 55}, {kCrackle, 8}, {kDropouts, 15} });

        // Older programs again, with the tape model as their saturator (Sat Type = VHS: Lo Sat
        // is the record level, Hi Sat the tape speed, Split the bias). Everything else is as
        // in the program they are named after, which stays as it was.
        // Every pass recorded hot on a dubbing deck with too much bias.
        program("VHS - 3rd Gen Copy", {
            {kDrive, 45}, {kWear, 45}, {kComp, 45}, {kHiCut, 6000},
            {kSatType, 1}, {kLoSat, 80}, {kHiSat, 30}, {kSplit, 4000}, {kHighs, -4},
            {kWarp, 50}, {kFlutter, 45}, {kWow, 45}, {kFlutter2, 40}, {kLoCut, 100}, {kHiCut2, 7000},
            {kHum, 25}, {kHeadBuzz, 25}, {kHiss, 55}, {kCrackle, 30},
            {kAgcModel, 0}, {kAgc, 40}, {kDrift, 35} });
        // SP, recorded hot, dull from too much bias.
        program("VHS - Training Video", {
            {kDrive, 40}, {kTone, -15}, {kWear, 40}, {kComp, 35}, {kHiCut, 4500},
            {kSatType, 1}, {kLoSat, 70}, {kHiSat, 0}, {kSplit, 5000}, {kHighs, -3},
            {kWarp, 45}, {kRpm, 34}, {kShape, 60}, {kFlutter, 35}, {kFlutRate, 7},
            {kWow, 40}, {kWowRate, 0.5f}, {kFlutter2, 30}, {kFlut2Rate, 12}, {kLoCut, 150}, {kHiCut2, 6000},
            {kHum, 30}, {kBuzz, 15}, {kHeadBuzz, 35}, {kHiss, 50}, {kCrackle, 20}, {kNoise, 40}, {kNoisePos, 1},
            {kMono, 1}, {kDropouts, 25},
            {kAgcModel, 0}, {kAgc, 75}, {kDrift, 40} });
        // EP, bias right, a little over reference level.
        program("VHS - Six Hour Tape", {
            {kDrive, 30}, {kWear, 30}, {kComp, 40}, {kHiCut, 6000},
            {kSatType, 1}, {kLoSat, 60}, {kHiSat, 100}, {kHighs, 0},
            {kWarp, 50}, {kFlutter, 50}, {kWow, 35}, {kFlutter2, 30}, {kLoCut, 100}, {kHiCut2, 8000},
            {kHum, 20}, {kHiss, 65}, {kCrackle, 20},
            {kAgcModel, 0}, {kAgc, 35}, {kDrift, 25} });
        // Too little bias: brighter and grittier.
        program("VHS - Sticky Tape", {
            {kDrive, 30}, {kWear, 45}, {kComp, 40}, {kHiCut, 6500},
            {kSatType, 1}, {kLoSat, 65}, {kHiSat, 20}, {kSplit, 1200}, {kHighs, -2},
            {kWarp, 45}, {kRpm, 40}, {kShape, 70}, {kFlutter, 50}, {kFlutRate, 13}, {kScrape, 65},
            {kWow, 25}, {kFlutter2, 25}, {kHiCut2, 8000},
            {kHum, 10}, {kHiss, 45}, {kCrackle, 20}, {kDropouts, 45},
            {kAgcModel, 0}, {kAgc, 20}, {kDrift, 35} });
        // Too much bias: the highs give way sooner still.
        program("VHS - Dirty Heads", {
            {kDrive, 30}, {kWear, 35}, {kComp, 40}, {kHiCut, 5000},
            {kSatType, 1}, {kLoSat, 60}, {kHiSat, 40}, {kSplit, 6000}, {kHighs, -4},
            {kWarp, 30}, {kFlutter, 30}, {kScrape, 30}, {kWow, 20}, {kFlutter2, 15}, {kLoCut, 80}, {kHiCut2, 7000},
            {kHum, 15}, {kHiss, 50}, {kCrackle, 15}, {kDropouts, 75},
            {kAgcModel, 0}, {kAgc, 25}, {kDrift, 45} });

        // Added in 1.6, after the others: the five programs with the Mic on, with the modelled
        // microphones (Mic Model = Modeled). Everything else is as in the program they are named
        // after, which keeps the Simple ones.
        // The camera across the room: with its distance (kCamDistanceDb) the levels_test tone sits
        // 3 dB past the top of its ALC's range (in Wedding Video 2 dB): squashed, with a little crunch.
        program("Mic - Camcorder 89", {
            {kDrive, 35}, {kComp, 30}, {kHiCut, 8000}, {kWarp, 20}, {kFlutter, 35},
            {kHiss, 45}, {kHeadBuzz, 30}, {kNoisePos, 1}, {kNoise, 35},
            {kMicOn, 1}, {kMicModel, 1}, {kMicType, 0}, {kMicGain, 2},
            {kAgcModel, 0}, {kAgc, 80}, {kDrift, 10} });
        // Into a carbon handset, down the line.
        program("Mic - Answer Machine", {
            {kDrive, 50}, {kWear, 30}, {kComp, 60}, {kHiCut, 3500}, {kHiSat, 50},
            {kWarp, 25}, {kRpm, 60}, {kFlutter, 40}, {kFlutRate, 11}, {kWow, 20}, {kLoCut, 300}, {kHiCut2, 4000},
            {kHum, 20}, {kHeadBuzz, 0}, {kHiss, 45}, {kCrackle, 10}, {kNoise, 40},
            {kMicOn, 1}, {kMicModel, 1}, {kMicType, 2}, {kMicGain, 3}, {kMono, 1},
            {kAgcModel, 0}, {kAgc, 60} });
        program("Mic - Wedding Video", {
            {kDrive, 30}, {kComp, 20}, {kHiCut, 8500}, {kWarp, 20}, {kFlutter, 25},
            {kHum, 25}, {kHeadBuzz, 15}, {kHiss, 40}, {kNoise, 40}, {kNoisePos, 1},
            {kMicOn, 1}, {kMicModel, 1}, {kMicType, 0}, {kMicGain, 1},
            {kAgcModel, 0}, {kAgc, 90}, {kDrift, 20} });
        // The radio cassette's own level control rides the crowd.
        program("Mic - Bootleg Concert", {
            {kDrive, 60}, {kWear, 25}, {kComp, 50}, {kHiCut, 7000}, {kLoSat, 45}, {kHiSat, 50},
            {kWarp, 30}, {kFlutter, 35}, {kHum, 15}, {kHiss, 50}, {kNoise, 35}, {kNoisePos, 1},
            {kMicOn, 1}, {kMicModel, 1}, {kMicType, 3}, {kMicGain, 2}, {kMono, 1},
            {kAgcModel, 0}, {kAgc, 65}, {kDrift, 30} });
        program("Mic - Tube TV", {
            {kDrive, 30}, {kWear, 15}, {kComp, 45}, {kHiCut, 7500},
            {kWarp, 15}, {kFlutter, 20}, {kWow, 10}, {kFlutter2, 10}, {kLoCut, 140}, {kHiCut2, 9000},
            {kHum, 30}, {kBuzz, 25}, {kHeadBuzz, 0}, {kHiss, 35}, {kCrackle, 5}, {kWhistle, 60},
            {kMicOn, 1}, {kMicModel, 1}, {kMicType, 3}, {kMono, 1},
            {kAgcModel, 0}, {kAgc, 25}, {kDrift, 10} });

        // Added in 1.7, after the others: the Hi-Fi track (Sat Type = Hi-Fi: Lo Sat is the record
        // level, 0 dB at 56 %; Hi Sat the carrier, 0.3 dB down per %; Split the playback
        // calibration; Crackle how near the deck sits to the click threshold). The rotating heads have no wow or flutter to speak of, so the wobble is
        // at rest in all five. 17 dB down the deck goes to the linear track.
        // A good deck and a new tape: nothing but the FM channel's own faint hiss (Hiss 9 % is a
        // real deck's, 55 dB under reference).
        program("Hi-Fi - Clean Deck", {
            {kDrive, 0}, {kWear, 0}, {kComp, 0}, {kHiCut, 18000},
            {kSatType, 2}, {kLoSat, 56}, {kHiSat, 0}, {kHighs, 0},
            {kWarp, 0}, {kFlutter, 0}, {kWow, 0}, {kFlutter2, 0}, {kLoCut, 20}, {kHiCut2, 18000},
            {kHum, 0}, {kHeadBuzz, 0}, {kHiss, 9}, {kCrackle, 0} });
        // Recorded 10 dB over: the deviation limiter's hard ceiling on every loud hit.
        program("Hi-Fi - Hot Signal", {
            {kDrive, 20}, {kWear, 0}, {kComp, 15}, {kHiCut, 18000},
            {kSatType, 2}, {kLoSat, 84}, {kHiSat, 0}, {kHighs, 0},
            {kWarp, 0}, {kFlutter, 0}, {kWow, 0}, {kFlutter2, 0}, {kLoCut, 20}, {kHiCut2, 18000},
            {kHum, 0}, {kHeadBuzz, 10}, {kHiss, 12}, {kCrackle, 0} });
        // A tape that has been through a lot of machines: a weak carrier, a steady scatter of
        // clicks, more when it wanders, noise that comes and goes with the music.
        program("Hi-Fi - Worn Rental", {
            {kDrive, 15}, {kWear, 5}, {kComp, 20}, {kHiCut, 16000},
            {kSatType, 2}, {kLoSat, 60}, {kHiSat, 40}, {kHighs, 0},
            {kWarp, 0}, {kFlutter, 0}, {kWow, 0}, {kFlutter2, 0}, {kLoCut, 30}, {kHiCut2, 16000},
            {kHum, 5}, {kHeadBuzz, 25}, {kHiss, 50}, {kCrackle, 25},
            {kDropouts, 30}, {kDrift, 30}, {kBreathing, 20} });
        // The tracking knob is off: the buzz of the head switch, bursts of clicks, and every
        // so often the dull mono linear track takes over.
        program("Hi-Fi - Tracking Lost", {
            {kDrive, 15}, {kWear, 5}, {kComp, 20}, {kHiCut, 16000},
            {kSatType, 2}, {kLoSat, 58}, {kHiSat, 45}, {kHighs, 0},
            {kWarp, 0}, {kFlutter, 0}, {kWow, 0}, {kFlutter2, 0}, {kLoCut, 30}, {kHiCut2, 16000},
            {kHum, 5}, {kHeadBuzz, 55}, {kHiss, 50}, {kCrackle, 22},
            {kDropouts, 40}, {kDrift, 40}, {kTracking, 70} });
        // Recorded on one deck, played on another: the expander disagrees with the compressor,
        // so the level is off and the sound pumps.
        program("Hi-Fi - Wrong Deck", {
            {kDrive, 10}, {kWear, 0}, {kComp, 10}, {kHiCut, 18000},
            {kSatType, 2}, {kLoSat, 62}, {kHiSat, 20}, {kSplit, 4500}, {kHighs, 0},
            {kWarp, 0}, {kFlutter, 0}, {kWow, 0}, {kFlutter2, 0}, {kLoCut, 20}, {kHiCut2, 18000},
            {kHum, 0}, {kHeadBuzz, 15}, {kHiss, 55}, {kCrackle, 10},
            {kBreathing, 85} });
    }

    // ----- state ------------------------------------------------------------------
    std::thread notifier_;
    std::atomic<bool> quit_{false}, pushPending_{false};
    std::mutex quitMutex_;                                // with quitCv_: wakes the notifier when the plugin closes
    std::condition_variable quitCv_;
    std::atomic<bool> started_{false};                    // the host has started us: the notifier may speak
    std::atomic<bool> resetPending_{false};               // a reset is due at the start of the next block
    std::atomic<bool> resetWanted_{false};                // the host asked for one: fade out first, then reset
    static constexpr uint32_t kLoadEvent = 1u << 8;
    std::atomic<uint32_t> loadState_{0};                  // programs or states being written; moves at each start and end
    std::atomic<bool> suspended_{false};                  // the host has suspended us
    std::atomic<float> srWanted_{0.0f};                   // the sample rate the host announced
    std::atomic<bool> tellHost_{false};                   // the host asked for the pending reset: send it the values after
    std::atomic<bool> animate_{true};                     // the Animation switch
    std::atomic<float> visReel_{0.0f}, visLevel_{0.0f};   // audio thread -> notifier thread
    std::atomic<int> wantLatency_{12};                    // resting delay of the tape path, in samples

    // which stages have work to do (set in controlTick)
    bool  humOn_ = false, buzzOn_ = false, headOn_ = false, hissOn_ = false, crackleOn_ = false;
    bool  tiltOn_ = false, driveOn_ = false, slewOn_ = false, loSatOn_ = false, hiSatOn_ = false;
    bool  boost_ = false, dropOn_ = false, whistleOn_ = false, scrapeOn_ = false, breathOn_ = false;
    NoiseRed nrEnc_, nrDec_;                 // noise reduction: record side, playback side
    bool  nrEncOn_ = false, nrDecOn_ = false, nrEncSeed_ = false, nrDecSeed_ = false;
    float nrEncMix_ = 0.0f, nrDecMix_ = 0.0f, nrFade_ = 0.0f;   // how far each side is faded in
    float nrEncGoal_ = 0.0f;
    bool  nrEncRun_ = false, nrDecRun_ = false;                 // the side is on or still fading out
    TapeSat tapeSat_;                                           // Sat Type = VHS
    bool  tape_ = false;
    bool  choActive_ = false, dryActive_ = false, dryStill_ = false;
    int   fadeHold_ = 0;                                  // control ticks the output stays shut after a reset
    int   choWait_ = 0;                                   // ... and then a few more for the chorus swing
    bool  gateOn_ = false;                                // after those, the output rises to the fade-in by gate_
    float gateS_ = 0.0f;
    Ramp  gate_;
    int   dryHold_ = 0, choHold_ = 0;                     // ticks Mix / the chorus wait for their delay to fill
    int   fadeOut_ = 0;                                   // ticks left of the fade-out before a reset
    bool  resetNextTick_ = false;                         // the fade-out is at its end: ask for the reset at the next tick

    // what the screen shows (audio thread)
    bool  playing_ = false;
    float inPeak_ = 0, outPeak_ = 0, inEnv_ = 0, outEnv_ = 0, spin_ = 0, reelPhase_ = 0;

    float sr_ = 0.0f;
    int   ctrlCount_ = 0;
    float lastNorm_[kNumParams] = {};   // parameter values readParams() last saw
    int   units_[kNumParams] = {};      // 0 none, 1 %, 2 dB, 3 Hz, 4 ms, 5 turns per minute, shown in Hz (see formatValue)

    float tgt_[kNumCtl] = {}, cur_[kNumCtl] = {};
    float rampT_[kNumRamp] = {}, rampS_[kNumRamp] = {};
    Ramp  ramp_[kNumRamp];

    // chorus
    PairDelay choDelay_;
    Ramp  choD_[2];
    float choPhase_ = 0.0f, choSpeed_ = 0.6f, choWidth_ = 0.7f;
    float choDelayTarget_ = 500.0f, choBase_ = 500.0f;

    // mic (its filters are the last members, see there)
    int   micType_ = -1, micWant_ = 0;    // the mic that is in; the one Mic Type and Mic Model ask for (see setMic)
    bool  micOn_ = false, micSwap_ = false;   // the Mic switch; the mic is out for a change of type or model
    bool  micSimple_ = false;                 // the mic that is in is a Simple one
    bool  micRun_ = false;                    // it runs (heard, fading, or settling unheard)
    int   micHold_ = 0, micHoldTicks_ = 55;   // control ticks it settles unheard before it fades in (20 ms)
    int   micHoldCamTicks_ = 276;              // the same for the Camcorder (100 ms, see micHoldFor)

    // preamp, wear, compressor
    Stereo tiltLp_ = {0.0f, 0.0f}, slew_ = {0.0f, 0.0f};
    float tiltCoeff_ = 0.1f;
    float env_ = 0.0f, envAttack_ = 0.01f, envRelease_ = 0.0002f;
    float compGain_ = 1.0f, compTarget_ = 1.0f, compSmooth_ = 0.02f;
    float slewStep_ = 2.0f;

    // HQ (2x oversampling of the distorting stages)
    bool  hq_ = false;
    float nlSr_ = 44100.0f;                  // the rate those stages run at
    HalfBand hbUp_, hbDown_, hbDry_;         // up, down, and the same phase shift for the dry copy

    // saturator and filters
    Stereo splitLp_ = {0.0f, 0.0f}, loCutLp_ = {0.0f, 0.0f};
    float splitCoeff_ = 0.3f, loCutCoeff_ = 0.01f;
    PairBiquad lpfA_, lpfB_, hiCut2_;

    // wobble
    PairDelay wf1Delay_, wf2Delay_;
    PairDelay dryDelay_;                 // the untouched input, for Mix
    Ramp  dryD_;                         // its delay: the resting length of the two wobble delays
    float dryJump_ = 0, dryXf_ = 0;      // its crossfade to a far new length (as TapeSpeed::jump, xf)
    int   dryFill_ = 0;                  // control ticks of sound it has taken in since it was switched in
    TapeSpeed wf1_, wf2_;
    float dev1Slow_ = 0, rate1Slow_ = 0.55f, shape1_ = 0, dev1Fast_ = 0, rate1Fast_ = 9;
    float dev2Slow_ = 0, rate2Slow_ = 0.8f,  dev2Fast_ = 0, rate2Fast_ = 14;
    float scrapeA_ = 0, scrapeB_ = 0, scrapeLo_ = 0.08f, scrapeHi_ = 0.35f;   // scrape flutter noise and its band
    Xorshift32 scrapeRng_{0x5c7a9e01u};

    // noise
    bool  noiseActive_ = true, noisePre_ = false, headFlip_ = false;
    float humPh_ = 0, buzzPh_ = 0, headPh_ = 0, humInc_ = 0, buzzInc_ = 0, headInc_ = 0;
    float buzzLp_ = 0, headLp_ = 0, headHp_ = 0;
    float headAmp_ = 0;                                       // head buzz level after Breathing
    float whistleS_ = 0, whistleC_ = 1.0f, whistleE_ = 0, whistleNorm_ = 1.0f;   // line whistle oscillator
    Stereo hissLp_ = {0.0f, 0.0f}, click_ = {0.0f, 0.0f};
    Xorshift32 hissRng_[2] = { Xorshift32(0x51f15eedu), Xorshift32(0x0c0ffee1u) };
    Xorshift32 crackleRng_{0x7a9e5eedu};
    uint32_t crackleThresh_ = 0;

    // deck
    PairBiquad fbLp_;
    Stereo fbHp_ = {0.0f, 0.0f}, edgeLp_ = {0.0f, 0.0f}, hsHeld_ = {0.0f, 0.0f};
    float fbHpCoeff_ = 0.01f, edgeCoeff_ = 0.15f;
    float linAmt_ = 0, linTarget_ = 0;                        // how much of the backup track is heard
    float hissDyn_ = 1.0f;
    float breathEnv_ = 0, breathAtk_ = 0.07f, breathRel_ = 0.005f;   // input level as the noise reduction sees it
    Stereo inMax_ = {0.0f, 0.0f};                                    // loudest input sample since the last tick
    bool  agcActive_ = false, agcBlend_ = false, agcMRun_ = false;   // (the last two: see agcModeled; here, in the padding)
    float agcAmt_ = 0, agcEnv_ = kRefLevel, agcGain_ = 1.0f, agcTarget_ = 1.0f, agcRelease_ = 0.0001f;
    int   agcHold_ = 0, agcHoldTicks_ = 4000;
    bool  edgeActive_ = false;
    float edgeN1_ = 0, edgeN2_ = 0, edgeMuffle_ = 0, edgeGain_ = 1.0f;
    Xorshift32 edgeRng_{0xed9e5eedu};
    float trackErr_ = 0;                                      // Tracking amount, 0..1
    float hsLen_[2] = {0, 0}, hsFrac_ = 0, hsPhase_ = 0, hsInc_ = 0;
    int   hsLeft_ = 0;
    bool  hsFlip_ = false, hsOn_ = false;
    int   fbState_ = 0, fbTicks_ = 0, fbLinearTicks_ = 0;     // 0 idle, 1 static, 2 on the linear track, 3 static,
                                                              // 4 static in the right channel only
    float fbStatic_ = 0, staticAmp_ = 0, staticL_ = 1.0f, fbMono_ = 0;
    Xorshift32 fbRng_{0x7ac61e55u}, staticRng_{0x57a71c00u};

    // dropouts
    Stereo dropLp_ = {0.0f, 0.0f};
    Xorshift32 dropRng_{0xd20b0075u};
    float dropAmount_ = 0, dropGain_ = 1.0f, dropTarget_ = 1.0f;
    int   dropTicks_ = 0;

    // The mic model, last: it is large (its filters), and placed among the other members it
    // moved the ones after it further into the object; with it here the sample loop compiles
    // 13 instructions a sample shorter with the Mic off (32-bit ARM build, counted). The Simple
    // filters go with it.
    SimpleMic simple_;
    MicModel mic_;

    // AGC Model = Modeled (1.6), after the mic for the same reason: the members before keep
    // their places
    ModeledAgc agcm_;
    bool  agcWantM_ = true;
    float agcB_ = 1.0f, agcBTo_ = 1.0f, agcBInc_ = 0.0f;   // how much of the Modeled one is heard, now and at the next tick
    float agcXfStep_ = 0.01f;                             // crossfade step per tick
    int   agcSettle_ = 0, agcSettleTicks_ = 2757, agcSettleSimpleTicks_ = 56;   // ticks the new one settles unheard before the crossfade

    // Sat Type = Hi-Fi (1.7), last for the same reason
    HiFiPath hifiPath_;
    bool  hifiType_ = false;   // the type in force (changes as tape_ does)
    bool  hifiOn_ = false;     // ... with the Saturator on: the path has taken the seven knobs over
    bool  hifiRun_ = false;    // the path runs (heard, or fading in or out)

    // The picture effects (1.7), last for the same reason: what the audio thread hands the notifier
    // (see the end of process()), and the two counters it keeps for that
    std::atomic<float> visHiss_{0.0f}, visHead_{0.0f};
    std::atomic<uint32_t> visDrops_{0}, visLost_{0};
    uint32_t dropEvents_ = 0, lostEvents_ = 0;   // dropouts started; times the Hi-Fi track was lost
    bool  visLinear_ = false;                    // the Hi-Fi path was on the linear track at the end of the last block
};

// The framework's dispatcher with five additions:
//  - Choosing a program leaves the Animation, HQ and Visual Vibes switches as they were. The
//    framework sets every value a program lists, and the parameters added after Animation have to
//    be in that list, so the program is first told the present position of the three switches.
//  - A saved state from before 1.4 does not mention the parameters added since. They go to
//    their resting values before any state is loaded (HQ off, Mic Model and AGC Model Simple: a new instance
//    has them otherwise; Visual Vibes on, as in a new instance), so such a state sounds as it was
//    saved whatever this instance was doing before; a newer state then sets them.
//  - A state that cannot be loaded (cut short, damaged) leaves every setting as it was.
//  - While a program or a state is being written, the audio thread takes in none of the new
//    values: they arrive with the reset, after the old sound has faded out.
//  - When the host suspends the plugin, the notifier thread stops calling the host.
static VstIntPtr vhsDispatcher(AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
    VHSVibes* p = static_cast<VHSVibes*>(static_cast<mpc::Plugin*>(e->object));
    if (p == nullptr) return 0;
    if (opcode == effMainsChanged) { if (value != 0) p->resume(); else p->goQuiet(); }
    if (opcode != effSetProgram && opcode != effSetChunk) return mpc::detail::dispatcher(e, opcode, index, value, ptr, opt);

    // A program or a state: many values at once, then a reset. The audio thread takes none of
    // them in until all are written and the reset has been asked for. (The framework asks for
    // that reset too, at the start of the next block; asked for here, before the load counts as
    // finished, no block can take the new values in before it.)
    p->beginLoad();
    // A state the framework turns down part of the way (cut short, or damaged) has set some
    // values by then, and the resting values above are in too. Every setting goes back to what
    // it was, before the load counts as finished: the audio thread sees none of it.
    float before[kNumParams];
    for (int i = 0; i < kNumParams; ++i) before[i] = p->getNormalised(i);
    if (opcode == effSetProgram) p->holdSwitches((int)value);
    else if (ptr != nullptr && value >= 12 && std::memcmp(ptr, "MPCP", 4) == 0)
        for (int i = kScrape; i < kNumParams; ++i)   // a state from before HQ existed: off; from before Mic Model or AGC Model: Simple
            p->set(i, i == kHq || i == kMicModel || i == kAgcModel ? 0.0f : p->param(i).def);
    const VstIntPtr result = mpc::detail::dispatcher(e, opcode, index, value, ptr, opt);
    if (opcode == effSetChunk && result == 0)
        for (int i = 0; i < kNumParams; ++i) if (isSetting(i)) p->setNormalised(i, before[i]);
    p->endLoad(opcode == effSetProgram ? value >= 0 && value < e->numPrograms : result != 0);
    return result;
}

// The exported entry point: MPC_PLUGIN_ENTRY of the framework, plus the dispatcher above. Keep it last.
extern "C" MPC_EXPORT AEffect* VSTPluginMain(audioMasterCallback host)
{
    if (host != nullptr && host(nullptr, audioMasterVersion, 0, 0, nullptr, 0.0f) == 0) return nullptr;   // VST1-only host
    AEffect* e = mpc::detail::createEffect(new VHSVibes(), host);
    if (e != nullptr) e->dispatcher = &vhsDispatcher;
    return e;
}
