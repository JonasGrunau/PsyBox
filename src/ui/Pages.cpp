#include "Pages.h"

using namespace theme;
using psy::P;

namespace
{
    juce::Colour stepColour (const psy::ArpPattern& pt, int i, bool arpOn)
    {
        const bool acc = pt.has (i, psy::ArpPattern::Accent);
        return arpOn ? accent().withAlpha (acc ? 1.f : 0.55f) : col::textFaint.withAlpha (acc ? 0.7f : 0.4f);
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
    : sub (p), noise (p), oscA (p, false), oscB (p, true), f1 (p, 0), f2 (p, 1), macros (p), mod (p), voicing (p), length (p), knock (p)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &sub, &noise, &oscA, &oscB, &f1, &f2, &macros, &mod, &voicing })
        addAndMakeVisible (c);
    addChildComponent (length);
    addChildComponent (knock);
    setExtra (Extra::None);
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
    constexpr int kTop = 2 * kRowH, kSubW = 144, kOscW = 288, kMacroW = 208, kRightW = 256;
    const int w = getWidth(), h = getHeight();
    sub.setBounds (0, 0, kSubW, kRowH);
    noise.setBounds (0, kRowH, kSubW, kRowH);
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
// FxModule
// ---------------------------------------------------------------------------
FxModule::FxModule (PsyBoxProcessor& p, int chainIndex, const juce::String& t, P onParam)
    : Section (t), proc (p), index (chainIndex), onParam (onParam), on (p, onParam, "On")
{
    addAndMakeVisible (on);
    startTimerHz (8);
}

ParamKnob* FxModule::knob (P prm, int col, int row, const juce::String& l)
{
    auto* k = ks.add (*this, proc, prm, l);
    items.push_back ({ k, col, row, 1, false, false, {} });
    return k;
}

void FxModule::segmented (P prm, int col, int row, int span, const juce::String& caption, juce::StringArray labels)
{
    auto s = std::make_unique<Segmented> (proc, prm, std::move (labels));
    addAndMakeVisible (*s);
    items.push_back ({ s.get(), col, row, span, false, true, caption });
    owned.push_back (std::move (s));
}

void FxModule::toggle (P prm, int col, int row, const juce::String& text)
{
    auto t = std::make_unique<ToggleDot> (proc, prm, text);
    t->setStacked (true);
    addAndMakeVisible (*t);
    items.push_back ({ t.get(), col, row, 1, false, false, {} });
    owned.push_back (std::move (t));
}

void FxModule::headerControl (std::unique_ptr<juce::Component> c, int width)
{
    addAndMakeVisible (*c);
    headerExtra = c.get();
    headerExtraW = width;
    owned.push_back (std::move (c));
}

void FxModule::showWhen (juce::Component* c, std::function<bool()> visible)
{
    conditional.emplace_back (c, std::move (visible));
    c->setVisible (conditional.back().second());
}

void FxModule::setSideDisplay (std::unique_ptr<juce::Component> c)
{
    addAndMakeVisible (*c);
    side = c.get();
    owned.push_back (std::move (c));
}

int FxModule::columns() const
{
    int n = 0;
    for (const auto& it : items) n = std::max (n, it.col + it.span);
    return n;
}

void FxModule::timerCallback()
{
    for (auto& [c, visible] : conditional)
        if (const bool v = visible(); v != c->isVisible()) c->setVisible (v);

    // a switched-off effect greys out like a switched-off oscillator
    const bool active = proc.plain (onParam) > 0.5f;
    if ((int) active == shownActive) return;
    shownActive = (int) active;
    for (const auto& it : items)
    {
        it.c->setEnabled (active);
        if (dynamic_cast<ParamKnob*> (it.c) == nullptr) it.c->setAlpha (active ? 1.f : 0.4f);
    }
    for (auto* c : { headerExtra, side })
        if (c != nullptr) c->setAlpha (active ? 1.f : 0.4f);
    repaint();   // the captions
}

void FxModule::paint (juce::Graphics& g)
{
    // chain position, then the title
    const auto h = header();
    const auto num = juce::String (index);
    g.setColour (col::textFaint);
    g.setFont (mono (10.f));
    const int numW = juce::GlyphArrangement::getStringWidthInt (mono (10.f), num) + sp::sm;
    g.drawText (num, h.withWidth (numW), juce::Justification::centredLeft, false);
    drawHeading (g, title, h.withTrimmedLeft (numW));
    g.setColour (shownActive == 0 ? col::textFaint : col::textDim);
    g.setFont (label (10.f));
    for (const auto& c : captions)
        g.drawText (c.text.toUpperCase(), c.area, juce::Justification::centred, false);
}

