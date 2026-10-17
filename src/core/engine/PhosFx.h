#pragma once

#include "Params.h"
#include "dsp/PhosEffects.h"

namespace psy
{

// The Phoscyon engine's own effects, read from the patch. The engine plays
// them in the chain's place of ours (Distortion, Chorus, Delay, Reverb) and
// adds the EQ and the limiter; the rest of the chain stays ours.
class PhosFx
{
public:
    void prepare (float sampleRate, float osRate);
    void setOsRate (float osRate);
    void reset() noexcept;

    // The distortion on the oversampled signal (first in the chain) or at
    // the base rate (anywhere later).
    void distortionOs (float* l, float* r, int n, const ParamState& p) noexcept;
    void distortion (float* l, float* r, int n, const ParamState& p) noexcept;
    void eq (float* l, float* r, int n, const ParamState& p) noexcept;
    void chorus (float* l, float* r, int n, const ParamState& p) noexcept;
    void delay (float* l, float* r, int n, double bpm, const ParamState& p) noexcept;
    void reverb (float* l, float* r, int n, const ParamState& p) noexcept;
    void limiter (float* l, float* r, int n, const ParamState& p) noexcept;
    void resetLimiter() noexcept { lim.reset(); }   // its lookahead holds old audio while it is off
    int limiterLookahead() const noexcept { return std::max (1, (int) std::lround (dsp::PhosLimiter::lookaheadMs * 0.001f * fs)); }

    // A synced delay time: steps / division of a whole note, dotted or triplet.
    static float delayMs (int steps, int division, int scale, double bpm) noexcept;

private:
    float fs = 48000.f;
    dsp::PhosDistortion distOs, distLate;
    dsp::PhosEq peq;
    dsp::PhosChorus cho;
    dsp::PhosDelay dly;
    dsp::PhosReverb rev;
    dsp::PhosLimiter lim;
};

} // namespace psy
