#include "TestUtil.h"
#include "dsp/Oscillators.h"
#include "dsp/Oversampling.h"
#include "dsp/Phoscyon.h"

#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace psy::dsp;

namespace
{
    // Renders `n` base-rate samples of an oscillator running at factor x fs.
    template <typename Fn>
    std::vector<float> renderOversampled (Fn&& osc, int factor, double fs, size_t n)
    {
        Decimator dec;
        dec.prepare ((int) n);
        std::vector<float> os (n * (size_t) factor), out (n);
        for (auto& s : os) s = osc();
        dec.process (os.data(), out.data(), (int) n, factor);
        return out;
    }
} // namespace

TEST_CASE ("BLEP residuals are continuous and vanish outside the kernel", "[blep]")
{
    using namespace psy::dsp::blep;
    CHECK (step (-2.0) == 0.f);
    CHECK (step (2.0) == 0.f);
    CHECK_THAT (step (-1e-9) - step (1e-9), Catch::Matchers::WithinAbs (1.0, 1e-6));  // cancels the unit step
    CHECK_THAT (ramp (-1e-9), Catch::Matchers::WithinAbs (ramp (1e-9), 1e-6));
    CHECK_THAT (ramp (0.0), Catch::Matchers::WithinAbs (7.0 / 30.0, 1e-6));
    for (double t = -1.99; t < 2.0; t += 0.01)
        CHECK (std::abs (step (t)) <= 0.5f + 1e-6f);
}

TEST_CASE ("Analog saw at 2x oversampling keeps aliasing far below the signal", "[osc]")
{
    const double fs = 48000.0;
    for (double f0 : { 1000.0, 2500.0, 4000.0 })
    {
        AnalogOsc osc;
        const double inc = f0 / (fs * 2.0);
        const auto out = renderOversampled ([&] { return osc.process (inc, 2.f, 0.5f); }, 2, fs, 1 << 15);
        const double alias = testutil::worstAliasDb (out, fs, f0, 19000.0);
        INFO ("f0 = " << f0 << " Hz, worst alias = " << alias << " dB");
        CHECK (alias < -80.0);
    }
}

TEST_CASE ("Analog saw at 4x oversampling", "[osc]")
{
    const double fs = 48000.0, f0 = 4000.0;
    AnalogOsc osc;
    const auto out = renderOversampled ([&] { return osc.process (f0 / (fs * 4.0), 2.f, 0.5f); }, 4, fs, 1 << 15);
    const double alias = testutil::worstAliasDb (out, fs, f0, 19000.0);
    INFO ("worst alias " << alias << " dB");
    CHECK (alias < -90.0);  // at the decimator's stopband floor
}

TEST_CASE ("Band-limited saw beats a naive saw by a wide margin", "[osc]")
{
    const double fs = 48000.0, f0 = 3150.0;
    AnalogOsc osc;
    double ph = 0.0;
    const double inc = f0 / fs;
    std::vector<float> naive (1 << 15), bl (1 << 15);
    for (auto& s : naive) { ph += inc; ph -= std::floor (ph); s = (float) (2.0 * ph - 1.0); }
    for (auto& s : bl) s = osc.process (inc, 2.f, 0.5f);
    const double aNaive = testutil::worstAliasDb (naive, fs, f0, 20000.0);
    const double aBl = testutil::worstAliasDb (bl, fs, f0, 20000.0);
    INFO ("naive " << aNaive << " dB, 4-point BLEP " << aBl << " dB (1x)");
    CHECK (aBl < aNaive - 20.0);  // at 1x; the default 2x path is tested above
}

