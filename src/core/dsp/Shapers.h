#pragma once

// Waveshapers with first-order antiderivative anti-aliasing (ADAA), after
// Parker, Zavalishin & Le Bivic, "Reducing the Aliasing of Nonlinear
// Waveshaping Using Continuous-Time Convolution" (DAFx 2016).
//   y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
// with F the antiderivative of the shaping function; falls back to f of the
// midpoint when consecutive inputs are (nearly) equal. Computed in double to
// avoid cancellation.

#include "Common.h"

namespace psy::dsp
{

class AdaaShaper
{
public:
    enum Type { Soft, Hard, Fold, Tube, Rectify };

    void reset() noexcept { x1 = 0.0; }

    float process (float xIn, int type) noexcept
    {
        const double x = xIn;
        const double dx = x - x1;
        double y;
        if (std::abs (dx) < 1e-5)
            y = f (0.5 * (x + x1), type);
        else
            y = (F (x, type) - F (x1, type)) / dx;
        x1 = x;
        return (float) y;
    }

    static double f (double x, int type) noexcept
    {
        switch (type)
        {
            case Soft:    return std::tanh (x);
            case Hard:    return std::clamp (x, -1.0, 1.0);
            case Fold:    return std::sin (x);
            case Tube:    return std::tanh (x + kBias) - std::tanh (kBias);
            case Rectify: return std::abs (x) * 0.9 - 0.3;
            default:      return x;
        }
    }

    static double F (double x, int type) noexcept
    {
        switch (type)
        {
            case Soft:    return logCosh (x);
            case Hard:    return std::abs (x) <= 1.0 ? 0.5 * x * x : std::abs (x) - 0.5;
            case Fold:    return -std::cos (x);
            case Tube:    return logCosh (x + kBias) - std::tanh (kBias) * x;
            case Rectify: return 0.45 * x * std::abs (x) - 0.3 * x;
            default:      return 0.5 * x * x;
        }
    }

private:
    static constexpr double kBias = 0.35;
    double x1 = 0.0;
};

} // namespace psy::dsp
