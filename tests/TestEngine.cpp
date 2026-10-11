#include "TestUtil.h"
#include "engine/Engine.h"
#include "generator/Generator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <tuple>

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

namespace
{
    // One note held for `seconds` (energy sounds: risers, impacts...), at 145 BPM.
    Render hold (const ParamState& state, double seconds, double noteSeconds = -1.0, int note = 48, double bpm = 145.0)
    {
        Engine e;
        e.setParams (state);
        e.prepare (48000.0, 256);
        Render out;
        const int total = (int) (seconds * 48000.0);
        const int off = noteSeconds < 0.0 ? total + 1 : (int) (noteSeconds * 48000.0);
        out.l.assign ((size_t) total, 0.f);
        out.r.assign ((size_t) total, 0.f);
        for (int pos = 0; pos < total; pos += 256)
        {
            const int n = std::min (256, total - pos);
            std::vector<MidiEvent> ev;
            if (pos == 0) ev.push_back ({ 0, MidiEvent::NoteOn, note, 0.8f });
            if (off >= pos && off < pos + n) ev.push_back ({ off - pos, MidiEvent::NoteOff, note, 0.f });
            e.process (out.l.data() + pos, out.r.data() + pos, n, ev.data(), (int) ev.size(), bpm);
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
            // energy sounds are one gesture per note: hold one through its
            // length and a second of tail (a one-beat shot would drown in six)
            const bool sustained = cat == Category::Atmos || cat == Category::Drone;
            const double length = p.geti (P::EnergyMode) == (int) EnergyMode::Time ? (double) p.get (P::EnergySec)
                                                                                   : energyBarsValue (p.geti (P::EnergyBars)) * 4.0 * 60.0 / 145.0;
            const Render out = hasLength (cat) ? hold (p, std::min (6.0, length + 1.0))
                                               : play (p, sustained ? 6.0 : 3.0, 48000.0, 256, sustained ? 40.0 : 0.6);
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

TEST_CASE ("Bass Shot patches are loud, sub-heavy, compressed one-shots", "[engine][generator]")
{
    // Traits measured on progressive-trance reference shots: nearly all energy
    // under 150 Hz, RMS within a few dB of the peak (heavy compression), the
    // compressor on and no delay or reverb tails.
    Generator g;
    const double fs = 44100.0;
    const int total = (int) (fs * 60.0 / 140.0);   // one beat at 140 BPM, cut like a sample
    std::vector<double> rmsRel;
    for (uint32_t seed = 1; seed <= 24; ++seed)
    {
        ParamState p = g.generate (Category::BassShot, seed * 7919u, ParamState(), LockSet(), false);
        INFO ("seed " << seed);
        CHECK (p.on (P::CompOn));
        CHECK_FALSE (p.on (P::DelayOn));
        CHECK_FALSE (p.on (P::ReverbOn));
        p.set (P::MasterVol, 0.f);

        Engine e;
        e.setParams (p);
        e.prepare (fs, 256);
        std::vector<float> l ((size_t) total), r ((size_t) total);
        MidiEvent on { 0, MidiEvent::NoteOn, 40 + (int) (seed % 6), 0.9f };   // E2..A2
        for (int pos = 0; pos < total; pos += 256)
            e.process (l.data() + pos, r.data() + pos, std::min (256, total - pos), pos == 0 ? &on : nullptr, pos == 0 ? 1 : 0, 140.0);

        float pk = 0.f;
        for (float v : l) pk = std::max (pk, std::abs (v));
        REQUIRE (pk > 0.01f);
        rmsRel.push_back (20.0 * std::log10 (testutil::rms (l) / pk));

        std::vector<std::complex<double>> a (1 << 15);
        for (size_t i = 0; i < l.size(); ++i) a[i] = l[i];
        testutil::fft (a);
        double low = 0.0, all = 0.0;
        for (size_t i = 1; i < a.size() / 2; ++i)
        {
            const double pw = std::norm (a[i]);
            all += pw;
            if ((double) i * fs / (double) a.size() < 150.0) low += pw;
        }
        CHECK (low / all > 0.5);
        CHECK (rmsRel.back() > -20.0);

        // Width may spread the top, but the low end stays mono.
        std::vector<std::complex<double>> sd (1 << 15);
        for (size_t i = 0; i < l.size(); ++i) sd[i] = 0.5 * ((double) l[i] - (double) r[i]);
        testutil::fft (sd);
        double sideLow = 0.0;
        for (size_t i = 1; (double) i * fs / (double) sd.size() < 60.0; ++i) sideLow += std::norm (sd[i]);
        CHECK (sideLow < 1e-3 * low);   // side where the fundamentals sit: 30 dB+ below the lows (refs: -32 dB)
    }
    std::sort (rmsRel.begin(), rmsRel.end());
    CHECK (rmsRel[rmsRel.size() / 2] > -9.0);   // references: -7.5 dB median
}

TEST_CASE ("Psy, Prog and Offbeat basses are mono, dry, dense and level with the kicks", "[engine][generator]")
{
    // Traits measured on sustained reference notes (C1-B2): the fundamental's
    // octave holds nearly all the energy, the body is almost as dense as a sine
    // (crest 4-5 dB), no stereo and no tails. The glue comp's make-up puts
    // them near the kicks' level.
    Generator g;
    const double fs = 44100.0;
    const int total = (int) (fs * 0.4);
    for (auto cat : { Category::PsyBass, Category::ProgBass, Category::OffbeatBass })
        for (uint32_t seed = 1; seed <= 16; ++seed)
        {
            ParamState p = g.generate (cat, seed * 7919u, ParamState(), LockSet(), false);
            INFO (categoryInfo()[(size_t) cat].name << " seed " << seed);
            CHECK (p.on (P::CompOn));
            for (auto fx : { P::DelayOn, P::ReverbOn, P::ChorusOn, P::WidthOn, P::ArpOn })
                CHECK_FALSE (p.on (fx));
            p.set (P::MasterVol, 0.f);

            Engine e;
            e.setParams (p);
            e.prepare (fs, 256);
            std::vector<float> l ((size_t) total), r ((size_t) total);
            MidiEvent on { 0, MidiEvent::NoteOn, 40 + (int) (seed % 6), 0.9f };   // E1..A1 with the recipe's octave down
            for (int pos = 0; pos < total; pos += 256)
                e.process (l.data() + pos, r.data() + pos, std::min (256, total - pos), pos == 0 ? &on : nullptr, pos == 0 ? 1 : 0, 140.0);

            float pk = 0.f;
            double side = 0.0, mid = 0.0;
            for (size_t i = 0; i < l.size(); ++i)
            {
                pk = std::max (pk, std::abs (l[i]));
                side += (double) (l[i] - r[i]) * (l[i] - r[i]);
                mid += (double) (l[i] + r[i]) * (l[i] + r[i]);
            }
            const double peakDb = 20.0 * std::log10 (pk);
            CHECK (peakDb > -12.0);
            CHECK (peakDb < -1.0);
            CHECK (side < 1e-8 * mid);

            std::vector<std::complex<double>> a (1 << 15);
            for (size_t i = 0; i < l.size(); ++i) a[i] = l[i];
            testutil::fft (a);
            double low = 0.0, all = 0.0;
            for (size_t i = 1; i < a.size() / 2; ++i)
            {
                const double pw = std::norm (a[i]);
                all += pw;
                if ((double) i * fs / (double) a.size() < 150.0) low += pw;
            }
            CHECK (low / all > 0.8);

            const std::vector<float> body (l.begin() + (int) (0.08 * fs), l.end());
            float bodyPk = 0.f;
            for (float v : body) bodyPk = std::max (bodyPk, std::abs (v));
            CHECK (20.0 * std::log10 (bodyPk / testutil::rms (body)) < 7.5);
        }
}

TEST_CASE ("Noise Direct adds the noise after the filters", "[engine]")
{
    // Noise only, through a dark low-pass: routed direct, its top end survives.
    const auto highShare = [] (bool direct) {
        ParamState p;
        p.set (P::VoiceMode, (float) VoiceMode::Mono);
        p.set (P::OscAOn, 0.f);
        p.set (P::NoiseLevel, 0.5f);
        p.set (P::NoiseType, 0.f);
        p.set (P::NoiseDirect, direct ? 1.f : 0.f);
        p.set (P::F1Type, (float) FilterType::LP24);
        p.set (P::F1Cutoff, 200.f);
        p.set (P::AenvS, 1.f);
        const double fs = 48000.0;
        Engine e;
        e.setParams (p);
        e.prepare (fs, 256);
        std::vector<float> l (1 << 15), r (1 << 15);
        MidiEvent on { 0, MidiEvent::NoteOn, 48, 0.9f };
        for (size_t pos = 0; pos < l.size(); pos += 256)
            e.process (l.data() + pos, r.data() + pos, 256, pos == 0 ? &on : nullptr, pos == 0 ? 1 : 0, 140.0);
        std::vector<std::complex<double>> a (l.begin(), l.end());
        testutil::fft (a);
        double hi = 0.0, all = 1e-30;
        for (size_t i = 1; i < a.size() / 2; ++i)
        {
            all += std::norm (a[i]);
            if ((double) i * fs / (double) a.size() > 4000.0) hi += std::norm (a[i]);
        }
        return hi / all;
    };
    CHECK (highShare (false) < 1e-4);
    CHECK (highShare (true) > 0.3);
}

namespace
{
    // A plain saw that swells over the energy length and stops at its end.
    ParamState cutAtEnd()
    {
        ParamState p;
        p.set (P::OscAType, (float) OscType::Analog);
        p.set (P::OscAShape, 2.f);
        p.set (P::AenvA, 1.f);
        p.set (P::AenvS, 1.f);
        p.set (P::AenvR, 2.f);
        p.set (P::EnergyFade, 0.5f);
        p.set (P::EnergyCut, 1.f);
        return p;
    }

    double lastSoundSeconds (const std::vector<float>& x)
    {
        for (size_t i = x.size(); i-- > 0;)
            if (std::abs (x[i]) > 1e-3f) return (double) i / 48000.0;
        return 0.0;
    }
} // namespace

TEST_CASE ("An energy sweep lasts its length in bars at the host tempo, or its time in seconds", "[engine][energy]")
{
    auto p = cutAtEnd();
    p.set (P::EnergyBars, 2.f);   // 1 bar
    CHECK (std::abs (lastSoundSeconds (hold (p, 4.0, -1.0, 48, 120.0).l) - 2.0) < 0.03);     // 4 beats at 120
    CHECK (std::abs (lastSoundSeconds (hold (p, 4.0, -1.0, 48, 145.0).l) - 240.0 / 145.0) < 0.03);
    p.set (P::EnergyBars, 1.f);   // 1/2 bar
    CHECK (std::abs (lastSoundSeconds (hold (p, 4.0, -1.0, 48, 120.0).l) - 1.0) < 0.03);
    p.set (P::EnergyMode, (float) EnergyMode::Time);
    p.set (P::EnergySec, 1.5f);
    CHECK (std::abs (lastSoundSeconds (hold (p, 4.0, -1.0, 48, 120.0).l) - 1.5) < 0.03);
}

TEST_CASE ("The sweep source climbs from 0 to 1 and the level follows the fade", "[engine][energy]")
{
    auto p = cutAtEnd();
    p.set (P::EnergyCut, 0.f);
    p.set (P::EnergyFade, 1.f);   // full fade in: silent at the start, full at the end
    p.set (P::EnergyMode, (float) EnergyMode::Time);
    p.set (P::EnergySec, 2.f);
    const auto out = hold (p, 3.0);
    const auto rmsAt = [&] (double t) {
        double s = 0.0;
        const int a = (int) (t * 48000.0), n = 2400;
        for (int i = a; i < a + n; ++i) s += (double) out.l[(size_t) i] * out.l[(size_t) i];
        return std::sqrt (s / n);
    };
    CHECK (rmsAt (0.1) < 0.2 * rmsAt (2.5));
    CHECK (rmsAt (1.0) < rmsAt (1.9));
    CHECK (rmsAt (2.5) > 0.01);
}

TEST_CASE ("Effects follow their matrix modulation", "[engine][fx]")
{
    ParamState p;
    p.set (P::AenvR, 5.f);
    p.set (P::ReverbOn, 1.f);
    p.set (P::ReverbMix, 0.f);
    p.set (P::Mod1Src, (float) ModSrc::Macro1);
    p.set (P::Mod1Dst, (float) ModDst::ReverbMix);
    p.set (P::Mod1Amt, 1.f);
    const auto tail = [] (const Render& r) {
        double s = 0.0;
        for (size_t i = 48000; i < r.l.size(); ++i) s += (double) r.l[i] * r.l[i];
        return s;
    };
    const double dry = tail (hold (p, 2.0, 0.3));
    p.set (P::Macro1, 1.f);
    const double wet = tail (hold (p, 2.0, 0.3));
    CHECK (wet > 1000.0 * dry + 1e-6);
}

namespace
{
    // Plays 256-sample blocks; `events[b]` arrive at the start of block b and
    // `change[b]`, when set, is the patch from block b on. Returns the peak of
    // the last half second.
    float tailPeak (ParamState p, const std::vector<std::vector<MidiEvent>>& events,
                    const std::vector<std::pair<int, ParamState>>& change = {}, int blocks = 400)
    {
        Engine e;
        e.setParams (p);
        e.prepare (48000.0, 256);
        std::vector<float> l (256), r (256);
        float peak = 0.f;
        for (int b = 0; b < blocks; ++b)
        {
            for (const auto& [at, state] : change)
                if (at == b) e.setParams (state);
            const std::vector<MidiEvent> none;
            const auto& ev = b < (int) events.size() ? events[(size_t) b] : none;
            e.process (l.data(), r.data(), 256, ev.data(), (int) ev.size(), 145.0);
            if (b >= blocks - 94)
                for (int i = 0; i < 256; ++i)
                    peak = std::max ({ peak, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]) });
        }
        return peak;
    }

    MidiEvent on (int note) { return { 0, MidiEvent::NoteOn, note, 0.8f }; }
    MidiEvent off (int note) { return { 0, MidiEvent::NoteOff, note, 0.f }; }
} // namespace

TEST_CASE ("A doubled note-on ends with its one note-off", "[engine][notes]")
{
    // e.g. a key pressed in the block where a new patch replays the held keys
    for (auto mode : { VoiceMode::Poly, VoiceMode::Mono, VoiceMode::Legato })
    {
        INFO ("voice mode " << (int) mode);
        ParamState p;
        p.set (P::VoiceMode, (float) mode);
        CHECK (tailPeak (p, { { on (60), on (60) }, {}, {}, {}, { off (60) } }) < 1e-4f);
        CHECK (tailPeak (p, { { on (60) }, { on (64) }, { on (60) }, {}, { off (60) }, { off (64) } }) < 1e-4f);
    }
}

TEST_CASE ("Notes held through a voice-mode change still end", "[engine][notes]")
{
    for (auto from : { VoiceMode::Poly, VoiceMode::Mono, VoiceMode::Legato })
        for (auto to : { VoiceMode::Poly, VoiceMode::Mono, VoiceMode::Legato })
        {
            INFO ("from " << (int) from << " to " << (int) to);
            ParamState a, b;
            a.set (P::VoiceMode, (float) from);
            b.set (P::VoiceMode, (float) to);
            CHECK (tailPeak (a, { { on (60) }, { on (64) }, {}, {}, {}, {}, { off (60) }, { off (64) } }, { { 3, b } }) < 1e-4f);
            CHECK (tailPeak (a, { { on (60) }, { on (64) }, {}, {}, {}, {}, { off (64) }, { off (60) } }, { { 3, b } }) < 1e-4f);
        }
}

TEST_CASE ("Reverse basses swell into the beat and stop at their length, key held or not", "[engine][bass]")
{
    Generator g;
    for (Category c : { Category::ReverseBass, Category::ReverseSub })
        for (uint32_t seed = 1; seed <= 12; ++seed)
        {
            INFO (categoryInfo()[(size_t) c].name << " seed " << seed);
            auto p = g.generate (c, seed, ParamState(), LockSet());
            p.set (P::EnergyBars, 0.f);   // 1/4 bar: one beat
            const double beat = 60.0 / 140.0;
            const auto out = hold (p, 4.0 * beat, -1.0, 40, 140.0);   // the key stays down for four beats
            const auto rmsAt = [&] (double t0, double t1) {
                double sum = 0.0;
                const auto a = (size_t) (t0 * 48000.0), b = (size_t) (t1 * 48000.0);
                for (size_t i = a; i < b; ++i) sum += (double) out.l[i] * out.l[i];
                return 10.0 * std::log10 (sum / (double) (b - a) + 1e-20);
            };
            // a swell: the reverse bass references start as little as 9 dB down
            CHECK (rmsAt (0.0, 0.15 * beat) < rmsAt (0.7 * beat, 0.95 * beat) - (c == Category::ReverseBass ? 7.0 : 12.0));
            CHECK (rmsAt (1.3 * beat, 4.0 * beat) < rmsAt (0.7 * beat, 0.95 * beat) - 30.0);   // then gone
            CHECK (out.peak > 0.1f);   // about level with the other basses
        }
}

TEST_CASE ("The mod view shows every sounding voice, the newest first", "[engine][notes]")
{
    ParamState p;
    p.set (P::AenvA, 100.f);
    p.set (P::Lfo1Retrig, 1.f);
    p.set (P::Lfo1Phase, 0.f);
    p.set (P::Lfo2Retrig, 0.f);
    Engine e;
    e.setParams (p);
    e.prepare (48000.0, 480);
    std::vector<float> l (480), r (480);
    ModView v;

    e.getModView (v);
    CHECK (v.numVoices == 0);

    const MidiEvent first[] = { on (60) };
    e.process (l.data(), r.data(), 480, first, 1, 120.0);   // 10 ms
    const MidiEvent second[] = { on (64) };
    e.process (l.data(), r.data(), 480, second, 1, 120.0);
    for (int i = 0; i < 3; ++i) e.process (l.data(), r.data(), 480, nullptr, 0, 120.0);   // 50 ms in all
    e.getModView (v);
    REQUIRE (v.numVoices == 2);
    const auto attack = (uint8_t) dsp::Adsr::Stage::Attack;
    CHECK (v.voices[0].env[0].stage == attack);
    CHECK (v.voices[1].env[0].stage == attack);
    CHECK (v.voices[0].env[0].pos < v.voices[1].env[0].pos);   // the newer note is less far in
    CHECK_THAT (v.voices[1].env[0].pos, Catch::Matchers::WithinAbs (0.5, 0.05));
    CHECK (v.voices[0].lfo[0] < v.voices[1].lfo[0]);            // retriggered per note
    CHECK (v.voices[0].lfo[1] == v.voices[1].lfo[1]);           // free running: one LFO for all
}

TEST_CASE ("Offline rendering plays at the Render quality", "[engine]")
{
    // The oversampling factor shows in the decimator's latency.
    const auto latency = [] (int live, int render, bool offline) {
        ParamState p;
        p.set (P::Quality, (float) live);
        p.set (P::RenderQuality, (float) render);
        Engine e;
        e.setOffline (offline);
        e.setParams (p);
        e.prepare (48000.0, 256);
        return e.latencySamples();
    };
    CHECK (latency (0, 2, false) == latency (0, 0, false));
    CHECK (latency (0, 2, true) == latency (2, 0, false));
    CHECK (latency (2, 0, true) == latency (0, 0, false));
    CHECK (latency (0, 2, true) != latency (0, 2, false));

    // and follows the switch between blocks
    ParamState p;
    p.set (P::Quality, 0.f);
    p.set (P::RenderQuality, 2.f);
    Engine e;
    e.setParams (p);
    e.prepare (48000.0, 256);
    std::vector<float> l (256), r (256);
    const int live = e.latencySamples();
    e.setOffline (true);
    e.process (l.data(), r.data(), 256, nullptr, 0, 120.0);
    CHECK (e.latencySamples() == latency (2, 2, false));
    e.setOffline (false);
    e.process (l.data(), r.data(), 256, nullptr, 0, 120.0);
    CHECK (e.latencySamples() == live);
}

TEST_CASE ("Unison voices spread evenly, keep the level and centre the middle voice", "[engine][unison]")
{
    dsp::UnisonLayout u;
    for (int n = 1; n <= dsp::UnisonLayout::kMax; ++n)
        for (float blend : { 0.f, 0.5f, 1.f })
        {
            u.compute (n, 1.f, 2.f, (int) UniMode::Linear, (int) UniStack::Off, blend, 1.f);
            REQUIRE (u.count == n);
            float power = 0.f, lo = 0.f, hi = 0.f;
            for (int k = 0; k < n; ++k)
            {
                power += u.gain[(size_t) k] * u.gain[(size_t) k];
                lo = std::min (lo, u.semis[(size_t) k]);
                hi = std::max (hi, u.semis[(size_t) k]);
            }
            CHECK_THAT (power, Catch::Matchers::WithinAbs (1.0, 1e-4));
            if (n > 1)
            {
                CHECK_THAT (lo, Catch::Matchers::WithinAbs (-1.0, 1e-5));   // detune 100 % covers the range
                CHECK_THAT (hi, Catch::Matchers::WithinAbs (1.0, 1e-5));
            }
            if (n % 2 == 1)
                CHECK (u.semis[0] == 0.f);
        }

    // Linear spacing: 5 voices at -1, -0.5, 0, 0.5, 1 semitones
    u.compute (5, 1.f, 2.f, (int) UniMode::Linear, (int) UniStack::Off, 1.f, 0.f);
    std::vector<float> s (u.semis.begin(), u.semis.begin() + 5);
    std::sort (s.begin(), s.end());
    for (int k = 0; k < 5; ++k)
        CHECK_THAT (s[(size_t) k], Catch::Matchers::WithinAbs (-1.0 + 0.5 * k, 1e-5));
    for (int k = 0; k < 5; ++k)
        CHECK (u.pan[(size_t) k] == 0.f);   // no width, no pan

    // Super keeps the inner voices closer than Linear, Inv pushes them out
    const auto inner = [&u] (int mode) {
        u.compute (7, 1.f, 2.f, mode, (int) UniStack::Off, 1.f, 1.f);
        return std::abs (u.semis[1]);
    };
    CHECK (inner ((int) UniMode::Super) < inner ((int) UniMode::Linear));
    CHECK (inner ((int) UniMode::Exp) < inner ((int) UniMode::Super));
    CHECK (inner ((int) UniMode::Inv) > inner ((int) UniMode::Linear));

    // Stack: the centre drops an octave, or the first pair goes up one
    u.compute (3, 0.f, 2.f, (int) UniMode::Linear, (int) UniStack::CenterDown1, 1.f, 1.f);
    CHECK (u.semis[0] == -12.f);
    u.compute (3, 0.f, 2.f, (int) UniMode::Linear, (int) UniStack::Oct, 1.f, 1.f);
    CHECK (u.semis[1] == 12.f);
    CHECK (u.semis[2] == 12.f);
}

TEST_CASE ("Unison spreads an oscillator in stereo at the same loudness", "[engine][unison]")
{
    const auto render = [] (int voices, float width, int type) {
        ParamState p;
        p.set (P::OscAType, (float) type);
        p.set (P::OscAUnison, (float) voices);
        p.set (P::OscADetune, 0.3f);
        p.set (P::OscABlend, 1.f);
        p.set (P::OscAUniWidth, width);
        p.set (P::OscARetrig, 0.f);
        return hold (p, 1.0);
    };
    const auto sideDb = [] (const Render& r) {
        double side = 0.0, mid = 0.0;
        for (size_t i = 0; i < r.l.size(); ++i)
        {
            const double m = 0.5 * (r.l[i] + r.r[i]), s = 0.5 * (r.l[i] - r.r[i]);
            mid += m * m;
            side += s * s;
        }
        return 10.0 * std::log10 ((side + 1e-20) / (mid + 1e-20));
    };

    for (int type : { (int) OscType::Analog, (int) OscType::Wavetable, (int) OscType::FM })
    {
        INFO ("type " << type);
        const auto one = render (1, 1.f, type);
        const auto wide = render (7, 1.f, type);
        const auto narrow = render (7, 0.f, type);
        CHECK (sideDb (one) < -100.0);       // one voice: mono
        CHECK (sideDb (narrow) < -100.0);    // no width: mono
        CHECK (sideDb (wide) > -15.0);       // spread: a real stereo image
        CHECK (std::abs (wide.rmsDb - one.rmsDb) < 3.0);
        CHECK (std::abs (narrow.rmsDb - one.rmsDb) < 3.0);
    }

    // the supersaw is its own unison: the count changes nothing
    const auto ss1 = render (1, 1.f, (int) OscType::Supersaw);
    const auto ss7 = render (7, 1.f, (int) OscType::Supersaw);
    CHECK (ss1.l == ss7.l);
    CHECK (ss1.r == ss7.r);
}

TEST_CASE ("The mod view shows each filter where its modulation has moved it", "[engine][notes]")
{
    ParamState p;
    p.set (P::F1Type, (float) FilterType::LP24);
    p.set (P::F1Cutoff, 500.f);
    p.set (P::F1Env, 1.f);
    p.set (P::FenvA, 0.f);
    p.set (P::FenvD, 2000.f);
    p.set (P::FenvS, 0.f);
    Engine e;
    e.setParams (p);
    e.prepare (48000.0, 480);
    std::vector<float> l (480), r (480);
    const MidiEvent note[] = { { 0, MidiEvent::NoteOn, 60, 1.f } };
    e.process (l.data(), r.data(), 480, note, 1, 120.0);
    ModView v;
    e.getModView (v);
    REQUIRE (v.numVoices == 1);
    const float early = v.voices[0].filter[0].cutoff;
    CHECK (early > 4000.f);   // the envelope opens it far above the knob
    for (int i = 0; i < 100; ++i) e.process (l.data(), r.data(), 480, nullptr, 0, 120.0);   // a second later
    e.getModView (v);
    REQUIRE (v.numVoices == 1);
    CHECK (v.voices[0].filter[0].cutoff < early);
    CHECK (v.voices[0].filter[0].cutoff >= 500.f);
}

TEST_CASE ("The effect order round-trips and changes the sound; the default is the old chain", "[engine][fx]")
{
    CHECK (fxChain (0) == FxChain { 0, 1, 2, 3, 4, 5, 6, 7, 8 });
    for (int code : { 0, 1, 2, 40319, 40320, 123456, 362879 })
        CHECK (fxOrderCode (fxChain (code)) == code);
    CHECK (fxOrderCode (FxChain { 0, 0, 2, 3, 4, 5, 6, 7, 8 }) == -1);
    const FxChain reverbFirst { 7, 0, 1, 2, 3, 4, 5, 6, 8 };
    CHECK (fxChain (fxOrderCode (reverbFirst)) == reverbFirst);

    ParamState p;
    p.set (P::DistOn, 1.f);
    p.set (P::DistDrive, 30.f);
    p.set (P::ReverbOn, 1.f);
    p.set (P::ReverbMix, 0.5f);
    const auto dryFirst = hold (p, 0.5);
    p.set (P::FxOrder, (float) fxOrderCode (reverbFirst));
    const auto wetFirst = hold (p, 0.5);
    double diff = 0.0;
    for (size_t i = 0; i < dryFirst.l.size(); ++i) diff += std::abs (dryFirst.l[i] - wetFirst.l[i]);
    CHECK (diff > 1.0);   // distorting the reverb tail is a different sound
    CHECK (wetFirst.peak < 1.f);
}

TEST_CASE ("Reverse Bass stays in its references' family and varies from patch to patch", "[engine][bass]")
{
    // The bright reverse shots in Bass Shots 2 (017 E, 024, 043, 049-051,
    // 055, 062, 072, 074, 079, 080): one beat; already there 9-33 dB down,
    // full by 32-60 % of the beat; 2-8 kHz 18-35 dB under the whole; crest
    // 5.4-7.8 dB; a hiss on top (017 E's bite). Around that they differ.
    Generator g;
    const double fs = 48000.0, beat = 60.0 / 140.0;
    std::set<std::string> kinds;
    double startLo = 0.0, startHi = -100.0;
    for (uint32_t seed = 1; seed <= 24; ++seed)
    {
        INFO ("seed " << seed);
        auto p = g.generate (Category::ReverseBass, seed * 7919u, ParamState(), LockSet());
        CHECK (p.geti (P::EnergyBars) == 0);   // one beat
        CHECK (p.geti (P::NoiseType) == 3);    // the hiss
        CHECK (p.get (P::NoiseLevel) >= 0.1f);
        const auto out = hold (p, beat, -1.0, 40, 140.0);
        const auto rms = [&] (double t0, double t1) {
            double s = 0.0;
            const auto a = (size_t) (t0 * fs), b = (size_t) (t1 * fs);
            for (size_t i = a; i < b; ++i) s += (double) out.l[i] * out.l[i];
            return 10.0 * std::log10 (s / (double) (b - a) + 1e-20);
        };
        const double full = rms (0.72 * beat, 0.95 * beat);
        const double start = rms (0.05 * beat, 0.15 * beat) - full;
        CHECK (start > -36.0);   // already there...
        CHECK (start < -4.0);    // ...but quiet
        CHECK (rms (0.66 * beat, 0.72 * beat) > full - 3.0);   // full by two thirds
        startLo = std::min (startLo, start);
        startHi = std::max (startHi, start);

        std::vector<std::complex<double>> a (1 << 14);
        const auto from = (size_t) (0.7 * beat * fs);
        for (size_t i = 0; i < a.size(); ++i) a[i] = out.l[from + i] * (0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) a.size()));
        testutil::fft (a);
        double hi = 0.0, all = 0.0;
        for (size_t i = 1; i < a.size() / 2; ++i)
        {
            const double f = (double) i * fs / (double) a.size(), pw = std::norm (a[i]);
            all += pw;
            if (f >= 2000.0 && f < 8000.0) hi += pw;
        }
        const double hiDb = 10.0 * std::log10 (hi / all);
        CHECK (hiDb > -38.0);
        CHECK (hiDb < -15.0);

