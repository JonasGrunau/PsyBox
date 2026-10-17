#include "PhosFx.h"

namespace psy
{

void PhosFx::prepare (float sampleRate, float osRate)
{
    distOs.prepare (osRate);
    distLate.prepare (sampleRate);
    peq.prepare (sampleRate);
    cho.prepare (sampleRate);
    dly.prepare (sampleRate);
    rev.prepare (sampleRate);
    lim.prepare (sampleRate);
}

void PhosFx::setOsRate (float osRate)
{
    distOs.prepare (osRate);
}

void PhosFx::reset() noexcept
{
    distOs.reset();
    distLate.reset();
    peq.reset();
    cho.reset();
    dly.reset();
    rev.reset();
    lim.reset();
}

namespace
{
    void runDistortion (dsp::PhosDistortion& d, float* l, float* r, int n, const ParamState& p) noexcept
    {
        d.process (l, r, n, p.geti (P::PhosDistModel), p.get (P::PhosDistDrive), p.get (P::PhosDistColor),
                   p.get (P::PhosDistDyn), p.geti (P::PhosDistOrder) == 1);
    }
} // namespace

void PhosFx::distortionOs (float* l, float* r, int n, const ParamState& p) noexcept { runDistortion (distOs, l, r, n, p); }
void PhosFx::distortion (float* l, float* r, int n, const ParamState& p) noexcept { runDistortion (distLate, l, r, n, p); }

void PhosFx::eq (float* l, float* r, int n, const ParamState& p) noexcept
{
    std::array<dsp::PhosEq::Band, 4> b {};
    for (int i = 0; i < 4; ++i)
    {
        const int base = (int) P::PhosEqLowGain + i * 3;
        b[(size_t) i] = { p.get ((P) base), p.get ((P) (base + 1)), p.get ((P) (base + 2)) };
    }
    peq.process (l, r, n, b);
}

void PhosFx::chorus (float* l, float* r, int n, const ParamState& p) noexcept
{
    cho.process (l, r, n, p.get (P::PhosChoOffset), p.get (P::PhosChoDepth), p.get (P::PhosChoRate),
                 p.get (P::PhosChoSpatial), p.get (P::PhosChoFat), p.get (P::PhosChoHp), p.get (P::PhosChoOut),
                 p.get (P::PhosChoMix));
}

float PhosFx::delayMs (int steps, int division, int scale, double bpm) noexcept
{
    const double whole = 4.0 * 60000.0 / std::max (bpm, 1.0);
    const double factor = scale == 1 ? 1.5 : scale == 2 ? 2.0 / 3.0 : 1.0;
    return (float) (whole * steps / std::max (division, 1) * factor);
}

void PhosFx::delay (float* l, float* r, int n, double bpm, const ParamState& p) noexcept
{
    float msL, msR;
    if (p.on (P::PhosDlySync))
    {
        msL = delayMs (p.geti (P::PhosDlyLNum), p.geti (P::PhosDlyLDen), p.geti (P::PhosDlyLScale), bpm);
        msR = delayMs (p.geti (P::PhosDlyRNum), p.geti (P::PhosDlyRDen), p.geti (P::PhosDlyRScale), bpm);
    }
    else
        msL = msR = p.get (P::PhosDlyTime) * 1000.f;
    dly.process (l, r, n, msL, msR, p.get (P::PhosDlyFb), p.geti (P::PhosDlyFilter), p.get (P::PhosDlyCut),
                 p.get (P::PhosDlyReso), p.get (P::PhosDlySpread), p.get (P::PhosDlyMix));
}

void PhosFx::reverb (float* l, float* r, int n, const ParamState& p) noexcept
{
    dsp::PhosReverb::Settings s;
    s.preMs = p.get (P::PhosRevPre);
    s.earlySize = p.get (P::PhosRevESize);
    s.earlyDiff = p.get (P::PhosRevEDiff);
    s.lateSize = p.get (P::PhosRevLSize);
    s.lateDiff = p.get (P::PhosRevLDiff);
    s.feedback = p.get (P::PhosRevFb);
    s.blend = p.get (P::PhosRevBlend);
    s.modulation = p.get (P::PhosRevMod);
    s.earlyBright = p.get (P::PhosRevEBright);
    s.lateBright = p.get (P::PhosRevLBright);
    s.mix = p.get (P::PhosRevMix);
    rev.process (l, r, n, s);
}

void PhosFx::limiter (float* l, float* r, int n, const ParamState& p) noexcept
{
    lim.process (l, r, n, p.get (P::PhosLimKnee), p.get (P::PhosLimRatio), p.get (P::PhosLimAttack), p.get (P::PhosLimRelease),
                 p.get (P::PhosLimThresh), p.get (P::PhosLimGain), p.on (P::PhosLimClip));
}

} // namespace psy
