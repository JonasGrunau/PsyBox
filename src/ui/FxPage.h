#pragma once

#include "Pages.h"

// ---------------------------------------------------------------------------
// FX: laid out like Serum 2's rack. The chain on the left lists every effect
// in signal order (click a row to switch it on or off, drag it up or down to
// move it in the chain); the rack on the right holds one strip per effect, in
// the same order: a badge, the controls, a display, the mix and the power
// switch. Switched-off strips grey out. The Phoscyon engine gets its own rack:
// the original's distortion, chorus, delay and reverb in ours' places, its EQ
// and limiter added, eleven strips that scroll.
// ---------------------------------------------------------------------------
class FxPage : public Page
{
public:
    explicit FxPage (PsyBoxProcessor&, bool phoscyon = false);
    void resized() override;
    void showStrip (int effect);   // scrolls the Phoscyon rack to a strip
    void paintOverChildren (juce::Graphics&) override;

    enum class Glyph { Distortion, Crush, Shift, Phaser, Chorus, Compressor, Delay, Reverb, Width, Eq, Limiter };
    using Order = std::vector<int>;   // effect ids, first to last

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
        void segmented (psy::P, int col, int span, const juce::String& caption, int row = 0);
        void combo (psy::P, int col, int span, const juce::String& caption, int row = 0);
        void toggle (psy::P, int col, const juce::String& text, int row = 0);
        void selector (psy::P);                                    // stacked choice before the cells
        ParamKnob* mix (psy::P, const juce::String& label = {});   // the knob in the mix column
        void display (std::unique_ptr<juce::Component>, int width = -1);   // -1: fill up to the mix column
        void showWhen (juce::Component*, std::function<bool()> visible);   // re-checked on a timer

        void setHighlight (bool);
        void setIndex (int i) { if (i != index) { index = i; repaint(); } }   // its place in the chain, from 1
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
        void refresh();   // conditional controls and the greyed-out state; FxPage runs it once the strip is built
        void timerCallback() override;

        // Hover over a child counts as hovering the strip. A listener of its own, so the
        // strip's mouseWheelMove (which scrolls the rack) isn't also called for the
        // wheel events the children get.
        struct HoverListener : juce::MouseListener
        {
            explicit HoverListener (Strip& s) : strip (s) {}
            void mouseEnter (const juce::MouseEvent&) override { strip.hoverChanged(); }
            void mouseExit (const juce::MouseEvent&) override { strip.hoverChanged(); }
            Strip& strip;
        } hoverListener { *this };
        friend class FxPage;
    };

    // The chain: a row per effect, in signal order, with its place, name and
    // power state. A click switches the effect; a drag moves it in the chain:
    // the dragged row follows the mouse, the others slide out of its way and
    // leave its landing place empty, and on release it settles there.
    class Chain : public juce::Component, private juce::Timer
    {
    public:
        Chain (PsyBoxProcessor&, const std::vector<std::pair<juce::String, psy::P>>& effects);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
        void setHover (int effect);
        void setOrder (const Order& o);
        std::function<void (int)> onHover;                 // effect under the mouse, -1 when none
        std::function<void (const Order&)> onReorder;     // a row was dragged somewhere else

        static constexpr int kRowH = 30;

    private:
        struct Row
        {
            juce::String name;
            bool on = false;
            std::unique_ptr<juce::ParameterAttachment> attachment;
        };
        std::vector<Row> rows;                // by effect
        Order order;                          // effects, top to bottom
        int hover = -1;                       // effect
        int pressed = -1, slot = -1;          // row the drag started on, place it would land in
        bool dragging = false, placed = false;
        float dragY = 0.f, grabOffset = 0.f;  // mouse y, and where on the row it grabbed
        std::vector<float> rowY;              // where each effect's row is drawn, easing to its place
        juce::Rectangle<int> rowArea (int i) const;
        int rowAt (juce::Point<int>) const;   // row (place in the chain) under the point
        int slotAt (float rowTop) const;      // place for a row whose top is at rowTop
        Order shown() const;                  // the order as it would be if dropped now
        void timerCallback() override;        // eases the rows
        void paintRow (juce::Graphics&, int effect, juce::Rectangle<int>, int place, bool lit, bool band = true) const;
    };

private:
    PsyBoxProcessor& proc;
    const bool phos;
    std::vector<std::unique_ptr<Strip>> strips;   // by effect
    std::unique_ptr<Chain> chain;
    Strip* tall = nullptr;   // our page: the compressor, two rows and a bigger display
    Order order;
    juce::ParameterAttachment orderAttachment;
    std::unique_ptr<juce::ParameterAttachment> eqSlot, limSlot;   // the Phoscyon rack's own two slots
    // The Phoscyon rack scrolls; its strips sit in here, hairlines between them.
    struct RackContent : juce::Component
    {
        std::function<void (juce::Graphics&)> over;
        void paintOverChildren (juce::Graphics& g) override { if (over) over (g); }
    };
    juce::Viewport rack;
    RackContent rackContent;
    int lastHovered = -1;
    bool reordering = false;   // the chain is being written: orderChanged waits for the last parameter
    Order currentOrder() const;
    void orderChanged();
};
