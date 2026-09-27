// Renders a few generated patches per category to WAV files for listening
// checks:  psybox_render <out-dir> [seeds-per-category] [arp]

#include "engine/Engine.h"
#include "generator/Generator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace psy;

namespace
{
    void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int fs)
    {
        FILE* f = std::fopen (path.c_str(), "wb");
        if (! f) return;
        const uint32_t n = (uint32_t) l.size();
        const uint32_t dataBytes = n * 2 * 2;
        const auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
        const auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
        std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
        std::fwrite ("fmt ", 1, 4, f); w32 (16); w16 (1); w16 (2); w32 ((uint32_t) fs); w32 ((uint32_t) fs * 4); w16 (4); w16 (16);
        std::fwrite ("data", 1, 4, f); w32 (dataBytes);
        for (uint32_t i = 0; i < n; ++i)
        {
            const auto s = [] (float x) { return (int16_t) std::lround (std::clamp (x, -1.f, 1.f) * 32767.f); };
            const int16_t a = s (l[i]), b = s (r[i]);
            std::fwrite (&a, 2, 1, f);
            std::fwrite (&b, 2, 1, f);
        }
        std::fclose (f);
    }

    // Pattern per category: basses roll 16ths, FX get one long note, leads a phrase.
    struct Note { double beat, len; int note; float vel; };

    std::vector<Note> patternFor (Category c)
    {
        std::vector<Note> p;
        switch (c)
        {
            case Category::PsyBass:
                for (int b = 0; b < 16; ++b)
                    for (int s = 1; s < 4; ++s) p.push_back ({ b + s * 0.25, 0.18, 36, 0.8f });
                break;
            case Category::Acid:
            case Category::Squelch:
            {
                static const int line[16] = { 36, 36, 48, 39, 36, 43, 36, 46, 36, 48, 36, 41, 39, 36, 51, 36 };
                for (int i = 0; i < 64; ++i)
                    p.push_back ({ i * 0.25, (i % 3 == 0) ? 0.3 : 0.2, line[i % 16], (i % 4 == 2) ? 1.f : 0.7f });
                break;
            }
            case Category::Riser: case Category::Uplifter: case Category::Downlifter: case Category::NoiseFx:
            case Category::Atmos: case Category::Drone:
                p.push_back ({ 0.0, 12.0, 48, 0.8f });
                break;
            case Category::Impact:   // one boom every two bars, long enough to hear the tail
                for (int bar = 0; bar < 4; bar += 2) p.push_back ({ bar * 4.0, 1.0, 36, 0.9f });
                break;
            case Category::Gate:     // held chords, so the chop is what moves
                for (int k : { 60, 63, 67 })
                    for (int half = 0; half < 2; ++half) p.push_back ({ half * 8.0, 7.5, k, 0.8f });
                break;
            case Category::Reverse:  // one swell per bar
                for (int bar = 0; bar < 4; ++bar) p.push_back ({ bar * 4.0, 2.0, 60, 0.8f });
                break;
            case Category::BassShot:   // one shot per beat, E1 E1 G1 D1 (octave -1 in the recipe)
            {
                static const int shots[4] = { 40, 40, 43, 38 };
                for (int b = 0; b < 16; ++b) p.push_back ({ (double) b, 0.95, shots[b % 4], 0.9f });
                break;
            }
            case Category::ProgBass:   // the roll, E1 E1 G1 D1 by the bar (octave -1 in the recipe)
            {
                static const int bars[4] = { 40, 40, 43, 38 };
                for (int b = 0; b < 16; ++b)
                    for (int s = 1; s < 4; ++s) p.push_back ({ b + s * 0.25, 0.2, bars[b / 4], 0.8f });
                break;
            }
            case Category::OffbeatBass:   // one note on every "and", same bar line
            {
                static const int bars[4] = { 40, 40, 43, 38 };
                for (int b = 0; b < 16; ++b) p.push_back ({ b + 0.5, 0.4, bars[b / 4], 0.8f });
                break;
            }
            case Category::KickFullOn: case Category::KickGoa: case Category::KickDarkForest:
            case Category::KickHiTech: case Category::KickProgressive:
                // four on the floor; the key changes every bar (F, F, G, D#) to hear the tracking
                for (int b = 0; b < 16; ++b) p.push_back ({ (double) b, 0.25, (b / 4) == 2 ? 31 : ((b / 4) == 3 ? 27 : 29), 0.9f });
                break;
            default:
            {
                static const int mel[8] = { 60, 63, 67, 70, 72, 67, 63, 58 };
                for (int i = 0; i < 16; ++i) p.push_back ({ i * 1.0, 0.5, mel[i % 8], 0.8f });
                break;
            }
        }
        return p;
    }

    // With the arp on, hold keys for four bars and let the arp play them
    // (synth categories only: basses and kicks keep their own pattern).
    std::vector<Note> arpPatternFor (Category c)
    {
        std::vector<int> keys;
        switch (c)
        {
            case Category::Acid: case Category::Squelch: case Category::Gurgle: keys = { 36, 39, 43 }; break;
            case Category::Riser: case Category::Uplifter: case Category::Downlifter: case Category::NoiseFx:
            case Category::Drone: case Category::Zap: case Category::Laser: keys = { 48 }; break;
            case Category::Impact: keys = { 36 }; break;
            default: keys = { 60, 63, 67 }; break;
        }
        std::vector<Note> p;
        for (int k : keys) p.push_back ({ 0.0, 16.0, k, 0.8f });
        return p;
    }
} // namespace

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    const int seeds = argc > 2 ? std::atoi (argv[2]) : 3;
    const bool withArp = argc > 3 && std::string (argv[3]) == "arp";   // generate with an arp, hold keys
    const int fs = 48000;
    const double bpm = 145.0;
    const double spb = fs * 60.0 / bpm;
    Generator g;

    for (int c = 0; c < kNumCategories; ++c)
        for (int s = 1; s <= seeds; ++s)
        {
            const ParamState p = g.generate ((Category) c, (uint32_t) s * 1009u, ParamState(), LockSet(), withArp);
            Engine e;
            e.setParams (p);
            e.prepare (fs, 256);
            const bool arp = withArp && hasArp (familyOf ((Category) c));
            const auto pattern = arp ? arpPatternFor ((Category) c) : patternFor ((Category) c);
            const int total = (int) (spb * 17);
            std::vector<float> l ((size_t) total), r ((size_t) total);
            std::vector<MidiEvent> all;
            for (const auto& n : pattern)
            {
                all.push_back ({ (int) (n.beat * spb), MidiEvent::NoteOn, n.note, n.vel });
                all.push_back ({ (int) ((n.beat + n.len) * spb), MidiEvent::NoteOff, n.note, 0.f });
            }
            std::sort (all.begin(), all.end(), [] (auto& a, auto& b) { return a.offset < b.offset; });
            size_t ei = 0;
            for (int pos = 0; pos < total; pos += 256)
            {
                const int n = std::min (256, total - pos);
                std::vector<MidiEvent> ev;
                while (ei < all.size() && all[ei].offset < pos + n)
                {
                    auto e2 = all[ei++];
                    e2.offset -= pos;
                    ev.push_back (e2);
                }
                e.process (l.data() + pos, r.data() + pos, n, ev.data(), (int) ev.size(), Transport { bpm, pos / spb, true });
            }
            std::string name = categoryInfo()[(size_t) c].name;
            for (auto& ch : name) if (ch == ' ') ch = '_';
            const std::string path = dir + "/" + name + (arp ? "_arp_" : "_") + std::to_string (s) + ".wav";
            writeWav (path, l, r, fs);
            std::printf ("%s\n", path.c_str());
        }
    return 0;
}
