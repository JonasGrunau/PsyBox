#include "Widgets.h"

using namespace theme;
using psy::P;

// ---------------------------------------------------------------------------
// ParamKnob
// ---------------------------------------------------------------------------
ParamKnob::ParamKnob (PsySynthProcessor& p, P prm, const juce::String& labelOverride)
    : proc (p), param (prm), labelText (labelOverride.isNotEmpty() ? labelOverride : juce::String (psy::def (prm).name))
{
    const auto& d = psy::def (prm);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (225.f), juce::degreesToRadians (495.f), true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityModeParameters (0.6, 1, 0.08, true, juce::ModifierKeys::shiftModifier);
    slider.getProperties().set ("bipolar", d.min < 0.f && d.max > 0.f);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.setTooltip (juce::String (d.group) + " " + d.name + "  -  right-click to lock or reset");
    slider.onDragStart = [this] { repaint(); };
    slider.onDragEnd = [this] { repaint(); };
    slider.onValueChange = [this] { repaint(); };
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, d.id, slider);
    slider.setDoubleClickReturnValue (true, d.def);
    startTimerHz (10);
}

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (kLabelH);
    const int d = std::min (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (d, d));
}

void ParamKnob::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().removeFromBottom (kLabelH);
    const bool hot = slider.isMouseOverOrDragging();
    if (hot)
    {
        g.setColour (col::text);
        g.setFont (mono (10.5f));
        g.drawText (psy::def (param).format (proc.plain (param)), area, juce::Justification::centred, false);
    }
    else
    {
        g.setColour (isEnabled() ? col::textDim : col::textFaint);
        g.setFont (label (10.f));
        g.drawText (labelText.toUpperCase(), area, juce::Justification::centred, false);
    }
}

void ParamKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showMenu();
    else if (e.mods.isAltDown())
        proc.setLocked (param, ! proc.isLocked (param));
}

void ParamKnob::showMenu()
{
    juce::PopupMenu m;
    const bool locked = proc.isLocked (param);
    m.addSectionHeader (juce::String (psy::def (param).group) + " / " + psy::def (param).name);
    m.addItem (1, locked ? "Unlock" : "Lock (keep on Generate)");
    m.addItem (2, "Reset to default");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider), [this, locked] (int r) {
        if (r == 1) proc.setLocked (param, ! locked);
        if (r == 2)
        {
            auto* p = proc.param (param);
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
    });
}

void ParamKnob::timerCallback()
{
    const bool locked = proc.isLocked (param);
    if (locked != lastLocked)
    {
        lastLocked = locked;
        slider.getProperties().set ("locked", locked);
        slider.repaint();
    }
}

// ---------------------------------------------------------------------------
// Segmented
// ---------------------------------------------------------------------------
Segmented::Segmented (PsySynthProcessor& p, P prm, juce::StringArray l)
    : proc (p), param (prm), labels (std::move (l)),
      attachment (*p.param (prm), [this] (float v) {
          selected = (int) std::lround (v);
          repaint();
          if (onChange) onChange (selected);
      })
{
    const auto& d = psy::def (prm);
    if (labels.isEmpty())
    {
        if (d.choices != nullptr)
            for (const auto& c : *d.choices) labels.add (c);
        else
            labels = { "Off", "On" };
    }
    attachment.sendInitialUpdate();
}

int Segmented::indexAt (juce::Point<int> pt) const
{
    const float w = (float) getWidth() / (float) labels.size();
    return juce::jlimit (0, labels.size() - 1, (int) ((float) pt.x / w));
}

void Segmented::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (col::lineStrong);
    g.drawRoundedRectangle (b, 3.f, 1.f);
    const float w = b.getWidth() / (float) labels.size();
    g.setFont (heading (9.5f));
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto seg = juce::Rectangle<float> (b.getX() + w * (float) i, b.getY(), w, b.getHeight());
        if (i == selected)
        {
            g.setColour (accent().withAlpha (0.16f));
            g.fillRoundedRectangle (seg.reduced (2.f), 2.f);
        }
        if (i > 0)
        {
            g.setColour (col::line);
            g.fillRect (seg.getX(), seg.getY() + 5.f, 1.f, seg.getHeight() - 10.f);
        }
        g.setColour (i == selected ? accent() : (i == hover ? col::text : col::textDim));
        g.drawText (labels[i].toUpperCase(), seg.toNearestInt(), juce::Justification::centred, false);
    }
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    attachment.setValueAsCompleteGesture ((float) i);
}

