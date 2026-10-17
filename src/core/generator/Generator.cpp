#include "Generator.h"

namespace psy
{

// ---------------------------------------------------------------------------
// RecipeBuilder
// ---------------------------------------------------------------------------

void RecipeBuilder::set (P p, float plain)
{
    auto& r = ranges.e[(size_t) p];
    r.touched = true;
    const auto& d = def (p);
    if (d.mapping == Mapping::Choice || d.mapping == Mapping::Toggle)
    {
        r.numOptions = 1;
        r.options[0] = (int8_t) std::lround (plain);
    }
    else
    {
        r.lo = r.hi = d.toNorm (plain);
    }
    if (! locked (p))
        s.set (p, plain);
}

float RecipeBuilder::uni (P p, float lo, float hi)
{
    const auto& d = def (p);
    const float nlo = d.toNorm (lo), nhi = d.toNorm (hi);
    const float n = nlo + (nhi - nlo) * rng.uniform();  // always consume, keeps seeds stable under locks
    auto& r = ranges.e[(size_t) p];
    r.touched = true;
    r.lo = std::min (nlo, nhi);
    r.hi = std::max (nlo, nhi);
    const float plain = d.toPlain (n);
    if (! locked (p))
        s.set (p, plain);
    return s.get (p);
}

int RecipeBuilder::weighted (std::initializer_list<float> weights)
{
    float total = 0.f;
    for (float w : weights) total += w;
    float x = rng.uniform() * total;
    int i = 0;
    for (float w : weights)
    {
        if (x < w) return i;
        x -= w;
        ++i;
    }
    return (int) weights.size() - 1;
}

int RecipeBuilder::pick (P p, std::initializer_list<int> options, std::initializer_list<float> weights)
{
    int idx;
    if (weights.size() == options.size())
        idx = weighted (weights);
    else
        idx = std::min ((int) (rng.uniform() * (float) options.size()), (int) options.size() - 1);
    const int value = *(options.begin() + idx);

    auto& r = ranges.e[(size_t) p];
    r.touched = true;
    r.numOptions = 0;
    const bool hasWeights = weights.size() == options.size();
    int i = 0;
    for (int o : options)
    {
        // an option the recipe gave no weight stays out of Mutate's reach too
        const bool excluded = hasWeights && *(weights.begin() + i) <= 0.f;
        ++i;
        if (! excluded && r.numOptions < (int) r.options.size())
            r.options[(size_t) r.numOptions++] = (int8_t) o;
    }
    if (! locked (p))
        s.set (p, (float) value);
    return locked (p) ? s.geti (p) : value;
}

float RecipeBuilder::among (P p, std::initializer_list<float> values)
{
    const int idx = std::min ((int) (rng.uniform() * (float) values.size()), (int) values.size() - 1);
    const float v = *(values.begin() + idx);
    set (p, v);
    return locked (p) ? s.get (p) : v;
}

int RecipeBuilder::route (ModSrc src, ModDst dst, float amtLo, float amtHi)
{
    const float amt = rng.range (amtLo, amtHi);
    while (nextSlot < kNumModSlots && locked (modSrcParam (nextSlot)))
        ++nextSlot;
    if (nextSlot >= kNumModSlots)
        return -1;
    const int slot = nextSlot++;
    set (modSrcParam (slot), (float) src);
    set (modDstParam (slot), (float) dst);
    auto& r = ranges.e[(size_t) modAmtParam (slot)];
    const auto& d = def (modAmtParam (slot));
    r.touched = true;
    r.lo = d.toNorm (std::min (amtLo, amtHi));
    r.hi = d.toNorm (std::max (amtLo, amtHi));
    if (! locked (modAmtParam (slot)))
        s.set (modAmtParam (slot), amt);
    return slot;
}

void RecipeBuilder::lfo (int index, LfoShape shape, float hzLo, float hzHi, bool retrig)
{
    set (lfoParam (index, 0), (float) shape);
    set (lfoParam (index, 2), 0.f);
    uni (lfoParam (index, 1), hzLo, hzHi);
    set (lfoParam (index, 4), retrig ? 1.f : 0.f);
}

void RecipeBuilder::lfoSync (int index, LfoShape shape, std::initializer_list<int> divisions, bool retrig)
{
    set (lfoParam (index, 0), (float) shape);
    set (lfoParam (index, 2), 1.f);
    pick (lfoParam (index, 3), divisions);
    set (lfoParam (index, 4), retrig ? 1.f : 0.f);
}

// ---------------------------------------------------------------------------
// Generator
// ---------------------------------------------------------------------------

ParamState Generator::generate (Category c, uint32_t seed, const ParamState& current, const LockSet& locks, bool withArp,
                                bool kickToKey)
{
    ParamState out;  // init patch
    for (int i = 0; i < kNumParams; ++i)
        if (locks.test ((size_t) i))
            out.v[(size_t) i] = current.v[(size_t) i];

    // The global quality and volume are the user's, never the recipe's, and
    // so is the unit an energy length is shown in (bars or seconds).
    out.set (P::Quality, current.get (P::Quality));
    out.set (P::RenderQuality, current.get (P::RenderQuality));
    out.set (P::MasterVol, current.get (P::MasterVol));
    out.set (P::EnergyMode, current.get (P::EnergyMode));
    // so is the key and scale the arp plays in
    out.set (P::ArpKey, current.get (P::ArpKey));
    out.set (P::ArpScale, current.get (P::ArpScale));

    ranges.clear();
    dsp::Rng rng (((uint64_t) seed << 8) ^ (uint64_t) ((int) c + 1) * 0x9E3779B97F4A7C15ULL);
    RecipeBuilder b (out, locks, rng, ranges);
    b.setCategory (c);
    recipeFor (c) (b);
    unisonRecipe (c, b);
    if (familyOf (c) == Family::Kick)
        b.on (P::KickKeyTrack, kickToKey);

    dsp::Rng arpRng (((uint64_t) seed << 8) ^ (uint64_t) ((int) c + 1) * 0xC2B2AE3D27D4EB4FULL ^ 0x41525050ULL);
    RecipeBuilder arp (out, locks, arpRng, ranges);
    arp.setCategory (c);
    arp.continueSlotsFrom (b);
    withArp = withArp && hasArp (c);   // basses, kicks and energy sounds have none
    arp.on (P::ArpOn, withArp);
    if (withArp)
    {
        arpRecipeFor (c) (arp);
        arpPatternRecipeFor (c) (arp);
    }

    // Not the recipe's to decide and not lockable: a kick category is a kick,
    // the Phoscyon category plays the 303 engine.
    out.set (P::EngineMode, (float) (familyOf (c) == Family::Kick ? EngineMode::Kick
                                     : isPhoscyon (c)            ? EngineMode::Phoscyon : EngineMode::Synth));
    return out;
}

ParamState Generator::generateArpPattern (Category c, uint32_t seed, const ParamState& current, const LockSet& locks)
{
    ParamState out = current;
    dsp::Rng rng (((uint64_t) seed << 8) ^ (uint64_t) ((int) c + 1) * 0xD6E8FEB86659FD93ULL ^ 0x50415454ULL);
    RecipeBuilder b (out, locks, rng, ranges);
    b.setCategory (c);
    arpPatternRecipeFor (c) (b);
    return out;
}

ParamState Generator::mutate (float amount, uint32_t seed, const ParamState& current, const LockSet& locks) const
{
    ParamState out = current;
    dsp::Rng rng (((uint64_t) seed << 1) ^ 0xA5A5A5A5ULL);
    amount = std::clamp (amount, 0.f, 1.f);
    const auto& defs = paramDefs();

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& r = ranges.e[(size_t) i];
        const auto& d = defs[(size_t) i];
        const float g = rng.gaussian();   // consume unconditionally for stability
        const float u = rng.uniform();
        const uint32_t pickRaw = rng.next();   // for choices; drawn for every param so locks don't shift the rest
        if (locks.test ((size_t) i) || ! r.touched)
            continue;
        if (i == (int) P::Quality || i == (int) P::RenderQuality || i == (int) P::MasterVol || i == (int) P::EngineMode
            || i == (int) P::EnergyMode || i == (int) P::ArpKey || i == (int) P::ArpScale)
            continue;

        // Choices and picked integers hop between the recipe's options; an
        // integer drawn from a range (voices, unison counts) moves like a
        // continuous param and is rounded by snap.
        if (d.mapping == Mapping::Choice || d.mapping == Mapping::Toggle || (d.unit == Unit::Int && r.numOptions > 0))
        {
            if (r.numOptions > 1 && u < amount * 0.35f)
                out.v[(size_t) i] = (float) r.options[(size_t) (pickRaw % (uint32_t) r.numOptions)];
            continue;
        }

        if (r.hi <= r.lo)
            continue;
        const float width = r.hi - r.lo;
        const float n = d.toNorm (current.v[(size_t) i]);
        const float margin = width * 0.15f;
        const float nn = std::clamp (n + g * amount * width * 0.5f, std::max (0.f, r.lo - margin), std::min (1.f, r.hi + margin));
        out.v[(size_t) i] = d.snap (d.toPlain (nn));
    }
    return out;
}

} // namespace psy
