#include "TestUtil.h"
#include "engine/Engine.h"
#include "generator/Generator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <limits>
#include <numbers>
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
        float preClip = 0.f;   // the loudest sample before the output's soft clip (the output itself never passes 0 dBFS)
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
        // The engine replaces a NaN or infinity with silence, so the samples
        // can't show one: its own count can.
        REQUIRE (e.diagnostics().nonFinite == 0);
        out.preClip = e.diagnostics().preClipPeak;
        double s = 0.0;
        for (size_t i = 0; i < out.l.size(); ++i)
        {
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
        // The engine replaces a NaN or infinity with silence, so the samples
        // can't show one: its own count can.
        REQUIRE (e.diagnostics().nonFinite == 0);
        out.preClip = e.diagnostics().preClipPeak;
        double s = 0.0;
        for (size_t i = 0; i < out.l.size(); ++i)
        {
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
            CHECK (out.preClip <= 1.0f);   // leveled patches stay under 0 dBFS before the soft clip too
            CHECK (out.rmsDb > -60.0);
        }
}

TEST_CASE ("All three quality settings render the same patch", "[engine]")
{
    // Level and tone match the 1x render: RMS within 0.5 dB, and the energy
    // under 15 kHz (where all three qualities agree) within 1 dB.
    Generator g;
    ParamState p = g.generate (Category::Acid, 5, ParamState(), LockSet());
    const auto bandDb = [] (const std::vector<float>& x) {
        const size_t n = 1 << 15;
        std::vector<float> w (x.begin() + 4800, x.begin() + 4800 + (std::ptrdiff_t) n);
        const auto db = testutil::spectrumDb (w, n);
        const size_t top = (size_t) (15000.0 / (44100.0 / (double) n));
        double e = 0.0;
        for (size_t i = 1; i < top; ++i) e += std::pow (10.0, db[i] / 10.0);
        return 10.0 * std::log10 (e + 1.0e-20);
    };
    double refRms = 0.0, refBand = 0.0;
    for (int q = 0; q < 3; ++q)
    {
        p.set (P::Quality, (float) q);
        const Render out = play (p, 1.0, 44100.0, 128);
        const double band = bandDb (out.l);
        if (q == 0) { refRms = out.rmsDb; refBand = band; }
        INFO ("quality " << q << ": rms " << out.rmsDb << " dB, band " << band << " dB");
        CHECK (out.rmsDb > -50.0);
        CHECK (out.preClip <= 1.f);
        CHECK (std::abs (out.rmsDb - refRms) < 0.5);
        CHECK (std::abs (band - refBand) < 1.0);
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
        INFO ("trial " << trial);
        CHECK (out.preClip < 16.f);   // +24 dBFS: loud is allowed, a runaway is not
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
    // loudness over both channels (the left alone says nothing about a spread)
    const auto totalDb = [] (const Render& r) {
        double s = 0.0;
        for (size_t i = 0; i < r.l.size(); ++i) s += (double) r.l[i] * r.l[i] + (double) r.r[i] * r.r[i];
        return 10.0 * std::log10 (s / (double) (2 * r.l.size()) + 1e-20);
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
        // measured: within 1.0 dB (analog), 1.8 dB (wavetable, FM: the detuned copies only add up in power)
        CHECK (std::abs (totalDb (wide) - totalDb (one)) < 2.0);
        CHECK (std::abs (totalDb (narrow) - totalDb (one)) < 2.0);
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

TEST_CASE ("Jilax's clipper at the end of the chain doesn't fold back", "[engine][fx][alias]")
{
    // A high-passed saw on G#1 into a hard clip with Auto Level at 23 dB,
    // last in the chain, as Jilax plays it. The voices' oversampling is gone
    // there: at the base rate the top octaves came back about 13 dB under
    // the harmonics.
    ParamState p;
    p.set (P::OscAUnison, 1.f);
    p.set (P::F1Type, (float) FilterType::Off);
    p.set (P::F2Type, (float) FilterType::HP24);
    p.set (P::F2Cutoff, 200.f);
    p.set (P::AenvS, 1.f);
    p.set (P::DistOn, 1.f);
    p.set (P::DistType, 1.f);
    p.set (P::DistAuto, 1.f);
    p.set (P::DistDrive, 23.f);
    p.set (P::DistTone, 20000.f);
    p.set (P::DistMix, 1.f);
    p.set (P::DistLowCut, 140.f);
    p.set (P::FxOrder, (float) fxOrderCode (FxChain { (int) Fx::Crush, (int) Fx::Shift, (int) Fx::Phaser, (int) Fx::Chorus, (int) Fx::Compressor,
                                                     (int) Fx::Delay, (int) Fx::Reverb, (int) Fx::Width, (int) Fx::Distortion }));
    const double f0 = 440.0 * std::pow (2.0, (32 - 69) / 12.0);
    const auto out = hold (p, 2.0, -1.0, 32);
    const double top = testutil::inharmonicDb (out.l, 48000.0, f0, 6000.0, 20000.0);
    const double mid = testutil::inharmonicDb (out.l, 48000.0, f0, 2000.0, 6000.0);
    INFO ("off the harmonics: 6-20 kHz " << top << " dB, 2-6 kHz " << mid << " dB");
    CHECK (top < -45.0);
    CHECK (mid < -45.0);
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

        // the window runs past the beat's end: zero-padded there
        std::vector<std::complex<double>> a (1 << 14);
        const auto from = (size_t) (0.7 * beat * fs);
        for (size_t i = 0; i < a.size(); ++i)
            a[i] = (from + i < out.l.size() ? out.l[from + i] : 0.f) * (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) a.size()));
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
        for (size_t i = 0; i < n; ++i) a[i] = out.l[from + i] * (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) n));
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
            a[i] = (from + i < mid.size() ? mid[from + i] : 0.f) * (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) a.size()));
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

// ---------------------------------------------------------------------------
// Review fixes: modulation that doesn't depend on the host's block size, clean
// hand-overs, what the stress tests really measure
// ---------------------------------------------------------------------------

namespace psy
{
    // Lets a test reach into the engine to break something on purpose.
    struct EngineTestAccess
    {
        static Voice& voice (Engine& e, size_t i) { return e.voices[i]; }
        static void poisonLevel (Voice& v) { v.level1 = std::numeric_limits<float>::quiet_NaN(); }
    };
} // namespace psy

