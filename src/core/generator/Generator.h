#pragma once

#include "Category.h"
#include "dsp/Common.h"
#include "engine/Params.h"

#include <array>
#include <bitset>
#include <string>
#include <initializer_list>
#include <vector>

namespace psy
{

using LockSet = std::bitset<kNumParams>;

// What a recipe allowed for each parameter, so Mutate stays inside the
// category's character instead of wandering off into noise.
struct RangeMemory
{
    struct Entry
    {
        bool touched = false;
        float lo = 0.f, hi = 0.f;       // normalised range for continuous params
        std::array<int8_t, 12> options {};
        int numOptions = 0;             // for choices
    };
    std::array<Entry, kNumParams> e;
    void clear() { e = {}; }
};

// The interface recipes are written against.
class RecipeBuilder
{
public:
    RecipeBuilder (ParamState& state, const LockSet& locks, dsp::Rng& rng, RangeMemory& ranges)
        : s (state), locks (locks), rng (rng), ranges (ranges) {}

    // Fixed value (still recorded so Mutate leaves it alone).
    void set (P p, float plain);
    // Uniform in the parameter's own (normalised) space: log for frequencies,
    // power curve for times, so ranges behave the way they sound.
    float uni (P p, float lo, float hi);
    // Pick one of the choice indices, optionally weighted.
    int pick (P p, std::initializer_list<int> options, std::initializer_list<float> weights = {});
    // Pick one of several plain values (e.g. musical intervals).
    float among (P p, std::initializer_list<float> values);
    void on (P p, bool enabled) { set (p, enabled ? 1.f : 0.f); }

    bool chance (float probability) { return rng.chance (probability); }
    float rnd (float lo, float hi) { return rng.range (lo, hi); }
    int weighted (std::initializer_list<float> weights);

    // Adds a modulation route in the next free slot (returns slot or -1).
    int route (ModSrc src, ModDst dst, float amtLo, float amtHi);
    // Gives every macro without a route one, from the targets listed (in
    // order of preference). Targets whose module is off or that a macro
    // already drives are skipped. Run it last: it draws one number per macro.
    void macros (std::initializer_list<ModDst> targets);

    // Tempo-synced LFO setup helpers.
    void lfo (int index, LfoShape shape, float hzLo, float hzHi, bool retrig);
    void lfoSync (int index, LfoShape shape, std::initializer_list<int> divisions, bool retrig);

    ParamState& state() { return s; }
    // The category being built (macros keep basses dry, unison picks its voices).
    void setCategory (Category c) { cat = c; }
    Category category() const { return cat; }

private:
    ParamState& s;
    Category cat = Category::Count;
    const LockSet& locks;
    dsp::Rng& rng;
    RangeMemory& ranges;
    int nextSlot = 0;

    bool locked (P p) const { return locks.test ((size_t) p); }
};

using Recipe = void (*) (RecipeBuilder&);
Recipe recipeFor (Category c) noexcept;
Recipe arpRecipeFor (Category c) noexcept;          // arp settings (ArpRecipes.cpp)
Recipe arpPatternRecipeFor (Category c) noexcept;   // the arp's 16 steps
Recipe kickRecipeFor (Category c) noexcept;         // kick categories (KickRecipes.cpp)
void jilaxRecipe (RecipeBuilder& b);                // Jilax shots (JilaxRecipes.cpp)
void ghostStabRecipe (RecipeBuilder& b);            // Ghost Rider's offbeat stab (GhostRecipes.cpp)
void ghostBassRecipe (RecipeBuilder& b);            // Ghost Rider's rolling bass (GhostRecipes.cpp)
// Unison for the category's oscillators (leads and pads spread, basses keep
// one voice). Runs after the sound recipe, on the same random stream.
void unisonRecipe (Category c, RecipeBuilder& b);

// What a macro does, in a word or two, from the targets it drives ("Cutoff",
// "Filter", "Reverb", "Cutoff + Drive"). Empty when it drives nothing.
std::string macroLabel (const ParamState& p, int macro);

// Named starting points for the pattern editor (ArpPattern::parse notation).
struct ArpTemplate { const char* name; const char* steps; };
const std::vector<ArpTemplate>& arpTemplates() noexcept;

class Generator
{
public:
    // A new patch of the category. Locked parameters keep their current value;
    // everything else starts from the init patch, then the recipe runs.
    // withArp adds the category's arp on a separate random stream, so the
    // sound itself is the same either way. kickToKey makes a generated kick
    // follow the played key (key tracking) instead of its fixed Tune; it
    // draws no randomness, so the same seed gives the same kick either way.
    // The engine mode always follows the category.
    ParamState generate (Category c, uint32_t seed, const ParamState& current, const LockSet& locks, bool withArp = false,
                         bool kickToKey = true);

    // Only a new arp pattern in the category's style; everything else stays.
    ParamState generateArpPattern (Category c, uint32_t seed, const ParamState& current, const LockSet& locks);

    // Nudges every unlocked parameter the last recipe touched by up to
    // `amount` (0..1) of its allowed range.
    ParamState mutate (float amount, uint32_t seed, const ParamState& current, const LockSet& locks) const;

    const RangeMemory& lastRanges() const noexcept { return ranges; }
    void setRanges (const RangeMemory& r) { ranges = r; }

private:
    RangeMemory ranges;
};

} // namespace psy
