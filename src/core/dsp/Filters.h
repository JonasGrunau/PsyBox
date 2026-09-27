#pragma once

// Zero-delay-feedback (TPT) filters after V. Zavalishin, "The Art of VA Filter
// Design" (rev. 2.1) and A. Simper's SVF notes. The two ladders put their
// saturator inside the feedback loop and solve the implicit equation with
// Newton-Raphson every sample, instead of the one-sample-delay approximations
// that detune the cutoff and make resonance depend on sample rate.

#include "Common.h"

#include <array>
#include <complex>
#include <vector>

namespace psy::dsp
{

// ---------------------------------------------------------------------------
// State-variable filter, trapezoidal integration (Simper form).
// ---------------------------------------------------------------------------
struct Svf
{
    float ic1 = 0.f, ic2 = 0.f;
    float a1 = 0.f, a2 = 0.f, a3 = 0.f, k = 2.f;

    void reset() noexcept { ic1 = ic2 = 0.f; }

    void setG (float g, float kDamping) noexcept
    {
        k = kDamping;
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    struct Out { float lp, bp, hp; };

    Out process (float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        return { v2, v1, v0 - k * v1 - v2 };
    }
};

inline float prewarp (float hz, float sampleRate) noexcept
{
    const float w = std::clamp (hz / sampleRate, 1e-5f, 0.49f);
    return std::tan (kPi * w);
}

// ---------------------------------------------------------------------------
// Four-pole transistor ladder. Cascade of TPT one-poles; the feedback node
// u = tanh(x - k*y4) is solved implicitly (y4 is affine in u).
// ---------------------------------------------------------------------------
struct Ladder
{
    std::array<float, 4> s {};
    float G = 0.f, k = 0.f;

    void reset() noexcept { s.fill (0.f); }

    void set (float g, float reso) noexcept
    {
        G = g / (1.f + g);
        k = 4.2f * reso;  // self-oscillation starts at k = 4
    }

    float process (float x) noexcept
    {
        const float beta = 1.f - G;
        const float G2 = G * G;
        const float a = G2 * G2;
        const float b = beta * (G2 * G * s[0] + G2 * s[1] + G * s[2] + s[3]);

        // Solve u = tanh(x - k (a u + b)) by Newton, starting from the linear guess.
        float u = (x - k * b) / (1.f + k * a);
        for (int it = 0; it < 2; ++it)
        {
            const float t = std::tanh (x - k * (a * u + b));
            const float f = u - t;
            const float df = 1.f + k * a * (1.f - t * t);
            u -= f / df;
        }

        float in = u;
        for (int i = 0; i < 4; ++i)
        {
            const float v = (in - s[(size_t) i]) * G;
            const float y = v + s[(size_t) i];
            s[(size_t) i] = y + v;
            in = y;
        }
        return in;
    }
};

// ---------------------------------------------------------------------------
// Diode ladder (TB-303 family). Unlike the transistor ladder the stages load
// each other, which spreads the poles, softens the resonance and gives the
// characteristic acid squelch. Linear core:
//   y1' = w (u  - 2 y1 + y2)
//   y2' = w (y1 - 2 y2 + y3)
//   y3' = w (y2 - 2 y3 + y4)
//   y4' = w (2 y3 - 2 y4)        (last capacitor is half size)
// discretised by TPT, the tridiagonal system solved with the Thomas algorithm,
// and the saturating feedback node solved by Newton like the Ladder.
// Resonance and cutoff are calibrated from the analogue prototype so that
// reso = 1 is the edge of self-oscillation and the peak sits at the cutoff.
// ---------------------------------------------------------------------------
struct DiodeLadder
{
    std::array<float, 4> s {};
    float g = 0.f, k = 0.f;
    // Thomas factorisation of M = I - g A (depends only on g)
    std::array<float, 4> cp {}, inv {};

    struct Calibration { float peakRatio; float kMax; };

    static const Calibration& calibration()
    {
        static const Calibration c = [] {
            using C = std::complex<double>;
            const auto openLoop = [] (double w) {
                // Solve (jw I - A) y = e1, return y4.
                const C jw (0.0, w);
                // Rows: [d_i on diagonal, lower l_i, upper u_i]
                C d[4] = { jw + 2.0, jw + 2.0, jw + 2.0, jw + 2.0 };
                const C up[3] = { -1.0, -1.0, -1.0 };
                const C lo[3] = { -1.0, -1.0, -2.0 };  // row i+1, col i
                C rhs[4] = { 1.0, 0.0, 0.0, 0.0 };
                for (int i = 1; i < 4; ++i)
                {
                    const C m = lo[i - 1] / d[i - 1];
                    d[i] -= m * up[i - 1];
                    rhs[i] -= m * rhs[i - 1];
                }
                C y[4];
                y[3] = rhs[3] / d[3];
                for (int i = 2; i >= 0; --i)
                    y[i] = (rhs[i] - up[i] * y[i + 1]) / d[i];
                return y[3];
            };
            // Scan for the -180 degree crossing (imag = 0, real < 0).
            double best = 1.0, bestErr = 1e9;
            for (int i = 0; i <= 20000; ++i)
            {
                const double w = 0.05 * std::pow (1000.0, i / 20000.0);
                const C h = openLoop (w);
                const double err = 3.14159265358979 - std::abs (std::arg (h));
                if (h.real() < 0.0 && err < bestErr)
                {
                    bestErr = err;
                    best = w;
                }
            }
            const C h = openLoop (best);
            return Calibration { (float) best, (float) (-1.0 / h.real()) };
        }();
        return c;
    }

