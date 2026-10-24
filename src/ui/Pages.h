#pragma once

#include "KickPanels.h"
#include "PhosPanels.h"
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
    void showNoiseSample (bool show);

private:
    SubPanel sub;
    NoisePanel noise;
    NoiseSampleView noiseView;   // the noise sample's settings, over the sub and noise
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

// The Phoscyon engine's main view, laid out like the original's front panel:
//   the 303 row (the knobs, with the couple LEDs), the filter and the
//   distortion, the calibration trims; then our macros and modulators.
class PhosPage : public Page
{
public:
    explicit PhosPage (PsyBoxProcessor&);
    void resized() override;

private:
    PhosSynthPanel synth;
    PhosFilterPanel filter;
    PhosDistortionPanel distortion;
    PhosCalibrationPanel calibration;
    MacroPanel macros;
    ModPanel mod;
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
        void refresh();   // greys the empty routes and keeps the values current; also run once by the constructor
        void timerCallback() override;
    };
    Panel panel;
};

// ---------------------------------------------------------------------------
// GLOBAL: the oversampling quality, live and for renders. Everything else
// lives next to the sound (Voicing on the OSC page, Vol in the top bar).
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
        Segmented live, render;
        std::array<juce::Rectangle<int>, 2> notes;   // the text beside each selector
    };
    QualityPanel quality;
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
            void mouseDoubleClick (const juce::MouseEvent&) override;
            void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
            void refresh();   // repaints when the playing step or the pattern changed

        private:
            PsyBoxProcessor& proc;
            psy::ArpPattern shown;
            int shownStep = -2;
            bool shownOn = false;
            juce::Colour shownAccent;
            enum class Row { None, Ruler, On, Note, Accent, Slide };
            struct Hit { Row row = Row::None; int step = -1; };
            Hit hover, drag;
            bool paintValue = true;
            int dragNote = 0;     // the note lane value when the drag started
            float wheel = 0.f;    // wheel travel not yet turned into a semitone
            juce::Rectangle<int> rowArea (Row) const;
            juce::Rectangle<int> cellArea (Row, int step) const;
            Hit locate (juce::Point<int>) const;
            void apply (Hit);
            void setNote (int step, int semitones);
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
        void timerCallback() override;
    };
    Panel panel;
};
