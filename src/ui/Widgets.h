#pragma once

#include "Theme.h"
#include "plugin/PluginProcessor.h"

#include <array>
#include <functional>

// Every control is bound to exactly one parameter through the processor.

// ---------------------------------------------------------------------------
// Modulation drag-and-drop. A ModSourceHandle starts the drag; any knob whose
// parameter is a mod target accepts it and adds a matrix route.
// ---------------------------------------------------------------------------
namespace moddrag
{
    juce::var describe (psy::ModSrc);
    psy::ModSrc sourceOf (const juce::var& description);   // ModSrc::None when it isn't a mod drag
    juce::String name (psy::ModSrc);                       // "LFO 1", "Amp Env", "Macro 2"...
    bool bipolar (psy::ModSrc);                            // LFOs, note, bend, random swing both ways
}

// ---------------------------------------------------------------------------
// Rotary knob with label under it. The label turns into the value while the
// knob is hovered or dragged. Right click: lock / reset / modulation.
// Alt-click: lock.
// Mod targets take dropped mod sources. Each route shows as a ring between
// the knob body and its track; drag the ring to change the amount (the
// selected source's ring first), right-click to invert or remove it.
// ---------------------------------------------------------------------------
class ParamKnob : public juce::Component,
                  public juce::DragAndDropTarget,
                  private juce::Timer
{
public:
    ParamKnob (PsyBoxProcessor&, psy::P, const juce::String& labelOverride = {});

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { ringHover = false; repaint(); }

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

    psy::P getParam() const noexcept { return param; }
    void setLabel (const juce::String& s) { labelText = s; repaint(); }
    void setCompact (bool c) { compact = c; resized(); }   // no label, knob fills the bounds
    // Double-clicking the label edits it in place; called with the new text.
    std::function<void (const juce::String&)> onRename;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    // Leaves the ring between body and track to the knob, so dragging there
    // edits the modulation instead of the value.
    class KnobSlider : public juce::Slider
    {
    public:
        std::function<bool (juce::Point<int>)> passThrough;
        bool hitTest (int x, int y) override { return ! (passThrough && passThrough ({ x, y })) && Slider::hitTest (x, y); }
    };

    PsyBoxProcessor& proc;
    psy::P param;
    psy::ModDst target = psy::ModDst::None;
    juce::String labelText;
    KnobSlider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    bool lastLocked = false, compact = false;
    std::unique_ptr<juce::TextEditor> editor;
    void startRename();
    void finishRename (bool commit);

    // modulation
    std::array<PsyBoxProcessor::ModRoute, psy::kNumModSlots> routes {};
    int numRoutes = 0;
    uint64_t routePrint = 0;
    bool dropHover = false, ringHover = false, dragShown = false;
    int ringSlot = -1;
    float ringStartAmount = 0.f, ringStartY = 0.f;
    juce::Point<float> knobCentre() const;
    float trackRadius() const;
    bool inRing (juce::Point<float>) const;
    int editableRoute() const;   // index into routes: the selected source, else the first
    void refreshRoutes();

    void timerCallback() override;
    void showMenu();
};

