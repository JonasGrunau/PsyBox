#include "PsyLookAndFeel.h"

using namespace theme;

namespace
{
    // Looked up on every paint, so built once instead of from a literal each time.
    const juce::Identifier bipolarId { "bipolar" }, lockedId { "locked" }, primaryId { "primary" };
}

PsyLookAndFeel::PsyLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff111114));
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::lineStrong);
    setColour (juce::PopupMenu::highlightedTextColourId, col::text);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::highlightColourId, col::lineStrong);
    setColour (juce::CaretComponent::caretColourId, col::text);
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff111114));
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::AlertWindow::outlineColourId, col::lineStrong);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff141417));
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, col::text);
}

// ---------------------------------------------------------------------------
// Knob: thin 270-degree track, accent value arc (from centre when bipolar),
// flat body with a short pointer. Modulation-style minimalism, no gradients.
// ---------------------------------------------------------------------------
void PsyLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float d = std::min (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float r = d * 0.5f - 2.f;
    const bool hot = s.isMouseOverOrDragging();
    const bool bipolar = (bool) s.getProperties().getWithDefault (bipolarId, false);
    const bool locked = (bool) s.getProperties().getWithDefault (lockedId, false);
    const float angle = start + pos * (end - start);
    const float stroke = d > 44.f ? 2.5f : 2.f;
    const juce::Colour acc = accent (s);

    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.f, start, end, true);
    g.setColour (col::lineStrong);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, r, r, 0.f, std::min (from, angle), std::max (from, angle), true);
        g.setColour (s.isEnabled() ? acc : col::textFaint);
        g.strokePath (val, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // body
    const float br = r - stroke - 3.5f;
    g.setColour (col::knob);
    g.fillEllipse (c.x - br, c.y - br, br * 2.f, br * 2.f);
    g.setColour (hot ? col::textFaint : col::knobRim);
    g.drawEllipse (c.x - br, c.y - br, br * 2.f, br * 2.f, 1.f);

    // pointer
    const auto p0 = c.getPointOnCircumference (br * 0.28f, angle);
    const auto p1 = c.getPointOnCircumference (br * 0.86f, angle);
    g.setColour (s.isEnabled() ? col::text : col::textFaint);
    g.drawLine ({ p0, p1 }, 1.6f);

    if (locked)
    {
        // small padlock at the top right of the knob
        const float lx = c.x + r * 0.78f, ly = c.y - r * 0.98f;
        g.setColour (acc);
        g.fillRoundedRectangle (lx - 3.5f, ly + 1.5f, 7.f, 5.5f, 1.f);
        juce::Path shackle;
        shackle.addCentredArc (lx, ly + 1.5f, 2.2f, 2.4f, 0.f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.strokePath (shackle, juce::PathStrokeType (1.2f));
    }
}

// Horizontal slider used for mod-matrix amounts: hairline track, bipolar fill.
void PsyLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                                       juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float cy = b.getCentreY();
    const bool bipolar = (bool) s.getProperties().getWithDefault (bipolarId, false);
    g.setColour (col::lineStrong);
    g.fillRoundedRectangle (b.getX(), cy - 1.f, b.getWidth(), 2.f, 1.f);
    const float from = bipolar ? b.getCentreX() : b.getX();
    const float to = pos;
    g.setColour (accent (s));
    g.fillRoundedRectangle (std::min (from, to), cy - 1.f, std::abs (to - from), 2.f, 1.f);
    if (bipolar)
    {
        g.setColour (col::textFaint);
        g.fillRect (b.getCentreX() - 0.5f, cy - 3.f, 1.f, 6.f);
    }
    g.setColour (s.isMouseOverOrDragging() ? col::text : col::textDim);
    g.fillEllipse (to - 3.f, cy - 3.f, 6.f, 6.f);
}

// ---------------------------------------------------------------------------
// Combo box: text plus chevron on a hairline, no box.
// ---------------------------------------------------------------------------
void PsyLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const bool hot = box.isMouseOver (true) || box.isPopupActive();
    g.setColour (hot ? col::textFaint : col::line);
    g.fillRect (0.f, (float) h - 1.f, (float) w, 1.f);

    const float cx = (float) w - 7.f, cy = (float) h * 0.5f;
    juce::Path chev;
    chev.startNewSubPath (cx - 3.5f, cy - 1.5f);
    chev.lineTo (cx, cy + 2.f);
    chev.lineTo (cx + 3.5f, cy - 1.5f);
    g.setColour (hot ? col::text : col::textDim);
    g.strokePath (chev, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font PsyLookAndFeel::getComboBoxFont (juce::ComboBox&) { return mono (11.5f); }

void PsyLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (0, 0, box.getWidth() - 16, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
    label.setBorderSize ({ 0, 1, 0, 0 });
}

juce::PopupMenu::Options PsyLookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label)
{
    return LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label).withStandardItemHeight (24).withMinimumWidth (std::max (140, box.getWidth()));
}

// ---------------------------------------------------------------------------
// Popup menus
// ---------------------------------------------------------------------------
void PsyLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff111114));
    g.setColour (col::lineStrong);
    g.drawRect (0, 0, w, h, 1);
}

