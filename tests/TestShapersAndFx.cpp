#include "TestUtil.h"
#include "dsp/Effects.h"
#include "dsp/Modulators.h"
#include "dsp/Oversampling.h"
#include "dsp/PhosEffects.h"
#include "dsp/Shapers.h"

#include <numbers>

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
        const float x = 8.f * (float) std::sin (2.0 * std::numbers::pi * f0 * (double) i / fs);
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
        os[i] = sh.process (8.f * (float) std::sin (2.0 * std::numbers::pi * f0 * (double) i / (2.0 * fs)), AdaaShaper::Soft);
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
        for (size_t i = 0; i < in.size(); ++i) in[i] = (float) std::sin (2.0 * std::numbers::pi * hz * (double) i / fsOs);
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
    for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.5f * (float) std::sin (2.0 * std::numbers::pi * 1000.0 * (double) i / fs);
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
    StereoWidth wd; wd.prepare (fs);

    float peak = 0.f;
    for (int block = 0; block < 1500; ++block)
    {
        for (int i = 0; i < n; ++i) { l[(size_t) i] = rng.bipolar(); r[(size_t) i] = rng.bipolar(); }
        dist.process (l.data(), r.data(), n, block % 5, 36.f, 12000.f, 1.f);
        wd.process (l.data(), r.data(), n, block % 50 != 0, 2.f, 1.f, 1.f + (float) (block % 30), 20.f + (float) (block % 13) * 37.f);
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

TEST_CASE ("The amp attack curve bends the rise into a swell and keeps its time", "[env]")
{
    const float fs = 48000.f;
    const auto levelAt = [fs] (float curve, float fraction, int* attackSamples = nullptr) {
        Adsr env;
        env.setSampleRate (fs);
        env.setAttackCurve (curve);
        env.set (200.f, 100.f, 1.f, 50.f);
        env.noteOn();
        const int target = (int) (fraction * 0.2f * fs);
        float at = 0.f;
        int n = 0;
        while (env.getStage() == Adsr::Stage::Attack)
        {
            const float v = env.process();
            if (++n == target) at = v;
        }
        if (attackSamples != nullptr) *attackSamples = n;
        return at;
    };
    int straight = 0, swell = 0;
    const float rcHalf = levelAt (0.f, 0.5f, &straight);
    const float swellHalf = levelAt (0.5f, 0.5f, &swell);
    CHECK_THAT ((double) straight, Catch::Matchers::WithinRel (9600.0, 0.02));
    CHECK_THAT ((double) swell, Catch::Matchers::WithinRel (9600.0, 0.02));
    CHECK (rcHalf > 0.6f);                             // the RC charge is past half way at half time
    CHECK (swellHalf < 0.2f);                          // a swell is still quiet
    CHECK (levelAt (1.f, 0.5f) < swellHalf);           // and quieter the further the curve goes
    CHECK (levelAt (0.5f, 0.9f) > 0.5f);               // most of the rise comes late

    // a retrigger mid-swell carries on from the level it is at
    Adsr env;
    env.setSampleRate (fs);
    env.setAttackCurve (0.6f);
    env.set (100.f, 100.f, 1.f, 30.f);
    env.noteOn();
    for (int i = 0; i < 4000; ++i) env.process();
    const float before = env.value();
    env.noteOn();
    CHECK_THAT (env.process(), Catch::Matchers::WithinAbs (before, 0.01));
}

TEST_CASE ("Envelope progress follows each stage's time", "[env]")
{
    // The editor's moving dots read this: half the stage's time is half way.
    const float fs = 48000.f;
    for (float curve : { 0.f, 0.6f })
    {
        INFO ("attack curve " << curve);
        Adsr env;
        env.setSampleRate (fs);
        env.setAttackCurve (curve);
        env.set (100.f, 200.f, 0.4f, 400.f);
        env.noteOn();
        for (int i = 0; i < 2400; ++i) env.process();   // half the attack
        REQUIRE (env.getStage() == Adsr::Stage::Attack);
        CHECK_THAT (env.stageProgress(), Catch::Matchers::WithinAbs (0.5, 0.03));
        while (env.getStage() == Adsr::Stage::Attack) env.process();
        for (int i = 0; i < 4800; ++i) env.process();   // half the decay
        REQUIRE (env.getStage() == Adsr::Stage::Decay);
        CHECK_THAT (env.stageProgress(), Catch::Matchers::WithinAbs (0.5, 0.03));
        for (int i = 0; i < 48000; ++i) env.process();
        CHECK (env.getStage() == Adsr::Stage::Sustain);
        CHECK (env.stageProgress() == 0.f);
        env.noteOff();
        for (int i = 0; i < 9600; ++i) env.process();   // half the release
        CHECK_THAT (env.stageProgress(), Catch::Matchers::WithinAbs (0.5, 0.03));
    }

    PitchEnv pe;
    pe.setSampleRate (fs);
    pe.trigger();
    for (int i = 0; i < 480; ++i) pe.process (20.f, 100.f, 0.5f);   // half the attack
    CHECK (pe.getStage() == 1);
    CHECK_THAT (pe.stageProgress(), Catch::Matchers::WithinAbs (0.5, 0.01));
    for (int i = 0; i < 480 + 2400; ++i) pe.process (20.f, 100.f, 0.5f);   // half the decay
    CHECK (pe.getStage() == 2);
    CHECK_THAT (pe.stageProgress(), Catch::Matchers::WithinAbs (0.5, 0.01));

    // an LFO's position over two cycles
    Lfo lfo;
    lfo.setSampleRate (fs);
    lfo.reset (0.25f);
    for (int i = 0; i < 48000; ++i) lfo.process (0, 1.5f);   // 1.5 cycles in
    CHECK_THAT (lfo.phase2(), Catch::Matchers::WithinAbs (1.75, 0.01));
}

namespace
{
    // Runs the width stage over stereo buffers in blocks, after a silent
    // lead-in so the 10 ms fade-in is done.
    void runWidth (StereoWidth& w, std::vector<float>& l, std::vector<float>& r, float width, float stereo, float timeMs, float monoHz)
    {
        std::vector<float> zl (1024, 0.f), zr (1024, 0.f);
        w.process (zl.data(), zr.data(), 1024, true, width, stereo, timeMs, monoHz);
        for (size_t i = 0; i < l.size(); i += 256)
        {
            const int n = (int) std::min<size_t> (256, l.size() - i);
            w.process (l.data() + i, r.data() + i, n, true, width, stereo, timeMs, monoHz);
        }
    }

    double rmsOf (const std::vector<float>& a, const std::vector<float>& b, float sign, size_t from)
    {
        double sum = 0.0;
        for (size_t i = from; i < a.size(); ++i) { const double v = 0.5 * ((double) a[i] + sign * (double) b[i]); sum += v * v; }
        return std::sqrt (sum / (double) (a.size() - from));
    }
} // namespace

TEST_CASE ("Width never changes the mono sum's magnitude", "[fx][width]")
{
    // L + R = 2 * allpass(mid) for any width and stereoize: an impulse on one
    // side must come out of the sum with a flat magnitude.
    StereoWidth w;
    w.prepare (48000.f);
    std::vector<float> l (1 << 15, 0.f), r (1 << 15, 0.f);
    l[0] = 1.f;
    runWidth (w, l, r, 1.7f, 0.8f, 9.f, 150.f);
    std::vector<std::complex<double>> a (l.size());
    for (size_t i = 0; i < l.size(); ++i) a[i] = (double) l[i] + (double) r[i];
    testutil::fft (a);
    double worst = 0.0;
    for (size_t i = 1; i < a.size() / 2; ++i)
    {
        const double hz = (double) i * 48000.0 / (double) a.size();
        if (hz < 20.0 || hz > 20000.0) continue;
        worst = std::max (worst, std::abs (20.0 * std::log10 (std::abs (a[i]))));
    }
    CHECK (worst < 0.02);
}

TEST_CASE ("Low Mono removes the side below the split and keeps it above", "[fx][width]")
{
    const float fs = 48000.f;
    for (double hz : { 60.0, 1000.0 })
    {
        StereoWidth w;
        w.prepare (fs);
        std::vector<float> l ((size_t) fs), r ((size_t) fs);
        for (size_t i = 0; i < l.size(); ++i) { l[i] = 0.5f * (float) std::sin (2.0 * std::numbers::pi * hz * (double) i / fs); r[i] = -l[i]; }
        runWidth (w, l, r, 1.f, 0.f, 10.f, 150.f);
        const double side = 20.0 * std::log10 (rmsOf (l, r, -1.f, l.size() / 2) / (0.5 / std::sqrt (2.0)));
        INFO (hz << " Hz: side " << side << " dB");
        if (hz < 150.0) CHECK (side < -30.0);   // LR4: 60 Hz is 1.3 octaves under a 150 Hz split
        else CHECK (std::abs (side) < 0.1);
    }
}

TEST_CASE ("Stereoize widens a mono source above the split only", "[fx][width]")
{
    const float fs = 48000.f;
    for (double hz : { 60.0, 2000.0 })
    {
        StereoWidth w;
        w.prepare (fs);
        std::vector<float> l ((size_t) fs), r ((size_t) fs);
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.5f * (float) std::sin (2.0 * std::numbers::pi * hz * (double) i / fs);
        runWidth (w, l, r, 1.f, 0.5f, 12.f, 200.f);
        const double ratio = 20.0 * std::log10 (rmsOf (l, r, -1.f, l.size() / 2) / rmsOf (l, r, 1.f, l.size() / 2));
        INFO (hz << " Hz: side/mid " << ratio << " dB");
        if (hz < 200.0) CHECK (ratio < -40.0);
        else CHECK (std::abs (ratio - 20.0 * std::log10 (0.5)) < 0.5);   // side = 0.5 x the delayed mid
    }
}

TEST_CASE ("Width at 0 % folds everything to mono", "[fx][width]")
{
    StereoWidth w;
    w.prepare (48000.f);
    std::vector<float> l (24000), r (24000, 0.f);
    for (size_t i = 0; i < l.size(); ++i) l[i] = (float) std::sin (2.0 * std::numbers::pi * 1000.0 * (double) i / 48000.0);
    runWidth (w, l, r, 0.f, 0.f, 10.f, 120.f);
    for (size_t i = 4800; i < l.size(); ++i)
        REQUIRE (std::abs (l[i] - r[i]) < 1e-5f);
}

namespace
{
    // Level of one frequency in x[from, from + n), in dB (Goertzel, Hann).
    double toneDb (const std::vector<float>& x, size_t from, size_t n, double hz, double fs)
    {
        const double w = 2.0 * std::numbers::pi * hz / fs, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) n);
            const double s = (double) x[from + i] * win + c * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        return 10.0 * std::log10 (s1 * s1 + s2 * s2 - c * s1 * s2 + 1e-30);
    }

    double rmsDb (const std::vector<float>& x, size_t from, size_t to)
    {
        double s = 0.0;
        for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
        return 10.0 * std::log10 (s / (double) (to - from) + 1e-30);
    }

    double crestDb (const std::vector<float>& x, size_t from, size_t to)
    {
        float pk = 0.f;
        for (size_t i = from; i < to; ++i) pk = std::max (pk, std::abs (x[i]));
        return 20.0 * std::log10 ((double) pk + 1e-30) - rmsDb (x, from, to);
    }

    // A high-passed saw on 55 Hz (harmonics 4 to 60), as spiky as a Jilax
    // shot before its clipper.
    std::vector<float> sawTone (int n, float fs, float gain, float decayDbPerSec = 0.f)
    {
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            double v = 0.0;
            for (int k = 4; k <= 60; ++k) v += std::sin (2.0 * std::numbers::pi * 55.0 * k * i / fs) / k;
            x[(size_t) i] = (float) (v * 0.5) * gain * std::pow (10.f, -decayDbPerSec * (float) i / fs / 20.f);
        }
        return x;
    }
} // namespace

