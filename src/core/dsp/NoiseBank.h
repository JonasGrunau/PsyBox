#pragma once

#include "Common.h"

#include <algorithm>
#include <array>
#include <vector>

namespace psy::dsp
{

// The band-limited read: a Kaiser-windowed sinc of kTaps taps (cut at half
// the source's rate, the window's transition 0.43-0.57 of it, about 90 dB
// down past that), tabled at kPhases fractions and interpolated between them.
// A 6-point Lagrange left its images 30-50 dB down, and a 2x voice folds the
// upper ones back under 20 kHz.
struct SincTable
{
    static constexpr int kTaps = 40;   // x[i - 19] .. x[i + 20]
    static constexpr int kPhases = 256;
    std::vector<float> h, d;           // (kPhases + 1) x kTaps: taps, and the step to the next phase

    SincTable();
    static const SincTable& shared();

    // The taps for a read `f` (0..1) past x[i].
    void taps (float f, float* out) const noexcept
    {
        const float pf = f * (float) kPhases;
        const int p = std::min ((int) pf, kPhases - 1);
        const float t = pf - (float) p;
        const float* a = h.data() + (size_t) p * kTaps;
        const float* b = d.data() + (size_t) p * kTaps;
        for (int k = 0; k < kTaps; ++k) out[k] = a[k] + t * b[k];
    }
};

// A noise sample ready to play: one or two channels at its own rate, stored
// as octave levels (each the one before it band-limited to half and taken at
// half the rate), so a read pitched up takes a level where it cannot alias.
// Every level carries kGuard samples on both ends for the sinc read: the
// other end of the loop for a looping clip, silence for a one-shot.
struct NoiseClip
{
    static constexpr int kGuard = SincTable::kTaps / 2;
    struct Level
    {
        int frames = 0;
        std::vector<float> l, r;   // r empty for a mono clip
        const float* left() const noexcept { return l.data() + kGuard; }
        const float* right() const noexcept { return (r.empty() ? l : r).data() + kGuard; }
    };

    float rate = 48000.f;   // level 0's
    int frames = 0;         // level 0's length
    bool loop = true;       // built as a loop: the levels wrap
    int loopStart = 0;      // a loop plays from 0 to the end once, then [loopStart, end) (a WAV's smpl loop)
    bool stereo = false;
    std::vector<Level> levels;

    // Builds the clip (allocates: never on the audio path). `right` empty for
    // mono. A loop wraps its filters around the end, so it plays seamlessly.
    // Every level holds nothing above 0.42 of its rate; level 0 is filtered
    // there too unless `bandLimited` says it already is.
    static constexpr double kTop = 0.42;
    static NoiseClip make (std::vector<float> left, std::vector<float> right, float sampleRate, bool loop, bool bandLimited = false,
                           int loopStart = 0);

    // The sample at `pos` frames of level 0 (0 <= pos < frames), read from
    // `level` through the sinc table.
    void read (const SincTable& sinc, int level, double pos, float& outL, float& outR) const noexcept
    {
        const auto& lv = levels[(size_t) level];
        const double p = pos / (double) (1 << level);
        const int i = std::min ((int) p, lv.frames - 1);
        float w[SincTable::kTaps];
        sinc.taps ((float) (p - (double) i), w);
        const float* a = lv.left() + i - (SincTable::kTaps / 2 - 1);
        float acc = 0.f;
        for (int k = 0; k < SincTable::kTaps; ++k) acc += a[k] * w[k];
        outL = acc;
        if (! stereo)
        {
            outR = outL;
            return;
        }
        const float* b = lv.right() + i - (SincTable::kTaps / 2 - 1);
        acc = 0.f;
        for (int k = 0; k < SincTable::kTaps; ++k) acc += b[k] * w[k];
        outR = acc;
    }
};

// One voice's read through a clip. `speed` is level-0 frames per output
// sample (the clip's rate over the output's, times the pitch ratio). The read
// takes the octave level whose content still folds above 20 kHz at that
// speed (`foldLimit` = 1 - 20 kHz / output rate, as the wavetables use) and
// blends into the next darker level over the last quarter of that range, so
// a pitch sweep never steps in brightness. A loop wraps unless played
// `once`; a one-shot (or a clip that isn't a loop) falls silent at its end.
class NoiseSampler
{
public:
    // Where a note starts, 0..1 of the clip: Start plus a random share after
    // it (u in 0..1), Random squared as Serum 2 spreads it (50 % covers a
    // quarter), wrapping round.
    static double startFor (float start, float random, float u) noexcept
    {
        return std::fmod ((double) start + (double) random * random * u, 1.0);
    }

