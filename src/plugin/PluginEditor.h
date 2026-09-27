#pragma once

#include "PluginProcessor.h"
#include "ui/Panels.h"
#include "ui/PsyLookAndFeel.h"

// The whole UI is laid out at a fixed logical size and scaled as one piece,
// so every proportion survives resizing.
class PsySynthEditor : public juce::AudioProcessorEditor,
                       public juce::ChangeListener
{
public:
    static constexpr int kW = 1280, kH = 820;

    explicit PsySynthEditor (PsySynthProcessor&);
    ~PsySynthEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

private:
    class Root : public juce::Component
    {
    public:
        explicit Root (PsySynthProcessor&);
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

    private:
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
    };

    PsySynthProcessor& proc;
    PsyLookAndFeel lnf;
    Root root;
    juce::TooltipWindow tooltips { this, 600 };
    void updateAccent();
};
