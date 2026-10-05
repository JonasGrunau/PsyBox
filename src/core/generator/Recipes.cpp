// Sound-design recipes, one per category.
//
// A recipe describes the *space* of a category: which modules are involved
// and the ranges that keep a sound recognisably "acid" or "zap". Ranges are
// sampled in each parameter's own curve (log for cutoffs, power for times).
// Every value set here remains a normal, editable parameter afterwards.

#include "Generator.h"

namespace psy
{

namespace
{
    using B = RecipeBuilder;
    using S = ModSrc;
    using D = ModDst;

    constexpr float kSaw = 2.f, kSquare = 3.f, kTri = 1.f, kSine = 0.f;

    // syncDiv indices
    enum Div { D4_1, D2_1, D1_1, D1_2, D1_2D, D1_2T, D1_4, D1_4D, D1_4T, D1_8, D1_8D, D1_8T,
               D1_16, D1_16D, D1_16T, D1_32, D1_32T, D1_64 };

    int ft (FilterType t) { return (int) t; }

    // ---- shared building blocks -------------------------------------------

    void monoVoice (B& b, bool legato, float glideLo, float glideHi)
    {
        b.set (P::VoiceMode, (float) (legato ? VoiceMode::Legato : VoiceMode::Mono));
        b.uni (P::Glide, glideLo, glideHi);
        b.set (P::GlideMode, 1.f);
    }

    void polyVoice (B& b, int lo, int hi)
    {
        b.set (P::VoiceMode, (float) VoiceMode::Poly);
        b.uni (P::PolyVoices, (float) lo, (float) hi);
        b.set (P::Glide, 0.f);
    }

    void ampEnv (B& b, float aLo, float aHi, float dLo, float dHi, float sLo, float sHi, float rLo, float rHi)
    {
        b.uni (P::AenvA, aLo, aHi);
        b.uni (P::AenvD, dLo, dHi);
        b.uni (P::AenvS, sLo, sHi);
        b.uni (P::AenvR, rLo, rHi);
    }

    void filterEnv (B& b, float aLo, float aHi, float dLo, float dHi, float sLo, float sHi, float rLo, float rHi)
    {
        b.uni (P::FenvA, aLo, aHi);
        b.uni (P::FenvD, dLo, dHi);
        b.uni (P::FenvS, sLo, sHi);
        b.uni (P::FenvR, rLo, rHi);
    }

    void modEnv (B& b, float aLo, float aHi, float dLo, float dHi, float sLo, float sHi, float rLo, float rHi)
    {
        b.uni (P::MenvA, aLo, aHi);
        b.uni (P::MenvD, dLo, dHi);
        b.uni (P::MenvS, sLo, sHi);
        b.uni (P::MenvR, rLo, rHi);
    }

    // Bass amp attack. Under ~1 ms the level jumps in one sample, a click at
    // every note start (and from the release tail when a note retriggers);
    // 1.5-4 ms keeps the front hard without it.
    constexpr float kBassAttackLo = 1.5f, kBassAttackHi = 4.f;

    // The knock at the start of a bass note: a fast, nearly linear pitch fall
    // (a psy bass drops from ~1.5 octaves up to the note within 20-40 ms).
    void bassKnock (B& b, float chance, float amtLo, float amtHi, float decLo, float decHi)
    {
        if (! b.chance (chance)) return;
        b.set (P::PenvAttack, 0.f);
        b.set (P::PenvDest, 0.f);
        b.uni (P::PenvAmt, amtLo, amtHi);
        b.uni (P::PenvDecay, decLo, decHi);
        b.uni (P::PenvCurve, 0.f, 0.2f);
    }

    // A plain 12 dB LP or HP request rolls its slope too, so the same recipe
    // yields soft 12 dB and steep 18/24 dB curves.
    void filter1 (B& b, int type, float cLo, float cHi, float rLo, float rHi, float eLo, float eHi)
    {
        if (type == ft (FilterType::LP12))
            b.pick (P::F1Type, { ft (FilterType::LP12), ft (FilterType::LP18), ft (FilterType::LP24) }, { 0.45f, 0.2f, 0.35f });
        else if (type == ft (FilterType::HP12))
            b.pick (P::F1Type, { ft (FilterType::HP12), ft (FilterType::HP18), ft (FilterType::HP24) }, { 0.4f, 0.2f, 0.4f });
        else
            b.set (P::F1Type, (float) type);
        b.uni (P::F1Cutoff, cLo, cHi);
        b.uni (P::F1Reso, rLo, rHi);
        b.uni (P::F1Env, eLo, eHi);
    }

    void analogA (B& b, float shape, float level = 0.8f)
    {
        b.on (P::OscAOn, true);
        b.set (P::OscAType, (float) OscType::Analog);
        b.set (P::OscAShape, shape);
        b.set (P::OscALevel, level);
    }

    void analogB (B& b, float shape, float levelLo, float levelHi)
    {
        b.on (P::OscBOn, true);
        b.set (P::OscBType, (float) OscType::Analog);
        b.set (P::OscBShape, shape);
        b.uni (P::OscBLevel, levelLo, levelHi);
    }

    void supersawA (B& b, float dLo, float dHi)
    {
        b.on (P::OscAOn, true);
        b.set (P::OscAType, (float) OscType::Supersaw);
        b.uni (P::OscADetune, dLo, dHi);
        b.set (P::OscALevel, 0.85f);
    }

    void wavetableA (B& b, std::initializer_list<int> tables, float pLo, float pHi)
    {
        b.on (P::OscAOn, true);
        b.set (P::OscAType, (float) OscType::Wavetable);
        b.pick (P::OscAWt, tables);
        b.uni (P::OscAWtPos, pLo, pHi);
        b.set (P::OscALevel, 0.85f);
    }

    void fmA (B& b, std::initializer_list<float> ratios, float iLo, float iHi, float fbLo, float fbHi)
    {
        b.on (P::OscAOn, true);
        b.set (P::OscAType, (float) OscType::FM);
        b.among (P::OscAFmRatio, ratios);
        b.uni (P::OscAFm, iLo, iHi);
        b.uni (P::OscAFmFb, fbLo, fbHi);
        b.set (P::OscALevel, 0.8f);
    }

    // Energy sounds run over a length: bars by default (the tempo follows),
    // or seconds. The Sweep source climbs 0 -> 1 over it from the note start,
    // and the recipes hang pitch, filter and level on the sweep, so the
    // Length control stretches the whole gesture.
    enum Bars { B1_4, B1_2, B1, B2, B4, B8, B16, B32 };

    void energyLength (B& b, std::initializer_list<int> bars, std::initializer_list<float> weights, float secLo, float secHi)
    {
        b.pick (P::EnergyBars, bars, weights);
        b.uni (P::EnergySec, secLo, secHi);
    }

    // ---- effects --------------------------------------------------------------

    void fxDelay (B& b, float p, std::initializer_list<int> divs, float fbLo, float fbHi, float mixLo, float mixHi)
    {
        if (! b.chance (p)) return;
        b.on (P::DelayOn, true);
        b.set (P::DelaySync, 1.f);
        b.pick (P::DelayDiv, divs);
        b.uni (P::DelayFb, fbLo, fbHi);
        b.uni (P::DelayMix, mixLo, mixHi);
        b.on (P::DelayPing, b.chance (0.7f));
        b.uni (P::DelayLowCut, 150.f, 500.f);
        b.uni (P::DelayHighCut, 3500.f, 9000.f);
    }

    void fxReverb (B& b, float p, float decLo, float decHi, float mixLo, float mixHi)
    {
        if (! b.chance (p)) return;
        b.on (P::ReverbOn, true);
        b.uni (P::ReverbDecay, decLo, decHi);
        b.uni (P::ReverbMix, mixLo, mixHi);
        b.uni (P::ReverbSize, 0.4f, 0.95f);
        b.uni (P::ReverbDamp, 0.2f, 0.7f);
        b.uni (P::ReverbPre, 5.f, 60.f);
    }

    void fxDist (B& b, float p, std::initializer_list<int> types, float dLo, float dHi, float mixLo, float mixHi)
    {
        if (! b.chance (p)) return;
        b.on (P::DistOn, true);
        b.pick (P::DistType, types);
        b.uni (P::DistDrive, dLo, dHi);
        b.uni (P::DistMix, mixLo, mixHi);
        b.uni (P::DistTone, 4000.f, 16000.f);
    }

    void fxPhaser (B& b, float p)
    {
        if (! b.chance (p)) return;
        b.on (P::PhaserOn, true);
        b.uni (P::PhaserRate, 0.05f, 1.5f);
        b.uni (P::PhaserDepth, 0.4f, 0.9f);
        b.uni (P::PhaserFb, 0.2f, 0.8f);
        b.uni (P::PhaserCenter, 300.f, 2500.f);
        b.uni (P::PhaserMix, 0.35f, 0.6f);
    }

    void fxChorus (B& b, float p, bool allowFlanger = true)
    {
        if (! b.chance (p)) return;
        b.on (P::ChorusOn, true);
        if (allowFlanger) b.pick (P::ChorusMode, { 0, 1 }, { 0.65f, 0.35f });
        b.uni (P::ChorusRate, 0.1f, 1.2f);
        b.uni (P::ChorusDepth, 0.3f, 0.8f);
        b.uni (P::ChorusFb, -0.3f, 0.6f);
        b.uni (P::ChorusMix, 0.25f, 0.5f);
    }

