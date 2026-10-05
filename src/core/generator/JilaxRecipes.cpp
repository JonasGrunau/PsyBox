// Jilax: psytrance synth one-shots.
//
// Fitted to 500 reference shots (one or two beats at 140-145 BPM, stereo,
// normalised) by rendering hundreds of generated shots the same way and
// comparing the two sets: level over the shot, brightness and its motion,
// octave bands, harmonics, width, crest. What the references share:
//  - The root sits in the bass register (octave 1 in two of three shots, 41-62
//    Hz), but the fundamental is gone: a steep high-pass around 150-200 Hz,
//    fixed whatever the key, leaves the 3rd harmonic 20 dB down and a plateau
//    from the 5th to the 10th. Above it the slope of a plain saw. That dense
//    comb of harmonics is the "big" sound. A sixth of the shots sit an octave
//    higher, a sixth higher still (leads, FM tones, the odd minor chord).
//  - Very wide: the side is about as loud as the mid above 300 Hz.
//  - Dense: about 7.5 dB crest where they are loudest (clipped, compressed).
//  - One beat (about two in three) or two, cut at the end, often fading over
//    the last tenth. The level slides about 5 dB over a beat in the typical
//    shot; a third fall 15-25 dB, a fifth swell, a fifth hit for a beat and
//    ring out through the next, a few are trance-gated.
//  - Half close their filter over the hit (the brightness halves in about
//    85 ms), the rest stay bright, open, or rise and fall. One in ten moves
//    in pitch (zaps, falls, rises), a few are flanged or phased.
// Every shot is played two octaves (sometimes one, or none) below the key, so
// E3 plays the E1 root the reference "E" shots have.

#include "Generator.h"

namespace psy
{

namespace
{
    using B = RecipeBuilder;
    using S = ModSrc;
    using D = ModDst;

    constexpr float kSaw = 2.f, kSquare = 3.f;

    // syncDiv indices
    enum Div { D4_1, D2_1, D1_1, D1_2, D1_2D, D1_2T, D1_4, D1_4D, D1_4T, D1_8, D1_8D, D1_8T,
               D1_16, D1_16D, D1_16T, D1_32, D1_32T, D1_64 };
    enum Bars { B1_4, B1_2, B1 };

    int ft (FilterType t) { return (int) t; }

    // ---- the body -----------------------------------------------------------

    // One unison oscillator, spread like a Serum patch: several voices, wide,
    // but barely detuned. The references' harmonics are about 20 cents wide
    // at -6 dB, and their top stays a clean comb of partials (a wider detune
    // fills the gaps between them with a hiss); the width comes from the
    // voices' phases and pans, not from a smear of pitches.
    void unison (B& b, bool isB, float nLo, float nHi)
    {
        b.uni (isB ? P::OscBUnison : P::OscAUnison, nLo, nHi);
        const bool smeared = b.chance (0.15f);
        b.uni (isB ? P::OscBDetune : P::OscADetune, smeared ? 0.06f : 0.01f, smeared ? 0.18f : 0.06f);
        b.uni (isB ? P::OscBBlend : P::OscABlend, 0.7f, 1.f);
        b.uni (isB ? P::OscBUniWidth : P::OscAUniWidth, 0.9f, 1.f);
        b.pick (isB ? P::OscBUniMode : P::OscAUniMode, { (int) UniMode::Linear, (int) UniMode::Super }, { 0.5f, 0.5f });
    }

    void osc (B& b, bool isB, OscType type)
    {
        b.on (isB ? P::OscBOn : P::OscAOn, true);
        b.set (isB ? P::OscBType : P::OscAType, (float) type);
        b.set (isB ? P::OscBRetrig : P::OscARetrig, 1.f);
    }

    enum class Body { Saw, Supersaw, Table, Square, Fm, Chord };

