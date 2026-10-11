#include "Voice.h"

namespace psy
{

using namespace dsp;

void Voice::prepare (float baseRate, float maxOsRate)
{
    f1.prepare (maxOsRate);
    f2.prepare (maxOsRate);
    f1R.prepare (maxOsRate);
    f2R.prepare (maxOsRate);
    setRates (baseRate, maxOsRate);
    for (auto& w : wtA) w.setBank (&WavetableBank::shared());
    for (auto& w : wtB) w.setBank (&WavetableBank::shared());
}

void Voice::setRates (float baseRate, float osRate)
{
    amp.setSampleRate (baseRate);
    fenv.setSampleRate (baseRate);
    menv.setSampleRate (baseRate);
    penv.setSampleRate (baseRate);
    for (auto& l : lfos)
        l.setSampleRate (baseRate);
    levelSlew = 0.5f / (0.001f * baseRate);   // 0.5 is a full note's level (see the gain in render)
    levelCoef = 1.f - std::exp (-1.f / (0.0001f * baseRate));
    onsetInc = 1.f / (0.001f * baseRate);
    outFadeRate = 1.f / (0.003f * baseRate);
    cutFall = 0.5f / (0.001f * baseRate);   // octaves a sample: half an octave a millisecond
    f1.setRate (osRate);
    f2.setRate (osRate);
    f1R.setRate (osRate);
    f2R.setRate (osRate);
    noise.setRate (osRate);
    kill();
}

void Voice::start (int n, float vel, bool acc, bool retrigger, bool stolen, float glideFrom, const VoiceContext& ctx,
                   uint64_t seed)
{
    const auto& p = *ctx.p;
    if (outFadeStep > 0.f && outFade <= 0.f)
        kill();   // faded out with its envelopes still running: start from silence
    rng.reseed (seed);
    note = n;
    velocity = vel;
    accent = acc;
    gate = true;
    outFade = 1.f;
    outFadeStep = 0.f;
    randomVal = rng.bipolar();
    pitchTarget = (float) n;
    gliding = glideFrom >= 0.f;
    pitchNow = gliding ? glideFrom : pitchTarget;

    if (retrigger)
    {
        sweepT = 0.0;
        amp.noteOn();
        fenv.noteOn();
        menv.noteOn();
        penv.trigger();
        for (int i = 0; i < kNumLfos; ++i)
            if (p.on (lfoParam (i, 4)))
                lfos[(size_t) i].reset (p.get (lfoParam (i, 5)));
        if (! stolen)
        {
            level1 = level2 = 0.f;
            cutFresh = true;
            for (auto& l : lfos) l.snap();
        }

        // Resetting phase on a voice that is still sounding would click.
        const bool quiet = ! stolen || level2 < kQuiet;
        if (quiet)
        {
            // the unison voices draw their phases from their own generator,
            // so the first voice and everything after it start as before
            Rng phases (seed ^ 0x756E69736F6EULL);
            onset = 0.f;
            if (p.on (P::OscARetrig)) syncPhase = 0.0;
            resetOscs (0, p.on (P::OscARetrig), phases);
            resetOscs (1, p.on (P::OscBRetrig), phases);
            sub.reset();
            ssA.reset (rng);
            ssB.reset (rng);
            if (! stolen)
            {
                f1.reset();
                f2.reset();
                f1R.reset();
                f2R.reset();
            }
        }
        noise.reset (seed * 7919u + 17u);
        for (int i = 0; i < kNumLfos; ++i)
            lfos[(size_t) i].seed (seed + (uint64_t) i * 101u);
    }
}

void Voice::resetOscs (int which, bool retrig, Rng& phases) noexcept
{
    auto& an = which == 0 ? anA : anB;
    auto& wt = which == 0 ? wtA : wtB;
    auto& fm = which == 0 ? fmA : fmB;
    const double first = retrig ? 0.0 : rng.uniform();
    for (int k = 0; k < kMaxUnison; ++k)
    {
        const double ph = k == 0 ? first : (retrig ? UnisonLayout::retrigPhase (k) : phases.uniform());
        an[(size_t) k].reset (ph);
        wt[(size_t) k].reset (ph);
        fm[(size_t) k].reset (ph);
    }
}

bool Voice::Unison::update (int count, float det, float rng, int md, int stk, float bl, float wd, bool st) noexcept
{
    if (count == n && det == detune && rng == range && md == mode && stk == stack && bl == blend && wd == width && st == stereo)
        return false;
    n = count; detune = det; range = rng; mode = md; stack = stk; blend = bl; width = wd; stereo = st;
    layout.compute (count, det, rng, md, stk, bl, wd);
    for (int k = 0; k < layout.count; ++k)
    {
        const auto i = (size_t) k;
        ratio[i] = std::exp2 (layout.semis[i] / 12.f);
        const float a = (layout.pan[i] + 1.f) * kPi * 0.25f;
        gL[i] = st ? layout.gain[i] * std::cos (a) * 1.41421356f : layout.gain[i];
        gR[i] = st ? layout.gain[i] * std::sin (a) * 1.41421356f : layout.gain[i];
    }
    return true;
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

void Voice::fadeOut() noexcept
{
    gate = false;
    outFadeStep = outFadeRate;
}

void Voice::kill()
{
    gate = false;
    amp.kill();
    fenv.kill();
    menv.kill();
    level1 = level2 = 0.f;
    outFade = 1.f;
    outFadeStep = 0.f;
    f1.reset();
    f2.reset();
    f1R.reset();
    f2R.reset();
}

float Voice::oscSample (int which, int k, int type, double inc, float shape, float pw, float detune, int table, float wtPos,
                        float fmRatio, float fmIndex, float fmFb, const SyncInfo* sync, float xmodPm, float hl) noexcept
{
    AnalogOsc& an = (which == 0 ? anA : anB)[(size_t) k];
    Supersaw& ss = which == 0 ? ssA : ssB;
    WavetableOsc& wt = (which == 0 ? wtA : wtB)[(size_t) k];
    FmOsc& fm = (which == 0 ? fmA : fmB)[(size_t) k];

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
    amp.setAttackCurve (p.get (P::AenvCurve));
    amp.setAttackStart (p.get (P::AenvStart));
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

    // Unison: the supersaw already is seven voices. A spread in stereo runs
    // the filters twice; the right channel starts from the left's state.
    const int nA = tA == (int) OscType::Supersaw ? 1 : p.geti (P::OscAUnison);
    const int nB = tB == (int) OscType::Supersaw ? 1 : p.geti (P::OscBUnison);
    const bool wide = (onA && nA > 1 && p.get (P::OscAUniWidth) > 0.f) || (onB && nB > 1 && p.get (P::OscBUniWidth) > 0.f);
    if (wide && ! stereo) { f1R = f1; f2R = f2; }
    stereo = wide;
    f1R.setType (f1Type);
    f2R.setType (f2Type);
    const float spreadA = p.get (P::OscAUniSpread), spreadB = p.get (P::OscBUniSpread);
    const int tableA = p.geti (P::OscAWt), tableB = p.geti (P::OscBWt);
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
        if (! isActive())
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
        const float sweep = sweepValue ((float) sweepT, ctx.sweepDelay, ctx.sweepExp);
        src[(size_t) ModSrc::Sweep] = sweep;
        if (sweepT < 1.0)
        {
            sweepT += ctx.sweepInc;
            if (sweepT >= 1.0)
            {
                sweepT = 1.0;
                if (ctx.sweepCut && gate) release();   // the note ends at the peak, however long it is held
            }
        }

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
        if (nA > 1)
            uniA.update (nA, detA, p.get (P::OscAUniRange), p.geti (P::OscAUniMode), p.geti (P::OscAUniStack),
                         p.get (P::OscABlend), p.get (P::OscAUniWidth), stereo);
        if (nB > 1)
            uniB.update (nB, detB, p.get (P::OscBUniRange), p.geti (P::OscBUniMode), p.geti (P::OscBUniStack),
                         p.get (P::OscBBlend), p.get (P::OscBUniWidth), stereo);

        // Filters: cutoff in octaves around the knob value. It opens as fast
        // as it is told but closes at most cutFall octaves a sample, from no
        // higher than 20 kHz: a deep envelope that snaps shut in a couple of
        // milliseconds leaves only a click of brightness.
        const auto closing = [this] (int slot, float oct, float knobHz) {
            auto& s = cutOct[(size_t) slot];
            oct = std::min (oct, std::log2 (20000.f / knobHz));
            s = cutFresh ? oct : std::max (oct, s - cutFall);
            return s;
        };
        const float envAmt1 = p.get (P::F1Env) + M (ModDst::FilterEnvAmt);
        const float keyOct = ((float) note - 60.f) / 12.f;
        const float accentEnv = 1.f + 0.8f * accentAmt;
        if (f1Type != 0)
        {
            const float oct = closing (0, envAmt1 * 8.f * fEnv * accentEnv + p.get (P::F1Key) * keyOct + M (ModDst::F1Cutoff) * 8.f,
                                       p.get (P::F1Cutoff));
            const FilterNow f { p.get (P::F1Cutoff) * std::exp2 (oct), clamp01 (p.get (P::F1Reso) + M (ModDst::F1Reso)),
                                clamp01 (p.get (P::F1Vowel) + M (ModDst::F1Vowel)) };
            const float drive = clamp01 (p.get (P::F1Drive) + M (ModDst::F1Drive));
            f1.update (f.cutoff, f.reso, drive, f.vowel);
            if (stereo) f1R.update (f.cutoff, f.reso, drive, f.vowel);
            filtNow[0] = f;
        }
        if (f2Type != 0)
        {
            const float oct = closing (1, (p.get (P::F2Env) + M (ModDst::F2EnvAmt)) * 8.f * fEnv * accentEnv + p.get (P::F2Key) * keyOct
                                              + M (ModDst::F2Cutoff) * 8.f,
                                       p.get (P::F2Cutoff));
            const FilterNow f { p.get (P::F2Cutoff) * std::exp2 (oct), clamp01 (p.get (P::F2Reso) + M (ModDst::F2Reso)),
                                clamp01 (p.get (P::F1Vowel) + M (ModDst::F1Vowel)) };
            const float drive = clamp01 (p.get (P::F2Drive) + M (ModDst::F2Drive));
            f2.update (f.cutoff, f.reso, drive, f.vowel);
            if (stereo) f2R.update (f.cutoff, f.reso, drive, f.vowel);
            filtNow[1] = f;
        }

        cutFresh = false;

        // the level can follow the energy sweep: up (fade in) or down (fade out)
        const float fade = ctx.sweepFade >= 0.f ? 1.f - ctx.sweepFade * (1.f - sweep) : 1.f + ctx.sweepFade * sweep;
        if (outFadeStep > 0.f)
            outFade = std::max (0.f, outFade - outFadeStep);
        const float gain = aEnv * velGain * accentGain * std::clamp (1.f + M (ModDst::Amp), 0.f, 2.f) * fade * 0.5f * outFade;
        level1 += std::clamp (gain - level1, -levelSlew, levelSlew);
        level2 += levelCoef * (level1 - level2);
        const float pan = std::clamp (M (ModDst::Pan), -1.f, 1.f);
        const float gL = std::cos ((pan + 1.f) * kPi * 0.25f) * 1.41421356f * level2;
        const float gR = std::sin ((pan + 1.f) * kPi * 0.25f) * 1.41421356f * level2;
        const float mixGain = 0.5f * onset;
        onset = std::min (1.f, onset + onsetInc);

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

            // Each unison voice is the oscillator at its own pitch (and, with
            // Spread, its own shape or wavetable position), summed per side.
            float aL = 0.f, aR = 0.f, bL = 0.f, bR = 0.f;
            if (needA)
            {
                if (nA == 1)
                    aL = aR = oscSample (0, 0, tA, incA, shapeA, pwA, detA, tableA, wtA_, p.get (P::OscAFmRatio), fmIdxA,
                                         p.get (P::OscAFmFb), nullptr, 0.f, ctx.harmonicLimit);
                else
                    for (int k = 0; k < nA; ++k)
                    {
                        const auto i = (size_t) k;
                        const float u = uniA.layout.pos[i] * spreadA;
                        const float v = oscSample (0, k, tA, incA * uniA.ratio[i], shapeA + u * 1.5f, pwA, detA, tableA,
                                                   clamp01 (wtA_ + u * 0.5f), p.get (P::OscAFmRatio), fmIdxA * (1.f + u * 0.5f),
                                                   p.get (P::OscAFmFb), nullptr, 0.f, ctx.harmonicLimit);
                        aL += v * uniA.gL[i];
                        aR += v * uniA.gR[i];
                    }
            }
            const float a = 0.5f * (aL + aR);   // what modulates B
            if (needB)
            {
                const SyncInfo* syncB = p.on (P::OscBSync) ? &sync : nullptr;
                if (nB == 1)
                    bL = bR = oscSample (1, 0, tB, incB, shapeB, pwB, detB, tableB, wtB_, p.get (P::OscBFmRatio), fmIdxB,
                                         p.get (P::OscBFmFb), syncB, xmod * a, ctx.harmonicLimit);
                else
                    for (int k = 0; k < nB; ++k)
                    {
                        const auto i = (size_t) k;
                        const float u = uniB.layout.pos[i] * spreadB;
                        const float v = oscSample (1, k, tB, incB * uniB.ratio[i], shapeB + u * 1.5f, pwB, detB, tableB,
                                                   clamp01 (wtB_ + u * 0.5f), p.get (P::OscBFmRatio), fmIdxB * (1.f + u * 0.5f),
                                                   p.get (P::OscBFmFb), syncB, xmod * a, ctx.harmonicLimit);
                        bL += v * uniB.gL[i];
                        bR += v * uniB.gR[i];
                    }
            }

            float x = aL * lvlA + bL * lvlB + aL * bL * lvlRing;
            float xR = stereo ? aR * lvlA + bR * lvlB + aR * bR * lvlRing : 0.f;
            if (lvlSub > 0.f)
            {
                const float s = sub.process (incSub, subShape == 0 ? 0.f : (subShape == 1 ? 1.f : 3.f), 0.5f) * lvlSub;
                x += s;
                xR += s;
            }
            float direct = 0.f;
            if (lvlNoise > 0.f)
            {
                const float nz = noise.process (noiseType, incA * 4.0) * lvlNoise;
                if (noiseDirect) direct = nz;
                else { x += nz; xR += nz; }
            }
            x *= mixGain;
            xR *= mixGain;

            float y, yR;
            if (parallel && f2Type != 0)
            {
                y = 0.5f * (f1.process (x) + f2.process (x)) * 1.4f;
                yR = stereo ? 0.5f * (f1R.process (xR) + f2R.process (xR)) * 1.4f : y;
            }
            else
            {
                y = f2.process (f1.process (x));
                yR = stereo ? f2R.process (f1R.process (xR)) : y;
            }
            y += 0.5f * direct;   // same staging as the filtered path
            yR += stereo ? 0.5f * direct : 0.f;

            y = flushDenormal (y);
            yR = flushDenormal (yR);
            outL[base + j] += y * gL;
            outR[base + j] += yR * gR;
        }
        if (n == nBase - 1) srcNow = src;
    }
}

} // namespace psy
