#include "Panels.h"

using namespace theme;
using psy::P;

namespace
{
    constexpr int kKnobW = 58;   // knob cell width
    constexpr int kKnobH = 60;   // knob cell height (incl. label)
    constexpr int kSmallKnobH = 54;

    juce::String hexSeed (uint32_t s) { return juce::String::toHexString ((int) s).toUpperCase().paddedLeft ('0', 6); }
} // namespace

// ---------------------------------------------------------------------------
// Section
// ---------------------------------------------------------------------------
void Section::paint (juce::Graphics& g)
{
    drawHeading (g, title, header());
}

int Section::titleWidth() const
{
    return juce::GlyphArrangement::getStringWidthInt (heading(), title.toUpperCase()) + 12;
}

void Section::row (juce::Rectangle<int> area, const std::vector<juce::Component*>& items, int cellW)
{
    if (items.empty()) return;
    const int n = (int) items.size();
    const int w = cellW > 0 ? cellW : area.getWidth() / n;
    int x = area.getX();
    for (auto* c : items)
    {
        c->setBounds (x, area.getY(), w, area.getHeight());
        x += w;
    }
}

// ---------------------------------------------------------------------------
// OscPanel
// ---------------------------------------------------------------------------
OscPanel::OscPanel (PsySynthProcessor& p, bool b)
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
    shape = ks.add (*this, p, b ? P::OscBShape : P::OscAShape);
    pw = ks.add (*this, p, b ? P::OscBPw : P::OscAPw);
    detune = ks.add (*this, p, b ? P::OscBDetune : P::OscADetune);
    wtpos = ks.add (*this, p, b ? P::OscBWtPos : P::OscAWtPos);
    ratio = ks.add (*this, p, b ? P::OscBFmRatio : P::OscAFmRatio);
    fm = ks.add (*this, p, b ? P::OscBFm : P::OscAFm, "Index");
    fb = ks.add (*this, p, b ? P::OscBFmFb : P::OscAFmFb);
    coarse = ks.add (*this, p, b ? P::OscBCoarse : P::OscACoarse);
    fine = ks.add (*this, p, b ? P::OscBFine : P::OscAFine);
    level = ks.add (*this, p, b ? P::OscBLevel : P::OscALevel);
    if (b)
    {
        xmod = ks.add (*this, p, P::OscBXMod);
        sync = std::make_unique<ToggleDot> (p, P::OscBSync, "Sync");
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
    type.setBounds (h.removeFromRight (196).withSizeKeepingCentre (196, 20));

    auto b = body();
    auto left = b.removeFromLeft (112);
    wave.setBounds (left.removeFromTop (78));
    left.removeFromTop (10);
    retrig.setBounds (left.removeFromTop (18).withWidth (retrig.idealWidth()));
    if (sync)
    {
        left.removeFromTop (6);
        sync->setBounds (left.removeFromTop (18).withWidth (sync->idealWidth()));
    }
    b.removeFromLeft (10);

    for (auto& k : ks.knobs) k->setVisible (false);
    table.setVisible (false);

    const int t = lastType < 0 ? 0 : lastType;
    std::vector<juce::Component*> top;
    switch (t)
    {
        case 1: top = { detune }; break;
        case 2: top = { &table, wtpos }; break;
        case 3: top = { ratio, fm, fb }; break;
        default: top = { shape, pw }; break;
    }
    std::vector<juce::Component*> bottom { coarse, fine, level };
    if (xmod) bottom.push_back (xmod);

    auto rowTop = b.removeFromTop (kKnobH);
    b.removeFromTop (6);
    auto rowBottom = b.removeFromTop (kKnobH);
    int x = rowTop.getX();
    for (auto* c : top)
    {
        const int w = c == &table ? 110 : kKnobW;
        if (c == &table)
            c->setBounds (x + 2, rowTop.getY() + 12, w - 16, 32);
        else
            c->setBounds (x, rowTop.getY(), w, rowTop.getHeight());
        c->setVisible (true);
        x += w;
    }
    row (rowBottom, bottom, kKnobW);
    for (auto* c : bottom) c->setVisible (true);
}

