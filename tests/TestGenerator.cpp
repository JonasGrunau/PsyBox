#include "generator/Generator.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string_view>

using namespace psy;

namespace
{
    // The slot a macro sends to `dst`, or -1.
    int macroSlot (const ParamState& s, int macro, ModDst dst)
    {
        for (int i = 0; i < kNumModSlots; ++i)
            if (s.geti (modSrcParam (i)) == (int) ModSrc::Macro1 + macro && s.geti (modDstParam (i)) == (int) dst)
                return i;
        return -1;
    }
}

TEST_CASE ("Same category and seed always give the same patch", "[generator]")
{
    Generator a, b;
    const ParamState cur;
    const LockSet none;
    for (int c = 0; c < kNumCategories; ++c)
    {
        const auto p1 = a.generate ((Category) c, 1234, cur, none);
        const auto p2 = b.generate ((Category) c, 1234, cur, none);
        CHECK (p1.v == p2.v);
        const auto p3 = a.generate ((Category) c, 1235, cur, none);
        CHECK (p1.v != p3.v);
    }
}

TEST_CASE ("Locked parameters survive generate and mutate", "[generator]")
{
    Generator g;
    ParamState cur;
    cur.set (P::F1Cutoff, 1234.f);
    cur.set (P::OscAType, 3.f);
    LockSet locks;
    locks.set ((size_t) P::F1Cutoff);
    locks.set ((size_t) P::OscAType);
    for (uint32_t seed = 1; seed < 40; ++seed)
    {
        const auto p = g.generate (Category::Acid, seed, cur, locks);
        CHECK (p.get (P::F1Cutoff) == 1234.f);
        CHECK (p.geti (P::OscAType) == 3);
        const auto m = g.mutate (1.f, seed, p, locks);
        CHECK (m.get (P::F1Cutoff) == 1234.f);
    }
}

TEST_CASE ("Locking one parameter does not reshuffle the others", "[generator]")
{
    Generator g;
    const ParamState cur;
    LockSet locks;
    const auto free = g.generate (Category::PsyLead, 99, cur, locks);
    locks.set ((size_t) P::AenvA);
    const auto locked = g.generate (Category::PsyLead, 99, cur, locks);
    for (int i = 0; i < kNumParams; ++i)
        if (i != (int) P::AenvA)
            CHECK (free.v[(size_t) i] == locked.v[(size_t) i]);
}

TEST_CASE ("Mutate stays near the recipe's ranges", "[generator]")
{
    Generator g;
    const auto p = g.generate (Category::PsyBass, 7, ParamState(), LockSet());
    const auto& r = g.lastRanges().e[(size_t) P::F1Cutoff];
    REQUIRE (r.touched);
    auto m = p;
    for (uint32_t s = 0; s < 200; ++s)
    {
        m = g.mutate (1.f, s, m, LockSet());
        const float n = def (P::F1Cutoff).toNorm (m.get (P::F1Cutoff));
        const float margin = (r.hi - r.lo) * 0.15f + 1e-4f;
        CHECK (n >= r.lo - margin);
        CHECK (n <= r.hi + margin);
    }
}

TEST_CASE ("Risers climb in pitch over their length, uplifters sweep noise", "[generator]")
{
    const auto routesTo = [] (const ParamState& p, ModSrc src, ModDst dst) {
        for (int s = 0; s < kNumModSlots; ++s)
            if ((src == ModSrc::None || p.geti (modSrcParam (s)) == (int) src) && p.geti (modDstParam (s)) == (int) dst)
                return true;
        return false;
    };
    Generator g;
    for (uint32_t seed = 1; seed <= 50; ++seed)
    {
        const auto riser = g.generate (Category::Riser, seed, ParamState(), LockSet());
        INFO ("seed " << seed);
        CHECK (routesTo (riser, ModSrc::Sweep, ModDst::PitchAll));
        CHECK (riser.get (P::NoiseLevel) <= 0.1f);

        const auto up = g.generate (Category::Uplifter, seed, ParamState(), LockSet());
        CHECK (up.get (P::NoiseLevel) >= 0.8f);
        CHECK ((up.geti (P::OscAOn) == 0 || up.get (P::OscALevel) <= 0.15f));
        CHECK_FALSE (routesTo (up, ModSrc::None, ModDst::PitchAll));
        CHECK (routesTo (up, ModSrc::Sweep, ModDst::F1Cutoff));
    }
}

TEST_CASE ("Parameter mappings round-trip", "[params]")
{
    for (const auto& d : paramDefs())
        for (float n = 0.f; n <= 1.f; n += 0.125f)
        {
            const float plain = d.toPlain (n);
            INFO (d.id << " n=" << n);
            CHECK (plain >= d.min - 1e-4f);
            CHECK (plain <= d.max + 1e-4f);
            if (! d.isDiscrete())
                CHECK (std::abs (d.toNorm (plain) - n) < 1e-3f);
        }
}

TEST_CASE ("Arp generation is off unless asked for, and on for every synth category when it is", "[generator][arp]")
{
    Generator g;
    for (int c = 0; c < kNumCategories; ++c)
        for (uint32_t seed = 1; seed < 6; ++seed)
        {
            INFO ("category " << c << " seed " << seed);
            CHECK_FALSE (g.generate ((Category) c, seed, ParamState(), LockSet()).on (P::ArpOn));
            const auto p = g.generate ((Category) c, seed, ParamState(), LockSet(), true);
            CHECK (p.on (P::ArpOn) == hasArp ((Category) c));   // basses, kicks and energy sounds never get one
            CHECK_FALSE (p.on (P::ArpHold));   // a performance switch, never the recipe's
        }
}

TEST_CASE ("Arp generation never changes the sound itself", "[generator][arp]")
{
    Generator g;
    const auto isArp = [] (int i) { return std::string_view (paramDefs()[(size_t) i].group) == "Arp"; };
    for (int c = 0; c < kNumCategories; ++c)
        for (uint32_t seed = 1; seed < 20; ++seed)
        {
            const auto plain = g.generate ((Category) c, seed, ParamState(), LockSet());
            const auto withArp = g.generate ((Category) c, seed, ParamState(), LockSet(), true);
            for (int i = 0; i < kNumParams; ++i)
                if (! isArp (i))
                {
                    INFO ("category " << c << " seed " << seed << " param " << paramDefs()[(size_t) i].id);
                    CHECK (plain.v[(size_t) i] == withArp.v[(size_t) i]);
                }
        }
}

