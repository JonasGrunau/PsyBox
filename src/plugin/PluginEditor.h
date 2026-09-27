#pragma once

#include "PluginProcessor.h"
#include "ui/KickPanels.h"
#include "ui/Panels.h"
#include "ui/PsyLookAndFeel.h"

// The whole UI is laid out at a fixed logical size and scaled as one piece,
// so every proportion survives resizing.
class PsySynthEditor : public juce::AudioProcessorEditor,
                       public juce::ChangeListener,
                       private juce::Timer
{
public:
    static constexpr int kW = 1280, kH = 820;
    static constexpr int kKeysH = 64;   // keyboard strip under the panels (standalone only)
    static inline bool forceKeyboard = false;   // lets the snapshot tool render the strip

    explicit PsySynthEditor (PsySynthProcessor&);
    ~PsySynthEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

private:
    class Root : public juce::Component, private juce::Timer
    {
    public:
        explicit Root (PsySynthProcessor&);
        VoicePanel& voicePanel() noexcept { return voice; }
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        // Kick patches swap the synth sections for the kick sections.
        void updateMode();

    private:
        PsySynthProcessor& proc;
        bool kickLayout = false;
        TopBar top;
        Sidebar side;
        OscPanel oscA, oscB;
        MixerPanel mixer;
        FilterPanel f1, f2;
        VoicePanel voice;
        EnvPanel env;
        LfoPanel lfo;
        ModMatrixPanel matrix;
        FxPanel fx;
        MacroPanel macros;
        OutputPanel output;
        KickPitchPanel kickPitch;
        KickAmpPanel kickAmp;
        KickClickPanel kickClick;
        KickDrivePanel kickDrive;
        KickWavePanel kickWave;
        void timerCallback() override { updateMode(); }   // automation / state loads
    };

    // Flat keys in the theme: dark white keys, black keys, accent when held.
    class Keys : public juce::MidiKeyboardComponent
    {
    public:
        using MidiKeyboardComponent::MidiKeyboardComponent;
        void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float>, bool down, bool over, juce::Colour, juce::Colour) override;
        void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float>, bool down, bool over, juce::Colour) override;
    };

    PsySynthProcessor& proc;
    PsyLookAndFeel lnf;
    Root root;
    const bool withKeyboard;
    Keys keyboard;
    ArpPatternEditor patternEditor;   // over the panels, same scale as root
    int totalH() const { return kH + (withKeyboard ? kKeysH : 0); }
    // Keeps the piano playable from the computer keyboard: controls never take
    // focus, and focus returns to the piano whenever nothing is being typed.
    void timerCallback() override;
    juce::TooltipWindow tooltips { this, 600 };
    void updateAccent();
};
