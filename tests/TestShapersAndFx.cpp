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
        for (size_t i = 0; i < l.size(); ++i) { l[i] = 0.5f * (float) std::sin (2.0 * M_PI * hz * (double) i / fs); r[i] = -l[i]; }
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
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.5f * (float) std::sin (2.0 * M_PI * hz * (double) i / fs);
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
    for (size_t i = 0; i < l.size(); ++i) l[i] = (float) std::sin (2.0 * M_PI * 1000.0 * (double) i / 48000.0);
    runWidth (w, l, r, 0.f, 0.f, 10.f, 120.f);
    for (size_t i = 4800; i < l.size(); ++i)
        REQUIRE (std::abs (l[i] - r[i]) < 1e-5f);
}

namespace
{
    // Level of one frequency in x[from, from + n), in dB (Goertzel, Hann).
    double toneDb (const std::vector<float>& x, size_t from, size_t n, double hz, double fs)
    {
        const double w = 2.0 * M_PI * hz / fs, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) n);
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
            for (int k = 4; k <= 60; ++k) v += std::sin (2.0 * M_PI * 55.0 * k * i / fs) / k;
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
        in[(size_t) i] = 0.2f * (float) (std::sin (2.0 * M_PI * 40.0 * i / fs) + std::sin (2.0 * M_PI * 1000.0 * i / fs));

    // at its 20 Hz default the distortion is sample for sample what it was
    Distortion a, b;
    a.prepare (fs);
    b.prepare (fs);
    auto la = in, ra = in, lb = in, rb = in;
    a.process (la.data(), ra.data(), n, 0, 12.f, 12000.f, 1.f);
    b.process (lb.data(), rb.data(), n, 0, 12.f, 12000.f, 1.f, 20.f);
    CHECK (la == lb);

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