    void fxShift (B& b, float p, float hzLo, float hzHi, float fbLo, float fbHi)
    {
        if (! b.chance (p)) return;
        b.on (P::FshiftOn, true);
        const float sign = b.chance (0.5f) ? 1.f : -1.f;
        b.uni (P::FshiftHz, sign * hzLo, sign * hzHi);
        b.uni (P::FshiftFb, fbLo, fbHi);
        b.uni (P::FshiftSpread, 0.f, 1.f);
        b.uni (P::FshiftMix, 0.3f, 0.6f);
    }

    // OTT-style multiband: down and up on every band, lows and highs lifted.
    // Thresholds sit where a mono bass voice lands after the voice gain staging.
    void fxCompMulti (B& b)
    {
        b.on (P::CompOn, true);
        b.set (P::CompMode, (float) CompMode::Multiband);
        b.uni (P::CompAttack, 2.f, 12.f);
        b.uni (P::CompRelease, 60.f, 220.f);
        b.uni (P::CompMix, 0.5f, 0.85f);
        b.uni (P::CompDown, 0.5f, 0.8f);
        b.uni (P::CompUp, 0.5f, 0.9f);
        b.uni (P::MbXLow, 120.f, 200.f);   // keeps the 2nd harmonic of an E1-A1 shot with its fundamental
        b.uni (P::MbXHigh, 1800.f, 3500.f);
        b.uni (P::MbLowDown, -10.f, -4.f);
        b.uni (P::MbMidDown, -22.f, -14.f);
        b.uni (P::MbHighDown, -28.f, -20.f);
        b.uni (P::MbLowUp, -40.f, -30.f);
        b.uni (P::MbMidUp, -44.f, -34.f);
        b.uni (P::MbHighUp, -42.f, -30.f);
        b.uni (P::MbLowGain, 0.f, 3.f);
        b.uni (P::MbMidGain, -1.f, 3.f);
        b.uni (P::MbHighGain, 4.f, 10.f);
        b.uni (P::CompOut, 0.f, 3.f);
    }

    // Stereo from a mono voice above a mono low end (Haas side from the mid).
    void fxWidth (B& b, float p, float stereoLo, float stereoHi)
    {
        if (! b.chance (p)) return;
        b.on (P::WidthOn, true);
        b.uni (P::WidthStereo, stereoLo, stereoHi);
        b.uni (P::WidthTime, 6.f, 18.f);
        b.uni (P::WidthMonoHz, 120.f, 200.f);
    }

    // Single-band glue: soft knee, medium ratio, made up to the same loudness.
    void fxCompGlue (B& b)
    {
        b.on (P::CompOn, true);
        b.set (P::CompMode, (float) CompMode::Normal);
        b.uni (P::CompThresh, -24.f, -14.f);
        b.uni (P::CompRatio, 4.f, 10.f);
        b.uni (P::CompKnee, 6.f, 12.f);
        b.uni (P::CompAttack, 5.f, 20.f);
        b.uni (P::CompRelease, 80.f, 200.f);
        b.set (P::CompMix, 1.f);
        b.uni (P::CompOut, 4.f, 8.f);
    }

    // The basses' glue: soft knee, gentle ratio, a few dB of gain reduction
    // on the body and the make-up that puts them level with the kicks.
    void fxCompBass (B& b)
    {
        b.on (P::CompOn, true);
        b.set (P::CompMode, (float) CompMode::Normal);
        b.uni (P::CompThresh, -24.f, -20.f);
        b.uni (P::CompRatio, 3.f, 5.f);
        b.uni (P::CompKnee, 6.f, 10.f);
        b.uni (P::CompAttack, 3.f, 8.f);
        b.uni (P::CompRelease, 50.f, 90.f);
        b.set (P::CompMix, 1.f);
        b.uni (P::CompOut, 10.f, 13.f);
    }

    // =========================================================================
    // Recipes
    // =========================================================================

    void acid (B& b)
    {
        monoVoice (b, true, 40.f, 95.f);
        b.uni (P::Accent, 0.5f, 0.95f);
        b.pick (P::Octave, { -1, 0 }, { 0.35f, 0.65f });
        b.set (P::VelSens, 0.25f);

        analogA (b, b.chance (0.65f) ? kSaw : kSquare, 0.85f);
        if (b.chance (0.2f)) b.uni (P::SubLevel, 0.1f, 0.3f);

        const int type = b.pick (P::F1Type, { ft (FilterType::Diode303), ft (FilterType::Ladder24) }, { 0.8f, 0.2f });
        (void) type;
        b.uni (P::F1Cutoff, 180.f, 900.f);
        b.uni (P::F1Reso, 0.55f, 0.92f);
        b.uni (P::F1Env, 0.35f, 0.8f);
        b.uni (P::F1Drive, 0.1f, 0.5f);
        b.uni (P::F1Key, 0.3f, 0.6f);
        filterEnv (b, 0.f, 3.f, 120.f, 600.f, 0.f, 0.1f, 30.f, 120.f);
        ampEnv (b, 0.5f, 3.f, 300.f, 900.f, 0.6f, 1.f, 20.f, 80.f);

        if (b.chance (0.5f))
        {
            b.lfoSync (0, b.chance (0.5f) ? LfoShape::Triangle : LfoShape::Sine, { D4_1, D2_1, D1_1 }, false);
            b.route (S::Lfo1, D::F1Cutoff, 0.04f, 0.2f);
        }
        b.route (S::Macro1, D::F1Cutoff, 0.3f, 0.5f);
        b.route (S::Macro2, D::F1Reso, 0.25f, 0.35f);
        b.route (S::Macro3, D::FilterEnvAmt, 0.3f, 0.5f);

        fxDist (b, 0.6f, { (int) DistType::Soft, (int) DistType::Tube }, 6.f, 18.f, 0.4f, 0.9f);
        fxDelay (b, 0.5f, { D1_8D, D1_4, D1_8 }, 0.2f, 0.45f, 0.1f, 0.25f);
        fxReverb (b, 0.25f, 0.8f, 2.f, 0.06f, 0.14f);
        fxPhaser (b, 0.15f);
        b.macros ({ D::DistDrive, D::F1Drive, D::DelayMix, D::ReverbMix });
    }

    void squelch (B& b)
    {
        monoVoice (b, b.chance (0.5f), 0.f, 30.f);
        b.pick (P::Octave, { -1, 0 });

        switch (b.weighted ({ 0.4f, 0.3f, 0.3f }))
        {
            case 0: analogA (b, b.rnd (1.9f, 2.6f)); break;
            case 1: supersawA (b, 0.05f, 0.2f); break;
            default: wavetableA (b, { 2, 7, 0 }, 0.2f, 0.8f); break;
        }
        if (b.chance (0.5f))
        {
            analogB (b, kSquare, 0.3f, 0.6f);
            b.among (P::OscBCoarse, { -12.f, 0.f, 7.f, 12.f });
            b.uni (P::OscBFine, -12.f, 12.f);
        }

        const int f = b.weighted ({ 0.4f, 0.3f, 0.3f });
        if (f == 0)
        {
            filter1 (b, ft (FilterType::Formant), 600.f, 1500.f, 0.4f, 0.85f, 0.f, 0.f);
            b.uni (P::F1Vowel, 0.f, 0.5f);
            modEnv (b, 0.f, 5.f, 60.f, 250.f, 0.f, 0.2f, 40.f, 120.f);
            b.route (S::ModEnv, D::F1Vowel, 0.35f, 0.7f);
        }
        else
        {
            filter1 (b, f == 1 ? ft (FilterType::BP12) : ft (FilterType::LP12), 300.f, 1500.f, 0.6f, 0.9f, 0.3f, 0.7f);
        }
        filterEnv (b, 0.f, 2.f, 60.f, 220.f, 0.f, 0.2f, 30.f, 90.f);
        b.uni (P::F1Drive, 0.1f, 0.5f);
        ampEnv (b, 0.3f, 2.f, 150.f, 400.f, 0.3f, 0.7f, 30.f, 80.f);

        if (b.chance (0.4f))
        {
            b.lfoSync (2, LfoShape::SampleHold, { D1_16, D1_32, D1_16T }, false);
            b.route (S::Lfo3, D::F1Cutoff, 0.1f, 0.3f);
        }
        b.route (S::Velocity, D::F1Cutoff, 0.1f, 0.25f);
        b.route (S::Macro1, D::F1Cutoff, 0.3f, 0.5f);

        fxDist (b, 0.5f, { 0, 3, 1 }, 6.f, 20.f, 0.4f, 0.8f);
        fxPhaser (b, 0.3f);
        fxDelay (b, 0.4f, { D1_16D, D1_8D, D1_16 }, 0.2f, 0.45f, 0.1f, 0.25f);
        fxChorus (b, 0.2f);
        // The band follows the note, so the squelch sits as loud on a low
        // line as on a high one (a fixed band swung it by 10 dB over C2-C5).
        b.uni (P::F1Key, 0.5f, 0.8f);
        b.macros ({ D::F1Reso, D::FilterEnvAmt, D::F1Vowel, D::DistDrive, D::PhaserMix, D::DelayMix });
    }

