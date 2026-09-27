#include "Params.h"

#include <algorithm>
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
    const std::vector<std::string> noiseType { "White", "Pink", "Digital" };
    const std::vector<std::string> penvDest { "A + B", "Osc A", "Osc B" };
    const std::vector<std::string> filterType { "Off", "LP 6", "LP 12", "LP 18", "LP 24", "HP 6", "HP 12", "HP 18", "HP 24",
                                                "BP 12", "BP 24", "Notch", "Ladder LP 12", "Ladder LP 24", "Ladder HP",
                                                "Ladder BP", "Diode 303", "Formant", "Comb +", "Comb -" };
    const std::vector<std::string> filterRouting { "Serial", "Parallel" };
    const std::vector<std::string> lfoShape { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth" };
    const std::vector<std::string> syncDiv { "4/1", "2/1", "1/1", "1/2", "1/2D", "1/2T", "1/4", "1/4D", "1/4T",
                                             "1/8", "1/8D", "1/8T", "1/16", "1/16D", "1/16T", "1/32", "1/32T", "1/64" };
    const std::vector<std::string> modSrc { "None", "Amp Env", "Filter Env", "Mod Env", "Pitch Env", "LFO 1",
                                            "LFO 2", "LFO 3", "Velocity", "Note", "Mod Wheel", "Aftertouch",
                                            "Pitch Bend", "Random", "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    const std::vector<std::string> modDst { "None", "Pitch", "Pitch A", "Pitch B", "Shape A", "Shape B", "PW A",
                                            "PW B", "WT Pos A", "WT Pos B", "FM A", "FM B", "Detune A", "Detune B",
                                            "A>B FM", "Level A", "Level B", "Sub Level", "Noise Level", "Ring Level",
                                            "F1 Cutoff", "F1 Reso", "F1 Drive", "F1 Vowel", "F2 Cutoff", "F2 Reso",
                                            "Amp", "Pan", "LFO 1 Rate", "LFO 2 Rate", "LFO 3 Rate", "Pitch Env Amt",
                                            "Filter Env Amt" };
    const std::vector<std::string> distType { "Soft", "Hard", "Fold", "Tube", "Rectify" };
    const std::vector<std::string> chorusMode { "Chorus", "Flanger" };
    const std::vector<std::string> arpMode { "Up", "Down", "Up/Down", "Random", "Order" };
    const std::vector<std::string> arpRhythm { "Straight", "Rolling", "Gallop", "Offbeat", "Skip", "3-3-2", "Broken" };
} // namespace choices

float syncDivBeats (int index) noexcept
{
    static constexpr float beats[] = { 16.f, 8.f, 4.f, 2.f, 3.f, 4.f / 3.f, 1.f, 1.5f, 2.f / 3.f,
                                       0.5f, 0.75f, 1.f / 3.f, 0.25f, 0.375f, 1.f / 6.f, 0.125f, 1.f / 12.f, 0.0625f };
    return beats[std::clamp (index, 0, (int) std::size (beats) - 1)];
}

ArpRhythmMask arpRhythmMask (int index) noexcept
{
    static constexpr ArpRhythmMask masks[] = {
        { "xxxx", 4 },               // Straight
        { "-xxx", 4 },               // Rolling: the kick owns the beat
        { "x-xx", 4 },               // Gallop
        { "--x-", 4 },               // Offbeat
        { "xx-x", 4 },               // Skip
        { "x--x--x-", 8 },           // 3-3-2
        { "x-xx-x-xx-x-xxx-", 16 },  // Broken
    };
    return masks[std::clamp (index, 0, (int) std::size (masks) - 1)];
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
        case Unit::Int:     std::snprintf (buf, sizeof buf, "%d", (int) std::lround (p)); break;
        case Unit::None:    std::snprintf (buf, sizeof buf, "%.2f", p); break;
    }
    return buf;
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

ParamState::ParamState()
{
    const auto& d = paramDefs();
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = d[i].def;
}

} // namespace psy