void PsyLookAndFeel::drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, bool isHighlighted,
                                                   const juce::PopupMenu::Item& item, const juce::PopupMenu::Options& o)
{
    // the accent of the window the menu opened from
    menuAccent = accent (o.getTargetComponent() != nullptr ? o.getTargetComponent() : o.getParentComponent());
    LookAndFeel_V4::drawPopupMenuItemWithOptions (g, area, isHighlighted, item, o);
}

void PsyLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                        bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                        const juce::String& shortcut, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (col::line);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    auto r = area.reduced (4, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (menuAccent.withAlpha (0.14f));
        g.fillRoundedRectangle (r.toFloat(), 3.f);
    }
    if (isTicked)
    {
        g.setColour (menuAccent);
        g.fillEllipse ((float) r.getX() + 7.f, (float) r.getCentreY() - 2.5f, 5.f, 5.f);
    }
    g.setColour (! isActive ? col::textFaint : (isHighlighted || isTicked ? col::text : col::textDim.brighter (0.3f)));
    g.setFont (getPopupMenuFont());
    g.drawText (text, r.withTrimmedLeft (20).withTrimmedRight (16), juce::Justification::centredLeft, true);
    if (shortcut.isNotEmpty())
    {
        g.setColour (col::textFaint);
        g.setFont (mono (10.5f));
        g.drawText (shortcut, r.withTrimmedRight (8), juce::Justification::centredRight, false);
    }
    if (hasSubMenu)
    {
        const float cx = (float) r.getRight() - 9.f, cy = (float) r.getCentreY();
        juce::Path p;
        p.startNewSubPath (cx - 1.5f, cy - 3.5f);
        p.lineTo (cx + 2.f, cy);
        p.lineTo (cx - 1.5f, cy + 3.5f);
        g.setColour (col::textDim);
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }
}

juce::Font PsyLookAndFeel::getPopupMenuFont() { return label (12.5f); }

void PsyLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight, int& w, int& h)
{
    if (isSeparator) { w = 50; h = 9; return; }
    h = standardHeight > 0 ? standardHeight : 24;
    w = juce::GlyphArrangement::getStringWidthInt (getPopupMenuFont(), text) + 48;
}

// ---------------------------------------------------------------------------
// Buttons. "primary" property = accent filled call-to-action.
// ---------------------------------------------------------------------------
void PsyLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool primary = (bool) b.getProperties().getWithDefault (primaryId, false);
    if (primary)
    {
        auto c = accent (b);
        if (down) c = c.darker (0.25f);
        else if (over) c = c.brighter (0.12f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 4.f);
        return;
    }
    if (b.getToggleState())
    {
        g.setColour (accent (b).withAlpha (0.14f));
        g.fillRoundedRectangle (r, 4.f);
    }
    else if (over || down)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.07f : 0.04f));
        g.fillRoundedRectangle (r, 4.f);
    }
    g.setColour (over ? col::textFaint : col::lineStrong);
    g.drawRoundedRectangle (r, 4.f, 1.f);
}

void PsyLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool)
{
    const bool primary = (bool) b.getProperties().getWithDefault (primaryId, false);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (primary ? juce::Colour (0xff0a0a0b) : (b.getToggleState() ? accent (b) : (over ? col::text : col::textDim.brighter (0.2f))));
    g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred, false);
}

juce::Font PsyLookAndFeel::getTextButtonFont (juce::TextButton& b, int)
{
    const bool primary = (bool) b.getProperties().getWithDefault (primaryId, false);
    return primary ? heading (12.5f) : heading (10.5f);
}

// ---------------------------------------------------------------------------
void PsyLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (juce::Colour (0xff141417));
    g.setColour (col::lineStrong);
    g.drawRect (0, 0, w, h, 1);
    g.setColour (col::text);
    g.setFont (label (11.5f));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (8, 4), juce::Justification::centredLeft, 3);
}

juce::Rectangle<int> PsyLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const int w = std::min (280, juce::GlyphArrangement::getStringWidthInt (label (11.5f), text) + 18);
    const int h = w >= 280 ? 44 : 24;
    return juce::Rectangle<int> (pos.x + 12, pos.y + 16, w, h).constrainedWithin (parent);
}

void PsyLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (col::well);
    g.fillRoundedRectangle (0.f, 0.f, (float) w, (float) h, 3.f);
}

void PsyLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? accent (e).withAlpha (0.7f) : col::lineStrong);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.f, (float) h - 1.f, 3.f, 1.f);
}

void PsyLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& a, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    g.fillAll (juce::Colour (0xff111114));
    g.setColour (col::lineStrong);
    g.drawRect (a.getLocalBounds(), 1);
    layout.draw (g, textArea.toFloat());
}

juce::Font PsyLookAndFeel::getAlertWindowTitleFont() { return title (15.f); }
juce::Font PsyLookAndFeel::getAlertWindowMessageFont() { return label (12.5f); }
juce::Font PsyLookAndFeel::getAlertWindowFont() { return label (12.f); }