namespace
{
    struct Stereo { std::vector<float> l, r; };

    // Renders `total` samples in blocks of `block`. `events` carry absolute
    // sample positions; `changes` give the patch from the block that holds
    // that sample on.
    Stereo renderBlocks (const ParamState& p, int total, int block, const std::vector<MidiEvent>& events,
                         const std::vector<std::pair<int, ParamState>>& changes = {}, double fs = 48000.0, Engine* use = nullptr)
    {
        Engine local;
        Engine& e = use != nullptr ? *use : local;
        e.setParams (p);
        e.prepare (fs, block);
        Stereo out { std::vector<float> ((size_t) total), std::vector<float> ((size_t) total) };
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            for (const auto& [at, st] : changes)
                if (at >= pos && at < pos + n)
                    e.setParams (st);
            std::vector<MidiEvent> ev;
            for (auto m : events)
                if (m.offset >= pos && m.offset < pos + n)
                {
                    m.offset -= pos;
                    ev.push_back (m);
                }
            e.process (out.l.data() + pos, out.r.data() + pos, n, ev.data(), (int) ev.size(), 145.0);
        }
        return out;
    }

    MidiEvent noteAt (int sample, int note, float vel = 0.8f) { return { sample, MidiEvent::NoteOn, note, vel }; }
    MidiEvent offAt (int sample, int note) { return { sample, MidiEvent::NoteOff, note, 0.f }; }

    // How far `b` is from `a` in dB below `a`'s power (over the samples from `from`).
    double diffDb (const std::vector<float>& a, const std::vector<float>& b, size_t from = 0)
    {
        double d = 0.0, s = 0.0;
        for (size_t i = from; i < a.size(); ++i)
        {
            d += ((double) a[i] - b[i]) * ((double) a[i] - b[i]);
            s += (double) a[i] * a[i];
        }
        return 10.0 * std::log10 ((d + 1.0e-30) / (s + 1.0e-30));
    }

    double powerDb (const std::vector<float>& x, size_t from, size_t to)
    {
        double s = 0.0;
        for (size_t i = from; i < std::min (to, x.size()); ++i) s += (double) x[i] * x[i];
        return 10.0 * std::log10 (s / (double) std::max<size_t> (1, to - from) + 1.0e-20);
    }

    float maxStepOf (const std::vector<float>& x, size_t from, size_t to)
    {
        float m = 0.f;
        for (size_t i = std::max<size_t> (1, from); i < std::min (to, x.size()); ++i)
            m = std::max (m, std::abs (x[i] - x[i - 1]));
        return m;
    }

    // A clean sustained sine: nothing but the engine's own gain moves it.
    ParamState sinePatch()
    {
        ParamState p;
        p.set (P::OscAShape, 0.f);
        p.set (P::F1Type, 0.f);
        p.set (P::AenvA, 0.f);
        p.set (P::AenvS, 1.f);
        p.set (P::AenvR, 20.f);
        p.set (P::MasterVol, -6.f);
        return p;
    }

    ParamState phosBase()
    {
        ParamState p;
        p.set (P::EngineMode, (float) EngineMode::Phoscyon);
        p.set (P::MasterVol, 0.f);
        p.set (P::PhosDistOn, 0.f);
        p.set (P::PhosEqOn, 0.f);
        p.set (P::PhosChoOn, 0.f);
        p.set (P::PhosDlyOn, 0.f);
        p.set (P::PhosRevOn, 0.f);
        p.set (P::PhosLimOn, 0.f);
        p.set (P::PhosCutoff, 0.35f);
        p.set (P::PhosReso, 0.4f);
        p.set (P::PhosEnvMod, 0.2f);
        return p;
    }
} // namespace

TEST_CASE ("Static patches render the same bits whatever the block size", "[engine][blocks]")
{
    ParamState p;
    p.set (P::DistOn, 1.f);
    p.set (P::ChorusOn, 1.f);
    p.set (P::DelayOn, 1.f);
    p.set (P::ReverbOn, 1.f);
    p.set (P::OscAUnison, 5.f);
    p.set (P::AenvR, 100.f);
    const std::vector<MidiEvent> ev { noteAt (0, 48), offAt(18000, 48) };
    const auto ref = renderBlocks (p, 36000, 4096, ev);
    for (int block : { 1, 7, 64 })
    {
        const auto x = renderBlocks (p, 36000, block, ev);
        INFO ("block " << block);
        CHECK (x.l == ref.l);
        CHECK (x.r == ref.r);
    }
}

TEST_CASE ("The 303 follows an LFO sample by sample, not once per host block", "[engine][blocks][phoscyon]")
{
    // The global LFO buffer was read at index 0 of every chunk: blocks of
    // 1024 updated the cutoff once per block (-2.9 dB against blocks of 16).
    ParamState p = phosBase();
    p.set (P::Lfo1Shape, 0.f);
    p.set (P::Lfo1Rate, 9.f);
    p.set (P::Lfo1Retrig, 0.f);
    p.set (P::Mod1Src, (float) ModSrc::Lfo1);
    p.set (P::Mod1Dst, (float) ModDst::PhosCutoff);
    p.set (P::Mod1Amt, 0.9f);
    const std::vector<MidiEvent> ev { noteAt (0, 45) };
    const auto ref = renderBlocks (p, 48000, 16, ev);
    // The matrix is read on a 16-sample grid counted across blocks, so blocks
    // that split it elsewhere (7, 1) read it at the same samples too: they
    // were -43 dB off when it was read at every chunk's start.
    for (int block : { 1, 7, 64, 1024 })
    {
        const auto x = renderBlocks (p, 48000, block, ev);
        INFO ("block " << block << ": " << diffDb (ref.l, x.l) << " dB");
        CHECK (diffDb (ref.l, x.l) < -60.0);
    }
}

