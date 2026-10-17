#pragma once

#include "Blep.h"
#include "Common.h"
#include "Wavetable.h"

#include <array>

namespace psy::dsp
{

// Result of one oscillator step that a hard-synced slave needs.
struct SyncInfo
{
    bool wrapped = false;
    double fraction = 0.0;  // samples since the wrap, in [0, 1)
};

// ---------------------------------------------------------------------------
// Analog-style oscillator: sine -> triangle -> saw -> square morph with a
// continuous shape control. Saw and pulse edges get 4-point BLEP corrections,
// triangle corners 4-point BLAMP corrections, hard sync resets correct both the
// value step and the slope change. The pulse edge is where phase minus width
// crosses a whole number, so a width that moves past the phase (PWM) flips the
// square with a correction too, not only the phase running into the edge.
// Output is delayed by two samples.
// ---------------------------------------------------------------------------
class AnalogOsc
{
public:
    void reset (double phase = 0.0) noexcept
    {
        p = phase;
        havePw = false;
        acc.reset();
        delay.reset();
    }

    double phase() const noexcept { return p; }

    // inc: cycles per sample (f / fs), clamped to [0, 0.49).
    // shape: 0 sine, 1 triangle, 2 saw, 3 square.
    float process (double inc, float shape, float pulseWidth, const SyncInfo* sync = nullptr,
                   SyncInfo* outSync = nullptr) noexcept
    {
        inc = std::clamp (inc, 0.0, 0.49);
        pw = std::clamp ((double) pulseWidth, 0.02, 0.98);
        if (! havePw) { pwPrev = pw; havePw = true; }
        computeWeights (shape);

        SyncInfo mine;
        if (sync != nullptr && sync->wrapped && inc > 0.0)
        {
            // Run up to the reset instant, restart from phase 0, continue.
            const double ds = sync->fraction;
            const double uReset = p + inc * (1.0 - ds);
            edgeEvents (p, uReset, inc, ds, pwPrev);
            const double atReset = uReset - std::floor (uReset);
            resetEvent (atReset, ds, inc);
            p = inc * ds;
            edgeEvents (0.0, p, inc, 0.0, pw);   // a short pulse can end before the sample does
            mine.wrapped = true;
            mine.fraction = ds;
        }
        else
        {
            const double end = p + inc;
            edgeEvents (p, end, inc, 0.0, pwPrev);
            if (end >= 1.0)
            {
                mine.wrapped = true;
                mine.fraction = inc > 0.0 ? (end - 1.0) / inc : 0.0;
            }
            p = end - std::floor (end);
        }
        pwPrev = pw;

        if (outSync != nullptr)
            *outSync = mine;
        return delay.push (naive (p)) + acc.pop();
    }

private:
    double p = 0.0, pw = 0.5, pwPrev = 0.5;
    bool havePw = false;
    blep::Accumulator acc;
    blep::Delay2 delay;
    float wSin = 0.f, wTri = 0.f, wSaw = 1.f, wSq = 0.f;

    void computeWeights (float shape) noexcept
    {
        shape = std::clamp (shape, 0.f, 3.f);
        wSin = wTri = wSaw = wSq = 0.f;
        if (shape < 1.f)      { wSin = 1.f - shape; wTri = shape; }
        else if (shape < 2.f) { wTri = 2.f - shape; wSaw = shape - 1.f; }
        else                  { wSaw = 3.f - shape; wSq = shape - 2.f; }
    }

    float naive (double ph) const noexcept
    {
        const float x = (float) ph;
        float out = 0.f;
        if (wSin > 0.f) out += wSin * std::sin (kTwoPi * x);
        if (wTri > 0.f) out += wTri * (x < 0.5f ? 4.f * x - 1.f : 3.f - 4.f * x);
        if (wSaw > 0.f) out += wSaw * (2.f * x - 1.f);
        if (wSq > 0.f)  out += wSq * (ph < pw ? 1.f : -1.f);
        return out;
    }

