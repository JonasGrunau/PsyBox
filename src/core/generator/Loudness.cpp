#include "Loudness.h"

#include "engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <future>
#include <vector>

namespace psy
{

namespace
{
    constexpr double kFs = 48000.0, kBpm = 145.0;
    constexpr int kBlock = 256;
    constexpr float kHeadroom = 24.f;   // measured this far down, so the output's soft clip never touches it
    constexpr float kSilence = -70.f;   // quieter than this is no sound (all sources off), not something to boost

    struct Biquad
    {
        double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0;

        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    // ITU-R BS.1770 K-weighting at 48 kHz: a shelf (+4 dB above ~1.5 kHz,
    // where the ear is most sensitive), then a high-pass at ~38 Hz.
    struct KWeighting
    {
        Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
        Biquad highPass { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };

        double process (double x) noexcept { return highPass.process (shelf.process (x)); }
    };

    struct Note { double beat, beats; };

    // The range a sound is played in: basses E1 to E3, acid and squelch
    // and Phoscyon lines C1 to C3, kicks on F1, everything else C2 to C5.
    std::vector<int> auditionKeys (Category c)
    {
        if (familyOf (c) == Family::Kick) return { 29 };
        if (familyOf (c) == Family::Bass) return { 28, 40, 52 };
        if (c == Category::Acid || c == Category::Squelch || isPhoscyon (c)) return { 24, 36, 48 };
        return { 36, 48, 60, 72 };
    }

    // The loudest 400 ms of `notes` (start, length in beats) on `key`, over
    // `beats`, K-weighted, in LUFS at Level and Volume 0 dB.
    float measure (ParamState p, const std::vector<Note>& notes, int key, double beats)
    {
        p.set (P::PatchGain, 0.f);
        p.set (P::Quality, 0.f);   // the gain differs <= 0.05 dB between qualities; Eco renders up to 2.8x faster
        p.set (P::MasterVol, -kHeadroom);

        const double spb = kFs * 60.0 / kBpm;
        std::vector<MidiEvent> all;
        for (const auto& n : notes)
        {
            all.push_back ({ (int) (n.beat * spb), MidiEvent::NoteOn, key, 0.8f });
            all.push_back ({ (int) ((n.beat + n.beats) * spb), MidiEvent::NoteOff, key, 0.f });
        }
        std::stable_sort (all.begin(), all.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.offset < b.offset; });

        Engine e;
        e.setParams (p);
        e.prepare (kFs, kBlock);

        // K-weighted power in 100 ms hops; a momentary window is four of them
        constexpr int kHop = (int) (kFs / 10);
        std::vector<double> hops;
        KWeighting kl, kr;
        double acc = 0.0;
        int inHop = 0;
        std::vector<float> l ((size_t) kBlock), r ((size_t) kBlock);
        std::vector<MidiEvent> due;
        size_t next = 0;
        const int total = (int) (beats * spb);
        for (int pos = 0; pos < total; pos += kBlock)
        {
            const int n = std::min (kBlock, total - pos);
            due.clear();
            while (next < all.size() && all[next].offset < pos + n)
            {
                auto m = all[next++];
                m.offset -= pos;
                due.push_back (m);
            }
            e.process (l.data(), r.data(), n, due.data(), (int) due.size(), Transport { kBpm, pos / spb, true });
            for (int i = 0; i < n; ++i)
            {
                const double a = kl.process (l[(size_t) i]), b = kr.process (r[(size_t) i]);
                acc += a * a + b * b;
                if (++inHop == kHop)
                {
                    hops.push_back (acc);
                    acc = 0.0;
                    inHop = 0;
                }
            }
        }

        double loudest = 0.0;
        for (size_t i = 3; i < hops.size(); ++i)
            loudest = std::max (loudest, (hops[i] + hops[i - 1] + hops[i - 2] + hops[i - 3]) / (4.0 * kHop));
        return (float) (-0.691 + 10.0 * std::log10 (std::max (loudest, 1.0e-12))) + kHeadroom;
    }
} // namespace

float auditionLoudness (const ParamState& patch, Category c)
{
    // Every run is independent, so they run side by side: a Generate waits
    // for about one of them.
    std::vector<std::future<float>> runs;
    const auto run = [&runs] (const ParamState& p, std::vector<Note> notes, int key, double beats) {
        runs.push_back (std::async (std::launch::async, [p, notes = std::move (notes), key, beats] { return measure (p, notes, key, beats); }));
    };

    const auto keys = auditionKeys (c);
    if (familyOf (c) == Family::Kick)
    {
        std::vector<Note> fourToTheFloor;
        for (int b = 0; b < 8; ++b) fourToTheFloor.push_back ({ (double) b, 0.25 });
        run (patch, fourToTheFloor, keys[0], 8.0);
    }
    else if (hasLength (c))
    {
        // the whole gesture, then a beat of tail; a long one shrinks to two bars
        ParamState p = patch;
        double length = p.geti (P::EnergyMode) == (int) EnergyMode::Bars ? energyBarsValue (p.geti (P::EnergyBars)) * 4.0
                                                                        : p.get (P::EnergySec) * kBpm / 60.0;
        if (length > 8.0)
        {
            p.set (P::EnergyMode, (float) EnergyMode::Bars);
            p.set (P::EnergyBars, 3.f);   // 2 bars
            length = 8.0;
        }
        for (int k : keys)
            run (p, { { 0.0, length } }, k, length + 1.0);
    }
    else
    {
        // The loudest way it is likely to be played, over its range (a sound
        // can get louder low, where a formant fills with harmonics, or high,
        // where the ear is keener): a held key (slow swells open fully), a
        // bar of 16ths (every short note fires its attack again), and the arp
        // running when it has one.
        std::vector<Note> sixteenths;
        for (int i = 0; i < 16; ++i) sixteenths.push_back ({ i * 0.25, 0.125 });
        ParamState noArp = patch;
        noArp.set (P::ArpOn, 0.f);
        for (int k : keys)
        {
            run (noArp, { { 0.0, 8.0 } }, k, 8.0);
            run (noArp, sixteenths, k, 4.0);
        }
        if (patch.on (P::ArpOn))
            run (patch, { { 0.0, 8.0 } }, keys[keys.size() / 2], 8.0);
    }

    float loudest = -120.f;
    for (auto& r : runs)
        loudest = std::max (loudest, r.get());
    return loudest;
}

void levelPatch (ParamState& patch, Category c, const LockSet& locks)
{
    if (locks.test ((size_t) P::PatchGain))
        return;
    const auto& d = def (P::PatchGain);
    const float loudness = auditionLoudness (patch, c);
    if (loudness < kSilence)   // nothing plays (every source off): keep the gain, don't boost silence by the maximum
        return;
    patch.set (P::PatchGain, std::clamp (kTargetLoudness - loudness, d.min, d.max));
}

} // namespace psy
