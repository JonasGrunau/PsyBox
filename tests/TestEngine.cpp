#include "TestUtil.h"
#include "engine/Engine.h"
#include "generator/Generator.h"

#include <catch2/catch_test_macros.hpp>

using namespace psy;

namespace
{
    struct Render
    {
        std::vector<float> l, r;
        float peak = 0.f;
        double rmsDb = -200.0;
    };

    // Plays a 16th-note pattern at 145 BPM (psytrance tempo) for `seconds`.
    Render play (const ParamState& state, double seconds, double fs = 48000.0, int block = 256, double gate = 0.6)
    {
        Engine e;
        e.setParams (state);
        e.prepare (fs, block);
        Render out;
        const int total = (int) (seconds * fs);
        out.l.assign ((size_t) total, 0.f);
        out.r.assign ((size_t) total, 0.f);
        const double sixteenth = fs * 60.0 / 145.0 / 4.0;
        static const int notes[] = { 36, 36, 48, 36, 39, 36, 43, 36 };
        int step = 0;
        double nextOn = 0.0, nextOff = -1.0;
        int playing = -1;
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            std::vector<MidiEvent> ev;
            while (nextOn < pos + n || (nextOff >= 0.0 && nextOff < pos + n))
            {
                if (nextOff >= 0.0 && nextOff <= nextOn && nextOff < pos + n)
                {
                    ev.push_back ({ std::max (0, (int) nextOff - pos), MidiEvent::NoteOff, playing, 0.f });
                    nextOff = -1.0;
                }
                else if (nextOn < pos + n)
                {
                    playing = notes[step % 8];
                    ev.push_back ({ std::max (0, (int) nextOn - pos), MidiEvent::NoteOn, playing, step % 4 == 2 ? 1.f : 0.7f });
                    nextOff = nextOn + sixteenth * gate;
                    nextOn += sixteenth;
                    ++step;
                }
                else break;
            }
            e.process (out.l.data() + pos, out.r.data() + pos, n, ev.data(), (int) ev.size(), 145.0);
        }
        double s = 0.0;
        for (size_t i = 0; i < out.l.size(); ++i)
        {
            REQUIRE (std::isfinite (out.l[i]));
            REQUIRE (std::isfinite (out.r[i]));
            out.peak = std::max ({ out.peak, std::abs (out.l[i]), std::abs (out.r[i]) });
            s += (double) out.l[i] * out.l[i];
        }
        out.rmsDb = 10.0 * std::log10 (s / (double) out.l.size() + 1e-20);
        return out;
    }
} // namespace

TEST_CASE ("Init patch makes sound on a note and goes silent after release", "[engine]")
{
    ParamState p;
    Engine e;
    e.setParams (p);
    e.prepare (48000.0, 512);
    std::vector<float> l (512), r (512);
    MidiEvent on { 0, MidiEvent::NoteOn, 60, 0.8f };
    e.process (l.data(), r.data(), 512, &on, 1, 120.0);
    float pk = 0.f;
    for (int b = 0; b < 20; ++b)
    {
        e.process (l.data(), r.data(), 512, nullptr, 0, 120.0);
        for (float v : l) pk = std::max (pk, std::abs (v));
    }
    CHECK (pk > 0.05f);
    MidiEvent off { 0, MidiEvent::NoteOff, 60, 0.f };
    e.process (l.data(), r.data(), 512, &off, 1, 120.0);
    for (int b = 0; b < 200; ++b) e.process (l.data(), r.data(), 512, nullptr, 0, 120.0);
    CHECK (e.activeVoices() == 0);
}

TEST_CASE ("Every category generates playable, finite, non-silent patches", "[engine][generator]")
{
    Generator g;
    ParamState current;
    LockSet locks;
    for (int c = 0; c < kNumCategories; ++c)
        for (uint32_t seed : { 1u, 77u, 4242u })
        {
            const ParamState p = g.generate ((Category) c, seed, current, locks);
            const auto cat = (Category) c;
            const bool sustained = cat == Category::Riser || cat == Category::Uplifter || cat == Category::Downlifter
                                || cat == Category::NoiseFx || cat == Category::Atmos || cat == Category::Drone;
            const Render out = play (p, sustained ? 6.0 : 3.0, 48000.0, 256, sustained ? 40.0 : 0.6);
            INFO (categoryInfo()[(size_t) c].name << " seed " << seed << ": peak " << out.peak << " rms " << out.rmsDb << " dB");
            CHECK (out.peak <= 1.0f);
            CHECK (out.rmsDb > -60.0);
        }
}

TEST_CASE ("All three quality settings render the same patch", "[engine]")
{
    Generator g;
    ParamState p = g.generate (Category::Acid, 5, ParamState(), LockSet());
    for (int q = 0; q < 3; ++q)
    {
        p.set (P::Quality, (float) q);
        const Render out = play (p, 1.0, 44100.0, 128);
        CHECK (out.rmsDb > -50.0);
        CHECK (out.peak <= 1.f);
    }
}

TEST_CASE ("Extreme parameter fuzzing never produces NaN or runaway output", "[engine][stress]")
{
    psy::dsp::Rng rng (99);
    const auto& defs = paramDefs();
    for (int trial = 0; trial < 12; ++trial)
    {
        ParamState p;
        for (int i = 0; i < kNumParams; ++i)
        {
            const float n = rng.chance (0.3f) ? (rng.chance (0.5f) ? 0.f : 1.f) : rng.uniform();
            p.v[(size_t) i] = defs[(size_t) i].snap (defs[(size_t) i].toPlain (n));
        }
        p.set (P::Quality, (float) (trial % 3));
        const Render out = play (p, 1.0);
        CHECK (out.peak <= 1.f);
    }
}