TEST_CASE ("Square, triangle and hard sync stay clean", "[osc]")
{
    const double fs = 48000.0, f0 = 2000.0;
    SECTION ("square")
    {
        AnalogOsc osc;
        const auto out = renderOversampled ([&] { return osc.process (f0 / 96000.0, 3.f, 0.5f); }, 2, fs, 1 << 15);
        CHECK (testutil::worstAliasDb (out, fs, f0, 19000.0) < -80.0);
    }
    SECTION ("triangle")
    {
        AnalogOsc osc;
        const auto out = renderOversampled ([&] { return osc.process (f0 / 96000.0, 1.f, 0.5f); }, 2, fs, 1 << 15);
        CHECK (testutil::worstAliasDb (out, fs, f0, 19000.0) < -90.0);
    }
    SECTION ("hard sync, slave at 2.37x master")
    {
        AnalogOsc slave;
        double master = 0.0;
        const double incM = 700.0 / 96000.0;
        const double incS = incM * 2.37;
        const auto out = renderOversampled ([&] {
            SyncInfo s;
            master += incM;
            if (master >= 1.0) { master -= 1.0; s.wrapped = true; s.fraction = master / incM; }
            return slave.process (incS, 2.f, 0.5f, &s);
        }, 2, fs, 1 << 15);
        const double alias = testutil::worstAliasDb (out, fs, 700.0, 19000.0);
        INFO ("sync alias " << alias << " dB");
        CHECK (alias < -70.0);
    }
}

TEST_CASE ("Supersaw is bounded and wide", "[osc]")
{
    Supersaw ss;
    Rng rng (3);
    ss.reset (rng);
    float peak = 0.f;
    for (int i = 0; i < 96000; ++i)
        peak = std::max (peak, std::abs (ss.process (220.0 / 96000.0, 0.5f)));
    CHECK (peak < 2.f);
    CHECK (peak > 0.2f);
}

TEST_CASE ("Every wavetable frame is finite, normalised, and band-limited when played", "[wavetable]")
{
    const auto& bank = WavetableBank::shared();
    for (int t = 0; t < WavetableBank::kTables; ++t)
        for (int f = 0; f < WavetableBank::kFrames; ++f)
        {
            const float* d = bank.frame (t, f);
            float pk = 0.f;
            for (int i = 0; i < WavetableBank::kSize; ++i)
            {
                REQUIRE (std::isfinite (d[i]));
                pk = std::max (pk, std::abs (d[i]));
            }
            CHECK (pk <= 0.91f);
        }

    // A bright table played high must not alias into the audible band. 2903 Hz
    // is no divisor of the table length, so the mip crossfade is mid-way.
    WavetableOsc osc;
    osc.setBank (&bank);
    const double fs = 48000.0, f0 = 2903.0;
    const auto out = renderOversampled ([&] { return osc.process (f0 / 96000.0, 5, 1.f, 0.62f); }, 2, fs, 1 << 15);
    const double alias = testutil::worstAliasDb (out, fs, f0, 19000.0);
    INFO ("wavetable alias " << alias << " dB");
    CHECK (alias < -80.0);
}

namespace
{
    // Worst in-band alias of a wavetable note at `factor` x 48 kHz; NaN when
    // the note is (nearly) silent: every partial of that table is above the limit.
    double wavetableAliasDb (int table, float position, double f0, int factor, float limit)
    {
        const double fs = 48000.0;
        WavetableOsc osc;
        osc.setBank (&WavetableBank::shared());
        const auto out = renderOversampled ([&] { return osc.process (f0 / (fs * factor), table, position, limit); }, factor, fs, 1 << 15);
        const auto db = testutil::spectrumDb (out, 1 << 15);
        if (*std::max_element (db.begin(), db.end()) < -40.0)
            return std::nan ("");
        return testutil::worstAliasDb (out, fs, f0, 20000.0);
    }
} // namespace

