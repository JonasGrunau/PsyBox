#pragma once

#include <array>
#include <cstdint>

namespace psy
{

enum class Category : int
{
    Acid, Squelch, Zap, Laser, RollingBass, PsyLead, Pluck, Stab, Screech, AlienVoice,
    Bubble, Gurgle, Wobble, Metallic, Riser, Downlifter, NoiseFx, Atmos, Drone, Count
};

constexpr int kNumCategories = (int) Category::Count;

struct CategoryInfo
{
    const char* name;
    const char* shortName;   // for compact chips
    uint32_t colour;         // 0xRRGGBB accent
    const char* blurb;
};

inline const std::array<CategoryInfo, kNumCategories>& categoryInfo() noexcept
{
    // Hues spread around the wheel at matched perceived lightness, so no
    // accent shouts louder than another on the black UI.
    static const std::array<CategoryInfo, kNumCategories> info { {
        { "Acid",         "Acid",    0xF2E205, "303 lines: diode ladder, accent, slide" },
        { "Squelch",      "Squelch", 0xB4E61D, "Wet, vocal mid-bass with fast filter motion" },
        { "Zap",          "Zap",     0x29C7F5, "Short pitch dives for percussive hits" },
        { "Laser",        "Laser",   0xFF4FB8, "Sync sweeps and long pitch falls" },
        { "Rolling Bass", "Bass",    0xFF4545, "Tight offbeat bass for 16th-note rolls" },
        { "Psy Lead",     "Lead",    0xFF9F1C, "Wide saw and wavetable leads" },
        { "Pluck",        "Pluck",   0x22D1C4, "Percussive tonal plucks" },
        { "Stab",         "Stab",    0xFF8A6B, "Chord stabs with room around them" },
        { "Screech",      "Screech", 0xFF6A2B, "Resonant, distorted screams" },
        { "Alien Voice",  "Alien",   0x9B6BFF, "Talking formant textures" },
        { "Bubble",       "Bubble",  0x45E06F, "Resonant blips and bloops" },
        { "Gurgle",       "Gurgle",  0x2FE0A8, "Liquid, random filter chatter" },
        { "Wobble",       "Wobble",  0xE45CF5, "Tempo-synced morphing tones" },
        { "Metallic",     "Metal",   0xC9CED6, "FM bells, combs and clangs" },
        { "Riser",        "Riser",   0xC08BFF, "Rising sweeps for build-ups" },
        { "Downlifter",   "Down",    0x6D6BFF, "Falling sweeps for drops" },
        { "Noise FX",     "Noise",   0xE8D6A8, "Filtered noise whooshes and textures" },
        { "Atmos",        "Atmos",   0x5AA9FF, "Evolving pads and air" },
        { "Drone",        "Drone",   0x4F7BFF, "Slow, dark, endless tones" },
    } };
    return info;
}

} // namespace psy
