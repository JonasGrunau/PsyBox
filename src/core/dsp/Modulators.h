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
class Adsr
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void setSampleRate (float sr) noexcept { fs = sr; }

    void set (float attackMs, float decayMs, float sustain, float releaseMs) noexcept
    {
        if (attackMs != aMs) { aMs = attackMs; calc (aMs, kAttackRatio, 1.f + kAttackRatio, aCoef, aBase); }
        if (decayMs != dMs || sustain != sus)
        {
            dMs = decayMs;
            sus = sustain;
            calc (dMs, kDecayRatio, 0.f, dCoef, dBase);
            dBase = (sus - kDecayRatio) * (1.f - dCoef);
        }
        if (releaseMs != rMs) { rMs = releaseMs; calc (rMs, kDecayRatio, 0.f, rCoef, rBase); rBase = -kDecayRatio * (1.f - rCoef); }
    }

    void noteOn() noexcept { stage = Stage::Attack; }
    void noteOff() noexcept { if (stage != Stage::Idle) stage = Stage::Release; }
    void kill() noexcept { stage = Stage::Idle; out = 0.f; }
    bool active() const noexcept { return stage != Stage::Idle; }
    Stage getStage() const noexcept { return stage; }
    float value() const noexcept { return out; }

    float process() noexcept
    {
        switch (stage)
        {
            case Stage::Idle: break;
            case Stage::Attack:
                out = aBase + out * aCoef;
                if (out >= 1.f || aMs <= 0.05f) { out = 1.f; stage = Stage::Decay; }
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
    void reset (float startPhase) noexcept { p = startPhase - std::floor (startPhase); }

    float process (int shape, float hz) noexcept
    {
        p += hz / fs;
        if (p >= 1.f)
        {
            p -= std::floor (p);
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

private:
    float fs = 48000.f, p = 0.f, out = 0.f;
    Rng rng;
    float prevRand = 0.f, nextRand = 0.f, held = 0.f;
};

} // namespace psy::dsp