TEST_CASE ("Effects follow per-voice envelopes and LFOs the same at any block size", "[engine][blocks][fx]")
{
    // The effects read the newest voice's sources as they were at the end of
    // the block: Env -> Drive in blocks of 1024 differed from blocks of 32
    // by -3.3 dB. Now each 32-sample slice reads the value at its own samples.
    ParamState p;
    p.set (P::DistOn, 1.f);
    p.set (P::DistDrive, def (P::DistDrive).toPlain (0.35f));
    p.set (P::DistMix, def (P::DistMix).toPlain (0.7f));
    p.set (P::AenvA, 5.f);
    p.set (P::AenvD, 150.f);
    p.set (P::AenvS, 0.3f);
    p.set (P::AenvR, 100.f);
    p.set (P::Lfo1Shape, 0.f);
    p.set (P::Lfo1Rate, 7.f);
    p.set (P::Lfo1Retrig, 0.f);
    p.set (P::Mod1Src, (float) ModSrc::AmpEnv);
    p.set (P::Mod1Dst, (float) ModDst::DistDrive);
    p.set (P::Mod1Amt, 0.6f);
    p.set (P::Mod2Src, (float) ModSrc::Lfo1);
    p.set (P::Mod2Dst, (float) ModDst::DistMix);
    p.set (P::Mod2Amt, 0.4f);
    const std::vector<MidiEvent> ev { noteAt (0, 48), offAt (24000, 48) };
    // the effects refresh every 32 samples counted across blocks, so every
    // block layout sees the same values at the same samples
    const auto ref = renderBlocks (p, 36000, 32, ev);
    for (int block : { 1, 7, 16, 64, 1024, 4096 })
    {
        const auto x = renderBlocks (p, 36000, block, ev);
        INFO ("block " << block << ": " << diffDb (ref.l, x.l) << " dB");
        CHECK (diffDb (ref.l, x.l) < -60.0);
    }

    // the distortion later in the chain, and the 303's own envelopes into its effects
    ParamState q = phosBase();
    q.set (P::PhosDistOn, 1.f);
    q.set (P::Mod1Src, (float) ModSrc::AmpEnv);
    q.set (P::Mod1Dst, (float) ModDst::PhosDistDrive);
    q.set (P::Mod1Amt, 0.7f);
    const std::vector<MidiEvent> pev { noteAt (0, 45), offAt (12000, 45) };
    const auto pref = renderBlocks (q, 24000, 32, pev);
    for (int block : { 1, 7, 1024 })
    {
        INFO ("303, block " << block);
        CHECK (diffDb (pref.l, renderBlocks (q, 24000, block, pev).l) < -60.0);
    }
}

TEST_CASE ("A macro on a free-running LFO's rate changes it, and a voice's LFO rate ignores the block size", "[engine][blocks][lfo]")
{
    // Retrig off: one LFO for every voice, which ignored its Rate targets
    // (the macros labelled "LFO 3 Rate" did nothing).
    ParamState p;
    p.set (P::F1Type, (float) FilterType::LP12);
    p.set (P::F1Cutoff, 900.f);
    p.set (P::AenvS, 1.f);
    p.set (P::Lfo1Shape, 0.f);
    p.set (P::Lfo1Rate, 2.f);
    p.set (P::Lfo1Retrig, 0.f);
    p.set (P::Mod1Src, (float) ModSrc::Lfo1);
    p.set (P::Mod1Dst, (float) ModDst::F1Cutoff);
    p.set (P::Mod1Amt, 0.8f);
    p.set (P::Mod2Src, (float) ModSrc::Macro1);
    p.set (P::Mod2Dst, (float) ModDst::Lfo1Rate);
    p.set (P::Mod2Amt, 1.f);
    const std::vector<MidiEvent> ev { noteAt (0, 40) };
    p.set (P::Macro1, 0.f);
    const auto slow = renderBlocks (p, 48000, 256, ev);
    p.set (P::Macro1, 1.f);
    const auto fast = renderBlocks (p, 48000, 256, ev);
    CHECK (diffDb (slow.l, fast.l, 12000) > -15.0);

    // the same LFO per voice (Retrig on): its rate offset carries over from
    // chunk to chunk, so the chunking doesn't matter
    p.set (P::Lfo1Retrig, 1.f);
    p.set (P::Macro1, 0.5f);
    const auto ref = renderBlocks (p, 24000, 1024, ev);
    for (int block : { 1, 7, 100 })
    {
        const auto x = renderBlocks (p, 24000, block, ev);
        INFO ("block " << block << ": " << diffDb (ref.l, x.l) << " dB");
        CHECK (diffDb (ref.l, x.l) < -60.0);
    }

    // and in Phoscyon mode, where every LFO is a free one
    ParamState q = phosBase();
    q.set (P::Lfo1Shape, 0.f);
    q.set (P::Lfo1Rate, 2.f);
    q.set (P::Mod1Src, (float) ModSrc::Lfo1);
    q.set (P::Mod1Dst, (float) ModDst::PhosCutoff);
    q.set (P::Mod1Amt, 0.9f);
    q.set (P::Mod2Src, (float) ModSrc::Macro1);
    q.set (P::Mod2Dst, (float) ModDst::Lfo1Rate);
    q.set (P::Mod2Amt, 1.f);
    const std::vector<MidiEvent> pev { noteAt (0, 45) };
    q.set (P::Macro1, 0.f);
    const auto pslow = renderBlocks (q, 48000, 256, pev);
    q.set (P::Macro1, 1.f);
    const auto pfast = renderBlocks (q, 48000, 256, pev);
    CHECK (diffDb (pslow.l, pfast.l, 12000) > -15.0);
}

TEST_CASE ("Volume and Level moves ramp across the block instead of stepping at its edge", "[engine][click]")
{
    // -6 -> -30 dB: the smoothed value at the end of a 1024-sample block
    // was applied to the whole next block, a 14 dB step at the block edge.
    ParamState p = sinePatch();
    ParamState quiet = p;
    quiet.set (P::MasterVol, -30.f);
    const std::vector<MidiEvent> ev { noteAt (0, 36) };
    const auto x = renderBlocks (p, 48000, 1024, ev, { { 24000, quiet } });
    const float steady = maxStepOf (x.l, 12000, 23000);
    REQUIRE (steady > 0.f);
    CHECK (maxStepOf (x.l, 23000, 40000) < 1.5f * steady);
}