TEST_CASE ("The distortion's low cut is off at 20 Hz and takes out the lows above it", "[fx]")
{
    const float fs = 48000.f;
    const int n = 1 << 15;
    std::vector<float> in ((size_t) n);
    for (int i = 0; i < n; ++i)
        in[(size_t) i] = 0.2f * (float) (std::sin (2.0 * std::numbers::pi * 40.0 * i / fs) + std::sin (2.0 * std::numbers::pi * 1000.0 * i / fs));

    // The cut is off up to 20.5 Hz: 20 Hz (the default), 20.4 Hz and the
    // default argument all give the same samples, 21 Hz does not.
    Distortion a, b, edge, bare;
    a.prepare (fs);
    b.prepare (fs);
    edge.prepare (fs);
    bare.prepare (fs);
    auto la = in, ra = in, lb = in, rb = in, l21 = in, r21 = in, ld = in, rd = in;
    a.process (la.data(), ra.data(), n, 0, 12.f, 12000.f, 1.f);
    b.process (lb.data(), rb.data(), n, 0, 12.f, 12000.f, 1.f, 20.4f);
    edge.process (l21.data(), r21.data(), n, 0, 12.f, 12000.f, 1.f, 21.f);
    CHECK (la == lb);
    CHECK (la != l21);
    // and 20 Hz is what the shaper alone gives (no low cut anywhere): the
    // tone at 40 Hz comes through at full level, as in the input
    bare.process (ld.data(), rd.data(), n, 0, 0.f, 20000.f, 1.f, 20.f);
    CHECK (std::abs (toneDb (ld, (size_t) n / 2, (size_t) n / 2, 40.0, fs) - toneDb (in, (size_t) n / 2, (size_t) n / 2, 40.0, fs)) < 3.0);

    // at 200 Hz the 40 Hz tone goes, the 1 kHz tone stays
    Distortion c;
    c.prepare (fs);
    auto lc = in, rc = in;
    c.process (lc.data(), rc.data(), n, 0, 0.f, 20000.f, 1.f, 200.f);
    const size_t from = (size_t) (n / 2), len = (size_t) (n / 2);
    CHECK (toneDb (lc, from, len, 40.0, fs) - toneDb (in, from, len, 40.0, fs) < -45.0);
    CHECK (std::abs (toneDb (lc, from, len, 1000.0, fs) - toneDb (in, from, len, 1000.0, fs)) < 3.0);
}

