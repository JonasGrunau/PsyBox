#include "Pages.h"

using namespace theme;
using psy::P;

namespace
{
    juce::Colour stepColour (juce::Colour accentColour, const psy::ArpPattern& pt, int i, bool arpOn)
    {
        const bool acc = pt.has (i, psy::ArpPattern::Accent);
        return arpOn ? accentColour.withAlpha (acc ? 1.f : 0.55f) : col::textFaint.withAlpha (acc ? 0.7f : 0.4f);
    }
} // namespace

// ---------------------------------------------------------------------------
// Page
// ---------------------------------------------------------------------------
void Page::paintOverChildren (juce::Graphics& g)
{
    // a hairline under and right of every section that doesn't touch the edge
    g.setColour (col::line);
    for (auto* c : getChildren())
    {
        if (! c->isVisible() || dynamic_cast<Section*> (c) == nullptr) continue;
        const auto b = c->getBounds();
        if (b.getBottom() < getHeight()) g.fillRect (b.getX(), b.getBottom(), b.getWidth(), 1);
        if (b.getRight() < getWidth()) g.fillRect (b.getRight(), b.getY() + kPad, 1, b.getHeight() - 2 * kPad);
    }
}

// ---------------------------------------------------------------------------
// OscPage
// ---------------------------------------------------------------------------
OscPage::OscPage (PsyBoxProcessor& p)
    : sub (p), noise (p), noiseView (p), oscA (p, false), oscB (p, true), f1 (p, 0), f2 (p, 1), macros (p), mod (p), voicing (p), length (p), knock (p)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &sub, &noise, &oscA, &oscB, &f1, &f2, &macros, &mod, &voicing })
        addAndMakeVisible (c);
    addChildComponent (noiseView);
    addChildComponent (length);
    addChildComponent (knock);
    noise.onGear = [this] { showNoiseSample (true); };
    noiseView.onClose = [this] { showNoiseSample (false); };
    setExtra (Extra::None);
}

void OscPage::showNoiseSample (bool show)
{
    noiseView.setVisible (show);
    sub.setVisible (! show);
    noise.setVisible (! show);
}

void OscPage::setExtra (Extra e)
{
    extra = e;
    length.setVisible (e == Extra::Length);
    knock.setVisible (e == Extra::Knock);
    resized();
    repaint();
}

void OscPage::resized()
{
    // top: 384 = two section rows; bottom the same
    constexpr int kTop = 2 * kRowH, kSubW = 144, kOscW = 296, kMacroW = 208, kRightW = 256;
    const int w = getWidth(), h = getHeight();
    sub.setBounds (0, 0, kSubW, kRowH);
    noise.setBounds (0, kRowH, kSubW, kRowH);
    noiseView.setBounds (0, 0, kSubW, kTop);
    oscA.setBounds (kSubW, 0, kOscW, kTop);
    oscB.setBounds (kSubW + kOscW, 0, kOscW, kTop);
    const int fx = kSubW + 2 * kOscW;
    f1.setBounds (fx, 0, w - fx, kRowH);
    f2.setBounds (fx, kRowH, w - fx, kRowH);

    macros.setBounds (0, kTop, kMacroW, h - kTop);
    mod.setBounds (kMacroW, kTop, w - kMacroW - kRightW, h - kTop);
    const int rx = w - kRightW;
    voicing.setBounds (rx, kTop, kRightW, extra == Extra::None ? h - kTop : kRowH);
    length.setBounds (rx, kTop + kRowH, kRightW, h - kTop - kRowH);
    knock.setBounds (length.getBounds());
}

// ---------------------------------------------------------------------------
// KickPage
// ---------------------------------------------------------------------------
KickPage::KickPage (PsyBoxProcessor& p) : pitch (p), amp (p), click (p), drive (p), wave (p)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &pitch, &amp, &click, &drive, &wave })
        addAndMakeVisible (c);
}