TEST_CASE ("A quality change mid-note fades the voices out instead of cutting them", "[engine][click]")
{
    ParamState p = sinePatch();
    ParamState other = p;
    other.set (P::Quality, 2.f);
    const std::vector<MidiEvent> ev { noteAt (0, 36) };
    for (int from : { 1, 2, 0 })
    {
        p.set (P::Quality, (float) from);
        other.set (P::Quality, (float) (from == 2 ? 0 : 2));
        const auto x = renderBlocks (p, 24000, 256, ev, { { 12000, other } });
        const float steady = maxStepOf (x.l, 6000, 11000);
        INFO ("quality " << from << " -> " << (from == 2 ? 0 : 2));
        REQUIRE (steady > 0.f);
        CHECK (maxStepOf (x.l, 11500, 14000) < 2.f * steady);
    }
    // nothing sounding: it switches at once
    Engine e;
    ParamState idle;
    e.setParams (idle);
    e.prepare (48000.0, 256);
    const int before = e.latencySamples();
    idle.set (P::Quality, 2.f);
    e.setParams (idle);
    std::vector<float> l (256), r (256);
    e.process (l.data(), r.data(), 256, nullptr, 0, 120.0);
    CHECK (e.latencySamples() != before);
}

TEST_CASE ("The 303's Amp modulation moves per sample", "[engine][click][phoscyon]")
{
    // The Amp target arrived per chunk after the voice's own smoothing: a
    // level step landed in one sample.
    ParamState p = phosBase();
    p.set (P::PhosCutoff, 0.15f);
    p.set (P::PhosReso, 0.f);
    const auto s = PhosSettings::from (p);
    PhosVoice v;
    v.prepare (48000.f, 96000.f);
    v.trigger (33, false, s);
    std::vector<float> l (96000, 0.f);
    PhosMods m;
    const int chunks = 3000 / 16;
    for (int c = 0; c < chunks; ++c)
    {
        m.level = c < chunks / 2 ? 1.f : 0.25f;
        v.render (l.data(), nullptr, c * 16, 16, s, m);
    }
    const size_t mid = (size_t) (chunks / 2) * 32;
    const float steady = maxStepOf (l, mid - 1500, mid - 10);
    REQUIRE (steady > 0.f);
    CHECK (maxStepOf (l, mid - 10, mid + 400) < 3.f * steady);
}

TEST_CASE ("A mono fallback and an arp hand-over keep the velocity of the held note", "[engine][notes]")
{
    // Both used a fixed velocity (0.8, and 1.0 or 0.8 for the arp).
    ParamState p = sinePatch();
    p.set (P::VelSens, 1.f);
    p.set (P::VoiceMode, (float) VoiceMode::Mono);
    p.set (P::MasterVol, 0.f);
    const auto level = [] (const Stereo& x) { return powerDb (x.l, 36000, 42000); };
    const double reference = level (renderBlocks (p, 48000, 256, { noteAt (0, 60, 0.25f) }));
    const double fallback = level (renderBlocks (p, 48000, 256, { noteAt (0, 60, 0.25f), noteAt (9600, 64, 1.f), offAt (19200, 64) }));
    INFO ("reference " << reference << " dB, fallback " << fallback << " dB");
    CHECK (std::abs (fallback - reference) < 1.0);

    ParamState arp = p;
    arp.set (P::VoiceMode, (float) VoiceMode::Poly);
    ParamState off = arp;
    arp.set (P::ArpOn, 1.f);
    const double handed = level (renderBlocks (arp, 48000, 256, { noteAt (0, 60, 0.25f) }, { { 24000, off } }));
    INFO ("arp off: " << handed << " dB");
    CHECK (std::abs (handed - reference) < 1.0);
}

TEST_CASE ("The first note after a reset starts on its pitch, and free LFOs start from their phase", "[engine][notes]")
{
    ParamState p;
    p.set (P::OscAShape, 0.f);
    p.set (P::F1Type, 0.f);
    p.set (P::VoiceMode, (float) VoiceMode::Poly);
    p.set (P::Glide, 400.f);
    p.set (P::GlideMode, 0.f);   // glide always
    p.set (P::AenvA, 0.f);
    p.set (P::AenvS, 1.f);
    p.set (P::AenvR, 10.f);
    Engine e;
    e.setParams (p);
    e.prepare (48000.0, 256);
    std::vector<float> l (4096), r (4096);
    MidiEvent on { 0, MidiEvent::NoteOn, 36, 0.8f }, off { 0, MidiEvent::NoteOff, 36, 0.f };
    e.process (l.data(), r.data(), 256, &on, 1, 120.0);
    e.process (l.data(), r.data(), 256, nullptr, 0, 120.0);
    e.process (l.data(), r.data(), 256, &off, 1, 120.0);
    for (int b = 0; b < 20; ++b) e.process (l.data(), r.data(), 256, nullptr, 0, 120.0);
    e.reset();
    const MidiEvent high { 0, MidiEvent::NoteOn, 72, 0.8f };
    e.process (l.data(), r.data(), 4096, &high, 1, 120.0);
    const double hz = testutil::peakFrequency (l, 48000.0, 4096);
    INFO (hz << " Hz");
    CHECK (std::abs (hz - 523.25) < 0.03 * 523.25);   // it used to glide up from the note before the swap

    ParamState q;
    q.set (P::Lfo1Shape, 0.f);
    q.set (P::Lfo1Rate, 2.f);
    q.set (P::Lfo1Retrig, 0.f);
    q.set (P::Lfo1Phase, 0.5f);
    q.set (P::AenvS, 1.f);
    Engine f;
    f.setParams (q);
    f.prepare (48000.0, 256);
    const MidiEvent n60 { 0, MidiEvent::NoteOn, 60, 0.8f };
    f.process (l.data(), r.data(), 256, &n60, 1, 120.0);
    for (int b = 0; b < 206; ++b) f.process (l.data(), r.data(), 256, nullptr, 0, 120.0);   // 1.1 s: the LFO is well on
    f.reset();
    f.process (l.data(), r.data(), 16, &n60, 1, 120.0);
    ModView view;
    f.getModView (view);
    REQUIRE (view.numVoices == 1);
    CHECK (view.voices[0].lfo[0] == Catch::Approx (0.5f).margin (0.02f));
}

