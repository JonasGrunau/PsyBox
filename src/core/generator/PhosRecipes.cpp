// The Phoscyon category: TB-303 patches on the Phoscyon engine. The ranges
// follow how the original's own factory sounds spread their knobs (171
// synthesis presets, 250 scenes): a mostly closed cutoff opened by the
// envelope, high resonance, the accent near full, distortion on in almost all
// of them (mostly the diode models, moderate drive), and the scenes' effects:
// an EQ that scoops the low mids and lifts the top, a short reverb, a limiter
// last, a filtered delay in some and a chorus in a few.

#include "Generator.h"
#include "engine/PhosVoice.h"

#include <array>

namespace psy
{

namespace
{
    using B = RecipeBuilder;


    void synth (B& b)
    {
        b.pick (P::PhosWave, { 0, 1 }, { 0.4f, 0.6f });
        b.set (P::PhosTune, 0.f);
        // dark and opened by the envelope (most), a mid cutoff, or open
        const int kind = b.weighted ({ 0.5f, 0.3f, 0.2f });
        if (kind == 0)
        {
            b.uni (P::PhosCutoff, 0.f, 0.06f);
            b.uni (P::PhosEnvMod, 0.3f, 0.95f);
        }
        else if (kind == 1)
        {
            b.uni (P::PhosCutoff, 0.06f, 0.35f);
            b.uni (P::PhosEnvMod, 0.1f, 0.7f);
        }
        else
        {
            b.uni (P::PhosCutoff, 0.35f, 0.75f);
            b.uni (P::PhosEnvMod, 0.f, 0.45f);
        }
        const int reso = b.weighted ({ 0.55f, 0.3f, 0.15f });
        if (reso == 0) b.uni (P::PhosReso, 0.85f, 1.f);
        else if (reso == 1) b.uni (P::PhosReso, 0.55f, 0.85f);
        else b.uni (P::PhosReso, 0.25f, 0.55f);
        b.uni (P::PhosDecay, 0.f, 1.f);
        b.uni (P::PhosAccent, 0.5f, 1.f);
        if (b.chance (0.7f)) b.set (P::PhosSlide, 0.5f);
        else b.uni (P::PhosSlide, 0.2f, 0.6f);
        if (b.chance (0.8f)) b.set (P::PhosSweep, 1.f);
        else b.uni (P::PhosSweep, 0.45f, 1.f);
        if (b.chance (0.92f)) b.set (P::PhosEnvAttack, 0.f);
        else b.uni (P::PhosEnvAttack, 0.f, 0.3f);
        if (b.chance (0.85f)) b.set (P::PhosAccDecay, 0.f);
        else b.uni (P::PhosAccDecay, 0.f, 0.4f);
        if (b.chance (0.6f)) b.set (P::PhosAccVolume, 1.f);
        else b.uni (P::PhosAccVolume, 0.5f, 1.f);
        b.set (P::PhosVibSpeed, 0.76f);
        b.set (P::PhosVibDepth, 0.264f);
        b.on (P::PhosResoCouple, b.chance (0.8f));
        b.on (P::PhosAccCouple, b.chance (0.55f));

        // mostly the ideal unit, now and then one of the original's other calibrations
        const auto& c = phosCalibrations()[(size_t) b.weighted ({ 6.f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f })];
        b.set (P::PhosTm3, c.tm3);
        b.set (P::PhosC21, c.c21);
        b.set (P::PhosAccFEnv, c.fenv);
        b.set (P::PhosAccAEnv, c.aenv);
        b.set (P::PhosC13, c.c13);
        b.set (P::PhosTm5, c.tm5);
        b.set (P::PhosPwShift, c.pw);
        b.on (P::PhosClicks, c.clicks);
        b.set (P::PhosNoise, c.noise);
    }

    void distortion (B& b)
    {
        b.on (P::PhosDistOn, b.chance (0.97f));
        // the models in the factory sounds' proportions
        b.pick (P::PhosDistModel, { 6, 5, 1, 3, 14, 12, 13, 9, 8, 2, 0, 16, 4, 7, 10, 11, 15, 17, 18 },
                { 39.f, 15.f, 13.f, 12.f, 11.f, 9.f, 9.f, 8.f, 8.f, 7.f, 7.f, 7.f, 5.f, 4.f, 3.f, 3.f, 3.f, 4.f, 4.f });
        b.uni (P::PhosDistDrive, 0.05f, 0.68f);
        if (b.chance (0.55f)) b.set (P::PhosDistColor, 0.f);
        else b.uni (P::PhosDistColor, -0.1f, 0.55f);
        if (b.chance (0.55f)) b.set (P::PhosDistDyn, 1.f);
        else b.uni (P::PhosDistDyn, 0.38f, 1.f);
        b.pick (P::PhosDistOrder, { 0, 1 }, { 0.58f, 0.42f });
    }