TEST_CASE ("The distortion's low cut trims the peaks it raises without bringing the lows back", "[fx]")
{
    const float fs = 48000.f;
    const int n = (int) fs;
    const size_t from = (size_t) (0.3f * fs), to = (size_t) n;
    const auto run = [&] (int type, float lowCutHz) {
        auto l = sawTone (n, fs, 1.f), r = l;
        Distortion d;
        d.prepare (fs);
        d.process (l.data(), r.data(), n, type, 24.f, 20000.f, 1.f, lowCutHz, true);
        return l;
    };
    for (int type : { 0, 1, 3 })   // soft, hard, tube
    {
        CAPTURE (type);
        const auto clipped = run (type, 20.f), cut = run (type, 150.f);
        // cutting the 55 Hz difference tone out of a clipped wave turns its
        // flat tops into spikes (about 7.5 dB more crest untrimmed)
        CHECK (crestDb (cut, from, to) < crestDb (clipped, from, to) + 4.5);
        // and the trim adds no lows of its own
        CHECK (toneDb (cut, from, to - from, 55.0, fs) < toneDb (clipped, from, to - from, 55.0, fs) - 25.0);
        CHECK (toneDb (cut, from, to - from, 110.0, fs) < toneDb (clipped, from, to - from, 110.0, fs) - 9.0);
    }
}

TEST_CASE ("Auto Level clips the same at any input level and keeps a decay", "[fx]")
{
    const float fs = 48000.f;
    const int n = (int) fs;
    const size_t from = (size_t) (0.3f * fs), to = (size_t) n;

    // the same tone 20 dB apart: with Auto Level the output is the same
    // sound 20 dB apart; without it the quiet one is barely touched
    const auto run = [&] (float gain, bool autoLevel) {
        auto l = sawTone (n, fs, gain), r = l;
        Distortion d;
        d.prepare (fs);
        d.process (l.data(), r.data(), n, 1, 12.f, 20000.f, 1.f, 20.f, autoLevel);
        return l;
    };
    const auto loud = run (1.f, true), quiet = run (0.1f, true);
    CHECK (std::abs (rmsDb (loud, from, to) - rmsDb (quiet, from, to) - 20.0) < 0.5);
    CHECK (std::abs (crestDb (loud, from, to) - crestDb (quiet, from, to)) < 0.5);
    const auto dry = sawTone (n, fs, 1.f);
    CHECK (crestDb (loud, from, to) < crestDb (dry, from, to) - 3.0);   // it really clips
    const auto fixedQuiet = run (0.1f, false), fixedLoud = run (1.f, false);
    CHECK (std::abs (crestDb (fixedLoud, from, to) - crestDb (fixedQuiet, from, to)) > 2.0);

    // a tone falling 30 dB a second still falls about 30 dB a second (18 dB
    // between the windows); a clipper at a fixed drive squashes the fall
    const auto fall = [&] (bool autoLevel) {
        auto l = sawTone (n, fs, 1.f, 30.f), r = l;
        Distortion d;
        d.prepare (fs);
        d.process (l.data(), r.data(), n, 1, 24.f, 20000.f, 1.f, 20.f, autoLevel);
        return rmsDb (l, (size_t) (0.2f * fs), (size_t) (0.3f * fs)) - rmsDb (l, (size_t) (0.8f * fs), (size_t) (0.9f * fs));
    };
    CHECK (std::abs (fall (true) - 18.0) < 3.0);
    CHECK (fall (false) < 13.0);
}

// ---------------------------------------------------------------------------
// Switching effects on and off
// ---------------------------------------------------------------------------
namespace
{
    // Every effect that fades with `on`, in the settings of a busy patch.
    struct AllFx
    {
        static constexpr size_t kCount = 13;
        static const char* name (size_t i)
        {
            static const char* n[kCount] = { "Distortion", "Bitcrusher", "FrequencyShifter", "Phaser", "ChorusFlanger", "StereoDelay", "Reverb",
                                             "PhosDistortion", "PhosEq", "PhosDelay", "PhosReverb", "PhosChorus", "PhosLimiter" };
            return n[i];
        }