TEST_CASE ("Arp recipes vary with the seed and Mutate keeps them in the category's space", "[generator][arp]")
{
    Generator g;
    const auto a = g.generate (Category::Bubble, 1, ParamState(), LockSet(), true);
    bool differs = false;
    for (uint32_t seed = 2; seed < 12 && ! differs; ++seed)
        differs = g.generate (Category::Bubble, seed, ParamState(), LockSet(), true).get (P::ArpGate) != a.get (P::ArpGate);
    CHECK (differs);

    auto p = g.generate (Category::Acid, 3, ParamState(), LockSet(), true);
    for (uint32_t s = 0; s < 200; ++s)
    {
        p = g.mutate (1.f, s, p, LockSet());
        CHECK (p.on (P::ArpOn));
        CHECK (p.geti (P::ArpDiv) == 12);   // acid lines stay on 1/16
        CHECK (p.get (P::ArpGate) <= 1.3f + 0.8f * 0.15f + 1e-3f);
    }
}

TEST_CASE ("Generated arp patterns change with the seed and keep each category's rules", "[generator][arp]")
{
    Generator g;
    const auto pattern = [&] (Category c, uint32_t seed) {
        return ArpPattern::from (g.generate (c, seed, ParamState(), LockSet(), true));
    };
    for (auto c : { Category::Acid, Category::Bubble, Category::Squelch, Category::Pluck, Category::Zap })
    {
        int distinct = 0;
        const auto first = pattern (c, 1);
        for (uint32_t seed = 2; seed < 30; ++seed)
            distinct += pattern (c, seed) == first ? 0 : 1;
        INFO ("category " << (int) c);
        CHECK (distinct > 10);
    }
    for (uint32_t seed = 1; seed < 60; ++seed)
    {
        // a drone plays every step, tied or struck once a step, and moves
        const auto drone = pattern (Category::Drone, seed);
        bool tied = true;
        std::set<int> notes;
        for (int i = 0; i < drone.length; ++i)
        {
            CHECK (drone.has (i, ArpPattern::On));
            tied = tied && drone.has (i, ArpPattern::Slide);
            notes.insert (drone.notes[(size_t) i]);
        }
        const bool struck = [&] { for (int i = 0; i < drone.length; ++i) if (drone.has (i, ArpPattern::Slide)) return false; return true; }();
        CHECK ((tied || struck));
        CHECK (notes.size() >= 3);
        const auto acid = pattern (Category::Acid, seed);
        CHECK (acid.has (0, ArpPattern::On));
    }
}

TEST_CASE ("A new arp pattern leaves everything else alone", "[generator][arp]")
{
    Generator g;
    const auto base = g.generate (Category::Acid, 5, ParamState(), LockSet(), true);
    const auto next = g.generateArpPattern (Category::Acid, 77, base, LockSet());
    CHECK_FALSE (ArpPattern::from (next) == ArpPattern::from (base));
    for (int i = 0; i < kNumParams; ++i)
    {
        const bool isPattern = i == (int) P::ArpLength || (i >= (int) P::ArpStep1 && i <= (int) P::ArpNote16);
        if (! isPattern)
            CHECK (next.v[(size_t) i] == base.v[(size_t) i]);
    }
    CHECK (ArpPattern::from (g.generateArpPattern (Category::Acid, 77, base, LockSet())) == ArpPattern::from (next));
}

TEST_CASE ("Generated arp patterns are composed: figures repeat, slides land, accents stay sparse", "[generator][arp]")
{
    Generator g;
    for (Category c : categoryDisplayOrder())
        for (uint32_t seed = 1; seed < 40; ++seed)
        {
            if (! hasArp (c)) continue;
            const auto p = ArpPattern::from (g.generate (c, seed, ParamState(), LockSet(), true));
            INFO (categoryInfo()[(size_t) c].name << " seed " << seed);
            int notes = 0;
            for (int i = 0; i < p.length; ++i)
            {
                notes += p.has (i, ArpPattern::On) ? 1 : 0;
                if (p.has (i, ArpPattern::Slide))
                    CHECK (p.has ((i + 1) % p.length, ArpPattern::On));   // every slide lands on a note
            }
            CHECK (notes > 0);
        }

    for (uint32_t seed = 1; seed < 60; ++seed)
    {
        INFO ("seed " << seed);
        // acid: a line with a few accents and at most four slides
        const auto acid = ArpPattern::from (g.generate (Category::Acid, seed, ParamState(), LockSet(), true));
        int accents = 0, slides = 0;
        for (int i = 0; i < acid.length; ++i)
        {
            accents += acid.has (i, ArpPattern::Accent) ? 1 : 0;
            slides += acid.has (i, ArpPattern::Slide) ? 1 : 0;
        }
        CHECK (accents >= 1);
        CHECK (accents <= 6);
        CHECK (slides <= 4);
    }
}

TEST_CASE ("Generated arp lines move off the key and stay in a scale", "[generator][arp]")
{
    // the scales the lines are written in: Phrygian, minor, harmonic minor,
    // Phrygian dominant, Dorian
    const std::array<std::array<int, 7>, 5> scales { { { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 },
                                                       { 0, 1, 4, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 } } };
    Generator g;
    for (Category c : categoryDisplayOrder())
    {
        if (! hasArp (c)) continue;
        int moving = 0, distinctSum = 0;
        constexpr int kSeeds = 60;
        for (uint32_t seed = 1; seed <= kSeeds; ++seed)
        {
            const auto p = ArpPattern::from (g.generate (c, seed, ParamState(), LockSet(), true));
            INFO (categoryInfo()[(size_t) c].name << " seed " << seed);
            std::set<int> played;
            for (int i = 0; i < p.length; ++i)
                if (p.has (i, ArpPattern::On)) played.insert (p.notes[(size_t) i]);
            bool inKey = false;
            for (const auto& s : scales)
                inKey = inKey || std::all_of (played.begin(), played.end(), [&] (int n) {
                    return std::find (s.begin(), s.end(), ((n % 12) + 12) % 12) != s.end();
                });
            CHECK (inKey);
            for (int n : played)
                CHECK (std::abs (n) <= kArpNoteRange);
            moving += played.size() > 1 ? 1 : 0;
            distinctSum += (int) played.size();
        }
        INFO (categoryInfo()[(size_t) c].name);
        CHECK (moving >= kSeeds * 6 / 10);   // a held key plays a line, not one note
        if (c == Category::Acid || c == Category::Pluck || c == Category::PsyLead || c == Category::Bubble)
            CHECK (distinctSum >= kSeeds * 3);   // the melodic ones use at least three notes on average
    }
}