// ---------------------------------------------------------------------------
// A mod source you can drag onto knobs: a four-way arrow, optionally with a
// label. Lit when the source drives something. As a tab (setTab) it also
// selects: click to show its page, drag to modulate.
// ---------------------------------------------------------------------------
class ModSourceHandle : public juce::Component,
                        public juce::SettableTooltipClient,
                        private juce::Timer
{
public:
    ModSourceHandle (PsyBoxProcessor&, psy::ModSrc, const juce::String& label = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    psy::ModSrc getSource() const noexcept { return source; }
    void setLabel (const juce::String& l) { label = l; repaint(); }
    void setTab (bool isTab) { tab = isTab; repaint(); }
    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
    bool isSelected() const noexcept { return selected; }
    int idealWidth() const;
    std::function<void()> onClick;
    static void drawIcon (juce::Graphics&, juce::Rectangle<float>, juce::Colour);

private:
    PsyBoxProcessor& proc;
    psy::ModSrc source;
    juce::String label;
    bool tab = false, selected = false, used = false, dragging = false;
    void timerCallback() override;
};

// ---------------------------------------------------------------------------
// A number you drag up and down, scroll, or step with the arrows (like
// Serum's unison and pitch fields). Inline style draws the caption inside
// the field ("SEM +7"); cell style puts the field in a knob cell's control
// area with the caption under it. With a gear it also opens settings
// (onGear). Double-click resets, right-click locks. Takes dropped mod
// sources when its parameter is a mod target.
// ---------------------------------------------------------------------------
class NumberBox : public juce::Component,
                  public juce::DragAndDropTarget,
                  public juce::SettableTooltipClient,
                  private juce::Timer
{
public:
    NumberBox (PsyBoxProcessor&, psy::P, const juce::String& caption);

    void setCellStyle (bool c) { cell = c; repaint(); }
    void setGear (bool g) { gear = g; repaint(); }
    void setGearOn (bool on) { if (on != gearOn) { gearOn = on; repaint(); } }
    // Shown instead of the value (the supersaw's fixed seven voices).
    void setOverride (const juce::String& text) { if (text != overrideText) { overrideText = text; repaint(); } }
    int idealWidth() const;   // inline style
    psy::P getParam() const noexcept { return param; }

    std::function<void()> onGear;
    std::function<void (bool)> onActive;   // hovered or dragged, or neither any more

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

private:
    PsyBoxProcessor& proc;
    psy::P param;
    psy::ModDst target;
    juce::String caption, overrideText;
    juce::ParameterAttachment attachment;
    float value = 0.f, dragStart = 0.f;
    bool cell = false, gear = false, gearOn = false, dragging = false, dropHover = false, dragShown = false;
    int numRoutes = 0;
    std::array<PsyBoxProcessor::ModRoute, psy::kNumModSlots> routes {};

    juce::Rectangle<float> field() const;       // the box itself
    juce::Rectangle<float> gearArea() const;
    juce::Rectangle<float> arrowsArea() const;
    juce::String text() const;
    void setValue (float plain, bool gesture);
    void step (int dir);
    void showMenu();
    void timerCallback() override;
};

// ---------------------------------------------------------------------------
// Segmented selector for a choice (or toggle) parameter.
// ---------------------------------------------------------------------------
class Segmented : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    Segmented (PsyBoxProcessor&, psy::P, juce::StringArray labels = {});

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

    int getSelected() const noexcept { return selected; }
    int getNumChoices() const noexcept { return labels.size(); }
    // Stacked: one row per choice instead of side by side.
    void setVertical (bool v) { vertical = v; repaint(); }
    std::function<void (int)> onChange;
    // When set, a click calls this instead of writing the parameter (for
    // switches that convert other values first, like bars <-> seconds).
    std::function<void (int)> onPick;

private:
    PsyBoxProcessor& proc;
    psy::P param;
    juce::StringArray labels;
    int selected = 0, hover = -1;
    bool vertical = false;
    juce::ParameterAttachment attachment;

    int indexAt (juce::Point<int>) const;
    std::vector<float> segmentWidths (float total) const;
};

// ---------------------------------------------------------------------------
// Small dot + label switch for a toggle parameter, or for a plain flag
// (text-only constructor: drive it with setOn, listen with onToggle).
// ---------------------------------------------------------------------------
class ToggleDot : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    ToggleDot (PsyBoxProcessor&, psy::P, const juce::String& text);
    explicit ToggleDot (const juce::String& text);
    void setOn (bool shouldBeOn) { if (on != shouldBeOn) { on = shouldBeOn; repaint(); } }
    std::function<void (bool)> onToggle;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    bool isOn() const noexcept { return on; }
    int idealWidth() const;
    // Cell style: a small switch in the control area, the label underneath,
    // matching knob cells.
    void setStacked (bool s) { stacked = s; repaint(); }

private:
    juce::String text;
    bool on = false, stacked = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// ---------------------------------------------------------------------------
// Compact choice list. With a caption it lays out like a knob cell: the box
// centred in the control area, the caption underneath.
// ---------------------------------------------------------------------------
class ParamCombo : public juce::Component
{
public:
    ParamCombo (PsyBoxProcessor&, psy::P, const juce::String& caption = {});
    void setTooltip (const juce::String& t) { box.setTooltip (t); }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String caption;
    juce::ComboBox box;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// ---------------------------------------------------------------------------
// Horizontal bipolar amount slider (mod matrix).
// ---------------------------------------------------------------------------
class AmountSlider : public juce::Component
{
public:
    AmountSlider (PsyBoxProcessor&, psy::P);
    void resized() override { slider.setBounds (getLocalBounds()); }

private:
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// ---------------------------------------------------------------------------
// Tabs drawn as text with an underline; optional per-tab state LED.
// ---------------------------------------------------------------------------
class TabStrip : public juce::Component
{
public:
    explicit TabStrip (juce::StringArray names);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

    int getSelected() const noexcept { return selected; }
    void setSelected (int i);
    int idealWidth() const;
    std::function<void (int)> onChange;
    std::function<bool (int)> isLit;  // draws a state dot before the name
    std::function<bool (int)> hasLed; // with isLit: which tabs get a dot (all when unset)

private:
    juce::StringArray names;
    int selected = 0, hover = -1;
    std::vector<juce::Rectangle<int>> tabBounds() const;
    bool ledOn (int i) const { return isLit && (! hasLed || hasLed (i)); }
};

// ---------------------------------------------------------------------------
// Line icon button.
// ---------------------------------------------------------------------------
class IconButton : public juce::Button
{
public:
    enum class Icon { Undo, Redo, Left, Right, Save, Menu, Dice, Mutate, Lock, Close };
    explicit IconButton (Icon, const juce::String& tooltip = {});
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    Icon icon;
};
