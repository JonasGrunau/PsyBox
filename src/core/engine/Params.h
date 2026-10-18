#pragma once

// Every parameter the synth has, defined once. The engine reads plain floats
// indexed by ParamId, the generator writes normalised values, and the plugin
// builds its JUCE parameters from the same table, so the three can never
// disagree about a range.

#include "dsp/FilterTypes.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace psy
{

enum class Mapping : uint8_t
{
    Linear,   // min + (max - min) * n
    Power,    // min + (max - min) * n^skew          (times, amounts)
    Exp,      // min * (max / min)^n                  (frequencies)
    BiPower,  // max * sign(2n-1) * |2n-1|^skew       (bipolar, fine near 0)
    Choice,   // integer index into a choice list
    Toggle,   // 0 / 1
    Inverse   // min / n, at most max                 (a capacitor whose 1/C the knob turns)
};

// Mv, Volt, NanoFarad, OctPerVolt and Meter label the Phoscyon engine's
// calibration trims and reverb sizes. OctPerVolt shows 1 + the value.
enum class Unit : uint8_t { None, Percent, Hz, Ms, Sec, Semi, Cents, Db, Octaves, Ratio, RatioTo1, Int,
                            Mv, Volt, NanoFarad, OctPerVolt, Meter };

struct ParamDef
{
    const char* id;
    const char* name;
    const char* group;
    Mapping mapping;
    float min, max, def;
    float skew;                       // Power / BiPower exponent
    Unit unit;
    const std::vector<std::string>* choices;  // Choice only
    bool modulatable;                 // shown as mod-matrix target in UI hints

    float toPlain (float norm) const noexcept;
    float toNorm (float plain) const noexcept;
    float snap (float plain) const noexcept;  // rounds Choice/Toggle/Int
    std::string format (float plain) const;
    // The inverse of format: a value typed in the units format prints ("1.20 kHz",
    // "50%", "2.00 s", a choice's name), snapped into range. Text without a
    // number gives the default.
    float parse (std::string_view text) const;
    int numChoices() const noexcept { return choices ? (int) choices->size() : 0; }
    bool isDiscrete() const noexcept { return mapping == Mapping::Choice || mapping == Mapping::Toggle || unit == Unit::Int; }
};

// ---- choice lists --------------------------------------------------------
namespace choices
{
    extern const std::vector<std::string> voiceMode, glideMode, quality, oscType, wavetable,
        subOct, subShape, noiseType, penvDest, filterType, filterRouting, lfoShape, syncDiv,
        modSrc, modDst, distType, chorusMode, compMode, arpMode, engineMode, kickClick, kickDrive,
        energyMode, energyBars, uniMode, uniStack, arpKey, arpScale,
        phosWave, phosDistModel, phosDistOrder, phosDelayFilter, phosDelayScale,
        eqLowType, eqHighType;
}

// Sync divisions expressed in beats (quarter notes), index-aligned with choices::syncDiv.
float syncDivBeats (int index) noexcept;
// Energy lengths in bars, index-aligned with choices::energyBars (4/4: a bar is 4 beats).
float energyBarsValue (int index) noexcept;
// The energy sweep at t (0..1 of the length): still for the first `delay`
// of it, then 0 -> 1 bent by `exponent` (EnergyCurve: 2^(2.5 curve)).
inline float sweepValue (float t, float delay, float exponent) noexcept
{
    if (t >= 1.f) return 1.f;
    const float u = delay > 0.f ? std::max (0.f, (t - delay) / (1.f - delay)) : t;
    return std::pow (u, exponent);
}

enum class VoiceMode : int { Poly, Mono, Legato };
enum class OscType : int { Analog, Supersaw, Wavetable, FM };
// Unison (per oscillator, like Serum 2): how the voices are spaced in pitch,
// and which of them are transposed instead of only detuned.
enum class UniMode : int { Linear, Super, Exp, Inv, Random };
enum class UniStack : int { Off, Oct, OctFifth, TwoOct, CenterDown1, CenterDown2 };
constexpr int kMaxUnison = 16;
// Drawn: the LFO's own curve of points (PSY_LFO_CURVE, see dsp::LfoCurve).
enum class LfoShape : int { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom, Drawn };
// An EQ band is a shelf, a bell, or a cut (a high-pass for the low band, a
// low-pass for the high band) of 12 or 24 dB an octave.
enum class EqBand : int { Shelf, Bell, Cut12, Cut24 };
enum class DistType : int { Soft, Hard, Fold, Tube, Rectify };
enum class CompMode : int { Normal, Multiband };
enum class ArpMode : int { Up, Down, UpDown, Random, Order };
// A patch is a synth sound, a kick or a Phoscyon (TB-303) line; the category
// decides, never the recipe.
enum class EngineMode : int { Synth, Kick, Phoscyon };
enum class EnergyMode : int { Bars, Time };
enum class KickClick : int { Noise, Tick, Blip };
enum class KickDrive : int { Soft, Tube, Hard };

// The editor calls the filter, mod and pitch envelopes Env 1, Env 2 and Env 3.
enum class ModSrc : int
{
    None, AmpEnv, FilterEnv, ModEnv, PitchEnv, Lfo1, Lfo2, Lfo3, Velocity, Note, ModWheel,
    Aftertouch, PitchBend, Random, Macro1, Macro2, Macro3, Macro4,
    Sweep,   // energy sounds: 0 -> 1 over the length (bars or seconds) from the note start
    Count
};

enum class ModDst : int
{
    None, PitchAll, PitchA, PitchB, ShapeA, ShapeB, PwA, PwB, WtPosA, WtPosB, FmA, FmB,
    DetuneA, DetuneB, XMod, LevelA, LevelB, SubLevel, NoiseLevel, RingLevel,
    F1Cutoff, F1Reso, F1Drive, F1Vowel, F2Cutoff, F2Reso, Amp, Pan,
    Lfo1Rate, Lfo2Rate, Lfo3Rate, PitchEnvAmt, FilterEnvAmt,
    F2Drive, F2EnvAmt,
    // Effects: one instance for all voices. Global sources (macros, wheel,
    // free LFOs) drive them directly; per-voice sources follow the newest note.
    DistDrive, DistMix, CrushMix, ShiftHz, ShiftMix, PhaserMix, ChorusMix,
    DelayFb, DelayMix, ReverbSize, ReverbMix, WidthAmount,
    // The Phoscyon engine's knobs, then its effects (appended: saved
    // patches store the index).
    PhosCutoff, PhosReso, PhosEnvMod, PhosDecay, PhosAccent, PhosTune,
    PhosDistDrive, PhosDistColor, PhosChorusMix, PhosDelayFb, PhosDelayMix, PhosReverbMix,
    // Our EQ and dimension expander (effects, appended later).
    EqLowFreq, EqLowGain, EqHighFreq, EqHighGain, DimMix,
    Count
};

// ---- the table -----------------------------------------------------------
// X(enumName, "id", "Display Name", "Group", mapping, min, max, default, skew, unit, choices, modulatable)
#define PSY_PARAMS(X) \
    X(MasterVol,   "master_vol",   "Volume",      "Master", Linear, -48.f, 6.f, -6.f, 1.f, Db, nullptr, false) \
    X(PatchGain,   "patch_gain",   "Level",       "Master", Linear, -36.f, 24.f, 0.f, 1.f, Db, nullptr, false) \
    X(VoiceMode,   "voice_mode",   "Mode",        "Voice", Choice, 0, 2, 0, 1.f, None, &choices::voiceMode, false) \
    X(PolyVoices,  "poly_voices",  "Voices",      "Voice", Linear, 1, 16, 8, 1.f, Int, nullptr, false) \
    X(Glide,       "glide",        "Glide",       "Voice", Power, 0.f, 2000.f, 0.f, 3.f, Ms, nullptr, false) \
    X(GlideMode,   "glide_mode",   "Glide Mode",  "Voice", Choice, 0, 1, 1, 1.f, None, &choices::glideMode, false) \
    X(BendRange,   "bend_range",   "Bend",        "Voice", Linear, 0, 24, 2, 1.f, Int, nullptr, false) \
    X(Accent,      "accent",       "Accent",      "Voice", Linear, 0.f, 1.f, 0.5f, 1.f, Percent, nullptr, false) \
    X(Octave,      "octave",       "Octave",      "Voice", Linear, -3, 3, 0, 1.f, Int, nullptr, false) \
    X(VelSens,     "vel_sens",     "Velocity",    "Voice", Linear, 0.f, 1.f, 0.5f, 1.f, Percent, nullptr, false) \
    X(Quality,     "quality",      "Quality",     "Voice", Choice, 0, 2, 1, 1.f, None, &choices::quality, false) \
    \
    X(OscAOn,      "oscA_on",      "On",          "Osc A", Toggle, 0, 1, 1, 1.f, None, nullptr, false) \
    X(OscAType,    "oscA_type",    "Type",        "Osc A", Choice, 0, 3, 0, 1.f, None, &choices::oscType, false) \
    X(OscAShape,   "oscA_shape",   "Shape",       "Osc A", Linear, 0.f, 3.f, 2.f, 1.f, None, nullptr, true) \
    X(OscAPw,      "oscA_pw",      "PW",          "Osc A", Linear, 0.02f, 0.98f, 0.5f, 1.f, Percent, nullptr, true) \
    X(OscACoarse,  "oscA_coarse",  "Coarse",      "Osc A", Linear, -36, 36, 0, 1.f, Int, nullptr, false) \
    X(OscAFine,    "oscA_fine",    "Fine",        "Osc A", Linear, -100.f, 100.f, 0.f, 1.f, Cents, nullptr, false) \
    X(OscALevel,   "oscA_level",   "Level",       "Osc A", Linear, 0.f, 1.f, 0.8f, 1.f, Percent, nullptr, true) \
    X(OscADetune,  "oscA_detune",  "Detune",      "Osc A", Power, 0.f, 1.f, 0.3f, 1.5f, Percent, nullptr, true) \
    X(OscAWt,      "oscA_wt",      "Table",       "Osc A", Choice, 0, 7, 0, 1.f, None, &choices::wavetable, false) \
    X(OscAWtPos,   "oscA_wtpos",   "Position",    "Osc A", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, true) \
    X(OscAFmRatio, "oscA_fmratio", "Ratio",       "Osc A", Power, 0.25f, 16.f, 1.f, 2.f, Ratio, nullptr, false) \
    X(OscAFm,      "oscA_fm",      "FM",          "Osc A", Power, 0.f, 12.f, 0.f, 2.f, None, nullptr, true) \
    X(OscAFmFb,    "oscA_fmfb",    "Feedback",    "Osc A", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(OscARetrig,  "oscA_retrig",  "Retrig",      "Osc A", Toggle, 0, 1, 1, 1.f, None, nullptr, false) \
    X(OscAUnison,  "oscA_unison",  "Unison",      "Osc A", Linear, 1, 16, 1, 1.f, Int, nullptr, false) \
    X(OscABlend,   "oscA_blend",   "Blend",       "Osc A", Linear, 0.f, 1.f, 0.75f, 1.f, Percent, nullptr, false) \
    X(OscAUniWidth,"oscA_uni_width","Width",      "Osc A", Linear, 0.f, 1.f, 1.f, 1.f, Percent, nullptr, false) \
    X(OscAUniRange,"oscA_uni_range","Range",      "Osc A", Power, 0.f, 48.f, 2.f, 2.f, Semi, nullptr, false) \
    X(OscAUniMode, "oscA_uni_mode", "Mode",       "Osc A", Choice, 0, 4, 0, 1.f, None, &choices::uniMode, false) \
    X(OscAUniStack,"oscA_uni_stack","Stack",      "Osc A", Choice, 0, 5, 0, 1.f, None, &choices::uniStack, false) \
    X(OscAUniSpread,"oscA_uni_spread","Spread",   "Osc A", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    \
    X(OscBOn,      "oscB_on",      "On",          "Osc B", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(OscBType,    "oscB_type",    "Type",        "Osc B", Choice, 0, 3, 0, 1.f, None, &choices::oscType, false) \
    X(OscBShape,   "oscB_shape",   "Shape",       "Osc B", Linear, 0.f, 3.f, 2.f, 1.f, None, nullptr, true) \
    X(OscBPw,      "oscB_pw",      "PW",          "Osc B", Linear, 0.02f, 0.98f, 0.5f, 1.f, Percent, nullptr, true) \
    X(OscBCoarse,  "oscB_coarse",  "Coarse",      "Osc B", Linear, -36, 36, 0, 1.f, Int, nullptr, false) \
    X(OscBFine,    "oscB_fine",    "Fine",        "Osc B", Linear, -100.f, 100.f, 7.f, 1.f, Cents, nullptr, false) \
    X(OscBLevel,   "oscB_level",   "Level",       "Osc B", Linear, 0.f, 1.f, 0.8f, 1.f, Percent, nullptr, true) \
    X(OscBDetune,  "oscB_detune",  "Detune",      "Osc B", Power, 0.f, 1.f, 0.3f, 1.5f, Percent, nullptr, true) \
    X(OscBWt,      "oscB_wt",      "Table",       "Osc B", Choice, 0, 7, 0, 1.f, None, &choices::wavetable, false) \
    X(OscBWtPos,   "oscB_wtpos",   "Position",    "Osc B", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, true) \
    X(OscBFmRatio, "oscB_fmratio", "Ratio",       "Osc B", Power, 0.25f, 16.f, 2.f, 2.f, Ratio, nullptr, false) \
    X(OscBFm,      "oscB_fm",      "FM",          "Osc B", Power, 0.f, 12.f, 0.f, 2.f, None, nullptr, true) \
    X(OscBFmFb,    "oscB_fmfb",    "Feedback",    "Osc B", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(OscBRetrig,  "oscB_retrig",  "Retrig",      "Osc B", Toggle, 0, 1, 1, 1.f, None, nullptr, false) \
    X(OscBSync,    "oscB_sync",    "Sync",        "Osc B", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(OscBXMod,    "oscB_xmod",    "A>B FM",      "Osc B", Power, 0.f, 1.f, 0.f, 2.f, Percent, nullptr, true) \
    X(OscBUnison,  "oscB_unison",  "Unison",      "Osc B", Linear, 1, 16, 1, 1.f, Int, nullptr, false) \
    X(OscBBlend,   "oscB_blend",   "Blend",       "Osc B", Linear, 0.f, 1.f, 0.75f, 1.f, Percent, nullptr, false) \
    X(OscBUniWidth,"oscB_uni_width","Width",      "Osc B", Linear, 0.f, 1.f, 1.f, 1.f, Percent, nullptr, false) \
    X(OscBUniRange,"oscB_uni_range","Range",      "Osc B", Power, 0.f, 48.f, 2.f, 2.f, Semi, nullptr, false) \
    X(OscBUniMode, "oscB_uni_mode", "Mode",       "Osc B", Choice, 0, 4, 0, 1.f, None, &choices::uniMode, false) \
    X(OscBUniStack,"oscB_uni_stack","Stack",      "Osc B", Choice, 0, 5, 0, 1.f, None, &choices::uniStack, false) \
    X(OscBUniSpread,"oscB_uni_spread","Spread",   "Osc B", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    \
    X(SubLevel,    "sub_level",    "Sub",         "Mixer", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, true) \
    X(SubOct,      "sub_oct",      "Sub Oct",     "Mixer", Choice, 0, 1, 0, 1.f, None, &choices::subOct, false) \
    X(SubShape,    "sub_shape",    "Sub Wave",    "Mixer", Choice, 0, 2, 0, 1.f, None, &choices::subShape, false) \
    X(NoiseLevel,  "noise_level",  "Noise",       "Mixer", Power, 0.f, 1.f, 0.f, 2.f, Percent, nullptr, true) \
    X(NoiseType,   "noise_type",   "Noise Type",  "Mixer", Choice, 0, 4, 0, 1.f, None, &choices::noiseType, false) \
    X(NoiseDirect, "noise_direct", "Noise Direct","Mixer", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(RingLevel,   "ring_level",   "Ring",        "Mixer", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, true) \
    \
    X(PenvAmt,     "penv_amt",     "Amount",      "Env 3", BiPower, -60.f, 60.f, 0.f, 2.f, Semi, nullptr, true) \
    X(PenvAttack,  "penv_attack",  "Attack",      "Env 3", Power, 0.f, 4000.f, 0.f, 3.f, Ms, nullptr, false) \
    X(PenvDecay,   "penv_decay",   "Decay",       "Env 3", Power, 1.f, 10000.f, 120.f, 3.f, Ms, nullptr, false) \
    X(PenvCurve,   "penv_curve",   "Curve",       "Env 3", Linear, 0.f, 1.f, 0.7f, 1.f, Percent, nullptr, false) \
    X(PenvDest,    "penv_dest",    "Target",      "Env 3", Choice, 0, 2, 0, 1.f, None, &choices::penvDest, false) \
    \
    X(F1Type,      "f1_type",      "Type",        "Filter 1", Choice, 0, 19, 2, 1.f, None, &choices::filterType, false) \
    X(F1Cutoff,    "f1_cutoff",    "Cutoff",      "Filter 1", Exp, 20.f, 20000.f, 8000.f, 1.f, Hz, nullptr, true) \
    X(F1Reso,      "f1_reso",      "Reso",        "Filter 1", Linear, 0.f, 1.f, 0.1f, 1.f, Percent, nullptr, true) \
    X(F1Drive,     "f1_drive",     "Drive",       "Filter 1", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, true) \
    X(F1Env,       "f1_env",       "Env",         "Filter 1", BiPower, -1.f, 1.f, 0.f, 1.3f, Percent, nullptr, true) \
    X(F1Key,       "f1_key",       "Key",         "Filter 1", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(F1Vowel,     "f1_vowel",     "Vowel",       "Filter 1", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, true) \
    \
    X(F2Type,      "f2_type",      "Type",        "Filter 2", Choice, 0, 19, 0, 1.f, None, &choices::filterType, false) \
    X(F2Cutoff,    "f2_cutoff",    "Cutoff",      "Filter 2", Exp, 20.f, 20000.f, 12000.f, 1.f, Hz, nullptr, true) \
    X(F2Reso,      "f2_reso",      "Reso",        "Filter 2", Linear, 0.f, 1.f, 0.1f, 1.f, Percent, nullptr, true) \
    X(F2Drive,     "f2_drive",     "Drive",       "Filter 2", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(F2Env,       "f2_env",       "Env",         "Filter 2", BiPower, -1.f, 1.f, 0.f, 1.3f, Percent, nullptr, false) \
    X(F2Key,       "f2_key",       "Key",         "Filter 2", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(FRouting,    "f_routing",    "Routing",     "Filter 2", Choice, 0, 1, 0, 1.f, None, &choices::filterRouting, false) \
    \
    X(AenvA,       "aenv_a",       "Attack",      "Amp Env", Power, 0.f, 10000.f, 1.f, 3.f, Ms, nullptr, false) \
    X(AenvD,       "aenv_d",       "Decay",       "Amp Env", Power, 1.f, 10000.f, 300.f, 3.f, Ms, nullptr, false) \
    X(AenvS,       "aenv_s",       "Sustain",     "Amp Env", Linear, 0.f, 1.f, 0.8f, 1.f, Percent, nullptr, false) \
    X(AenvR,       "aenv_r",       "Release",     "Amp Env", Power, 1.f, 15000.f, 120.f, 3.f, Ms, nullptr, false) \
    X(AenvCurve,   "aenv_curve",   "Curve",       "Amp Env", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(AenvStart,   "aenv_start",   "Start",       "Amp Env", Power, 0.f, 0.9f, 0.f, 2.f, Percent, nullptr, false) \
    X(FenvA,       "fenv_a",       "Attack",      "Env 1", Power, 0.f, 10000.f, 0.f, 3.f, Ms, nullptr, false) \
    X(FenvD,       "fenv_d",       "Decay",       "Env 1", Power, 1.f, 10000.f, 250.f, 3.f, Ms, nullptr, false) \
    X(FenvS,       "fenv_s",       "Sustain",     "Env 1", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(FenvR,       "fenv_r",       "Release",     "Env 1", Power, 1.f, 15000.f, 200.f, 3.f, Ms, nullptr, false) \
    X(MenvA,       "menv_a",       "Attack",      "Env 2", Power, 0.f, 10000.f, 0.f, 3.f, Ms, nullptr, false) \
    X(MenvD,       "menv_d",       "Decay",       "Env 2", Power, 1.f, 10000.f, 500.f, 3.f, Ms, nullptr, false) \
    X(MenvS,       "menv_s",       "Sustain",     "Env 2", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(MenvR,       "menv_r",       "Release",     "Env 2", Power, 1.f, 15000.f, 300.f, 3.f, Ms, nullptr, false) \
    \
    X(Lfo1Shape,   "lfo1_shape",   "Shape",       "LFO 1", Choice, 0, 7, 0, 1.f, None, &choices::lfoShape, false) \
    X(Lfo1Rate,    "lfo1_rate",    "Rate",        "LFO 1", Exp, 0.02f, 40.f, 2.f, 1.f, Hz, nullptr, true) \
    X(Lfo1Sync,    "lfo1_sync",    "Sync",        "LFO 1", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(Lfo1Div,     "lfo1_div",     "Division",    "LFO 1", Choice, 0, 17, 9, 1.f, None, &choices::syncDiv, false) \
    X(Lfo1Retrig,  "lfo1_retrig",  "Retrig",      "LFO 1", Toggle, 0, 1, 1, 1.f, None, nullptr, false) \
    X(Lfo1Phase,   "lfo1_phase",   "Phase",       "LFO 1", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(Lfo2Shape,   "lfo2_shape",   "Shape",       "LFO 2", Choice, 0, 7, 1, 1.f, None, &choices::lfoShape, false) \
    X(Lfo2Rate,    "lfo2_rate",    "Rate",        "LFO 2", Exp, 0.02f, 40.f, 0.5f, 1.f, Hz, nullptr, true) \
    X(Lfo2Sync,    "lfo2_sync",    "Sync",        "LFO 2", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(Lfo2Div,     "lfo2_div",     "Division",    "LFO 2", Choice, 0, 17, 6, 1.f, None, &choices::syncDiv, false) \
    X(Lfo2Retrig,  "lfo2_retrig",  "Retrig",      "LFO 2", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(Lfo2Phase,   "lfo2_phase",   "Phase",       "LFO 2", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(Lfo3Shape,   "lfo3_shape",   "Shape",       "LFO 3", Choice, 0, 7, 5, 1.f, None, &choices::lfoShape, false) \
    X(Lfo3Rate,    "lfo3_rate",    "Rate",        "LFO 3", Exp, 0.02f, 40.f, 8.f, 1.f, Hz, nullptr, true) \
    X(Lfo3Sync,    "lfo3_sync",    "Sync",        "LFO 3", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(Lfo3Div,     "lfo3_div",     "Division",    "LFO 3", Choice, 0, 17, 12, 1.f, None, &choices::syncDiv, false) \
    X(Lfo3Retrig,  "lfo3_retrig",  "Retrig",      "LFO 3", Toggle, 0, 1, 1, 1.f, None, nullptr, false) \
    X(Lfo3Phase,   "lfo3_phase",   "Phase",       "LFO 3", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    \
    PSY_MOD_SLOT (X, 1)  PSY_MOD_SLOT (X, 2)  PSY_MOD_SLOT (X, 3)  PSY_MOD_SLOT (X, 4) \
    PSY_MOD_SLOT (X, 5)  PSY_MOD_SLOT (X, 6)  PSY_MOD_SLOT (X, 7)  PSY_MOD_SLOT (X, 8) \
    PSY_MOD_SLOT (X, 9)  PSY_MOD_SLOT (X, 10) PSY_MOD_SLOT (X, 11) PSY_MOD_SLOT (X, 12) \
    PSY_MOD_SLOT (X, 13) PSY_MOD_SLOT (X, 14) PSY_MOD_SLOT (X, 15) PSY_MOD_SLOT (X, 16) \
    X(Macro1,  "macro1",   "Macro 1", "Macros", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(Macro2,  "macro2",   "Macro 2", "Macros", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(Macro3,  "macro3",   "Macro 3", "Macros", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(Macro4,  "macro4",   "Macro 4", "Macros", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    \
    X(DistOn,      "dist_on",      "On",          "Distortion", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(DistType,    "dist_type",    "Type",        "Distortion", Choice, 0, 4, 0, 1.f, None, &choices::distType, false) \
    X(DistDrive,   "dist_drive",   "Drive",       "Distortion", Linear, 0.f, 36.f, 12.f, 1.f, Db, nullptr, false) \
    X(DistTone,    "dist_tone",    "Tone",        "Distortion", Exp, 800.f, 20000.f, 12000.f, 1.f, Hz, nullptr, false) \
    X(DistMix,     "dist_mix",     "Mix",         "Distortion", Linear, 0.f, 1.f, 1.f, 1.f, Percent, nullptr, false) \
    X(DistLowCut,  "dist_lowcut",  "Low Cut",     "Distortion", Exp, 20.f, 2000.f, 20.f, 1.f, Hz, nullptr, false) \
    X(DistAuto,    "dist_auto",    "Auto Level",  "Distortion", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(CrushOn,     "crush_on",     "On",          "Crusher", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(CrushBits,   "crush_bits",   "Bits",        "Crusher", Linear, 2.f, 16.f, 12.f, 1.f, None, nullptr, false) \
    X(CrushRate,   "crush_rate",   "Downsample",  "Crusher", Power, 1.f, 64.f, 1.f, 2.f, Ratio, nullptr, false) \
    X(CrushMix,    "crush_mix",    "Mix",         "Crusher", Linear, 0.f, 1.f, 1.f, 1.f, Percent, nullptr, false) \
    X(FshiftOn,    "fshift_on",    "On",          "Freq Shift", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(FshiftHz,    "fshift_hz",    "Shift",       "Freq Shift", BiPower, -2000.f, 2000.f, 0.f, 3.f, Hz, nullptr, false) \
    X(FshiftFb,    "fshift_fb",    "Feedback",    "Freq Shift", Linear, 0.f, 0.95f, 0.f, 1.f, Percent, nullptr, false) \
    X(FshiftSpread,"fshift_spread","Spread",      "Freq Shift", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(FshiftMix,   "fshift_mix",   "Mix",         "Freq Shift", Linear, 0.f, 1.f, 0.5f, 1.f, Percent, nullptr, false) \
    X(PhaserOn,    "phaser_on",    "On",          "Phaser", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(PhaserRate,  "phaser_rate",  "Rate",        "Phaser", Exp, 0.02f, 10.f, 0.3f, 1.f, Hz, nullptr, false) \
    X(PhaserDepth, "phaser_depth", "Depth",       "Phaser", Linear, 0.f, 1.f, 0.7f, 1.f, Percent, nullptr, false) \
    X(PhaserFb,    "phaser_fb",    "Feedback",    "Phaser", Linear, -0.95f, 0.95f, 0.5f, 1.f, Percent, nullptr, false) \
    X(PhaserCenter,"phaser_center","Center",      "Phaser", Exp, 100.f, 8000.f, 800.f, 1.f, Hz, nullptr, false) \
    X(PhaserMix,   "phaser_mix",   "Mix",         "Phaser", Linear, 0.f, 1.f, 0.5f, 1.f, Percent, nullptr, false) \
    X(ChorusOn,    "chorus_on",    "On",          "Chorus", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(ChorusMode,  "chorus_mode",  "Mode",        "Chorus", Choice, 0, 1, 0, 1.f, None, &choices::chorusMode, false) \
    X(ChorusRate,  "chorus_rate",  "Rate",        "Chorus", Exp, 0.02f, 8.f, 0.4f, 1.f, Hz, nullptr, false) \
    X(ChorusDepth, "chorus_depth", "Depth",       "Chorus", Linear, 0.f, 1.f, 0.5f, 1.f, Percent, nullptr, false) \
    X(ChorusFb,    "chorus_fb",    "Feedback",    "Chorus", Linear, -0.95f, 0.95f, 0.f, 1.f, Percent, nullptr, false) \
    X(ChorusMix,   "chorus_mix",   "Mix",         "Chorus", Linear, 0.f, 1.f, 0.4f, 1.f, Percent, nullptr, false) \
    X(DelayOn,     "delay_on",     "On",          "Delay", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(DelaySync,   "delay_sync",   "Sync",        "Delay", Toggle, 0, 1, 1, 1.f, None, nullptr, false) \
    X(DelayDiv,    "delay_div",    "Division",    "Delay", Choice, 0, 17, 10, 1.f, None, &choices::syncDiv, false) \
    X(DelayMs,     "delay_ms",     "Time",        "Delay", Power, 1.f, 2000.f, 375.f, 2.f, Ms, nullptr, false) \
    X(DelayFb,     "delay_fb",     "Feedback",    "Delay", Linear, 0.f, 0.98f, 0.4f, 1.f, Percent, nullptr, false) \
    X(DelayPing,   "delay_ping",   "Ping-Pong",   "Delay", Toggle, 0, 1, 1, 1.f, None, nullptr, false) \
    X(DelayLowCut, "delay_lowcut", "Low Cut",     "Delay", Exp, 20.f, 2000.f, 200.f, 1.f, Hz, nullptr, false) \
    X(DelayHighCut,"delay_highcut","High Cut",    "Delay", Exp, 1000.f, 20000.f, 8000.f, 1.f, Hz, nullptr, false) \
    X(DelayMix,    "delay_mix",    "Mix",         "Delay", Linear, 0.f, 1.f, 0.25f, 1.f, Percent, nullptr, false) \
    X(ReverbOn,    "reverb_on",    "On",          "Reverb", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(ReverbSize,  "reverb_size",  "Size",        "Reverb", Linear, 0.f, 1.f, 0.6f, 1.f, Percent, nullptr, false) \
    X(ReverbDecay, "reverb_decay", "Decay",       "Reverb", Exp, 0.2f, 30.f, 3.f, 1.f, Sec, nullptr, false) \
    X(ReverbDamp,  "reverb_damp",  "Damping",     "Reverb", Linear, 0.f, 1.f, 0.4f, 1.f, Percent, nullptr, false) \
    X(ReverbPre,   "reverb_pre",   "Pre-Delay",   "Reverb", Power, 0.f, 250.f, 10.f, 2.f, Ms, nullptr, false) \
    X(ReverbMix,   "reverb_mix",   "Mix",         "Reverb", Linear, 0.f, 1.f, 0.2f, 1.f, Percent, nullptr, false) \
    \
    X(CompOn,      "comp_on",      "On",          "Compressor", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(CompMode,    "comp_mode",    "Mode",        "Compressor", Choice, 0, 1, 0, 1.f, None, &choices::compMode, false) \
    X(CompThresh,  "comp_thresh",  "Threshold",   "Compressor", Linear, -60.f, 0.f, -18.f, 1.f, Db, nullptr, false) \
    X(CompRatio,   "comp_ratio",   "Ratio",       "Compressor", Power, 1.f, 20.f, 4.f, 2.f, RatioTo1, nullptr, false) \
    X(CompKnee,    "comp_knee",    "Knee",        "Compressor", Linear, 0.f, 24.f, 6.f, 1.f, Db, nullptr, false) \
    X(CompAttack,  "comp_attack",  "Attack",      "Compressor", Power, 0.05f, 200.f, 8.f, 3.f, Ms, nullptr, false) \
    X(CompRelease, "comp_release", "Release",     "Compressor", Power, 5.f, 2000.f, 150.f, 3.f, Ms, nullptr, false) \
    X(CompMix,     "comp_mix",     "Mix",         "Compressor", Linear, 0.f, 1.f, 1.f, 1.f, Percent, nullptr, false) \
    X(CompOut,     "comp_out",     "Output",      "Compressor", Linear, -24.f, 24.f, 0.f, 1.f, Db, nullptr, false) \
    X(CompDown,    "comp_down",    "Down",        "Compressor", Linear, 0.f, 1.f, 1.f, 1.f, Percent, nullptr, false) \
    X(CompUp,      "comp_up",      "Up",          "Compressor", Linear, 0.f, 1.f, 0.5f, 1.f, Percent, nullptr, false) \
    X(MbXLow,      "mb_xlow",      "Low Split",   "Compressor", Exp, 40.f, 600.f, 120.f, 1.f, Hz, nullptr, false) \
    X(MbXHigh,     "mb_xhigh",     "High Split",  "Compressor", Exp, 1000.f, 12000.f, 2500.f, 1.f, Hz, nullptr, false) \
    X(MbLowDown,   "mb_low_down",  "Low Down",    "Compressor", Linear, -60.f, 0.f, -20.f, 1.f, Db, nullptr, false) \
    X(MbMidDown,   "mb_mid_down",  "Mid Down",    "Compressor", Linear, -60.f, 0.f, -24.f, 1.f, Db, nullptr, false) \
    X(MbHighDown,  "mb_high_down", "High Down",   "Compressor", Linear, -60.f, 0.f, -28.f, 1.f, Db, nullptr, false) \
    X(MbLowUp,     "mb_low_up",    "Low Up",      "Compressor", Linear, -72.f, 0.f, -40.f, 1.f, Db, nullptr, false) \
    X(MbMidUp,     "mb_mid_up",    "Mid Up",      "Compressor", Linear, -72.f, 0.f, -44.f, 1.f, Db, nullptr, false) \
    X(MbHighUp,    "mb_high_up",   "High Up",     "Compressor", Linear, -72.f, 0.f, -48.f, 1.f, Db, nullptr, false) \
    X(MbLowGain,   "mb_low_gain",  "Low",         "Compressor", Linear, -24.f, 24.f, 0.f, 1.f, Db, nullptr, false) \
    X(MbMidGain,   "mb_mid_gain",  "Mid",         "Compressor", Linear, -24.f, 24.f, 0.f, 1.f, Db, nullptr, false) \
    X(MbHighGain,  "mb_high_gain", "High",        "Compressor", Linear, -24.f, 24.f, 0.f, 1.f, Db, nullptr, false) \
    \
    X(WidthOn,     "width_on",     "On",          "Width", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(WidthAmount, "width_amount", "Width",       "Width", Linear, 0.f, 2.f, 1.f, 1.f, Percent, nullptr, false) \
    X(WidthStereo, "width_stereo", "Stereoize",   "Width", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(WidthTime,   "width_time",   "Time",        "Width", Power, 1.f, 30.f, 12.f, 2.f, Ms, nullptr, false) \
    X(WidthMonoHz, "width_mono",   "Low Mono",    "Width", Exp, 20.f, 500.f, 120.f, 1.f, Hz, nullptr, false) \
    X(FxOrder,     "fx_order",     "Order",       "FX", Linear, 0, 362879, 0, 1.f, Int, nullptr, false) \
    \
    X(EnergyMode,  "energy_mode",  "Length In",   "Energy", Choice, 0, 1, 0, 1.f, None, &choices::energyMode, false) \
    X(EnergyBars,  "energy_bars",  "Length",      "Energy", Choice, 0, 7, 4, 1.f, None, &choices::energyBars, false) \
    X(EnergySec,   "energy_sec",   "Length",      "Energy", Power, 0.1f, 60.f, 6.f, 2.f, Sec, nullptr, false) \
    X(EnergyCurve, "energy_curve", "Curve",       "Energy", BiPower, -1.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(EnergyDelay, "energy_delay", "Delay",       "Energy", Linear, 0.f, 0.9f, 0.f, 1.f, Percent, nullptr, false) \
    X(EnergyFade,  "energy_fade",  "Fade",        "Energy", BiPower, -1.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(EnergyCut,   "energy_cut",   "Cut at End",  "Energy", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(EnergyOneShot, "energy_oneshot", "One Shot", "Energy", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    \
    X(ArpOn,       "arp_on",       "On",          "Arp", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(ArpMode,     "arp_mode",     "Mode",        "Arp", Choice, 0, 4, 0, 1.f, None, &choices::arpMode, false) \
    X(ArpDiv,      "arp_div",      "Rate",        "Arp", Choice, 0, 17, 12, 1.f, None, &choices::syncDiv, false) \
    X(ArpOctaves,  "arp_oct",      "Octaves",     "Arp", Linear, 1, 4, 1, 1.f, Int, nullptr, false) \
    X(ArpGate,     "arp_gate",     "Gate",        "Arp", Linear, 0.05f, 1.5f, 0.5f, 1.f, Percent, nullptr, false) \
    X(ArpSwing,    "arp_swing",    "Swing",       "Arp", Linear, 0.f, 0.75f, 0.f, 1.f, Percent, nullptr, false) \
    X(ArpHold,     "arp_hold",     "Hold",        "Arp", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(ArpLength,   "arp_length",   "Steps",       "Arp", Linear, 1, 16, 16, 1.f, Int, nullptr, false) \
    X(ArpKey,      "arp_key",      "Key",         "Arp", Choice, 0, 12, 0, 1.f, None, &choices::arpKey, false) \
    X(ArpScale,    "arp_scale",    "Scale",       "Arp", Choice, 0, 5, 0, 1.f, None, &choices::arpScale, false) \
    PSY_ARP_STEP (X, 1)  PSY_ARP_STEP (X, 2)  PSY_ARP_STEP (X, 3)  PSY_ARP_STEP (X, 4) \
    PSY_ARP_STEP (X, 5)  PSY_ARP_STEP (X, 6)  PSY_ARP_STEP (X, 7)  PSY_ARP_STEP (X, 8) \
    PSY_ARP_STEP (X, 9)  PSY_ARP_STEP (X, 10) PSY_ARP_STEP (X, 11) PSY_ARP_STEP (X, 12) \
    PSY_ARP_STEP (X, 13) PSY_ARP_STEP (X, 14) PSY_ARP_STEP (X, 15) PSY_ARP_STEP (X, 16) \
    \
    X(EngineMode,  "engine_mode",  "Engine",      "Engine", Choice, 0, 2, 0, 1.f, None, &choices::engineMode, false) \
    \
    X(KickTune,    "kick_tune",    "Tune",        "Kick Pitch", Exp, 30.f, 90.f, 44.f, 1.f, Hz, nullptr, false) \
    X(KickKeyTrack,"kick_keytrack","Key Track",   "Kick Pitch", Toggle, 0, 1, 1, 1.f, None, nullptr, false) \
    X(KickStart,   "kick_start",   "Start",       "Kick Pitch", Exp, 200.f, 8000.f, 2000.f, 1.f, Hz, nullptr, false) \
    X(KickSnap,    "kick_snap",    "Snap",        "Kick Pitch", Power, 0.3f, 20.f, 3.f, 2.f, Ms, nullptr, false) \
    X(KickKnee,    "kick_knee",    "Knee",        "Kick Pitch", Exp, 60.f, 1500.f, 330.f, 1.f, Hz, nullptr, false) \
    X(KickSweep,   "kick_sweep",   "Sweep",       "Kick Pitch", Power, 2.f, 300.f, 30.f, 2.f, Ms, nullptr, false) \
    X(KickAttack,  "kick_attack",  "Attack",      "Kick Amp", Power, 0.f, 50.f, 40.f, 2.f, Ms, nullptr, false) \
    X(KickHold,    "kick_hold",    "Hold",        "Kick Amp", Power, 0.f, 1000.f, 120.f, 2.f, Ms, nullptr, false) \
    X(KickDecay,   "kick_decay",   "Decay",       "Kick Amp", Power, 10.f, 3000.f, 120.f, 2.f, Ms, nullptr, false) \
    X(KickPunch,   "kick_punch",   "Punch",       "Kick Amp", Linear, 0.f, 1.f, 1.f, 1.f, Percent, nullptr, false) \
    X(KickPunchLen,"kick_punch_len","Punch Len",  "Kick Amp", Power, 1.f, 60.f, 6.f, 2.f, Ms, nullptr, false) \
    X(KickClickType, "kick_click_type", "Type",   "Kick Click", Choice, 0, 2, 0, 1.f, None, &choices::kickClick, false) \
    X(KickClickLevel,"kick_click_level","Level",  "Kick Click", Linear, 0.f, 1.f, 0.15f, 1.f, Percent, nullptr, false) \
    X(KickClickDecay,"kick_click_decay","Decay",  "Kick Click", Power, 0.3f, 40.f, 12.f, 2.f, Ms, nullptr, false) \
    X(KickClickTone, "kick_click_tone", "Tone",   "Kick Click", Exp, 300.f, 16000.f, 4000.f, 1.f, Hz, nullptr, false) \
    X(KickDriveType, "kick_drive_type", "Type",   "Kick Drive", Choice, 0, 2, 1, 1.f, None, &choices::kickDrive, false) \
    X(KickDrive,   "kick_drive",   "Drive",       "Kick Drive", Linear, -30.f, 30.f, -27.f, 1.f, Db, nullptr, false) \
    X(KickTone,    "kick_tone",    "Tone",        "Kick Drive", Exp, 200.f, 20000.f, 20000.f, 1.f, Hz, nullptr, false) \
    \
    PSY_PHOS_PARAMS (X) \
    \
    X(RenderQuality, "render_quality", "Quality", "Render", Choice, 0, 2, 2, 1.f, None, &choices::quality, false) \
    \
    PSY_LATER_PARAMS (X)

// Appended after RenderQuality, so no older parameter moves (hosts can keep
// automation by index, and Mutate draws for every index in order): our EQ
// and dimension expander with their places in the chain, the LFOs' envelope
// mode and drawn curves, and the amp envelope's clap bursts.
#define PSY_LATER_PARAMS(X) \
    X(EqOn,        "eq_on",        "On",          "EQ", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(EqLowType,   "eq_low_type",  "Low Type",    "EQ", Choice, 0, 3, 0, 1.f, None, &choices::eqLowType, false) \
    X(EqLowFreq,   "eq_low_freq",  "Low Freq",    "EQ", Exp, 20.f, 20000.f, 200.f, 1.f, Hz, nullptr, false) \
    X(EqLowGain,   "eq_low_gain",  "Low Gain",    "EQ", Linear, -24.f, 24.f, 0.f, 1.f, Db, nullptr, false) \
    X(EqLowQ,      "eq_low_q",     "Low Q",       "EQ", Exp, 0.25f, 16.f, 0.707f, 1.f, None, nullptr, false) \
    X(EqHighType,  "eq_high_type", "High Type",   "EQ", Choice, 0, 3, 0, 1.f, None, &choices::eqHighType, false) \
    X(EqHighFreq,  "eq_high_freq", "High Freq",   "EQ", Exp, 20.f, 20000.f, 6000.f, 1.f, Hz, nullptr, false) \
    X(EqHighGain,  "eq_high_gain", "High Gain",   "EQ", Linear, -24.f, 24.f, 0.f, 1.f, Db, nullptr, false) \
    X(EqHighQ,     "eq_high_q",    "High Q",      "EQ", Exp, 0.25f, 16.f, 0.707f, 1.f, None, nullptr, false) \
    X(DimOn,       "dim_on",       "On",          "Dimension", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(DimSize,     "dim_size",     "Size",        "Dimension", Linear, 0.f, 1.f, 0.5f, 1.f, Percent, nullptr, false) \
    X(DimMix,      "dim_mix",      "Mix",         "Dimension", Linear, 0.f, 1.f, 0.5f, 1.f, Percent, nullptr, false) \
    X(EqSlot,      "eq_slot",      "EQ Slot",     "FX", Linear, 0, 10, 7, 1.f, Int, nullptr, false) \
    X(DimSlot,     "dim_slot",     "Dimension Slot", "FX", Linear, 0, 10, 5, 1.f, Int, nullptr, false) \
    X(Lfo1Env,     "lfo1_env",     "Env",         "LFO 1", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(Lfo2Env,     "lfo2_env",     "Env",         "LFO 2", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(Lfo3Env,     "lfo3_env",     "Env",         "LFO 3", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(AenvBursts,  "aenv_bursts",  "Bursts",      "Amp Env", Linear, 1, 8, 1, 1.f, Int, nullptr, false) \
    X(AenvGap,     "aenv_gap",     "Gap",         "Amp Env", Power, 2.f, 40.f, 10.f, 2.f, Ms, nullptr, false) \
    PSY_LFO_CURVE (X, 1) PSY_LFO_CURVE (X, 2) PSY_LFO_CURVE (X, 3)

// An LFO's drawn curve: how many points it uses, then 16 points of x
// (0..1 of the cycle; the first sits at 0 whatever its own x), y (-1..1) and
// the bend of the segment from the point to the next; after the last point
// the curve runs back to the first's value at the cycle's end (see
// dsp::LfoCurve). The default is a triangle from the bottom.
#define PSY_LFO_CURVE(X, n) \
    X(Lfo##n##Points, "lfo" #n "_points", "Points", "LFO " #n " Shape", Linear, 2, 16, 3, 1.f, Int, nullptr, false) \
    PSY_LFO_POINT (X, n, 1, 0.f, -1.f)  PSY_LFO_POINT (X, n, 2, 0.5f, 1.f)  PSY_LFO_POINT (X, n, 3, 1.f, -1.f) \
    PSY_LFO_POINT (X, n, 4, 1.f, 0.f)   PSY_LFO_POINT (X, n, 5, 1.f, 0.f)   PSY_LFO_POINT (X, n, 6, 1.f, 0.f) \
    PSY_LFO_POINT (X, n, 7, 1.f, 0.f)   PSY_LFO_POINT (X, n, 8, 1.f, 0.f)   PSY_LFO_POINT (X, n, 9, 1.f, 0.f) \
    PSY_LFO_POINT (X, n, 10, 1.f, 0.f)  PSY_LFO_POINT (X, n, 11, 1.f, 0.f)  PSY_LFO_POINT (X, n, 12, 1.f, 0.f) \
    PSY_LFO_POINT (X, n, 13, 1.f, 0.f)  PSY_LFO_POINT (X, n, 14, 1.f, 0.f)  PSY_LFO_POINT (X, n, 15, 1.f, 0.f) \
    PSY_LFO_POINT (X, n, 16, 1.f, 0.f)

#define PSY_LFO_POINT(X, n, k, x, y) \
    X(Lfo##n##X##k, "lfo" #n "_x" #k, "Point " #k " X", "LFO " #n " Shape", Linear, 0.f, 1.f, x, 1.f, Percent, nullptr, false) \
    X(Lfo##n##Y##k, "lfo" #n "_y" #k, "Point " #k " Y", "LFO " #n " Shape", Linear, -1.f, 1.f, y, 1.f, Percent, nullptr, false) \
    X(Lfo##n##B##k, "lfo" #n "_b" #k, "Point " #k " Bend", "LFO " #n " Shape", Linear, -1.f, 1.f, 0.f, 1.f, Percent, nullptr, false)

// The Phoscyon engine (EngineMode::Phoscyon): a TB-303 voice with its
// calibration trims, and the effects that replace ours in its chain. Knobs
// keep the original's ranges and units.
#define PSY_PHOS_PARAMS(X) \
    X(PhosWave,      "phos_wave",       "Wave",        "Phoscyon", Choice, 0, 1, 0, 1.f, None, &choices::phosWave, false) \
    X(PhosTune,      "phos_tune",       "Tuning",      "Phoscyon", Linear, -12.f, 12.f, 0.f, 1.f, Semi, nullptr, true) \
    X(PhosCutoff,    "phos_cutoff",     "Cut Off",     "Phoscyon", Linear, 0.f, 1.f, 0.34f, 1.f, Percent, nullptr, true) \
    X(PhosReso,      "phos_reso",       "Resonance",   "Phoscyon", Linear, 0.f, 1.f, 0.904f, 1.f, Percent, nullptr, true) \
    X(PhosEnvMod,    "phos_envmod",     "Envmod",      "Phoscyon", Linear, 0.f, 1.f, 0.32f, 1.f, Percent, nullptr, true) \
    X(PhosDecay,     "phos_decay",      "Decay",       "Phoscyon", Linear, 0.f, 1.f, 0.664f, 1.f, Percent, nullptr, true) \
    X(PhosAccent,    "phos_accent",     "Accent",      "Phoscyon", Linear, 0.f, 1.f, 1.f, 1.f, Percent, nullptr, true) \
    X(PhosSlide,     "phos_slide",      "Slide Time",  "Phoscyon", Linear, 0.f, 1.f, 0.5f, 1.f, Percent, nullptr, false) \
    X(PhosSweep,     "phos_sweep",      "Sweep Amt.",  "Phoscyon", Linear, 0.f, 1.f, 1.f, 1.f, Percent, nullptr, false) \
    X(PhosEnvAttack, "phos_env_attack", "Env. Attack", "Phoscyon", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(PhosAccDecay,  "phos_acc_decay",  "Acc. Decay",  "Phoscyon", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(PhosAccVolume, "phos_acc_vol",    "Acc. Vol.",   "Phoscyon", Linear, 0.f, 1.f, 1.f, 1.f, Percent, nullptr, false) \
    X(PhosVibSpeed,  "phos_vib_speed",  "Vib. Speed",  "Phoscyon", Linear, 0.f, 1.f, 0.5f, 1.f, Percent, nullptr, false) \
    X(PhosVibDepth,  "phos_vib_depth",  "Vib. Depth",  "Phoscyon", Linear, 0.f, 1.f, 0.5f, 1.f, Percent, nullptr, false) \
    X(PhosResoCouple,"phos_reso_couple","Reso / Sweep Couple", "Phoscyon", Toggle, 0, 1, 1, 1.f, None, nullptr, false) \
    X(PhosAccCouple, "phos_acc_couple", "Acc. / Volume Couple", "Phoscyon", Toggle, 0, 1, 1, 1.f, None, nullptr, false) \
    X(PhosTm3,       "phos_tm3",        "TM3 Cutoff Tuning",    "Phoscyon Calibration", Linear, -500.f, 500.f, 0.f, 1.f, Mv, nullptr, false) \
    X(PhosC21,       "phos_c21",        "C21/C22 DC Filter",    "Phoscyon Calibration", Exp, 7.9f, 42.5f, 15.9f, 1.f, NanoFarad, nullptr, false) \
    X(PhosAccFEnv,   "phos_acc_fenv",   "Accent Filter Env",    "Phoscyon Calibration", Linear, 0.f, 4000.f, 1951.f, 1.f, Mv, nullptr, false) \
    X(PhosAccAEnv,   "phos_acc_aenv",   "Accent Amp Env",       "Phoscyon Calibration", Linear, 0.f, 3.5f, 1.0984f, 1.f, Volt, nullptr, false) \
    X(PhosC13,       "phos_c13",        "C13 Accent Capacitor", "Phoscyon Calibration", Inverse, 457.f, 1638000.f, 926.f, 1.f, NanoFarad, nullptr, false) \
    X(PhosTm5,       "phos_tm5",        "TM5 Octave Width",     "Phoscyon Calibration", BiPower, -0.1f, 0.1f, 0.f, 2.f, OctPerVolt, nullptr, false) \
    X(PhosPwShift,   "phos_pw_shift",   "Square PW Shift",      "Phoscyon Calibration", Linear, -1.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(PhosClicks,    "phos_clicks",     "BA662 Clicks",         "Phoscyon Calibration", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(PhosNoise,     "phos_noise",      "BA662 Noise",          "Phoscyon Calibration", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(PhosDistOn,    "phos_dist_on",    "On",          "Phoscyon Distortion", Toggle, 0, 1, 1, 1.f, None, nullptr, false) \
    X(PhosDistModel, "phos_dist_model", "Model",       "Phoscyon Distortion", Choice, 0, 18, 6, 1.f, None, &choices::phosDistModel, false) \
    X(PhosDistDyn,   "phos_dist_dyn",   "Dynamics",    "Phoscyon Distortion", Linear, 0.f, 1.f, 1.f, 1.f, Percent, nullptr, false) \
    X(PhosDistOrder, "phos_dist_order", "Order",       "Phoscyon Distortion", Choice, 0, 1, 0, 1.f, None, &choices::phosDistOrder, false) \
    X(PhosDistDrive, "phos_dist_drive", "Drive",       "Phoscyon Distortion", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(PhosDistColor, "phos_dist_color", "Color",       "Phoscyon Distortion", Linear, -1.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    PSY_PHOS_EQ_BAND (X, Low, "low", "Low", 100.f, 2.f) \
    PSY_PHOS_EQ_BAND (X, MidLow, "midlow", "Mid-Low", 550.f, 1.5f) \
    PSY_PHOS_EQ_BAND (X, MidHigh, "midhigh", "Mid-High", 3500.f, 2.63f) \
    PSY_PHOS_EQ_BAND (X, High, "high", "High", 10000.f, 3.53f) \
    X(PhosEqOn,      "phos_eq_on",      "On",          "Phoscyon EQ", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(PhosDlyOn,     "phos_dly_on",     "On",          "Phoscyon Delay", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(PhosDlyFb,     "phos_dly_fb",     "Feedback",    "Phoscyon Delay", Linear, 0.f, 1.f, 0.132f, 1.f, Percent, nullptr, false) \
    X(PhosDlyCut,    "phos_dly_cut",    "FB Cutoff",   "Phoscyon Delay", Linear, 10.f, 16000.f, 16000.f, 1.f, Hz, nullptr, false) \
    X(PhosDlyReso,   "phos_dly_reso",   "FB Reso",     "Phoscyon Delay", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(PhosDlyFilter, "phos_dly_filter", "FB Filter",   "Phoscyon Delay", Choice, 0, 3, 0, 1.f, None, &choices::phosDelayFilter, false) \
    X(PhosDlySpread, "phos_dly_spread", "Spread",      "Phoscyon Delay", Linear, -1.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(PhosDlyTime,   "phos_dly_time",   "Time",        "Phoscyon Delay", Linear, 0.01f, 1.f, 0.22f, 1.f, Sec, nullptr, false) \
    X(PhosDlySync,   "phos_dly_sync",   "Sync",        "Phoscyon Delay", Toggle, 0, 1, 1, 1.f, None, nullptr, false) \
    X(PhosDlyLScale, "phos_dly_lscale", "L Scale",     "Phoscyon Delay", Choice, 0, 2, 0, 1.f, None, &choices::phosDelayScale, false) \
    X(PhosDlyLNum,   "phos_dly_lnum",   "L Steps",     "Phoscyon Delay", Linear, 1, 32, 3, 1.f, Int, nullptr, false) \
    X(PhosDlyLDen,   "phos_dly_lden",   "L Division",  "Phoscyon Delay", Linear, 1, 32, 16, 1.f, Int, nullptr, false) \
    X(PhosDlyRScale, "phos_dly_rscale", "R Scale",     "Phoscyon Delay", Choice, 0, 2, 0, 1.f, None, &choices::phosDelayScale, false) \
    X(PhosDlyRNum,   "phos_dly_rnum",   "R Steps",     "Phoscyon Delay", Linear, 1, 32, 3, 1.f, Int, nullptr, false) \
    X(PhosDlyRDen,   "phos_dly_rden",   "R Division",  "Phoscyon Delay", Linear, 1, 32, 16, 1.f, Int, nullptr, false) \
    X(PhosDlyMix,    "phos_dly_mix",    "Mix",         "Phoscyon Delay", Linear, 0.f, 1.f, 0.304f, 1.f, Percent, nullptr, false) \
    X(PhosRevOn,     "phos_rev_on",     "On",          "Phoscyon Reverb", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(PhosRevPre,    "phos_rev_pre",    "Predelay",    "Phoscyon Reverb", Linear, 0.f, 1000.f, 116.f, 1.f, Ms, nullptr, false) \
    X(PhosRevESize,  "phos_rev_esize",  "Early Size",  "Phoscyon Reverb", Linear, 1.1f, 20.5f, 1.1f, 1.f, Meter, nullptr, false) \
    X(PhosRevEDiff,  "phos_rev_ediff",  "Early Diffusion", "Phoscyon Reverb", Linear, 0.f, 1.f, 0.42f, 1.f, Percent, nullptr, false) \
    X(PhosRevLSize,  "phos_rev_lsize",  "Late Size",   "Phoscyon Reverb", Linear, 5.1f, 51.1f, 5.1f, 1.f, Meter, nullptr, false) \
    X(PhosRevLDiff,  "phos_rev_ldiff",  "Late Diffusion", "Phoscyon Reverb", Linear, 0.f, 1.f, 0.45f, 1.f, Percent, nullptr, false) \
    X(PhosRevFb,     "phos_rev_fb",     "Feedback",    "Phoscyon Reverb", Linear, 0.f, 1.f, 0.5f, 1.f, Percent, nullptr, false) \
    X(PhosRevBlend,  "phos_rev_blend",  "Early/Late",  "Phoscyon Reverb", Linear, 0.f, 1.f, 0.204f, 1.f, Percent, nullptr, false) \
    X(PhosRevMod,    "phos_rev_mod",    "Tail Mod",    "Phoscyon Reverb", Linear, 0.f, 1.f, 0.47f, 1.f, Percent, nullptr, false) \
    X(PhosRevEBright,"phos_rev_ebright","Early Bright", "Phoscyon Reverb", Linear, 0.f, 1.f, 0.7f, 1.f, Percent, nullptr, false) \
    X(PhosRevLBright,"phos_rev_lbright","Late Bright", "Phoscyon Reverb", Linear, 0.f, 1.f, 0.7f, 1.f, Percent, nullptr, false) \
    X(PhosRevMix,    "phos_rev_mix",    "Mix",         "Phoscyon Reverb", Linear, 0.f, 1.f, 0.288f, 1.f, Percent, nullptr, false) \
    X(PhosChoOn,     "phos_cho_on",     "On",          "Phoscyon Chorus", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(PhosChoOffset, "phos_cho_offset", "Offset",      "Phoscyon Chorus", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(PhosChoDepth,  "phos_cho_depth",  "Depth",       "Phoscyon Chorus", Linear, 0.f, 1.f, 0.372f, 1.f, Percent, nullptr, false) \
    X(PhosChoRate,   "phos_cho_rate",   "Rate",        "Phoscyon Chorus", Linear, 0.f, 1.f, 0.36f, 1.f, Percent, nullptr, false) \
    X(PhosChoSpatial,"phos_cho_spatial","Spatial",     "Phoscyon Chorus", Linear, 0.f, 1.f, 0.5f, 1.f, Percent, nullptr, false) \
    X(PhosChoFat,    "phos_cho_fat",    "Fatness",     "Phoscyon Chorus", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(PhosChoHp,     "phos_cho_hp",     "HP Cutoff",   "Phoscyon Chorus", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(PhosChoOut,    "phos_cho_out",    "Output",      "Phoscyon Chorus", Linear, -24.f, 12.f, 0.f, 1.f, Db, nullptr, false) \
    X(PhosChoMix,    "phos_cho_mix",    "Mix",         "Phoscyon Chorus", Linear, 0.f, 1.f, 0.5f, 1.f, Percent, nullptr, false) \
    X(PhosLimOn,     "phos_lim_on",     "On",          "Phoscyon Limiter", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(PhosLimKnee,   "phos_lim_knee",   "Knee",        "Phoscyon Limiter", Linear, 0.f, 1.f, 1.f, 1.f, Percent, nullptr, false) \
    X(PhosLimRatio,  "phos_lim_ratio",  "Ratio",       "Phoscyon Limiter", Exp, 2.f, 40.f, 40.f, 1.f, RatioTo1, nullptr, false) \
    X(PhosLimAttack, "phos_lim_attack", "Attack",      "Phoscyon Limiter", Exp, 0.1f, 100.f, 0.1f, 1.f, Ms, nullptr, false) \
    X(PhosLimRelease,"phos_lim_release","Release",     "Phoscyon Limiter", Exp, 25.f, 500.f, 145.6f, 1.f, Ms, nullptr, false) \
    X(PhosLimThresh, "phos_lim_thresh", "Threshold",   "Phoscyon Limiter", Linear, -48.f, 0.f, 0.f, 1.f, Db, nullptr, false) \
    X(PhosLimGain,   "phos_lim_gain",   "Gain",        "Phoscyon Limiter", Linear, 0.f, 24.f, 0.f, 1.f, Db, nullptr, false) \
    X(PhosLimClip,   "phos_lim_clip",   "Soft Clip",   "Phoscyon Limiter", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(PhosEqSlot,    "phos_eq_slot",    "EQ Slot",     "FX", Linear, 0, 10, 1, 1.f, Int, nullptr, false) \
    X(PhosLimSlot,   "phos_lim_slot",   "Limiter Slot","FX", Linear, 0, 10, 10, 1.f, Int, nullptr, false)

// One band of the Phoscyon EQ: gain, frequency, width in octaves.
#define PSY_PHOS_EQ_BAND(X, B, id, name, hz, oct) \
    X(PhosEq##B##Gain,  "phos_eq_" id "_gain",  name " Gain",  "Phoscyon EQ", Linear, -24.f, 24.f, 0.f, 1.f, Db, nullptr, false) \
    X(PhosEq##B##Freq,  "phos_eq_" id "_freq",  name " Freq",  "Phoscyon EQ", Exp, 20.f, 16000.f, hz, 1.f, Hz, nullptr, false) \
    X(PhosEq##B##Width, "phos_eq_" id "_width", name " Width", "Phoscyon EQ", Linear, 0.5f, 4.f, oct, 1.f, Octaves, nullptr, false)

// One modulation slot: source, target, bipolar amount.
#define PSY_MOD_SLOT(X, n) \
    X(Mod##n##Src, "mod" #n "_src", "Source", "Mod " #n, Choice, 0, 18, 0, 1.f, None, &choices::modSrc, false) \
    X(Mod##n##Dst, "mod" #n "_dst", "Target", "Mod " #n, Choice, 0, 63, 0, 1.f, None, &choices::modDst, false) \
    X(Mod##n##Amt, "mod" #n "_amt", "Amount", "Mod " #n, BiPower, -1.f, 1.f, 0.f, 1.5f, Percent, nullptr, false)

// One step of the arp pattern: plays, accented, slides into the next step,
// and its note in semitones from the key the arp picked.
#define PSY_ARP_STEP(X, n) \
    X(ArpStep##n,  "arp_step" #n,  "Step " #n,   "Arp", Toggle, 0, 1, 1, 1.f, None, nullptr, false) \
    X(ArpAcc##n,   "arp_acc" #n,   "Accent " #n, "Arp", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(ArpSlide##n, "arp_slide" #n, "Slide " #n,  "Arp", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(ArpNote##n,  "arp_note" #n,  "Note " #n,   "Arp", Linear, -24, 24, 0, 1.f, Int, nullptr, false)

enum class P : int
{
#define PSY_ENUM(e, ...) e,
    PSY_PARAMS (PSY_ENUM)
#undef PSY_ENUM
    Count
};

constexpr int kNumParams = (int) P::Count;
constexpr int kNumModSlots = 16;
constexpr int kNumLfos = 3;
constexpr int kMaxVoices = 16;

const std::array<ParamDef, kNumParams>& paramDefs() noexcept;
inline const ParamDef& def (P p) noexcept { return paramDefs()[(size_t) p]; }
int findParam (std::string_view id) noexcept;  // -1 if unknown

// Parameter index helpers for the repeated blocks.
inline P modSrcParam (int slot) noexcept { return (P) ((int) P::Mod1Src + slot * 3); }
inline P modDstParam (int slot) noexcept { return (P) ((int) P::Mod1Dst + slot * 3); }
inline P modAmtParam (int slot) noexcept { return (P) ((int) P::Mod1Amt + slot * 3); }
inline P lfoParam (int lfo, int offset) noexcept { return (P) ((int) P::Lfo1Shape + lfo * 6 + offset); }
// Multiband compressor, per band (0 low, 1 mid, 2 high).
inline P mbDownParam (int band) noexcept { return (P) ((int) P::MbLowDown + band); }
inline P mbUpParam (int band) noexcept { return (P) ((int) P::MbLowUp + band); }
inline P mbGainParam (int band) noexcept { return (P) ((int) P::MbLowGain + band); }

// Modulation targets and the knobs they sit on. Drag-and-drop and the
// rings around modulated knobs go through these.
P modTargetParam (ModDst d) noexcept;   // P::Count when the target has no knob (Pitch, Amp, Pan)
ModDst modTargetFor (P p) noexcept;     // ModDst::None when the knob can't be modulated
// How far an amount of 1.0 moves the target, as a share of its knob's
// travel (exact for effects, approximate for the voice targets).
float modDepthNorm (ModDst d) noexcept;
// Effect targets are modulated in their parameter's normalised range, once
// for all voices, after the voices are mixed.
constexpr bool isFxTarget (ModDst d) noexcept
{
    return ((int) d >= (int) ModDst::DistDrive && (int) d <= (int) ModDst::WidthAmount)
        || ((int) d >= (int) ModDst::PhosDistDrive && d != ModDst::Count);
}
// The Phoscyon voice's own targets (its knobs, read by PhosVoice).
constexpr bool isPhosVoiceTarget (ModDst d) noexcept { return (int) d >= (int) ModDst::PhosCutoff && (int) d <= (int) ModDst::PhosTune; }

// The effects in their default signal order. FxOrder holds the order the
// chain plays them in, as the index of that permutation (0 = this order).
enum class Fx : int { Distortion, Crush, Shift, Phaser, Chorus, Compressor, Delay, Reverb, Width, Count };
constexpr int kNumFx = (int) Fx::Count;
using FxChain = std::array<int, kNumFx>;   // effect ids, first to last
FxChain fxChain (int orderCode) noexcept;
int fxOrderCode (const FxChain&) noexcept;   // -1 unless every effect appears once

// The Phoscyon engine plays two more effects, its EQ and limiter. Their places
// in the eleven-long chain are their own parameters (PhosEqSlot, PhosLimSlot),
// so the nine effects keep their FxOrder code and sessions from before load.
constexpr int kPhosEq = kNumFx, kPhosLimiter = kNumFx + 1, kNumPhosFx = kNumFx + 2;
using PhosFxChain = std::array<int, kNumPhosFx>;
PhosFxChain phosFxChain (int orderCode, int eqSlot, int limSlot) noexcept;
// The inverse: the order code and both slots of an eleven-long chain.
void phosFxSlots (const PhosFxChain& chain, int& orderCode, int& eqSlot, int& limSlot) noexcept;

// The synth engine's chain is eleven long too: the nine plus our EQ and
// dimension expander, placed by EqSlot and DimSlot the same way (their ids
// are the Phoscyon pair's numbers; the two chains never play together).
constexpr int kEq = kNumFx, kDimension = kNumFx + 1, kNumSynthFx = kNumFx + 2;
using SynthFxChain = std::array<int, kNumSynthFx>;
SynthFxChain synthFxChain (int orderCode, int eqSlot, int dimSlot) noexcept;
void synthFxSlots (const SynthFxChain& chain, int& orderCode, int& eqSlot, int& dimSlot) noexcept;

// The arp's key (ArpKey: 0 off, 1..12 C..B) and scale. With a key the arp
// plays every note in that scale, and generated lines are written in it.
constexpr int kNumArpScales = 6;
const std::array<int, 7>& arpScaleSteps (int scale) noexcept;   // semitones of the 7 degrees
int quantizeToScale (int note, int key, int scale) noexcept;    // key 0..11; nearest scale note, down on a tie

// Arp pattern lanes, per step: plays, accent, slide, note (see PSY_ARP_STEP).
constexpr int kArpSteps = 16;
constexpr int kArpNoteRange = 24;   // the note lane spans +-2 octaves
enum class ArpLane : int { On, Accent, Slide, Note };
inline P arpStepParam (int step, ArpLane lane) noexcept { return (P) ((int) P::ArpStep1 + step * 4 + (int) lane); }

// A full patch: plain (denormalised) values for every parameter.
struct ParamState
{
    std::array<float, kNumParams> v {};

    ParamState();  // defaults
    float get (P p) const noexcept { return v[(size_t) p]; }
    int geti (P p) const noexcept { return (int) std::lround (v[(size_t) p]); }
    bool on (P p) const noexcept { return v[(size_t) p] > 0.5f; }
    void set (P p, float plain) noexcept { v[(size_t) p] = def (p).snap (plain); }
};

// The LFOs' envelope mode and drawn curves (outside the 6-a-LFO block).
inline P lfoEnvParam (int lfo) noexcept { return (P) ((int) P::Lfo1Env + lfo); }
constexpr int kLfoPoints = 16;
enum class LfoLane : int { X, Y, Bend };
inline P lfoPointsParam (int lfo) noexcept { return (P) ((int) P::Lfo1Points + lfo * (1 + 3 * kLfoPoints)); }
inline P lfoPointParam (int lfo, int point, LfoLane lane) noexcept
{
    return (P) ((int) lfoPointsParam (lfo) + 1 + point * 3 + (int) lane);
}
// An LFO runs in each voice, restarting with the note, when Retrig is on
// or it is an envelope (it runs once from the note and holds its end).
inline bool lfoPerVoice (const ParamState& p, int lfo) noexcept { return p.on (lfoParam (lfo, 4)) || p.on (lfoEnvParam (lfo)); }

// The arp pattern as one value. Step 0 is the beat when the host plays.
struct ArpPattern
{
    enum Flag : uint8_t { On = 1, Accent = 2, Slide = 4 };

    int length = kArpSteps;
    std::array<uint8_t, kArpSteps> steps {};
    std::array<int8_t, kArpSteps> notes {};   // semitones added to the step's key

    bool has (int step, Flag f) const noexcept { return (steps[(size_t) step] & f) != 0; }
    void setFlag (int step, Flag f, bool on) noexcept
    {
        steps[(size_t) step] = (uint8_t) (on ? (steps[(size_t) step] | f) : (steps[(size_t) step] & ~f));
    }

    static ArpPattern from (const ParamState&) noexcept;
    void writeTo (ParamState&) const noexcept;

    // '-' rest, 'x' play, 'X' accent, 's' slide, 'S' accent + slide.
    // Shorter strings repeat to fill 16 steps; the length stays 16 and every
    // note is the key.
    static ArpPattern parse (std::string_view text) noexcept;
    bool operator== (const ArpPattern&) const = default;
    bool sameRhythm (const ArpPattern& o) const noexcept { return length == o.length && steps == o.steps; }
};

} // namespace psy
