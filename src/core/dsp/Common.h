#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

namespace psy::dsp
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.f * kPi;
constexpr double kPiD = 3.14159265358979323846;

inline float noteToHz (float note) noexcept { return 440.f * std::exp2 ((note - 69.f) * (1.f / 12.f)); }
inline float dbToGain (float db) noexcept { return std::pow (10.f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return 20.f * std::log10 (std::max (g, 1e-10f)); }  // floors at -200 dB
inline float clamp01 (float x) noexcept { return std::clamp (x, 0.f, 1.f); }

// True when two parameter values are not bit-identical: the exact comparison
// a "did it change" cache wants, without -Wfloat-equal (and NaN never sticks).
inline bool differs (float a, float b) noexcept { return std::bit_cast<uint32_t> (a) != std::bit_cast<uint32_t> (b); }
inline bool differs (double a, double b) noexcept { return std::bit_cast<uint64_t> (a) != std::bit_cast<uint64_t> (b); }

// 4-point, 3rd-order Hermite (Catmull-Rom) interpolation between x0 and x1,
// f in [0, 1); xm1 and x2 are the neighbours either side.
inline float hermite4 (float xm1, float x0, float x1, float x2, float f) noexcept
{
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

// 6-point, 5th-order Lagrange interpolation between y2 and y3 (the points at
// -2 .. 3, f in [0, 1) from y2): flatter and less imaging than the Hermite on
// signals with content up to a quarter of the sample rate.
inline float lagrange6 (float y0, float y1, float y2, float y3, float y4, float y5, float f) noexcept
{
    const float tm3 = f - 3.f, tm2 = f - 2.f, tm1 = f - 1.f, tp1 = f + 1.f, tp2 = f + 2.f;
    const float w0 = -tp1 * f * tm1 * tm2 * tm3 * (1.f / 120.f);
    const float w1 = tp2 * f * tm1 * tm2 * tm3 * (1.f / 24.f);
    const float w2 = -tp2 * tp1 * tm1 * tm2 * tm3 * (1.f / 12.f);
    const float w3 = tp2 * tp1 * f * tm2 * tm3 * (1.f / 12.f);
    const float w4 = -tp2 * tp1 * f * tm1 * tm3 * (1.f / 24.f);
    const float w5 = tp2 * tp1 * f * tm1 * tm2 * (1.f / 120.f);
    return w0 * y0 + w1 * y1 + w2 * y2 + w3 * y3 + w4 * y4 + w5 * y5;
}

// Solves a 4x4 tridiagonal system from its Thomas factorisation: inv[i] the
// reciprocal of the modified diagonal, cp[i] the modified upper diagonal,
// lower[i - 1] the coefficient of row i on row i - 1. y may not alias rhs.
inline void thomas4 (const std::array<float, 4>& inv, const std::array<float, 4>& lower, const std::array<float, 4>& cp,
                     const std::array<float, 4>& rhs, std::array<float, 4>& y) noexcept
{
    std::array<float, 4> dp;
    dp[0] = rhs[0] * inv[0];
    for (size_t i = 1; i < 4; ++i)
        dp[i] = (rhs[i] - lower[i - 1] * dp[i - 1]) * inv[i];
    y[3] = dp[3];
    for (int i = 2; i >= 0; --i)
        y[(size_t) i] = dp[(size_t) i] - cp[(size_t) i] * y[(size_t) i + 1];
}

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
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
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

// A block-rate value that moves to each new target in a fixed number of
// samples, however the calls split the audio: the same at any block size
// (Ramp spreads a change over its call, so a 1-sample call would step).
struct Glide
{
    float cur = 0.f, target = 0.f, step = 0.f;
    int left = 0;
    bool init = false;

    void set (float t, int samples) noexcept
    {
        if (! init)
        {
            cur = target = t;
            init = true;
            left = 0;
            return;
        }
        if (! differs (t, target)) return;   // still on the way there
        target = t;
        left = std::max (samples, 1);
        step = (target - cur) / (float) left;
    }
    void reset() noexcept { init = false; left = 0; }
    float next() noexcept
    {
        if (left > 0)
            cur = --left == 0 ? target : cur + step;
        return cur;
    }
};

// The on/off of an effect: the wet part fades over 10 ms, and an effect that
// has been fully off starts from clean state when it is switched on again (so
// no stale delay or reverb audio replays). An effect that is on when the
// sound starts (after a reset: a new patch, a bounce, a render) plays fully
// wet from the first sample, so a first note sounds like every other one; only
// a switch while playing fades. The pattern of StereoWidth and the
// Compressor, shared. An effect uses it as:
//     if (! sw.wake (on, [this] { reset(); })) return;
//     per sample: const float f = sw.tick (on);  ...  out = FxSwitch::blend (dry, wet, f);
//                 if (sw.done (on)) { sw.stop(); return; }   // the rest of the block passes
// The engine skips the effect while `isActive (on)` is false.
struct FxSwitch
{
    float fade = 0.f, step = 1.f / 480.f;
    bool running = false;
    bool fresh = true;   // nothing played since reset(), and never seen off

    void prepare (float sampleRate) noexcept { step = 1.f / (0.01f * sampleRate); }
    void reset() noexcept { fade = 0.f; running = false; fresh = true; }
    bool isActive (bool on) noexcept
    {
        if (! on) fresh = false;   // off while the sound plays: switching on later fades in
        return on || fade > 0.f;
    }
    // At the top of process: false while the effect stays off (pass the block
    // through); coming out of a full stop it clears the effect's state.
    template <typename Clear>
    bool wake (bool on, Clear&& clear) noexcept
    {
        if (running) return true;
        if (! on)
        {
            fresh = false;
            return false;
        }
        const bool wasFresh = fresh;
        clear();   // the effect's reset(), which also resets this switch
        running = true;
        fade = wasFresh ? 1.f : 0.f;
        fresh = false;
        return true;
    }
    void stop() noexcept { running = false; }
    float tick (bool on) noexcept
    {
        fade = on ? std::min (1.f, fade + step) : std::max (0.f, fade - step);
        return fade;
    }
    bool done (bool on) const noexcept { return ! on && fade <= 0.f; }
    // Fully on is the wet signal itself and fully off the dry one, bit for bit.
    static float blend (float dry, float wet, float f) noexcept
    {
        return f >= 1.f ? wet : (f <= 0.f ? dry : dry + f * (wet - dry));
    }
};

// A stereo delay that tops a varying latency up to a fixed one, so a host
// never sees the latency move (a VST3 host restarts the plugin when it
// does, and the keys held through that are lost). When the delay changes
// while a sound plays (an effect switched), it crossfades from the old tap
// to the new one over 10 ms; after a reset it starts on the new one. At a
// delay of 0 it passes the signal bit for bit.
struct LatencyPad
{
    void prepare (int maxDelay, float sampleRate)
    {
        size_t size = 1;
        while (size < (size_t) maxDelay + 1) size <<= 1;
        for (auto& b : buf) b.assign (size, 0.f);
        mask = size - 1;
        fadeLen = std::max (1, (int) std::lround (0.01f * sampleRate));
        reset();
    }
    void reset() noexcept
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.f);
        w = 0;
        from = cur = 0;
        fade = 0;
        snap = true;
    }
    int delay() const noexcept { return cur; }

    void process (float* l, float* r, int n, int wanted) noexcept
    {
        wanted = std::clamp (wanted, 0, (int) mask);
        if (snap)
        {
            cur = wanted;
            snap = false;
        }
        else if (wanted != cur && fade == 0)   // a change during a fade waits for it to end
        {
            from = cur;
            cur = wanted;
            fade = fadeLen;
        }
        for (int i = 0; i < n; ++i)
        {
            buf[0][w] = l[i];
            buf[1][w] = r[i];
            if (fade > 0)
            {
                const float t = 1.f - (float) --fade / (float) fadeLen;
                const float g = t * t * (3.f - 2.f * t);   // smoothstep
                const size_t a = (w - (size_t) from) & mask, b = (w - (size_t) cur) & mask;
                l[i] = buf[0][a] + g * (buf[0][b] - buf[0][a]);
                r[i] = buf[1][a] + g * (buf[1][b] - buf[1][a]);
            }
            else if (cur > 0)
            {
                const size_t b = (w - (size_t) cur) & mask;
                l[i] = buf[0][b];
                r[i] = buf[1][b];
            }
            w = (w + 1) & mask;
        }
    }

private:
    std::array<std::vector<float>, 2> buf;
    size_t mask = 0, w = 0;
    int from = 0, cur = 0, fade = 0, fadeLen = 480;
    bool snap = true;
};

} // namespace psy::dsp
