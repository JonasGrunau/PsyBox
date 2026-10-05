// Macros: every generated patch gets four, and each one is named after what
// it drives.

#include "Generator.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace psy
{

namespace
{
    ModSrc macroSource (int m) noexcept { return (ModSrc) ((int) ModSrc::Macro1 + m); }

    bool filterOn (const ParamState& s, P type) noexcept { return s.geti (type) != (int) FilterType::Off; }

    bool lfoRouted (const ParamState& s, int lfo) noexcept
    {
        for (int i = 0; i < kNumModSlots; ++i)
            if (s.geti (modSrcParam (i)) == (int) ModSrc::Lfo1 + lfo && s.geti (modDstParam (i)) > 0 && s.get (modAmtParam (i)) != 0.f)
                return true;
        return false;
    }

    // Whether turning a macro on this target would be heard at all.
    bool targetActive (const ParamState& s, ModDst d) noexcept
    {
        using D = ModDst;
        const auto osc = [&] (bool b, OscType t) {
            return s.on (b ? P::OscBOn : P::OscAOn) && s.geti (b ? P::OscBType : P::OscAType) == (int) t;
        };
        switch (d)
        {
            case D::F1Cutoff: case D::F1Reso: case D::F1Drive: return filterOn (s, P::F1Type);
            case D::F1Vowel:      return s.geti (P::F1Type) == (int) FilterType::Formant;
            case D::FilterEnvAmt: return filterOn (s, P::F1Type) && std::abs (s.get (P::F1Env)) > 0.05f;
            case D::F2Cutoff: case D::F2Reso: case D::F2Drive: return filterOn (s, P::F2Type);
            case D::F2EnvAmt:     return filterOn (s, P::F2Type) && std::abs (s.get (P::F2Env)) > 0.05f;
            case D::PitchEnvAmt:  return std::abs (s.get (P::PenvAmt)) > 1.f;
            case D::ShapeA:       return osc (false, OscType::Analog);
            case D::ShapeB:       return osc (true, OscType::Analog);
            case D::PwA:          return osc (false, OscType::Analog) && s.get (P::OscAShape) > 2.5f;
            case D::PwB:          return osc (true, OscType::Analog) && s.get (P::OscBShape) > 2.5f;
            case D::WtPosA:       return osc (false, OscType::Wavetable);
            case D::WtPosB:       return osc (true, OscType::Wavetable);
            case D::FmA:          return osc (false, OscType::FM);
            case D::FmB:          return osc (true, OscType::FM);
            case D::DetuneA:      return osc (false, OscType::Supersaw);
            case D::DetuneB:      return osc (true, OscType::Supersaw);
            case D::XMod:         return s.on (P::OscBOn) && s.on (P::OscAOn);
            case D::LevelA:       return s.on (P::OscAOn);
            case D::LevelB:       return s.on (P::OscBOn);
            case D::SubLevel:     return s.get (P::SubLevel) > 0.f;
            case D::NoiseLevel:   return s.get (P::NoiseLevel) > 0.f;
            case D::RingLevel:    return s.get (P::RingLevel) > 0.f;
            case D::Lfo1Rate:     return lfoRouted (s, 0) && ! s.on (P::Lfo1Sync);
            case D::Lfo2Rate:     return lfoRouted (s, 1) && ! s.on (P::Lfo2Sync);
            case D::Lfo3Rate:     return lfoRouted (s, 2) && ! s.on (P::Lfo3Sync);
            case D::DistDrive: case D::DistMix:   return s.on (P::DistOn);
            case D::CrushMix:                     return s.on (P::CrushOn);
            case D::ShiftHz: case D::ShiftMix:    return s.on (P::FshiftOn);
            case D::PhaserMix:                    return s.on (P::PhaserOn);
            case D::ChorusMix:                    return s.on (P::ChorusOn);
            case D::DelayFb: case D::DelayMix:    return s.on (P::DelayOn);
            case D::ReverbSize: case D::ReverbMix: return s.on (P::ReverbOn);
            case D::WidthAmount:                  return s.on (P::WidthOn);
            case D::PitchAll: case D::PitchA: case D::PitchB: case D::Amp: case D::Pan: return true;
            case D::None: case D::Count:          return false;
        }
        return false;
    }

    // How far a macro at full travel takes each target: enough to hear the
    // move, not so far that the sound leaves its category.
    std::pair<float, float> macroRange (ModDst d) noexcept
    {
        using D = ModDst;
        switch (d)
        {
            case D::F1Cutoff:     return { 0.25f, 0.45f };
            case D::F2Cutoff:     return { 0.2f, 0.4f };
            case D::F1Reso: case D::F2Reso: return { 0.2f, 0.35f };
            case D::F1Drive: case D::F2Drive: return { 0.3f, 0.5f };
            case D::F1Vowel:      return { 0.3f, 0.6f };
            case D::FilterEnvAmt: case D::F2EnvAmt: case D::PitchEnvAmt: return { 0.2f, 0.4f };
            case D::WtPosA: case D::WtPosB: return { 0.3f, 0.6f };
            case D::DetuneA: case D::DetuneB: return { 0.25f, 0.5f };
            case D::PwA: case D::PwB:   return { 0.2f, 0.35f };
            case D::LevelA: case D::LevelB: case D::SubLevel: case D::NoiseLevel: case D::RingLevel: return { 0.3f, 0.5f };
            case D::DistDrive:    return { 0.25f, 0.45f };
            case D::CrushMix:     return { 0.3f, 0.6f };
            case D::ShiftHz:      return { 0.08f, 0.16f };
            case D::DelayFb: case D::DelayMix: return { 0.2f, 0.35f };
            case D::ReverbMix:    return { 0.25f, 0.4f };
            default:              return { 0.2f, 0.4f };
        }
    }

    // ---- labels -----------------------------------------------------------

    enum class Kind { None, Filter, Pitch, Tone, Mix, Motion, Space, Grit, Colour, Amp };

    struct Label { const char* name; Kind kind; };

    Label labelOf (ModDst d) noexcept
    {
        using D = ModDst;
        switch (d)
        {
            case D::PitchAll:     return { "Pitch", Kind::Pitch };
            case D::PitchA:       return { "Pitch A", Kind::Pitch };
            case D::PitchB:       return { "Pitch B", Kind::Pitch };
            case D::PitchEnvAmt:  return { "Pitch Env", Kind::Pitch };
            case D::ShapeA:       return { "Shape", Kind::Tone };
            case D::ShapeB:       return { "Shape B", Kind::Tone };
            case D::PwA:          return { "Pulse Width", Kind::Tone };
            case D::PwB:          return { "PW B", Kind::Tone };
            case D::WtPosA:       return { "Morph", Kind::Tone };
            case D::WtPosB:       return { "Morph B", Kind::Tone };
            case D::FmA:          return { "FM", Kind::Tone };
            case D::FmB:          return { "FM B", Kind::Tone };
            case D::DetuneA:      return { "Detune", Kind::Tone };
            case D::DetuneB:      return { "Detune B", Kind::Tone };
            case D::XMod:         return { "A>B FM", Kind::Tone };
            case D::LevelA:       return { "Osc A", Kind::Mix };
            case D::LevelB:       return { "Osc B", Kind::Mix };
            case D::SubLevel:     return { "Sub", Kind::Mix };
            case D::NoiseLevel:   return { "Noise", Kind::Mix };
            case D::RingLevel:    return { "Ring", Kind::Mix };
            case D::F1Cutoff:     return { "Cutoff", Kind::Filter };
            case D::F2Cutoff:     return { "Cutoff 2", Kind::Filter };
            case D::F1Reso:       return { "Resonance", Kind::Filter };
            case D::F2Reso:       return { "Reso 2", Kind::Filter };
            case D::F1Drive:      return { "Drive", Kind::Filter };
            case D::F2Drive:      return { "Drive 2", Kind::Filter };
            case D::F1Vowel:      return { "Vowel", Kind::Filter };
            case D::FilterEnvAmt: return { "Env Amount", Kind::Filter };
            case D::F2EnvAmt:     return { "Env 2", Kind::Filter };
            case D::Amp:          return { "Volume", Kind::Amp };
            case D::Pan:          return { "Pan", Kind::Amp };
            case D::Lfo1Rate:     return { "LFO 1 Rate", Kind::Motion };
            case D::Lfo2Rate:     return { "LFO 2 Rate", Kind::Motion };
            case D::Lfo3Rate:     return { "LFO 3 Rate", Kind::Motion };
            case D::DistDrive:    return { "Distortion", Kind::Grit };
            case D::DistMix:      return { "Dist Mix", Kind::Grit };
            case D::CrushMix:     return { "Crush", Kind::Grit };
            case D::ShiftHz:      return { "Shift", Kind::Colour };
            case D::ShiftMix:     return { "Shift Mix", Kind::Colour };
            case D::PhaserMix:    return { "Phaser", Kind::Colour };
            case D::ChorusMix:    return { "Chorus", Kind::Colour };
            case D::DelayFb:      return { "Feedback", Kind::Space };
            case D::DelayMix:     return { "Delay", Kind::Space };
            case D::ReverbSize:   return { "Room", Kind::Space };
            case D::ReverbMix:    return { "Reverb", Kind::Space };
            case D::WidthAmount:  return { "Width", Kind::Space };
            case D::None: case D::Count: break;
        }
        return { "", Kind::None };
    }

    const char* kindName (Kind k) noexcept
    {
        switch (k)
        {
            case Kind::Filter: return "Filter";
            case Kind::Pitch:  return "Pitch";
            case Kind::Tone:   return "Tone";
            case Kind::Mix:    return "Mix";
            case Kind::Motion: return "Motion";
            case Kind::Space:  return "Space";
            case Kind::Grit:   return "Grit";
            case Kind::Colour: return "Colour";
            case Kind::Amp:    return "Level";
            case Kind::None:   break;
        }
        return "";
    }

    constexpr size_t kMaxLabel = 14;
} // namespace

void RecipeBuilder::macros (std::initializer_list<ModDst> targets)
{
    const auto drives = [this] (int m, ModDst d) {
        for (int i = 0; i < kNumModSlots; ++i)
            if (s.geti (modSrcParam (i)) == (int) macroSource (m) && (d == ModDst::None || s.geti (modDstParam (i)) == (int) d)
                && s.geti (modDstParam (i)) > 0)
                return true;
        return false;
    };
    const auto driven = [&] (ModDst d) {
        for (int m = 0; m < 4; ++m)
            if (drives (m, d)) return true;
        return false;
    };

    // The recipe's own preferences first, then targets most patches have.
    static constexpr ModDst kFallback[] = { ModDst::F1Cutoff, ModDst::F1Reso, ModDst::FilterEnvAmt, ModDst::PitchEnvAmt,
                                            ModDst::WtPosA, ModDst::FmA, ModDst::DetuneA, ModDst::F2Cutoff,
                                            ModDst::DistDrive, ModDst::DelayMix, ModDst::ReverbMix, ModDst::ChorusMix,
                                            ModDst::PhaserMix, ModDst::NoiseLevel, ModDst::LevelB };
    std::vector<ModDst> order (targets);
    order.insert (order.end(), std::begin (kFallback), std::end (kFallback));

    auto next = order.begin();
    for (int m = 0; m < 4; ++m)
    {
        if (drives (m, ModDst::None)) continue;
        while (next != order.end() && (! targetActive (s, *next) || driven (*next)))
            ++next;
        if (next != order.end())
        {
            const auto [lo, hi] = macroRange (*next);
            route (macroSource (m), *next, lo, hi);
            ++next;
            continue;
        }
        // Nothing left to move: the macro brings in reverb, then delay, then
        // chorus. The effect is switched on at 0 % mix, so the patch sounds
        // the same until the macro is turned up.
        if (! driven (ModDst::ReverbMix))
        {
            if (! s.on (P::ReverbOn)) { on (P::ReverbOn, true); set (P::ReverbMix, 0.f); }
            route (macroSource (m), ModDst::ReverbMix, 0.3f, 0.45f);
        }
        else if (! driven (ModDst::DelayMix))
        {
            if (! s.on (P::DelayOn))
            {
                on (P::DelayOn, true);
                set (P::DelaySync, 1.f);
                set (P::DelayDiv, 10.f);   // 1/8 dotted
                set (P::DelayFb, 0.35f);
                set (P::DelayMix, 0.f);
            }
            route (macroSource (m), ModDst::DelayMix, 0.2f, 0.35f);
        }
        else if (! driven (ModDst::ChorusMix))
        {
            if (! s.on (P::ChorusOn)) { on (P::ChorusOn, true); set (P::ChorusMix, 0.f); }
            route (macroSource (m), ModDst::ChorusMix, 0.3f, 0.5f);
        }
    }
}

std::string macroLabel (const ParamState& p, int macro)
{
    std::vector<ModDst> dsts;
    for (int i = 0; i < kNumModSlots; ++i)
    {
        const int dst = p.geti (modDstParam (i));
        if (p.geti (modSrcParam (i)) != (int) macroSource (macro) || dst <= 0 || dst >= (int) ModDst::Count
            || p.get (modAmtParam (i)) == 0.f)
            continue;
        if (std::find (dsts.begin(), dsts.end(), (ModDst) dst) == dsts.end())
            dsts.push_back ((ModDst) dst);
    }
    if (dsts.empty()) return {};

    // both cutoffs move together: still just "Cutoff"
    const bool cutoffsOnly = std::all_of (dsts.begin(), dsts.end(), [] (ModDst d) { return d == ModDst::F1Cutoff || d == ModDst::F2Cutoff; });
    if (dsts.size() == 1 || cutoffsOnly)
        return cutoffsOnly ? "Cutoff" : labelOf (dsts.front()).name;

    const Kind k0 = labelOf (dsts[0]).kind;
    if (std::all_of (dsts.begin(), dsts.end(), [k0] (ModDst d) { return labelOf (d).kind == k0; }))
        return kindName (k0);

    if (dsts.size() == 2)
    {
        const std::string a = labelOf (dsts[0]).name, b = labelOf (dsts[1]).name;
        if (a.size() + b.size() + 3 <= kMaxLabel) return a + " + " + b;
        const std::string ka = kindName (k0), kb = kindName (labelOf (dsts[1]).kind);
        if (ka.size() + kb.size() + 3 <= kMaxLabel) return ka + " + " + kb;
    }
    return std::string (labelOf (dsts[0]).name) + " +";
}

} // namespace psy