    // Where the read starts, 0..1 of the clip.
    void start (const NoiseClip& clip, double where) noexcept
    {
        sinc = &SincTable::shared();
        pos = std::clamp (where, 0.0, 1.0) * (double) clip.frames;
        if (pos >= (double) clip.frames) pos = 0.0;
        done = clip.frames == 0;
    }

    bool finished() const noexcept { return done; }

    void process (const NoiseClip& clip, double speed, float foldLimit, bool once, float& l, float& r) noexcept
    {
        l = r = 0.f;
        if (done || clip.levels.empty())
            return;
        // The fastest a level may be read: its top (kTop of its rate) lands
        // at kTop * speed of the output's rate and folds to 1 - kTop * speed,
        // which must stay above 20 kHz (what the sinc leaves of its images is
        // 90 dB down). x octaves past that on level 0, a level l = ceil (x) is
        // the brightest that may play; the next one blends in over the last
        // quarter octave before the switch.
        const double sMax = std::max (0.5, (double) foldLimit) / NoiseClip::kTop;
        const double x = std::log2 (std::max (speed, 1e-9) / sMax);
        const int top = (int) clip.levels.size() - 1;
        const int level = std::clamp ((int) std::ceil (x), 0, top);
        const float blend = level < top ? (float) std::clamp ((x - level + 0.25) * 4.0, 0.0, 1.0) : 0.f;
        float al, ar;
        clip.read (*sinc, level, pos, al, ar);
        if (blend > 0.f)
        {
            float bl, br;
            clip.read (*sinc, level + 1, pos, bl, br);
            al += (bl - al) * blend;
            ar += (br - ar) * blend;
        }
        l = al;
        r = ar;
        pos += speed;
        const double len = (double) clip.frames;
        if (pos >= len)
        {
            if (clip.loop && ! once)
            {
                const double from = (double) clip.loopStart;
                pos = from + std::fmod (pos - from, len - from);
            }
            else done = true;
        }
    }

private:
    const SincTable* sinc = nullptr;
    double pos = 0.0;
    bool done = true;
};

// PsyBox's own noise samples, synthesised once at startup (as seamless loops
// at 48 kHz, band-limited to 20 kHz, the tonal ones tuned to C so the key
// tracks them) and shared read-only by every voice and instance. Grouped like
// Serum's factory folders: analog noise colours, organic air, inharmonic
// clusters and long evolving atmospheres.
class NoiseBank
{
public:
    static constexpr int kCount = 20;
    enum Sample : int { AnalogPink, AnalogWhite, JunoChorus, BitNoise, Breath, Shush, AirCan, MainsHum, WindChimes, Inharmonic,
                        MetalDrone, Glass, DarkWind, Howl, PolarDrone, Water, Electric, Mantra, Birds, Rain };
    // The ones built on C (a note's pitch makes sense with Key on).
    static constexpr bool tonal (int s) noexcept { return s == MainsHum || s == WindChimes || s == MetalDrone || s == PolarDrone || s == Mantra; }
    static constexpr float kRootHz = 130.8128f;   // C3: a tonal sample plays at its own speed on C3
    // The families, in bank order: where each starts (the last entry ends them).
    static constexpr int kFamilies = 4;
    static constexpr int kFamilyStart[kFamilies + 1] = { 0, 4, 9, 12, kCount };
    static constexpr const char* kFamilyName[kFamilies] = { "Analog", "Organic", "Inharmonic", "Atmosphere" };

    NoiseBank();

    const NoiseClip& clip (int index) const noexcept { return clips[(size_t) std::clamp (index, 0, kCount - 1)]; }
    static const char* name (int index) noexcept;

    static const NoiseBank& shared();

private:
    std::array<NoiseClip, kCount> clips;
};

} // namespace psy::dsp
