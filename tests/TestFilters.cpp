#include "TestUtil.h"
#include "dsp/Filters.h"

#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace psy::dsp;

namespace
{
    // Steady-state gain of a filter for a sine at `hz`.
    template <typename F>
    double gainAt (F&& process, double hz, double fs)
    {
        double sIn = 0.0, sOut = 0.0;
        const int n = (int) fs;
        for (int i = 0; i < n; ++i)
        {
            const float x = 0.1f * (float) std::sin (2.0 * std::numbers::pi * hz * i / fs);
            const float y = process (x);
            if (i > n / 2) { sIn += (double) x * x; sOut += (double) y * y; }
        }
        return 20.0 * std::log10 (std::sqrt (sOut / sIn));
    }
} // namespace

TEST_CASE ("TPT SVF low-pass matches the analogue Butterworth response", "[filter]")
{
    const double fs = 96000.0, fc = 1000.0;
    for (double f : { 100.0, 1000.0, 4000.0, 20000.0 })
    {
        Svf svf;
        svf.setG (prewarp ((float) fc, (float) fs), 1.41421356f);
        const double g = gainAt ([&] (float x) { return svf.process (x).lp; }, f, fs);
        // bilinear transform: exact at dc and fc, compressed near nyquist
        const double w = std::tan (std::numbers::pi * f / fs) / std::tan (std::numbers::pi * fc / fs);
        const double expected = -10.0 * std::log10 (1.0 + w * w * w * w);
        INFO (f << " Hz: " << g << " dB vs " << expected);
        CHECK_THAT (g, Catch::Matchers::WithinAbs (expected, 0.1));
    }
}

TEST_CASE ("Ladder self-oscillates at its cutoff and stays bounded", "[filter]")
{
    const float fs = 96000.f, fc = 1000.f;
    Ladder l;
    l.set (prewarp (fc, fs), 1.f);
    std::vector<float> out (1 << 16);
    float x = 0.1f;
    for (auto& y : out) { y = l.process (x); x = 0.f; }
    std::vector<float> tail (out.begin() + (1 << 15), out.end());
    CHECK (testutil::rms (tail) > 0.05);
    for (float v : out) REQUIRE (std::abs (v) < 2.f);
    const double f = testutil::peakFrequency (tail, fs);
    INFO ("oscillates at " << f << " Hz");
    CHECK_THAT (f, Catch::Matchers::WithinRel (1000.0, 0.03));
}

TEST_CASE ("Ladder passes DC with light resonance", "[filter]")
{
    Ladder l;
    l.set (prewarp (2000.f, 96000.f), 0.f);
    float y = 0.f;
    for (int i = 0; i < 20000; ++i) y = l.process (0.2f);
    CHECK_THAT (y, Catch::Matchers::WithinAbs (std::tanh (0.2), 0.01));
}

TEST_CASE ("Diode ladder calibration puts the resonant peak on the cutoff", "[filter]")
{
    const auto& cal = DiodeLadder::calibration();
    INFO ("peak ratio " << cal.peakRatio << ", kMax " << cal.kMax);
    CHECK (cal.peakRatio > 0.1f);
    CHECK (cal.kMax > 1.f);

    const float fs = 96000.f, fc = 800.f;
    DiodeLadder d;
    d.set (fc, fs, 1.f);
    std::vector<float> out (1 << 16);
    float x = 0.2f;
    for (auto& y : out) { y = d.process (x); x = 0.f; }
    std::vector<float> tail (out.begin() + (1 << 15), out.end());
    for (float v : out) REQUIRE (std::isfinite (v));
    // sustained, not decaying: the last quarter is as loud as the third
    std::vector<float> q3 (out.begin() + (1 << 15), out.begin() + (3 << 14)), q4 (out.begin() + (3 << 14), out.end());
    CHECK (testutil::rms (q4) > 0.005);
    CHECK (testutil::rms (q4) > 0.8 * testutil::rms (q3));
    const double f = testutil::peakFrequency (tail, fs);
    INFO ("self-oscillation at " << f << " Hz");
    CHECK_THAT (f, Catch::Matchers::WithinRel ((double) fc, 0.05));
}

TEST_CASE ("All filter types survive brutal modulation without blowing up", "[filter][stress]")
{
    const float fs = 96000.f;
    Rng rng (42);
    for (int type = 1; type < (int) psy::FilterType::Count; ++type)
    {
        FilterSlot f;
        f.prepare (fs * 2.f);
        f.setRate (fs);
        f.setType (type);
        float peak = 0.f;
        for (int i = 0; i < 200000; ++i)
        {
            if (i % 2 == 0)
            {
                const float cutoff = 20.f * std::pow (1000.f, rng.uniform());
                f.update (cutoff, rng.uniform(), rng.uniform(), rng.uniform());
            }
            const float y = f.process (rng.bipolar() * 2.f);
            REQUIRE (std::isfinite (y));
            peak = std::max (peak, std::abs (y));
        }
        INFO ("type " << type << " peak " << peak);
        CHECK (peak < 50.f);
    }
}

