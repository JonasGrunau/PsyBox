#pragma once

// Zero-delay-feedback (TPT) filters after V. Zavalishin, "The Art of VA Filter
// Design" (rev. 2.1) and A. Simper's SVF notes. The two ladders put their
// saturator inside the feedback loop and solve the implicit equation with
// Newton-Raphson every sample, instead of the one-sample-delay approximations
// that detune the cutoff and make resonance depend on sample rate.

#include "Common.h"
#include "FilterTypes.h"

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
// TPT one-pole (6 dB/oct), used alone and to make the odd-order slopes.
// ---------------------------------------------------------------------------
struct TptOnePole
{
    float s = 0.f, G = 0.f;
    void reset() noexcept { s = 0.f; }
    void setG (float g) noexcept { G = g / (1.f + g); }
    float lp (float x) noexcept
    {
        const float v = (x - s) * G;
        const float y = v + s;
        s = y + v;
        return y;
    }
    float hp (float x) noexcept { return x - lp (x); }
};

// ---------------------------------------------------------------------------
// Four-pole transistor ladder. Cascade of TPT one-poles; the feedback node
// u = tanh(x - k*y4) is solved implicitly (y4 is affine in u).
// The stage outputs are kept so other responses can be mixed from them
// (Oberheim Xpander style) while the resonance always comes from y4:
//   LP12 = y2, LP24 = y4, HP24 = u - 4y1 + 6y2 - 4y3 + y4, BP = y2 - 2y3 + y4
// ---------------------------------------------------------------------------
struct Ladder
{
    std::array<float, 4> s {}, y {};
    float G = 0.f, k = 0.f, u = 0.f;

    float lp12() const noexcept { return y[1]; }
    float hp24() const noexcept { return u - 4.f * y[0] + 6.f * y[1] - 4.f * y[2] + y[3]; }
    float bp() const noexcept { return 4.f * (y[1] - 2.f * y[2] + y[3]); }   // x4: unity at the cutoff

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
        u = (x - k * b) / (1.f + k * a);
        for (int it = 0; it < 2; ++it)
        {
            const float t = std::tanh (x - k * (a * u + b));
            const float f = u - t;
            const float df = 1.f + k * a * (1.f - t * t);
            u -= f / df;
        }

        float in = u;
        for (size_t i = 0; i < 4; ++i)
        {
            const float v = (in - s[i]) * G;
            y[i] = v + s[i];
            s[i] = y[i] + v;
            in = y[i];
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
    // Thomas factorisation of M = I - g A (depends only on g), and the
    // response to the input node, M^-1 (g e1), which depends on g alone too
    std::array<float, 4> cp {}, inv {}, lower {}, ye {};

    struct Calibration { float peakRatio; float kMax; };

    // What computeCalibration() finds (a test checks them against it), so
    // the 2.8 ms scan never runs on the audio thread.
    static const Calibration& calibration() noexcept
    {
        static constexpr Calibration c { 1.41422987f, 17.0003681f };
        return c;
    }

    // The scan the constants come from: the -180 degree crossing of the open loop.
    static Calibration computeCalibration()
    {
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
        lower = { -g, -g, -2.f * g, 0.f };
        // Forward sweep of Thomas; inv[i] = 1 / modified diagonal, cp[i] = modified upper / diag
        inv[0] = 1.f / diag;
        cp[0] = upper * inv[0];
        for (size_t i = 1; i < 4; ++i)
        {
            const float d = diag - lower[i - 1] * cp[i - 1];
            inv[i] = 1.f / d;
            cp[i] = (i < 3 ? upper : 0.f) * inv[i];
        }
        thomas4 (inv, lower, cp, { g, 0.f, 0.f, 0.f }, ye);
    }

    float process (float x) noexcept
    {
        // y = M^-1 s + u * M^-1 (g e1)
        std::array<float, 4> ys;
        thomas4 (inv, lower, cp, s, ys);

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
            bands[(size_t) b].setG (prewarp (std::min (f, 0.45f * sampleRate), sampleRate), 1.f / (q * (1.f + 0.3f * (float) b)));
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
// Feedback comb with Hermite fractional delay and a gentle loop damping: a
// one-pole low-pass in the loop with its corner at 14 kHz whatever rate the
// comb runs at (the fixed coefficient 0.6 it had put that corner at 7.5, 15
// and 30 kHz at 1x, 2x and 4x; 14 kHz is 0.6 at 2x of 48 kHz).
// ---------------------------------------------------------------------------
struct Comb
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    float delay = 100.f, fb = 0.f, damp = 0.f, dampCoef = 0.6f, dampFs = 96000.f;

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
        if (differs (sampleRate, dampFs))
        {
            dampFs = sampleRate;
            dampCoef = 1.f - std::exp (-kTwoPi * 14000.f / sampleRate);
        }
    }

    float process (float x) noexcept
    {
        if (buf.empty())
            return x;
        const int di = (int) delay;   // delay >= 2: split before the subtraction, so the fraction keeps its precision
        const float f0 = delay - (float) di;
        const int i = f0 > 0.f ? w - di - 1 : w - di;
        const float f = f0 > 0.f ? 1.f - f0 : 0.f;
        const float d = hermite4 (buf[(size_t) ((i - 1) & mask)], buf[(size_t) (i & mask)],
                                  buf[(size_t) ((i + 1) & mask)], buf[(size_t) ((i + 2) & mask)], f);

        damp += dampCoef * (d - damp);
        const float y = x + fb * fastTanh (damp);
        buf[(size_t) w] = flushDenormal (y);
        w = (w + 1) & mask;
        return y * (1.f - 0.6f * std::abs (fb));
    }
};

// ---------------------------------------------------------------------------
// One filter of a voice: any of the types above behind a common API.
//
// Slopes are built from Butterworth prototypes so that at zero resonance
// each one has a clean -3 dB corner at the cutoff:
//   12 dB: one SVF          18 dB: one-pole + SVF (k = 1)
//   24 dB: two SVFs (k = 1.848, 0.765)
// Resonance narrows only the last (highest-Q) section, which keeps the
// peak on the cutoff and the rest of the curve steep.
//
// Drive: tanh (x g) / sqrt (g) in front of the filter. It used to be bypassed
// up to g = 1.001 (drive 0.0082) and fully on above, a jump a modulated drive
// crossed; now it is blended in over g - 1 = 0 .. 0.001, so the sound is
// continuous from drive 0 (where the filter sees the input untouched, bit for
// bit) and what it was from drive 0.0082 up.
// ---------------------------------------------------------------------------
class FilterCore
{
public:
    // Allocates for the highest rate the filter will ever run at.
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
        svf2.reset();
        pole.reset();
        ladder.reset();
        diode.reset();
        formant.reset();
        comb.reset();
    }