TEST_CASE ("Wavetable notes from 200 Hz to 4.1 kHz stay clear of aliasing at 2x and 4x", "[wavetable]")
{
    // The mip crossfade used to keep a brighter level than the limit allows
    // (up to twice the harmonics): -35 dB aliases at 2x and -38 dB at 4x.
    // What is left at the lowest notes is the table read's own imaging.
    struct Case { int factor; float limit; double bound; };
    for (const auto& c : { Case { 2, 0.62f, -80.0 }, Case { 4, 0.8f, -65.0 } })
    {
        double worst = -999.0;
        int measured = 0;
        for (int table : { 0, 1, 2, 3, 4, 5, 7 })
            for (float position : { 0.4f, 1.f })
                for (double f0 = 200.0; f0 < 4100.0; f0 *= 1.17)
                {
                    const double a = wavetableAliasDb (table, position, f0, c.factor, c.limit);
                    if (std::isnan (a)) continue;
                    ++measured;
                    if (a > worst)
                    {
                        worst = a;
                        INFO ("table " << table << " position " << position << " at " << f0 << " Hz");
                    }
                }
        INFO (c.factor << "x: worst alias " << worst << " dB over " << measured << " notes");
        CHECK (measured > 60);
        CHECK (worst < c.bound);
    }

    // a note between two mip levels, at both rates
    CHECK (wavetableAliasDb (5, 1.f, 2903.0, 2, 0.62f) < -80.0);
    CHECK (wavetableAliasDb (5, 1.f, 2903.0, 4, 0.8f) < -80.0);
}

TEST_CASE ("Wavetable read is continuous in pitch where the mip level changes", "[wavetable]")
{
    const auto& bank = WavetableBank::shared();
    float prev = bank.read (1, 0.5f, 0.3f, 0.002f, 0.62f);
    for (float inc = 0.002f; inc < 0.2f; inc *= 1.001f)
    {
        const float y = bank.read (1, 0.5f, 0.3f, inc, 0.62f);
        CHECK (std::abs (y - prev) < 0.35f);
        prev = y;
    }
    // an explicit fold limit is accepted
    CHECK (std::isfinite (bank.read (1, 0.5f, 0.3f, 0.01f, 0.62f, 0.7f)));
}

TEST_CASE ("Analog pulse width moves past the phase without an uncorrected step", "[osc]")
{
    // A width that crosses the phase flips the naive square; with no BLEP at
    // that flip the output jumps by the full 2 (100 of them in 10 s at 5 Hz,
    // full depth). Every edge now rounds off over a few samples: the biggest
    // step is 1.2 at the worst, 0.92 for a fixed width.
    const double fs = 96000.0;
    for (double f0 : { 300.0, 1000.0, 4000.0 })
        for (double lfoHz : { 5.0, 50.0 })
        {
            AnalogOsc osc;
            float prev = 0.f, biggest = 0.f;
            for (size_t i = 0; i < (size_t) (fs * 2.0); ++i)
            {
                const float pw = (float) (0.5 + 0.48 * std::sin (2.0 * std::numbers::pi * lfoHz * (double) i / fs));
                const float y = osc.process (f0 / fs, 3.f, pw);
                if (i > 10) biggest = std::max (biggest, std::abs (y - prev));
                prev = y;
            }
            INFO ("f0 " << f0 << " Hz, PWM " << lfoHz << " Hz: biggest step " << biggest);
            CHECK (biggest < 1.4f);
        }
}

TEST_CASE ("Analog pulse after a sync reset keeps the edge inside the restart", "[osc]")
{
    // A narrow pulse (2 %) on a slave that is reset right before: the edge
    // falls within the first sample after the reset.
    AnalogOsc slave;
    float prev = 0.f, biggest = 0.f;
    double master = 0.0;
    const double incM = 700.0 / 96000.0, incS = 9000.0 / 96000.0;
    for (int i = 0; i < 96000; ++i)
    {
        SyncInfo s;
        master += incM;
        if (master >= 1.0) { master -= 1.0; s.wrapped = true; s.fraction = master / incM; }
        const float y = slave.process (incS, 3.f, 0.02f, &s);
        if (i > 10) biggest = std::max (biggest, std::abs (y - prev));
        prev = y;
    }
    INFO ("biggest step " << biggest);
    CHECK (biggest < 1.6f);
}