    void zap (B& b)
    {
        if (b.chance (0.6f)) polyVoice (b, 4, 8);
        else monoVoice (b, false, 0.f, 0.f);
        b.pick (P::Octave, { 0, 1 }, { 0.6f, 0.4f });

        if (b.chance (0.5f))
            fmA (b, { 0.5f, 1.f, 2.f, 3.5f, 7.f }, 0.5f, 6.f, 0.f, 0.4f);
        else
            analogA (b, b.rnd (0.f, 1.2f));

        b.set (P::PenvAttack, 0.f);
        if (b.chance (0.9f)) b.uni (P::PenvAmt, 24.f, 60.f);
        else b.uni (P::PenvAmt, -48.f, -24.f);
        b.uni (P::PenvDecay, 20.f, 200.f);
        b.uni (P::PenvCurve, 0.6f, 1.f);
        b.set (P::PenvDest, 0.f);

        if (b.chance (0.3f)) { b.pick (P::NoiseType, { 0, 2 }); b.uni (P::NoiseLevel, 0.02f, 0.15f); }

        if (b.chance (0.6f)) b.set (P::F1Type, (float) ft (FilterType::Off));
        else filter1 (b, ft (FilterType::LP12), 3000.f, 16000.f, 0.f, 0.3f, 0.f, 0.f);

        ampEnv (b, 0.f, 0.5f, 60.f, 350.f, 0.f, 0.f, 30.f, 150.f);
        if (b.chance (0.3f)) b.route (S::PitchEnv, D::FmA, 0.2f, 0.5f);
        b.route (S::Macro1, D::PitchEnvAmt, 0.2f, 0.4f);

        fxDelay (b, 0.7f, { D1_8, D1_16D, D1_8D }, 0.3f, 0.6f, 0.15f, 0.35f);
        fxReverb (b, 0.4f, 1.f, 3.f, 0.1f, 0.25f);
        fxShift (b, 0.2f, 20.f, 300.f, 0.f, 0.5f);
        b.macros ({ D::FmA, D::F1Cutoff, D::DelayMix, D::ReverbMix, D::ShiftHz, D::NoiseLevel });
    }

    void laser (B& b)
    {
        polyVoice (b, 3, 6);
        if (b.chance (0.6f))
        {
            analogA (b, kSaw, 0.5f);
            analogB (b, b.chance (0.7f) ? kSaw : kSquare, 0.7f, 0.9f);
            b.on (P::OscBSync, true);
            b.uni (P::OscBCoarse, 12.f, 24.f);
            b.set (P::PenvDest, 2.f);  // sweep only the synced slave
            b.uni (P::PenvAmt, 24.f, 48.f);
            b.uni (P::PenvDecay, 150.f, 700.f);
            b.uni (P::PenvCurve, 0.3f, 0.7f);
        }
        else
        {
            fmA (b, { 1.f, 2.f, 3.f }, 1.f, 5.f, 0.1f, 0.5f);
            b.set (P::PenvDest, 0.f);
            b.uni (P::PenvAmt, 30.f, 60.f);
            b.uni (P::PenvDecay, 200.f, 900.f);
            b.uni (P::PenvCurve, 0.4f, 0.8f);
        }
        b.set (P::PenvAttack, 0.f);
        filter1 (b, b.chance (0.5f) ? ft (FilterType::LP12) : ft (FilterType::BP12), 2000.f, 10000.f, 0.2f, 0.5f, 0.f, 0.2f);
        filterEnv (b, 0.f, 0.f, 200.f, 800.f, 0.f, 0.2f, 100.f, 300.f);
        ampEnv (b, 0.f, 1.f, 200.f, 800.f, 0.f, 0.f, 100.f, 300.f);
        b.route (S::Macro1, D::PitchEnvAmt, 0.2f, 0.4f);

        fxDelay (b, 0.8f, { D1_8D, D1_4, D1_8, D1_16D }, 0.3f, 0.6f, 0.2f, 0.35f);
        fxReverb (b, 0.5f, 1.5f, 4.f, 0.1f, 0.25f);
        fxChorus (b, 0.2f);
        b.macros ({ D::F1Cutoff, D::F1Reso, D::FmA, D::DelayFb, D::DelayMix, D::ReverbMix });
    }

    // Psy bass for 16th rolls, modelled on sustained reference notes (C1-B2): one
    // saw through two low-passes in series, an 18 dB one that follows the key
    // and a 12 dB one fixed around 250 Hz, so low notes keep a soft top and
    // high notes lose theirs faster. A filter burst through both and a pitch
    // knock start every 16th; the body holds at full level and stops dead.
    void psyBass (B& b)
    {
        monoVoice (b, false, 0.f, 0.f);
        b.set (P::Octave, -1.f);
        b.set (P::VelSens, 0.2f);
        b.set (P::Accent, 0.2f);

        analogA (b, kSaw, 0.85f);
        b.set (P::F1Type, (float) ft (FilterType::LP18));
        b.uni (P::F1Cutoff, 420.f, 520.f);
        b.uni (P::F1Reso, 0.f, 0.1f);
        b.uni (P::F1Key, 0.7f, 0.8f);
        b.uni (P::F1Drive, 0.f, 0.2f);
        b.uni (P::F1Env, 0.22f, 0.28f);
        b.set (P::F2Type, (float) ft (FilterType::LP12));
        b.set (P::FRouting, 0.f);
        b.uni (P::F2Cutoff, 220.f, 290.f);
        b.set (P::F2Key, 0.f);
        b.uni (P::F2Env, 0.22f, 0.3f);
        filterEnv (b, 0.f, 0.f, 220.f, 300.f, 0.f, 0.f, 20.f, 40.f);
        ampEnv (b, kBassAttackLo, kBassAttackHi, 80.f, 150.f, 0.85f, 1.f, 12.f, 35.f);
        bassKnock (b, 0.9f, 18.f, 24.f, 25.f, 35.f);

        b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.35f);
        b.route (S::Macro1, D::F2Cutoff, 0.2f, 0.35f);
        b.route (S::Macro2, D::FilterEnvAmt, 0.2f, 0.3f);
        fxCompBass (b);
        b.macros ({ D::F1Reso, D::PitchEnvAmt, D::F1Drive });
    }

    void psyLead (B& b)
    {
        if (b.chance (0.4f)) monoVoice (b, true, 30.f, 120.f);
        else polyVoice (b, 6, 8);

        switch (b.weighted ({ 0.5f, 0.3f, 0.2f }))
        {
            case 0: supersawA (b, 0.2f, 0.6f); break;
            case 1:
                wavetableA (b, { 0, 1, 5, 7 }, 0.2f, 0.8f);
                b.lfoSync (1, LfoShape::Triangle, { D1_4, D1_2, D1_1 }, false);
                b.route (S::Lfo2, D::WtPosA, 0.15f, 0.4f);
                break;
            default: analogA (b, kSaw); break;
        }
        if (b.chance (0.6f))
        {
            analogB (b, b.chance (0.6f) ? kSaw : kSquare, 0.4f, 0.7f);
            b.among (P::OscBCoarse, { 0.f, 7.f, 12.f, -12.f });
            b.uni (P::OscBFine, 5.f, 15.f);
        }
        filter1 (b, b.chance (0.6f) ? ft (FilterType::LP12) : ft (FilterType::Ladder24), 1500.f, 6000.f, 0.15f, 0.45f, 0.1f, 0.35f);
        filterEnv (b, 0.f, 10.f, 300.f, 1200.f, 0.3f, 0.6f, 150.f, 500.f);
        ampEnv (b, 2.f, 20.f, 400.f, 1000.f, 0.6f, 0.9f, 150.f, 500.f);

        if (b.chance (0.6f))
        {
            b.lfo (0, LfoShape::Sine, 4.5f, 6.5f, false);
            b.route (S::Lfo1, D::PitchAll, 0.004f, 0.01f);
        }
        b.route (S::ModWheel, D::F1Cutoff, 0.2f, 0.4f);
        b.route (S::Macro1, D::F1Cutoff, 0.25f, 0.45f);

        fxDelay (b, 0.9f, { D1_8D, D1_4D, D1_4 }, 0.35f, 0.55f, 0.2f, 0.35f);
        fxReverb (b, 0.7f, 2.f, 5.f, 0.15f, 0.3f);
        fxChorus (b, 0.3f, false);
        fxDist (b, 0.2f, { 0, 3 }, 4.f, 12.f, 0.3f, 0.6f);
        b.macros ({ D::F1Reso, D::WtPosA, D::DetuneA, D::FilterEnvAmt, D::DelayMix, D::ReverbMix, D::ChorusMix, D::DistDrive });
    }

    void pluck (B& b)
    {
        polyVoice (b, 6, 10);
        switch (b.weighted ({ 0.45f, 0.25f, 0.3f }))
        {
            case 0: analogA (b, b.chance (0.6f) ? kSaw : kSquare); break;
            case 1: wavetableA (b, { 6, 0, 3 }, 0.f, 0.7f); break;
            default: fmA (b, { 2.f, 3.f, 4.f }, 1.f, 3.f, 0.f, 0.2f); break;
        }
        filter1 (b, b.chance (0.5f) ? ft (FilterType::LP12) : ft (FilterType::Ladder24), 300.f, 1500.f, 0.1f, 0.5f, 0.4f, 0.8f);
        filterEnv (b, 0.f, 0.f, 80.f, 300.f, 0.f, 0.f, 80.f, 250.f);
        ampEnv (b, 0.f, 0.5f, 150.f, 600.f, 0.f, 0.f, 100.f, 400.f);
        b.route (S::Velocity, D::F1Cutoff, 0.15f, 0.3f);
        b.route (S::Macro1, D::FilterEnvAmt, 0.2f, 0.4f);

        fxDelay (b, 0.8f, { D1_8D, D1_8, D1_16D }, 0.3f, 0.5f, 0.15f, 0.3f);
        fxReverb (b, 0.6f, 1.2f, 3.5f, 0.1f, 0.25f);
        b.macros ({ D::F1Cutoff, D::F1Reso, D::WtPosA, D::FmA, D::DelayMix, D::ReverbMix });
    }

