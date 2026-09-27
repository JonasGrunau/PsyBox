#include "TestUtil.h"
#include "engine/Engine.h"
#include "generator/Generator.h"

#include <catch2/catch_test_macros.hpp>

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