// ---------------------------------------------------------------------------
// MixerPanel: sources on top, the pitch envelope below.
// ---------------------------------------------------------------------------
MixerPanel::MixerPanel (PsySynthProcessor& p)
    : Section ("Mixer"),
      subOct (p, P::SubOct, { "-1", "-2" }),
      subShape (p, P::SubShape), noiseType (p, P::NoiseType), penvDest (p, P::PenvDest),
      penvDisplay (p)
{
    sub = ks.add (*this, p, P::SubLevel);
    noise = ks.add (*this, p, P::NoiseLevel);
    ring = ks.add (*this, p, P::RingLevel);
    pAmt = ks.add (*this, p, P::PenvAmt, "Amount");
    pAtk = ks.add (*this, p, P::PenvAttack, "Attack");
    pDec = ks.add (*this, p, P::PenvDecay, "Decay");
    pCurve = ks.add (*this, p, P::PenvCurve, "Curve");
    for (juce::Component* c : { (juce::Component*) &subOct, (juce::Component*) &subShape, (juce::Component*) &noiseType,
                                (juce::Component*) &penvDest, (juce::Component*) &penvDisplay })
        addAndMakeVisible (c);
}

void MixerPanel::paint (juce::Graphics& g)
{
    Section::paint (g);
    auto b = body();
    const int y = b.getY() + kKnobH + 2;
    g.setColour (col::line);
    g.fillRect (b.getX(), y, b.getWidth(), 1);
    drawHeading (g, "Pitch Env", { b.getX(), y + 2, 120, 20 });
}

void MixerPanel::resized()
{
    auto b = body();
    auto top = b.removeFromTop (kKnobH);
    row (top.removeFromLeft (kKnobW * 3), { sub, noise, ring }, kKnobW);
    top.removeFromLeft (8);
    auto col1 = top;
    subOct.setBounds (col1.removeFromTop (20).withWidth (64));
    col1.removeFromTop (6);
    auto combos = col1.removeFromTop (22);
    subShape.setBounds (combos.removeFromLeft (combos.getWidth() / 2 - 4));
    combos.removeFromLeft (8);
    noiseType.setBounds (combos);

    b.removeFromTop (4);
    auto hdr = b.removeFromTop (20);
    penvDest.setBounds (hdr.removeFromRight (78).withSizeKeepingCentre (78, 20));
    auto bottom = b.removeFromTop (kKnobH);
    row (bottom.removeFromLeft (kKnobW * 4), { pAmt, pAtk, pDec, pCurve }, kKnobW);
    bottom.removeFromLeft (6);
    penvDisplay.setBounds (bottom.withTrimmedBottom (ParamKnob::kLabelH).reduced (0, 4));
}

// ---------------------------------------------------------------------------
// FilterPanel
// ---------------------------------------------------------------------------
FilterPanel::FilterPanel (PsySynthProcessor& p, int s)
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
        if (vowel) vowel->setEnabled (t == (int) psy::FilterType::Formant);
        resized();
    }
}

void FilterPanel::resized()
{
    auto h = header();
    h.removeFromLeft (titleWidth());
    type.setBounds (h.removeFromRight (104).withSizeKeepingCentre (104, 22));
    if (routing)
    {
        h.removeFromRight (10);
        routing->setBounds (h.removeFromRight (104).withSizeKeepingCentre (104, 20));
    }

    auto b = body();
    const int dispW = slot == 0 ? 176 : 120;
    display.setBounds (b.removeFromLeft (dispW).withTrimmedBottom (4));
    b.removeFromLeft (8);
    auto r1 = b.removeFromTop (kKnobH);
    b.removeFromTop (6);
    auto r2 = b.removeFromTop (kKnobH);
    row (r1, { cutoff, reso, drive });
    if (vowel) row (r2, { env, key, vowel });
    else row (r2, { env, key }, r2.getWidth() / 3);
}