        float pk = 0.f;
        double sq = 0.0, side = 0.0, mid = 0.0;
        const auto b0 = (size_t) (0.7 * beat * fs), b1 = (size_t) (0.95 * beat * fs);
        for (size_t i = b0; i < b1; ++i)
        {
            pk = std::max (pk, std::abs (out.l[i]));
            sq += (double) out.l[i] * out.l[i];
            side += 0.25 * (double) (out.l[i] - out.r[i]) * (out.l[i] - out.r[i]);
            mid += 0.25 * (double) (out.l[i] + out.r[i]) * (out.l[i] + out.r[i]);
        }
        const double crest = 20.0 * std::log10 (pk / std::sqrt (sq / (double) (b1 - b0)));
        CHECK (crest > 4.0);
        CHECK (crest < 9.5);
        CHECK (side < mid * 0.16);   // a stereo top at most: the side 8 dB under the middle

        // what this patch does on top of the shape
        std::string kind;
        kind += p.on (P::OscBOn) ? 'o' : '-';
        kind += p.geti (P::F2Type) != 0 ? 'n' : '-';
        kind += p.on (P::WidthOn) ? 'w' : '-';
        kind += p.on (P::DistOn) ? 'd' : '-';
        for (int i = 0; i < kNumModSlots; ++i)
            if (p.geti (modSrcParam (i)) == (int) ModSrc::Sweep)
                kind += p.geti (modDstParam (i)) == (int) ModDst::PitchAll ? 'f' : 'c';
        kinds.insert (kind);
    }
    CHECK (kinds.size() >= 10);             // not one patch over and over
    CHECK (startHi - startLo > 10.0);
}

