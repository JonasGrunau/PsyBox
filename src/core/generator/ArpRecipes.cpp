// Arp recipes, one per category, used when "Arp" is ticked next to Generate.
//
// They run after the sound recipe on their own random stream and only touch
// arp parameters, so a seed gives the same sound with or without an arp.
// Divisions and rhythms are picked for a 138-148 BPM psy grid: a rhythm's
// step 0 is the beat, so Rolling leaves the kick alone and Offbeat at 1/16
// lands on the "and" of every beat.

#include "Generator.h"

namespace psy
{

namespace
{
    using B = RecipeBuilder;

    // syncDiv indices
    enum Div { D4_1, D2_1, D1_1, D1_2, D1_2D, D1_2T, D1_4, D1_4D, D1_4T, D1_8, D1_8D, D1_8T,
               D1_16, D1_16D, D1_16T, D1_32, D1_32T, D1_64 };

    constexpr int Up = (int) ArpMode::Up, Down = (int) ArpMode::Down, UpDown = (int) ArpMode::UpDown,
                  Random = (int) ArpMode::Random, Order = (int) ArpMode::Order;
    constexpr int Straight = (int) ArpRhythm::Straight, Rolling = (int) ArpRhythm::Rolling,
                  Gallop = (int) ArpRhythm::Gallop, Offbeat = (int) ArpRhythm::Offbeat, Skip = (int) ArpRhythm::Skip,
                  Tresillo = (int) ArpRhythm::ThreeThreeTwo, Broken = (int) ArpRhythm::Broken;

    void gate (B& b, float lo, float hi) { b.uni (P::ArpGate, lo, hi); }
    void swing (B& b, float hi) { b.uni (P::ArpSwing, 0.f, hi); }

    // ---- basses and lines ----------------------------------------------------

    // The psy roll: the kick owns the beat, the bass fills the other three 16ths.
    void rollingBass (B& b)
    {
        b.pick (P::ArpDiv, { D1_16 });
        b.pick (P::ArpMode, { Order });
        b.pick (P::ArpOctaves, { 1, 2 }, { 0.85f, 0.15f });
        b.pick (P::ArpRhythm, { Rolling, Gallop, Straight }, { 0.7f, 0.2f, 0.1f });
        gate (b, 0.35f, 0.6f);
        swing (b, 0.06f);
    }

    // 303 line: long gates tie into slides on the legato voice.
    void acid (B& b)
    {
        b.pick (P::ArpDiv, { D1_16 });
        b.pick (P::ArpMode, { Random, UpDown, Order }, { 0.45f, 0.3f, 0.25f });
        b.pick (P::ArpOctaves, { 1, 2 }, { 0.35f, 0.65f });
        b.pick (P::ArpRhythm, { Straight, Broken, Skip }, { 0.4f, 0.35f, 0.25f });
        gate (b, 0.5f, 1.3f);
        swing (b, 0.2f);
    }

