#include "dsp/NoiseBank.h"
#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <string_view>
#include <vector>

using namespace psy::dsp;

namespace
{
    std::vector<std::complex<double>> spectrum (const float* x, int n)
    {
        std::vector<std::complex<double>> a ((size_t) n);
        for (int i = 0; i < n; ++i) a[(size_t) i] = x[i];
        testutil::fft (a);
        return a;
    }

    double rms (const float* x, int n)
    {
        double e = 0.0;
        for (int i = 0; i < n; ++i) e += (double) x[i] * x[i];
        return std::sqrt (e / n);
    }
} // namespace

// The bank's samples are loops: built around the end, so the step from the
// last sample to the first is an ordinary one; they stop at 20 kHz (every
// pitch below the original stays alias-free in the voice), and they share one
// loudness so changing the sample doesn't jump in level (the spiky ones, rain
// and crackle, a few dB under at the same peak).
TEST_CASE ("Every bank sample loops seamlessly, stops at 20 kHz and plays near one loudness", "[noise][bank]")
{
    const auto& bank = NoiseBank::shared();
    for (int i = 0; i < NoiseBank::kCount; ++i)
    {
        INFO (NoiseBank::name (i));
        const auto& c = bank.clip (i);
        REQUIRE (c.loop);
        REQUIRE (c.rate == 48000.f);
        REQUIRE ((c.frames & (c.frames - 1)) == 0);
        const auto& lv = c.levels[0];
        for (const float* x : { lv.left(), lv.right() })
        {
            const int n = lv.frames;
            // the wrap is a step like the loop's own (a click would stand out)
            double step = 0.0, peak = 0.0;
            for (int k = 1; k < n; ++k) step = std::max (step, (double) std::abs (x[k] - x[k - 1]));
            for (int k = 0; k < n; ++k) peak = std::max (peak, (double) std::abs (x[k]));
            CHECK (std::abs (x[0] - x[n - 1]) <= 1.25 * step);
            CHECK (peak < 0.95);
            const double level = 20.0 * std::log10 (rms (x, n));
            CHECK (level > -24.0);
            CHECK (level < -13.0);

            const auto s = spectrum (x, n);
            double all = 0.0, top = 0.0;
            for (int k = 1; k < n / 2; ++k)
            {
                const double e = std::norm (s[(size_t) k]);
                all += e;
                if (k * 48000.0 / n > 20100.0) top += e;
            }
            CHECK (10.0 * std::log10 (top / all + 1e-30) < -80.0);
        }
    }
}

// Each octave level is the one before it low-passed to half and taken at half
// the rate: a 5 kHz tone survives into level 1 (12 kHz wide) unchanged, a
// 15 kHz one is gone, and both stay seamless around the loop.
TEST_CASE ("A clip's octave levels are the halves, band-limited", "[noise][bank]")
{
    const int n = 1 << 14;
    std::vector<float> lo ((size_t) n), hi ((size_t) n);
    const int c5 = (int) std::lround (5000.0 * n / 48000.0), c15 = (int) std::lround (15000.0 * n / 48000.0);
    for (int i = 0; i < n; ++i)
    {
        lo[(size_t) i] = (float) (0.5 * std::sin (2.0 * psy::dsp::kPiD * c5 * i / n));
        hi[(size_t) i] = (float) (0.5 * std::sin (2.0 * psy::dsp::kPiD * c15 * i / n));
    }
    const auto keep = NoiseClip::make (lo, {}, 48000.f, true);
    const auto drop = NoiseClip::make (hi, {}, 48000.f, true);
    REQUIRE (keep.levels.size() >= 2);
    REQUIRE (keep.levels[1].frames == n / 2);
    CHECK (std::abs (20.0 * std::log10 (rms (keep.levels[1].left(), n / 2) / rms (lo.data(), n))) < 0.05);
    CHECK (20.0 * std::log10 (rms (drop.levels[1].left(), n / 2) / rms (hi.data(), n)) < -90.0);
    // the guards carry the loop's other end
    const auto& l1 = keep.levels[1];
    CHECK (l1.left()[-1] == l1.left()[l1.frames - 1]);
    CHECK (l1.left()[l1.frames] == l1.left()[0]);
    // a one-shot starts and ends in silence
    const auto once = NoiseClip::make (lo, {}, 48000.f, false);
    CHECK (once.levels[0].left()[-1] == 0.f);
    CHECK (once.levels[0].left()[n] == 0.f);
}