    void effects (B& b)
    {
        // the chain: distortion first (the 303's own), the EQ after it,
        // our effects in their usual order, the limiter last
        b.set (P::PhosEqSlot, 1.f);
        b.set (P::PhosLimSlot, 10.f);

        b.on (P::PhosEqOn, b.chance (0.94f));
        b.uni (P::PhosEqLowGain, -5.f, 8.6f);
        b.uni (P::PhosEqLowFreq, 74.f, 167.f);
        b.uni (P::PhosEqLowWidth, 1.06f, 2.17f);
        b.uni (P::PhosEqMidLowGain, -11.f, -1.f);
        b.uni (P::PhosEqMidLowFreq, 250.f, 810.f);
        b.uni (P::PhosEqMidLowWidth, 0.9f, 2.f);
        b.uni (P::PhosEqMidHighGain, -4.6f, 11.3f);
        b.uni (P::PhosEqMidHighFreq, 1000.f, 6900.f);
        b.uni (P::PhosEqMidHighWidth, 1.33f, 3.37f);
        b.uni (P::PhosEqHighGain, 0.f, 11.7f);
        b.uni (P::PhosEqHighFreq, 5000.f, 14500.f);
        b.uni (P::PhosEqHighWidth, 1.66f, 3.53f);

        b.on (P::PhosRevOn, b.chance (0.89f));
        b.uni (P::PhosRevPre, 0.f, 270.f);
        b.uni (P::PhosRevESize, 1.1f, 8.4f);
        b.uni (P::PhosRevEDiff, 0.f, 1.f);
        b.uni (P::PhosRevLSize, 5.1f, 28.f);
        b.uni (P::PhosRevLDiff, 0.09f, 1.f);
        b.uni (P::PhosRevFb, 0.f, 0.49f);
        b.uni (P::PhosRevBlend, 0.f, 0.81f);
        b.uni (P::PhosRevMod, 0.f, 0.99f);
        b.uni (P::PhosRevEBright, 0.19f, 0.92f);
        b.uni (P::PhosRevLBright, 0.3f, 1.f);
        b.uni (P::PhosRevMix, 0.064f, 0.32f);

        b.on (P::PhosDlyOn, b.chance (0.42f));
        b.on (P::PhosDlySync, b.chance (0.9f));
        b.uni (P::PhosDlyTime, 0.15f, 0.5f);
        // steps of a 16th, left / right, as the scenes set them
        const int pair = b.weighted ({ 13.f, 12.f, 18.f, 6.f, 5.f, 5.f, 4.f });
        constexpr int left[] = { 3, 2, 3, 2, 4, 6, 5 }, right[] = { 3, 3, 3, 2, 4, 6, 5 };
        b.set (P::PhosDlyLNum, (float) left[pair]);
        b.set (P::PhosDlyRNum, (float) right[pair]);
        b.set (P::PhosDlyLDen, 16.f);
        b.set (P::PhosDlyRDen, 16.f);
        b.set (P::PhosDlyLScale, 0.f);
        b.set (P::PhosDlyRScale, 0.f);
        b.pick (P::PhosDlyFilter, { 2, 3, 1 }, { 0.6f, 0.25f, 0.15f });
        b.uni (P::PhosDlyCut, 2800.f, 7900.f);
        b.uni (P::PhosDlyReso, 0.f, 0.88f);
        b.uni (P::PhosDlySpread, -0.07f, 0.09f);
        b.uni (P::PhosDlyFb, 0.f, 0.43f);
        b.uni (P::PhosDlyMix, 0.076f, 0.39f);

        b.on (P::PhosChoOn, b.chance (0.24f));
        b.uni (P::PhosChoOffset, 0.f, 0.51f);
        b.uni (P::PhosChoDepth, 0.07f, 0.66f);
        b.uni (P::PhosChoRate, 0.26f, 1.f);
        b.uni (P::PhosChoSpatial, 0.f, 1.f);
        b.among (P::PhosChoFat, { 0.f, 1.f, 1.f });
        b.uni (P::PhosChoHp, 0.2f, 1.f);
        b.set (P::PhosChoOut, 0.f);
        b.uni (P::PhosChoMix, 0.116f, 0.36f);

        b.on (P::PhosLimOn, b.chance (0.91f));
        b.uni (P::PhosLimKnee, 0.89f, 1.f);
        b.among (P::PhosLimRatio, { 2.54f, 40.f });
        b.uni (P::PhosLimAttack, 0.1f, 4.1f);
        b.uni (P::PhosLimRelease, 25.f, 142.f);
        b.uni (P::PhosLimThresh, -24.f, -10.f);
        b.uni (P::PhosLimGain, 0.f, 6.f);
        b.on (P::PhosLimClip, b.chance (0.93f));
    }
} // namespace

void phoscyonRecipe (RecipeBuilder& b)
{
    synth (b);
    distortion (b);
    effects (b);
    b.macros ({ ModDst::PhosCutoff, ModDst::PhosEnvMod, ModDst::PhosReso, ModDst::PhosDecay, ModDst::PhosDistDrive,
                ModDst::PhosAccent, ModDst::PhosDelayMix, ModDst::PhosReverbMix });
}

} // namespace psy
