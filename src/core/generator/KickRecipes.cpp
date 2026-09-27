// Kick recipes, one per kick category. They only touch the Kick params, the
// velocity sensitivity and a little global FX; every synth param stays at its
// init value. The generator sets the engine mode and key tracking afterwards.
//
// Numbers come from measured kicks: the pitch falls in two exponential stages
// (start -> knee in ~3 ms, knee -> body in ~30 ms for a progressive kick),
// the amp holds flat and then drops fast so the bass has room, and Punch with
// a slower Attack gives the knock between the click and the body.

#include "Generator.h"

namespace psy
{

namespace
{
    using B = RecipeBuilder;

    constexpr int Noise = (int) KickClick::Noise, Tick = (int) KickClick::Tick, Blip = (int) KickClick::Blip;
    constexpr int Soft = (int) KickDrive::Soft, Tube = (int) KickDrive::Tube, Hard = (int) KickDrive::Hard;

    // Call after amp(): the sweep lands on the key after three time constants,
    // and that has to happen while the body is still loud (by the end of the
    // hold plus the first tenth of the decay), or the kick is never in tune.
    void pitch (B& b, float tLo, float tHi, float sLo, float sHi, float snapLo, float snapHi, float kLo, float kHi,
                float swLo, float swHi)
    {
        const auto& s = b.state();
        const float loud = s.get (P::KickAttack) + s.get (P::KickHold) + 0.1f * s.get (P::KickDecay);
        swHi = std::min (swHi, loud / 3.f);
        swLo = std::min (swLo, swHi);
        b.uni (P::KickTune, tLo, tHi);
        b.uni (P::KickStart, sLo, sHi);
        b.uni (P::KickSnap, snapLo, snapHi);
        b.uni (P::KickKnee, kLo, kHi);
        b.uni (P::KickSweep, swLo, swHi);
    }

    void amp (B& b, float aLo, float aHi, float hLo, float hHi, float dLo, float dHi, float pLo, float pHi,
              float plLo, float plHi)
    {
        b.uni (P::KickAttack, aLo, aHi);
        b.uni (P::KickHold, hLo, hHi);
        b.uni (P::KickDecay, dLo, dHi);
        b.uni (P::KickPunch, pLo, pHi);
        b.uni (P::KickPunchLen, plLo, plHi);
    }

    void click (B& b, std::initializer_list<int> types, std::initializer_list<float> weights, float lLo, float lHi,
                float dLo, float dHi, float tLo, float tHi)
    {
        b.pick (P::KickClickType, types, weights);
        b.uni (P::KickClickLevel, lLo, lHi);
        b.uni (P::KickClickDecay, dLo, dHi);
        b.uni (P::KickClickTone, tLo, tHi);
    }

    void drive (B& b, std::initializer_list<int> types, std::initializer_list<float> weights, float dLo, float dHi,
                float toneLo, float toneHi)
    {
        b.pick (P::KickDriveType, types, weights);
        b.uni (P::KickDrive, dLo, dHi);
        b.uni (P::KickTone, toneLo, toneHi);
    }

    // Global distortion on top of the kick's own drive.
    void globalDist (B& b, float p, std::initializer_list<int> types, float dLo, float dHi, float mixLo, float mixHi)
    {
        if (! b.chance (p)) return;
        b.on (P::DistOn, true);
        b.pick (P::DistType, types);
        b.uni (P::DistDrive, dLo, dHi);
        b.uni (P::DistMix, mixLo, mixHi);
        b.uni (P::DistTone, 5000.f, 14000.f);
    }

    // The centre of the measured references: clean sine body, deep knock,
    // flat to ~160 ms, -30 dB at ~220 ms.
    void progressive (B& b)
    {
        amp (b, 30.f, 45.f, 105.f, 150.f, 90.f, 140.f, 0.85f, 1.f, 4.f, 9.f);
        pitch (b, 38.f, 53.f, 1600.f, 3000.f, 2.f, 5.f, 230.f, 600.f, 15.f, 40.f);
        click (b, { Noise, Tick }, { 0.7f, 0.3f }, 0.f, 0.25f, 8.f, 20.f, 2500.f, 7000.f);
        drive (b, { Tube, Soft }, { 0.8f, 0.2f }, -30.f, -20.f, 12000.f, 20000.f);
        b.uni (P::VelSens, 0.2f, 0.4f);
    }