// The tonal samples are built on C, so a key-tracked sample plays in tune:
// the lowest partial within 20 dB of the strongest is a C (the strongest can
// be any harmonic a formant lifts), a chime's on the semitone grid from C.
TEST_CASE ("The bank's tonal samples sit on C", "[noise][bank]")
{
    const auto& bank = NoiseBank::shared();
    for (const char* tonal : { "Mains Hum", "Metal Drone", "Polar Drone", "Mantra", "Wind Chimes" })
    {
        INFO (tonal);
        int index = -1;
        for (int i = 0; i < NoiseBank::kCount; ++i)
            if (std::string_view (NoiseBank::name (i)) == tonal) index = i;
        REQUIRE (index >= 0);
        const auto& lv = bank.clip (index).levels[0];
        const auto s = spectrum (lv.left(), lv.frames);
        int best = 1;
        for (int k = 1; k < lv.frames / 2; ++k)
            if (std::norm (s[(size_t) k]) > std::norm (s[(size_t) best])) best = k;
        int lowest = best;
        for (int k = 2; k < best; ++k)
            if (std::norm (s[(size_t) k]) > 0.01 * std::norm (s[(size_t) best])
                && std::norm (s[(size_t) k]) >= std::norm (s[(size_t) k - 1]) && std::norm (s[(size_t) k]) >= std::norm (s[(size_t) k + 1]))
            {
                lowest = k;
                break;
            }
        const double hz = (std::string_view (tonal) == "Wind Chimes" ? best : lowest) * 48000.0 / lv.frames;
        const double cents = 1200.0 * std::log2 (hz / NoiseBank::kRootHz);
        const double off = cents - 1200.0 * std::round (cents / 1200.0);
        // a chime's strongest partial is any note of its pentatonic scale
        if (std::string_view (tonal) == "Wind Chimes")
            CHECK (std::abs (off - 100.0 * std::round (off / 100.0)) < 8.0);
        else
            CHECK (std::abs (off) < 12.0);   // Mantra's four voices sit up to 9 cents either side
    }
}

// The player reads the level a speed allows. A clip of five tones (1.5 to
// 22.5 kHz at 48 kHz) played up to three octaves up, on a 2x voice (96 kHz)
// and a 1x one: under 20 kHz nothing but the pitched tones, everything else
// (folds and the interpolator's images) 90 dB down at 2x, 80 at Eco's 1x
// (a tone in the levels' 0.42-0.5 transition leaves -85 dB there). A one-shot stops at its
// end, a loop runs on; a slow sweep through the level switches never jumps.
TEST_CASE ("The noise player never aliases into the audible band and sweeps without steps", "[noise][bank]")
{
    const int n = 1 << 15;
    static constexpr double kTones[] = { 1500.0, 4500.0, 10500.0, 16500.0, 22500.0 };
    std::vector<float> tones ((size_t) n, 0.f);
    for (double f : kTones)
    {
        const int c = (int) std::lround (f * n / 48000.0);
        for (int i = 0; i < n; ++i) tones[(size_t) i] += (float) (0.15 * std::sin (2.0 * psy::dsp::kPiD * c * i / n));
    }
    const auto clip = NoiseClip::make (tones, {}, 48000.f, true);

    for (double rate : { 96000.0, 48000.0 })
        for (double octaves : { 0.0, 0.3, 1.0, 1.6, 2.0, 3.0 })
        {
            INFO (rate << " Hz, up " << octaves << " octaves");
            const double ratio = std::pow (2.0, octaves), speed = 48000.0 / rate * ratio;
            NoiseSampler s;
            s.start (clip, 0.3);
            std::vector<float> out (1 << 16);
            float r;
            for (auto& v : out) s.process (clip, speed, (float) (1.0 - 20000.0 / rate), false, v, r);
            const auto db = testutil::spectrumDb (out, out.size());
            const double binHz = rate / (double) out.size();
            double stray = -300.0;
            for (size_t k = 2; k < db.size(); ++k)
            {
                const double f = (double) k * binHz;
                if (f > 20000.0) break;
                bool wanted = false;
                for (double t : kTones) wanted = wanted || std::abs (f - t * ratio) < 8.0 * binHz;
                if (! wanted) stray = std::max (stray, db[k]);
            }
            CHECK (stray < (rate > 50000.0 ? -90.0 : -80.0));
        }

    NoiseSampler once;
    const auto shot = NoiseClip::make (tones, {}, 48000.f, false);
    once.start (shot, 0.9);
    float l, r;
    int played = 0;
    for (int i = 0; i < n; ++i)
    {
        once.process (shot, 1.0, 0.58f, false, l, r);
        if (! once.finished()) ++played;
    }
    CHECK (std::abs (played - (int) (0.1 * n)) <= 2);
    CHECK (l == 0.f);

    // a sweep from 1 to 5 octaves up over two seconds: no sample-to-sample jump
    // much past the signal's own steps (a level switch would show as one)
    std::vector<float> lowTone ((size_t) n);
    const int c = 40;   // 58.6 Hz at 48 kHz over the loop
    for (int i = 0; i < n; ++i) lowTone[(size_t) i] = (float) std::sin (2.0 * psy::dsp::kPiD * c * i / n);
    const auto tone = NoiseClip::make (lowTone, {}, 48000.f, true);
    NoiseSampler sw;
    sw.start (tone, 0.0);
    float prev = 0.f, worst = 0.f, typical = 0.f;
    const int len = 96000 * 2;
    for (int i = 0; i < len; ++i)
    {
        const double speed = 0.5 * std::pow (2.0, 1.0 + 4.0 * i / len);
        sw.process (tone, speed, 0.79f, false, l, r);
        if (i > 0)
        {
            const float d = std::abs (l - prev);
            const float expected = (float) (2.0 * psy::dsp::kPiD * 58.6 * speed / 48000.0 * 2.0);   // a sine's steepest step, doubled
            worst = std::max (worst, d / std::max (expected, 1e-6f));
            typical = std::max (typical, expected);
        }
        prev = l;
    }
    CHECK (worst < 1.0f);
}

