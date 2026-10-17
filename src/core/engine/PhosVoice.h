#pragma once

#include "Params.h"
#include "dsp/Common.h"
#include "dsp/Phoscyon.h"

#include <array>

namespace psy
{

// The Phoscyon engine's constants: how the knobs map onto the circuit. Fitted
// to measurements (see PhosVoice.cpp for the defaults and where they came from).
struct PhosModel
{
    // oscillator: the square's top level, its bottom's depth and sag, the
    // share of the cycle it is high plus the transistor's switching delay,
    // and how far Square PW Shift (-1..1) moves the fall
    float sqTop = 0.878f, sqLow = 1.039f, sqSagMs = 8.f;
    float sqDuty = 0.474f, sqDelayMs = 0.33f, sqPwShift = 0.0412f;

    // filter: the ladder's cutoff (log2 Hz) at Cutoff 0, 0.1 .. 1 with the
    // resonance, env mod and envelope at rest; the feedback gain at Resonance
    // 0, .25 .. 1 and how many octaves the cutoff rises with it, at 200 Hz
    // (log2 7.63) and at 1.1 kHz (10.12), straight in between
    std::array<float, 11> cutoffOct { 7.356f, 7.418f, 7.517f, 7.701f, 7.939f, 8.230f, 8.627f, 9.063f, 9.476f, 9.828f, 10.155f };
    std::array<float, 5> resoK { 1.612f, 4.452f, 8.048f, 11.630f, 16.539f };
    std::array<float, 5> resoShiftLo { 0.f, 0.248f, 0.399f, 0.514f, 0.559f };
    std::array<float, 5> resoShiftHi { 0.f, 0.172f, 0.435f, 0.606f, 0.665f };
    float ladderM = 1.288f, fbHpHz = 121.3f, inHpHz = 46.9f, inHpQ = 0.97f, outHpHz = 410.3f;
    float gainDb = 2.22f, drive = 0.001f;

    // filter envelope: ln (decay ms) = a + b d + c d^2; depth in octaves
    // d0 + d1 2^((envmod - 1) / taper), the base pulled down by `envOffset`
    // of the depth above d0; Envmod Attack's longest attack
    float decayA = 4.376f, decayB = 0.898f, decayC = 1.516f;
    float envDepth0 = 1.0f, envDepth1 = 3.468f, envTaper = 0.119f, envOffset = 0.305f;
    float envAttackMaxMs = 402.6f;

    // accent: the envelope's decay on an accented note (Accent Decay 0 and
    // 1), the sweep capacitor's charge time at Resonance 0 and 1 and its
    // discharge, how many octaves a fully charged sweep opens the filter,
    // and how much an accent adds to the VCA (both at the default
    // calibration)
    float accDecayMs = 72.8f, accDecayMaxMs = 2425.f;
    float sweepChargeMs0 = 5.95f, sweepChargeMs1 = 15.63f, sweepDischargeMs = 133.7f;
    float sweepOct = 1.879f, accAmp = 2.349f, accReleaseMs = 51.75f;

    // VCA and slide: a released note falls fast to `vcaTailLevel` of where it
    // was, then rings out slowly
    float vcaAttackMs = 6.28f, vcaDecayMs = 1003.f, vcaReleaseMs = 4.62f, vcaTailMs = 206.f, vcaTailLevel = 0.0058f;
    float slideMs = 100.4f, slideTaper = 0.182f;

    // vibrato: rate at Speed 0 and 1, semitones at full Depth and full wheel
    float vibHz0 = 2.f, vibHz1 = 12.f, vibSemis = 2.f;

    // calibration trims: octaves per volt of TM3, the octave width's
    // reference note, and the noise level at BA662 Noise 100 %
    float tm3OctPerV = 2.f, tm5RefNote = 60.f, noiseLevel = 0.02f;

    static const PhosModel& standard() noexcept;
};

// Everything the voice reads from the patch, in the knobs' own terms.
struct PhosSettings
{
    bool square = false;
    float tune = 0.f;                 // semitones, -12..12
    float cutoff = 0.34f, reso = 0.9f, envmod = 0.32f, decay = 0.66f, accent = 1.f;   // 0..1
    float slide = 0.5f, sweep = 1.f, envAttack = 0.f, accDecay = 0.f, accVolume = 1.f;
    float vibSpeed = 0.5f, vibDepth = 0.5f;
    bool resoSweepCouple = true, accVolumeCouple = true;
    // calibration (defaults: the "ideal" unit)
    float tm3mV = 0.f, c21nF = 15.9f, accFilterEnvmV = 1951.f, accAmpEnvV = 1.0984f, c13nF = 926.f;
    float tm5 = 1.f, pwShift = 0.f;   // oct/V, -1..1
    bool clicks = false;
    float noise = 0.f;                // 0..1

