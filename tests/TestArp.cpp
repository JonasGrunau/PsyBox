#include "engine/Arpeggiator.h"
#include "engine/Engine.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace psy;

namespace
{
    constexpr double kFs = 48000.0;

    struct Ev { int64_t t; bool on; int note; };

    // Drives the arp the way the engine does: sync per block, fire what is
    // due, then advance to the next event or the end of the block.
    std::vector<Ev> run (Arpeggiator& a, int64_t total, int block, double bpm = 120.0, bool playing = false, double ppq = 0.0)
    {
        std::vector<Ev> out;
        const double samplesPerBeat = 60.0 / bpm * kFs;
        for (int64_t start = 0; start < total; start += block)
        {
            const int n = (int) std::min<int64_t> (block, total - start);
            a.sync (ppq + (double) start / samplesPerBeat, bpm, playing);
            int pos = 0;
            while (pos < n)
            {
                a.fireDue ([&] (const ArpEvent& e) { out.push_back ({ start + pos, e.on, e.note }); });
                const int step = std::min (n - pos, a.samplesToNextEvent());
                a.advance (step);
                pos += step;
            }
        }
        return out;
    }

    std::vector<Ev> ons (const std::vector<Ev>& ev)
    {
        std::vector<Ev> o;
        for (const auto& e : ev)
            if (e.on) o.push_back (e);
        return o;
    }

    Arpeggiator make (ArpSettings s)
    {
        Arpeggiator a;
        a.prepare (kFs);
        a.reset();
        a.setSettings (s);
        return a;
    }

    constexpr int64_t kSixteenth = 6000;   // 1/16 at 120 BPM, 48 kHz
} // namespace

TEST_CASE ("Up mode walks the chord on the 16th grid", "[arp]")
{
    ArpSettings s;
    s.mode = (int) ArpMode::Up;
    s.gate = 0.5f;
    auto a = make (s);
    a.keyOn (67, 0.8f);
    a.keyOn (60, 0.8f);
    a.keyOn (64, 0.8f);
    const auto ev = run (a, kSixteenth * 4, 256);
    const auto o = ons (ev);
    REQUIRE (o.size() == 4);
    const int expected[] = { 60, 64, 67, 60 };
    for (size_t i = 0; i < 4; ++i)
    {
        CHECK (o[i].t == (int64_t) i * kSixteenth);
        CHECK (o[i].note == expected[i]);
    }
    // 50 % gate: each note ends half a step after it starts
    REQUIRE (ev.size() >= 2);
    CHECK_FALSE (ev[1].on);
    CHECK (ev[1].t == kSixteenth / 2);
}

TEST_CASE ("Order keeps the played order, octaves stack on top", "[arp]")
{
    ArpSettings s;
    s.mode = (int) ArpMode::Order;
    s.octaves = 2;
    auto a = make (s);
    a.keyOn (67, 0.8f);
    a.keyOn (60, 0.8f);
    const auto o = ons (run (a, kSixteenth * 4, 512));
    REQUIRE (o.size() == 4);
    CHECK (o[0].note == 67);
    CHECK (o[1].note == 60);
    CHECK (o[2].note == 79);
    CHECK (o[3].note == 72);
}

TEST_CASE ("Up/Down does not repeat the turning notes", "[arp]")
{
    ArpSettings s;
    s.mode = (int) ArpMode::UpDown;
    s.octaves = 3;
    auto a = make (s);
    a.keyOn (48, 0.8f);
    const auto o = ons (run (a, kSixteenth * 6, 333));
    REQUIRE (o.size() == 6);
    const int expected[] = { 48, 60, 72, 60, 48, 60 };
    for (size_t i = 0; i < 6; ++i)
        CHECK (o[i].note == expected[i]);
}

TEST_CASE ("Rolling leaves the beat to the kick when synced to the host", "[arp]")
{
    ArpSettings s;
    s.rhythm = (int) ArpRhythm::Rolling;
    auto a = make (s);
    a.keyOn (36, 0.8f);
    const auto o = ons (run (a, kSixteenth * 8, 256, 120.0, true, 0.0));
    REQUIRE (o.size() == 6);
    const int64_t expectedSteps[] = { 1, 2, 3, 5, 6, 7 };
    for (size_t i = 0; i < 6; ++i)
        CHECK (o[i].t == expectedSteps[i] * kSixteenth);
}

TEST_CASE ("Host sync starting mid-step waits for the grid and never doubles a step", "[arp]")
{
    ArpSettings s;
    auto a = make (s);
    a.keyOn (60, 0.8f);
    // 0.1 beats in: the next 1/16 is at 0.25 beats, 0.15 beats = 3600 samples away
    const auto o = ons (run (a, 24000 * 2, 97, 120.0, true, 0.1));
    REQUIRE (o.size() == 8);
    CHECK (o[0].t == 3600);
    for (size_t i = 1; i < o.size(); ++i)
        CHECK (o[i].t - o[i - 1].t == kSixteenth);
}