// ---- in the engine ---------------------------------------------------------------

#include "engine/Engine.h"

namespace
{
    using namespace psy;

    // Only the noise, through no filter, dry: what the sample plays.
    ParamState noiseOnly (int sample)
    {
        ParamState p;
        p.set (P::OscAOn, 0.f);
        p.set (P::OscBOn, 0.f);
        p.set (P::NoiseLevel, 1.f);
        p.set (P::NoiseType, (float) kNoiseSampleType);
        p.set (P::NoiseSample, (float) sample);
        p.set (P::NoiseRand, 0.f);
        p.set (P::F1Type, 0.f);
        p.set (P::AenvA, 0.f);
        p.set (P::AenvS, 1.f);
        p.set (P::AenvR, 5.f);
        p.set (P::MasterVol, 0.f);
        return p;
    }

    struct Held { std::vector<float> l, r; };

    // Keys held from 0 (each its note and start), released at `off` seconds.
    Held play (const ParamState& p, std::initializer_list<std::pair<int, double>> keys, double seconds, double off,
               const dsp::NoiseClip* file = nullptr)
    {
        Engine e;
        e.setParams (p);
        e.prepare (48000.0, 256);
        e.setNoiseFile (file);
        const int total = (int) (seconds * 48000.0);
        Held h { std::vector<float> ((size_t) total), std::vector<float> ((size_t) total) };
        std::vector<MidiEvent> all;
        for (const auto& [note, at] : keys)
        {
            all.push_back ({ (int) (at * 48000.0), MidiEvent::NoteOn, note, 0.8f });
            all.push_back ({ (int) (off * 48000.0), MidiEvent::NoteOff, note, 0.f });
        }
        std::sort (all.begin(), all.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.offset < b.offset; });
        size_t ei = 0;
        for (int pos = 0; pos < total; pos += 256)
        {
            const int n = std::min (256, total - pos);
            std::vector<MidiEvent> ev;
            while (ei < all.size() && all[ei].offset < pos + n)
            {
                auto e2 = all[ei++];
                e2.offset -= pos;
                ev.push_back (e2);
            }
            e.process (h.l.data() + pos, h.r.data() + pos, n, ev.data(), (int) ev.size(), 120.0);
        }
        return h;
    }

    double strongestHz (const std::vector<float>& x, size_t from, double maxHz)
    {
        const size_t n = 1 << 16;
        std::vector<float> seg (x.begin() + (long) from, x.begin() + (long) (from + n));
        const auto db = testutil::spectrumDb (seg, n);
        size_t best = 2;
        for (size_t k = 2; k < db.size() && k * 48000.0 / n < maxHz; ++k)
            if (db[k] > db[best]) best = k;
        return best * 48000.0 / n;
    }

    int bankIndex (std::string_view name)
    {
        for (int i = 0; i < dsp::NoiseBank::kCount; ++i)
            if (name == dsp::NoiseBank::name (i)) return i;
        return -1;
    }
} // namespace

