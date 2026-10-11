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
    BassShot, OffbeatBass, ProgBass, ReverseBass, ReverseSub, Jilax, Grid, GhostStab, GhostBass, Count
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
        { "Squelch",      "Squelch", Fx,         0x65DD5C, "Wet, vocal mid-bass with fast filter motion" },
        { "Zap",          "Zap",     Fx,         0x19D68C, "Short pitch dives for percussive hits" },
        { "Laser",        "Laser",   Fx,         0xC4DB0C, "Sync sweeps and long pitch falls" },
        { "Psy Bass",     "Psy",     Bass,       0x828EFF, "Rolling 16th bass with a knock on every note" },
        { "Psy Lead",     "Lead",    Main,       0xFEA124, "Wide saw and wavetable leads" },
        { "Pluck",        "Pluck",   Main,       0xFE9226, "Percussive tonal plucks" },
        { "Stab",         "Stab",    Main,       0xFEAF1E, "Chord stabs with room around them" },
        { "Screech",      "Screech", Fx,         0x89DF42, "Resonant, distorted screams" },
        { "Alien Voice",  "Alien",   Fx,         0xFBBC13, "Talking formant textures" },
        { "Bubble",       "Bubble",  Fx,         0xF4C70A, "Resonant blips and bloops" },
        { "Gurgle",       "Gurgle",  Fx,         0xD9D716, "Liquid, random filter chatter" },
        { "Wobble",       "Wobble",  Fx,         0x3EDA75, "Tempo-synced morphing tones" },
        { "Metallic",     "Metal",   Fx,         0xA9DE28, "FM bells, combs and clangs" },
        { "Riser",        "Riser",   Energy,     0x08C3CD, "Synth tones climbing in pitch for build-ups" },
        { "Downlifter",   "Down",    Energy,     0x16D19F, "Falling sweeps for drops" },
        { "Noise FX",     "Noise",   Energy,     0x05C9B8, "Filtered noise whooshes and textures" },
        { "Atmos",        "Atmos",   Background, 0x02BDE1, "Evolving pads and air" },
        { "Drone",        "Drone",   Background, 0x09B9EA, "Slow, dark, endless tones" },
        { "Uplifter",     "Uplift",  Energy,     0x01C1D7, "Noise sweeps that open upward" },
        { "Impact",       "Impact",  Energy,     0x07CDAD, "Sub booms and hits for drops" },
        { "Gate",         "Gate",    Background, 0x21B3F3, "Tempo-gated pads and chopped textures" },
        { "Glitch",       "Glitch",  Fx,         0xE9D003, "Stuttered, bit-crushed digital chatter" },
        { "Reverse",      "Rev",     Energy,     0x15C6C2, "Swells that cut off at the peak" },
        { "Full-On Kick",     "Full-On", Kick, 0xC575FB, "Punchy and bright, tight for 16th rolls" },
        { "Goa Kick",         "Goa",     Kick, 0xD771F4, "Round and boomy with a soft top" },
        { "Dark Forest Kick", "Forest",  Kick, 0xB37CFD, "Hard, distorted and short" },
        { "Hi-Tech Kick",     "Hi-Tech", Kick, 0xE76EE9, "Very short, clicky and high" },
        { "Progressive Kick", "Prog",    Kick, 0xF56CDC, "Clean sine body with a deep knock" },
        // Categories added after the kicks (the enum only ever grows).
        { "Bass Shot",    "Shot",    Bass,       0x38ACF9, "Progressive one-shot bass hits, OTT-compressed" },
        { "Offbeat Bass", "Offbeat", Bass,       0x5F9CFE, "One plucked note between the kicks" },
        { "Prog Bass",    "Prog",    Bass,       0x7194FF, "Round, dark progressive bass with a soft knock" },
        { "Reverse Bass", "Rev Bass", Bass,       0x9288FD, "A bass that swells into the next kick and stops dead" },
        { "Reverse Sub",  "Rev Sub",  Bass,       0xA282FD, "A sine sub that swells into the kick, sliding at the end" },
        { "Jilax",        "Jilax",   Main,       0xFE822D, "Wide psytrance synth shots, one or two beats long" },
        { "Grid",         "Grid",    Main,       0xFF7337, "Short FM, pluck and stab hits locked to the 16th grid" },
        { "Ghost Stab",   "Ghost",   Main,       0xFE6445, "Wide fifth stacks on the offbeat, cut at the kick (Ghost Rider)" },
        { "Ghost Bass",   "Ghost",   Bass,       0x4CA4FD, "Rolling prog bass with a quick filter flick on every note (Ghost Rider)" },
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
        || c == Category::ReverseBass || c == Category::ReverseSub || c == Category::GhostBass)
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