    // All waveform corners in the unwrapped phase interval (from, to], where
    // `to` is reached dOffset samples before the current sample. pwFrom is the
    // pulse width at `from`; it moves to `pw` in a straight line across the
    // interval.
    void edgeEvents (double from, double to, double inc, double dOffset, double pwFrom) noexcept
    {
        if (inc <= 0.0)
            return;
        const auto since = [&] (double u) { return (to - u) / inc + dOffset; };
        const auto inRange = [&] (double u) { return u > from && u <= to; };
        const float triSlope = 8.f * (float) inc * wTri;

        // The pulse edge: p - pw crossing a whole number, upwards (the square
        // falls) or, when the width runs ahead of the phase, downwards (it rises).
        if (wSq > 0.f)
        {
            const double u0 = from - pwFrom, u1 = to - pw, du = u1 - u0;
            if (std::abs (du) > 1e-12)
                for (int n = 0; n <= 1; ++n)
                {
                    const double edge = (double) n;
                    const bool up = du > 0.0 && edge > u0 && edge <= u1;
                    const bool down = du < 0.0 && edge >= u1 && edge < u0;
                    if (up || down)
                        acc.addStep ((1.0 - (edge - u0) / du) * (to - from) / inc + dOffset, up ? -2.f * wSq : 2.f * wSq);
                }
        }

        for (double k = 0.0; k <= 1.0; k += 1.0)
        {
            if (inRange (k + 0.5)) acc.addRamp (since (k + 0.5), -triSlope);
            if (k > 0.0 && inRange (k))
            {
                const double d = since (k);
                acc.addStep (d, 2.f * wSq - 2.f * wSaw);
                acc.addRamp (d, triSlope);
            }
        }
    }

    // Hard-sync restart: value step plus slope change of every component.
    void resetEvent (double atReset, double d, double inc) noexcept
    {
        const float x = (float) atReset;
        const float triBefore = x < 0.5f ? 4.f * x - 1.f : 3.f - 4.f * x;
        const float sawBefore = 2.f * x - 1.f;
        const float sqBefore = atReset < pw ? 1.f : -1.f;
        const float sinBefore = std::sin (kTwoPi * x);
        const float jump = wTri * (-1.f - triBefore) + wSaw * (-1.f - sawBefore)
                         + wSq * (1.f - sqBefore) + wSin * (0.f - sinBefore);
        acc.addStep (d, jump);

        const float fi = (float) inc;
        const float triSlopeBefore = (x < 0.5f ? 4.f : -4.f) * fi;
        const float sinSlopeBefore = kTwoPi * fi * std::cos (kTwoPi * x);
        acc.addRamp (d, wTri * (4.f * fi - triSlopeBefore) + wSin * (kTwoPi * fi - sinSlopeBefore));
    }
};

// ---------------------------------------------------------------------------
// Supersaw: seven BLEP saws with the JP-8000 detune and mix curves
// (after Adam Szabo, "How to Emulate the Super Saw", 2010). All seven share
// one correction accumulator since they are summed with the same delay.
// ---------------------------------------------------------------------------
class Supersaw
{
public:
    void reset (Rng& rng) noexcept
    {
        for (auto& ph : phases)
            ph = rng.uniform();
        acc.reset();
        delay.reset();
    }

    float process (double inc, float detuneAmount) noexcept
    {
        const double x = std::clamp ((double) detuneAmount, 0.0, 1.0);
        const double dt = detuneCurve (x);
        const float cGain = centreGain (x), sGain = sideGain (x);

        float naive = 0.f;
        for (int i = 0; i < 7; ++i)
        {
            const double vInc = std::clamp (inc * (1.0 + kOffsets[i] * dt), 0.0, 0.49);
            const float g = i == 3 ? cGain : sGain;
            double ph = phases[(size_t) i] + vInc;
            if (ph >= 1.0)
            {
                ph -= 1.0;
                if (vInc > 0.0)
                    acc.addStep (ph / vInc, -2.f * g);
            }
            phases[(size_t) i] = ph;
            naive += g * (2.f * (float) ph - 1.f);
        }
        return 0.45f * (delay.push (naive) + acc.pop());
    }

