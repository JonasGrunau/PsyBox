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
    Toggle    // 0 / 1
};

enum class Unit : uint8_t { None, Percent, Hz, Ms, Sec, Semi, Cents, Db, Octaves, Ratio, RatioTo1, Int };

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
    int numChoices() const noexcept { return choices ? (int) choices->size() : 0; }
    bool isDiscrete() const noexcept { return mapping == Mapping::Choice || mapping == Mapping::Toggle || unit == Unit::Int; }
};

// ---- choice lists --------------------------------------------------------
namespace choices
{
    extern const std::vector<std::string> voiceMode, glideMode, quality, oscType, wavetable,
        subOct, subShape, noiseType, penvDest, filterType, filterRouting, lfoShape, syncDiv,
        modSrc, modDst, distType, chorusMode, compMode, arpMode, engineMode, kickClick, kickDrive,
        energyMode, energyBars;
}

// Sync divisions expressed in beats (quarter notes), index-aligned with choices::syncDiv.
float syncDivBeats (int index) noexcept;
// Energy lengths in bars, index-aligned with choices::energyBars (4/4: a bar is 4 beats).
float energyBarsValue (int index) noexcept;

enum class VoiceMode : int { Poly, Mono, Legato };
enum class OscType : int { Analog, Supersaw, Wavetable, FM };
enum class LfoShape : int { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom };
enum class DistType : int { Soft, Hard, Fold, Tube, Rectify };
enum class CompMode : int { Normal, Multiband };
enum class ArpMode : int { Up, Down, UpDown, Random, Order };
// A patch is a synth sound or a kick; the category decides, never the recipe.
enum class EngineMode : int { Synth, Kick };
enum class EnergyMode : int { Bars, Time };
enum class KickClick : int { Noise, Tick, Blip };
enum class KickDrive : int { Soft, Tube, Hard };

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
    Count
};