// ---------------------------------------------------------------------------
// VoicePanel
// ---------------------------------------------------------------------------
VoicePanel::VoicePanel (PsySynthProcessor& p)
    : Section ("Voice"),
      mode (p, P::VoiceMode), glideMode (p, P::GlideMode, { "Always", "Legato" })
{
    addAndMakeVisible (mode);
    addAndMakeVisible (glideMode);
    knobs = { ks.add (*this, p, P::PolyVoices), ks.add (*this, p, P::Glide), ks.add (*this, p, P::Accent),
              ks.add (*this, p, P::Octave), ks.add (*this, p, P::BendRange), ks.add (*this, p, P::VelSens) };
}

void VoicePanel::paint (juce::Graphics& g)
{
    Section::paint (g);
    auto b = body();
    g.setColour (col::textDim);
    g.setFont (label (10.f));
    g.drawText ("PLAY MODE", b.getX(), b.getY(), 150, 14, juce::Justification::centredLeft);
    g.drawText ("GLIDE", b.getX() + b.getWidth() - 132, b.getY(), 130, 14, juce::Justification::centredLeft);
}

void VoicePanel::resized()
{
    auto b = body();
    auto top = b.removeFromTop (40).withTrimmedTop (16);
    glideMode.setBounds (top.removeFromRight (132).withHeight (22));
    top.removeFromRight (14);
    mode.setBounds (top.withHeight (22));
    b.removeFromTop (14);
    row (b.removeFromTop (kKnobH), { knobs.begin(), knobs.end() });
}

// ---------------------------------------------------------------------------
// EnvPanel
// ---------------------------------------------------------------------------
EnvPanel::EnvPanel (PsySynthProcessor& p) : Section ("Envelopes"), display (p)
{
    addAndMakeVisible (tabs);
    addAndMakeVisible (display);
    static const P ids[3][4] = { { P::AenvA, P::AenvD, P::AenvS, P::AenvR },
                                 { P::FenvA, P::FenvD, P::FenvS, P::FenvR },
                                 { P::MenvA, P::MenvD, P::MenvS, P::MenvR } };
    for (int e = 0; e < 3; ++e)
        for (int i = 0; i < 4; ++i)
            knobs[e][i] = ks.add (*this, p, ids[e][i]);
    tabs.onChange = [this] (int i) { show (i); };
    show (0);
}

void EnvPanel::show (int e)
{
    display.setEnvelope (e);
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < 4; ++i)
            knobs[k][i]->setVisible (k == e);
}

void EnvPanel::resized()
{
    auto h = header();
    h.removeFromLeft (titleWidth());
    tabs.setBounds (h.removeFromRight (190));

    auto b = body();
    auto knobArea = b.removeFromRight (kKnobW * 4);
    b.removeFromRight (10);
    display.setBounds (b.withTrimmedBottom (4));
    for (int e = 0; e < 3; ++e)
    {
        auto r = knobArea.withSizeKeepingCentre (knobArea.getWidth(), kKnobH);
        row (r, { knobs[e][0], knobs[e][1], knobs[e][2], knobs[e][3] }, kKnobW);
    }
}

