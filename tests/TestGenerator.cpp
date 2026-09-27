#include "generator/Generator.h"

#include <catch2/catch_test_macros.hpp>

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
    const auto p = g.generate (Category::RollingBass, 7, ParamState(), LockSet());
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

TEST_CASE ("Risers climb in pitch, uplifters sweep noise", "[generator]")
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
        CHECK (routesTo (riser, ModSrc::ModEnv, ModDst::PitchAll));
        CHECK (riser.get (P::NoiseLevel) <= 0.1f);

        const auto up = g.generate (Category::Uplifter, seed, ParamState(), LockSet());
        CHECK (up.get (P::NoiseLevel) >= 0.8f);
        CHECK ((up.geti (P::OscAOn) == 0 || up.get (P::OscALevel) <= 0.15f));
        CHECK_FALSE (routesTo (up, ModSrc::None, ModDst::PitchAll));
        CHECK (routesTo (up, ModSrc::ModEnv, ModDst::F1Cutoff));
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

TEST_CASE ("Arp generation is off unless asked for, and on for every category when it is", "[generator][arp]")
{
    Generator g;
    for (int c = 0; c < kNumCategories; ++c)
        for (uint32_t seed = 1; seed < 6; ++seed)
        {
            INFO ("category " << c << " seed " << seed);
            CHECK_FALSE (g.generate ((Category) c, seed, ParamState(), LockSet()).on (P::ArpOn));
            const auto p = g.generate ((Category) c, seed, ParamState(), LockSet(), true);
            CHECK (p.on (P::ArpOn));
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

    auto p = g.generate (Category::RollingBass, 3, ParamState(), LockSet(), true);
    for (uint32_t s = 0; s < 200; ++s)
    {
        p = g.mutate (1.f, s, p, LockSet());
        CHECK (p.on (P::ArpOn));
        CHECK (p.geti (P::ArpDiv) == 12);   // rolling basses stay on 1/16
        CHECK (p.get (P::ArpGate) <= 0.6f + 0.25f * 0.15f + 1e-3f);
    }
}

TEST_CASE ("Generated arp patterns change with the seed and keep each category's rules", "[generator][arp]")
{
    Generator g;
    const auto pattern = [&] (Category c, uint32_t seed) {
        return ArpPattern::from (g.generate (c, seed, ParamState(), LockSet(), true));
    };
    for (auto c : { Category::Acid, Category::Bubble, Category::RollingBass, Category::Pluck, Category::Zap })
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
        const auto p = pattern (Category::RollingBass, seed);
        for (int beat = 0; beat < 4; ++beat)
            CHECK_FALSE (p.has (beat * 4, ArpPattern::On));   // the kick owns the beat
        const auto sweep = pattern (Category::Riser, seed);
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
        const bool isPattern = i == (int) P::ArpLength || (i >= (int) P::ArpStep1 && i <= (int) P::ArpSlide16);
        if (! isPattern)
            CHECK (next.v[(size_t) i] == base.v[(size_t) i]);
    }
    CHECK (ArpPattern::from (g.generateArpPattern (Category::Acid, 77, base, LockSet())) == ArpPattern::from (next));
}

TEST_CASE ("Generated arp patterns are composed: figures repeat, slides land, accents stay sparse", "[generator][arp]")
{
    Generator g;
    for (int c = 0; c < kNumCategories; ++c)
        for (uint32_t seed = 1; seed < 40; ++seed)
        {
            const auto p = ArpPattern::from (g.generate ((Category) c, seed, ParamState(), LockSet(), true));
            INFO ("category " << c << " seed " << seed);
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
        // rolling bass: beats 1 and 3 carry the same figure (AAAA, AAAB, ABAB)
        const auto bass = ArpPattern::from (g.generate (Category::RollingBass, seed, ParamState(), LockSet(), true));
        for (int i = 0; i < 4; ++i)
            CHECK (bass.has (i, ArpPattern::On) == bass.has (i + 8, ArpPattern::On));

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

TEST_CASE ("Every category has a name, a short name and a blurb", "[generator]")
{
    for (const auto& info : categoryInfo())
    {
        REQUIRE (info.name != nullptr);
        REQUIRE (info.shortName != nullptr);
        REQUIRE (info.blurb != nullptr);
        CHECK (std::string_view (info.name).find ('/') == std::string_view::npos);   // names are preset folders
    }
    CHECK ((int) categoriesIn (Family::Synth).size() + (int) categoriesIn (Family::Kick).size() == kNumCategories);
    CHECK ((int) categoriesIn (Family::Kick).size() == kNumKickCategories);
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
                if (familyOf (cat) == Family::Synth && kickParam)
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

TEST_CASE ("Kick arps are four on the floor, never tied or accented", "[generator][kick][arp]")
{
    Generator g;
    for (Category c : categoriesIn (Family::Kick))
        for (uint32_t seed = 1; seed < 30; ++seed)
        {
            const auto p = g.generate (c, seed, ParamState(), LockSet(), true);
            const auto pattern = ArpPattern::from (p);
            INFO (categoryInfo()[(size_t) c].name << " seed " << seed);
            CHECK (p.get (P::ArpGate) < 1.f);
            for (int beat = 0; beat < 3; ++beat)
                CHECK (pattern.has (beat * 4, ArpPattern::On));
            for (int i = 0; i < kArpSteps; ++i)
            {
                CHECK_FALSE (pattern.has (i, ArpPattern::Slide));
                CHECK_FALSE (pattern.has (i, ArpPattern::Accent));
            }
        }
}