    // The seven saws' frequency offsets (times the detune curve) and the
    // centre and side gains, as the editor draws them.
    static constexpr double kOffsets[7] = { -0.11002313, -0.06288439, -0.01952356, 0.0,
                                            0.01991221, 0.06216538, 0.10745242 };
    static float centreGain (double x) noexcept { return (float) (-0.55366 * x + 0.99785); }
    static float sideGain (double x) noexcept { return (float) (-0.73764 * x * x + 1.2841 * x + 0.044372); }

    static double detuneCurve (double x) noexcept
    {
        // 11th-order fit of the measured JP-8000 detune knob response.
        static constexpr double c[12] = { 0.0030115596, 0.6717417634, -24.1878824391, 404.2703938388,
                                          -3425.0836591318, 17019.9518580080, -53046.9642751875,
                                          106649.6679158292, -138150.6761080548, 111363.4808729368,
                                          -50818.8652045924, 10028.7312891634 };
        double y = 0.0;
        for (int i = 11; i >= 0; --i)
            y = y * x + c[i];
        return y;
    }

private:
    std::array<double, 7> phases {};
    blep::Accumulator acc;
    blep::Delay2 delay;
};

// ---------------------------------------------------------------------------
// Wavetable oscillator reading mip-mapped, band-limited frames with 4-point
// Hermite interpolation, crossfading between mip levels and frames.
// ---------------------------------------------------------------------------
class WavetableOsc
{
public:
    void setBank (const WavetableBank* b) noexcept { bank = b; }
    void reset (double phase = 0.0) noexcept { p = phase; }

    // harmonicLimit: the partials' target ceiling as a fraction of the
    // oscillator's rate. foldLimit (0: derived from harmonicLimit, see
    // WavetableBank::read) is the true bound, 1 - 20 kHz / rate, above which
    // a partial folds into the audible band.
    float process (double inc, int table, float position, float harmonicLimit, float foldLimit = 0.f) noexcept
    {
        if (bank == nullptr)
            return 0.f;
        inc = std::clamp (inc, 0.0, 0.49);
        p += inc;
        if (p >= 1.0)
            p -= 1.0;
        return bank->read (table, position, (float) p, (float) inc, harmonicLimit, foldLimit);
    }

    double phase() const noexcept { return p; }

private:
    const WavetableBank* bank = nullptr;
    double p = 0.0;
};

// ---------------------------------------------------------------------------
// Two-operator phase-modulation oscillator with modulator self-feedback.
// Feedback uses the average of the last two outputs (as the DX7 does) to
// suppress the period-2 hunting that plain feedback produces at high amounts.
// ---------------------------------------------------------------------------
class FmOsc
{
public:
    void reset (double phase = 0.0) noexcept
    {
        pc = phase;
        pm = phase;
        fb1 = fb2 = 0.f;
    }

    float process (double inc, float ratio, float index, float feedback, float extPm = 0.f) noexcept
    {
        inc = std::clamp (inc, 0.0, 0.49);
        const double mInc = std::min (inc * (double) ratio, 0.49);
        pm += mInc;
        pm -= std::floor (pm);
        pc += inc;
        pc -= std::floor (pc);

        const float fbPhase = feedback * 1.4f * 0.5f * (fb1 + fb2);
        const float mod = std::sin (kTwoPi * (float) pm + fbPhase);
        fb2 = fb1;
        fb1 = mod;
        return std::sin (kTwoPi * (float) pc + index * mod + extPm);
    }