void KickPage::resized()
{
    const int w = getWidth(), half = w / 2;
    pitch.setBounds (0, 0, half, kRowH);
    amp.setBounds (half, 0, w - half, kRowH);
    click.setBounds (0, kRowH, half, kRowH);
    drive.setBounds (half, kRowH, w - half, kRowH);
    wave.setBounds (0, 2 * kRowH, w, getHeight() - 2 * kRowH);
}

// ---------------------------------------------------------------------------
// PhosPage
// ---------------------------------------------------------------------------
PhosPage::PhosPage (PsyBoxProcessor& p)
    : synth (p), filter (p), distortion (p), calibration (p), macros (p), mod (p, true)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &synth, &filter, &distortion, &calibration, &macros, &mod })
        addAndMakeVisible (c);
}

void PhosPage::resized()
{
    // one row of cells for the 303 row and the calibration, two for the
    // filter and distortion; the modulation gets the rest
    constexpr int kOneRow = kPad + kHeaderH + kHeaderGap + kCellH + kPad;   // 122
    constexpr int kFilterW = 360, kMacroW = 208;
    const int w = getWidth(), h = getHeight();
    synth.setBounds (0, 0, w, kOneRow);
    filter.setBounds (0, kOneRow, kFilterW, kRowH);
    distortion.setBounds (kFilterW, kOneRow, w - kFilterW, kRowH);
    const int calY = kOneRow + kRowH;
    calibration.setBounds (0, calY, w, kOneRow);
    const int modY = calY + kOneRow;
    macros.setBounds (0, modY, kMacroW, h - modY);
    mod.setBounds (kMacroW, modY, w - kMacroW, h - modY);
}

// ---------------------------------------------------------------------------
// MatrixPage
// ---------------------------------------------------------------------------
MatrixPage::MatrixPage (PsyBoxProcessor& p) : panel (p)
{
    addAndMakeVisible (panel);
}

void MatrixPage::resized() { panel.setBounds (getLocalBounds()); }

MatrixPage::Panel::Panel (PsyBoxProcessor& p) : Section ("Mod Matrix"), proc (p)
{
    for (int i = 0; i < psy::kNumModSlots; ++i)
    {
        auto& r = rows[(size_t) i];
        r.src = std::make_unique<ParamCombo> (p, psy::modSrcParam (i));
        r.dst = std::make_unique<ParamCombo> (p, psy::modDstParam (i));
        r.amt = std::make_unique<AmountSlider> (p, psy::modAmtParam (i));
        r.clear = std::make_unique<IconButton> (IconButton::Icon::Close, "Remove this route");
        r.clear->onClick = [this, i] { proc.removeModulation (i); };
        for (juce::Component* c : { (juce::Component*) r.src.get(), (juce::Component*) r.dst.get(),
                                    (juce::Component*) r.amt.get(), (juce::Component*) r.clear.get() })
            addAndMakeVisible (c);
    }
    shown.fill (-9.f);
    refresh();
    startTimerHz (10);
}

void MatrixPage::Panel::timerCallback()
{
    if (isShowing()) refresh();
}

void MatrixPage::Panel::refresh()
{
    for (int i = 0; i < psy::kNumModSlots; ++i)
    {
        const bool hasSrc = std::lround (proc.plain (psy::modSrcParam (i))) > 0;
        const bool hasDst = std::lround (proc.plain (psy::modDstParam (i))) > 0;
        const bool used = hasSrc && hasDst;
        auto& r = rows[(size_t) i];
        // empty slots stay quiet so the routes in use stand out
        r.src->setAlpha (hasSrc ? 1.f : 0.45f);
        r.dst->setAlpha (hasDst ? 1.f : 0.45f);
        r.amt->setAlpha (used ? 1.f : 0.35f);
        r.clear->setVisible (used);
        const float v = used ? proc.plain (psy::modAmtParam (i)) : -2.f;
        if (std::abs (v - shown[(size_t) i]) > 1e-6f)
        {
            shown[(size_t) i] = v;
            repaint (valueArea[(size_t) i]);
            repaint (numberArea[(size_t) i]);
        }
    }
}

