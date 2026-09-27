#include "TestUtil.h"
#include "dsp/Dynamics.h"

#include <catch2/catch_test_macros.hpp>

using namespace psy::dsp;

namespace
{
    constexpr float kFs = 48000.f;

    // Runs the compressor over a mono signal fed to both channels, in blocks.
    std::vector<float> run (Compressor& c, const std::vector<float>& x, const CompSettings& s, int block = 256)
    {
        std::vector<float> l (x), r (x);
        for (size_t i = 0; i < x.size(); i += (size_t) block)
        {
            const int n = (int) std::min ((size_t) block, x.size() - i);
            c.process (l.data() + i, r.data() + i, n, s);
        }
        return l;
    }

    std::vector<float> sine (double hz, float amp, int n, double fs = kFs)
    {
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i) x[(size_t) i] = amp * (float) std::sin (2.0 * M_PI * hz * i / fs);
        return x;
    }

    float peakDb (const std::vector<float>& x, size_t from)
    {
        float pk = 0.f;
        for (size_t i = from; i < x.size(); ++i) pk = std::max (pk, std::abs (x[i]));
        return gainToDb (pk);
    }

    // Magnitude response (dB) of an impulse response, no window.
    std::vector<double> magnitudeDb (const std::vector<float>& ir)
    {
        std::vector<std::complex<double>> a (ir.begin(), ir.end());
        testutil::fft (a);
        std::vector<double> db (a.size() / 2);
        for (size_t i = 0; i < db.size(); ++i) db[i] = 20.0 * std::log10 (std::abs (a[i]) + 1e-15);
        return db;
    }

    // Worst harmonic (2nd..10th) relative to the fundamental, in dB.
    double worstHarmonicDb (const std::vector<float>& x, double fs, double f0)
    {
        const size_t n = 1 << 16;
        const std::vector<float> tail (x.end() - (long) n, x.end());
        const auto db = testutil::spectrumDb (tail, n);
        const double bin = fs / (double) n;
        const auto around = [&] (double hz) {
            const auto c = (size_t) std::lround (hz / bin);
            double m = -300.0;
            for (size_t i = c - 3; i <= c + 3; ++i) m = std::max (m, db[i]);
            return m;
        };
        const double fund = around (f0);
        double worst = -300.0;
        for (int h = 2; h <= 10; ++h) worst = std::max (worst, around (f0 * h));
        return worst - fund;
    }

    CompSettings multiband()
    {
        CompSettings s;
        s.on = true;
        s.mode = 1;
        return s;
    }
} // namespace

TEST_CASE ("Crossover bands sum to a flat magnitude response", "[dynamics]")
{
    for (float fs : { 44100.f, 48000.f, 96000.f })
        for (auto [f1, f2] : { std::pair { 80.f, 2000.f }, std::pair { 120.f, 2500.f }, std::pair { 400.f, 9000.f } })
        {
            dyn::Crossover3 x;
            x.set (prewarp (f1, fs), prewarp (f2, fs));
            std::vector<float> ir (1 << 16);
            std::array<float, 3> b;
            for (size_t i = 0; i < ir.size(); ++i)
            {
                x.process (i == 0 ? 1.f : 0.f, b);
                ir[i] = b[0] + b[1] + b[2];
            }
            const auto db = magnitudeDb (ir);
            const double bin = fs / (double) ir.size();
            double worst = 0.0;
            for (size_t i = (size_t) (20.0 / bin); i < db.size() && i * bin < 20000.0; ++i)
                worst = std::max (worst, std::abs (db[i]));
            INFO ("fs " << fs << "  split " << f1 << " / " << f2);
            CHECK (worst < 0.05);
        }
}

TEST_CASE ("Multiband with no compression is transparent in magnitude at any depth", "[dynamics]")
{
    for (float depth : { 0.f, 0.5f, 1.f })
    {
        Compressor c;
        c.prepare (kFs);
        auto s = multiband();
        s.down = 0.f;
        s.up = 0.f;
        s.mix = depth;
        // A settled impulse response: run the fade-in on silence first.
        std::vector<float> x (1 << 16, 0.f);
        run (c, std::vector<float> (4800, 0.f), s);
        x[0] = 0.5f;
        const auto y = run (c, x, s);
        std::vector<float> ir (y);
        for (auto& v : ir) v *= 2.f;
        const auto db = magnitudeDb (ir);
        const double bin = kFs / (double) ir.size();
        double worst = 0.0;
        for (size_t i = (size_t) (20.0 / bin); i < db.size() && i * bin < 20000.0; ++i)
            worst = std::max (worst, std::abs (db[i]));
        INFO ("depth " << depth);
        CHECK (worst < 0.05);
    }
}

