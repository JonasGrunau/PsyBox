#include "TestUtil.h"
#include "engine/Engine.h"
#include "generator/Generator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>

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
            // energy sounds are one gesture per note: hold one through the render
            const bool sustained = cat == Category::Atmos || cat == Category::Drone;
            const Render out = hasLength (cat) ? hold (p, 6.0) : play (p, sustained ? 6.0 : 3.0, 48000.0, 256, sustained ? 40.0 : 0.6);
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
            CHECK (rmsAt (0.0, 0.15 * beat) < rmsAt (0.7 * beat, 0.95 * beat) - 12.0);   // a swell
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