// With Key, MIDI 72 plays a sample at its own speed (Serum 2's reference)
// and the note moves it: the hum's C2 fundamental on 72, C3 a key an octave
// up; Pitch +24 puts its own speed on MIDI 48. Without Key every note plays
// it at its own speed; Pitch moves it either way.
TEST_CASE ("A noise sample follows the key, or keeps its speed without Key", "[noise][engine]")
{
    auto p = noiseOnly (bankIndex ("Mains Hum"));
    p.set (P::NoiseKey, 1.f);
    const double c2 = dsp::NoiseBank::kRootHz * 0.5;
    const auto near = [] (double hz, double want) { return std::abs (1200.0 * std::log2 (hz / want)) < 15.0; };
    CHECK (near (strongestHz (play (p, { { 72, 0.0 } }, 2.0, 2.0).l, 24000, 100.0), c2));
    CHECK (near (strongestHz (play (p, { { 84, 0.0 } }, 2.0, 2.0).l, 24000, 200.0), 2.0 * c2));
    p.set (P::NoisePitch, 24.f);
    CHECK (near (strongestHz (play (p, { { 48, 0.0 } }, 2.0, 2.0).l, 24000, 100.0), c2));
    p.set (P::NoisePitch, 0.f);
    p.set (P::NoiseKey, 0.f);
    CHECK (near (strongestHz (play (p, { { 60, 0.0 } }, 2.0, 2.0).l, 24000, 100.0), c2));
    p.set (P::NoisePitch, -12.f);
    CHECK (near (strongestHz (play (p, { { 72, 0.0 } }, 2.0, 2.0).l, 24000, 60.0), 0.5 * c2));
}

// Random squared, as Serum 2 spreads the start: at 50 % the starts fall in
// the quarter of the sample after Start. A pan to one side is a balance:
// hard left keeps the left and drops the right, the centre stays 1 a side.
TEST_CASE ("Random spreads the start over its square, and the noise pans", "[noise][engine]")
{
    using Sampler = dsp::NoiseSampler;
    CHECK (Sampler::startFor (0.2f, 0.5f, 0.f) == 0.2f);
    CHECK (std::abs (Sampler::startFor (0.2f, 0.5f, 0.999f) - 0.44975) < 1e-4);
    CHECK (std::abs (Sampler::startFor (0.9f, 1.f, 0.5f) - 0.4) < 1e-6);   // wraps round
    CHECK (Sampler::startFor (0.3f, 0.f, 0.7f) == 0.3f);

    auto q = noiseOnly (bankIndex ("Dark Wind"));   // stereo
    q.set (P::NoiseRand, 0.f);
    const auto centre = play (q, { { 48, 0.0 } }, 1.0, 1.0);
    q.set (P::NoisePan, -1.f);
    const auto left = play (q, { { 48, 0.0 } }, 1.0, 1.0);
    double rC = 0.0, rL = 0.0, lL = 0.0, lC = 0.0;
    for (size_t i = 24000; i < centre.l.size(); ++i)
    {
        rC += centre.r[i] * centre.r[i];
        rL += left.r[i] * left.r[i];
        lC += centre.l[i] * centre.l[i];
        lL += left.l[i] * left.l[i];
    }
    CHECK (rL < 1e-12 * rC);
    CHECK (std::abs (10.0 * std::log10 (lL / lC) - 20.0 * std::log10 (1.656)) < 0.2);
}

// A stereo sample is stereo in the voice without any unison; a mono one is
// mono. The bank's samples sit near white noise's level at the same knob.
TEST_CASE ("A stereo noise sample comes out stereo, at white noise's level", "[noise][engine]")
{
    const auto sideToMid = [] (const Held& h) {
        double m = 0.0, s = 0.0;
        for (size_t i = 24000; i < h.l.size(); ++i)
        {
            m += std::pow (0.5 * (h.l[i] + h.r[i]), 2.0);
            s += std::pow (0.5 * (h.l[i] - h.r[i]), 2.0);
        }
        return 10.0 * std::log10 (s / std::max (m, 1e-30));
    };
    CHECK (sideToMid (play (noiseOnly (bankIndex ("Dark Wind")), { { 48, 0.0 } }, 2.0, 2.0)) > -6.0);
    CHECK (sideToMid (play (noiseOnly (bankIndex ("Analog Pink")), { { 48, 0.0 } }, 2.0, 2.0)) < -100.0);

    const auto level = [] (const Held& h) {
        double e = 0.0;
        for (size_t i = 24000; i < h.l.size(); ++i) e += h.l[i] * h.l[i] + h.r[i] * h.r[i];
        return 10.0 * std::log10 (e / (2.0 * (h.l.size() - 24000)));
    };
    auto white = noiseOnly (0);
    white.set (P::NoiseType, 0.f);
    const double w = level (play (white, { { 48, 0.0 } }, 2.0, 2.0));
    const double pink = level (play (noiseOnly (bankIndex ("Analog Pink")), { { 48, 0.0 } }, 2.0, 2.0));
    CHECK (std::abs (pink - w) < 2.0);
}

