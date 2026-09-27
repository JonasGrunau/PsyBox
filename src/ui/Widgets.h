#pragma once

#include "Theme.h"
#include "plugin/PluginProcessor.h"

#include <functional>

// Every control is bound to exactly one parameter through the processor.

// ---------------------------------------------------------------------------
// Rotary knob with label under it. The label turns into the value while the
// knob is hovered or dragged. Right click: lock / reset. Alt-click: lock.
// ---------------------------------------------------------------------------
class ParamKnob : public juce::Component,
                  private juce::Timer
{
public:
    ParamKnob (PsySynthProcessor&, psy::P, const juce::String& labelOverride = {});

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    psy::P getParam() const noexcept { return param; }
    void setLabel (const juce::String& s) { labelText = s; repaint(); }
    static constexpr int kLabelH = 15;

private:
    PsySynthProcessor& proc;
    psy::P param;
    juce::String labelText;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    bool lastLocked = false;

    void timerCallback() override;
    void showMenu();
};

// ---------------------------------------------------------------------------
// Segmented selector for a choice (or toggle) parameter.
// ---------------------------------------------------------------------------
class Segmented : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    Segmented (PsySynthProcessor&, psy::P, juce::StringArray labels = {});

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

    int getSelected() const noexcept { return selected; }
    std::function<void (int)> onChange;

private:
    PsySynthProcessor& proc;
    psy::P param;
    juce::StringArray labels;
    int selected = 0, hover = -1;
    juce::ParameterAttachment attachment;

    int indexAt (juce::Point<int>) const;
};

// ---------------------------------------------------------------------------
// Small dot + label switch for a toggle parameter.
// ---------------------------------------------------------------------------
class ToggleDot : public juce::Component
{
public:
    ToggleDot (PsySynthProcessor&, psy::P, const juce::String& text);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    bool isOn() const noexcept { return on; }
    int idealWidth() const;

private:
    PsySynthProcessor& proc;
    psy::P param;
    juce::String text;
    bool on = false;
    juce::ParameterAttachment attachment;
};

// ---------------------------------------------------------------------------
// Compact choice list with an optional caption.
// ---------------------------------------------------------------------------
class ParamCombo : public juce::Component
{
public:
    ParamCombo (PsySynthProcessor&, psy::P, const juce::String& caption = {});
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
    AmountSlider (PsySynthProcessor&, psy::P);
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
    std::function<void (int)> onChange;
    std::function<bool (int)> isLit;  // draws a state dot before the name

private:
    juce::StringArray names;
    int selected = 0, hover = -1;
    std::vector<juce::Rectangle<int>> tabBounds() const;
};

// ---------------------------------------------------------------------------
// Line icon button.
// ---------------------------------------------------------------------------
class IconButton : public juce::Button
{
public:
    enum class Icon { Undo, Redo, Left, Right, Save, Menu, Dice, Mutate, Lock };
    explicit IconButton (Icon, const juce::String& tooltip = {});
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    Icon icon;
};