void Segmented::mouseMove (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    if (i != hover) { hover = i; repaint(); }
}

// ---------------------------------------------------------------------------
// ToggleDot
// ---------------------------------------------------------------------------
ToggleDot::ToggleDot (PsySynthProcessor& p, P prm, const juce::String& t)
    : proc (p), param (prm), text (t),
      attachment (*p.param (prm), [this] (float v) { on = v > 0.5f; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int ToggleDot::idealWidth() const
{
    return 16 + juce::GlyphArrangement::getStringWidthInt (heading (9.5f), text.toUpperCase()) + 4;
}

void ToggleDot::paint (juce::Graphics& g)
{
    const float cy = (float) getHeight() * 0.5f;
    const auto dot = juce::Rectangle<float> (1.f, cy - 4.f, 8.f, 8.f);
    if (on)
    {
        g.setColour (accent());
        g.fillEllipse (dot);
    }
    else
    {
        g.setColour (isMouseOver() ? col::textDim : col::textFaint);
        g.drawEllipse (dot.reduced (0.5f), 1.f);
    }
    g.setColour (on ? col::text : (isMouseOver() ? col::text : col::textDim));
    g.setFont (heading (9.5f));
    g.drawText (text.toUpperCase(), getLocalBounds().withTrimmedLeft (15), juce::Justification::centredLeft, false);
}

void ToggleDot::mouseDown (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (on ? 0.f : 1.f);
}

// ---------------------------------------------------------------------------
// ParamCombo
// ---------------------------------------------------------------------------
ParamCombo::ParamCombo (PsySynthProcessor& p, P prm, const juce::String& cap) : caption (cap)
{
    const auto& d = psy::def (prm);
    int id = 1;
    for (const auto& c : *d.choices)
        box.addItem (c, id++);
    addAndMakeVisible (box);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, d.id, box);
}

void ParamCombo::resized()
{
    auto r = getLocalBounds();
    if (caption.isNotEmpty())
        r.removeFromTop (ParamKnob::kLabelH - 2);
    box.setBounds (r);
}

void ParamCombo::paint (juce::Graphics& g)
{
    if (caption.isEmpty())
        return;
    g.setColour (col::textDim);
    g.setFont (label (10.f));
    g.drawText (caption.toUpperCase(), getLocalBounds().removeFromTop (ParamKnob::kLabelH - 2), juce::Justification::centredLeft, false);
}

// ---------------------------------------------------------------------------
// AmountSlider
// ---------------------------------------------------------------------------
AmountSlider::AmountSlider (PsySynthProcessor& p, P prm)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.getProperties().set ("bipolar", true);
    slider.setDoubleClickReturnValue (true, 0.0);
    slider.setVelocityModeParameters (0.6, 1, 0.08, true, juce::ModifierKeys::shiftModifier);
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, psy::def (prm).id, slider);
    slider.onValueChange = [this, &p, prm] {
        slider.setTooltip (psy::def (prm).format (p.plain (prm)));
    };
}

// ---------------------------------------------------------------------------
// TabStrip
// ---------------------------------------------------------------------------
TabStrip::TabStrip (juce::StringArray n) : names (std::move (n)) {}

std::vector<juce::Rectangle<int>> TabStrip::tabBounds() const
{
    std::vector<juce::Rectangle<int>> out;
    int x = 0;
    const auto f = heading (9.5f);
    for (int i = 0; i < names.size(); ++i)
    {
        const bool lit = (bool) isLit;
        const int w = juce::GlyphArrangement::getStringWidthInt (f, names[i].toUpperCase()) + (lit ? 12 : 0) + 16;
        out.push_back ({ x, 0, w, getHeight() });
        x += w;
    }
    return out;
}

void TabStrip::setSelected (int i)
{
    selected = juce::jlimit (0, names.size() - 1, i);
    repaint();
}