TEST_CASE ("Every category has a name, a short name and a blurb", "[generator]")
{
    for (const auto& info : categoryInfo())
    {
        REQUIRE (info.name != nullptr);
        REQUIRE (info.shortName != nullptr);
        REQUIRE (info.blurb != nullptr);
        CHECK (std::string_view (info.name).find ('/') == std::string_view::npos);   // names are preset folders
    }
    int listed = 0;
    for (auto f : { Family::Synth, Family::Bass, Family::Kick })
    {
        CHECK ((int) categoriesIn (f).size() == familySize (f));
        listed += (int) categoriesIn (f).size();
    }
    CHECK (listed == kNumCategories);
    CHECK (familySize (Family::Bass) == 7);
    CHECK (familySize (Family::Kick) == 5);
}

TEST_CASE ("The sidebar lists sections in order, alphabetical inside each", "[generator][category]")
{
    const auto& info = categoryInfo();
    for (int c = 0; c < kNumCategories; ++c)
    {
        const auto g = info[(size_t) c].group;
        INFO (info[(size_t) c].name);
        CHECK ((g == CategoryGroup::Kick) == (familyOf ((Category) c) == Family::Kick));
        CHECK ((g == CategoryGroup::Bass) == (familyOf ((Category) c) == Family::Bass));
    }

    const auto& order = categoryDisplayOrder();
    for (size_t i = 1; i < order.size(); ++i)
    {
        const auto& a = info[(size_t) order[i - 1]];
        const auto& b = info[(size_t) order[i]];
        INFO (a.name << " before " << b.name);
        CHECK (a.group <= b.group);
        if (a.group == b.group) CHECK (std::string_view (a.name) < std::string_view (b.name));
    }

    int perGroup[(size_t) CategoryGroup::Count] {};
    for (const auto& ci : info) ++perGroup[(size_t) ci.group];
    for (int n : perGroup) CHECK (n > 0);
}

TEST_CASE ("One rainbow runs down the whole sidebar, tab after tab", "[generator][category]")
{
    const auto hue = [] (Category c) {
        const uint32_t rgb = categoryInfo()[(size_t) c].colour;
        const float r = (float) ((rgb >> 16) & 0xFF) / 255.f, g = (float) ((rgb >> 8) & 0xFF) / 255.f, b = (float) (rgb & 0xFF) / 255.f;
        const float mx = std::max ({ r, g, b }), d = mx - std::min ({ r, g, b });
        const float h = mx == r ? std::fmod ((g - b) / d, 6.f) : mx == g ? (b - r) / d + 2.f : (r - g) / d + 4.f;
        return std::fmod (h * 60.f + 360.f, 360.f);
    };
    // red first, towards magenta, no wrap past red
    const auto& order = categoryDisplayOrder();
    CHECK (hue (order.front()) < 15.f);
    CHECK (hue (order.back()) > 290.f);
    float last = -1.f;
    for (Category c : order)
    {
        INFO (categoryInfo()[(size_t) c].name << " hue " << hue (c));
        CHECK (hue (c) > last);
        last = hue (c);
    }
}

TEST_CASE ("The sidebar tabs hold the sounds they are named for", "[generator][category]")
{
    using enum Category;
    const auto in = [] (CategoryGroup g, Category c) {
        const auto& l = categoriesIn (g);
        return std::find (l.begin(), l.end(), c) != l.end();
    };
    for (auto c : { GhostStab, Grid, Jilax, PsyLead, Stab, Pluck }) CHECK (in (CategoryGroup::Main, c));
    CHECK (categoriesIn (CategoryGroup::Main).size() == 6);
    for (auto c : { Acid, Phoscyon }) CHECK (in (CategoryGroup::Acid, c));
    CHECK (categoriesIn (CategoryGroup::Acid).size() == 2);
    for (auto c : { Squelch, Zap, Laser, Screech, AlienVoice, Bubble, Gurgle, Wobble, Metallic, Glitch }) CHECK (in (CategoryGroup::Fx, c));
    for (auto c : { Riser, Downlifter, NoiseFx, Uplifter, Impact, Reverse }) CHECK (in (CategoryGroup::Energy, c));
    for (auto c : { Atmos, Drone, Gate }) CHECK (in (CategoryGroup::Background, c));
    int listed = 0;
    for (int g = 0; g < kNumCategoryGroups; ++g) listed += (int) categoriesIn ((CategoryGroup) g).size();
    CHECK (listed == kNumCategories);
}

TEST_CASE ("The engine mode always follows the category, even when locked", "[generator][kick]")
{
    Generator g;
    LockSet locks;
    locks.set ((size_t) P::EngineMode);
    ParamState kickNow, synthNow;
    kickNow.set (P::EngineMode, (float) EngineMode::Kick);
    for (int c = 0; c < kNumCategories; ++c)
    {
        const auto cat = (Category) c;
        const int want = familyOf (cat) == Family::Kick ? (int) EngineMode::Kick
                       : isPhoscyon (cat)              ? (int) EngineMode::Phoscyon : (int) EngineMode::Synth;
        INFO (categoryInfo()[(size_t) c].name);
        CHECK (g.generate (cat, 5, kickNow, locks).geti (P::EngineMode) == want);
        CHECK (g.generate (cat, 5, synthNow, locks).geti (P::EngineMode) == want);
        auto m = g.generate (cat, 5, synthNow, LockSet());
        for (uint32_t s = 0; s < 20; ++s)
            m = g.mutate (1.f, s, m, LockSet());
        CHECK (m.geti (P::EngineMode) == want);
    }
}

TEST_CASE ("Kick and synth patches keep to their own parameters", "[generator][kick]")
{
    Generator g;
    const ParamState init;
    const auto& defs = paramDefs();
    auto group = [&] (int i) { return std::string_view (defs[(size_t) i].group); };
    for (int c = 0; c < kNumCategories; ++c)
        for (uint32_t seed : { 1u, 9u, 333u })
        {
            const auto cat = (Category) c;
            const auto p = g.generate (cat, seed, init, LockSet(), true);
            for (int i = 0; i < kNumParams; ++i)
            {
                const bool kickParam = group (i).substr (0, 4) == "Kick";
                const bool shared = group (i) == "Engine" || group (i) == "Arp" || group (i) == "Distortion"
                                 || group (i) == "Reverb" || i == (int) P::VelSens;
                INFO (categoryInfo()[(size_t) c].name << " seed " << seed << " param " << defs[(size_t) i].id);
                if (familyOf (cat) == Family::Kick && ! kickParam && ! shared)
                    CHECK (p.v[(size_t) i] == init.v[(size_t) i]);
                if (familyOf (cat) != Family::Kick && kickParam)
                    CHECK (p.v[(size_t) i] == init.v[(size_t) i]);
            }
        }
}