    double phase() const noexcept { return pc; }

private:
    double pc = 0.0, pm = 0.0;
    float fb1 = 0.f, fb2 = 0.f;
};

// ---------------------------------------------------------------------------
// Noise: white, pink (Kellet's refined 7-pole filter), "digital" sample &
// hold noise clocked from the note pitch, and hiss: white noise above about
// 10 kHz (three one-pole high-passes), the fizz on top of a distorted bass.
//
// It runs at the oversampled rate, and what reaches the output is its band
// up to the base rate's Nyquist, so white noise of a fixed amplitude would
// fall 3 dB per doubling of the rate (white and hiss: -7.9 / -11.0 / -14.0 dB
// at 1x / 2x / 4x of 48 kHz). White is scaled by sqrt (rate / 96 kHz), which
// leaves 2x at 48 kHz as it was and makes the level the same at every rate.
// Pink needs no such scaling: Kellet's filter has fixed poles in cycles per
// sample, so its 1/f band grows with the rate by as much as the white input
// loses. What differs is the shape in the top octave, which a trim of up to
// 0.8 dB evens out. The hiss's high-passes are TPT one-poles (flat to Nyquist
// at any rate, with the corner prewarped) that reproduce the old exp-matched
// one-poles at 96 kHz: their corner is 9.35 kHz, which is where those sat;
// their warping at 48 kHz leaves 1.2 dB more in the top octave, also trimmed.
// Both trims level the band below 19 kHz with 2x of 48 kHz (measured).
// ---------------------------------------------------------------------------
class Noise
{
public:
    Noise() noexcept { setRate (96000.f); }

    // The rate the noise runs at, for the level and the hiss's corner.
    void setRate (float sampleRate) noexcept
    {
        const float fs = std::max (sampleRate, 24000.f);
        whiteGain = std::sqrt (fs / 96000.f);
        const float hissG = std::tan (kPi * kHissHz / fs);
        hissK = hissG / (1.f + hissG);
        const float octaves = std::clamp (std::log2 (fs / 96000.f), -1.5f, 1.5f);
        const auto trimDb = [octaves] (float below, float above) { return octaves < 0.f ? -octaves * below : octaves * above; };
        pinkGain = 0.2f * dbToGain (trimDb (0.512f, 0.764f));
        hissGain = kHissGain * dbToGain (trimDb (-1.162f, 0.181f));
    }

    void reset (uint64_t seed) noexcept
    {
        rng.reseed (seed);
        b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.f;
        held = 0.f;
        clock = 0.0;
        hp1 = hp2 = hp3 = 0.f;
    }

    float process (int type, double clockInc) noexcept
    {
        const float white = rng.bipolar();
        switch (type)
        {
            case 1:
            {
                b0 = 0.99886f * b0 + white * 0.0555179f;
                b1 = 0.99332f * b1 + white * 0.0750759f;
                b2 = 0.96900f * b2 + white * 0.1538520f;
                b3 = 0.86650f * b3 + white * 0.3104856f;
                b4 = 0.55000f * b4 + white * 0.5329522f;
                b5 = -0.7616f * b5 - white * 0.0168980f;
                const float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
                b6 = white * 0.115926f;
                return pink * pinkGain;
            }
            case 2:
                clock += clockInc;
                if (clock >= 1.0)
                {
                    clock -= std::floor (clock);
                    held = white;
                }
                return held;
            case 3:
            {
                // white minus its low end, three times: 18 dB per octave under the corner
                const float w = white * whiteGain;
                const float a = hp (hp1, w);
                const float b = hp (hp2, a);
                return hp (hp3, b) * hissGain;
            }
            default:
                return white * whiteGain * 0.7f;
        }
    }

private:
    // the old one-pole high-pass at 96 kHz had 2c / (1 + c) of a TPT one's gain at Nyquist
    // (c = exp (-2 pi 10 kHz / 96 kHz)); cubed and with the old 1.8 it is this
    static constexpr float kHissGain = 0.5759057f;
    static constexpr float kHissHz = 9354.123f;

    float hp (float& state, float x) noexcept
    {
        const float v = (x - state) * hissK;
        const float lp = v + state;
        state = lp + v;
        return x - lp;
    }

    Rng rng;
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    float held = 0.f;
    double clock = 0.0;
    float whiteGain = 1.f, pinkGain = 0.2f, hissGain = kHissGain, hissK = 0.f, hp1 = 0.f, hp2 = 0.f, hp3 = 0.f;
};

} // namespace psy::dsp