void MatrixPage::Panel::paint (juce::Graphics& g)
{
    Section::paint (g);
    auto h = header().withTrimmedLeft (titleWidth());
    g.setColour (col::textFaint);
    g.setFont (label (10.5f));
    const bool phos = juce::roundToInt (proc.plain (P::EngineMode)) == (int) psy::EngineMode::Phoscyon;
    g.drawText ("Drag a source's arrows onto any knob on the " + juce::String (phos ? "Phoscyon" : "OSC")
                    + " page to add a route, or set one up here.", h, juce::Justification::centredRight, false);

    g.setFont (label (10.f));
    g.setColour (col::textDim);
    g.drawText ("#", heads.withX (numberArea[0].getX()).withWidth (numberArea[0].getWidth()), juce::Justification::centredLeft, false);
    g.drawText ("SOURCE", heads.withX (srcX).withWidth (200), juce::Justification::centredLeft, false);
    g.drawText ("AMOUNT", heads.withX (amtX).withWidth (amtW), juce::Justification::centred, false);
    g.drawText ("TARGET", heads.withX (dstX).withWidth (200), juce::Justification::centredLeft, false);

    g.setFont (mono (10.5f));
    for (int i = 0; i < psy::kNumModSlots; ++i)
    {
        const bool used = shown[(size_t) i] > -1.5f;
        g.setColour (used ? col::textDim : col::textFaint);
        g.drawText (juce::String (i + 1), numberArea[(size_t) i], juce::Justification::centredLeft, false);
        if (used)
        {
            g.setColour (col::text);
            g.drawText ((shown[(size_t) i] > 0.f ? "+" : "") + juce::String (juce::roundToInt (shown[(size_t) i] * 100.f)) + "%",
                        valueArea[(size_t) i], juce::Justification::centredRight, false);
        }
        if (i > 0)
        {
            g.setColour (col::grid);
            g.fillRect (body().getX(), numberArea[(size_t) i].getY(), body().getWidth(), 1);
        }
    }
}

void MatrixPage::Panel::resized()
{
    auto b = body();
    heads = b.removeFromTop (kLabelH);
    b.removeFromTop (sp::sm);
    const int rh = b.getHeight() / psy::kNumModSlots;
    constexpr int kNumW = 32, kSrcW = 220, kDstW = 240, kValW = 56, kClearW = 24, kGap = sp::xl;
    amtW = b.getWidth() - kNumW - kSrcW - kDstW - kValW - kClearW - 3 * kGap;
    srcX = b.getX() + kNumW;
    amtX = srcX + kSrcW + kGap;
    dstX = amtX + amtW + kValW + kGap;
    for (int i = 0; i < psy::kNumModSlots; ++i)
    {
        auto r = b.removeFromTop (rh);
        auto& row = rows[(size_t) i];
        numberArea[(size_t) i] = r.removeFromLeft (kNumW);
        const auto ctl = [&] (int w) { return r.removeFromLeft (w).withSizeKeepingCentre (w, kCtrlH); };
        row.src->setBounds (ctl (kSrcW));
        r.removeFromLeft (kGap);
        row.amt->setBounds (ctl (amtW));
        valueArea[(size_t) i] = r.removeFromLeft (kValW);
        r.removeFromLeft (kGap);
        row.dst->setBounds (ctl (kDstW));
        r.removeFromLeft (kGap);
        row.clear->setBounds (r.removeFromLeft (kClearW).withSizeKeepingCentre (kClearW, kClearW));
    }
}

// ---------------------------------------------------------------------------
// GlobalPage
// ---------------------------------------------------------------------------
GlobalPage::GlobalPage (PsyBoxProcessor& p) : quality (p)
{
    addAndMakeVisible (quality);
}

void GlobalPage::resized()
{
    quality.setBounds (0, 0, getWidth(), kRowH);
}