    // The oscillators; `octave` gets the octave the root is played in,
    // relative to the key.
    Body body (B& b, int& octave)
    {
        const auto kind = (Body) b.weighted ({ 0.47f, 0.14f, 0.17f, 0.07f, 0.07f, 0.08f });
        switch (kind)
        {
            case Body::Saw:
                osc (b, false, OscType::Analog);
                b.set (P::OscAShape, kSaw);
                unison (b, false, 3.f, 8.f);
                break;
            case Body::Supersaw:
                osc (b, false, OscType::Supersaw);
                b.uni (P::OscADetune, 0.08f, 0.3f);
                break;
            case Body::Table:
                osc (b, false, OscType::Wavetable);
                b.pick (P::OscAWt, { 0, 1, 4, 5, 7, 2 }, { 0.25f, 0.15f, 0.2f, 0.2f, 0.1f, 0.1f });
                b.uni (P::OscAWtPos, 0.1f, 0.9f);
                unison (b, false, 4.f, 9.f);
                break;
            case Body::Square:
                osc (b, false, OscType::Analog);
                b.set (P::OscAShape, kSquare);
                b.uni (P::OscAPw, 0.25f, 0.5f);
                unison (b, false, 4.f, 9.f);
                break;
            case Body::Fm:
                osc (b, false, OscType::FM);
                b.among (P::OscAFmRatio, { 1.f, 2.f, 3.f, 0.5f, 4.f });
                b.uni (P::OscAFm, 1.f, 5.f);
                b.uni (P::OscAFmFb, 0.f, 0.3f);
                unison (b, false, 1.f, 4.f);
                break;
            case Body::Chord:
                // root, fifth and octave from the stack, the third from osc B
                osc (b, false, OscType::Analog);
                b.set (P::OscAShape, kSaw);
                unison (b, false, 5.f, 9.f);
                b.set (P::OscAUniStack, (float) UniStack::OctFifth);
                break;
        }
        b.set (P::OscALevel, 0.8f);

        // the register: mostly the bass octave, FM tones and chords higher up
        if (kind == Body::Fm) octave = b.pick (P::Octave, { -2, -1, 0 }, { 0.25f, 0.35f, 0.4f });
        else if (kind == Body::Chord) octave = b.pick (P::Octave, { -1, 0 }, { 0.4f, 0.6f });
        else octave = b.pick (P::Octave, { -2, -1, 0 }, { 0.78f, 0.17f, 0.05f });

        // a second layer: an octave or a fifth up, a detuned double, an octave down
        if (kind == Body::Chord)
        {
            osc (b, true, OscType::Analog);
            b.set (P::OscBShape, kSaw);
            b.among (P::OscBCoarse, { 3.f, 4.f, 3.f });   // minor twice as often
            unison (b, true, 3.f, 7.f);
            b.uni (P::OscBLevel, 0.55f, 0.75f);
        }
        else if (kind != Body::Fm && b.chance (0.6f))
        {
            osc (b, true, b.chance (0.75f) ? OscType::Analog : OscType::Wavetable);
            b.set (P::OscBShape, b.chance (0.8f) ? kSaw : kSquare);
            b.pick (P::OscBWt, { 0, 4, 5 });
            b.uni (P::OscBWtPos, 0.1f, 0.8f);
            b.among (P::OscBCoarse, { 12.f, 12.f, 7.f, 0.f, -12.f, 19.f });
            b.uni (P::OscBFine, -8.f, 8.f);
            unison (b, true, 3.f, 9.f);
            b.uni (P::OscBLevel, 0.3f, 0.7f);
        }
        if (b.chance (0.2f))
        {
            b.pick (P::NoiseType, { 0, 1 });
            b.uni (P::NoiseLevel, 0.03f, 0.12f);
        }
        return kind;
    }

    // ---- length and level ---------------------------------------------------

    // One beat or two (1/4 or 1/2 bar), cut at the end, and a one-shot: a tap
    // plays the whole length, like the sample it stands in for. Time mode
    // gets the same length at 142 BPM, the tempo most references were made at.
    void length (B& b, int bars)
    {
        b.set (P::EnergyBars, (float) bars);
        b.set (P::EnergySec, energyBarsValue (bars) * 4.f * 60.f / 142.f);
        b.set (P::EnergyFade, 0.f);
        b.on (P::EnergyCut, true);
        b.on (P::EnergyOneShot, true);
    }