        Distortion dist;
        Bitcrusher crush;
        FrequencyShifter shift;
        Phaser phaser;
        ChorusFlanger chorus;
        StereoDelay delay;
        Reverb reverb;
        PhosDistortion phosDist;
        PhosEq phosEq;
        PhosDelay phosDelay;
        PhosReverb phosReverb;
        PhosChorus phosChorus;
        PhosLimiter phosLimiter;

        void prepare (float fs)
        {
            dist.prepare (fs); crush.prepare (fs); shift.prepare (fs); phaser.prepare (fs); chorus.prepare (fs); delay.prepare (fs);
            reverb.prepare (fs); phosDist.prepare (fs); phosEq.prepare (fs); phosDelay.prepare (fs); phosReverb.prepare (fs);
            phosChorus.prepare (fs); phosLimiter.prepare (fs);
        }

        bool active (size_t i, bool on)
        {
            switch (i)
            {
                case 0: return dist.isActive (on);
                case 1: return crush.isActive (on);
                case 2: return shift.isActive (on);
                case 3: return phaser.isActive (on);
                case 4: return chorus.isActive (on);
                case 5: return delay.isActive (on);
                case 6: return reverb.isActive (on);
                case 7: return phosDist.isActive (on);
                case 8: return phosEq.isActive (on);
                case 9: return phosDelay.isActive (on);
                case 10: return phosReverb.isActive (on);
                case 11: return phosChorus.isActive (on);
                default: return phosLimiter.isActive (on);
            }
        }

        void run (size_t i, float* l, float* r, int n, bool on)
        {
            switch (i)
            {
                case 0: dist.process (l, r, n, 0, 12.f, 12000.f, 1.f, 20.f, false, on); break;
                case 1: crush.process (l, r, n, 4.f, 4.f, 1.f, on); break;
                case 2: shift.process (l, r, n, 200.f, 0.3f, 0.f, 1.f, on); break;
                case 3: phaser.process (l, r, n, 0.5f, 0.8f, 0.5f, 800.f, 1.f, on); break;
                case 4: chorus.process (l, r, n, false, 1.f, 0.7f, 0.3f, 1.f, on); break;
                case 5: delay.process (l, r, n, 120.f, 0.9f, true, 20.f, 18000.f, 0.7f, on); break;
                case 6: reverb.process (l, r, n, 0.5f, 6.f, 0.3f, 10.f, 0.5f, on); break;
                case 7: phosDist.process (l, r, n, 3, 0.7f, 0.3f, 0.5f, false, on); break;
                case 8:
                {
                    const std::array<PhosEq::Band, 4> bands { { { 6.f, 100.f, 1.f }, { -4.f, 800.f, 1.f }, { 5.f, 3000.f, 1.f }, { 3.f, 9000.f, 1.f } } };
                    phosEq.process (l, r, n, bands, on);
                    break;
                }
                case 9: phosDelay.process (l, r, n, 150.f, 200.f, 0.8f, 1, 6000.f, 0.2f, 0.3f, 0.6f, on); break;
                case 10:
                {
                    PhosReverb::Settings s;
                    s.mix = 0.5f;
                    s.feedback = 0.8f;
                    phosReverb.process (l, r, n, s, on);
                    break;
                }
                case 11: phosChorus.process (l, r, n, 0.5f, 0.6f, 0.5f, 0.5f, 0.5f, 0.2f, 0.f, 0.5f, on); break;
                default: phosLimiter.process (l, r, n, 0.5f, 8.f, 1.f, 100.f, -12.f, 0.f, true, on); break;
            }
        }
    };

    float maxStep (const std::vector<float>& x, size_t from, size_t to)
    {
        float m = 0.f;
        for (size_t i = std::max<size_t> (from, 1); i < to; ++i)
            m = std::max (m, std::abs (x[i] - x[i - 1]));
        return m;
    }

    std::vector<float> busyInput (size_t n, float fs)
    {
        std::vector<float> x (n);
        for (size_t i = 0; i < n; ++i)
            x[i] = 0.4f * (float) std::sin (2.0 * std::numbers::pi * 300.0 * (double) i / fs)
                   + 0.2f * (float) std::sin (2.0 * std::numbers::pi * 1700.0 * (double) i / fs);
        return x;
    }

    // The engine's way: call the effect while it is active, leave the audio alone otherwise.
    std::vector<float> runSwitched (size_t which, const std::vector<float>& dry, float fs, size_t offAt, size_t onAt)
    {
        AllFx fx;
        fx.prepare (fs);
        std::vector<float> out (dry.size());
        for (size_t at = 0; at < dry.size(); at += 64)
        {
            const bool on = at < offAt || at >= onAt;
            std::vector<float> l (dry.begin() + (long) at, dry.begin() + (long) at + 64), r = l;
            if (fx.active (which, on))
                fx.run (which, l.data(), r.data(), 64, on);
            std::copy (l.begin(), l.end(), out.begin() + (long) at);
        }
        return out;
    }
} // namespace

TEST_CASE ("Every effect fades out and in over 10 ms with no step, and rests bit-exact", "[fx][switch]")
{
    const float fs = 48000.f;
    const size_t T = 48000;
    const auto dry = busyInput (T * 3, fs);
    for (size_t which = 0; which < AllFx::kCount; ++which)
    {
        const auto always = runSwitched (which, dry, fs, dry.size(), dry.size());
        const float bound = 2.5f * std::max (maxStep (always, T / 2, T), maxStep (dry, T / 2, T));

        // off at T, back on at 2T
        const auto toggled = runSwitched (which, dry, fs, T, 2 * T);
        INFO (AllFx::name (which) << ": allowed step " << bound);
        for (size_t i = 0; i < T; ++i)
            REQUIRE (toggled[i] == always[i]);   // identical until the switch
        CHECK (maxStep (toggled, T - 1, T + 2400) < bound);
        CHECK (maxStep (toggled, 2 * T - 1, 2 * T + 2400) < bound);
        // once faded, the audio passes untouched
        for (size_t i = T + 1024; i < 2 * T; ++i)
            REQUIRE (toggled[i] == dry[i]);
    }
}