TEST_CASE ("Reverse Sub matches its reference shots: a swell from silence, a held pitch, a late slide", "[engine][bass]")
{
    // Traits of Bass Shots 2/018 E, 025 G# and 026 G# (one beat at 140 BPM):
    // near silence at first, full by 60 % of the beat; the pitch holds until
    // just past the middle and then slides the whole interval by the end;
    // falls are warm sines (second harmonic 8-20 dB down), the rise a
    // band-passed saw whose second harmonic tops the fundamental. Mono.
    Generator g;
    const double fs = 48000.0, beat = 60.0 / 140.0;
    // the period by autocorrelation (YIN's cumulative mean normalised
    // difference), 30-300 Hz: robust for the sine and the band-passed saw
    const auto pitchHz = [&] (const std::vector<float>& x, double t0, double t1) {
        const auto from = (size_t) (t0 * fs), n = (size_t) ((t1 - t0) * fs);
        const int lo = (int) (fs / 300.0), hi = (int) (fs / 30.0);
        const int w = (int) n - hi;
        std::vector<double> d ((size_t) hi + 1, 0.0);
        for (int tau = 1; tau <= hi; ++tau)
            for (int i = 0; i < w; ++i)
            {
                const double e = (double) x[from + (size_t) i] - (double) x[from + (size_t) (i + tau)];
                d[(size_t) tau] += e * e;
            }
        double run = 0.0;
        int best = lo;
        double bestV = 1e9;
        for (int tau = 1; tau <= hi; ++tau)
        {
            run += d[(size_t) tau];
            const double cm = d[(size_t) tau] * tau / (run + 1e-20);
            if (tau >= lo && cm < 0.2) { best = tau; while (best < hi && d[(size_t) best + 1] < d[(size_t) best]) ++best; break; }
            if (tau >= lo && cm < bestV) { bestV = cm; best = tau; }
        }
        return fs / (double) best;
    };
    int falls = 0, rises = 0;
    for (uint32_t seed = 1; seed <= 16; ++seed)
    {
        INFO ("seed " << seed);
        auto p = g.generate (Category::ReverseSub, seed * 7919u, ParamState(), LockSet());
        CHECK (p.geti (P::EnergyBars) == 0);
        float semis = 0.f;
        for (int i = 0; i < kNumModSlots; ++i)
            if (p.geti (modSrcParam (i)) == (int) ModSrc::Sweep && p.geti (modDstParam (i)) == (int) ModDst::PitchAll)
                semis = p.get (modAmtParam (i)) * 24.f;
        REQUIRE (std::abs (semis) >= 5.f);
        const auto out = hold (p, beat, -1.0, 44, 140.0);
        const auto rms = [&] (double t0, double t1) {
            double s = 0.0;
            const auto a = (size_t) (t0 * fs), b = (size_t) (t1 * fs);
            for (size_t i = a; i < b; ++i) s += (double) out.l[i] * out.l[i];
            return 10.0 * std::log10 (s / (double) (b - a) + 1e-20);
        };
        const double full = rms (0.62 * beat, 0.75 * beat);
        CHECK (rms (0.0, 0.08 * beat) < full - 25.0);
        CHECK (rms (0.6 * beat, 0.66 * beat) > full - 3.0);

        // the pitch: held, then the whole interval by the end
        const double held = pitchHz (out.l, 0.3 * beat, 0.42 * beat);
        const double late = pitchHz (out.l, 0.4 * beat, 0.52 * beat);
        const double end = pitchHz (out.l, 0.8 * beat, 0.99 * beat);
        CHECK (std::abs (12.0 * std::log2 (late / held)) < 0.8);
        CHECK (12.0 * std::log2 (end / held) * (semis > 0.f ? 1.0 : -1.0) > 0.6 * std::abs (semis));

        // the tone: a warm sine falling, a band-passed saw rising
        std::vector<std::complex<double>> a (1 << 16);
        const auto from = (size_t) (0.3 * beat * fs), n = (size_t) (0.2 * beat * fs);
        for (size_t i = 0; i < n; ++i) a[i] = out.l[from + i] * (0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) n));
        testutil::fft (a);
        const double f0 = held;
        const auto level = [&] (double f) {
            double m = 0.0;
            for (size_t i = 0; i < a.size() / 2; ++i)
            {
                const double fi = (double) i * fs / (double) a.size();
                if (fi > f * 0.97 && fi < f * 1.03) m = std::max (m, std::abs (a[i]));
            }
            return 20.0 * std::log10 (m + 1e-12);
        };
        const double h2 = level (2.0 * f0) - level (f0), h4 = level (4.0 * f0) - level (f0);
        if (semis < 0.f)
        {
            ++falls;
            CHECK (h2 < -8.0);
            CHECK (h2 > -20.0);
            CHECK (h4 < -22.0);
        }
        else
        {
            ++rises;
            CHECK (h2 > 0.0);
        }
        double side = 0.0, sq = 0.0;
        for (size_t i = 0; i < out.l.size(); ++i) { side += (double) (out.l[i] - out.r[i]) * (out.l[i] - out.r[i]); sq += (double) out.l[i] * out.l[i]; }
        CHECK (side < 1e-6 * sq);
    }
    CHECK (falls > 0);
    CHECK (rises > 0);
}

