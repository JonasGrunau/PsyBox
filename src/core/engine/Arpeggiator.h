#pragma once

// Tempo-synced arpeggiator. Sits between the MIDI input and the voices: keys
// go in, timed note-on / note-off events come out. Runs off the host
// transport while it plays (so step 0 of a rhythm lands on the beat) and
// free-runs from the first key otherwise.

#include "Params.h"
#include "dsp/Common.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace psy
{

struct ArpEvent
{
    bool on = true;
    int note = 0;
    float velocity = 0.f;
    int slideFrom = -1;   // on-events only: >= 0 moves the voice playing this note, no retrigger
};

struct ArpSettings
{
    int mode = 0, div = 12, octaves = 1, rhythm = 0;
    float gate = 0.5f, swing = 0.f;
    bool hold = false;

    static ArpSettings from (const ParamState& p) noexcept;
};

class Arpeggiator
{
public:
    static constexpr int kMaxKeys = 32;

    void prepare (double sampleRate) noexcept { fs = sampleRate; }
    void reset() noexcept;

    void setSettings (const ArpSettings& s) noexcept;
    void setSustain (bool down) noexcept;

    // Aligns the step grid with the host. Call once per block with the
    // transport position at the block's first sample.
    void sync (double ppq, double bpm, bool playing) noexcept;

    void keyOn (int note, float velocity) noexcept;
    void keyOff (int note) noexcept;
    void allOff() noexcept;   // forgets every key, latched ones too

    // Emits every event due at the current sample (note-offs first).
    template <typename Sink>
    void fireDue (Sink&& sink) noexcept
    {
        if (current >= 0 && (releasePending || (toOff >= 0.0 && toOff < 0.5)))
        {
            sink (ArpEvent { false, current, 0.f });
            current = -1;
            toOff = -1.0;
        }
        releasePending = false;

        while (toStep < 0.5)
        {
            if (numKeys == 0 && ! synced)
            {
                toStep = kIdle;
                break;
            }
            if (numKeys > 0)
                playStep (sink);
            const double from = swingOffset (stepIndex);
            ++stepIndex;
            toStep += stepSamples * (1.0 + swingOffset (stepIndex) - from);
        }
    }

    // Samples until the next event, at least 1 (INT_MAX when idle).
    int samplesToNextEvent() const noexcept;
    void advance (int numSamples) noexcept;

    int numPhysicalKeys() const noexcept;
    int physicalKey (int i) const noexcept;   // i-th key that is physically down
    bool isRunning() const noexcept { return numKeys > 0 || current >= 0; }

private:
    static constexpr double kIdle = 1.0e18;

    struct Key { int note; float velocity; bool down; };

    double fs = 48000.0, bpm = 120.0;
    ArpSettings set;
    std::array<Key, kMaxKeys> keys {};   // in played order
    int numKeys = 0;
    bool sustain = false, synced = false;

    double stepSamples = 6000.0;
    double toStep = kIdle, toOff = -1.0;
    int64_t stepIndex = 0, lastFired = INT64_MIN;
    uint64_t seqPos = 0;
    int current = -1, lastRandom = -1;
    bool releasePending = false;
    dsp::Rng rng { 0xA59u };

    bool latched() const noexcept { return set.hold || sustain; }
    double swingOffset (int64_t step) const noexcept { return (step & 1) ? (double) set.swing * 0.5 : 0.0; }
    double beatsPerStep() const noexcept { return (double) syncDivBeats (set.div); }
    void updateStepSamples() noexcept { stepSamples = std::max (1.0, beatsPerStep() * 60.0 / bpm * fs); }
    void dropReleasedKeys() noexcept;
    void removeKey (int index) noexcept;
    int nextNote (float& velocity) noexcept;

    template <typename Sink>
    void playStep (Sink& sink) noexcept
    {
        lastFired = stepIndex;
        const auto mask = arpRhythmMask (set.rhythm);
        const int len = std::max (1, mask.length);
        const bool plays = mask.steps[(size_t) (((stepIndex % len) + len) % len)] == 'x';
        if (! plays)
        {
            if (current >= 0)
            {
                sink (ArpEvent { false, current, 0.f });
                current = -1;
                toOff = -1.0;
            }
            return;
        }

        float vel = 0.f;
        const int note = nextNote (vel);
        // Still sounding means the gate is 100 % or longer: a tie. The voice
        // slides to the new note with its envelopes running (303 slides,
        // risers and pads keep swelling); the same pitch just keeps sounding.
        if (current < 0)
            sink (ArpEvent { true, note, vel });
        else if (note != current)
            sink (ArpEvent { true, note, vel, current });
        current = note;
        const double gateLen = (double) set.gate * stepSamples;
        toOff = set.gate >= 1.f ? -1.0 : std::max (1.0, gateLen);
    }
};

} // namespace psy