TEST_CASE ("An effect switched on after a while off starts clean: no old echoes", "[fx][switch]")
{
    const float fs = 48000.f;
    Rng rng (5);
    for (size_t which = 0; which < AllFx::kCount; ++which)
    {
        AllFx fx;
        fx.prepare (fs);
        const int n = 64;
        std::vector<float> l ((size_t) n), r ((size_t) n);
        for (int block = 0; block < 400; ++block)   // loud noise: long tails in every delay and reverb
        {
            for (int i = 0; i < n; ++i) { l[(size_t) i] = 0.8f * rng.bipolar(); r[(size_t) i] = 0.8f * rng.bipolar(); }
            fx.run (which, l.data(), r.data(), n, true);
        }
        int guard = 0;
        while (fx.active (which, false) && guard++ < 100)   // off: until it has faded
        {
            std::fill (l.begin(), l.end(), 0.f);
            std::fill (r.begin(), r.end(), 0.f);
            fx.run (which, l.data(), r.data(), n, false);
        }
        REQUIRE (! fx.active (which, false));
        // on again, with silence in: nothing may come out
        float peak = 0.f;
        for (int block = 0; block < 300; ++block)
        {
            std::fill (l.begin(), l.end(), 0.f);
            std::fill (r.begin(), r.end(), 0.f);
            fx.run (which, l.data(), r.data(), n, true);
            for (int i = 0; i < n; ++i)
                peak = std::max ({ peak, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]) });
        }
        INFO (AllFx::name (which) << ": peak " << peak);
        CHECK (peak == 0.f);
    }
}

TEST_CASE ("An effect on from the start plays fully wet at once; only a switch fades", "[fx][switch]")
{
    // A from the first sample; B seen off for a block, then on with the same
    // input one block later. After B's 10 ms fade the two are the same audio,
    // so A never faded: a bounce or the first note of a patch sounds like the rest.
    const float fs = 48000.f;
    const int n = 64, blocks = 40;
    const auto x = busyInput ((size_t) (n * blocks), fs);
    for (size_t which = 0; which < AllFx::kCount; ++which)
    {
        AllFx a, b;
        a.prepare (fs);
        b.prepare (fs);
        std::vector<float> outA, outB;
        std::vector<float> zl ((size_t) n, 0.f), zr = zl;
        if (b.active (which, false)) b.run (which, zl.data(), zr.data(), n, false);
        for (int k = 0; k < blocks; ++k)
        {
            std::vector<float> l (x.begin() + k * n, x.begin() + (k + 1) * n), r = l;
            auto l2 = l, r2 = r;
            if (a.active (which, true)) a.run (which, l.data(), r.data(), n, true);
            if (b.active (which, true)) b.run (which, l2.data(), r2.data(), n, true);
            outA.insert (outA.end(), l.begin(), l.end());
            outB.insert (outB.end(), l2.begin(), l2.end());
        }
        INFO (AllFx::name (which));
        float early = 0.f, wet = 0.f;
        for (size_t i = 0; i < 480; ++i)
        {
            early = std::max (early, std::abs (outA[i] - outB[i]));
            wet = std::max (wet, std::abs (outA[i] - x[i]));
        }
        if (wet > 1e-3f)   // (a delay is still dry there: its first echo comes later)
            CHECK (early > 1e-4f);   // B faded in, A didn't
        bool same = true;
        for (size_t i = 600; i < outA.size(); ++i)
            same = same && outA[i] == outB[i];
        CHECK (same);   // the same effect, the same input, both fully wet
    }
}

TEST_CASE ("Distortion and the limiter report the samples they delay by", "[fx][latency]")
{
    const auto peakAt = [] (const std::vector<float>& x) {
        size_t best = 0;
        for (size_t i = 0; i < x.size(); ++i)
            if (std::abs (x[i]) > std::abs (x[best])) best = i;
        return (int) best;
    };

    SECTION ("Distortion below 60 kHz looks one sample ahead")
    {
        for (float fs : { 44100.f, 48000.f, 96000.f })
        {
            Distortion d;
            d.prepare (fs);
            std::vector<float> l (4096, 0.f), r (4096, 0.f);
            d.process (l.data(), r.data(), 2048, 0, 0.f, 20000.f, 0.f);   // fades in on silence
            l[2048] = r[2048] = 0.5f;
            d.process (l.data() + 2048, r.data() + 2048, 2048, 0, 0.f, 20000.f, 0.f);
            INFO ("fs " << fs << ": impulse at 2048 comes out at " << peakAt (l));
            CHECK (peakAt (l) - 2048 == d.latencySamples());
            CHECK (d.latencySamples() == (fs < 60000.f ? 1 : 0));
        }
    }
    SECTION ("PhosLimiter delays by its lookahead")
    {
        for (float fs : { 44100.f, 48000.f, 96000.f })
        {
            PhosLimiter lim;
            lim.prepare (fs);
            std::vector<float> l (4096, 0.f), r (4096, 0.f);
            l[2000] = r[2000] = 0.25f;
            lim.process (l.data(), r.data(), 4096, 0.5f, 4.f, 1.f, 100.f, 0.f, 0.f, false);
            INFO ("fs " << fs << ": peak " << peakAt (l));
            CHECK (peakAt (l) - 2000 == lim.latencySamples());
            CHECK (lim.latencySamples() == (int) std::lround (0.0033 * (double) fs));
        }
    }
    SECTION ("PhosDistortion's half-bands")
    {
        for (float fs : { 44100.f, 48000.f, 96000.f, 192000.f })
        {
            PhosDistortion d;
            d.prepare (fs);
            std::vector<float> l (4096, 0.f), r (4096, 0.f);
            d.process (l.data(), r.data(), 1024, 6, 0.f, 0.f, 0.f, false);
            l[1024] = r[1024] = 0.01f;   // small: the clipper is linear
            d.process (l.data() + 1024, r.data() + 1024, 3072, 6, 0.f, 0.f, 0.f, false);
            INFO ("fs " << fs << ": peak " << peakAt (l) - 1024 << ", reported " << d.latencySamples());
            CHECK (std::abs (peakAt (l) - 1024 - d.latencySamples()) <= 1);
        }
    }
}