    void reset() noexcept { s.fill (0.f); }

    void set (float cutoffHz, float sampleRate, float reso) noexcept
    {
        const auto& cal = calibration();
        g = prewarp (cutoffHz / cal.peakRatio, sampleRate);
        k = cal.kMax * 1.04f * reso;

        // M = I - g A:
        //   diag: 1 + 2g ; upper: -g ; lower: -g, -g, -2g ; last diag: 1 + 2g
        const float diag = 1.f + 2.f * g;
        const float upper = -g;
        const float lower[3] = { -g, -g, -2.f * g };
        // Forward sweep of Thomas; inv[i] = 1 / modified diagonal, cp[i] = modified upper / diag
        float dPrev = diag;
        inv[0] = 1.f / dPrev;
        cp[0] = upper * inv[0];
        for (int i = 1; i < 4; ++i)
        {
            const float d = diag - lower[i - 1] * cp[(size_t) i - 1];
            inv[(size_t) i] = 1.f / d;
            cp[(size_t) i] = (i < 3 ? upper : 0.f) * inv[(size_t) i];
        }
        lowerCache[0] = lower[0];
        lowerCache[1] = lower[1];
        lowerCache[2] = lower[2];
    }

    float process (float x) noexcept
    {
        // y = M^-1 s + u * M^-1 (g e1)
        std::array<float, 4> ys, ye;
        solve (s, ys);
        solve ({ g, 0.f, 0.f, 0.f }, ye);

        const float a = ye[3], b = ys[3];
        float u = (x - k * b) / (1.f + k * a);
        for (int it = 0; it < 2; ++it)
        {
            const float t = std::tanh (x - k * (a * u + b));
            u -= (u - t) / (1.f + k * a * (1.f - t * t));
        }

        for (size_t i = 0; i < 4; ++i)
        {
            const float y = ys[i] + u * ye[i];
            s[i] = 2.f * y - s[i];
        }
        return (ys[3] + u * ye[3]);
    }

private:
    float lowerCache[3] {};

    void solve (const std::array<float, 4>& rhs, std::array<float, 4>& y) const noexcept
    {
        std::array<float, 4> dp;
        dp[0] = rhs[0] * inv[0];
        for (size_t i = 1; i < 4; ++i)
            dp[i] = (rhs[i] - lowerCache[i - 1] * dp[i - 1]) * inv[i];
        y[3] = dp[3];
        for (int i = 2; i >= 0; --i)
            y[(size_t) i] = dp[(size_t) i] - cp[(size_t) i] * y[(size_t) i + 1];
    }
};

// ---------------------------------------------------------------------------
// Formant filter: three peak-normalised band-passes on vowel formants,
// morphing a -> e -> i -> o -> u, shiftable and with adjustable sharpness.
// ---------------------------------------------------------------------------
struct Formant
{
    std::array<Svf, 3> bands;

    void reset() noexcept { for (auto& b : bands) b.reset(); }

    void set (float vowel, float shift, float reso, float sampleRate) noexcept
    {
        static constexpr float F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 1700, 2700 },
                                           { 450, 800, 2830 }, { 325, 700, 2530 } };
        static constexpr float A[5][3] = { { 1.f, 0.5f, 0.25f }, { 1.f, 0.35f, 0.25f }, { 1.f, 0.2f, 0.2f },
                                           { 1.f, 0.35f, 0.15f }, { 1.f, 0.25f, 0.1f } };
        const float pos = clamp01 (vowel) * 3.999f;
        const int i = (int) pos;
        const float fr = pos - (float) i;
        const float q = 4.f + 26.f * reso;
        for (int b = 0; b < 3; ++b)
        {
            const float f = (F[i][b] + (F[i + 1][b] - F[i][b]) * fr) * shift;
            amps[(size_t) b] = A[i][b] + (A[i + 1][b] - A[i][b]) * fr;
            bands[(size_t) b].setG (prewarp (std::min (f, 0.45f * sampleRate), sampleRate), 1.f / (q * (1.f + 0.3f * b)));
        }
    }