TEST_CASE ("Gates of 100 % or more tie: the voice slides to the next note", "[arp]")
{
    ArpSettings s;
    s.gate = 1.2f;
    auto a = make (s);
    a.keyOn (60, 0.8f);
    a.keyOn (63, 0.8f);
    std::vector<ArpEvent> ev;
    for (int pos = 0; pos < kSixteenth * 2 + 10; ++pos)
    {
        a.fireDue ([&] (const ArpEvent& e) { ev.push_back (e); });
        a.advance (1);
    }
    REQUIRE (ev.size() == 3);
    CHECK ((ev[0].on && ev[0].note == 60 && ev[0].slideFrom < 0));
    CHECK ((ev[1].on && ev[1].note == 63 && ev[1].slideFrom == 60));
    CHECK ((ev[2].on && ev[2].note == 60 && ev[2].slideFrom == 63));
}

TEST_CASE ("Engine: tied arp steps keep a slow attack swelling", "[arp][engine]")
{
    ParamState p;
    p.set (P::ArpOn, 1.f);
    p.set (P::ArpGate, 1.2f);
    p.set (P::ArpOctaves, 3.f);
    p.set (P::AenvA, 1000.f);
    p.set (P::AenvS, 1.f);
    Engine e;
    e.setParams (p);
    e.prepare (kFs, 256);
    std::vector<float> l (48000), r (48000);
    MidiEvent on { 0, MidiEvent::NoteOn, 48, 0.8f };
    for (int pos = 0; pos < 48000; pos += 256)
        e.process (l.data() + pos, r.data() + pos, 256, pos == 0 ? &on : nullptr, pos == 0 ? 1 : 0, 120.0);
    float pk = 0.f;
    for (int i = 40000; i < 48000; ++i) pk = std::max (pk, std::abs (l[(size_t) i]));
    CHECK (pk > 0.1f);   // retriggering every 1/16 would keep it near silence
    CHECK (e.activeVoices() == 1);
}

TEST_CASE ("Swing delays the odd steps", "[arp]")
{
    ArpSettings s;
    s.swing = 0.5f;
    auto a = make (s);
    a.keyOn (60, 0.8f);
    const auto o = ons (run (a, kSixteenth * 4, 256));
    REQUIRE (o.size() == 4);
    CHECK (o[0].t == 0);
    CHECK (o[1].t == kSixteenth + kSixteenth / 4);
    CHECK (o[2].t == 2 * kSixteenth);
    CHECK (o[3].t == 3 * kSixteenth + kSixteenth / 4);
}

TEST_CASE ("Releasing the keys stops the arp unless Hold is on", "[arp]")
{
    ArpSettings s;
    auto a = make (s);
    a.keyOn (60, 0.8f);
    run (a, 100, 64);
    a.keyOff (60);
    const auto after = run (a, kSixteenth * 3, 256);
    CHECK (ons (after).empty());
    CHECK_FALSE (a.isRunning());

    s.hold = true;
    auto h = make (s);
    h.keyOn (60, 0.8f);
    run (h, 100, 64);
    h.keyOff (60);
    CHECK (ons (run (h, kSixteenth * 3, 256)).size() == 3);
    // the next key after letting go replaces the latched set
    h.keyOn (65, 0.8f);
    const auto o = ons (run (h, kSixteenth * 2, 256));
    REQUIRE_FALSE (o.empty());
    for (const auto& e : o)
        CHECK (e.note == 65);
}

TEST_CASE ("Engine: with the arp on, rhythm rests are silent and steps play", "[arp][engine]")
{
    ParamState p;
    p.set (P::ArpOn, 1.f);
    p.set (P::ArpRhythm, (float) ArpRhythm::Offbeat);   // --x-
    p.set (P::AenvR, 5.f);
    p.set (P::ArpGate, 0.5f);
    Engine e;
    e.setParams (p);
    e.prepare (kFs, 256);

    std::vector<float> l (kSixteenth * 4), r (kSixteenth * 4);
    MidiEvent on { 0, MidiEvent::NoteOn, 48, 0.8f };
    for (int pos = 0; pos < (int) l.size(); pos += 256)
    {
        const int n = std::min (256, (int) l.size() - pos);
        e.process (l.data() + pos, r.data() + pos, n, pos == 0 ? &on : nullptr, pos == 0 ? 1 : 0, 120.0);
    }
    const auto rms = [&] (int64_t from, int64_t to) {
        double s = 0.0;
        for (int64_t i = from; i < to; ++i) s += (double) l[(size_t) i] * l[(size_t) i];
        return std::sqrt (s / (double) (to - from));
    };
    CHECK (rms (0, 2 * kSixteenth - 200) < 1.0e-4);                                // steps 0 and 1 rest
    CHECK (rms (2 * kSixteenth + 200, 2 * kSixteenth + kSixteenth / 2) > 0.01);    // step 2 plays
}

TEST_CASE ("Engine: turning the arp off hands the held key back to the voices", "[arp][engine]")
{
    ParamState p;
    p.set (P::ArpOn, 1.f);
    Engine e;
    e.setParams (p);
    e.prepare (kFs, 256);
    std::vector<float> l (256), r (256);
    MidiEvent on { 0, MidiEvent::NoteOn, 60, 0.8f };
    e.process (l.data(), r.data(), 256, &on, 1, 120.0);
    CHECK (e.activeVoices() == 1);

    p.set (P::ArpOn, 0.f);
    e.setParams (p);
    for (int b = 0; b < 100; ++b) e.process (l.data(), r.data(), 256, nullptr, 0, 120.0);
    CHECK (e.activeVoices() >= 1);   // the key is still down, so it keeps sounding
    float pk = 0.f;
    for (float v : l) pk = std::max (pk, std::abs (v));
    CHECK (pk > 0.05f);
}
