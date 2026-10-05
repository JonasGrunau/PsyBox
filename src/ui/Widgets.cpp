#include "Widgets.h"

using namespace theme;
using psy::P;

// ---------------------------------------------------------------------------
// moddrag
// ---------------------------------------------------------------------------
namespace moddrag
{
    juce::var describe (psy::ModSrc s) { return "psymod:" + juce::String ((int) s); }

    psy::ModSrc sourceOf (const juce::var& d)
    {
        const auto t = d.toString();
        if (! t.startsWith ("psymod:")) return psy::ModSrc::None;
        const int i = t.fromFirstOccurrenceOf (":", false, false).getIntValue();
        return i > 0 && i < (int) psy::ModSrc::Count ? (psy::ModSrc) i : psy::ModSrc::None;
    }

    juce::String name (psy::ModSrc s)
    {
        return psy::choices::modSrc[(size_t) juce::jlimit (0, (int) psy::ModSrc::Count - 1, (int) s)];
    }

    bool bipolar (psy::ModSrc s)
    {
        using S = psy::ModSrc;
        return s == S::Lfo1 || s == S::Lfo2 || s == S::Lfo3 || s == S::Note || s == S::PitchBend || s == S::Random;
    }
} // namespace moddrag

// ---------------------------------------------------------------------------
// ParamKnob
// ---------------------------------------------------------------------------
ParamKnob::ParamKnob (PsyBoxProcessor& p, P prm, const juce::String& labelOverride)
    : proc (p), param (prm), target (psy::modTargetFor (prm)),
      labelText (labelOverride.isNotEmpty() ? labelOverride : juce::String (psy::def (prm).name))
{
    const auto& d = psy::def (prm);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (225.f), juce::degreesToRadians (495.f), true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityModeParameters (0.6, 1, 0.08, true, juce::ModifierKeys::shiftModifier);
    slider.getProperties().set ("bipolar", d.min < 0.f && d.max > 0.f);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.setTooltip (juce::String (d.group) + " " + d.name
                       + (target != psy::ModDst::None ? "  -  drop a mod source here, right-click to lock or reset"
                                                       : "  -  right-click to lock or reset"));
    slider.onDragStart = [this] { repaint(); };
    slider.onDragEnd = [this] { repaint(); };
    slider.onValueChange = [this] { repaint(); };
    slider.passThrough = [this] (juce::Point<int> pt) {
        return numRoutes > 0 && inRing (slider.getBounds().getPosition().toFloat() + pt.toFloat());
    };
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, d.id, slider);
    slider.setDoubleClickReturnValue (true, d.def);
    refreshRoutes();
    startTimerHz (target != psy::ModDst::None ? 15 : 10);
}

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    if (compact)
    {
        const int d = std::min (r.getWidth(), r.getHeight());
        slider.setBounds (r.withSizeKeepingCentre (d, d));
        return;
    }
    const int d = std::min (r.getWidth(), r.getHeight() - kLabelGap - kLabelH);
    slider.setBounds (r.getCentreX() - d / 2, r.getY(), d, d);
}

juce::Point<float> ParamKnob::knobCentre() const { return slider.getBounds().toFloat().getCentre(); }

// Same geometry as PsyLookAndFeel::drawRotarySlider.
float ParamKnob::trackRadius() const
{
    return (float) std::min (slider.getWidth(), slider.getHeight()) * 0.5f - 2.f;
}

bool ParamKnob::inRing (juce::Point<float> pt) const
{
    const float r = trackRadius();
    const float body = r - (r * 2.f + 4.f > 44.f ? 2.5f : 2.f) - 3.5f;
    const float dist = pt.getDistanceFrom (knobCentre());
    return dist >= body - 1.f && dist <= r + 4.f;
}

int ParamKnob::editableRoute() const
{
    for (int i = 0; i < numRoutes; ++i)
        if (routes[(size_t) i].src == proc.uiSelectedSource) return i;
    return numRoutes > 0 ? 0 : -1;
}

void ParamKnob::refreshRoutes()
{
    if (target == psy::ModDst::None) return;
    numRoutes = proc.modRoutesTo (target, routes);
    uint64_t h = 1469598103934665603ULL ^ (uint64_t) numRoutes;
    for (int i = 0; i < numRoutes; ++i)
    {
        const auto& r = routes[(size_t) i];
        h = (h ^ (uint64_t) (r.slot * 131 + (int) r.src)) * 1099511628211ULL;
        h = (h ^ (uint64_t) std::lround (r.amount * 10000.f)) * 1099511628211ULL;
    }
    h ^= (uint64_t) proc.uiSelectedSource << 40;
    if (h != routePrint)
    {
        routePrint = h;
        repaint();
    }
}

