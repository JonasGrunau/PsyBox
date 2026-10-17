#include "engine/Engine.h"
#include "generator/Loudness.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
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
                CHECK (level <= d.max);   // a Jilax shot can sit right at the cap (the shortfall is a few hundredths of a dB)
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

TEST_CASE ("A patch with every source off keeps its Level instead of getting the maximum", "[generator][level]")
{
    ParamState p;
    p.set (P::OscAOn, 0.f);
    p.set (P::OscBOn, 0.f);
    p.set (P::SubLevel, 0.f);
    p.set (P::NoiseLevel, 0.f);
    p.set (P::RingLevel, 0.f);
    p.set (P::PatchGain, 5.f);
    CHECK (auditionLoudness (p, Category::PsyLead) < -70.f);
    levelPatch (p, Category::PsyLead, LockSet());
    CHECK (p.get (P::PatchGain) == 5.f);
}

namespace
{
    // BS.1770 K-weighting at 48 kHz, designed from the standard's analogue
    // prototypes (high shelf, then the RLB high-pass), independent of the
    // coefficients in Loudness.cpp.
    struct Bq
    {
        double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
        double run (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct KMeter
    {
        Bq shelf, rlb;
        KMeter()
        {
            constexpr double fs = 48000.0, pi = std::numbers::pi;
            {
                const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
                const double K = std::tan (pi * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
                const double a0 = 1.0 + K / Q + K * K;
                shelf = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                          2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
            }
            {
                const double f0 = 38.13547087602444, Q = 0.5003270373238773;
                const double K = std::tan (pi * f0 / fs);
                const double a0 = 1.0 + K / Q + K * K;
                rlb = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
            }
        }
        double run (double x) { return rlb.run (shelf.run (x)); }
    };

    using Notes = std::vector<std::pair<double, double>>;   // (beat, beats)

    // Plays `notes` on `key` the way the audition does and returns the loudest
    // 400 ms window (100 ms hop) in LUFS, with the patch's own Level included.
    double loudestWindow (const ParamState& patch, const Notes& notes, int key, double beats)
    {
        constexpr double fs = 48000.0, bpm = 145.0, headroom = 24.0;
        constexpr int block = 256;
        ParamState p = patch;
        p.set (P::MasterVol, (float) -headroom);   // the audition's own headroom, added back below

        const double spb = fs * 60.0 / bpm;
        std::vector<MidiEvent> all;
        for (const auto& n : notes)
        {
            all.push_back ({ (int) (n.first * spb), MidiEvent::NoteOn, key, 0.8f });
            all.push_back ({ (int) ((n.first + n.second) * spb), MidiEvent::NoteOff, key, 0.f });
        }
        std::stable_sort (all.begin(), all.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.offset < b.offset; });

        Engine e;
        e.setParams (p);
        e.prepare (fs, block);
        KMeter kl, kr;
        std::vector<float> l ((size_t) block), r ((size_t) block);
        std::vector<double> power;
        const int total = (int) (beats * spb);
        size_t next = 0;
        std::vector<MidiEvent> due;
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            due.clear();
            while (next < all.size() && all[next].offset < pos + n)
            {
                auto m = all[next++];
                m.offset -= pos;
                due.push_back (m);
            }
            e.process (l.data(), r.data(), n, due.data(), (int) due.size(), Transport { bpm, pos / spb, true });
            for (int i = 0; i < n; ++i)
            {
                const double a = kl.run (l[(size_t) i]), b = kr.run (r[(size_t) i]);
                power.push_back (a * a + b * b);
            }
        }
        double loudest = 0.0;
        const size_t win = 19200, hop = 4800;
        for (size_t s = 0; s + win <= power.size(); s += hop)
        {
            double acc = 0.0;
            for (size_t i = s; i < s + win; ++i) acc += power[i];
            loudest = std::max (loudest, acc / (double) win);
        }
        return -0.691 + 10.0 * std::log10 (std::max (loudest, 1e-12)) + headroom;
    }

    // The audition's ways of playing a sound, written out again here.
    double playedLoudness (const ParamState& p, Category c)
    {
        double loudest = -120.0;
        if (familyOf (c) == Family::Kick)
        {
            Notes hits;
            for (int b = 0; b < 8; ++b) hits.push_back ({ (double) b, 0.25 });
            return loudestWindow (p, hits, 29, 8.0);
        }
        const bool bass = familyOf (c) == Family::Bass;
        if (hasLength (c))
        {
            ParamState q = p;
            double length = q.geti (P::EnergyMode) == (int) EnergyMode::Bars ? energyBarsValue (q.geti (P::EnergyBars)) * 4.0
                                                                            : q.get (P::EnergySec) * 145.0 / 60.0;
            if (length > 8.0)
            {
                q.set (P::EnergyMode, (float) EnergyMode::Bars);
                q.set (P::EnergyBars, 3.f);
                length = 8.0;
            }
            for (int k : bass ? std::vector<int> { 28, 40, 52 } : std::vector<int> { 36, 48, 60, 72 })
                loudest = std::max (loudest, loudestWindow (q, { { 0.0, length } }, k, length + 1.0));
            return loudest;
        }
        const bool threeOhThree = c == Category::Acid || c == Category::Squelch || c == Category::Phoscyon;
        const std::vector<int> keys = bass ? std::vector<int> { 28, 40, 52 } : threeOhThree ? std::vector<int> { 24, 36, 48 }
                                                                                            : std::vector<int> { 36, 48, 60, 72 };
        Notes sixteenths;
        for (int i = 0; i < 16; ++i) sixteenths.push_back ({ i * 0.25, 0.125 });
        for (int k : keys)
        {
            loudest = std::max (loudest, loudestWindow (p, { { 0.0, 8.0 } }, k, 8.0));
            loudest = std::max (loudest, loudestWindow (p, sixteenths, k, 4.0));
        }
        return loudest;
    }
}

TEST_CASE ("A leveled patch measures the target on an independent BS.1770 meter", "[generator][level]")
{
    Generator g;
    for (Category c : { Category::Acid, Category::Phoscyon, Category::PsyLead, Category::Atmos, Category::PsyBass, Category::ProgBass,
                        Category::KickFullOn, Category::Jilax, Category::ReverseSub, Category::Riser, Category::Grid, Category::Gate })
        for (uint32_t seed : { 11u, 12u })
        {
            INFO (categoryInfo()[(size_t) c].name << " seed " << seed);
            auto p = g.generate (c, seed, ParamState(), LockSet());
            levelPatch (p, c, LockSet());
            CHECK (std::abs (playedLoudness (p, c) - (double) kTargetLoudness) < 0.5);
        }
}
