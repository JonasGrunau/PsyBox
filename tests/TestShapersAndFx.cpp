#include "TestUtil.h"
#include "dsp/Effects.h"
#include "dsp/Modulators.h"
#include "dsp/Oversampling.h"
#include "dsp/Shapers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace psy::dsp;

TEST_CASE ("ADAA tanh reduces aliasing compared to naive tanh", "[shaper]")
{
    const double fs = 48000.0, f0 = 2873.0;
    const size_t n = 1 << 15;
    std::vector<float> naive (n), adaa (n);
    AdaaShaper sh;
    for (size_t i = 0; i < n; ++i)
    {
        const float x = 8.f * (float) std::sin (2.0 * M_PI * f0 * (double) i / fs);
        naive[i] = std::tanh (x);
        adaa[i] = sh.process (x, AdaaShaper::Soft);
    }
    const double a = testutil::worstAliasDb (naive, fs, f0, 20000.0);
    const double b = testutil::worstAliasDb (adaa, fs, f0, 20000.0);
    INFO ("naive " << a << " dB, ADAA " << b << " dB");
    CHECK (b < a - 6.0);
}

TEST_CASE ("ADAA tanh at 2x (as the engine runs it) is clean", "[shaper]")
{
    const double fs = 48000.0, f0 = 2873.0;
    const size_t n = 1 << 15;
    std::vector<float> os (n * 2), out (n);
    AdaaShaper sh;
    for (size_t i = 0; i < os.size(); ++i)
        os[i] = sh.process (8.f * (float) std::sin (2.0 * M_PI * f0 * (double) i / (2.0 * fs)), AdaaShaper::Soft);
    Decimator d;
    d.prepare ((int) n);
    d.process (os.data(), out.data(), (int) n, 2);
    const double alias = testutil::worstAliasDb (out, fs, f0, 19000.0);
    INFO ("2x ADAA alias " << alias << " dB");
    CHECK (alias < -50.0);
}

TEST_CASE ("ADAA antiderivatives match their functions", "[shaper]")
{
    for (int type = 0; type < 5; ++type)
        for (double x = -3.0; x <= 3.0; x += 0.37)
        {
            const double h = 1e-5;
            const double num = (AdaaShaper::F (x + h, type) - AdaaShaper::F (x - h, type)) / (2.0 * h);
            INFO ("type " << type << " x " << x);
            CHECK_THAT (num, Catch::Matchers::WithinAbs (AdaaShaper::f (x, type), 1e-4));
        }
}

TEST_CASE ("Half-band decimator: flat passband, deep stopband", "[oversampling]")
{
    const double fsOs = 96000.0;
    const auto gain = [&] (double hz) {
        Decimator d;
        d.prepare (1 << 14);
        std::vector<float> in (1 << 15), out (1 << 14);
        for (size_t i = 0; i < in.size(); ++i) in[i] = (float) std::sin (2.0 * M_PI * hz * (double) i / fsOs);
        d.process (in.data(), out.data(), (int) out.size(), 2);
        return 20.0 * std::log10 (testutil::rms (out, 2000) / std::sqrt (0.5));
    };
    CHECK_THAT (gain (1000.0), Catch::Matchers::WithinAbs (0.0, 0.01));
    CHECK_THAT (gain (19000.0), Catch::Matchers::WithinAbs (0.0, 0.05));
    CHECK (gain (29000.0) < -95.0);  // would fold to 19 kHz
    CHECK (gain (40000.0) < -95.0);
}

TEST_CASE ("Frequency shifter moves a sine by the set amount in the right direction", "[fx]")
{
    const float fs = 48000.f;
    FrequencyShifter fsh;
    fsh.prepare (fs);
    std::vector<float> l (1 << 15), r (1 << 15);
    for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.5f * (float) std::sin (2.0 * M_PI * 1000.0 * (double) i / fs);
    fsh.process (l.data(), r.data(), (int) l.size(), 150.f, 0.f, 0.f, 1.f);
    const double f = testutil::peakFrequency (l, fs);
    INFO ("shifted to " << f << " Hz");
    CHECK_THAT (f, Catch::Matchers::WithinAbs (1150.0, 3.0));
    // the unwanted sideband is suppressed
    const auto db = testutil::spectrumDb (l, 1 << 15);
    const size_t bin850 = (size_t) std::lround (850.0 / fs * (1 << 15));
    const size_t bin1150 = (size_t) std::lround (1150.0 / fs * (1 << 15));
    CHECK (db[bin850] < db[bin1150] - 35.0);
}

TEST_CASE ("Every effect stays finite and bounded on loud noise with max feedback", "[fx][stress]")
{
    const float fs = 48000.f;
    Rng rng (7);
    const int n = 256;
    std::vector<float> l (n), r (n);

    Distortion dist; dist.prepare (fs);
    Bitcrusher crush;
    FrequencyShifter shift; shift.prepare (fs);
    Phaser ph; ph.prepare (fs);
    ChorusFlanger ch; ch.prepare (fs);
    StereoDelay dl; dl.prepare (fs);
    Reverb rv; rv.prepare (fs);

    float peak = 0.f;
    for (int block = 0; block < 1500; ++block)
    {
        for (int i = 0; i < n; ++i) { l[(size_t) i] = rng.bipolar(); r[(size_t) i] = rng.bipolar(); }
        dist.process (l.data(), r.data(), n, block % 5, 36.f, 12000.f, 1.f);
        crush.process (l.data(), r.data(), n, 4.f, 8.f, 1.f);
        shift.process (l.data(), r.data(), n, 300.f, 0.95f, 1.f, 1.f);
        ph.process (l.data(), r.data(), n, 3.f, 1.f, 0.95f, 1000.f, 1.f);
        ch.process (l.data(), r.data(), n, block % 2 == 0, 2.f, 1.f, 0.95f, 1.f);
        dl.process (l.data(), r.data(), n, 50.f + (float) (block % 7) * 30.f, 0.98f, true, 20.f, 20000.f, 1.f);
        rv.process (l.data(), r.data(), n, 1.f, 30.f, 0.f, 0.f, 1.f);
        for (int i = 0; i < n; ++i)
        {
            REQUIRE (std::isfinite (l[(size_t) i]));
            REQUIRE (std::isfinite (r[(size_t) i]));
            peak = std::max ({ peak, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]) });
        }
    }
    INFO ("peak " << peak);
    CHECK (peak < 100.f);
}

TEST_CASE ("ADSR segment times are exact", "[env]")
{
    const float fs = 48000.f;
    Adsr env;
    env.setSampleRate (fs);
    env.set (10.f, 100.f, 0.5f, 200.f);
    env.noteOn();
    int attackSamples = 0;
    while (env.getStage() == Adsr::Stage::Attack) { env.process(); ++attackSamples; }
    CHECK_THAT ((double) attackSamples, Catch::Matchers::WithinRel (480.0, 0.02));
    for (int i = 0; i < 48000; ++i) env.process();
    CHECK_THAT (env.value(), Catch::Matchers::WithinAbs (0.5, 1e-3));
    env.noteOff();
    int rel = 0;
    while (env.active()) { env.process(); ++rel; }
    CHECK_THAT ((double) rel, Catch::Matchers::WithinRel (200.0 * 48.0, 0.1));
}