// ---------------------------------------------------------------------------
// LfoPanel
// ---------------------------------------------------------------------------
LfoPanel::LfoPanel (PsySynthProcessor& p) : Section ("LFO"), proc (p), display (p)
{
    addAndMakeVisible (tabs);
    addAndMakeVisible (display);
    for (int i = 0; i < 3; ++i)
    {
        auto& pg = pages[i];
        pg.shape = std::make_unique<ParamCombo> (p, psy::lfoParam (i, 0), "Shape");
        pg.div = std::make_unique<ParamCombo> (p, psy::lfoParam (i, 3), "Division");
        pg.rate = ks.add (*this, p, psy::lfoParam (i, 1));
        pg.phase = ks.add (*this, p, psy::lfoParam (i, 5));
        pg.sync = std::make_unique<ToggleDot> (p, psy::lfoParam (i, 2), "Sync");
        pg.retrig = std::make_unique<ToggleDot> (p, psy::lfoParam (i, 4), "Retrig");
        addChildComponent (*pg.shape);
        addChildComponent (*pg.div);
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
    tabs.setBounds (h.removeFromRight (100));

    auto b = body();
    auto left = b.removeFromLeft (132);
    display.setBounds (left.removeFromTop (84));
    left.removeFromTop (10);
    auto toggles = left.removeFromTop (18);
    for (auto& pg : pages)
    {
        auto t = toggles;
        pg.sync->setBounds (t.removeFromLeft (pg.sync->idealWidth()));
        t.removeFromLeft (12);
        pg.retrig->setBounds (t.removeFromLeft (pg.retrig->idealWidth()));
    }
    b.removeFromLeft (12);
    for (auto& pg : pages)
    {
        auto r = b;
        pg.shape->setBounds (r.removeFromTop (38).withTrimmedBottom (4));
        r.removeFromTop (6);
        auto kr = r.removeFromTop (kKnobH);
        pg.rate->setBounds (kr.removeFromLeft (kKnobW));
        pg.div->setBounds (juce::Rectangle<int> (kr.getX() - kKnobW, kr.getY() + 10, kKnobW + 18, 36));
        pg.phase->setBounds (kr.removeFromLeft (kKnobW).translated (18, 0));
    }
}

// ---------------------------------------------------------------------------
// ModMatrixPanel
// ---------------------------------------------------------------------------
ModMatrixPanel::ModMatrixPanel (PsySynthProcessor& p) : Section ("Mod Matrix")
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
    auto b = body().withTrimmedTop (-4);
    const int rh = b.getHeight() / psy::kNumModSlots;
    for (int i = 0; i < psy::kNumModSlots; ++i)
    {
        auto r = b.removeFromTop (rh).reduced (0, 1);
        rows[i].src->setBounds (r.removeFromLeft (98));
        r.removeFromLeft (10);
        rows[i].dst->setBounds (r.removeFromLeft (108));
        r.removeFromLeft (12);
        rows[i].amt->setBounds (r);
    }
}

// ---------------------------------------------------------------------------
// FxPanel
// ---------------------------------------------------------------------------
FxPanel::FxPanel (PsySynthProcessor& p) : Section ("Effects"), proc (p)
{
    addAndMakeVisible (tabs);
    static const P onIds[7] = { P::DistOn, P::CrushOn, P::FshiftOn, P::PhaserOn, P::ChorusOn, P::DelayOn, P::ReverbOn };
    tabs.isLit = [this] (int i) { return proc.plain (onIds[i]) > 0.5f; };
    tabs.onChange = [this] (int i) { show (i); };

    const auto knob = [&] (int page, P prm, const juce::String& l = {}) {
        auto* k = ks.add (*this, p, prm, l);
        pages[page].items.push_back (k);
        return k;
    };
    const auto own = [&] (int page, std::unique_ptr<juce::Component> c) {
        addAndMakeVisible (*c);
        pages[page].items.push_back (c.get());
        pages[page].owned.push_back (std::move (c));
    };
    for (int i = 0; i < 7; ++i)
    {
        pages[i].on = std::make_unique<ToggleDot> (p, onIds[i], "Enabled");
        addAndMakeVisible (*pages[i].on);
    }

    own (0, std::make_unique<Segmented> (p, P::DistType));
    knob (0, P::DistDrive); knob (0, P::DistTone); knob (0, P::DistMix);

    knob (1, P::CrushBits); knob (1, P::CrushRate); knob (1, P::CrushMix);

    knob (2, P::FshiftHz); knob (2, P::FshiftFb); knob (2, P::FshiftSpread); knob (2, P::FshiftMix);

    knob (3, P::PhaserRate); knob (3, P::PhaserDepth); knob (3, P::PhaserFb); knob (3, P::PhaserCenter); knob (3, P::PhaserMix);

    own (4, std::make_unique<Segmented> (p, P::ChorusMode));
    knob (4, P::ChorusRate); knob (4, P::ChorusDepth); knob (4, P::ChorusFb); knob (4, P::ChorusMix);

    own (5, std::make_unique<ToggleDot> (p, P::DelaySync, "Sync"));
    own (5, std::make_unique<ToggleDot> (p, P::DelayPing, "Ping-Pong"));
    delayDiv = std::make_unique<ParamCombo> (p, P::DelayDiv, "Division");
    addAndMakeVisible (*delayDiv);
    delayTime = knob (5, P::DelayMs);
    knob (5, P::DelayFb); knob (5, P::DelayLowCut); knob (5, P::DelayHighCut); knob (5, P::DelayMix);

    knob (6, P::ReverbSize); knob (6, P::ReverbDecay); knob (6, P::ReverbDamp); knob (6, P::ReverbPre); knob (6, P::ReverbMix);

    show (0);
    startTimerHz (10);
}

