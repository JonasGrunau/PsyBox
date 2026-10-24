#pragma once

#include "Arpeggiator.h"
#include "KickVoice.h"
#include "Params.h"
#include "PhosFx.h"
#include "PhosVoice.h"
#include "Voice.h"
#include "dsp/Dynamics.h"
#include "dsp/Effects.h"
#include "dsp/Eq.h"
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
    friend struct EngineTestAccess;   // the tests reach in to poison a voice

    Engine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Target parameter values; smoothed internally. A value that is NaN or
    // infinite becomes the parameter's default and the rest are held to their
    // range: one bad value (an automation glitch) would otherwise poison the
    // smoothed state, and every filter and effect behind it, for good.
    void setParams (const ParamState& p) noexcept;

    void process (float* left, float* right, int numSamples, const MidiEvent* events, int numEvents, const Transport& transport) noexcept;
    void process (float* left, float* right, int numSamples, const MidiEvent* events, int numEvents, double bpm) noexcept
    {
        process (left, right, numSamples, events, numEvents, Transport { bpm, 0.0, false });
    }

    // Offline (a host render, a bounce) plays at the Render quality instead
    // of the live one.
    void setOffline (bool isOffline) noexcept { offline = isOffline; }
    // The decimator's, plus the most the effects can add in this engine mode
    // (maxEffectLatency): the output is padded up to it, so switching an
    // effect, or a new patch, never moves the latency the host sees. Only a
    // quality change and a switch between the 303 and the other engines do.
    int latencySamples() const noexcept { return decimL.latency (osFactor) + maxEffectLatency(); }
    int activeVoices() const noexcept;
    int arpStep() const noexcept { return arpActive ? arp.currentStep() : -1; }   // pattern step playing, or -1

    // Health since the last reset(): samples that came out of the voices or the
    // effects as NaN or infinity (the engine replaces them and recovers), and
    // the loudest sample before the output's soft clip. The stress tests read
    // these, because the output itself is always finite and below 0 dBFS.
    struct Diagnostics
    {
        uint32_t nonFinite = 0;
        float preClipPeak = 0.f;
    };
    const Diagnostics& diagnostics() const noexcept { return diag; }

    // For the GUI (approximate, read without locking).
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

    // Phoscyon mode: one 303 voice (it slides, so it never retriggers in a
    // second slot), the note's Env 2 and its velocity and random value for
    // the matrix, and the original's effects.
    PhosVoice phos;
    dsp::Adsr phosModEnv;
    float phosVelocity = 0.f, phosRandom = 0.f;
    dsp::Rng phosRng { 909 };
    // The 303 reads its matrix every kPhosModGrid samples, counted on from
    // block to block (like the effects' slices), so where it reads never
    // depends on the host's block size.
    static constexpr int kPhosModGrid = 16;
    PhosMods phosModsNow;
    int phosModCount = 0;   // samples into the current grid step
    PhosFx phosFx;

    // mono / legato note stack
    std::array<int, 32> held {};
    std::array<bool, 32> heldAccent {};
    std::array<float, 32> heldVel {};
    int numHeld = 0;
    std::array<bool, 128> sustained {};
    bool sustainPedal = false;
    int lastNote = -1;
    float lastPitch = -1.f;

    Arpeggiator arp;
    bool arpActive = false;

    // Controllers as the host sent them; ctx holds them smoothed (7-bit steps
    // would zipper a cutoff or an effect mix).
    float rawBend = 0.f, rawWheel = 0.f, rawAftertouch = 0.f;
    std::array<dsp::Lfo, kNumLfos> globalLfos;
    std::array<std::vector<float>, kNumLfos> globalLfoBuf;

    std::vector<float> osL, osR;
    dsp::Decimator decimL, decimR;

    dsp::Distortion dist;
    dsp::LateDistortion distLate;   // the distortion moved later in the chain, with its own oversampling
    dsp::Bitcrusher crush;
    dsp::FrequencyShifter fshift;
    dsp::Phaser phaser;
    dsp::ChorusFlanger chorus;
    dsp::Compressor comp;
    dsp::StereoDelay delay;
    dsp::Reverb reverb;
    dsp::StereoWidth width;
    dsp::Eq eq;
    dsp::Dimension dim;
    std::array<dsp::DcBlocker, 2> outDc;
    dsp::LatencyPad pad;   // tops effectLatency up to maxEffectLatency

    // Matrix slots that target an effect, and the patch the effects play
    // (the smoothed patch plus those modulations).
    std::array<VoiceContext::Slot, kNumModSlots> fxSlots {};
    int numFxSlots = 0;
    ParamState fxParams, fxParamsOs;   // after the decimator / on the oversampled signal (distortion first)
    int fxCount = 0, fxCountOs = 0;    // samples into the current 32-sample slice of each
    int leadVoice = -1;   // the newest note; effect modulation follows its envelopes
    // Every matrix source at every sample of the block, as the newest voice
    // (or the 303) had it: the effects read the value that covers their own
    // sample instead of the block's last. Only filled while a slot targets an effect.
    std::vector<std::array<float, (size_t) ModSrc::Count>> fxSrc;

    float outGain = 1.f;   // the output gain the last sample used (it ramps to the next block's)
    bool outGainSet = false;
    bool qualityPending = false;   // voices are fading out before the oversampling changes
    bool lastPlaying = false;       // the transport as the last process() left it, to see a start or a seek
    double lastPpqEnd = 0.0;
    int lfoLockIn = 0;              // samples until a synced free LFO is next checked against the bar
    std::array<float, kNumLfos> freeLfoHz {};   // the free LFOs' rates, matrix offsets included
    Diagnostics diag;
    bool fxBad = false;

    int effectLatency() const noexcept;
    int maxEffectLatency() const noexcept;

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
    void followQuality() noexcept;
    void snapDiscrete() noexcept;
    void smoothControllers (int n) noexcept;
    void recoverFromNonFinite (int offset, int n) noexcept;
    void resetEffects() noexcept;
    void fireArp() noexcept;
    void voiceNoteOn (int note, float velocity) noexcept;
    void voiceNoteOff (int note) noexcept;
    void dropHeld (int note) noexcept;   // takes the note out of the mono stack
    void voiceSlide (int from, int to, float velocity) noexcept;
    void allNotesOff() noexcept;
    bool kickMode() const noexcept { return cur.geti (P::EngineMode) == (int) EngineMode::Kick; }
    bool phosMode() const noexcept { return cur.geti (P::EngineMode) == (int) EngineMode::Phoscyon; }
    void kickNoteOn (int note, float velocity) noexcept;
    void phosNoteOn (int note, float velocity) noexcept;
    // The matrix sources while the Phoscyon voice plays (its envelopes stand
    // in for the amp env and Env 1), and the knob offsets they add up to.
    std::array<float, (size_t) ModSrc::Count> phosSources (int sampleInBlock) const noexcept;
    PhosMods phosMods (int sampleInBlock) const noexcept;
    void followEngineMode() noexcept;
    Voice& allocateVoice (bool& stolen) noexcept;
    // A voice that still sounds would click if it restarted in place (its
    // filters ring on the envelope's jump): it moves to an idle slot at or
    // above `from` and fades out there, and slot i is free for a fresh start.
    // False when it is quiet enough to restart, or no slot is idle.
    bool handOff (size_t i, size_t from) noexcept;
    void renderVoices (int offset, int n, bool record) noexcept;
    // The effects in the patch's order. Distortion first runs on the
    // oversampled signal before the decimator instead (see process).
    void processFx (float* l, float* r, int n, double bpm, const ParamState& p) noexcept;
    static SynthFxChain synthChain (const ParamState& p) noexcept
    {
        return synthFxChain (p.geti (P::FxOrder), p.geti (P::EqSlot), p.geti (P::DimSlot));
    }
    bool distFirst (const ParamState& p) const noexcept { return synthChain (p)[0] == (int) Fx::Distortion; }
    static PhosFxChain phosChain (const ParamState& p) noexcept
    {
        return phosFxChain (p.geti (P::FxOrder), p.geti (P::PhosEqSlot), p.geti (P::PhosLimSlot));
    }
    bool phosDistFirst (const ParamState& p) const noexcept { return phosChain (p)[0] == (int) Fx::Distortion; }
    void processFxSlot (int fx, bool first, float* l, float* r, int n, double bpm, const ParamState& p) noexcept;
    void modulateFx (int sampleInBlock, ParamState& out) noexcept;   // the patch plus the effect slots at that sample
};

} // namespace psy
