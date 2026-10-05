#include "Panels.h"

using namespace theme;
using psy::P;

namespace
{
    juce::String hexSeed (uint32_t s) { return juce::String::toHexString ((int) s).toUpperCase().paddedLeft ('0', 6); }
} // namespace

// ---------------------------------------------------------------------------
// Section
// ---------------------------------------------------------------------------
void Section::paint (juce::Graphics& g)
{
    drawHeading (g, title, header());
    g.setColour (col::textDim);
    g.setFont (label (10.f));
    for (const auto& c : captions)
        g.drawText (c.text.toUpperCase(), c.area, juce::Justification::centred, false);
}

int Section::titleWidth() const
{
    return juce::GlyphArrangement::getStringWidthInt (heading(), title.toUpperCase()) + sp::md;
}

// ---------------------------------------------------------------------------
// OscPanel: waveform on the left, a 4 x 2 grid on the right.
//   row 0: type-specific controls, retrig in the last cell
//   row 1: coarse, fine (, A>B FM, sync)
// Levels live in the mixer.
// ---------------------------------------------------------------------------
OscPanel::OscPanel (PsyBoxProcessor& p, bool b)
    : Section (b ? "Osc B" : "Osc A"), proc (p), isB (b),
      onToggle (p, b ? P::OscBOn : P::OscAOn, "On"),
      type (p, b ? P::OscBType : P::OscAType, { "Analog", "Super", "Wave", "FM" }),
      wave (p, b),
      table (p, b ? P::OscBWt : P::OscAWt, "Table"),
      retrig (p, b ? P::OscBRetrig : P::OscARetrig, "Retrig")
{
    addAndMakeVisible (onToggle);
    addAndMakeVisible (type);
    addAndMakeVisible (wave);
    addChildComponent (table);
    addAndMakeVisible (retrig);
    retrig.setStacked (true);
    shape = ks.add (*this, p, b ? P::OscBShape : P::OscAShape);
    pw = ks.add (*this, p, b ? P::OscBPw : P::OscAPw);
    detune = ks.add (*this, p, b ? P::OscBDetune : P::OscADetune);
    wtpos = ks.add (*this, p, b ? P::OscBWtPos : P::OscAWtPos, "Position");
    ratio = ks.add (*this, p, b ? P::OscBFmRatio : P::OscAFmRatio);
    fm = ks.add (*this, p, b ? P::OscBFm : P::OscAFm, "Index");
    fb = ks.add (*this, p, b ? P::OscBFmFb : P::OscAFmFb);
    coarse = ks.add (*this, p, b ? P::OscBCoarse : P::OscACoarse);
    fine = ks.add (*this, p, b ? P::OscBFine : P::OscAFine);
    if (b)
    {
        xmod = ks.add (*this, p, P::OscBXMod);
        sync = std::make_unique<ToggleDot> (p, P::OscBSync, "Sync");
        sync->setStacked (true);
        addAndMakeVisible (*sync);
    }
    startTimerHz (15);
}

void OscPanel::timerCallback()
{
    const bool on = proc.plain (isB ? P::OscBOn : P::OscAOn) > 0.5f;
    for (auto& k : ks.knobs)
        if (k->isEnabled() != on) k->setEnabled (on);
    table.setEnabled (on);
    const int t = (int) proc.plain (isB ? P::OscBType : P::OscAType);
    if (t != lastType)
    {
        lastType = t;
        resized();
    }
}

void OscPanel::resized()
{
    auto h = header();
    h.removeFromLeft (titleWidth());
    onToggle.setBounds (h.removeFromLeft (onToggle.idealWidth()));
    type.setBounds (h.removeFromRight (cellsW (4)).withSizeKeepingCentre (cellsW (4), kCtrlH));

    auto b = body();
    const int gridX = b.getRight() - cellsW (4);
    wave.setBounds (b.withRight (gridX - sp::lg));
    const auto gr = grid (gridX);

    for (auto& k : ks.knobs) k->setVisible (false);
    table.setVisible (false);

    const auto put = [&] (juce::Component* c, int col, int row) { c->setBounds (gr.cell (col, row)); c->setVisible (true); };
    switch (lastType < 0 ? 0 : lastType)
    {
        case 1: put (detune, 0, 0); break;
        case 2:
            table.setBounds (gr.captioned (0, 0, 2));
            table.setVisible (true);
            put (wtpos, 2, 0);
            break;
        case 3: put (ratio, 0, 0); put (fm, 1, 0); put (fb, 2, 0); break;
        default: put (shape, 0, 0); put (pw, 1, 0); break;
    }
    retrig.setBounds (gr.cell (3, 0));
    put (coarse, 0, 1);
    put (fine, 1, 1);
    if (xmod) put (xmod, 2, 1);
    if (sync) sync->setBounds (gr.cell (3, 1));
}

// ---------------------------------------------------------------------------
// MixerPanel: source levels on top, their options below.
// ---------------------------------------------------------------------------
MixerPanel::MixerPanel (PsyBoxProcessor& p)
    : Section ("Mixer"),
      subOct (p, P::SubOct, { "-1", "-2" }),
      subShape (p, P::SubShape, "Sub Wave"), noiseType (p, P::NoiseType, "Noise Type"),
      noiseDirect (p, P::NoiseDirect, "Noise direct")
{
    noiseDirect.setTooltip ("Noise skips the filters and is added after them: bright grit over a dark sound");
    addAndMakeVisible (noiseDirect);
    oscA = ks.add (*this, p, P::OscALevel, "Osc A");
    oscB = ks.add (*this, p, P::OscBLevel, "Osc B");
    sub = ks.add (*this, p, P::SubLevel);
    noise = ks.add (*this, p, P::NoiseLevel);
    ring = ks.add (*this, p, P::RingLevel);
    for (juce::Component* c : { (juce::Component*) &subOct, (juce::Component*) &subShape, (juce::Component*) &noiseType })
        addAndMakeVisible (c);
}

void MixerPanel::resized()
{
    auto h = header();
    noiseDirect.setBounds (h.removeFromRight (noiseDirect.idealWidth()));
    const auto gr = grid();
    oscA->setBounds (gr.cell (0, 0));
    oscB->setBounds (gr.cell (1, 0));
    sub->setBounds (gr.cell (2, 0));
    noise->setBounds (gr.cell (3, 0));
    ring->setBounds (gr.cell (4, 0));
    captions.clear();
    subOct.setBounds (gr.control (0, 1));
    addCaption ("Sub Oct", gr.cell (0, 1));
    subShape.setBounds (gr.captioned (1, 1, 2));
    noiseType.setBounds (gr.captioned (3, 1, 2));
}

// ---------------------------------------------------------------------------
// FilterPanel: response on the left, a 3 x 2 grid on the right.
// ---------------------------------------------------------------------------
FilterPanel::FilterPanel (PsyBoxProcessor& p, int s)
    : Section (s == 0 ? "Filter 1" : "Filter 2"), proc (p), slot (s),
      type (p, s == 0 ? P::F1Type : P::F2Type), display (p, s)
{
    addAndMakeVisible (type);
    addAndMakeVisible (display);
    cutoff = ks.add (*this, p, s == 0 ? P::F1Cutoff : P::F2Cutoff);
    reso = ks.add (*this, p, s == 0 ? P::F1Reso : P::F2Reso);
    drive = ks.add (*this, p, s == 0 ? P::F1Drive : P::F2Drive);
    env = ks.add (*this, p, s == 0 ? P::F1Env : P::F2Env);
    key = ks.add (*this, p, s == 0 ? P::F1Key : P::F2Key);
    if (s == 0)
        vowel = ks.add (*this, p, P::F1Vowel);
    else
    {
        routing = std::make_unique<Segmented> (p, P::FRouting, juce::StringArray { "Serial", "Para" });
        addAndMakeVisible (*routing);
    }
    startTimerHz (15);
}

void FilterPanel::timerCallback()
{
    const int t = (int) proc.plain (slot == 0 ? P::F1Type : P::F2Type);
    if (t != lastType)
    {
        lastType = t;
        const bool active = t != 0;
        for (auto& k : ks.knobs) k->setEnabled (active);
        using T = psy::FilterType;
        reso->setEnabled (active && t != (int) T::LP6 && t != (int) T::HP6);
        if (vowel) vowel->setEnabled (t == (int) T::Formant);
        resized();
    }
}