TEST_CASE ("Jilax shots end at their length with the key held, wide and without a sub", "[engine][jilax]")
{
    Generator g;
    const double fs = 48000.0, bpm = 142.0, beat = 60.0 / bpm;
    const int n = 16;
    int wide = 0;
    for (uint32_t seed = 1; seed <= (uint32_t) n; ++seed)
    {
        INFO ("seed " << seed);
        const auto p = g.generate (Category::Jilax, seed, ParamState(), LockSet());
        const double len = energyBarsValue (p.geti (P::EnergyBars)) * 4.0 * beat;
        const auto out = hold (p, len + 2.5, -1.0, 52, bpm);   // E3, held down the whole time
        const auto power = [&] (const std::vector<float>& x, double t0, double t1) {
            double s = 0.0;
            const auto a = (size_t) (t0 * fs), b = (size_t) (t1 * fs);
            for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
            return s / (double) (b - a) + 1e-30;
        };
        const double body = 10.0 * std::log10 (power (out.l, 0.02, 0.8 * len));
        CHECK (body > -70.0);   // sounding (unlevelled, so well under the plugin's level)
        CHECK (10.0 * std::log10 (power (out.l, len + 1.5, len + 2.5)) < body - 30.0);   // over, tails and all

        std::vector<float> mid (out.l.size()), side (out.l.size());
        for (size_t i = 0; i < mid.size(); ++i)
        {
            mid[i] = 0.5f * (out.l[i] + out.r[i]);
            side[i] = 0.5f * (out.l[i] - out.r[i]);
        }
        wide += power (side, 0.02, 0.8 * len) > 0.1 * power (mid, 0.02, 0.8 * len) ? 1 : 0;

        // a high-passed shot in the bass register keeps under 60 Hz far below the rest
        const int f2 = p.geti (P::F2Type);
        if (p.geti (P::Octave) != -2 || (f2 != (int) FilterType::HP12 && f2 != (int) FilterType::HP18 && f2 != (int) FilterType::HP24))
            continue;
        std::vector<std::complex<double>> a (1 << 14);
        const auto from = (size_t) (0.02 * fs);
        for (size_t i = 0; i < a.size(); ++i)
            a[i] = (from + i < mid.size() ? mid[from + i] : 0.f) * (0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) a.size()));
        testutil::fft (a);
        double low = 0.0, all = 0.0;
        for (size_t i = 1; i < a.size() / 2; ++i)
        {
            const double f = (double) i * fs / (double) a.size(), pw = std::norm (a[i]);
            all += pw;
            if (f < 60.0) low += pw;
        }
        CHECK (10.0 * std::log10 (low / all) < -20.0);
    }
    CHECK (wide >= n * 3 / 4);   // the side within 10 dB of the mid
}

