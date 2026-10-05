#pragma once

#include "Arpeggiator.h"
#include "KickVoice.h"
#include "Params.h"
#include "Voice.h"
#include "dsp/Dynamics.h"
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

// Host transport at the first sample of a block.
struct Transport
{
    double bpm = 120.0;
    double ppq = 0.0;       // quarter notes since the song start
    bool playing = false;   // ppq is only meaningful while playing
};

// Where the modulators of every sounding voice are, for the editor's moving
// dots. Envelope stages follow dsp::Adsr::Stage (the pitch envelope uses
// Attack and Decay, Idle once it is done); pos is how far into the stage.
struct ModView
{
    struct Env { uint8_t stage = 0; float pos = 0.f; };
    struct Voice
    {
        std::array<Env, 4> env {};            // amp, env 1 (filter), env 2 (mod), env 3 (pitch)
        std::array<float, kNumLfos> lfo {};   // phase over two cycles, 0..2 (see dsp::Lfo::phase2)
        float sweep = 0.f;                    // energy sweep, linear 0..1
        std::array<psy::Voice::FilterNow, 2> filter {};   // each filter slot as modulated now
    };
    std::array<Voice, kMaxVoices> voices {};
    int numVoices = 0;                         // newest first
};

class Engine
{
public:
    Engine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Target parameter values; smoothed internally.
    void setParams (const ParamState& p) noexcept { target = p; }

    void process (float* left, float* right, int numSamples, const MidiEvent* events, int numEvents, const Transport& transport) noexcept;
    void process (float* left, float* right, int numSamples, const MidiEvent* events, int numEvents, double bpm) noexcept
    {
        process (left, right, numSamples, events, numEvents, Transport { bpm, 0.0, false });
    }

    // Offline (a host render, a bounce) plays at the Render quality instead
    // of the live one.
    void setOffline (bool isOffline) noexcept { offline = isOffline; }
    int latencySamples() const noexcept { return decimL.latency (osFactor); }
    int activeVoices() const noexcept;
    int arpStep() const noexcept { return arpActive ? arp.currentStep() : -1; }   // pattern step playing, or -1

    // For the GUI (approximate, read without locking).
    float lastPeak() const noexcept { return peak; }
    const dsp::CompMeter& compMeter() const noexcept { return comp.meter(); }
    void getModView (ModView&) const noexcept;

private:
    double fs = 48000.0;
    int maxBlock = 512;
    int osFactor = 2;
    bool offline = false;

    ParamState target, cur;
    bool firstBlock = true;

    std::array<Voice, kMaxVoices> voices;
    uint64_t ageCounter = 0, noteSeed = 1;
    VoiceContext ctx;

    // Kick mode: two one-shot kicks, so a retrigger can fade the old hit out.
    std::array<KickVoice, 2> kicks;
    int lastKick = 0;
    int engineMode = -1;   // last mode seen; -1 after a reset

    // mono / legato note stack
    std::array<int, 32> held {};
    std::array<bool, 32> heldAccent {};
    int numHeld = 0;
    std::array<bool, 128> sustained {};
    bool sustainPedal = false;
    int lastNote = -1;
    float lastPitch = -1.f;

    Arpeggiator arp;
    bool arpActive = false;

    std::array<dsp::Lfo, kNumLfos> globalLfos;
    std::array<std::vector<float>, kNumLfos> globalLfoBuf;

    std::vector<float> osL, osR;
    dsp::Decimator decimL, decimR;

    dsp::Distortion dist;
    dsp::Distortion distLate;   // the distortion moved later in the chain, at the base rate
    dsp::Bitcrusher crush;
    dsp::FrequencyShifter fshift;
    dsp::Phaser phaser;
    dsp::ChorusFlanger chorus;
    dsp::Compressor comp;
    dsp::StereoDelay delay;
    dsp::Reverb reverb;
    dsp::StereoWidth width;
    std::array<dsp::DcBlocker, 2> outDc;

    // Matrix slots that target an effect, and the patch the effects play
    // (the smoothed patch plus those modulations).
    std::array<VoiceContext::Slot, kNumModSlots> fxSlots {};
    int numFxSlots = 0;
    ParamState fxParams;
    int leadVoice = -1;   // the newest note; effect modulation follows its envelopes

    float peak = 0.f;

    int wantedFactor() const noexcept;   // oversampling for the live or the render quality
    void applyQuality();
    void smoothParams (int numSamples) noexcept;
    void prepareContext (double bpm) noexcept;
    void handleEvent (const MidiEvent& e) noexcept;
    // Keys from MIDI go through the arp when it is on; the voice-level calls
    // below take the arp's output (or the keys directly when it is off).
    void keyOn (int note, float velocity) noexcept;
    void keyOff (int note) noexcept;
    void updateArp() noexcept;
    void fireArp() noexcept;
    void voiceNoteOn (int note, float velocity) noexcept;
    void voiceNoteOff (int note) noexcept;
    void dropHeld (int note) noexcept;   // takes the note out of the mono stack
    void voiceSlide (int from, int to, float velocity) noexcept;
    void allNotesOff() noexcept;
    bool kickMode() const noexcept { return cur.geti (P::EngineMode) == (int) EngineMode::Kick; }
    void kickNoteOn (int note, float velocity) noexcept;
    void followEngineMode() noexcept;
    Voice& allocateVoice (bool& stolen) noexcept;
    void renderVoices (int offset, int n) noexcept;
    // The effects in the patch's order. Distortion first runs on the
    // oversampled signal before the decimator instead (see process).
    void processFx (float* l, float* r, int n, double bpm, const ParamState& p) noexcept;
    bool distFirst (const ParamState& p) const noexcept { return fxChain (p.geti (P::FxOrder))[0] == (int) Fx::Distortion; }
    void modulateFx (int sampleInBlock) noexcept;   // fills fxParams
};

} // namespace psy