    // Brighter start, shorter body and a clearer click: room for a 16th roll.
    void fullOn (B& b)
    {
        amp (b, 8.f, 22.f, 50.f, 100.f, 50.f, 100.f, 0.85f, 1.f, 3.f, 7.f);
        pitch (b, 45.f, 60.f, 3000.f, 6000.f, 1.f, 3.f, 300.f, 700.f, 15.f, 32.f);
        click (b, { Noise, Tick, Blip }, { 0.5f, 0.3f, 0.2f }, 0.2f, 0.5f, 5.f, 15.f, 3000.f, 9000.f);
        drive (b, { Tube, Soft }, { 0.6f, 0.4f }, -15.f, 0.f, 10000.f, 20000.f);
        b.uni (P::VelSens, 0.2f, 0.4f);
        globalDist (b, 0.2f, { (int) DistType::Soft }, 2.f, 8.f, 0.15f, 0.35f);
    }

    // Slow sweep from a low start, long round tail, soft top, a touch of room.
    void goa (B& b)
    {
        amp (b, 0.f, 15.f, 40.f, 130.f, 150.f, 420.f, 0.3f, 0.8f, 5.f, 15.f);
        pitch (b, 42.f, 55.f, 800.f, 2000.f, 3.f, 8.f, 150.f, 350.f, 30.f, 70.f);
        click (b, { Blip, Noise }, { 0.5f, 0.5f }, 0.f, 0.2f, 10.f, 30.f, 1500.f, 5000.f);
        drive (b, { Tube, Soft }, { 0.8f, 0.2f }, -25.f, -10.f, 3000.f, 10000.f);
        b.uni (P::VelSens, 0.3f, 0.5f);
        if (b.chance (0.35f))
        {
            b.on (P::ReverbOn, true);
            b.uni (P::ReverbSize, 0.3f, 0.6f);
            b.uni (P::ReverbDecay, 0.6f, 1.5f);
            b.uni (P::ReverbDamp, 0.4f, 0.7f);
            b.uni (P::ReverbPre, 5.f, 20.f);
            b.uni (P::ReverbMix, 0.05f, 0.12f);
        }
    }

    // Hard click, fast snap, heavy saturation, short and tight.
    void darkForest (B& b)
    {
        amp (b, 0.f, 10.f, 40.f, 90.f, 30.f, 80.f, 0.9f, 1.f, 2.f, 6.f);
        pitch (b, 42.f, 55.f, 3000.f, 8000.f, 0.5f, 2.f, 200.f, 600.f, 10.f, 25.f);
        click (b, { Tick, Noise }, { 0.5f, 0.5f }, 0.3f, 0.7f, 3.f, 12.f, 4000.f, 12000.f);
        drive (b, { Hard, Soft, Tube }, { 0.45f, 0.35f, 0.2f }, 0.f, 18.f, 6000.f, 16000.f);
        b.uni (P::VelSens, 0.1f, 0.3f);
        globalDist (b, 0.5f, { (int) DistType::Soft, (int) DistType::Hard, (int) DistType::Tube }, 4.f, 12.f, 0.2f, 0.5f);
    }

    // The shortest: high tune, very fast sweep, bright tick.
    void hiTech (B& b)
    {
        amp (b, 0.f, 5.f, 30.f, 70.f, 25.f, 60.f, 0.9f, 1.f, 1.5f, 4.f);
        pitch (b, 50.f, 65.f, 4000.f, 8000.f, 0.5f, 1.5f, 300.f, 900.f, 6.f, 18.f);
        click (b, { Tick, Blip }, { 0.6f, 0.4f }, 0.4f, 0.8f, 2.f, 8.f, 6000.f, 14000.f);
        drive (b, { Hard, Tube }, { 0.7f, 0.3f }, 0.f, 12.f, 12000.f, 20000.f);
        b.uni (P::VelSens, 0.1f, 0.3f);
    }
} // namespace

Recipe kickRecipeFor (Category c) noexcept
{
    switch (c)
    {
        case Category::KickFullOn:      return fullOn;
        case Category::KickGoa:         return goa;
        case Category::KickDarkForest:  return darkForest;
        case Category::KickHiTech:      return hiTech;
        case Category::KickProgressive: return progressive;
        default:                        break;
    }
    return progressive;
}

} // namespace psy
