#pragma once

// The TB-303 parts of the Phoscyon engine. Built from measurements of how a
// 303 behaves, not from any circuit listing: the saw and the 303 square, and
// the diode ladder with the 303's top capacitor and its high-passed
// resonance feedback.

#include "Blep.h"
#include "Common.h"
#include "Filters.h"

#include <array>
#include <cmath>

namespace psy::dsp
{

// ---------------------------------------------------------------------------
// 303 oscillator. The saw is a rising ramp. The square is made from it the way
// the 303's transistor shaper does: a flat top for `duty` of the cycle, and a
// bottom that sags back towards zero like a coupling capacitor (`sagPerSample`
// is the per-sample decay of the bottom's distance from zero, `low` its depth
// right after the fall). Steps get BLEP corrections and the sag's slope
// changes BLAMP corrections. Output is delayed by two samples.
// ---------------------------------------------------------------------------
class PhosOsc
{
public:
    void reset (double phase = 0.0) noexcept
    {
        p = phase;
        sag = 1.0;
        high = true;
        acc.reset();
        delay.reset();
    }

    double phase() const noexcept { return p; }

    // inc: cycles per sample. Returns the saw (square == false) or the square.
    float process (double inc, bool square, double duty, double sagPerSample, float top, float low) noexcept
    {
        inc = std::clamp (inc, 0.0, 0.45);
        duty = std::clamp (duty, 0.05, 0.95);
        const double end = p + inc;
        const double tau = -1.0 / std::log (std::clamp (sagPerSample, 1e-9, 0.999999999));   // samples

        if (! square)
        {
            if (end >= 1.0)
                acc.addStep ((end - 1.0) / inc, -2.f);
            p = end - std::floor (end);
            return delay.push ((float) (2.0 * p - 1.0)) + acc.pop();
        }

        // Square: rises at phase 0, falls at `duty`.
        double sagNow = sag * sagPerSample;
        const auto since = [&] (double u) { return (end - u) / inc; };
        if (high && end >= duty && p < duty)
        {
            const double d = since (duty);   // samples since the fall
            acc.addStep (d, -(low + top));
            acc.addRamp (d, (float) (low / tau));
            high = false;
            sagNow = std::pow (sagPerSample, d);
        }
        if (end >= 1.0)
        {
            const double d = since (1.0);
            const double before = sagNow / std::pow (sagPerSample, d);   // the bottom's sag when it rose
            acc.addStep (d, (float) (top + low * before));
            acc.addRamp (d, (float) (-low * before / tau));
            high = true;
            if (duty < end - 1.0)   // a very short top falls again within the sample
            {
                const double d2 = (end - 1.0 - duty) / inc;
                acc.addStep (d2, -(low + top));
                acc.addRamp (d2, (float) (low / tau));
                high = false;
                sagNow = std::pow (sagPerSample, d2);
            }
        }
        sag = high ? 1.0 : sagNow;
        p = end - std::floor (end);
        const float naive = high ? top : (float) (-low * sag);
        return delay.push (naive) + acc.pop();
    }

private:
    double p = 0.0, sag = 1.0;
    bool high = true;
    blep::Accumulator acc;
    blep::Delay2 delay;
};

// ---------------------------------------------------------------------------
// The 303's diode ladder. Four stages that load each other; the top one's
// capacitor is `m` times the others':
//   y1' = w (u  - 2 y1 + y2)
//   y2' = w (y1 - 2 y2 + y3)
//   y3' = w (y2 - 2 y3 + y4)
//   y4' = (w / m) (y3 - y4)
// which gives the open loop 1 / (m p^4 + (6m+1) p^3 + (10m+5) p^2 + (4m+6) p + 1).
// The resonance feeds y4 back through a one-pole high-pass, so the bass
// isn't eaten the way a plain ladder's is, and the input node saturates:
//   u = tanh (drive (x - k hp(y4))) / drive
// Trapezoidal (TPT) like the other ladders, the tridiagonal system solved with
// the Thomas algorithm and the saturating node with Newton.
// ---------------------------------------------------------------------------
struct PhosLadder
{
    std::array<float, 4> s {};
    float hpS = 0.f;                  // feedback high-pass state
    float g = 0.f, k = 0.f, drive = 1.f, hpG = 0.f;
    std::array<float, 4> cp {}, inv {}, lower {};

    void reset() noexcept
    {
        s.fill (0.f);
        hpS = 0.f;
    }

    // g: prewarped tan (pi fc / fs); hpG: tan (pi fhp / fs) of the feedback high-pass.
    void set (float gIn, float kIn, float m, float hpGIn, float driveIn) noexcept
    {
        g = gIn;
        k = kIn;
        drive = std::max (driveIn, 1e-3f);
        hpG = hpGIn / (1.f + hpGIn);
        // M = I - g A, A = [-2 1 . .; 1 -2 1 .; . 1 -2 1; . . 1/m -1/m]
        const float gm = g / m;
        const float diag[4] = { 1.f + 2.f * g, 1.f + 2.f * g, 1.f + 2.f * g, 1.f + gm };
        const float up = -g;
        lower = { -g, -g, -gm, 0.f };
        float d = diag[0];
        inv[0] = 1.f / d;
        cp[0] = up * inv[0];
        for (size_t i = 1; i < 4; ++i)
        {
            d = diag[i] - lower[i - 1] * cp[i - 1];
            inv[i] = 1.f / d;
            cp[i] = (i < 3 ? up : 0.f) * inv[i];
        }
    }

    float process (float x) noexcept
    {
        std::array<float, 4> ys, ye;
        solve (s, ys);
        solve ({ g, 0.f, 0.f, 0.f }, ye);
        // y4 = a u + b; the high-pass of it: hp = (1 - G) (y4 - hpS) = a' u + b'
        const float hg = 1.f - hpG;
        const float a = hg * ye[3], b = hg * (ys[3] - hpS);

        float u = (x - k * b) / (1.f + k * a);
        const float dr = drive, idr = 1.f / dr;
        for (int it = 0; it < 3; ++it)
        {
            const float t = std::tanh (dr * (x - k * (a * u + b)));
            const float f = u - t * idr;
            const float df = 1.f + k * a * (1.f - t * t);
            u -= f / df;
        }

        float y4 = 0.f;
        for (size_t i = 0; i < 4; ++i)
        {
            const float y = ys[i] + u * ye[i];
            s[i] = 2.f * y - s[i];
            if (i == 3) y4 = y;
        }
        // advance the feedback high-pass (its TPT one-pole low-pass state)
        const float v = (y4 - hpS) * hpG;
        hpS = v + hpS + v;
        return y4;
    }

private:
    void solve (const std::array<float, 4>& rhs, std::array<float, 4>& y) const noexcept
    {
        std::array<float, 4> dp;
        dp[0] = rhs[0] * inv[0];
        for (size_t i = 1; i < 4; ++i)
            dp[i] = (rhs[i] - lower[i - 1] * dp[i - 1]) * inv[i];
        y[3] = dp[3];
        for (int i = 2; i >= 0; --i)
            y[(size_t) i] = dp[(size_t) i] - cp[(size_t) i] * y[(size_t) i + 1];
    }
};

// TPT state-variable high-pass (12 dB/oct) with a Q, for the 303's input coupling.
struct HighPass2
{
    Svf svf;
    void reset() noexcept { svf.reset(); }
    void set (float hz, float q, float sampleRate) noexcept { svf.setG (prewarp (hz, sampleRate), 1.f / std::max (q, 0.05f)); }
    float process (float x) noexcept { return svf.process (x).hp; }
};

} // namespace psy::dsp