    enum class Amp { Hold, Decay, Swell, Tail, Gate };

    Amp amp (B& b)
    {
        const auto shape = (Amp) b.weighted ({ 0.24f, 0.32f, 0.18f, 0.2f, 0.06f });
        // most shots are one beat; a held two-beat shot still fades through its second beat
        if (shape == Amp::Tail) length (b, B1_4);
        else length (b, b.weighted ({ 0.82f, 0.16f, 0.02f }));

        switch (shape)
        {
            case Amp::Hold:
            case Amp::Gate:
                // a slow slide from full level to about -6 dB over the beat
                b.uni (P::AenvA, 0.f, 2.f);
                b.uni (P::AenvD, 250.f, 1000.f);
                b.uni (P::AenvS, 0.2f, 0.55f);
                b.uni (P::AenvR, 10.f, 40.f);
                break;
            case Amp::Decay:
                // falls 15-25 dB over the beat
                b.uni (P::AenvA, 0.f, 2.f);
                b.uni (P::AenvD, 200.f, 550.f);
                b.uni (P::AenvS, 0.f, 0.08f);
                b.uni (P::AenvR, 20.f, 80.f);
                break;
            case Amp::Swell:
                b.uni (P::AenvA, 40.f, 250.f);
                b.uni (P::AenvCurve, 0.3f, 0.8f);
                b.uni (P::AenvD, 300.f, 1200.f);
                b.uni (P::AenvS, 0.5f, 1.f);
                b.uni (P::AenvR, 10.f, 40.f);
                break;
            case Amp::Tail:
                // the hit holds for its beat, then rings out through the next
                b.uni (P::AenvA, 0.f, 2.f);
                b.uni (P::AenvD, 300.f, 1500.f);
                b.uni (P::AenvS, 0.45f, 0.9f);
                b.uni (P::AenvR, 60.f, 250.f);
                break;
        }
        if (shape == Amp::Gate)
        {
            // a trance gate: a hard square on the grid, retriggered by the note
            b.lfoSync (0, LfoShape::Square, { D1_16, D1_16, D1_32, D1_8, D1_16D }, true);
            b.route (S::Lfo1, D::Amp, 0.85f, 1.f);
        }
        return shape;
    }

    // ---- filters ------------------------------------------------------------
    //
    // Filter 1 is the low-pass that moves (with a little drive in front of
    // it), filter 2 the high-pass.

    // The high-pass that takes the fundamental out (fixed in Hz, steep).
    // Returns its cutoff, or 0 without one.
    float highPass (B& b, int octave)
    {
        const float chance = octave == -2 ? 0.97f : (octave == -1 ? 0.85f : 0.5f);
        if (! b.chance (chance)) return 0.f;
        b.pick (P::F2Type, { ft (FilterType::HP12), ft (FilterType::HP18), ft (FilterType::HP24) }, { 0.05f, 0.3f, 0.65f });
        const float hz = octave == 0 ? b.uni (P::F2Cutoff, 80.f, 200.f) : b.uni (P::F2Cutoff, 190.f, 380.f);
        b.uni (P::F2Reso, 0.15f, 0.6f);   // a peak at the corner: the energy bunches into a few harmonics
        b.set (P::F2Key, 0.f);
        b.set (P::F2Env, 0.f);
        return hz;
    }

    enum class Motion { Static, Close, Open, RiseFall };

