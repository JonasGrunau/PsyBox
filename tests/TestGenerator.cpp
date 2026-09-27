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
