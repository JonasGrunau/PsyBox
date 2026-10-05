#pragma once

#include "engine/Engine.h"
#include "generator/Generator.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

class PsyBoxProcessor : public juce::AudioProcessor,
                          public juce::ChangeBroadcaster
{
public:
    PsyBoxProcessor();
    ~PsyBoxProcessor() override = default;

    // ---- AudioProcessor -------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- Parameters -----------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    juce::RangedAudioParameter* param (psy::P p) const noexcept { return params[(size_t) p]; }
    float plain (psy::P p) const noexcept { return rawValues[(size_t) p]->load (std::memory_order_relaxed); }

    // ---- Generator (message thread) ------------------------------------------
    psy::Category getCategory() const noexcept { return category; }
    void setCategory (psy::Category c);
    void generate();
    void generateWithSeed (uint32_t seed);
    void mutate();
    float getMutateAmount() const noexcept { return mutateAmount; }
    void setMutateAmount (float a) { mutateAmount = juce::jlimit (0.f, 1.f, a); }
    uint32_t getSeed() const noexcept { return seed; }
    // "Arp" next to Generate: new patches come with the category's arp.
    bool getArpGeneration() const noexcept { return arpGeneration; }
    void setArpGeneration (bool on);
    // "Key" next to Generate (kick categories): new kicks follow the played key.
    bool getKickKeyGeneration() const noexcept { return kickKeyGeneration; }
    void setKickKeyGeneration (bool on);
    bool isKickCategory() const noexcept { return psy::familyOf (category) == psy::Family::Kick; }
    bool isEnergyCategory() const noexcept { return psy::isEnergy (category); }
    bool categoryHasArp() const noexcept { return psy::hasArp (category); }
    double getBpm() const noexcept { return lastBpm.load (std::memory_order_relaxed); }
    int getLastPlayedNote() const noexcept { return lastPlayedNote.load (std::memory_order_relaxed); }

    // Arp pattern editing (message thread). The pattern is plain parameters;
    // these read and write it as one value.
    psy::ArpPattern getArpPattern() const { return psy::ArpPattern::from (currentState()); }
    void setArpPattern (const psy::ArpPattern& p);
    void generateArpPattern();   // a new pattern in the category's style, undoable
    int getArpStep() const noexcept { return arpStep.load (std::memory_order_relaxed); }   // -1 when idle

    bool isLocked (psy::P p) const noexcept { return locks.test ((size_t) p); }
    void setLocked (psy::P p, bool shouldLock);
    void clearLocks();
    int numLocks() const noexcept { return (int) locks.count(); }

    bool canUndo() const noexcept { return historyIndex > 0; }
    bool canRedo() const noexcept { return historyIndex + 1 < (int) history.size(); }
    void undo();
    void redo();

    // ---- Presets --------------------------------------------------------------
    static juce::File presetFolder();
    juce::String getPresetName() const { return presetName; }
    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::File& file);
    void stepPreset (int delta);
    juce::Array<juce::File> listPresets() const;
    void initPatch();

    // Macro names. Each macro is named after what it drives ("Cutoff",
    // "Reverb", "Filter + Space") and the name follows the matrix as it
    // changes, until the user types one; an empty name goes back to the
    // automatic one. Typed names are part of the patch (session + presets);
    // Generate clears them, Mutate leaves them alone.
    static constexpr int kNumMacros = 4;
    using MacroNames = std::array<juce::String, kNumMacros>;
    juce::String getMacroName (int i) const;   // never empty ("Macro 3" when it drives nothing)
    bool macroDrivesAnything (int i) const;
    bool isMacroNameTyped (int i) const { return macroNames[(size_t) i].isNotEmpty(); }
    void setMacroName (int i, const juce::String& name);

    // ---- Modulation (message thread) ------------------------------------------
    // The matrix as routes. Drag-and-drop adds one (or finds the existing one
    // for the same source and target); the rings around knobs edit amounts.
    struct ModRoute { int slot; psy::ModSrc src; psy::ModDst dst; float amount; };
    std::vector<ModRoute> getModRoutes() const;   // slots with a source and a target
    int findModSlot (psy::ModSrc, psy::ModDst) const;
    int modRoutesTo (psy::ModDst, std::array<ModRoute, psy::kNumModSlots>& out) const;   // no allocation: knobs poll it
    int addModulation (psy::ModSrc, psy::ModDst, float amount = 0.3f);   // -1 when the matrix is full
    void removeModulation (int slot);
    void beginModAmountChange (int slot);
    void setModAmount (int slot, float amount);   // inside begin/end, or as a one-off gesture
    void endModAmountChange (int slot);

    // Editor state (message thread, not saved): the mod source the rings
    // edit first, and the one being dragged onto a knob.
    psy::ModSrc uiSelectedSource = psy::ModSrc::None;
    psy::ModSrc uiDragSource = psy::ModSrc::None;

    // ---- Energy length --------------------------------------------------------
    // Switching between bars and seconds keeps the length as it sounds at the
    // current tempo.
    void setEnergyMode (psy::EnergyMode);
    double energyLengthSeconds() const;

    // Renders the current patch offline (one note: the last one played, held,
    // then released until the tail dies away) to a 24-bit WAV in
    // ~/Music/PsyBox/Bounces and returns the file. Message thread.
    juce::File bounceToFile();

    // On-screen / computer keyboard (standalone). Merged into the MIDI input.
    juce::MidiKeyboardState keyboardState;

    // ---- GUI feeds ------------------------------------------------------------
    // Mono scope ring buffer written by the audio thread, read by the GUI.
    static constexpr int kScopeSize = 4096;
    void copyScope (float* dest, int num) const noexcept;
    // Output level of one channel (0 = L, 1 = R), linear: the peak since the
    // last call (so the GUI never misses one between repaints) and the RMS
    // over ~300 ms. Message thread.
    struct OutLevel { float peak = 0.f, rms = 0.f; };
    OutLevel takeOutLevel (int channel) noexcept
    {
        return { outPeak[(size_t) channel].exchange (0.f, std::memory_order_relaxed),
                 std::sqrt (outMeanSquare[(size_t) channel].load (std::memory_order_relaxed)) };
    }
    // Compressor band readings in dB (band 0 only in Normal mode). The input
    // level holds its peaks and falls back at ~45 dB/s.
    struct CompReading { float inDb = -200.f, gainDb = 0.f, totalDb = 0.f; };
    CompReading getCompReading (int band) const noexcept
    {
        const auto& m = compMeters[(size_t) band];
        return { m[0].load (std::memory_order_relaxed), m[1].load (std::memory_order_relaxed), m[2].load (std::memory_order_relaxed) };
    }

