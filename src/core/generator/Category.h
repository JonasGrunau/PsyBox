#pragma once

#include <algorithm>
#include <array>
#include <string_view>
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
    // Accents run as a rainbow down the alphabetical list (red -> magenta),
    // generated in OKLCH with hue in even steps and lightness lifted around
    // yellow and lowered around blue, so no accent shouts louder than another.
    static const std::array<CategoryInfo, kNumCategories> info { {
        { "Acid",         "Acid",    0xFE5654, "303 lines: diode ladder, accent, slide" },
        { "Squelch",      "Squelch", 0x8D86FC, "Wet, vocal mid-bass with fast filter motion" },
        { "Zap",          "Zap",     0xF56CDC, "Short pitch dives for percussive hits" },
        { "Laser",        "Laser",   0x4ADD5E, "Sync sweeps and long pitch falls" },
        { "Rolling Bass", "Bass",    0x41A6FF, "Tight offbeat bass for 16th-note rolls" },
        { "Psy Lead",     "Lead",    0x06BFDE, "Wide saw and wavetable leads" },
        { "Pluck",        "Pluck",   0x14C5C9, "Percussive tonal plucks" },
        { "Stab",         "Stab",    0xB17BFE, "Chord stabs with room around them" },
        { "Screech",      "Screech", 0x6C95FD, "Resonant, distorted screams" },
        { "Alien Voice",  "Alien",   0xFF702E, "Talking formant textures" },
        { "Bubble",       "Bubble",  0xFDB42D, "Resonant blips and bloops" },
        { "Gurgle",       "Gurgle",  0xA0E125, "Liquid, random filter chatter" },
        { "Wobble",       "Wobble",  0xD770F7, "Tempo-synced morphing tones" },
        { "Metallic",     "Metal",   0x07D293, "FM bells, combs and clangs" },
        { "Riser",        "Riser",   0x13B6F1, "Rising sweeps for build-ups" },
        { "Downlifter",   "Down",    0xF7CE0C, "Falling sweeps for drops" },
        { "Noise FX",     "Noise",   0x0FCAB2, "Filtered noise whooshes and textures" },
        { "Atmos",        "Atmos",   0xFD952D, "Evolving pads and air" },
        { "Drone",        "Drone",   0xD9DE06, "Slow, dark, endless tones" },
    } };
    return info;
}

// Sidebar order: alphabetical. The enum order stays fixed because saved
// sessions store the category index.
inline const std::array<Category, kNumCategories>& categoryDisplayOrder() noexcept
{
    static const std::array<Category, kNumCategories> order = [] {
        std::array<Category, kNumCategories> o {};
        for (int i = 0; i < kNumCategories; ++i) o[(size_t) i] = (Category) i;
        std::sort (o.begin(), o.end(), [] (Category a, Category b) {
            return std::string_view (categoryInfo()[(size_t) a].name) < std::string_view (categoryInfo()[(size_t) b].name);
        });
        return o;
    }();
    return order;
}

} // namespace psy
