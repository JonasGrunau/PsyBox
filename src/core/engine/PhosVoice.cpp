#include "PhosVoice.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace psy
{

using namespace dsp;

namespace
{
    constexpr int kControl = 8;              // oversampled samples per control-rate update
    constexpr float kFadeMs = 3.f;
    constexpr double kQuiet = 1e-5;

    // Linear interpolation in an evenly spaced table over 0..1.
    template <size_t N>
    float lookup (const std::array<float, N>& t, float x) noexcept
    {
        const float pos = std::clamp (x, 0.f, 1.f) * (float) (N - 1);
        const auto i = std::min ((size_t) pos, N - 2);
        const float f = pos - (float) i;
        return t[i] + (t[i + 1] - t[i]) * f;
    }

    double perSample (double ms, double rate) noexcept { return std::exp (-1000.0 / (std::max (ms, 1e-3) * rate)); }
} // namespace

const PhosModel& PhosModel::standard() noexcept
{
    static const PhosModel m;
    return m;
}

const std::array<PhosCalibration, 9>& phosCalibrations() noexcept
{
    static constexpr std::array<PhosCalibration, 9> c { {
        { "Ideal TB303", 0.f, 15.9f, 1951.f, 1.0984f, 926.f, 0.f, 0.005f, false, 0.f },
        { "Long Accent Tail", -72.f, 10.83f, 3567.f, 2.45f, 1362.f, 0.f, 0.f, true, 0.136f },
        { "Lower Cutoff", -76.f, 15.9f, 1951.f, 1.0984f, 937.6f, 0.f, 0.005f, false, 0.f },
        { "More Expressive", 0.f, 12.09f, 1999.f, 1.4944f, 926.f, -0.002f, 0.061f, false, 0.f },
        { "Noisy Old One", -60.f, 11.31f, 2063.f, 1.1f, 915.4f, 0.f, 0.f, true, 0.208f },
        { "Plain TB303 with Noisy VCA", 0.f, 15.9f, 1951.f, 1.0984f, 926.f, 0.f, 0.005f, true, 0.308f },
        { "Plain TB303", 0.f, 15.9f, 1951.f, 1.0984f, 926.f, 0.f, 0.005f, true, 0.f },
        { "Ramon's TB", -40.f, 20.51f, 2127.f, 1.8824f, 973.3f, 0.f, 0.005f, true, 0.076f },
        { "Sub-bass Monster", -372.f, 36.45f, 3344.f, 1.408f, 915.4f, 0.002f, 0.016f, false, 0.f },
    } };
    return c;
}

PhosSettings PhosSettings::from (const ParamState& p) noexcept
{
    PhosSettings s;
    s.square = p.geti (P::PhosWave) == 1;
    s.tune = p.get (P::PhosTune);
    s.cutoff = p.get (P::PhosCutoff);
    s.reso = p.get (P::PhosReso);
    s.envmod = p.get (P::PhosEnvMod);
    s.decay = p.get (P::PhosDecay);
    s.accent = p.get (P::PhosAccent);
    s.slide = p.get (P::PhosSlide);
    s.sweep = p.get (P::PhosSweep);
    s.envAttack = p.get (P::PhosEnvAttack);
    s.accDecay = p.get (P::PhosAccDecay);
    s.accVolume = p.get (P::PhosAccVolume);
    s.vibSpeed = p.get (P::PhosVibSpeed);
    s.vibDepth = p.get (P::PhosVibDepth);
    s.resoSweepCouple = p.on (P::PhosResoCouple);
    s.accVolumeCouple = p.on (P::PhosAccCouple);
    s.tm3mV = p.get (P::PhosTm3);
    s.c21nF = p.get (P::PhosC21);
    s.accFilterEnvmV = p.get (P::PhosAccFEnv);
    s.accAmpEnvV = p.get (P::PhosAccAEnv);
    s.c13nF = p.get (P::PhosC13);
    s.tm5 = 1.f + p.get (P::PhosTm5);
    s.pwShift = p.get (P::PhosPwShift);
    s.clicks = p.on (P::PhosClicks);
    s.noise = p.get (P::PhosNoise);
    return s;
}

void PhosVoice::prepare (float base, float os)
{
    setRates (base, os);
    kill();
}

void PhosVoice::setRates (float base, float os)
{
    baseRate = base;
    osRate = os;
    osFactor = std::max (1, (int) std::lround (os / base));
    ctl = 0;
}

void PhosVoice::trigger (int n, bool accent, const PhosSettings& s) noexcept
{
    if (! active || fading)
    {
        // a fresh start: filters and envelopes from rest, the oscillator free-running
        inHp.reset();
        ladder.reset();
        outHp.reset();
        osc.reset (rng.uniform());
        meg = sweep = vca = vcaTail = accEnv = 0.0;
        pitch = (double) n;
        vibPhase = 0.0;
    }
    vcaTail = 0.0;
    note = n;
    target = (double) n;
    pitch = (double) n;
    accented = accent;
    gate = true;
    active = true;
    fading = false;
    fade = 1.f;
    megAttack = s.envAttack > 0.f;
    if (! megAttack)
        meg = 1.0;
    vcaRising = true;
    vibPhase = 0.0;
    ctl = 0;
}

void PhosVoice::slideTo (int n, bool accent) noexcept
{
    note = n;
    target = (double) n;
    accented = accent;
    gate = true;
}

void PhosVoice::release() noexcept
{
    gate = false;
}

void PhosVoice::fadeOut() noexcept
{
    if (! active)
        return;
    fading = true;
    gate = false;
    fadeStep = 1.f / (kFadeMs * 0.001f * osRate);
}

void PhosVoice::kill() noexcept
{
    active = gate = fading = false;
    meg = sweep = vca = 0.0;
    fade = 1.f;
    inHp.reset();
    ladder.reset();
    outHp.reset();
    osc.reset();
}

float PhosVoice::decayMs (float decay, const PhosModel& M) noexcept
{
    return std::exp (M.decayA + M.decayB * decay + M.decayC * decay * decay);
}

float PhosVoice::cutoffOctaves (const PhosSettings& s, const PhosMods& mods, float meg, float sweep, const PhosModel& M) noexcept
{
    const float cut = std::clamp (s.cutoff + mods.cutoff, 0.f, 1.f);
    const float reso = std::clamp (s.reso + mods.reso, 0.f, 1.f);
    const float envmod = std::clamp (s.envmod + mods.envmod, 0.f, 1.f);
    // knob + trims + envelope + accent sweep, then the resonance's shift
    const float depth = M.envDepth0 + M.envDepth1 * std::exp2 ((envmod - 1.f) / M.envTaper);
    const float depth0 = M.envDepth0 + M.envDepth1 * std::exp2 (-1.f / M.envTaper);
    float cv = lookup (M.cutoffOct, cut) + s.tm3mV * 0.001f * M.tm3OctPerV
             - M.envOffset * (depth - depth0) + depth * meg + M.sweepOct * s.sweep * sweep;
    const float lo = lookup (M.resoShiftLo, reso), hi = lookup (M.resoShiftHi, reso);
    return cv + std::max (0.f, lo + (hi - lo) * (cv - 7.63f) / (10.12f - 7.63f));
}

float PhosVoice::responseDb (float cutoffHz, float reso, float hz, const PhosModel& M) noexcept
{
    using C = std::complex<double>;
    const C s (0.0, 2.0 * 3.141592653589793 * hz);
    const C p = s / (2.0 * 3.141592653589793 * std::max (cutoffHz, 1.f));
    const double m = M.ladderM, k = lookup (M.resoK, std::clamp (reso, 0.f, 1.f));
    const C a = m * p * p * p * p + (6 * m + 1) * p * p * p + (10 * m + 5) * p * p + (4 * m + 6) * p + 1.0;
    const C fb = s / (s + 2.0 * 3.141592653589793 * (double) M.fbHpHz);
    const double w1 = 2.0 * 3.141592653589793 * M.inHpHz, w2 = 2.0 * 3.141592653589793 * M.outHpHz;
    const C h = s * s / (s * s + w1 / (double) M.inHpQ * s + w1 * w1) * (s / (s + w2)) / (a + k * fb);
    return (float) (20.0 * std::log10 (std::max (std::abs (h), 1e-9)));
}

void PhosVoice::updateControl (const PhosSettings& s, const PhosMods& mods, int samples) noexcept
{
    const auto& M = *model;
    const double rate = osRate;
    const float reso = std::clamp (s.reso + mods.reso, 0.f, 1.f);
    const float decay = std::clamp (s.decay + mods.decay, 0.f, 1.f);
    const float acc = std::clamp (s.accent + mods.accent, 0.f, 1.f);

    // envelope times
    const double accMs = M.accDecayMs + (M.accDecayMaxMs - M.accDecayMs) * s.accDecay * s.accDecay;
    megDecay = perSample (accented ? accMs : decayMs (decay, M), rate);
    megAtt = 1.0 - perSample (std::max (0.05, (double) s.envAttack * s.envAttack * M.envAttackMaxMs), rate);

    // the accent sweep capacitor (C13): charge time from the resonance (or Sweep Amount)
    const double c13 = s.c13nF / 926.0;
    const float chargeBy = s.resoSweepCouple ? reso : s.sweep;
    sweepUp = 1.0 - perSample ((M.sweepChargeMs0 + (M.sweepChargeMs1 - M.sweepChargeMs0) * chargeBy) * c13, rate);
    sweepDown = perSample (M.sweepDischargeMs * c13, rate);
    accentAmt = acc * s.accFilterEnvmV / 1951.f;

    const float cv = cutoffOctaves (s, mods, (float) meg, (float) sweep, M);
    const float fc = std::clamp (std::exp2 (cv), 10.f, 0.45f * osRate);
    lastCutoffHz = fc;
    g = prewarp (fc, osRate);
    k = lookup (M.resoK, reso);
    const float fbHz = M.fbHpHz * 15.9f / std::max (s.c21nF, 0.1f);
    ladder.set (g, k, M.ladderM, prewarp (fbHz, osRate), M.drive);

    slideCoef = 1.0 - perSample (M.slideMs * std::exp2 ((s.slide - 1.0) / M.slideTaper), rate);
    vcaUp = 1.0 - perSample (M.vcaAttackMs, rate);
    vcaDown = perSample (M.vcaDecayMs, rate);
    vcaRel = perSample (M.vcaReleaseMs, rate);
    vcaTailRel = perSample (M.vcaTailMs, rate);
    accRel = perSample (M.accReleaseMs, rate);
    (void) samples;
}

void PhosVoice::render (float* outL, float* outR, int chunkOffsetBase, int nBase, const PhosSettings& s, const PhosMods& mods) noexcept
{
    if (! active)
        return;
    const auto& M = *model;
    const int offset = chunkOffsetBase * osFactor;
    const int n = nBase * osFactor;
    const double rate = osRate;

    inHp.set (M.inHpHz, M.inHpQ, osRate);
    outHp.setG (prewarp (M.outHpHz, osRate));
    const double sagPerSample = perSample (M.sqSagMs, rate);
    const float gain = dbToGain (M.gainDb) * mods.level;
    const float accVol = s.accVolumeCouple ? std::clamp (s.accent + mods.accent, 0.f, 1.f) : s.accVolume;
    const float accAmp = M.accAmp * accVol * s.accAmpEnvV / 1.0984f;
    const double vibInc = (M.vibHz0 + (M.vibHz1 - M.vibHz0) * s.vibSpeed) / rate;
    const double vibDepth = (double) s.vibDepth * M.vibSemis * mods.wheel;
    const double width = s.tm5;
    const double pwDuty = M.sqDuty + M.sqPwShift * s.pwShift;

    for (int i = 0; i < n; ++i)
    {
        if (--ctl <= 0)
        {
            updateControl (s, mods, kControl);
            ctl = kControl;
        }

        // envelopes
        if (megAttack)
        {
            meg += (1.2 - meg) * megAtt;
            if (meg >= 1.0) { meg = 1.0; megAttack = false; }
        }
        else
            meg *= megDecay;
        const double in = accented && gate ? accentAmt * meg : 0.0;
        sweep = in > sweep ? sweep + (in - sweep) * sweepUp : sweep * sweepDown;
        // the VCA: charges up at the attack, then decays while the gate is held;
        // let go, it falls fast to a share of its level and rings on from there
        if (! gate)
        {
            if (wasGate)
                vcaTail = vca * M.vcaTailLevel;
            const double head = (vca - vcaTail) * vcaRel;
            vcaTail *= vcaTailRel;
            vca = head + vcaTail;
        }
        else if (vcaRising)
        {
            vca += (1.05 - vca) * vcaUp;
            if (vca >= 1.0) { vca = 1.0; vcaRising = false; }
        }
        else
            vca *= vcaDown;
        wasGate = gate;

        // pitch
        pitch += (target - pitch) * slideCoef;
        double semis = pitch + s.tune + mods.pitch;
        if (vibDepth != 0.0)
        {
            vibPhase += vibInc;
            vibPhase -= std::floor (vibPhase);
            semis += vibDepth * std::sin (kTwoPi * vibPhase);
        }
        semis = M.tm5RefNote + (semis - M.tm5RefNote) * width;
        const double hz = 440.0 * std::exp2 ((semis - 69.0) / 12.0);
        const double inc = hz / rate;
        const double duty = pwDuty + M.sqDelayMs * 0.001 * hz;

        float x = osc.process (inc, s.square, duty, sagPerSample, M.sqTop, M.sqLow);
        x = inHp.process (x);
        float y = ladder.process (x);
        // the accent's lift reaches the VCA by its own path, with its own gate
        // that lets go more slowly than the note's
        if (gate) accEnv += (1.0 - accEnv) * vcaUp;
        else accEnv *= accRel;
        // the VCA, then the output coupling: a note cut off rings out through it
        const double amp = vca + (accented ? accAmp * meg * accEnv : 0.0);
        float out = outHp.hp (y * (float) amp) * gain;
        if (s.noise > 0.f)
            out += rng.bipolar() * M.noiseLevel * s.noise * (float) vca;
        if (fading)
        {
            fade -= fadeStep;
            if (fade <= 0.f)
            {
                kill();
                return;
            }
            out *= fade;
        }
        outL[offset + i] += out;
        if (outR != nullptr)
            outR[offset + i] += out;
    }

    if (! gate && vca < kQuiet)
        kill();
    meg = flushDenormal ((float) meg);
    sweep = flushDenormal ((float) sweep);
}

} // namespace psy