TEST_CASE ("A One Shot sound plays its whole length from a tap", "[engine][energy]")
{
    Generator g;
    for (uint32_t seed = 1; seed <= 6; ++seed)
    {
        INFO ("seed " << seed);
        auto p = g.generate (Category::Jilax, seed, ParamState(), LockSet());
        REQUIRE (p.on (P::EnergyOneShot));
        const double len = energyBarsValue (p.geti (P::EnergyBars)) * 4.0 * 60.0 / 145.0;
        // a 20 ms tap and a key held past the end sound exactly the same
        const auto tap = hold (p, len + 1.0, 0.02);
        const auto held = hold (p, len + 1.0, len + 0.5);
        CHECK (tap.l == held.l);
        CHECK (tap.r == held.r);

    }

    // on the init patch (a held saw), a tap ends the note early without One
    // Shot and plays the whole beat with it; either way it stops at the end
    ParamState p;
    p.set (P::EnergyBars, 0.f);   // one beat
    p.set (P::EnergyCut, 1.f);
    const double beat = 60.0 / 145.0;
    const auto power = [&] (const std::vector<float>& x, double t0, double t1) {
        double s = 0.0;
        for (auto i = (size_t) (t0 * 48000.0); i < (size_t) (t1 * 48000.0); ++i) s += (double) x[i] * x[i];
        return 10.0 * std::log10 (s / ((t1 - t0) * 48000.0) + 1e-30);
    };
    const auto cut = hold (p, 2.0, 0.02);
    p.set (P::EnergyOneShot, 1.f);
    const auto shot = hold (p, 2.0, 0.02);
    CHECK (power (cut.l, 0.5 * beat, 0.9 * beat) < power (shot.l, 0.5 * beat, 0.9 * beat) - 40.0);
    CHECK (power (shot.l, beat + 0.5, 2.0) < power (shot.l, 0.5 * beat, 0.9 * beat) - 60.0);
}

