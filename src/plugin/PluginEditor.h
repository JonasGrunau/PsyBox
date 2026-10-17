#pragma once

#include "PluginProcessor.h"
#include "ui/FxPage.h"
#include "ui/PsyLookAndFeel.h"

// The whole UI is laid out at a fixed logical size and scaled as one piece,
// so every proportion survives resizing. Mod sources are dragged onto knobs
// through the editor (it is the drag-and-drop container).
class PsyBoxEditor : public juce::AudioProcessorEditor,
                     public juce::ChangeListener,
                     public juce::DragAndDropContainer,
                     private juce::Timer
{
public:
    static constexpr int kW = 1280, kH = 820;
    static constexpr int kKeysH = 64;   // keyboard strip under the panels (standalone only)
    static inline bool forceKeyboard = false;   // lets the snapshot tool render the strip

    explicit PsyBoxEditor (PsyBoxProcessor&);
    ~PsyBoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showView (View v) { root.showView (v); }

protected:
    void dragOperationStarted (const juce::DragAndDropTarget::SourceDetails&) override;
    void dragOperationEnded (const juce::DragAndDropTarget::SourceDetails&) override;

private:
    class Root : public juce::Component, private juce::Timer
    {
    public:
        explicit Root (PsyBoxProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;
        // The views a category offers (kicks: no matrix, only melodic sounds
        // an arp) and what sits under the voicing on the OSC page.
        void updateMode();
        void showView (View);

    private:
        PsyBoxProcessor& proc;
        bool kickLayout = false, phosLayout = false;
        int lastMode = -1;
        View view = View::Main;
        TopBar top;
        Sidebar side;
        OscPage osc;
        KickPage kick;
        PhosPage phos;
        FxPage fx, phosFx;   // our rack, and the Phoscyon engine's
        MatrixPage matrix;
        GlobalPage global;
        ArpPage arp;
        std::vector<View> available() const;
        juce::Component* pageFor (View) const;
        std::vector<juce::Component*> pages();
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

    PsyBoxProcessor& proc;
    PsyLookAndFeel lnf;
    Root root;
    const bool withKeyboard;
    Keys keyboard;
    int totalH() const { return kH + (withKeyboard ? kKeysH : 0); }
    // Keeps the piano playable from the computer keyboard: controls never take
    // focus, and focus returns to the piano whenever nothing is being typed.
    void timerCallback() override;
    juce::TooltipWindow tooltips { this, 600 };
    void updateAccent();
};
