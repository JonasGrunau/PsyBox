#pragma once

#include "Params.h"
#include "dsp/Filters.h"
#include "dsp/Modulators.h"
#include "dsp/NoiseBank.h"
#include "dsp/Oscillators.h"
#include "dsp/Unison.h"

#include <array>

namespace psy
{

// An LFO's drawn curve from its points (lfoPointsParam, lfoPointParam).
inline void readLfoCurve (const ParamState& p, int lfo, dsp::LfoCurve& out) noexcept
{
    std::array<float, kLfoPoints> xs {}, ys {}, bs {};
    for (int k = 0; k < kLfoPoints; ++k)
    {
        xs[(size_t) k] = p.get (lfoPointParam (lfo, k, LfoLane::X));
        ys[(size_t) k] = p.get (lfoPointParam (lfo, k, LfoLane::Y));
        bs[(size_t) k] = p.get (lfoPointParam (lfo, k, LfoLane::Bend));
    }
    out.set (p.geti (lfoPointsParam (lfo)), xs.data(), ys.data(), bs.data());
}

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
    // Per LFO: runs in the voice (Retrig or Env), runs once as an envelope,
    // and its drawn curve (read only when its shape is Drawn).
    std::array<bool, kNumLfos> lfoPerVoice {}, lfoEnv {};
    std::array<dsp::LfoCurve, kNumLfos> lfoCurve {};
    // Energy sweep: linear phase step per base-rate sample (1 / length), the
    // curve's exponent, how much the level follows it, and whether the note
    // ends when it reaches the end.
    double sweepInc = 0.0;
    float sweepExp = 1.f, sweepFade = 0.f, sweepDelay = 0.f;
    bool sweepCut = false;
    bool sweepUsed = true;   // the sweep source or the level fade reads it; otherwise its curve isn't computed
    // Active matrix slots, pre-filtered by the engine.
    struct Slot { int src, dst; float amt; };
    std::array<Slot, kNumModSlots> slots {};
    int numSlots = 0;
    // The noise samples: the bank (set by Engine::prepare, built there) and
    // a file of your own (null when none is loaded).
    const dsp::NoiseBank* noiseBank = nullptr;
    const dsp::NoiseClip* noiseFile = nullptr;
};

class Voice
{
public:
    friend struct EngineTestAccess;

    // maxOsRate sizes the comb buffers once; setRates never allocates.
    void prepare (float baseRate, float maxOsRate);
    void setRates (float baseRate, float osRate);

    // glideFrom < 0: no glide.
    void start (int note, float velocity, bool accent, bool stolen, float glideFrom,
                const VoiceContext& ctx, uint64_t seed);
    void changeNote (int note, bool accent, bool doGlide);
    void release();
    void kill();
    // Out in 3 ms whatever the envelope does: a voice making room for a new note.
    void fadeOut() noexcept;

    // Sounding until the smoothed level has followed the envelope down too.
    bool isActive() const noexcept { return (amp.active() && outFade > 0.f) || level2 > 1e-5f; }
    // The output level (envelope, velocity and Amp modulation, smoothed);
    // below kQuiet a voice can restart in place without a click.
    float level() const noexcept { return level2; }
    static constexpr float kQuiet = 0.002f;
    bool isGateOn() const noexcept { return gate; }
    int getNote() const noexcept { return note; }
    uint64_t age = 0;

    // Adds nBase * osFactor oversampled samples into outL/outR. `record`
    // (when given) gets every mod source at every base-rate sample, for the
    // effects' modulation.
    using SourceArray = std::array<float, (size_t) ModSrc::Count>;
    void render (float* outL, float* outR, int chunkOffsetBase, int nBase, const VoiceContext& ctx,
                 SourceArray* record = nullptr) noexcept;
    // False when the last render ended on a NaN or infinity (a filter state gone bad): the engine kills such a voice.
    bool isHealthy() const noexcept { return healthy; }