TEST_CASE ("The key toggle only switches key tracking", "[generator][kick]")
{
    Generator g;
    for (Category c : categoriesIn (Family::Kick))
        for (uint32_t seed = 1; seed < 10; ++seed)
        {
            const auto toKey = g.generate (c, seed, ParamState(), LockSet(), false, true);
            const auto fixed = g.generate (c, seed, ParamState(), LockSet(), false, false);
            CHECK (toKey.on (P::KickKeyTrack));
            CHECK_FALSE (fixed.on (P::KickKeyTrack));
            for (int i = 0; i < kNumParams; ++i)
                if (i != (int) P::KickKeyTrack)
                    CHECK (toKey.v[(size_t) i] == fixed.v[(size_t) i]);
        }
}

TEST_CASE ("Basses, kicks and energy sounds never get an arp", "[generator][arp]")
{
    Generator g;
    for (Category c : categoryDisplayOrder())
        if (familyOf (c) != Family::Synth || isEnergy (c))
            for (uint32_t seed = 1; seed < 30; ++seed)
            {
                INFO (categoryInfo()[(size_t) c].name << " seed " << seed);
                const auto p = g.generate (c, seed, ParamState(), LockSet(), true);
                CHECK_FALSE (p.on (P::ArpOn));
                for (int i = 0; i < kNumParams; ++i)   // the arp settings stay at the init patch
                    if (std::string_view (paramDefs()[(size_t) i].group) == "Arp")
                        CHECK (p.v[(size_t) i] == ParamState().v[(size_t) i]);
            }
}

TEST_CASE ("The mod matrix lists match their enums, and targets map to knobs both ways", "[params]")
{
    CHECK ((int) choices::modSrc.size() == (int) ModSrc::Count);
    CHECK ((int) choices::modDst.size() == (int) ModDst::Count);
    for (int s = 0; s < kNumModSlots; ++s)
    {
        CHECK ((int) def (modSrcParam (s)).max == (int) ModSrc::Count - 1);
        CHECK ((int) def (modDstParam (s)).max == (int) ModDst::Count - 1);
        CHECK (std::string_view (def (modAmtParam (s)).name) == "Amount");
    }
    for (int d = 1; d < (int) ModDst::Count; ++d)
    {
        const P knob = modTargetParam ((ModDst) d);
        if (knob == P::Count) continue;   // Pitch, Amp, Pan: matrix only
        INFO (choices::modDst[(size_t) d]);
        CHECK (modTargetFor (knob) == (ModDst) d);
        CHECK (modDepthNorm ((ModDst) d) > 0.f);
    }
    CHECK (modTargetFor (P::AenvA) == ModDst::None);
}

TEST_CASE ("Macro labels say what a macro drives", "[generator][macro]")
{
    const auto with = [] (std::initializer_list<std::pair<ModSrc, ModDst>> routes) {
        ParamState p;
        int slot = 0;
        for (const auto& [src, dst] : routes)
        {
            p.set (modSrcParam (slot), (float) src);
            p.set (modDstParam (slot), (float) dst);
            p.set (modAmtParam (slot), 0.3f);
            ++slot;
        }
        return p;
    };
    using S = ModSrc;
    using D = ModDst;
    CHECK (macroLabel (ParamState(), 0).empty());
    CHECK (macroLabel (with ({ { S::Macro1, D::F1Cutoff } }), 0) == "Cutoff");
    CHECK (macroLabel (with ({ { S::Macro1, D::F1Cutoff } }), 1).empty());
    CHECK (macroLabel (with ({ { S::Macro2, D::F1Cutoff }, { S::Macro2, D::F2Cutoff } }), 1) == "Cutoff");
    CHECK (macroLabel (with ({ { S::Macro2, D::F2Cutoff } }), 1) == "Cutoff 2");
    CHECK (macroLabel (with ({ { S::Macro1, D::F1Cutoff }, { S::Macro1, D::F1Reso } }), 0) == "Filter");
    CHECK (macroLabel (with ({ { S::Macro3, D::ReverbMix } }), 2) == "Reverb");
    CHECK (macroLabel (with ({ { S::Macro4, D::F1Cutoff }, { S::Macro4, D::DelayMix } }), 3) == "Cutoff + Delay");
    CHECK (macroLabel (with ({ { S::Macro1, D::F1Cutoff }, { S::Macro1, D::ReverbMix } }), 0) == "Filter + Space");
    CHECK (macroLabel (with ({ { S::Lfo1, D::F1Cutoff } }), 0).empty());   // only macro sources count
}

TEST_CASE ("Generated synth and bass patches drive all four macros, kicks none", "[generator][macro]")
{
    Generator g;
    for (int c = 0; c < kNumCategories; ++c)
        for (uint32_t seed = 1; seed <= 25; ++seed)
        {
            const auto cat = (Category) c;
            const auto p = g.generate (cat, seed, ParamState(), LockSet());
            for (int m = 0; m < 4; ++m)
            {
                INFO (categoryInfo()[(size_t) c].name << " seed " << seed << " macro " << m + 1);
                if (familyOf (cat) == Family::Kick) CHECK (macroLabel (p, m).empty());
                else CHECK_FALSE (macroLabel (p, m).empty());
            }
        }
}

TEST_CASE ("Energy sounds hang their motion on the sweep and keep the user's length unit", "[generator][energy]")
{
    Generator g;
    ParamState current;
    current.set (P::EnergyMode, (float) EnergyMode::Time);
    for (Category c : categoriesIn (CategoryGroup::Energy))
        for (uint32_t seed = 1; seed <= 20; ++seed)
        {
            INFO (categoryInfo()[(size_t) c].name << " seed " << seed);
            const auto p = g.generate (c, seed, current, LockSet());
            CHECK (p.geti (P::EnergyMode) == (int) EnergyMode::Time);
            bool sweep = std::abs (p.get (P::EnergyFade)) > 0.1f;
            for (int s = 0; s < kNumModSlots; ++s)
                sweep = sweep || (p.geti (modSrcParam (s)) == (int) ModSrc::Sweep && p.get (modAmtParam (s)) != 0.f);
            CHECK (sweep);
            CHECK (g.generate (c, seed, ParamState(), LockSet()).geti (P::EnergyMode) == (int) EnergyMode::Bars);   // bars by default
        }
}

