#pragma once

#include "Common.h"

namespace psy::dsp
{

// ---------------------------------------------------------------------------
// ADSR with analogue-style segments: the attack charges towards an overshoot
// target (slightly convex, like an RC charging past its comparator threshold),
// decay and release are true exponentials. Segment times are exact: the
// coefficient is solved so each segment lands on its target at `time`.
// Retriggering continues from the current level, so there are no clicks.
// ---------------------------------------------------------------------------
// Attack shape family: (1 - e^(-k*p)) / (1 - e^(-k)) for phase p in 0..1.
// k = ln(1.3 / 0.3) is the RC charge below, k = 0 a straight line, negative
// k a swell (slow, then fast: in dB close to a straight rise, like a decay
// played backwards). The amp envelope's Curve 0..1 maps onto k = 1.47 .. -8.
inline float attackBend (float curve) noexcept
{
    constexpr float kRc = 1.4663371f;   // ln (1.3 / 0.3)
    return kRc - curve * (kRc + 8.f);
}

inline float attackShape (float phase, float bend) noexcept
{
    phase = std::clamp (phase, 0.f, 1.f);
    if (std::abs (bend) < 1e-3f) return phase;
    return (1.f - std::exp (-bend * phase)) / (1.f - std::exp (-bend));
}

// Inverse of attackShape: where on the curve a level sits.
inline float attackPhase (float level, float bend) noexcept
{
    level = std::clamp (level, 0.f, 1.f);
    if (std::abs (bend) < 1e-3f) return level;
    return -std::log (1.f - level * (1.f - std::exp (-bend))) / bend;
}

class Adsr
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void setSampleRate (float sr) noexcept { fs = sr; }

    // 0 keeps the RC charge; above it the attack bends into a swell (see attackBend).
    void setAttackCurve (float curve) noexcept { aCurve = curve; }

    void set (float attackMs, float decayMs, float sustain, float releaseMs) noexcept
    {
        if (attackMs != aMs)
        {
            aMs = attackMs;
            calc (aMs, kAttackRatio, 1.f + kAttackRatio, aCoef, aBase);
            aInc = 1.f / std::max (aMs * 0.001f * fs, 1.f);
        }
        if (decayMs != dMs || sustain != sus)
        {
            dMs = decayMs;
            sus = sustain;
            calc (dMs, kDecayRatio, 0.f, dCoef, dBase);
            dBase = (sus - kDecayRatio) * (1.f - dCoef);
        }
        if (releaseMs != rMs) { rMs = releaseMs; calc (rMs, kDecayRatio, 0.f, rCoef, rBase); rBase = -kDecayRatio * (1.f - rCoef); }
    }

    void noteOn() noexcept { stage = Stage::Attack; aPhase = -1.f; }
    void noteOff() noexcept { if (stage != Stage::Idle) { stage = Stage::Release; relFrom = out; } }
    void kill() noexcept { stage = Stage::Idle; out = 0.f; }
    bool active() const noexcept { return stage != Stage::Idle; }
    Stage getStage() const noexcept { return stage; }
    float value() const noexcept { return out; }

    // How far into the current stage, 0..1, as a share of the stage's time
    // (sustain: 0). Read from the level, so a retriggered attack starts
    // part of the way in, like it sounds.
    float stageProgress() const noexcept
    {
        constexpr float kLogRatio = -7.6014f;   // ln (kDecayRatio / (1 + kDecayRatio))
        switch (stage)
        {
            case Stage::Attack: return attackPhase (out, attackBend (std::max (aCurve, 0.f)));
            case Stage::Decay:
                if (1.f - sus < 1e-4f) return 1.f;
                return std::clamp (std::log ((out - sus + kDecayRatio) / (1.f - sus + kDecayRatio)) / kLogRatio, 0.f, 1.f);
            case Stage::Release:
                if (relFrom <= 0.f) return 1.f;
                return std::clamp (std::log ((out + kDecayRatio) / (relFrom + kDecayRatio)) / kLogRatio, 0.f, 1.f);
            case Stage::Idle:
            case Stage::Sustain: break;
        }
        return 0.f;
    }

    float process() noexcept
    {
        switch (stage)
        {
            case Stage::Idle: break;
            case Stage::Attack:
                if (aCurve <= 0.f)
                {
                    out = aBase + out * aCoef;
                    if (out >= 1.f || aMs <= 0.05f) { out = 1.f; stage = Stage::Decay; }
                    break;
                }
                // bent attack: walk the curve, starting where the level is (a retrigger)
                {
                    const float bend = attackBend (aCurve);
                    if (aPhase < 0.f) aPhase = attackPhase (out, bend);
                    aPhase += aInc;
                    out = attackShape (aPhase, bend);
                    if (aPhase >= 1.f || aMs <= 0.05f) { out = 1.f; stage = Stage::Decay; }
                }
                break;
            case Stage::Decay:
                out = dBase + out * dCoef;
                if (out <= sus) { out = sus; stage = Stage::Sustain; }
                break;
            case Stage::Sustain:
                out = sus;
                break;
            case Stage::Release:
                out = rBase + out * rCoef;
                if (out <= 1e-5f) { out = 0.f; stage = Stage::Idle; }
                break;
        }
        return out;
    }

private:
    static constexpr float kAttackRatio = 0.3f;
    static constexpr float kDecayRatio = 0.0005f;

