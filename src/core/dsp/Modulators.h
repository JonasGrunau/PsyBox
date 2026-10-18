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
    // Burst: the clap hits before the attack (setBursts).
    enum class Stage { Idle, Attack, Decay, Sustain, Release, Burst };

    // A new rate drops the cached coefficients, so the next set() solves them
    // for it (they depend on the rate; the time in ms must stay the same).
    void setSampleRate (float sr) noexcept
    {
        if (differs (sr, fs))
        {
            aMs = dMs = rMs = -1.f;
            burstMs = -1.f;
        }
        fs = sr;
    }

    // Clap bursts: a note starts with count - 1 short hits gapMs apart, each
    // up in 0.5 ms and down about 30 dB by the next, and its own attack is
    // the last hit (the flutter of a hand clap). 1 is a plain envelope.
    void setBursts (int count, float gapMs) noexcept
    {
        bursts = std::clamp (count, 1, 16);
        if (differs (gapMs, burstMs))
        {
            burstMs = gapMs;
            bRise = std::max (1, (int) std::lround (0.0005f * fs));
            bGap = std::max (bRise + 2, (int) std::lround (gapMs * 0.001f * fs));
            bCoef = std::exp (-3.5f / (float) (bGap - bRise));
        }
    }

    // 0 keeps the RC charge; above it the attack bends into a swell (see attackBend).
    void setAttackCurve (float curve) noexcept { aCurve = curve; }
    // The level the attack starts from (0..1): a note already there, quietly,
    // that then swells (a reversed sample's tail). Reached within 3 ms.
    void setAttackStart (float level) noexcept { aStart = std::clamp (level, 0.f, 0.95f); }

    void set (float attackMs, float decayMs, float sustain, float releaseMs) noexcept
    {
        if (differs (attackMs, aMs))
        {
            aMs = attackMs;
            calc (aMs, kAttackRatio, 1.f + kAttackRatio, aCoef, aBase);
            aInc = 1.f / std::max (aMs * 0.001f * fs, 1.f);
        }
        if (differs (decayMs, dMs) || differs (sustain, sus))
        {
            dMs = decayMs;
            sus = sustain;
            calc (dMs, kDecayRatio, 0.f, dCoef, dBase);
            dBase = (sus - kDecayRatio) * (1.f - dCoef);
        }
        if (differs (releaseMs, rMs)) { rMs = releaseMs; calc (rMs, kDecayRatio, 0.f, rCoef, rBase); rBase = -kDecayRatio * (1.f - rCoef); }
    }

    void noteOn() noexcept
    {
        if (bursts > 1)
        {
            stage = Stage::Burst;
            burstsLeft = bursts - 1;
            bT = 0;
            bFrom = out;   // a retrigger rises from where it is
            return;
        }
        startAttack();
    }
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
            case Stage::Attack: return attackPhase ((out - aStart) / (1.f - aStart), attackBend (std::max (aCurve, 0.f)));
            case Stage::Decay:
                if (1.f - sus < 1e-4f) return 1.f;
                return std::clamp (std::log ((out - sus + kDecayRatio) / (1.f - sus + kDecayRatio)) / kLogRatio, 0.f, 1.f);
            case Stage::Release:
                if (relFrom <= 0.f) return 1.f;
                return std::clamp (std::log ((out + kDecayRatio) / (relFrom + kDecayRatio)) / kLogRatio, 0.f, 1.f);
            case Stage::Burst:
                return std::clamp (((float) (bursts - 1 - burstsLeft) + (float) bT / (float) bGap) / (float) std::max (bursts - 1, 1), 0.f, 1.f);
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
                if (aCurve <= 0.f && aStart <= 0.f)
                {
                    aPhase = -1.f;   // the bent path picks the curve up again from the level
                    out = aBase + out * aCoef;
                    if (out >= 1.f || aMs <= 0.05f) { out = 1.f; stage = Stage::Decay; }
                    break;
                }
                // bent attack: walk the curve from the start level, picking up
                // where the level is (a retrigger)
                {
                    const float bend = attackBend (std::max (aCurve, 0.f));
                    if (aPhase < 0.f) aPhase = attackPhase ((std::max (out, aStart) - aStart) / (1.f - aStart), bend);
                    aPhase += aInc;
                    aRamp = std::min (1.f, aRamp + 1.f / (0.003f * fs));
                    out = aStart * aRamp + (1.f - aStart) * attackShape (aPhase, bend);
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
            case Stage::Burst:
                ++bT;
                out = bT <= bRise ? bFrom + (1.f - bFrom) * (float) bT / (float) bRise : out * bCoef;
                if (bT >= bGap)
                {
                    bT = 0;
                    bFrom = out;
                    if (--burstsLeft <= 0) startAttack();
                }
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
    float aCurve = 0.f, aInc = 1.f, aPhase = -1.f, aStart = 0.f, aRamp = 1.f;
    float relFrom = 0.f;   // level when the release began
    int bursts = 1, burstsLeft = 0, bT = 0, bRise = 24, bGap = 480;
    float burstMs = -1.f, bCoef = 0.99f, bFrom = 0.f;

    void startAttack() noexcept
    {
        stage = Stage::Attack;
        aPhase = -1.f;
        aRamp = aStart > 0.f ? std::min (1.f, out / aStart) : 1.f;   // a retrigger above the start needs no ramp
    }

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
// A drawn LFO shape: 2..16 points along the cycle (the first sits at x = 0,
// the rest are held in order up to x = 1), y in -1..1, and each point's bend
// shapes the segment to the next: 0 is a straight line, towards +1 it starts
// slow and ends fast (t^(2^(6 b))), +1 holds the point's value up to the next
// point (a step); towards -1 fast then slow, -1 jumps to the next value at
// once. After the last point the curve runs on to the end of the cycle, back
// to the first point's value, so 16 held points are 16 steps; a last point at
// x = 1 ends the cycle on its own value instead (what an envelope holds).
// Shared by the LFOs and the editor's drawing.
// ---------------------------------------------------------------------------
struct LfoCurve
{
    static constexpr int kMaxPoints = 16;
    int n = 3;
    std::array<float, kMaxPoints> x { 0.f, 0.5f, 1.f }, y { -1.f, 1.f, -1.f }, e { 1.f, 1.f, 1.f };   // e: exponent; 0 holds, < 0 jumps

    void set (int count, const float* xs, const float* ys, const float* bends) noexcept
    {
        n = std::clamp (count, 2, kMaxPoints);
        for (int k = 0; k < n; ++k)
        {
            const auto i = (size_t) k;
            x[i] = k == 0 ? 0.f : std::clamp (xs[i], x[i - 1], 1.f);
            y[i] = std::clamp (ys[i], -1.f, 1.f);
            e[i] = bendExponent (bends[i]);
        }
    }

    static float bendExponent (float b) noexcept
    {
        if (b >= 0.999f) return 0.f;
        if (b <= -0.999f) return -1.f;
        return std::exp2 (6.f * b);
    }

    // The value at phase 0..1; seg caches the segment between calls (phase
    // runs forward, so the search usually starts where it ended).
    float value (float phase, int& seg) const noexcept
    {
        phase = std::clamp (phase, 0.f, 1.f);
        if (seg < 0 || seg >= n || phase < x[(size_t) seg]) seg = 0;
        while (seg < n - 1 && phase >= x[(size_t) seg + 1]) ++seg;
        const auto i = (size_t) seg;
        const bool closing = seg == n - 1;   // the last point to the end of the cycle
        const float x1 = closing ? 1.f : x[i + 1], y1 = closing ? y[0] : y[i + 1];
        if (closing && x[i] >= 1.f) return y[i];   // a last point at the end: its own value
        const float span = x1 - x[i];
        const float t = span > 1e-6f ? (phase - x[i]) / span : 1.f;
        const float ex = e[i];
        const float s = ex == 0.f ? (t >= 1.f ? 1.f : 0.f) : (ex < 0.f ? (t > 0.f ? 1.f : 0.f) : (ex == 1.f ? t : std::pow (t, ex)));
        return y[i] + (y1 - y[i]) * s;
    }
    float value (float phase) const noexcept { int seg = 0; return value (phase, seg); }
};

// ---------------------------------------------------------------------------
// LFO, bipolar output in [-1, 1]. The square, saws and S&H jump; a 0.35 ms
// one-pole rounds every edge so a jump on the level, a filter or an FM index
// doesn't click. Shape 7 plays a drawn LfoCurve. As an envelope (oneShot) it
// runs once from its start phase and holds the value it ends on.
// ---------------------------------------------------------------------------
class Lfo
{
public:
    void setSampleRate (float sr) noexcept { fs = sr; edge = 1.f - std::exp (-1.f / (0.00035f * sr)); }
    void seed (uint64_t s) noexcept { rng.reseed (s); prevRand = rng.bipolar(); nextRand = rng.bipolar(); held = prevRand; }
    void reset (float startPhase) noexcept { p = (double) startPhase - std::floor ((double) startPhase); odd = false; ended = false; seg = 0; }
    // The next value is taken as it is, without rounding from the last: a new note on a silent voice.
    void snap() noexcept { fresh = true; }

    float process (int shape, float hz, const LfoCurve* curve = nullptr, bool oneShot = false) noexcept
    {
        if (! ended)
            p += (double) hz / (double) fs;   // double: a float phase drifts the rate of a slow LFO
        if (p >= 1.0)
        {
            if (oneShot)
            {
                p = 1.0;   // the envelope's end: held until the next note
                ended = true;
            }
            else
            {
                p -= std::floor (p);
                odd = ! odd;
                prevRand = nextRand;
                nextRand = rng.bipolar();
                held = nextRand;
            }
        }
        else if (! oneShot)
            ended = false;   // switched back from an envelope: runs on
        const float ph = (float) p;
        float raw;
        switch (shape)
        {
            case 0: raw = std::sin (kTwoPi * ph); break;
            case 1: raw = ph < 0.25f ? 4.f * ph : (ph < 0.75f ? 2.f - 4.f * ph : 4.f * ph - 4.f); break;
            case 2: raw = 2.f * ph - 1.f; break;
            case 3: raw = 1.f - 2.f * ph; break;
            case 4: raw = ph < 0.5f ? 1.f : -1.f; break;
            case 5: raw = held; break;
            case 7:
                if (curve != nullptr)
                {
                    raw = curve->value (ph, seg);
                    break;
                }
                [[fallthrough]];
            default:
            {
                const float s = 0.5f - 0.5f * std::cos (kPi * ph);  // cosine interpolation
                raw = prevRand + (nextRand - prevRand) * s;
                break;
            }
        }
        if (fresh) { out = raw; fresh = false; }
        else out += edge * (raw - out);
        return out;
    }

    float value() const noexcept { return out; }
    float phase() const noexcept { return (float) p; }
    // Phase over two cycles, 0..2 (displays draw two).
    float phase2() const noexcept { return (float) p + (odd ? 1.f : 0.f); }
    // An envelope that has run to its end and holds there.
    bool hasEnded() const noexcept { return ended; }

private:
    float fs = 48000.f, out = 0.f, edge = 1.f;
    double p = 0.0;
    bool odd = false, fresh = true, ended = false;
    int seg = 0;
    Rng rng;
    float prevRand = 0.f, nextRand = 0.f, held = 0.f;
};

} // namespace psy::dsp
