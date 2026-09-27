#include "TestUtil.h"
#include "dsp/Oscillators.h"
#include "dsp/Oversampling.h"

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

    // A bright table played high must not alias into the audible band.
    WavetableOsc osc;
    osc.setBank (&bank);
    const double fs = 48000.0, f0 = 3000.0;
    const auto out = renderOversampled ([&] { return osc.process (f0 / 96000.0, 5, 1.f, 0.62f); }, 2, fs, 1 << 15);
    const double alias = testutil::worstAliasDb (out, fs, f0, 19000.0);
    INFO ("wavetable alias " << alias << " dB");
    CHECK (alias < -80.0);
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
