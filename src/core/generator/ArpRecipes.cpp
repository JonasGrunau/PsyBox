// Arp recipes, one per category, used when "Arp" is ticked next to Generate.
//
// They run after the sound recipe on their own random stream and only touch
// arp parameters, so a seed gives the same sound with or without an arp.
// Each category has a settings recipe (rate, mode, gate...) and a pattern
// recipe that builds its 16 steps. Patterns are written for a 138-148 BPM psy
// grid: step 0 is the beat, so a rolling bass leaves steps 0, 4, 8, 12 to the
// kick, and step 2 of a beat is the offbeat "and".

#include "Generator.h"

#include <string>

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

    void gate (B& b, float lo, float hi) { b.uni (P::ArpGate, lo, hi); }
    void swing (B& b, float hi) { b.uni (P::ArpSwing, 0.f, hi); }

    // ---- basses and lines ----------------------------------------------------

    // The psy roll: the kick owns the beat, the bass fills the other three 16ths.
    void rollingBass (B& b)
    {
        b.pick (P::ArpDiv, { D1_16 });
        b.pick (P::ArpMode, { Order });
        b.pick (P::ArpOctaves, { 1, 2 }, { 0.85f, 0.15f });
        gate (b, 0.35f, 0.6f);
        swing (b, 0.06f);
    }

    // 303 line: long gates tie into slides on the legato voice.
    void acid (B& b)
    {
        b.pick (P::ArpDiv, { D1_16 });
        b.pick (P::ArpMode, { Random, UpDown, Order }, { 0.45f, 0.3f, 0.25f });
        b.pick (P::ArpOctaves, { 1, 2 }, { 0.35f, 0.65f });
        gate (b, 0.5f, 1.3f);
        swing (b, 0.2f);
    }

    void squelch (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_16T }, { 0.7f, 0.3f });
        b.pick (P::ArpMode, { Random, Order }, { 0.7f, 0.3f });
        b.pick (P::ArpOctaves, { 1, 2 });
        gate (b, 0.3f, 0.7f);
        swing (b, 0.1f);
    }

    // ---- hits and fx -----------------------------------------------------------

    void zap (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_8 }, { 0.65f, 0.35f });
        b.pick (P::ArpMode, { Down, Random }, { 0.6f, 0.4f });
        b.pick (P::ArpOctaves, { 1, 2 });
        gate (b, 0.15f, 0.4f);
        swing (b, 0.08f);
    }

    // Dotted-eighth falls (3-3-2 on 16ths), the classic psy laser echo.
    void laser (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_8D, D1_8 }, { 0.5f, 0.3f, 0.2f });
        b.pick (P::ArpMode, { Down });
        b.pick (P::ArpOctaves, { 2, 3 });
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
        gate (b, 1.f, 1.3f);
    }

    void downlifter (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_16T });
        b.pick (P::ArpMode, { Down });
        b.pick (P::ArpOctaves, { 3, 4 });
        gate (b, 1.f, 1.3f);
    }

    void noiseFx (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_8, D1_16T }, { 0.5f, 0.3f, 0.2f });
        b.pick (P::ArpMode, { Random, Up }, { 0.6f, 0.4f });
        b.pick (P::ArpOctaves, { 2, 3 });
        gate (b, 1.f, 1.3f);
    }

    void uplifter (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_16T }, { 0.75f, 0.25f });
        b.pick (P::ArpMode, { Up });
        b.pick (P::ArpOctaves, { 3, 4 });
        gate (b, 1.f, 1.3f);
    }

    // A boom on the one: quarter-note steps, the pattern keeps them sparse.
    void impact (B& b)
    {
        b.pick (P::ArpDiv, { D1_4 });
        b.pick (P::ArpMode, { Down, Order }, { 0.6f, 0.4f });
        b.pick (P::ArpOctaves, { 1 });
        gate (b, 1.2f, 1.5f);
    }

    // The voice is already gated by its LFO; each step retriggers the LFO, so
    // the arp only moves the chord and keeps the chop on the grid.
    void gated (B& b)
    {
        b.pick (P::ArpDiv, { D1_8, D1_4 }, { 0.6f, 0.4f });
        b.pick (P::ArpMode, { UpDown, Order }, { 0.55f, 0.45f });
        b.pick (P::ArpOctaves, { 1, 2 }, { 0.6f, 0.4f });
        gate (b, 0.9f, 1.2f);
        swing (b, 0.05f);
    }

    void glitch (B& b)
    {
        b.pick (P::ArpDiv, { D1_32, D1_16T, D1_16 }, { 0.45f, 0.3f, 0.25f });
        b.pick (P::ArpMode, { Random });
        b.pick (P::ArpOctaves, { 1, 2 });
        gate (b, 0.2f, 0.6f);
        swing (b, 0.1f);
    }

    // Each step swells for its length and is cut when the next one starts.
    void reverse (B& b)
    {
        b.pick (P::ArpDiv, { D1_4, D1_2, D1_8D }, { 0.5f, 0.3f, 0.2f });
        b.pick (P::ArpMode, { Up, UpDown }, { 0.65f, 0.35f });
        b.pick (P::ArpOctaves, { 1, 2 });
        gate (b, 1.f, 1.2f);
    }

    // ---- tonal ------------------------------------------------------------------

    void psyLead (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_16T, D1_8T }, { 0.55f, 0.25f, 0.2f });
        b.pick (P::ArpMode, { Up, UpDown, Order }, { 0.35f, 0.4f, 0.25f });
        b.pick (P::ArpOctaves, { 2, 3 }, { 0.6f, 0.4f });
        gate (b, 0.6f, 1.f);
        swing (b, 0.08f);
    }

    void pluck (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_8T }, { 0.7f, 0.3f });
        b.pick (P::ArpMode, { UpDown, Random, Up }, { 0.4f, 0.35f, 0.25f });
        b.pick (P::ArpOctaves, { 2, 3 });
        gate (b, 0.3f, 0.6f);
        swing (b, 0.1f);
    }

    // Offbeat chord stabs: 1/16 with Offbeat hits the "and" of every beat.
    void stab (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_8 }, { 0.65f, 0.35f });
        b.pick (P::ArpMode, { Order, Up }, { 0.7f, 0.3f });
        b.pick (P::ArpOctaves, { 1 });
        gate (b, 0.3f, 0.6f);
        swing (b, 0.05f);
    }

    void metallic (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_8T }, { 0.65f, 0.35f });
        b.pick (P::ArpMode, { Random, Up }, { 0.6f, 0.4f });
        b.pick (P::ArpOctaves, { 2, 3 });
        gate (b, 0.2f, 0.5f);
        swing (b, 0.08f);
    }

    // ---- talking and liquid -------------------------------------------------------

    void screech (B& b)
    {
        b.pick (P::ArpDiv, { D1_16T, D1_32, D1_16 }, { 0.4f, 0.3f, 0.3f });
        b.pick (P::ArpMode, { Random });
        b.pick (P::ArpOctaves, { 1, 2 });
        gate (b, 0.7f, 1.f);
        swing (b, 0.06f);
    }

    void alienVoice (B& b)
    {
        b.pick (P::ArpDiv, { D1_8T, D1_16 }, { 0.5f, 0.5f });
        b.pick (P::ArpMode, { Random });
        b.pick (P::ArpOctaves, { 1 });
        gate (b, 0.8f, 1.2f);
        swing (b, 0.2f);
    }

    void bubble (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_32, D1_16T }, { 0.5f, 0.25f, 0.25f });
        b.pick (P::ArpMode, { Random, Up }, { 0.65f, 0.35f });
        b.pick (P::ArpOctaves, { 2, 3, 4 }, { 0.4f, 0.4f, 0.2f });
        gate (b, 0.15f, 0.4f);
        swing (b, 0.2f);
    }

    void gurgle (B& b)
    {
        b.pick (P::ArpDiv, { D1_32, D1_16T }, { 0.55f, 0.45f });
        b.pick (P::ArpMode, { Random });
        b.pick (P::ArpOctaves, { 1, 2 }, { 0.6f, 0.4f });
        gate (b, 0.5f, 1.f);
        swing (b, 0.1f);
    }

    void wobble (B& b)
    {
        b.pick (P::ArpDiv, { D1_8, D1_4, D1_8T }, { 0.5f, 0.25f, 0.25f });
        b.pick (P::ArpMode, { Order, UpDown }, { 0.55f, 0.45f });
        b.pick (P::ArpOctaves, { 1, 2 });
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
        gate (b, 1.f, 1.5f);
        swing (b, 0.05f);
    }

    void drone (B& b)
    {
        b.pick (P::ArpDiv, { D1_4, D1_2, D1_1 }, { 0.4f, 0.35f, 0.25f });
        b.pick (P::ArpMode, { Random, Order }, { 0.6f, 0.4f });
        b.pick (P::ArpOctaves, { 1, 2 });
        gate (b, 1.2f, 1.5f);
    }
    // Kicks: straight 16ths so the pattern owns the groove; short gates, no
    // swing. The kick engine never ties steps (see ArpSettings::from).
    void kick (B& b)
    {
        b.pick (P::ArpDiv, { D1_16 });
        b.pick (P::ArpMode, { Order });
        b.pick (P::ArpOctaves, { 1 });
        gate (b, 0.3f, 0.5f);
    }

    // ---- patterns ---------------------------------------------------------------
    //
    // Patterns are composed, not rolled per step: each category has a small
    // vocabulary of figures that work in psy (rolling -xxx, offbeat --x-,
    // 3-3-2, 303 motifs, Euclidean rhythms), arranged in a bar form (AAAA,
    // AAAB, ABAB, A A'), then accented and slid by the category's rule.
    // Notation as ArpPattern::parse: - rest, x note, X accent, s slide.

    constexpr auto On = ArpPattern::On, Accent = ArpPattern::Accent, Slide = ArpPattern::Slide;

    template <typename T, size_t N>
    T choose (B& b, const T (&items)[N], const float (&weights)[N])
    {
        float total = 0.f;
        for (float w : weights) total += w;
        float x = b.rnd (0.f, total);
        for (size_t i = 0; i < N; ++i)
        {
            if (x < weights[i]) return items[i];
            x -= weights[i];
        }
        return items[N - 1];
    }

    int integer (B& b, int lo, int hi) { return std::min (hi, lo + (int) b.rnd (0.f, (float) (hi - lo + 1))); }

    // Cells laid out by a form: 'A' and 'B' each stand for one cell.
    std::string form (std::string_view shape, std::string_view a, std::string_view b)
    {
        std::string out;
        for (char c : shape) out += c == 'A' ? a : b;
        return out;
    }

    // A': the same figure with its ending changed, the usual way a bar turns
    // around. Flips one of the last two steps, keeping a note on the first.
    std::string vary (B& b, std::string cell)
    {
        const int n = (int) cell.size();
        const int at = b.chance (0.6f) ? n - 1 : n - 2;
        char& c = cell[(size_t) std::max (1, at)];
        c = c == '-' ? 'x' : '-';
        return cell;
    }

    enum class Accents { None, Offbeats, EveryThird, FigureStart, BarEnd };

    // Accents follow a rule, so they repeat with the figure.
    void accent (ArpPattern& p, Accents rule, int offset = 0, int figure = 8)
    {
        for (int i = 0; i < p.length; ++i)
        {
            if (! p.has (i, On)) continue;
            bool hit = false;
            switch (rule)
            {
                case Accents::None:        break;
                case Accents::Offbeats:    hit = i % 4 == 2; break;
                case Accents::EveryThird:  hit = (i + offset) % 3 == 0; break;   // 3 against 4, the rolling acid accent
                case Accents::FigureStart: hit = i % figure == 0; break;
                case Accents::BarEnd:      hit = i == p.length - 1; break;
            }
            if (hit) p.setFlag (i, Accent, true);
        }
    }

    // Slides into accented notes: the 303 move. At most `max` per bar.
    void slideIntoAccents (ArpPattern& p, int max)
    {
        for (int i = 0; i < p.length && max > 0; ++i)
        {
            const int next = (i + 1) % p.length;
            if (p.has (i, On) && ! p.has (i, Accent) && p.has (next, On) && p.has (next, Accent) && ! p.has (i, Slide))
            {
                p.setFlag (i, Slide, true);
                --max;
            }
        }
    }

    // Slides on the weak 16ths (the "e" and "a" of the beat) into a following note.
    void slideOnWeakSteps (B& b, ArpPattern& p, int max, float probability)
    {
        int count = 0;
        for (int i = 0; i < p.length; ++i)
            count += p.has (i, Slide) ? 1 : 0;
        for (int i = 1; i < p.length && count < max; i += 2)
            if (p.has (i, On) && p.has ((i + 1) % p.length, On) && ! p.has (i, Slide) && b.chance (probability))
            {
                p.setFlag (i, Slide, true);
                ++count;
            }
    }

    // A slide must land on a note; one before a rest is dropped.
    void landSlides (ArpPattern& p)
    {
        for (int i = 0; i < p.length; ++i)
            if (p.has (i, Slide) && ! p.has ((i + 1) % p.length, On)) p.setFlag (i, Slide, false);
    }

    // Recorded through the builder, so the pattern is part of the recipe's
    // fixed values: Mutate leaves it alone, Generate in the editor rerolls it.
    void write (B& b, ArpPattern p)
    {
        landSlides (p);
        b.set (P::ArpLength, (float) p.length);
        for (int i = 0; i < kArpSteps; ++i)
        {
            b.set (arpStepParam (i, ArpLane::On), p.has (i, On) ? 1.f : 0.f);
            b.set (arpStepParam (i, ArpLane::Accent), p.has (i, Accent) ? 1.f : 0.f);
            b.set (arpStepParam (i, ArpLane::Slide), p.has (i, Slide) ? 1.f : 0.f);
        }
    }

    ArpPattern withLength (ArpPattern p, int length)
    {
        p.length = length;
        return p;
    }

    // `hits` spread as evenly as possible over `n` steps (Euclidean rhythm),
    // starting on a hit and repeated to fill 16 steps. E(3,8) is the tresillo,
    // E(5,8) the cinquillo, E(5,16) the bossa.
    ArpPattern euclid (int hits, int n)
    {
        ArpPattern p;
        for (int i = 0; i < kArpSteps; ++i)
            if (((i % n) * hits) % n < hits) p.setFlag (i, On, true);
        return p;
    }

    // ---- basses and lines ----------------------------------------------------

    // The kick owns every beat. Main figure: the roll (-xxx), sometimes the
    // progressive offbeat bass (--x-); the last beat may turn around.
    void rollingBassPattern (B& b)
    {
        const char* mains[] = { "-xxx", "--x-", "-x-x" };
        const std::string main = choose (b, mains, { 0.8f, 0.12f, 0.08f });
        const char* turns[] = { "-x-x", "--xx", "-xx-", "-xxX" };
        const std::string turn = main == "--x-" ? std::string ("--xx") : std::string (choose (b, turns, { 0.35f, 0.25f, 0.2f, 0.2f }));
        const char* shapes[] = { "AAAA", "AAAB", "ABAB" };
        auto p = ArpPattern::parse (form (choose (b, shapes, { 0.5f, 0.4f, 0.1f }), main, turn));
        const Accents rules[] = { Accents::None, Accents::Offbeats, Accents::BarEnd };
        accent (p, choose (b, rules, { 0.55f, 0.3f, 0.15f }));
        write (b, p);
    }

    // 303 line: an 8-step motif and its answer (A A', A B or A A), accents on
    // a rule (3 against 4, offbeats, motif starts), slides into accents and on
    // the weak 16ths. Now and then a 3-beat loop against the 4/4 bar.
    void acidPattern (B& b)
    {
        const char* motifs[] = { "x-xxx-x-", "xx-x-xx-", "x-x-xxx-", "xxx-x-x-", "x-xx-xx-", "xx-xx-x-", "x-xxx-xx", "xx-xxx-x" };
        const float w[] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
        const std::string a = choose (b, motifs, w);
        const int shape = b.weighted ({ 0.5f, 0.25f, 0.25f });
        const std::string second = shape == 0 ? vary (b, a) : (shape == 1 ? std::string (choose (b, motifs, w)) : a);
        auto p = ArpPattern::parse (a + second);
        if (b.chance (0.15f)) p.length = 12;

        const int rule = b.weighted ({ 0.4f, 0.35f, 0.25f });
        if (rule == 0) accent (p, Accents::EveryThird, integer (b, 0, 2));
        else if (rule == 1) accent (p, Accents::Offbeats);
        else { accent (p, Accents::FigureStart); if (p.has (6, On)) p.setFlag (6, Accent, true); if (p.has (14, On)) p.setFlag (14, Accent, true); }
        // a motif resting on every offbeat gets its accent on the first note
        bool any = false;
        for (int i = 0; i < p.length; ++i) any = any || p.has (i, Accent);
        if (! any) p.setFlag (0, Accent, true);

        slideIntoAccents (p, 2);
        slideOnWeakSteps (b, p, integer (b, 2, 4), 0.35f);
        write (b, p);
    }

    // Offbeat-heavy mid-bass figures, answered once per bar.
    void squelchPattern (B& b)
    {
        const char* motifs[] = { "-xx-x-xx", "x-xx-x-x", "-x-xx-x-", "x--xx-x-", "-xxx-x-x" };
        const float w[] = { 1.f, 1.f, 1.f, 1.f, 1.f };
        const std::string a = choose (b, motifs, w);
        auto p = ArpPattern::parse (a + (b.chance (0.6f) ? vary (b, a) : a));
        accent (p, b.chance (0.5f) ? Accents::Offbeats : Accents::EveryThird, integer (b, 0, 2));
        slideIntoAccents (p, 1);
        write (b, p);
    }

    // ---- hits and fx -----------------------------------------------------------

    // Offbeats, 3-3-2, or two-beat call figures; accent on each half's first hit.
    void zapPattern (B& b)
    {
        const char* bars[] = { "--x-", "--x---x---x--xx-", "x--x--x-", "---x--x----x--x-", "--x--x----x--x--" };
        auto p = ArpPattern::parse (choose (b, bars, { 0.3f, 0.2f, 0.2f, 0.15f, 0.15f }));
        for (int half : { 0, 8 })
            for (int i = half; i < half + 8; ++i)
                if (p.has (i, On)) { p.setFlag (i, Accent, true); break; }
        write (b, p);
    }

    // Dotted figures: 3-3-2, 3-3-3-3-2-2, or dotted quarters.
    void laserPattern (B& b)
    {
        const char* bars[] = { "X--x--x-", "X--x--x--x--x-x-", "X-----x-----x---" };
        write (b, ArpPattern::parse (choose (b, bars, { 0.5f, 0.3f, 0.2f })));
    }

    void riserPattern (B& b)
    {
        auto p = ArpPattern::parse ("s");
        p.length = b.chance (0.5f) ? 16 : 8;
        write (b, p);
    }

    // ---- tonal ------------------------------------------------------------------

    // One-beat cells in a bar form; the figure slides across its cell
    // boundaries now and then.
    void psyLeadPattern (B& b)
    {
        const char* cells[] = { "xxxx", "xxx-", "xx-x", "x-xx" };
        const float w[] = { 0.3f, 0.25f, 0.2f, 0.25f };
        const std::string a = choose (b, cells, w);
        const int shape = b.weighted ({ 0.5f, 0.3f, 0.2f });
        const std::string other = shape == 0 ? vary (b, a) : std::string (choose (b, cells, w));
        auto p = ArpPattern::parse (form (shape == 0 ? "AAAB" : (shape == 1 ? "ABAB" : "AAAA"), a, other));
        if (b.chance (0.4f))
            for (int i = 3; i < p.length; i += 4)
                if (p.has (i, On) && p.has ((i + 1) % p.length, On)) p.setFlag (i, Slide, true);
        if (b.chance (0.3f)) p.setFlag (0, Accent, true);
        write (b, p);
    }

    // Euclidean figures: tresillo, cinquillo, bossa, 7 and 9 over 16.
    void pluckPattern (B& b)
    {
        const int figures[][2] = { { 5, 8 }, { 3, 8 }, { 5, 16 }, { 7, 16 }, { 9, 16 } };
        const int idx = b.weighted ({ 0.25f, 0.2f, 0.2f, 0.2f, 0.15f });
        auto p = euclid (figures[idx][0], figures[idx][1]);
        const int rule = b.weighted ({ 0.5f, 0.2f, 0.3f });
        accent (p, rule == 0 ? Accents::FigureStart : (rule == 1 ? Accents::EveryThird : Accents::None));
        write (b, p);
    }

    // Chord stabs on the offbeat, with a push into the next bar, or dotted
    // eighths from the "and" for a rolling polyrhythm.
    void stabPattern (B& b)
    {
        const char* bars[] = { "--x-", "--x---x---x---xx", "--x--x--x--x--x-" };
        write (b, ArpPattern::parse (choose (b, bars, { 0.55f, 0.25f, 0.2f })));
    }

    void metallicPattern (B& b)
    {
        const int figures[][2] = { { 5, 12 }, { 7, 12 }, { 5, 16 }, { 7, 16 }, { 3, 10 } };
        const int idx = b.weighted ({ 0.25f, 0.2f, 0.2f, 0.2f, 0.15f });
        auto p = withLength (euclid (figures[idx][0], figures[idx][1]), figures[idx][1]);
        p.setFlag (0, Accent, true);
        write (b, p);
    }

    // ---- talking and liquid -------------------------------------------------------

    // Legato screams: slide chains inside each beat, one long run at the end.
    void screechPattern (B& b)
    {
        const char* cells[] = { "ssx-", "sssx", "x-sx", "xxx-" };
        const float w[] = { 0.35f, 0.25f, 0.2f, 0.2f };
        const std::string a = choose (b, cells, w);
        const int shape = b.weighted ({ 0.5f, 0.3f, 0.2f });
        const std::string other = shape == 0 ? std::string ("sssx") : std::string (choose (b, cells, w));
        write (b, ArpPattern::parse (form (shape == 0 ? "AAAB" : (shape == 1 ? "ABAB" : "AAAA"), a, other)));
    }

    // Syllables: short slid pairs with gaps, like speech.
    void alienVoicePattern (B& b)
    {
        const char* cells[] = { "sx--", "x-sx", "ssx-", "x--x" };
        const float w[] = { 0.3f, 0.25f, 0.2f, 0.25f };
        const std::string a = choose (b, cells, w), other = choose (b, cells, w);
        const char* shapes[] = { "ABAB", "AABA", "AAAB" };
        write (b, ArpPattern::parse (form (choose (b, shapes, { 0.4f, 0.3f, 0.3f }), a, other)));
    }

    // Short odd loops (Euclidean) drifting against the bar.
    void bubblePattern (B& b)
    {
        const int figures[][2] = { { 3, 5 }, { 4, 7 }, { 3, 7 }, { 5, 7 }, { 5, 9 }, { 7, 16 } };
        const int idx = b.weighted ({ 0.25f, 0.2f, 0.15f, 0.15f, 0.15f, 0.1f });
        auto p = withLength (euclid (figures[idx][0], figures[idx][1]), figures[idx][1]);
        if (b.chance (0.6f)) p.setFlag (0, Accent, true);
        write (b, p);
    }

    void gurglePattern (B& b)
    {
        const char* cells[] = { "sxsx", "ssx-", "sxx-", "xsxs" };
        const float w[] = { 0.3f, 0.25f, 0.2f, 0.25f };
        const std::string a = choose (b, cells, w), other = choose (b, cells, w);
        auto p = ArpPattern::parse (form (b.chance (0.5f) ? "AAAB" : "ABAB", a, other));
        if (b.chance (0.25f)) p.length = 12;
        write (b, p);
    }

    void wobblePattern (B& b)
    {
        const char* motifs[] = { "x-x-x-xx", "x--x--x-", "sxx-sxx-", "x-xsx-x-" };
        const std::string a = choose (b, motifs, { 0.3f, 0.25f, 0.25f, 0.2f });
        auto p = ArpPattern::parse (a + (b.chance (0.5f) ? vary (b, a) : a));
        if (b.chance (0.4f)) p.length = 8;
        write (b, p);
    }

    // One boom on the one (quarter-note steps), now and then every two bars
    // or with a pickup into the next bar.
    // Four on the floor, sometimes with a fill in the last beat. No accents
    // (every kick at the same level) and no slides.
    void kickPattern (B& b)
    {
        const char* fills[] = { "x---", "x-x-", "x--x", "x-xx" };
        const std::string fill = choose (b, fills, { 0.4f, 0.25f, 0.2f, 0.15f });
        const char* shapes[] = { "AAAA", "AAAB" };
        write (b, ArpPattern::parse (form (choose (b, shapes, { 0.6f, 0.4f }), "x---", fill)));
    }

    void impactPattern (B& b)
    {
        const char* bars[] = { "X---", "X-------", "X------x", "X-----x-" };
        write (b, ArpPattern::parse (choose (b, bars, { 0.5f, 0.25f, 0.15f, 0.1f })));
    }

    // The voice's LFO does the chopping; the arp moves the chord on the grid,
    // straight or 3-3-2.
    void gatePattern (B& b)
    {
        const char* bars[] = { "x", "x--x--x-", "x-x-" };
        write (b, ArpPattern::parse (choose (b, bars, { 0.55f, 0.3f, 0.15f })));
    }

    // Stutter bursts: dense cells broken by rests in a bar form, sometimes an
    // odd loop that never lines up with the bar the same way twice.
    void glitchPattern (B& b)
    {
        const char* cells[] = { "xxxx", "xx-x", "x-xx", "xxx-", "x---" };
        const float w[] = { 0.25f, 0.2f, 0.2f, 0.2f, 0.15f };
        const std::string a = choose (b, cells, w), other = choose (b, cells, w);
        const char* shapes[] = { "AABA", "ABAB", "AAAB" };
        auto p = ArpPattern::parse (form (choose (b, shapes, { 0.4f, 0.3f, 0.3f }), a, other));
        if (b.chance (0.35f)) p.length = b.chance (0.5f) ? 7 : 5;
        accent (p, b.chance (0.5f) ? Accents::EveryThird : Accents::FigureStart, integer (b, 0, 2), 4);
        write (b, p);
    }

    // Each step swells for its length and is cut by the next: straight, or a
    // tresillo for a lopsided suck-back.
    void reversePattern (B& b)
    {
        write (b, b.chance (0.6f) ? ArpPattern::parse ("x") : euclid (3, 8));
    }

    // Sweeps and beds: every step tied, so the envelopes run through.
    void sweepPattern (B& b) { riserPattern (b); }
} // namespace