    float currentPitchSemis() const noexcept { return pitchNow; }
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
    // The clip the noise plays (the bank's or the file), or null when the
    // noise is one of the generated types.
    static const dsp::NoiseClip* noiseClip (const VoiceContext& ctx) noexcept;
    // A bank sample (rms -14 dBFS) as loud as white noise at the same level
    // (white's rms 0.4 runs at the oversampled rate and loses half its power
    // to the decimator: 0.28 at the base rate).
    static constexpr float kSampleGain = 1.41421356f;
    // One oscillator per unison voice (the supersaw is its own unison).
    template <typename Osc> using Stack = std::array<Osc, kMaxUnison>;
    Stack<dsp::AnalogOsc> anA, anB;
    Stack<dsp::WavetableOsc> wtA, wtB;
    Stack<dsp::FmOsc> fmA, fmB;
    dsp::AnalogOsc sub;
    dsp::Supersaw ssA, ssB;
    dsp::Noise noise;
    dsp::NoiseSampler sampler;
    const dsp::NoiseClip* samplerClip = nullptr;   // what the sampler last started on
    // The right channel's filters run only while a unison oscillator is spread
    // in stereo; otherwise the voice is mono up to the pan.
    dsp::FilterSlot f1, f2, f1R, f2R;
    bool stereo = false;
    // How much of the right channel comes from the right-hand filters (1) and
    // how much is the left channel (0); it moves over 5 ms when the unison
    // stops or starts spreading, so the hand-over doesn't jump.
    float stereoMix = 0.f;
    bool stereoSnap = true;   // a fresh note takes the spread it has without the fade
    std::array<FilterNow, 2> filtNow {};

    struct Unison
    {
        dsp::UnisonLayout layout;
        std::array<float, kMaxUnison> ratio {}, gL {}, gR {};
        int n = -1, mode = -1, stack = -1;
        float detune = -1.f, range = -1.f, blend = -1.f, width = -1.f;
        bool stereo = false;
        // Recomputes the voices when a setting moved; true when something changed.
        bool update (int count, float det, float rangeSemis, int md, int stk, float bl, float wd, bool st) noexcept;
    };
    Unison uniA, uniB;
    dsp::Adsr amp, fenv, menv;
    dsp::PitchEnv penv;
    std::array<dsp::Lfo, kNumLfos> lfos;
    dsp::Rng rng;

    // Rate offsets of the per-voice LFOs from the matrix, carried from sample
    // to sample (and chunk to chunk): an LFO runs before the offsets of its
    // own sample are summed.
    std::array<float, kNumLfos> lfoRateMod {};
    bool healthy = true;
    double syncPhase = 0.0;
    double sweepT = 0.0;   // energy sweep, 0..1 linear
    int note = 60;
    float velocity = 1.f, randomVal = 0.f;
    bool accent = false, gate = false;
    float pitchNow = 60.f, pitchTarget = 60.f;
    bool gliding = false;
    int typeA = -1, typeB = -1;
    // The level moves at most a full note's level per millisecond, then a
    // 0.1 ms one-pole rounds the corners: an instant attack, a retrigger, a
    // fast release or a gate chop becomes a ~1 ms ramp instead of a click,
    // and anything slower passes as it is.
    float level1 = 0.f, level2 = 0.f, levelSlew = 1.f, levelCoef = 1.f;
    // Oscillators that start from a reset phase jump from silence to their
    // first sample, and a resonant filter rings on that step: the mix fades
    // in over 1 ms before the filters.
    float onset = 1.f, onsetInc = 1.f;
    float outFade = 1.f, outFadeStep = 0.f, outFadeRate = 0.f;   // fadeOut()
    std::array<float, 2> cutOct {};   // each filter's cutoff offset (octaves) as last set
    float cutFall = 1.f;
    bool cutFresh = true;

    float oscSample (int which, int k, int type, double inc, float shape, float pw, float detune, int table, float wtPos,
                     float fmRatio, float fmIndex, float fmFb, const dsp::SyncInfo* sync, float xmodPm, float harmonicLimit) noexcept;
    void resetOscs (int which, bool retrig, dsp::Rng& phases) noexcept;
};

} // namespace psy