TEST_CASE ("Reverse basses last their length, and reverse subs slide by a musical interval", "[generator][bass]")
{
    Generator g;
    for (Category c : { Category::ReverseBass, Category::ReverseSub })
        for (uint32_t seed = 1; seed <= 30; ++seed)
        {
            INFO (categoryInfo()[(size_t) c].name << " seed " << seed);
            const auto p = g.generate (c, seed, ParamState(), LockSet());
            CHECK (familyOf (c) == Family::Bass);
            CHECK (hasLength (c));
            CHECK_FALSE (hasArp (c));
            CHECK (p.on (P::EnergyCut));
            CHECK (energyBarsValue (p.geti (P::EnergyBars)) <= 0.5f);   // one beat, or two
            CHECK (p.geti (P::VoiceMode) != (int) VoiceMode::Poly);
            if (c != Category::ReverseSub) continue;
            int slides = 0;
            for (int s = 0; s < kNumModSlots; ++s)
                if (p.geti (modSrcParam (s)) == (int) ModSrc::Sweep && p.geti (modDstParam (s)) == (int) ModDst::PitchAll)
                {
                    ++slides;
                    const float semis = p.get (modAmtParam (s)) * 24.f;
                    const float interval = std::abs (semis);
                    CHECK ((std::abs (interval - 5.f) < 0.05f || std::abs (interval - 7.f) < 0.05f || std::abs (interval - 12.f) < 0.05f));
                    CHECK (p.get (P::OscACoarse) == (semis < 0.f ? 12.f : 0.f));   // falls start an octave up
                }
            CHECK (slides == 1);
        }
}

TEST_CASE ("Generate and Mutate keep both quality settings", "[generator]")
{
    Generator g;
    ParamState current;
    current.set (P::Quality, 0.f);
    current.set (P::RenderQuality, 1.f);
    for (int c = 0; c < kNumCategories; ++c)
    {
        auto p = g.generate ((Category) c, 7, current, LockSet());
        CHECK (p.geti (P::Quality) == 0);
        CHECK (p.geti (P::RenderQuality) == 1);
        p = g.mutate (1.f, 11, p, LockSet());
        CHECK (p.geti (P::Quality) == 0);
        CHECK (p.geti (P::RenderQuality) == 1);
    }
}

TEST_CASE ("No arp category generates a one- or two-note pattern more than now and then", "[generator][arp]")
{
    Generator g;
    for (Category c : categoryDisplayOrder())
    {
        if (! hasArp (c)) continue;
        int lively = 0;
        constexpr int kSeeds = 100;
        for (uint32_t seed = 1; seed <= kSeeds; ++seed)
        {
            const auto p = ArpPattern::from (g.generate (c, seed * 7919u, ParamState(), LockSet(), true));
            std::set<int> played;
            for (int i = 0; i < p.length; ++i)
                if (p.has (i, ArpPattern::On)) played.insert (p.notes[(size_t) i]);
            lively += played.size() >= 3 ? 1 : 0;
        }
        INFO (categoryInfo()[(size_t) c].name);
        CHECK (lively >= kSeeds * 9 / 10);
    }
}

TEST_CASE ("Every arp category writes many different rhythms and none of them most of the time", "[generator][arp]")
{
    // drones play every step by design: their variety is in the notes
    Generator g;
    for (Category c : categoryDisplayOrder())
    {
        if (! hasArp (c)) continue;
        constexpr int kSeeds = 100;
        std::map<std::string, int> rhythms, lines;
        for (uint32_t seed = 1; seed <= kSeeds; ++seed)
        {
            const auto st = g.generate (c, seed, ParamState(), LockSet(), true);
            const auto p = ArpPattern::from (st);
            std::string r = std::to_string (st.geti (P::ArpDiv)) + "/" + std::to_string (p.length) + ":";
            std::string l = r;
            for (int i = 0; i < p.length; ++i)
            {
                r += p.has (i, ArpPattern::On) ? (p.has (i, ArpPattern::Slide) ? 's' : 'x') : '-';
                l += p.has (i, ArpPattern::On) ? std::to_string (p.notes[(size_t) i]) + "," : "-,";
            }
            ++rhythms[r];
            ++lines[l];
        }
        int top = 0;
        for (const auto& [r, n] : rhythms) top = std::max (top, n);
        INFO (categoryInfo()[(size_t) c].name << ": " << rhythms.size() << " rhythms, the commonest in " << top);
        CHECK ((int) lines.size() >= kSeeds * 3 / 4);
        if (c == Category::Drone) continue;
        CHECK ((int) rhythms.size() >= 15);
        CHECK (top <= kSeeds / 5);
    }
}

TEST_CASE ("A key set for the arp keeps through Generate and writes lines in its scale", "[generator][arp]")
{
    Generator g;
    for (int scale = 0; scale < kNumArpScales; ++scale)
    {
        ParamState current;
        current.set (P::ArpKey, 7.f);   // F#
        current.set (P::ArpScale, (float) scale);
        const auto& steps = arpScaleSteps (scale);
        for (Category c : { Category::Acid, Category::PsyLead, Category::Pluck, Category::Atmos, Category::Gate, Category::Stab })
            for (uint32_t seed = 1; seed <= 20; ++seed)
            {
                const auto p = g.generate (c, seed, current, LockSet(), true);
                INFO (categoryInfo()[(size_t) c].name << " scale " << scale << " seed " << seed);
                CHECK (p.geti (P::ArpKey) == 7);
                CHECK (p.geti (P::ArpScale) == scale);
                const auto a = ArpPattern::from (p);
                for (int i = 0; i < a.length; ++i)
                    if (a.has (i, ArpPattern::On))
                        CHECK (std::find (steps.begin(), steps.end(), ((a.notes[(size_t) i] % 12) + 12) % 12) != steps.end());
            }
        // a new pattern alone keeps the scale too
        const auto base = g.generate (Category::Acid, 3, current, LockSet(), true);
        const auto next = ArpPattern::from (g.generateArpPattern (Category::Acid, 99, base, LockSet()));
        for (int i = 0; i < next.length; ++i)
            if (next.has (i, ArpPattern::On))
                CHECK (std::find (steps.begin(), steps.end(), ((next.notes[(size_t) i] % 12) + 12) % 12) != steps.end());
    }
}

