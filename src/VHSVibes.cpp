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
//      -> MIC         three-band EQ curve of a cheap microphone ("re-recorded through a mic")
//      -> AGC         (deck) the recorder's automatic level control: ducks fast, creeps back up
//      -> PREAMP      tilt tone control into a soft clipper (the VCR's input stage). Between
//                     the two: NR ENCODE, (deck) B-type noise reduction, record side: lifts
//                     quiet highs
//      -> SLEW        slew limiter: the signal cannot change faster than a set rate
//      -> COMP        one-knob compressor with make-up gain
//      -> 2-BAND SAT  crossover at Split; lows and highs get their own level and soft clip.
//                     Or, with Sat Type = VHS, a model of the linear track's tape (see TapeSat)
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
//      -> WHISTLE     the TV set's line whistle, added here so no filter or wobble touches it
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
// Compatibility: parameters are only ever appended, and a new one rests at 0 where the plugin
// sounds as it did without it. Programs 0 to 20 keep their numbers and leave the 1.4 knobs at
// 0, so a project saved with an older version loads with every setting in place.
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
    kTone,         // -100..100  tilt: negative = darker, positive = brighter
    kWear,         // %     slew limiter amount (tape age / generation loss)
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
    kRpm,          // rpm   slow wobble rate (one lurch per reel turn)
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
    kSatType,      // choice the saturator stage: 2-Band (as before) or VHS (the linear track's tape;
                   //        Lo Sat = record level, Hi Sat = tape speed SP..EP, Split = bias)
    kNumParams
};

// The two meters are not settings.
inline bool isSetting(int i) { return i != kVisReel && i != kVisLevel; }
constexpr int kReelFrames  = 24;           // frames in the skin's reel / grid film strips
constexpr int kLevelFrames = 16;           // frames in the skin's sun film strips
constexpr float kReelStepsPerSec = 3.6f;   // 0.6 turns per second, six teeth per turn

constexpr int   kCtrl      = 16;          // control-rate interval in samples (must be a power of two)
constexpr float kCtrlInv   = 1.0f / (float)kCtrl;
constexpr float kSmooth    = 0.05f;       // per-tick smoothing of control values (~7 ms)
constexpr int   kTapeDelaySamples   = 8192;   // per wobble stage (186 ms at 44.1 kHz)
constexpr int   kChorusDelaySamples = 2048;
constexpr float kMaxExcursionSec    = 0.045f; // largest wobble swing we allow, in seconds of delay
#ifndef VHS_VIBES_SWING_ROOM            // (the test bench overrides these two to compare against 1.2)
#define VHS_VIBES_SWING_ROOM 2.2f
#endif
constexpr float kSwingRoom          = VHS_VIBES_SWING_ROOM;   // resting delay = this many times the slow wobble's own swing
constexpr bool  kReportLatency      = true;   // tell the host how late the output is (see notifierMain)
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
        case kMicType: case kMicGain:                                       return gMic;
        case kDeckOn: case kAgc: case kDrift: case kTracking: case kBreathing: return gDeck;
        case kHq:                                                           return gQuality;
        case kNr:                                                           return gNr;
        case kSatType:                                                      return gQuality;   // fade out, restart
        default:                                                            return 0;   // the two meters
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
inline float  maxf(float a, float b) { return a > b ? a : b; }
inline float  minf(float a, float b) { return a < b ? a : b; }

// The cubic soft clipper of dsp/math.h, for both channels.
inline Stereo softClip(Stereo x)
{
    x = vmax(vmin(x, splat(1.5f)), splat(-1.5f));
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
        return x + vmax(vmin(e, splat(kNrClip)), splat(-kNrClip)) * mix;
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
        t = vmax(vmin(t, splat(3.0f)), splat(-3.0f));
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
    cMicGain,
    cWobbleOn,
    cAgc, cDrift, cBreath,
    cScrape, cWhistle,
    kNumCtl
};

// Values that are additionally ramped every sample (anything that would click).
enum RampIndex { rOut, rMono, rChorus, rMic, rSat, rMix, kNumRamp };

