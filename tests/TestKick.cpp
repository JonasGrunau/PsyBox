#include "dsp/Common.h"
#include "engine/Arpeggiator.h"
#include "engine/Engine.h"
#include "engine/KickVoice.h"
#include "generator/Generator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace psy;

namespace
{
    constexpr double kFs = 48000.0;

    ParamState kickPatch()
    {
        ParamState p;
        p.set (P::EngineMode, (float) EngineMode::Kick);
        p.set (P::MasterVol, 0.f);
        return p;
    }

    struct Hit { int at; int note; float vel; bool on = true; };

    // Renders the engine for `seconds` with the given note events (mono, left).
    std::vector<float> render (const ParamState& p, double seconds, std::vector<Hit> hits, Engine* use = nullptr, int block = 256)
    {
        Engine local;
        Engine& e = use != nullptr ? *use : local;
        if (use == nullptr)
        {
            e.setParams (p);
            e.prepare (kFs, block);
        }
        const int total = (int) (seconds * kFs);
        std::vector<float> l ((size_t) total), r ((size_t) total);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            std::vector<MidiEvent> ev;
            for (const auto& h : hits)
                if (h.at >= pos && h.at < pos + n)
                    ev.push_back ({ h.at - pos, h.on ? MidiEvent::NoteOn : MidiEvent::NoteOff, h.note, h.on ? h.vel : 0.f });
            e.process (l.data() + pos, r.data() + pos, n, ev.data(), (int) ev.size(), 145.0);
        }
        for (float v : l) REQUIRE (std::isfinite (v));
        return l;
    }

    // Mean frequency from upward zero crossings between t0 and t1 (seconds).
    double measureHz (const std::vector<float>& x, double fs, double t0, double t1)
    {
        double first = -1.0, last = -1.0;
        int count = 0;
        for (size_t i = (size_t) (t0 * fs); i + 1 < x.size() && (double) i < t1 * fs; ++i)
            if (x[i] <= 0.f && x[i + 1] > 0.f)
            {
                const double t = (double) i + (double) (-x[i] / (x[i + 1] - x[i]));
                if (first < 0.0) first = t;
                last = t;
                ++count;
            }
        return count > 1 ? (double) (count - 1) * fs / (last - first) : 0.0;
    }

    double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }

    double rms (const std::vector<float>& x, double fs, double t0, double t1)
    {
        double s = 0.0;
        int n = 0;
        for (size_t i = (size_t) (t0 * fs); i < x.size() && (double) i < t1 * fs; ++i, ++n)
            s += (double) x[i] * x[i];
        return std::sqrt (s / std::max (n, 1));
    }

    std::vector<float> offline (const ParamState& p, int note, double seconds = 0.4)
    {
        std::vector<float> out ((size_t) (seconds * kFs));
        KickVoice::renderOffline (p, note, 1.f, (float) kFs, (int) out.size(), out.data());
        return out;
    }
} // namespace

TEST_CASE ("Every kick hit is identical", "[kick]")
{
    const auto p = kickPatch();
    const int gap = (int) (0.6 * kFs);
    const auto x = render (p, 1.3, { { 100, 29, 0.8f }, { 100 + gap, 29, 0.8f } });
    float worst = 0.f;
    for (int i = 0; i < (int) (0.3 * kFs); ++i)
        worst = std::max (worst, std::abs (x[(size_t) (100 + i)] - x[(size_t) (100 + gap + i)]));
    CHECK (worst < 1e-4f);
}

TEST_CASE ("The kick's body lands on the played key", "[kick]")
{
    auto p = kickPatch();
    const auto s = KickSettings::from (p);
    for (int note = 24; note < 36; ++note)   // every pitch class
    {
        const auto x = offline (p, note);
        const double hz = measureHz (x, kFs, 0.1, 0.16);   // after the sweep lands, before the release
        INFO ("note " << note << " measured " << hz << " Hz, expected " << kickBodyHz (s, note));
        CHECK (std::abs (cents (hz, kickBodyHz (s, note))) < 10.0);
        // same pitch class as the key
        const double semis = 12.0 * std::log2 (hz / dsp::noteToHz ((float) note));
        CHECK (std::abs (semis - 12.0 * std::round (semis / 12.0)) < 0.1);
        // and within half an octave of Tune
        CHECK (std::abs (12.0 * std::log2 (hz / s.tuneHz)) <= 6.05);
    }
}

TEST_CASE ("Any octave of the key gives the same kick", "[kick]")
{
    const auto p = kickPatch();
    const auto a = offline (p, 29), b = offline (p, 41), c = offline (p, 65);
    for (size_t i = 0; i < a.size(); ++i)
    {
        REQUIRE (a[i] == b[i]);
        REQUIRE (a[i] == c[i]);
    }
}

TEST_CASE ("Without key tracking every note plays Tune", "[kick]")
{
    auto p = kickPatch();
    p.set (P::KickKeyTrack, 0.f);
    p.set (P::KickTune, 51.f);
    for (int note : { 24, 30, 36, 60 })
        CHECK (std::abs (cents (measureHz (offline (p, note), kFs, 0.1, 0.16), 51.0)) < 10.0);
}