void FilterPanel::resized()
{
    auto h = header();
    h.removeFromLeft (titleWidth());
    type.setBounds (h.removeFromRight (cellsW (3)).withSizeKeepingCentre (cellsW (3), kCtrlH));
    if (routing)
    {
        h.removeFromRight (sp::lg);
        routing->setBounds (h.removeFromRight (cellsW (2)).withSizeKeepingCentre (cellsW (2), kCtrlH));
    }

    auto b = body();
    const int gridX = b.getRight() - cellsW (3);
    display.setBounds (b.withRight (gridX - sp::lg));
    const auto gr = grid (gridX);
    cutoff->setBounds (gr.cell (0, 0));
    reso->setBounds (gr.cell (1, 0));
    drive->setBounds (gr.cell (2, 0));
    env->setBounds (gr.cell (0, 1));
    key->setBounds (gr.cell (1, 1));
    if (vowel) vowel->setBounds (gr.cell (2, 1));
}

// ---------------------------------------------------------------------------
// VoicePanel: modes on top, six knobs below.
// ---------------------------------------------------------------------------
VoicePanel::VoicePanel (PsyBoxProcessor& p)
    : Section ("Voice"), proc (p),
      mode (p, P::VoiceMode), glideMode (p, P::GlideMode, { "Always", "Legato" }),
      arpOn (p, P::ArpOn, "On"), arpHold (p, P::ArpHold, "Hold"),
      arpMode (p, P::ArpMode, "Mode"), pattern (p)
{
    addAndMakeVisible (tabs);
    tabs.hasLed = [] (int i) { return i == 1; };
    tabs.isLit = [this] (int) { return proc.plain (P::ArpOn) > 0.5f; };
    tabs.onChange = [this] (int i) { show (i); };

    addAndMakeVisible (mode);
    addAndMakeVisible (glideMode);
    knobs = { ks.add (*this, p, P::PolyVoices), ks.add (*this, p, P::Glide), ks.add (*this, p, P::Accent),
              ks.add (*this, p, P::Octave), ks.add (*this, p, P::BendRange), ks.add (*this, p, P::VelSens) };

    arpOn.setStacked (true);
    arpHold.setStacked (true);
    arpHold.setTooltip ("Keep the arp running after you let go of the keys");
    pattern.onClick = [this] { if (onEditPattern) onEditPattern(); };
    for (juce::Component* c : { (juce::Component*) &arpOn, (juce::Component*) &arpHold, (juce::Component*) &arpMode,
                                (juce::Component*) &pattern })
        addChildComponent (c);
    arpKnobs = { ks.add (*this, p, P::ArpDiv), ks.add (*this, p, P::ArpOctaves), ks.add (*this, p, P::ArpGate),
                 ks.add (*this, p, P::ArpSwing) };

    show (0);
    startTimerHz (30);   // the pattern grid follows the playing step
}

void VoicePanel::setArpAllowed (bool allowed)
{
    if (allowed == arpAllowed) return;
    arpAllowed = allowed;
    tabs.setVisible (allowed);
    tabs.setSelected (0);
    show (0);
}

void VoicePanel::show (int page)
{
    current = page;
    const bool voice = page == 0;
    mode.setVisible (voice);
    glideMode.setVisible (voice);
    for (auto* k : knobs) k->setVisible (voice);
    for (juce::Component* c : { (juce::Component*) &arpOn, (juce::Component*) &arpHold, (juce::Component*) &arpMode,
                                (juce::Component*) &pattern })
        c->setVisible (! voice);
    for (auto* k : arpKnobs) k->setVisible (! voice);
    resized();
    repaint();
}

void VoicePanel::timerCallback()
{
    tabs.repaint();
    if (pattern.isVisible()) pattern.repaint();
}

void VoicePanel::resized()
{
    auto h = header();
    h.removeFromLeft (titleWidth());
    tabs.setBounds (h.removeFromRight (tabs.idealWidth()));

    const auto gr = grid();
    captions.clear();
    mode.setBounds (gr.control (0, 0, 3));
    glideMode.setBounds (gr.control (3, 0, 3));
    for (int i = 0; i < (int) knobs.size(); ++i)
        knobs[(size_t) i]->setBounds (gr.cell (i, 1));

    arpOn.setBounds (gr.cell (0, 0));
    arpHold.setBounds (gr.cell (1, 0));
    arpMode.setBounds (gr.captioned (2, 0, 2));
    for (int i = 0; i < (int) arpKnobs.size(); ++i)
        arpKnobs[(size_t) i]->setBounds (gr.cell (i, 1));
    // the pad grid fills the last two columns of both rows
    pattern.setBounds (gr.captioned (4, 0, 2).getUnion (gr.captioned (4, 1, 2)).withTrimmedBottom (kLabelH + kLabelGap));

    if (current == 0)
    {
        addCaption ("Play Mode", gr.cell (0, 0, 3));
        addCaption ("Glide Mode", gr.cell (3, 0, 3));
    }
    else
    {
        addCaption ("Pattern", gr.cell (4, 1, 2));
    }
}

// ---------------------------------------------------------------------------
// Arp pattern: pad grid and editor
// ---------------------------------------------------------------------------
namespace
{
    // Fill for one step, shared by the pad grid and the editor.
    juce::Colour stepColour (const psy::ArpPattern& pt, int i, bool arpOn)
    {
        const bool acc = pt.has (i, psy::ArpPattern::Accent);
        return arpOn ? accent().withAlpha (acc ? 1.f : 0.55f) : col::textFaint.withAlpha (acc ? 0.7f : 0.4f);
    }
} // namespace

ArpPatternView::ArpPatternView (PsyBoxProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("Click to edit the pattern");
}

void ArpPatternView::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && onClick) onClick();
}

void ArpPatternView::paint (juce::Graphics& g)
{
    const auto pt = proc.getArpPattern();
    const bool on = proc.plain (P::ArpOn) > 0.5f;
    const int playing = proc.getArpStep();
    const bool hot = isMouseOver();

    constexpr int kGap = 4;
    const float w = (float) (getWidth() - 3 * kGap) / 4.f, h = (float) (getHeight() - 3 * kGap) / 4.f;
    for (int i = 0; i < psy::kArpSteps; ++i)
    {
        const juce::Rectangle<float> r ((float) (i % 4) * (w + kGap), (float) (i / 4) * (h + kGap), w, h);
        const bool inLoop = i < pt.length;
        if (pt.has (i, psy::ArpPattern::On) && inLoop)
        {
            g.setColour (stepColour (pt, i, on));
            g.fillRoundedRectangle (r, 2.f);
            // a tie bridges the gap to the next pad in the row
            if (pt.has (i, psy::ArpPattern::Slide) && i % 4 != 3 && i + 1 < pt.length)
                g.fillRect (r.getRight(), r.getCentreY() - 1.5f, (float) kGap, 3.f);
        }
        else
        {
            g.setColour (inLoop ? (hot ? col::textFaint : col::lineStrong) : col::grid);
            g.drawRoundedRectangle (r.reduced (0.5f), 2.f, 1.f);
        }
        if (i == playing)
        {
            g.setColour (col::text);
            g.drawRoundedRectangle (r.reduced (0.5f), 2.f, 1.5f);
        }
    }
}

ArpPatternEditor::ArpPatternEditor (PsyBoxProcessor& p) : proc (p), grid (p), arpOn (p, P::ArpOn, "On")
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &grid, &templates, &clear, &done, &dice,
                                                                         &shiftLeft, &shiftRight, &arpOn })
        addAndMakeVisible (c);
    done.getProperties().set ("primary", true);
    dice.setTooltip ("New pattern in the style of the category");
    templates.setTooltip ("Start from a classic figure");
    clear.setTooltip ("Rest on every step");
    templates.onClick = [this] { showTemplates(); };
    clear.onClick = [this] {
        auto pt = proc.getArpPattern();
        pt.steps = {};
        proc.setArpPattern (pt);
    };
    done.onClick = [this] { close(); };
    dice.onClick = [this] { proc.generateArpPattern(); };
    shiftLeft.onClick = [this] { rotate (-1); };
    shiftRight.onClick = [this] { rotate (1); };
    knobs = { ks.add (*this, p, P::ArpLength), ks.add (*this, p, P::ArpDiv), ks.add (*this, p, P::ArpGate),
              ks.add (*this, p, P::ArpSwing), ks.add (*this, p, P::ArpOctaves) };
    setVisible (false);
}