struct MicCurve { float hpHz, peakHz, peakQ, peakDb, lpHz; };
const MicCurve kMicCurves[4] = {
    { 180.0f, 3200.0f, 1.0f, 5.0f,  7500.0f },   // Camcorder
    {  90.0f, 5000.0f, 0.9f, 4.0f, 12000.0f },   // Dynamic
    { 350.0f, 1800.0f, 1.2f, 6.0f,  3400.0f },   // Phone
    { 120.0f,  900.0f, 0.8f, 4.0f,  6000.0f },   // Boombox
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
        addParam(floatParam ("tone",      "Tone",      -100.0f, 100.0f,  0.0f, "",  0));
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
        addParam(logParam   ("rpm",       "RPM",          6.0f, 300.0f, 33.0f, "rpm", 0));
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
        addParam(choiceParam("sat_type",   "Sat Type",   {"2-Band", "VHS"}, 0));

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

    // A program is about to be chosen: it takes over the Animation and HQ switches as they
    // stand, so they do not move. (The host's thread, the same one that then loads the program.)
    void holdSwitches(int program)
    {
        if (program < 0 || program >= (int)programs_.size()) return;
        for (int i : {(int)kAnimate, (int)kHq}) programs_[(size_t)program].plainValues[(size_t)i] = get(i);
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
        agcRelease_   = 1.0f / (2.5f * sr_ * kCtrlInv);
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
        micHp_.reset(); micPk_.reset(); micLp_.reset();
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

        // The delays were just emptied. Keep the output shut until the sound has come through
        // them (their resting length), then fade in over about 20 ms instead of letting it cut in.
#ifndef VHS_VIBES_NO_FADE_IN
        rampS_[rOut] = 0.0f;
        ramp_[rOut].snap(0.0f);
        fadeHold_ = (int)((wf1_.base + wf2_.base + (choActive_ ? choBase_ : 0.0f)) * kCtrlInv) + 1;
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
        if (boost_) x = vmax(vmin(x, splat(kBoostCeil)), splat(-kBoostCeil));

        // ----- 2-band saturator ------------------------------------------
        if (satMix > 0.0f && tape_)
        {
            const Stereo y = tapeSat_.process(x);
            splitLp_ += splitCoeff_ * (y - splitLp_);
            x += (splitLp_ * cur_[cLowsG] + (y - splitLp_) * cur_[cHighsG] - x) * satMix;
        }
        else if (satMix > 0.0f)
        {
            splitLp_ += splitCoeff_ * (x - splitLp_);
            Stereo lo = splitLp_ * cur_[cLowsG];
            Stereo hi = (x - splitLp_) * cur_[cHighsG];
            if (loSatOn_) lo = driveSat(lo, cur_[cLoG], cur_[cLoMk], cur_[cLoMix]);
            if (hiSatOn_) hi = driveSat(hi, cur_[cHiG], cur_[cHiMk], cur_[cHiMix]);
            x += (lo + hi - x) * satMix;
        }
        return x;
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

            // ----- mic emulation ---------------------------------------------
            if (micMix > 0.0f)
            {
                const Stereo m = micLp_.process(micPk_.process(micHp_.process(x))) * cur_[cMicGain];
                x += (m - x) * micMix;
            }

            // ----- deck: automatic level control -----------------------------
            // A new peak pulls the gain down at once; controlTick() holds it there and then
            // lets it creep back up.
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
                x *= agcGain_;
            }

            // ----- preamp: tilt tone, then soft clip (each only when it does something) -----
            if (tiltOn_)
            {
                tiltLp_ += tiltCoeff_ * (x - tiltLp_);
                x = tiltLp_ * cur_[cTiltLo] + (x - tiltLp_) * cur_[cTiltHi];
            }
            else tiltLp_ = x;   // stays with the sound, so Tone can come in without a click
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
    }

    // The Split knob as bias: 0 at its default (2.5 kHz, normalised 0.68468), -1 at the bottom
    // (too little), +1 at the top (too much).
    static float tapeBias(float n)
    {
        const float n0 = 0.68468f;
        return n < n0 ? (n - n0) / n0 : (n - n0) / (1.0f - n0);
    }

    // Value text with its unit, at most 8 characters: "25 %", "+3.0 dB", "9.0 kHz", "33 rpm".
    bool formatValue(int index, float v, std::string& out) override
    {
        if (index < 0 || index >= kNumParams || !isSetting(index) || param(index).kind != mpc::Param::Kind::Float) return false;
        if (units_[index] == 0 && index != kTone) return false;
        char buf[32];
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
                if (v < 0.5f) std::snprintf(buf, sizeof(buf), "SP");
                else if (v > 99.5f) std::snprintf(buf, sizeof(buf), "EP");
                else std::snprintf(buf, sizeof(buf), "%.0f%% EP", (double)v);   // that far from SP towards EP
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
            case 5: std::snprintf(buf, sizeof(buf), "%.0f rpm", (double)v); break;
            default:   // Tone: a signed amount, no unit
                if (std::fabs(v) < 0.5f) std::snprintf(buf, sizeof(buf), "0");
                else std::snprintf(buf, sizeof(buf), "%+.0f", (double)v);
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
        info.version      = 1502;           // 1.5.2
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

        if (groups & gRamps)
        {
            rampT_[rOut]    = dbToGain(get(kOutput));
            rampT_[rMono]   = getBool(kMono) ? 1.0f : 0.0f;
            rampT_[rChorus] = getBool(kChorusOn) ? pct(kChoMix) : 0.0f;
            micOn_          = getBool(kMicOn);
            rampT_[rMic]    = micOn_ && !micSwap_ ? 1.0f : 0.0f;   // (out while it changes type: see gMic)
            rampT_[rSat]    = getBool(kSatOn) ? 1.0f : 0.0f;
            rampT_[rMix]    = pct(kMix);
            dropAmount_     = pct(kDropouts);
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
            splitCoeff_ = lpCoeff(get(kSplit), nlSr_);
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
            dev1Slow_ = 0.05f  * sq(pct(kWarp));
            rate1Slow_ = get(kRpm) * (1.0f / 60.0f);
            shape1_   = pct(kShape);
            dev1Fast_ = 0.012f * sq(pct(kFlutter));
            rate1Fast_ = get(kFlutRate);
            dev2Slow_ = 0.03f  * sq(pct(kWow));
            rate2Slow_ = get(kWowRate);
            dev2Fast_ = 0.010f * sq(pct(kFlutter2));
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
            tgt_[cHead]    = sq(pct(kHeadBuzz)) * 0.50f * master;
            tgt_[cHiss]    = sq(pct(kHiss))     * 0.10f * master;
            tgt_[cCrackle] = sq(pct(kCrackle))  * 0.60f * master;
            // about -45 dBFS at 50 %, where a real deck has it; the factor evens out the oscillator
            tgt_[cWhistle] = sq(pct(kWhistle))  * 0.022f * whistleNorm_ * master;
            const float clicksPerSec = 2.0f + 60.0f * pct(kCrackle);
            crackleThresh_ = (uint32_t)(clicksPerSec / sr * 4294967296.0f);
            noisePre_ = getInt(kNoisePos) == 1;

            // head switching: how long the signal is held at each switch, in samples. It follows
            // the Head Buzz knob and the noise master like the rasp does.
            const float headHold = getBool(kNoiseOn) ? pct(kHeadBuzz) * minf(2.0f * pct(kNoise), 1.0f) : 0.0f;
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
            // A new Mic Type while the mic is heard would swap three filters under the running
            // sound (a click at each position a Q-Link passes). So the mic fades out first, the
            // new curve goes in while it is out, and it fades back in (see controlTick). At a
            // reset, or while the mic is off and silent, the curve goes in at once.
            micWant_ = clamp(getInt(kMicType), 0, 3);
            if (groups == gAll || (!micSwap_ && rampT_[rMic] == 0.0f && ramp_[rMic].v == 0.0f)) { micSwap_ = false; setMicCurve(micWant_); }
            else micSwap_ = micWant_ != micType_;   // (turned back to the type that is in: no fade)
            rampT_[rMic] = micOn_ && !micSwap_ ? 1.0f : 0.0f;
            tgt_[cMicGain] = dbToGain(get(kMicGain));
        }
        if (groups & gDeck)
        {
            const bool deck = getBool(kDeckOn);
            tgt_[cAgc]    = deck ? pct(kAgc) : 0.0f;
            tgt_[cDrift]  = deck ? pct(kDrift) : 0.0f;
            tgt_[cBreath] = deck ? pct(kBreathing) : 0.0f;
            trackErr_     = deck ? pct(kTracking) : 0.0f;
        }
        if (groups & gNr)
        {
            // a side that is switched in starts from rest (unless it was still fading out)
            // and fades in; one that is switched out fades out (see process)
            const int mode = getInt(kNr);
            const bool enc = mode == 1 || mode == 3, dec = mode >= 2;
            if (enc && !nrEncOn_ && nrEncMix_ == 0.0f) { nrEnc_.reset(); nrEncSeed_ = true; }
            if (dec && !nrDecOn_ && nrDecMix_ == 0.0f) { nrDec_.reset(); nrDecSeed_ = true; }
            nrEncOn_ = enc; nrDecOn_ = dec;
            nrEncGoal_ = enc ? 1.0f : 0.0f;
            nrEncRun_ = enc || nrEncMix_ > 0.0f; nrDecRun_ = dec || nrDecMix_ > 0.0f;
        }
    }

    void setMicCurve(int type)
    {
        const MicCurve& mc = kMicCurves[type];
        micHp_.setCoeffs(BiquadCoeffs::highpass(sr_, mc.hpHz, 0.7071f));
        micPk_.setCoeffs(BiquadCoeffs::peak(sr_, mc.peakHz, mc.peakQ, mc.peakDb));
        micLp_.setCoeffs(BiquadCoeffs::lowpass(sr_, mc.lpHz, 0.7071f));
        micType_ = type;
    }

    // amount 0..1 -> clipper gain, make-up and clean/driven blend.
    void setDrive(float amount, float maxExtraGain, int gIdx, int mkIdx, int mixIdx)
    {
        const float g = 1.0f + maxExtraGain * amount * amount;
        tgt_[gIdx]   = g;
        tgt_[mkIdx]  = kRefLevel / softClip(g * kRefLevel);
        tgt_[mixIdx] = clamp(amount * 6.0f, 0.0f, 1.0f);
    }

    // ----- every 16 samples: smoothing, modulation, compressor gain -----------
    void controlTick()
    {
        // The fade-out before a reset reached 0 over the interval that ends here: only now may
        // the reset come (at the start of the next block). Asked for a tick earlier, a block
        // that ended inside the fade's last step would have cut it short, with a step.
        if (resetNextTick_) { resetNextTick_ = false; resetPending_.store(true); }
        // Mic Type: the mic has faded out for its new curve (see readParams). Put it in, from
        // rest, and fade the mic back in.
        if (micSwap_ && ramp_[rMic].v == 0.0f)
        {
            setMicCurve(micWant_);
            micHp_.reset(); micPk_.reset(); micLp_.reset();
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

        // Which stages have anything to do for the next 16 samples.
        humOn_     = cur_[cHum] > 0.0f     || tgt_[cHum] > 0.0f;
        buzzOn_    = cur_[cBuzz] > 0.0f    || tgt_[cBuzz] > 0.0f;
        headOn_    = cur_[cHead] > 0.0f    || tgt_[cHead] > 0.0f;
        hissOn_    = cur_[cHiss] > 0.0f    || tgt_[cHiss] > 0.0f;
        crackleOn_ = cur_[cCrackle] > 0.0f || tgt_[cCrackle] > 0.0f;
        noiseActive_ = humOn_ || buzzOn_ || headOn_ || hissOn_ || crackleOn_;
        whistleOn_ = cur_[cWhistle] > 0.0f || tgt_[cWhistle] > 0.0f;
        scrapeOn_  = cur_[cScrape] > 0.0f  || tgt_[cScrape] > 0.0f;
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
            }
        }
        else dropTarget_ = 1.0f;
        if (dropTarget_ == 1.0f && dropGain_ > 0.9999f) dropGain_ = 1.0f;   // a dip is over
        dropOn_ = dropGain_ != 1.0f || dropTarget_ != 1.0f;                  // only then is there work

        // ----- deck ------------------------------------------------------------
        // AGC: after the last peak the gain holds for a second and a half, then creeps back
        // up over a few seconds and brings the background up with it.
        agcAmt_ = cur_[cAgc];
        agcActive_ = agcAmt_ > 0.0f || tgt_[cAgc] > 0.0f || std::fabs(agcGain_ - 1.0f) > 1.0e-3f;
        if (agcActive_)
        {
            if (agcHold_ > 0) --agcHold_;
            else agcEnv_ = maxf(agcEnv_ - agcRelease_ * agcEnv_, 1.0e-4f);
            agcTarget_ = 1.0f + agcAmt_ * (clamp(kRefLevel / agcEnv_, 0.25f, 5.0f) - 1.0f);
        }
        else { agcGain_ = agcTarget_ = 1.0f; agcEnv_ = kRefLevel; }
        // The clip after the compressor is needed while something may turn the sound up, unless
        // the 2-band saturator, whose clippers have the same rail, is fully in. (With Sat Type on
        // VHS those clippers are not in the path, and Lo Sat and Hi Sat mean something else.)
        boost_ = (agcActive_ || cur_[cMakeup] > 1.0f)
              && !(!tape_ && ramp_[rSat].v == 1.0f && cur_[cLoMix] == 1.0f && cur_[cHiMix] == 1.0f);

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
        breathOn_ = breath > 0.0f || tgt_[cBreath] > 0.0f;
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
    void notifierMain()
    {
        int lastReel = -1, lastLevel = -1;
        int latCandidate = 0, latStable = 0, latReported = 0;
        // Every call into the host checks first that the plugin is still running: the host may
        // have suspended or started closing it since the last look. (suspended_ as well as
        // started_: doReset can set started_ for a moment while a suspend comes in.)
        auto speaking = [this] { return started_.load() && !suspended_.load(); };
        auto tell = [&](int i) { if (speaking()) notifyHostParameterChanged(i); };
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
            }

            if (animate_.load(std::memory_order_relaxed))
            {
                const int reel = clamp((int)(visReel_.load(std::memory_order_relaxed) * (float)kReelFrames), 0, kReelFrames - 1);
                if (reel != lastReel)
                {
                    lastReel = reel;
                    setNormalised(kVisReel, (float)reel / (float)(kReelFrames - 1));
                    tell(kVisReel);
                }
                // -42 dBFS .. 0 dBFS across the frames
                const float env = visLevel_.load(std::memory_order_relaxed);
                const float db = 20.0f * std::log10(env > 1.0e-6f ? env : 1.0e-6f);
                const int level = clamp((int)((db + 42.0f) * (1.0f / 42.0f) * (float)(kLevelFrames - 1) + 0.5f), 0, kLevelFrames - 1);
                if (level != lastLevel)
                {
                    lastLevel = level;
                    setNormalised(kVisLevel, (float)level / (float)(kLevelFrames - 1));
                    tell(kVisLevel);
                }
            }

            // Latency: the output is late by the resting length of the two wobble delays. Tell
            // the host once that length has been steady for half a second and is more than
            // 32 samples (0.7 ms) away from what it was told last, so turning a knob does not
            // send a stream of updates.
            if (kReportLatency)
            {
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

        program("Home Recording", { {kAgc, 25}, {kDrift, 10} });
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
            {kAgc, 40}, {kDrift, 35} });
        program("Vaporwave Mall", {
            {kDrive, 30}, {kComp, 35}, {kHiCut, 7000}, {kWarp, 60}, {kRpm, 20}, {kShape, 50}, {kFlutter, 20},
            {kWow, 40}, {kWowRate, 0.4f}, {kHiss, 25}, {kCrackle, 10},
            {kChorusOn, 1}, {kChoMix, 55}, {kChoSpeed, 0.35f}, {kChoDelay, 18}, {kChoWidth, 90},
            {kDrift, 20} });
        // The camcorder's level control does the squashing here.
        program("Camcorder 89", {
            {kDrive, 35}, {kComp, 30}, {kHiCut, 8000}, {kWarp, 20}, {kFlutter, 35},
            {kHiss, 45}, {kHeadBuzz, 30}, {kNoisePos, 1}, {kNoise, 35},
            {kMicOn, 1}, {kMicType, 0}, {kMicGain, 2},
            {kAgc, 80}, {kDrift, 10} });
        // A tape that will not track: buzz on the music, static, drops to the backup track.
        program("Tracking Error", {
            {kWarp, 70}, {kRpm, 90}, {kShape, 80}, {kFlutter, 60}, {kFlutRate, 16}, {kWow, 30},
            {kHeadBuzz, 60}, {kHiss, 40}, {kCrackle, 40}, {kDropouts, 60},
            {kTracking, 85}, {kBreathing, 30} });
        program("Late Night TV", {
            {kDrive, 40}, {kWear, 35}, {kComp, 50}, {kHiCut, 5000}, {kWarp, 25}, {kLoCut, 180},
            {kHum, 40}, {kBuzz, 25}, {kHiss, 45}, {kMono, 1},
            {kAgc, 30}, {kDrift, 15} });
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
            {kAgc, 75}, {kDrift, 40} });
        // The machine ate the tape.
        program("Chewed Tape", {
            {kDrive, 35}, {kWear, 50}, {kComp, 45}, {kHiCut, 5500},
            {kWarp, 90}, {kRpm, 45}, {kShape, 100}, {kFlutter, 70}, {kFlutRate, 5},
            {kWow, 60}, {kWowRate, 1.5f}, {kFlutter2, 45}, {kFlut2Rate, 9}, {kHiCut2, 7000},
            {kHiss, 40}, {kCrackle, 50}, {kHeadBuzz, 20}, {kDropouts, 80},
            {kAgc, 20}, {kDrift, 85} });
        // A message left on a micro-cassette answering machine.
        program("Answer Machine", {
            {kDrive, 50}, {kWear, 30}, {kComp, 60}, {kHiCut, 3500}, {kHiSat, 50},
            {kWarp, 25}, {kRpm, 60}, {kFlutter, 40}, {kFlutRate, 11}, {kWow, 20}, {kLoCut, 300}, {kHiCut2, 4000},
            {kHum, 20}, {kHeadBuzz, 0}, {kHiss, 45}, {kCrackle, 10}, {kNoise, 40},
            {kMicOn, 1}, {kMicType, 2}, {kMicGain, 3}, {kMono, 1},
            {kAgc, 60} });
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
            {kAgc, 35}, {kDrift, 25} });
        // A camcorder at the back of the hall: the level control rides everything.
        program("Wedding Video", {
            {kDrive, 30}, {kComp, 20}, {kHiCut, 8500}, {kWarp, 20}, {kFlutter, 25},
            {kHum, 25}, {kHeadBuzz, 15}, {kHiss, 40}, {kNoise, 40}, {kNoisePos, 1},
            {kMicOn, 1}, {kMicType, 0}, {kMicGain, 1},
            {kAgc, 90}, {kDrift, 20} });
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
            {kAgc, 15}, {kDrift, 95} });
        // Taped from the crowd on a boombox and copied a few times.
        program("Bootleg Concert", {
            {kDrive, 60}, {kWear, 25}, {kComp, 50}, {kHiCut, 7000}, {kLoSat, 45}, {kHiSat, 50},
            {kWarp, 30}, {kFlutter, 35}, {kHum, 15}, {kHiss, 50}, {kNoise, 35}, {kNoisePos, 1},
            {kMicOn, 1}, {kMicType, 3}, {kMicGain, 2}, {kMono, 1},
            {kAgc, 65}, {kDrift, 30} });
        // Local cable studio: hum, buzz and a level control doing its best.
        program("Public Access", {
            {kDrive, 35}, {kComp, 40}, {kHiCut, 7000}, {kWarp, 20}, {kFlutter, 20}, {kLoCut, 120},
            {kHum, 50}, {kBuzz, 35}, {kHeadBuzz, 25}, {kHiss, 40}, {kMono, 1},
            {kAgc, 50}, {kDrift, 10} });

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
            {kAgc, 20}, {kDrift, 35} });
        // The living-room set: a small speaker, mains hum and the whistle of the picture tube.
        program("Tube TV", {
            {kDrive, 30}, {kWear, 15}, {kComp, 45}, {kHiCut, 7500},
            {kWarp, 15}, {kFlutter, 20}, {kWow, 10}, {kFlutter2, 10}, {kLoCut, 140}, {kHiCut2, 9000},
            {kHum, 30}, {kBuzz, 25}, {kHeadBuzz, 0}, {kHiss, 35}, {kCrackle, 5}, {kWhistle, 60},
            {kMicOn, 1}, {kMicType, 3}, {kMono, 1},
            {kAgc, 25}, {kDrift, 10} });
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
            {kAgc, 25}, {kDrift, 45} });

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
            {kAgc, 40}, {kDrift, 35} });
        // SP, recorded hot, dull from too much bias.
        program("VHS - Training Video", {
            {kDrive, 40}, {kTone, -15}, {kWear, 40}, {kComp, 35}, {kHiCut, 4500},
            {kSatType, 1}, {kLoSat, 70}, {kHiSat, 0}, {kSplit, 5000}, {kHighs, -3},
            {kWarp, 45}, {kRpm, 34}, {kShape, 60}, {kFlutter, 35}, {kFlutRate, 7},
            {kWow, 40}, {kWowRate, 0.5f}, {kFlutter2, 30}, {kFlut2Rate, 12}, {kLoCut, 150}, {kHiCut2, 6000},
            {kHum, 30}, {kBuzz, 15}, {kHeadBuzz, 35}, {kHiss, 50}, {kCrackle, 20}, {kNoise, 40}, {kNoisePos, 1},
            {kMono, 1}, {kDropouts, 25},
            {kAgc, 75}, {kDrift, 40} });
        // EP, bias right, a little over reference level.
        program("VHS - Six Hour Tape", {
            {kDrive, 30}, {kWear, 30}, {kComp, 40}, {kHiCut, 6000},
            {kSatType, 1}, {kLoSat, 60}, {kHiSat, 100}, {kHighs, 0},
            {kWarp, 50}, {kFlutter, 50}, {kWow, 35}, {kFlutter2, 30}, {kLoCut, 100}, {kHiCut2, 8000},
            {kHum, 20}, {kHiss, 65}, {kCrackle, 20},
            {kAgc, 35}, {kDrift, 25} });
        // Too little bias: brighter and grittier.
        program("VHS - Sticky Tape", {
            {kDrive, 30}, {kWear, 45}, {kComp, 40}, {kHiCut, 6500},
            {kSatType, 1}, {kLoSat, 65}, {kHiSat, 20}, {kSplit, 1200}, {kHighs, -2},
            {kWarp, 45}, {kRpm, 40}, {kShape, 70}, {kFlutter, 50}, {kFlutRate, 13}, {kScrape, 65},
            {kWow, 25}, {kFlutter2, 25}, {kHiCut2, 8000},
            {kHum, 10}, {kHiss, 45}, {kCrackle, 20}, {kDropouts, 45},
            {kAgc, 20}, {kDrift, 35} });
        // Too much bias: the highs give way sooner still.
        program("VHS - Dirty Heads", {
            {kDrive, 30}, {kWear, 35}, {kComp, 40}, {kHiCut, 5000},
            {kSatType, 1}, {kLoSat, 60}, {kHiSat, 40}, {kSplit, 6000}, {kHighs, -4},
            {kWarp, 30}, {kFlutter, 30}, {kScrape, 30}, {kWow, 20}, {kFlutter2, 15}, {kLoCut, 80}, {kHiCut2, 7000},
            {kHum, 15}, {kHiss, 50}, {kCrackle, 15}, {kDropouts, 75},
            {kAgc, 25}, {kDrift, 45} });
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
    int   units_[kNumParams] = {};      // 0 none, 1 %, 2 dB, 3 Hz, 4 ms, 5 rpm (see formatValue)

    float tgt_[kNumCtl] = {}, cur_[kNumCtl] = {};
    float rampT_[kNumRamp] = {}, rampS_[kNumRamp] = {};
    Ramp  ramp_[kNumRamp];

    // chorus
    PairDelay choDelay_;
    Ramp  choD_[2];
    float choPhase_ = 0.0f, choSpeed_ = 0.6f, choWidth_ = 0.7f;
    float choDelayTarget_ = 500.0f, choBase_ = 500.0f;

    // mic
    PairBiquad micHp_, micPk_, micLp_;
    int   micType_ = -1, micWant_ = 0;    // the curve in the filters; the one Mic Type asks for
    bool  micOn_ = false, micSwap_ = false;   // the Mic switch; the mic is out for a change of curve

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
    bool  agcActive_ = false;
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
};

// The framework's dispatcher with five additions:
//  - Choosing a program leaves the Animation and HQ switches as they were. The framework sets
//    every value a program lists, and the parameters added after Animation have to be in that
//    list, so the program is first told the present position of the two switches.
//  - A saved state from before 1.4 does not mention the parameters added since. They go to
//    their resting values before any state is loaded, so such a state sounds as it was saved
//    whatever this instance was doing before; a newer state then sets them.
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
        for (int i = kScrape; i < kNumParams; ++i) p->set(i, i == kHq ? 0.0f : p->param(i).def);   // a state from before HQ existed: off
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
