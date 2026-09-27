#include "generator/Generator.h"

#include <catch2/catch_test_macros.hpp>

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
