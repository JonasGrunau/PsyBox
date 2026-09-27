#pragma once

#include "Displays.h"
#include "Widgets.h"

#include <memory>
#include <vector>

// A titled area of the main grid. Header on top, content below.
class Section : public juce::Component
{
public:
    explicit Section (juce::String title) : title (std::move (title)) {}
    void paint (juce::Graphics&) override;

protected:
    juce::String title;
    juce::Rectangle<int> header() const { return getLocalBounds().reduced (theme::kPad, 0).removeFromTop (theme::kHeaderH + 6).withTrimmedTop (6); }
    juce::Rectangle<int> body() const { return getLocalBounds().reduced (theme::kPad, 0).withTrimmedTop (theme::kHeaderH + 10).withTrimmedBottom (10); }
    int titleWidth() const;

    // Lays out components left to right in equal cells.
    static void row (juce::Rectangle<int> area, const std::vector<juce::Component*>& items, int cellW = 0);
};

// Helper that owns knobs for a panel.
struct KnobSet
{
    std::vector<std::unique_ptr<ParamKnob>> knobs;
    ParamKnob* add (juce::Component& parent, PsySynthProcessor& p, psy::P prm, const juce::String& label = {})
    {
        knobs.push_back (std::make_unique<ParamKnob> (p, prm, label));
        parent.addAndMakeVisible (*knobs.back());
        return knobs.back().get();
    }
};

// ---------------------------------------------------------------------------
class OscPanel : public Section, private juce::Timer
{
public:
    OscPanel (PsySynthProcessor&, bool isB);
    void resized() override;

private:
    PsySynthProcessor& proc;
    bool isB;
    ToggleDot onToggle;
    Segmented type;
    WaveDisplay wave;
    KnobSet ks;
    ParamKnob *shape, *pw, *detune, *wtpos, *ratio, *fm, *fb, *coarse, *fine, *level, *xmod = nullptr;
    ParamCombo table;
    ToggleDot retrig;
    std::unique_ptr<ToggleDot> sync;
    int lastType = -1;
    void timerCallback() override;
};

class MixerPanel : public Section
{
public:
    explicit MixerPanel (PsySynthProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    KnobSet ks;
    ParamKnob *sub, *noise, *ring, *pAmt, *pAtk, *pDec, *pCurve;
    Segmented subOct;
    ParamCombo subShape, noiseType, penvDest;
    PitchEnvDisplay penvDisplay;
};

class FilterPanel : public Section, private juce::Timer
{
public:
    FilterPanel (PsySynthProcessor&, int slot);
    void resized() override;

private:
    PsySynthProcessor& proc;
    int slot;
    ParamCombo type;
    FilterDisplay display;
    KnobSet ks;
    ParamKnob *cutoff, *reso, *drive, *env, *key, *vowel = nullptr;
    std::unique_ptr<Segmented> routing;
    int lastType = -1;
    void timerCallback() override;
};

class VoicePanel : public Section
{
public:
    explicit VoicePanel (PsySynthProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    Segmented mode, glideMode;
    KnobSet ks;
    std::vector<ParamKnob*> knobs;
};

class EnvPanel : public Section
{
public:
    explicit EnvPanel (PsySynthProcessor&);
    void resized() override;

private:
    TabStrip tabs { { "Amp", "Filter", "Mod" } };
    EnvDisplay display;
    KnobSet ks;
    ParamKnob* knobs[3][4] {};
    void show (int env);
};

class LfoPanel : public Section, private juce::Timer
{
public:
    explicit LfoPanel (PsySynthProcessor&);
    void resized() override;

private:
    PsySynthProcessor& proc;
    TabStrip tabs { { "1", "2", "3" } };
    LfoDisplay display;
    KnobSet ks;
    struct Page
    {
        std::unique_ptr<ParamCombo> shape, div;
        ParamKnob *rate = nullptr, *phase = nullptr;
        std::unique_ptr<ToggleDot> sync, retrig;
    };
    Page pages[3];
    int current = 0;
    bool lastSync = false;
    void show (int lfo);
    void timerCallback() override;
};

class ModMatrixPanel : public Section
{
public:
    explicit ModMatrixPanel (PsySynthProcessor&);
    void resized() override;

private:
    struct Row
    {
        std::unique_ptr<ParamCombo> src, dst;
        std::unique_ptr<AmountSlider> amt;
    };
    Row rows[psy::kNumModSlots];
};

class FxPanel : public Section, private juce::Timer
{
public:
    explicit FxPanel (PsySynthProcessor&);
    void resized() override;

private:
    PsySynthProcessor& proc;
    TabStrip tabs { { "Dist", "Crush", "Shift", "Phaser", "Chorus", "Delay", "Reverb" } };
    struct Page
    {
        std::unique_ptr<ToggleDot> on;
        std::vector<juce::Component*> items;
        std::vector<std::unique_ptr<juce::Component>> owned;
    };
    Page pages[7];
    KnobSet ks;
    ParamKnob* delayTime = nullptr;
    std::unique_ptr<ParamCombo> delayDiv;
    int current = 0;
    bool lastDelaySync = true;
    void show (int page);
    void timerCallback() override;
};

class MacroPanel : public Section
{
public:
    explicit MacroPanel (PsySynthProcessor&);
    void resized() override;

private:
    KnobSet ks;
};

class OutputPanel : public Section, private juce::Timer
{
public:
    explicit OutputPanel (PsySynthProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    PsySynthProcessor& proc;
    Scope scope;
    int lastVoices = -1;
    void timerCallback() override;
};

// ---------------------------------------------------------------------------
// Left column: category list, then the generator controls.
// ---------------------------------------------------------------------------
class Sidebar : public juce::Component, public juce::ChangeListener
{
public:
    explicit Sidebar (PsySynthProcessor&);
    ~Sidebar() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

private:
    PsySynthProcessor& proc;
    int hover = -1;
    juce::TextButton generateButton { "Generate" }, mutateButton { "Mutate" }, clearLocks { "Clear" };
    juce::Slider mutateAmount;
    IconButton undoButton { IconButton::Icon::Undo, "Undo generate / mutate" };
    IconButton redoButton { IconButton::Icon::Redo, "Redo" };
    juce::Label seedLabel;

    static constexpr int kRowH = 26;
    static constexpr int kListTop = 40;
    int categoryAt (juce::Point<int>) const;
    juce::Rectangle<int> rowBounds (int i) const;
    juce::Rectangle<int> infoArea() const;
    void refresh();
};

class TopBar : public juce::Component, public juce::ChangeListener
{
public:
    explicit TopBar (PsySynthProcessor&);
    ~TopBar() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

private:
    PsySynthProcessor& proc;
    IconButton prev { IconButton::Icon::Left, "Previous preset" };
    IconButton next { IconButton::Icon::Right, "Next preset" };
    IconButton menu { IconButton::Icon::Menu, "Presets" };
    IconButton save { IconButton::Icon::Save, "Save preset" };
    Segmented quality;
    ParamKnob volume;
    juce::Rectangle<int> presetArea;
    void showPresetMenu();
    void savePresetDialog();
    std::unique_ptr<juce::AlertWindow> saveWindow;
};