void ParamKnob::paint (juce::Graphics& g)
{
    if (compact) return;
    const auto area = getLocalBounds().removeFromBottom (kLabelH);
    const int ri = editableRoute();
    if ((ringHover || ringSlot >= 0) && ri >= 0)
    {
        const auto& r = routes[(size_t) ri];
        g.setColour (col::text);
        g.setFont (mono (10.f));
        g.drawText (moddrag::name (r.src) + " " + juce::String (juce::roundToInt (r.amount * 100.f)) + "%", area,
                    juce::Justification::centred, false);
        return;
    }
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
        g.drawText (labelText.toUpperCase(), area, juce::Justification::centred, true);
    }
}

void ParamKnob::paintOverChildren (juce::Graphics& g)
{
    if (target == psy::ModDst::None) return;
    const auto c = knobCentre();
    const float r = trackRadius();
    const float stroke = r * 2.f + 4.f > 44.f ? 2.5f : 2.f;
    const float body = r - stroke - 3.5f;

    // while a source is dragged, every knob that takes it shows a ring
    if (proc.uiDragSource != psy::ModSrc::None)
    {
        const float rr = r + 1.5f;
        g.setColour (dropHover ? col::text : col::text.withAlpha (0.55f));
        if (dropHover)
            g.drawEllipse (c.x - rr, c.y - rr, rr * 2.f, rr * 2.f, 2.f);
        else
        {
            juce::Path ring;
            ring.addEllipse (c.x - rr, c.y - rr, rr * 2.f, rr * 2.f);
            juce::Path dashed;
            const float dashes[] = { 2.f, 3.f };
            juce::PathStrokeType (1.f).createDashedStroke (dashed, ring, dashes, 2);
            g.fillPath (dashed);
        }
    }
    if (numRoutes == 0) return;

    // one arc per route between body and track: from the value as far as the
    // amount takes it (both ways for sources that swing both ways)
    const float start = juce::degreesToRadians (225.f), end = juce::degreesToRadians (495.f);
    const float pos = (float) slider.valueToProportionOfLength (slider.getValue());
    const float ringR = body + 2.25f;
    const int sel = editableRoute();
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < numRoutes; ++i)
        {
            const bool isSel = i == sel;
            if (isSel != (pass == 1)) continue;   // the editable ring on top
            const auto& rt = routes[(size_t) i];
            const float depth = rt.amount * psy::modDepthNorm (rt.dst);
            const float to = juce::jlimit (0.f, 1.f, pos + depth);
            const float from = moddrag::bipolar (rt.src) ? juce::jlimit (0.f, 1.f, pos - depth) : pos;
            const float a0 = start + std::min (from, to) * (end - start), a1 = start + std::max (from, to) * (end - start);
            const auto colour = isSel ? col::text : col::text.withAlpha (0.4f);
            g.setColour (colour);
            if (a1 - a0 > 0.01f)
            {
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, ringR, ringR, 0.f, a0, a1, true);
                g.strokePath (arc, juce::PathStrokeType (isSel ? 2.f : 1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            const auto tip = c.getPointOnCircumference (ringR, start + to * (end - start));
            g.fillEllipse (tip.x - 2.f, tip.y - 2.f, 4.f, 4.f);
        }
}

void ParamKnob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (onRename && e.eventComponent == this && e.y >= getHeight() - kLabelH - kLabelGap)
        startRename();
}

void ParamKnob::startRename()
{
    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (label (10.f));
    editor->setJustification (juce::Justification::centred);
    editor->setInputRestrictions (14);
    editor->setIndents (0, 0);
    editor->setBorder ({});
    editor->setColour (juce::TextEditor::backgroundColourId, col::well);
    editor->setText (labelText, false);
    editor->selectAll();
    editor->onReturnKey = [this] { finishRename (true); };
    editor->onEscapeKey = [this] { finishRename (false); };
    editor->onFocusLost = [this] { finishRename (true); };
    addAndMakeVisible (*editor);
    editor->setBounds (getLocalBounds().removeFromBottom (kLabelH + 2));
    editor->grabKeyboardFocus();
}