const std::vector<ArpTemplate>& arpTemplates() noexcept
{
    static const std::vector<ArpTemplate> t {
        { "Straight", "x" },
        { "Rolling", "-xxx" },
        { "Rolling, pushed", "-xXx" },
        { "Gallop", "x-xx" },
        { "Offbeat", "--x-" },
        { "Skip", "xx-x" },
        { "3-3-2", "x--x--x-" },
        { "Broken", "x-xx-x-xx-x-xxx-" },
        { "Acid line", "Xxs-xXs-xx-Xsx-x" },
        { "Tied", "s" },
    };
    return t;
}

Recipe arpPatternRecipeFor (Category c) noexcept
{
    switch (c)
    {
        case Category::Acid:        return acidPattern;
        case Category::Squelch:     return squelchPattern;
        case Category::Zap:         return zapPattern;
        case Category::Laser:       return laserPattern;
        case Category::RollingBass: return rollingBassPattern;
        case Category::PsyLead:     return psyLeadPattern;
        case Category::Pluck:       return pluckPattern;
        case Category::Stab:        return stabPattern;
        case Category::Screech:     return screechPattern;
        case Category::AlienVoice:  return alienVoicePattern;
        case Category::Bubble:      return bubblePattern;
        case Category::Gurgle:      return gurglePattern;
        case Category::Wobble:      return wobblePattern;
        case Category::Metallic:    return metallicPattern;
        case Category::Impact:      return impactPattern;
        case Category::Gate:        return gatePattern;
        case Category::Glitch:      return glitchPattern;
        case Category::Reverse:     return reversePattern;
        case Category::Riser:
        case Category::Uplifter:
        case Category::Downlifter:
        case Category::NoiseFx:
        case Category::Atmos:
        case Category::Drone:       return sweepPattern;
        case Category::KickFullOn:
        case Category::KickGoa:
        case Category::KickDarkForest:
        case Category::KickHiTech:
        case Category::KickProgressive: return kickPattern;
        case Category::Count:       break;
    }
    return acidPattern;
}

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
        case Category::Uplifter:    return uplifter;
        case Category::Impact:      return impact;
        case Category::Gate:        return gated;
        case Category::Glitch:      return glitch;
        case Category::Reverse:     return reverse;
        case Category::KickFullOn:
        case Category::KickGoa:
        case Category::KickDarkForest:
        case Category::KickHiTech:
        case Category::KickProgressive: return kick;
        case Category::Count:       break;
    }
    return acid;
}

} // namespace psy