void ArpPatternEditor::open()
{
    resized();   // the header follows the category name
    setVisible (true);
    toFront (false);
    startTimerHz (30);
}

void ArpPatternEditor::close()
{
    stopTimer();
    setVisible (false);
}

void ArpPatternEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! panel.contains (e.getPosition())) close();
}

void ArpPatternEditor::rotate (int by)
{
    const auto pt = proc.getArpPattern();
    auto out = pt;
    for (int i = 0; i < pt.length; ++i)
        out.steps[(size_t) ((i + by + pt.length) % pt.length)] = pt.steps[(size_t) i];
    proc.setArpPattern (out);
}

void ArpPatternEditor::showTemplates()
{
    juce::PopupMenu m;
    const auto& t = psy::arpTemplates();
    for (int i = 0; i < (int) t.size(); ++i)
        m.addItem (i + 1, t[(size_t) i].name);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&templates), [this] (int r) {
        const auto& list = psy::arpTemplates();
        if (r > 0 && r <= (int) list.size())
            proc.setArpPattern (psy::ArpPattern::parse (list[(size_t) r - 1].steps));
    });
}

void ArpPatternEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.6f));
    g.setColour (col::panel);
    g.fillRoundedRectangle (panel.toFloat(), 6.f);
    g.setColour (col::lineStrong);
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 6.f, 1.f);

    const auto pair = shiftLeft.getBounds().getUnion (shiftRight.getBounds()).toFloat().reduced (0.5f);
    g.setColour (col::lineStrong);
    g.drawRoundedRectangle (pair, 4.f, 1.f);
    g.fillRect ((float) shiftRight.getX(), pair.getY() + 5.f, 1.f, pair.getHeight() - 10.f);

    auto head = panel.reduced (sp::xl).removeFromTop (kHeaderH);
    drawHeading (g, "Arp Pattern", head);
    const int titleW = juce::GlyphArrangement::getStringWidthInt (heading(), "ARP PATTERN") + sp::md;
    g.setColour (accent());
    g.setFont (label (11.f));
    g.drawText (psy::categoryInfo()[(size_t) proc.getCategory()].name, head.withTrimmedLeft (titleW), juce::Justification::centredLeft, false);

    // beside the knobs, centred on their dials
    auto hint = panel.reduced (sp::xl).removeFromBottom (kCellH).removeFromTop (kKnobD).withTrimmedLeft (5 * kCellW + sp::lg);
    g.setColour (col::textFaint);
    g.setFont (label (10.5f));
    g.drawFittedText ("Click or drag to paint steps. Accent hits full velocity, slide glides into the next step "
                      "without a retrigger. Click the numbers to set the loop length.",
                      hint, juce::Justification::centredRight, 3, 1.f);
}

void ArpPatternEditor::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (820, 330);
    auto a = panel.reduced (sp::xl);

    auto head = a.removeFromTop (kHeaderH);
    // On switch after the title and category name (both painted)
    const int nameW = juce::GlyphArrangement::getStringWidthInt (heading(), "ARP PATTERN") + sp::md
                    + juce::GlyphArrangement::getStringWidthInt (label (11.f), psy::categoryInfo()[(size_t) proc.getCategory()].name) + sp::xl;
    arpOn.setBounds (head.withTrimmedLeft (nameW).withWidth (arpOn.idealWidth()));
    const auto button = [&] (juce::Component& c, int w) { c.setBounds (head.removeFromRight (w).withSizeKeepingCentre (w, kCtrlH)); head.removeFromRight (sp::sm); };
    button (done, 64);
    head.removeFromRight (sp::md);
    button (clear, 56);
    button (templates, 84);
    // the shift arrows share one outlined pair (drawn in paint)
    auto pair = head.removeFromRight (56).withSizeKeepingCentre (56, kCtrlH);
    shiftRight.setBounds (pair.removeFromRight (28));
    shiftLeft.setBounds (pair);
    head.removeFromRight (sp::sm);
    button (dice, 88);
    a.removeFromTop (sp::lg);

    // knob cells start one inset early so the first knob lines up with the lane names
    auto foot = a.removeFromBottom (kCellH).withTrimmedLeft (-kCellInset);
    for (auto* k : knobs)
        k->setBounds (foot.removeFromLeft (kCellW));
    a.removeFromBottom (sp::lg);
    grid.setBounds (a);
}

// ---- grid -------------------------------------------------------------------

namespace
{
    constexpr int kLabelCol = 72, kStepGap = 4, kBeatGap = 12, kRulerH = 18, kLaneGap = 6, kSmallLaneH = 24;
}

juce::Rectangle<int> ArpPatternEditor::Grid::rowArea (Row row) const
{
    auto a = getLocalBounds().withTrimmedLeft (kLabelCol);
    const auto ruler = a.removeFromTop (kRulerH);
    a.removeFromTop (kLaneGap);
    const auto slide = a.removeFromBottom (kSmallLaneH);
    a.removeFromBottom (kLaneGap);
    const auto acc = a.removeFromBottom (kSmallLaneH);
    a.removeFromBottom (kLaneGap);
    switch (row)
    {
        case Row::Ruler:  return ruler;
        case Row::On:     return a;
        case Row::Accent: return acc;
        case Row::Slide:  return slide;
        case Row::None:   break;
    }
    return {};
}

juce::Rectangle<int> ArpPatternEditor::Grid::cellArea (Row row, int step) const
{
    // fractional columns, rounded per edge, so the last one ends on the row's right edge
    const auto r = rowArea (row);
    const float colW = (float) (r.getWidth() - 12 * kStepGap - 3 * kBeatGap) / (float) psy::kArpSteps;
    const float x = (float) r.getX() + (float) step * (colW + (float) kStepGap) + (float) ((step / 4) * (kBeatGap - kStepGap));
    const int left = juce::roundToInt (x), right = juce::roundToInt (x + colW);
    return { left, r.getY(), right - left, r.getHeight() };
}

ArpPatternEditor::Grid::Hit ArpPatternEditor::Grid::locate (juce::Point<int> pt) const
{
    for (auto row : { Row::Ruler, Row::On, Row::Accent, Row::Slide })
    {
        if (! rowArea (row).expanded (0, kLaneGap / 2).contains (pt)) continue;
        for (int i = 0; i < psy::kArpSteps; ++i)
            if (cellArea (row, i).expanded (kBeatGap / 2, kLaneGap / 2).contains (pt))
                return { row, i };
    }
    return {};
}

void ArpPatternEditor::Grid::apply (Hit h)
{
    auto pt = proc.getArpPattern();
    using F = psy::ArpPattern;
    switch (h.row)
    {
        case Row::Ruler:
            pt.length = h.step + 1;
            break;
        case Row::On:
            pt.setFlag (h.step, F::On, paintValue);
            if (! paintValue) { pt.setFlag (h.step, F::Accent, false); pt.setFlag (h.step, F::Slide, false); }
            break;
        case Row::Accent:
        case Row::Slide:
            pt.setFlag (h.step, h.row == Row::Accent ? F::Accent : F::Slide, paintValue);
            if (paintValue) pt.setFlag (h.step, F::On, true);   // an accent or slide needs a note
            break;
        case Row::None:
            return;
    }
    proc.setArpPattern (pt);
    repaint();
}

void ArpPatternEditor::Grid::mouseDown (const juce::MouseEvent& e)
{
    drag = locate (e.getPosition());
    if (drag.row == Row::None) return;
    const auto pt = proc.getArpPattern();
    using F = psy::ArpPattern;
    const F::Flag flag = drag.row == Row::Accent ? F::Accent : (drag.row == Row::Slide ? F::Slide : F::On);
    paintValue = ! (pt.has (drag.step, flag) && drag.step < pt.length);
    if (drag.row != Row::Ruler && drag.step >= pt.length)
        paintValue = true;
    apply (drag);
}

