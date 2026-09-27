#pragma once

// Four-point polynomial BLEP / BLAMP residuals.
//
// The band-limited impulse is approximated by the cubic B-spline B3 (support of
// four samples). Integrating it once gives a smooth step I(t); the residual
// r(t) = I(t) - H(t) is what has to be added around a discontinuity. Integrating
// once more gives the ramp residual R(t) used for slope discontinuities
// (triangle corners, sync). Compared with the common two-point polyBLEP this
// rolls off ~24 dB/oct faster in the aliasing region, which together with 2x
// oversampling keeps aliasing well under the audible floor.
//
// Time t is measured in samples relative to the discontinuity; both residuals
// vanish for |t| >= 2.

#include <cmath>

namespace psy::dsp::blep
{

// Integral of the B-spline from -2 to t, for t <= 0.
inline double stepLeft (double t) noexcept
{
    if (t <= -2.0) return 0.0;
    if (t <= -1.0)
    {
        const double s = t + 2.0;
        return s * s * s * s / 24.0;
    }
    const double t2 = t * t;
    return 1.0 / 24.0 + (4.0 * t - 2.0 * t2 * t - 0.75 * t2 * t2 + 2.75) / 6.0;
}

// Double integral of the B-spline from -2 to t, for t <= 0.
inline double rampLeft (double t) noexcept
{
    if (t <= -2.0) return 0.0;
    if (t <= -1.0)
    {
        const double s = t + 2.0;
        return s * s * s * s * s / 120.0;
    }
    const double t2 = t * t;
    return 1.0 / 120.0 + (t + 1.0) / 24.0
         + (2.0 * t2 - 0.5 * t2 * t2 - 0.15 * t2 * t2 * t + 2.75 * t + 1.1) / 6.0;
}

// Step residual r(t) = I(t) - H(t): antisymmetric.
inline float step (double t) noexcept
{
    if (t >= 2.0 || t <= -2.0) return 0.f;
    return t < 0.0 ? (float) stepLeft (t) : (float) -stepLeft (-t);
}

// Ramp residual R(t) = J(t) - max(t, 0): symmetric.
inline float ramp (double t) noexcept
{
    if (t >= 2.0 || t <= -2.0) return 0.f;
    return (float) rampLeft (-std::abs (t));
}

// Correction accumulator for an oscillator whose output is delayed by two
// samples, so residuals can be written into the "past" (t = d-2, d-1) as well
// as the future (t = d, d+1).
struct Accumulator
{
    float c[4] {};

    // d: how many samples ago (fraction in [0,1)) the discontinuity happened,
    // measured from the sample being generated now.
    void addStep (double d, float height) noexcept
    {
        c[0] += height * step (d - 2.0);
        c[1] += height * step (d - 1.0);
        c[2] += height * step (d);
        c[3] += height * step (d + 1.0);
    }

    // slopeChange in units of output per sample.
    void addRamp (double d, float slopeChange) noexcept
    {
        c[0] += slopeChange * ramp (d - 2.0);
        c[1] += slopeChange * ramp (d - 1.0);
        c[2] += slopeChange * ramp (d);
        c[3] += slopeChange * ramp (d + 1.0);
    }

    float pop() noexcept
    {
        const float out = c[0];
        c[0] = c[1];
        c[1] = c[2];
        c[2] = c[3];
        c[3] = 0.f;
        return out;
    }

    void reset() noexcept { c[0] = c[1] = c[2] = c[3] = 0.f; }
};

// Two-sample delay for the naive waveform so it lines up with the corrections.
struct Delay2
{
    float z1 = 0.f, z2 = 0.f;
    float push (float x) noexcept
    {
        const float out = z2;
        z2 = z1;
        z1 = x;
        return out;
    }
    void reset() noexcept { z1 = z2 = 0.f; }
};

} // namespace psy::dsp::blep
