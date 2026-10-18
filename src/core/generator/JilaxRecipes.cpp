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
//  - One beat or two, about half each. Nearly every two-beat shot is a
//    one-beat hit and its tail: the dry sound stops (10 dB down within a
//    tenth of a beat) and a reverb rings out through the second beat (about
//    80 dB a second). The one-beat shots are cut at the end, often fading
//    over the last tenth; the level slides about 5 dB over the beat in the
//    typical one, some fall 15-25 dB, some swell, a few are trance-gated.
//  - Half close their filter over the hit (the brightness halves in about
//    85 ms), the rest stay bright, open, or rise and fall. One in ten moves
//    in pitch (zaps, falls, rises), a few are flanged or phased.
// Every shot is played two octaves (sometimes one, or none) below the key, so
// E3 plays the E1 root the reference "E" shots have.
//
// Jilax's own Serum patches (250 of them, decoded from their .fxp files; the
// 126 one-shots among them also rendered through Serum and measured like the
// references) show how he builds such a sound, and the recipe follows them
// where the references agree:
//  - Saws or tables, often with all sixteen unison voices and random start
//    phases, at a spread that varies widely from patch to patch (see
//    unison()). Rendered, his shots have the references' tonal top. Often a
//    second stack: the same pitch on another table, an octave up, or an
//    octave and a fifth up (+19).
//  - A noise layer in about a third, half of it through the filter: mostly
//    Serum's default noise, a bright hiss over a 60 Hz hum.
//  - A Moog-style ladder low-pass with its cutoff low, opened by an LFO
//    synced to the beat and drawn as an envelope: a jump and a fall over an
//    eighth or a quarter, a rise and fall, a ramp. The amp envelope stays a
//    gate; the filter makes the hit.
//  - The same beat LFO on the wavetable position or the reverb (dry on the
//    hit, wet on the tail); a dimension expander for width.
//  - Then OTT-style multiband compression with its lows and highs turned
//    down (the references keep more air, so here the highs stay lifted), a
//    tube saturator, and an EQ cutting under 100-210 Hz at the end.

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
    enum Bars { B1_4, B1_2, B1 };

    // noise types (choices::noiseType)
    constexpr int kPinkNoise = 1, kTunedNoise = 4;


    // ---- the body -----------------------------------------------------------

    // How far apart the unison voices sit, the same for a patch's two
    // stacks. Our detune is about Serum's knob squared (on a 16-voice saw,
    // Serum's 0.2 sounds like 0.045 here, its 0.3 like 0.1). Jilax's
    // one-shot stacks spread widely: a third nearly in unison (thick without
    // hiss), most a few cents apart, a quarter wide. One gentle detune on
    // every stack (0.04-0.1) made the top hiss (spectral flatness 0.22 from 6
    // to 16 kHz, his rendered shots 0.12, the references 0.10); his spread
    // takes it to 0.19 and the clipper's products down with it.
    enum class Spread { Unison, Few, Wide };

    Spread spread (B& b) { return (Spread) b.weighted ({ 0.3f, 0.45f, 0.25f }); }

    // One unison oscillator. The width comes from the voices' pans.
    void unison (B& b, bool isB, float nLo, float nHi, Spread sp)
    {
        const P count = isB ? P::OscBUnison : P::OscAUnison;
        if (nHi >= 8.f && b.chance (0.6f)) b.set (count, 16.f);
        else b.uni (count, nLo, nHi);
        static constexpr float lo[] = { 0.002f, 0.015f, 0.06f }, hi[] = { 0.012f, 0.06f, 0.16f };
        b.uni (isB ? P::OscBDetune : P::OscADetune, lo[(int) sp], hi[(int) sp]);
        b.uni (isB ? P::OscBBlend : P::OscABlend, 0.7f, 1.f);
        b.uni (isB ? P::OscBUniWidth : P::OscAUniWidth, 0.9f, 1.f);
        b.pick (isB ? P::OscBUniMode : P::OscAUniMode, { (int) UniMode::Linear, (int) UniMode::Super }, { 0.5f, 0.5f });
        // free-running phases, like Serum's at Rand 100 % (nearly all of
        // Jilax's stacks): restarted, the voices start on fixed phases that
        // pile the energy into a few harmonics (the 8th the loudest in a
        // third of the shots, against a plateau from the 4th to the 9th)
        b.set (isB ? P::OscBRetrig : P::OscARetrig, 0.f);
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
        const Spread sp = spread (b);
        switch (kind)
        {
            case Body::Saw:
                osc (b, false, OscType::Analog);
                b.set (P::OscAShape, kSaw);
                unison (b, false, 7.f, 16.f, sp);
                break;
            case Body::Supersaw:
                osc (b, false, OscType::Supersaw);
                b.uni (P::OscADetune, 0.048f, 0.18f);
                break;
            case Body::Table:
            {
                osc (b, false, OscType::Wavetable);
                const int wt = b.pick (P::OscAWt, { 0, 1, 4, 5, 7, 2 }, { 0.25f, 0.15f, 0.2f, 0.2f, 0.1f, 0.1f });
                // Harmonic Sweep and Digital Grit start as sines: over a bass
                // root and its high-pass nothing would be left of that end
                const bool sineEnd = wt == 0 || wt == 4;
                b.uni (P::OscAWtPos, sineEnd ? 0.55f : 0.1f, sineEnd ? 1.f : 0.9f);
                unison (b, false, 7.f, 16.f, sp);
                // the table scanned over the hit, like Jilax's LFOs on the position
                if (b.chance (0.45f))
                {
                    b.set (P::MenvA, 0.f);
                    b.uni (P::MenvD, 150.f, 700.f);
                    b.uni (P::MenvS, 0.f, 0.3f);
                    b.uni (P::MenvR, 50.f, 200.f);
                    const float dir = sineEnd || b.chance (0.5f) ? 1.f : -1.f;
                    b.route (S::ModEnv, D::WtPosA, 0.2f * dir, 0.45f * dir);
                }
                break;
            }
            case Body::Square:
                osc (b, false, OscType::Analog);
                b.set (P::OscAShape, kSquare);
                b.uni (P::OscAPw, 0.25f, 0.5f);
                unison (b, false, 7.f, 16.f, sp);
                break;
            case Body::Fm:
                osc (b, false, OscType::FM);
                b.among (P::OscAFmRatio, { 1.f, 2.f, 3.f, 0.5f, 4.f });
                b.uni (P::OscAFm, 1.f, 5.f);
                b.uni (P::OscAFmFb, 0.f, 0.3f);
                unison (b, false, 1.f, 4.f, sp);
                break;
            case Body::Chord:
                // root, fifth and octave from the stack, the third from osc B
                osc (b, false, OscType::Analog);
                b.set (P::OscAShape, kSaw);
                unison (b, false, 9.f, 16.f, sp);
                b.set (P::OscAUniStack, (float) UniStack::OctFifth);
                break;
        }
        b.set (P::OscALevel, 0.8f);

        // the register: mostly the bass octave, FM tones and chords higher up
        if (kind == Body::Fm) octave = b.pick (P::Octave, { -2, -1, 0 }, { 0.25f, 0.35f, 0.4f });
        else if (kind == Body::Chord) octave = b.pick (P::Octave, { -1, 0 }, { 0.4f, 0.6f });
        else octave = b.pick (P::Octave, { -2, -1, 0 }, { 0.78f, 0.17f, 0.05f });

        // A second stack: an octave and a fifth up (+19, the 3rd harmonic's
        // pitch, so it thickens the comb rather than adding a note), the same
        // pitch on another shape, an octave up, a fifth, an octave down; 4-12
        // dB under the first. (Jilax's basses favour +19, his one-shots the
        // same pitch at about the first's level; their mix measured no closer
        // to the references.)
        if (kind == Body::Chord)
        {
            osc (b, true, OscType::Analog);
            b.set (P::OscBShape, kSaw);
            b.among (P::OscBCoarse, { 3.f, 4.f, 3.f });   // minor twice as often
            unison (b, true, 5.f, 9.f, sp);
            b.uni (P::OscBLevel, 0.55f, 0.75f);
        }
        else if (kind != Body::Fm && b.chance (0.65f))
        {
            const int interval = b.weighted ({ 0.38f, 0.22f, 0.2f, 0.1f, 0.1f });
            const bool sameShape = interval == 2 || b.chance (0.75f);
            osc (b, true, sameShape ? OscType::Analog : OscType::Wavetable);
            b.set (P::OscBShape, interval == 2 ? (b.chance (0.5f) ? kSquare : kSaw) : kSaw);
            const bool sineEnd = b.pick (P::OscBWt, { 0, 4, 5 }) != 5;
            b.uni (P::OscBWtPos, sineEnd ? 0.55f : 0.1f, sineEnd ? 1.f : 0.8f);
            static constexpr float semis[5] = { 19.f, 12.f, 0.f, 7.f, -12.f };
            b.set (P::OscBCoarse, semis[interval]);
            b.uni (P::OscBFine, -8.f, 8.f);
            unison (b, true, 7.f, 16.f, sp);
            b.uni (P::OscBLevel, 0.2f, 0.5f);
        }
        // A noise layer (half of Jilax's patches have one), through the
        // filter or after it: the air the references keep at 10 kHz while
        // their filters close. Tuned noise, on the note's harmonics: their
        // air is tonal (spectral flatness 0.1 from 6 to 16 kHz), and white
        // noise on top of the stack made it hiss (0.37); tuned keeps it at
        // 0.18. Now and then pink through the filter.
        if (b.chance (0.4f))
        {
            b.pick (P::NoiseType, { kTunedNoise, kPinkNoise }, { 0.85f, 0.15f });
            if (b.chance (0.5f))
            {
                b.set (P::NoiseType, (float) kTunedNoise);   // pink only through the filter
                b.on (P::NoiseDirect, true);
                b.uni (P::NoiseLevel, 0.045f, 0.135f);
            }
            else b.uni (P::NoiseLevel, 0.06f, 0.195f);
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
        // Half the references are a one-beat hit with its tail ringing
        // through a second beat (Tail: the note is one beat, the release
        // and the reverb ring on); the rest one beat, a few held for two.
        const auto shape = (Amp) b.weighted ({ 0.16f, 0.2f, 0.11f, 0.45f, 0.04f });
        if (shape == Amp::Tail) length (b, B1_4);
        else length (b, b.weighted ({ 0.9f, 0.08f, 0.02f }));

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
        b.pick (P::F2Type, { ft (FilterType::HP18), ft (FilterType::HP24) }, { 0.25f, 0.75f });
        const float hz = octave == 0 ? b.uni (P::F2Cutoff, 80.f, 200.f) : b.uni (P::F2Cutoff, 160.f, 330.f);
        b.uni (P::F2Reso, 0.1f, 0.45f);   // a peak at the corner: the energy bunches into a few harmonics
        b.set (P::F2Key, 0.f);
        b.set (P::F2Env, 0.f);
        return hz;
    }

    enum class Motion { Static, Close, Shut, Open, RiseFall };

    // Where the references fall the furthest (from a 2.5-6.5 kHz centroid),
    // the hit is thin as well as bright: 100-200 Hz sits about 14 dB lower in
    // the first 50 ms than later, and some air stays on after the filter has
    // closed. The high-pass rides the filter envelope down to its corner, and
    // a little noise skips the filter.
    void thinHit (B& b)
    {
        const bool ride = b.chance (0.6f);   // drawn first, so a locked F2 type doesn't skip it
        if (b.state().geti (P::F2Type) != ft (FilterType::Off) && ride)
            b.uni (P::F2Env, 0.1f, 0.22f);
        if (b.chance (0.4f))
        {
            b.set (P::NoiseType, (float) kTunedNoise);
            b.on (P::NoiseDirect, true);
            b.uni (P::NoiseLevel, 0.03f, 0.09f);
        }
    }

    // LFO 3, synced to the beat and restarted by the note: Jilax's way of
    // drawing a filter envelope. The LFO swings both ways, so the cutoff
    // knob sits in the middle of the sweep.
    void beatLfo (B& b, LfoShape shape, std::initializer_list<int> divisions, float phase)
    {
        b.lfoSync (2, shape, divisions, true);
        b.set (P::Lfo3Phase, phase);
    }

    // A decaying shot never opens over its length (the two cancel out) and
    // never shuts (it would be gone in a tenth of a second): Jilax keeps the
    // amp a gate when the filter makes the hit.
    Motion lowPass (B& b, Amp shape)
    {
        const bool decay = shape == Amp::Decay;
        const auto motion = (Motion) b.weighted ({ 0.3f, 0.2f, decay ? 0.f : 0.38f, decay ? 0.f : 0.09f, 0.13f });
        const bool twoBeats = b.state().geti (P::EnergyBars) != B1_4;
        // a Moog-style ladder in most of Jilax's patches
        b.pick (P::F1Type, { ft (FilterType::LP12), ft (FilterType::LP18), ft (FilterType::LP24),
                             ft (FilterType::LadderLP12), ft (FilterType::Ladder24) }, { 0.3f, 0.1f, 0.1f, 0.3f, 0.2f });
        // a touch of drive: above 0.25 it saturated the stack in front of the
        // filter (up to a fifth of the shot's energy distortion products)
        b.uni (P::F1Drive, 0.f, 0.14f);
        switch (motion)
        {
            case Motion::Static:
                if (b.chance (0.85f))
                {
                    // open: the saws as they are, bright to the top
                    b.set (P::F1Type, (float) ft (FilterType::Off));
                    break;
                }
                // a gentle slope from the upper mids, the same on every key:
                // the top keeps sounding through the shot
                b.pick (P::F1Type, { ft (FilterType::LP12), ft (FilterType::LadderLP12) }, { 0.6f, 0.4f });
                b.set (P::F1Key, 0.f);
                b.uni (P::F1Cutoff, 3500.f, 9000.f);
                b.uni (P::F1Reso, 0.f, 0.2f);
                b.set (P::F1Env, 0.f);
                break;
            case Motion::Close:
            case Motion::Shut:
            {
                b.pick (P::F1Type, { ft (FilterType::LP12), ft (FilterType::LP18), ft (FilterType::LadderLP12), ft (FilterType::Ladder24) },
                        { 0.35f, 0.15f, 0.3f, 0.2f });
                b.uni (P::F1Key, 0.f, 0.3f);
                // a shut filter's peak sweeps down from the top: a bright start
                if (motion == Motion::Shut) b.uni (P::F1Reso, 0.35f, 0.65f);
                else b.uni (P::F1Reso, 0.05f, 0.4f);
                b.set (P::FenvA, 0.f);
                b.uni (P::FenvS, 0.f, 0.2f);
                b.uni (P::FenvR, 50.f, 200.f);
                if (motion == Motion::Shut)
                {
                    // Jilax's own way: the filter shuts nearly all the way, the
                    // hit is the filter. A third of the references end this dark.
                    b.uni (P::F1Cutoff, 120.f, 400.f);
                    b.uni (P::F1Env, 0.7f, 1.f);
                    b.uni (P::FenvD, 150.f, 700.f);
                    thinHit (b);
                    break;
                }
                // a third of them close from the top to a few kHz: bright
                // shots that still fall one or two octaves
                const bool brightEnd = b.chance (0.35f);
                if (b.chance (0.55f))
                {
                    // the beat LFO as the envelope: a jump and a straight fall over
                    // a beat (or an eighth, or half the shot: it hits again on the beat)
                    if (twoBeats) beatLfo (b, LfoShape::SawDown, { D1_2, D1_2, D1_4 }, 0.f);
                    else beatLfo (b, LfoShape::SawDown, { D1_4, D1_4, D1_4, D1_8, D1_2 }, 0.f);
                    if (brightEnd) b.uni (P::F1Cutoff, 1800.f, 4500.f);
                    else b.uni (P::F1Cutoff, 300.f, 1400.f);
                    b.set (P::F1Env, 0.f);
                    b.route (S::Lfo3, D::F1Cutoff, 0.2f, 0.38f);
                    break;
                }
                // the brightness halves (in octaves) in about 85 ms, 1.5 octaves down in all;
                // it closes to a few kHz, not to the bottom: a gentle slope
                // that ends there keeps the top sounding through the shot
                // (the references keep 60 % of their top after the first
                // 100 ms) while the brightness still drops 1-2 octaves
                if (brightEnd) b.uni (P::F1Cutoff, 1500.f, 4000.f);
                else b.uni (P::F1Cutoff, 300.f, 1500.f);
                b.uni (P::F1Env, 0.6f, 0.95f);
                b.uni (P::FenvD, 300.f, 1500.f);
                thinHit (b);
                break;
            }
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
                b.uni (P::F1Reso, 0.1f, 0.5f);
                if (b.chance (0.5f))
                {
                    // a triangle over the shot from its bottom (two beats may
                    // wobble twice, a quarter each)
                    beatLfo (b, LfoShape::Triangle, twoBeats ? std::initializer_list<int> { D1_2, D1_4 }
                                                             : std::initializer_list<int> { D1_4 }, 0.75f);
                    b.uni (P::F1Cutoff, 500.f, 1500.f);
                    b.set (P::F1Env, 0.f);
                    b.route (S::Lfo3, D::F1Cutoff, 0.15f, 0.3f);
                    break;
                }
                b.uni (P::F1Cutoff, 200.f, 600.f);
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
    // Bright: the high band from the upper mids, lifted further (the
    // brightest third of the references sit only 5-6 dB under their
    // loudest octave at 2.5 kHz, where a clipped saw stack is 9 dB down).
    void ott (B& b, bool bright, bool closing)
    {
        b.on (P::CompOn, true);
        b.set (P::CompMode, (float) CompMode::Multiband);
        b.uni (P::CompAttack, 2.f, 12.f);
        b.uni (P::CompRelease, 60.f, 220.f);
        b.uni (P::CompMix, 0.5f, 0.9f);
        b.uni (P::CompDown, 0.5f, 0.85f);
        b.uni (P::CompUp, 0.2f, 0.55f);   // enough to fill the body, not to flatten the decays
        b.uni (P::MbXLow, 120.f, 250.f);
        if (bright) b.uni (P::MbXHigh, 1200.f, 2800.f);
        else b.uni (P::MbXHigh, 4500.f, 8000.f);   // the high band is the air, not the upper mids
        b.uni (P::MbLowDown, -18.f, -8.f);
        b.uni (P::MbMidDown, -24.f, -14.f);
        b.uni (P::MbHighDown, -28.f, -18.f);
        b.uni (P::MbLowUp, -44.f, -34.f);
        b.uni (P::MbMidUp, -44.f, -34.f);
        // the upward lift brings the air back as a filter closes: off where
        // the closing is the shot (its top has to fall away with the filter)
        if (closing) b.uni (P::MbHighUp, -60.f, -50.f);
        else b.uni (P::MbHighUp, -38.f, -28.f);
        b.uni (P::MbLowGain, -2.f, 2.f);
        b.uni (P::MbMidGain, 0.f, 3.f);
        if (bright) b.uni (P::MbHighGain, 6.f, 11.f);
        else b.uni (P::MbHighGain, 1.f, 6.f);
        b.uni (P::CompOut, 4.f, 8.f);   // makeup: the clipper gives the drive back, so a shot comes out well under its input
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
        b.uni (P::CompOut, 7.f, 12.f);
    }

    void effects (B& b, Body kind, Amp shape, Motion motion, float highPassHz)
    {
        // about one reference in five is close to mono: the side 10-25 dB
        // under the mid, where the rest have it about level
        const bool narrow = b.chance (0.2f);
        // The references are dense (about 7.5 dB crest where they are
        // loudest, against 12-15 dB for a high-passed saw stack) but not
        // chewed: their samples spread like a body that is only limited
        // (kurtosis 2.5; 3.5 unclipped, 2.1 for the hard clip at 19-27 dB
        // this used to be, which clipped 70 % of the loudest samples and made
        // a quarter of the shot distortion products, three quarters of its
        // top octaves). A clipper at the very end of the chain, after the
        // compressor and the tails, mostly soft, 13-20 dB over the sound's
        // own level: crest 8, kurtosis 2.5, a tenth of the shot products.
        // Auto Level makes it bite the same on every patch and lets a decay
        // fall through it. Otherwise the odd shot gets its grit early.
        if (b.chance (0.9f))
        {
            b.on (P::DistOn, true);
            b.pick (P::DistType, { (int) DistType::Hard, (int) DistType::Soft, (int) DistType::Tube }, { 0.3f, 0.45f, 0.25f });
            b.on (P::DistAuto, true);
            b.uni (P::DistDrive, 13.f, 20.f);
            b.uni (P::DistMix, 0.92f, 1.f);
            // A shut filter would come out bright again: the clipper adds back
            // the top it took away. Its tone keeps a shut shot dark.
            if (motion == Motion::Shut) b.uni (P::DistTone, 2500.f, 7000.f);
            else b.uni (P::DistTone, 19000.f, 20000.f);
            // A filter envelope that rises and falls leaves the shot dark at
            // both ends, where Auto Level still drives it as hard as in the
            // bright middle: up to two thirds of such a shot was the
            // clipper's harmonics. The drive follows the envelope instead,
            // 13 dB under the drawn drive while the filter is shut.
            if (motion == Motion::RiseFall && b.state().get (P::F1Env) > 0.f)
            {
                constexpr float kFollowDb = 13.f;
                b.set (P::DistDrive, b.state().get (P::DistDrive) - kFollowDb);
                b.route (S::FilterEnv, D::DistDrive, kFollowDb / 36.f, kFollowDb / 36.f);
            }
            // Clipping brings a high-passed root back as a difference tone.
            // The clipper's low cut takes it out again, a little under the
            // voice's high-pass: the references keep the octave around 80 Hz
            // about 37 dB under their loudest and the 2nd harmonic about 31
            // dB down. (The clipper trims the peaks the cut raises.)
            if (highPassHz > 0.f) b.uni (P::DistLowCut, 0.6f * highPassHz, 0.75f * highPassHz);
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
        if (! narrow && b.chance (kind == Body::Supersaw ? 0.7f : 0.4f))
        {
            b.on (P::ChorusOn, true);
            b.pick (P::ChorusMode, { 0, 1 }, { 0.7f, 0.3f });
            b.uni (P::ChorusRate, 0.2f, 1.2f);
            b.uni (P::ChorusDepth, 0.3f, 0.8f);
            b.uni (P::ChorusFb, -0.3f, 0.5f);
            b.uni (P::ChorusMix, 0.25f, 0.5f);
        }
        // a compressor on every shot (nearly all of Jilax's patches have one),
        // the bright lift on most static shots and half the rest
        const bool closing = motion == Motion::Close || motion == Motion::Shut;
        if (b.chance (0.78f)) ott (b, b.chance (motion == Motion::Static ? 0.9f : 0.5f), closing);
        else glue (b);
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
            // dry on the hit, wetter on the tail (Jilax ducks the reverb with the beat LFO)
            if (b.chance (0.35f))
            {
                b.set (P::ReverbMix, b.state().get (P::ReverbMix) * 0.4f);
                b.route (S::Sweep, D::ReverbMix, 0.15f, 0.3f);
            }
        }
        // wide highs over narrower lows: the side climbs from about -5 dB of
        // the mid under 160 Hz to level with it above 2.5 kHz
        if (narrow)
        {
            b.on (P::WidthOn, true);
            b.uni (P::WidthAmount, 0.05f, 0.4f);
        }
        else if (b.chance (kind == Body::Supersaw ? 0.9f : 0.75f))
        {
            b.on (P::WidthOn, true);
            if (b.chance (0.3f)) b.uni (P::WidthAmount, 1.f, 1.3f);
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
    effects (b, kind, shape, motion, highPassHz);
    b.macros ({ D::F1Cutoff, D::F2Cutoff, D::F1Drive, D::F1Reso, D::DetuneA, D::ReverbMix, D::DelayMix, D::ChorusMix });
    // The macros start part way up, so they add to what they move: the
    // detune and the drives start where the recipe drew them (in a sixth of
    // the shots a macro on the clipper added 3-5 dB of drive).
    for (const auto d : { D::DetuneA, D::F1Drive, D::DistDrive })
        b.settle (d);
}

} // namespace psy