void ArpPatternEditor::Grid::mouseDrag (const juce::MouseEvent& e)
{
    if (drag.row == Row::None) return;
    // stay on the lane the drag started in; only the column follows the mouse
    const auto y = rowArea (drag.row).getCentreY();
    auto h = locate ({ e.getPosition().x, y });
    if (h.row == drag.row && h.step >= 0 && h.step != drag.step)
    {
        drag = h;
        apply (h);
    }
}

void ArpPatternEditor::Grid::mouseMove (const juce::MouseEvent& e)
{
    const auto h = locate (e.getPosition());
    if (h.row != hover.row || h.step != hover.step)
    {
        hover = h;
        setMouseCursor (h.row == Row::None ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void ArpPatternEditor::Grid::paint (juce::Graphics& g)
{
    using F = psy::ArpPattern;
    const auto pt = proc.getArpPattern();
    const bool on = proc.plain (P::ArpOn) > 0.5f;
    const int playing = proc.getArpStep();

    // lane names
    g.setFont (label (10.f));
    g.setColour (col::textDim);
    for (auto [row, name] : { std::pair { Row::Ruler, "STEP" }, std::pair { Row::On, "PLAY" },
                              std::pair { Row::Accent, "ACCENT" }, std::pair { Row::Slide, "SLIDE" } })
        g.drawText (name, rowArea (row).withX (0).withWidth (kLabelCol - sp::md), juce::Justification::centredLeft, false);

    for (int i = 0; i < psy::kArpSteps; ++i)
    {
        const bool inLoop = i < pt.length;
        const float dim = inLoop ? 1.f : 0.3f;

        // playing column
        if (i == playing)
        {
            const auto col = cellArea (Row::On, i).getUnion (cellArea (Row::Slide, i)).expanded (2, 4).toFloat();
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (col, 3.f);
        }

        // ruler: step numbers, the last step of the loop marked
        const auto rr = cellArea (Row::Ruler, i);
        const bool isEnd = i == pt.length - 1;
        g.setFont (mono (10.f));
        g.setColour (isEnd ? accent() : (i == playing ? col::text : (hover.row == Row::Ruler && hover.step == i ? col::text : (inLoop ? col::textDim : col::textFaint))));
        g.drawText (juce::String (i + 1), rr, juce::Justification::centred, false);
        if (isEnd)
            g.fillRect (rr.getX() + 4, rr.getBottom() - 1, rr.getWidth() - 8, 2);

        // play lane; a slide bridges the gap into the next step, like a tie
        const auto c = cellArea (Row::On, i).toFloat();
        if (pt.has (i, F::On))
        {
            g.setColour (stepColour (pt, i, on).withMultipliedAlpha (dim));
            g.fillRoundedRectangle (c, 3.f);
            if (pt.has (i, F::Slide) && i + 1 < pt.length)
            {
                const float next = (float) cellArea (Row::On, i + 1).getX();
                g.fillRect (c.getRight(), c.getCentreY() - 2.f, next - c.getRight(), 4.f);
            }
        }
        else
        {
            g.setColour (col::well);
            g.fillRoundedRectangle (c, 3.f);
            g.setColour (col::lineStrong.withMultipliedAlpha (dim));
            g.drawRoundedRectangle (c.reduced (0.5f), 3.f, 1.f);
        }

        // accent lane
        const auto a = cellArea (Row::Accent, i).toFloat();
        const auto dot = a.withSizeKeepingCentre (12.f, 12.f);
        if (pt.has (i, F::Accent))
        {
            g.setColour ((on ? accent() : col::textDim).withMultipliedAlpha (dim));
            g.fillRoundedRectangle (dot, 2.f);
        }
        else
        {
            g.setColour (col::lineStrong.withMultipliedAlpha (dim));
            g.drawRoundedRectangle (dot.reduced (0.5f), 2.f, 1.f);
        }

        // slide lane: a pill in the step's own column, filled when on
        const auto pill = cellArea (Row::Slide, i).toFloat().withSizeKeepingCentre (22.f, 10.f);
        if (pt.has (i, F::Slide))
        {
            g.setColour ((on ? accent() : col::textDim).withMultipliedAlpha (dim));
            g.fillRoundedRectangle (pill, 5.f);
        }
        else
        {
            g.setColour (col::lineStrong.withMultipliedAlpha (dim));
            g.drawRoundedRectangle (pill.reduced (0.5f), 4.5f, 1.f);
        }

        if (hover.step == i && hover.row != Row::None && hover.row != Row::Ruler)
        {
            g.setColour (col::text.withAlpha (0.35f));
            g.drawRoundedRectangle (cellArea (hover.row, i).toFloat().reduced (0.5f), 3.f, 1.f);
        }
    }
}

// ---------------------------------------------------------------------------
// EnvPanel: curve on the left, 2 x 2 knobs on the right. The pitch envelope
// shares the panel as a fourth tab.
// ---------------------------------------------------------------------------
EnvPanel::EnvPanel (PsyBoxProcessor& p)
    : Section ("Envelopes"), display (p), pitchDisplay (p), pitchDest (p, P::PenvDest, "Target")
{
    addAndMakeVisible (tabs);
    addAndMakeVisible (display);
    addChildComponent (pitchDisplay);
    addChildComponent (pitchDest);
    static const P ids[4][4] = { { P::AenvA, P::AenvD, P::AenvS, P::AenvR },
                                 { P::FenvA, P::FenvD, P::FenvS, P::FenvR },
                                 { P::MenvA, P::MenvD, P::MenvS, P::MenvR },
                                 { P::PenvAmt, P::PenvAttack, P::PenvDecay, P::PenvCurve } };
    static const char* pitchLabels[4] = { "Amount", "Attack", "Decay", "Curve" };
    for (int e = 0; e < 4; ++e)
        for (int i = 0; i < 4; ++i)
            knobs[e][i] = ks.add (*this, p, ids[e][i], e == 3 ? juce::String (pitchLabels[i]) : juce::String());
    tabs.onChange = [this] (int i) { show (i); };
    show (0);
}

void EnvPanel::show (int e)
{
    current = e;
    const bool pitch = e == 3;
    if (! pitch) display.setEnvelope (e);
    display.setVisible (! pitch);
    pitchDisplay.setVisible (pitch);
    pitchDest.setVisible (pitch);
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < 4; ++i)
            knobs[k][i]->setVisible (k == e);
    resized();
}

void EnvPanel::resized()
{
    auto h = header();
    h.removeFromLeft (titleWidth());
    tabs.setBounds (h.removeFromRight (tabs.idealWidth()));

    // ADSR: 2 x 2 grid. Pitch: 3 x 2 with the target selector spanning two cells.
    const bool pitch = current == 3;
    auto b = body();
    const int gridX = b.getRight() - cellsW (pitch ? 3 : 2);
    const auto disp = b.withRight (gridX - sp::lg);
    display.setBounds (disp);
    pitchDisplay.setBounds (disp);
    const auto gr = grid (gridX);
    for (int e = 0; e < 3; ++e)
        for (int i = 0; i < 4; ++i)
            knobs[e][i]->setBounds (gr.cell (i % 2, i / 2));
    for (int i = 0; i < 4; ++i)
        knobs[3][i]->setBounds (gr.cell (i % 3, i / 3));
    pitchDest.setBounds (gr.captioned (1, 1, 2));
}

// ---------------------------------------------------------------------------
// LfoPanel: shape preview on the left, a 3 x 2 grid on the right.
//   row 0: shape (2 cells), phase
//   row 1: rate or division, sync, retrig
// ---------------------------------------------------------------------------
LfoPanel::LfoPanel (PsyBoxProcessor& p) : Section ("LFO"), proc (p), display (p)
{
    addAndMakeVisible (tabs);
    addAndMakeVisible (display);
    for (int i = 0; i < 3; ++i)
    {
        auto& pg = pages[i];
        pg.shape = std::make_unique<ParamCombo> (p, psy::lfoParam (i, 0), "Shape");
        pg.rate = ks.add (*this, p, psy::lfoParam (i, 1));
        pg.div = ks.add (*this, p, psy::lfoParam (i, 3), "Division");
        pg.phase = ks.add (*this, p, psy::lfoParam (i, 5));
        pg.sync = std::make_unique<ToggleDot> (p, psy::lfoParam (i, 2), "Sync");
        pg.retrig = std::make_unique<ToggleDot> (p, psy::lfoParam (i, 4), "Retrig");
        pg.sync->setStacked (true);
        pg.retrig->setStacked (true);
        addChildComponent (*pg.shape);
        addChildComponent (*pg.sync);
        addChildComponent (*pg.retrig);
    }
    tabs.onChange = [this] (int i) { show (i); };
    show (0);
    startTimerHz (15);
}

void LfoPanel::show (int lfo)
{
    current = lfo;
    display.setLfo (lfo);
    lastSync = proc.plain (psy::lfoParam (lfo, 2)) > 0.5f;
    for (int i = 0; i < 3; ++i)
    {
        auto& pg = pages[i];
        const bool vis = i == lfo;
        pg.shape->setVisible (vis);
        pg.sync->setVisible (vis);
        pg.retrig->setVisible (vis);
        pg.phase->setVisible (vis);
        pg.rate->setVisible (vis && ! lastSync);
        pg.div->setVisible (vis && lastSync);
    }
}

void LfoPanel::timerCallback()
{
    const bool s = proc.plain (psy::lfoParam (current, 2)) > 0.5f;
    if (s != lastSync) show (current);
}

void LfoPanel::resized()
{
    auto h = header();
    h.removeFromLeft (titleWidth());
    tabs.setBounds (h.removeFromRight (tabs.idealWidth()));

    auto b = body();
    const int gridX = b.getRight() - cellsW (3);
    display.setBounds (b.withRight (gridX - sp::lg));
    const auto gr = grid (gridX);
    for (auto& pg : pages)
    {
        pg.shape->setBounds (gr.captioned (0, 0, 2));
        pg.phase->setBounds (gr.cell (2, 0));
        pg.rate->setBounds (gr.cell (0, 1));
        pg.div->setBounds (gr.cell (0, 1));
        pg.sync->setBounds (gr.cell (1, 1));
        pg.retrig->setBounds (gr.cell (2, 1));
    }
}

// ---------------------------------------------------------------------------
// ModMatrixPanel: eight rows filling the body height.
// ---------------------------------------------------------------------------
ModMatrixPanel::ModMatrixPanel (PsyBoxProcessor& p) : Section ("Mod Matrix")
{
    for (int i = 0; i < psy::kNumModSlots; ++i)
    {
        rows[i].src = std::make_unique<ParamCombo> (p, psy::modSrcParam (i));
        rows[i].dst = std::make_unique<ParamCombo> (p, psy::modDstParam (i));
        rows[i].amt = std::make_unique<AmountSlider> (p, psy::modAmtParam (i));
        addAndMakeVisible (*rows[i].src);
        addAndMakeVisible (*rows[i].dst);
        addAndMakeVisible (*rows[i].amt);
    }
}

void ModMatrixPanel::resized()
{
    auto b = body();
    const int rh = b.getHeight() / psy::kNumModSlots;   // 16
    const int srcW = (b.getWidth() - 2 * sp::md) * 3 / 10;
    const int dstW = (b.getWidth() - 2 * sp::md) * 4 / 10;
    for (int i = 0; i < psy::kNumModSlots; ++i)
    {
        auto r = b.removeFromTop (rh);
        rows[i].src->setBounds (r.removeFromLeft (srcW));
        r.removeFromLeft (sp::md);
        rows[i].dst->setBounds (r.removeFromLeft (dstW));
        r.removeFromLeft (sp::md);
        rows[i].amt->setBounds (r);
    }
}

// ---------------------------------------------------------------------------
// FxPanel: one page per effect, in signal-chain order.
//   row 0: knobs
//   row 1: enabled, then the effect's selectors and switches
// The compressor page lays itself out (knobs in both rows, display right).
// ---------------------------------------------------------------------------
FxPanel::FxPanel (PsyBoxProcessor& p) : Section ("Effects"), proc (p)
{
    addAndMakeVisible (tabs);
    static const P onIds[kNumFx] = { P::DistOn, P::CrushOn, P::FshiftOn, P::PhaserOn, P::ChorusOn, P::CompOn, P::DelayOn, P::ReverbOn, P::WidthOn };
    tabs.isLit = [this] (int i) { return proc.plain (onIds[i]) > 0.5f; };
    tabs.onChange = [this] (int i) { show (i); };

    const auto knob = [&] (int page, P prm, const juce::String& l = {}) {
        auto* k = ks.add (*this, p, prm, l);
        pages[page].items.push_back (k);
        return k;
    };
    const auto own = [&] (int page, std::unique_ptr<juce::Component> c) {
        addAndMakeVisible (*c);
        auto* raw = c.get();
        pages[page].items.push_back (raw);
        pages[page].owned.push_back (std::move (c));
        return raw;
    };
    const auto toggle = [&] (int page, P prm, const juce::String& t) {
        auto tg = std::make_unique<ToggleDot> (p, prm, t);
        tg->setStacked (true);
        own (page, std::move (tg));
    };
    for (int i = 0; i < kNumFx; ++i)
    {
        pages[i].on = std::make_unique<ToggleDot> (p, onIds[i], "On");
        pages[i].on->setStacked (true);
        addAndMakeVisible (*pages[i].on);
    }

    own (Dist, std::make_unique<Segmented> (p, P::DistType));
    knob (Dist, P::DistDrive); knob (Dist, P::DistTone); knob (Dist, P::DistMix);

    knob (Crush, P::CrushBits); knob (Crush, P::CrushRate); knob (Crush, P::CrushMix);

    knob (Shift, P::FshiftHz); knob (Shift, P::FshiftFb); knob (Shift, P::FshiftSpread); knob (Shift, P::FshiftMix);

    knob (Phaser, P::PhaserRate); knob (Phaser, P::PhaserDepth); knob (Phaser, P::PhaserFb); knob (Phaser, P::PhaserCenter); knob (Phaser, P::PhaserMix);

    own (Chorus, std::make_unique<Segmented> (p, P::ChorusMode));
    knob (Chorus, P::ChorusRate); knob (Chorus, P::ChorusDepth); knob (Chorus, P::ChorusFb); knob (Chorus, P::ChorusMix);

    compMode = dynamic_cast<Segmented*> (own (Comp, std::make_unique<Segmented> (p, P::CompMode)));
    compDisplay = dynamic_cast<CompDisplay*> (own (Comp, std::make_unique<CompDisplay> (p)));
    compNormal = { knob (Comp, P::CompThresh), knob (Comp, P::CompRatio), knob (Comp, P::CompKnee), knob (Comp, P::CompMix) };
    compMulti = { knob (Comp, P::MbLowGain), knob (Comp, P::MbMidGain), knob (Comp, P::MbHighGain), knob (Comp, P::CompMix, "Depth"),
                  knob (Comp, P::CompDown), knob (Comp, P::CompUp) };
    knob (Comp, P::CompAttack); knob (Comp, P::CompRelease); knob (Comp, P::CompOut);

    toggle (Delay, P::DelaySync, "Sync");
    toggle (Delay, P::DelayPing, "Ping-Pong");
    delayTime = knob (Delay, P::DelayMs);
    delayDiv = ks.add (*this, p, P::DelayDiv, "Division");
    knob (Delay, P::DelayFb); knob (Delay, P::DelayLowCut); knob (Delay, P::DelayHighCut); knob (Delay, P::DelayMix);

    knob (Reverb, P::ReverbSize); knob (Reverb, P::ReverbDecay); knob (Reverb, P::ReverbDamp); knob (Reverb, P::ReverbPre); knob (Reverb, P::ReverbMix);

    knob (Width, P::WidthAmount); knob (Width, P::WidthStereo); knob (Width, P::WidthTime); knob (Width, P::WidthMonoHz);

    show (Dist);
    startTimerHz (10);
}

void FxPanel::show (int page)
{
    current = page;
    for (int i = 0; i < kNumFx; ++i)
    {
        const bool vis = i == page;
        pages[i].on->setVisible (vis);
        for (auto* c : pages[i].items) c->setVisible (vis);
    }
    lastDelaySync = proc.plain (P::DelaySync) > 0.5f;
    delayDiv->setVisible (page == Delay && lastDelaySync);
    delayTime->setVisible (page == Delay && ! lastDelaySync);

    lastCompMulti = proc.plain (P::CompMode) > 0.5f;
    for (auto* k : compNormal) k->setVisible (page == Comp && ! lastCompMulti);
    for (auto* k : compMulti) k->setVisible (page == Comp && lastCompMulti);
    resized();
}

void FxPanel::timerCallback()
{
    tabs.repaint();
    const bool s = proc.plain (P::DelaySync) > 0.5f;
    const bool m = proc.plain (P::CompMode) > 0.5f;
    if (s != lastDelaySync || m != lastCompMulti) show (current);
}

void FxPanel::layoutComp (const CellGrid& gr)
{
    pages[Comp].on->setBounds (gr.cell (0, 1));
    compMode->setBounds (gr.control (1, 1, 3));   // three cells, like the other pages' mode selectors

    // Normal: Thresh Ratio Knee | Attack Release Mix Output.
    // Multi:  Low Mid High (band gains) | Attack Release Depth Output, Down Up under Release Depth.
    const auto at = [&] (juce::Component* c, int col, int row) { c->setBounds (gr.cell (col, row)); };
    for (int i = 0; i < 3; ++i) at (compNormal[(size_t) i], i, 0);
    at (compNormal[3], 5, 0);
    for (int i = 0; i < 4; ++i) at (compMulti[(size_t) i], i < 3 ? i : 5, 0);
    at (compMulti[4], 4, 1);
    at (compMulti[5], 5, 1);
    for (auto* c : pages[Comp].items)
        if (auto* k = dynamic_cast<ParamKnob*> (c))
        {
            if (k->getParam() == P::CompAttack) at (k, 3, 0);
            else if (k->getParam() == P::CompRelease) at (k, 4, 0);
            else if (k->getParam() == P::CompOut) at (k, 6, 0);
        }

    // The display takes the rest of the body, both rows.
    const auto top = gr.cell (7, 0);
    compDisplay->setBounds (juce::Rectangle<int>::leftTopRightBottom (top.getX() + kCellInset, top.getY(), body().getRight(),
                                                                       gr.cell (7, 1).getBottom()));
}

void FxPanel::resized()
{
    auto h = header();
    h.removeFromLeft (titleWidth());
    tabs.setBounds (h.removeFromRight (tabs.idealWidth()));

    const auto gr = grid();
    for (int i = 0; i < kNumFx; ++i)
    {
        if (i == Comp) continue;
        pages[i].on->setBounds (gr.cell (0, 1));
        int knobCol = 0, lowCol = 1;
        for (auto* c : pages[i].items)
        {
            if (auto* seg = dynamic_cast<Segmented*> (c))
            {
                const int span = i == Dist ? 5 : 3;
                seg->setBounds (gr.control (lowCol, 1, span));
                lowCol += span;
            }
            else if (dynamic_cast<ToggleDot*> (c) != nullptr)
            {
                c->setBounds (gr.cell (lowCol++, 1));
            }
            else
            {
                if (c == delayTime) delayDiv->setBounds (gr.cell (knobCol, 0));
                c->setBounds (gr.cell (knobCol++, 0));
            }
        }
    }
    layoutComp (gr);

    captions.clear();
    if (current == Dist) addCaption ("Type", gr.cell (1, 1, 5));
    if (current == Chorus || current == Comp) addCaption ("Mode", gr.cell (1, 1, current == Comp ? 2 : 3));
    repaint();
}

// ---------------------------------------------------------------------------
MacroPanel::MacroPanel (PsyBoxProcessor& p) : Section ("Macros"), proc (p)
{
    const P ids[4] = { P::Macro1, P::Macro2, P::Macro3, P::Macro4 };
    for (int i = 0; i < 4; ++i)
    {
        auto* k = ks.add (*this, p, ids[i], p.getMacroName (i));
        k->onRename = [this, i] (const juce::String& n) { proc.setMacroName (i, n); };
    }
    proc.addChangeListener (this);
}

MacroPanel::~MacroPanel() { proc.removeChangeListener (this); }

void MacroPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (int i = 0; i < 4; ++i)
        ks.knobs[(size_t) i]->setLabel (proc.getMacroName (i));
}

void MacroPanel::resized()
{
    const auto gr = grid();
    for (int i = 0; i < 4; ++i)
        ks.knobs[(size_t) i]->setBounds (gr.cell (i % 2, i / 2));
}

// ---------------------------------------------------------------------------
// Sidebar
// ---------------------------------------------------------------------------
Sidebar::Sidebar (PsyBoxProcessor& p) : proc (p)
{
    generateButton.getProperties().set ("primary", true);
    generateButton.setTooltip ("New sound in this category (locked knobs are kept)");
    generateButton.onClick = [this] { proc.generate(); };
    mutateButton.setTooltip ("Vary the current sound within the category");
    mutateButton.onClick = [this] { proc.mutate(); };
    clearLocks.onClick = [this] { proc.clearLocks(); };
    arpToggle.setTooltip ("Generate adds an arp in the style of the category");
    arpToggle.setStacked (true);
    addChildComponent (arpToggle);
    arpToggle.onToggle = [this] (bool on) { proc.setArpGeneration (on); };
    keyToggle.setTooltip ("Generate tunes the kick's body to the key you play (key tracking)");
    keyToggle.setStacked (true);
    keyToggle.onToggle = [this] (bool on) { proc.setKickKeyGeneration (on); };
    addChildComponent (keyToggle);
    familyTabs.onChange = [this] (int i)
    {
        // switching family selects its first category
        const auto f = (psy::Family) i;
        if (psy::familyOf (proc.getCategory()) != f)
            proc.setCategory (psy::categoriesIn (f).front());
        showFamily (f);
    };
    addAndMakeVisible (familyTabs);

    mutateAmount.setSliderStyle (juce::Slider::LinearHorizontal);
    mutateAmount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    mutateAmount.setRange (0.02, 1.0);
    mutateAmount.setValue (proc.getMutateAmount(), juce::dontSendNotification);
    mutateAmount.setTooltip ("Mutation amount");
    mutateAmount.onValueChange = [this] { proc.setMutateAmount ((float) mutateAmount.getValue()); repaint (infoArea()); };

    seedLabel.setFont (mono (10.f));
    seedLabel.setBorderSize ({ 0, 0, 0, 0 });
    seedLabel.setColour (juce::Label::textColourId, col::textFaint);
    seedLabel.setJustificationType (juce::Justification::centredRight);
    seedLabel.setEditable (false, true);
    seedLabel.setTooltip ("The seed of the current sound. Same category + same seed = same sound. Double-click to type one.");
    seedLabel.onTextChange = [this] {
        const auto t = seedLabel.getText().trim().removeCharacters ("#");
        if (t.containsOnly ("0123456789abcdefABCDEF") && t.isNotEmpty())
            proc.generateWithSeed ((uint32_t) t.getHexValue32() & 0xFFFFFFu);
        refresh();
    };

    for (juce::Component* c : { (juce::Component*) &generateButton, (juce::Component*) &mutateButton, (juce::Component*) &clearLocks,
                                (juce::Component*) &mutateAmount, (juce::Component*) &seedLabel })
        addAndMakeVisible (c);
    proc.addChangeListener (this);
    refresh();
}

Sidebar::~Sidebar() { proc.removeChangeListener (this); }

void Sidebar::changeListenerCallback (juce::ChangeBroadcaster*) { refresh(); }

void Sidebar::refresh()
{
    seedLabel.setText (hexSeed (proc.getSeed()), juce::dontSendNotification);
    arpToggle.setOn (proc.getArpGeneration());
    keyToggle.setOn (proc.getKickKeyGeneration());
    // only the generator options that apply to the current category's family
    const bool arp = psy::hasArp (psy::familyOf (proc.getCategory()));
    if (keyToggle.isVisible() != proc.isKickCategory() || arpToggle.isVisible() != arp)
    {
        keyToggle.setVisible (proc.isKickCategory());
        arpToggle.setVisible (arp);
        resized();
    }
    if (proc.getCategory() != lastCategory)
    {
        lastCategory = proc.getCategory();
        showFamily (psy::familyOf (lastCategory));
    }
    clearLocks.setVisible (proc.numLocks() > 0);
    clearLocks.setTooltip ("Unlock all " + juce::String (proc.numLocks()) + " locked parameters");
    if (proc.getCategory() != shownCategory && listH > 0)
        scrollToShow (proc.getCategory());   // e.g. a session that loads with a hidden category
    repaint();
}

juce::Rectangle<int> Sidebar::rowBounds (int i) const
{
    return { 0, kListTop + i * kItemH - scrollY, getWidth() - 1, kItemH };
}

// The visible window of the category list; rows scroll inside it.
juce::Rectangle<int> Sidebar::listArea() const
{
    return { 0, kListTop, getWidth() - 1, listH };
}

// Everything below the category list, starting at the divider.
juce::Rectangle<int> Sidebar::infoArea() const
{
    return getLocalBounds().withTrimmedTop (kListTop + listH);
}

int Sidebar::maxScroll() const
{
    return juce::jmax (0, (int) rows().size() * kItemH - listH);
}

void Sidebar::showFamily (psy::Family f)
{
    familyTabs.setSelected ((int) f);
    if (f == viewFamily && shownCategory != psy::Category::Count) return;
    viewFamily = f;
    scrollY = 0;
    hover = -1;
    shownCategory = psy::Category::Count;
    if (listH > 0) scrollToShow (proc.getCategory());
    repaint();
}

void Sidebar::setScroll (int y)
{
    y = juce::jlimit (0, maxScroll(), y);
    if (y != scrollY) { scrollY = y; repaint(); }
}

void Sidebar::scrollToShow (psy::Category c)
{
    shownCategory = c;
    const auto& order = rows();
    const auto it = std::find (order.begin(), order.end(), c);
    if (it == order.end()) return;   // the other family is on screen
    const int row = (int) (it - order.begin());
    const int top = row * kItemH;
    if (top < scrollY) setScroll (top);
    else if (top + kItemH > scrollY + listH) setScroll (top + kItemH - listH);
}

// Returns the category (enum value) under the point; rows are in display order.
int Sidebar::categoryAt (juce::Point<int> pt) const
{
    if (! listArea().contains (pt)) return -1;
    const auto& order = rows();
    for (int i = 0; i < (int) order.size(); ++i)
        if (rowBounds (i).contains (pt)) return (int) order[(size_t) i];
    return -1;
}

void Sidebar::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::line);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());

    const juce::Rectangle<int> head (kPad, kPad, getWidth() - 2 * kPad, kHeaderH);
    g.setColour (col::textFaint);
    g.setFont (mono (10.f));
    g.drawText (juce::String ((int) rows().size()), head, juce::Justification::centredRight);

    const int sel = (int) proc.getCategory();
    const auto& info = psy::categoryInfo();
    const auto list = listArea();
    g.saveState();   // clip the rows (and their scroll hints) to the list window
    g.reduceClipRegion (list);
    const auto& order = rows();
    for (int row = 0; row < (int) order.size(); ++row)
    {
        const int i = (int) order[(size_t) row];
        const auto r = rowBounds (row);
        if (! r.intersects (list)) continue;
        const auto c = juce::Colour ((juce::uint32) (0xff000000 | info[(size_t) i].colour));
        const bool isSel = i == sel;
        if (isSel)
        {
            g.setColour (c.withAlpha (0.10f));
            g.fillRect (r);
            g.setColour (c);
            g.fillRect (r.getX(), r.getY(), 2, r.getHeight());
        }
        else if (i == hover)
        {
            g.setColour (juce::Colours::white.withAlpha (0.03f));
            g.fillRect (r);
        }
        const float cy = (float) r.getCentreY();
        g.setColour (isSel || i == hover ? c : c.withAlpha (0.85f));
        g.fillEllipse ((float) kPad, cy - 3.5f, 7.f, 7.f);
        g.setColour (isSel ? col::text : (i == hover ? col::text.withAlpha (0.85f) : col::textDim));
        g.setFont (isSel ? labelStrong (12.5f) : label (12.5f));
        g.drawText (info[(size_t) i].name, r.withTrimmedLeft (kPad + 7 + sp::md), juce::Justification::centredLeft, false);
    }

    // scroll hints: rows fade out at an edge with more beyond it, and a thin thumb
    if (maxScroll() > 0)
    {
        constexpr float kFade = 18.f;
        const auto top = (float) list.getY(), bottom = (float) list.getBottom();
        if (scrollY > 0)
        {
            g.setGradientFill ({ col::panel, 0.f, top, col::panel.withAlpha (0.f), 0.f, top + kFade, false });
            g.fillRect (list.withHeight ((int) kFade));
        }
        if (scrollY < maxScroll())
        {
            g.setGradientFill ({ col::panel, 0.f, bottom, col::panel.withAlpha (0.f), 0.f, bottom - kFade, false });
            g.fillRect (list.withTrimmedTop (list.getHeight() - (int) kFade));
        }
        const int contentH = (int) order.size() * kItemH;
        const int thumbH = juce::jmax (24, listH * listH / contentH);
        const int thumbY = list.getY() + (listH - thumbH) * scrollY / maxScroll();
        g.setColour (col::textFaint.withAlpha (0.5f));
        g.fillRoundedRectangle (juce::Rectangle<int> (getWidth() - 5, thumbY, 2, thumbH).toFloat(), 1.f);
    }
    g.restoreState();

    // info block
    auto a = infoArea();
    g.setColour (col::line);
    g.fillRect (0, a.getY(), getWidth() - 1, 1);
    a = a.reduced (kPad, 0).withTrimmedTop (kPad);
    g.setColour (accent());
    g.setFont (title (17.f));
    g.drawText (info[(size_t) sel].name, a.removeFromTop (24), juce::Justification::centredLeft, false);
    a.removeFromTop (sp::xs);
    g.setColour (col::textDim);
    g.setFont (label (11.f));
    g.drawFittedText (info[(size_t) sel].blurb, a.removeFromTop (30), juce::Justification::topLeft, 2, 1.f);

    // mutate amount caption and readout, in the label row above the slider
    const auto cap = mutateAmount.getBounds().withY (mutateButton.getY()).withHeight (kLabelH);
    g.setColour (col::textFaint);
    g.setFont (mono (10.f));
    g.drawText (juce::String (juce::roundToInt (proc.getMutateAmount() * 100.f)) + "%", cap, juce::Justification::centredRight, false);
    g.setFont (label (10.f));
    g.drawText ("AMOUNT", cap, juce::Justification::centredLeft, false);
    g.drawText ("SEED", seedCaption, juce::Justification::centredLeft, false);
    if (arpToggle.isVisible())
        g.drawText ("ARP", arpCaption, juce::Justification::centredLeft, false);
    if (keyToggle.isVisible())
        g.drawText ("TUNE TO KEY", keyCaption, juce::Justification::centredLeft, false);

}