    float process (float x) noexcept
    {
        float out = 0.f;
        for (size_t b = 0; b < 3; ++b)
        {
            const auto o = bands[b].process (x);
            out += amps[b] * o.bp * bands[b].k;  // k * bp = unity peak gain
        }
        return out * 2.f;
    }

    std::array<float, 3> amps {};
};

// ---------------------------------------------------------------------------
// Feedback comb with Hermite fractional delay and a gentle loop damping.
// ---------------------------------------------------------------------------
struct Comb
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    float delay = 100.f, fb = 0.f, damp = 0.f;

    void prepare (float sampleRate)
    {
        int size = 1;
        while (size < (int) (sampleRate / 18.f) + 8)
            size <<= 1;
        buf.assign ((size_t) size, 0.f);
        mask = size - 1;
    }

    void reset() noexcept
    {
        std::fill (buf.begin(), buf.end(), 0.f);
        damp = 0.f;
    }

    void set (float hz, float feedback, float sampleRate) noexcept
    {
        delay = std::clamp (sampleRate / std::max (hz, 20.f), 2.f, (float) (mask - 4));
        fb = feedback;
    }

    float process (float x) noexcept
    {
        if (buf.empty())
            return x;
        const float rp = (float) w - delay;
        const int i = (int) std::floor (rp);
        const float f = rp - (float) i;
        const float xm1 = buf[(size_t) ((i - 1) & mask)], x0 = buf[(size_t) (i & mask)];
        const float x1 = buf[(size_t) ((i + 1) & mask)], x2 = buf[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        const float d = ((c3 * f + c2) * f + c1) * f + x0;

        damp += 0.6f * (d - damp);
        const float y = x + fb * fastTanh (damp);
        buf[(size_t) w] = flushDenormal (y);
        w = (w + 1) & mask;
        return y * (1.f - 0.6f * std::abs (fb));
    }
};

// ---------------------------------------------------------------------------
// One filter slot of a voice: any of the types above behind a common API.
// ---------------------------------------------------------------------------
class FilterSlot
{
public:
    // Allocates for the highest rate the slot will ever run at.
    void prepare (float maxSampleRate)
    {
        fs = maxSampleRate;
        comb.prepare (maxSampleRate);
        reset();
    }

    void setRate (float sampleRate) noexcept
    {
        fs = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        svf.reset();
        ladder.reset();
        diode.reset();
        formant.reset();
        comb.reset();
    }

    void setType (int t) noexcept
    {
        if (t != type)
        {
            type = t;
            reset();
        }
    }

    int getType() const noexcept { return type; }

    // Call at control rate (every base-rate sample).
    void update (float cutoffHz, float reso, float drive, float vowel) noexcept
    {
        cutoffHz = std::clamp (cutoffHz, 16.f, 0.47f * fs);
        reso = clamp01 (reso);
        driveGain = 1.f + drive * drive * 15.f;
        switch (type)
        {
            case 1: case 2: case 3: case 4:
                svf.setG (prewarp (cutoffHz, fs), 2.f * (1.f - 0.985f * reso));
                break;
            case 5: ladder.set (prewarp (cutoffHz, fs), reso); break;
            case 6: diode.set (cutoffHz, fs, reso); break;
            case 7: formant.set (vowel, std::clamp (std::sqrt (cutoffHz / 1000.f), 0.35f, 2.5f), reso, fs); break;
            case 8: comb.set (cutoffHz, 0.985f * reso, fs); break;
            case 9: comb.set (cutoffHz, -0.985f * reso, fs); break;
            default: break;
        }
        resoCache = reso;
    }

    float process (float x) noexcept
    {
        switch (type)
        {
            case 1: case 2: case 3: case 4:
            {
                const float in = driveGain > 1.001f ? fastTanh (x * driveGain) / std::sqrt (driveGain) : x;
                const auto o = svf.process (in);
                if (type == 1) return o.lp;
                if (type == 2) return o.bp * (0.3f + 0.7f * svf.k);
                if (type == 3) return o.hp;
                return in - svf.k * o.bp;  // notch
            }
            case 5:
                return ladder.process (x * 0.8f * driveGain) * (1.f + 0.6f * ladder.k) / (0.8f * std::sqrt (driveGain));
            case 6:
                return diode.process (x * 0.9f * driveGain) * (1.f + 0.35f * diode.k) / (0.9f * std::sqrt (driveGain));
            case 7:
                return formant.process (driveGain > 1.001f ? fastTanh (x * driveGain) : x);
            case 8: case 9:
                return comb.process (driveGain > 1.001f ? fastTanh (x * driveGain) : x);
            default:
                return x;
        }
    }

private:
    float fs = 48000.f;
    int type = 0;
    float driveGain = 1.f, resoCache = 0.f;
    Svf svf;
    Ladder ladder;
    DiodeLadder diode;
    Formant formant;
    Comb comb;
};

} // namespace psy::dsp