    // A decaying shot never opens over its length: the two cancel out.
    Motion lowPass (B& b, Amp shape)
    {
        const auto motion = (Motion) b.weighted ({ 0.22f, 0.5f, shape == Amp::Decay ? 0.f : 0.12f, 0.16f });
        b.pick (P::F1Type, { ft (FilterType::LP12), ft (FilterType::LP18), ft (FilterType::LP24) }, { 0.5f, 0.25f, 0.25f });
        b.uni (P::F1Drive, 0.f, 0.35f);
        switch (motion)
        {
            case Motion::Static:
                // a gentle slope from the upper mids, the same on every key:
                // the top keeps sounding through the shot
                b.set (P::F1Type, (float) ft (FilterType::LP12));
                b.set (P::F1Key, 0.f);
                b.uni (P::F1Cutoff, 3500.f, 9000.f);
                b.uni (P::F1Reso, 0.f, 0.2f);
                b.set (P::F1Env, 0.f);
                break;
            case Motion::Close:
                // the brightness halves (in octaves) in about 85 ms, 1.5 octaves down in all;
                // it closes to a few kHz, not to the bottom: a gentle slope
                // that ends there keeps the top sounding through the shot
                // (the references keep 60 % of their top after the first
                // 100 ms) while the brightness still drops 1-2 octaves
                b.pick (P::F1Type, { ft (FilterType::LP12), ft (FilterType::LP18) }, { 0.7f, 0.3f });
                b.uni (P::F1Key, 0.f, 0.3f);
                b.uni (P::F1Cutoff, 400.f, 2000.f);
                b.uni (P::F1Reso, 0.05f, 0.4f);
                b.uni (P::F1Env, 0.5f, 0.85f);
                b.set (P::FenvA, 0.f);
                b.uni (P::FenvD, 300.f, 1500.f);
                b.uni (P::FenvS, 0.f, 0.2f);
                b.uni (P::FenvR, 50.f, 200.f);
                break;
            case Motion::Open:
                b.uni (P::F1Key, 0.f, 0.3f);
                b.uni (P::F1Cutoff, 250.f, 800.f);
                b.uni (P::F1Reso, 0.1f, 0.45f);
                b.set (P::F1Env, 0.f);
                b.route (S::Sweep, D::F1Cutoff, 0.5f, 0.85f);
                b.uni (P::EnergyCurve, -0.3f, 0.5f);
                break;
            case Motion::RiseFall:
                // dark at the start, brightest about halfway, dark again
                b.uni (P::F1Key, 0.f, 0.3f);
                b.uni (P::F1Cutoff, 200.f, 600.f);
                b.uni (P::F1Reso, 0.1f, 0.5f);
                b.uni (P::F1Env, 0.6f, 0.9f);
                b.uni (P::FenvA, 150.f, 350.f);
                b.uni (P::FenvD, 120.f, 350.f);
                b.uni (P::FenvS, 0.f, 0.15f);
                b.uni (P::FenvR, 50.f, 200.f);
                break;
        }
        return motion;
    }

    // ---- pitch --------------------------------------------------------------

    // Returns true when it hangs the pitch on the sweep.
    bool pitch (B& b)
    {
        const int kind = b.weighted ({ 0.9f, 0.04f, 0.025f, 0.015f, 0.01f, 0.01f });
        switch (kind)
        {
            case 1:   // a zap into the note
                b.set (P::PenvDest, 0.f);
                b.set (P::PenvAttack, 0.f);
                b.uni (P::PenvAmt, 24.f, 48.f);
                b.uni (P::PenvDecay, 30.f, 150.f);
                b.uni (P::PenvCurve, 0.3f, 0.7f);
                break;
            case 2:   // a long fall over the shot
                b.route (S::Sweep, D::PitchAll, -1.f, -0.5f);
                b.uni (P::EnergyCurve, -0.2f, 0.6f);
                break;
            case 3:   // a rise over the shot
                b.route (S::Sweep, D::PitchAll, 0.5f, 1.f);
                b.uni (P::EnergyCurve, -0.2f, 0.6f);
                break;
            case 4:   // a bump up and back
                b.set (P::PenvDest, 0.f);
                b.uni (P::PenvAmt, 7.f, 24.f);
                b.uni (P::PenvAttack, 20.f, 80.f);
                b.uni (P::PenvDecay, 40.f, 150.f);
                b.uni (P::PenvCurve, 0.4f, 0.8f);
                break;
            case 5:   // a wobble in pitch on the grid
                b.lfoSync (1, b.chance (0.5f) ? LfoShape::Triangle : LfoShape::Sine, { D1_8, D1_16, D1_8T }, true);
                b.route (S::Lfo2, D::PitchAll, 0.1f, 0.5f);
                break;
            default:
                break;
        }
        return kind == 2 || kind == 3;
    }