void FxModule::resized()
{
    auto h = header();
    const int numW = juce::GlyphArrangement::getStringWidthInt (mono (10.f), juce::String (index)) + sp::sm;
    h.removeFromLeft (numW + titleWidth());
    on.setBounds (h.removeFromLeft (on.idealWidth()));
    if (headerExtra)
        headerExtra->setBounds (h.removeFromRight (headerExtraW).withSizeKeepingCentre (headerExtraW, kCtrlH));

    // two rows of cells under the header, like every other section
    const auto b = body();
    const auto gr = gridAt (b.getX(), b.getY());
    captions.clear();
    for (const auto& it : items)
    {
        if (it.control)
        {
            it.c->setBounds (gr.control (it.col, it.row, it.span));
            if (it.caption.isNotEmpty()) addCaption (it.caption, gr.cell (it.col, it.row, it.span));
        }
        else
        {
            it.c->setBounds (gr.cell (it.col, it.row, it.span));
        }
    }
    if (side)
    {
        const int x = gr.cell (columns(), 0).getX() + kCellInset + sp::sm;
        side->setBounds (juce::Rectangle<int>::leftTopRightBottom (x, b.getY(), b.getRight(), b.getBottom()));
    }
}

// ---------------------------------------------------------------------------
// FxPage
// ---------------------------------------------------------------------------
FxPage::FxPage (PsyBoxProcessor& p)
{
    const auto add = [&] (const char* t, P onParam) {
        modules.push_back (std::make_unique<FxModule> (p, (int) modules.size() + 1, t, onParam));
        addAndMakeVisible (*modules.back());
        return modules.back().get();
    };

    auto* dist = add ("Distortion", P::DistOn);
    dist->knob (P::DistDrive, 0, 0);
    dist->knob (P::DistTone, 1, 0);
    dist->knob (P::DistMix, 2, 0);
    dist->segmented (P::DistType, 0, 1, 5, "Type");

    auto* crush = add ("Crush", P::CrushOn);
    crush->knob (P::CrushBits, 0, 0);
    crush->knob (P::CrushRate, 1, 0, "Rate");
    crush->knob (P::CrushMix, 2, 0);

    auto* shift = add ("Freq Shift", P::FshiftOn);
    shift->knob (P::FshiftHz, 0, 0);
    shift->knob (P::FshiftFb, 1, 0);
    shift->knob (P::FshiftSpread, 2, 0);
    shift->knob (P::FshiftMix, 3, 0);

    auto* phaser = add ("Phaser", P::PhaserOn);
    phaser->knob (P::PhaserRate, 0, 0);
    phaser->knob (P::PhaserDepth, 1, 0);
    phaser->knob (P::PhaserFb, 2, 0);
    phaser->knob (P::PhaserCenter, 3, 0);
    phaser->knob (P::PhaserMix, 0, 1);

    auto* chorus = add ("Chorus", P::ChorusOn);
    chorus->knob (P::ChorusRate, 0, 0);
    chorus->knob (P::ChorusDepth, 1, 0);
    chorus->knob (P::ChorusFb, 2, 0);
    chorus->knob (P::ChorusMix, 3, 0);
    chorus->segmented (P::ChorusMode, 0, 1, 3, "Mode");

    // Compressor: Normal  Thresh Ratio Knee Mix Output / Attack Release
    //             Multi   Low Mid High Depth Output / Attack Release Down Up
    auto* comp = add ("Compressor", P::CompOn);
    comp->headerControl (std::make_unique<Segmented> (p, P::CompMode), cellsW (2));
    const auto multi = [&p] { return p.plain (P::CompMode) > 0.5f; };
    for (auto [prm, col, l] : { std::tuple { P::CompThresh, 0, "" }, std::tuple { P::CompRatio, 1, "" },
                                std::tuple { P::CompKnee, 2, "" }, std::tuple { P::CompMix, 3, "Mix" } })
        comp->showWhen (comp->knob (prm, col, 0, l), [multi] { return ! multi(); });
    for (auto [prm, col, row, l] : { std::tuple { P::MbLowGain, 0, 0, "" }, std::tuple { P::MbMidGain, 1, 0, "" },
                                     std::tuple { P::MbHighGain, 2, 0, "" }, std::tuple { P::CompMix, 3, 0, "Depth" },
                                     std::tuple { P::CompDown, 2, 1, "" }, std::tuple { P::CompUp, 3, 1, "" } })
        comp->showWhen (comp->knob (prm, col, row, l), multi);
    comp->knob (P::CompOut, 4, 0);
    comp->knob (P::CompAttack, 0, 1);
    comp->knob (P::CompRelease, 1, 1);
    comp->setSideDisplay (std::make_unique<CompDisplay> (p));

    auto* delay = add ("Delay", P::DelayOn);
    const auto synced = [&p] { return p.plain (P::DelaySync) > 0.5f; };
    delay->showWhen (delay->knob (P::DelayMs, 0, 0), [synced] { return ! synced(); });
    delay->showWhen (delay->knob (P::DelayDiv, 0, 0, "Division"), synced);
    delay->knob (P::DelayFb, 1, 0);
    delay->knob (P::DelayLowCut, 2, 0);
    delay->knob (P::DelayHighCut, 3, 0);
    delay->knob (P::DelayMix, 4, 0);
    delay->toggle (P::DelaySync, 0, 1, "Sync");
    delay->toggle (P::DelayPing, 1, 1, "Ping-Pong");

    auto* reverb = add ("Reverb", P::ReverbOn);
    reverb->knob (P::ReverbSize, 0, 0);
    reverb->knob (P::ReverbDecay, 1, 0);
    reverb->knob (P::ReverbDamp, 2, 0);
    reverb->knob (P::ReverbPre, 3, 0);
    reverb->knob (P::ReverbMix, 4, 0);

    auto* width = add ("Width", P::WidthOn);
    width->knob (P::WidthAmount, 0, 0);
    width->knob (P::WidthStereo, 1, 0);
    width->knob (P::WidthTime, 2, 0);
    width->knob (P::WidthMonoHz, 3, 0);
}