    void stab (B& b)
    {
        polyVoice (b, 8, 12);
        if (b.chance (0.5f)) supersawA (b, 0.15f, 0.45f);
        else analogA (b, kSaw);
        analogB (b, kSaw, 0.4f, 0.7f);
        b.among (P::OscBCoarse, { 12.f, 7.f, 0.f });
        b.uni (P::OscBFine, -10.f, 10.f);

        filter1 (b, ft (FilterType::LP12), 800.f, 3000.f, 0.1f, 0.4f, 0.3f, 0.6f);
        filterEnv (b, 0.f, 2.f, 150.f, 400.f, 0.1f, 0.3f, 150.f, 400.f);
        ampEnv (b, 0.f, 2.f, 200.f, 500.f, 0.f, 0.3f, 150.f, 400.f);
        b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.4f);

        fxReverb (b, 0.8f, 1.5f, 4.f, 0.15f, 0.3f);
        fxDelay (b, 0.5f, { D1_8D, D1_4 }, 0.25f, 0.45f, 0.1f, 0.25f);
        fxChorus (b, 0.4f, false);
        fxDist (b, 0.3f, { 0, 3 }, 4.f, 10.f, 0.3f, 0.5f);
        b.macros ({ D::F1Reso, D::FilterEnvAmt, D::DetuneA, D::ReverbMix, D::DelayMix, D::ChorusMix, D::DistDrive });
    }

    void screech (B& b)
    {
        monoVoice (b, true, 20.f, 100.f);
        analogA (b, b.chance (0.6f) ? kSaw : kSquare, 0.6f);
        if (b.chance (0.6f))
        {
            analogB (b, kSaw, 0.7f, 0.9f);
            b.on (P::OscBSync, true);
            b.uni (P::OscBCoarse, 7.f, 19.f);
            modEnv (b, 0.f, 20.f, 200.f, 900.f, 0.f, 0.4f, 100.f, 300.f);
            b.route (S::ModEnv, D::PitchB, 0.1f, 0.4f);
        }
        filter1 (b, b.chance (0.5f) ? ft (FilterType::BP12) : ft (FilterType::LP12), 1500.f, 5000.f, 0.75f, 0.95f, 0.f, 0.25f);
        b.uni (P::F1Drive, 0.3f, 0.7f);
        if (b.chance (0.5f)) b.lfo (0, LfoShape::Sine, 4.f, 10.f, false);
        else b.lfoSync (0, LfoShape::Triangle, { D1_16, D1_8T, D1_8 }, false);
        b.route (S::Lfo1, D::F1Cutoff, 0.1f, 0.3f);
        ampEnv (b, 1.f, 10.f, 300.f, 800.f, 0.8f, 1.f, 100.f, 300.f);
        b.route (S::Macro1, D::F1Reso, 0.1f, 0.2f);

        fxDist (b, 0.7f, { 1, 2, 3 }, 10.f, 24.f, 0.5f, 0.9f);
        fxPhaser (b, 0.3f);
        fxDelay (b, 0.6f, { D1_8D, D1_4, D1_16D }, 0.3f, 0.55f, 0.15f, 0.3f);
        fxReverb (b, 0.3f, 1.f, 3.f, 0.08f, 0.18f);
        b.macros ({ D::F1Cutoff, D::F1Drive, D::DistDrive, D::Lfo1Rate, D::PhaserMix, D::DelayMix, D::ReverbMix });
    }

    void alienVoice (B& b)
    {
        monoVoice (b, true, 30.f, 150.f);
        if (b.chance (0.5f)) analogA (b, kSaw);
        else wavetableA (b, { 2 }, 0.f, 1.f);
        if (b.chance (0.4f))
        {
            analogB (b, kSquare, 0.3f, 0.5f);
            b.set (P::OscBCoarse, -12.f);
        }
        filter1 (b, ft (FilterType::Formant), 700.f, 1400.f, 0.4f, 0.8f, 0.f, 0.f);
        b.uni (P::F1Vowel, 0.f, 1.f);
        if (b.chance (0.5f)) b.lfo (0, b.chance (0.5f) ? LfoShape::SmoothRandom : LfoShape::Sine, 0.5f, 4.f, false);
        else b.lfoSync (0, LfoShape::Triangle, { D1_8, D1_4, D1_8T }, false);
        b.route (S::Lfo1, D::F1Vowel, 0.3f, 0.7f);
        if (b.chance (0.5f))
        {
            modEnv (b, 0.f, 30.f, 150.f, 600.f, 0.f, 0.3f, 100.f, 300.f);
            b.route (S::ModEnv, D::F1Vowel, -0.5f, 0.5f);
        }
        if (b.chance (0.25f))
        {
            b.set (P::F2Type, (float) ft (FilterType::LP12));
            b.uni (P::F2Cutoff, 3000.f, 8000.f);
        }
        ampEnv (b, 5.f, 40.f, 300.f, 800.f, 0.7f, 1.f, 100.f, 400.f);
        b.route (S::Macro1, D::F1Vowel, 0.3f, 0.6f);

        fxDelay (b, 0.5f, { D1_8D, D1_4, D1_8 }, 0.25f, 0.5f, 0.1f, 0.25f);
        fxChorus (b, 0.3f);
        fxShift (b, 0.3f, 5.f, 60.f, 0.f, 0.3f);
        fxReverb (b, 0.4f, 1.5f, 4.f, 0.1f, 0.25f);
        b.macros ({ D::F1Reso, D::Lfo1Rate, D::ShiftHz, D::ChorusMix, D::DelayMix, D::ReverbMix });
    }

    void bubble (B& b)
    {
        if (b.chance (0.6f)) polyVoice (b, 4, 6);
        else monoVoice (b, false, 0.f, 0.f);
        b.pick (P::Octave, { 1, 2 }, { 0.6f, 0.4f });
        if (b.chance (0.6f)) analogA (b, b.rnd (0.8f, 2.f));
        else fmA (b, { 1.f, 2.f }, 0.5f, 2.f, 0.f, 0.1f);

        filter1 (b, b.chance (0.4f) ? ft (FilterType::BP12) : ft (FilterType::LP12), 300.f, 1200.f, 0.7f, 0.95f, 0.4f, 0.9f);
        b.uni (P::F1Key, 0.6f, 1.f);
        filterEnv (b, 0.f, 0.f, 20.f, 90.f, 0.f, 0.f, 20.f, 60.f);
        if (b.chance (0.5f))
        {
            b.set (P::PenvAttack, 0.f);
            b.uni (P::PenvAmt, 7.f, 24.f);
            b.uni (P::PenvDecay, 10.f, 60.f);
            b.set (P::PenvCurve, 0.8f);
        }
        ampEnv (b, 0.f, 0.f, 60.f, 250.f, 0.f, 0.f, 30.f, 120.f);
        b.route (S::Random, D::F1Cutoff, 0.05f, 0.2f);
        b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.4f);

        fxDelay (b, 0.8f, { D1_16, D1_8T, D1_16D, D1_8 }, 0.3f, 0.6f, 0.15f, 0.35f);
        fxReverb (b, 0.4f, 1.f, 3.f, 0.1f, 0.2f);
        fxShift (b, 0.2f, 10.f, 150.f, 0.f, 0.4f);
        b.macros ({ D::F1Reso, D::FilterEnvAmt, D::PitchEnvAmt, D::DelayMix, D::DelayFb, D::ReverbMix, D::ShiftHz });
    }

    void gurgle (B& b)
    {
        monoVoice (b, true, 0.f, 40.f);
        analogA (b, b.chance (0.6f) ? kSaw : kTri);
        if (b.chance (0.4f))
        {
            analogB (b, kSaw, 0.3f, 0.6f);
            b.uni (P::OscBFine, -20.f, 20.f);
        }
        if (b.chance (0.5f))
        {
            filter1 (b, ft (FilterType::Formant), 500.f, 1400.f, 0.5f, 0.85f, 0.f, 0.f);
            b.lfo (2, b.chance (0.5f) ? LfoShape::SmoothRandom : LfoShape::SampleHold, 8.f, 20.f, false);
            b.route (S::Lfo3, D::F1Vowel, 0.3f, 0.6f);
        }
        else
        {
            filter1 (b, ft (FilterType::BP12), 400.f, 1500.f, 0.7f, 0.92f, 0.f, 0.2f);
            b.lfo (2, LfoShape::SmoothRandom, 8.f, 20.f, false);
            b.route (S::Lfo3, D::F1Cutoff, 0.2f, 0.45f);
        }
        b.lfo (0, LfoShape::Sine, 0.2f, 1.5f, false);
        b.route (S::Lfo1, D::F1Reso, 0.05f, 0.15f);
        if (b.chance (0.3f))
        {
            b.set (P::F2Type, (float) ft (FilterType::CombPos));
            b.uni (P::F2Cutoff, 200.f, 1200.f);
            b.uni (P::F2Reso, 0.3f, 0.7f);
        }
        // a quick attack, so 1/32 arps and short notes speak
        ampEnv (b, 1.f, 8.f, 400.f, 800.f, 0.6f, 1.f, 200.f, 600.f);
        b.route (S::Macro1, D::Lfo3Rate, 0.2f, 0.4f);

        fxPhaser (b, 0.5f);
        fxChorus (b, 0.4f);
        fxDelay (b, 0.5f, { D1_16D, D1_8D }, 0.3f, 0.5f, 0.1f, 0.25f);
        fxReverb (b, 0.4f, 1.5f, 4.f, 0.1f, 0.25f);
        // The band follows the note: a fixed one leaves low notes only their
        // weak upper harmonics to chatter with (18 dB quieter two octaves down).
        b.uni (P::F1Key, 0.6f, 0.9f);
        b.macros ({ D::F1Cutoff, D::F1Reso, D::F1Vowel, D::PhaserMix, D::ChorusMix, D::DelayMix, D::ReverbMix });
    }

    void wobble (B& b)
    {
        if (b.chance (0.6f)) monoVoice (b, true, 0.f, 50.f);
        else polyVoice (b, 4, 6);
        wavetableA (b, { 0, 1, 2, 4, 5, 7 }, 0.f, 0.3f);
        if (b.chance (0.4f))
        {
            analogB (b, kSaw, 0.3f, 0.6f);
            b.set (P::OscBCoarse, -12.f);
        }
        b.lfoSync (0, b.chance (0.6f) ? LfoShape::Sine : LfoShape::Triangle, { D1_4, D1_8, D1_8T, D1_16 }, true);
        b.route (S::Lfo1, D::WtPosA, 0.3f, 0.8f);
        filter1 (b, b.chance (0.5f) ? ft (FilterType::LP12) : ft (FilterType::Ladder24), 400.f, 2000.f, 0.2f, 0.6f, 0.f, 0.2f);
        b.route (S::Lfo1, D::F1Cutoff, 0.15f, 0.35f);
        ampEnv (b, 1.f, 5.f, 300.f, 600.f, 0.8f, 1.f, 50.f, 200.f);
        b.route (S::Macro1, D::Lfo1Rate, 0.0f, 0.25f);

        fxDist (b, 0.5f, { 0, 1, 3 }, 6.f, 18.f, 0.3f, 0.7f);
        fxDelay (b, 0.4f, { D1_8D, D1_4 }, 0.2f, 0.45f, 0.1f, 0.2f);
        fxPhaser (b, 0.2f);
        b.macros ({ D::WtPosA, D::F1Cutoff, D::F1Reso, D::DistDrive, D::DelayMix, D::PhaserMix });
    }

    void metallic (B& b)
    {
        polyVoice (b, 6, 10);
        fmA (b, { 1.41f, 2.76f, 3.5f, 5.19f, 7.13f, 1.5f, 11.f }, 1.5f, 6.f, 0.1f, 0.5f);
        modEnv (b, 0.f, 2.f, 200.f, 1200.f, 0.f, 0.1f, 200.f, 800.f);
        b.route (S::ModEnv, D::FmA, 0.2f, 0.5f);
        if (b.chance (0.4f))
        {
            b.on (P::OscBOn, true);
            if (b.chance (0.5f))
            {
                b.set (P::OscBType, (float) OscType::Wavetable);
                b.pick (P::OscBWt, { 3, 6 });
                b.uni (P::OscBWtPos, 0.f, 1.f);
                b.uni (P::OscBLevel, 0.3f, 0.6f);
            }
            else
            {
                b.set (P::OscBType, (float) OscType::Analog);
                b.set (P::OscBShape, kSine);
                b.among (P::OscBCoarse, { 7.f, 11.f, 19.f, 5.f });
                b.set (P::OscBLevel, 0.f);
                b.uni (P::RingLevel, 0.3f, 0.6f);
            }
        }
        if (b.chance (0.3f))
            filter1 (b, ft (FilterType::CombPos), 300.f, 2000.f, 0.5f, 0.85f, 0.f, 0.f);
        else if (b.chance (0.5f))
            filter1 (b, ft (FilterType::HP12), 200.f, 600.f, 0.f, 0.3f, 0.f, 0.f);
        else
            b.set (P::F1Type, (float) ft (FilterType::Off));
        ampEnv (b, 0.f, 1.f, 500.f, 2500.f, 0.f, 0.2f, 400.f, 1500.f);
        b.route (S::Velocity, D::FmA, 0.1f, 0.25f);
        b.route (S::Macro1, D::FmA, 0.2f, 0.4f);

        fxReverb (b, 0.8f, 2.f, 6.f, 0.15f, 0.35f);
        fxDelay (b, 0.6f, { D1_8D, D1_4, D1_4D }, 0.3f, 0.5f, 0.1f, 0.25f);
        fxChorus (b, 0.3f, false);
        b.macros ({ D::F1Cutoff, D::RingLevel, D::WtPosB, D::ReverbMix, D::ReverbSize, D::DelayMix, D::ChorusMix });
    }

    // Tonal: the pitch climbs one to two octaves over the length while the
    // filter opens and the level fades in. Noise is at most a trace of air;
    // the noise sweep is the Uplifter.
    void riser (B& b)
    {
        if (b.chance (0.5f)) polyVoice (b, 2, 4);
        else monoVoice (b, false, 0.f, 0.f);
        if (b.chance (0.55f)) supersawA (b, 0.3f, 0.7f);
        else
        {
            analogA (b, kSaw);
            analogB (b, kSaw, 0.5f, 0.8f);
            b.uni (P::OscBFine, 6.f, 18.f);
        }
        if (b.chance (0.4f)) { b.pick (P::NoiseType, { 0, 1 }); b.uni (P::NoiseLevel, 0.02f, 0.1f); }

        energyLength (b, { B4, B8, B16 }, { 0.35f, 0.45f, 0.2f }, 4.f, 16.f);
        b.uni (P::EnergyCurve, 0.2f, 0.7f);   // slow start, the climb speeds up into the drop
        b.uni (P::EnergyFade, 0.5f, 0.85f);   // starts quiet, never from silence
        b.on (P::EnergyCut, b.chance (0.3f));
        b.route (S::Sweep, D::PitchAll, 0.45f, 1.f);
        filter1 (b, ft (FilterType::LP12), 400.f, 1500.f, 0.2f, 0.5f, 0.f, 0.f);
        b.route (S::Sweep, D::F1Cutoff, 0.4f, 0.7f);
        if (b.chance (0.5f))
        {
            // spiral: a wobble that speeds up as the pitch climbs
            b.lfo (1, LfoShape::Sine, 2.f, 5.f, true);
            if (b.chance (0.5f)) b.route (S::Lfo2, D::PitchAll, 0.02f, 0.06f);
            else b.route (S::Lfo2, D::F1Cutoff, 0.05f, 0.15f);
            b.route (S::Sweep, D::Lfo2Rate, 0.3f, 0.6f);
        }
        ampEnv (b, 5.f, 40.f, 1000.f, 1000.f, 1.f, 1.f, 500.f, 2000.f);   // the sweep shapes the level
        b.route (S::Macro1, D::F1Reso, 0.2f, 0.4f);

        fxReverb (b, 0.9f, 4.f, 10.f, 0.3f, 0.5f);
        fxDelay (b, 0.7f, { D1_8D, D1_4, D1_8 }, 0.35f, 0.6f, 0.15f, 0.3f);
        fxPhaser (b, 0.3f);
        fxDist (b, 0.3f, { 0, 3 }, 3.f, 12.f, 0.2f, 0.5f);
        fxShift (b, 0.2f, 5.f, 200.f, 0.2f, 0.6f);
        b.macros ({ D::F1Cutoff, D::DetuneA, D::ReverbMix, D::DelayMix, D::DistDrive, D::PhaserMix, D::ShiftMix });
    }

    // Noise sweep: the filter opens upward over the length and the pitch
    // stays put.
    void uplifter (B& b)
    {
        polyVoice (b, 2, 4);
        b.pick (P::NoiseType, { 0, 1 }, { 0.6f, 0.4f });
        b.uni (P::NoiseLevel, 0.8f, 1.f);
        if (b.chance (0.2f)) { supersawA (b, 0.3f, 0.7f); b.uni (P::OscALevel, 0.05f, 0.15f); }
        else b.on (P::OscAOn, false);

        energyLength (b, { B2, B4, B8 }, { 0.25f, 0.5f, 0.25f }, 3.f, 12.f);
        b.uni (P::EnergyCurve, 0.1f, 0.6f);
        b.uni (P::EnergyFade, 0.5f, 0.85f);
        b.on (P::EnergyCut, b.chance (0.3f));
        filter1 (b, b.chance (0.4f) ? ft (FilterType::BP12) : (b.chance (0.5f) ? ft (FilterType::HP12) : ft (FilterType::LP12)),
                 150.f, 500.f, 0.2f, 0.6f, 0.f, 0.f);
        b.route (S::Sweep, D::F1Cutoff, 0.6f, 0.95f);
        if (b.chance (0.4f))
        {
            // a high-pass that follows, so the bottom thins out as it rises
            b.set (P::F2Type, (float) ft (FilterType::HP12));
            b.uni (P::F2Cutoff, 100.f, 300.f);
            b.route (S::Sweep, D::F2Cutoff, 0.3f, 0.6f);
        }
        if (b.chance (0.5f))
        {
            b.lfoSync (1, LfoShape::Triangle, { D1_16, D1_8, D1_16T }, false);
            b.route (S::Lfo2, D::F1Cutoff, 0.05f, 0.15f);
        }
        ampEnv (b, 5.f, 40.f, 1000.f, 1000.f, 1.f, 1.f, 500.f, 2000.f);
        b.route (S::Macro1, D::F1Reso, 0.2f, 0.4f);

        fxPhaser (b, 0.5f);
        fxShift (b, 0.4f, 5.f, 200.f, 0.2f, 0.6f);
        fxReverb (b, 0.9f, 4.f, 10.f, 0.3f, 0.5f);
        fxDelay (b, 0.5f, { D1_8D, D1_4 }, 0.3f, 0.55f, 0.15f, 0.3f);
        b.macros ({ D::F1Cutoff, D::F2Cutoff, D::ReverbMix, D::PhaserMix, D::ShiftMix, D::DelayMix });
    }

    // The reverse of a riser: pitch and filter fall over the length, fast
    // at first, and the level fades out with them.
    void downlifter (B& b)
    {
        if (b.chance (0.5f)) polyVoice (b, 2, 4);
        else monoVoice (b, false, 0.f, 0.f);
        b.pick (P::Octave, { 0, 1 });
        b.pick (P::NoiseType, { 0, 1 });
        b.uni (P::NoiseLevel, 0.3f, 0.8f);
        if (b.chance (0.5f)) supersawA (b, 0.2f, 0.6f);
        else analogA (b, kSaw);

        energyLength (b, { B1, B2, B4 }, { 0.3f, 0.45f, 0.25f }, 1.5f, 6.f);
        b.uni (P::EnergyCurve, -0.6f, 0.f);
        b.uni (P::EnergyFade, -1.f, -0.7f);
        b.route (S::Sweep, D::PitchAll, -1.f, -0.5f);
        filter1 (b, ft (FilterType::LP12), 1500.f, 6000.f, 0.2f, 0.6f, 0.f, 0.f);
        b.route (S::Sweep, D::F1Cutoff, -0.9f, -0.5f);
        ampEnv (b, 0.f, 30.f, 1000.f, 1000.f, 1.f, 1.f, 500.f, 2000.f);
        b.route (S::Macro1, D::F1Reso, 0.2f, 0.4f);

        fxReverb (b, 0.9f, 3.f, 9.f, 0.25f, 0.45f);
        fxDelay (b, 0.6f, { D1_8D, D1_4 }, 0.3f, 0.55f, 0.15f, 0.3f);
        if (b.chance (0.4f))
        {
            b.on (P::FshiftOn, true);
            b.uni (P::FshiftHz, -400.f, -50.f);
            b.uni (P::FshiftFb, 0.2f, 0.6f);
            b.uni (P::FshiftMix, 0.3f, 0.6f);
        }
        b.macros ({ D::F1Cutoff, D::NoiseLevel, D::ReverbMix, D::DelayMix, D::ShiftMix });
    }

    // A filtered-noise whoosh: swells in over the length while an LFO moves
    // the filter.
    void noiseFx (B& b)
    {
        polyVoice (b, 2, 4);
        b.pick (P::NoiseType, { 0, 1, 2 }, { 0.45f, 0.4f, 0.15f });
        b.uni (P::NoiseLevel, 0.8f, 1.f);
        if (b.chance (0.3f)) { analogA (b, kSaw, 0.2f); b.uni (P::OscALevel, 0.05f, 0.25f); }
        else b.on (P::OscAOn, false);

        energyLength (b, { B2, B4, B8 }, { 0.3f, 0.45f, 0.25f }, 2.f, 10.f);
        b.uni (P::EnergyCurve, -0.2f, 0.6f);
        b.uni (P::EnergyFade, 0.3f, 0.8f);
        const int f = b.pick (P::F1Type, { ft (FilterType::BP12), ft (FilterType::BP24), ft (FilterType::Notch), ft (FilterType::LadderBP),
                                           ft (FilterType::CombPos), ft (FilterType::Formant) },
                              { 0.25f, 0.15f, 0.15f, 0.15f, 0.15f, 0.15f });
        (void) f;
        b.uni (P::F1Cutoff, 300.f, 4000.f);
        b.uni (P::F1Reso, 0.3f, 0.9f);
        b.uni (P::F1Vowel, 0.f, 1.f);
        b.route (S::Sweep, D::F1Cutoff, 0.2f, 0.5f);
        if (b.chance (0.5f)) b.lfo (0, LfoShape::Sine, 0.1f, 1.f, true);
        else b.lfoSync (0, LfoShape::Triangle, { D1_2, D1_1, D2_1 }, true);
        b.route (S::Lfo1, D::F1Cutoff, 0.3f, 0.7f);
        if (b.chance (0.3f))
        {
            b.set (P::F2Type, (float) ft (FilterType::HP12));
            b.uni (P::F2Cutoff, 200.f, 1000.f);
        }
        ampEnv (b, 20.f, 200.f, 1000.f, 1000.f, 0.6f, 1.f, 300.f, 2000.f);
        b.route (S::Macro1, D::F1Cutoff, 0.3f, 0.5f);

        fxPhaser (b, 0.5f);
        fxShift (b, 0.4f, 10.f, 400.f, 0.1f, 0.6f);
        fxDelay (b, 0.5f, { D1_8D, D1_4 }, 0.3f, 0.55f, 0.15f, 0.3f);
        fxReverb (b, 0.7f, 2.f, 8.f, 0.2f, 0.4f);
        if (b.chance (0.2f))
        {
            b.on (P::CrushOn, true);
            b.uni (P::CrushBits, 4.f, 10.f);
            b.uni (P::CrushRate, 1.f, 12.f);
            b.uni (P::CrushMix, 0.3f, 0.7f);
        }
        b.macros ({ D::F1Reso, D::Lfo1Rate, D::F1Vowel, D::PhaserMix, D::ShiftHz, D::CrushMix, D::ReverbMix, D::DelayMix });
    }

    void atmos (B& b)
    {
        polyVoice (b, 8, 12);
        if (b.chance (0.5f)) supersawA (b, 0.3f, 0.7f);
        else wavetableA (b, { 2, 0, 6 }, 0.1f, 0.8f);
        if (b.chance (0.7f))
        {
            b.on (P::OscBOn, true);
            b.pick (P::OscBType, { (int) OscType::Analog, (int) OscType::Wavetable });
            b.set (P::OscBShape, kSaw);
            b.pick (P::OscBWt, { 0, 2, 6 });
            b.uni (P::OscBWtPos, 0.f, 1.f);
            b.uni (P::OscBLevel, 0.3f, 0.6f);
            b.among (P::OscBCoarse, { 12.f, 7.f, 0.f });
            b.uni (P::OscBFine, 8.f, 20.f);
        }
        filter1 (b, b.chance (0.5f) ? ft (FilterType::LP12) : ft (FilterType::Ladder24), 600.f, 3000.f, 0.1f, 0.4f, 0.1f, 0.3f);
        filterEnv (b, 500.f, 3000.f, 2000.f, 2000.f, 0.5f, 0.5f, 1500.f, 3000.f);
        ampEnv (b, 400.f, 2500.f, 1000.f, 1000.f, 0.7f, 1.f, 1500.f, 5000.f);
        b.lfo (1, LfoShape::Sine, 0.05f, 0.3f, false);
        b.route (S::Lfo2, D::F1Cutoff, 0.1f, 0.2f);
        b.route (S::Lfo2, D::WtPosA, 0.1f, 0.25f);
        b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.4f);

        fxReverb (b, 1.f, 5.f, 15.f, 0.35f, 0.6f);
        fxChorus (b, 0.6f, false);
        fxDelay (b, 0.6f, { D1_4D, D1_2 }, 0.3f, 0.55f, 0.15f, 0.3f);
        fxPhaser (b, 0.2f);
        b.macros ({ D::F1Reso, D::WtPosA, D::DetuneA, D::Lfo2Rate, D::ReverbMix, D::ChorusMix, D::DelayMix });
    }

    void drone (B& b)
    {
        polyVoice (b, 2, 3);
        if (b.chance (0.5f)) analogA (b, kSaw);
        else wavetableA (b, { 0, 3, 5, 7 }, 0.f, 1.f);
        analogB (b, kSaw, 0.4f, 0.7f);
        b.among (P::OscBCoarse, { -12.f, 0.f, 7.f });
        b.uni (P::OscBFine, 3.f, 10.f);
        b.uni (P::SubLevel, 0.2f, 0.4f);
        b.uni (P::NoiseLevel, 0.05f, 0.15f);

        filter1 (b, b.chance (0.5f) ? ft (FilterType::Ladder24) : ft (FilterType::LP12), 150.f, 900.f, 0.2f, 0.6f, 0.f, 0.f);
        b.lfo (0, LfoShape::Sine, 0.02f, 0.1f, false);
        b.route (S::Lfo1, D::F1Cutoff, 0.2f, 0.4f);
        b.lfo (1, LfoShape::SmoothRandom, 0.05f, 0.3f, false);
        b.route (S::Lfo2, D::Pan, 0.2f, 0.4f);
        if (b.chance (0.4f))
        {
            b.set (P::F2Type, (float) ft (FilterType::CombPos));
            b.uni (P::F2Cutoff, 60.f, 400.f);
            b.uni (P::F2Reso, 0.4f, 0.8f);
        }
        ampEnv (b, 1000.f, 4000.f, 1000.f, 1000.f, 1.f, 1.f, 3000.f, 8000.f);
        b.route (S::Macro1, D::F1Cutoff, 0.3f, 0.5f);

        fxReverb (b, 0.8f, 6.f, 20.f, 0.3f, 0.5f);
        fxPhaser (b, 0.4f);
        fxShift (b, 0.3f, 0.1f, 3.f, 0.f, 0.3f);
        fxChorus (b, 0.3f, false);
        b.macros ({ D::F1Reso, D::Lfo1Rate, D::NoiseLevel, D::F2Cutoff, D::ReverbMix, D::PhaserMix, D::ShiftHz });
    }

    // A boom that falls into the sub: bright noise transient, then a tail
    // that dies away over the length (fast at first) and outlasts the note.
    void impact (B& b)
    {
        monoVoice (b, false, 0.f, 0.f);
        b.pick (P::Octave, { -1, 0 }, { 0.7f, 0.3f });
        analogA (b, kSine, 0.9f);
        if (b.chance (0.4f)) { analogB (b, kTri, 0.2f, 0.4f); b.set (P::OscBCoarse, 12.f); }

        b.set (P::PenvAttack, 0.f);
        b.set (P::PenvDest, 0.f);
        b.uni (P::PenvAmt, 12.f, 36.f);
        b.uni (P::PenvDecay, 300.f, 1500.f);
        b.uni (P::PenvCurve, 0.3f, 0.7f);

        energyLength (b, { B1_2, B1, B2 }, { 0.25f, 0.5f, 0.25f }, 1.f, 4.f);
        b.uni (P::EnergyCurve, -1.f, -0.5f);
        b.set (P::EnergyFade, -1.f);

        b.pick (P::NoiseType, { 0, 1 });
        b.uni (P::NoiseLevel, 0.2f, 0.5f);
        filter1 (b, ft (FilterType::LP12), 300.f, 1000.f, 0.f, 0.25f, 0.4f, 0.8f);
        filterEnv (b, 0.f, 0.f, 80.f, 400.f, 0.f, 0.f, 100.f, 400.f);
        ampEnv (b, 0.f, 0.f, 1000.f, 1000.f, 1.f, 1.f, 800.f, 2500.f);   // the sweep fades the tail
        b.route (S::Macro1, D::PitchEnvAmt, 0.2f, 0.4f);
        b.route (S::Velocity, D::F1Cutoff, 0.1f, 0.25f);

        fxDist (b, 0.5f, { 0, 3 }, 3.f, 9.f, 0.2f, 0.5f);
        fxReverb (b, 0.9f, 4.f, 12.f, 0.25f, 0.45f);
        fxDelay (b, 0.2f, { D1_4, D1_4D }, 0.2f, 0.4f, 0.1f, 0.2f);
        b.macros ({ D::F1Cutoff, D::NoiseLevel, D::DistDrive, D::ReverbMix, D::ReverbSize, D::DelayMix });
    }

    // A held pad chopped on the grid. Amp mod is 1 + m, so a full-depth
    // square swings the gain between silence and 2x: sources sit at half level.
    void gate (B& b)
    {
        if (b.chance (0.7f)) polyVoice (b, 4, 8);
        else monoVoice (b, false, 0.f, 0.f);
        if (b.chance (0.2f))
        {
            // chopped noise texture
            b.on (P::OscAOn, false);
            b.pick (P::NoiseType, { 0, 1 });
            b.uni (P::NoiseLevel, 0.35f, 0.5f);
        }
        else if (b.chance (0.4f)) { supersawA (b, 0.3f, 0.6f); b.set (P::OscALevel, 0.5f); }
        else if (b.chance (0.5f)) { wavetableA (b, { 0, 2, 6 }, 0.1f, 0.7f); b.set (P::OscALevel, 0.5f); }
        else
        {
            analogA (b, kSaw, 0.5f);
            analogB (b, kSaw, 0.3f, 0.45f);
            b.uni (P::OscBFine, 8.f, 20.f);
        }

        filter1 (b, b.chance (0.5f) ? ft (FilterType::LP12) : ft (FilterType::Ladder24), 800.f, 4000.f, 0.1f, 0.4f, 0.f, 0.f);
        // mostly a hard square; sometimes a falling saw, the "acid gate" shape
        b.lfoSync (0, b.chance (0.75f) ? LfoShape::Square : LfoShape::SawDown, { D1_16, D1_8, D1_16D, D1_8T }, true);
        b.route (S::Lfo1, D::Amp, 0.85f, 1.f);
        ampEnv (b, 0.f, 10.f, 1000.f, 1000.f, 1.f, 1.f, 50.f, 300.f);
        b.route (S::Macro1, D::F1Cutoff, 0.3f, 0.5f);

        fxDelay (b, 0.7f, { D1_8D, D1_4 }, 0.3f, 0.55f, 0.15f, 0.3f);
        fxReverb (b, 0.7f, 2.f, 6.f, 0.15f, 0.35f);
        fxChorus (b, 0.4f, false);
        fxPhaser (b, 0.2f);
        b.macros ({ D::F1Reso, D::WtPosA, D::DetuneA, D::DelayMix, D::ReverbMix, D::ChorusMix, D::PhaserMix });
    }

    // Stepped random pitch and a stuttering gain on the grid, through a crusher.
    void glitch (B& b)
    {
        if (b.chance (0.6f)) monoVoice (b, false, 0.f, 0.f);
        else polyVoice (b, 2, 4);
        if (b.chance (0.4f)) { fmA (b, { 1.f, 2.f, 3.5f, 7.f }, 1.f, 6.f, 0.f, 0.5f); b.set (P::OscALevel, 0.5f); }
        else if (b.chance (0.5f)) { wavetableA (b, { 1, 3, 4, 5 }, 0.f, 1.f); b.set (P::OscALevel, 0.5f); }
        else analogA (b, kSquare, 0.5f);
        if (b.chance (0.3f)) { b.pick (P::NoiseType, { 0, 2 }); b.uni (P::NoiseLevel, 0.1f, 0.3f); }

        b.lfoSync (0, LfoShape::SampleHold, { D1_16, D1_32, D1_16T, D1_32T }, true);
        b.route (S::Lfo1, D::PitchAll, 0.1f, 0.4f);
        b.lfoSync (1, LfoShape::Square, { D1_32, D1_64, D1_16T }, true);
        b.route (S::Lfo2, D::Amp, 0.6f, 1.f);
        filter1 (b, b.chance (0.5f) ? ft (FilterType::BP12) : ft (FilterType::LP12), 800.f, 5000.f, 0.2f, 0.6f, 0.f, 0.f);
        b.lfo (2, LfoShape::SampleHold, 8.f, 30.f, false);
        b.route (S::Lfo3, D::F1Cutoff, 0.2f, 0.5f);
        ampEnv (b, 0.f, 0.f, 150.f, 800.f, 0.f, 0.6f, 30.f, 150.f);
        b.route (S::Macro1, D::Lfo3Rate, 0.2f, 0.4f);

        if (b.chance (0.8f))
        {
            b.on (P::CrushOn, true);
            b.uni (P::CrushBits, 3.f, 8.f);
            b.uni (P::CrushRate, 2.f, 16.f);
            b.uni (P::CrushMix, 0.4f, 0.9f);
        }
        fxShift (b, 0.4f, 20.f, 400.f, 0.f, 0.5f);
        fxDelay (b, 0.5f, { D1_16, D1_32, D1_16T }, 0.2f, 0.5f, 0.1f, 0.3f);
        fxDist (b, 0.3f, { 1, 2 }, 4.f, 14.f, 0.3f, 0.6f);
        fxReverb (b, 0.3f, 0.5f, 1.5f, 0.1f, 0.2f);
        b.macros ({ D::CrushMix, D::F1Cutoff, D::F1Reso, D::FmA, D::ShiftHz, D::DelayMix, D::DistDrive });
    }

    // Swells over the length and cuts at the peak like a reversed hit,
    // however long the note is held. The filter opens with it. Little
    // reverb, so no tail softens the cut.
    void reverse (B& b)
    {
        polyVoice (b, 4, 8);
        if (b.chance (0.4f)) supersawA (b, 0.3f, 0.7f);
        else if (b.chance (0.5f))
        {
            analogA (b, kSaw);
            analogB (b, kSaw, 0.4f, 0.7f);
            b.among (P::OscBCoarse, { 0.f, 7.f, 12.f });
            b.uni (P::OscBFine, 6.f, 16.f);
        }
        else fmA (b, { 1.41f, 2.76f, 3.5f, 1.5f }, 1.f, 4.f, 0.f, 0.3f);
        if (b.chance (0.3f)) { b.pick (P::NoiseType, { 0, 1 }); b.uni (P::NoiseLevel, 0.1f, 0.3f); }

        energyLength (b, { B1_2, B1, B2 }, { 0.3f, 0.45f, 0.25f }, 0.8f, 3.f);
        b.uni (P::EnergyCurve, 0.4f, 1.f);
        b.set (P::EnergyFade, 1.f);
        b.on (P::EnergyCut, true);
        filter1 (b, ft (FilterType::LP12), 300.f, 1200.f, 0.1f, 0.4f, 0.f, 0.f);
        b.route (S::Sweep, D::F1Cutoff, 0.4f, 0.8f);
        ampEnv (b, 0.f, 10.f, 1000.f, 1000.f, 1.f, 1.f, 5.f, 40.f);   // the release is the cut
        b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.4f);

        fxReverb (b, 0.5f, 1.f, 3.f, 0.08f, 0.2f);
        fxDelay (b, 0.3f, { D1_8D, D1_4 }, 0.2f, 0.4f, 0.1f, 0.2f);
        fxChorus (b, 0.3f, false);
        b.macros ({ D::F1Reso, D::DetuneA, D::FmA, D::ReverbMix, D::DelayMix, D::ChorusMix });
    }
    // Progressive-trance bass shot: one big, sustained, heavily compressed hit
    // per beat. Modelled on reference one-shots: a low-passed saw over a sine
    // body (E1-A1 when played at E2-A2), a kick-like pitch drop on most hits,
    // flat sustain, swell or pluck shapes, soft saturation, OTT on top. Dry.
    void bassShot (B& b)
    {
        if (b.chance (0.1f)) monoVoice (b, true, 40.f, 150.f);   // the odd two-note glide shot
        else monoVoice (b, false, 0.f, 0.f);
        b.pick (P::Octave, { -1, 0 }, { 0.75f, 0.25f });
        b.set (P::VelSens, 0.15f);
        b.set (P::Accent, 0.f);

        const bool analog = b.chance (0.55f);
        if (analog) analogA (b, kSaw, 0.8f);
        else { supersawA (b, 0.08f, 0.25f); b.set (P::OscALevel, 0.7f); }
        // The body, same pitch and retriggered with A. It must add to the saw's
        // fundamental, not cancel it: the analog saw's fundamental lines up with
        // a triangle, the supersaw's with a sine.
        analogB (b, analog ? kTri : kSine, 0.5f, 0.8f);
        b.set (P::OscBFine, 0.f);

        // Gentle slopes: the references keep a long, soft top above the body.
        // Key tracking counts from C4, so at E2 the cutoff lands ~1 octave lower.
        b.pick (P::F1Type, { ft (FilterType::LP12), ft (FilterType::LP18) }, { 0.7f, 0.3f });
        b.uni (P::F1Cutoff, 500.f, 2000.f);
        b.uni (P::F1Reso, 0.05f, 0.3f);
        b.uni (P::F1Key, 0.3f, 0.7f);
        b.uni (P::F1Drive, 0.1f, 0.4f);

        // Shape: flat sustain, swell, pluck or slow decay, in the reference mix.
        const int shape = b.weighted ({ 0.55f, 0.35f, 0.06f, 0.04f });
        switch (shape)
        {
            case 0:
                ampEnv (b, kBassAttackLo, kBassAttackHi, 200.f, 600.f, 0.85f, 1.f, 30.f, 120.f);
                filterEnv (b, 0.f, 0.f, 60.f, 200.f, 0.2f, 0.5f, 40.f, 120.f);
                b.uni (P::F1Env, 0.2f, 0.45f);
                break;
            case 1:
                // The amp attack alone rises too fast early on; multiplying it by a
                // falling mod env on Amp gives the slow start of a real swell
                // (about -16 dB at 50 ms, -4 dB at 150 ms).
                ampEnv (b, 150.f, 300.f, 200.f, 600.f, 1.f, 1.f, 40.f, 120.f);
                modEnv (b, 0.f, 0.f, 600.f, 1200.f, 0.f, 0.f, 100.f, 300.f);
                b.route (S::ModEnv, D::Amp, -0.95f, -0.85f);
                filterEnv (b, 120.f, 350.f, 200.f, 500.f, 0.5f, 0.9f, 40.f, 120.f);
                b.uni (P::F1Env, 0.25f, 0.5f);
                break;
            case 2:
                ampEnv (b, kBassAttackLo, 3.f, 120.f, 300.f, 0.f, 0.3f, 60.f, 150.f);
                filterEnv (b, 0.f, 0.f, 60.f, 150.f, 0.f, 0.1f, 40.f, 100.f);
                b.uni (P::F1Env, 0.3f, 0.55f);
                break;
            default:
                ampEnv (b, kBassAttackLo, 3.f, 250.f, 500.f, 0.3f, 0.6f, 60.f, 150.f);
                filterEnv (b, 0.f, 0.f, 100.f, 250.f, 0.1f, 0.3f, 40.f, 100.f);
                b.uni (P::F1Env, 0.3f, 0.5f);
                break;
        }

        // Kick-like punch: a fast drop that settles in 50-100 ms. Swells rarely have one.
        if (b.chance (shape == 1 ? 0.3f : 0.9f))
        {
            b.set (P::PenvAttack, 0.f);
            b.set (P::PenvDest, 0.f);
            switch (b.weighted ({ 0.25f, 0.45f, 0.3f }))
            {
                case 0:  b.uni (P::PenvAmt, 7.f, 12.f); break;
                case 1:  b.uni (P::PenvAmt, 12.f, 24.f); break;
                default: b.uni (P::PenvAmt, 24.f, 40.f); break;
            }
            b.uni (P::PenvDecay, 60.f, 180.f);
            b.uni (P::PenvCurve, 0.3f, 0.6f);   // a gradual fall over ~10 cycles, like a kick body
        }

        b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.35f);
        b.route (S::Macro2, D::PitchEnvAmt, 0.2f, 0.4f);

        fxDist (b, 0.9f, { 0, 3, 1 }, 10.f, 20.f, 0.5f, 0.9f);   // the grit that OTT then lifts
        b.uni (P::DistTone, 9000.f, 18000.f);
        if (b.chance (0.85f)) fxCompMulti (b);
        else fxCompGlue (b);

        // The references' top end is noise, not harmonics: grit that skips the
        // filter, then gets saturated and lifted with the rest. Width spreads it
        // (and the upper body) while everything under ~150 Hz stays mono.
        if (b.chance (0.85f))
        {
            b.on (P::NoiseDirect, true);
            b.pick (P::NoiseType, { 1, 0 }, { 0.7f, 0.3f });
            b.uni (P::NoiseLevel, 0.02f, 0.07f);
        }
        fxWidth (b, 0.85f, 0.35f, 0.6f);
        b.macros ({ D::F1Drive, D::DistDrive, D::NoiseLevel, D::F1Reso, D::WidthAmount });
    }

    // Offbeat bass: one note between the kicks. Modelled on reference notes
    // (C1-C2, one beat each): a big pitch knock, then an 18 dB low-pass that
    // follows the key and closes from a bright pluck to almost a sine over
    // ~300 ms. The level holds while the key is down and falls away slowly
    // after (about -12 dB by the next kick). Mono and dry.
    void offbeatBass (B& b)
    {
        monoVoice (b, false, 0.f, 0.f);
        b.set (P::Octave, -1.f);
        b.set (P::VelSens, 0.2f);
        b.set (P::Accent, 0.f);

        analogA (b, kSaw, 0.85f);
        b.set (P::F1Type, (float) ft (FilterType::LP18));
        b.uni (P::F1Cutoff, 130.f, 175.f);
        b.uni (P::F1Reso, 0.15f, 0.3f);
        b.uni (P::F1Key, 0.9f, 1.f);
        b.uni (P::F1Drive, 0.4f, 0.55f);   // rounds the peaks without losing the even harmonics
        b.uni (P::F1Env, 0.5f, 0.6f);
        filterEnv (b, 0.f, 0.f, 700.f, 900.f, 0.f, 0.f, 60.f, 150.f);
        ampEnv (b, kBassAttackLo, kBassAttackHi, 200.f, 400.f, 0.9f, 1.f, 500.f, 800.f);
        bassKnock (b, 0.9f, 20.f, 26.f, 45.f, 60.f);

        b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.35f);
        b.route (S::Macro2, D::FilterEnvAmt, 0.2f, 0.3f);
        fxCompBass (b);
        b.macros ({ D::F1Reso, D::PitchEnvAmt, D::F1Drive });
    }

    // Progressive bass: rounder and darker than the rolling psy bass. Modelled
    // on sustained reference notes (C1-B2): one saw into a 12 dB low-pass with
    // a little resonance, fixed near 120 Hz whatever the key, a filter burst
    // and a knock at the start. Mono and dry.
    void progBass (B& b)
    {
        monoVoice (b, false, 0.f, 0.f);
        b.set (P::Octave, -1.f);
        b.set (P::VelSens, 0.15f);
        b.set (P::Accent, 0.f);

        analogA (b, kSaw, 0.85f);
        const int type = b.pick (P::F1Type, { ft (FilterType::LP12), ft (FilterType::LadderLP12) }, { 0.7f, 0.3f });
        b.uni (P::F1Cutoff, 110.f, 135.f);
        if (type == ft (FilterType::LP12)) b.uni (P::F1Reso, 0.25f, 0.35f);
        else b.uni (P::F1Reso, 0.1f, 0.2f);
        b.uni (P::F1Key, 0.f, 0.1f);
        b.uni (P::F1Drive, 0.f, 0.2f);
        b.uni (P::F1Env, 0.26f, 0.32f);
        filterEnv (b, 0.f, 0.f, 220.f, 300.f, 0.f, 0.f, 20.f, 40.f);
        ampEnv (b, kBassAttackLo, kBassAttackHi, 80.f, 150.f, 0.9f, 1.f, 15.f, 45.f);
        bassKnock (b, 0.9f, 18.f, 24.f, 30.f, 42.f);

        b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.35f);
        b.route (S::Macro2, D::FilterEnvAmt, 0.2f, 0.3f);
        fxCompBass (b);
        b.macros ({ D::F1Reso, D::PitchEnvAmt, D::F1Drive });
    }
} // namespace