// ---- the table -----------------------------------------------------------
// X(enumName, "id", "Display Name", "Group", mapping, min, max, default, skew, unit, choices, modulatable)
#define PSY_PARAMS(X) \
    X(MasterVol,   "master_vol",   "Volume",      "Master", Linear, -48.f, 6.f, -6.f, 1.f, Db, nullptr, false) \
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
    \
    X(SubLevel,    "sub_level",    "Sub",         "Mixer", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, true) \
    X(SubOct,      "sub_oct",      "Sub Oct",     "Mixer", Choice, 0, 1, 0, 1.f, None, &choices::subOct, false) \
    X(SubShape,    "sub_shape",    "Sub Wave",    "Mixer", Choice, 0, 2, 0, 1.f, None, &choices::subShape, false) \
    X(NoiseLevel,  "noise_level",  "Noise",       "Mixer", Power, 0.f, 1.f, 0.f, 2.f, Percent, nullptr, true) \
    X(NoiseType,   "noise_type",   "Noise Type",  "Mixer", Choice, 0, 2, 0, 1.f, None, &choices::noiseType, false) \
    X(NoiseDirect, "noise_direct", "Noise Direct","Mixer", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(RingLevel,   "ring_level",   "Ring",        "Mixer", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, true) \
    \
    X(PenvAmt,     "penv_amt",     "Amount",      "Pitch Env", BiPower, -60.f, 60.f, 0.f, 2.f, Semi, nullptr, true) \
    X(PenvAttack,  "penv_attack",  "Attack",      "Pitch Env", Power, 0.f, 4000.f, 0.f, 3.f, Ms, nullptr, false) \
    X(PenvDecay,   "penv_decay",   "Decay",       "Pitch Env", Power, 1.f, 10000.f, 120.f, 3.f, Ms, nullptr, false) \
    X(PenvCurve,   "penv_curve",   "Curve",       "Pitch Env", Linear, 0.f, 1.f, 0.7f, 1.f, Percent, nullptr, false) \
    X(PenvDest,    "penv_dest",    "Target",      "Pitch Env", Choice, 0, 2, 0, 1.f, None, &choices::penvDest, false) \
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
    X(FenvA,       "fenv_a",       "Attack",      "Filter Env", Power, 0.f, 10000.f, 0.f, 3.f, Ms, nullptr, false) \
    X(FenvD,       "fenv_d",       "Decay",       "Filter Env", Power, 1.f, 10000.f, 250.f, 3.f, Ms, nullptr, false) \
    X(FenvS,       "fenv_s",       "Sustain",     "Filter Env", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(FenvR,       "fenv_r",       "Release",     "Filter Env", Power, 1.f, 15000.f, 200.f, 3.f, Ms, nullptr, false) \
    X(MenvA,       "menv_a",       "Attack",      "Mod Env", Power, 0.f, 10000.f, 0.f, 3.f, Ms, nullptr, false) \
    X(MenvD,       "menv_d",       "Decay",       "Mod Env", Power, 1.f, 10000.f, 500.f, 3.f, Ms, nullptr, false) \
    X(MenvS,       "menv_s",       "Sustain",     "Mod Env", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(MenvR,       "menv_r",       "Release",     "Mod Env", Power, 1.f, 15000.f, 300.f, 3.f, Ms, nullptr, false) \
    \
    X(Lfo1Shape,   "lfo1_shape",   "Shape",       "LFO 1", Choice, 0, 6, 0, 1.f, None, &choices::lfoShape, false) \
    X(Lfo1Rate,    "lfo1_rate",    "Rate",        "LFO 1", Exp, 0.02f, 40.f, 2.f, 1.f, Hz, nullptr, true) \
    X(Lfo1Sync,    "lfo1_sync",    "Sync",        "LFO 1", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(Lfo1Div,     "lfo1_div",     "Division",    "LFO 1", Choice, 0, 17, 9, 1.f, None, &choices::syncDiv, false) \
    X(Lfo1Retrig,  "lfo1_retrig",  "Retrig",      "LFO 1", Toggle, 0, 1, 1, 1.f, None, nullptr, false) \
    X(Lfo1Phase,   "lfo1_phase",   "Phase",       "LFO 1", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(Lfo2Shape,   "lfo2_shape",   "Shape",       "LFO 2", Choice, 0, 6, 1, 1.f, None, &choices::lfoShape, false) \
    X(Lfo2Rate,    "lfo2_rate",    "Rate",        "LFO 2", Exp, 0.02f, 40.f, 0.5f, 1.f, Hz, nullptr, true) \
    X(Lfo2Sync,    "lfo2_sync",    "Sync",        "LFO 2", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(Lfo2Div,     "lfo2_div",     "Division",    "LFO 2", Choice, 0, 17, 6, 1.f, None, &choices::syncDiv, false) \
    X(Lfo2Retrig,  "lfo2_retrig",  "Retrig",      "LFO 2", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(Lfo2Phase,   "lfo2_phase",   "Phase",       "LFO 2", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(Lfo3Shape,   "lfo3_shape",   "Shape",       "LFO 3", Choice, 0, 6, 5, 1.f, None, &choices::lfoShape, false) \
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
    \
    X(EnergyMode,  "energy_mode",  "Length In",   "Energy", Choice, 0, 1, 0, 1.f, None, &choices::energyMode, false) \
    X(EnergyBars,  "energy_bars",  "Length",      "Energy", Choice, 0, 7, 4, 1.f, None, &choices::energyBars, false) \
    X(EnergySec,   "energy_sec",   "Length",      "Energy", Power, 0.1f, 60.f, 6.f, 2.f, Sec, nullptr, false) \
    X(EnergyCurve, "energy_curve", "Curve",       "Energy", BiPower, -1.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(EnergyFade,  "energy_fade",  "Fade",        "Energy", BiPower, -1.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(EnergyCut,   "energy_cut",   "Cut at End",  "Energy", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    \
    X(ArpOn,       "arp_on",       "On",          "Arp", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(ArpMode,     "arp_mode",     "Mode",        "Arp", Choice, 0, 4, 0, 1.f, None, &choices::arpMode, false) \
    X(ArpDiv,      "arp_div",      "Rate",        "Arp", Choice, 0, 17, 12, 1.f, None, &choices::syncDiv, false) \
    X(ArpOctaves,  "arp_oct",      "Octaves",     "Arp", Linear, 1, 4, 1, 1.f, Int, nullptr, false) \
    X(ArpGate,     "arp_gate",     "Gate",        "Arp", Linear, 0.05f, 1.5f, 0.5f, 1.f, Percent, nullptr, false) \
    X(ArpSwing,    "arp_swing",    "Swing",       "Arp", Linear, 0.f, 0.75f, 0.f, 1.f, Percent, nullptr, false) \
    X(ArpHold,     "arp_hold",     "Hold",        "Arp", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(ArpLength,   "arp_length",   "Steps",       "Arp", Linear, 1, 16, 16, 1.f, Int, nullptr, false) \
    PSY_ARP_STEP (X, 1)  PSY_ARP_STEP (X, 2)  PSY_ARP_STEP (X, 3)  PSY_ARP_STEP (X, 4) \
    PSY_ARP_STEP (X, 5)  PSY_ARP_STEP (X, 6)  PSY_ARP_STEP (X, 7)  PSY_ARP_STEP (X, 8) \
    PSY_ARP_STEP (X, 9)  PSY_ARP_STEP (X, 10) PSY_ARP_STEP (X, 11) PSY_ARP_STEP (X, 12) \
    PSY_ARP_STEP (X, 13) PSY_ARP_STEP (X, 14) PSY_ARP_STEP (X, 15) PSY_ARP_STEP (X, 16) \
    \
    X(EngineMode,  "engine_mode",  "Engine",      "Engine", Choice, 0, 1, 0, 1.f, None, &choices::engineMode, false) \
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
    X(KickTone,    "kick_tone",    "Tone",        "Kick Drive", Exp, 200.f, 20000.f, 20000.f, 1.f, Hz, nullptr, false)

// One modulation slot: source, target, bipolar amount.
#define PSY_MOD_SLOT(X, n) \
    X(Mod##n##Src, "mod" #n "_src", "Source", "Mod " #n, Choice, 0, 18, 0, 1.f, None, &choices::modSrc, false) \
    X(Mod##n##Dst, "mod" #n "_dst", "Target", "Mod " #n, Choice, 0, 46, 0, 1.f, None, &choices::modDst, false) \
    X(Mod##n##Amt, "mod" #n "_amt", "Amount", "Mod " #n, BiPower, -1.f, 1.f, 0.f, 1.5f, Percent, nullptr, false)

// One step of the arp pattern: plays, accented, slides into the next step.
#define PSY_ARP_STEP(X, n) \
    X(ArpStep##n,  "arp_step" #n,  "Step " #n,   "Arp", Toggle, 0, 1, 1, 1.f, None, nullptr, false) \
    X(ArpAcc##n,   "arp_acc" #n,   "Accent " #n, "Arp", Toggle, 0, 1, 0, 1.f, None, nullptr, false) \
    X(ArpSlide##n, "arp_slide" #n, "Slide " #n,  "Arp", Toggle, 0, 1, 0, 1.f, None, nullptr, false)

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
constexpr bool isFxTarget (ModDst d) noexcept { return (int) d >= (int) ModDst::DistDrive && d != ModDst::Count; }

// Arp pattern lanes, per step: plays, accent, slide (see PSY_ARP_STEP).
constexpr int kArpSteps = 16;
enum class ArpLane : int { On, Accent, Slide };
inline P arpStepParam (int step, ArpLane lane) noexcept { return (P) ((int) P::ArpStep1 + step * 3 + (int) lane); }

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

// The arp pattern as one value. Step 0 is the beat when the host plays.
struct ArpPattern
{
    enum Flag : uint8_t { On = 1, Accent = 2, Slide = 4 };

    int length = kArpSteps;
    std::array<uint8_t, kArpSteps> steps {};

    bool has (int step, Flag f) const noexcept { return (steps[(size_t) step] & f) != 0; }
    void setFlag (int step, Flag f, bool on) noexcept
    {
        steps[(size_t) step] = (uint8_t) (on ? (steps[(size_t) step] | f) : (steps[(size_t) step] & ~f));
    }

    static ArpPattern from (const ParamState&) noexcept;
    void writeTo (ParamState&) const noexcept;

    // '-' rest, 'x' play, 'X' accent, 's' slide, 'S' accent + slide.
    // Shorter strings repeat to fill 16 steps; the length stays 16.
    static ArpPattern parse (std::string_view text) noexcept;
    bool operator== (const ArpPattern&) const = default;
};

} // namespace psy