TEST_CASE ("Phoscyon's overdrive clipper cannot overflow on a loud peak", "[fx][phoscyon]")
{
    // |x|^k with k up to 19 overflowed a float from |x| ~ 100 and the output fell to 0
    int overdrives = 0;
    for (const auto& m : PhosDistortion::models())
    {
        if (m.clip != PhosDistortion::Overdrive) continue;
        ++overdrives;
        for (float x : { 2.f, 10.f, 100.f, 1.0e3f, 1.0e6f, 1.0e18f })
        {
            INFO ("knee " << m.knee << ", x " << x);
            CHECK_THAT (PhosDistortion::clip (m, x), Catch::Matchers::WithinAbs (1.0, 2e-2));
            CHECK_THAT (PhosDistortion::clip (m, -x), Catch::Matchers::WithinAbs (-1.0, 2e-2));
        }
        // and the value below 1 is the old one
        CHECK_THAT (PhosDistortion::clip (m, 0.5f), Catch::Matchers::WithinAbs (0.5 / std::pow (1.0 + std::pow (0.5, 1.0 + 18.1 * m.knee * 0.01), 1.0 / (1.0 + 18.1 * m.knee * 0.01)), 1e-5));
    }
    CHECK (overdrives >= 6);

    // a full-drive overdrive on a half-scale sine keeps its peaks
    PhosDistortion d;
    d.prepare (48000.f);
    const int n = 9600;
    std::vector<float> l ((size_t) n), r;
    for (int i = 0; i < n; ++i) l[(size_t) i] = 0.5f * (float) std::sin (2.0 * std::numbers::pi * 200.0 * i / 48000.0);
    r = l;
    const auto in = l;
    d.process (l.data(), r.data(), n, 14, 1.f, 0.f, 0.f, false);
    float top = 0.f;
    for (int i = 2000; i < n; ++i) top = std::max (top, std::abs (l[(size_t) i]));
    int zeroed = 0;
    for (int i = 2000; i < n; ++i)
        if (std::abs (in[(size_t) i]) > 0.45f && std::abs (l[(size_t) i]) < 0.3f * top) ++zeroed;
    CHECK (top > 0.01f);
    CHECK (zeroed == 0);
}

TEST_CASE ("Phoscyon's biquads run in double", "[fx][phoscyon]")
{
    // The 16 .. 60 Hz high-passes of the models at the clipper's 192 kHz: the
    // float coefficients put the response 50 dB off.
    for (double hz : { 16.0, 24.0, 60.0 })
    {
        Biquad b;
        b.highPass (192000.0, hz, 0.7071);
        for (double f : { 5.0, 10.0, 30.0, 100.0, 1000.0 })
        {
            const std::complex<double> z = std::polar (1.0, -2.0 * std::numbers::pi * f / 192000.0);
            const auto h = ((double) b.b0 + (double) b.b1 * z + (double) b.b2 * z * z) / (1.0 + (double) b.a1 * z + (double) b.a2 * z * z);
            // the analogue 2nd-order Butterworth high-pass at the warped frequency
            const double w = std::tan (std::numbers::pi * f / 192000.0) / std::tan (std::numbers::pi * hz / 192000.0);
            const auto s = std::complex<double> (0.0, w);
            const auto ideal = (s * s) / (s * s + std::sqrt (2.0) * s + 1.0);
            const double errDb = 20.0 * std::log10 (std::abs (h) / std::abs (ideal));
            INFO ("hp " << hz << " Hz at " << f << " Hz: " << errDb << " dB off");
            CHECK (std::abs (errDb) < 0.001);
        }
    }

    // the filtered signal agrees with a double-precision run of the same recursion
    Biquad b;
    b.highPass (192000.0, 20.0, 0.7071);
    const double w0 = 2.0 * std::numbers::pi * 20.0 / 192000.0, c = std::cos (w0), alpha = std::sin (w0) / (2.0 * 0.7071);
    const double a0 = 1.0 + alpha, b0 = (1.0 + c) / 2.0 / a0, b1 = -(1.0 + c) / a0, a1 = -2.0 * c / a0, a2 = (1.0 - alpha) / a0;
    double z1 = 0.0, z2 = 0.0, err = 0.0, sig = 0.0;
    for (int i = 0; i < 400000; ++i)
    {
        const float x = 0.5f * (float) std::sin (2.0 * std::numbers::pi * 200.0 * i / 192000.0) + 0.1f;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b0 * x - a2 * y;   // b2 == b0 for a high-pass
        const double got = b.process (x);
        if (i > 100000) { err += (got - y) * (got - y); sig += y * y; }
    }
    CHECK (10.0 * std::log10 (err / sig) < -90.0);
}

TEST_CASE ("Phoscyon's EQ restarts a band that was at 0 dB from rest", "[fx][phoscyon]")
{
    const float fs = 48000.f;
    Rng rng (3);
    std::array<PhosEq::Band, 4> on { { { 6.f, 300.f, 1.f }, { 0.f, 1000.f, 1.f }, { 0.f, 3000.f, 1.f }, { 0.f, 9000.f, 1.f } } };
    auto off = on;
    off[0].db = 0.f;

    PhosEq a, fresh;
    a.prepare (fs);
    fresh.prepare (fs);
    std::vector<float> l (512), r (512);
    const auto noise = [&] (std::vector<float>& x) { for (auto& v : x) v = 0.5f * rng.bipolar(); };
    for (int block = 0; block < 20; ++block) { noise (l); r = l; a.process (l.data(), r.data(), 512, on); }
    for (int block = 0; block < 20; ++block) { noise (l); r = l; a.process (l.data(), r.data(), 512, off); }
    // a clone fed zeros has the same blank state and fully faded in
    std::fill (l.begin(), l.end(), 0.f);
    r = l;
    for (int block = 0; block < 4; ++block) fresh.process (l.data(), r.data(), 512, on);

    noise (l);
    r = l;
    auto lf = l, rf = r;
    a.process (l.data(), r.data(), 512, on);
    fresh.process (lf.data(), rf.data(), 512, on);
    CHECK (l == lf);
}