void ParamKnob::finishRename (bool commit)
{
    if (editor == nullptr) return;
    const auto text = editor->getText();
    // Defer the delete: this can run from inside the editor's own callback.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ParamKnob> (this)] {
        if (safe != nullptr) safe->editor.reset();
    });
    editor->setVisible (false);
    if (commit && onRename) onRename (text);
}

void ParamKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    if (e.eventComponent == this && numRoutes > 0 && inRing (e.position))
    {
        const int ri = editableRoute();
        ringSlot = routes[(size_t) ri].slot;
        proc.uiSelectedSource = routes[(size_t) ri].src;
        ringStartAmount = routes[(size_t) ri].amount;
        ringStartY = e.position.y;
        proc.beginModAmountChange (ringSlot);
        repaint();
        return;
    }
    if (e.mods.isAltDown())
        proc.setLocked (param, ! proc.isLocked (param));
}

void ParamKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (ringSlot < 0 || e.eventComponent != this) return;
    const float speed = e.mods.isShiftDown() ? 0.0015f : 0.008f;
    proc.setModAmount (ringSlot, ringStartAmount + (ringStartY - e.position.y) * speed);
    refreshRoutes();
}

void ParamKnob::mouseUp (const juce::MouseEvent&)
{
    if (ringSlot < 0) return;
    proc.endModAmountChange (ringSlot);
    ringSlot = -1;
    repaint();
}

