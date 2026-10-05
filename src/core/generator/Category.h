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

// Sidebar sections, in the order they are listed. Synth sounds are split by
// their role in a track, most important first; basses and kicks are one
// section each (their tab shows no captions).
enum class CategoryGroup : int { Main, Shots, Fx, Energy, Background, Bass, Kick, Count };

inline const char* categoryGroupName (CategoryGroup g) noexcept
{
    static constexpr const char* names[] = { "Main", "Shots", "FX", "Energy", "Background", "Bass", "Kick" };
    return names[(size_t) g];
}

struct CategoryInfo
{
    const char* name;
    const char* shortName;   // for compact chips
    CategoryGroup group;     // sidebar section
    uint32_t colour;         // 0xRRGGBB accent
    const char* blurb;
};

inline const std::array<CategoryInfo, kNumCategories>& categoryInfo() noexcept
{
    // Synth accents run as a rainbow down the sidebar list (red -> magenta,
    // through the sections in order and A-Z inside each), generated in OKLCH
    // with hue in even steps and lightness lifted around yellow and lowered
    // around blue, so no accent shouts louder than another.
    // Basses and kicks sit on the half steps between the synth hues, and
    // each of those tabs is a rainbow of its own.
    using enum CategoryGroup;
    static const std::array<CategoryInfo, kNumCategories> info { {
        { "Acid",         "Acid",    Main,       0xFE5654, "303 lines: diode ladder, accent, slide" },
        { "Squelch",      "Squelch", Main,       0xFE882B, "Wet, vocal mid-bass with fast filter motion" },
        { "Zap",          "Zap",     Shots,      0xF3CD02, "Short pitch dives for percussive hits" },
        { "Laser",        "Laser",   Fx,         0x0ED19B, "Sync sweeps and long pitch falls" },
        { "Psy Bass",     "Psy",     Bass,       0xE96DE8, "Rolling 16th bass with a knock on every note" },
        { "Psy Lead",     "Lead",    Main,       0xFE6D3A, "Wide saw and wavetable leads" },
        { "Pluck",        "Pluck",   Shots,      0xFEA227, "Percussive tonal plucks" },
        { "Stab",         "Stab",    Shots,      0xFDBA20, "Chord stabs with room around them" },
        { "Screech",      "Screech", Fx,         0x10C5C4, "Resonant, distorted screams" },
        { "Alien Voice",  "Alien",   Fx,         0xDAD904, "Talking formant textures" },
        { "Bubble",       "Bubble",  Fx,         0xB1DF1D, "Resonant blips and bloops" },
        { "Gurgle",       "Gurgle",  Fx,         0x33D977, "Liquid, random filter chatter" },
        { "Wobble",       "Wobble",  Fx,         0x0EC1D5, "Tempo-synced morphing tones" },
        { "Metallic",     "Metal",   Fx,         0x11CBB2, "FM bells, combs and clangs" },
        { "Riser",        "Riser",   Energy,     0x858BFE, "Synth tones climbing in pitch for build-ups" },
        { "Downlifter",   "Down",    Energy,     0x0FBCE6, "Falling sweeps for drops" },
        { "Noise FX",     "Noise",   Energy,     0x49A5FD, "Filtered noise whooshes and textures" },
        { "Atmos",        "Atmos",   Background, 0xBF77FD, "Evolving pads and air" },
        { "Drone",        "Drone",   Background, 0xDD6FF2, "Slow, dark, endless tones" },
        { "Uplifter",     "Uplift",  Energy,     0xA181FE, "Noise sweeps that open upward" },
        { "Impact",       "Impact",  Energy,     0x23B2F5, "Sub booms and hits for drops" },
        { "Gate",         "Gate",    Background, 0xF56CDC, "Tempo-gated pads and chopped textures" },
        { "Glitch",       "Glitch",  Fx,         0x77DF4A, "Stuttered, bit-crushed digital chatter" },
        { "Reverse",      "Rev",     Energy,     0x6A97FE, "Swells that cut off at the peak" },
        // Kicks: their own five hues on the same lightness/chroma curve.
        { "Full-On Kick",     "Full-On", Kick, 0xFEAF27, "Punchy and bright, tight for 16th rolls" },
        { "Goa Kick",         "Goa",     Kick, 0x56DD61, "Round and boomy with a soft top" },
        { "Dark Forest Kick", "Forest",  Kick, 0xFE6247, "Hard, distorted and short" },
        { "Hi-Tech Kick",     "Hi-Tech", Kick, 0x0EB7EF, "Very short, clicky and high" },
        { "Progressive Kick", "Prog",    Kick, 0xB07CFF, "Clean sine body with a deep knock" },
        // Categories added after the kicks (the enum only ever grows).
        { "Bass Shot",    "Shot",    Bass,       0xFF9527, "Progressive one-shot bass hits, OTT-compressed" },
        { "Offbeat Bass", "Offbeat", Bass,       0x0FD58A, "One plucked note between the kicks" },
        { "Prog Bass",    "Prog",    Bass,       0x5A9EFF, "Round, dark progressive bass with a soft knock" },
    } };
    return info;
}

// Sidebar order: by section, then alphabetical. The enum order stays fixed
// because saved sessions store the category index.
inline const std::array<Category, kNumCategories>& categoryDisplayOrder() noexcept
{
    static const std::array<Category, kNumCategories> order = [] {
        std::array<Category, kNumCategories> o {};
        for (int i = 0; i < kNumCategories; ++i) o[(size_t) i] = (Category) i;
        std::sort (o.begin(), o.end(), [] (Category a, Category b) {
            const auto& x = categoryInfo()[(size_t) a];
            const auto& y = categoryInfo()[(size_t) b];
            if (x.group != y.group) return x.group < y.group;
            return std::string_view (x.name) < std::string_view (y.name);
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

// One family's categories in sidebar order (by section, then alphabetical).
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