TEST_CASE ("A synced free LFO stays on the bar while the host plays", "[engine][lfo]")
{
    int quarter = -1;
    for (int i = 0; i < 32; ++i)
        if (syncDivBeats (i) == 1.f) { quarter = i; break; }
    REQUIRE (quarter >= 0);
    ParamState p;
    p.set (P::Lfo1Shape, 0.f);
    p.set (P::Lfo1Sync, 1.f);
    p.set (P::Lfo1Div, (float) quarter);
    p.set (P::Lfo1Retrig, 0.f);
    p.set (P::Lfo1Phase, 0.25f);
    p.set (P::AenvS, 1.f);
    Engine e;
    e.setParams (p);
    e.prepare (48000.0, 512);
    std::vector<float> l (512), r (512);
    const double spb = 24000.0;   // 120 BPM
    const MidiEvent on { 0, MidiEvent::NoteOn, 60, 0.8f };
    double ppq = 0.0;
    const auto phaseNow = [&] {
        ModView v;
        e.getModView (v);
        return v.numVoices > 0 ? std::fmod (v.voices[0].lfo[0], 1.f) : -1.f;
    };
    const auto expectedNow = [&] {
        const double c = ppq + 512.0 / spb + 0.25;   // a one-beat cycle
        return (float) (c - std::floor (c));
    };
    const auto close = [] (float a, float b) {
        float d = std::abs (a - b);
        return std::min (d, 1.f - d) < 0.01f;
    };
    e.process (l.data(), r.data(), 512, &on, 1, Transport { 120.0, ppq, true });
    CHECK (close (phaseNow(), expectedNow()));
    // the host jumps: a seek lands the LFO where the bar says
    for (double jump : { 100.3, 7.77, 0.4 })
    {
        ppq = jump;
        e.process (l.data(), r.data(), 512, nullptr, 0, Transport { 120.0, ppq, true });
        INFO ("seek to " << jump);
        CHECK (close (phaseNow(), expectedNow()));
        for (int b = 0; b < 20; ++b)
        {
            ppq += 512.0 / spb;
            e.process (l.data(), r.data(), 512, nullptr, 0, Transport { 120.0, ppq, true });
        }
        CHECK (close (phaseNow(), expectedNow()));
    }
}

TEST_CASE ("The mod wheel is smoothed instead of stepping a cutoff", "[engine][click]")
{
    ParamState p;
    p.set (P::F1Type, (float) FilterType::LP12);
    p.set (P::F1Cutoff, 500.f);
    p.set (P::AenvS, 1.f);
    p.set (P::Mod1Src, (float) ModSrc::ModWheel);
    p.set (P::Mod1Dst, (float) ModDst::F1Cutoff);
    p.set (P::Mod1Amt, 0.5f);   // 4 octaves at the top
    Engine e;
    e.setParams (p);
    e.prepare (48000.0, 48);
    std::vector<float> l (48), r (48);
    const MidiEvent on { 0, MidiEvent::NoteOn, 48, 0.8f }, wheel { 0, MidiEvent::Controller, 1, 1.f };
    e.process (l.data(), r.data(), 48, &on, 1, 120.0);
    for (int b = 0; b < 10; ++b) e.process (l.data(), r.data(), 48, nullptr, 0, 120.0);
    const auto octaves = [&] {
        ModView v;
        e.getModView (v);
        return std::log2 (v.voices[0].filter[0].cutoff / 500.f);
    };
    CHECK (octaves() == Catch::Approx (0.0).margin (0.01));
    e.process (l.data(), r.data(), 48, &wheel, 1, 120.0);   // 1 ms after the step
    const double soon = octaves();
    CHECK (soon < 1.2);       // a quarter of the way at most (it was all 4 octaves at once)
    CHECK (soon > 0.05);
    for (int b = 0; b < 40; ++b) e.process (l.data(), r.data(), 48, nullptr, 0, 120.0);   // 40 ms later
    CHECK (octaves() > 3.8);
}

TEST_CASE ("A unison stack that stops spreading hands the right channel over smoothly", "[engine][click][unison]")
{
    // The right channel ran its own filters; with the spread gone it jumped
    // to a copy of the left in one sample.
    ParamState p;
    p.set (P::OscAUnison, 5.f);
    p.set (P::OscAUniWidth, 1.f);
    p.set (P::OscAUniRange, 12.f);
    p.set (P::F1Type, (float) FilterType::LP12);
    p.set (P::F1Cutoff, 400.f);
    p.set (P::F1Reso, 0.4f);
    p.set (P::AenvS, 1.f);
    p.set (P::MasterVol, 0.f);
    ParamState mono = p;
    mono.set (P::OscAUnison, 1.f);
    const auto x = renderBlocks (p, 24000, 256, { noteAt (0, 40) }, { { 12032, mono } });
    const float steady = maxStepOf (x.r, 6000, 11000);
    REQUIRE (steady > 0.f);
    INFO ("steady " << steady << ", across the switch " << maxStepOf (x.r, 12000, 12700));
    CHECK (maxStepOf (x.r, 12000, 12700) < 2.f * steady);   // the old hand-over jumped 12x
}

TEST_CASE ("The Phoscyon limiter reports its lookahead and starts empty when it comes back on", "[engine][phoscyon]")
{
    ParamState p = phosBase();
    p.set (P::AenvR, 5.f);
    p.set (P::PhosLimThresh, 0.f);
    p.set (P::PhosLimGain, 0.f);
    p.set (P::PhosLimClip, 0.f);
    const std::vector<MidiEvent> ev { noteAt (2000, 45), offAt (6000, 45) };
    const auto firstSound = [] (const Stereo& x) {
        for (size_t i = 0; i < x.l.size(); ++i)
            if (std::abs (x.l[i]) > 0.003f) return (int) i;
        return -1;
    };
    Engine offEngine, onEngine;
    ParamState on = p;
    on.set (P::PhosLimOn, 1.f);
    const auto a = renderBlocks (p, 8000, 128, ev, {}, 48000.0, &offEngine);
    const auto b = renderBlocks (on, 8000, 128, ev, {}, 48000.0, &onEngine);
    const int lookahead = (int) std::lround (dsp::PhosLimiter::lookaheadMs * 0.001 * 48000.0);
    REQUIRE (firstSound (a) > 0);
    CHECK (onEngine.latencySamples() - offEngine.latencySamples() == lookahead);
    CHECK (firstSound (b) - firstSound (a) == lookahead);

    // off while a note sounds, back on in silence: no 3.3 ms of the old note
    ParamState lim = p;
    lim.set (P::PhosLimOn, 1.f);
    const std::vector<MidiEvent> long_ { noteAt (0, 45), offAt (9000, 45) };
    const auto y = renderBlocks (lim, 36000, 128, long_, { { 4800, p }, { 24000, lim } });
    CHECK (powerDb (y.l, 24000, 25000) < -90.0);
}