void FxPage::resized()
{
    // three rows; reading order is signal order
    const int w = getWidth(), rh = getHeight() / 3;
    const int third = w / 3, quarter = w / 4;
    const auto at = [&] (int i, int x, int y, int ww, int hh) { modules[(size_t) i]->setBounds (x, y, ww, hh); };
    at (0, 0, 0, third, rh);
    at (1, third, 0, third, rh);
    at (2, 2 * third, 0, w - 2 * third, rh);
    at (3, 0, rh, quarter, rh);
    at (4, quarter, rh, quarter, rh);
    at (5, 2 * quarter, rh, w - 2 * quarter, rh);
    at (6, 0, 2 * rh, third, getHeight() - 2 * rh);
    at (7, third, 2 * rh, third, getHeight() - 2 * rh);
    at (8, 2 * third, 2 * rh, w - 2 * third, getHeight() - 2 * rh);
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
    startTimerHz (10);
}

void MatrixPage::Panel::timerCallback()
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
    g.drawText ("Drag a source's arrows onto any knob on the OSC page to add a route, or set one up here.", h,
                juce::Justification::centredRight, false);

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
GlobalPage::GlobalPage (PsyBoxProcessor& p) : quality (p), voice (p), output (p)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &quality, &voice, &output, &about })
        addAndMakeVisible (c);
}

void GlobalPage::resized()
{
    const int w = getWidth();
    voice.setBounds (0, 0, w / 2, kRowH);
    quality.setBounds (w / 2, 0, w / 4, kRowH);
    output.setBounds (w / 2 + w / 4, 0, w - w / 2 - w / 4, kRowH);
    about.setBounds (0, kRowH, w, getHeight() - kRowH);
}

GlobalPage::QualityPanel::QualityPanel (PsyBoxProcessor& p) : Section ("Quality"), quality (p, P::Quality, { "Eco", "2x", "4x" })
{
    quality.setTooltip ("Oscillator oversampling: Eco 1x, 2x (default), 4x");
    addAndMakeVisible (quality);
}

