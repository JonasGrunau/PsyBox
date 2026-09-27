#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace psy::dsp
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.f * kPi;
constexpr double kPiD = 3.14159265358979323846;

inline float noteToHz (float note) noexcept { return 440.f * std::exp2 ((note - 69.f) * (1.f / 12.f)); }
inline float dbToGain (float db) noexcept { return std::pow (10.f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return 20.f * std::log10 (std::max (g, 1e-10f)); }  // floors at -200 dB
inline float clamp01 (float x) noexcept { return std::clamp (x, 0.f, 1.f); }

// Pade-style tanh, accurate to ~1e-4 in [-3, 3], saturates exactly outside.
inline float fastTanh (float x) noexcept
{
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

// Numerically safe log(cosh(x)), the antiderivative of tanh (used by ADAA).
inline double logCosh (double x) noexcept
{
    const double a = std::abs (x);
    return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994530942;
}

// PCG32: small, fast, and identical on every platform (unlike std distributions),
// so a generator seed always produces the same patch.
struct Rng
{
    uint64_t state = 0x853c49e6748fea9bULL;
    uint64_t inc = 0xda3e39cb94b95bdbULL;

    explicit Rng (uint64_t seed = 1) noexcept { reseed (seed); }

    void reseed (uint64_t seed) noexcept
    {
        state = 0u;
        inc = (seed << 1u) | 1u;
        next();
        state += 0x853c49e6748fea9bULL + seed * 0x9E3779B97F4A7C15ULL;
        next();
    }

    uint32_t next() noexcept
    {
        const uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        const auto xorshifted = (uint32_t) (((old >> 18u) ^ old) >> 27u);
        const auto rot = (uint32_t) (old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }

    float uniform() noexcept { return (float) (next() >> 8) * (1.f / 16777216.f); }  // [0, 1)
    float bipolar() noexcept { return uniform() * 2.f - 1.f; }
    float range (float a, float b) noexcept { return a + (b - a) * uniform(); }
    int integer (int lo, int hiInclusive) noexcept { return lo + (int) (uniform() * (float) (hiInclusive - lo + 1)); }
    bool chance (float p) noexcept { return uniform() < p; }

    float gaussian() noexcept  // Box-Muller, one sample
    {
        const float u1 = std::max (uniform(), 1e-7f);
        const float u2 = uniform();
        return std::sqrt (-2.f * std::log (u1)) * std::cos (kTwoPi * u2);
    }
};

// One-pole smoother with a time constant in samples.
struct OnePole
{
    float z = 0.f, a = 0.f;
    void setTime (float seconds, float sampleRate) noexcept
    {
        a = seconds <= 0.f ? 0.f : std::exp (-1.f / (seconds * sampleRate));
    }
    void reset (float v) noexcept { z = v; }
    float process (float target) noexcept { z = target + a * (z - target); return z; }
};

// DC blocker (leaky differentiator), ~5 Hz corner.
struct DcBlocker
{
    float x1 = 0.f, y1 = 0.f, r = 0.9995f;
    void prepare (float sampleRate) noexcept { r = 1.f - (kTwoPi * 5.f / sampleRate); }
    void reset() noexcept { x1 = y1 = 0.f; }
    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

inline float flushDenormal (float x) noexcept { return std::abs (x) < 1e-20f ? 0.f : x; }

// Linear per-sample ramp to a value that arrives once per block, so block-rate
// parameters never step (zipper noise). Snaps to the first value it gets.
struct Ramp
{
    float cur = 0.f, step = 0.f;
    bool init = false;

    void set (float target, int n) noexcept
    {
        if (! init) { cur = target; init = true; }
        step = (target - cur) / (float) std::max (n, 1);
    }
    void reset() noexcept { init = false; step = 0.f; }
    float next() noexcept { cur += step; return cur; }
};

} // namespace psy::dsp