GlobalPage::QualityPanel::QualityPanel (PsyBoxProcessor& p)
    : Section ("Quality"), live (p, P::Quality, { "Eco", "2x", "4x" }), render (p, P::RenderQuality, { "Eco", "2x", "4x" })
{
    live.setTooltip ("Oscillator oversampling while you play: Eco 1x, 2x (default), 4x");
    render.setTooltip ("Oscillator oversampling when the track is rendered: Eco 1x, 2x, 4x (default)");
    addAndMakeVisible (live);
    addAndMakeVisible (render);
}

void GlobalPage::QualityPanel::paint (juce::Graphics& g)
{
    Section::paint (g);
    static constexpr const char* text[] = {
        "While you play. Oscillators run oversampled: less aliasing on high notes and hard sync, more CPU. Eco plays "
        "one sample per sample.",
        "When the host renders the track (export, bounce, freeze) and when you drag a WAV out of the output window. A "
        "render has no deadline, so the higher setting only costs time.",
    };
    g.setColour (col::textFaint);
    g.setFont (label (10.5f));
    for (size_t i = 0; i < notes.size(); ++i)
        g.drawFittedText (text[i], notes[i], juce::Justification::centredLeft, 3, 1.f);
}

void GlobalPage::QualityPanel::resized()
{
    const auto gr = grid();
    captions.clear();
    for (int row = 0; row < 2; ++row)
    {
        (row == 0 ? live : render).setBounds (gr.control (0, row, 3));
        addCaption (row == 0 ? "Live" : "Render", gr.cell (0, row, 3));
        const auto ctl = gr.control (0, row, 3);
        notes[(size_t) row] = juce::Rectangle<int> (gr.cell (3, row).getX() + sp::lg, ctl.getCentreY() - 20, 440, 40);
    }
}

// ---------------------------------------------------------------------------
// ArpPage
// ---------------------------------------------------------------------------
ArpPage::ArpPage (PsyBoxProcessor& p) : panel (p)
{
    addAndMakeVisible (panel);
}

void ArpPage::resized() { panel.setBounds (getLocalBounds()); }

ArpPage::Panel::Panel (PsyBoxProcessor& p)
    : Section ("Arp"), proc (p), grid (p), templates (p), arpOn (p, P::ArpOn, "On"), hold (p, P::ArpHold, "Hold"),
      mode (p, P::ArpMode, "Mode")
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &grid, &templates, &clear, &dice, &shiftLeft, &shiftRight,
                                                                         &arpOn, &hold, &mode })
        addAndMakeVisible (c);
    hold.setTooltip ("Keep the arp running after you let go of the keys");
    dice.setTooltip ("New pattern in the style of the category");
    clear.setTooltip ("Rest on every step and put every note back on the key");
    clear.onClick = [this] {
        auto pt = proc.getArpPattern();
        pt.steps = {};
        pt.notes = {};
        proc.setArpPattern (pt);
    };
    dice.onClick = [this] { proc.generateArpPattern(); };
    shiftLeft.onClick = [this] { rotate (-1); };
    shiftRight.onClick = [this] { rotate (1); };
    knobs = { ks.add (*this, p, P::ArpDiv), ks.add (*this, p, P::ArpOctaves), ks.add (*this, p, P::ArpGate),
              ks.add (*this, p, P::ArpSwing), ks.add (*this, p, P::ArpLength) };
    startTimerHz (30);   // the playing step
}

void ArpPage::Panel::timerCallback()
{
    if (! grid.isShowing()) return;
    grid.refresh();
    templates.refresh();
}

void ArpPage::Panel::rotate (int by)
{
    const auto pt = proc.getArpPattern();
    auto out = pt;
    for (int i = 0; i < pt.length; ++i)
    {
        const auto to = (size_t) ((i + by + pt.length) % pt.length);
        out.steps[to] = pt.steps[(size_t) i];
        out.notes[to] = pt.notes[(size_t) i];
    }
    proc.setArpPattern (out);
}