void TabStrip::paint (juce::Graphics& g)
{
    const auto tabs = tabBounds();
    g.setFont (heading (9.5f));
    for (int i = 0; i < names.size(); ++i)
    {
        auto r = tabs[(size_t) i];
        const bool sel = i == selected;
        if (sel)
        {
            g.setColour (accent());
            g.fillRect (r.getX() + 8, r.getBottom() - 2, r.getWidth() - 16, 2);
        }
        auto tr = r.reduced (8, 0);
        if (isLit)
        {
            const bool lit = isLit (i);
            const float cy = (float) r.getCentreY() - 1.f;
            g.setColour (lit ? accent() : col::textFaint.withAlpha (0.6f));
            if (lit) g.fillEllipse ((float) tr.getX(), cy - 2.5f, 5.f, 5.f);
            else g.drawEllipse ((float) tr.getX() + 0.5f, cy - 2.f, 4.f, 4.f, 1.f);
            tr.removeFromLeft (11);
        }
        g.setColour (sel ? col::text : (i == hover ? col::text.withAlpha (0.8f) : col::textDim));
        g.drawText (names[i].toUpperCase(), tr.withTrimmedBottom (2), juce::Justification::centredLeft, false);
    }
}

void TabStrip::mouseDown (const juce::MouseEvent& e)
{
    const auto tabs = tabBounds();
    for (int i = 0; i < (int) tabs.size(); ++i)
        if (tabs[(size_t) i].contains (e.getPosition()))
        {
            setSelected (i);
            if (onChange) onChange (i);
        }
}

void TabStrip::mouseMove (const juce::MouseEvent& e)
{
    const auto tabs = tabBounds();
    int h = -1;
    for (int i = 0; i < (int) tabs.size(); ++i)
        if (tabs[(size_t) i].contains (e.getPosition())) h = i;
    if (h != hover) { hover = h; repaint(); }
}

// ---------------------------------------------------------------------------
// IconButton
// ---------------------------------------------------------------------------
IconButton::IconButton (Icon i, const juce::String& tip) : juce::Button (tip), icon (i)
{
    setTooltip (tip);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto b = getLocalBounds().toFloat();
    if (over || down)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.08f : 0.045f));
        g.fillRoundedRectangle (b, 4.f);
    }
    const auto c = b.getCentre();
    const float s = std::min (b.getWidth(), b.getHeight()) * 0.26f;
    juce::Path p;
    switch (icon)
    {
        case Icon::Undo:
        case Icon::Redo:
        {
            const float dir = icon == Icon::Undo ? -1.f : 1.f;
            p.addCentredArc (c.x, c.y + s * 0.2f, s, s, 0.f, dir * -1.9f, dir * 0.9f, true);
            const auto tip = juce::Point<float> (c.x, c.y + s * 0.2f).getPointOnCircumference (s, dir * -1.9f);
            p.startNewSubPath (tip.x + dir * 3.2f, tip.y - 2.8f);
            p.lineTo (tip);
            p.lineTo (tip.x + dir * 3.8f, tip.y + 2.2f);
            break;
        }
        case Icon::Left:
            p.startNewSubPath (c.x + s * 0.4f, c.y - s * 0.8f);
            p.lineTo (c.x - s * 0.4f, c.y);
            p.lineTo (c.x + s * 0.4f, c.y + s * 0.8f);
            break;
        case Icon::Right:
            p.startNewSubPath (c.x - s * 0.4f, c.y - s * 0.8f);
            p.lineTo (c.x + s * 0.4f, c.y);
            p.lineTo (c.x - s * 0.4f, c.y + s * 0.8f);
            break;
        case Icon::Save:
            p.addRoundedRectangle (c.x - s, c.y - s, s * 2.f, s * 2.f, 1.5f);
            p.startNewSubPath (c.x - s * 0.5f, c.y - s);
            p.lineTo (c.x - s * 0.5f, c.y - s * 0.35f);
            p.lineTo (c.x + s * 0.5f, c.y - s * 0.35f);
            p.lineTo (c.x + s * 0.5f, c.y - s);
            break;
        case Icon::Menu:
            for (int i = -1; i <= 1; ++i)
            {
                p.startNewSubPath (c.x - s, c.y + (float) i * s * 0.65f);
                p.lineTo (c.x + s, c.y + (float) i * s * 0.65f);
            }
            break;
        case Icon::Dice:
        case Icon::Mutate:
        case Icon::Lock:
            p.addRoundedRectangle (c.x - s, c.y - s, s * 2.f, s * 2.f, 2.f);
            break;
    }
    g.setColour (! isEnabled() ? col::textFaint : (over ? col::text : col::textDim));
    g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