// ---------------------------------------------------------------------------
// Ids, event positions, sample rates and qualities
// ---------------------------------------------------------------------------

namespace
{
    // Every parameter id in table order. The ids are the save format: a renamed or
    // moved one drops what presets and hosts stored under it, so this list is
    // the contract (append new ids at the end of the table and of this list).
    constexpr const char* kGoldenIds[] = {
        "master_vol", "patch_gain", "voice_mode", "poly_voices", "glide", "glide_mode", "bend_range", "accent",
        "octave", "vel_sens", "quality", "oscA_on", "oscA_type", "oscA_shape", "oscA_pw", "oscA_coarse", "oscA_fine",
        "oscA_level", "oscA_detune", "oscA_wt", "oscA_wtpos", "oscA_fmratio", "oscA_fm", "oscA_fmfb", "oscA_retrig",
        "oscA_unison", "oscA_blend", "oscA_uni_width", "oscA_uni_range", "oscA_uni_mode", "oscA_uni_stack",
        "oscA_uni_spread", "oscB_on", "oscB_type", "oscB_shape", "oscB_pw", "oscB_coarse", "oscB_fine", "oscB_level",
        "oscB_detune", "oscB_wt", "oscB_wtpos", "oscB_fmratio", "oscB_fm", "oscB_fmfb", "oscB_retrig", "oscB_sync",
        "oscB_xmod", "oscB_unison", "oscB_blend", "oscB_uni_width", "oscB_uni_range", "oscB_uni_mode",
        "oscB_uni_stack", "oscB_uni_spread", "sub_level", "sub_oct", "sub_shape", "noise_level", "noise_type",
        "noise_direct", "ring_level", "penv_amt", "penv_attack", "penv_decay", "penv_curve", "penv_dest", "f1_type",
        "f1_cutoff", "f1_reso", "f1_drive", "f1_env", "f1_key", "f1_vowel", "f2_type", "f2_cutoff", "f2_reso",
        "f2_drive", "f2_env", "f2_key", "f_routing", "aenv_a", "aenv_d", "aenv_s", "aenv_r", "aenv_curve",
        "aenv_start", "fenv_a", "fenv_d", "fenv_s", "fenv_r", "menv_a", "menv_d", "menv_s", "menv_r", "lfo1_shape",
        "lfo1_rate", "lfo1_sync", "lfo1_div", "lfo1_retrig", "lfo1_phase", "lfo2_shape", "lfo2_rate", "lfo2_sync",
        "lfo2_div", "lfo2_retrig", "lfo2_phase", "lfo3_shape", "lfo3_rate", "lfo3_sync", "lfo3_div", "lfo3_retrig",
        "lfo3_phase", "mod1_src", "mod1_dst", "mod1_amt", "mod2_src", "mod2_dst", "mod2_amt", "mod3_src", "mod3_dst",
        "mod3_amt", "mod4_src", "mod4_dst", "mod4_amt", "mod5_src", "mod5_dst", "mod5_amt", "mod6_src", "mod6_dst",
        "mod6_amt", "mod7_src", "mod7_dst", "mod7_amt", "mod8_src", "mod8_dst", "mod8_amt", "mod9_src", "mod9_dst",
        "mod9_amt", "mod10_src", "mod10_dst", "mod10_amt", "mod11_src", "mod11_dst", "mod11_amt", "mod12_src",
        "mod12_dst", "mod12_amt", "mod13_src", "mod13_dst", "mod13_amt", "mod14_src", "mod14_dst", "mod14_amt",
        "mod15_src", "mod15_dst", "mod15_amt", "mod16_src", "mod16_dst", "mod16_amt", "macro1", "macro2", "macro3",
        "macro4", "dist_on", "dist_type", "dist_drive", "dist_tone", "dist_mix", "dist_lowcut", "dist_auto",
        "crush_on", "crush_bits", "crush_rate", "crush_mix", "fshift_on", "fshift_hz", "fshift_fb", "fshift_spread",
        "fshift_mix", "phaser_on", "phaser_rate", "phaser_depth", "phaser_fb", "phaser_center", "phaser_mix",
        "chorus_on", "chorus_mode", "chorus_rate", "chorus_depth", "chorus_fb", "chorus_mix", "delay_on",
        "delay_sync", "delay_div", "delay_ms", "delay_fb", "delay_ping", "delay_lowcut", "delay_highcut", "delay_mix",
        "reverb_on", "reverb_size", "reverb_decay", "reverb_damp", "reverb_pre", "reverb_mix", "comp_on", "comp_mode",
        "comp_thresh", "comp_ratio", "comp_knee", "comp_attack", "comp_release", "comp_mix", "comp_out", "comp_down",
        "comp_up", "mb_xlow", "mb_xhigh", "mb_low_down", "mb_mid_down", "mb_high_down", "mb_low_up", "mb_mid_up",
        "mb_high_up", "mb_low_gain", "mb_mid_gain", "mb_high_gain", "width_on", "width_amount", "width_stereo",
        "width_time", "width_mono", "fx_order", "energy_mode", "energy_bars", "energy_sec", "energy_curve",
        "energy_delay", "energy_fade", "energy_cut", "energy_oneshot", "arp_on", "arp_mode", "arp_div", "arp_oct",
        "arp_gate", "arp_swing", "arp_hold", "arp_length", "arp_key", "arp_scale", "arp_step1", "arp_acc1",
        "arp_slide1", "arp_note1", "arp_step2", "arp_acc2", "arp_slide2", "arp_note2", "arp_step3", "arp_acc3",
        "arp_slide3", "arp_note3", "arp_step4", "arp_acc4", "arp_slide4", "arp_note4", "arp_step5", "arp_acc5",
        "arp_slide5", "arp_note5", "arp_step6", "arp_acc6", "arp_slide6", "arp_note6", "arp_step7", "arp_acc7",
        "arp_slide7", "arp_note7", "arp_step8", "arp_acc8", "arp_slide8", "arp_note8", "arp_step9", "arp_acc9",
        "arp_slide9", "arp_note9", "arp_step10", "arp_acc10", "arp_slide10", "arp_note10", "arp_step11", "arp_acc11",
        "arp_slide11", "arp_note11", "arp_step12", "arp_acc12", "arp_slide12", "arp_note12", "arp_step13",
        "arp_acc13", "arp_slide13", "arp_note13", "arp_step14", "arp_acc14", "arp_slide14", "arp_note14",
        "arp_step15", "arp_acc15", "arp_slide15", "arp_note15", "arp_step16", "arp_acc16", "arp_slide16",
        "arp_note16", "engine_mode", "kick_tune", "kick_keytrack", "kick_start", "kick_snap", "kick_knee",
        "kick_sweep", "kick_attack", "kick_hold", "kick_decay", "kick_punch", "kick_punch_len", "kick_click_type",
        "kick_click_level", "kick_click_decay", "kick_click_tone", "kick_drive_type", "kick_drive", "kick_tone",
        "phos_wave", "phos_tune", "phos_cutoff", "phos_reso", "phos_envmod", "phos_decay", "phos_accent",
        "phos_slide", "phos_sweep", "phos_env_attack", "phos_acc_decay", "phos_acc_vol", "phos_vib_speed",
        "phos_vib_depth", "phos_reso_couple", "phos_acc_couple", "phos_tm3", "phos_c21", "phos_acc_fenv",
        "phos_acc_aenv", "phos_c13", "phos_tm5", "phos_pw_shift", "phos_clicks", "phos_noise", "phos_dist_on",
        "phos_dist_model", "phos_dist_dyn", "phos_dist_order", "phos_dist_drive", "phos_dist_color",
        "phos_eq_low_gain", "phos_eq_low_freq", "phos_eq_low_width", "phos_eq_midlow_gain", "phos_eq_midlow_freq",
        "phos_eq_midlow_width", "phos_eq_midhigh_gain", "phos_eq_midhigh_freq", "phos_eq_midhigh_width",
        "phos_eq_high_gain", "phos_eq_high_freq", "phos_eq_high_width", "phos_eq_on", "phos_dly_on", "phos_dly_fb",
        "phos_dly_cut", "phos_dly_reso", "phos_dly_filter", "phos_dly_spread", "phos_dly_time", "phos_dly_sync",
        "phos_dly_lscale", "phos_dly_lnum", "phos_dly_lden", "phos_dly_rscale", "phos_dly_rnum", "phos_dly_rden",
        "phos_dly_mix", "phos_rev_on", "phos_rev_pre", "phos_rev_esize", "phos_rev_ediff", "phos_rev_lsize",
        "phos_rev_ldiff", "phos_rev_fb", "phos_rev_blend", "phos_rev_mod", "phos_rev_ebright", "phos_rev_lbright",
        "phos_rev_mix", "phos_cho_on", "phos_cho_offset", "phos_cho_depth", "phos_cho_rate", "phos_cho_spatial",
        "phos_cho_fat", "phos_cho_hp", "phos_cho_out", "phos_cho_mix", "phos_lim_on", "phos_lim_knee",
        "phos_lim_ratio", "phos_lim_attack", "phos_lim_release", "phos_lim_thresh", "phos_lim_gain", "phos_lim_clip",
        "phos_eq_slot", "phos_lim_slot", "render_quality",
    };
} // namespace