TEST_CASE ("Generated macros start part way along; basses keep their space macros at zero", "[generator][macro]")
{
    Generator g;
    int synthTurned = 0, synthTotal = 0;
    for (Category c : categoryDisplayOrder())
    {
        if (familyOf (c) == Family::Kick) continue;
        for (uint32_t seed = 1; seed <= 12; ++seed)
        {
            const auto p = g.generate (c, seed * 31u, ParamState(), LockSet(), false);
            INFO (categoryInfo()[(size_t) c].name << " seed " << seed);
            for (int m = 0; m < 4; ++m)
            {
                const float v = p.get ((P) ((int) P::Macro1 + m));
                CHECK (v >= 0.f);
                CHECK (v <= 0.7f);
                const auto label = macroLabel (p, m);
                if (familyOf (c) == Family::Bass && (label == "Reverb" || label == "Delay" || label == "Chorus"))
                    CHECK (v == 0.f);
                if (familyOf (c) == Family::Synth) { synthTurned += v > 0.f ? 1 : 0; ++synthTotal; }
            }
        }
    }
    CHECK (synthTurned > synthTotal / 2);
}

TEST_CASE ("Leads and pads get unison now and then, basses never", "[generator][unison]")
{
    Generator g;
    int leads = 0, pads = 0;
    for (uint32_t seed = 1; seed <= 40; ++seed)
    {
        const auto lead = g.generate (Category::PsyLead, seed, ParamState(), LockSet(), false);
        const auto pad = g.generate (Category::Atmos, seed, ParamState(), LockSet(), false);
        leads += lead.geti (P::OscAUnison) > 1 || lead.geti (P::OscBUnison) > 1 ? 1 : 0;
        pads += pad.geti (P::OscAUnison) > 1 || pad.geti (P::OscBUnison) > 1 ? 1 : 0;
        for (auto c : { Category::PsyBass, Category::ProgBass, Category::OffbeatBass, Category::BassShot, Category::ReverseBass, Category::ReverseSub,
                        Category::GhostBass })
        {
            const auto b = g.generate (c, seed, ParamState(), LockSet(), false);
            CHECK (b.geti (P::OscAUnison) == 1);
            CHECK (b.geti (P::OscBUnison) == 1);
            const auto m = g.mutate (1.f, seed, b, LockSet());
            CHECK (m.geti (P::OscAUnison) == 1);
        }
    }
    CHECK (leads >= 10);
    CHECK (pads >= 15);
}

TEST_CASE ("Jilax shots last a beat or two, cut at the end, high-passed in the bass register and clipped last", "[generator][jilax]")
{
    Generator g;
    const int n = 200;
    int oneBeat = 0, bassRoot = 0, highPassed = 0, clippedLast = 0;
    for (uint32_t seed = 1; seed <= (uint32_t) n; ++seed)
    {
        INFO ("seed " << seed);
        const auto p = g.generate (Category::Jilax, seed, ParamState(), LockSet(), true);
        CHECK_FALSE (p.on (P::ArpOn));   // asked for, never given: a shot is one hit
        CHECK (p.geti (P::EngineMode) == (int) EngineMode::Synth);
        CHECK (p.on (P::EnergyCut));
        CHECK (p.on (P::EnergyOneShot));   // a tap plays the whole shot
        CHECK (energyBarsValue (p.geti (P::EnergyBars)) <= 1.f);
        CHECK (p.geti (P::Octave) <= 0);
        oneBeat += p.geti (P::EnergyBars) == 0 ? 1 : 0;
        bassRoot += p.geti (P::Octave) == -2 ? 1 : 0;
        const int f2 = p.geti (P::F2Type);
        highPassed += (f2 == (int) FilterType::HP12 || f2 == (int) FilterType::HP18 || f2 == (int) FilterType::HP24) ? 1 : 0;
        if (p.on (P::DistOn) && p.on (P::DistAuto) && fxChain (p.geti (P::FxOrder)).back() == (int) Fx::Distortion)
            ++clippedLast;
    }
    CHECK (hasLength (Category::Jilax));
    CHECK_FALSE (hasArp (Category::Jilax));
    CHECK (familyOf (Category::Jilax) == Family::Synth);
    CHECK (oneBeat > n / 2);              // most shots are one beat
    CHECK (bassRoot > n * 6 / 10);        // most roots two octaves under the key
    CHECK (highPassed > n * 3 / 4);       // and the fundamental taken out
    CHECK (clippedLast > n * 3 / 4);      // a clipper at the end of the chain
}

TEST_CASE ("Ghost stabs start off the beat and stop before the next kick", "[generator][ghost]")
{
    Generator g;
    const int n = 200;
    int widened = 0, stacked = 0;
    for (uint32_t seed = 1; seed <= (uint32_t) n; ++seed)
    {
        INFO ("seed " << seed);
        const auto p = g.generate (Category::GhostStab, seed, ParamState(), LockSet(), true);
        bool cutoffMacro = false;   // a cutoff macro takes the knob down to keep the sound where the recipe drew it
        for (int m = 0; m < 4; ++m) cutoffMacro = cutoffMacro || macroSlot (p, m, ModDst::F2Cutoff) >= 0;
        REQUIRE (p.on (P::ArpOn));
        const int div = p.geti (P::ArpDiv);
        REQUIRE ((div == 9 || div == 12));   // 1/8 or 1/16
        const int perBeat = div == 9 ? 2 : 4;
        const float gate = p.get (P::ArpGate);
        CHECK (gate < 1.f);   // only the ties hold a stab over a step
        const auto pat = ArpPattern::from (p);
        // every stab: from its first step through its ties to the step that ends it
        for (int i = 0; i < pat.length; ++i)
        {
            if (! pat.has (i, ArpPattern::On) || (i > 0 && pat.has (i - 1, ArpPattern::Slide))) continue;
            CHECK (i % perBeat != 0);   // never on the kick
            int end = i;
            while (pat.has (end, ArpPattern::Slide) && end + 1 < pat.length && pat.has (end + 1, ArpPattern::On))
            {
                CHECK (pat.notes[(size_t) end + 1] == pat.notes[(size_t) i]);   // a tie holds its chord
                ++end;
            }
            CHECK (end + gate < (float) ((end / perBeat + 1) * perBeat));   // gone before the next beat
        }
        CHECK (p.geti (P::Octave) <= 0);
        CHECK_FALSE (p.on (P::CompOn));
        CHECK (p.geti (P::F2Type) == (int) FilterType::HP24);
        if (! cutoffMacro) CHECK (p.get (P::F2Cutoff) >= 300.f);   // from C4: 240 Hz and up around G#3
        CHECK (p.on (P::OscBOn));
        CHECK (p.get (P::F2Key) == 1.f);
        widened += p.on (P::WidthOn) ? 1 : 0;
        stacked += p.geti (P::OscAUniStack) == (int) UniStack::OctFifth ? 1 : 0;
    }
    CHECK (hasArp (Category::GhostStab));
    CHECK_FALSE (hasLength (Category::GhostStab));
    CHECK (familyOf (Category::GhostStab) == Family::Synth);
    CHECK (widened > n * 3 / 4);   // the side about as loud as the mid
    CHECK (stacked > n / 2);       // a stack of fifths in most
}

