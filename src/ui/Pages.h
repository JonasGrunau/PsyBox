#pragma once

#include "KickPanels.h"
#include "SynthPanels.h"

// The editor's pages (the views picked in the top bar). Each fills the area
// right of the sidebar and under the top bar.

// Draws the hairlines between the sections a page holds.
class Page : public juce::Component
{
public:
    void paint (juce::Graphics& g) override { g.fillAll (theme::col::bg); }
    void paintOverChildren (juce::Graphics&) override;
};

// ---------------------------------------------------------------------------
// OSC: Serum's main view.
//   top:    sub / noise | osc A | osc B | filter 1 / filter 2
//   bottom: macros | modulators | voicing / length or knock
// ---------------------------------------------------------------------------
class OscPage : public Page
{
public:
    explicit OscPage (PsyBoxProcessor&);
    void resized() override;
    // What sits under the voicing: the length (energy sounds), the knock
    // (basses) or nothing (melodic sounds: the voicing takes the column; their
    // arp has its own page).
    enum class Extra { Length, Knock, None };
    void setExtra (Extra);

private:
    SubPanel sub;
    NoisePanel noise;
    OscPanel oscA, oscB;
    FilterPanel f1, f2;
    MacroPanel macros;
    ModPanel mod;
    VoicingPanel voicing;
    LengthPanel length;
    KnockPanel knock;
    Extra extra = Extra::None;
};

// The kick engine's sections; kicks have no matrix and no macros.
class KickPage : public Page
{
public:
    explicit KickPage (PsyBoxProcessor&);
    void resized() override;

private:
    KickPitchPanel pitch;
    KickAmpPanel amp;
    KickClickPanel click;
    KickDrivePanel drive;
    KickWavePanel wave;
};

// ---------------------------------------------------------------------------
// FX: every effect at once, in signal-chain order (left to right, top to
// bottom). Each module switches on in its header.
// ---------------------------------------------------------------------------
class FxModule : public Section, private juce::Timer
{
public:
    FxModule (PsyBoxProcessor&, int chainIndex, const juce::String& title, psy::P onParam);
    void paint (juce::Graphics&) override;
    void resized() override;

    ParamKnob* knob (psy::P, int col, int row, const juce::String& label = {});
    void segmented (psy::P, int col, int row, int span, const juce::String& caption, juce::StringArray labels = {});
    void toggle (psy::P, int col, int row, const juce::String& text);
    void headerControl (std::unique_ptr<juce::Component>, int width);
    void showWhen (juce::Component*, std::function<bool()> visible);   // re-checked on a timer
    void setSideDisplay (std::unique_ptr<juce::Component>);            // fills the body right of the cells

protected:
    PsyBoxProcessor& proc;
    int index;
    psy::P onParam;
    int shownActive = -1;   // on state the controls were last greyed for
    ToggleDot on;
    KnobSet ks;
    struct Item { juce::Component* c; int col, row, span; bool captioned, control; juce::String caption; };
    std::vector<Item> items;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::pair<juce::Component*, std::function<bool()>>> conditional;
    juce::Component* headerExtra = nullptr;
    int headerExtraW = 0;
    juce::Component* side = nullptr;
    int columns() const;
    void timerCallback() override;
};

class FxPage : public Page
{
public:
    explicit FxPage (PsyBoxProcessor&);
    void resized() override;

private:
    std::vector<std::unique_ptr<FxModule>> modules;
};

