#include "engine/Engine.h"
#include "generator/Loudness.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace psy;

TEST_CASE ("Level scales the output like Volume", "[engine][level]")
{
    const auto peak = [] (float levelDb) {
        ParamState p;
        p.set (P::PatchGain, levelDb);
        Engine e;
        e.setParams (p);
        e.prepare (48000.0, 256);
        std::vector<float> l (256), r (256);
        float pk = 0.f;
        for (int b = 0; b < 100; ++b)
        {
            const MidiEvent on { 0, MidiEvent::NoteOn, 60, 0.8f };
            e.process (l.data(), r.data(), 256, &on, b == 0 ? 1 : 0, 120.0);
            for (int i = 0; i < 256; ++i) pk = std::max ({ pk, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]) });
        }
        return pk;
    };
    const float ref = peak (0.f);
    REQUIRE (ref > 1e-3f);
    CHECK (std::abs (peak (-12.f) / ref - std::pow (10.f, -12.f / 20.f)) < 0.01f);
}

TEST_CASE ("Generated patches land on one loudness, whatever the category or seed", "[generator][level]")
{
    // before leveling the categories spread over 40 dB; afterwards every
    // audition plays at the target (Level is a plain gain after everything
    // else, so the patch plays at its measured loudness plus its Level)
    Generator g;
    const auto& d = def (P::PatchGain);
    for (Category c : categoryDisplayOrder())
        for (uint32_t seed : { 3u, 4242u })
            for (bool arp : { false, true })
            {
                if (arp && ! hasArp (c)) continue;
                INFO (categoryInfo()[(size_t) c].name << " seed " << seed << (arp ? " with arp" : ""));
                auto p = g.generate (c, seed, ParamState(), LockSet(), arp);
                levelPatch (p, c, LockSet());
                const float level = p.get (P::PatchGain);
                CHECK (level > d.min);   // in range: nothing needed more than Level has
                CHECK (level < d.max);
                CHECK (std::abs (auditionLoudness (p, c) + level - kTargetLoudness) < 0.1f);
            }
}

TEST_CASE ("Leveling is repeatable and leaves a locked Level alone", "[generator][level]")
{
    Generator g;
    auto a = g.generate (Category::Screech, 7, ParamState(), LockSet());
    auto b = a;
    levelPatch (a, Category::Screech, LockSet());
    levelPatch (b, Category::Screech, LockSet());
    CHECK (a.get (P::PatchGain) == b.get (P::PatchGain));
    CHECK (a.get (P::PatchGain) != 0.f);

    // the measurement ignores the Level and Volume it is given
    auto louder = a;
    louder.set (P::PatchGain, 10.f);
    louder.set (P::MasterVol, 3.f);
    CHECK (std::abs (auditionLoudness (louder, Category::Screech) - auditionLoudness (a, Category::Screech)) < 1e-3f);

    LockSet locks;
    locks.set ((size_t) P::PatchGain);
    auto locked = g.generate (Category::Screech, 7, ParamState(), LockSet());
    locked.set (P::PatchGain, -3.f);
    levelPatch (locked, Category::Screech, locks);
    CHECK (locked.get (P::PatchGain) == -3.f);
}