    static PhosSettings from (const ParamState&) noexcept;
};

// The original's calibration presets: TM3 (mV), C21/C22 (nF), the accent's
// filter env (mV) and amp env (V), C13 (nF), the octave width - 1, the PW
// shift (-1..1), the BA662's clicks and noise (0..1).
struct PhosCalibration
{
    const char* name;
    float tm3, c21, fenv, aenv, c13, tm5, pw;
    bool clicks;
    float noise;
};
const std::array<PhosCalibration, 9>& phosCalibrations() noexcept;

// Velocity that makes an accent, as in the original (64 of 127).
constexpr float kPhosAccentVelocity = 64.f / 127.f;

// Per-chunk modulation from the matrix and the controllers.
struct PhosMods
{
    float cutoff = 0.f, reso = 0.f, envmod = 0.f, decay = 0.f, accent = 0.f;   // knob units
    float pitch = 0.f;        // semitones (bend, matrix)
    float wheel = 0.f;        // 0..1, drives the vibrato
    float level = 1.f;        // linear gain
};

// One TB-303 voice: mono, glides between legato notes, accents from the note.
class PhosVoice
{
public:
    void prepare (float baseRate, float osRate);
    void setRates (float baseRate, float osRate);

    void trigger (int note, bool accent, const PhosSettings& s) noexcept;   // a new note: envelopes restart
    void slideTo (int note, bool accent) noexcept;                          // a tie: the pitch glides
    void release() noexcept;
    void fadeOut() noexcept;   // ~3 ms, then idle
    void kill() noexcept;

    bool isActive() const noexcept { return active; }
    bool isGateOn() const noexcept { return gate; }
    int getNote() const noexcept { return note; }
    float level() const noexcept { return (float) vca * fade; }

    // What the editor and the matrix see.
    float filterEnv() const noexcept { return (float) meg; }
    float ampEnv() const noexcept { return (float) vca; }
    float cutoffHz() const noexcept { return lastCutoffHz; }

    // Adds nBase * osFactor oversampled samples (outR may be null).
    // `record` (when given) gets the amp and filter envelope at every base-rate
    // sample, for the effects' modulation.
    using SourceArray = std::array<float, (size_t) ModSrc::Count>;
    void render (float* outL, float* outR, int chunkOffsetBase, int nBase, const PhosSettings& s, const PhosMods& m,
                 SourceArray* record = nullptr) noexcept;

    // For the editor and the voice alike: the filter's cutoff (log2 Hz) for
    // the knobs, the envelope and the accent sweep; the envelope's decay;
    // the ladder's response (dB) at a frequency for a cutoff and resonance.
    static float cutoffOctaves (const PhosSettings&, const PhosMods&, float meg, float sweep, const PhosModel&) noexcept;
    static float decayMs (float decay, const PhosModel& = PhosModel::standard()) noexcept;
    static float responseDb (float cutoffHz, float reso, float hz, const PhosModel& = PhosModel::standard()) noexcept;

private:
    const PhosModel* model = &PhosModel::standard();
    float baseRate = 48000.f, osRate = 96000.f;
    int osFactor = 2;

    dsp::PhosOsc osc;
    dsp::HighPass2 inHp;
    dsp::PhosLadder ladder;
    dsp::TptOnePole outHp;
    dsp::Rng rng { 303 };

    bool active = false, gate = false, accented = false, fading = false;
    int note = 60;
    double pitch = 60.0, target = 60.0;
    double meg = 0.0, sweep = 0.0, vca = 0.0;
    bool megAttack = false, vcaRising = false;
    double vibPhase = 0.0;
    float fade = 1.f, fadeStep = 0.f;
    float lastCutoffHz = 1000.f;
    int ctl = 0;   // samples until the next control-rate update

    // control-rate state
    float g = 0.f, k = 0.f;
    double megDecay = 1.0, megAtt = 0.0, sweepUp = 0.0, sweepDown = 1.0, slideCoef = 0.0;
    double vcaUp = 0.0, vcaDown = 1.0, vcaRel = 1.0, vcaTailRel = 1.0;
    double vcaTail = 0.0;     // the slow part of a released note
    double accEnv = 0.0, accRel = 1.0;   // the accent's own gate on the VCA (it lets go more slowly)
    // declicking, as the original's onsets: the envelope as the accent's VCA
    // path sees it (a fresh note's jump to full takes ~0.3 ms), the accent
    // switching on and off over 0.3 / 3 ms, and the summed gain through a
    // 0.1 ms pole, so a note never starts or changes level within a sample
    double megAmp = 0.0, accOn = 0.0, ampNow = 0.0;
    bool wasGate = false;
    float accentAmt = 0.f;
    float levelNow = 1.f;      // the Amp modulation's gain, smoothed per sample
    bool levelFresh = true;

    void updateControl (const PhosSettings& s, const PhosMods& m) noexcept;
};

} // namespace psy