void ParamKnob::mouseMove (const juce::MouseEvent& e)
{
    const bool h = e.eventComponent == this && numRoutes > 0 && inRing (e.position);
    if (h != ringHover)
    {
        ringHover = h;
        setMouseCursor (h ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

bool ParamKnob::isInterestedInDragSource (const SourceDetails& d)
{
    return target != psy::ModDst::None && moddrag::sourceOf (d.description) != psy::ModSrc::None;
}

void ParamKnob::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    const auto src = moddrag::sourceOf (d.description);
    if (proc.addModulation (src, target, 0.25f) >= 0)
        proc.uiSelectedSource = src;
    refreshRoutes();
    repaint();
}

void ParamKnob::showMenu()
{
    juce::PopupMenu m;
    const bool locked = proc.isLocked (param);
    m.addSectionHeader (juce::String (psy::def (param).group) + " / " + psy::def (param).name);
    m.addItem (1, locked ? "Unlock" : "Lock (keep on Generate)");
    m.addItem (2, "Reset to default");
    if (onRename) m.addItem (3, "Rename...");
    if (numRoutes > 0)
    {
        m.addSectionHeader ("Modulation");
        for (int i = 0; i < numRoutes; ++i)
        {
            const auto& r = routes[(size_t) i];
            juce::PopupMenu sub;
            sub.addItem (100 + i * 4, "Edit with the ring", true, editableRoute() == i);
            sub.addItem (101 + i * 4, "Invert");
            sub.addItem (102 + i * 4, "Remove");
            m.addSubMenu (moddrag::name (r.src) + "  " + juce::String (juce::roundToInt (r.amount * 100.f)) + "%", sub);
        }
    }
    else if (target != psy::ModDst::None)
    {
        m.addSeparator();
        m.addItem (4, "Drag an LFO, envelope or macro here to modulate", false);
    }
    const auto snapshot = routes;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider), [this, locked, snapshot] (int r) {
        if (r == 1) proc.setLocked (param, ! locked);
        if (r == 3) startRename();
        if (r == 2)
        {
            auto* p = proc.param (param);
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
        if (r >= 100)
        {
            const auto& rt = snapshot[(size_t) ((r - 100) / 4)];
            switch ((r - 100) % 4)
            {
                case 0: proc.uiSelectedSource = rt.src; break;
                case 1:
                    proc.beginModAmountChange (rt.slot);
                    proc.setModAmount (rt.slot, -rt.amount);
                    proc.endModAmountChange (rt.slot);
                    break;
                case 2: proc.removeModulation (rt.slot); break;
                default: break;
            }
            refreshRoutes();
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
    refreshRoutes();
    const bool dragging = proc.uiDragSource != psy::ModSrc::None && target != psy::ModDst::None;
    if (dragging != dragShown)
    {
        dragShown = dragging;
        repaint();
    }
}

// ---------------------------------------------------------------------------
// ModSourceHandle
// ---------------------------------------------------------------------------
ModSourceHandle::ModSourceHandle (PsyBoxProcessor& p, psy::ModSrc s, const juce::String& l)
    : proc (p), source (s), label (l)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Drag " + moddrag::name (s) + " onto a knob to modulate it");
    startTimerHz (5);
}

int ModSourceHandle::idealWidth() const
{
    return 12 + (label.isNotEmpty() ? sp::xs + juce::GlyphArrangement::getStringWidthInt (heading (9.5f), label.toUpperCase()) + 2 : 0);
}

void ModSourceHandle::drawIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    // four arrows from the centre, like Serum's drag handles
    const auto ctr = r.getCentre();
    const float s = std::min (r.getWidth(), r.getHeight()) * 0.5f;
    juce::Path p;
    for (int i = 0; i < 4; ++i)
    {
        const float a = (float) i * juce::MathConstants<float>::halfPi;
        const auto dir = juce::Point<float> (std::cos (a), std::sin (a));
        const auto nrm = juce::Point<float> (-dir.y, dir.x);
        const auto tip = ctr + dir * s;
        p.startNewSubPath (ctr + dir * (s * 0.2f));
        p.lineTo (tip);
        p.startNewSubPath (tip - dir * (s * 0.42f) + nrm * (s * 0.36f));
        p.lineTo (tip);
        p.lineTo (tip - dir * (s * 0.42f) - nrm * (s * 0.36f));
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
}

void ModSourceHandle::paint (juce::Graphics& g)
{
    const bool hot = isMouseOver() || dragging;
    auto r = getLocalBounds().toFloat();
    const auto iconColour = used ? accent() : (hot ? col::text : col::textDim);
    drawIcon (g, r.removeFromLeft (12.f).withSizeKeepingCentre (11.f, 11.f).translated (0.f, tab ? -1.f : 0.f), iconColour);
    if (label.isEmpty()) return;
    r.removeFromLeft ((float) sp::xs);
    g.setFont (heading (9.5f));
    g.setColour (selected || hot ? col::text : col::textDim);
    g.drawText (label.toUpperCase(), r.withTrimmedBottom (tab ? 2.f : 0.f), juce::Justification::centredLeft, false);
    if (tab && selected)
    {
        g.setColour (col::text);
        g.fillRect (0.f, (float) getHeight() - 2.f, (float) getWidth(), 2.f);
    }
}

void ModSourceHandle::mouseDown (const juce::MouseEvent&)
{
    dragging = false;
}

void ModSourceHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 4) return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        proc.uiSelectedSource = source;
        // a small pill with the source name follows the mouse
        const auto name = moddrag::name (source).toUpperCase();
        const int w = juce::GlyphArrangement::getStringWidthInt (heading (9.5f), name) + 30, h = 20;
        juce::Image img (juce::Image::ARGB, w * 2, h * 2, true);
        {
            juce::Graphics ig (img);
            ig.addTransform (juce::AffineTransform::scale (2.f));
            ig.setColour (col::panel.withAlpha (0.95f));
            ig.fillRoundedRectangle (0.5f, 0.5f, (float) w - 1.f, (float) h - 1.f, 10.f);
            ig.setColour (accent());
            ig.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.f, (float) h - 1.f, 10.f, 1.f);
            drawIcon (ig, { 7.f, 4.f, 12.f, 12.f }, accent());
            ig.setColour (col::text);
            ig.setFont (heading (9.5f));
            ig.drawText (name, juce::Rectangle<int> (23, 0, w - 26, h), juce::Justification::centredLeft, false);
        }
        juce::ScaledImage scaled (img, 2.0);
        container->startDragging (moddrag::describe (source), this, scaled, false, nullptr, &e.source);
        repaint();
    }
}

void ModSourceHandle::mouseUp (const juce::MouseEvent& e)
{
    const bool wasDrag = dragging;
    dragging = false;
    repaint();
    if (! wasDrag && e.mouseWasClicked())
    {
        proc.uiSelectedSource = source;
        if (onClick) onClick();
    }
}

void ModSourceHandle::timerCallback()
{
    bool u = false;
    for (int s = 0; s < psy::kNumModSlots && ! u; ++s)
        u = (int) std::lround (proc.plain (psy::modSrcParam (s))) == (int) source
         && (int) std::lround (proc.plain (psy::modDstParam (s))) > 0;
    if (u != used) { used = u; repaint(); }
}

// ---------------------------------------------------------------------------
// Segmented
// ---------------------------------------------------------------------------
Segmented::Segmented (PsyBoxProcessor& p, P prm, juce::StringArray l)
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

