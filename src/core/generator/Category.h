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
    BassShot, OffbeatBass, ProgBass, ReverseBass, ReverseSub, Jilax, Count
};

constexpr int kNumCategories = (int) Category::Count;

// Sidebar tabs, in the order they are shown. Synth sounds are split by their
// role in a track (leads first, then effects, build-up tools and beds);
// basses and kicks have a tab each.
enum class CategoryGroup : int { Main, Fx, Energy, Background, Bass, Kick, Count };

constexpr int kNumCategoryGroups = (int) CategoryGroup::Count;

inline const char* categoryGroupName (CategoryGroup g) noexcept
{
    static constexpr const char* names[] = { "Main", "FX", "Energy", "Background", "Bass", "Kick" };
    return names[(size_t) g];
}

struct CategoryInfo
{
    const char* name;
    const char* shortName;   // for compact chips
    CategoryGroup group;     // sidebar tab
    uint32_t colour;         // 0xRRGGBB accent
    const char* blurb;
};

inline const std::array<CategoryInfo, kNumCategories>& categoryInfo() noexcept
{
    // The accents run as one rainbow down the whole sidebar: red at the first
    // sound of the first tab, through every tab in order (A-Z inside each),
    // to magenta at the last kick. Generated in OKLCH with hue in even steps
    // (25 -> 335 degrees) and lightness lifted around yellow and lowered
    // around blue, so no accent shouts louder than another.
    using enum CategoryGroup;
    static const std::array<CategoryInfo, kNumCategories> info { {
        { "Acid",         "Acid",    Main,       0xFE5654, "303 lines: diode ladder, accent, slide" },
        { "Squelch",      "Squelch", Fx,         0x87DF42, "Wet, vocal mid-bass with fast filter motion" },
        { "Zap",          "Zap",     Fx,         0x33D97A, "Short pitch dives for percussive hits" },
        { "Laser",        "Laser",   Fx,         0xDED603, "Sync sweeps and long pitch falls" },
        { "Psy Bass",     "Psy",     Bass,       0x7792FF, "Rolling 16th bass with a knock on every note" },
        { "Psy Lead",     "Lead",    Main,       0xFE862B, "Wide saw and wavetable leads" },
        { "Pluck",        "Pluck",   Main,       0xFF7635, "Percussive tonal plucks" },
        { "Stab",         "Stab",    Main,       0xFE9726, "Chord stabs with room around them" },
        { "Screech",      "Screech", Fx,         0xABDE25, "Resonant, distorted screams" },
        { "Alien Voice",  "Alien",   Fx,         0xFEA824, "Talking formant textures" },
        { "Bubble",       "Bubble",  Fx,         0xFDB61D, "Resonant blips and bloops" },
        { "Gurgle",       "Gurgle",  Fx,         0xEDCE11, "Liquid, random filter chatter" },
        { "Wobble",       "Wobble",  Fx,         0x5EDD5F, "Tempo-synced morphing tones" },
        { "Metallic",     "Metal",   Fx,         0xC8DC07, "FM bells, combs and clangs" },
        { "Riser",        "Riser",   Energy,     0x05C4C9, "Synth tones climbing in pitch for build-ups" },
        { "Downlifter",   "Down",    Energy,     0x0BD492, "Falling sweeps for drops" },
        { "Noise FX",     "Noise",   Energy,     0x10CBB3, "Filtered noise whooshes and textures" },
        { "Atmos",        "Atmos",   Background, 0x0EBEDF, "Evolving pads and air" },
        { "Drone",        "Drone",   Background, 0x07B9EA, "Slow, dark, endless tones" },
        { "Uplifter",     "Uplift",  Energy,     0x01C1D4, "Noise sweeps that open upward" },
        { "Impact",       "Impact",  Energy,     0x0CCFA5, "Sub booms and hits for drops" },
        { "Gate",         "Gate",    Background, 0x21B3F4, "Tempo-gated pads and chopped textures" },
        { "Glitch",       "Glitch",  Fx,         0xF8C40B, "Stuttered, bit-crushed digital chatter" },
        { "Reverse",      "Rev",     Energy,     0x04C7BE, "Swells that cut off at the peak" },
        { "Full-On Kick",     "Full-On", Kick, 0xC176FC, "Punchy and bright, tight for 16th rolls" },
        { "Goa Kick",         "Goa",     Kick, 0xD471F6, "Round and boomy with a soft top" },
        { "Dark Forest Kick", "Forest",  Kick, 0xAD7EFD, "Hard, distorted and short" },
        { "Hi-Tech Kick",     "Hi-Tech", Kick, 0xE66EEA, "Very short, clicky and high" },
        { "Progressive Kick", "Prog",    Kick, 0xF56CDC, "Clean sine body with a deep knock" },
        // Categories added after the kicks (the enum only ever grows).
        { "Bass Shot",    "Shot",    Bass,       0x3AABFA, "Progressive one-shot bass hits, OTT-compressed" },
        { "Offbeat Bass", "Offbeat", Bass,       0x50A2FE, "One plucked note between the kicks" },
        { "Prog Bass",    "Prog",    Bass,       0x6499FE, "Round, dark progressive bass with a soft knock" },
        { "Reverse Bass", "Rev Bass", Bass,       0x898BFD, "A bass that swells into the next kick and stops dead" },
        { "Reverse Sub",  "Rev Sub",  Bass,       0x9B85FD, "A sine sub that swells into the kick, sliding at the end" },
        { "Jilax",        "Jilax",   Main,       0xFE6544, "Wide psytrance synth shots, one or two beats long" },
    } };
    return info;
}