void ArpPage::Panel::paint (juce::Graphics& g)
{
    Section::paint (g);
    const auto pair = shiftLeft.getBounds().getUnion (shiftRight.getBounds()).toFloat().reduced (0.5f);
    g.setColour (col::lineStrong);
    g.drawRoundedRectangle (pair, 4.f, 1.f);
    g.fillRect ((float) shiftRight.getX(), pair.getY() + 5.f, 1.f, pair.getHeight() - 10.f);

    g.setColour (col::textFaint);
    g.setFont (label (10.5f));
    g.drawFittedText ("Click or drag to paint steps. Drag a note up or down (or scroll) to move it off the key, double-click "
                      "to put it back. Accent hits full velocity, slide glides into the next step without a retrigger. "
                      "Click the numbers to set the loop length.",
                      hintArea, juce::Justification::centredRight, 3, 1.f);
}

void ArpPage::Panel::resized()
{
    auto h = header();
    h.removeFromLeft (titleWidth());
    arpOn.setBounds (h.removeFromLeft (arpOn.idealWidth()));
    h.removeFromLeft (sp::lg);
    hold.setBounds (h.removeFromLeft (hold.idealWidth()));
    const auto button = [&] (juce::Component& c, int w) { c.setBounds (h.removeFromRight (w).withSizeKeepingCentre (w, kCtrlH)); h.removeFromRight (sp::sm); };
    button (clear, 56);
    auto pair = h.removeFromRight (56).withSizeKeepingCentre (56, kCtrlH);
    shiftRight.setBounds (pair.removeFromRight (28));
    shiftLeft.setBounds (pair);
    h.removeFromRight (sp::sm);
    button (dice, 88);

    auto b = body();
    const auto gr = gridAt (b.getX(), b.getY());
    mode.setBounds (gr.captioned (0, 0, 2));
    for (int i = 0; i < (int) knobs.size(); ++i)
        knobs[(size_t) i]->setBounds (gr.cell (2 + i, 0));
    const int used = gr.cell (2 + (int) knobs.size(), 0).getX();
    hintArea = juce::Rectangle<int> (used + sp::xl, b.getY(), b.getRight() - used - sp::xl, kKnobD);
    b.removeFromTop (kCellH + sp::xl);
    // the templates along the bottom, the steps fill the space between
    const int templateRows = ((int) psy::arpTemplates().size() + Templates::kCols - 1) / Templates::kCols;
    templates.setBounds (b.removeFromBottom (Templates::heightFor (templateRows)));
    b.removeFromBottom (sp::xl);
    grid.setBounds (b);
}

// ---- templates --------------------------------------------------------------

juce::Rectangle<int> ArpPage::Panel::Templates::cardArea (int i) const
{
    const int w = (getWidth() - (kCols - 1) * sp::sm) / kCols;
    return { (i % kCols) * (w + sp::sm), kLabelH + sp::sm + (i / kCols) * (kCardH + sp::sm), w, kCardH };
}

int ArpPage::Panel::Templates::cardAt (juce::Point<int> pt) const
{
    for (int i = 0; i < (int) psy::arpTemplates().size(); ++i)
        if (cardArea (i).contains (pt)) return i;
    return -1;
}

void ArpPage::Panel::Templates::refresh()
{
    if (const auto pt = proc.getArpPattern(); ! (pt == shown))
    {
        shown = pt;
        repaint();
    }
}