    float fs = 48000.f;
    Stage stage = Stage::Idle;
    float out = 0.f;
    float aMs = -1.f, dMs = -1.f, rMs = -1.f, sus = -1.f;
    float aCoef = 0.f, aBase = 0.f, dCoef = 0.f, dBase = 0.f, rCoef = 0.f, rBase = 0.f;
    float aCurve = 0.f, aInc = 1.f, aPhase = -1.f;
    float relFrom = 0.f;   // level when the release began

    void calc (float ms, float ratio, float target, float& coef, float& base) const noexcept
    {
        const float samples = std::max (ms * 0.001f * fs, 1.f);
        coef = std::exp (-std::log ((1.f + ratio) / ratio) / samples);
        base = target * (1.f - coef);
    }
};

// ---------------------------------------------------------------------------
// Attack-decay envelope for pitch sweeps (zaps, lasers, kicks-in-disguise).
// Decay curve: 0 = linear, 1 = steep exponential-like dive.
// ---------------------------------------------------------------------------
class PitchEnv
{
public:
    void setSampleRate (float sr) noexcept { fs = sr; }

    void trigger() noexcept { t = 0.f; stage = 1; }

    float process (float attackMs, float decayMs, float curve) noexcept
    {
        switch (stage)
        {
            case 1:
            {
                const float n = std::max (attackMs * 0.001f * fs, 1.f);
                t += 1.f / n;
                out = std::min (t, 1.f);
                if (t >= 1.f || attackMs <= 0.05f) { stage = 2; t = 0.f; out = 1.f; }
                break;
            }
            case 2:
            {
                const float n = std::max (decayMs * 0.001f * fs, 1.f);
                t += 1.f / n;
                if (t >= 1.f) { stage = 0; out = 0.f; break; }
                out = std::pow (1.f - t, 1.f + curve * 7.f);
                break;
            }
            default: out = 0.f; break;
        }
        return out;
    }

    float value() const noexcept { return out; }
    // 0 done, 1 attack, 2 decay; and how far into the stage, 0..1.
    int getStage() const noexcept { return stage; }
    float stageProgress() const noexcept { return stage == 0 ? 1.f : std::min (t, 1.f); }

private:
    float fs = 48000.f;
    float t = 0.f, out = 0.f;
    int stage = 0;
};

// ---------------------------------------------------------------------------
// LFO, bipolar output in [-1, 1].
// ---------------------------------------------------------------------------
class Lfo
{
public:
    void setSampleRate (float sr) noexcept { fs = sr; }
    void seed (uint64_t s) noexcept { rng.reseed (s); prevRand = rng.bipolar(); nextRand = rng.bipolar(); held = prevRand; }
    void reset (float startPhase) noexcept { p = startPhase - std::floor (startPhase); odd = false; }

    float process (int shape, float hz) noexcept
    {
        p += hz / fs;
        if (p >= 1.f)
        {
            p -= std::floor (p);
            odd = ! odd;
            prevRand = nextRand;
            nextRand = rng.bipolar();
            held = nextRand;
        }
        switch (shape)
        {
            case 0: out = std::sin (kTwoPi * p); break;
            case 1: out = p < 0.25f ? 4.f * p : (p < 0.75f ? 2.f - 4.f * p : 4.f * p - 4.f); break;
            case 2: out = 2.f * p - 1.f; break;
            case 3: out = 1.f - 2.f * p; break;
            case 4: out = p < 0.5f ? 1.f : -1.f; break;
            case 5: out = held; break;
            default:
            {
                const float s = 0.5f - 0.5f * std::cos (kPi * p);  // cosine interpolation
                out = prevRand + (nextRand - prevRand) * s;
                break;
            }
        }
        return out;
    }

    float value() const noexcept { return out; }
    float phase() const noexcept { return p; }
    // Phase over two cycles, 0..2 (displays draw two).
    float phase2() const noexcept { return p + (odd ? 1.f : 0.f); }

private:
    float fs = 48000.f, p = 0.f, out = 0.f;
    bool odd = false;
    Rng rng;
    float prevRand = 0.f, nextRand = 0.f, held = 0.f;
};

} // namespace psy::dsp