// ---------------------------------------------------------------------------
// MATRIX: all sixteen routes as rows: source, amount, target.
// ---------------------------------------------------------------------------
class MatrixPage : public Page
{
public:
    explicit MatrixPage (PsyBoxProcessor&);
    void resized() override;

private:
    class Panel : public Section, private juce::Timer
    {
    public:
        explicit Panel (PsyBoxProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct Row
        {
            std::unique_ptr<ParamCombo> src, dst;
            std::unique_ptr<AmountSlider> amt;
            std::unique_ptr<IconButton> clear;
        };
        PsyBoxProcessor& proc;
        std::array<Row, psy::kNumModSlots> rows;
        std::array<juce::Rectangle<int>, psy::kNumModSlots> numberArea, valueArea;
        std::array<float, psy::kNumModSlots> shown {};
        juce::Rectangle<int> heads;
        int srcX = 0, amtX = 0, amtW = 0, dstX = 0;
        void timerCallback() override;
    };
    Panel panel;
};

// ---------------------------------------------------------------------------
// GLOBAL: quality, the full voice settings, output.
// ---------------------------------------------------------------------------
class GlobalPage : public Page
{
public:
    explicit GlobalPage (PsyBoxProcessor&);
    void resized() override;

private:
    class QualityPanel : public Section
    {
    public:
        explicit QualityPanel (PsyBoxProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        Segmented quality;
    };
    class VoicePanel : public Section
    {
    public:
        explicit VoicePanel (PsyBoxProcessor&);
        void resized() override;

    private:
        Segmented mode, glideMode;
        KnobSet ks;
    };
    class OutputPanel : public Section
    {
    public:
        explicit OutputPanel (PsyBoxProcessor&);
        void resized() override;

    private:
        KnobSet ks;
    };
    class AboutPanel : public Section
    {
    public:
        AboutPanel() : Section ("Guide") {}
        void paint (juce::Graphics&) override;
    };
    QualityPanel quality;
    VoicePanel voice;
    OutputPanel output;
    AboutPanel about;
};

// ---------------------------------------------------------------------------
// ARP: the arp's settings, the step editor (step, accent and slide lanes, a
// ruler that sets the loop length) and the templates under it.
// ---------------------------------------------------------------------------
class ArpPage : public Page
{
public:
    explicit ArpPage (PsyBoxProcessor&);
    void resized() override;

private:
    class Panel : public Section, private juce::Timer
    {
    public:
        explicit Panel (PsyBoxProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class Grid : public juce::Component
        {
        public:
            explicit Grid (PsyBoxProcessor& p) : proc (p) {}
            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent&) override;
            void mouseDrag (const juce::MouseEvent&) override;
            void mouseMove (const juce::MouseEvent&) override;
            void mouseExit (const juce::MouseEvent&) override { hover = {}; repaint(); }

        private:
            PsyBoxProcessor& proc;
            enum class Row { None, Ruler, On, Accent, Slide };
            struct Hit { Row row = Row::None; int step = -1; };
            Hit hover, drag;
            bool paintValue = true;
            juce::Rectangle<int> rowArea (Row) const;
            juce::Rectangle<int> cellArea (Row, int step) const;
            Hit locate (juce::Point<int>) const;
            void apply (Hit);
        };

        // The classic figures as cards (name and a strip of the steps); a
        // click loads one, the card matching the pattern is outlined.
        class Templates : public juce::Component
        {
        public:
            explicit Templates (PsyBoxProcessor& p) : proc (p) {}
            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent&) override;
            void mouseMove (const juce::MouseEvent&) override;
            void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
            void refresh();   // repaints when the pattern changed
            static constexpr int kCols = 5, kCardH = 56;
            static int heightFor (int rows) { return theme::kLabelH + theme::sp::sm + rows * kCardH + (rows - 1) * theme::sp::sm; }

        private:
            PsyBoxProcessor& proc;
            int hover = -1;
            psy::ArpPattern shown;
            juce::Rectangle<int> cardArea (int i) const;
            int cardAt (juce::Point<int>) const;
        };

        PsyBoxProcessor& proc;
        Grid grid;
        Templates templates;
        juce::TextButton clear { "Clear" }, dice { "Generate" };
        ToggleDot arpOn, hold;
        ParamCombo mode;
        IconButton shiftLeft { IconButton::Icon::Left, "Shift the pattern one step earlier" };
        IconButton shiftRight { IconButton::Icon::Right, "Shift the pattern one step later" };
        KnobSet ks;
        std::vector<ParamKnob*> knobs;
        juce::Rectangle<int> hintArea;
        void rotate (int by);
        void timerCallback() override
        {
            if (! grid.isShowing()) return;
            grid.repaint();
            templates.refresh();
        }
    };
    Panel panel;
};