TEST_CASE ("Formant filter emphasises the vowel's first formant", "[filter]")
{
    const float fs = 96000.f;
    Formant fo;
    fo.set (0.f, 1.f, 0.5f, fs);  // "a", F1 = 800 Hz
    const double atF1 = gainAt ([&] (float x) { return fo.process (x); }, 800.0, fs);
    fo.reset();
    const double between = gainAt ([&] (float x) { return fo.process (x); }, 5000.0, fs);
    CHECK (atF1 > between + 12.0);
}

TEST_CASE ("LP and HP slopes fall at 6, 12, 18 and 24 dB per octave", "[filter]")
{
    using T = psy::FilterType;
    const float fs = 96000.f, fc = 500.f;
    const auto measure = [&] (T type, double hz) {
        FilterSlot f;
        f.prepare (fs);
        f.setType ((int) type);
        f.update (fc, 0.f, 0.f, 0.f);
        return gainAt ([&] (float x) { return f.process (x); }, hz, fs);
    };
    const T lps[4] = { T::LP6, T::LP12, T::LP18, T::LP24 };
    const T hps[4] = { T::HP6, T::HP12, T::HP18, T::HP24 };
    for (int order = 1; order <= 4; ++order)
    {
        // three octaves from the corner: -6 dB * order * 3, less a little near the knee
        const double expected = -10.0 * order * std::log10 (1.0 + 64.0);   // exact for Butterworth/Q=0.5 at 8x
        const double lp = measure (lps[order - 1], fc * 8.0);
        const double hp = measure (hps[order - 1], fc / 8.0);
        INFO ("order " << order << " lp " << lp << " hp " << hp << " expected " << expected);
        CHECK (std::abs (lp - expected) < 3.0);
        CHECK (std::abs (hp - expected) < 3.0);
    }
    // Butterworth sections: -3 dB at the cutoff for 18 and 24
    for (T t : { T::LP18, T::LP24, T::HP18, T::HP24 })
    {
        const double g = measure (t, fc);
        INFO ("type " << (int) t << " gain at fc " << g);
        CHECK (std::abs (g + 3.01) < 0.5);
    }
}

TEST_CASE ("Ladder HP and BP modes keep their shape", "[filter]")
{
    using T = psy::FilterType;
    const float fs = 96000.f, fc = 1000.f;
    const auto measure = [&] (T type, double hz) {
        FilterSlot f;
        f.prepare (fs);
        f.setType ((int) type);
        f.update (fc, 0.3f, 0.f, 0.f);
        return gainAt ([&] (float x) { return f.process (x); }, hz, fs);
    };
    CHECK (measure (T::LadderHP, 8000.0) > -3.0);
    CHECK (measure (T::LadderHP, 125.0) < -50.0);
    const double peak = measure (T::LadderBP, fc);
    CHECK (peak > measure (T::LadderBP, 125.0) + 20.0);
    CHECK (peak > measure (T::LadderBP, 8000.0) + 20.0);
    CHECK (measure (T::LadderLP12, 8000.0) < -25.0);
}

TEST_CASE ("The diode ladder's calibration constants are what the scan finds", "[filter]")
{
    // They are hard-coded so the 20,001-point scan never runs on the audio thread
    const auto scanned = DiodeLadder::computeCalibration();
    const auto& cal = DiodeLadder::calibration();
    CHECK_THAT ((double) cal.peakRatio, Catch::Matchers::WithinRel ((double) scanned.peakRatio, 1e-6));
    CHECK_THAT ((double) cal.kMax, Catch::Matchers::WithinRel ((double) scanned.kMax, 1e-6));
}

TEST_CASE ("Filter drive fades in from zero without a jump", "[filter]")
{
    // drive < 0.0082 was bypassed and above it tanh (x g) / sqrt (g) switched in
    // whole: a full-scale input stepped from 1.0 to 0.76 as a modulated
    // drive crossed it.
    const float fs = 96000.f;
    for (int type : { (int) psy::FilterType::LP12, (int) psy::FilterType::HP24, (int) psy::FilterType::Formant,
                      (int) psy::FilterType::CombPos, (int) psy::FilterType::LadderLP12, (int) psy::FilterType::Diode303 })
    {
        const auto run = [&] (float drive) {
            FilterSlot f;
            f.prepare (fs);
            f.setRate (fs);
            f.setType (type);
            float y = 0.f;
            for (int i = 0; i < 700; ++i)
            {
                f.update (6000.f, 0.3f, drive, 0.4f);
                y = f.process (0.9f * (float) std::sin (2.0 * std::numbers::pi * 1000.0 * i / fs));
            }
            return y;   // after the filter has settled
        };
        float prev = run (0.f), biggest = 0.f;
        for (float drive = 0.00005f; drive < 0.02f; drive += 0.00005f)
        {
            const float y = run (drive);
            biggest = std::max (biggest, std::abs (y - prev));
            prev = y;
        }
        INFO ("type " << type << ": biggest change for a drive step of 0.00005: " << biggest);
        CHECK (biggest < 0.02f);
    }
}