// Equal segments while every label keeps sp::sm of room on each side;
// otherwise each segment is its label plus an equal share of the spare
// width, so a long label ("Rectify", "Normal") doesn't touch the pill.
std::vector<float> Segmented::segmentWidths (float total) const
{
    const auto f = heading (9.5f);
    const float n = (float) labels.size();
    std::vector<float> text;
    float sum = 0.f, widest = 0.f;
    for (const auto& l : labels)
    {
        text.push_back (juce::GlyphArrangement::getStringWidth (f, l.toUpperCase()));
        sum += text.back();
        widest = juce::jmax (widest, text.back());
    }
    if (total / n - widest >= 2.f * (float) sp::sm)
        return std::vector<float> ((size_t) labels.size(), total / n);
    const float share = (total - sum) / n;
    for (auto& w : text) w += share;
    return text;
}

int Segmented::indexAt (juce::Point<int> pt) const
{
    float x = 0.f;
    const auto w = segmentWidths ((float) getWidth());
    for (int i = 0; i < labels.size() - 1; ++i)
    {
        x += w[(size_t) i];
        if ((float) pt.x < x) return i;
    }
    return labels.size() - 1;
}

void Segmented::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (col::lineStrong);
    g.drawRoundedRectangle (b, 3.f, 1.f);
    const auto w = segmentWidths (b.getWidth());
    g.setFont (heading (9.5f));
    float x = b.getX();
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto seg = juce::Rectangle<float> (x, b.getY(), w[(size_t) i], b.getHeight());
        x += w[(size_t) i];
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
    if (onPick) onPick (i);
    else attachment.setValueAsCompleteGesture ((float) i);
}

void Segmented::mouseMove (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    if (i != hover) { hover = i; repaint(); }
}

// ---------------------------------------------------------------------------
// ToggleDot
// ---------------------------------------------------------------------------
ToggleDot::ToggleDot (PsyBoxProcessor& p, P prm, const juce::String& t) : ToggleDot (t)
{
    attachment = std::make_unique<juce::ParameterAttachment> (*p.param (prm), [this] (float v) { on = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
}

ToggleDot::ToggleDot (const juce::String& t) : text (t)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int ToggleDot::idealWidth() const
{
    return 16 + juce::GlyphArrangement::getStringWidthInt (heading (9.5f), text.toUpperCase()) + 4;
}

void ToggleDot::paint (juce::Graphics& g)
{
    if (stacked)
    {
        // switch: 26 x 14 pill, knob slides right when on
        const auto top = getLocalBounds().removeFromTop (kKnobD).toFloat();
        const auto pill = top.withSizeKeepingCentre (26.f, 14.f);
        const bool hot = isMouseOver();
        if (on)
        {
            g.setColour (accent().withAlpha (0.22f));
            g.fillRoundedRectangle (pill, 7.f);
            g.setColour (accent());
            g.fillEllipse (pill.getRight() - 12.f, pill.getY() + 2.f, 10.f, 10.f);
        }
        else
        {
            g.setColour (hot ? col::textFaint : col::lineStrong);
            g.drawRoundedRectangle (pill.reduced (0.5f), 6.5f, 1.f);
            g.setColour (hot ? col::textDim : col::textFaint);
            g.fillEllipse (pill.getX() + 3.f, pill.getY() + 3.f, 8.f, 8.f);
        }
        g.setColour (on || hot ? col::text : col::textDim);
        g.setFont (label (10.f));
        g.drawText (text.toUpperCase(), getLocalBounds().removeFromBottom (kLabelH), juce::Justification::centred, false);
        return;
    }
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
    if (attachment != nullptr)
    {
        attachment->setValueAsCompleteGesture (on ? 0.f : 1.f);
        return;
    }
    setOn (! on);
    if (onToggle) onToggle (on);
}

// ---------------------------------------------------------------------------
// ParamCombo
// ---------------------------------------------------------------------------
ParamCombo::ParamCombo (PsyBoxProcessor& p, P prm, const juce::String& cap) : caption (cap)
{
    const auto& d = psy::def (prm);
    // The filter list is long; group it by family in the popup.
    const bool isFilter = d.choices == &psy::choices::filterType;
    const auto heading = [] (int index) -> const char* {
        using T = psy::FilterType;
        switch ((T) index)
        {
            case T::LP6: return "Low-pass";
            case T::HP6: return "High-pass";
            case T::BP12: return "Band-pass / Notch";
            case T::LadderLP12: return "Ladder";
            case T::Diode303: return "Character";
            default: return nullptr;
        }
    };
    int id = 1;
    for (const auto& c : *d.choices)
    {
        if (isFilter)
            if (const char* h = heading (id - 1)) box.addSectionHeading (h);
        box.addItem (c, id++);
    }
    addAndMakeVisible (box);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, d.id, box);
}

void ParamCombo::resized()
{
    auto r = getLocalBounds();
    if (caption.isNotEmpty())
        r = r.removeFromTop (kKnobD).withSizeKeepingCentre (r.getWidth(), kCtrlH);
    box.setBounds (r);
}

void ParamCombo::paint (juce::Graphics& g)
{
    if (caption.isEmpty())
        return;
    g.setColour (col::textDim);
    g.setFont (label (10.f));
    g.drawText (caption.toUpperCase(), getLocalBounds().removeFromBottom (kLabelH), juce::Justification::centred, false);
}

// ---------------------------------------------------------------------------
// AmountSlider
// ---------------------------------------------------------------------------
AmountSlider::AmountSlider (PsyBoxProcessor& p, P prm)
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
    // Text runs flush with the strip edges; tabs are sp::lg apart.
    std::vector<juce::Rectangle<int>> out;
    int x = 0;
    const auto f = heading (9.5f);
    for (int i = 0; i < names.size(); ++i)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (f, names[i].toUpperCase()) + (ledOn (i) ? 11 : 0);
        out.push_back ({ x, 0, w, getHeight() });
        x += w + sp::lg;
    }
    return out;
}