void ArpPage::Panel::Templates::paint (juce::Graphics& g)
{
    using F = psy::ArpPattern;
    g.setColour (col::textDim);
    g.setFont (label (10.f));
    g.drawText ("TEMPLATES", getLocalBounds().removeFromTop (kLabelH), juce::Justification::centredLeft, false);

    const auto current = proc.getArpPattern();
    const auto& list = psy::arpTemplates();
    for (int i = 0; i < (int) list.size(); ++i)
    {
        const auto pt = F::parse (list[(size_t) i].steps);
        const bool sel = pt.sameRhythm (current);
        const auto r = cardArea (i).toFloat();
        g.setColour (i == hover ? col::panel.brighter (0.06f) : col::panel);
        g.fillRoundedRectangle (r, 6.f);
        g.setColour (sel ? accent (*this) : (i == hover ? col::lineStrong : col::line));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.f, 1.f);

        auto in = r.reduced ((float) sp::md, (float) sp::sm + 2.f);
        g.setColour (sel || i == hover ? col::text : col::textDim);
        g.setFont (label (11.5f));
        g.drawText (list[(size_t) i].name, in.removeFromTop (16.f), juce::Justification::centredLeft, false);

        // the sixteen steps; a slide bridges into the next one
        constexpr float kGap = 2.f, kStepH = 10.f;
        const float sw = (in.getWidth() - 15.f * kGap) / 16.f;
        const float y = in.getBottom() - kStepH;
        for (int s = 0; s < psy::kArpSteps; ++s)
        {
            const juce::Rectangle<float> c (in.getX() + (float) s * (sw + kGap), y, sw, kStepH);
            if (pt.has (s, F::On))
            {
                g.setColour (stepColour (accent (*this), pt, s, true));
                g.fillRoundedRectangle (c, 1.5f);
                if (pt.has (s, F::Slide) && s + 1 < psy::kArpSteps)
                    g.fillRect (c.getRight(), c.getCentreY() - 1.f, kGap, 2.f);
            }
            else
            {
                g.setColour (col::lineStrong);
                g.drawRoundedRectangle (c.reduced (0.5f), 1.5f, 1.f);
            }
        }
    }
}

void ArpPage::Panel::Templates::mouseDown (const juce::MouseEvent& e)
{
    // a template is a rhythm: the line on the note lane stays
    const auto& list = psy::arpTemplates();
    if (const int i = cardAt (e.getPosition()); i >= 0)
    {
        auto pt = psy::ArpPattern::parse (list[(size_t) i].steps);
        pt.notes = proc.getArpPattern().notes;
        proc.setArpPattern (pt);
    }
}

void ArpPage::Panel::Templates::mouseMove (const juce::MouseEvent& e)
{
    const int i = cardAt (e.getPosition());
    if (i != hover) { hover = i; repaint(); }
    setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

// ---- grid -------------------------------------------------------------------

namespace
{
    constexpr int kLabelCol = 72, kStepGap = 4, kBeatGap = 12, kRulerH = 18, kLaneGap = 6, kSmallLaneH = 24, kNoteLaneH = 84;
    constexpr float kPxPerSemitone = 6.f;   // vertical drag on the note lane

    juce::String noteText (int semitones) { return semitones > 0 ? "+" + juce::String (semitones) : juce::String (semitones); }
}

juce::Rectangle<int> ArpPage::Panel::Grid::rowArea (Row row) const
{
    auto a = getLocalBounds().withTrimmedLeft (kLabelCol);
    const auto ruler = a.removeFromTop (kRulerH);
    a.removeFromTop (kLaneGap);
    const auto slide = a.removeFromBottom (kSmallLaneH);
    a.removeFromBottom (kLaneGap);
    const auto acc = a.removeFromBottom (kSmallLaneH);
    a.removeFromBottom (kLaneGap);
    const auto note = a.removeFromBottom (kNoteLaneH);
    a.removeFromBottom (kLaneGap);
    switch (row)
    {
        case Row::Ruler:  return ruler;
        case Row::On:     return a;
        case Row::Note:   return note;
        case Row::Accent: return acc;
        case Row::Slide:  return slide;
        case Row::None:   break;
    }
    return {};
}

juce::Rectangle<int> ArpPage::Panel::Grid::cellArea (Row row, int step) const
{
    // fractional columns, rounded per edge, so the last one ends on the row's right edge
    const auto r = rowArea (row);
    const float colW = (float) (r.getWidth() - 12 * kStepGap - 3 * kBeatGap) / (float) psy::kArpSteps;
    const float x = (float) r.getX() + (float) step * (colW + (float) kStepGap) + (float) ((step / 4) * (kBeatGap - kStepGap));
    const int left = juce::roundToInt (x), right = juce::roundToInt (x + colW);
    return { left, r.getY(), right - left, r.getHeight() };
}

ArpPage::Panel::Grid::Hit ArpPage::Panel::Grid::locate (juce::Point<int> pt) const
{
    for (auto row : { Row::Ruler, Row::On, Row::Note, Row::Accent, Row::Slide })
    {
        if (! rowArea (row).expanded (0, kLaneGap / 2).contains (pt)) continue;
        // the nearest step: the gaps between cells (4 px inside a beat, 12 between beats) go to
        // whichever neighbour is closer, and the ends reach half a beat gap past the first and last
        int best = -1, bestDist = kBeatGap / 2 + 1;
        for (int i = 0; i < psy::kArpSteps; ++i)
        {
            const auto c = cellArea (row, i);
            const int dist = std::max ({ 0, c.getX() - pt.x, pt.x - (c.getRight() - 1) });
            if (dist < bestDist) { best = i; bestDist = dist; }
        }
        if (best >= 0) return { row, best };
    }
    return {};
}

// Repaints only when what the grid shows changed: the playing step, the pattern, the arp switch, the accent.
void ArpPage::Panel::Grid::refresh()
{
    const auto pt = proc.getArpPattern();
    const int step = proc.getArpStep();
    const bool on = proc.plain (P::ArpOn) > 0.5f;
    if (step == shownStep && on == shownOn && accent (*this) == shownAccent && pt == shown) return;
    shown = pt;
    shownStep = step;
    shownOn = on;
    shownAccent = accent (*this);
    repaint();
}

void ArpPage::Panel::Grid::apply (Hit h)
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
        case Row::Note:   // dragged, see mouseDrag
        case Row::None:
            return;
    }
    proc.setArpPattern (pt);
    repaint();
}

