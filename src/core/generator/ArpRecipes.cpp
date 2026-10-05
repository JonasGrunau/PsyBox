// Arp recipes, one per synth category, used when "Arp" is ticked next to
// Generate. Basses and kicks have none.
//
// They run after the sound recipe on their own random stream and only touch
// arp parameters, so a seed gives the same sound with or without an arp.
// Each category has a settings recipe (rate, mode, gate...) and a pattern
// recipe that builds its 16 steps: rhythm, accents, slides and a line on the
// note lane, so one held key already plays a riff. The note lane carries
// the line, so the settings keep to one or two octaves. Patterns are written for a 138-148 BPM psy
// grid: step 0 is the beat (where the kick lands) and step 2 of a beat is the
// offbeat "and".

#include "Generator.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

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

    // ---- lines -----------------------------------------------------------------

    // 303 line: long gates tie into slides on the legato voice.
    void acid (B& b)
    {
        b.pick (P::ArpDiv, { D1_16 });
        b.pick (P::ArpMode, { Random, UpDown, Order }, { 0.45f, 0.3f, 0.25f });
        b.pick (P::ArpOctaves, { 1, 2 }, { 0.75f, 0.25f });
        gate (b, 0.5f, 1.3f);
        swing (b, 0.2f);
    }

    void squelch (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_16T }, { 0.7f, 0.3f });
        b.pick (P::ArpMode, { Random, Order }, { 0.7f, 0.3f });
        b.pick (P::ArpOctaves, { 1, 2 }, { 0.75f, 0.25f });
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
        b.pick (P::ArpOctaves, { 1, 2, 3 }, { 0.45f, 0.4f, 0.15f });
        gate (b, 0.4f, 0.8f);
        swing (b, 0.05f);
    }

    // The voice is already gated by its LFO; each step retriggers the LFO, so
    // the arp only moves the chord and keeps the chop on the grid.
    void gated (B& b)
    {
        b.pick (P::ArpDiv, { D1_8, D1_4 }, { 0.6f, 0.4f });
        b.pick (P::ArpMode, { UpDown, Order }, { 0.55f, 0.45f });
        b.pick (P::ArpOctaves, { 1, 2 }, { 0.8f, 0.2f });
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

    // ---- tonal ------------------------------------------------------------------

    void psyLead (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_16T, D1_8T }, { 0.55f, 0.25f, 0.2f });
        b.pick (P::ArpMode, { Up, UpDown, Order }, { 0.35f, 0.4f, 0.25f });
        b.pick (P::ArpOctaves, { 1, 2 }, { 0.6f, 0.4f });
        gate (b, 0.6f, 1.f);
        swing (b, 0.08f);
    }

    void pluck (B& b)
    {
        b.pick (P::ArpDiv, { D1_16, D1_8T }, { 0.7f, 0.3f });
        b.pick (P::ArpMode, { UpDown, Random, Up }, { 0.4f, 0.35f, 0.25f });
        b.pick (P::ArpOctaves, { 1, 2, 3 }, { 0.5f, 0.4f, 0.1f });
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
        b.pick (P::ArpOctaves, { 1, 2, 3 }, { 0.4f, 0.4f, 0.2f });
        gate (b, 0.2f, 0.5f);
        swing (b, 0.08f);
    }

    // ---- talking and liquid -------------------------------------------------------

    void screech (B& b)
    {
        b.pick (P::ArpDiv, { D1_16T, D1_32, D1_16 }, { 0.4f, 0.3f, 0.3f });
        b.pick (P::ArpMode, { Random });
        b.pick (P::ArpOctaves, { 1, 2 }, { 0.7f, 0.3f });
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
        b.pick (P::ArpOctaves, { 1, 2, 3 }, { 0.3f, 0.45f, 0.25f });
        gate (b, 0.15f, 0.4f);
        swing (b, 0.2f);
    }

    void gurgle (B& b)
    {
        b.pick (P::ArpDiv, { D1_32, D1_16T }, { 0.55f, 0.45f });
        b.pick (P::ArpMode, { Random });
        b.pick (P::ArpOctaves, { 1, 2 }, { 0.7f, 0.3f });
        gate (b, 0.5f, 1.f);
        swing (b, 0.1f);
    }

    void wobble (B& b)
    {
        b.pick (P::ArpDiv, { D1_8, D1_4, D1_8T }, { 0.5f, 0.25f, 0.25f });
        b.pick (P::ArpMode, { Order, UpDown }, { 0.55f, 0.45f });
        b.pick (P::ArpOctaves, { 1, 2 }, { 0.7f, 0.3f });
        gate (b, 0.8f, 1.1f);
        swing (b, 0.06f);
    }

    // ---- beds ---------------------------------------------------------------------

    // Overlapping gates let the pad voices ring into each other.
    void atmos (B& b)
    {
        b.pick (P::ArpDiv, { D1_8, D1_8D, D1_4 }, { 0.45f, 0.3f, 0.25f });
        b.pick (P::ArpMode, { UpDown, Random }, { 0.6f, 0.4f });
        b.pick (P::ArpOctaves, { 1, 2 });
        gate (b, 1.f, 1.5f);
        swing (b, 0.05f);
    }

    void drone (B& b)
    {
        b.pick (P::ArpDiv, { D1_4, D1_2, D1_1 }, { 0.4f, 0.35f, 0.25f });
        b.pick (P::ArpMode, { Random, Order }, { 0.6f, 0.4f });
        b.pick (P::ArpOctaves, { 1, 2 }, { 0.7f, 0.3f });
        gate (b, 1.2f, 1.5f);
    }
    // ---- patterns ---------------------------------------------------------------
    //
    // Patterns are composed, not rolled per step: each category has a small
    // vocabulary of figures that work in psy (rolling -xxx, offbeat --x-,
    // 3-3-2, 303 motifs, Euclidean rhythms), arranged in a bar form (AAAA,
    // AAAB, ABAB, A A', a fill into the next bar), then accented and slid by
    // the category's rule, and last given a line on the note lane (see
    // "melody" below). Notation as ArpPattern::parse: - rest, x note,
    // X accent, s slide.

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

    // The figure with its last beat swapped for a fill that pushes into the
    // next bar.
    std::string fill (B& b, std::string cell)
    {
        const char* fills[] = { "xxxx", "x-xx", "xx-x", "-xxx", "x-x-" };
        cell.replace (cell.size() - 4, 4, choose (b, fills, { 0.3f, 0.2f, 0.2f, 0.2f, 0.1f }));
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
            b.set (arpStepParam (i, ArpLane::Note), (float) p.notes[(size_t) i]);
        }
    }

    ArpPattern withLength (ArpPattern p, int length)
    {
        p.length = length;
        return p;
    }

    // The figure started `by` steps later, so it leans off the beat.
    ArpPattern rotated (ArpPattern p, int by)
    {
        auto out = p;
        for (int i = 0; i < kArpSteps; ++i)
            out.steps[(size_t) ((i + by) % kArpSteps)] = p.steps[(size_t) i];
        return out;
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

    // ---- melody -----------------------------------------------------------------
    //
    // The note lane moves each step away from the held key, so a single key
    // plays a line. Lines are written in scale degrees (0 the key, 4 its
    // fifth, 7 the octave, -1 the seventh below) of a scale picked per
    // pattern, so every leap stays in key: Phrygian (the b2 is the psy
    // sound), natural and harmonic minor, Phrygian dominant (the goa one) and
    // Dorian. A line repeats with its figure and answers it the last time
    // round, the way a riff is built.

    using Scale = std::array<int, 7>;
    using Degrees = std::array<int, kArpSteps>;

    Scale scale (B& b)
    {
        const Scale all[] = { { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 },
                              { 0, 1, 4, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 } };
        return choose (b, all, { 0.35f, 0.25f, 0.15f, 0.15f, 0.1f });
    }

    int semitones (const Scale& s, int degree)
    {
        const int octave = (degree >= 0 ? degree : degree - 6) / 7;   // rounds down
        return s[(size_t) (degree - 7 * octave)] + 12 * octave;
    }

    // How a line moves from one note to the next: it stays, steps to a
    // neighbour in the scale (turning back at lo and hi), or leaps to one of
    // `leaps` (degree, weight).
    struct Motion
    {
        float stay, step;
        std::vector<std::pair<int, float>> leaps;
        int lo, hi;
    };

    int leap (B& b, const Motion& m)
    {
        float total = 0.f;
        for (const auto& l : m.leaps) total += l.second;
        float x = b.rnd (0.f, total);
        for (const auto& l : m.leaps)
        {
            if (x < l.second) return l.first;
            x -= l.second;
        }
        return m.leaps.back().first;
    }

    // A leap that lands somewhere else.
    int away (B& b, int from, const Motion& m)
    {
        const int to = leap (b, m);
        return to != from ? to : from + (from < m.hi ? 1 : -1);
    }

    int move (B& b, int from, const Motion& m)
    {
        const float x = b.rnd (0.f, 1.f);
        if (x < m.stay) return from;
        if (x < m.stay + m.step)
        {
            const int to = from + (b.chance (0.5f) ? 1 : -1);
            return to < m.lo || to > m.hi ? 2 * from - to : to;
        }
        return away (b, from, m);
    }

    // A line over the first `period` steps, from the key, moving on each
    // played step (a rest keeps the note before it), then repeated to fill
    // the pattern. A slide wants somewhere to go: it seldom lands on the
    // note it left.
    Degrees line (B& b, const ArpPattern& p, int period, const Motion& m)
    {
        Degrees d {};
        int last = -1;
        for (int i = 0; i < std::min (period, kArpSteps); ++i)
        {
            const int prev = i > 0 ? d[(size_t) i - 1] : 0;
            d[(size_t) i] = prev;
            if (! p.has (i, On)) continue;
            if (last >= 0)
            {
                const bool slidIn = last == i - 1 && p.has (last, Slide);
                int to = move (b, prev, m);
                if (to == prev && slidIn && b.chance (0.7f)) to = away (b, prev, m);
                d[(size_t) i] = to;
            }
            last = i;
        }
        for (int i = period; i < kArpSteps; ++i)
            d[(size_t) i] = d[(size_t) (i - period)];
        return d;
    }

    enum class Answer { Same, Ending, Shift };

    // The last time round, the figure answers the first one: the same again,
    // a new ending (its last two notes), or all of it moved `shift` degrees
    // along the scale (a sequence).
    void answer (B& b, const ArpPattern& p, Degrees& d, int period, const Motion& m, Answer how, int shift)
    {
        const int from = (p.length / period - 1) * period;
        if (from <= 0 || how == Answer::Same) return;
        if (how == Answer::Shift)
        {
            for (int i = from; i < kArpSteps; ++i) d[(size_t) i] += shift;
            return;
        }
        int ending[2] = { -1, -1 }, n = 0;
        for (int i = std::min (from + period, p.length) - 1; i > from && n < 2; --i)
            if (p.has (i, On)) ending[n++] = i;
        for (int k = n - 1; k >= 0; --k)
            d[(size_t) ending[k]] = away (b, d[(size_t) ending[k]], m);
        for (int i = from + 1; i < kArpSteps; ++i)
            if (! p.has (i, On)) d[(size_t) i] = d[(size_t) i - 1];
    }

    Answer answerOf (B& b, float same, float ending, float shift) { return (Answer) b.weighted ({ same, ending, shift }); }

    // Accented notes jump up an octave, the 303 trick; the jumps follow the
    // accent rule (3 against 4 makes them roll across the bar).
    void liftAccents (const ArpPattern& p, Degrees& d)
    {
        for (int i = 0; i < kArpSteps; ++i)
            if (p.has (i, Accent) && d[(size_t) i] < 4) d[(size_t) i] += 7;
    }

    // Roots in degrees for four sections: the key held with a move at the
    // end (to the VII, the Phrygian II), i-i-VI-VII, i-II-i-VII, i-VI-VII-i,
    // i-i-iv-v, i-VII-VI-VII.
    std::array<int, 4> progression (B& b)
    {
        const std::array<int, 4> all[] = { { 0, 0, 0, 0 }, { 0, 0, 0, -1 }, { 0, 0, 0, 1 }, { 0, 0, -2, -1 },
                                           { 0, 1, 0, -1 }, { 0, -2, -1, 0 }, { 0, 0, 3, 4 }, { 0, -1, -2, -1 } };
        return choose (b, all, { 0.1f, 0.15f, 0.15f, 0.17f, 0.13f, 0.1f, 0.1f, 0.1f });
    }

    // Each step moves to the root of its section, `size` steps each.
    void sections (Degrees& d, const std::array<int, 4>& roots, int size)
    {
        for (int i = 0; i < kArpSteps; ++i)
            d[(size_t) i] += roots[(size_t) std::min (3, i / std::max (1, size))];
    }

    void setNotes (ArpPattern& p, const Scale& s, const Degrees& d)
    {
        for (int i = 0; i < kArpSteps; ++i)
            p.notes[(size_t) i] = (int8_t) std::clamp (semitones (s, d[(size_t) i]), -kArpNoteRange, kArpNoteRange);
    }

    // ---- lines -----------------------------------------------------------------

    // 303 line: an 8-step motif and its answer (A A', A B, A A, or a fill
    // into the next bar), accents on a rule (3 against 4, offbeats, motif
    // starts), slides into accents and on the weak 16ths. Now and then a
    // 3-beat loop against the 4/4 bar. The line sits on the key and leaves
    // it for the b2, third, fifth and seventh; accents may jump an octave.
    void acidPattern (B& b)
    {
        const char* motifs[] = { "x-xxx-x-", "xx-x-xx-", "x-x-xxx-", "xxx-x-x-", "x-xx-xx-", "xx-xx-x-", "x-xxx-xx",
                                 "xx-xxx-x", "xxx-xx-x", "x-xxxx-x", "xx-x-xxx", "x--xx-xx", "xx-xx-xx", "x-xx-xxx" };
        const float w[] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
        const std::string a = choose (b, motifs, w);
        const int shape = b.weighted ({ 0.35f, 0.2f, 0.15f, 0.3f });
        const std::string second = shape == 0 ? vary (b, a)
                                 : shape == 1 ? std::string (choose (b, motifs, w))
                                 : shape == 2 ? a : fill (b, a);
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

        const Motion m { 0.3f, 0.2f, { { 0, 5.f }, { 7, 2.5f }, { 4, 1.2f }, { 2, 1.f }, { 1, 1.f }, { -1, 0.8f }, { -3, 0.6f }, { 3, 0.4f }, { 6, 0.4f } }, -3, 9 };
        const Scale s = scale (b);
        auto d = line (b, p, 8, m);
        const auto how = answerOf (b, 0.25f, 0.45f, 0.3f);
        const int shifts[] = { 3, 4, -1, 1 };
        answer (b, p, d, 8, m, how, choose (b, shifts, { 0.35f, 0.25f, 0.2f, 0.2f }));
        if (b.chance (0.5f)) liftAccents (p, d);
        setNotes (p, s, d);
        write (b, p);
    }

    // Offbeat-heavy mid-bass figures, answered once per bar. Mostly the key,
    // with a turn to a neighbour or the fifth.
    void squelchPattern (B& b)
    {
        const char* motifs[] = { "-xx-x-xx", "x-xx-x-x", "-x-xx-x-", "x--xx-x-", "-xxx-x-x", "x-x--xx-", "-x-x-xxx", "xx--x-x-" };
        const float w[] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
        const std::string a = choose (b, motifs, w);
        const int shape = b.weighted ({ 0.45f, 0.25f, 0.3f });
        auto p = ArpPattern::parse (a + (shape == 0 ? vary (b, a) : shape == 1 ? fill (b, a) : a));
        accent (p, b.chance (0.5f) ? Accents::Offbeats : Accents::EveryThird, integer (b, 0, 2));
        slideIntoAccents (p, 1);

        const Motion m { 0.45f, 0.15f, { { 0, 6.f }, { 7, 2.f }, { 1, 1.f }, { 2, 1.f }, { 4, 1.f }, { -1, 0.8f } }, -2, 7 };
        const Scale s = scale (b);
        auto d = line (b, p, 8, m);
        const auto how = answerOf (b, 0.35f, 0.45f, 0.2f);
        const int shifts[] = { -1, 1, 3 };
        answer (b, p, d, 8, m, how, choose (b, shifts, { 0.4f, 0.3f, 0.3f }));
        setNotes (p, s, d);
        write (b, p);
    }

    // ---- hits and fx -----------------------------------------------------------

    // Offbeats, 3-3-2, or two-beat call figures; accent on each half's first
    // hit. The hits jump between the key, its fifth and the octaves, and the
    // second half answers the first.
    void zapPattern (B& b)
    {
        const char* bars[] = { "--x-", "--x---x---x--xx-", "x--x--x-", "---x--x----x--x-", "--x--x----x--x--", "--xx", "x-x---x-x-x---xx" };
        auto p = ArpPattern::parse (choose (b, bars, { 0.22f, 0.15f, 0.15f, 0.12f, 0.12f, 0.12f, 0.12f }));
        for (int half : { 0, 8 })
            for (int i = half; i < half + 8; ++i)
                if (p.has (i, On)) { p.setFlag (i, Accent, true); break; }

        const Motion m { 0.35f, 0.05f, { { 0, 4.f }, { 7, 2.5f }, { 4, 1.5f }, { 2, 1.f }, { -7, 1.f }, { 14, 0.8f } }, -7, 14 };
        const Scale s = scale (b);
        auto d = line (b, p, 8, m);
        const auto how = answerOf (b, 0.3f, 0.3f, 0.4f);
        const int shifts[] = { -1, 3, 4, 7 };
        answer (b, p, d, 8, m, how, choose (b, shifts, { 0.3f, 0.25f, 0.25f, 0.2f }));
        setNotes (p, s, d);
        write (b, p);
    }

    // Dotted figures: 3-3-2, 3-3-3-3-2-2, dotted quarters, or threes running
    // across the bar. Each figure falls from its accent down a chord
    // (octave, fifth, third, key) or the scale; a second 3-3-2 may fall from
    // somewhere else.
    void laserPattern (B& b)
    {
        const char* bars[] = { "X--x--x-", "X--x--x--x--x-x-", "X-----x-----x---", "X--x--x--x--x--x" };
        const int idx = b.weighted ({ 0.4f, 0.25f, 0.15f, 0.2f });
        auto p = ArpPattern::parse (bars[idx]);
        const int tops[] = { 7, 4, 9, 11 }, falls[] = { 2, 1, 3 };
        const int top = choose (b, tops, { 0.4f, 0.25f, 0.2f, 0.15f });
        const int by = choose (b, falls, { 0.5f, 0.3f, 0.2f });
        Degrees d {};
        int next = top;
        for (int i = 0; i < kArpSteps; ++i)
        {
            if (p.has (i, Accent)) next = top;
            if (! p.has (i, On)) { d[(size_t) i] = i > 0 ? d[(size_t) i - 1] : top; continue; }
            d[(size_t) i] = next;
            next = std::max (-7, next - by);
        }
        const int shifts[] = { -1, 1, 2, 3 };
        const int shift = choose (b, shifts, { 0.3f, 0.25f, 0.25f, 0.2f });
        if (idx == 0 && b.chance (0.5f))
            answer (b, p, d, 8, {}, Answer::Shift, shift);
        setNotes (p, scale (b), d);
        write (b, p);
    }

    // ---- tonal ------------------------------------------------------------------

    // One-beat cells in a bar form; the figure slides across its cell
    // boundaries now and then. Each cell plays a broken chord (up, down,
    // the octave pump, a Phrygian turn) on the root of its beat, so the bar
    // can walk a progression.
    void psyLeadPattern (B& b)
    {
        const char* cells[] = { "xxxx", "xxx-", "xx-x", "x-xx", "-xxx" };
        const float w[] = { 0.3f, 0.2f, 0.15f, 0.2f, 0.15f };
        const std::string a = choose (b, cells, w);
        const int shape = b.weighted ({ 0.5f, 0.3f, 0.2f });
        const std::string other = shape == 0 ? vary (b, a) : std::string (choose (b, cells, w));
        const char* forms[] = { "AAAB", "ABAB", "AAAA" };
        auto p = ArpPattern::parse (form (forms[shape], a, other));
        if (b.chance (0.4f))
            for (int i = 3; i < p.length; i += 4)
                if (p.has (i, On) && p.has ((i + 1) % p.length, On)) p.setFlag (i, Slide, true);
        if (b.chance (0.3f)) p.setFlag (0, Accent, true);

        const std::array<int, 4> figures[] = { { 0, 2, 4, 7 }, { 7, 4, 2, 0 }, { 0, 4, 7, 4 }, { 0, 2, 4, 2 }, { 0, 7, 4, 2 },
                                               { 0, 1, 0, -1 }, { 4, 2, 1, 0 }, { 0, 0, 7, 0 }, { 0, 4, 2, 4 } };
        const float fw[] = { 0.15f, 0.12f, 0.12f, 0.1f, 0.12f, 0.1f, 0.1f, 0.09f, 0.1f };
        const auto fa = choose (b, figures, fw);
        const auto fb = b.chance (0.5f) ? fa : choose (b, figures, fw);
        const auto roots = progression (b);
        // the figure's notes go to the cell's played steps in order, so a rest
        // never swallows the octave of the pump
        Degrees d {};
        for (int cell = 0; cell < 4; ++cell)
        {
            const auto& f = forms[shape][cell] == 'A' ? fa : fb;
            for (int i = cell * 4, k = 0; i < cell * 4 + 4; ++i)
                d[(size_t) i] = roots[(size_t) cell] + (p.has (i, On) ? f[(size_t) std::min (k++, 3)] : f[(size_t) std::max (0, k - 1)]);
        }
        setNotes (p, scale (b), d);
        write (b, p);
    }

    // Euclidean figures: tresillo, cinquillo, bossa, 7, 9 and 11 over 16,
    // now and then started off the beat. A singing line walks the scale over
    // them, an 8-step phrase and its answer.
    void pluckPattern (B& b)
    {
        const int figures[][2] = { { 5, 8 }, { 3, 8 }, { 5, 16 }, { 7, 16 }, { 9, 16 }, { 11, 16 } };
        const int idx = b.weighted ({ 0.22f, 0.18f, 0.18f, 0.18f, 0.12f, 0.12f });
        auto p = euclid (figures[idx][0], figures[idx][1]);
        if (b.chance (0.3f)) p = rotated (p, integer (b, 1, 3));
        const int rule = b.weighted ({ 0.5f, 0.2f, 0.3f });
        accent (p, rule == 0 ? Accents::FigureStart : (rule == 1 ? Accents::EveryThird : Accents::None));

        const Motion m { 0.15f, 0.4f, { { 0, 3.f }, { 4, 2.5f }, { 2, 2.f }, { 7, 2.f }, { 3, 1.f }, { 5, 0.8f }, { 6, 0.6f }, { -1, 0.8f }, { -3, 0.8f } }, -3, 9 };
        const Scale s = scale (b);
        auto d = line (b, p, 8, m);
        const auto how = answerOf (b, 0.2f, 0.5f, 0.3f);
        const int shifts[] = { 1, 2, -1, 3 };
        answer (b, p, d, 8, m, how, choose (b, shifts, { 0.3f, 0.25f, 0.25f, 0.2f }));
        setNotes (p, s, d);
        write (b, p);
    }

    // Chord stabs on the offbeat, with a push into the next bar, dotted
    // eighths from the "and" for a rolling polyrhythm, or a roll into the
    // bar line. The chord walks a progression a beat at a time, and the
    // last stab may jump an octave.
    void stabPattern (B& b)
    {
        const char* bars[] = { "--x-", "--x---x---x---xx", "--x--x--x--x--x-", "--x---x---x-x-x-", "-x---x---x---x-x" };
        auto p = ArpPattern::parse (choose (b, bars, { 0.4f, 0.2f, 0.15f, 0.15f, 0.1f }));
        Degrees d {};
        sections (d, progression (b), p.length / 4);
        if (b.chance (0.3f))
            for (int i = p.length - 1; i >= 0; --i)
                if (p.has (i, On)) { d[(size_t) i] += 7; break; }
        setNotes (p, scale (b), d);
        write (b, p);
    }

    // Short bell loops (Euclidean over 10, 12 or 16) with wide, ringing
    // intervals: the b2 and the eleventh as well as fifths and octaves.
    void metallicPattern (B& b)
    {
        const int figures[][2] = { { 5, 12 }, { 7, 12 }, { 5, 16 }, { 7, 16 }, { 3, 10 } };
        const int idx = b.weighted ({ 0.25f, 0.2f, 0.2f, 0.2f, 0.15f });
        auto p = withLength (euclid (figures[idx][0], figures[idx][1]), figures[idx][1]);
        p.setFlag (0, Accent, true);

        const Motion m { 0.15f, 0.1f, { { 0, 3.f }, { 7, 2.f }, { 4, 1.5f }, { 1, 1.2f }, { 8, 1.f }, { 10, 0.8f }, { 14, 0.6f }, { -3, 0.8f } }, -3, 14 };
        const Scale s = scale (b);
        auto d = line (b, p, p.length, m);
        setNotes (p, s, d);
        write (b, p);
    }

    // ---- talking and liquid -------------------------------------------------------

    // Legato screams: slide chains inside each beat, one long run at the end.
    // The chains glide across wide intervals: key, fifth, octave, tenth.
    void screechPattern (B& b)
    {
        const char* cells[] = { "ssx-", "sssx", "x-sx", "xxx-" };
        const float w[] = { 0.35f, 0.25f, 0.2f, 0.2f };
        const std::string a = choose (b, cells, w);
        const int shape = b.weighted ({ 0.5f, 0.3f, 0.2f });
        const std::string other = shape == 0 ? std::string ("sssx") : std::string (choose (b, cells, w));
        auto p = ArpPattern::parse (form (shape == 0 ? "AAAB" : (shape == 1 ? "ABAB" : "AAAA"), a, other));

        const Motion m { 0.1f, 0.15f, { { 0, 3.f }, { 7, 3.f }, { 4, 2.f }, { 9, 1.f }, { 14, 1.f }, { 10, 0.8f }, { -3, 1.f } }, -3, 14 };
        const Scale s = scale (b);
        auto d = line (b, p, 8, m);
        const auto how = answerOf (b, 0.3f, 0.4f, 0.3f);
        const int shifts[] = { 3, 4, -1 };
        answer (b, p, d, 8, m, how, choose (b, shifts, { 0.35f, 0.35f, 0.3f }));
        setNotes (p, s, d);
        write (b, p);
    }

    // Syllables: short slid pairs with gaps, like speech, rising and falling
    // by small steps.
    void alienVoicePattern (B& b)
    {
        const char* cells[] = { "sx--", "x-sx", "ssx-", "x--x" };
        const float w[] = { 0.3f, 0.25f, 0.2f, 0.25f };
        const std::string a = choose (b, cells, w), other = choose (b, cells, w);
        const char* shapes[] = { "ABAB", "AABA", "AAAB" };
        auto p = ArpPattern::parse (form (choose (b, shapes, { 0.4f, 0.3f, 0.3f }), a, other));

        const Motion m { 0.25f, 0.5f, { { 0, 3.f }, { 1, 2.f }, { 2, 1.5f }, { -1, 1.5f }, { 3, 1.f }, { 4, 0.6f } }, -2, 4 };
        const Scale s = scale (b);
        auto d = line (b, p, 8, m);
        answer (b, p, d, 8, m, answerOf (b, 0.4f, 0.6f, 0.f), 0);
        setNotes (p, s, d);
        write (b, p);
    }

    // Short odd loops (Euclidean) drifting against the bar, popping up and
    // down the chord.
    void bubblePattern (B& b)
    {
        const int figures[][2] = { { 3, 5 }, { 4, 7 }, { 3, 7 }, { 5, 7 }, { 5, 9 }, { 7, 16 } };
        const int idx = b.weighted ({ 0.25f, 0.2f, 0.15f, 0.15f, 0.15f, 0.1f });
        auto p = withLength (euclid (figures[idx][0], figures[idx][1]), figures[idx][1]);
        if (b.chance (0.6f)) p.setFlag (0, Accent, true);

        const Motion m { 0.05f, 0.25f, { { 0, 2.f }, { 2, 2.f }, { 4, 2.f }, { 7, 2.f }, { 9, 1.5f }, { 11, 1.2f }, { 14, 1.f } }, 0, 14 };
        const Scale s = scale (b);
        auto d = line (b, p, p.length, m);
        setNotes (p, s, d);
        write (b, p);
    }

    // Slid cells crawling around the key by half and whole steps.
    void gurglePattern (B& b)
    {
        const char* cells[] = { "sxsx", "ssx-", "sxx-", "xsxs" };
        const float w[] = { 0.3f, 0.25f, 0.2f, 0.25f };
        const std::string a = choose (b, cells, w), other = choose (b, cells, w);
        auto p = ArpPattern::parse (form (b.chance (0.5f) ? "AAAB" : "ABAB", a, other));
        if (b.chance (0.25f)) p.length = 12;

        const Motion m { 0.3f, 0.45f, { { 0, 4.f }, { 1, 2.f }, { -1, 2.f }, { 2, 1.f }, { 7, 0.6f } }, -2, 3 };
        const Scale s = scale (b);
        auto d = line (b, p, 8, m);
        answer (b, p, d, 8, m, answerOf (b, 0.5f, 0.5f, 0.f), 0);
        setNotes (p, s, d);
        write (b, p);
    }

    // Half-bar figures on the key, its fifth and octave; the second half
    // moves to another root.
    void wobblePattern (B& b)
    {
        const char* motifs[] = { "x-x-x-xx", "x--x--x-", "sxx-sxx-", "x-xsx-x-" };
        const std::string a = choose (b, motifs, { 0.3f, 0.25f, 0.25f, 0.2f });
        auto p = ArpPattern::parse (a + (b.chance (0.5f) ? vary (b, a) : a));
        if (b.chance (0.4f)) p.length = 8;

        const Motion m { 0.65f, 0.f, { { 0, 4.f }, { 7, 2.f }, { 4, 1.2f }, { -3, 1.f } }, -3, 7 };
        const Scale s = scale (b);
        auto d = line (b, p, 8, m);
        const int roots[] = { 0, -1, 3, 4, -2, 1 };
        const int to = choose (b, roots, { 0.25f, 0.2f, 0.15f, 0.15f, 0.15f, 0.1f });
        for (int i = 8; i < kArpSteps; ++i) d[(size_t) i] += to;
        setNotes (p, s, d);
        write (b, p);
    }

    // The voice's LFO does the chopping; the arp moves the chord on the grid,
    // straight or 3-3-2, through a progression over the pattern's quarters.
    void gatePattern (B& b)
    {
        const char* bars[] = { "x", "x--x--x-", "x-x-" };
        auto p = ArpPattern::parse (choose (b, bars, { 0.55f, 0.3f, 0.15f }));
        Degrees d {};
        sections (d, progression (b), p.length / 4);
        setNotes (p, scale (b), d);
        write (b, p);
    }

    // Stutter bursts: dense cells broken by rests in a bar form, sometimes an
    // odd loop that never lines up with the bar the same way twice. Each
    // cell stutters on one note and the cells jump around it; now and then
    // every cell flips up an octave on its last hit.
    void glitchPattern (B& b)
    {
        const char* cells[] = { "xxxx", "xx-x", "x-xx", "xxx-", "x---" };
        const float w[] = { 0.25f, 0.2f, 0.2f, 0.2f, 0.15f };
        const std::string a = choose (b, cells, w), other = choose (b, cells, w);
        const char* shapes[] = { "AABA", "ABAB", "AAAB" };
        auto p = ArpPattern::parse (form (choose (b, shapes, { 0.4f, 0.3f, 0.3f }), a, other));
        if (b.chance (0.35f)) p.length = b.chance (0.5f) ? 7 : 5;
        accent (p, b.chance (0.5f) ? Accents::EveryThird : Accents::FigureStart, integer (b, 0, 2), 4);

        const Motion m { 0.3f, 0.f, { { 0, 4.f }, { 7, 2.f }, { 4, 1.5f }, { 1, 1.f }, { 14, 0.8f }, { -7, 0.8f }, { 2, 0.6f } }, -7, 14 };
        Degrees d {};
        for (int i = 4; i < kArpSteps; i += 4)
        {
            const int to = move (b, d[(size_t) i - 1], m);
            for (int k = i; k < i + 4; ++k) d[(size_t) k] = to;
        }
        if (b.chance (0.35f))
            for (int i = 3; i < kArpSteps; i += 4) d[(size_t) i] += 7;
        setNotes (p, scale (b), d);
        write (b, p);
    }

    // ---- beds ---------------------------------------------------------------------

    // Every step tied, so the envelopes run through. The pad drifts through
    // a progression, gliding each time the root moves; a short loop moves
    // once, halfway.
    void atmosPattern (B& b)
    {
        auto p = ArpPattern::parse ("s");
        p.length = b.chance (0.5f) ? 16 : 8;
        const int moves[] = { 4, 3, -1, -2, 1 };
        const int move = choose (b, moves, { 0.25f, 0.2f, 0.2f, 0.2f, 0.15f });
        const auto roots = p.length == 8 ? std::array<int, 4> { 0, move, move, move } : progression (b);
        Degrees d {};
        if (! b.chance (0.2f)) sections (d, roots, 4);
        setNotes (p, scale (b), d);
        write (b, p);
    }

    // Tied too, and slower: the drone holds the key and may move once, to
    // the fifth, fourth, seventh, third or octave, halfway through.
    void dronePattern (B& b)
    {
        auto p = ArpPattern::parse ("s");
        p.length = b.chance (0.5f) ? 16 : 8;
        const int moves[] = { 0, 4, 3, -1, 2, 7 };
        const int to = choose (b, moves, { 0.35f, 0.2f, 0.15f, 0.1f, 0.1f, 0.1f });
        Degrees d {};
        for (int i = p.length / 2; i < kArpSteps; ++i) d[(size_t) i] = to;
        setNotes (p, scale (b), d);
        write (b, p);
    }

    // Basses, kicks and energy sounds never get an arp (hasArp): you play
    // their rhythm, or they are one gesture over their length.
    void noArp (B&) {}
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
        case Category::PsyLead:     return psyLeadPattern;
        case Category::Pluck:       return pluckPattern;
        case Category::Stab:        return stabPattern;
        case Category::Screech:     return screechPattern;
        case Category::AlienVoice:  return alienVoicePattern;
        case Category::Bubble:      return bubblePattern;
        case Category::Gurgle:      return gurglePattern;
        case Category::Wobble:      return wobblePattern;
        case Category::Metallic:    return metallicPattern;
        case Category::Gate:        return gatePattern;
        case Category::Glitch:      return glitchPattern;
        case Category::Atmos:       return atmosPattern;
        case Category::Drone:       return dronePattern;
        case Category::Riser:
        case Category::Uplifter:
        case Category::Downlifter:
        case Category::NoiseFx:
        case Category::Impact:
        case Category::Reverse:
        case Category::PsyBass:
        case Category::BassShot:
        case Category::OffbeatBass:
        case Category::ProgBass:
        case Category::ReverseBass:
        case Category::ReverseSub:
        case Category::KickFullOn:
        case Category::KickGoa:
        case Category::KickDarkForest:
        case Category::KickHiTech:
        case Category::KickProgressive: return noArp;
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
        case Category::PsyLead:     return psyLead;
        case Category::Pluck:       return pluck;
        case Category::Stab:        return stab;
        case Category::Screech:     return screech;
        case Category::AlienVoice:  return alienVoice;
        case Category::Bubble:      return bubble;
        case Category::Gurgle:      return gurgle;
        case Category::Wobble:      return wobble;
        case Category::Metallic:    return metallic;
        case Category::Atmos:       return atmos;
        case Category::Drone:       return drone;
        case Category::Gate:        return gated;
        case Category::Glitch:      return glitch;
        case Category::Riser:
        case Category::Downlifter:
        case Category::NoiseFx:
        case Category::Uplifter:
        case Category::Impact:
        case Category::Reverse:
        case Category::PsyBass:
        case Category::BassShot:
        case Category::OffbeatBass:
        case Category::ProgBass:
        case Category::ReverseBass:
        case Category::ReverseSub:
        case Category::KickFullOn:
        case Category::KickGoa:
        case Category::KickDarkForest:
        case Category::KickHiTech:
        case Category::KickProgressive: return noArp;
        case Category::Count:       break;
    }
    return acid;
}

} // namespace psy