    // The references fade over the last tenth or so of their length (about
    // -4 dB at 80 %, -12 dB at the very end): the level follows the sweep
    // down, late. Only when nothing else hangs on the sweep.
    void endFade (B& b, Amp shape, bool sweepUsed)
    {
        if (sweepUsed || shape == Amp::Tail || ! b.chance (0.8f)) return;
        b.uni (P::EnergyFade, -0.95f, -0.7f);
        b.uni (P::EnergyCurve, 0.6f, 0.9f);
    }

    // ---- effects ------------------------------------------------------------

    // OTT-style multiband: down and up on every band, the highs lifted.
    void ott (B& b)
    {
        b.on (P::CompOn, true);
        b.set (P::CompMode, (float) CompMode::Multiband);
        b.uni (P::CompAttack, 2.f, 12.f);
        b.uni (P::CompRelease, 60.f, 220.f);
        b.uni (P::CompMix, 0.5f, 0.9f);
        b.uni (P::CompDown, 0.5f, 0.85f);
        b.uni (P::CompUp, 0.2f, 0.55f);   // enough to fill the body, not to flatten the decays
        b.uni (P::MbXLow, 120.f, 250.f);
        b.uni (P::MbXHigh, 4500.f, 8000.f);   // the high band is the air, not the upper mids
        b.uni (P::MbLowDown, -18.f, -8.f);
        b.uni (P::MbMidDown, -24.f, -14.f);
        b.uni (P::MbHighDown, -28.f, -18.f);
        b.uni (P::MbLowUp, -44.f, -34.f);
        b.uni (P::MbMidUp, -44.f, -34.f);
        b.uni (P::MbHighUp, -38.f, -28.f);   // lifts the air back up as a filter closes
        b.uni (P::MbLowGain, -2.f, 2.f);
        b.uni (P::MbMidGain, 0.f, 3.f);
        b.uni (P::MbHighGain, 1.f, 6.f);
        b.uni (P::CompOut, 0.f, 3.f);
    }

    void glue (B& b)
    {
        b.on (P::CompOn, true);
        b.set (P::CompMode, (float) CompMode::Normal);
        b.uni (P::CompThresh, -24.f, -14.f);
        b.uni (P::CompRatio, 4.f, 10.f);
        b.uni (P::CompKnee, 6.f, 12.f);
        b.uni (P::CompAttack, 3.f, 15.f);
        b.uni (P::CompRelease, 60.f, 200.f);
        b.set (P::CompMix, 1.f);
        b.uni (P::CompOut, 3.f, 8.f);
    }