int TabStrip::idealWidth() const
{
    const auto t = tabBounds();
    return t.empty() ? 0 : t.back().getRight();
}

void TabStrip::setSelected (int i)
{
    selected = juce::jlimit (0, names.size() - 1, i);
    repaint();
}

// Horizontal ink extent of a line of text drawn at x = 0. The advance width
// also counts the tracking after the last glyph, which pushes an underline
// sized from it past the end of the word.
static juce::Range<float> inkRange (const juce::Font& f, const juce::String& text)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, text, 0.f, 0.f);
    juce::Path p;
    ga.createPath (p);
    const auto b = p.getBounds();
    return { b.getX(), b.getRight() };
}

void TabStrip::paint (juce::Graphics& g)
{
    const auto tabs = tabBounds();
    const auto f = heading (9.5f);
    g.setFont (f);
    for (int i = 0; i < names.size(); ++i)
    {
        auto r = tabs[(size_t) i];
        const bool sel = i == selected;
        if (sel)
        {
            // spans the ink: from the LED (or first glyph) to the last glyph
            const auto ink = inkRange (f, names[i].toUpperCase());
            const float x0 = ledOn (i) ? (float) r.getX() : (float) r.getX() + ink.getStart();
            const float x1 = (float) r.getX() + (ledOn (i) ? 11.f : 0.f) + ink.getEnd();
            g.setColour (col::text);   // neutral: the active tab doesn't change colour with the category
            g.fillRect (juce::Rectangle<float> (x0, (float) r.getBottom() - 2.f, x1 - x0, 2.f));
        }
        auto tr = r;
        if (ledOn (i))
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
        if (tabs[(size_t) i].expanded (sp::sm, 0).contains (e.getPosition()))
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
        if (tabs[(size_t) i].expanded (sp::sm, 0).contains (e.getPosition())) h = i;
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
            // U-turn arrow on a 24-unit grid (arrowhead left for undo), mirrored for redo
            const float u = s / 8.f, dir = icon == Icon::Undo ? 1.f : -1.f;
            const auto pt = [&] (float x, float y) { return juce::Point<float> (c.x + dir * (x - 12.f) * u, c.y + (y - 12.f) * u); };
            p.startNewSubPath (pt (9.f, 14.f));
            p.lineTo (pt (4.f, 9.f));
            p.lineTo (pt (9.f, 4.f));
            p.startNewSubPath (pt (4.f, 9.f));
            p.lineTo (pt (14.5f, 9.f));
            const auto ac = pt (14.5f, 14.5f);
            p.addCentredArc (ac.x, ac.y, 5.5f * u, 5.5f * u, 0.f, 0.f, dir * juce::MathConstants<float>::pi, false);
            p.lineTo (pt (11.f, 20.f));
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
        case Icon::Close:
            p.startNewSubPath (c.x - s * 0.7f, c.y - s * 0.7f);
            p.lineTo (c.x + s * 0.7f, c.y + s * 0.7f);
            p.startNewSubPath (c.x + s * 0.7f, c.y - s * 0.7f);
            p.lineTo (c.x - s * 0.7f, c.y + s * 0.7f);
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