namespace
{
    // Plays { seconds, note, on } events and returns the left channel.
    std::vector<float> playEvents (const ParamState& p, double seconds, const std::vector<std::tuple<double, int, bool>>& events)
    {
        Engine e;
        e.setParams (p);
        e.prepare (48000.0, 256);
        const int total = (int) (seconds * 48000.0);
        std::vector<float> l ((size_t) total), r ((size_t) total);
        for (int pos = 0; pos < total; pos += 256)
        {
            const int n = std::min (256, total - pos);
            std::vector<MidiEvent> ev;
            for (const auto& [t, note, down] : events)
            {
                const int at = (int) (t * 48000.0);
                if (at >= pos && at < pos + n)
                    ev.push_back ({ at - pos, down ? MidiEvent::NoteOn : MidiEvent::NoteOff, note, down ? 0.8f : 0.f });
            }
            e.process (l.data() + pos, r.data() + pos, n, ev.data(), (int) ev.size(), 145.0);
        }
        return l;
    }

    // The biggest jump from one sample to the next in [t0, t1) seconds. A
    // click is a jump far bigger than the waveform's own steepest slope.
    float maxStep (const std::vector<float>& x, double t0, double t1)
    {
        float m = 0.f;
        for (auto i = std::max ((size_t) 1, (size_t) (t0 * 48000.0)); i < std::min (x.size(), (size_t) (t1 * 48000.0)); ++i)
            m = std::max (m, std::abs (x[i] - x[i - 1]));
        return m;
    }
} // namespace

