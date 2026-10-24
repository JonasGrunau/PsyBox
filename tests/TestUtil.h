#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <numbers>
#include <vector>

namespace testutil
{

inline void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * std::numbers::pi / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Magnitude spectrum in dB (Blackman-Harris window), size n/2.
inline std::vector<double> spectrumDb (const std::vector<float>& x, size_t n)
{
    std::vector<std::complex<double>> a (n);
    double wsum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = 2.0 * std::numbers::pi * (double) i / (double) (n - 1);
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        a[i] = x[i] * w;
        wsum += w;
    }
    fft (a);
    std::vector<double> db (n / 2);
    for (size_t i = 0; i < n / 2; ++i)
        db[i] = 20.0 * std::log10 (std::abs (a[i]) * 2.0 / wsum + 1e-15);
    return db;
}

// Worst non-harmonic component (dB relative to the strongest bin) below maxHz,
// ignoring +-guard bins around every harmonic of f0.
inline double worstAliasDb (const std::vector<float>& x, double fs, double f0, double maxHz, size_t n = 1 << 15, int guard = 6)
{
    const auto db = spectrumDb (x, n);
    double peak = -1e9;
    for (double v : db) peak = std::max (peak, v);
    const double binHz = fs / (double) n;
    double worst = -1e9;
    for (size_t i = 4; i < db.size(); ++i)
    {
        const double f = (double) i * binHz;
        if (f > maxHz) break;
        const double h = std::round (f / f0);
        if (h >= 1 && std::abs (f - h * f0) <= guard * binHz) continue;
        worst = std::max (worst, db[i]);
    }
    return worst - peak;
}

// What share of the power in [lo, hi) Hz lies off the harmonics of f0, in
// dB: what a distortion folded back (the last 2^16 samples, 4-term
// Blackman-Harris, 4 bins either side of a harmonic count as on it).
inline double inharmonicDb (const std::vector<float>& x, double fs, double f0, double lo, double hi)
{
    const size_t n = 1 << 16;
    const std::vector<float> tail (x.end() - (long) n, x.end());
    const auto db = spectrumDb (tail, n);
    const double bin = fs / (double) n;
    double off = 0.0, all = 0.0;
    for (size_t i = 1; i < db.size(); ++i)
    {
        const double f = (double) i * bin;
        if (f < lo || f >= hi) continue;
        const double p = std::pow (10.0, db[i] / 10.0);
        all += p;
        if (std::abs (f - std::round (f / f0) * f0) > 4.0 * bin) off += p;
    }
    return 10.0 * std::log10 (off / all + 1e-30);
}

inline double rms (const std::vector<float>& x, size_t from = 0)
{
    double s = 0.0;
    for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) std::max<size_t> (1, x.size() - from));
}

// The 6 kHz+ band (4th-order Butterworth high-pass, run from the first
// sample) in 0.25 ms frames of RMS: what a click lights up.
inline std::vector<double> highBandFrames (const std::vector<float>& x, double fs)
{
    const auto frame = (size_t) std::max (1.0, std::round (0.00025 * fs));
    std::vector<float> hp (x.size());
    for (size_t i = 0; i < x.size(); ++i) hp[i] = x[i];
    for (const double q : { 0.5412, 1.3066 })
    {
        const double w = 2.0 * std::numbers::pi * 6000.0 / fs, al = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + al;
        const double b0 = (1.0 + c) / 2.0 / a0, b1 = -(1.0 + c) / a0, a1 = -2.0 * c / a0, a2 = (1.0 - al) / a0;
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
        for (auto& v : hp)
        {
            const double y = b0 * v + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = v; y2 = y1; y1 = y;
            v = (float) y;
        }
    }
    std::vector<double> out (x.size() / frame);
    for (size_t k = 0; k < out.size(); ++k)
    {
        double s = 0.0;
        for (size_t i = k * frame; i < (k + 1) * frame; ++i) s += (double) hp[i] * hp[i];
        out[k] = std::sqrt (s / (double) frame);
    }
    return out;
}

// Clicks in [t0, t1) seconds: a 0.25 ms frame of the 6 kHz+ band that
// stands p times (in RMS) over the loudest frame within 30 ms either side
// of it, the 1 ms around it left out (a click is shorter than that). p >= 10
// is a strong click; frames under `floor` (RMS) never count. Each run of
// frames over `minP` gives one hit, at its worst frame.
struct Click { double t = 0.0, p = 0.0, rms = 0.0; };
inline std::vector<Click> findClicks (const std::vector<float>& x, double fs, double t0, double t1, double floor = 1e-5, double minP = 3.0)
{
    const auto band = highBandFrames (x, fs);
    const double frameSec = std::max (1.0, std::round (0.00025 * fs)) / fs;
    const long n = (long) band.size(), near = (long) std::lround (0.001 / frameSec), far = (long) std::lround (0.03 / frameSec);
    std::vector<Click> hits;
    bool inRun = false;
    for (long k = std::max (0L, (long) (t0 / frameSec)); k < std::min (n, (long) (t1 / frameSec)); ++k)
    {
        double ref = 0.0;
        for (long j = std::max (0L, k - far); j <= std::min (n - 1, k + far); ++j)
            if (std::abs (j - k) > near) ref = std::max (ref, band[(size_t) j]);
        const double p = band[(size_t) k] / (ref + 1e-30);
        if (band[(size_t) k] < floor || p < minP) { inRun = false; continue; }
        if (! inRun) hits.push_back ({ (double) k * frameSec, p, band[(size_t) k] });
        else if (p > hits.back().p) hits.back() = { (double) k * frameSec, p, band[(size_t) k] };
        inRun = true;
    }
    return hits;
}

inline double worstClick (const std::vector<Click>& hits)
{
    double w = 0.0;
    for (const auto& h : hits) w = std::max (w, h.p);
    return w;
}

// Frequency of the strongest spectral bin with parabolic interpolation.
inline double peakFrequency (const std::vector<float>& x, double fs, size_t n = 1 << 15)
{
    const auto db = spectrumDb (x, n);
    size_t best = 1;
    for (size_t i = 2; i + 1 < db.size(); ++i)
        if (db[i] > db[best]) best = i;
    const double a = db[best - 1], b = db[best], c = db[best + 1];
    const double p = 0.5 * (a - c) / (a - 2 * b + c);
    return ((double) best + p) * fs / (double) n;
}

} // namespace testutil
