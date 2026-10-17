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
    int mode = 0, div = 12, octaves = 1;
    float gate = 0.5f, swing = 0.f;
    bool hold = false;
    int key = -1, scale = 0;   // key 0..11: every note is moved into the scale; -1 plays them as they come
    ArpPattern pattern = ArpPattern::parse ("x");
    float plainVelocity = 0.75f;   // the most an unaccented step plays at (under the accent threshold)

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
    int currentStep() const noexcept { return isRunning() ? lastStep : -1; }   // pattern position, for the UI

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
    int current = -1, lastRandom = -1, lastStep = -1;
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
        const int len = std::clamp (set.pattern.length, 1, kArpSteps);
        const int pos = (int) (((stepIndex % len) + len) % len);
        lastStep = pos;
        if (! set.pattern.has (pos, ArpPattern::On))
        {
            if (current >= 0)
            {
                sink (ArpEvent { false, current, 0.f });
                current = -1;
                toOff = -1.0;
            }
            return;
        }

        // the mode picks the key, the step's note lane moves it, so one held
        // key can play a whole line
        float vel = 0.f;
        int note = std::clamp (nextNote (vel) + (int) set.pattern.notes[(size_t) pos], 0, 127);
        if (set.key >= 0)
            note = std::clamp (quantizeToScale (note, set.key, set.scale), 0, 127);
        // The pattern owns the accent: accented steps hit full velocity, the
        // others stay under the engine's accent threshold.
        vel = set.pattern.has (pos, ArpPattern::Accent) ? 1.f : std::min (vel, set.plainVelocity);

        // Still sounding means the last step was tied (slide flag, or a gate of
        // 100 % or more). The voice slides to the new note with its envelopes
        // running (303 slides, risers and pads keep swelling); the same pitch
        // just keeps sounding.
        if (current < 0)
            sink (ArpEvent { true, note, vel });
        else if (note != current)
            sink (ArpEvent { true, note, vel, current });
        current = note;
        const bool tie = set.gate >= 1.f || set.pattern.has (pos, ArpPattern::Slide);
        toOff = tie ? -1.0 : std::max (1.0, (double) set.gate * stepSamples);
    }
};

} // namespace psy
