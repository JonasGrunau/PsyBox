#pragma once

#include <algorithm>
#include <array>
#include <string_view>
#include <cstdint>
#include <vector>

namespace psy
{

enum class Category : int
{
    Acid, Squelch, Zap, Laser, RollingBass, PsyLead, Pluck, Stab, Screech, AlienVoice,
    Bubble, Gurgle, Wobble, Metallic, Riser, Downlifter, NoiseFx, Atmos, Drone,
    Uplifter, Impact, Gate, Glitch, Reverse,
    KickFullOn, KickGoa, KickDarkForest, KickHiTech, KickProgressive, Count
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
        { "Squelch",      "Squelch", 0x8988FD, "Wet, vocal mid-bass with fast filter motion" },
        { "Zap",          "Zap",     0xF56CDC, "Short pitch dives for percussive hits" },
        { "Laser",        "Laser",   0x0FD48C, "Sync sweeps and long pitch falls" },
        { "Rolling Bass", "Bass",    0x51A1FF, "Tight offbeat bass for 16th-note rolls" },
        { "Psy Lead",     "Lead",    0x03C0DD, "Wide saw and wavetable leads" },
        { "Pluck",        "Pluck",   0x0CC4CC, "Percussive tonal plucks" },
        { "Stab",         "Stab",    0xA47FFF, "Chord stabs with room around them" },
        { "Screech",      "Screech", 0x6F94FD, "Resonant, distorted screams" },
        { "Alien Voice",  "Alien",   0xFF6B39, "Talking formant textures" },
        { "Bubble",       "Bubble",  0xFE9F2A, "Resonant blips and bloops" },
        { "Gurgle",       "Gurgle",  0x8DE038, "Liquid, random filter chatter" },
        { "Wobble",       "Wobble",  0xDE6FF2, "Tempo-synced morphing tones" },
        { "Metallic",     "Metal",   0x03CDA8, "FM bells, combs and clangs" },
        { "Riser",        "Riser",   0x2DAEF9, "Synth tones climbing in pitch for build-ups" },
        { "Downlifter",   "Down",    0xFDB729, "Falling sweeps for drops" },
        { "Noise FX",     "Noise",   0x02C8BC, "Filtered noise whooshes and textures" },
        { "Atmos",        "Atmos",   0xFF8528, "Evolving pads and air" },
        { "Drone",        "Drone",   0xF8CB0E, "Slow, dark, endless tones" },
        { "Uplifter",     "Uplift",  0xC276FD, "Noise sweeps that open upward" },
        { "Impact",       "Impact",  0x46DD62, "Sub booms and hits for drops" },
        { "Gate",         "Gate",    0xE4D907, "Tempo-gated pads and chopped textures" },
        { "Glitch",       "Glitch",  0xC1DF0B, "Stuttered, bit-crushed digital chatter" },
        { "Reverse",      "Rev",     0x05B9ED, "Swells that cut off at the peak" },
        // Kicks: their own five hues on the same lightness/chroma curve, half a
        // step off the synth hues so no kick shares a synth's colour.
        { "Full-On Kick",     "Full-On", 0x65DE54, "Punchy and bright, tight for 16th rolls" },
        { "Goa Kick",         "Goa",     0x0BBDE2, "Round and boomy with a soft top" },
        { "Dark Forest Kick", "Forest",  0xFE9C29, "Hard, distorted and short" },
        { "Hi-Tech Kick",     "Hi-Tech", 0xA87EFE, "Very short, clicky and high" },
        { "Progressive Kick", "Prog",    0xFF5A77, "Clean sine body with a deep knock" },
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

// Synth sounds and kicks are separate families: a kick category plays the
// kick engine (EngineMode::Kick) and the sidebar lists one family at a time.
enum class Family : int { Synth, Kick };

constexpr Family familyOf (Category c) noexcept
{
    return (int) c >= (int) Category::KickFullOn ? Family::Kick : Family::Synth;
}

// One family's categories in sidebar (alphabetical) order.
inline const std::vector<Category>& categoriesIn (Family f) noexcept
{
    static const auto lists = [] {
        std::array<std::vector<Category>, 2> l;
        for (Category c : categoryDisplayOrder())
            l[(size_t) familyOf (c)].push_back (c);
        return l;
    }();
    return lists[(size_t) f];
}

constexpr int kNumKickCategories = kNumCategories - (int) Category::KickFullOn;
constexpr int kMaxFamilySize = std::max ((int) Category::KickFullOn, kNumKickCategories);

} // namespace psy
