#include "Voice.h"

namespace psy
{

using namespace dsp;

void Voice::prepare (float baseRate, float maxOsRate)
{
    f1.prepare (maxOsRate);
    f2.prepare (maxOsRate);
    setRates (baseRate, maxOsRate);
    wtA.setBank (&WavetableBank::shared());
    wtB.setBank (&WavetableBank::shared());
}

void Voice::setRates (float baseRate, float osRate)
{
    amp.setSampleRate (baseRate);
    fenv.setSampleRate (baseRate);
    menv.setSampleRate (baseRate);
    penv.setSampleRate (baseRate);
    for (auto& l : lfos)
        l.setSampleRate (baseRate);
    f1.setRate (osRate);
    f2.setRate (osRate);
    kill();
}

void Voice::start (int n, float vel, bool acc, bool retrigger, bool stolen, float glideFrom, const VoiceContext& ctx,
                   uint64_t seed)
{
    const auto& p = *ctx.p;
    rng.reseed (seed);
    note = n;
    velocity = vel;
    accent = acc;
    gate = true;
    randomVal = rng.bipolar();
    pitchTarget = (float) n;
    gliding = glideFrom >= 0.f;
    pitchNow = gliding ? glideFrom : pitchTarget;

    if (retrigger)
    {
        amp.noteOn();
        fenv.noteOn();
        menv.noteOn();
        penv.trigger();
        for (int i = 0; i < kNumLfos; ++i)
            if (p.on (lfoParam (i, 4)))
                lfos[(size_t) i].reset (p.get (lfoParam (i, 5)));

        // Resetting phase on a voice that is still sounding would click.
        const bool quiet = ! stolen || amp.value() < 0.01f;
        if (quiet)
        {
            if (p.on (P::OscARetrig)) { anA.reset(); wtA.reset(); fmA.reset(); syncPhase = 0.0; }
            else { const double ph = rng.uniform(); anA.reset (ph); wtA.reset (ph); fmA.reset (ph); }
            if (p.on (P::OscBRetrig)) { anB.reset(); wtB.reset(); fmB.reset(); }
            else { const double ph = rng.uniform(); anB.reset (ph); wtB.reset (ph); fmB.reset (ph); }
            sub.reset();
            ssA.reset (rng);
            ssB.reset (rng);
            if (! stolen)
            {
                f1.reset();
                f2.reset();
            }
        }
        noise.reset (seed * 7919u + 17u);
        for (int i = 0; i < kNumLfos; ++i)
            lfos[(size_t) i].seed (seed + (uint64_t) i * 101u);
    }
}

void Voice::changeNote (int n, bool acc, bool doGlide)
{
    note = n;
    accent = acc;
    pitchTarget = (float) n;
    gate = true;
    if (! doGlide)
        pitchNow = pitchTarget;
    gliding = doGlide;
}

void Voice::release()
{
    gate = false;
    amp.noteOff();
    fenv.noteOff();
    menv.noteOff();
}

void Voice::kill()
{
    gate = false;
    amp.kill();
    fenv.kill();
    menv.kill();
    f1.reset();
    f2.reset();
}

float Voice::oscSample (int which, int type, double inc, float shape, float pw, float detune, int table, float wtPos,
                        float fmRatio, float fmIndex, float fmFb, const SyncInfo* sync, float xmodPm, float hl) noexcept
{
    AnalogOsc& an = which == 0 ? anA : anB;
    Supersaw& ss = which == 0 ? ssA : ssB;
    WavetableOsc& wt = which == 0 ? wtA : wtB;
    FmOsc& fm = which == 0 ? fmA : fmB;

    // Non-analog types get a plain phase reset on sync (no correction needed:
    // the wavetable and FM outputs are already smooth at the restart point
    // most of the time, and the decimator cleans up what is left).
    switch (type)
    {
        case 1:
            if (xmodPm != 0.f) inc = std::max (0.0, inc * (1.0 + 3.0 * xmodPm));
            return ss.process (inc, detune);
        case 2:
            if (sync != nullptr && sync->wrapped) wt.reset (inc * sync->fraction);
            if (xmodPm != 0.f) inc = std::max (0.0, inc * (1.0 + 3.0 * xmodPm));
            return wt.process (inc, table, wtPos, hl);
        case 3:
            if (sync != nullptr && sync->wrapped) fm.reset (inc * sync->fraction);
            return fm.process (inc, fmRatio, fmIndex, fmFb, xmodPm * 2.f * kPi);
        default:
            if (xmodPm != 0.f) inc = std::max (0.0, inc * (1.0 + 3.0 * xmodPm));
            return an.process (inc, shape, pw, sync);
    }
}

void Voice::render (float* outL, float* outR, int chunkOffsetBase, int nBase, const VoiceContext& ctx) noexcept
{
    const ParamState& p = *ctx.p;
    const int os = ctx.osFactor;
    const float osRate = ctx.osRate;

    amp.set (p.get (P::AenvA), p.get (P::AenvD), p.get (P::AenvS), p.get (P::AenvR));
    const float accentAmt = accent ? p.get (P::Accent) : 0.f;
    // 303-style accent: shorter, harder filter envelope
    fenv.set (p.get (P::FenvA), p.get (P::FenvD) * (1.f - 0.5f * accentAmt), p.get (P::FenvS), p.get (P::FenvR));
    menv.set (p.get (P::MenvA), p.get (P::MenvD), p.get (P::MenvS), p.get (P::MenvR));

    const bool onA = p.on (P::OscAOn), onB = p.on (P::OscBOn);
    const int tA = p.geti (P::OscAType), tB = p.geti (P::OscBType);
    if (tA != typeA) { typeA = tA; ssA.reset (rng); }
    if (tB != typeB) { typeB = tB; ssB.reset (rng); }
    const int f1Type = p.geti (P::F1Type), f2Type = p.geti (P::F2Type);
    f1.setType (f1Type);
    f2.setType (f2Type);
    const bool parallel = p.geti (P::FRouting) == 1;
    const int penvDest = p.geti (P::PenvDest);
    const int noiseType = p.geti (P::NoiseType);
    const bool noiseDirect = p.on (P::NoiseDirect);   // added after the filters instead of before
    const int subShape = p.geti (P::SubShape);
    const float subOct = p.geti (P::SubOct) == 0 ? -12.f : -24.f;
    const float velGain = 1.f - p.get (P::VelSens) * (1.f - velocity);
    const float accentGain = 1.f + 0.7f * accentAmt;
    const float basePitchOffset = 12.f * (float) p.geti (P::Octave);
    const float bend = ctx.pitchBend * (float) p.geti (P::BendRange);
    const float glideMs = p.get (P::Glide);
    const float glideCoef = glideMs <= 0.5f ? 0.f : std::exp (-1.f / (glideMs * 0.001f * ctx.baseRate / 3.f));

    std::array<float, (size_t) ModSrc::Count> src {};
    std::array<float, (size_t) ModDst::Count> mod {};

    for (int n = 0; n < nBase; ++n)
    {
        if (! amp.active())
            break;

        // ---- control rate -------------------------------------------------
        const float aEnv = amp.process();
        const float fEnv = fenv.process();
        const float mEnv = menv.process();
        const float pEnv = penv.process (p.get (P::PenvAttack), p.get (P::PenvDecay), p.get (P::PenvCurve));

        std::array<float, kNumLfos> lfoOut;
        for (int i = 0; i < kNumLfos; ++i)
        {
            if (p.on (lfoParam (i, 4)))
            {
                float hz = ctx.lfoHz[(size_t) i];
                const auto rateDst = (size_t) ModDst::Lfo1Rate + (size_t) i;
                if (mod[rateDst] != 0.f) hz *= std::exp2 (mod[rateDst] * 4.f);
                lfoOut[(size_t) i] = lfos[(size_t) i].process (p.geti (lfoParam (i, 0)), hz);
            }
            else
            {
                lfoOut[(size_t) i] = ctx.globalLfo[(size_t) i] != nullptr ? ctx.globalLfo[(size_t) i][chunkOffsetBase + n] : 0.f;
            }
        }

        src[(size_t) ModSrc::AmpEnv] = aEnv;
        src[(size_t) ModSrc::FilterEnv] = fEnv;
        src[(size_t) ModSrc::ModEnv] = mEnv;
        src[(size_t) ModSrc::PitchEnv] = pEnv;
        src[(size_t) ModSrc::Lfo1] = lfoOut[0];
        src[(size_t) ModSrc::Lfo2] = lfoOut[1];
        src[(size_t) ModSrc::Lfo3] = lfoOut[2];
        src[(size_t) ModSrc::Velocity] = velocity;
        src[(size_t) ModSrc::Note] = ((float) note - 60.f) / 60.f;
        src[(size_t) ModSrc::ModWheel] = ctx.modWheel;
        src[(size_t) ModSrc::Aftertouch] = ctx.aftertouch;
        src[(size_t) ModSrc::PitchBend] = ctx.pitchBend;
        src[(size_t) ModSrc::Random] = randomVal;
        src[(size_t) ModSrc::Macro1] = p.get (P::Macro1);
        src[(size_t) ModSrc::Macro2] = p.get (P::Macro2);
        src[(size_t) ModSrc::Macro3] = p.get (P::Macro3);
        src[(size_t) ModSrc::Macro4] = p.get (P::Macro4);
        mod.fill (0.f);
        for (int s = 0; s < ctx.numSlots; ++s)
        {
            const auto& sl = ctx.slots[(size_t) s];
            mod[(size_t) sl.dst] += sl.amt * src[(size_t) sl.src];
        }
        const auto M = [&] (ModDst d) { return mod[(size_t) d]; };

        // glide
        if (gliding && glideCoef > 0.f)
        {
            pitchNow = pitchTarget + glideCoef * (pitchNow - pitchTarget);
            if (std::abs (pitchNow - pitchTarget) < 0.001f) { pitchNow = pitchTarget; gliding = false; }
        }
        else
        {
            pitchNow = pitchTarget;
        }

        const float penvSemis = (p.get (P::PenvAmt) + M (ModDst::PitchEnvAmt) * 60.f) * pEnv;
        const float common = pitchNow + basePitchOffset + bend + M (ModDst::PitchAll) * 24.f;
        const float pitchA = common + (float) p.geti (P::OscACoarse) + p.get (P::OscAFine) * 0.01f
                           + M (ModDst::PitchA) * 24.f + (penvDest != 2 ? penvSemis : 0.f);
        const float pitchB = common + (float) p.geti (P::OscBCoarse) + p.get (P::OscBFine) * 0.01f
                           + M (ModDst::PitchB) * 24.f + (penvDest != 1 ? penvSemis : 0.f);
        const double incA = noteToHz (std::min (pitchA, 150.f)) / osRate;
        const double incB = noteToHz (std::min (pitchB, 150.f)) / osRate;
        const double incSub = noteToHz (std::min (pitchA + subOct, 150.f)) / osRate;

        const float shapeA = p.get (P::OscAShape) + M (ModDst::ShapeA) * 3.f;
        const float shapeB = p.get (P::OscBShape) + M (ModDst::ShapeB) * 3.f;
        const float pwA = p.get (P::OscAPw) + M (ModDst::PwA) * 0.48f;
        const float pwB = p.get (P::OscBPw) + M (ModDst::PwB) * 0.48f;
        const float wtA_ = clamp01 (p.get (P::OscAWtPos) + M (ModDst::WtPosA));
        const float wtB_ = clamp01 (p.get (P::OscBWtPos) + M (ModDst::WtPosB));
        const float fmIdxA = std::max (0.f, p.get (P::OscAFm) + M (ModDst::FmA) * 12.f);
        const float fmIdxB = std::max (0.f, p.get (P::OscBFm) + M (ModDst::FmB) * 12.f);
        const float detA = clamp01 (p.get (P::OscADetune) + M (ModDst::DetuneA));
        const float detB = clamp01 (p.get (P::OscBDetune) + M (ModDst::DetuneB));
        const float xmod = clamp01 (p.get (P::OscBXMod) + M (ModDst::XMod));
        const float lvlA = onA ? clamp01 (p.get (P::OscALevel) + M (ModDst::LevelA)) : 0.f;
        const float lvlB = onB ? clamp01 (p.get (P::OscBLevel) + M (ModDst::LevelB)) : 0.f;
        const float lvlSub = clamp01 (p.get (P::SubLevel) + M (ModDst::SubLevel));
        const float lvlNoise = clamp01 (p.get (P::NoiseLevel) + M (ModDst::NoiseLevel));
        const float lvlRing = clamp01 (p.get (P::RingLevel) + M (ModDst::RingLevel));
        const bool needA = lvlA > 0.f || lvlRing > 0.f || xmod > 0.f;
        const bool needB = lvlB > 0.f || lvlRing > 0.f;

        // Filters: cutoff in octaves around the knob value
        const float envAmt1 = p.get (P::F1Env) + M (ModDst::FilterEnvAmt);
        const float keyOct = ((float) note - 60.f) / 12.f;
        const float accentEnv = 1.f + 0.8f * accentAmt;
        if (f1Type != 0)
        {
            const float oct = envAmt1 * 8.f * fEnv * accentEnv + p.get (P::F1Key) * keyOct + M (ModDst::F1Cutoff) * 8.f;
            f1.update (p.get (P::F1Cutoff) * std::exp2 (oct), clamp01 (p.get (P::F1Reso) + M (ModDst::F1Reso)),
                       clamp01 (p.get (P::F1Drive) + M (ModDst::F1Drive)), clamp01 (p.get (P::F1Vowel) + M (ModDst::F1Vowel)));
        }
        if (f2Type != 0)
        {
            const float oct = (p.get (P::F2Env) + M (ModDst::F2EnvAmt)) * 8.f * fEnv * accentEnv + p.get (P::F2Key) * keyOct + M (ModDst::F2Cutoff) * 8.f;
            f2.update (p.get (P::F2Cutoff) * std::exp2 (oct), clamp01 (p.get (P::F2Reso) + M (ModDst::F2Reso)),
                       clamp01 (p.get (P::F2Drive) + M (ModDst::F2Drive)), clamp01 (p.get (P::F1Vowel) + M (ModDst::F1Vowel)));
        }

        const float gain = aEnv * velGain * accentGain * std::clamp (1.f + M (ModDst::Amp), 0.f, 2.f) * 0.5f;
        const float pan = std::clamp (M (ModDst::Pan), -1.f, 1.f);
        const float gL = std::cos ((pan + 1.f) * kPi * 0.25f) * 1.41421356f * gain;
        const float gR = std::sin ((pan + 1.f) * kPi * 0.25f) * 1.41421356f * gain;

        // ---- audio rate ---------------------------------------------------
        const int base = (chunkOffsetBase + n) * os;
        for (int j = 0; j < os; ++j)
        {
            SyncInfo sync;
            syncPhase += incA;
            if (syncPhase >= 1.0)
            {
                syncPhase -= 1.0;
                sync.wrapped = true;
                sync.fraction = incA > 0.0 ? syncPhase / incA : 0.0;
            }

            float a = 0.f, b = 0.f;
            if (needA)
                a = oscSample (0, tA, incA, shapeA, pwA, detA, p.geti (P::OscAWt), wtA_, p.get (P::OscAFmRatio), fmIdxA,
                               p.get (P::OscAFmFb), nullptr, 0.f, ctx.harmonicLimit);
            if (needB)
                b = oscSample (1, tB, incB, shapeB, pwB, detB, p.geti (P::OscBWt), wtB_, p.get (P::OscBFmRatio), fmIdxB,
                               p.get (P::OscBFmFb), p.on (P::OscBSync) ? &sync : nullptr, xmod * a, ctx.harmonicLimit);

            float x = a * lvlA + b * lvlB + a * b * lvlRing;
            if (lvlSub > 0.f)
                x += sub.process (incSub, subShape == 0 ? 0.f : (subShape == 1 ? 1.f : 3.f), 0.5f) * lvlSub;
            float direct = 0.f;
            if (lvlNoise > 0.f)
            {
                const float nz = noise.process (noiseType, incA * 4.0) * lvlNoise;
                if (noiseDirect) direct = nz;
                else x += nz;
            }
            x *= 0.5f;

            float y;
            if (parallel && f2Type != 0)
                y = 0.5f * (f1.process (x) + f2.process (x)) * 1.4f;
            else
                y = f2.process (f1.process (x));
            y += 0.5f * direct;   // same staging as the filtered path

            y = flushDenormal (y);
            outL[base + j] += y * gL;
            outR[base + j] += y * gR;
        }
        if (n == nBase - 1) srcNow = src;
    }
}

} // namespace psy
