#include "engine/Arpeggiator.h"
#include "engine/Engine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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
    s.pattern = ArpPattern::parse ("-xxx");
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
    CHECK (pk > 0.05f);   // retriggering every 1/16 would keep it near silence
    CHECK (e.activeVoices() == 1);
}

namespace
{
    // Every event with its sample time, stepping one sample at a time.
    std::vector<std::pair<int64_t, ArpEvent>> trace (Arpeggiator& a, int64_t total)
    {
        std::vector<std::pair<int64_t, ArpEvent>> out;
        for (int64_t t = 0; t < total; ++t)
        {
            a.fireDue ([&] (const ArpEvent& e) { out.push_back ({ t, e }); });
            a.advance (1);
        }
        return out;
    }
} // namespace

TEST_CASE ("Pattern accents own the velocity", "[arp]")
{
    ArpSettings s;
    s.pattern = ArpPattern::parse ("xX");
    auto a = make (s);
    a.keyOn (60, 0.9f);
    std::vector<float> vel;
    for (const auto& [t, e] : trace (a, kSixteenth * 4))
        if (e.on) vel.push_back (e.velocity);
    REQUIRE (vel.size() == 4);
    CHECK (vel[0] < 100.f / 127.f);   // under the engine's accent threshold
    CHECK (vel[1] == 1.f);
    CHECK (vel[2] < 100.f / 127.f);
    CHECK (vel[3] == 1.f);
}

TEST_CASE ("A slide step holds its note and glides into the next one", "[arp]")
{
    ArpSettings s;
    s.gate = 0.5f;
    s.pattern = ArpPattern::parse ("sx");
    auto a = make (s);
    a.keyOn (60, 0.8f);
    a.keyOn (63, 0.8f);
    const auto ev = trace (a, kSixteenth * 2 - 1);
    REQUIRE (ev.size() == 3);
    CHECK ((ev[0].first == 0 && ev[0].second.on && ev[0].second.note == 60));
    CHECK ((ev[1].first == kSixteenth && ev[1].second.slideFrom == 60 && ev[1].second.note == 63));
    CHECK ((ev[2].first == kSixteenth + kSixteenth / 2 && ! ev[2].second.on));   // the plain step keeps its gate
}

TEST_CASE ("The pattern loops over its length, not over 16", "[arp]")
{
    ArpSettings s;
    s.pattern = ArpPattern::parse ("x-x");
    s.pattern.length = 3;
    auto a = make (s);
    a.keyOn (60, 0.8f);
    const auto o = ons (run (a, kSixteenth * 7, 256));
    const int64_t expected[] = { 0, 2, 3, 5, 6 };
    REQUIRE (o.size() == 5);
    for (size_t i = 0; i < 5; ++i)
        CHECK (o[i].t == expected[i] * kSixteenth);
    CHECK (a.currentStep() == 0);
}

TEST_CASE ("Arp patterns survive a trip through the parameters", "[arp][params]")
{
    auto p = ArpPattern::parse ("Xxs--S-x");
    p.length = 11;
    p.notes = { 0, 12, -5, 3, 0, 24, -24, 7, 0, 0, 1, 0, 0, 0, 0, -1 };
    ParamState st;
    p.writeTo (st);
    CHECK (ArpPattern::from (st) == p);
    CHECK (ArpPattern::from (ParamState()) == ArpPattern::parse ("x"));   // default: every step plays the key
}