void FxPanel::show (int page)
{
    current = page;
    for (int i = 0; i < 7; ++i)
    {
        const bool vis = i == page;
        pages[i].on->setVisible (vis);
        for (auto* c : pages[i].items) c->setVisible (vis);
    }
    lastDelaySync = proc.plain (P::DelaySync) > 0.5f;
    delayDiv->setVisible (page == 5 && lastDelaySync);
    delayTime->setVisible (page == 5 && ! lastDelaySync);
    resized();
}

void FxPanel::timerCallback()
{
    tabs.repaint();
    const bool s = proc.plain (P::DelaySync) > 0.5f;
    if (s != lastDelaySync) show (current);
}

void FxPanel::resized()
{
    auto h = header();
    h.removeFromLeft (titleWidth());
    tabs.setBounds (h.withTrimmedLeft (8));

    const auto b = body();
    for (int i = 0; i < 7; ++i)
    {
        auto r = b;
        auto side = r.removeFromLeft (112);
        pages[i].on->setBounds (side.removeFromTop (20).withWidth (pages[i].on->idealWidth()));
        side.removeFromTop (12);

        // A type selector sits above the knobs; otherwise knobs are centred.
        Segmented* seg = nullptr;
        for (auto* c : pages[i].items)
            if ((seg = dynamic_cast<Segmented*> (c)) != nullptr) break;
        int ky = r.getY() + (r.getHeight() - kKnobH) / 2 - 8;
        if (seg != nullptr)
        {
            seg->setBounds (r.getX(), r.getY(), i == 0 ? 300 : 150, 22);
            ky = r.getY() + 34;
        }

        int x = r.getX();
        for (auto* c : pages[i].items)
        {
            if (c == seg) continue;
            if (auto* t = dynamic_cast<ToggleDot*> (c))
            {
                t->setBounds (side.removeFromTop (18).withWidth (t->idealWidth()));
                side.removeFromTop (8);
                continue;
            }
            if (c == delayTime)
                delayDiv->setBounds (x + 4, ky + 12, kKnobW + 16, 36);
            c->setBounds (x, ky, kKnobW + 6, kKnobH);
            x += kKnobW + 18;
        }
    }
}

// ---------------------------------------------------------------------------
MacroPanel::MacroPanel (PsySynthProcessor& p) : Section ("Macros")
{
    ks.add (*this, p, P::Macro1, "M1");
    ks.add (*this, p, P::Macro2, "M2");
    ks.add (*this, p, P::Macro3, "M3");
    ks.add (*this, p, P::Macro4, "M4");
}

