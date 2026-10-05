#pragma once

#include "Pages.h"

// ---------------------------------------------------------------------------
// FX: laid out like Serum 2's rack. The chain on the left lists every effect
// in signal order (click a row to switch it on or off); the rack on the right
// holds one strip per effect: a badge, the controls, a display, the mix and
// the power switch. Switched-off strips grey out.
// ---------------------------------------------------------------------------
class FxPage : public Page
{
public:
    explicit FxPage (PsyBoxProcessor&);
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;

    enum class Glyph { Distortion, Crush, Shift, Phaser, Chorus, Compressor, Delay, Reverb, Width };

    // The square power switch on the right of every strip.
    class Power : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        Power (PsyBoxProcessor&, psy::P onParam);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        static void drawIcon (juce::Graphics&, juce::Rectangle<float>, juce::Colour);

    private:
        bool on = false;
        juce::ParameterAttachment attachment;
    };

    class Strip : public juce::Component, private juce::Timer
    {
    public:
        Strip (PsyBoxProcessor&, int index, const juce::String& name, Glyph, psy::P onParam);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseEnter (const juce::MouseEvent&) override { hoverChanged(); }
        void mouseExit (const juce::MouseEvent&) override { hoverChanged(); }

        // Controls sit in one row of cells (two for a tall strip) after the
        // badge, or after the selector when there is one.
        ParamKnob* knob (psy::P, int col, int row = 0, const juce::String& label = {});
        void segmented (psy::P, int col, int span, const juce::String& caption);
        void toggle (psy::P, int col, const juce::String& text);
        void selector (psy::P);                                    // stacked choice before the cells
        ParamKnob* mix (psy::P, const juce::String& label = {});   // the knob in the mix column
        void display (std::unique_ptr<juce::Component>, int width = -1);   // -1: fill up to the mix column
        void showWhen (juce::Component*, std::function<bool()> visible);   // re-checked on a timer

        void setHighlight (bool);
        std::function<void (bool)> onHover;   // the mouse entered or left the strip

        static constexpr int kSelectorW = 72;

    private:
        PsyBoxProcessor& proc;
        int index;
        juce::String name;
        Glyph glyph;
        psy::P onParam;
        Power power;
        KnobSet ks;
        struct Item { juce::Component* c; int col, row, span; bool control; juce::String caption; };
        std::vector<Item> items;
        std::vector<std::unique_ptr<juce::Component>> owned;
        std::vector<std::pair<juce::Component*, std::function<bool()>>> conditional;
        std::unique_ptr<Segmented> select;
        std::vector<ParamKnob*> mixKnobs;   // one per label (the compressor's Mix is Depth in Multi)
        juce::Component* side = nullptr;
        int sideW = 0, shownActive = -1;
        bool highlight = false, hovered = false;
        struct Caption { juce::String text; juce::Rectangle<int> area; };
        std::vector<Caption> captions;
        juce::Rectangle<int> badge, mixColumn;

        int rows() const;
        int columns() const;
        int gridX() const;
        void hoverChanged();
        void timerCallback() override;
        friend class FxPage;
    };

    // The chain: a row per effect with its number, name and power state.
    class Chain : public juce::Component
    {
    public:
        Chain (PsyBoxProcessor&, const std::vector<std::pair<juce::String, psy::P>>& effects);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
        void setHover (int row);
        std::function<void (int)> onHover;   // row under the mouse, -1 when none

        static constexpr int kRowH = 30;

    private:
        struct Row
        {
            juce::String name;
            bool on = false;
            std::unique_ptr<juce::ParameterAttachment> attachment;
        };
        std::vector<Row> rows;
        int hover = -1;
        juce::Rectangle<int> rowArea (int i) const;
        int rowAt (juce::Point<int>) const;
    };

private:
    std::vector<std::unique_ptr<Strip>> strips;
    std::unique_ptr<Chain> chain;
    Strip* tall = nullptr;   // the compressor: two rows and a bigger display
};