TEST_CASE ("The note lane moves each step off the key, one key plays a line", "[arp]")
{
    ArpSettings s;
    s.pattern = ArpPattern::parse ("xxx-");
    s.pattern.notes = { 0, 7, 12, 5, 3, -2, 0, 0 };
    s.pattern.length = 8;
    auto a = make (s);
    a.keyOn (60, 0.8f);
    const auto o = ons (run (a, kSixteenth * 8, 256));
    REQUIRE (o.size() == 6);
    const int expected[] = { 60, 67, 72, 63, 58, 60 };   // the fourth step rests, its +5 never sounds
    for (size_t i = 0; i < 6; ++i)
        CHECK (o[i].note == expected[i]);

    // the lane adds to whatever the mode picked, clamped to MIDI
    s.pattern = ArpPattern::parse ("x");
    s.pattern.notes.fill (24);
    s.mode = (int) ArpMode::Up;
    auto b = make (s);
    b.keyOn (104, 0.8f);
    b.keyOn (110, 0.8f);
    for (const auto& e : ons (run (b, kSixteenth * 4, 256)))
        CHECK (e.note == 127);
}

TEST_CASE ("A slide glides to the next step's note on the lane", "[arp]")
{
    ArpSettings s;
    s.pattern = ArpPattern::parse ("sx--");
    s.pattern.notes = { 0, 7 };
    auto a = make (s);
    a.keyOn (48, 0.8f);
    std::vector<ArpEvent> ev;
    a.sync (0.0, 120.0, false);
    for (int64_t t = 0; t < kSixteenth * 2; ++t)
    {
        a.fireDue ([&] (const ArpEvent& e) { ev.push_back (e); });
        a.advance (1);
    }
    REQUIRE (ev.size() >= 2);
    CHECK (ev[0].on);
    CHECK (ev[0].note == 48);
    CHECK (ev[1].on);
    CHECK (ev[1].note == 55);
    CHECK (ev[1].slideFrom == 48);   // tied: the voice moves, no retrigger
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
    ArpPattern::parse ("--x-").writeTo (p);
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

TEST_CASE ("With a key set the arp plays every note in its scale", "[arp]")
{
    ArpSettings s;
    s.pattern = ArpPattern::parse ("x");
    for (int i = 0; i < kArpSteps; ++i) s.pattern.notes[(size_t) i] = (int8_t) i;   // every semitone
    s.key = 5;     // F
    s.scale = 0;   // Phrygian: F Gb Ab Bb C Db Eb
    const auto& steps = arpScaleSteps (0);
    for (int held : { 53, 55, 60 })   // F, G (not in the scale), C
    {
        auto a = make (s);
        a.keyOn (held, 0.8f);
        const auto o = ons (run (a, kSixteenth * 16, 256));
        REQUIRE (o.size() == 16);
        for (const auto& e : o)
        {
            INFO ("held " << held << " note " << e.note);
            CHECK (std::find (steps.begin(), steps.end(), ((e.note - 5) % 12 + 12) % 12) != steps.end());
        }
    }
    // in the scale already: untouched; out of it: the nearest scale note, down on a tie
    CHECK (quantizeToScale (65, 5, 0) == 65);
    CHECK (quantizeToScale (67, 5, 0) == 66);   // G -> Gb
    CHECK (quantizeToScale (69, 5, 0) == 68);   // A -> Ab

    // without a key the notes pass as they are
    s.key = -1;
    auto free = make (s);
    free.keyOn (55, 0.8f);
    const auto o = ons (run (free, kSixteenth * 16, 256));
    REQUIRE (o.size() == 16);
    for (size_t i = 0; i < o.size(); ++i)
        CHECK (o[i].note == 55 + (int) i);
}

// ---------------------------------------------------------------------------
// Host sync, swing against long gates, hold, the order of settings and sync
// ---------------------------------------------------------------------------

namespace
{
    // A host that plays `bpm` and cuts its blocks the way hosts do. `seg` lists
    // where the transport jumps: { sample, ppq } from that sample on.
    struct Seg { int64_t at; double ppq; };

    std::vector<Ev> hostRun (Arpeggiator& a, int64_t total, double bpm, int maxBlock, bool randomBlocks, const std::vector<Seg>& seg = { { 0, 0.0 } })
    {
        std::vector<Ev> out;
        const double spb = 60.0 / bpm * kFs;
        uint32_t rs = 4242;
        for (int64_t start = 0; start < total;)
        {
            int b = maxBlock;
            if (randomBlocks)
            {
                rs = rs * 1664525u + 1013904223u;
                b = 1 + (int) ((rs >> 8) % (uint32_t) maxBlock);
            }
            // a block never straddles a jump
            int64_t end = std::min<int64_t> (total, start + b);
            for (const auto& s : seg)
                if (s.at > start)
                    end = std::min (end, s.at);
            const int n = (int) (end - start);
            const Seg* cur = &seg[0];
            for (const auto& s : seg)
                if (s.at <= start)
                    cur = &s;
            a.sync (cur->ppq + (double) (start - cur->at) / spb, bpm, true);
            int pos = 0;
            while (pos < n)
            {
                a.fireDue ([&] (const ArpEvent& e) { out.push_back ({ start + pos, e.on, e.note }); });
                const int step = std::min (n - pos, a.samplesToNextEvent());
                a.advance (step);
                pos += step;
            }
            start += n;
        }
        return out;
    }

    // Onsets in a percussive signal: the 2 ms peak rises through a quarter of
    // the loudest peak after at least 3 ms under a twentieth of it.
    int countOnsets (const std::vector<float>& x, int from, int to)
    {
        float top = 0.f;
        for (int i = from; i < to; ++i) top = std::max (top, std::abs (x[(size_t) i]));
        const float hi = 0.25f * top, lo = 0.05f * top;
        int count = 0, quiet = 1000;
        for (int i = from; i < to; ++i)
        {
            float m = 0.f;
            for (int k = std::max (0, i - 96); k <= i; ++k) m = std::max (m, std::abs (x[(size_t) k]));
            if (m < lo) ++quiet;
            else
            {
                if (m > hi && quiet >= 144) ++count;
                if (m > hi) quiet = 0;
            }
        }
        return count;
    }

    ParamState percussive()
    {
        ParamState p;
        p.set (P::ArpOn, 1.f);
        p.set (P::ArpGate, 0.5f);
        p.set (P::AenvA, 0.f);
        p.set (P::AenvD, 30.f);
        p.set (P::AenvS, 0.f);
        p.set (P::AenvR, 20.f);
        p.set (P::F1Type, 0.f);
        return p;
    }
} // namespace

TEST_CASE ("A host-synced arp fires every step exactly once, whatever the block size", "[arp][sync]")
{
    // 145 BPM: a 16th is 4965.517 samples, so steps fall between samples and
    // a block boundary lands within a sample of one now and then. The old
    // sync skipped a step that was a fraction of a sample behind the block
    // start: block 1 played 300 of 580 steps.
    const double bpm = 145.0;
    const double step = 60.0 / bpm * kFs / 4.0;
    const int64_t total = (int64_t) (kFs * 60.0);
    const int expected = (int) std::floor (((double) total - 2.0) / step) + 1;
    for (const auto& [block, random] : std::vector<std::pair<int, bool>> { { 1, false }, { 2, false }, { 37, false }, { 64, true }, { 441, false }, { 4096, false } })
    {
        ArpSettings s;
        auto a = make (s);
        a.keyOn (60, 0.8f);
        const auto o = ons (hostRun (a, total, bpm, block, random));
        INFO ("block " << block << (random ? " (random 1.." : " (fixed") << ")");
        REQUIRE ((int) o.size() == expected);
        for (size_t k = 0; k < o.size(); ++k)
            REQUIRE (std::abs ((double) o[k].t - (double) k * step) <= 1.0);
    }
}

TEST_CASE ("A host-synced arp follows a loop or a seek and keeps the grid", "[arp][sync]")
{
    const double bpm = 145.0;
    const double spb = 60.0 / bpm * kFs;
    // plays 2.1 beats, loops back to beat 1, plays on, then seeks 9.9 beats ahead (off the grid)
    const std::vector<Seg> seg { { 0, 0.0 }, { (int64_t) (2.1 * spb), 1.0 }, { (int64_t) (5.0 * spb), 14.9 } };
    const int64_t total = (int64_t) (9.0 * spb);
    for (int block : { 1, 37, 4096 })
    {
        ArpSettings s;
        auto a = make (s);
        a.keyOn (60, 0.8f);
        const auto o = ons (hostRun (a, total, bpm, block, false, seg));
        std::vector<double> want;
        for (size_t i = 0; i < seg.size(); ++i)
        {
            const int64_t from = seg[i].at, to = i + 1 < seg.size() ? seg[i + 1].at : total;
            const double ppq0 = seg[i].ppq, ppq1 = ppq0 + (double) (to - from) / spb;
            for (int q = (int) std::ceil (ppq0 * 4.0 - 1.0e-9); (double) q * 0.25 < ppq1 - 0.01; ++q)
                want.push_back ((double) from + ((double) q * 0.25 - ppq0) * spb);
        }
        INFO ("block " << block);
        REQUIRE (o.size() == want.size());
        for (size_t k = 0; k < o.size(); ++k)
            CHECK (std::abs ((double) o[k].t - want[k]) <= 1.0);
    }
}

TEST_CASE ("Engine: a host-synced arp keeps every step with any block size", "[arp][sync][engine]")
{
    // A short percussive note per step: count the onsets in the audio.
    const ParamState p = percussive();
    const double bpm = 145.0, spb = 60.0 / bpm * kFs, step = spb / 4.0;
    const int total = (int) (kFs * 6.0);
    const int expected = (int) std::floor (((double) total - 6000.0) / step) + 1;
    for (const auto& [block, random] : std::vector<std::pair<int, bool>> { { 1, false }, { 7, false }, { 64, true }, { 4096, false } })
    {
        Engine e;
        e.setParams (p);
        e.prepare (kFs, std::max (block, 16));
        std::vector<float> l ((size_t) total), r ((size_t) total);
        uint32_t rs = 777;
        for (int pos = 0; pos < total;)
        {
            int b = block;
            if (random)
            {
                rs = rs * 1664525u + 1013904223u;
                b = 1 + (int) ((rs >> 8) % (uint32_t) block);
            }
            const int n = std::min (b, total - pos);
            MidiEvent on { 0, MidiEvent::NoteOn, 60, 0.8f };
            e.process (l.data() + pos, r.data() + pos, n, &on, pos == 0 ? 1 : 0, Transport { bpm, (double) pos / spb, true });
            pos += n;
        }
        INFO ("block " << block << (random ? " random" : ""));
        CHECK (countOnsets (l, 0, total - 6000) == expected);
    }
}

TEST_CASE ("A long gate against a swung, shorter step ends the note before the next one starts", "[arp][swing]")
{
    // Swing pulls every second step closer; a 95 % gate outlasted it, the next
    // step found the note open, took it for a tie and swallowed a same-pitch hit.
    ArpSettings s;
    s.gate = 0.95f;
    s.swing = 0.5f;
    auto a = make (s);
    a.keyOn (60, 0.8f);
    const auto ev = run (a, kSixteenth * 16, 256);
    REQUIRE (ons (ev).size() == 16);
    bool open = false;
    for (const auto& e : ev)
    {
        CHECK (e.on != open);   // on, off, on, off...
        open = e.on;
    }
    // the same through the settings a kick gets, where every step is a fresh hit
    ParamState p;
    p.set (P::EngineMode, (float) EngineMode::Kick);
    p.set (P::ArpGate, 1.f);
    p.set (P::ArpSwing, 0.4f);
    auto k = make (ArpSettings::from (p));
    k.keyOn (36, 0.8f);
    const auto kev = run (k, kSixteenth * 32, 256);
    CHECK (ons (kev).size() == 32);
    open = false;
    for (const auto& e : kev)
    {
        CHECK (e.on != open);
        open = e.on;
    }

    // a real tie still ties: the same pitch just keeps sounding
    ArpSettings t;
    t.pattern = ArpPattern::parse ("sx--");
    t.swing = 0.5f;
    auto tied = make (t);
    tied.keyOn (60, 0.8f);
    const auto tev = run (tied, kSixteenth * 2, 256);
    CHECK (ons (tev).size() == 1);
}

TEST_CASE ("A new chord in Hold mode doesn't slide out of the old one", "[arp][hold]")
{
    ArpSettings s;
    s.hold = true;
    s.gate = 1.f;   // every step is tied: the note is open when the chord changes
    auto a = make (s);
    a.keyOn (60, 0.8f);
    run (a, kSixteenth * 2 + 100, 256);
    a.keyOff (60);
    std::vector<ArpEvent> ev;
    a.keyOn (65, 0.8f);
    a.sync (0.0, 120.0, false);
    for (int t = 0; t < 4; ++t)
    {
        a.fireDue ([&] (const ArpEvent& e) { ev.push_back (e); });
        a.advance (1);
    }
    REQUIRE (ev.size() >= 2);
    CHECK_FALSE (ev[0].on);
    CHECK (ev[0].note == 60);
    CHECK (ev[1].on);
    CHECK (ev[1].note == 65);
    CHECK (ev[1].slideFrom < 0);
}

TEST_CASE ("Engine: a division change applies in the block it arrives in", "[arp][engine]")
{
    // sync() used to run before the block's settings were applied, so the
    // first block after the change still played the old grid.
    ParamState p = percussive();
    int quarter = -1;
    for (int i = 0; i < 32; ++i)
        if (syncDivBeats (i) == 1.f) { quarter = i; break; }
    REQUIRE (quarter >= 0);
    Engine e;
    e.setParams (p);
    e.prepare (kFs, 4096);
    const double spb = 24000.0;   // 120 BPM
    std::vector<float> l (48000), r (48000);
    MidiEvent on { 0, MidiEvent::NoteOn, 60, 0.8f };
    e.process (l.data(), r.data(), 4096, &on, 1, Transport { 120.0, 0.0, true });
    p.set (P::ArpDiv, (float) quarter);
    e.setParams (p);
    // two beats starting a 16th after a beat: the quarter grid has steps on the
    // next two beats, the old 16th grid also fired one at once
    const double startPpq = 8.25;
    for (int pos = 0; pos < 48000; pos += 4096)
    {
        const int n = std::min (4096, 48000 - pos);
        e.process (l.data() + pos, r.data() + pos, n, nullptr, 0, Transport { 120.0, startPpq + pos / spb, true });
    }
    CHECK (countOnsets (l, 0, 48000 - 100) == 2);
}

TEST_CASE ("Engine: keys the pedal holds when the arp turns on are let go with the pedal", "[arp][engine]")
{
    ParamState p;
    Engine e;
    e.setParams (p);
    e.prepare (kFs, 256);
    std::vector<float> l (256), r (256);
    const MidiEvent pedalDown { 0, MidiEvent::Controller, 64, 1.f }, pedalUp { 0, MidiEvent::Controller, 64, 0.f };
    const MidiEvent on { 0, MidiEvent::NoteOn, 60, 0.8f }, off { 0, MidiEvent::NoteOff, 60, 0.f };
    e.process (l.data(), r.data(), 256, &pedalDown, 1, 120.0);
    e.process (l.data(), r.data(), 256, &on, 1, 120.0);
    e.process (l.data(), r.data(), 256, &off, 1, 120.0);   // the pedal holds it
    p.set (P::ArpOn, 1.f);
    e.setParams (p);
    for (int b = 0; b < 40; ++b) e.process (l.data(), r.data(), 256, nullptr, 0, 120.0);
    CHECK (e.arpStep() >= 0);   // arpeggiating the held note
    e.process (l.data(), r.data(), 256, &pedalUp, 1, 120.0);
    for (int b = 0; b < 40; ++b) e.process (l.data(), r.data(), 256, nullptr, 0, 120.0);
    CHECK (e.arpStep() == -1);
    CHECK (e.activeVoices() == 0);
}