    void effects (B& b, Body kind, Amp shape, float highPassHz)
    {
        // The references are clipped hard (about 7.5 dB crest where they are
        // loudest, against 12-15 dB for a high-passed saw stack): a clipper
        // at the very end of the chain, after the compressor and the tails.
        // Auto Level makes it bite the same on every patch and lets a decay
        // fall through it. Otherwise the odd shot gets its grit early.
        if (b.chance (0.9f))
        {
            b.on (P::DistOn, true);
            b.pick (P::DistType, { (int) DistType::Hard, (int) DistType::Soft, (int) DistType::Tube }, { 0.75f, 0.15f, 0.1f });
            b.on (P::DistAuto, true);
            b.uni (P::DistDrive, 19.f, 27.f);
            b.uni (P::DistMix, 0.85f, 1.f);
            b.uni (P::DistTone, 19000.f, 20000.f);
            // Clipping brings a high-passed root back as a difference tone.
            // A little of it belongs (the references keep it about 35 dB
            // down, and cutting it all makes the wave spiky again), so the
            // clipper's low cut sits well under the voice's high-pass.
            if (highPassHz > 0.f) b.uni (P::DistLowCut, 0.35f * highPassHz, 0.5f * highPassHz);
            const FxChain last { (int) Fx::Crush, (int) Fx::Shift, (int) Fx::Phaser, (int) Fx::Chorus, (int) Fx::Compressor,
                                 (int) Fx::Delay, (int) Fx::Reverb, (int) Fx::Width, (int) Fx::Distortion };
            b.set (P::FxOrder, (float) fxOrderCode (last));
        }
        else if (b.chance (0.4f))
        {
            b.on (P::DistOn, true);
            b.pick (P::DistType, { (int) DistType::Soft, (int) DistType::Tube }, { 0.5f, 0.5f });
            b.uni (P::DistDrive, 6.f, 16.f);
            b.uni (P::DistMix, 0.3f, 0.7f);
            b.uni (P::DistTone, 6000.f, 16000.f);
        }
        if (b.chance (0.08f))
        {
            b.on (P::PhaserOn, true);
            b.uni (P::PhaserRate, 0.2f, 2.f);
            b.uni (P::PhaserDepth, 0.5f, 0.9f);
            b.uni (P::PhaserFb, 0.3f, 0.8f);
            b.uni (P::PhaserCenter, 400.f, 2500.f);
            b.uni (P::PhaserMix, 0.35f, 0.6f);
        }
        if (b.chance (kind == Body::Supersaw ? 0.7f : 0.4f))
        {
            b.on (P::ChorusOn, true);
            b.pick (P::ChorusMode, { 0, 1 }, { 0.7f, 0.3f });
            b.uni (P::ChorusRate, 0.2f, 1.2f);
            b.uni (P::ChorusDepth, 0.3f, 0.8f);
            b.uni (P::ChorusFb, -0.3f, 0.5f);
            b.uni (P::ChorusMix, 0.25f, 0.5f);
        }
        if (b.chance (0.9f))
        {
            if (b.chance (0.78f)) ott (b);
            else glue (b);
        }
        if (b.chance (0.2f))
        {
            b.on (P::DelayOn, true);
            b.set (P::DelaySync, 1.f);
            b.pick (P::DelayDiv, { D1_8, D1_16, D1_8D });
            b.uni (P::DelayFb, 0.2f, 0.4f);
            b.uni (P::DelayMix, 0.08f, 0.2f);
            b.on (P::DelayPing, b.chance (0.7f));
            b.uni (P::DelayLowCut, 200.f, 500.f);
            b.uni (P::DelayHighCut, 4000.f, 10000.f);
        }
        if (b.chance (shape == Amp::Tail ? 0.85f : 0.5f))
        {
            b.on (P::ReverbOn, true);
            b.uni (P::ReverbDecay, shape == Amp::Tail ? 0.6f : 0.8f, shape == Amp::Tail ? 1.5f : 2.5f);
            b.uni (P::ReverbMix, shape == Amp::Tail ? 0.15f : 0.08f, shape == Amp::Tail ? 0.35f : 0.25f);
            b.uni (P::ReverbSize, 0.4f, 0.9f);
            b.uni (P::ReverbDamp, 0.2f, 0.6f);
            b.uni (P::ReverbPre, 0.f, 30.f);
        }
        // wide highs over narrower lows: the side climbs from about -5 dB of
        // the mid under 160 Hz to level with it above 2.5 kHz
        if (b.chance (kind == Body::Supersaw ? 0.9f : 0.75f))
        {
            b.on (P::WidthOn, true);
            b.uni (P::WidthStereo, 0.5f, 1.f);
            b.uni (P::WidthTime, 6.f, 18.f);
            b.uni (P::WidthMonoHz, 100.f, 220.f);
        }
    }
} // namespace

void jilaxRecipe (RecipeBuilder& b)
{
    b.set (P::VoiceMode, (float) VoiceMode::Poly);
    b.uni (P::PolyVoices, 4.f, 8.f);
    b.set (P::Glide, 0.f);
    b.set (P::VelSens, 0.2f);
    b.set (P::Accent, 0.f);

    int octave = -2;
    const Body kind = body (b, octave);
    const Amp shape = amp (b);
    const float highPassHz = highPass (b, octave);
    const Motion motion = lowPass (b, shape);
    const bool pitchSweep = pitch (b);
    endFade (b, shape, pitchSweep || motion == Motion::Open);
    effects (b, kind, shape, highPassHz);
    b.macros ({ D::F1Cutoff, D::F2Cutoff, D::F1Drive, D::F1Reso, D::DetuneA, D::ReverbMix, D::DelayMix, D::ChorusMix });
}

} // namespace psy
