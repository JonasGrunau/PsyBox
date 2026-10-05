#pragma once

#include "Params.h"
#include "dsp/Filters.h"
#include "dsp/Modulators.h"
#include "dsp/Oscillators.h"

#include <array>

namespace psy
{

// Everything a voice needs from the engine for one chunk of samples.
struct VoiceContext
{
    const ParamState* p = nullptr;   // smoothed parameter values
    float baseRate = 48000.f;
    float osRate = 96000.f;
    int osFactor = 2;
    float harmonicLimit = 0.62f;     // highest partial as fraction of osRate
    float bpm = 120.f;
    float pitchBend = 0.f;           // -1..1
    float modWheel = 0.f, aftertouch = 0.f;
    std::array<const float*, kNumLfos> globalLfo {};  // per-sample values for free-running LFOs
    std::array<float, kNumLfos> lfoHz {};
    // Active matrix slots, pre-filtered by the engine.
    struct Slot { int src, dst; float amt; };
    std::array<Slot, kNumModSlots> slots {};
    int numSlots = 0;
};

class Voice
{
public:
    // maxOsRate sizes the comb buffers once; setRates never allocates.
    void prepare (float baseRate, float maxOsRate);
    void setRates (float baseRate, float osRate);

    // retrigger=false keeps envelopes running (legato). glideFrom < 0: no glide.
    void start (int note, float velocity, bool accent, bool retrigger, bool stolen, float glideFrom,
                const VoiceContext& ctx, uint64_t seed);
    void changeNote (int note, bool accent, bool doGlide);
    void release();
    void kill();

    bool isActive() const noexcept { return amp.active(); }
    bool isGateOn() const noexcept { return gate; }
    int getNote() const noexcept { return note; }
    uint64_t age = 0;

    // Adds nBase * osFactor oversampled samples into outL/outR.
    void render (float* outL, float* outR, int chunkOffsetBase, int nBase, const VoiceContext& ctx) noexcept;

    float currentPitchSemis() const noexcept { return pitchNow; }
    float ampLevel() const noexcept { return amp.value(); }
    // Mod source values at the end of the last render (effect modulation
    // follows the newest voice).
    const std::array<float, (size_t) ModSrc::Count>& sources() const noexcept { return srcNow; }

private:
    dsp::AnalogOsc anA, anB, sub;
    dsp::Supersaw ssA, ssB;
    dsp::WavetableOsc wtA, wtB;
    dsp::FmOsc fmA, fmB;
    dsp::Noise noise;
    dsp::FilterSlot f1, f2;
    dsp::Adsr amp, fenv, menv;
    dsp::PitchEnv penv;
    std::array<dsp::Lfo, kNumLfos> lfos;
    dsp::Rng rng;

    std::array<float, (size_t) ModSrc::Count> srcNow {};
    double syncPhase = 0.0;
    int note = 60;
    float velocity = 1.f, randomVal = 0.f;
    bool accent = false, gate = false;
    float pitchNow = 60.f, pitchTarget = 60.f;
    bool gliding = false;
    int typeA = -1, typeB = -1;

    float oscSample (int which, int type, double inc, float shape, float pw, float detune, int table, float wtPos,
                     float fmRatio, float fmIndex, float fmFb, const dsp::SyncInfo* sync, float xmodPm, float harmonicLimit) noexcept;
};

} // namespace psy
