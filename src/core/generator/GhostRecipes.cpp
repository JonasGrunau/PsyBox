// Ghost Rider's stabs and bass.
//
// Fitted to 28 of Ghost Rider's tracks (2014-2025, mostly 138 BPM), split
// into stems, by measuring the stab and the bass in every main section and
// rendering generated patches the same way.
//
// The stab: a chord on the offbeat that holds until the next kick and stops
// there. In most sections it starts on the "and", reaches full level in
// about 10 ms, sags 2-3 dB over the half beat and is cut at the beat; some
// sections hold it for a beat and a half, push it to the last sixteenth or
// roll it on the sixteenths. The chord is a stack of fifths (root, fifth,
// octave, twelfth; often with the fifth underneath), its lowest note around
// G3-A#3. Nothing under ~180 Hz, most energy at 300-800 Hz, then 6-8 dB
// less per octave above 1.4 kHz. Wide (the side as loud as the mid above
// 350 Hz), but the harmonics stay distinct: a few moderately detuned voices,
// not a smeared supersaw.
//
// The bass: E1-G#1, rolling sixteenths with the first one after the kick
// left out or ducked. No pitch knock. A round saw whose fundamental sits
// about level with the second harmonic (the kick owns the sub), falling
// about 4 dB a harmonic above the fourth; the top eases down only ~3 dB
// over the first 60 ms of a note. Mono.

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


    // syncDiv indices


    void osc (B& b, bool isB, float shape)
    {
        b.on (isB ? P::OscBOn : P::OscAOn, true);
        b.set (isB ? P::OscBType : P::OscAType, (float) OscType::Analog);
        b.set (isB ? P::OscBShape : P::OscAShape, shape);
    }

    // A few voices, moderately detuned, spread wide, phases free-running.
    void spread (B& b, bool isB, float nLo, float nHi, float detLo, float detHi)
    {
        b.uni (isB ? P::OscBUnison : P::OscAUnison, nLo, nHi);
        b.uni (isB ? P::OscBDetune : P::OscADetune, detLo, detHi);
        b.uni (isB ? P::OscBBlend : P::OscABlend, 0.6f, 0.9f);
        b.uni (isB ? P::OscBUniWidth : P::OscAUniWidth, 0.85f, 1.f);
        b.pick (isB ? P::OscBUniMode : P::OscAUniMode, { (int) UniMode::Linear, (int) UniMode::Super }, { 0.5f, 0.5f });
        b.set (isB ? P::OscBRetrig : P::OscARetrig, 0.f);
    }
} // namespace

