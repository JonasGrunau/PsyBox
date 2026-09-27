#pragma once

#include "Displays.h"
#include "Widgets.h"

#include <memory>
#include <vector>

// Cells of the control grid (see theme::kCellW). x0 is the left edge of the
// first cell, which sits kCellInset left of the first knob.
struct CellGrid
{
    int x0 = 0, y0 = 0;
    juce::Rectangle<int> cell (int col, int row, int span = 1) const
    {
        return { x0 + col * theme::kCellW, y0 + row * (theme::kCellH + theme::kRowGap), span * theme::kCellW, theme::kCellH };
    }
    // A selector spanning cells, aligned to the knob edges, centred in the control area.
    juce::Rectangle<int> control (int col, int row, int span = 1) const
    {
        return cell (col, row, span).reduced (theme::kCellInset, 0).removeFromTop (theme::kKnobD)
            .withSizeKeepingCentre (theme::cellsW (span), theme::kCtrlH);
    }
    // Same span, full cell height, for components that draw their own caption.
    juce::Rectangle<int> captioned (int col, int row, int span = 1) const
    {
        return cell (col, row, span).reduced (theme::kCellInset, 0);
    }
};

// A titled area of the main grid. Header row on top, a two-row body below.
class Section : public juce::Component
{
public:
    explicit Section (juce::String title) : title (std::move (title)) {}
    void paint (juce::Graphics&) override;

protected:
    juce::String title;
    juce::Rectangle<int> header() const { return { theme::kPad, theme::kPad, getWidth() - 2 * theme::kPad, theme::kHeaderH }; }
    juce::Rectangle<int> body() const
    {
        return { theme::kPad, theme::kPad + theme::kHeaderH + theme::kHeaderGap, getWidth() - 2 * theme::kPad, theme::kBodyH };
    }
    int titleWidth() const;   // title plus the gap after it
    // Grid whose first knob starts at x (defaults to the body's left edge).
    CellGrid grid (int x = -1) const { return { (x < 0 ? body().getX() : x) - theme::kCellInset, body().getY() }; }
    static void place (juce::Component* c, juce::Rectangle<int> r) { c->setBounds (r); }

    // Captions drawn under selectors that have none of their own.
    struct Caption { juce::String text; juce::Rectangle<int> area; };
    std::vector<Caption> captions;
    void addCaption (const juce::String& t, juce::Rectangle<int> cell) { captions.push_back ({ t, cell.removeFromBottom (theme::kLabelH) }); }
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
    ParamKnob *shape, *pw, *detune, *wtpos, *ratio, *fm, *fb, *coarse, *fine, *xmod = nullptr;
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

private:
    KnobSet ks;
    ParamKnob *oscA, *oscB, *sub, *noise, *ring;
    Segmented subOct;
    ParamCombo subShape, noiseType;
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

// The arp pattern as a 4 x 4 pad grid (one row per beat). Click to edit.
class ArpPatternView : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    explicit ArpPatternView (PsySynthProcessor&);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    std::function<void()> onClick;

private:
    PsySynthProcessor& proc;
};

// Step editor for the arp pattern, shown over the whole UI: step, accent and
// slide lanes, a ruler that sets the loop length, templates and tools.
class ArpPatternEditor : public juce::Component, private juce::Timer
{
public:
    explicit ArpPatternEditor (PsySynthProcessor&);
    void open();
    void close();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // outside the panel closes

private:
    class Grid : public juce::Component
    {
    public:
        explicit Grid (PsySynthProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override { hover = {}; repaint(); }

    private:
        PsySynthProcessor& proc;
        enum class Row { None, Ruler, On, Accent, Slide };
        struct Hit { Row row = Row::None; int step = -1; };
        Hit hover, drag;
        bool paintValue = true;
        juce::Rectangle<int> rowArea (Row) const;
        juce::Rectangle<int> cellArea (Row, int step) const;
        Hit locate (juce::Point<int>) const;
        void apply (Hit);
    };

    PsySynthProcessor& proc;
    Grid grid;
    juce::TextButton templates { "Templates" }, clear { "Clear" }, done { "Done" }, dice { "Generate" };
    ToggleDot arpOn;
    IconButton shiftLeft { IconButton::Icon::Left, "Shift the pattern one step earlier" };
    IconButton shiftRight { IconButton::Icon::Right, "Shift the pattern one step later" };
    KnobSet ks;
    std::vector<ParamKnob*> knobs;
    juce::Rectangle<int> panel;
    void rotate (int by);
    void showTemplates();
    void timerCallback() override { grid.repaint(); }
};

class VoicePanel : public Section, private juce::Timer
{
public:
    explicit VoicePanel (PsySynthProcessor&);
    void resized() override;
    std::function<void()> onEditPattern;   // the pattern grid was clicked
    // Kick patches have no voice settings: only the arp page, titled "Arp".
    void setKickMode (bool kick);

private:
    PsySynthProcessor& proc;
    TabStrip tabs { { "Voice", "Arp" } };
    Segmented mode, glideMode;
    KnobSet ks;
    std::vector<ParamKnob*> knobs;