TEST_CASE ("The leveller eases its gain back when Dynamics goes to 0", "[fx][phoscyon]")
{
    // Dynamics 1 lifts a quiet signal after the clipper by up to 24 dB; Dynamics 0
    // used to drop that in a single sample.
    const float fs = 48000.f;
    PhosDistortion d;
    d.prepare (fs);
    const int n = 24000;
    std::vector<float> l ((size_t) n), r;
    for (int i = 0; i < n; ++i) l[(size_t) i] = 0.004f * (float) std::sin (2.0 * std::numbers::pi * 150.0 * i / fs);
    r = l;
    auto l2 = l, r2 = l;
    d.process (l.data(), r.data(), n, 6, 0.1f, 0.f, 1.f, true);
    d.process (l2.data(), r2.data(), n, 6, 0.1f, 0.f, 0.f, true);   // continues: Dynamics 0
    auto joined = l;
    joined.insert (joined.end(), l2.begin(), l2.end());
    const float before = maxStep (l, 12000, 24000);
    const float after = maxStep (joined, (size_t) n - 1, (size_t) n + 600);
    INFO ("steady step " << before << ", after Dynamics -> 0: " << after);
    CHECK (after < 4.f * before);
}

TEST_CASE ("Frequency shifter keeps the bass image down at 96 and 192 kHz", "[fx]")
{
    // The 4 + 4 sections of the 48 kHz design were 20 degrees off in the bass
    // at 96 kHz and 46 at 192 kHz: the unwanted sideband of a 40 Hz tone came
    // back at -10 dB.
    for (float fs : { 44100.f, 48000.f, 96000.f, 192000.f })
    {
        FrequencyShifter fsh;
        fsh.prepare (fs);
        const size_t n = 1 << 17;
        std::vector<float> l (n), r (n);
        for (size_t i = 0; i < n; ++i) l[i] = r[i] = 0.5f * (float) std::sin (2.0 * std::numbers::pi * 40.0 * (double) i / fs);
        fsh.process (l.data(), r.data(), (int) n, 100.f, 0.f, 0.f, 1.f);
        const auto db = testutil::spectrumDb (l, n);
        const auto at = [&] (double hz) {
            const size_t c = (size_t) std::lround (hz / fs * (double) n);
            double m = -300.0;
            for (size_t i = c - 6; i <= c + 6; ++i) m = std::max (m, db[i]);
            return m;
        };
        const double wanted = at (140.0), image = at (60.0);
        INFO ("fs " << fs << ": 140 Hz " << wanted << " dB, image at 60 Hz " << image << " dB");
        CHECK (image < wanted - 45.0);
    }
}

TEST_CASE ("An LFO keeps its rate at 0.02 Hz and at a synced 1/16", "[env]")
{
    // a float phase added 0.02 / 96000 per sample and lost the low bits: -7.6 % in rate
    struct Case { float fs, hz; double seconds; };
    for (const auto& c : { Case { 96000.f, 0.02f, 37.0 }, Case { 48000.f, 0.0625f, 28.0 } })
    {
        Lfo lfo;
        lfo.setSampleRate (c.fs);
        lfo.reset (0.f);
        const auto n = (size_t) std::llround (c.seconds * (double) c.fs);
        for (size_t i = 0; i < n; ++i) lfo.process (0, c.hz);
        double expected = std::fmod ((double) c.hz * c.seconds, 2.0);
        double got = (double) lfo.phase2();
        INFO (c.hz << " Hz for " << c.seconds << " s: phase " << got << ", expected " << expected);
        CHECK_THAT (got, Catch::Matchers::WithinAbs (expected, 1e-3));
    }
}

TEST_CASE ("An envelope keeps its times when the sample rate changes", "[env]")
{
    // the cached coefficients belonged to the old rate: 100 ms became 49.9 ms
    const auto releaseMs = [] (float fromRate, float toRate) {   // 95 % of the set time: it ends at 1e-5, not 0
        Adsr env;
        env.setSampleRate (fromRate);
        env.set (1.f, 50.f, 0.7f, 100.f);
        env.setSampleRate (toRate);
        env.set (1.f, 50.f, 0.7f, 100.f);
        env.noteOn();
        for (int i = 0; i < (int) (toRate * 0.3f); ++i) env.process();
        env.noteOff();
        int n = 0;
        while (env.active() && n < (int) (toRate * 5.f)) { env.process(); ++n; }
        return 1000.0 * n / (double) toRate;
    };
    CHECK_THAT (releaseMs (96000.f, 192000.f), Catch::Matchers::WithinRel (releaseMs (192000.f, 192000.f), 0.01));
    CHECK_THAT (releaseMs (48000.f, 96000.f), Catch::Matchers::WithinRel (releaseMs (96000.f, 96000.f), 0.01));
    CHECK_THAT (releaseMs (192000.f, 44100.f), Catch::Matchers::WithinRel (releaseMs (44100.f, 44100.f), 0.01));
    CHECK_THAT (releaseMs (48000.f, 48000.f), Catch::Matchers::WithinRel (95.0, 0.02));
}

TEST_CASE ("A bent attack does not jump back after the curve is switched off and on", "[env]")
{
    Adsr env;
    env.setSampleRate (48000.f);
    env.set (400.f, 100.f, 0.5f, 100.f);
    env.setAttackCurve (0.6f);
    env.noteOn();
    float prev = 0.f;
    for (int i = 0; i < 4000; ++i) prev = env.process();   // part of the way up the swell
    env.setAttackCurve (0.f);                              // the plain RC charge runs for a while
    for (int i = 0; i < 2000; ++i) prev = env.process();
    env.setAttackCurve (0.6f);
    for (int i = 0; i < 1000; ++i)
    {
        const float y = env.process();
        INFO ("sample " << i << ": " << y << " after " << prev);
        REQUIRE (y >= prev - 1e-6f);
        prev = y;
    }
}

TEST_CASE ("Distortion and the other block-rate controls ramp across the block", "[fx]")
{
    // drive 0 -> 12 dB on alternate blocks: the step at the block boundary
    // used to be the gain change in one sample
    const float fs = 48000.f;
    Distortion d;
    d.prepare (fs);
    const int n = 256;
    std::vector<float> all;
    for (int block = 0; block < 60; ++block)
    {
        std::vector<float> l ((size_t) n), r;
        for (int i = 0; i < n; ++i)
            l[(size_t) i] = 0.3f * (float) std::sin (2.0 * std::numbers::pi * 200.0 * (block * n + i) / fs);
        r = l;
        d.process (l.data(), r.data(), n, 0, block % 2 == 0 ? 0.f : 12.f, 20000.f, 1.f);
        all.insert (all.end(), l.begin(), l.end());
    }
    float inner = 0.f, edge = 0.f;
    for (size_t b = 10; b < 60; ++b)
    {
        edge = std::max (edge, std::abs (all[b * 256] - all[b * 256 - 1]));
        for (size_t i = 8; i < 248; ++i)
            inner = std::max (inner, std::abs (all[b * 256 + i] - all[b * 256 + i - 1]));
    }
    INFO ("block edge " << edge << ", inside the blocks " << inner);
    CHECK (edge < 1.2f * inner);
}