TEST_CASE ("Normal mode follows its static curve", "[dynamics]")
{
    CompSettings s;
    s.on = true;
    s.threshDb = -18.f;
    s.ratio = 4.f;
    s.attackMs = 1.f;
    s.releaseMs = 50.f;

    SECTION ("hard knee: -6 dB in, 12 dB over at 4:1 -> -15 dB out")
    {
        s.kneeDb = 0.f;
        Compressor c;
        c.prepare (kFs);
        const auto y = run (c, sine (1000.0, dbToGain (-6.f), (int) kFs), s);
        CHECK (std::abs (peakDb (y, (size_t) kFs / 2) - -15.f) < 0.3f);
    }
    SECTION ("soft knee: a signal at the threshold gets slope * (w/2)^2 / 2w")
    {
        s.kneeDb = 12.f;
        Compressor c;
        c.prepare (kFs);
        const auto y = run (c, sine (1000.0, dbToGain (-18.f), (int) kFs), s);
        const float expected = -18.f - 0.75f * 36.f / 24.f;
        CHECK (std::abs (peakDb (y, (size_t) kFs / 2) - expected) < 0.3f);
    }
    SECTION ("below the knee nothing changes")
    {
        s.kneeDb = 6.f;
        Compressor c;
        c.prepare (kFs);
        const auto y = run (c, sine (1000.0, dbToGain (-30.f), (int) kFs), s);
        CHECK (std::abs (peakDb (y, (size_t) kFs / 2) - -30.f) < 0.05f);
    }
}

TEST_CASE ("Upward compression lifts a quiet band by the expected amount", "[dynamics]")
{
    Compressor c;
    c.prepare (kFs);
    auto s = multiband();
    s.down = 0.f;
    s.up = 1.f;                 // 1:4 upward, slope 0.75
    s.upDb = { -30.f, -30.f, -30.f };
    s.downDb = { 0.f, 0.f, 0.f };
    s.attackMs = 2.f;
    s.releaseMs = 50.f;
    const auto y = run (c, sine (1000.0, dbToGain (-50.f), (int) kFs * 2), s);
    // mid band sees ~-50.2 dB (LP4 skirt at 1 kHz), so it is lifted by 0.75 * 20.2
    CHECK (std::abs (peakDb (y, (size_t) kFs) - -35.f) < 0.5f);
}

TEST_CASE ("Digital silence stays exactly silent, even after a loud burst", "[dynamics]")
{
    for (int mode : { 0, 1 })
    {
        Compressor c;
        c.prepare (kFs);
        auto s = multiband();
        s.mode = mode;
        s.up = 1.f;
        s.upDb = { -20.f, -20.f, -20.f };
        s.gainDb = { 12.f, 12.f, 12.f };
        s.outDb = 12.f;
        psy::dsp::Rng rng (3);
        std::vector<float> x ((size_t) kFs * 2, 0.f);
        for (size_t i = 0; i < 4800; ++i) x[i] = rng.bipolar();
        const auto y = run (c, x, s);
        for (size_t i = (size_t) kFs; i < y.size(); ++i)
            REQUIRE (y[i] == 0.f);
    }
}

TEST_CASE ("A low sine under heavy, fast compression stays clean", "[dynamics]")
{
    // The detector holds a whole cycle, so the gain does not ride the waveform.
    for (int mode : { 0, 1 })
    {
        Compressor c;
        c.prepare (kFs);
        CompSettings s = multiband();
        s.mode = mode;
        s.threshDb = -30.f;
        s.ratio = 10.f;
        s.kneeDb = 0.f;
        s.downDb = { -30.f, -30.f, -30.f };
        s.down = 1.f;
        s.up = 0.f;
        s.attackMs = 0.1f;
        s.releaseMs = 20.f;
        const auto y = run (c, sine (41.0, 0.7f, (int) kFs * 3), s);
        INFO ("mode " << mode);
        CHECK (peakDb (y, (size_t) kFs * 2) < -18.f);   // it did compress hard
        CHECK (worstHarmonicDb (y, kFs, 41.0) < -45.0);
    }
}