    ToggleDot arpOn, arpHold;
    ParamCombo arpMode;
    ArpPatternView pattern;
    std::vector<ParamKnob*> arpKnobs;
    int current = 0;
    bool kickMode = false;
    void show (int page);
    void timerCallback() override;
};

class EnvPanel : public Section
{
public:
    explicit EnvPanel (PsySynthProcessor&);
    void resized() override;

private:
    TabStrip tabs { { "Amp", "Filter", "Mod", "Pitch" } };
    EnvDisplay display;
    PitchEnvDisplay pitchDisplay;
    ParamCombo pitchDest;
    KnobSet ks;
    ParamKnob* knobs[4][4] {};
    int current = 0;
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
        std::unique_ptr<ParamCombo> shape;
        ParamKnob *rate = nullptr, *div = nullptr, *phase = nullptr;
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
    // Pages in signal-chain order.
    enum Fx : int { Dist, Crush, Shift, Phaser, Chorus, Comp, Delay, Reverb, kNumFx };
    PsySynthProcessor& proc;
    TabStrip tabs { { "Dist", "Crush", "Shift", "Phaser", "Chorus", "Comp", "Delay", "Reverb" } };
    struct Page
    {
        std::unique_ptr<ToggleDot> on;
        std::vector<juce::Component*> items;
        std::vector<std::unique_ptr<juce::Component>> owned;
    };
    Page pages[kNumFx];
    KnobSet ks;
    ParamKnob *delayTime = nullptr, *delayDiv = nullptr;
    // Compressor page: a knob set per mode (attack, release and output are in
    // both), the mode switch, and the display on the right.
    std::vector<ParamKnob*> compNormal, compMulti;
    Segmented* compMode = nullptr;
    CompDisplay* compDisplay = nullptr;
    int current = 0;
    bool lastDelaySync = true, lastCompMulti = false;
    void show (int page);
    void layoutComp (const CellGrid&);
    void timerCallback() override;
};

class MacroPanel : public Section, public juce::ChangeListener
{
public:
    explicit MacroPanel (PsySynthProcessor&);
    ~MacroPanel() override;
    void resized() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

private:
    PsySynthProcessor& proc;
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
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

private:
    PsySynthProcessor& proc;
    int hover = -1;
    int scrollY = 0, listH = 0;                            // the category list scrolls inside listH
    psy::Category shownCategory = psy::Category::Count;   // last category scrolled into view
    juce::TextButton generateButton { "Generate" }, mutateButton { "Mutate" }, clearLocks { "Unlock" };
    ToggleDot arpToggle { juce::String() };   // switch only; the caption is painted like SEED
    ToggleDot keyToggle { juce::String() };   // kick categories only: new kicks follow the played key
    juce::Slider mutateAmount;
    juce::Label seedLabel;
    juce::Rectangle<int> seedCaption, arpCaption, keyCaption;

    // Which family the list shows. Follows the category when it changes;
    // the tabs only change the view, a click on a row switches the sound.
    TabStrip familyTabs { { "Synth", "Kick" } };
    psy::Family viewFamily = psy::Family::Synth;
    psy::Category lastCategory = psy::Category::Count;
    const std::vector<psy::Category>& rows() const { return psy::categoriesIn (viewFamily); }
    void showFamily (psy::Family);

    static constexpr int kItemH = 25;
    static constexpr int kListTop = theme::kPad + theme::kHeaderH + theme::sp::sm;
    static constexpr int kInfoH = theme::kPad + 24 + theme::sp::xs + 30;   // category name + blurb
    int categoryAt (juce::Point<int>) const;
    juce::Rectangle<int> rowBounds (int i) const;
    juce::Rectangle<int> listArea() const;
    juce::Rectangle<int> infoArea() const;
    int maxScroll() const;
    void setScroll (int y);
    void scrollToShow (psy::Category);
    void refresh();
};

class TopBar : public juce::Component, public juce::ChangeListener
{
public:
    explicit TopBar (PsySynthProcessor&);
    ~TopBar() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

private:
    PsySynthProcessor& proc;
    IconButton undoButton { IconButton::Icon::Undo, "Undo" };
    IconButton redoButton { IconButton::Icon::Redo, "Redo" };
    IconButton prev { IconButton::Icon::Left, "Previous preset" };
    IconButton next { IconButton::Icon::Right, "Next preset" };
    IconButton menu { IconButton::Icon::Menu, "Presets" };
    IconButton save { IconButton::Icon::Save, "Save preset" };
    Segmented quality;
    ParamKnob volume;
    juce::Rectangle<int> presetArea, qualityCaption, volumeCaption;
    void showPresetMenu();
    void savePresetDialog();
    std::unique_ptr<juce::AlertWindow> saveWindow;
};