TEST_CASE ("An instant attack, a 1 ms release and a gate chop ramp instead of clicking", "[engine][click]")
{
    ParamState p;
    p.set (P::F1Type, 0.f);   // no filter to round anything off
    p.set (P::AenvA, 0.f);
    p.set (P::AenvS, 1.f);
    p.set (P::AenvR, 1.f);
    p.set (P::OscARetrig, 1.f);

    // a triangle starts at -1: with no attack it jumped there (66x its own slope)
    p.set (P::OscAShape, 1.f);
    auto x = playEvents (p, 0.3, { { 0.0, 48, true }, { 0.2, 48, false } });
    const float slope = maxStep (x, 0.05, 0.15);
    CHECK (maxStep (x, 0.0, 0.01) < 2.f * slope);
    CHECK (maxStep (x, 0.2, 0.25) < 2.f * slope);   // the exponential release fell 5x its slope in a sample

    // a sine chopped by a square LFO on the level (it jumped 47x its slope)
    p.set (P::OscAShape, 0.f);
    p.set (P::Lfo1Shape, 4.f);
    p.set (P::Lfo1Sync, 0.f);
    p.set (P::Lfo1Rate, 8.f);
    p.set (P::Mod1Src, (float) ModSrc::Lfo1);
    p.set (P::Mod1Dst, (float) ModDst::Amp);
    p.set (P::Mod1Amt, 1.f);
    x = playEvents (p, 0.5, { { 0.0, 48, true } });
    CHECK (maxStep (x, 0.055, 0.45) < 2.f * maxStep (x, 0.02, 0.055));   // the first half cycle is open
}