TEST_CASE ("The Phoscyon square keeps its top when the duty falls from sample to sample", "[osc][phoscyon]")
{
    // A pitch wobble moves the duty (it follows the frequency) while the
    // phase is inside the top: the fall used to be missed, the cycle stayed
    // high up to the wrap and the next rise stepped past the top.
    const double rate = 96000.0, sag = std::exp (-1.0 / (8.0 * 0.001 * rate));
    const float top = 0.878f, low = 1.039f;
    PhosOsc osc;
    osc.reset();
    float peak = 0.f;
    int stuck = 0, run = 0;
    for (int i = 0; i < (int) (rate * 4); ++i)
    {
        const double semis = 12.0 * std::sin (2.0 * std::numbers::pi * 5.0 * i / rate);
        const double hz = 110.0 * std::exp2 (semis / 12.0);
        const double duty = 0.474 + 0.33 * 0.001 * hz;
        const float y = osc.process (hz / rate, true, duty, sag, top, low);
        peak = std::max (peak, y);
        if (y > 0.5f * top)
        {
            if (++run == (int) (1.05 * rate / hz)) ++stuck;
        }
        else
            run = 0;
    }
    INFO ("peak " << peak << " (top " << top << "), cycles whose top outlasted a period: " << stuck);
    CHECK (stuck == 0);
    CHECK (peak < top * 1.08f);   // the BLEP's overshoot, no more
}

TEST_CASE ("Noise keeps its base-rate level at 1x, 2x and 4x", "[osc][noise]")
{
    // White, pink and hiss as they leave the decimator, per oversampling
    // factor: the level below 19 kHz (the band the decimator passes at 2x
    // and 4x). White and hiss used to fall 3 dB per doubling of the rate,
    // and the hiss rose by 7 dB between 1x and 4x.
    for (int type : { 0, 1, 3, 4 })
    {
        double levelDb[3] {};
        int k = 0;
        for (int factor : { 1, 2, 4 })
        {
            Noise noise;
            noise.prepare (48000.f * 4.f);
            noise.setRate (48000.f * (float) factor);
            noise.reset (7);
            const size_t n = 1 << 16;
            std::vector<float> os (n * (size_t) factor), out (n);
            for (auto& v : os) v = noise.process (type, 0.01, 51.913 / (48000.0 * factor));
            Decimator dec;
            dec.prepare ((int) n);
            dec.process (os.data(), out.data(), (int) n, factor);
            const auto db = testutil::spectrumDb (out, n);
            double power = 0.0;
            for (size_t i = 4; i < (size_t) (19000.0 / 48000.0 * (double) n); ++i)
                power += std::pow (10.0, db[i] / 10.0);
            levelDb[k++] = 10.0 * std::log10 (power);
        }
        INFO ("type " << type << ": 1x " << levelDb[0] << " dB, 2x " << levelDb[1] << " dB, 4x " << levelDb[2] << " dB");
        CHECK (std::abs (levelDb[0] - levelDb[1]) < 0.5);
        CHECK (std::abs (levelDb[2] - levelDb[1]) < 0.5);
    }
}