    void squelch (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_16T }, { 0.7f, 0.3f });
        b.pick (P::ArpMode, { Random, Order }, { 0.7f, 0.3f });
        b.pick (P::ArpOctaves, { 1, 2 });
        b.pick (P::ArpRhythm, { Broken, Offbeat, Rolling }, { 0.45f, 0.3f, 0.25f });
        gate (b, 0.3f, 0.7f);
        swing (b, 0.1f);
    }

    // ---- hits and fx -----------------------------------------------------------

    void zap (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_8 }, { 0.65f, 0.35f });
        b.pick (P::ArpMode, { Down, Random }, { 0.6f, 0.4f });
        b.pick (P::ArpOctaves, { 1, 2 });
        b.pick (P::ArpRhythm, { Offbeat, Skip, Tresillo }, { 0.45f, 0.3f, 0.25f });
        gate (b, 0.15f, 0.4f);
        swing (b, 0.08f);
    }

    // Dotted-eighth falls (3-3-2 on 16ths), the classic psy laser echo.
    void laser (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_8D, D1_8 }, { 0.5f, 0.3f, 0.2f });
        b.pick (P::ArpMode, { Down });
        b.pick (P::ArpOctaves, { 2, 3 });
        b.pick (P::ArpRhythm, { Tresillo, Straight }, { 0.6f, 0.4f });
        gate (b, 0.4f, 0.8f);
        swing (b, 0.05f);
    }

    // Sweeps live on seconds-long envelopes, so their arps tie (gate >= 100 %,
    // no rests): the voice steps through the notes while the sweep runs on.
    void riser (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_16T }, { 0.75f, 0.25f });
        b.pick (P::ArpMode, { Up });
        b.pick (P::ArpOctaves, { 3, 4 });
        b.pick (P::ArpRhythm, { Straight });
        gate (b, 1.f, 1.3f);
    }

    void downlifter (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_16T });
        b.pick (P::ArpMode, { Down });
        b.pick (P::ArpOctaves, { 3, 4 });
        b.pick (P::ArpRhythm, { Straight });
        gate (b, 1.f, 1.3f);
    }

    void noiseFx (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_8, D1_16T }, { 0.5f, 0.3f, 0.2f });
        b.pick (P::ArpMode, { Random, Up }, { 0.6f, 0.4f });
        b.pick (P::ArpOctaves, { 2, 3 });
        b.pick (P::ArpRhythm, { Straight });
        gate (b, 1.f, 1.3f);
    }

    // ---- tonal ------------------------------------------------------------------

    void psyLead (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_16T, D1_8T }, { 0.55f, 0.25f, 0.2f });
        b.pick (P::ArpMode, { Up, UpDown, Order }, { 0.35f, 0.4f, 0.25f });
        b.pick (P::ArpOctaves, { 2, 3 }, { 0.6f, 0.4f });
        b.pick (P::ArpRhythm, { Straight, Tresillo }, { 0.7f, 0.3f });
        gate (b, 0.6f, 1.f);
        swing (b, 0.08f);
    }

    void pluck (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_8T }, { 0.7f, 0.3f });
        b.pick (P::ArpMode, { UpDown, Random, Up }, { 0.4f, 0.35f, 0.25f });
        b.pick (P::ArpOctaves, { 2, 3 });
        b.pick (P::ArpRhythm, { Straight, Skip, Tresillo }, { 0.4f, 0.3f, 0.3f });
        gate (b, 0.3f, 0.6f);
        swing (b, 0.1f);
    }

    // Offbeat chord stabs: 1/16 with Offbeat hits the "and" of every beat.
    void stab (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_8 }, { 0.65f, 0.35f });
        b.pick (P::ArpMode, { Order, Up }, { 0.7f, 0.3f });
        b.pick (P::ArpOctaves, { 1 });
        b.pick (P::ArpRhythm, { Offbeat, Skip }, { 0.7f, 0.3f });
        gate (b, 0.3f, 0.6f);
        swing (b, 0.05f);
    }

    void metallic (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_8T }, { 0.65f, 0.35f });
        b.pick (P::ArpMode, { Random, Up }, { 0.6f, 0.4f });
        b.pick (P::ArpOctaves, { 2, 3 });
        b.pick (P::ArpRhythm, { Skip, Broken, Tresillo }, { 0.4f, 0.35f, 0.25f });
        gate (b, 0.2f, 0.5f);
        swing (b, 0.08f);
    }

    // ---- talking and liquid -------------------------------------------------------

    void screech (B& b)
    {
        b.pick (P::ArpDiv, { D1_16T, D1_32, D1_16 }, { 0.4f, 0.3f, 0.3f });
        b.pick (P::ArpMode, { Random });
        b.pick (P::ArpOctaves, { 1, 2 });
        b.pick (P::ArpRhythm, { Straight, Broken }, { 0.55f, 0.45f });
        gate (b, 0.7f, 1.f);
        swing (b, 0.06f);
    }

    void alienVoice (B& b)
    {
        b.pick (P::ArpDiv, { D1_8T, D1_16 }, { 0.5f, 0.5f });
        b.pick (P::ArpMode, { Random });
        b.pick (P::ArpOctaves, { 1 });
        b.pick (P::ArpRhythm, { Broken, Skip }, { 0.6f, 0.4f });
        gate (b, 0.8f, 1.2f);
        swing (b, 0.2f);
    }

    void bubble (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_32, D1_16T }, { 0.5f, 0.25f, 0.25f });
        b.pick (P::ArpMode, { Random, Up }, { 0.65f, 0.35f });
        b.pick (P::ArpOctaves, { 2, 3, 4 }, { 0.4f, 0.4f, 0.2f });
        b.pick (P::ArpRhythm, { Broken, Straight, Skip }, { 0.45f, 0.3f, 0.25f });
        gate (b, 0.15f, 0.4f);
        swing (b, 0.2f);
    }

    void gurgle (B& b)
    {
        b.pick (P::ArpDiv, { D1_32, D1_16T }, { 0.55f, 0.45f });
        b.pick (P::ArpMode, { Random });
        b.pick (P::ArpOctaves, { 1, 2 }, { 0.6f, 0.4f });
        b.pick (P::ArpRhythm, { Broken, Straight }, { 0.7f, 0.3f });
        gate (b, 0.5f, 1.f);
        swing (b, 0.1f);
    }

    void wobble (B& b)
    {
        b.pick (P::ArpDiv, { D1_8, D1_4, D1_8T }, { 0.5f, 0.25f, 0.25f });
        b.pick (P::ArpMode, { Order, UpDown }, { 0.55f, 0.45f });
        b.pick (P::ArpOctaves, { 1, 2 });
        b.pick (P::ArpRhythm, { Straight, Tresillo }, { 0.6f, 0.4f });
        gate (b, 0.8f, 1.1f);
        swing (b, 0.06f);
    }

    // ---- beds ---------------------------------------------------------------------

    // Overlapping gates let the pad voices ring into each other.
    void atmos (B& b)
    {
        b.pick (P::ArpDiv, { D1_8, D1_8D, D1_4 }, { 0.45f, 0.3f, 0.25f });
        b.pick (P::ArpMode, { UpDown, Random }, { 0.6f, 0.4f });
        b.pick (P::ArpOctaves, { 2, 3 });
        b.pick (P::ArpRhythm, { Straight });
        gate (b, 1.f, 1.5f);
        swing (b, 0.05f);
    }

    void drone (B& b)
    {
        b.pick (P::ArpDiv, { D1_4, D1_2, D1_1 }, { 0.4f, 0.35f, 0.25f });
        b.pick (P::ArpMode, { Random, Order }, { 0.6f, 0.4f });
        b.pick (P::ArpOctaves, { 1, 2 });
        b.pick (P::ArpRhythm, { Straight });
        gate (b, 1.2f, 1.5f);
    }
} // namespace

Recipe arpRecipeFor (Category c) noexcept
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
        case Category::Count:       break;
    }
    return acid;
}

} // namespace psy