TEST_CASE ("Ghost basses roll without a knock, their fundamental thinned for the kick", "[generator][ghost]")
{
    Generator g;
    for (uint32_t seed = 1; seed <= 200; ++seed)
    {
        INFO ("seed " << seed);
        const auto p = g.generate (Category::GhostBass, seed, ParamState(), LockSet(), true);
        CHECK_FALSE (p.on (P::ArpOn));
        CHECK (p.geti (P::VoiceMode) == (int) VoiceMode::Mono);
        CHECK (p.geti (P::Octave) == -1);
        CHECK (p.get (P::PenvAmt) == 0.f);
        CHECK (p.geti (P::F2Type) == (int) FilterType::HP12);
        CHECK (p.get (P::F2Cutoff) >= 55.f);
        CHECK (p.get (P::F2Cutoff) <= 78.f);
        CHECK (p.get (P::F1Cutoff) <= 190.f);
        CHECK (p.get (P::AenvA) >= 1.5f);   // no click at the note start
    }
    CHECK (familyOf (Category::GhostBass) == Family::Bass);
    CHECK (groupOf (Category::GhostBass) == CategoryGroup::Bass);
    CHECK_FALSE (hasArp (Category::GhostBass));
}

TEST_CASE ("Grids are short hits on a 16th arp that leaves most beats to the kick", "[generator][grid]")
{
    Generator g;
    const int n = 200;
    int offKick = 0, total = 0, highPassed = 0, varied = 0;
    for (uint32_t seed = 1; seed <= (uint32_t) n; ++seed)
    {
        INFO ("seed " << seed);
        const auto p = g.generate (Category::Grid, seed, ParamState(), LockSet(), true);
        REQUIRE (p.on (P::ArpOn));
        CHECK (p.geti (P::ArpDiv) == 12);   // 1/16
        CHECK (p.get (P::ArpGate) <= 0.5f);
        CHECK (p.get (P::AenvS) == 0.f);
        CHECK (p.get (P::AenvD) <= 220.f);
        CHECK (p.geti (P::Octave) <= 0);
        const auto pat = ArpPattern::from (p);
        CHECK ((pat.length == 16 || pat.length == 12));
        int hits = 0;
        for (int i = 0; i < pat.length; ++i)
            if (pat.has (i, ArpPattern::On))
            {
                ++hits;
                offKick += i % 4 != 0 ? 1 : 0;
            }
        for (int i = 0; i < pat.length; ++i)
            if (pat.has (i, ArpPattern::On))
            {
                CHECK (pat.notes[(size_t) i] >= -8);   // never down into the bass
                CHECK (pat.notes[(size_t) i] <= 23);   // an octave and a seventh up at most
            }
        CHECK (hits >= 3);
        CHECK (hits <= 12);   // rests make the groove
        total += hits;
        const int f2 = p.geti (P::F2Type);
        highPassed += (f2 == (int) FilterType::HP12 || f2 == (int) FilterType::HP24) ? 1 : 0;
        for (int i = 0; i < kNumModSlots; ++i)
        {
            const int src = p.geti (modSrcParam (i));
            if (src == (int) ModSrc::Random || src == (int) ModSrc::Lfo1) { ++varied; break; }
        }
    }
    CHECK (hasArp (Category::Grid));
    CHECK_FALSE (hasLength (Category::Grid));
    CHECK (offKick > total * 7 / 10);   // mostly the "e", "and" and "a"
    CHECK (highPassed > n * 7 / 10);    // above the bass
    CHECK (varied > n * 6 / 10);        // each hit a little different
}

TEST_CASE ("Acid comes both ways: the classic wet 303 and the psy kind, cut and driven", "[generator][acid]")
{
    Generator g;
    const int n = 300;
    int psy = 0, goa = 0;
    double steps[2] = {}, played[2] = {}, up[2] = {};
    for (uint32_t seed = 1; seed <= (uint32_t) n; ++seed)
    {
        INFO ("seed " << seed);
        const auto p = g.generate (Category::Acid, seed, ParamState(), LockSet(), true);
        const int f2 = p.geti (P::F2Type);
        const bool cut = f2 == (int) FilterType::HP12 || f2 == (int) FilterType::HP18 || f2 == (int) FilterType::HP24;
        const auto pat = ArpPattern::from (p);
        for (int i = 0; i < pat.length; ++i)
            if (pat.has (i, ArpPattern::On))
            {
                played[cut] += 1.0;
                up[cut] += pat.notes[(size_t) i] >= 12 ? 1.0 : 0.0;
            }
        steps[cut] += pat.length;
        CHECK (p.geti (P::ArpDiv) == 12);   // 1/16 either way
        if (! cut)
        {
            // the classic: a squelchy resonant 303 with its lows left in
            CHECK (f2 == (int) FilterType::Off);
            CHECK (p.get (P::F1Reso) >= 0.55f);
            continue;
        }
        ++psy;
        CHECK (p.on (P::DistOn));            // always driven
        CHECK (p.geti (P::FRouting) == 0);   // the cut before the clipper
        CHECK (p.get (P::F1Reso) <= 0.8f);
        CHECK (p.get (P::ArpSwing) <= 0.04f + 1e-4f);   // straight
        if (p.on (P::DistAuto))
        {
            CHECK (p.get (P::DistMix) >= 0.85f);
            CHECK (p.on (P::CompOn));   // its makeup after the clipper
            CHECK (p.get (P::CompOut) >= 2.f);
        }
        else ++goa;
    }
    CHECK (psy > n * 4 / 10);   // a good mix of both
    CHECK (psy < n * 7 / 10);
    CHECK (goa > psy / 6);      // and some of the psy ones the softer Goa kind
    CHECK (goa < psy / 2);
    // the psy line rolls on and leaps an octave or two off its pedal
    CHECK (played[1] / steps[1] > played[0] / steps[0] + 0.08);
    CHECK (up[1] / played[1] > 0.4);
    CHECK (up[1] / played[1] > 2.0 * up[0] / played[0]);
}

// ---- review fixes ---------------------------------------------------------

TEST_CASE ("Every short name is different and fits a chip", "[generator][category]")
{
    std::set<std::string> seen;
    for (const auto& info : categoryInfo())
    {
        CHECK (std::string_view (info.shortName).size() <= 8);
        CHECK (seen.insert (info.shortName).second);
    }
}