TEST_CASE ("Tuned noise sits on the note's harmonics, follows the pitch and keeps white's level", "[osc][noise]")
{
    // A comb on the note's period: the power between the harmonics is a
    // small share (white noise has nearly all of it there), at the level of
    // white noise, and tonal up top (spectral flatness 6-16 kHz).
    const double fs = 96000.0, f0 = 51.913;
    const size_t n = 1 << 16;
    const auto run = [&] (int type, double hz, size_t len, uint64_t seed) {
        Noise noise;
        noise.prepare ((float) fs);
        noise.setRate ((float) fs);
        noise.reset (seed);
        std::vector<float> x (len);
        for (auto& v : x) v = noise.process (type, 0.01, hz / fs);
        return x;
    };
    const auto flatness = [&] (const std::vector<float>& x) {
        const auto db = testutil::spectrumDb (std::vector<float> (x.end() - (long) n, x.end()), n);
        double logSum = 0.0, sum = 0.0;
        int c = 0;
        for (size_t i = (size_t) (6000.0 / fs * n); i < (size_t) (16000.0 / fs * n); ++i, ++c)
        {
            const double p = std::pow (10.0, db[i] / 10.0);
            logSum += std::log (p);
            sum += p;
        }
        return std::exp (logSum / c) / (sum / c);
    };
    const auto tuned = run (4, f0, 2 * n, 3), white = run (0, f0, 2 * n, 3);
    const double offTuned = testutil::inharmonicDb (tuned, fs, f0, 200.0, 20000.0);
    const double offWhite = testutil::inharmonicDb (white, fs, f0, 200.0, 20000.0);
    const double level = testutil::rms (tuned, n) / testutil::rms (white, n);
    INFO ("off the harmonics: tuned " << offTuned << " dB, white " << offWhite << " dB; level " << 20.0 * std::log10 (level)
          << " dB; flatness 6-16 kHz tuned " << flatness (tuned) << ", white " << flatness (white));
    CHECK (offTuned < -9.0);
    CHECK (offWhite > -2.0);
    CHECK (std::abs (20.0 * std::log10 (level)) < 1.0);
    CHECK (flatness (tuned) < 0.2);
    CHECK (flatness (white) > 0.45);

    // an octave up halfway: the odd harmonics of the old note go (the lines
    // are twice as wide in Hz an octave up, the same feedback over half the
    // period, so a little more spills past the 4 bins either side)
    Noise noise;
    noise.prepare ((float) fs);
    noise.setRate ((float) fs);
    noise.reset (5);
    std::vector<float> x (3 * n);
    for (size_t i = 0; i < x.size(); ++i) x[i] = noise.process (4, 0.01, (i < n ? f0 : 2.0 * f0) / fs);
    const double octaveUp = testutil::inharmonicDb (x, fs, 2.0 * f0, 200.0, 20000.0);
    INFO ("an octave up: " << octaveUp << " dB off its harmonics");
    CHECK (octaveUp < -7.0);

    // a reset leaves nothing ringing from the last note
    for (int i = 0; i < 4096; ++i) noise.process (4, 0.01, f0 / fs);
    noise.reset (5);
    const auto fresh = run (4, f0, 64, 5);
    bool same = true;
    for (size_t i = 0; i < fresh.size(); ++i) same = same && fresh[i] == noise.process (4, 0.01, f0 / fs);
    CHECK (same);
}

TEST_CASE ("Hiss is white above ~10 kHz and falls steeply under it", "[osc][noise]")
{
    Noise noise;
    noise.setRate (96000.f);
    noise.reset (11);
    std::vector<float> x (1 << 16);
    for (auto& v : x) v = noise.process (3, 0.01);
    const auto db = testutil::spectrumDb (x, 1 << 16);
    const auto band = [&] (double lo, double hi) {
        double s = 0.0;
        int c = 0;
        for (size_t i = (size_t) (lo / 96000.0 * (1 << 16)); i < (size_t) (hi / 96000.0 * (1 << 16)); ++i, ++c)
            s += std::pow (10.0, db[i] / 10.0);
        return 10.0 * std::log10 (s / c);
    };
    const double low = band (2500.0, 3500.0), mid = band (9000.0, 11000.0), high = band (14000.0, 16000.0);
    INFO ("2.5-3.5 kHz " << low << " dB, 9-11 kHz " << mid << " dB, 14-16 kHz " << high << " dB");
    CHECK (high - low > 25.0);
    CHECK (high - mid > 3.0);
    CHECK (high - mid < 12.0);
}

TEST_CASE ("FM oscillator stays bounded at extreme index and feedback", "[osc]")
{
    FmOsc fm;
    for (int i = 0; i < 48000; ++i)
    {
        const float y = fm.process (0.01, 3.5f, 12.f, 1.f);
        REQUIRE (std::isfinite (y));
        REQUIRE (std::abs (y) <= 1.0001f);
    }
}
