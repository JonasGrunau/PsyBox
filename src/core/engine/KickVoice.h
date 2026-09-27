#pragma once

#include "Params.h"
#include "Voice.h"
#include "dsp/Filters.h"
#include "dsp/Modulators.h"
#include "dsp/Oscillators.h"
#include "dsp/Shapers.h"

namespace psy
{

// Everything one kick hit reads from the patch, gathered once per chunk.
struct KickSettings
{
    float tuneHz = 44.f;        // final body pitch (the centre of the key-track range)
    bool keyTrack = true;       // the body lands on the played key
    float startHz = 2000.f, snapMs = 3.f;     // stage 1: the fast drop from the top
    float kneeHz = 330.f, sweepMs = 30.f;     // stage 2: the slower fall into the body
    float attackMs = 40.f, holdMs = 120.f, decayMs = 120.f;
    float punch = 1.f, punchMs = 6.f;
    int clickType = 0;
    float clickLevel = 0.15f, clickMs = 12.f, clickHz = 4000.f;
    int shaper = dsp::AdaaShaper::Tube;
    float driveDb = -27.f, toneHz = 20000.f;
    float velSens = 0.5f;

    static KickSettings from (const ParamState&) noexcept;
};

// The frequency the body settles on for a note. With key tracking the note's
// pitch class is placed within half an octave of Tune, so any octave of the
// key gives the same register; without it the note is ignored.
float kickBodyHz (const KickSettings&, int note) noexcept;

// Instantaneous pitch at time t (seconds) after the hit. Both pitch stages
// land exactly on their target after three time constants, so the body is
// in tune from then on instead of creeping down forever.
float kickPitchAt (const KickSettings&, int note, float seconds) noexcept;

// A psytrance kick: a sine whose pitch falls in two exponential stages from
// a click-like top to the tuned body, an attack/hold/decay amp envelope with
// a punch transient, an optional click layer, a body saturator and a tone
// low-pass. One-shot and mono. Every hit starts at phase 0, so repeated hits
// are identical; a retrigger chokes the old hit with a short fade instead.
class KickVoice
{
public:
    void prepare (float baseRate, float osRate);
    void setRates (float baseRate, float osRate);

    void trigger (int note, float velocity, const VoiceContext& ctx) noexcept;
    void choke() noexcept;            // ~3 ms fade, then idle
    void kill() noexcept;
    void raiseVelocity (float velocity) noexcept;   // merge a near-simultaneous hit

    bool isActive() const noexcept { return active; }
    float level() const noexcept { return amp.value() * chokeGain; }
    int getNote() const noexcept { return note; }
    int samplesSinceTrigger() const noexcept { return (int) std::min<int64_t> (elapsed / osFactor, 1 << 30); }   // base rate

    // Adds nBase * osFactor oversampled samples into outL/outR (outR may be null).
    void render (float* outL, float* outR, int chunkOffsetBase, int nBase, const VoiceContext& ctx) noexcept;

    // Offline render of one hit at `sampleRate` (no oversampling), for displays
    // and tests. Any output pointer may be null.
    static void renderOffline (const ParamState& p, int note, float velocity, float sampleRate, int numSamples,
                               float* out, float* env = nullptr, float* hz = nullptr);

private:
    float baseRate = 48000.f, osRate = 96000.f;
    int osFactor = 2;

    dsp::Adsr amp;
    dsp::AdaaShaper shaper;
    dsp::DcBlocker dc;
    dsp::Svf clickBp, tone;
    dsp::Noise noise;

    bool active = false, choking = false, released = false;
    int note = 60;
    float velocity = 1.f;
    int64_t elapsed = 0;            // oversampled samples since the hit
    double phase = 0.0, blipPhase = 0.0;
    float g1 = 1.f, g2 = 1.f;       // raw exponential decays of the two pitch stages
    float punchEnv = 1.f, clickEnv = 1.f;
    float chokeGain = 1.f, chokeStep = 0.f;
    float lastEnv = 0.f, lastHz = 0.f;

    void renderMono (float* outL, float* outR, int offsetOs, int numOs, const KickSettings& s) noexcept;
};

} // namespace psy
