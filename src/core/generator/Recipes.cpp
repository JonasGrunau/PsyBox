// Sound-design recipes, one per category.
//
// A recipe describes the *space* of a category: which modules are involved
// and the ranges that keep a sound recognisably "acid" or "zap". Ranges are
// sampled in each parameter's own curve (log for cutoffs, power for times).
// Every value set here remains a normal, editable parameter afterwards.

#include "Generator.h"
#include "RecipeCommon.h"

namespace psy
{

using namespace recipe;

namespace
{
    using B = RecipeBuilder;
    using S = ModSrc;
    using D = ModDst;

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

    // A synced LFO whose division is drawn with weights.
    void lfoSyncW (B& b, int index, LfoShape shape, std::initializer_list<int> divs, std::initializer_list<float> weights, bool retrig)
    {
        b.set (lfoParam (index, 0), (float) shape);
        b.set (lfoParam (index, 2), 1.f);
        b.pick (lfoParam (index, 3), divs, weights);
        b.set (lfoParam (index, 4), retrig ? 1.f : 0.f);
    }

    // ---- effects --------------------------------------------------------------

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

    // ---- EQ, dimension and drawn LFOs, as the Serum corpus uses them -----------
    // Nearly every psy preset in the corpus runs its EQ. The low band cuts in
    // nine of ten: a 12 dB high-pass (24 dB in some) between cutLo and cutHi
    // (200 Hz in the middle for leads), or a shelf 4-24 dB down below
    // 90-240 Hz (`lowShelf` of them). The high band shapes the top in `high`
    // of them: a bell at 1.5-8 kHz or a shelf at 1.2-6 kHz, mostly 2-8 dB up
    // (a dip in `dip` of them), or a low-pass at 4-17 kHz that rounds it off;
    // `highKinds` weighs bell, shelf and low-pass.
    void fxEq (B& b, float on, float lowShelf, float cutLo, float cutHi, float high, std::initializer_list<float> highKinds,
               float dip = 0.25f, float lpLo = 4000.f, float lpHi = 17000.f)
    {
        if (! b.chance (on)) return;
        b.on (P::EqOn, true);
        if (b.chance (lowShelf))
        {
            b.set (P::EqLowType, (float) EqBand::Shelf);
            b.uni (P::EqLowFreq, 90.f, 240.f);
            b.uni (P::EqLowGain, -24.f, -4.f);
            b.uni (P::EqLowQ, 0.5f, 0.9f);
        }
        else
        {
            b.pick (P::EqLowType, { (int) EqBand::Cut12, (int) EqBand::Cut24 }, { 0.75f, 0.25f });
            b.uni (P::EqLowFreq, cutLo, cutHi);
            b.uni (P::EqLowQ, 0.55f, 0.9f);
        }
        if (! b.chance (high)) return;
        const int kind = b.weighted (highKinds);
        const bool down = b.chance (dip);
        if (kind == 0)
        {
            b.set (P::EqHighType, (float) EqBand::Bell);
            b.uni (P::EqHighFreq, 1500.f, 8000.f);
            if (down) b.uni (P::EqHighGain, -10.f, -3.f);
            else b.uni (P::EqHighGain, 2.f, 8.f);
            b.uni (P::EqHighQ, 0.7f, 2.f);
        }
        else if (kind == 1)
        {
            b.set (P::EqHighType, (float) EqBand::Shelf);
            b.uni (P::EqHighFreq, 1200.f, 6000.f);
            if (down) b.uni (P::EqHighGain, -9.f, -3.f);
            else b.uni (P::EqHighGain, 2.f, 7.f);
            b.uni (P::EqHighQ, 0.5f, 0.9f);
        }
        else
        {
            b.pick (P::EqHighType, { (int) EqBand::Cut12, (int) EqBand::Cut24 }, { 0.8f, 0.2f });
            b.uni (P::EqHighFreq, lpLo, lpHi);
            b.uni (P::EqHighQ, 0.6f, 1.f);
        }
    }

    // The dimension expander: on in a fifth to a third of the corpus's
    // presets of most kinds, about a quarter wet (pads half).
    void fxDim (B& b, float p, float mixLo, float mixHi)
    {
        if (! b.chance (p)) return;
        b.on (P::DimOn, true);
        b.uni (P::DimSize, 0.15f, 0.75f);
        b.uni (P::DimMix, mixLo, mixHi);
    }

    // A drawn LFO curve (see dsp::LfoCurve): points of x, y and bend.
    struct Pt { float x, y, bend; };
    void drawLfo (B& b, int lfo, std::initializer_list<Pt> pts)
    {
        b.set (lfoParam (lfo, 0), (float) LfoShape::Drawn);
        b.set (lfoPointsParam (lfo), (float) pts.size());
        int k = 0;
        for (const auto& pt : pts)
        {
            b.set (lfoPointParam (lfo, k, LfoLane::X), pt.x);
            b.set (lfoPointParam (lfo, k, LfoLane::Y), pt.y);
            b.set (lfoPointParam (lfo, k, LfoLane::Bend), pt.bend);
            ++k;
        }
    }

    // A step pattern drawn as held points, synced (Retrig off keeps it on the
    // host's bar): `count` steps, each one of `levels`.
    void drawSteps (B& b, int lfo, int count, std::initializer_list<float> levels)
    {
        count = std::clamp (count, 2, kLfoPoints);
        b.set (lfoParam (lfo, 0), (float) LfoShape::Drawn);
        b.set (lfoPointsParam (lfo), (float) count);
        const int n = (int) levels.size();
        for (int k = 0; k < count; ++k)
        {
            const int pick = std::min ((int) (b.rnd (0.f, 1.f) * (float) n), n - 1);
            b.set (lfoPointParam (lfo, k, LfoLane::X), (float) k / (float) count);
            b.set (lfoPointParam (lfo, k, LfoLane::Y), levels.begin()[pick]);
            b.set (lfoPointParam (lfo, k, LfoLane::Bend), 1.f);
        }
    }

    // An LFO run once from each note as an extra envelope, the way a fifth to
    // a third of the corpus's leads, plucks and stabs use one: drawn up from
    // rest to a peak early on, then back down fast-then-slow, and held there.
    void lfoEnvelope (B& b, int lfo, std::initializer_list<int> divs, std::initializer_list<float> weights)
    {
        const float peak = b.rnd (0.06f, 0.45f), rise = b.rnd (-0.4f, 0.2f), fall = b.rnd (-0.7f, -0.2f);
        drawLfo (b, lfo, { { 0.f, 0.f, rise }, { peak, 1.f, fall }, { 1.f, 0.f, 0.f } });
        b.set (lfoParam (lfo, 2), 1.f);
        b.pick (lfoParam (lfo, 3), divs, weights);
        b.set (lfoParam (lfo, 4), 1.f);
        b.on (lfoEnvParam (lfo), true);
    }

    // =========================================================================
    // Recipes
    // =========================================================================

    // Psy acid, fitted to dry psy trance acid loops (Cr2's Psy Trance Acid
    // Rave): the lows cut at 300-550 Hz before the distortion, so the
    // fundamental sits 15-30 dB under the loudest harmonic; the filter driven
    // and the line clipped or folded after it, so h2-h8 sit nearly level and
    // the top falls ~2 dB an octave band instead of the 6 of a clean ladder.
    // Less resonance, a short shallow filter sweep and a slow LFO: the grit
    // carries the line, not the squelch. The Goa kind keeps more resonance,
    // a lower cut, a softer drive and a phaser or a dotted delay.
    void psyAcid (B& b)
    {
        const bool goa = b.chance (0.3f);
        monoVoice (b, true, 15.f, goa ? 70.f : 45.f);
        b.uni (P::Accent, 0.6f, 1.f);
        b.pick (P::Octave, { -1, 0 }, { 0.5f, 0.5f });
        b.set (P::VelSens, 0.25f);

        analogA (b, b.chance (0.55f) ? kSaw : kSquare, 0.85f);
        // a second oscillator a few cents off beats against the first in the clipper
        if (b.chance (0.3f))
        {
            analogB (b, b.chance (0.5f) ? kSaw : kSquare, 0.3f, 0.6f);
            b.among (P::OscBCoarse, { 0.f, 0.f, 12.f, -12.f });
            b.uni (P::OscBFine, 4.f, 14.f);
        }

        b.pick (P::F1Type, { ft (FilterType::Diode303), ft (FilterType::Ladder24), ft (FilterType::LP24) }, { 0.5f, 0.3f, 0.2f });
        b.uni (P::F1Cutoff, goa ? 600.f : 1000.f, goa ? 2000.f : 3000.f);
        b.uni (P::F1Reso, goa ? 0.5f : 0.3f, goa ? 0.8f : 0.6f);
        b.uni (P::F1Env, goa ? 0.3f : 0.15f, goa ? 0.6f : 0.45f);
        b.uni (P::F1Drive, goa ? 0.4f : 0.6f, 1.f);
        b.uni (P::F1Key, 0.3f, 0.6f);
        filterEnv (b, 0.f, 2.f, 80.f, goa ? 450.f : 300.f, 0.f, 0.15f, 30.f, 100.f);
        ampEnv (b, 0.5f, 2.f, 300.f, 900.f, 0.7f, 1.f, 20.f, 60.f);

        // the cut before the clipper, mostly fixed in Hz to leave the bass its room
        b.set (P::FRouting, 0.f);
        if (goa) b.pick (P::F2Type, { ft (FilterType::HP12), ft (FilterType::HP18) });
        else b.pick (P::F2Type, { ft (FilterType::HP18), ft (FilterType::HP24) }, { 0.3f, 0.7f });
        const float cut = b.uni (P::F2Cutoff, goa ? 200.f : 350.f, goa ? 380.f : 600.f);
        b.uni (P::F2Reso, 0.05f, 0.3f);
        b.uni (P::F2Key, 0.f, 0.3f);

        if (b.chance (goa ? 0.4f : 0.6f))
        {
            b.lfoSync (0, b.chance (0.5f) ? LfoShape::Triangle : LfoShape::Sine, { D4_1, D2_1, D1_1 }, false);
            b.route (S::Lfo1, D::F1Cutoff, 0.05f, 0.2f);
        }
        b.route (S::Macro1, D::F1Cutoff, 0.3f, 0.5f);
        b.route (S::Macro2, D::F1Reso, 0.25f, 0.35f);

        // The references' top falls ~2 dB an octave band, flatter than a
        // clipped wave (~5): a fold gets there on its own, a clipper with the
        // OTT's lifted highs. Auto Level drives every patch the same, so a
        // quiet filter output still clips and the accents keep their jump.
        b.on (P::DistOn, true);
        int dist = (int) DistType::Tube;
        float drive = 0.f;
        if (goa)
        {
            dist = b.pick (P::DistType, { (int) DistType::Tube, (int) DistType::Hard, (int) DistType::Soft }, { 0.5f, 0.3f, 0.2f });
            b.uni (P::DistDrive, 12.f, 22.f);
            b.uni (P::DistMix, 0.6f, 0.9f);
        }
        else
        {
            dist = b.pick (P::DistType, { (int) DistType::Fold, (int) DistType::Hard, (int) DistType::Tube, (int) DistType::Rectify },
                           { 0.4f, 0.25f, 0.2f, 0.15f });
            drive = b.uni (P::DistDrive, dist == (int) DistType::Fold ? 16.f : 18.f, dist == (int) DistType::Fold ? 26.f : 30.f);
            b.uni (P::DistMix, 0.85f, 1.f);
            b.on (P::DistAuto, true);
        }
        b.uni (P::DistTone, goa ? 6000.f : 10000.f, goa ? 12000.f : 16000.f);
        b.uni (P::DistLowCut, 0.4f * cut, 0.6f * cut);

        // Auto Level gives the clipper's output back about (drive - 18) dB
        // under a line already quiet without its lows; the compressor after
        // it makes that up, so the leveller has room left (it tops out at
        // +24 dB, and a fold without the OTT needed all of it).
        if (! goa)
        {
            const bool clipped = dist == (int) DistType::Hard || dist == (int) DistType::Tube;
            const bool ott = b.chance (clipped ? 0.9f : 0.5f);
            if (ott) fxCompMulti (b);
            else fxCompGlue (b);
            const float makeup = drive - (ott ? 12.f : 4.f);
            // fxCompGlue drew CompOut already (4-8 dB); this second draw overrides
            // it, and stays so the seeds keep their values
            b.uni (P::CompOut, std::min (24.f, makeup - 2.f), std::min (24.f, makeup + 2.f));
        }
        fxWidth (b, 0.7f, 0.2f, 0.5f);
        fxPhaser (b, goa ? 0.4f : 0.1f);
        fxDelay (b, goa ? 0.7f : 0.4f, { D1_8D, D1_16D, D1_8 }, 0.15f, 0.35f, 0.06f, 0.16f);
        fxReverb (b, 0.15f, 0.6f, 1.5f, 0.04f, 0.1f);
        b.macros ({ D::F2Cutoff, D::DistDrive, D::FilterEnvAmt, D::DelayMix, D::PhaserMix });
    }