void MacroPanel::resized()
{
    auto b = body();
    auto r1 = b.removeFromTop (b.getHeight() / 2);
    row (r1, { ks.knobs[0].get(), ks.knobs[1].get() });
    row (b, { ks.knobs[2].get(), ks.knobs[3].get() });
}

// ---------------------------------------------------------------------------
OutputPanel::OutputPanel (PsySynthProcessor& p) : Section ("Output"), proc (p), scope (p)
{
    addAndMakeVisible (scope);
    startTimerHz (8);
}

void OutputPanel::timerCallback()
{
    const int v = proc.getActiveVoices();
    if (v != lastVoices) { lastVoices = v; repaint (header()); }
}

void OutputPanel::paint (juce::Graphics& g)
{
    Section::paint (g);
    g.setColour (col::textFaint);
    g.setFont (mono (10.f));
    const int v = std::max (0, lastVoices);
    g.drawText (juce::String (v) + (v == 1 ? " VOICE" : " VOICES"), header(), juce::Justification::centredRight, false);
}

void OutputPanel::resized()
{
    scope.setBounds (body());
}

// ---------------------------------------------------------------------------
// Sidebar
// ---------------------------------------------------------------------------
Sidebar::Sidebar (PsySynthProcessor& p) : proc (p)
{
    generateButton.getProperties().set ("primary", true);
    generateButton.setTooltip ("New sound in this category (locked knobs are kept)");
    generateButton.onClick = [this] { proc.generate(); };
    mutateButton.setTooltip ("Vary the current sound within the category");
    mutateButton.onClick = [this] { proc.mutate(); };
    clearLocks.setTooltip ("Unlock all parameters");
    clearLocks.onClick = [this] { proc.clearLocks(); };
    undoButton.onClick = [this] { proc.undo(); };
    redoButton.onClick = [this] { proc.redo(); };

    mutateAmount.setSliderStyle (juce::Slider::LinearHorizontal);
    mutateAmount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    mutateAmount.setRange (0.02, 1.0);
    mutateAmount.setValue (proc.getMutateAmount(), juce::dontSendNotification);
    mutateAmount.setTooltip ("Mutation amount");
    mutateAmount.onValueChange = [this] { proc.setMutateAmount ((float) mutateAmount.getValue()); repaint (infoArea()); };

    seedLabel.setFont (mono (11.f));
    seedLabel.setColour (juce::Label::textColourId, col::textDim);
    seedLabel.setJustificationType (juce::Justification::centredRight);
    seedLabel.setEditable (false, true);
    seedLabel.setTooltip ("Seed. Double-click to type one and regenerate that exact sound.");
    seedLabel.onTextChange = [this] {
        const auto t = seedLabel.getText().trim().removeCharacters ("#");
        if (t.containsOnly ("0123456789abcdefABCDEF") && t.isNotEmpty())
            proc.generateWithSeed ((uint32_t) t.getHexValue32() & 0xFFFFFFu);
        refresh();
    };

    for (juce::Component* c : { (juce::Component*) &generateButton, (juce::Component*) &mutateButton, (juce::Component*) &clearLocks,
                                (juce::Component*) &mutateAmount, (juce::Component*) &undoButton, (juce::Component*) &redoButton,
                                (juce::Component*) &seedLabel })
        addAndMakeVisible (c);
    proc.addChangeListener (this);
    refresh();
}

Sidebar::~Sidebar() { proc.removeChangeListener (this); }

void Sidebar::changeListenerCallback (juce::ChangeBroadcaster*) { refresh(); }

void Sidebar::refresh()
{
    seedLabel.setText ("#" + hexSeed (proc.getSeed()), juce::dontSendNotification);
    undoButton.setEnabled (proc.canUndo());
    redoButton.setEnabled (proc.canRedo());
    clearLocks.setVisible (proc.numLocks() > 0);
    repaint();
}

juce::Rectangle<int> Sidebar::rowBounds (int i) const
{
    return { 0, kListTop + i * kRowH, getWidth(), kRowH };
}