private:
    std::array<juce::RangedAudioParameter*, psy::kNumParams> params {};
    std::array<std::atomic<float>*, psy::kNumParams> rawValues {};

    psy::Engine engine;
    psy::ParamState audioParams;
    std::vector<psy::MidiEvent> midiEvents;
    int reportedLatency = -1;

    psy::Generator generator;
    psy::Category category = psy::Category::Acid;
    uint32_t seed = 0;
    psy::LockSet locks;
    float mutateAmount = 0.25f;
    bool arpGeneration = false;
    bool kickKeyGeneration = true;
    juce::String presetName { "Init" };
    std::atomic<int> lastPlayedNote { 48 };

    // Patch swaps (Generate, Mutate, undo, presets, category switch) bump the
    // epoch before touching any parameter. The audio thread then fades the
    // old sound out on the old values, resets the engine (voices, filters, FX
    // tails) and retriggers the held notes with the new patch.
    std::atomic<uint32_t> patchEpoch { 0 };
    uint32_t seenEpoch = 0;
    enum class Swap { None, FadingOut, Restart };
    Swap swap = Swap::None;
    int fadePos = 0;
    std::array<float, 128> heldVelocity {};   // audio thread: notes currently down
    std::array<bool, 128> pressedInFade {};   // audio thread: note-ons that arrived during the fade-out
    void beginPatchChange() noexcept { patchEpoch.fetch_add (1, std::memory_order_release); }
    static constexpr int kFadeOut = 480, kFadeIn = 128;   // 10 ms, ~3 ms at 48 kHz
    std::atomic<double> lastBpm { 120.0 };
    MacroNames macroNames;   // typed names; empty = automatic
    void resetMacroNames();
    void writeMacroNames (juce::ValueTree&) const;
    void readMacroNames (const juce::ValueTree&);

    // The patch each category had when you last left it; restored when you come back.
    struct Slot { bool valid = false; psy::ParamState state; uint32_t seed = 0; juce::String name; MacroNames macros; };
    std::array<Slot, psy::kNumCategories> slots;
    void saveSlot();
    void switchCategory (psy::Category c);   // saves the old slot, restores or generates the new one
    void writeSlots (juce::ValueTree&) const;
    void readSlots (const juce::ValueTree&);

    struct Snapshot { psy::ParamState state; uint32_t seed; psy::Category category; juce::String name; MacroNames macros; };
    std::vector<Snapshot> history;
    int historyIndex = -1;

    std::array<float, kScopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };
    std::array<std::atomic<float>, 2> outPeak {}, outMeanSquare {};
    std::array<float, 2> meanSquare {};   // audio thread
    std::atomic<int> arpStep { -1 };
    std::array<std::array<std::atomic<float>, 3>, 3> compMeters {};   // per band: in, gain, total
    juce::Random random;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    psy::ParamState currentState() const;
    void normaliseFamily();   // the engine mode and the arp follow the category
    void applyState (const psy::ParamState& s);
    void pushHistory();
    void restoreRanges();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PsyBoxProcessor)
};