// One Shot: the sample plays once from Start and falls silent with the key
// still down. Random 0 starts every note at Start (the same sound), Random 1
// anywhere (two notes differ).
TEST_CASE ("A one-shot noise sample ends, and Random spreads the start", "[noise][engine]")
{
    auto p = noiseOnly (bankIndex ("Breath"));   // 2.73 s
    p.set (P::NoiseOneShot, 1.f);
    p.set (P::NoiseStart, 0.5f);
    const auto h = play (p, { { 48, 0.0 } }, 3.0, 3.0);
    double during = 0.0, after = 0.0;
    for (size_t i = 12000; i < 48000; ++i) during = std::max (during, (double) std::abs (h.l[i]));
    for (size_t i = (size_t) (1.45 * 48000); i < h.l.size(); ++i) after = std::max (after, (double) std::abs (h.l[i]));
    CHECK (during > 0.05);
    CHECK (after < 1e-4);

    auto q = noiseOnly (bankIndex ("Dark Wind"));
    q.set (P::NoiseRand, 0.f);
    const auto a = play (q, { { 48, 0.0 } }, 1.0, 1.0), b = play (q, { { 48, 0.0 } }, 1.0, 1.0);
    CHECK (a.l == b.l);
    q.set (P::NoiseRand, 1.f);
    const auto two = play (q, { { 48, 0.0 }, { 55, 0.0 } }, 1.0, 1.0);
    const auto one = play (q, { { 48, 0.0 } }, 1.0, 1.0);
    double diff = 0.0;
    for (size_t i = 0; i < one.l.size(); ++i) diff += std::abs (two.l[i] - one.l[i]);
    CHECK (diff > 1.0);
}

// The File entry plays the clip handed to the engine, and nothing while no
// file is loaded.
TEST_CASE ("The File noise sample plays the loaded clip", "[noise][engine]")
{
    std::vector<float> tone (1 << 14);
    const int c = (int) std::lround (440.0 * tone.size() / 44100.0);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = (float) (0.3 * std::sin (2.0 * dsp::kPiD * c * (double) i / tone.size()));
    const auto clip = dsp::NoiseClip::make (tone, {}, 44100.f, true);
    auto p = noiseOnly (kNoiseFile);
    const auto withFile = play (p, { { 48, 0.0 } }, 2.0, 2.0, &clip);
    CHECK (std::abs (strongestHz (withFile.l, 24000, 2000.0) - 440.0 * 44100.0 / clip.rate * c * clip.rate / (tone.size() * 440.0)) < 3.0);
    const auto without = play (p, { { 48, 0.0 } }, 1.0, 1.0);
    double peak = 0.0;
    for (float v : without.l) peak = std::max (peak, (double) std::abs (v));
    CHECK (peak == 0.0);
}

// A loop that starts inside the clip (a WAV's smpl loop) plays the head once,
// then [loopStart, end) round and round, as Serum 2 does.
TEST_CASE ("A clip with a loop start plays its head once, then loops from there", "[noise][bank]")
{
    const int n = 4096;
    std::vector<float> ramp ((size_t) n);
    for (int i = 0; i < n; ++i) ramp[(size_t) i] = (float) i / n;
    const auto clip = NoiseClip::make (ramp, {}, 48000.f, true, true, n / 2);
    REQUIRE (clip.loopStart == n / 2);
    NoiseSampler s;
    s.start (clip, 0.0);
    std::vector<float> out ((size_t) (2 * n));
    float r;
    for (auto& v : out) s.process (clip, 1.0, 0.79f, false, v, r);
    CHECK (std::abs (out[100] - 100.f / n) < 1e-4f);                  // the head
    CHECK (std::abs (out[(size_t) n + 100] - (n / 2 + 100.f) / n) < 1e-4f);      // then from the loop start
    CHECK (std::abs (out[(size_t) n + n / 2 + 100] - (n / 2 + 100.f) / n) < 1e-4f);   // and round again
}