TEST_CASE ("Parameter ids are unique and match the saved list, in order", "[params]")
{
    // The ids are the save format: presets and host state store values by id,
    // so a renamed id drops them and a moved one hands them to another
    // parameter. Adding a parameter means appending it here as well.
    const auto& defs = paramDefs();
    std::set<std::string> seen;
    for (const auto& d : defs)
        CHECK (seen.insert (d.id).second);
    REQUIRE (defs.size() >= std::size (kGoldenIds));
    for (size_t i = 0; i < std::size (kGoldenIds); ++i)
    {
        INFO ("index " << i);
        CHECK (std::string (defs[i].id) == kGoldenIds[i]);
    }
    // anything past the list is new: it has to be added to the list
    CHECK (defs.size() == std::size (kGoldenIds));
}

TEST_CASE ("Events at the edges of a block land on the same sample as in any other block layout", "[engine][blocks]")
{
    ParamState p = sinePatch();
    p.set (P::AenvR, 30.f);
    const std::vector<MidiEvent> ev { noteAt (255, 48), offAt (700, 48), noteAt (1024, 55), offAt (1500, 55) };
    // 1-sample blocks put every event exactly where it says
    const auto one = renderBlocks (p, 3000, 1, ev);
    for (int block : { 256, 512, 1000 })
    {
        const auto x = renderBlocks (p, 3000, block, ev);
        INFO ("block " << block);
        CHECK (x.l == one.l);
        CHECK (x.r == one.r);
    }
    // note-on at the last sample of a block and at the first of the next: one sample apart
    const auto last = renderBlocks (p, 1024, 256, { noteAt (255, 48) });
    const auto first = renderBlocks (p, 1024, 256, { noteAt (256, 48) });
    for (size_t i = 0; i < 255; ++i) REQUIRE (last.l[i] == 0.f);
    CHECK (last.l[255] == 0.f);   // the onset ramp starts from silence
    CHECK (std::abs (last.l[400]) > 0.f);
    for (size_t i = 0; i < 256; ++i) REQUIRE (first.l[i] == 0.f);
    CHECK (first.l[256 + 145] == last.l[255 + 145]);   // the same note, one sample later: bits differ only by that shift
    // an offset equal to the block length happens at the start of the next block
    {
        Engine e;
        e.setParams (p);
        e.prepare (48000.0, 256);
        std::vector<float> l (1024), r (1024);
        const MidiEvent atEnd { 256, MidiEvent::NoteOn, 48, 0.8f };
        e.process (l.data(), r.data(), 256, &atEnd, 1, 145.0);
        e.process (l.data() + 256, r.data() + 256, 768, nullptr, 0, 145.0);
        CHECK (l == first.l);
        CHECK (r == first.r);
    }
    // a note-on and its note-off at the same sample: nothing sticks
    const std::vector<MidiEvent> blip { noteAt (300, 60), offAt (300, 60), noteAt (800, 61), offAt (800, 61) };
    Engine e;
    const auto y = renderBlocks (p, 24000, 256, blip, {}, 48000.0, &e);
    CHECK (e.activeVoices() == 0);
    CHECK (powerDb (y.l, 20000, 24000) < -80.0);
    CHECK (e.diagnostics().nonFinite == 0);
}