void Sidebar::resized()
{
    familyTabs.setBounds (kPad, kPad, familyTabs.idealWidth(), kHeaderH);

    // Built from the bottom edge up so the buttons sit on the window's padding.
    auto a = getLocalBounds().reduced (kPad);
    auto r = a.removeFromBottom (32);   // mutate + amount
    mutateButton.setBounds (r.removeFromLeft ((r.getWidth() - sp::md) / 2));
    r.removeFromLeft (sp::md);
    mutateAmount.setBounds (r.withTrimmedTop (kLabelH));
    a.removeFromBottom (sp::sm);

    generateButton.setBounds (a.removeFromBottom (44));
    a.removeFromBottom (sp::md);

    // The options that shape the next Generate sit right above it.
    // "SEED" caption left, value right: same pattern as AMOUNT / 25% below
    auto seedRow = a.removeFromBottom (kLabelH);
    seedCaption = seedRow;
    seedLabel.setBounds (seedRow.removeFromRight (100));

    // "ARP" (synth categories): whether Generate adds an arp
    arpCaption = {};
    if (arpToggle.isVisible())
    {
        a.removeFromBottom (sp::sm);
        auto arpRow = a.removeFromBottom (kLabelH);
        arpCaption = arpRow;
        arpToggle.setBounds (arpRow.removeFromRight (26));
    }

    // "TUNE TO KEY" (kick categories): whether Generate key-tracks the kick
    keyCaption = {};
    if (keyToggle.isVisible())
    {
        a.removeFromBottom (sp::sm);
        auto keyRow = a.removeFromBottom (kLabelH);
        keyCaption = keyRow;
        keyToggle.setBounds (keyRow.removeFromRight (26));
    }

    // The list runs down to the divider above the info block; the rest scroll.
    listH = juce::jmax (kItemH, a.getBottom() - sp::sm - kInfoH - kListTop);
    setScroll (scrollY);
    scrollToShow (proc.getCategory());

    // Unlock sits on the category name line, right-aligned.
    auto nameLine = infoArea().reduced (kPad, 0).withTrimmedTop (kPad).removeFromTop (24);
    clearLocks.setBounds (nameLine.removeFromRight (64).withSizeKeepingCentre (64, kCtrlH));
}