TEST_CASE ("Reverb, chorus and Phoscyon's reverb and chorus glide to new settings", "[fx]")
{
    // the line lengths and pre-delays used to jump at the block boundary
    const float fs = 48000.f;
    const auto jumpRatio = [&] (auto&& makeFx, auto&& call) {
        auto fx = makeFx();
        const int n = 128;
        std::vector<float> out;
        for (int block = 0; block < 700; ++block)
        {
            std::vector<float> l ((size_t) n), r;
            for (int i = 0; i < n; ++i)
                l[(size_t) i] = 0.3f * (float) std::sin (2.0 * std::numbers::pi * 220.0 * (block * n + i) / fs);
            r = l;
            call (fx, l.data(), r.data(), n, block >= 350);
            out.insert (out.end(), l.begin(), l.end());
        }
        // the step where the setting changes against the largest step either side of it
        const float before = maxStep (out, 300 * 128, 350 * 128), later = maxStep (out, 500 * 128, 700 * 128);
        const float after = maxStep (out, 350 * 128 - 1, 360 * 128);
        return after / std::max (before, later);
    };

    SECTION ("Reverb size and pre-delay")
    {
        const double ratio = jumpRatio ([&] { Reverb r; r.prepare (fs); return r; },
                                        [] (Reverb& r, float* l, float* rr, int n, bool second) {
                                            r.process (l, rr, n, second ? 0.9f : 0.2f, 3.f, 0.3f, second ? 80.f : 5.f, 0.5f);
                                        });
        INFO ("largest step after the change / before: " << ratio);
        CHECK (ratio < 1.5);
    }
    SECTION ("Chorus depth and the flanger switch")
    {
        const double ratio = jumpRatio ([&] { ChorusFlanger c; c.prepare (fs); return c; },
                                        [] (ChorusFlanger& c, float* l, float* r, int n, bool second) {
                                            c.process (l, r, n, second, 0.3f, second ? 1.f : 0.2f, 0.2f, 1.f);
                                        });
        INFO ("largest step after the change / before: " << ratio);
        CHECK (ratio < 1.5);
    }
    SECTION ("PhosReverb room size")
    {
        const double ratio = jumpRatio ([&] { PhosReverb r; r.prepare (fs); return r; },
                                        [] (PhosReverb& r, float* l, float* rr, int n, bool second) {
                                            PhosReverb::Settings s;
                                            s.mix = 0.5f;
                                            s.lateSize = second ? 40.f : 8.f;
                                            s.earlySize = second ? 9.f : 1.f;
                                            s.preMs = second ? 90.f : 5.f;
                                            r.process (l, rr, n, s);
                                        });
        INFO ("largest step after the change / before: " << ratio);
        CHECK (ratio < 1.5);
    }
    SECTION ("PhosChorus offset and depth")
    {
        const double ratio = jumpRatio ([&] { PhosChorus c; c.prepare (fs); return c; },
                                        [] (PhosChorus& c, float* l, float* r, int n, bool second) {
                                            c.process (l, r, n, second ? 1.f : 0.f, second ? 1.f : 0.1f, 0.3f, second ? 1.f : 0.f,
                                                       second ? 1.f : 0.f, 0.2f, 0.f, 1.f);
                                        });
        INFO ("largest step after the change / before: " << ratio);
        CHECK (ratio < 1.5);
    }
}

TEST_CASE ("A long delay line keeps the precision of a fractional read", "[fx]")
{
    // The read position was a float of the write index minus the delay: past
    // 65536 samples it is quantised to 1/128 sample (1/32 past 2^18), a
    // zipper in a smoothed delay time. Checked against a Hermite read in double.
    DelayLine line;
    line.prepare (131072);
    const double fs = 48000.0;
    const size_t n = 131072;
    std::vector<float> in (n);
    for (size_t i = 0; i < n; ++i)
    {
        in[i] = 0.5f * (float) std::sin (2.0 * std::numbers::pi * 3000.0 * (double) i / fs);
        line.push (in[i]);
    }
    double worst = 0.0;
    for (int k = 0; k < 40; ++k)
    {
        const float delay = 5000.f + 0.0137f * (float) k;
        const double pos = (double) (n - 1) - (double) delay;
        const auto i = (size_t) std::floor (pos);
        const double f = pos - (double) i;
        const double xm1 = in[i - 1], x0 = in[i], x1 = in[i + 1], x2 = in[i + 2];
        const double c1 = 0.5 * (x1 - xm1), c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
        const double ref = ((c3 * f + c2) * f + c1) * f + x0;
        worst = std::max (worst, std::abs ((double) line.read (delay) - ref));
    }
    INFO ("worst error " << worst);
    CHECK (worst < 2e-6);
}

TEST_CASE ("The distortion's low cut starts from rest when it comes on", "[fx]")
{
    // the cut's filters sat idle holding whatever they had when it went off
    const float fs = 48000.f;
    const int n = 4096;
    std::vector<float> in ((size_t) n);
    for (int i = 0; i < n; ++i) in[(size_t) i] = 0.3f * (float) std::sin (2.0 * std::numbers::pi * 500.0 * i / fs);

    // a: cut on, off, on.   c: off, off, on.   Nothing else differs between them.
    Distortion a, c;
    a.prepare (fs);
    c.prepare (fs);
    const auto block = [&] (Distortion& d, float cut) {
        auto l = in, r = in;
        d.process (l.data(), r.data(), n, 0, 12.f, 12000.f, 1.f, cut);
        return l;
    };
    block (a, 300.f);
    block (a, 20.f);
    block (c, 20.f);
    block (c, 20.f);
    CHECK (block (a, 300.f) == block (c, 300.f));
}