void GlobalPage::QualityPanel::paint (juce::Graphics& g)
{
    Section::paint (g);
    auto b = body().withTrimmedTop (kKnobD + kLabelGap);
    g.setColour (col::textFaint);
    g.setFont (label (10.5f));
    g.drawFittedText ("Oscillators run oversampled: less aliasing on high notes and hard sync, more CPU. Eco plays one "
                      "sample per sample.", b, juce::Justification::topLeft, 4, 1.f);
}

void GlobalPage::QualityPanel::resized()
{
    quality.setBounds (grid().control (0, 0, 3));
}

GlobalPage::VoicePanel::VoicePanel (PsyBoxProcessor& p)
    : Section ("Voice"), mode (p, P::VoiceMode), glideMode (p, P::GlideMode, { "Always", "Legato" })
{
    addAndMakeVisible (mode);
    addAndMakeVisible (glideMode);
    for (auto prm : { P::PolyVoices, P::Glide, P::Octave, P::Accent, P::VelSens, P::BendRange })
        ks.add (*this, p, prm);
}

void GlobalPage::VoicePanel::resized()
{
    const auto gr = grid();
    captions.clear();
    mode.setBounds (gr.control (0, 0, 3));
    addCaption ("Play Mode", gr.cell (0, 0, 3));
    glideMode.setBounds (gr.control (3, 0, 3));
    addCaption ("Glide", gr.cell (3, 0, 3));
    for (int i = 0; i < (int) ks.knobs.size(); ++i)
        ks.knobs[(size_t) i]->setBounds (gr.cell (i, 1));
}

GlobalPage::OutputPanel::OutputPanel (PsyBoxProcessor& p) : Section ("Output")
{
    ks.add (*this, p, P::MasterVol);
}

void GlobalPage::OutputPanel::resized()
{
    ks.knobs[0]->setBounds (grid().cell (0, 0));
}

void GlobalPage::AboutPanel::paint (juce::Graphics& g)
{
    Section::paint (g);
    // one card per idea, three to a row
    static constexpr std::pair<const char*, const char*> tips[] = {
        { "Generate", "Pick a sound in the sidebar and press Generate for a new patch in that style. Same sound and seed, "
                      "same patch: double-click the seed to type one." },
        { "Lock", "Right-click a knob and pick Lock (or alt-click it) to keep its value through the next Generate." },
        { "Modulate", "Drag the arrows of an envelope, LFO, macro or the Sweep onto any knob. Drag the ring that appears "
                      "to set the amount; right-click to invert or remove." },
        { "Macros", "Every Generate gives the four macros a job and names them after it. Double-click a name to type "
                    "your own." },
        { "Length", "Energy sounds run over a length in bars or seconds, and the whole gesture stretches with it: pitch, "
                    "filter and level." },
        { "Arp", "Melodic sounds get a 16-step arp on the ARP page: paint steps, accents and slides, or start from a "
                 "template." },
    };
    constexpr int kCols = 3, kCardH = 100;
    const auto b = body();
    const int cardW = (b.getWidth() - (kCols - 1) * sp::md) / kCols;
    for (int i = 0; i < (int) std::size (tips); ++i)
    {
        const juce::Rectangle<int> r (b.getX() + (i % kCols) * (cardW + sp::md), b.getY() + (i / kCols) * (kCardH + sp::md),
                                      cardW, kCardH);
        g.setColour (col::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.f);
        g.setColour (col::line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.f, 1.f);
        auto t = r.reduced (kPad);
        g.setColour (col::text);
        g.setFont (labelStrong (12.5f));
        g.drawText (tips[i].first, t.removeFromTop (18), juce::Justification::centredLeft, false);
        t.removeFromTop (sp::xs);
        g.setColour (col::textDim);
        g.setFont (label (11.5f));
        g.drawFittedText (tips[i].second, t, juce::Justification::topLeft, 3, 1.f);
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
    templates.setBounds (b.removeFromBottom (Templates::heightFor (2)));
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
        g.setColour (sel ? accent() : (i == hover ? col::lineStrong : col::line));
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
                g.setColour (stepColour (pt, s, true));
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
        for (int i = 0; i < psy::kArpSteps; ++i)
            if (cellArea (row, i).expanded (kBeatGap / 2, kLaneGap / 2).contains (pt))
                return { row, i };
    }
    return {};
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
        g.setColour ((on ? accent() : col::textDim).withMultipliedAlpha (noteDim));
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

