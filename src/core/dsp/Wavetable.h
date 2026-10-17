#pragma once

#include <array>
#include <vector>

namespace psy::dsp
{

// A set of procedurally designed wavetables, each with kFrames single-cycle
// frames, each frame stored at kLevels band limits (one per octave). Built once
// at startup through FFT and shared read-only between all voices and plugin
// instances.
class WavetableBank
{
public:
    static constexpr int kTables = 8;
    static constexpr int kFrames = 16;
    static constexpr int kSize = 2048;         // samples per cycle
    static constexpr int kMaxHarmonics = 512;  // level 0; halves each level
    static constexpr int kLevels = 10;         // 512 .. 1 harmonics

    WavetableBank();

    // phase in [0,1), inc = f / fs. harmonicLimit is the highest wanted
    // partial as a fraction of fs (e.g. 0.45 at 1x, 0.62 at 2x). foldLimit is
    // the fraction above which a partial folds back into the audible band,
    // 1 - 20 kHz / rate (0.58 / 0.79 / 0.9 for 48 kHz at 1x / 2x / 4x); the
    // brighter of two crossfaded levels never goes past it. 0 takes
    // min (1.27 x harmonicLimit, 0.88), which is those values for the
    // engine's three limits.
    float read (int table, float position, float phase, float inc, float harmonicLimit, float foldLimit = 0.f) const noexcept;

    // Raw access for the GUI's waveform preview (level 0).
    const float* frame (int table, int frame) const noexcept;

    static const WavetableBank& shared();

private:
    // [table][frame][level] -> kSize + 3 guard samples for Hermite reads.
    std::vector<float> data;

    float* slot (int t, int f, int l) noexcept { return data.data() + index (t, f, l); }
    const float* slot (int t, int f, int l) const noexcept { return data.data() + index (t, f, l); }
    static size_t index (int t, int f, int l) noexcept
    {
        return (((size_t) t * kFrames + (size_t) f) * kLevels + (size_t) l) * (kSize + 4);
    }

    float readFrame (int t, int f, int level, float darker, float phase) const noexcept;
    void build();
};

} // namespace psy::dsp
