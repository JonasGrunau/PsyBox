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
    Acid, Squelch, Zap, Laser, PsyBass, PsyLead, Pluck, Stab, Screech, AlienVoice,
    Bubble, Gurgle, Wobble, Metallic, Riser, Downlifter, NoiseFx, Atmos, Drone,
    Uplifter, Impact, Gate, Glitch, Reverse,
    KickFullOn, KickGoa, KickDarkForest, KickHiTech, KickProgressive,
    BassShot, OffbeatBass, ProgBass, Count
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
    // Synth accents run as a rainbow down the alphabetical list (red -> magenta),
    // generated in OKLCH with hue in even steps and lightness lifted around
    // yellow and lowered around blue, so no accent shouts louder than another.
    // Basses and kicks sit on the half steps between the synth hues.
    static const std::array<CategoryInfo, kNumCategories> info { {
        { "Acid",         "Acid",    0xFE5654, "303 lines: diode ladder, accent, slide" },
        { "Squelch",      "Squelch", 0x858BFE, "Wet, vocal mid-bass with fast filter motion" },
        { "Zap",          "Zap",     0xF56CDC, "Short pitch dives for percussive hits" },
        { "Laser",        "Laser",   0x0ED19B, "Sync sweeps and long pitch falls" },
        { "Psy Bass",     "Psy",     0x5A9EFF, "Rolling 16th bass with a knock on every note" },
        { "Psy Lead",     "Lead",    0x0FBCE6, "Wide saw and wavetable leads" },
        { "Pluck",        "Pluck",   0x0EC1D5, "Percussive tonal plucks" },
        { "Stab",         "Stab",    0xA181FE, "Chord stabs with room around them" },
        { "Screech",      "Screech", 0x6A97FE, "Resonant, distorted screams" },
        { "Alien Voice",  "Alien",   0xFE6D3A, "Talking formant textures" },
        { "Bubble",       "Bubble",  0xFEA227, "Resonant blips and bloops" },
        { "Gurgle",       "Gurgle",  0x77DF4A, "Liquid, random filter chatter" },
        { "Wobble",       "Wobble",  0xDD6FF2, "Tempo-synced morphing tones" },
        { "Metallic",     "Metal",   0x11CBB2, "FM bells, combs and clangs" },
        { "Riser",        "Riser",   0x49A5FD, "Synth tones climbing in pitch for build-ups" },
        { "Downlifter",   "Down",    0xFDBA20, "Falling sweeps for drops" },
        { "Noise FX",     "Noise",   0x10C5C4, "Filtered noise whooshes and textures" },
        { "Atmos",        "Atmos",   0xFE882B, "Evolving pads and air" },
        { "Drone",        "Drone",   0xF3CD02, "Slow, dark, endless tones" },
        { "Uplifter",     "Uplift",  0xBF77FD, "Noise sweeps that open upward" },
        { "Impact",       "Impact",  0x33D977, "Sub booms and hits for drops" },
        { "Gate",         "Gate",    0xDAD904, "Tempo-gated pads and chopped textures" },
        { "Glitch",       "Glitch",  0xB1DF1D, "Stuttered, bit-crushed digital chatter" },
        { "Reverse",      "Rev",     0x23B2F5, "Swells that cut off at the peak" },
        // Kicks: their own five hues on the same lightness/chroma curve.
        { "Full-On Kick",     "Full-On", 0x56DD61, "Punchy and bright, tight for 16th rolls" },
        { "Goa Kick",         "Goa",     0x0EB7EF, "Round and boomy with a soft top" },
        { "Dark Forest Kick", "Forest",  0xFEAF27, "Hard, distorted and short" },
        { "Hi-Tech Kick",     "Hi-Tech", 0xB07CFF, "Very short, clicky and high" },
        { "Progressive Kick", "Prog",    0xFE6247, "Clean sine body with a deep knock" },
        // Categories added after the kicks (the enum only ever grows).
        { "Bass Shot",    "Shot",    0xFF9527, "Progressive one-shot bass hits, OTT-compressed" },
        { "Offbeat Bass", "Offbeat", 0x0FD58A, "One plucked note between the kicks" },
        { "Prog Bass",    "Prog",    0xE96DE8, "Round, dark progressive bass with a soft knock" },
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

// Synth sounds, basses and kicks are separate families: the sidebar lists one
// family at a time, a kick category plays the kick engine (EngineMode::Kick),
// and only synth sounds get an arp.
enum class Family : int { Synth, Bass, Kick, Count };

constexpr Family familyOf (Category c) noexcept
{
    if ((int) c >= (int) Category::KickFullOn && (int) c <= (int) Category::KickProgressive)
        return Family::Kick;
    if (c == Category::PsyBass || c == Category::BassShot || c == Category::OffbeatBass || c == Category::ProgBass)
        return Family::Bass;
    return Family::Synth;
}

constexpr bool hasArp (Family f) noexcept { return f == Family::Synth; }

// One family's categories in sidebar (alphabetical) order.
inline const std::vector<Category>& categoriesIn (Family f) noexcept
{
    static const auto lists = [] {
        std::array<std::vector<Category>, (size_t) Family::Count> l;
        for (Category c : categoryDisplayOrder())
            l[(size_t) familyOf (c)].push_back (c);
        return l;
    }();
    return lists[(size_t) f];
}

constexpr int familySize (Family f) noexcept
{
    int n = 0;
    for (int i = 0; i < kNumCategories; ++i) n += familyOf ((Category) i) == f ? 1 : 0;
    return n;
}

} // namespace psy