void ghostStabRecipe (RecipeBuilder& b)
{
    b.set (P::VoiceMode, (float) VoiceMode::Poly);
    b.uni (P::PolyVoices, 6.f, 8.f);
    b.set (P::Glide, 0.f);
    b.pick (P::Octave, { 0, -1 }, { 0.85f, 0.15f });
    b.uni (P::VelSens, 0.2f, 0.4f);

    // the stack of fifths: root, fifth and octave from Osc A's unison stack
    osc (b, false, kSaw);
    spread (b, false, 5.f, 9.f, 0.12f, 0.25f);
    b.pick (P::OscAUniStack, { (int) UniStack::OctFifth, (int) UniStack::Oct, (int) UniStack::Off }, { 0.7f, 0.15f, 0.15f });
    b.set (P::OscALevel, 0.8f);
    // Osc B completes the chord: the twelfth (root, fifth, octave, twelfth:
    // his most common shape), the fifth underneath (the next most common),
    // the fifth doubled or the octave
    osc (b, true, kSaw);
    b.among (P::OscBCoarse, { 19.f, -5.f, 7.f, 12.f });
    b.uni (P::OscBFine, -6.f, 6.f);
    spread (b, true, 3.f, 5.f, 0.1f, 0.2f);
    b.uni (P::OscBLevel, 0.35f, 0.55f);

    // Nothing under ~200 Hz. Above 1.4 kHz the references fall a steady
    // 6-8 dB an octave (a saw alone falls 3): a one-pole low-pass around
    // 1.2-2.5 kHz in most, or a 12 dB one higher up, where its slope is
    // still gentle; a little brighter on the hit.
    // The high-pass follows the key (from C4), so around G#3, where his
    // stabs sit, it lands at 240-285 Hz: steep, so the root is thinned and
    // the fifth above it kept, and a stab played lower keeps its shape.
    b.set (P::F2Type, (float) ft (FilterType::HP24));
    b.uni (P::F2Cutoff, 300.f, 360.f);
    b.uni (P::F2Reso, 0.f, 0.15f);
    b.set (P::F2Key, 1.f);
    const int lp = b.pick (P::F1Type, { ft (FilterType::LP6), ft (FilterType::LP12), ft (FilterType::LadderLP12) }, { 0.6f, 0.25f, 0.15f });
    if (lp == ft (FilterType::LP6)) b.uni (P::F1Cutoff, 1200.f, 2500.f);
    else b.uni (P::F1Cutoff, 2500.f, 4000.f);
    b.uni (P::F1Reso, 0.05f, 0.2f);
    b.uni (P::F1Key, 0.2f, 0.5f);
    b.uni (P::F1Env, 0.f, 0.12f);
    b.set (P::FenvA, 0.f);
    b.uni (P::FenvD, 200.f, 500.f);
    b.uni (P::FenvS, 0.f, 0.2f);
    b.uni (P::FenvR, 30.f, 80.f);

    // up at once, sagging 1-3 dB over the half beat, cut short at the note's end
    b.uni (P::AenvA, 0.5f, 3.f);
    b.uni (P::AenvD, 300.f, 600.f);
    b.uni (P::AenvS, 0.75f, 0.9f);
    b.uni (P::AenvR, 15.f, 40.f);

    b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.4f);

    fxReverb (b, 0.9f, 1.2f, 3.f, 0.15f, 0.3f, 0.5f, 0.9f, 0.2f, 0.5f, 10.f, 40.f);
    fxDelay (b, 0.7f, { D1_8D, D1_4, D1_8 }, 0.2f, 0.4f, 0.12f, 0.25f, 250.f, 600.f, 4000.f, 9000.f);
    if (b.chance (0.3f))
    {
        b.on (P::ChorusOn, true);
        b.uni (P::ChorusRate, 0.1f, 0.8f);
        b.uni (P::ChorusDepth, 0.3f, 0.7f);
        b.uni (P::ChorusMix, 0.2f, 0.45f);
    }
    // As wide as the references (the side as loud as the mid above 300 Hz):
    // the unison's pans alone leave the side 6 dB down, the width stage
    // brings it up over a mono low end. No compressor: its attack would
    // carve the hit off the hold.
    if (b.chance (0.85f))
    {
        b.on (P::WidthOn, true);
        b.uni (P::WidthAmount, 1.4f, 1.7f);
        b.uni (P::WidthStereo, 0.4f, 0.65f);
        b.uni (P::WidthTime, 8.f, 18.f);
        b.uni (P::WidthMonoHz, 150.f, 250.f);
    }
    b.macros ({ D::FilterEnvAmt, D::DetuneA, D::WidthAmount, D::ReverbMix, D::DelayMix, D::ChorusMix, D::F1Reso });
}

void ghostBassRecipe (RecipeBuilder& b)
{
    b.set (P::VoiceMode, (float) VoiceMode::Mono);
    b.set (P::Glide, 0.f);
    b.set (P::GlideMode, 1.f);
    b.set (P::Octave, -1.f);
    b.set (P::VelSens, 0.15f);
    b.set (P::Accent, 0.f);

    osc (b, false, kSaw);
    b.set (P::OscALevel, 0.85f);

    // A round saw with a thin fundamental, fitted to the steady part of the
    // reference notes (within 0.7 dB over the first twelve harmonics): a
    // 12 dB low-pass at about 150 Hz, fixed whatever the key, and a 12 dB
    // high-pass at 55-78 Hz that takes the fundamental down to about the
    // second harmonic's level (the full mix agrees once the kick is gone),
    // leaving the sub to the kick.
    b.pick (P::F1Type, { ft (FilterType::LP12), ft (FilterType::LadderLP12) }, { 0.6f, 0.4f });
    b.uni (P::F1Cutoff, 130.f, 190.f);
    b.uni (P::F1Reso, 0.05f, 0.25f);
    b.uni (P::F1Key, 0.f, 0.15f);
    b.uni (P::F1Drive, 0.f, 0.2f);
    b.set (P::F2Type, (float) ft (FilterType::HP12));
    b.uni (P::F2Cutoff, 55.f, 78.f);
    b.uni (P::F2Reso, 0.f, 0.1f);
    b.set (P::F2Key, 0.f);
    b.set (P::FRouting, 0.f);
    // a slight flick: the top eases down about 3 dB over the first 60 ms
    b.uni (P::F1Env, 0.05f, 0.15f);
    b.set (P::FenvA, 0.f);
    b.uni (P::FenvD, 40.f, 90.f);
    b.set (P::FenvS, 0.f);
    b.uni (P::FenvR, 20.f, 40.f);

    b.uni (P::AenvA, 1.5f, 4.f);
    b.uni (P::AenvD, 100.f, 200.f);
    b.uni (P::AenvS, 0.9f, 1.f);
    b.uni (P::AenvR, 15.f, 40.f);

    b.route (S::Macro1, D::F1Cutoff, 0.2f, 0.35f);
    b.route (S::Macro2, D::FilterEnvAmt, 0.2f, 0.3f);

    fxCompBass (b);   // the basses' glue
    b.macros ({ D::F1Reso, D::F1Drive });
}

} // namespace psy
