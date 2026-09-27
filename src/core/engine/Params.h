#pragma once

// Every parameter the synth has, defined once. The engine reads plain floats
// indexed by ParamId, the generator writes normalised values, and the plugin
// builds its JUCE parameters from the same table, so the three can never
// disagree about a range.

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

enum class Unit : uint8_t { None, Percent, Hz, Ms, Sec, Semi, Cents, Db, Octaves, Ratio, Int };

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
        modSrc, modDst, distType, chorusMode;
}

// Sync divisions expressed in beats (quarter notes), index-aligned with choices::syncDiv.
float syncDivBeats (int index) noexcept;

enum class VoiceMode : int { Poly, Mono, Legato };
enum class OscType : int { Analog, Supersaw, Wavetable, FM };
enum class FilterType : int { Off, LP12, BP12, HP12, Notch, Ladder24, Diode303, Formant, CombPos, CombNeg };
enum class LfoShape : int { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom };
enum class DistType : int { Soft, Hard, Fold, Tube, Rectify };

enum class ModSrc : int
{
    None, AmpEnv, FilterEnv, ModEnv, PitchEnv, Lfo1, Lfo2, Lfo3, Velocity, Note, ModWheel,
    Aftertouch, PitchBend, Random, Macro1, Macro2, Macro3, Macro4, Count
};