    void acid (B& b)
    {
        // the two acids: the psy one, dirty and cut, and the classic wet 303
        if (b.chance (0.55f)) { psyAcid (b); return; }

        monoVoice (b, true, 40.f, 95.f);
        b.uni (P::Accent, 0.5f, 0.95f);
        b.pick (P::Octave, { -1, 0 }, { 0.35f, 0.65f });
        b.set (P::VelSens, 0.25f);

        analogA (b, b.chance (0.65f) ? kSaw : kSquare, 0.85f);
        if (b.chance (0.2f)) b.uni (P::SubLevel, 0.1f, 0.3f);

        b.pick (P::F1Type, { ft (FilterType::Diode303), ft (FilterType::Ladder24) }, { 0.8f, 0.2f });
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

    // Zaps, refitted to the corpus's 48 zap presets: a pitch dive from two to
    // five octaves up (Serum draws it as an LFO; here the pitch envelope), an
    // FM pair or a plain wave with a second osc an octave or two down in
    // some, through a band-pass or ladder that follows the dive. Poly, driven
    // hard, squashed by a multiband compressor, widened, and an echo on the
    // dotted quarter.
    void zap (B& b)
    {
        if (b.chance (0.9f)) polyVoice (b, 4, 8);
        else monoVoice (b, false, 0.f, 0.f);
        b.pick (P::Octave, { -1, 0, 1 }, { 0.3f, 0.5f, 0.2f });

        if (b.chance (0.5f))
            fmA (b, { 0.5f, 1.f, 2.f, 3.5f, 7.f }, 0.5f, 6.f, 0.f, 0.4f);
        else
            analogA (b, b.rnd (0.f, 1.2f));
        const bool second = b.chance (0.4f);
        if (second)
        {
            analogB (b, b.chance (0.5f) ? kSaw : kSine, 0.3f, 0.6f);
            b.pick (P::OscBCoarse, { 0, -24, -12, 12 }, { 0.42f, 0.22f, 0.22f, 0.14f });
        }

        b.set (P::PenvAttack, 0.f);
        if (b.chance (0.9f)) b.uni (P::PenvAmt, 24.f, 60.f);
        else b.uni (P::PenvAmt, -48.f, -24.f);
        b.uni (P::PenvDecay, 20.f, 200.f);
        b.uni (P::PenvCurve, 0.6f, 1.f);
        b.set (P::PenvDest, 0.f);
        // a third draw the dive the way Serum's zaps do, on an LFO run once:
        // down fast, then slower (through a knee in some), over a quarter to
        // two bars and two to four octaves, in place of the pitch envelope
        const bool drawn = b.chance (0.35f);
        if (drawn)
        {
            const bool knee = b.chance (0.4f);
            const float b0 = b.rnd (-0.8f, -0.3f), kx = b.rnd (0.1f, 0.35f), ky = b.rnd (0.15f, 0.45f), b1 = b.rnd (-0.5f, 0.3f);
            if (knee) drawLfo (b, 0, { { 0.f, 1.f, b0 }, { kx, ky, b1 }, { 1.f, 0.f, 0.f } });
            else drawLfo (b, 0, { { 0.f, 1.f, b0 }, { 1.f, 0.f, 0.f } });
            b.set (lfoParam (0, 2), 1.f);
            b.pick (lfoParam (0, 3), { D1_4, D1_2, D1_1, D2_1 }, { 0.3f, 0.35f, 0.25f, 0.1f });
            b.set (lfoParam (0, 4), 1.f);
            b.on (lfoEnvParam (0), true);
            b.set (P::PenvAmt, 0.f);
            // a route reaches two octaves: two of them
            b.route (S::Lfo1, D::PitchAll, 0.5f, 1.f);
            b.route (S::Lfo1, D::PitchAll, 0.5f, 1.f);
        }
        const S dive = drawn ? S::Lfo1 : S::PitchEnv;

        if (b.chance (0.15f)) { b.pick (P::NoiseType, { 0, 2 }); b.uni (P::NoiseLevel, 0.02f, 0.12f); }

        // band-pass or ladder mostly, the cutoff falling with the pitch
        switch (b.weighted ({ 0.42f, 0.21f, 0.12f, 0.1f, 0.1f, 0.05f }))
        {
            case 0: filter1 (b, ft (FilterType::BP12), 300.f, 3000.f, 0.2f, 0.6f, 0.f, 0.f); break;
            case 1: filter1 (b, ft (FilterType::Ladder24), 200.f, 2500.f, 0.2f, 0.6f, 0.f, 0.f); break;
            case 2: filter1 (b, ft (FilterType::HP12), 60.f, 300.f, 0.1f, 0.4f, 0.f, 0.f); break;
            case 3: filter1 (b, ft (FilterType::LP12), 1500.f, 8000.f, 0.1f, 0.4f, 0.f, 0.f); break;
            case 4: b.set (P::F1Type, (float) ft (FilterType::Off)); break;
            default:
                filter1 (b, ft (FilterType::CombPos), 200.f, 1200.f, 0.4f, 0.7f, 0.f, 0.f);
                b.uni (P::F1Key, 0.8f, 1.f);
                break;
        }
        const bool follow = b.chance (0.65f);
        if (follow) b.route (dive, D::F1Cutoff, 0.15f, 0.4f);

        const bool ring = b.chance (0.4f);   // the bottom of the dive rings on in some
        ampEnv (b, 0.f, 1.f, 120.f, 600.f, 0.f, ring ? 0.3f : 0.f, 15.f, 60.f);
        if (b.chance (0.3f)) b.route (dive, D::FmA, 0.2f, 0.5f);
        b.route (S::Macro1, D::PitchEnvAmt, 0.2f, 0.4f);

        fxDist (b, 0.67f, { (int) DistType::Tube, (int) DistType::Soft, (int) DistType::Rectify, (int) DistType::Hard, (int) DistType::Fold },
                8.f, 20.f, 0.6f, 1.f);
        const int squash = b.weighted ({ 0.73f, 0.12f, 0.15f });   // multiband, glue, none
        if (squash == 0) fxCompMulti (b);
        else if (squash == 1) fxCompGlue (b);
        fxDelay (b, 0.9f, { D1_4D, D1_4D, D1_4D, D1_4D, D1_4D, D1_4, D1_4, D1_8D, D1_2 }, 0.17f, 0.65f, 0.14f, 0.3f);
        fxReverb (b, 0.67f, 1.f, 3.f, 0.05f, 0.2f);
        fxChorus (b, 0.33f);
        fxWidth (b, 0.44f, 0.3f, 0.8f);
        fxShift (b, 0.15f, 20.f, 300.f, 0.f, 0.5f);
        fxEq (b, 0.8f, 0.06f, 80.f, 250.f, 0.6f, { 0.4f, 0.18f, 0.42f });
        fxDim (b, 0.29f, 0.2f, 0.5f);
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

    // Psy leads, refitted to the corpus's 400 lead presets: a saw, square,
    // wavetable or FM voice (a supersaw now and then) with a second osc on
    // the note or an octave away in most, mono in half and gliding in a
    // third; a ladder or band-pass that a synced LFO sweeps in half (an
    // envelope snaps it in a few); driven, squashed by a multiband or glue
    // compressor, a dotted-eighth echo and a little room. A noise layer or
    // a sub in one of six, a 16th tremolo or a vibrato now and then.
    void psyLead (B& b)
    {
        const int voice = b.weighted ({ 0.48f, 0.16f, 0.36f });   // poly, mono, legato with glide
        if (voice == 0) polyVoice (b, 6, 8);
        else monoVoice (b, voice == 2, voice == 2 ? 30.f : 0.f, voice == 2 ? 140.f : 0.f);

        const int kind = b.weighted ({ 0.35f, 0.25f, 0.22f, 0.18f });
        switch (kind)
        {
            case 0: analogA (b, b.chance (0.65f) ? kSaw : kSquare); break;
            case 1:
            {
                wavetableA (b, { 0, 1, 5, 7, 4 }, 0.2f, 0.8f);
                const bool scan = b.chance (0.6f);
                if (scan)
                {
                    lfoSyncW (b, 2, LfoShape::Triangle, { D2_1, D1_1, D4_1, D1_4, D1_2 }, { 0.25f, 0.25f, 0.2f, 0.15f, 0.15f }, false);
                    b.route (S::Lfo3, D::WtPosA, 0.15f, 0.4f);
                }
                break;
            }
            case 2:   // FM: osc A's own pair, its index on an envelope or the bars
            {
                fmA (b, { 1.f, 2.f, 3.f, 0.5f, 1.5f }, 0.5f, 5.f, 0.f, 0.4f);
                const bool bite = b.chance (0.5f);
                if (bite)
                {
                    modEnv (b, 0.f, 2.f, 80.f, 600.f, 0.f, 0.4f, 30.f, 200.f);
                    b.route (S::ModEnv, D::FmA, 0.15f, 0.4f);
                }
                else
                {
                    lfoSyncW (b, 2, LfoShape::Triangle, { D1_1, D2_1, D1_4 }, { 0.4f, 0.35f, 0.25f }, false);
                    b.route (S::Lfo3, D::FmA, 0.1f, 0.3f);
                }
                break;
            }
            default: supersawA (b, 0.2f, 0.6f); break;
        }
        const bool second = b.chance (0.7f);
        if (second)
        {
            analogB (b, b.chance (0.6f) ? kSaw : kSquare, 0.4f, 0.7f);
            b.pick (P::OscBCoarse, { 0, 12, -12, -24, 24, 7 }, { 0.52f, 0.22f, 0.14f, 0.04f, 0.04f, 0.04f });
            b.uni (P::OscBFine, 4.f, 15.f);
        }
        const bool air = b.chance (0.17f);
        if (air) { b.pick (P::NoiseType, { 0, 1, 3 }); b.uni (P::NoiseLevel, 0.02f, 0.1f); }
        const bool sub = b.chance (0.16f);
        if (sub) b.uni (P::SubLevel, 0.1f, 0.3f);

        // the filter: ladder or band-pass mostly, swept by a synced LFO in half
        const int fil = b.weighted ({ 0.3f, 0.27f, 0.12f, 0.1f, 0.1f, 0.06f, 0.05f });
        const bool snap = b.chance (0.3f);
        const float eLo = snap ? 0.15f : 0.f, eHi = snap ? 0.4f : 0.05f;
        switch (fil)
        {
            case 0: filter1 (b, ft (FilterType::Ladder24), 800.f, 5000.f, 0.05f, 0.6f, eLo, eHi); break;
            case 1: filter1 (b, ft (FilterType::BP12), 600.f, 3000.f, 0.15f, 0.6f, eLo, eHi); break;
            case 2: filter1 (b, ft (FilterType::LP12), 1200.f, 7000.f, 0.05f, 0.5f, eLo, eHi); break;
            case 3: filter1 (b, ft (FilterType::HP12), 100.f, 500.f, 0.05f, 0.4f, eLo * 0.5f, eHi * 0.5f); break;
            case 4: b.set (P::F1Type, (float) ft (FilterType::Off)); break;
            case 5:
                filter1 (b, ft (FilterType::Formant), 700.f, 2000.f, 0.3f, 0.7f, eLo, eHi);
                b.uni (P::F1Vowel, 0.f, 1.f);
                break;
            default:
                filter1 (b, ft (FilterType::CombPos), 200.f, 1200.f, 0.3f, 0.7f, eLo * 0.5f, eHi * 0.5f);
                b.uni (P::F1Key, 0.8f, 1.f);
                break;
        }
        filterEnv (b, 0.f, 10.f, 200.f, 900.f, 0.f, 0.4f, 30.f, 200.f);
        const bool sweep = b.chance (0.55f);
        if (sweep && fil != 4)
        {
            lfoSyncW (b, 1, b.chance (0.65f) ? LfoShape::Triangle : LfoShape::Sine, { D2_1, D1_1, D4_1, D1_4, D1_2 },
                      { 0.25f, 0.25f, 0.2f, 0.15f, 0.15f }, false);
            b.route (S::Lfo2, D::F1Cutoff, 0.1f, 0.35f);
        }
        const bool drive = b.chance (0.5f);
        if (drive) b.uni (P::F1Drive, 0.f, 0.5f);
        ampEnv (b, 0.5f, 6.f, 300.f, 1000.f, 0.6f, 1.f, 15.f, 150.f);

        // a vibrato, or a 16th tremolo, now and then
        const int life = b.weighted ({ 0.65f, 0.2f, 0.15f });
        if (life == 1)
        {
            b.lfo (0, LfoShape::Sine, 4.5f, 6.5f, false);
            b.route (S::Lfo1, D::PitchAll, 0.004f, 0.01f);
        }
        else if (life == 2)
        {
            lfoSyncW (b, 0, b.chance (0.5f) ? LfoShape::Triangle : LfoShape::Square, { D1_16, D1_8 }, { 0.7f, 0.3f }, true);
            b.route (S::Lfo1, D::Amp, 0.3f, 0.6f);
        }
        const bool bright = b.chance (0.12f);
        if (bright) b.route (S::Velocity, D::F1Cutoff, 0.05f, 0.15f);
        b.route (S::ModWheel, D::F1Cutoff, 0.2f, 0.4f);
        b.route (S::Macro1, D::F1Cutoff, 0.25f, 0.45f);

        fxDist (b, 0.6f, { (int) DistType::Tube, (int) DistType::Soft, (int) DistType::Hard, (int) DistType::Fold }, 4.f, 16.f, 0.4f, 1.f);
        if (b.chance (0.1f))
        {
            b.on (P::CrushOn, true);
            b.uni (P::CrushBits, 6.f, 12.f);
            b.uni (P::CrushRate, 1.f, 6.f);
            b.uni (P::CrushMix, 0.2f, 0.5f);
        }
        const int squash = b.weighted ({ 0.48f, 0.3f, 0.22f });   // multiband, glue, none
        if (squash == 0) fxCompMulti (b);
        else if (squash == 1) fxCompGlue (b);
        fxDelay (b, 0.65f, { D1_8D, D1_8D, D1_8D, D1_8D, D1_4, D1_4, D1_8, D1_4D }, 0.2f, 0.6f, 0.15f, 0.45f);
        fxReverb (b, 0.65f, 2.f, 5.f, 0.12f, 0.34f);
        fxChorus (b, 0.29f);
        fxPhaser (b, 0.09f);
        fxWidth (b, 0.3f, 0.3f, 0.7f);
        // an extra filter envelope from LFO 2 run once, in a fifth (where it doesn't sweep)
        const bool lfoEnv = b.chance (0.2f);
        if (lfoEnv && ! sweep && fil != 4)
        {
            lfoEnvelope (b, 1, { D1_4, D1_2, D1_8 }, { 0.6f, 0.25f, 0.15f });
            b.route (S::Lfo2, D::F1Cutoff, 0.08f, 0.25f);
        }
        fxEq (b, 0.92f, 0.09f, 100.f, 340.f, 0.73f, { 0.44f, 0.28f, 0.28f });
        fxDim (b, 0.3f, 0.15f, 0.45f);
        b.macros ({ D::F1Reso, D::WtPosA, D::FmA, D::DetuneA, D::FilterEnvAmt, D::DelayMix, D::ReverbMix, D::ChorusMix, D::DistDrive });
    }

    // Plucks, refitted to the corpus's 108 pluck presets: a saw or square
    // (a wavetable or soft FM pair in some) with a second osc on the note or
    // an octave up in most, a little unison; a ladder low-pass snapped shut
    // from a few kHz by an envelope (a noise click or a sub in one of four);
    // the body decays over 0.15-1 s. Drive, chorus, width and multiband
    // compression in about half, a dotted or straight echo and some room.
    void pluck (B& b)
    {
        const int voice = b.weighted ({ 0.73f, 0.15f, 0.12f });   // poly, mono, legato with glide
        if (voice == 0) polyVoice (b, 6, 10);
        else monoVoice (b, voice == 2, voice == 2 ? 10.f : 0.f, voice == 2 ? 60.f : 0.f);
        switch (b.weighted ({ 0.55f, 0.25f, 0.2f }))
        {
            case 0: analogA (b, b.chance (0.6f) ? kSaw : kSquare); break;
            case 1: wavetableA (b, { 6, 0, 3, 1 }, 0.f, 0.7f); break;
            default: fmA (b, { 2.f, 3.f, 4.f, 1.f }, 1.f, 3.f, 0.f, 0.2f); break;
        }
        const bool second = b.chance (0.75f);
        if (second)
        {
            analogB (b, b.chance (0.6f) ? kSaw : kSquare, 0.35f, 0.65f);
            b.pick (P::OscBCoarse, { 0, 12, -12, -24, 7 }, { 0.56f, 0.24f, 0.08f, 0.06f, 0.06f });
            b.uni (P::OscBFine, 3.f, 12.f);
        }
        const bool click = b.chance (0.28f);
        if (click) { b.pick (P::NoiseType, { 0, 3, 2 }); b.uni (P::NoiseLevel, 0.03f, 0.12f); }
        const bool sub = b.chance (0.21f);
        if (sub) b.uni (P::SubLevel, 0.1f, 0.3f);

        // a ladder low-pass snapped shut in most; a few keep the filter still
        const int fil = b.weighted ({ 0.65f, 0.12f, 0.1f, 0.05f, 0.04f, 0.04f });
        const bool still = b.chance (0.15f);
        const float cLo = still ? 800.f : 90.f, cHi = still ? 4000.f : 800.f, eLo = still ? 0.f : 0.4f, eHi = still ? 0.1f : 0.8f;
        switch (fil)
        {
            case 0: filter1 (b, ft (FilterType::Ladder24), cLo, cHi, 0.f, 0.4f, eLo, eHi); break;
            case 1: filter1 (b, ft (FilterType::LP12), cLo, cHi, 0.f, 0.4f, eLo, eHi); break;
            case 2: filter1 (b, ft (FilterType::BP12), cLo * 2.f, cHi * 2.f, 0.1f, 0.45f, eLo * 0.8f, eHi * 0.8f); break;
            case 3: filter1 (b, ft (FilterType::HP12), 80.f, 300.f, 0.f, 0.3f, 0.f, 0.05f); break;
            case 4:
                filter1 (b, ft (FilterType::CombPos), 200.f, 1200.f, 0.3f, 0.7f, eLo * 0.5f, eHi * 0.5f);
                b.uni (P::F1Key, 0.8f, 1.f);
                break;
            default: b.set (P::F1Type, (float) ft (FilterType::Off)); break;
        }
        filterEnv (b, 0.f, 0.f, 120.f, 700.f, 0.f, 0.2f, 15.f, 300.f);
        ampEnv (b, 0.f, 6.f, 150.f, 1000.f, 0.f, 0.f, 30.f, 600.f);
        const bool drift = b.chance (0.15f);
        if (drift)
        {
            b.lfo (0, LfoShape::SmoothRandom, 0.2f, 1.f, false);
            b.route (S::Lfo1, D::PitchAll, 0.002f, 0.006f);
        }
        const bool touch = b.chance (0.3f);
        if (touch) b.route (S::Velocity, D::F1Cutoff, 0.15f, 0.3f);
        b.route (S::Macro1, D::FilterEnvAmt, 0.2f, 0.4f);

        fxDist (b, 0.45f, { (int) DistType::Tube, (int) DistType::Soft, (int) DistType::Fold }, 3.f, 12.f, 0.3f, 0.8f);
        if (b.chance (0.1f))
        {
            b.on (P::CrushOn, true);
            b.uni (P::CrushBits, 6.f, 12.f);
            b.uni (P::CrushRate, 1.f, 6.f);
            b.uni (P::CrushMix, 0.2f, 0.5f);
        }
        const int squash = b.weighted ({ 0.38f, 0.3f, 0.32f });   // multiband, glue, none
        if (squash == 0) fxCompMulti (b);
        else if (squash == 1) fxCompGlue (b);
        fxDelay (b, 0.8f, { D1_8D, D1_8D, D1_8D, D1_8, D1_8, D1_16, D1_16, D1_4, D1_4, D1_4D }, 0.3f, 0.6f, 0.18f, 0.45f);
        fxReverb (b, 0.7f, 1.2f, 3.5f, 0.14f, 0.38f);
        fxChorus (b, 0.4f);
        fxWidth (b, 0.45f, 0.3f, 0.7f);
        // an extra filter envelope from LFO 2 run once, in a quarter
        const bool lfoEnv = b.chance (0.25f);
        if (lfoEnv && fil != 5)
        {
            lfoEnvelope (b, 1, { D1_8, D1_4, D1_2 }, { 0.45f, 0.35f, 0.2f });
            b.route (S::Lfo2, D::F1Cutoff, 0.08f, 0.22f);
        }
        fxEq (b, 0.94f, 0.06f, 100.f, 340.f, 0.8f, { 0.32f, 0.47f, 0.21f });
        fxDim (b, 0.37f, 0.15f, 0.4f);
        b.macros ({ D::F1Cutoff, D::F1Reso, D::WtPosA, D::FmA, D::DelayMix, D::ReverbMix, D::DistDrive });
    }

    // Chord stabs, refitted to the corpus's 60 stab and chord presets: two
    // oscillators (saws, a supersaw or an FM pair; osc B on the note, an
    // octave away or a third up), a fifth or an octave in the unison stack
    // in some, a noise layer in four of ten; a ladder low-pass that an
    // envelope opens from low down, or a synced LFO on the beat. The note
    // holds while it is played; driven, compressed, a dotted-eighth echo and
    // a room around it.
    void stab (B& b)
    {
        polyVoice (b, 8, 12);
        const int kind = b.weighted ({ 0.45f, 0.25f, 0.3f });
        if (kind == 0)
        {
            analogA (b, kSaw);
            const bool stacked = b.chance (0.35f);
            if (stacked)
            {
                b.pick (P::OscAUniStack, { (int) UniStack::OctFifth, (int) UniStack::Oct }, { 0.6f, 0.4f });
                b.uni (P::OscAUnison, 5.f, 7.f);
            }
        }
        else if (kind == 1) supersawA (b, 0.15f, 0.45f);
        else fmA (b, { 1.f, 2.f, 3.f, 0.5f }, 0.8f, 4.f, 0.f, 0.3f);
        analogB (b, kSaw, 0.4f, 0.7f);
        b.pick (P::OscBCoarse, { 0, 12, -12, 3, 4, 24, 7 }, { 0.3f, 0.24f, 0.16f, 0.1f, 0.08f, 0.06f, 0.06f });
        b.uni (P::OscBFine, -10.f, 10.f);
        const bool air = b.chance (0.42f);
        if (air) { b.pick (P::NoiseType, { 0, 1, 3 }); b.uni (P::NoiseLevel, 0.02f, 0.1f); }
        const bool sub = b.chance (0.23f);
        if (sub) b.uni (P::SubLevel, 0.1f, 0.3f);

        const int fil = b.weighted ({ 0.6f, 0.12f, 0.1f, 0.06f, 0.06f, 0.06f });
        switch (fil)
        {
            case 0: filter1 (b, ft (FilterType::Ladder24), 200.f, 1500.f, 0.f, 0.45f, 0.3f, 0.6f); break;
            case 1: filter1 (b, ft (FilterType::LP12), 250.f, 1800.f, 0.f, 0.4f, 0.3f, 0.6f); break;
            case 2: filter1 (b, ft (FilterType::BP12), 500.f, 2500.f, 0.1f, 0.45f, 0.2f, 0.5f); break;
            case 3: filter1 (b, ft (FilterType::HP12), 80.f, 300.f, 0.f, 0.3f, 0.f, 0.05f); break;
            case 4:
                filter1 (b, ft (FilterType::CombPos), 200.f, 1200.f, 0.3f, 0.7f, 0.1f, 0.3f);
                b.uni (P::F1Key, 0.8f, 1.f);
                break;
            default: b.set (P::F1Type, (float) ft (FilterType::Off)); break;
        }
        filterEnv (b, 0.f, 10.f, 80.f, 900.f, 0.f, 0.5f, 15.f, 400.f);
        const bool beat = b.chance (0.35f);   // a synced LFO opens it on the beat
        if (beat && fil < 3)
        {
            lfoSyncW (b, 1, LfoShape::Triangle, { D1_4, D1_2, D1_8, D1_1 }, { 0.3f, 0.3f, 0.2f, 0.2f }, true);
            b.route (S::Lfo2, D::F1Cutoff, 0.08f, 0.25f);
        }
        ampEnv (b, 0.5f, 10.f, 250.f, 900.f, 0.1f, 0.8f, 15.f, 250.f);
        const bool touch = b.chance (0.22f);
        if (touch) b.route (S::Velocity, D::F1Cutoff, 0.1f, 0.25f);
        b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.4f);

        fxReverb (b, 0.85f, 1.5f, 4.f, 0.18f, 0.42f);
        fxDelay (b, 0.75f, { D1_8D, D1_8D, D1_8D, D1_8, D1_8, D1_4D, D1_4, D1_16 }, 0.35f, 0.7f, 0.2f, 0.45f);
        fxChorus (b, 0.4f, false);
        fxDist (b, 0.5f, { (int) DistType::Tube, (int) DistType::Soft, (int) DistType::Hard }, 3.f, 12.f, 0.4f, 1.f);
        if (b.chance (0.12f))
        {
            b.on (P::CrushOn, true);
            b.uni (P::CrushBits, 6.f, 12.f);
            b.uni (P::CrushRate, 1.f, 6.f);
            b.uni (P::CrushMix, 0.2f, 0.5f);
        }
        const int squash = b.weighted ({ 0.38f, 0.35f, 0.27f });   // multiband, glue, none
        if (squash == 0) fxCompMulti (b);
        else if (squash == 1) fxCompGlue (b);
        fxWidth (b, 0.35f, 0.3f, 0.7f);
        // a quarter open slowly over the bar: LFO 2 run once, drawn up (the
        // corpus's stabs ramp their filter that way where it isn't on the beat)
        const bool swell = b.chance (0.25f);
        if (swell && ! beat && fil < 3)
        {
            drawLfo (b, 1, { { 0.f, 0.f, b.rnd (-0.3f, 0.5f) }, { 1.f, 1.f, 0.f } });
            b.set (lfoParam (1, 2), 1.f);
            b.pick (lfoParam (1, 3), { D1_1, D1_2, D2_1 }, { 0.55f, 0.25f, 0.2f });
            b.set (lfoParam (1, 4), 1.f);
            b.on (lfoEnvParam (1), true);
            b.route (S::Lfo2, D::F1Cutoff, 0.08f, 0.25f);
        }
        fxEq (b, 0.9f, 0.08f, 100.f, 340.f, 0.75f, { 0.42f, 0.27f, 0.31f });
        fxDim (b, 0.18f, 0.1f, 0.3f);
        b.macros ({ D::F1Reso, D::FilterEnvAmt, D::DetuneA, D::FmA, D::ReverbMix, D::DelayMix, D::ChorusMix, D::DistDrive });
    }

    // The short hits a full-on track's groove is built from, the "grids" of
    // the sample packs: FM blips, plucks, small chord stabs and ring-modded
    // ticks that answer the kick and the rolling bass on the 16ths. Each one
    // is over in a sixteenth or two and sits above the bass (a high-pass
    // keeps it out of the bass's octaves); every hit comes out a little
    // different, the way a grid of layered one-shots does. Squashed by a
    // multiband compressor and dry but for a short echo on the grid and a
    // small room; a grain of noise in four of ten and a chorus in a third
    // (the corpus's grid presets).
    void grid (B& b)
    {
        polyVoice (b, 4, 8);
        b.pick (P::Octave, { -1, 0 }, { 0.3f, 0.7f });
        b.uni (P::VelSens, 0.3f, 0.6f);
        const int kind = b.weighted ({ 0.4f, 0.3f, 0.2f, 0.1f });
        switch (kind)
        {
            case 0:   // FM blip: the index falls in a few ms, a tick then a tone
                fmA (b, { 1.f, 2.f, 3.f, 4.f, 2.f, 3.5f }, 0.5f, 3.f, 0.f, 0.35f);
                modEnv (b, 0.f, 0.f, 12.f, 90.f, 0.f, 0.1f, 20.f, 60.f);
                b.route (S::ModEnv, D::FmA, 0.25f, 0.55f);
                break;
            case 1:   // pluck: a saw or square the filter snaps shut on
                analogA (b, b.chance (0.6f) ? kSaw : kSquare);
                if (b.chance (0.35f))
                {
                    analogB (b, b.chance (0.5f) ? kSaw : kSquare, 0.3f, 0.6f);
                    b.among (P::OscBCoarse, { 12.f, 7.f, 19.f });
                    b.uni (P::OscBFine, -8.f, 8.f);
                }
                break;
            case 2:   // a small chord stab: root, fifth and octave, or a third on osc B
                analogA (b, kSaw);
                if (b.chance (0.5f))
                {
                    // the stack puts the fifth on the second outer pair: root, octave
                    // and fifth need 5 voices (unisonRecipe leaves the count alone)
                    b.set (P::OscAUniStack, (float) UniStack::OctFifth);
                    b.set (P::OscAUnison, 5.f);
                }
                analogB (b, kSaw, 0.4f, 0.7f);
                b.among (P::OscBCoarse, { 3.f, 7.f, 10.f, 12.f });
                b.uni (P::OscBFine, -10.f, 10.f);
                break;
            default:  // a ring-modded tick: square against a detuned sine
                analogA (b, kSquare);
                analogB (b, kSine, 0.f, 0.f);
                b.among (P::OscBCoarse, { 7.f, 11.f, 13.f, 19.f, 25.f });
                b.uni (P::RingLevel, 0.4f, 0.7f);
                break;
        }

        // the snap: a filter envelope over in 30-160 ms (an FM blip is shaped
        // by its index, so its filter mostly stays open)
        const bool openBlip = kind == 0 && b.chance (0.6f);
        if (openBlip)
            filter1 (b, ft (FilterType::LP12), 6000.f, 16000.f, 0.f, 0.2f, 0.f, 0.f);
        else
        {
            // a low-pass mostly; a band-pass or a notch (the corpus's band-reject) in some
            const int types[] = { ft (FilterType::LP12), ft (FilterType::Ladder24), ft (FilterType::BP12), ft (FilterType::Notch) };
            filter1 (b, types[b.weighted ({ 0.45f, 0.25f, 0.15f, 0.15f })], 350.f, 2200.f, 0.1f, 0.55f, 0.4f, 0.85f);
        }
        const bool grain = b.chance (0.4f);
        if (grain) { b.pick (P::NoiseType, { 2, 0 }); b.uni (P::NoiseLevel, 0.03f, 0.12f); }
        filterEnv (b, 0.f, 0.f, 30.f, 160.f, 0.f, 0.f, 20.f, 80.f);
        if (b.chance (0.8f))
        {
            b.pick (P::F2Type, { ft (FilterType::HP12), ft (FilterType::HP24) }, { 0.5f, 0.5f });
            b.uni (P::F2Cutoff, 180.f, 450.f);
            b.uni (P::F2Reso, 0.f, 0.3f);
        }
        ampEnv (b, 0.f, 0.5f, 40.f, 220.f, 0.f, 0.f, 20.f, 90.f);
        // a click on the front: the pitch drops a few semitones in a few ms
        if (b.chance (0.35f))
        {
            b.set (P::PenvAttack, 0.f);
            b.set (P::PenvDest, 0.f);
            b.uni (P::PenvAmt, 7.f, 30.f);
            b.uni (P::PenvDecay, 4.f, 25.f);
            b.uni (P::PenvCurve, 0.5f, 0.9f);
        }

        // every hit a little different: a new random value per note, or a
        // sample & hold on the 16ths (the "fractal" grid), on the brightness
        // or the FM index; accents come out brighter
        const D target = kind == 0 ? D::FmA : D::F1Cutoff;
        if (b.chance (0.5f)) b.route (S::Random, target, 0.1f, 0.3f);
        else if (b.chance (0.5f))
        {
            b.lfoSync (0, LfoShape::SampleHold, { D1_16, D1_8, D1_16D }, false);
            b.route (S::Lfo1, target, 0.1f, 0.25f);
        }
        const bool touch = b.chance (0.3f);
        if (touch) b.route (S::Velocity, D::F1Cutoff, 0.1f, 0.25f);
        // the open blip's filter envelope is 0, so its macro opens the FM index instead
        b.route (S::Macro1, openBlip ? D::FmA : D::FilterEnvAmt, 0.2f, 0.4f);

        if (b.chance (0.8f)) fxCompMulti (b);
        fxDelay (b, 0.35f, { D1_16, D1_16, D1_4D, D1_8D }, 0.25f, 0.6f, 0.05f, 0.2f);
        fxReverb (b, 0.6f, 0.6f, 1.8f, 0.05f, 0.2f);
        fxDist (b, 0.3f, { (int) DistType::Soft, (int) DistType::Tube, (int) DistType::Hard }, 4.f, 14.f, 0.3f, 0.7f);
        fxChorus (b, 0.35f);
        fxWidth (b, 0.4f, 0.3f, 0.8f);
        fxEq (b, 0.92f, 0.08f, 70.f, 200.f, 0.7f, { 0.11f, 0.56f, 0.33f });
        fxDim (b, 0.3f, 0.15f, 0.4f);
        b.macros ({ D::F1Cutoff, D::FilterEnvAmt, D::FmA, D::F1Reso, D::DelayMix, D::DistDrive, D::ReverbMix });
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
        b.pick (P::F1Type, { ft (FilterType::BP12), ft (FilterType::BP24), ft (FilterType::Notch), ft (FilterType::LadderBP),
                                           ft (FilterType::CombPos), ft (FilterType::Formant) },
                              { 0.25f, 0.15f, 0.15f, 0.15f, 0.15f, 0.15f });
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

    // Atmospheres, from the 57 atmos presets of the Serum corpus (Tunecraft
    // Lifeforms, Dimensions, Outerverse, Ghosthack, PME, TekWan), decoded and
    // rendered through Serum on a held C3. They are textures more than
    // chords: a fifth of their power on the note's harmonics, bright (centroid
    // ~800 Hz, little under 250 Hz), noisy up top (a noise layer in four of
    // five, often as loud as the oscillators), nearly as wide as deep (side
    // ~0.4 dB under the mid), 1-3.5 s to come in, and always moving: about
    // four LFOs each, mostly slow (a cycle every 4-8 s), on the filter, the
    // wavetable, the noise, the level, the pan and the pitch. The filter is
    // always on, and as often a comb, band-pass or formant as a low-pass,
    // with resonance (40 % in the middle). Six kinds, the corpus's families:
    // air, choir, glass, a dark bed, alien life and an evolving pad.
    void atmos (B& b)
    {
        const int kind = b.weighted ({ 0.22f, 0.14f, 0.15f, 0.18f, 0.15f, 0.16f });
        if (b.chance (0.15f)) monoVoice (b, false, 60.f, 400.f);   // a held texture that glides
        else polyVoice (b, 6, 10);
        const auto isFm = [&b] { return b.state().geti (P::OscAType) == (int) OscType::FM; };
        const auto isWt = [&b] { return b.state().geti (P::OscAType) == (int) OscType::Wavetable; };
        float cutLo = 100.f, cutHi = 300.f;   // the EQ's low cut
        bool rollOff = false;   // the EQ's top a low-pass, mostly
        switch (kind)
        {
            case 0:   // air: wind and breath, noise through a band-pass or comb that wanders
            {
                if (b.chance (0.6f))
                {
                    // a tone under the wind, an octave or two down (where the corpus keeps its oscillators)
                    wavetableA (b, { 0, 2, 5 }, 0.f, 0.6f);
                    b.uni (P::OscALevel, 0.1f, 0.3f);
                    b.pick (P::OscACoarse, { -12, -24, 0 }, { 0.5f, 0.3f, 0.2f });
                }
                else b.on (P::OscAOn, false);
                // never the Hiss: it sits above 10 kHz, where the band-pass and the roll-off are not
                b.pick (P::NoiseType, { 1, 0, 4 }, { 0.55f, 0.33f, 0.12f });
                b.uni (P::NoiseLevel, 0.6f, 1.f);
                if (b.chance (0.7f))
                    filter1 (b, b.chance (0.6f) ? ft (FilterType::BP12) : ft (FilterType::BP24), 350.f, 2200.f, 0.15f, 0.5f, 0.f, 0.f);
                else
                {
                    // a comb's teeth run to the top: a low-pass after it keeps the wind soft
                    filter1 (b, b.chance (0.5f) ? ft (FilterType::CombPos) : ft (FilterType::CombNeg), 150.f, 1200.f, 0.4f, 0.8f, 0.f, 0.f);
                    b.set (P::F2Type, (float) ft (FilterType::LP12));
                    b.uni (P::F2Cutoff, 3000.f, 8000.f);
                }
                // gusts: the cutoff and the noise level wander on their own
                b.lfo (1, LfoShape::SmoothRandom, 0.04f, 0.2f, false);
                b.route (S::Lfo2, D::F1Cutoff, 0.12f, 0.3f);
                b.lfo (2, LfoShape::SmoothRandom, 0.06f, 0.35f, false);
                b.route (S::Lfo3, D::NoiseLevel, 0.15f, 0.35f);
                rollOff = true;
                break;
            }
            case 1:   // choir: a vowel that turns over a voiced source and a breath
            {
                if (b.chance (0.55f)) wavetableA (b, { 2 }, 0.1f, 0.9f);
                else analogA (b, kSaw, 0.8f);
                analogB (b, kSaw, 0.3f, 0.6f);
                b.pick (P::OscBCoarse, { 0, 12, -12, 7 }, { 0.4f, 0.25f, 0.2f, 0.15f });
                b.uni (P::OscBFine, 6.f, 18.f);
                b.pick (P::NoiseType, { 4, 1 }, { 0.65f, 0.35f });
                b.uni (P::NoiseLevel, 0.35f, 0.7f);
                filter1 (b, ft (FilterType::Formant), 700.f, 1600.f, 0.3f, 0.65f, 0.f, 0.f);
                b.uni (P::F1Vowel, 0.f, 1.f);
                // the mantra: the vowel turns over bars, the voices waver
                if (b.chance (0.6f)) lfoSyncW (b, 1, LfoShape::Triangle, { D4_1, D2_1, D1_1 }, { 0.4f, 0.4f, 0.2f }, false);
                else b.lfo (1, LfoShape::SmoothRandom, 0.05f, 0.25f, false);
                b.route (S::Lfo2, D::F1Vowel, 0.3f, 0.6f);
                b.route (S::Lfo2, D::F1Cutoff, 0.1f, 0.25f);   // the formants shift with it
                b.lfo (2, LfoShape::Sine, 4.f, 6.f, true);
                b.route (S::Lfo3, D::PitchAll, 0.002f, 0.005f);   // 5-12 cents of vibrato
                fxChorus (b, 0.85f, false);
                break;
            }
            case 2:   // glass: inharmonic FM or a bell table up high, rung and shifted
            {
                b.pick (P::Octave, { 0, 1 });
                if (b.chance (0.65f)) fmA (b, { 1.41f, 2.76f, 3.5f, 4.2f, 5.4f, 7.13f }, 1.5f, 6.f, 0.f, 0.3f);
                else wavetableA (b, { 6, 3 }, 0.1f, 0.9f);
                if (b.chance (0.6f))
                {
                    analogB (b, kSine, 0.3f, 0.6f);
                    b.pick (P::OscBCoarse, { 12, 19, 24, 7, -12 }, { 0.3f, 0.2f, 0.15f, 0.15f, 0.2f });
                    b.uni (P::OscBFine, 3.f, 25.f);
                    if (b.chance (0.5f)) b.uni (P::RingLevel, 0.2f, 0.5f);
                }
                b.pick (P::NoiseType, { 2, 1 });
                b.uni (P::NoiseLevel, 0.15f, 0.45f);
                if (b.chance (0.4f)) b.on (P::NoiseDirect, true);   // sparkle beside the filter
                if (b.chance (0.5f)) filter1 (b, ft (FilterType::HP12), 250.f, 900.f, 0.1f, 0.4f, 0.f, 0.f);
                else filter1 (b, ft (FilterType::CombPos), 800.f, 4000.f, 0.4f, 0.75f, 0.f, 0.f);
                // the shimmer: the FM index or the table glints, the cutoff drifts
                b.lfo (1, LfoShape::SmoothRandom, 0.08f, 0.4f, false);
                if (isFm()) b.route (S::Lfo2, D::FmA, 0.06f, 0.15f);
                else b.route (S::Lfo2, D::WtPosA, 0.15f, 0.35f);
                b.lfo (2, LfoShape::Triangle, 0.05f, 0.2f, false);
                b.route (S::Lfo3, D::F1Cutoff, 0.1f, 0.25f);
                fxShift (b, 0.65f, 0.5f, 12.f, 0.3f, 0.75f);
                cutLo = 150.f; cutHi = 400.f;
                break;
            }
            case 3:   // a dark bed: a low comb or ladder that a slow LFO tunes, grit on top
            {
                b.pick (P::Octave, { 0, -1 }, { 0.75f, 0.25f });
                if (b.chance (0.5f)) wavetableA (b, { 0, 1, 4, 5 }, 0.f, 1.f);
                else analogA (b, b.chance (0.7f) ? kSaw : kSquare, 0.8f);
                analogB (b, kSaw, 0.35f, 0.65f);
                b.pick (P::OscBCoarse, { 0, -12, 7, 12 }, { 0.35f, 0.3f, 0.2f, 0.15f });
                b.uni (P::OscBFine, 5.f, 20.f);
                b.pick (P::NoiseType, { 1, 0 }, { 0.6f, 0.4f });
                b.uni (P::NoiseLevel, 0.25f, 0.6f);
                if (b.chance (0.55f))
                {
                    // the corpus's combs: tuned low, ringing, a low-pass after them
                    filter1 (b, b.chance (0.6f) ? ft (FilterType::CombPos) : ft (FilterType::CombNeg), 80.f, 600.f, 0.5f, 0.85f, 0.f, 0.f);
                    b.set (P::F2Type, (float) ft (FilterType::LP12));
                    b.uni (P::F2Cutoff, 1500.f, 6000.f);
                }
                else filter1 (b, ft (FilterType::Ladder24), 400.f, 2000.f, 0.3f, 0.6f, 0.f, 0.f);
                if (b.chance (0.5f)) lfoSyncW (b, 1, LfoShape::Triangle, { D4_1, D2_1 }, { 0.6f, 0.4f }, false);
                else b.lfo (1, LfoShape::SmoothRandom, 0.04f, 0.2f, false);
                b.route (S::Lfo2, D::F1Cutoff, 0.1f, 0.3f);
                b.lfo (2, LfoShape::SmoothRandom, 0.05f, 0.2f, false);
                if (isWt()) b.route (S::Lfo3, D::WtPosA, 0.15f, 0.4f);
                else b.route (S::Lfo3, D::ShapeA, 0.05f, 0.15f);
                fxDist (b, 0.45f, { (int) DistType::Tube, (int) DistType::Soft }, 0.2f, 0.5f, 0.3f, 0.7f);
                fxPhaser (b, 0.35f);
                cutLo = 80.f; cutHi = 220.f;
                break;
            }
            case 4:   // alien life: random chirps and gurgles, fast and small
            {
                b.pick (P::Octave, { 0, 1 }, { 0.6f, 0.4f });
                // harmonics for the band-pass or the vowel to catch (a sine through them is silent low down)
                if (b.chance (0.5f)) fmA (b, { 0.5f, 1.f, 1.5f, 2.f, 3.f }, 1.f, 5.f, 0.1f, 0.5f);
                else analogA (b, b.chance (0.5f) ? kSaw : kSquare, 0.8f);
                const bool xmod = b.chance (0.5f);
                if (xmod)
                {
                    analogB (b, kSaw, 0.3f, 0.6f);
                    b.pick (P::OscBCoarse, { 12, 7, -12 });
                    b.uni (P::OscBXMod, 0.2f, 0.6f);
                }
                b.pick (P::NoiseType, { 2, 0 }, { 0.6f, 0.4f });
                b.uni (P::NoiseLevel, 0.1f, 0.35f);
                const bool vowel = b.chance (0.4f);
                if (vowel)
                {
                    filter1 (b, ft (FilterType::Formant), 600.f, 1800.f, 0.3f, 0.6f, 0.f, 0.f);
                    b.uni (P::F1Vowel, 0.f, 1.f);
                }
                else filter1 (b, ft (FilterType::BP12), 600.f, 2500.f, 0.3f, 0.65f, 0.f, 0.f);
                // the creature: a fast random LFO jumps the pitch, the filter or the FM
                b.lfo (2, b.chance (0.5f) ? LfoShape::SampleHold : LfoShape::SmoothRandom, 2.f, 9.f, false);
                const int what = b.weighted ({ 0.4f, 0.35f, 0.25f });
                if (what == 0) b.route (S::Lfo3, D::PitchA, 0.03f, 0.15f);   // 0.7-3.6 semitones
                else if (what == 1) b.route (S::Lfo3, vowel ? D::F1Vowel : D::F1Cutoff, 0.15f, 0.35f);
                else if (isFm()) b.route (S::Lfo3, D::FmA, 0.1f, 0.3f);
                else b.route (S::Lfo3, xmod ? D::XMod : D::F1Cutoff, 0.15f, 0.35f);
                // and it moves about, slower
                b.lfo (1, LfoShape::SmoothRandom, 0.1f, 0.5f, false);
                b.route (S::Lfo2, D::F1Cutoff, 0.1f, 0.3f);
                if (b.chance (0.35f))
                {
                    b.on (P::CrushOn, true);
                    b.uni (P::CrushBits, 6.f, 12.f);
                    b.uni (P::CrushRate, 2.f, 10.f);
                    b.uni (P::CrushMix, 0.2f, 0.5f);
                }
                fxShift (b, 0.3f, 2.f, 40.f, 0.2f, 0.6f);
                cutLo = 150.f; cutHi = 400.f;
                break;
            }
            default:   // an evolving pad: a scanned stack, a noise layer, a slow sweep
            {
                const bool supersaw = b.chance (0.3f);
                if (supersaw) supersawA (b, 0.25f, 0.6f);
                else wavetableA (b, { 0, 1, 2, 5, 6 }, 0.f, 0.8f);
                if (b.chance (0.75f))
                {
                    b.on (P::OscBOn, true);
                    b.pick (P::OscBType, { (int) OscType::Analog, (int) OscType::Wavetable });
                    b.set (P::OscBShape, kSaw);
                    b.pick (P::OscBWt, { 0, 2, 6 });
                    b.uni (P::OscBWtPos, 0.f, 1.f);
                    b.uni (P::OscBLevel, 0.3f, 0.6f);
                    b.pick (P::OscBCoarse, { 12, 7, 19, 0, 24 }, { 0.35f, 0.2f, 0.15f, 0.15f, 0.15f });
                    b.uni (P::OscBFine, 8.f, 20.f);
                }
                b.pick (P::NoiseType, { 1, 4, 0 }, { 0.5f, 0.3f, 0.2f });
                b.uni (P::NoiseLevel, 0.3f, 0.65f);
                if (b.chance (0.2f)) b.on (P::NoiseDirect, true);
                const int fil = b.weighted ({ 0.5f, 0.25f, 0.25f });
                if (fil == 0) filter1 (b, ft (FilterType::LP12), 1000.f, 6000.f, 0.1f, 0.4f, 0.f, 0.f);
                else if (fil == 1) filter1 (b, ft (FilterType::BP12), 800.f, 3000.f, 0.2f, 0.5f, 0.f, 0.f);
                else filter1 (b, ft (FilterType::HP12), 300.f, 900.f, 0.1f, 0.4f, 0.f, 0.f);
                if (b.chance (0.5f)) lfoSyncW (b, 1, LfoShape::Sine, { D2_1, D4_1, D1_1 }, { 0.4f, 0.35f, 0.25f }, false);
                else b.lfo (1, LfoShape::Sine, 0.05f, 0.25f, false);
                b.route (S::Lfo2, D::F1Cutoff, 0.15f, 0.35f);
                lfoSyncW (b, 2, LfoShape::Triangle, { D4_1, D2_1 }, { 0.6f, 0.4f }, false);
                if (supersaw) b.route (S::Lfo3, D::DetuneA, 0.1f, 0.3f);
                else b.route (S::Lfo3, D::WtPosA, 0.15f, 0.35f);
                break;
            }
        }

        // a slow swell in, a long release; the filter opens with the note in a quarter
        // a third of the corpus is texture first (its noise samples carry it): the
        // noise leads, plain or pink, and the oscillators sit under it
        if (kind != 0 && kind != 4 && b.chance (0.3f))
        {
            b.pick (P::NoiseType, { 1, 0 }, { 0.6f, 0.4f });
            b.uni (P::NoiseLevel, 0.75f, 1.f);
            b.uni (P::OscALevel, 0.15f, 0.35f);
            b.uni (P::OscBLevel, 0.1f, 0.3f);
            b.on (P::NoiseDirect, false);
            rollOff = true;
        }
        if (kind == 2) rollOff = true;   // digital sparkle and a bell: the top goes past Serum's

        // the RC charge or close to it: a swell curve keeps an arp's short notes near silent
        ampEnv (b, 400.f, 4500.f, 1000.f, 3000.f, 0.55f, 1.f, 1200.f, 5000.f);
        b.uni (P::AenvCurve, 0.f, 0.15f);
        if (b.chance (0.25f))
        {
            b.uni (P::F1Env, 0.1f, 0.3f);
            filterEnv (b, 800.f, 4000.f, 1000.f, 3000.f, 0.5f, 1.f, 1500.f, 4000.f);
        }
        // it evolves over the held note: Env 2 takes the filter or the table somewhere over
        // seconds (not the air's band, which its gusts already move)
        if (kind != 0 && b.chance (0.5f))
        {
            modEnv (b, 2000.f, 8000.f, 2000.f, 6000.f, 0.f, 0.6f, 1500.f, 4000.f);
            const float sign = b.chance (0.6f) ? 1.f : -1.f;
            const bool table = b.chance (0.5f);
            if (isWt() && table) b.route (S::ModEnv, D::WtPosA, sign * 0.2f, sign * 0.5f);
            else b.route (S::ModEnv, D::F1Cutoff, sign * 0.1f, sign * 0.25f);
        }
        // drift: the pan wanders, the pitch floats or the level swells
        b.lfo (0, LfoShape::SmoothRandom, 0.03f, 0.2f, false);
        const int drift = b.weighted ({ 0.4f, 0.3f, 0.3f });
        if (drift == 0) b.route (S::Lfo1, D::Pan, 0.3f, 0.7f);
        else if (drift == 1) b.route (S::Lfo1, D::PitchAll, 0.002f, 0.006f);
        else b.route (S::Lfo1, D::Amp, 0.2f, 0.45f);
        // every note a little different (the arp plays several)
        if (b.chance (0.4f)) b.route (S::Random, D::F1Cutoff, 0.05f, 0.15f);

        // grit the corpus loves: a downsampler (Serum's most used distortion here)
        if (kind != 4 && b.chance (0.25f))
        {
            b.on (P::CrushOn, true);
            b.uni (P::CrushBits, 8.f, 14.f);
            b.uni (P::CrushRate, 2.f, 8.f);
            b.uni (P::CrushMix, 0.15f, 0.4f);
        }
        // a slow shift beats every partial against its neighbour
        if (kind == 1 || kind == 3 || kind == 5) fxShift (b, 0.35f, 0.3f, 3.f, 0.f, 0.3f);
        // compressed in 43 of the 49 Serum 1 atmospheres, OTT-style in 15: the upward half
        // brings a quiet texture forward (and noise sits ~25 dB under an oscillator here)
        if (b.chance (kind == 0 ? 0.5f : 0.3f)) fxCompMulti (b);
        else if (b.chance (kind == 0 ? 0.7f : 0.35f)) fxCompGlue (b);   // more of it flattens the swells
        fxReverb (b, 1.f, 3.f, 14.f, 0.3f, 0.55f, 0.5f, 0.95f);
        fxDelay (b, 0.85f, { D1_4D, D1_2, D1_8D, D1_4 }, 0.35f, 0.65f, 0.15f, 0.4f);
        if (kind != 1) fxChorus (b, 0.6f);
        if (kind != 3) fxPhaser (b, 0.2f);
        fxDim (b, 0.75f, 0.4f, 0.8f);
        fxWidth (b, kind == 0 ? 0.8f : 0.4f, 0.4f, 0.9f);   // noise is mono in the voice: air needs it most
        // Serum's noise samples stop well under 20 kHz, ours runs to the top: the
        // noisy kinds roll their top off with the EQ's low-pass
        if (rollOff || kind == 1 || kind == 5) fxEq (b, 0.9f, 0.15f, cutLo, cutHi, 0.85f, { 0.1f, 0.1f, 0.8f }, 0.25f, 5000.f, 11000.f);
        else fxEq (b, 0.9f, 0.15f, cutLo, cutHi, 0.35f, { 0.4f, 0.3f, 0.3f });

        // last, so the macros find the effects on
        switch (kind)
        {
            case 0:  b.macros ({ D::F1Reso, D::NoiseLevel, D::Lfo2Rate, D::ReverbMix, D::DelayMix }); break;
            case 1:  b.macros ({ D::F1Vowel, D::NoiseLevel, D::ChorusMix, D::ReverbMix, D::DelayMix }); break;
            case 2:  b.macros ({ D::ShiftHz, isFm() ? D::FmA : D::WtPosA, D::RingLevel, D::DelayFb, D::ReverbMix }); break;
            case 3:  b.macros ({ D::F1Reso, D::DistDrive, D::WtPosA, D::PhaserMix, D::ReverbMix }); break;
            case 4:  b.macros ({ D::Lfo3Rate, D::CrushMix, D::F1Reso, D::ShiftHz, D::DelayMix }); break;
            default: b.macros ({ D::WtPosA, D::DetuneA, D::NoiseLevel, D::ReverbMix, D::ChorusMix, D::DelayMix }); break;
        }
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

    // Glitches, refitted to the corpus's glitch presets: a stepped random
    // pitch and a stuttering gain on the grid, a cutoff and wavetable thrown
    // around by sample & hold; poly, through a ladder or band-pass, a
    // downsampler in some, squashed by a multiband compressor, a free-timed
    // echo and a small room.
    void glitch (B& b)
    {
        if (b.chance (0.8f)) polyVoice (b, 2, 4);
        else monoVoice (b, false, 0.f, 0.f);
        const int kind = b.weighted ({ 0.45f, 0.35f, 0.2f });
        if (kind == 0) { fmA (b, { 1.f, 2.f, 3.5f, 7.f }, 1.f, 6.f, 0.f, 0.5f); b.set (P::OscALevel, 0.5f); }
        else if (kind == 1) { wavetableA (b, { 1, 3, 4, 5 }, 0.f, 1.f); b.set (P::OscALevel, 0.5f); }
        else analogA (b, kSquare, 0.5f);
        const bool second = b.chance (0.4f);
        if (second)
        {
            analogB (b, b.chance (0.5f) ? kSaw : kSquare, 0.2f, 0.4f);
            b.pick (P::OscBCoarse, { 0, -24, 12 }, { 0.5f, 0.25f, 0.25f });
        }
        if (b.chance (0.05f)) { b.pick (P::NoiseType, { 0, 2 }); b.uni (P::NoiseLevel, 0.05f, 0.2f); }

        // the pitch jumps: a sample & hold, or (as the corpus's glitches draw
        // them) a stepped line of semitones over the bar, the same every time
        const bool stepped = b.chance (0.4f);
        if (stepped)
        {
            drawSteps (b, 0, b.chance (0.6f) ? 16 : 8, { 0.f, 0.f, 0.f, 0.f, 1.f, -1.f, 7.f / 12.f, 5.f / 12.f, -5.f / 12.f, 3.f / 12.f });
            b.set (lfoParam (0, 2), 1.f);
            b.pick (lfoParam (0, 3), { D1_1, D1_2, D2_1 }, { 0.5f, 0.3f, 0.2f });
            b.set (lfoParam (0, 4), 1.f);
            b.route (S::Lfo1, D::PitchAll, 0.5f, 0.5f);   // a step of 1/12 is a semitone
        }
        else
        {
            b.lfoSync (0, LfoShape::SampleHold, { D1_16, D1_32, D1_16T, D1_32T }, true);
            b.route (S::Lfo1, D::PitchAll, 0.1f, 0.4f);
        }
        b.lfoSync (1, LfoShape::Square, { D1_32, D1_64, D1_16T }, true);
        b.route (S::Lfo2, D::Amp, 0.6f, 1.f);
        filter1 (b, b.chance (0.6f) ? ft (FilterType::Ladder24) : ft (FilterType::BP12), 800.f, 5000.f, 0.05f, 0.5f, 0.f, 0.f);
        b.lfo (2, LfoShape::SampleHold, 8.f, 30.f, false);
        b.route (S::Lfo3, D::F1Cutoff, 0.25f, 0.55f);
        // the S&H throws the wavetable or the FM index around too
        const bool scatter = b.chance (0.4f);
        if (scatter && kind < 2) b.route (S::Lfo3, kind == 0 ? D::FmA : D::WtPosA, 0.2f, 0.5f);
        ampEnv (b, 0.2f, 5.f, 300.f, 1500.f, 0.3f, 1.f, 5.f, 40.f);
        b.route (S::Macro1, D::Lfo3Rate, 0.2f, 0.4f);

        if (b.chance (0.4f))
        {
            b.on (P::CrushOn, true);
            b.uni (P::CrushBits, 3.f, 8.f);
            b.uni (P::CrushRate, 2.f, 16.f);
            b.uni (P::CrushMix, 0.4f, 0.9f);
        }
        if (b.chance (0.85f)) fxCompMulti (b);
        fxShift (b, 0.3f, 20.f, 400.f, 0.f, 0.5f);
        // a free-timed echo (the references leave the delay unsynced)
        if (b.chance (0.6f))
        {
            b.on (P::DelayOn, true);
            b.set (P::DelaySync, 0.f);
            b.uni (P::DelayMs, 20.f, 300.f);
            b.uni (P::DelayFb, 0.4f, 0.6f);
            b.uni (P::DelayMix, 0.15f, 0.45f);
            b.on (P::DelayPing, b.chance (0.4f));
            b.uni (P::DelayLowCut, 150.f, 500.f);
            b.uni (P::DelayHighCut, 3500.f, 9000.f);
        }
        fxDist (b, 0.3f, { 1, 2 }, 4.f, 14.f, 0.3f, 0.6f);
        fxReverb (b, 0.7f, 0.8f, 3.f, 0.15f, 0.35f, 0.2f, 0.5f);
        fxEq (b, 0.35f, 0.3f, 100.f, 300.f, 0.5f, { 0.5f, 0.25f, 0.25f });
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

    // ---- reverse basses ------------------------------------------------------

    // Their level: the compressor as a clean gain stage. The threshold sits
    // above the swell, so its shape stays as drawn, and the make-up brings
    // the peak level with the other basses.
    void fxGainBass (B& b, float dbLo, float dbHi)
    {
        b.on (P::CompOn, true);
        b.set (P::CompMode, (float) CompMode::Normal);
        b.set (P::CompThresh, -6.f);
        b.set (P::CompRatio, 2.f);
        b.set (P::CompKnee, 6.f);
        b.set (P::CompMix, 1.f);
        b.uni (P::CompOut, dbLo, dbHi);
    }

    // A reverse bass lasts a set length and stops dead (Cut at End): one beat
    // (1/4 bar), now and then two when `twoBeats` allows. Returns the length
    // in beats; Time mode gets the same length at 140 BPM, the tempo of the
    // reference shots.
    float reverseLength (B& b, bool twoBeats)
    {
        const int bars = twoBeats ? b.pick (P::EnergyBars, { B1_4, B1_2 }, { 0.85f, 0.15f })
                                  : b.pick (P::EnergyBars, { B1_4 });
        const float beats = energyBarsValue (bars) * 4.f;
        b.set (P::EnergySec, beats * 60.f / 140.f);
        b.set (P::EnergyFade, 0.f);
        b.on (P::EnergyCut, true);
        return beats;
    }

    // Reverse bass: a bass shot played backwards, in the beat before a kick.
    // Fitted to the bright reverse shots in Bass Shots 2 (017 E first, then
    // 024, 043, 049-051, 055, 062, 072, 074, 079, 080: one beat at 128-142
    // BPM) by rendering and measuring. Every one keeps the shape: the note is
    // already there, quietly, swells late and fast, holds, and stops dead;
    // a saw with its phases turned (round, with a spike each cycle) and the
    // fizz of a distorted top (hiss above 10 kHz, as loud against the spike as
    // in 017 E). Around that the references differ, and so do the patches:
    // how quietly it starts (-9 to -33 dB) and how late it is full (32-60 %
    // of the beat), how bright it is, a dip around 500 Hz, the filter
    // opening through the swell, an octave saw for a strong second harmonic,
    // a fall onto the note at the end, a stereo top over a mono low end.
    void reverseBass (B& b)
    {
        monoVoice (b, false, 0.f, 0.f);
        b.set (P::Octave, -1.f);
        b.set (P::VelSens, 0.15f);
        b.set (P::Accent, 0.f);

        analogA (b, b.chance (0.75f) ? kSaw : b.rnd (kSaw, kSaw + 0.6f), 0.85f);   // now and then towards a square
        if (b.chance (0.3f))   // the octave: a second harmonic as loud as the first (049-051)
        {
            b.on (P::OscBOn, true);
            b.set (P::OscBType, (float) OscType::Analog);
            b.set (P::OscBShape, kSaw);
            b.set (P::OscBCoarse, 12.f);
            b.set (P::OscBFine, 0.f);
            b.uni (P::OscBLevel, 0.3f, 0.7f);
        }

        // the brightness: wide open (a 6 dB slope keeps the top), or darker
        const bool open = b.chance (0.6f);
        filter1 (b, open ? ft (FilterType::LP6) : ft (FilterType::LP12), open ? 12000.f : 2800.f, open ? 20000.f : 8000.f,
                 0.f, 0.2f, 0.f, 0.f);
        b.set (P::F1Key, 0.f);
        b.uni (P::F1Drive, 0.f, 0.2f);
        if (b.chance (0.6f))   // the dip around 500 Hz (017): a notch beside the main path
        {
            b.set (P::F2Type, (float) ft (FilterType::Notch));
            b.uni (P::F2Cutoff, 380.f, 700.f);
            b.uni (P::F2Reso, 0.f, 0.15f);
            b.set (P::F2Key, 0.f);
            b.set (P::F2Env, 0.f);
            b.set (P::FRouting, 1.f);   // parallel: half the signal skips the notch, a dip of about 7 dB
        }

        // The phases turned: the phaser, fully wet and standing still, is an
        // all-pass (017: crest 6.7 dB against a saw's 4.7).
        if (b.chance (0.8f))
        {
            b.on (P::PhaserOn, true);
            b.set (P::PhaserMix, 1.f);
            b.set (P::PhaserDepth, 0.f);
            b.set (P::PhaserFb, 0.f);
            b.set (P::PhaserRate, 0.02f);
            b.uni (P::PhaserCenter, 150.f, 450.f);
        }

        // The bite: a fizz above 10 kHz under the spike of every cycle.
        b.set (P::NoiseType, 3.f);
        b.on (P::NoiseDirect, true);
        b.uni (P::NoiseLevel, 0.12f, 0.28f);
        if (b.chance (0.3f))   // more grit in the body
        {
            b.on (P::DistOn, true);
            b.pick (P::DistType, { (int) DistType::Tube, (int) DistType::Hard });
            b.uni (P::DistDrive, 6.f, 14.f);
            b.uni (P::DistMix, 0.2f, 0.5f);
            b.set (P::DistTone, 20000.f);
        }

        // the swell: the start level and how late it is full
        const float lengthMs = reverseLength (b, false) * 60000.f / 140.f;   // always the references' one beat
        ampEnv (b, 0.26f * lengthMs, 0.66f * lengthMs, 1000.f, 1000.f, 1.f, 1.f, 2.f, 5.f);   // the release is the cut
        b.uni (P::AenvCurve, 0.55f, 0.95f);
        b.uni (P::AenvStart, 0.025f, 0.35f);

        // Through the sweep (0 -> 1 over the beat): a fall onto the note at the
        // end (043, 080), or the filter opening as it swells (062, 072, 074).
        const int move = b.weighted ({ 0.45f, 0.2f, 0.35f });
        if (move == 1)
        {
            static constexpr float falls[] = { 3.f, 4.f, 5.f, 7.f };
            const float semis = falls[b.weighted ({ 0.2f, 0.35f, 0.3f, 0.15f })];
            b.set (P::OscACoarse, semis);   // starts above the note, lands on it
            if (b.state().on (P::OscBOn)) b.set (P::OscBCoarse, semis + 12.f);
            b.route (S::Sweep, D::PitchAll, -semis / 24.f, -semis / 24.f);
            b.uni (P::EnergyDelay, 0.6f, 0.72f);
            b.uni (P::EnergyCurve, -0.3f, 0.f);
        }
        else if (move == 2)
        {
            b.uni (P::F1Cutoff, 500.f, 1600.f);
            b.route (S::Sweep, D::F1Cutoff, 0.35f, 0.55f);
            b.uni (P::EnergyCurve, 0.2f, 0.7f);
        }

        // a stereo top over a mono low end, in about half the references
        fxWidth (b, 0.45f, 0.3f, 0.75f);

        b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.35f);
        fxGainBass (b, 5.5f, 7.5f);
        b.macros ({ D::NoiseLevel, D::F2Cutoff, D::F1Reso, D::DistDrive, D::F1Drive });
    }

    // Reverse sub: a sub played backwards. Fitted to the reference shots
    // (Bass Shots 2/018 E, 025 G#, 026 G#: one beat at 140 BPM) by rendering
    // and comparing level, pitch and harmonics. It swells from silence (about
    // -40 dB after 30 ms, -12 dB at 180 ms) to full at 55-60 % of the beat,
    // holds its pitch, then slides from just past the middle to the end by an
    // octave (half the patches), a fifth or a fourth (a quarter each), down
    // from an octave up (two thirds) or up from the note, easing in. A fall is a warm sine (a quieter sine an octave up
    // for the second harmonic, a little tube for the third); a rise is a saw
    // through a band-pass that follows the key (the fundamental 6 dB under
    // the second harmonic), as in the references. Stops dead. Mono, dry.
    void reverseSub (B& b)
    {
        monoVoice (b, false, 0.f, 0.f);
        b.set (P::Octave, -1.f);
        b.set (P::VelSens, 0.15f);
        b.set (P::Accent, 0.f);

        const float interval = b.chance (0.5f) ? 12.f : (b.chance (0.5f) ? 7.f : 5.f);
        const bool down = b.chance (0.67f);
        const float startOct = down ? 12.f : 0.f;   // falls start an octave up

        if (down)   // the falling references are warm sines, the rising one is the band-passed saw
        {
            analogA (b, kSine, 0.85f);
            b.on (P::OscBOn, true);   // the second harmonic, clean at any level
            b.set (P::OscBType, (float) OscType::Analog);
            b.set (P::OscBShape, kSine);
            b.set (P::OscBFine, 0.f);
            b.uni (P::OscBLevel, 0.08f, 0.2f);
            // the third harmonic; more drive would squash the swell and,
            // through the octave, raise the fourth above the references'
            b.on (P::DistOn, true);
            b.set (P::DistType, (float) DistType::Tube);
            b.uni (P::DistDrive, 19.f, 24.f);
            b.uni (P::DistMix, 0.6f, 0.75f);
            b.uni (P::DistTone, 2000.f, 5000.f);
            b.set (P::F1Type, (float) ft (FilterType::Off));
        }
        else
        {
            analogA (b, kSaw, 0.85f);
            filter1 (b, ft (FilterType::BP24), 300.f, 420.f, 0.f, 0.2f, 0.f, 0.f);
            b.set (P::F1Key, 1.f);   // the band sits on the second harmonic of every key
            b.set (P::DistOn, 0.f);
        }
        b.set (P::OscACoarse, startOct);
        b.set (P::OscBCoarse, startOct + 12.f);

        const float beatMs = reverseLength (b, false) * 60000.f / 140.f;   // always the references' one beat
        ampEnv (b, 0.53f * beatMs, 0.6f * beatMs, 1000.f, 1000.f, 1.f, 1.f, 2.f, 5.f);
        b.uni (P::AenvCurve, 0.62f, 0.78f);

        // the slide: still until just past the middle, then straight down,
        // or easing in going up
        const float amt = (down ? -interval : interval) / 24.f;
        b.route (S::Sweep, D::PitchAll, amt, amt);
        b.uni (P::EnergyDelay, 0.55f, 0.63f);
        if (down) b.uni (P::EnergyCurve, -0.5f, -0.15f);
        else b.uni (P::EnergyCurve, -0.4f, -0.2f);

        fxGainBass (b, 4.f, 6.f);
        b.macros ({ D::DistDrive, D::LevelB, D::F1Cutoff });
    }

    // =========================================================================
    // Psy Arp, FM, Pad and Perc come from about 2,800 psytrance Serum presets
    // (Ghosthack, Outerverse, Lunatica, PME, InterSys, Tunecraft, WeirDDose
    // and more), decoded and read as a population: how many presets use each
    // module, and the spread of its settings. The ranges and odds below are
    // those shares and spreads, mapped onto this engine.
    // =========================================================================

    // Goa and psy sequences: one 16th line over an octave pedal (the arp
    // writes it). The 245 reference sequences mostly run a saw, square or
    // wavetable through a ladder or band-pass that a slow synced LFO sweeps
    // over one to four bars (about five octaves), or that snaps per note;
    // an FM pair or a synced osc in some. A dotted-eighth echo on nearly
    // all, multiband compression and grit on most.
    void psyArp (B& b)
    {
        const int voice = b.weighted ({ 0.69f, 0.19f, 0.12f });   // poly, mono, legato with glide
        if (voice == 0) polyVoice (b, 4, 8);
        else monoVoice (b, voice == 2, voice == 2 ? 20.f : 0.f, voice == 2 ? 120.f : 0.f);
        b.pick (P::Octave, { -1, 0, 1 }, { 0.35f, 0.5f, 0.15f });

        // most of the references layer a second oscillator: the same note, an octave away or a fifth up
        const auto secondOsc = [&b] (float chance) {
            const bool second = b.chance (chance);
            if (! second) return;
            analogB (b, b.chance (0.55f) ? kSaw : kSquare, 0.35f, 0.7f);
            b.pick (P::OscBCoarse, { 0, 12, -12, 7 }, { 0.5f, 0.22f, 0.2f, 0.08f });
            b.uni (P::OscBFine, 4.f, 14.f);
        };
        switch (b.weighted ({ 0.38f, 0.3f, 0.26f, 0.06f }))
        {
            case 0:   // saw or square
                analogA (b, b.chance (0.6f) ? kSaw : kSquare);
                secondOsc (0.75f);
                break;
            case 1:   // a wavetable, scanned over the bars in most
            {
                wavetableA (b, { 0, 1, 5, 7, 4 }, 0.1f, 0.8f);
                secondOsc (0.5f);
                const bool scan = b.chance (0.75f);
                if (scan)
                {
                    lfoSyncW (b, 2, LfoShape::Triangle, { D1_1, D2_1, D4_1 }, { 0.5f, 0.3f, 0.2f }, false);
                    b.route (S::Lfo3, D::WtPosA, 0.15f, 0.4f);
                }
                break;
            }
            case 2:   // FM: the index snapped per note or swept with the bars
            {
                fmA (b, { 1.f, 2.f, 3.f, 0.5f, 1.5f }, 0.5f, 4.f, 0.f, 0.4f);
                secondOsc (0.4f);
                if (b.chance (0.5f))
                {
                    modEnv (b, 0.f, 0.f, 40.f, 250.f, 0.f, 0.2f, 20.f, 80.f);
                    b.route (S::ModEnv, D::FmA, 0.15f, 0.4f);
                }
                else
                {
                    lfoSyncW (b, 2, LfoShape::Triangle, { D1_1, D2_1, D1_2 }, { 0.5f, 0.3f, 0.2f }, false);
                    b.route (S::Lfo3, D::FmA, 0.1f, 0.3f);
                }
                break;
            }
            default:  // hard sync: osc B synced to A, its pitch swept with the bars
                analogA (b, kSaw, 0.4f);
                analogB (b, kSaw, 0.7f, 0.9f);
                b.on (P::OscBSync, true);
                b.uni (P::OscBCoarse, 5.f, 19.f);
                lfoSyncW (b, 2, LfoShape::Triangle, { D1_1, D2_1, D1_2 }, { 0.5f, 0.3f, 0.2f }, false);
                b.route (S::Lfo3, D::PitchB, 0.1f, 0.3f);
                break;
        }

        // A ladder or plain low-pass in half, a band-pass in a quarter, a
        // high-pass, comb or formant filter now and then. The cutoff knob sits
        // in the middle of where the LFO sweeps it (it swings both ways).
        const int fil = b.weighted ({ 0.38f, 0.15f, 0.25f, 0.1f, 0.06f, 0.06f });
        const int motion = b.weighted ({ 0.65f, 0.2f, 0.15f });   // the bars' sweep, a per-note snap, or both
        const float envLo = motion == 0 ? 0.f : 0.3f, envHi = motion == 0 ? 0.05f : 0.65f;
        switch (fil)
        {
            case 0:  filter1 (b, ft (FilterType::Ladder24), 500.f, 3500.f, 0.1f, 0.5f, envLo, envHi); break;
            case 1:  filter1 (b, ft (FilterType::LP12), 600.f, 4000.f, 0.1f, 0.45f, envLo, envHi); break;
            case 2:  filter1 (b, ft (FilterType::BP12), 500.f, 2000.f, 0.15f, 0.55f, envLo, envHi); break;
            case 3:  filter1 (b, ft (FilterType::HP12), 150.f, 600.f, 0.05f, 0.35f, envLo * 0.5f, envHi * 0.5f); break;
            case 4:
                filter1 (b, ft (FilterType::CombPos), 200.f, 1200.f, 0.3f, 0.7f, envLo * 0.5f, envHi * 0.5f);
                b.uni (P::F1Key, 0.8f, 1.f);
                break;
            default:
                filter1 (b, ft (FilterType::Formant), 600.f, 2000.f, 0.3f, 0.7f, envLo * 0.5f, envHi * 0.5f);
                b.uni (P::F1Vowel, 0.f, 1.f);
                break;
        }
        if (motion != 1)
        {
            lfoSyncW (b, 1, b.chance (0.7f) ? LfoShape::Triangle : LfoShape::Sine, { D1_1, D2_1, D4_1, D1_2 }, { 0.4f, 0.25f, 0.2f, 0.15f }, false);
            b.route (S::Lfo2, D::F1Cutoff, 0.12f, 0.32f);
        }
        filterEnv (b, 0.f, 0.f, 60.f, 280.f, 0.f, 0.15f, 30.f, 120.f);
        const bool drive = b.chance (0.6f);
        if (drive) b.uni (P::F1Drive, 0.f, 0.4f);
        // the references gate their notes (the rhythm is the sequence's), so the body holds
        ampEnv (b, 0.f, 3.f, 120.f, 500.f, 0.5f, 1.f, 15.f, 80.f);

        // a quarter step the cutoff through the bar as the references draw it:
        // 16 (or 8) held steps on LFO 1, synced to the bar; accents brighter in some
        const bool steps = b.chance (0.25f);
        if (steps)
        {
            drawSteps (b, 0, b.chance (0.65f) ? 16 : 8, { 0.f, 0.25f, 0.5f, 0.75f, 1.f });
            b.set (lfoParam (0, 2), 1.f);
            b.pick (lfoParam (0, 3), { D1_1, D2_1 }, { 0.8f, 0.2f });
            b.set (lfoParam (0, 4), 0.f);
            b.route (S::Lfo1, D::F1Cutoff, 0.06f, 0.18f);
        }
        const bool bright = b.chance (0.25f);
        if (bright) b.route (S::Velocity, D::F1Cutoff, 0.05f, 0.15f);
        b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.4f);

        fxDelay (b, 0.78f, { D1_8D, D1_8D, D1_8D, D1_8, D1_4, D1_4D, D1_16D }, 0.2f, 0.6f, 0.2f, 0.5f);
        fxReverb (b, 0.6f, 1.2f, 4.f, 0.1f, 0.35f, 0.25f, 0.6f);
        // grit: a drive or a downsampler
        fxDist (b, 0.45f, { (int) DistType::Tube, (int) DistType::Soft, (int) DistType::Hard }, 3.f, 14.f, 0.3f, 0.8f);
        if (b.chance (0.15f))
        {
            b.on (P::CrushOn, true);
            b.uni (P::CrushBits, 6.f, 12.f);
            b.uni (P::CrushRate, 1.f, 6.f);
            b.uni (P::CrushMix, 0.2f, 0.6f);
        }
        // squashed: a multiband compressor in half, a glue compressor in some of the rest
        const int squash = b.weighted ({ 0.47f, 0.3f, 0.23f });
        if (squash == 0) fxCompMulti (b);
        else if (squash == 1) fxCompGlue (b);
        fxChorus (b, 0.2f);
        fxPhaser (b, 0.08f);
        fxWidth (b, 0.26f, 0.3f, 0.7f);
        fxEq (b, 0.93f, 0.29f, 100.f, 340.f, 0.62f, { 0.29f, 0.46f, 0.25f });
        fxDim (b, 0.26f, 0.15f, 0.4f);
        b.macros ({ D::F1Cutoff, D::F1Reso, D::FilterEnvAmt, D::Lfo2Rate, D::DelayMix, D::DistDrive, D::WtPosA, D::FmA, D::ReverbMix });
    }

    // Hi-tech and full-on FM, from the corpus's 62 FM presets: a two-operator
    // voice whose index moves with a synced LFO (the talking growl), an
    // envelope (the bite) or the bars, through a band-pass or high-pass that
    // a slow LFO sweeps. Mostly one oscillator, mono in four of ten, an
    // octave down, driven, squashed by a multiband compressor and thrown
    // into a dotted-eighth echo.
    void fm (B& b)
    {
        const int voice = b.weighted ({ 0.6f, 0.35f, 0.05f });   // poly, mono, legato with glide
        if (voice == 0) polyVoice (b, 4, 6);
        else monoVoice (b, voice == 2, voice == 2 ? 15.f : 0.f, voice == 2 ? 70.f : 0.f);
        b.pick (P::Octave, { -1, 0, -2 }, { 0.45f, 0.4f, 0.15f });

        // osc A's own FM pair (a second pair on osc B now and then), or osc A
        // (a saw, square or sine) phase-modulating an FM carrier on osc B
        const bool pair = b.chance (0.6f);
        if (pair)
        {
            fmA (b, { 1.f, 2.f, 3.f, 0.5f, 1.5f, 4.f, 7.f }, 1.f, 7.f, 0.f, 0.5f);
            const bool second = b.chance (0.3f);
            if (second)
            {
                b.on (P::OscBOn, true);
                b.set (P::OscBType, (float) OscType::FM);
                b.among (P::OscBFmRatio, { 1.f, 2.f, 3.f, 0.5f });
                b.uni (P::OscBFm, 0.5f, 4.f);
                b.uni (P::OscBLevel, 0.3f, 0.6f);
                b.pick (P::OscBCoarse, { 12, -12, 7, 0 }, { 0.35f, 0.3f, 0.15f, 0.2f });
                b.uni (P::OscBFine, -8.f, 8.f);
            }
        }
        else
        {
            const float shapes[] = { kSaw, kSquare, kSine };
            analogA (b, shapes[b.weighted ({ 0.4f, 0.25f, 0.35f })], 0.f);
            b.uni (P::OscALevel, 0.f, 0.25f);
            b.on (P::OscBOn, true);
            b.set (P::OscBType, (float) OscType::FM);
            b.among (P::OscBFmRatio, { 1.f, 2.f, 0.5f, 3.f });
            b.uni (P::OscBFm, 0.f, 2.f);
            b.uni (P::OscBLevel, 0.7f, 0.9f);
            b.pick (P::OscBCoarse, { 0, 12, -12, 7, 19 }, { 0.45f, 0.2f, 0.15f, 0.1f, 0.1f });
            b.uni (P::OscBXMod, 0.15f, 0.6f);
        }
        const D index = pair ? D::FmA : D::XMod;

        // what moves the index: a synced LFO, an envelope, or a slow sweep
        const int move = b.weighted ({ 0.4f, 0.3f, 0.3f });
        if (move == 0)
        {
            lfoSyncW (b, 0, b.chance (0.6f) ? LfoShape::Triangle : LfoShape::SawDown, { D1_16, D1_8, D1_8D, D1_4, D1_8T },
                      { 0.3f, 0.25f, 0.15f, 0.15f, 0.15f }, true);
            b.route (S::Lfo1, index, pair ? 0.15f : 0.1f, pair ? 0.4f : 0.3f);
        }
        else if (move == 1)
        {
            modEnv (b, 0.f, 2.f, 60.f, 500.f, 0.f, 0.3f, 30.f, 150.f);
            b.route (S::ModEnv, index, 0.2f, 0.5f);
        }
        else
        {
            lfoSyncW (b, 0, LfoShape::Triangle, { D2_1, D4_1, D1_1 }, { 0.4f, 0.3f, 0.3f }, false);
            b.route (S::Lfo1, index, 0.15f, 0.4f);
        }
        const bool velocity = b.chance (0.2f);
        if (velocity) b.route (S::Velocity, index, 0.1f, 0.25f);

        // a little noise, a ring layer or a sub now and then; a pitch blip on the front in some
        if (b.chance (0.15f)) { b.pick (P::NoiseType, { 0, 2 }); b.uni (P::NoiseLevel, 0.02f, 0.08f); }
        if (b.chance (0.12f)) b.uni (P::RingLevel, 0.15f, 0.4f);
        if (b.chance (0.1f)) b.uni (P::SubLevel, 0.1f, 0.3f);
        if (b.chance (0.25f))
        {
            b.set (P::PenvAttack, 0.f);
            b.set (P::PenvDest, 0.f);
            b.uni (P::PenvAmt, 3.f, 12.f);
            b.uni (P::PenvDecay, 8.f, 40.f);
            b.uni (P::PenvCurve, 0.4f, 0.8f);
        }

        // band-pass or high-pass mostly, swept by a slow LFO in most
        const int fil = b.weighted ({ 0.4f, 0.3f, 0.2f, 0.05f, 0.05f });
        switch (fil)
        {
            case 0:  filter1 (b, ft (FilterType::BP12), 400.f, 2500.f, 0.15f, 0.5f, 0.f, 0.1f); break;
            case 1:  filter1 (b, ft (FilterType::HP12), 80.f, 450.f, 0.1f, 0.4f, 0.f, 0.05f); break;
            case 2:  filter1 (b, b.chance (0.5f) ? ft (FilterType::LP12) : ft (FilterType::Ladder24), 1200.f, 6000.f, 0.1f, 0.4f, 0.2f, 0.5f); break;
            case 3:
                filter1 (b, ft (FilterType::CombPos), 150.f, 800.f, 0.4f, 0.8f, 0.f, 0.f);
                b.uni (P::F1Key, 0.8f, 1.f);
                break;
            default: b.set (P::F1Type, (float) ft (FilterType::Off)); break;
        }
        filterEnv (b, 0.f, 0.f, 80.f, 400.f, 0.f, 0.2f, 30.f, 150.f);
        const bool sweep = b.chance (fil == 0 ? 0.65f : 0.4f);
        if (sweep && fil < 3)
        {
            lfoSyncW (b, 1, LfoShape::Triangle, { D1_1, D1_16, D2_1, D1_2, D1_8D }, { 0.3f, 0.2f, 0.2f, 0.15f, 0.15f }, false);
            b.route (S::Lfo2, D::F1Cutoff, 0.12f, 0.35f);
        }
        ampEnv (b, 0.5f, 5.f, 150.f, 1500.f, 0.4f, 1.f, 10.f, 80.f);
        b.route (S::Macro1, index, 0.2f, 0.4f);

        fxDist (b, 0.65f, { (int) DistType::Tube, (int) DistType::Soft, (int) DistType::Hard, (int) DistType::Fold, (int) DistType::Rectify },
                4.f, 16.f, 0.4f, 1.f);
        if (b.chance (0.12f))
        {
            b.on (P::CrushOn, true);
            b.uni (P::CrushBits, 5.f, 10.f);
            b.uni (P::CrushRate, 1.f, 8.f);
            b.uni (P::CrushMix, 0.2f, 0.6f);
        }
        if (b.chance (0.5f)) fxCompMulti (b);
        // the references mostly leave the echo for a macro to open: on in some, quiet
        fxDelay (b, 0.4f, { D1_8D, D1_8D, D1_8D, D1_8, D1_16 }, 0.1f, 0.45f, 0.1f, 0.3f);
        fxReverb (b, 0.5f, 0.8f, 3.f, 0.05f, 0.25f, 0.2f, 0.5f);
        fxChorus (b, 0.3f);
        fxWidth (b, 0.2f, 0.3f, 0.7f);
        fxEq (b, 0.95f, 0.06f, 70.f, 220.f, 0.65f, { 0.7f, 0.15f, 0.15f });
        fxDim (b, 0.17f, 0.08f, 0.25f);
        b.macros ({ index, D::F1Cutoff, D::F1Reso, D::Lfo1Rate, D::DistDrive, D::DelayMix, D::ReverbMix });
    }

    // Breakdown and melodic-psy chord beds, from the corpus's 115 pads: two
    // oscillators (a supersaw, stacked saws or a scanned wavetable, osc B on
    // the note or an octave away), a noise layer in four of ten and a sub in
    // three, a ladder low-pass that a slow LFO breathes on, a swell of
    // 0.15-1.8 s and a long release, chorus, width and a big reverb. Atmos
    // keeps the textures; a pad is the harmony.
    void pad (B& b)
    {
        polyVoice (b, 6, 8);
        const int kind = b.weighted ({ 0.25f, 0.4f, 0.35f });
        if (kind == 0) supersawA (b, 0.25f, 0.6f);
        else if (kind == 1)
        {
            analogA (b, b.chance (0.75f) ? kSaw : kSquare, 0.75f);
            // an octave or a fifth inside the unison stack (unisonRecipe keeps a stacked count)
            const bool stacked = b.chance (0.35f);
            if (stacked)
            {
                b.pick (P::OscAUniStack, { (int) UniStack::Oct, (int) UniStack::OctFifth, (int) UniStack::TwoOct, (int) UniStack::CenterDown1 },
                        { 0.35f, 0.25f, 0.15f, 0.25f });
                b.uni (P::OscAUnison, 5.f, 9.f);
            }
        }
        else wavetableA (b, { 0, 1, 2, 6 }, 0.1f, 0.8f);

        const bool second = b.chance (0.85f);
        if (second)
        {
            b.on (P::OscBOn, true);
            b.pick (P::OscBType, { (int) OscType::Analog, (int) OscType::Wavetable }, { 0.7f, 0.3f });
            b.set (P::OscBShape, b.chance (0.7f) ? kSaw : kSquare);
            b.pick (P::OscBWt, { 0, 1, 2, 6 });
            b.uni (P::OscBWtPos, 0.f, 1.f);
            b.uni (P::OscBLevel, 0.35f, 0.65f);
            b.pick (P::OscBCoarse, { 0, 12, -12, 7, 19, 24 }, { 0.35f, 0.25f, 0.2f, 0.07f, 0.05f, 0.08f });
            b.uni (P::OscBFine, 5.f, 15.f);
        }
        const bool air = b.chance (0.4f);
        if (air) { b.pick (P::NoiseType, { 1, 0, 3 }, { 0.5f, 0.3f, 0.2f }); b.uni (P::NoiseLevel, 0.02f, 0.1f); }
        const bool sub = b.chance (0.3f);
        if (sub) b.uni (P::SubLevel, 0.1f, 0.3f);

        const int fil = b.weighted ({ 0.55f, 0.35f, 0.05f, 0.05f });
        const bool swell = b.chance (0.15f);   // the filter opens with the note
        const float envLo = swell ? 0.15f : 0.f, envHi = swell ? 0.35f : 0.08f;
        switch (fil)
        {
            case 0:  filter1 (b, ft (FilterType::Ladder24), 500.f, 3500.f, 0.05f, 0.4f, envLo, envHi); break;
            case 1:  filter1 (b, ft (FilterType::LP12), 600.f, 4000.f, 0.05f, 0.35f, envLo, envHi); break;
            case 2:  filter1 (b, ft (FilterType::BP12), 600.f, 2000.f, 0.1f, 0.4f, envLo, envHi); break;
            default:
                filter1 (b, ft (FilterType::Formant), 700.f, 2000.f, 0.2f, 0.5f, envLo, envHi);
                b.uni (P::F1Vowel, 0.f, 1.f);
                break;
        }
        filterEnv (b, 200.f, 1800.f, 800.f, 3000.f, 0.3f, 0.8f, 800.f, 3000.f);
        const bool keyed = b.chance (0.12f);
        if (keyed) b.uni (P::F1Key, 0.1f, 0.3f);

        // the breath: a slow LFO on the cutoff, synced over bars or free
        const bool breathe = b.chance (0.7f);
        const bool synced = b.chance (0.55f);
        if (breathe)
        {
            if (synced) lfoSyncW (b, 1, b.chance (0.6f) ? LfoShape::Sine : LfoShape::Triangle, { D2_1, D4_1, D1_1 }, { 0.4f, 0.35f, 0.25f }, false);
            else b.lfo (1, LfoShape::Sine, 0.05f, 0.4f, false);
            b.route (S::Lfo2, D::F1Cutoff, 0.08f, 0.3f);
        }
        // a wavetable scans slowly, a saw or square morphs a little
        const bool scan = b.chance (kind == 2 ? 0.8f : 0.4f);
        if (scan && kind != 0)
        {
            lfoSyncW (b, 2, LfoShape::Triangle, { D4_1, D2_1 }, { 0.6f, 0.4f }, false);
            if (kind == 2) b.route (S::Lfo3, D::WtPosA, 0.1f, 0.3f);
            else b.route (S::Lfo3, D::ShapeA, 0.04f, 0.12f);
        }
        // a slow drift in pitch, or a tremolo on the beat
        const int life = b.weighted ({ 0.55f, 0.3f, 0.15f });
        if (life == 1)
        {
            b.lfo (0, LfoShape::SmoothRandom, 0.1f, 0.6f, false);
            b.route (S::Lfo1, D::PitchAll, 0.002f, 0.006f);
        }
        else if (life == 2)
        {
            lfoSyncW (b, 0, LfoShape::Sine, { D1_8, D1_4, D1_8D }, { 0.4f, 0.35f, 0.25f }, false);
            b.route (S::Lfo1, D::Amp, 0.15f, 0.35f);
        }

        ampEnv (b, 150.f, 1800.f, 1000.f, 3000.f, 0.7f, 1.f, 600.f, 3500.f);
        const bool curve = b.chance (0.4f);
        if (curve) b.uni (P::AenvCurve, 0.3f, 0.7f);
        b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.4f);

        fxReverb (b, 0.95f, 2.5f, 8.f, 0.25f, 0.5f, 0.45f, 0.85f);
        fxDelay (b, 0.7f, { D1_4, D1_4, D1_8D, D1_4D, D1_2 }, 0.25f, 0.6f, 0.15f, 0.35f);
        fxChorus (b, 0.45f);
        fxWidth (b, 0.45f, 0.3f, 0.7f);
        fxPhaser (b, 0.15f);
        fxDist (b, 0.35f, { (int) DistType::Tube, (int) DistType::Soft }, 2.f, 8.f, 0.2f, 0.5f);
        const int squash = b.weighted ({ 0.2f, 0.3f, 0.5f });   // multiband, glue, none
        if (squash == 0) fxCompMulti (b);
        else if (squash == 1) fxCompGlue (b);
        // a gated pad now and then (the corpus's pads chop their volume with
        // drawn steps): 16 sixteenths on or off over the bar, on LFO 1
        const bool gated = b.chance (0.12f);
        if (gated && life == 0)
        {
            drawSteps (b, 0, 16, { 0.f, 0.f, -1.f });
            b.set (lfoParam (0, 2), 1.f);
            b.set (lfoParam (0, 3), (float) D1_1);
            b.set (lfoParam (0, 4), 0.f);
            b.route (S::Lfo1, D::Amp, 0.85f, 1.f);
        }
        fxEq (b, 0.88f, 0.19f, 90.f, 300.f, 0.7f, { 0.32f, 0.27f, 0.41f });
        fxDim (b, 0.26f, 0.3f, 0.6f);
        b.macros ({ D::F1Cutoff, D::F1Reso, D::DetuneA, D::WtPosA, D::Lfo2Rate, D::ReverbMix, D::ChorusMix, D::DelayMix, D::NoiseLevel });
    }

    // Psytrance percussion around the kick, from the corpus's drum presets:
    // closed and open hats (noise high-passed at 2-9 kHz, 30-450 ms, a
    // ring-modded metal layer in some), snares with a short body or a laser
    // dive, tonal percs and toms (a sine or soft FM pair falling a few
    // semitones), alien percs (inharmonic FM or ring mod through a band-pass
    // or comb) and claps (band-passed noise in bursts a few ms apart, the
    // amp envelope's Bursts). One hit per note, so no arp.
    void perc (B& b)
    {
        polyVoice (b, 4, 8);
        b.uni (P::VelSens, 0.5f, 0.8f);
        const int kind = b.weighted ({ 0.27f, 0.14f, 0.17f, 0.15f, 0.13f, 0.14f });
        switch (kind)
        {
            case 0:   // closed hat (a shaker when its attack is soft)
            case 1:   // open hat
            {
                b.pick (P::NoiseType, { 0, 3, 2, 1 }, { 0.45f, 0.25f, 0.2f, 0.1f });
                b.uni (P::NoiseLevel, 0.45f, 0.65f);
                const bool metal = b.chance (0.35f);   // two detuned squares ring-modulated under the noise
                if (metal)
                {
                    analogA (b, kSquare, 0.f);
                    analogB (b, kSquare, 0.f, 0.f);
                    b.pick (P::OscBCoarse, { 11, 13, 17, 23 });
                    b.uni (P::OscBFine, -40.f, 40.f);
                    b.uni (P::RingLevel, 0.15f, 0.35f);
                    b.pick (P::Octave, { 1, 2 });
                }
                else b.on (P::OscAOn, false);
                // high-passed mostly, a band-pass in some, left open now and then
                const int fil = b.weighted ({ 0.7f, 0.2f, 0.1f });
                if (fil == 0) filter1 (b, ft (FilterType::HP12), 3000.f, 9000.f, 0.f, 0.35f, 0.f, 0.f);
                else if (fil == 1) filter1 (b, ft (FilterType::BP12), 5000.f, 10000.f, 0.1f, 0.4f, 0.f, 0.f);
                else b.set (P::F1Type, (float) ft (FilterType::Off));
                const bool ring = b.chance (0.25f);   // a band of ring on an open hat
                if (kind == 1 && ring)
                {
                    b.set (P::F2Type, (float) ft (FilterType::BP12));
                    b.uni (P::F2Cutoff, 6000.f, 10000.f);
                    b.uni (P::F2Reso, 0.2f, 0.5f);
                }
                const bool shaker = b.chance (0.2f);
                if (kind == 0) ampEnv (b, shaker ? 8.f : 0.f, shaker ? 30.f : 1.5f, 30.f, 140.f, 0.f, 0.f, 15.f, 70.f);
                else ampEnv (b, 0.f, 2.f, 120.f, 450.f, 0.f, 0.f, 40.f, 150.f);
                break;
            }
            case 2:   // snare: a short body under bright noise, or a laser snare's dive
            {
                const bool laser = b.chance (0.45f);
                const bool sawBody = b.chance (0.5f);
                const bool knock = b.chance (0.4f);   // a plain snare's body drops a little in some
                analogA (b, laser && sawBody ? kSaw : kTri, 0.55f);
                b.set (P::PenvAttack, 0.f);
                b.set (P::PenvDest, 0.f);
                if (laser)
                {
                    b.uni (P::PenvAmt, 24.f, 48.f);
                    b.uni (P::PenvDecay, 60.f, 300.f);
                    b.uni (P::PenvCurve, 0.3f, 0.7f);
                }
                else if (knock)
                {
                    b.uni (P::PenvAmt, 5.f, 14.f);
                    b.uni (P::PenvDecay, 15.f, 50.f);
                    b.uni (P::PenvCurve, 0.4f, 0.8f);
                }
                b.pick (P::NoiseType, { 0, 1, 3 }, { 0.6f, 0.25f, 0.15f });
                b.uni (P::NoiseLevel, laser ? 0.1f : 0.35f, laser ? 0.3f : 0.6f);
                filter1 (b, ft (FilterType::HP12), 250.f, 900.f, 0.f, 0.3f, 0.f, 0.f);
                ampEnv (b, 0.f, 1.f, 110.f, 350.f, 0.f, 0.f, 30.f, 120.f);
                b.pick (P::Octave, { 0, 1 }, { 0.6f, 0.4f });
                break;
            }
            case 3:   // tonal perc or tom: a sine, triangle or soft FM pair dropping a few semitones
            {
                const int body = b.weighted ({ 0.45f, 0.25f, 0.3f });
                if (body == 2) fmA (b, { 1.f, 2.f, 1.5f, 3.f }, 0.3f, 2.5f, 0.f, 0.2f);
                else analogA (b, body == 0 ? kSine : kTri, 0.8f);
                b.set (P::PenvAttack, 0.f);
                b.set (P::PenvDest, 0.f);
                b.uni (P::PenvAmt, 5.f, 24.f);
                b.uni (P::PenvDecay, 25.f, 180.f);
                b.uni (P::PenvCurve, 0.3f, 0.8f);
                const bool click = b.chance (0.5f);
                if (click) { b.pick (P::NoiseType, { 0, 1 }); b.uni (P::NoiseLevel, 0.03f, 0.12f); }
                filter1 (b, ft (FilterType::LP12), 1500.f, 7000.f, 0.f, 0.3f, 0.2f, 0.5f);
                filterEnv (b, 0.f, 0.f, 40.f, 200.f, 0.f, 0.f, 20.f, 80.f);
                ampEnv (b, 0.f, 1.f, 100.f, 400.f, 0.f, 0.f, 30.f, 150.f);
                b.pick (P::Octave, { -1, 0, 1 }, { 0.3f, 0.5f, 0.2f });
                break;
            }
            case 4:   // alien perc: inharmonic FM or ring mod through a band-pass or comb
            {
                const bool fmBody = b.chance (0.55f);
                if (fmBody)
                {
                    fmA (b, { 1.41f, 1.73f, 2.76f, 3.5f, 5.19f }, 1.5f, 6.f, 0.f, 0.5f);
                    modEnv (b, 0.f, 0.f, 20.f, 150.f, 0.f, 0.f, 20.f, 60.f);
                    b.route (S::ModEnv, D::FmA, 0.2f, 0.45f);
                }
                else
                {
                    analogA (b, kSquare, 0.3f);
                    analogB (b, b.chance (0.5f) ? kSquare : kSine, 0.f, 0.f);
                    b.pick (P::OscBCoarse, { 6, 11, 13, 18, 25 });
                    b.uni (P::OscBFine, -30.f, 30.f);
                    b.uni (P::RingLevel, 0.5f, 0.8f);
                }
                b.set (P::PenvAttack, 0.f);
                b.set (P::PenvDest, 0.f);
                b.uni (P::PenvAmt, 4.f, 24.f);
                b.uni (P::PenvDecay, 20.f, 200.f);
                b.uni (P::PenvCurve, 0.3f, 0.8f);
                const bool comb = b.chance (0.4f);
                if (comb)
                {
                    filter1 (b, ft (FilterType::CombPos), 200.f, 1500.f, 0.4f, 0.8f, 0.f, 0.2f);
                    b.uni (P::F1Key, 0.5f, 1.f);
                }
                else filter1 (b, ft (FilterType::BP12), 600.f, 4000.f, 0.3f, 0.7f, 0.2f, 0.5f);
                filterEnv (b, 0.f, 0.f, 60.f, 300.f, 0.f, 0.f, 20.f, 100.f);
                ampEnv (b, 0.f, 1.f, 150.f, 600.f, 0.f, 0.f, 40.f, 200.f);
                b.pick (P::Octave, { -1, 0, 1 }, { 0.3f, 0.45f, 0.25f });
                break;
            }
            default:  // clap: three to five bursts of band-passed noise 7-14 ms apart, then a short tail
            {
                b.on (P::OscAOn, false);
                b.pick (P::NoiseType, { 0, 1 }, { 0.7f, 0.3f });
                b.uni (P::NoiseLevel, 0.5f, 0.7f);
                filter1 (b, ft (FilterType::BP12), 900.f, 2400.f, 0.15f, 0.45f, 0.f, 0.f);
                b.pick (P::AenvBursts, { 3, 4, 5 }, { 0.3f, 0.45f, 0.25f });
                b.uni (P::AenvGap, 7.f, 14.f);
                ampEnv (b, 0.f, 1.f, 120.f, 380.f, 0.f, 0.f, 30.f, 120.f);
                const bool body = b.chance (0.3f);   // a snare's knock under some
                if (body)
                {
                    analogA (b, kTri, 0.35f);
                    b.set (P::PenvAttack, 0.f);
                    b.set (P::PenvDest, 0.f);
                    b.uni (P::PenvAmt, 5.f, 12.f);
                    b.uni (P::PenvDecay, 15.f, 40.f);
                    b.uni (P::PenvCurve, 0.4f, 0.8f);
                }
                b.pick (P::Octave, { 0, 1 }, { 0.6f, 0.4f });
                break;
            }
        }

        // hats open up on soft hits and thin out on hard ones, tonal hits and
        // claps brighten (in some); no two hits quite alike
        const bool hat = kind < 2;
        const bool touch = b.chance (0.3f);
        if (touch) b.route (S::Velocity, D::F1Cutoff, hat ? -0.15f : 0.1f, hat ? -0.05f : 0.25f);
        b.route (S::Random, D::F1Cutoff, 0.03f, 0.1f);
        b.route (S::Macro1, hat || kind == 5 ? D::F1Cutoff : D::PitchEnvAmt, 0.2f, 0.4f);

        fxDist (b, 0.45f, { (int) DistType::Soft, (int) DistType::Tube, (int) DistType::Hard }, 3.f, 12.f, 0.2f, 0.6f);
        if (b.chance (0.12f))
        {
            b.on (P::CrushOn, true);
            b.uni (P::CrushBits, 6.f, 12.f);
            b.uni (P::CrushRate, 1.f, 6.f);
            b.uni (P::CrushMix, 0.2f, 0.5f);
        }
        fxReverb (b, kind == 5 ? 0.8f : 0.5f, 0.3f, 1.5f, 0.06f, 0.25f, 0.2f, 0.5f);   // claps want a room
        fxDelay (b, 0.2f, { D1_8D, D1_16, D1_16D, D1_4 }, 0.1f, 0.35f, 0.05f, 0.2f);
        fxWidth (b, 0.35f, 0.3f, 0.8f);
        if (b.chance (0.35f)) fxCompGlue (b);
        // the corpus's percs only lift their top, and a hat's top is its sound: never cut
        if (hat) fxEq (b, 0.85f, 0.1f, 120.f, 400.f, 0.6f, { 0.25f, 0.75f, 0.f }, 0.f);
        else fxEq (b, 0.85f, 0.1f, 120.f, 400.f, 0.6f, { 0.2f, 0.55f, 0.25f }, 0.f);
        fxDim (b, 0.3f, 0.15f, 0.4f);
        b.macros ({ D::F1Cutoff, D::NoiseLevel, D::PitchEnvAmt, D::F1Reso, D::DistDrive, D::ReverbMix, D::DelayMix });
    }
} // namespace

// Leads and pads spread their oscillators like a Serum patch would: a
// handful of voices, a little detune, wide. Basses keep one voice: detuned
// voices under 100 Hz cancel each other and the low end wobbles from note to
// note. The supersaw is already seven voices and is left alone.
void unisonRecipe (Category c, RecipeBuilder& b)
{
    struct Spread { float chanceA, chanceB; int nLo, nHi; float detLo, detHi, blendLo, blendHi, widthLo, widthHi; bool random; };
    Spread sp {};
    switch (c)
    {
        case Category::PsyLead:    sp = { 0.45f, 0.35f, 2, 7, 0.03f, 0.2f, 0.55f, 0.85f, 0.6f, 1.f, false }; break;
        case Category::Pluck:      sp = { 0.55f, 0.4f, 2, 6, 0.04f, 0.15f, 0.4f, 0.75f, 0.4f, 0.9f, false }; break;
        case Category::Stab:       sp = { 0.55f, 0.45f, 2, 9, 0.06f, 0.2f, 0.5f, 0.85f, 0.6f, 1.f, false }; break;
        case Category::Grid:       sp = { 0.4f, 0.25f, 2, 5, 0.04f, 0.12f, 0.3f, 0.6f, 0.3f, 0.8f, false }; break;
        case Category::Screech:    sp = { 0.3f, 0.2f, 2, 3, 0.04f, 0.12f, 0.3f, 0.6f, 0.3f, 0.7f, false }; break;
        case Category::Metallic:   sp = { 0.2f, 0.15f, 2, 3, 0.03f, 0.1f, 0.3f, 0.6f, 0.5f, 0.9f, true }; break;
        case Category::Atmos:      sp = { 0.7f, 0.5f, 5, 12, 0.12f, 0.45f, 0.7f, 0.95f, 0.8f, 1.f, true }; break;
        case Category::Drone:      sp = { 0.6f, 0.4f, 3, 6, 0.08f, 0.25f, 0.6f, 0.9f, 0.7f, 1.f, true }; break;
        case Category::Riser:
        case Category::Uplifter:
        case Category::Downlifter: sp = { 0.5f, 0.35f, 4, 7, 0.15f, 0.4f, 0.7f, 0.9f, 0.9f, 1.f, false }; break;
        case Category::PsyArp:     sp = { 0.3f, 0.2f, 2, 4, 0.02f, 0.12f, 0.4f, 0.75f, 0.4f, 0.9f, false }; break;
        case Category::Fm:         sp = { 0.12f, 0.1f, 2, 3, 0.02f, 0.08f, 0.3f, 0.6f, 0.3f, 0.7f, false }; break;
        case Category::Pad:        sp = { 0.75f, 0.55f, 4, 12, 0.06f, 0.25f, 0.6f, 0.9f, 0.8f, 1.f, true }; break;
        default:
            if (familyOf (c) == Family::Bass)
            {
                b.set (P::OscAUnison, 1.f);
                b.set (P::OscBUnison, 1.f);
            }
            return;
    }

    const auto osc = [&] (bool isB, float chance) {
        const bool roll = b.chance (chance);   // always drawn, so the seeds stay put
        const P on = isB ? P::OscBOn : P::OscAOn, type = isB ? P::OscBType : P::OscAType;
        if (! roll || ! b.state().on (on) || b.state().geti (type) == (int) OscType::Supersaw)
            return;
        // a recipe that set a stack chose its voice count (the stack needs it):
        // draw the number anyway so the seeds stay put
        if (! isB && b.state().geti (P::OscAUniStack) != (int) UniStack::Off)
            b.rnd (0.f, 1.f);
        else
            b.uni (isB ? P::OscBUnison : P::OscAUnison, (float) sp.nLo, (float) sp.nHi);
        b.uni (isB ? P::OscBDetune : P::OscADetune, sp.detLo, sp.detHi);
        b.uni (isB ? P::OscBBlend : P::OscABlend, sp.blendLo, sp.blendHi);
        b.uni (isB ? P::OscBUniWidth : P::OscAUniWidth, sp.widthLo, sp.widthHi);
        const int modes[] = { (int) UniMode::Linear, (int) UniMode::Super, (int) UniMode::Random };
        b.pick (isB ? P::OscBUniMode : P::OscAUniMode, { modes[0], modes[1], modes[2] },
                { 0.5f, sp.random ? 0.2f : 0.5f, sp.random ? 0.3f : 0.f });
        if (sp.random && b.state().geti (type) == (int) OscType::Wavetable)
            b.uni (isB ? P::OscBUniSpread : P::OscAUniSpread, 0.f, 0.3f);
    };
    osc (false, sp.chanceA);
    osc (true, sp.chanceB);
}

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
        case Category::ReverseBass: return reverseBass;
        case Category::ReverseSub:  return reverseSub;
        case Category::Jilax:       return jilaxRecipe;
        case Category::Grid:        return grid;
        case Category::GhostStab:   return ghostStabRecipe;
        case Category::GhostBass:   return ghostBassRecipe;
        case Category::Phoscyon:    return phoscyonRecipe;
        case Category::PsyArp:      return psyArp;
        case Category::Fm:          return fm;
        case Category::Pad:         return pad;
        case Category::Perc:        return perc;
        case Category::JilaxBass:   return jilaxBassRecipe;
        case Category::KickFullOn:
        case Category::KickGoa:
        case Category::KickDarkForest:
        case Category::KickHiTech:
        case Category::KickProgressive: return kickRecipeFor (c);
        case Category::Count:       break;   // no default: -Wswitch flags a category without a recipe
    }
    return acid;
}

} // namespace psy