TEST_CASE ("The pitch falls from Start through the knee and lands exactly on the body", "[kick]")
{
    auto p = kickPatch();
    p.set (P::KickKeyTrack, 0.f);
    const auto s = KickSettings::from (p);
    std::vector<float> hz ((size_t) (0.3 * kFs));
    KickVoice::renderOffline (p, 36, 1.f, (float) kFs, (int) hz.size(), nullptr, nullptr, hz.data());
    CHECK (hz[0] == Catch::Approx (s.startHz).epsilon (0.02));
    // after three snap time constants only the knee stage is left
    const float atKnee = hz[(size_t) (3.f * s.snapMs * 0.001 * kFs) + 2];
    CHECK (atKnee < s.kneeHz);
    CHECK (atKnee > s.tuneHz + (s.kneeHz - s.tuneHz) * 0.5f);
    // after three sweep time constants the body is exactly in tune
    CHECK (hz[(size_t) (3.f * s.sweepMs * 0.001 * kFs) + 2] == Catch::Approx (s.tuneHz).epsilon (1e-5));
    // and it never overshoots or rises again
    for (size_t i = 1; i < hz.size(); ++i)
        REQUIRE (hz[i] <= hz[i - 1] + 1e-3f);
    CHECK (kickPitchAt (s, 36, 0.f) == Catch::Approx (s.startHz).epsilon (1e-4));
    CHECK (kickPitchAt (s, 36, 0.2f) == Catch::Approx (s.tuneHz).epsilon (1e-5));
}

TEST_CASE ("Punch keeps the hit at full level while the body fades in", "[kick]")
{
    auto p = kickPatch();
    std::vector<float> env ((size_t) (0.1 * kFs));
    KickVoice::renderOffline (p, 29, 1.f, (float) kFs, (int) env.size(), nullptr, env.data());
    CHECK (env[0] > 0.95f);
    const float dip = env[(size_t) (0.012 * kFs)];
    CHECK (dip < 0.75f);    // the knock between the click and the body
    CHECK (dip > 0.35f);
    CHECK (env[(size_t) (0.045 * kFs)] > 0.99f);
    for (float e : env) REQUIRE (e <= 1.f + 1e-6f);
}

TEST_CASE ("A kick is a one-shot: an early note-off does not shorten it", "[kick]")
{
    const auto p = kickPatch();
    const auto shortGate = render (p, 0.5, { { 0, 29, 0.8f }, { 480, 29, 0.f, false } });
    const auto longGate = render (p, 0.5, { { 0, 29, 0.8f }, { 22000, 29, 0.f, false } });
    for (size_t i = 0; i < shortGate.size(); ++i)
        REQUIRE (shortGate[i] == Catch::Approx (longGate[i]).margin (1e-6));
    // and it does end
    CHECK (rms (shortGate, kFs, 0.05, 0.15) > 0.05);
    CHECK (rms (shortGate, kFs, 0.4, 0.5) < 1e-4);
}

TEST_CASE ("Retriggering a ringing kick does not click", "[kick]")
{
    auto p = kickPatch();
    p.set (P::KickClickLevel, 0.f);
    const int second = (int) (0.1 * kFs);   // in the middle of the hold
    const auto x = render (p, 0.5, { { 0, 29, 1.f }, { second, 29, 1.f } });
    auto maxStep = [&] (int from, int to) {
        float m = 0.f;
        for (int i = std::max (1, from); i < to; ++i)
            m = std::max (m, std::abs (x[(size_t) i] - x[(size_t) i - 1]));
        return m;
    };
    const int lat = 30;
    const float fromSilence = maxStep (0, lat + 240);
    const float atRetrigger = maxStep (second - 48, second + lat + 240);
    INFO ("from silence " << fromSilence << ", at the retrigger " << atRetrigger);
    CHECK (atRetrigger <= fromSilence * 1.25f);
}

TEST_CASE ("Near-simultaneous notes make one kick", "[kick]")
{
    const auto p = kickPatch();
    const auto one = render (p, 0.3, { { 0, 29, 1.f } });
    const auto chord = render (p, 0.3, { { 0, 29, 1.f }, { 0, 33, 0.5f }, { 20, 36, 0.7f } });
    for (size_t i = 0; i < one.size(); ++i)
        REQUIRE (chord[i] == Catch::Approx (one[i]).margin (1e-6));
}

TEST_CASE ("Kick mode leaves the synth voices idle", "[kick][engine]")
{
    const auto p = kickPatch();
    Engine e;
    e.setParams (p);
    e.prepare (kFs, 256);
    std::vector<Hit> hits;
    for (int i = 0; i < 8; ++i)
        hits.push_back ({ i * 4000, 29 + i, 0.8f });
    render (p, 0.7, hits, &e);
    CHECK (e.activeVoices() <= 2);
    render (p, 0.5, {}, &e);
    CHECK (e.activeVoices() == 0);
}