inline CategoryGroup groupOf (Category c) noexcept { return categoryInfo()[(size_t) c].group; }

// Sidebar order: by tab, then alphabetical. The enum order stays fixed
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

// One sidebar tab's categories, alphabetical.
inline const std::vector<Category>& categoriesIn (CategoryGroup g) noexcept
{
    static const auto lists = [] {
        std::array<std::vector<Category>, (size_t) CategoryGroup::Count> l;
        for (Category c : categoryDisplayOrder())
            l[(size_t) groupOf (c)].push_back (c);
        return l;
    }();
    return lists[(size_t) g];
}

// Synth sounds, basses and kicks are separate engine families: a kick
// category plays the kick engine (EngineMode::Kick), basses and synth sounds
// the synth engine.
enum class Family : int { Synth, Bass, Kick, Count };

constexpr Family familyOf (Category c) noexcept
{
    if ((int) c >= (int) Category::KickFullOn && (int) c <= (int) Category::KickProgressive)
        return Family::Kick;
    if (c == Category::PsyBass || c == Category::BassShot || c == Category::OffbeatBass || c == Category::ProgBass
        || c == Category::ReverseBass || c == Category::ReverseSub)
        return Family::Bass;
    return Family::Synth;
}

// Energy sounds (risers, downlifters, impacts...) run over a length in bars
// or seconds instead of an arp.
inline bool isEnergy (Category c) noexcept { return groupOf (c) == CategoryGroup::Energy; }

// Sounds that last a set length (Length panel: bars or seconds, Cut at End)
// instead of as long as the key is held: energy sounds, the reverse basses,
// which swell over a beat into the next kick, and the Jilax shots, one or two
// beats each.
inline bool hasLength (Category c) noexcept
{
    return isEnergy (c) || c == Category::ReverseBass || c == Category::ReverseSub || c == Category::Jilax;
}

// Only melodic synth sounds get an arp: basses and kicks play what you play,
// energy sounds are one long gesture and a Jilax shot is one hit.
inline bool hasArp (Category c) noexcept { return familyOf (c) == Family::Synth && ! isEnergy (c) && c != Category::Jilax; }

// One family's categories in sidebar order.
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