    void setType (int t) noexcept { type = (FilterType) t; }
    int getType() const noexcept { return (int) type; }

    // Call at control rate (every base-rate sample).
    void update (float cutoffHz, float reso, float drive, float vowel) noexcept
    {
        using T = FilterType;
        cutoffHz = std::clamp (cutoffHz, 16.f, 0.47f * fs);
        reso = clamp01 (reso);
        driveGain = 1.f + drive * drive * 15.f;
        driveSqrt = std::sqrt (driveGain);
        driveMix = std::min (1.f, (driveGain - 1.f) * 1000.f);
        const float g = prewarp (cutoffHz, fs);
        const float q = 1.f - 0.985f * reso;   // shrinks the damping of the resonant section
        switch (type)
        {
            case T::LP6: case T::HP6: pole.setG (g); break;
            case T::LP12: case T::HP12: case T::BP12: case T::Notch: svf.setG (g, 2.f * q); break;
            case T::LP18: case T::HP18: pole.setG (g); svf.setG (g, 1.f * q); break;
            case T::LP24: case T::HP24: svf2.setG (g, 1.848f); svf.setG (g, 0.765f * q); break;
            case T::BP24: svf2.setG (g, 1.f); svf.setG (g, 2.f * q); break;
            case T::LadderLP12: case T::Ladder24: case T::LadderHP: case T::LadderBP: ladder.set (g, reso); break;
            case T::Diode303: diode.set (cutoffHz, fs, reso); break;
            case T::Formant: formant.set (vowel, std::clamp (std::sqrt (cutoffHz / 1000.f), 0.35f, 2.5f), reso, fs); break;
            case T::CombPos: comb.set (cutoffHz, 0.985f * reso, fs); break;
            case T::CombNeg: comb.set (cutoffHz, -0.985f * reso, fs); break;
            case T::Off: case T::Count: break;
        }
    }