TEST_CASE ("Every category is silent at rest at every sample rate and quality", "[engine][rates]")
{
    Generator g;
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (int q = 0; q < 3; ++q)
            for (int c = 0; c < kNumCategories; ++c)
            {
                ParamState p = g.generate ((Category) c, 11u, ParamState(), LockSet());
                p.set (P::Quality, (float) q);
                Engine e;
                e.setParams (p);
                e.prepare (fs, 256);
                std::vector<float> l (256), r (256);
                float worst = 0.f;
                for (int b = 0; b < 8; ++b)
                {
                    e.process (l.data(), r.data(), 256, nullptr, 0, 145.0);
                    for (int i = 0; i < 256; ++i) worst = std::max ({ worst, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]) });
                }
                INFO (categoryInfo()[(size_t) c].name << " at " << fs << " Hz, quality " << q);
                REQUIRE (worst == 0.f);
            }
}

TEST_CASE ("The 303 and the kick play in tune at every sample rate and quality", "[engine][rates]")
{
    const auto meanHz = [] (const std::vector<float>& x, double fs, double t0, double t1) {
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
    };
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (int q = 0; q < 3; ++q)
        {
            // a plain 303 on A4
            ParamState p = phosBase();
            p.set (P::PhosCutoff, 0.f);
            p.set (P::PhosReso, 0.f);
            p.set (P::PhosEnvMod, 0.f);
            p.set (P::Quality, (float) q);
            const int total = (int) (0.6 * fs);
            auto x = renderBlocks (p, total, 256, { noteAt (0, 69, 0.4f) }, {}, fs);
            INFO ("303 at " << fs << " Hz, quality " << q);
            CHECK (std::abs (meanHz (x.l, fs, 0.2, 0.4) - 440.0) < 1.5);

            // a kick's body on its key, after the sweep has landed
            ParamState k;
            k.set (P::EngineMode, (float) EngineMode::Kick);
            k.set (P::MasterVol, 0.f);
            k.set (P::Quality, (float) q);
            x = renderBlocks (k, total, 256, { noteAt (0, 29, 1.f) }, {}, fs);
            const double want = kickBodyHz (KickSettings::from (k), 29);
            INFO ("kick at " << fs << " Hz, quality " << q);
            CHECK (std::abs (1200.0 * std::log2 (meanHz (x.l, fs, 0.1, 0.16) / want)) < 15.0);
        }
}

// ---- needs Engine::diagnostics() (the lines below don't build against the old engine) ----

TEST_CASE ("A NaN in a parameter, a velocity or a bend never reaches the sound", "[engine][stress]")
{
    // A NaN in the smoothed patch (F1Cutoff here) used to silence the engine
    // for good, and the tests couldn't see it: the output is always finite.
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    ParamState p = sinePatch();
    p.set (P::F1Type, (float) FilterType::LP12);
    p.v[(size_t) P::F1Cutoff] = nan;
    p.v[(size_t) P::OscALevel] = inf;
    p.v[(size_t) P::DistDrive] = -inf;
    p.v[(size_t) P::MasterVol] = nan;
    Engine e;
    e.setParams (p);
    e.prepare (48000.0, 256);
    std::vector<float> l (256), r (256);
    const MidiEvent events[] = { { 0, MidiEvent::PitchBend, 0, nan }, { 0, MidiEvent::Controller, 1, inf }, { 0, MidiEvent::ChannelPressure, 0, nan },
                                 { 0, MidiEvent::NoteOn, 200, 0.8f }, { 0, MidiEvent::NoteOn, -3, 0.8f }, { 0, MidiEvent::NoteOff, 999, 0.f },
                                 { 0, MidiEvent::NoteOn, 48, nan } };
    e.process (l.data(), r.data(), 256, events, 7, 120.0);
    float pk = 0.f;
    for (int b = 0; b < 20; ++b)
    {
        e.process (l.data(), r.data(), 256, nullptr, 0, 120.0);
        for (float v : l) pk = std::max (pk, std::abs (v));
    }
    CHECK (pk > 0.02f);
    CHECK (e.diagnostics().nonFinite == 0);
    CHECK (e.diagnostics().preClipPeak < 16.f);
}

TEST_CASE ("A voice that goes NaN is replaced and the engine plays on", "[engine][stress]")
{
    ParamState p = sinePatch();
    p.set (P::MasterVol, 0.f);
    Engine e;
    e.setParams (p);
    e.prepare (48000.0, 256);
    std::vector<float> l (256), r (256);
    const MidiEvent on { 0, MidiEvent::NoteOn, 48, 0.8f }, off { 0, MidiEvent::NoteOff, 48, 0.f }, next { 0, MidiEvent::NoteOn, 55, 0.8f };
    e.process (l.data(), r.data(), 256, &on, 1, 120.0);
    for (int b = 0; b < 10; ++b) e.process (l.data(), r.data(), 256, nullptr, 0, 120.0);
    CHECK (e.diagnostics().nonFinite == 0);

    EngineTestAccess::poisonLevel (EngineTestAccess::voice (e, 0));   // a filter or envelope state turned to NaN
    e.process (l.data(), r.data(), 256, nullptr, 0, 120.0);
    CHECK (e.diagnostics().nonFinite > 0);   // seen, and replaced by silence
    const auto seen = e.diagnostics().nonFinite;
    for (int b = 0; b < 20; ++b) e.process (l.data(), r.data(), 256, nullptr, 0, 120.0);
    CHECK (e.diagnostics().nonFinite == seen);   // that voice is gone, nothing more comes out of it
    CHECK (e.activeVoices() == 0);

    e.process (l.data(), r.data(), 256, &off, 1, 120.0);
    e.process (l.data(), r.data(), 256, &next, 1, 120.0);
    float pk = 0.f;
    for (int b = 0; b < 20; ++b)
    {
        e.process (l.data(), r.data(), 256, nullptr, 0, 120.0);
        for (float v : l) pk = std::max (pk, std::abs (v));
    }
    CHECK (pk > 0.05f);   // audio resumes with the next note
    CHECK (e.diagnostics().nonFinite == seen);
}
