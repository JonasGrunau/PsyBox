#pragma once

#include "Params.h"
#include "Voice.h"
#include "dsp/Effects.h"
#include "dsp/Oversampling.h"

#include <array>
#include <vector>

namespace psy
{

struct MidiEvent
{
    enum Type : uint8_t { NoteOn, NoteOff, PitchBend, Controller, ChannelPressure, AllNotesOff };
    int offset = 0;       // sample position in the block
    Type type = NoteOn;
    int number = 0;       // note or controller number
    float value = 0.f;    // velocity 0..1, bend -1..1, CC 0..1, pressure 0..1
};

class Engine
{
public:
    Engine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Target parameter values; smoothed internally.
    void setParams (const ParamState& p) noexcept { target = p; }

    void process (float* left, float* right, int numSamples, const MidiEvent* events, int numEvents, double bpm) noexcept;

    int latencySamples() const noexcept { return decimL.latency (osFactor); }
    int activeVoices() const noexcept;

    // For the GUI (approximate, read without locking).
    float lastPeak() const noexcept { return peak; }

private:
    double fs = 48000.0;
    int maxBlock = 512;
    int osFactor = 2;

    ParamState target, cur;
    bool firstBlock = true;

    std::array<Voice, kMaxVoices> voices;
    uint64_t ageCounter = 0, noteSeed = 1;
    VoiceContext ctx;

    // mono / legato note stack
    std::array<int, 32> held {};
    std::array<bool, 32> heldAccent {};
    int numHeld = 0;
    std::array<bool, 128> sustained {};
    bool sustainPedal = false;
    int lastNote = -1;
    float lastPitch = -1.f;

    std::array<dsp::Lfo, kNumLfos> globalLfos;
    std::array<std::vector<float>, kNumLfos> globalLfoBuf;

    std::vector<float> osL, osR;
    dsp::Decimator decimL, decimR;

    dsp::Distortion dist;
    dsp::Bitcrusher crush;
    dsp::FrequencyShifter fshift;
    dsp::Phaser phaser;
    dsp::ChorusFlanger chorus;
    dsp::StereoDelay delay;
    dsp::Reverb reverb;
    std::array<dsp::DcBlocker, 2> outDc;

    float peak = 0.f;

    void applyQuality();
    void smoothParams (int numSamples) noexcept;
    void prepareContext (double bpm) noexcept;
    void handleEvent (const MidiEvent& e) noexcept;
    void noteOn (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;
    void allNotesOff() noexcept;
    Voice& allocateVoice (bool& stolen) noexcept;
    void renderVoices (int offset, int n) noexcept;
    void processFx (float* l, float* r, int n, double bpm) noexcept;
};

} // namespace psy