Recipe recipeFor (Category c) noexcept
{
    switch (c)
    {
        case Category::Acid:        return acid;
        case Category::Squelch:     return squelch;
        case Category::Zap:         return zap;
        case Category::Laser:       return laser;
        case Category::PsyBass: return psyBass;
        case Category::PsyLead:     return psyLead;
        case Category::Pluck:       return pluck;
        case Category::Stab:        return stab;
        case Category::Screech:     return screech;
        case Category::AlienVoice:  return alienVoice;
        case Category::Bubble:      return bubble;
        case Category::Gurgle:      return gurgle;
        case Category::Wobble:      return wobble;
        case Category::Metallic:    return metallic;
        case Category::Riser:       return riser;
        case Category::Downlifter:  return downlifter;
        case Category::NoiseFx:     return noiseFx;
        case Category::Atmos:       return atmos;
        case Category::Drone:       return drone;
        case Category::Uplifter:    return uplifter;
        case Category::Impact:      return impact;
        case Category::Gate:        return gate;
        case Category::Glitch:      return glitch;
        case Category::Reverse:     return reverse;
        case Category::BassShot:    return bassShot;
        case Category::OffbeatBass: return offbeatBass;
        case Category::ProgBass:    return progBass;
        case Category::KickFullOn:
        case Category::KickGoa:
        case Category::KickDarkForest:
        case Category::KickHiTech:
        case Category::KickProgressive: return kickRecipeFor (c);
        default:                    return acid;
    }
}

} // namespace psy
