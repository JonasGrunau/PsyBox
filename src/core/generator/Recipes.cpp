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
    }

    void rollingBass (B& b)
    {
        monoVoice (b, false, 0.f, 0.f);
        b.set (P::Octave, -1.f);
        b.set (P::VelSens, 0.2f);
        b.set (P::Accent, 0.2f);

        analogA (b, b.chance (0.7f) ? kSaw : b.rnd (2.4f, 3.f), 0.85f);
        if (b.chance (0.4f))
        {
            analogB (b, kSaw, 0.4f, 0.7f);
            b.uni (P::OscBFine, -12.f, 12.f);
        }
        b.pick (P::SubShape, { 0 });
        b.uni (P::SubLevel, 0.2f, 0.5f);

        b.pick (P::F1Type, { ft (FilterType::Ladder24), ft (FilterType::LP24), ft (FilterType::LP12), ft (FilterType::LadderLP12), ft (FilterType::Diode303) },
               { 0.4f, 0.2f, 0.15f, 0.1f, 0.15f });
        b.uni (P::F1Cutoff, 90.f, 350.f);
        b.uni (P::F1Reso, 0.1f, 0.4f);
        b.uni (P::F1Env, 0.35f, 0.6f);
        b.uni (P::F1Key, 0.5f, 1.f);
        b.uni (P::F1Drive, 0.1f, 0.35f);
        filterEnv (b, 0.f, 0.f, 70.f, 180.f, 0.f, 0.05f, 20.f, 60.f);
        ampEnv (b, 0.f, 1.f, 130.f, 260.f, 0.f, 0.2f, 15.f, 50.f);

        if (b.chance (0.5f))
        {
            b.set (P::PenvAttack, 0.f);
            b.uni (P::PenvAmt, 5.f, 14.f);
            b.uni (P::PenvDecay, 5.f, 25.f);
            b.set (P::PenvCurve, 0.8f);
        }
        b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.35f);
        b.route (S::Macro2, D::FilterEnvAmt, 0.2f, 0.3f);
        fxDist (b, 0.25f, { 0 }, 3.f, 8.f, 0.2f, 0.4f);
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
        ampEnv (b, 5.f, 30.f, 400.f, 800.f, 0.6f, 1.f, 200.f, 600.f);
        b.route (S::Macro1, D::Lfo3Rate, 0.2f, 0.4f);

        fxPhaser (b, 0.5f);
        fxChorus (b, 0.4f);
        fxDelay (b, 0.5f, { D1_16D, D1_8D }, 0.3f, 0.5f, 0.1f, 0.25f);
        fxReverb (b, 0.4f, 1.5f, 4.f, 0.1f, 0.25f);
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
    }

    // Tonal: the pitch climbs one to two octaves while the filter opens.
    // Noise is at most a trace of air; the noise sweep is the Uplifter.
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

        modEnv (b, 2000.f, 8000.f, 1000.f, 1000.f, 1.f, 1.f, 300.f, 1500.f);
        b.route (S::ModEnv, D::PitchAll, 0.45f, 1.f);
        filter1 (b, ft (FilterType::LP12), 400.f, 1500.f, 0.2f, 0.5f, 0.f, 0.f);
        b.route (S::ModEnv, D::F1Cutoff, 0.4f, 0.7f);
        if (b.chance (0.5f))
        {
            // spiral: a wobble that speeds up as the pitch climbs
            b.lfo (1, LfoShape::Sine, 2.f, 5.f, true);
            if (b.chance (0.5f)) b.route (S::Lfo2, D::PitchAll, 0.02f, 0.06f);
            else b.route (S::Lfo2, D::F1Cutoff, 0.05f, 0.15f);
            b.route (S::ModEnv, D::Lfo2Rate, 0.3f, 0.6f);
        }
        ampEnv (b, 1000.f, 6000.f, 1000.f, 1000.f, 1.f, 1.f, 500.f, 2000.f);
        b.route (S::Macro1, D::F1Reso, 0.2f, 0.4f);

        fxReverb (b, 0.9f, 4.f, 10.f, 0.3f, 0.5f);
        fxDelay (b, 0.7f, { D1_8D, D1_4, D1_8 }, 0.35f, 0.6f, 0.15f, 0.3f);
        fxPhaser (b, 0.3f);
        fxDist (b, 0.3f, { 0, 3 }, 3.f, 12.f, 0.2f, 0.5f);
        fxShift (b, 0.2f, 5.f, 200.f, 0.2f, 0.6f);
    }

    // Noise sweep: the filter opens upward and the pitch stays put.
    void uplifter (B& b)
    {
        polyVoice (b, 2, 4);
        b.pick (P::NoiseType, { 0, 1 }, { 0.6f, 0.4f });
        b.uni (P::NoiseLevel, 0.8f, 1.f);
        if (b.chance (0.2f)) { supersawA (b, 0.3f, 0.7f); b.uni (P::OscALevel, 0.05f, 0.15f); }
        else b.on (P::OscAOn, false);

        filter1 (b, b.chance (0.4f) ? ft (FilterType::BP12) : (b.chance (0.5f) ? ft (FilterType::HP12) : ft (FilterType::LP12)),
                 150.f, 500.f, 0.2f, 0.6f, 0.f, 0.f);
        modEnv (b, 2000.f, 8000.f, 1000.f, 1000.f, 1.f, 1.f, 300.f, 1500.f);
        b.route (S::ModEnv, D::F1Cutoff, 0.6f, 0.95f);
        if (b.chance (0.4f))
        {
            // a high-pass that follows, so the bottom thins out as it rises
            b.set (P::F2Type, (float) ft (FilterType::HP12));
            b.uni (P::F2Cutoff, 100.f, 300.f);
            b.route (S::ModEnv, D::F2Cutoff, 0.3f, 0.6f);
        }
        if (b.chance (0.5f))
        {
            b.lfoSync (1, LfoShape::Triangle, { D1_16, D1_8, D1_16T }, false);
            b.route (S::Lfo2, D::F1Cutoff, 0.05f, 0.15f);
        }
        ampEnv (b, 1000.f, 6000.f, 1000.f, 1000.f, 1.f, 1.f, 500.f, 2000.f);
        b.route (S::Macro1, D::F1Reso, 0.2f, 0.4f);

        fxPhaser (b, 0.5f);
        fxShift (b, 0.4f, 5.f, 200.f, 0.2f, 0.6f);
        fxReverb (b, 0.9f, 4.f, 10.f, 0.3f, 0.5f);
        fxDelay (b, 0.5f, { D1_8D, D1_4 }, 0.3f, 0.55f, 0.15f, 0.3f);
    }

    void downlifter (B& b)
    {
        if (b.chance (0.5f)) polyVoice (b, 2, 4);
        else monoVoice (b, false, 0.f, 0.f);
        b.pick (P::NoiseType, { 0, 1 });
        b.uni (P::NoiseLevel, 0.3f, 0.8f);
        if (b.chance (0.5f)) supersawA (b, 0.2f, 0.6f);
        else analogA (b, kSaw);

        b.set (P::PenvAttack, 0.f);
        b.set (P::PenvDest, 0.f);
        b.uni (P::PenvAmt, 24.f, 48.f);
        b.uni (P::PenvDecay, 1500.f, 6000.f);
        b.uni (P::PenvCurve, 0.3f, 0.7f);

        filter1 (b, ft (FilterType::LP12), 200.f, 800.f, 0.2f, 0.6f, 0.f, 0.f);
        modEnv (b, 0.f, 0.f, 2000.f, 6000.f, 0.f, 0.f, 500.f, 1500.f);
        b.route (S::ModEnv, D::F1Cutoff, 0.5f, 0.9f);
        ampEnv (b, 0.f, 50.f, 2000.f, 6000.f, 0.f, 0.f, 500.f, 2000.f);
        b.route (S::Macro1, D::PitchEnvAmt, 0.2f, 0.4f);

        fxReverb (b, 0.9f, 3.f, 9.f, 0.25f, 0.45f);
        fxDelay (b, 0.6f, { D1_8D, D1_4 }, 0.3f, 0.55f, 0.15f, 0.3f);
        if (b.chance (0.4f))
        {
            b.on (P::FshiftOn, true);
            b.uni (P::FshiftHz, -400.f, -50.f);
            b.uni (P::FshiftFb, 0.2f, 0.6f);
            b.uni (P::FshiftMix, 0.3f, 0.6f);
        }
    }

    void noiseFx (B& b)
    {
        polyVoice (b, 2, 4);
        b.pick (P::NoiseType, { 0, 1, 2 }, { 0.45f, 0.4f, 0.15f });
        b.uni (P::NoiseLevel, 0.8f, 1.f);
        if (b.chance (0.3f)) { analogA (b, kSaw, 0.2f); b.uni (P::OscALevel, 0.05f, 0.25f); }
        else b.on (P::OscAOn, false);

        const int f = b.pick (P::F1Type, { ft (FilterType::BP12), ft (FilterType::BP24), ft (FilterType::Notch), ft (FilterType::LadderBP),
                                           ft (FilterType::CombPos), ft (FilterType::Formant) },
                              { 0.25f, 0.15f, 0.15f, 0.15f, 0.15f, 0.15f });
        (void) f;
        b.uni (P::F1Cutoff, 300.f, 4000.f);
        b.uni (P::F1Reso, 0.3f, 0.9f);
        b.uni (P::F1Vowel, 0.f, 1.f);
        if (b.chance (0.5f)) b.lfo (0, LfoShape::Sine, 0.1f, 1.f, true);
        else b.lfoSync (0, LfoShape::Triangle, { D1_2, D1_1, D2_1 }, true);
        b.route (S::Lfo1, D::F1Cutoff, 0.3f, 0.7f);
        if (b.chance (0.3f))
        {
            b.set (P::F2Type, (float) ft (FilterType::HP12));
            b.uni (P::F2Cutoff, 200.f, 1000.f);
        }
        ampEnv (b, 50.f, 1500.f, 1000.f, 1000.f, 0.6f, 1.f, 300.f, 2000.f);
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
    }

    // A boom that falls into the sub: bright noise transient, seconds of
    // tail. Deeper and longer than a Zap, and the tail outlasts the note.
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

        b.pick (P::NoiseType, { 0, 1 });
        b.uni (P::NoiseLevel, 0.2f, 0.5f);
        filter1 (b, ft (FilterType::LP12), 300.f, 1000.f, 0.f, 0.25f, 0.4f, 0.8f);
        filterEnv (b, 0.f, 0.f, 80.f, 400.f, 0.f, 0.f, 100.f, 400.f);
        ampEnv (b, 0.f, 0.f, 1500.f, 5000.f, 0.f, 0.f, 1500.f, 5000.f);
        b.route (S::Macro1, D::PitchEnvAmt, 0.2f, 0.4f);
        b.route (S::Velocity, D::F1Cutoff, 0.1f, 0.25f);

        fxDist (b, 0.5f, { 0, 3 }, 3.f, 9.f, 0.2f, 0.5f);
        fxReverb (b, 0.9f, 4.f, 12.f, 0.25f, 0.45f);
        fxDelay (b, 0.2f, { D1_4, D1_4D }, 0.2f, 0.4f, 0.1f, 0.2f);
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
    }

    // Swells and cuts at the peak like a reversed hit: the amp envelope
    // drops to nothing a few ms after its attack, whatever the note length.
    // The filter opens with it. Little reverb, so no tail softens the cut.
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

        filter1 (b, ft (FilterType::LP12), 300.f, 1200.f, 0.1f, 0.4f, 0.4f, 0.8f);
        filterEnv (b, 300.f, 2000.f, 1000.f, 1000.f, 1.f, 1.f, 20.f, 80.f);
        ampEnv (b, 300.f, 1800.f, 5.f, 40.f, 0.f, 0.f, 5.f, 40.f);
        b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.4f);

        fxReverb (b, 0.5f, 1.f, 3.f, 0.08f, 0.2f);
        fxDelay (b, 0.3f, { D1_8D, D1_4 }, 0.2f, 0.4f, 0.1f, 0.2f);
        fxChorus (b, 0.3f, false);
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
                ampEnv (b, 0.f, 3.f, 200.f, 600.f, 0.85f, 1.f, 30.f, 120.f);
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
                ampEnv (b, 0.f, 2.f, 120.f, 300.f, 0.f, 0.3f, 60.f, 150.f);
                filterEnv (b, 0.f, 0.f, 60.f, 150.f, 0.f, 0.1f, 40.f, 100.f);
                b.uni (P::F1Env, 0.3f, 0.55f);
                break;
            default:
                ampEnv (b, 0.f, 2.f, 250.f, 500.f, 0.3f, 0.6f, 60.f, 150.f);
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
        case Category::RollingBass: return rollingBass;
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
        case Category::KickFullOn:
        case Category::KickGoa:
        case Category::KickDarkForest:
        case Category::KickHiTech:
        case Category::KickProgressive: return kickRecipeFor (c);
        default:                    return acid;
    }
}

} // namespace psy