TEST_CASE ("Switching the engine mode mid-note leaves nothing hanging", "[kick][engine]")
{
    ParamState synth;
    synth.set (P::AenvS, 1.f);
    synth.set (P::AenvR, 20.f);
    Engine e;
    e.setParams (synth);
    e.prepare (kFs, 256);
    render (synth, 0.2, { { 0, 48, 0.8f } }, &e);   // held synth note
    REQUIRE (e.activeVoices() == 1);

    e.setParams (kickPatch());
    render (synth, 0.2, {}, &e);
    CHECK (e.activeVoices() == 0);   // released on the mode change

    const auto x = render (synth, 0.3, { { 0, 29, 0.8f } }, &e);
    CHECK (rms (x, kFs, 0.05, 0.15) > 0.05);   // the next key plays a kick
    CHECK (e.activeVoices() <= 1);

    e.setParams (synth);
    render (synth, 0.05, {}, &e);
    CHECK (e.activeVoices() == 0);   // the kick fades out when going back
}

TEST_CASE ("The kick sounds the same at every quality setting", "[kick]")
{
    double ref = 0.0;
    for (int q = 0; q < 3; ++q)
    {
        auto p = kickPatch();
        p.set (P::Quality, (float) q);
        p.set (P::KickClickLevel, 0.5f);
        const auto x = render (p, 0.4, { { 0, 29, 1.f } });
        const double level = rms (x, kFs, 0.0, 0.3);
        if (q == 0) ref = level;
        INFO ("quality " << q << " rms " << level);
        CHECK (std::abs (20.0 * std::log10 (level / ref)) < 0.5);
    }
}

TEST_CASE ("The arp never ties kicks together", "[kick][arp]")
{
    auto p = kickPatch();
    p.set (P::ArpGate, 1.4f);
    for (int i = 0; i < kArpSteps; ++i)
        p.set (arpStepParam (i, ArpLane::Slide), 1.f);
    const auto s = ArpSettings::from (p);
    CHECK (s.gate < 1.f);
    for (int i = 0; i < kArpSteps; ++i)
        CHECK_FALSE (s.pattern.has (i, ArpPattern::Slide));

    p.set (P::EngineMode, (float) EngineMode::Synth);
    CHECK (ArpSettings::from (p).gate == Catch::Approx (1.4f));
}

TEST_CASE ("Random kick settings stay finite and always finish", "[kick][stress]")
{
    dsp::Rng rng (99);
    const P kickParams[] = { P::KickTune, P::KickKeyTrack, P::KickStart, P::KickSnap, P::KickKnee, P::KickSweep,
                             P::KickAttack, P::KickHold, P::KickDecay, P::KickPunch, P::KickPunchLen, P::KickClickType,
                             P::KickClickLevel, P::KickClickDecay, P::KickClickTone, P::KickDriveType, P::KickDrive,
                             P::KickTone, P::VelSens, P::Quality };
    for (int trial = 0; trial < 40; ++trial)
    {
        auto p = kickPatch();
        for (P k : kickParams)
            p.set (k, def (k).toPlain (rng.uniform()));
        p.set (P::KickHold, rng.range (0.f, 200.f));
        p.set (P::KickDecay, rng.range (10.f, 400.f));
        Engine e;
        const auto x = render (p, 1.0, { { 0, rng.integer (0, 127), rng.range (0.1f, 1.f) }, { 9000, rng.integer (0, 127), 1.f } });
        float pk = 0.f;
        for (float v : x) pk = std::max (pk, std::abs (v));
        INFO ("trial " << trial);
        CHECK (pk <= 1.f);
        CHECK (rms (x, kFs, 0.9, 1.0) < 1e-3);
    }
}

TEST_CASE ("Generated kicks are in tune with the played key", "[kick][generator]")
{
    Generator g;
    for (Category c : categoriesIn (Family::Kick))
        for (uint32_t seed : { 1u, 42u, 777u, 9001u, 31337u })
        {
            const auto p = g.generate (c, seed, ParamState(), LockSet());
            const auto s = KickSettings::from (p);
            const int landed = (int) (3.0 * s.sweepMs * 0.001 * kFs) + 2;
            for (int note : { 24, 26, 29, 31, 34, 35, 47 })
            {
                std::vector<float> env ((size_t) landed + 1), hz ((size_t) landed + 1);
                KickVoice::renderOffline (p, note, 1.f, (float) kFs, landed + 1, nullptr, env.data(), hz.data());
                INFO (categoryInfo()[(size_t) c].name << " seed " << seed << " note " << note << ": " << hz[(size_t) landed] << " Hz");
                // the body has landed exactly on the key...
                const double semis = 12.0 * std::log2 (hz[(size_t) landed] / dsp::noteToHz ((float) note));
                CHECK (std::abs (semis - 12.0 * std::round (semis / 12.0)) * 100.0 < 1.0);
                // ...while the kick is still loud, so the tuned part is the part you hear
                CHECK (env[(size_t) landed] > 0.2f);
            }
        }
}