TEST_CASE ("Every choice list fits the options a recipe remembers", "[generator]")
{
    for (const auto& d : paramDefs())
        if (d.mapping == Mapping::Choice && d.choices != &choices::modDst)   // the matrix targets are set, never picked
            CHECK ((int) d.max + 1 <= kMaxChoiceOptions);
}

TEST_CASE ("Grid chord stabs stack root, octave and fifth with enough voices to play the fifth", "[generator][grid]")
{
    Generator g;
    int stacks = 0;
    for (uint32_t seed = 0; seed < 300; ++seed)
    {
        const auto p = g.generate (Category::Grid, seed, ParamState(), LockSet());
        if (p.geti (P::OscAUniStack) != (int) UniStack::OctFifth) continue;
        ++stacks;
        // the stack puts the fifth on the second pair outside the centre: 5 voices
        CHECK (p.geti (P::OscAUnison) >= 5);
    }
    CHECK (stacks > 20);
}

TEST_CASE ("Locking Jilax's second filter type does not reshuffle the other parameters", "[generator][jilax]")
{
    Generator g;
    for (uint32_t seed = 0; seed < 300; ++seed)
    {
        const auto free = g.generate (Category::Jilax, seed, ParamState(), LockSet());
        LockSet locks;
        locks.set ((size_t) P::F2Type);
        const auto locked = g.generate (Category::Jilax, seed, free, locks);
        for (int i = 0; i < kNumParams; ++i)
            CHECK (free.v[(size_t) i] == locked.v[(size_t) i]);
    }
}

TEST_CASE ("A cutoff macro on a dark Phoscyon patch keeps the recipe's brightness and Mutate can move the cutoff", "[generator][macro][phoscyon]")
{
    int parked = 0, movable = 0;
    float mean = 0.f;
    for (uint32_t seed = 0; seed < 200; ++seed)
    {
        Generator g;
        const auto p = g.generate (Category::Phoscyon, seed, ParamState(), LockSet());
        for (int m = 0; m < 4; ++m)
        {
            const int slot = macroSlot (p, m, ModDst::PhosCutoff);
            if (slot < 0) continue;
            const float knob = p.get (P::PhosCutoff);
            const float effective = knob + p.get (modAmtParam (slot)) * p.get ((P) ((int) P::Macro1 + m));
            // the recipe drew at most 0.75; the macro brightens a synth by up to 1/0.6 of what it takes off
            CHECK (effective <= 0.75f / 0.6f + 1e-3f);
            if (knob == 0.f)
            {
                ++parked;
                // the knob gave way to the macro: it lands where the recipe drew (a dark or
                // mid cutoff, below 0.35), scaled up by the share a synth may brighten by
                CHECK (effective <= 0.35f / 0.6f + 1e-3f);
                mean += effective;
                if (g.lastRanges().e[(size_t) P::PhosCutoff].hi > 0.f) ++movable;
            }
        }
    }
    CHECK (parked > 20);
    CHECK (movable == parked);
    CHECK (mean / (float) parked < 0.15f);   // a dark recipe stays dark (the macro used to open it past 0.3)
}

TEST_CASE ("No macro drives the resonance of a 6 dB filter", "[generator][macro]")
{
    Generator g;
    for (Category c : { Category::ReverseBass, Category::GhostStab, Category::ReverseSub })
        for (uint32_t seed = 0; seed < 150; ++seed)
        {
            const auto p = g.generate (c, seed, ParamState(), LockSet());
            for (int m = 0; m < 4; ++m)
                if (macroSlot (p, m, ModDst::F1Reso) >= 0)
                {
                    CHECK (p.geti (P::F1Type) != (int) FilterType::LP6);
                    CHECK (p.geti (P::F1Type) != (int) FilterType::HP6);
                }
        }
}

TEST_CASE ("Grid's macro 1 always drives something audible", "[generator][grid][macro]")
{
    Generator g;
    for (uint32_t seed = 0; seed < 300; ++seed)
    {
        const auto p = g.generate (Category::Grid, seed, ParamState(), LockSet());
        if (macroSlot (p, 0, ModDst::FilterEnvAmt) >= 0)
            CHECK (std::abs (p.get (P::F1Env)) > 0.05f);
    }
}

TEST_CASE ("Atmos scans a wavetable position only when Osc A is a wavetable", "[generator]")
{
    Generator g;
    for (uint32_t seed = 0; seed < 100; ++seed)
    {
        const auto p = g.generate (Category::Atmos, seed, ParamState(), LockSet());
        bool lfoToWt = false;
        for (int i = 0; i < kNumModSlots; ++i)
            if (p.geti (modSrcParam (i)) == (int) ModSrc::Lfo2 && p.geti (modDstParam (i)) == (int) ModDst::WtPosA)
                lfoToWt = true;
        if (p.geti (P::OscAType) == (int) OscType::Supersaw) CHECK (! lfoToWt);
        else CHECK (lfoToWt);
    }
}

TEST_CASE ("Mutate keeps to the options a recipe allowed and reaches integer parameters", "[generator]")
{
    int moved = 0, unisonPatches = 0;
    for (uint32_t seed = 0; seed < 100; ++seed)
    {
        Generator g;
        const auto p = g.generate (Category::PsyLead, seed, ParamState(), LockSet());
        if (p.geti (P::OscAUnison) > 1) ++unisonPatches;
        for (uint32_t m = 1; m <= 20; ++m)
        {
            const auto q = g.mutate (1.f, m, p, LockSet());
            // PsyLead's unison mode is Linear or Super, never Random (weight 0)
            if (p.geti (P::OscAUnison) > 1)
                CHECK (q.geti (P::OscAUniMode) != (int) UniMode::Random);
            if (q.geti (P::PolyVoices) != p.geti (P::PolyVoices)) ++moved;
        }
    }
    CHECK (unisonPatches > 20);
    CHECK (moved > 0);
}

TEST_CASE ("Locking one parameter does not reshuffle what Mutate does to the others", "[generator]")
{
    Generator g;
    const auto p = g.generate (Category::Atmos, 5, ParamState(), LockSet());
    for (uint32_t m = 1; m <= 30; ++m)
    {
        const auto free = g.mutate (1.f, m, p, LockSet());
        LockSet locks;
        locks.set ((size_t) P::OscAType);
        locks.set ((size_t) P::F1Type);
        const auto locked = g.mutate (1.f, m, p, locks);
        for (int i = 0; i < kNumParams; ++i)
            if (i != (int) P::OscAType && i != (int) P::F1Type)
                CHECK (free.v[(size_t) i] == locked.v[(size_t) i]);
    }
}
