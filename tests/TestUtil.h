#pragma once

#include <cmath>
#include <complex>
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