    float process (float x) noexcept
    {
        using T = FilterType;
        const auto shaped = [this] (float in) {
            if (driveMix <= 0.f) return in;
            const float sh = fastTanh (in * driveGain) / driveSqrt;
            return driveMix >= 1.f ? sh : in + driveMix * (sh - in);
        };
        // the same without the level normalisation (formant and comb)
        const auto saturated = [this] (float in) {
            if (driveMix <= 0.f) return in;
            const float sh = fastTanh (in * driveGain);
            return driveMix >= 1.f ? sh : in + driveMix * (sh - in);
        };
        switch (type)
        {
            case T::LP6: return pole.lp (shaped (x));
            case T::HP6: return pole.hp (shaped (x));
            case T::LP12: return svf.process (shaped (x)).lp;
            case T::HP12: return svf.process (shaped (x)).hp;
            case T::BP12: return svf.process (shaped (x)).bp * (0.3f + 0.7f * svf.k);
            case T::Notch: { const float in = shaped (x); return in - svf.k * svf.process (in).bp; }
            case T::LP18: return svf.process (pole.lp (shaped (x))).lp;
            case T::HP18: return svf.process (pole.hp (shaped (x))).hp;
            case T::LP24: return svf.process (svf2.process (shaped (x)).lp).lp;
            case T::HP24: return svf.process (svf2.process (shaped (x)).hp).hp;
            case T::BP24:
            {
                const float wide = svf2.process (shaped (x)).bp * svf2.k;
                return svf.process (wide).bp * (0.3f + 0.7f * svf.k);
            }
            case T::LadderLP12: case T::Ladder24: case T::LadderHP: case T::LadderBP:
            {
                const float lp = ladder.process (x * 0.8f * driveGain);
                const float norm = 1.f / (0.8f * driveSqrt);
                const float comp = 1.f + 0.6f * ladder.k;   // passband loss from the feedback
                if (type == T::Ladder24) return lp * comp * norm;
                if (type == T::LadderLP12) return ladder.lp12() * comp * norm;
                if (type == T::LadderHP) return ladder.hp24() * norm;
                return ladder.bp() * (1.f + 0.25f * ladder.k) * norm;
            }
            case T::Diode303:
                return diode.process (x * 0.9f * driveGain) * (1.f + 0.35f * diode.k) / (0.9f * driveSqrt);
            case T::Formant:
                return formant.process (saturated (x));
            case T::CombPos: case T::CombNeg:
                return comb.process (saturated (x));
            case T::Off: case T::Count:
                break;
        }
        return x;
    }

private:
    float fs = 48000.f;
    FilterType type = FilterType::Off;
    float driveGain = 1.f, driveSqrt = 1.f, driveMix = 0.f;
    Svf svf, svf2;
    TptOnePole pole;
    Ladder ladder;
    DiodeLadder diode;
    Formant formant;
    Comb comb;
};

// A filter that can change type while a note sounds: the old type keeps
// running next to the new one and the output crossfades over 5 ms, instead
// of the state being zeroed under a sustained sound (a click). A slot that
// has not run since its last reset switches at once (nothing to fade from).
class FilterSlot
{
public:
    void prepare (float maxSampleRate)
    {
        for (auto& c : core) c.prepare (maxSampleRate);
        setRate (maxSampleRate);
    }

    void setRate (float sampleRate) noexcept
    {
        for (auto& c : core) c.setRate (sampleRate);
        fadeStep = 1.f / (0.005f * sampleRate);
        fade = 1.f;
        used = false;
    }

    void reset() noexcept
    {
        for (auto& c : core) c.reset();
        fade = 1.f;
        used = false;
    }

    void setType (int t) noexcept
    {
        if (t == core[cur].getType())
            return;
        if (used)
        {
            old = cur;
            cur = 1 - cur;
            fade = 0.f;
        }
        core[cur].reset();
        core[cur].setType (t);
        if (haveParams)
            core[cur].update (lastCutoff, lastReso, lastDrive, lastVowel);
    }

    int getType() const noexcept { return core[cur].getType(); }

    // Call at control rate (every base-rate sample).
    void update (float cutoffHz, float reso, float drive, float vowel) noexcept
    {
        lastCutoff = cutoffHz; lastReso = reso; lastDrive = drive; lastVowel = vowel;
        haveParams = true;
        core[cur].update (cutoffHz, reso, drive, vowel);
        if (fade < 1.f)
            core[old].update (cutoffHz, reso, drive, vowel);
    }

    float process (float x) noexcept
    {
        used = true;
        if (fade >= 1.f)
            return core[cur].process (x);
        const float a = core[old].process (x), b = core[cur].process (x);
        fade = std::min (1.f, fade + fadeStep);
        return a + fade * (b - a);
    }

private:
    std::array<FilterCore, 2> core;
    size_t cur = 0, old = 1;
    float fade = 1.f, fadeStep = 1.f / 240.f;
    bool used = false, haveParams = false;
    float lastCutoff = 1000.f, lastReso = 0.f, lastDrive = 0.f, lastVowel = 0.f;
};

} // namespace psy::dsp