void Sidebar::mouseDown (const juce::MouseEvent& e)
{
    const int c = categoryAt (e.getPosition());
    if (c >= 0) proc.setCategory ((psy::Category) c);
}

void Sidebar::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (categoryAt (e.getPosition()) >= 0) proc.generate();
}

void Sidebar::mouseMove (const juce::MouseEvent& e)
{
    const int c = categoryAt (e.getPosition());
    if (c != hover) { hover = c; repaint(); }
    setMouseCursor (c >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void Sidebar::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // deltaY already follows the OS scroll direction; isReversed only reports it (Viewport ignores it too)
    const float dy = w.deltaY;
    if (maxScroll() == 0 || dy == 0.f || ! listArea().contains (e.getPosition()))
    {
        Component::mouseWheelMove (e, w);
        return;
    }
    // same distance per notch as juce::Viewport with a 16 px step
    const float px = dy * 14.f * 16.f;
    setScroll (scrollY - juce::roundToInt (px < 0.f ? juce::jmin (px, -1.f) : juce::jmax (px, 1.f)));
    mouseMove (e);   // a different row is under the pointer now
}

// ---------------------------------------------------------------------------
// TopBar
// ---------------------------------------------------------------------------
TopBar::TopBar (PsyBoxProcessor& p)
    : proc (p), quality (p, P::Quality, { "Eco", "2x", "4x" }), volume (p, P::MasterVol, "Vol"), output (p)
{
    for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton,
                                (juce::Component*) &prev, (juce::Component*) &next, (juce::Component*) &menu,
                                (juce::Component*) &save, (juce::Component*) &quality, (juce::Component*) &volume,
                                (juce::Component*) &output })
        addAndMakeVisible (c);
    prev.onClick = [this] { proc.stepPreset (-1); };
    next.onClick = [this] { proc.stepPreset (1); };
    menu.onClick = [this] { showPresetMenu(); };
    save.onClick = [this] { savePresetDialog(); };
    undoButton.onClick = [this] { proc.undo(); };
    redoButton.onClick = [this] { proc.redo(); };
    quality.setTooltip ("Oscillator oversampling: Eco 1x, 2x (default), 4x");
    proc.addChangeListener (this);
    changeListenerCallback (nullptr);
}

