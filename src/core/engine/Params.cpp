#include "Params.h"

#include "../dsp/NoiseBank.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace psy
{

namespace choices
{
    const std::vector<std::string> voiceMode { "Poly", "Mono", "Legato" };
    const std::vector<std::string> glideMode { "Always", "Legato" };
    const std::vector<std::string> quality { "Eco 1x", "Standard 2x", "High 4x" };
    const std::vector<std::string> oscType { "Analog", "Supersaw", "Wavetable", "FM" };
    const std::vector<std::string> wavetable { "Harmonic Sweep", "PWM Stack", "Vox", "Metallic",
                                               "Digital Grit", "Sync Sweep", "Bell Spectra", "Acid Morph" };
    const std::vector<std::string> subOct { "-1 Oct", "-2 Oct" };
    const std::vector<std::string> subShape { "Sine", "Triangle", "Square" };
    const std::vector<std::string> noiseType { "White", "Pink", "Digital", "Hiss", "Tuned", "Sample" };
    // the bank's samples, then a file of your own (kNoiseFile)
    const std::vector<std::string> noiseSample = [] {
        std::vector<std::string> names;
        for (int i = 0; i < dsp::NoiseBank::kCount; ++i) names.emplace_back (dsp::NoiseBank::name (i));
        names.emplace_back ("File");
        return names;
    }();
    const std::vector<std::string> penvDest { "A + B", "Osc A", "Osc B" };
    const std::vector<std::string> filterType { "Off", "LP 6", "LP 12", "LP 18", "LP 24", "HP 6", "HP 12", "HP 18", "HP 24",
                                                "BP 12", "BP 24", "Notch", "Ladder LP 12", "Ladder LP 24", "Ladder HP",
                                                "Ladder BP", "Diode 303", "Formant", "Comb +", "Comb -" };
    const std::vector<std::string> filterRouting { "Serial", "Parallel" };
    const std::vector<std::string> lfoShape { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth", "Drawn" };
    const std::vector<std::string> syncDiv { "4/1", "2/1", "1/1", "1/2", "1/2D", "1/2T", "1/4", "1/4D", "1/4T",
                                             "1/8", "1/8D", "1/8T", "1/16", "1/16D", "1/16T", "1/32", "1/32T", "1/64" };
    const std::vector<std::string> modSrc { "None", "Amp Env", "Env 1", "Env 2", "Env 3", "LFO 1",
                                            "LFO 2", "LFO 3", "Velocity", "Note", "Mod Wheel", "Aftertouch",
                                            "Pitch Bend", "Random", "Macro 1", "Macro 2", "Macro 3", "Macro 4",
                                            "Sweep" };
    const std::vector<std::string> modDst { "None", "Pitch", "Pitch A", "Pitch B", "Shape A", "Shape B", "PW A",
                                            "PW B", "WT Pos A", "WT Pos B", "FM A", "FM B", "Detune A", "Detune B",
                                            "A>B FM", "Level A", "Level B", "Sub Level", "Noise Level", "Ring Level",
                                            "F1 Cutoff", "F1 Reso", "F1 Drive", "F1 Vowel", "F2 Cutoff", "F2 Reso",
                                            "Amp", "Pan", "LFO 1 Rate", "LFO 2 Rate", "LFO 3 Rate", "Env 3 Amt",
                                            "F1 Env Amt", "F2 Drive", "F2 Env Amt",
                                            "Dist Drive", "Dist Mix", "Crush Mix", "Shift Freq", "Shift Mix", "Phaser Mix",
                                            "Chorus Mix", "Delay Feedback", "Delay Mix", "Reverb Size", "Reverb Mix", "Width",
                                            "Phoscyon Cutoff", "Phoscyon Reso", "Phoscyon Envmod", "Phoscyon Decay",
                                            "Phoscyon Accent", "Phoscyon Tuning", "Phoscyon Drive", "Phoscyon Color",
                                            "Phoscyon Chorus Mix", "Phoscyon Delay Fb", "Phoscyon Delay Mix", "Phoscyon Reverb Mix",
                                            "EQ Low Freq", "EQ Low Gain", "EQ High Freq", "EQ High Gain", "Dim Mix",
                                            "Noise Pitch" };
    const std::vector<std::string> distType { "Soft", "Hard", "Fold", "Tube", "Rectify" };
    const std::vector<std::string> chorusMode { "Chorus", "Flanger" };
    const std::vector<std::string> compMode { "Normal", "Multi" };
    const std::vector<std::string> arpMode { "Up", "Down", "Up/Down", "Random", "Order" };
    const std::vector<std::string> engineMode { "Synth", "Kick", "Phoscyon" };
    const std::vector<std::string> kickClick { "Noise", "Tick", "Blip" };
    const std::vector<std::string> kickDrive { "Soft", "Tube", "Hard" };
    const std::vector<std::string> energyMode { "Bars", "Time" };
    const std::vector<std::string> arpKey { "Off", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const std::vector<std::string> arpScale { "Phrygian", "Minor", "Harmonic Min", "Phrygian Dom", "Dorian", "Major" };
    const std::vector<std::string> uniMode { "Linear", "Super", "Exp", "Inv", "Random" };
    const std::vector<std::string> uniStack { "Off", "12", "12+7", "24", "Center -12", "Center -24" };
    const std::vector<std::string> energyBars { "1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars" };
    const std::vector<std::string> phosWave { "Saw", "Square" };
    const std::vector<std::string> phosDistModel {
        "Diode + Bias", "Diode + Low-Boost", "Diode + Low-Cut Extreme", "Diode + Low-Cut Medium", "Diode + Mid-Punch",
        "Diode + Tilt", "Diode Clean", "Diode Whistle", "Fuzzy Overdrive", "Overdrive + Bias", "Overdrive + Low-Cut Extreme",
        "Overdrive + Low-Cut Medium", "Overdrive + Tilt Strong", "Overdrive + Tilt Subtle", "Overdrive Clean",
        "Overdrive Colorized", "Overdrive Dirty", "RAT + Low-Cut", "RAT Clean" };
    const std::vector<std::string> phosDistOrder { "Pre", "Post" };
    const std::vector<std::string> phosDelayFilter { "Off", "LP", "BP", "HP" };
    const std::vector<std::string> phosDelayScale { "Full", "Dotted", "Triplet" };
    const std::vector<std::string> eqLowType { "Shelf", "Bell", "Low Cut 12", "Low Cut 24" };
    const std::vector<std::string> eqHighType { "Shelf", "Bell", "High Cut 12", "High Cut 24" };
} // namespace choices

float syncDivBeats (int index) noexcept
{
    static constexpr float beats[] = { 16.f, 8.f, 4.f, 2.f, 3.f, 4.f / 3.f, 1.f, 1.5f, 2.f / 3.f,
                                       0.5f, 0.75f, 1.f / 3.f, 0.25f, 0.375f, 1.f / 6.f, 0.125f, 1.f / 12.f, 0.0625f };
    return beats[std::clamp (index, 0, (int) std::size (beats) - 1)];
}

FxChain fxChain (int code) noexcept
{
    // the order's index as a factorial-base number: each digit picks one
    // of the effects still left, in default order
    FxChain out {};
    std::array<int, kNumFx> left {};
    for (int i = 0; i < kNumFx; ++i) left[(size_t) i] = i;
    int n = kNumFx;
    int radix = 1;
    for (int i = 2; i < kNumFx; ++i) radix *= i;   // (kNumFx - 1)!
    code = std::clamp (code, 0, radix * kNumFx - 1);
    for (int i = 0; i < kNumFx; ++i)
    {
        const int digit = code / radix;
        code %= radix;
        out[(size_t) i] = left[(size_t) digit];
        for (int k = digit; k < n - 1; ++k) left[(size_t) k] = left[(size_t) k + 1];
        --n;
        if (n > 0) radix /= n;
    }
    return out;
}

int fxOrderCode (const FxChain& chain) noexcept
{
    std::array<bool, kNumFx> seen {};
    for (int fx : chain)
    {
        if (fx < 0 || fx >= kNumFx || seen[(size_t) fx]) return -1;
        seen[(size_t) fx] = true;
    }
    int code = 0;
    for (int i = 0; i < kNumFx; ++i)
    {
        int smaller = 0;   // effects after this one that come earlier by default
        for (int k = i + 1; k < kNumFx; ++k)
            smaller += chain[(size_t) k] < chain[(size_t) i] ? 1 : 0;
        code = code * (kNumFx - i) + smaller;
    }
    return code;
}

namespace
{
    // The nine in their FxOrder plus two effects at their own slots; the
    // second moves aside when both ask for the same one.
    std::array<int, kNumFx + 2> twoInSlots (int orderCode, int slotA, int idA, int slotB, int idB) noexcept
    {
        constexpr int n = kNumFx + 2;
        const auto base = fxChain (orderCode);
        slotA = std::clamp (slotA, 0, n - 1);
        slotB = std::clamp (slotB, 0, n - 1);
        if (slotB == slotA)
            slotB = slotA == n - 1 ? slotA - 1 : slotA + 1;
        std::array<int, n> out {};
        size_t b = 0;
        for (int i = 0; i < n; ++i)
            out[(size_t) i] = i == slotA ? idA : i == slotB ? idB : base[b++];
        return out;
    }

    void slotsOfTwo (const std::array<int, kNumFx + 2>& chain, int idA, int idB, int& orderCode, int& slotA, int& slotB) noexcept
    {
        FxChain base {};
        size_t b = 0;
        for (int i = 0; i < kNumFx + 2; ++i)
        {
            const int fx = chain[(size_t) i];
            if (fx == idA) slotA = i;
            else if (fx == idB) slotB = i;
            else if (b < base.size()) base[b++] = fx;
        }
        orderCode = std::max (0, fxOrderCode (base));
    }
} // namespace

PhosFxChain phosFxChain (int orderCode, int eqSlot, int limSlot) noexcept
{
    return twoInSlots (orderCode, eqSlot, kPhosEq, limSlot, kPhosLimiter);
}

void phosFxSlots (const PhosFxChain& chain, int& orderCode, int& eqSlot, int& limSlot) noexcept
{
    slotsOfTwo (chain, kPhosEq, kPhosLimiter, orderCode, eqSlot, limSlot);
}

SynthFxChain synthFxChain (int orderCode, int eqSlot, int dimSlot) noexcept
{
    return twoInSlots (orderCode, eqSlot, kEq, dimSlot, kDimension);
}

void synthFxSlots (const SynthFxChain& chain, int& orderCode, int& eqSlot, int& dimSlot) noexcept
{
    slotsOfTwo (chain, kEq, kDimension, orderCode, eqSlot, dimSlot);
}

const std::array<int, 7>& arpScaleSteps (int scale) noexcept
{
    static constexpr std::array<int, 7> scales[kNumArpScales] = {
        { 0, 1, 3, 5, 7, 8, 10 },   // Phrygian
        { 0, 2, 3, 5, 7, 8, 10 },   // natural minor
        { 0, 2, 3, 5, 7, 8, 11 },   // harmonic minor
        { 0, 1, 4, 5, 7, 8, 10 },   // Phrygian dominant
        { 0, 2, 3, 5, 7, 9, 10 },   // Dorian
        { 0, 2, 4, 5, 7, 9, 11 },   // major
    };
    return scales[std::clamp (scale, 0, kNumArpScales - 1)];
}

int quantizeToScale (int note, int key, int scale) noexcept
{
    const auto& steps = arpScaleSteps (scale);
    const auto inScale = [&] (int n) {
        const int pc = ((n - key) % 12 + 12) % 12;
        return std::find (steps.begin(), steps.end(), pc) != steps.end();
    };
    for (int d = 0; d < 12; ++d)
    {
        if (inScale (note - d)) return note - d;
        if (inScale (note + d)) return note + d;
    }
    return note;
}

float energyBarsValue (int index) noexcept
{
    static constexpr float bars[] = { 0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f, 32.f };
    return bars[std::clamp (index, 0, (int) std::size (bars) - 1)];
}

namespace
{
    struct ModTarget { ModDst dst; P param; float depth; };

    // depth: share of the knob's travel that an amount of 1.0 covers. Voice
    // targets keep their own units (cutoff 8 octaves, pitch 24 semitones,
    // LFO rate 4 octaves...), effect targets work in the knob's own range.
    constexpr ModTarget kTargets[] = {
        { ModDst::PitchA,       P::OscACoarse,  24.f / 72.f },
        { ModDst::PitchB,       P::OscBCoarse,  24.f / 72.f },
        { ModDst::ShapeA,       P::OscAShape,   1.f },
        { ModDst::ShapeB,       P::OscBShape,   1.f },
        { ModDst::PwA,          P::OscAPw,      0.48f / 0.96f },
        { ModDst::PwB,          P::OscBPw,      0.48f / 0.96f },
        { ModDst::WtPosA,       P::OscAWtPos,   1.f },
        { ModDst::WtPosB,       P::OscBWtPos,   1.f },
        { ModDst::FmA,          P::OscAFm,      1.f },
        { ModDst::FmB,          P::OscBFm,      1.f },
        { ModDst::DetuneA,      P::OscADetune,  1.f },
        { ModDst::DetuneB,      P::OscBDetune,  1.f },
        { ModDst::XMod,         P::OscBXMod,    1.f },
        { ModDst::LevelA,       P::OscALevel,   1.f },
        { ModDst::LevelB,       P::OscBLevel,   1.f },
        { ModDst::SubLevel,     P::SubLevel,    1.f },
        { ModDst::NoiseLevel,   P::NoiseLevel,  1.f },
        { ModDst::RingLevel,    P::RingLevel,   1.f },
        { ModDst::F1Cutoff,     P::F1Cutoff,    0.8f },    // 8 of the knob's 10 octaves
        { ModDst::F1Reso,       P::F1Reso,      1.f },
        { ModDst::F1Drive,      P::F1Drive,     1.f },
        { ModDst::F1Vowel,      P::F1Vowel,     1.f },
        { ModDst::F2Cutoff,     P::F2Cutoff,    0.8f },
        { ModDst::F2Reso,       P::F2Reso,      1.f },
        { ModDst::Lfo1Rate,     P::Lfo1Rate,    4.f / 11.f },
        { ModDst::Lfo2Rate,     P::Lfo2Rate,    4.f / 11.f },
        { ModDst::Lfo3Rate,     P::Lfo3Rate,    4.f / 11.f },
        { ModDst::PitchEnvAmt,  P::PenvAmt,     0.5f },
        { ModDst::FilterEnvAmt, P::F1Env,       0.5f },
        { ModDst::F2Drive,      P::F2Drive,     1.f },
        { ModDst::F2EnvAmt,     P::F2Env,       0.5f },
        { ModDst::DistDrive,    P::DistDrive,   1.f },
        { ModDst::DistMix,      P::DistMix,     1.f },
        { ModDst::CrushMix,     P::CrushMix,    1.f },
        { ModDst::ShiftHz,      P::FshiftHz,    1.f },
        { ModDst::ShiftMix,     P::FshiftMix,   1.f },
        { ModDst::PhaserMix,    P::PhaserMix,   1.f },
        { ModDst::ChorusMix,    P::ChorusMix,   1.f },
        { ModDst::DelayFb,      P::DelayFb,     1.f },
        { ModDst::DelayMix,     P::DelayMix,    1.f },
        { ModDst::ReverbSize,   P::ReverbSize,  1.f },
        { ModDst::ReverbMix,    P::ReverbMix,   1.f },
        { ModDst::WidthAmount,  P::WidthAmount, 1.f },
        { ModDst::PhosCutoff,   P::PhosCutoff,  1.f },
        { ModDst::PhosReso,     P::PhosReso,    1.f },
        { ModDst::PhosEnvMod,   P::PhosEnvMod,  1.f },
        { ModDst::PhosDecay,    P::PhosDecay,   1.f },
        { ModDst::PhosAccent,   P::PhosAccent,  1.f },
        { ModDst::PhosTune,     P::PhosTune,    1.f },
        { ModDst::PhosDistDrive,P::PhosDistDrive, 1.f },
        { ModDst::PhosDistColor,P::PhosDistColor, 1.f },
        { ModDst::PhosChorusMix,P::PhosChoMix,  1.f },
        { ModDst::PhosDelayFb,  P::PhosDlyFb,   1.f },
        { ModDst::PhosDelayMix, P::PhosDlyMix,  1.f },
        { ModDst::PhosReverbMix,P::PhosRevMix,  1.f },
        { ModDst::EqLowFreq,    P::EqLowFreq,   1.f },
        { ModDst::EqLowGain,    P::EqLowGain,   1.f },
        { ModDst::EqHighFreq,   P::EqHighFreq,  1.f },
        { ModDst::EqHighGain,   P::EqHighGain,  1.f },
        { ModDst::DimMix,       P::DimMix,      1.f },
        { ModDst::NoisePitch,   P::NoisePitch,  0.5f },   // 1.0 = 48 semitones, half the knob
    };
} // namespace

static_assert (kNoiseFile == dsp::NoiseBank::kCount, "the File entry follows the bank's samples");

P modTargetParam (ModDst d) noexcept
{
    for (const auto& t : kTargets)
        if (t.dst == d) return t.param;
    return P::Count;
}

ModDst modTargetFor (P p) noexcept
{
    for (const auto& t : kTargets)
        if (t.param == p) return t.dst;
    return ModDst::None;
}

float modDepthNorm (ModDst d) noexcept
{
    for (const auto& t : kTargets)
        if (t.dst == d) return t.depth;
    return 1.f;
}

float ParamDef::toPlain (float n) const noexcept
{
    n = std::clamp (n, 0.f, 1.f);
    switch (mapping)
    {
        case Mapping::Linear:  return min + (max - min) * n;
        case Mapping::Power:   return min + (max - min) * std::pow (n, skew);
        case Mapping::Exp:     return min * std::pow (max / min, n);
        case Mapping::BiPower:
        {
            const float b = 2.f * n - 1.f;
            return max * std::copysign (std::pow (std::abs (b), skew), b);
        }
        case Mapping::Choice:
        case Mapping::Toggle:  return std::round (min + (max - min) * n);
        case Mapping::Inverse: return min / std::max (n, min / max);
    }
    return min;
}

float ParamDef::toNorm (float p) const noexcept
{
    p = std::clamp (p, min, max);
    switch (mapping)
    {
        case Mapping::Linear:
        case Mapping::Choice:
        case Mapping::Toggle:  return max > min ? (p - min) / (max - min) : 0.f;
        case Mapping::Power:   return std::pow ((p - min) / (max - min), 1.f / skew);
        case Mapping::Exp:     return std::log (p / min) / std::log (max / min);
        case Mapping::BiPower:
        {
            const float b = p / max;
            return 0.5f + 0.5f * std::copysign (std::pow (std::abs (b), 1.f / skew), b);
        }
        case Mapping::Inverse: return min / p;
    }
    return 0.f;
}

float ParamDef::snap (float p) const noexcept
{
    p = std::clamp (p, min, max);
    return isDiscrete() ? std::round (p) : p;
}

std::string ParamDef::format (float p) const
{
    char buf[48];
    if (mapping == Mapping::Choice)
    {
        const int i = std::clamp ((int) std::lround (p), 0, numChoices() - 1);
        return (*choices)[(size_t) i];
    }
    if (mapping == Mapping::Toggle)
        return p > 0.5f ? "On" : "Off";

    switch (unit)
    {
        case Unit::Percent:
            std::snprintf (buf, sizeof buf, "%d%%", (int) std::lround (p * 100.f)); break;
        case Unit::Hz:
            if (std::abs (p) >= 1000.f) std::snprintf (buf, sizeof buf, "%.2f kHz", p / 1000.f);
            else if (std::abs (p) >= 100.f) std::snprintf (buf, sizeof buf, "%.0f Hz", p);
            else std::snprintf (buf, sizeof buf, "%.2f Hz", p);
            break;
        case Unit::Ms:
            if (p >= 1000.f) std::snprintf (buf, sizeof buf, "%.2f s", p / 1000.f);
            else if (p >= 100.f) std::snprintf (buf, sizeof buf, "%.0f ms", p);
            else std::snprintf (buf, sizeof buf, "%.1f ms", p);
            break;
        case Unit::Sec:     std::snprintf (buf, sizeof buf, "%.2f s", p); break;
        case Unit::Semi:    std::snprintf (buf, sizeof buf, "%+.1f st", p); break;
        case Unit::Cents:   std::snprintf (buf, sizeof buf, "%+.0f ct", p); break;
        case Unit::Db:      std::snprintf (buf, sizeof buf, "%.1f dB", p); break;
        case Unit::Octaves: std::snprintf (buf, sizeof buf, "%+.2f oct", p); break;
        case Unit::Ratio:   std::snprintf (buf, sizeof buf, "%.2fx", p); break;
        case Unit::RatioTo1:
            if (p >= 10.f) std::snprintf (buf, sizeof buf, "%.0f:1", p);
            else std::snprintf (buf, sizeof buf, "%.1f:1", p);
            break;
        case Unit::Int:     std::snprintf (buf, sizeof buf, "%d", (int) std::lround (p)); break;
        case Unit::None:    std::snprintf (buf, sizeof buf, "%.2f", p); break;
        case Unit::Mv:      std::snprintf (buf, sizeof buf, "%+.0f mV", p); break;
        case Unit::Volt:    std::snprintf (buf, sizeof buf, "%.2f V", p); break;
        case Unit::NanoFarad:
            if (p >= 1.0e6f) std::snprintf (buf, sizeof buf, "%.2f mF", p * 1.0e-6f);
            else if (p >= 1000.f) std::snprintf (buf, sizeof buf, "%.2f uF", p * 0.001f);
            else std::snprintf (buf, sizeof buf, "%.1f nF", p);
            break;
        case Unit::OctPerVolt: std::snprintf (buf, sizeof buf, "%.3f oct/V", 1.f + p); break;
        case Unit::Meter:   std::snprintf (buf, sizeof buf, "%.1f m", p); break;
    }
    return buf;
}

namespace
{
    std::string lower (std::string_view s)
    {
        std::string out;
        for (const char c : s)
            if (c != ' ' && c != '\t')
                out += (char) std::tolower ((unsigned char) c);
        return out;
    }

    // A decimal number at the start of s (a comma counts as the decimal point;
    // no locale), and where it ends. strtod would follow the host's locale.
    bool leadingNumber (std::string_view s, double& value, size_t& end)
    {
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        const bool negative = i < s.size() && s[i] == '-';
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        double v = 0.0, scale = 1.0;
        bool digits = false, point = false;
        for (; i < s.size(); ++i)
        {
            const char c = s[i];
            if (c >= '0' && c <= '9')
            {
                digits = true;
                if (point) { scale *= 0.1; v += (c - '0') * scale; }
                else v = v * 10.0 + (c - '0');
            }
            else if ((c == '.' || c == ',') && ! point) point = true;
            else break;
        }
        if (! digits) return false;
        if (i + 1 < s.size() && (s[i] == 'e' || s[i] == 'E') && (std::isdigit ((unsigned char) s[i + 1]) || s[i + 1] == '-' || s[i + 1] == '+'))
        {
            size_t j = i + 1;
            const bool negExp = s[j] == '-';
            if (s[j] == '-' || s[j] == '+') ++j;
            int e = 0;
            bool expDigits = false;
            for (; j < s.size() && std::isdigit ((unsigned char) s[j]); ++j) { e = std::min (e * 10 + (s[j] - '0'), 400); expDigits = true; }
            if (expDigits) { v *= std::pow (10.0, negExp ? -e : e); i = j; }
        }
        value = negative ? -v : v;
        end = i;
        return true;
    }

    bool startsWith (const std::string& s, std::string_view prefix) { return s.compare (0, prefix.size(), prefix) == 0; }
} // namespace

float ParamDef::parse (std::string_view text) const
{
    const auto t = lower (text);
    if (mapping == Mapping::Choice)
    {
        for (int i = 0; i < numChoices(); ++i)
            if (lower ((*choices)[(size_t) i]) == t)
                return (float) i;
    }
    if (mapping == Mapping::Toggle)
    {
        if (t == "on" || t == "true" || t == "yes") return 1.f;
        if (t == "off" || t == "false" || t == "no") return 0.f;
    }

    double v = 0.0;
    size_t end = 0;
    if (! leadingNumber (text, v, end))
        return snap (def);
    const auto suffix = lower (text.substr (end));
    switch (unit)
    {
        case Unit::Percent:    v *= 0.01; break;
        case Unit::Hz:         if (startsWith (suffix, "k")) v *= 1000.0; break;
        case Unit::Ms:         if (startsWith (suffix, "s")) v *= 1000.0; break;
        case Unit::Sec:        if (startsWith (suffix, "ms")) v *= 0.001; break;
        case Unit::NanoFarad:
            if (startsWith (suffix, "mf")) v *= 1.0e6;
            else if (startsWith (suffix, "uf") || startsWith (suffix, "\xc2\xb5" "f")) v *= 1000.0;
            else if (startsWith (suffix, "pf")) v *= 0.001;
            break;
        case Unit::OctPerVolt: v -= 1.0; break;
        case Unit::None: case Unit::Semi: case Unit::Cents: case Unit::Db: case Unit::Octaves: case Unit::Ratio:
        case Unit::RatioTo1: case Unit::Int: case Unit::Mv: case Unit::Volt: case Unit::Meter:
            break;
    }
    return snap ((float) v);
}

const std::array<ParamDef, kNumParams>& paramDefs() noexcept
{
#define PSY_DEF(e, id, name, group, mapping, mn, mx, df, skew, unit, ch, mod) \
    ParamDef { id, name, group, Mapping::mapping, (float) (mn), (float) (mx), (float) (df), skew, Unit::unit, ch, mod },
    static const std::array<ParamDef, kNumParams> defs { { PSY_PARAMS (PSY_DEF) } };
#undef PSY_DEF
    return defs;
}

int findParam (std::string_view id) noexcept
{
    const auto& d = paramDefs();
    for (int i = 0; i < kNumParams; ++i)
        if (id == d[(size_t) i].id)
            return i;
    return -1;
}

ArpPattern ArpPattern::from (const ParamState& s) noexcept
{
    ArpPattern p;
    p.length = std::clamp (s.geti (P::ArpLength), 1, kArpSteps);
    for (int i = 0; i < kArpSteps; ++i)
    {
        p.steps[(size_t) i] = (uint8_t) ((s.on (arpStepParam (i, ArpLane::On)) ? On : 0)
                                         | (s.on (arpStepParam (i, ArpLane::Accent)) ? Accent : 0)
                                         | (s.on (arpStepParam (i, ArpLane::Slide)) ? Slide : 0));
        p.notes[(size_t) i] = (int8_t) std::clamp (s.geti (arpStepParam (i, ArpLane::Note)), -kArpNoteRange, kArpNoteRange);
    }
    return p;
}

void ArpPattern::writeTo (ParamState& s) const noexcept
{
    s.set (P::ArpLength, (float) length);
    for (int i = 0; i < kArpSteps; ++i)
    {
        s.set (arpStepParam (i, ArpLane::On), has (i, On) ? 1.f : 0.f);
        s.set (arpStepParam (i, ArpLane::Accent), has (i, Accent) ? 1.f : 0.f);
        s.set (arpStepParam (i, ArpLane::Slide), has (i, Slide) ? 1.f : 0.f);
        s.set (arpStepParam (i, ArpLane::Note), (float) notes[(size_t) i]);
    }
}

ArpPattern ArpPattern::parse (std::string_view text) noexcept
{
    ArpPattern p;
    if (text.empty()) return p;
    for (int i = 0; i < kArpSteps; ++i)
    {
        const char c = text[(size_t) i % text.size()];
        p.steps[(size_t) i] = (uint8_t) (c == 'x' ? On : c == 'X' ? On | Accent : c == 's' ? On | Slide
                                         : c == 'S' ? On | Accent | Slide : 0);
    }
    return p;
}

ParamState::ParamState()
{
    const auto& d = paramDefs();
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = d[i].def;
}

} // namespace psy
