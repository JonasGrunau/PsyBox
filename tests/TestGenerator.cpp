#include "generator/Generator.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string_view>

using namespace psy;

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
        const auto sweep = pattern (Category::Drone, seed);
        for (int i = 0; i < sweep.length; ++i)
            CHECK ((sweep.has (i, ArpPattern::On) && sweep.has (i, ArpPattern::Slide)));
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
    CHECK (familySize (Family::Bass) == 4);
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
    for (auto c : { Acid, PsyLead, Stab, Pluck }) CHECK (in (CategoryGroup::Main, c));
    CHECK (categoriesIn (CategoryGroup::Main).size() == 4);
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
        const int want = familyOf (cat) == Family::Kick ? (int) EngineMode::Kick : (int) EngineMode::Synth;
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