TopBar::~TopBar() { proc.removeChangeListener (this); }

void TopBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    undoButton.setEnabled (proc.canUndo());
    redoButton.setEnabled (proc.canRedo());
    repaint();
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::line);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    // wordmark
    g.setColour (col::text);
    g.setFont (wordmarkFont());
    g.drawText ("PSYBOX", juce::Rectangle<int> (kPad, 0, 160, getHeight()), juce::Justification::centredLeft, false);

    // preset display
    auto r = presetArea;
    g.setColour (col::well);
    g.fillRoundedRectangle (r.toFloat(), 4.f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.f, 1.f);
    auto text = r.withTrimmedLeft (prev.getRight() - r.getX() + sp::sm).withTrimmedRight (r.getRight() - next.getX() + sp::sm);
    const auto& info = psy::categoryInfo()[(size_t) proc.getCategory()];
    g.setColour (accent());
    g.setFont (heading (9.5f));
    const auto catName = juce::String (info.shortName).toUpperCase();
    const int cw = juce::GlyphArrangement::getStringWidthInt (heading (9.5f), catName) + sp::md;
    g.drawText (catName, text.removeFromLeft (cw), juce::Justification::centredLeft, false);
    g.setColour (col::text);
    g.setFont (label (12.5f));
    g.drawText (proc.getPresetName(), text, juce::Justification::centredLeft, true);

    g.setColour (col::textFaint);
    g.setFont (label (10.f));
    g.drawText ("QUALITY", qualityCaption, juce::Justification::centredLeft, false);
    g.drawText ("VOL", volumeCaption, juce::Justification::centredRight, false);
}