juce::Rectangle<int> Sidebar::infoArea() const
{
    return getLocalBounds().withTrimmedTop (kListTop + psy::kNumCategories * kRowH + 10);
}

int Sidebar::categoryAt (juce::Point<int> pt) const
{
    for (int i = 0; i < psy::kNumCategories; ++i)
        if (rowBounds (i).contains (pt)) return i;
    return -1;
}

void Sidebar::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::line);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());

    drawHeading (g, "Sound", { kPad, 12, 100, 20 });
    g.setColour (col::textFaint);
    g.setFont (mono (10.f));
    g.drawText (juce::String (psy::kNumCategories), juce::Rectangle<int> (kPad, 12, getWidth() - kPad * 2, 20), juce::Justification::centredRight);

    const int sel = (int) proc.getCategory();
    const auto& info = psy::categoryInfo();
    for (int i = 0; i < psy::kNumCategories; ++i)
    {
        const auto r = rowBounds (i);
        const auto c = juce::Colour ((juce::uint32) (0xff000000 | info[(size_t) i].colour));
        const bool isSel = i == sel;
        if (isSel)
        {
            g.setColour (c.withAlpha (0.10f));
            g.fillRect (r);
            g.setColour (c);
            g.fillRect (r.getX(), r.getY() + 4, 2, r.getHeight() - 8);
        }
        else if (i == hover)
        {
            g.setColour (juce::Colours::white.withAlpha (0.03f));
            g.fillRect (r);
        }
        const float cy = (float) r.getCentreY();
        g.setColour (isSel || i == hover ? c : c.withAlpha (0.55f));
        g.fillEllipse ((float) kPad + 1.f, cy - 3.5f, 7.f, 7.f);
        g.setColour (isSel ? col::text : (i == hover ? col::text.withAlpha (0.85f) : col::textDim));
        g.setFont (isSel ? labelStrong (12.5f) : label (12.5f));
        g.drawText (info[(size_t) i].name, r.withTrimmedLeft (kPad + 18), juce::Justification::centredLeft, false);
    }

    // info block
    auto a = infoArea().reduced (kPad, 0);
    g.setColour (col::line);
    g.fillRect (0, a.getY() - 6, getWidth() - 1, 1);
    const auto acc = accent();
    g.setColour (acc);
    g.setFont (title (17.f));
    g.drawText (info[(size_t) sel].name, a.removeFromTop (30).withTrimmedTop (6), juce::Justification::centredLeft, false);
    g.setColour (col::textDim);
    g.setFont (label (11.f));
    g.drawFittedText (info[(size_t) sel].blurb, a.removeFromTop (30), juce::Justification::topLeft, 2, 1.f);

    // mutate amount readout
    g.setColour (col::textFaint);
    g.setFont (mono (10.f));
    g.drawText (juce::String (juce::roundToInt (proc.getMutateAmount() * 100.f)) + "%",
                mutateAmount.getBounds().translated (0, -12).withHeight (12), juce::Justification::centredRight, false);
    g.setFont (label (10.f));
    g.drawText ("AMOUNT", mutateAmount.getBounds().translated (0, -12).withHeight (12), juce::Justification::centredLeft, false);

    if (proc.numLocks() > 0)
    {
        g.setColour (col::textDim);
        g.setFont (label (10.f));
        g.drawText (juce::String (proc.numLocks()) + " LOCKED", clearLocks.getBounds().withX (kPad).withWidth (100),
                    juce::Justification::centredLeft, false);
    }
}