enum class ModDst : int
{
    None, PitchAll, PitchA, PitchB, ShapeA, ShapeB, PwA, PwB, WtPosA, WtPosB, FmA, FmB,
    DetuneA, DetuneB, XMod, LevelA, LevelB, SubLevel, NoiseLevel, RingLevel,
    F1Cutoff, F1Reso, F1Drive, F1Vowel, F2Cutoff, F2Reso, Amp, Pan,
    Lfo1Rate, Lfo2Rate, Lfo3Rate, PitchEnvAmt, FilterEnvAmt, Count
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
    X(RingLevel,   "ring_level",   "Ring",        "Mixer", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, true) \
    \
    X(PenvAmt,     "penv_amt",     "Amount",      "Pitch Env", BiPower, -60.f, 60.f, 0.f, 2.f, Semi, nullptr, true) \
    X(PenvAttack,  "penv_attack",  "Attack",      "Pitch Env", Power, 0.f, 4000.f, 0.f, 3.f, Ms, nullptr, false) \
    X(PenvDecay,   "penv_decay",   "Decay",       "Pitch Env", Power, 1.f, 10000.f, 120.f, 3.f, Ms, nullptr, false) \
    X(PenvCurve,   "penv_curve",   "Curve",       "Pitch Env", Linear, 0.f, 1.f, 0.7f, 1.f, Percent, nullptr, false) \
    X(PenvDest,    "penv_dest",    "Target",      "Pitch Env", Choice, 0, 2, 0, 1.f, None, &choices::penvDest, false) \
    \
    X(F1Type,      "f1_type",      "Type",        "Filter 1", Choice, 0, 9, 1, 1.f, None, &choices::filterType, false) \
    X(F1Cutoff,    "f1_cutoff",    "Cutoff",      "Filter 1", Exp, 20.f, 20000.f, 8000.f, 1.f, Hz, nullptr, true) \
    X(F1Reso,      "f1_reso",      "Reso",        "Filter 1", Linear, 0.f, 1.f, 0.1f, 1.f, Percent, nullptr, true) \
    X(F1Drive,     "f1_drive",     "Drive",       "Filter 1", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, true) \
    X(F1Env,       "f1_env",       "Env",         "Filter 1", BiPower, -1.f, 1.f, 0.f, 1.3f, Percent, nullptr, true) \
    X(F1Key,       "f1_key",       "Key",         "Filter 1", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, false) \
    X(F1Vowel,     "f1_vowel",     "Vowel",       "Filter 1", Linear, 0.f, 1.f, 0.f, 1.f, Percent, nullptr, true) \
    \
    X(F2Type,      "f2_type",      "Type",        "Filter 2", Choice, 0, 9, 0, 1.f, None, &choices::filterType, false) \
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
    X(Mod1Src, "mod1_src", "Source", "Mod 1", Choice, 0, 17, 0, 1.f, None, &choices::modSrc, false) \
    X(Mod1Dst, "mod1_dst", "Target", "Mod 1", Choice, 0, 32, 0, 1.f, None, &choices::modDst, false) \
    X(Mod1Amt, "mod1_amt", "Amount", "Mod 1", BiPower, -1.f, 1.f, 0.f, 1.5f, Percent, nullptr, false) \
    X(Mod2Src, "mod2_src", "Source", "Mod 2", Choice, 0, 17, 0, 1.f, None, &choices::modSrc, false) \
    X(Mod2Dst, "mod2_dst", "Target", "Mod 2", Choice, 0, 32, 0, 1.f, None, &choices::modDst, false) \
    X(Mod2Amt, "mod2_amt", "Amount", "Mod 2", BiPower, -1.f, 1.f, 0.f, 1.5f, Percent, nullptr, false) \
    X(Mod3Src, "mod3_src", "Source", "Mod 3", Choice, 0, 17, 0, 1.f, None, &choices::modSrc, false) \
    X(Mod3Dst, "mod3_dst", "Target", "Mod 3", Choice, 0, 32, 0, 1.f, None, &choices::modDst, false) \
    X(Mod3Amt, "mod3_amt", "Amount", "Mod 3", BiPower, -1.f, 1.f, 0.f, 1.5f, Percent, nullptr, false) \
    X(Mod4Src, "mod4_src", "Source", "Mod 4", Choice, 0, 17, 0, 1.f, None, &choices::modSrc, false) \
    X(Mod4Dst, "mod4_dst", "Target", "Mod 4", Choice, 0, 32, 0, 1.f, None, &choices::modDst, false) \
    X(Mod4Amt, "mod4_amt", "Amount", "Mod 4", BiPower, -1.f, 1.f, 0.f, 1.5f, Percent, nullptr, false) \
    X(Mod5Src, "mod5_src", "Source", "Mod 5", Choice, 0, 17, 0, 1.f, None, &choices::modSrc, false) \
    X(Mod5Dst, "mod5_dst", "Target", "Mod 5", Choice, 0, 32, 0, 1.f, None, &choices::modDst, false) \
    X(Mod5Amt, "mod5_amt", "Amount", "Mod 5", BiPower, -1.f, 1.f, 0.f, 1.5f, Percent, nullptr, false) \
    X(Mod6Src, "mod6_src", "Source", "Mod 6", Choice, 0, 17, 0, 1.f, None, &choices::modSrc, false) \
    X(Mod6Dst, "mod6_dst", "Target", "Mod 6", Choice, 0, 32, 0, 1.f, None, &choices::modDst, false) \
    X(Mod6Amt, "mod6_amt", "Amount", "Mod 6", BiPower, -1.f, 1.f, 0.f, 1.5f, Percent, nullptr, false) \
    X(Mod7Src, "mod7_src", "Source", "Mod 7", Choice, 0, 17, 0, 1.f, None, &choices::modSrc, false) \
    X(Mod7Dst, "mod7_dst", "Target", "Mod 7", Choice, 0, 32, 0, 1.f, None, &choices::modDst, false) \
    X(Mod7Amt, "mod7_amt", "Amount", "Mod 7", BiPower, -1.f, 1.f, 0.f, 1.5f, Percent, nullptr, false) \
    X(Mod8Src, "mod8_src", "Source", "Mod 8", Choice, 0, 17, 0, 1.f, None, &choices::modSrc, false) \
    X(Mod8Dst, "mod8_dst", "Target", "Mod 8", Choice, 0, 32, 0, 1.f, None, &choices::modDst, false) \
    X(Mod8Amt, "mod8_amt", "Amount", "Mod 8", BiPower, -1.f, 1.f, 0.f, 1.5f, Percent, nullptr, false) \
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
    X(ReverbMix,   "reverb_mix",   "Mix",         "Reverb", Linear, 0.f, 1.f, 0.2f, 1.f, Percent, nullptr, false)

enum class P : int
{
#define PSY_ENUM(e, ...) e,
    PSY_PARAMS (PSY_ENUM)
#undef PSY_ENUM
    Count
};

constexpr int kNumParams = (int) P::Count;
constexpr int kNumModSlots = 8;
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

} // namespace psy