void ArpPage::Panel::Grid::setNote (int step, int semitones)
{
    auto pt = proc.getArpPattern();
    const auto v = (int8_t) juce::jlimit (-psy::kArpNoteRange, psy::kArpNoteRange, semitones);
    if (pt.notes[(size_t) step] == v) return;
    pt.notes[(size_t) step] = v;
    proc.setArpPattern (pt);
    repaint();
}

void ArpPage::Panel::Grid::mouseDown (const juce::MouseEvent& e)
{
    drag = locate (e.getPosition());
    if (drag.row == Row::None) return;
    const auto pt = proc.getArpPattern();
    if (drag.row == Row::Note)
    {
        dragNote = pt.notes[(size_t) drag.step];
        return;
    }
    using F = psy::ArpPattern;
    const F::Flag flag = drag.row == Row::Accent ? F::Accent : (drag.row == Row::Slide ? F::Slide : F::On);
    paintValue = ! (pt.has (drag.step, flag) && drag.step < pt.length);
    if (drag.row != Row::Ruler && drag.step >= pt.length)
        paintValue = true;
    apply (drag);
}

void ArpPage::Panel::Grid::mouseDrag (const juce::MouseEvent& e)
{
    if (drag.row == Row::None) return;
    if (drag.row == Row::Note)
    {
        // up raises the note, a semitone every few pixels
        setNote (drag.step, dragNote + juce::roundToInt ((float) -e.getDistanceFromDragStartY() / kPxPerSemitone));
        return;
    }
    // stay on the lane the drag started in; only the column follows the mouse
    const auto y = rowArea (drag.row).getCentreY();
    auto h = locate ({ e.getPosition().x, y });
    if (h.row == drag.row && h.step >= 0 && h.step != drag.step)
    {
        drag = h;
        apply (h);
    }
}