void TopBar::resized()
{
    auto r = getLocalBounds();
    // preset box centred in the window, its icons to the right
    presetArea = r.withSizeKeepingCentre (352, 32);
    const int iconY = presetArea.getY() + sp::xs;
    prev.setBounds (presetArea.getX() + sp::xs, iconY, 24, 24);
    next.setBounds (presetArea.getRight() - sp::xs - 24, iconY, 24, 24);
    menu.setBounds (presetArea.getRight() + sp::sm, iconY, 24, 24);
    save.setBounds (menu.getRight() + sp::xs, iconY, 24, 24);
    // history mirrors the preset icons on the left of the box
    redoButton.setBounds (presetArea.getX() - sp::sm - 24, iconY, 24, 24);
    undoButton.setBounds (redoButton.getX() - sp::xs - 24, iconY, 24, 24);

    // left: the wordmark, then oscillator quality
    auto left = r.withTrimmedLeft (kPad + juce::GlyphArrangement::getStringWidthInt (wordmarkFont(), "PSYBOX") + sp::xl);
    qualityCaption = left.removeFromLeft (juce::GlyphArrangement::getStringWidthInt (label (10.f), "QUALITY") + sp::sm);
    quality.setBounds (left.removeFromLeft (cellsW (3)).withSizeKeepingCentre (cellsW (3), kCtrlH));

    // right: the output window (as tall as the preset box), then volume
    auto right = r.reduced (kPad, 0);
    volume.setCompact (true);
    volume.setBounds (right.removeFromRight (28).withSizeKeepingCentre (28, 28));
    right.removeFromRight (sp::sm);
    volumeCaption = right.removeFromRight (24);
    right.removeFromRight (sp::lg);
    output.setBounds (right.removeFromRight (256).withSizeKeepingCentre (256, presetArea.getHeight()));
}

juce::Font TopBar::wordmarkFont()
{
    return juce::Font (juce::FontOptions (heading (15.f).getTypefacePtr()).withHeight (15.f).withKerningFactor (0.32f));
}

void TopBar::showPresetMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Init patch");
    m.addSeparator();
    const auto files = proc.listPresets();
    std::map<juce::String, juce::PopupMenu> byFolder;
    for (int i = 0; i < files.size(); ++i)
        byFolder[files[i].getParentDirectory().getFileName()].addItem (100 + i, files[i].getFileNameWithoutExtension(),
                                                                       true, files[i].getFileNameWithoutExtension() == proc.getPresetName());
    for (auto& [folder, sub] : byFolder)
        m.addSubMenu (folder, sub);
    if (files.isEmpty())
        m.addItem (2, "No saved presets yet", false);
    m.addSeparator();
    m.addItem (3, "Reveal preset folder");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menu), [this, files] (int res) {
        if (res == 1) proc.initPatch();
        else if (res == 3) PsyBoxProcessor::presetFolder().revealToUser();
        else if (res >= 100 && res - 100 < files.size()) proc.loadPreset (files[res - 100]);
    });
}

void TopBar::savePresetDialog()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset", "Name", juce::MessageBoxIconType::NoIcon, this);
    saveWindow->addTextEditor ("name", proc.getPresetName().trimCharactersAtEnd ("*"));
    saveWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result) {
        if (result == 1 && saveWindow != nullptr)
        {
            const auto name = saveWindow->getTextEditorContents ("name").trim();
            if (name.isNotEmpty()) proc.savePreset (name);
        }
        saveWindow.reset();
    }), false);
}
