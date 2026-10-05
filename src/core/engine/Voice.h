#pragma once

#include "Params.h"
#include "dsp/Filters.h"
#include "dsp/Modulators.h"
#include "dsp/Oscillators.h"
#include "dsp/Unison.h"

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
    // Energy sweep: linear phase step per base-rate sample (1 / length), the
    // curve's exponent, how much the level follows it, and whether the note
    // ends when it reaches the end.
    double sweepInc = 0.0;
    float sweepExp = 1.f, sweepFade = 0.f, sweepDelay = 0.f;
    bool sweepCut = false;
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
    float sweepPhase() const noexcept { return (float) sweepT; }
    // For the editor's moving dots: amp, env 1 (filter), env 2 (mod).
    const dsp::Adsr& envelope (int i) const noexcept { return i == 0 ? amp : (i == 1 ? fenv : menv); }
    const dsp::PitchEnv& pitchEnvelope() const noexcept { return penv; }
    const dsp::Lfo& lfo (int i) const noexcept { return lfos[(size_t) i]; }
    // What each filter slot is set to right now, modulation included, for
    // the moving filter curves.
    struct FilterNow { float cutoff = 1000.f, reso = 0.f, vowel = 0.f; };
    const FilterNow& filterNow (int slot) const noexcept { return filtNow[(size_t) slot]; }

private:
    // One oscillator per unison voice (the supersaw is its own unison).
    template <typename Osc> using Stack = std::array<Osc, kMaxUnison>;
    Stack<dsp::AnalogOsc> anA, anB;
    Stack<dsp::WavetableOsc> wtA, wtB;
    Stack<dsp::FmOsc> fmA, fmB;
    dsp::AnalogOsc sub;
    dsp::Supersaw ssA, ssB;
    dsp::Noise noise;
    // The right channel's filters run only while a unison oscillator is spread
    // in stereo; otherwise the voice is mono up to the pan.
    dsp::FilterSlot f1, f2, f1R, f2R;
    bool stereo = false;
    std::array<FilterNow, 2> filtNow {};

    struct Unison
    {
        dsp::UnisonLayout layout;
        std::array<float, kMaxUnison> ratio {}, gL {}, gR {};
        int n = -1, mode = -1, stack = -1;
        float detune = -1.f, range = -1.f, blend = -1.f, width = -1.f;
        bool stereo = false;
        // Recomputes the voices when a setting moved; true when something changed.
        bool update (int count, float det, float rng, int md, int stk, float bl, float wd, bool st) noexcept;
    };
    Unison uniA, uniB;
    dsp::Adsr amp, fenv, menv;
    dsp::PitchEnv penv;
    std::array<dsp::Lfo, kNumLfos> lfos;
    dsp::Rng rng;

    std::array<float, (size_t) ModSrc::Count> srcNow {};
    double syncPhase = 0.0;
    double sweepT = 0.0;   // energy sweep, 0..1 linear
    int note = 60;
    float velocity = 1.f, randomVal = 0.f;
    bool accent = false, gate = false;
    float pitchNow = 60.f, pitchTarget = 60.f;
    bool gliding = false;
    int typeA = -1, typeB = -1;

    float oscSample (int which, int k, int type, double inc, float shape, float pw, float detune, int table, float wtPos,
                     float fmRatio, float fmIndex, float fmFb, const dsp::SyncInfo* sync, float xmodPm, float harmonicLimit) noexcept;
    void resetOscs (int which, bool retrig, dsp::Rng& phases) noexcept;
};

} // namespace psy
