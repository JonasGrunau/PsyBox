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
    // the base rate (anywhere later). Each effect fades over 10 ms when `on`
    // changes and starts clean after a full stop (dsp::FxSwitch): the engine
    // calls it while its `...Active (on)` is true.
    void distortionOs (float* l, float* r, int n, const ParamState& p, bool on) noexcept;
    void distortion (float* l, float* r, int n, const ParamState& p, bool on) noexcept;
    void eq (float* l, float* r, int n, const ParamState& p, bool on) noexcept;
    void chorus (float* l, float* r, int n, const ParamState& p, bool on) noexcept;
    void delay (float* l, float* r, int n, double bpm, const ParamState& p, bool on) noexcept;
    void reverb (float* l, float* r, int n, const ParamState& p, bool on) noexcept;
    void limiter (float* l, float* r, int n, const ParamState& p, bool on) noexcept;
    bool distortionOsActive (bool on) noexcept { return distOs.isActive (on); }
    bool distortionActive (bool on) noexcept { return distLate.isActive (on); }
    bool eqActive (bool on) noexcept { return peq.isActive (on); }
    bool chorusActive (bool on) noexcept { return cho.isActive (on); }
    bool delayActive (bool on) noexcept { return dly.isActive (on); }
    bool reverbActive (bool on) noexcept { return rev.isActive (on); }
    bool limiterActive (bool on) noexcept { return lim.isActive (on); }

    // How late each one plays the audio, in samples at the rate it runs at.
    int distortionOsLatency() const noexcept { return distOs.latencySamples(); }
    int distortionLatency() const noexcept { return distLate.latencySamples(); }
    int limiterLatency() const noexcept { return lim.latencySamples(); }

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