void ArpPage::Panel::Grid::mouseMove (const juce::MouseEvent& e)
{
    const auto h = locate (e.getPosition());
    if (h.row != hover.row || h.step != hover.step)
    {
        hover = h;
        setMouseCursor (h.row == Row::None ? juce::MouseCursor::NormalCursor
                        : h.row == Row::Note ? juce::MouseCursor::UpDownResizeCursor
                                             : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void ArpPage::Panel::Grid::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const auto h = locate (e.getPosition()); h.row == Row::Note)
        setNote (h.step, 0);
}

void ArpPage::Panel::Grid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto h = locate (e.getPosition());
    if (h.row != Row::Note)
    {
        Component::mouseWheelMove (e, w);
        return;
    }
    if (w.isInertial) return;
    // a wheel moves a semitone per notch, a trackpad one per stretch of travel
    int by = 0;
    if (! w.isSmooth) by = w.deltaY > 0.f ? 1 : (w.deltaY < 0.f ? -1 : 0);
    else
    {
        wheel += w.deltaY * 8.f;
        by = (int) wheel;
        wheel -= (float) by;
    }
    if (by != 0)
        setNote (h.step, proc.getArpPattern().notes[(size_t) h.step] + by);
}

void ArpPage::Panel::Grid::paint (juce::Graphics& g)
{
    using F = psy::ArpPattern;
    const auto pt = proc.getArpPattern();
    const bool on = proc.plain (P::ArpOn) > 0.5f;
    const int playing = proc.getArpStep();

    // lane names
    g.setFont (label (10.f));
    g.setColour (col::textDim);
    for (auto [row, name] : { std::pair { Row::Ruler, "STEP" }, std::pair { Row::On, "PLAY" }, std::pair { Row::Note, "NOTE" },
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
        g.setColour (isEnd ? accent (*this) : (i == playing ? col::text : (hover.row == Row::Ruler && hover.step == i ? col::text : (inLoop ? col::textDim : col::textFaint))));
        g.drawText (juce::String (i + 1), rr, juce::Justification::centred, false);
        if (isEnd)
            g.fillRect (rr.getX() + 4, rr.getBottom() - 1, rr.getWidth() - 8, 2);

        // play lane; a slide bridges the gap into the next step, like a tie
        const auto c = cellArea (Row::On, i).toFloat();
        if (pt.has (i, F::On))
        {
            g.setColour (stepColour (accent (*this), pt, i, on).withMultipliedAlpha (dim));
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

        // note lane: a marker at the step's pitch, the key on the middle line,
        // so the lane draws the line's contour; the number sits in the free
        // half. A rest's note shows faintly, it plays once the step does.
        const auto n = cellArea (Row::Note, i).toFloat();
        const int semis = pt.notes[(size_t) i];
        const float noteDim = dim * (pt.has (i, F::On) ? 1.f : 0.4f);
        g.setColour (col::well);
        g.fillRoundedRectangle (n, 3.f);
        g.setColour (col::lineStrong.withMultipliedAlpha (dim));
        g.drawRoundedRectangle (n.reduced (0.5f), 3.f, 1.f);
        g.setColour (col::line.withMultipliedAlpha (dim));
        g.fillRect (n.getX() + 3.f, n.getCentreY() - 0.5f, n.getWidth() - 6.f, 1.f);
        const float travel = n.getHeight() * 0.5f - 6.f;
        const float y = n.getCentreY() - travel * (float) semis / (float) psy::kArpNoteRange;
        g.setColour ((on ? accent (*this) : col::textDim).withMultipliedAlpha (noteDim));
        g.fillRoundedRectangle (n.getX() + 4.f, y - 2.f, n.getWidth() - 8.f, 4.f, 2.f);
        const auto half = semis > 0 ? n.withTrimmedTop (n.getHeight() * 0.5f) : n.withTrimmedBottom (n.getHeight() * 0.5f);
        g.setFont (mono (10.5f));
        g.setColour ((semis == 0 ? col::textFaint : col::text).withMultipliedAlpha (noteDim));
        g.drawText (noteText (semis), half.toNearestInt(), juce::Justification::centred, false);

        // accent lane
        const auto a = cellArea (Row::Accent, i).toFloat();
        const auto dot = a.withSizeKeepingCentre (12.f, 12.f);
        if (pt.has (i, F::Accent))
        {
            g.setColour ((on ? accent (*this) : col::textDim).withMultipliedAlpha (dim));
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
            g.setColour ((on ? accent (*this) : col::textDim).withMultipliedAlpha (dim));
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