void Sidebar::resized()
{
    auto a = infoArea().reduced (kPad, 0);
    a.removeFromTop (66);
    generateButton.setBounds (a.removeFromTop (40));
    a.removeFromTop (10);
    auto r = a.removeFromTop (28);
    mutateButton.setBounds (r.removeFromLeft (84));
    r.removeFromLeft (10);
    mutateAmount.setBounds (r.withTrimmedTop (12));
    a.removeFromTop (12);
    auto bottom = a.removeFromTop (26);
    undoButton.setBounds (bottom.removeFromLeft (26));
    redoButton.setBounds (bottom.removeFromLeft (26));
    seedLabel.setBounds (bottom);
    a.removeFromTop (6);
    clearLocks.setBounds (a.removeFromTop (22).removeFromRight (56));
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

// ---------------------------------------------------------------------------
// TopBar
// ---------------------------------------------------------------------------
TopBar::TopBar (PsySynthProcessor& p)
    : proc (p), quality (p, P::Quality, { "Eco", "2x", "4x" }), volume (p, P::MasterVol, "Vol")
{
    for (juce::Component* c : { (juce::Component*) &prev, (juce::Component*) &next, (juce::Component*) &menu,
                                (juce::Component*) &save, (juce::Component*) &quality, (juce::Component*) &volume })
        addAndMakeVisible (c);
    prev.onClick = [this] { proc.stepPreset (-1); };
    next.onClick = [this] { proc.stepPreset (1); };
    menu.onClick = [this] { showPresetMenu(); };
    save.onClick = [this] { savePresetDialog(); };
    quality.setTooltip ("Oscillator oversampling: Eco 1x, 2x (default), 4x");
    proc.addChangeListener (this);
}

TopBar::~TopBar() { proc.removeChangeListener (this); }

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::line);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    // wordmark
    g.setColour (col::text);
    g.setFont (juce::Font (juce::FontOptions (heading (15.f).getTypefacePtr()).withHeight (15.f).withKerningFactor (0.32f)));
    g.drawText ("PSYSYNTH", juce::Rectangle<int> (kPad + 16, 0, 140, getHeight()), juce::Justification::centredLeft, false);
    g.setColour (accent());
    g.fillEllipse ((float) kPad, (float) getHeight() * 0.5f - 4.f, 8.f, 8.f);

    // preset display
    auto r = presetArea;
    g.setColour (col::well);
    g.fillRoundedRectangle (r.toFloat(), 4.f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.f, 1.f);
    auto text = r.reduced (40, 0);
    const auto& info = psy::categoryInfo()[(size_t) proc.getCategory()];
    g.setColour (accent());
    g.setFont (heading (9.5f));
    const auto catName = juce::String (info.shortName).toUpperCase();
    const int cw = juce::GlyphArrangement::getStringWidthInt (heading (9.5f), catName) + 12;
    g.drawText (catName, text.removeFromLeft (cw), juce::Justification::centredLeft, false);
    g.setColour (col::text);
    g.setFont (label (12.5f));
    g.drawText (proc.getPresetName(), text, juce::Justification::centredLeft, true);

    g.setColour (col::textFaint);
    g.setFont (label (10.f));
    g.drawText ("QUALITY", quality.getBounds().withX (quality.getX() - 58).withWidth (54), juce::Justification::centredRight, false);
}

void TopBar::resized()
{
    auto r = getLocalBounds();
    presetArea = r.withSizeKeepingCentre (380, 32).translated (60, 0);
    prev.setBounds (presetArea.getX() + 4, presetArea.getY() + 4, 24, 24);
    next.setBounds (presetArea.getRight() - 28, presetArea.getY() + 4, 24, 24);
    menu.setBounds (presetArea.getRight() + 8, presetArea.getY() + 4, 24, 24);
    save.setBounds (presetArea.getRight() + 36, presetArea.getY() + 4, 24, 24);

    auto right = r.reduced (kPad, 0);
    volume.setBounds (right.removeFromRight (46).withSizeKeepingCentre (46, 44));
    right.removeFromRight (18);
    quality.setBounds (right.removeFromRight (120).withSizeKeepingCentre (120, 22));
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
        else if (res == 3) PsySynthProcessor::presetFolder().revealToUser();
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