TEST_CASE ("Filter drive 0 passes the input to the filter untouched", "[filter]")
{
    const float fs = 96000.f;
    FilterSlot f;
    f.prepare (fs);
    f.setRate (fs);
    f.setType ((int) psy::FilterType::LP12);
    Svf ref;
    ref.setG (prewarp (5000.f, fs), 2.f);
    for (int i = 0; i < 2000; ++i)
    {
        f.update (5000.f, 0.f, 0.f, 0.f);
        const float x = 0.9f * (float) std::sin (2.0 * std::numbers::pi * 700.0 * i / fs);
        REQUIRE (f.process (x) == ref.process (x).lp);
    }
}

TEST_CASE ("Switching a filter's type under a sustained note crossfades instead of clicking", "[filter]")
{
    // The state used to be zeroed at the switch: the output dropped to the
    // new filter's rest.
    const float fs = 96000.f;
    struct Pair { psy::FilterType from, to; };
    for (const auto& p : { Pair { psy::FilterType::LP24, psy::FilterType::LP12 }, Pair { psy::FilterType::LP12, psy::FilterType::Ladder24 },
                           Pair { psy::FilterType::LP18, psy::FilterType::Diode303 } })
    {
        FilterSlot f;
        f.prepare (fs);
        f.setRate (fs);
        f.setType ((int) p.from);
        const int switchAt = 20000;
        float prev = 0.f, steady = 0.f, biggest = 0.f;
        for (int i = 0; i < switchAt + 2000; ++i)
        {
            if (i == switchAt) f.setType ((int) p.to);
            f.update (3000.f, 0.2f, 0.f, 0.f);
            const float y = f.process (0.5f * (float) std::sin (2.0 * std::numbers::pi * 220.0 * i / fs));
            if (i > 2000 && i < switchAt) steady = std::max (steady, std::abs (y - prev));
            if (i >= switchAt - 1) biggest = std::max (biggest, std::abs (y - prev));
            prev = y;
        }
        INFO ("type " << (int) p.from << " -> " << (int) p.to << ": biggest step " << biggest << ", steady " << steady);
        CHECK (biggest < 3.f * steady);
        CHECK (f.getType() == (int) p.to);
    }
}

TEST_CASE ("The comb's loop damping keeps its corner in Hz at any rate", "[filter]")
{
    // A fixed coefficient of 0.6 put the corner at 7.5, 15 and 30 kHz at 1x, 2x, 4x of 48 kHz.
    for (float rate : { 44100.f, 48000.f, 96000.f, 192000.f })
    {
        Comb c;
        c.prepare (rate);
        c.set (440.f, 0.8f, rate);
        const double corner = -std::log (1.0 - (double) c.dampCoef) * (double) rate / (2.0 * std::numbers::pi);
        INFO ("rate " << rate << ": corner " << corner << " Hz");
        CHECK_THAT (corner, Catch::Matchers::WithinRel (14000.0, 0.01));
    }
    Comb c;
    c.prepare (96000.f);
    c.set (440.f, 0.8f, 96000.f);
    CHECK_THAT (c.dampCoef, Catch::Matchers::WithinAbs (0.6, 1e-3));   // 2x of 48 kHz: as fitted
}

TEST_CASE ("The diode ladder gives the same output with its input response cached", "[filter]")
{
    // The response of the input node, M^-1 (g e1), depends on the cutoff alone and is
    // cached by set (): its first stage is g / (1 + 2 g - ...), checked against the
    // direct solve of the tridiagonal system.
    DiodeLadder d;
    d.set (900.f, 96000.f, 0.5f);
    std::array<float, 4> ye {};
    thomas4 (d.inv, d.lower, d.cp, { d.g, 0.f, 0.f, 0.f }, ye);
    for (size_t i = 0; i < 4; ++i)
        CHECK (d.ye[i] == ye[i]);
    // M ye = g e1
    const float diag = 1.f + 2.f * d.g;
    CHECK_THAT (diag * d.ye[0] - d.g * d.ye[1], Catch::Matchers::WithinAbs ((double) d.g, 1e-6));
}