TEST_CASE ("A retriggered mono note starts as cleanly as a fresh voice", "[engine][click]")
{
    // A resonant filter swept by its envelope: retriggered in place, the
    // envelope's jump pinged the still-ringing filter (twice the top end of a
    // fresh start). Now the sounding voice fades out in a spare slot.
    ParamState p;
    p.set (P::F1Type, (float) FilterType::LP12);
    p.set (P::F1Cutoff, 300.f);
    p.set (P::F1Reso, 0.9f);
    p.set (P::F1Env, 0.7f);
    p.set (P::FenvD, 80.f);
    p.set (P::AenvS, 1.f);
    const auto top = [] (const std::vector<float>& x) {
        double s = 0.0;
        for (auto i = (size_t) (0.06 * 48000.0); i < (size_t) (0.066 * 48000.0); ++i)
        {
            const double d = (double) x[i] - 2.0 * x[i - 1] + x[i - 2];
            s += d * d;
        }
        return s;
    };
    const std::vector<std::tuple<double, int, bool>> notes { { 0.0, 48, true }, { 0.06, 55, true }, { 0.3, 48, false }, { 0.3, 55, false } };
    p.set (P::VoiceMode, (float) VoiceMode::Poly);
    const double fresh = top (playEvents (p, 0.4, notes));
    p.set (P::VoiceMode, (float) VoiceMode::Mono);
    CHECK (top (playEvents (p, 0.4, notes)) < 1.3 * fresh);

    // the old voice is gone 3 ms later, and a release still ends everything
    Engine e;
    e.setParams (p);
    e.prepare (48000.0, 48);
    std::vector<float> l (48), r (48);
    const MidiEvent first { 0, MidiEvent::NoteOn, 48, 0.8f }, second { 0, MidiEvent::NoteOn, 55, 0.8f };
    e.process (l.data(), r.data(), 48, &first, 1, 145.0);
    for (int b = 0; b < 20; ++b) e.process (l.data(), r.data(), 48, nullptr, 0, 145.0);
    e.process (l.data(), r.data(), 48, &second, 1, 145.0);
    CHECK (e.activeVoices() == 2);
    for (int b = 0; b < 4; ++b) e.process (l.data(), r.data(), 48, nullptr, 0, 145.0);
    CHECK (e.activeVoices() == 1);
    CHECK (tailPeak (p, { { on (48) }, { on (55) }, { on (48) }, {}, { off (48) }, { off (55) } }) < 1e-4f);
}