TEST_CASE ("Normal mode at 0 % mix is sample-identical to the input", "[dynamics]")
{
    Compressor c;
    c.prepare (kFs);
    CompSettings s;
    s.on = true;
    s.mix = 0.f;
    s.threshDb = -40.f;
    s.ratio = 20.f;
    const auto x = sine (220.0, 0.8f, 20000);
    const auto y = run (c, x, s);
    for (size_t i = 0; i < x.size(); ++i)
        REQUIRE (y[i] == x[i]);
}

TEST_CASE ("Detection is stereo-linked, so the image does not move", "[dynamics]")
{
    for (int mode : { 0, 1 })
    {
        Compressor c;
        c.prepare (kFs);
        auto s = multiband();
        s.mode = mode;
        s.threshDb = -30.f;
        s.ratio = 8.f;
        s.up = 1.f;
        psy::dsp::Rng rng (11);
        std::vector<float> l (48000), r (48000);
        for (size_t i = 0; i < l.size(); ++i)
        {
            l[i] = rng.bipolar() * (i % 12000 < 6000 ? 0.9f : 0.02f);
            r[i] = 0.5f * l[i];
        }
        for (size_t i = 0; i < l.size(); i += 256)
            c.process (l.data() + i, r.data() + i, (int) std::min<size_t> (256, l.size() - i), s);
        for (size_t i = 0; i < l.size(); ++i)
            REQUIRE (std::abs (r[i] - 0.5f * l[i]) <= 1e-5f * (1.f + std::abs (l[i])));
    }
}

TEST_CASE ("Switching on, off and between modes does not click", "[dynamics]")
{
    // A 200 Hz sine moves at most 0.013 per sample; a hard switch under ~10 dB
    // of gain change would jump by ~0.3.
    Compressor c;
    c.prepare (kFs);
    CompSettings s;
    s.on = true;
    s.threshDb = -30.f;
    s.ratio = 10.f;
    s.downDb = { -30.f, -30.f, -30.f };
    s.gainDb = { 6.f, 6.f, 6.f };
    const auto x = sine (200.0, 0.5f, (int) kFs * 3);
    std::vector<float> l (x), r (x);
    const int block = 128;
    for (size_t i = 0; i < x.size(); i += block)
    {
        const size_t t = i / (size_t) (kFs / 4);   // change state every 250 ms
        s.on = t % 5 != 4;
        s.mode = (int) (t / 2) % 2;
        s.outDb = (t % 3 == 0) ? 6.f : -6.f;      // automation jumps too
        c.process (l.data() + i, r.data() + i, block, s);
    }
    float worst = 0.f;
    for (size_t i = 1; i < l.size(); ++i) worst = std::max (worst, std::abs (l[i] - l[i - 1]));
    CHECK (worst < 0.03f);
}

TEST_CASE ("Compressor stays finite and bounded under extreme settings", "[dynamics][stress]")
{
    Compressor c;
    c.prepare (kFs);
    psy::dsp::Rng rng (5);
    std::vector<float> l (64), r (64);
    float peak = 0.f;
    for (int block = 0; block < 6000; ++block)
    {
        CompSettings s;
        s.on = block % 97 != 0;
        s.mode = (block / 50) % 2;
        s.attackMs = block % 2 ? 0.05f : 200.f;
        s.releaseMs = block % 3 ? 5.f : 2000.f;
        s.threshDb = -60.f;
        s.ratio = 20.f;
        s.kneeDb = (float) (block % 25);
        s.down = 1.f;
        s.up = 1.f;
        s.xLow = block % 7 ? 40.f : 600.f;
        s.xHigh = block % 5 ? 1000.f : 12000.f;
        s.downDb = { -60.f, 0.f, -30.f };
        s.upDb = { 0.f, -72.f, -10.f };
        s.gainDb = { 24.f, -24.f, 24.f };
        s.outDb = 24.f;
        const float amp = (block / 300) % 2 ? 1.f : 1e-4f;
        for (size_t i = 0; i < l.size(); ++i) { l[i] = amp * rng.bipolar(); r[i] = amp * rng.bipolar(); }
        c.process (l.data(), r.data(), (int) l.size(), s);
        for (size_t i = 0; i < l.size(); ++i)
        {
            REQUIRE (std::isfinite (l[i]));
            REQUIRE (std::isfinite (r[i]));
            peak = std::max (peak, std::max (std::abs (l[i]), std::abs (r[i])));
        }
    }
    // +24 up, +24 band, +24 out is the most any setting can add
    CHECK (peak < 2.f * dbToGain (72.f));
}
