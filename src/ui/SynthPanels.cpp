#include "SynthPanels.h"

#include "PhosPanels.h"

#include "dsp/Unison.h"

using namespace theme;
using psy::P;

// ---------------------------------------------------------------------------
// UnisonSettings
// ---------------------------------------------------------------------------
UnisonSettings::UnisonSettings (PsyBoxProcessor& p, bool b)
    : proc (p), isB (b),
      mode (p, b ? P::OscBUniMode : P::OscAUniMode, "Mode"),
      stack (p, b ? P::OscBUniStack : P::OscAUniStack, "Stack")
{
    addAndMakeVisible (mode);
    addAndMakeVisible (stack);
    range = ks.add (*this, p, b ? P::OscBUniRange : P::OscAUniRange);
    spread = ks.add (*this, p, b ? P::OscBUniSpread : P::OscAUniSpread);
    startTimerHz (30);
}

void UnisonSettings::setType (int t)
{
    spread->setLabel (t == (int) psy::OscType::Wavetable ? "WT Pos" : (t == (int) psy::OscType::FM ? "Index" : "Shape"));
}

namespace
{
    // The settings the voice view draws, Osc A's or Osc B's.
    struct UniIds { P count, detune, blend, width, range, mode, stack; };
    UniIds uniIds (bool b)
    {
        if (b) return { P::OscBUnison, P::OscBDetune, P::OscBBlend, P::OscBUniWidth, P::OscBUniRange, P::OscBUniMode, P::OscBUniStack };
        return { P::OscAUnison, P::OscADetune, P::OscABlend, P::OscAUniWidth, P::OscAUniRange, P::OscAUniMode, P::OscAUniStack };
    }
} // namespace

uint64_t UnisonSettings::print() const
{
    const auto id = uniIds (isB);
    uint64_t h = kFnvSeed;
    for (auto prm : { id.count, id.detune, id.blend, id.width, id.range, id.mode, id.stack })
        h = fnvMix (h, (uint64_t) std::lround (proc.plain (prm) * 1000.f));
    return h ^ (uint64_t) accent (*this).getARGB();
}

void UnisonSettings::timerCallback()
{
    if (! isShowing()) return;
    const auto h = print();
    if (h != lastPrint) { lastPrint = h; repaint (linesArea); }
}

void UnisonSettings::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (b, 4.f);
    g.setColour (col::line);
    g.drawRoundedRectangle (b.reduced (0.5f), 4.f, 1.f);

    // where the voices sit in pitch (across the range) and in level
    const auto r = linesArea.toFloat();
    const auto id = uniIds (isB);
    psy::dsp::UnisonLayout l;
    const float pitchRange = proc.plain (id.range);
    l.compute ((int) proc.plain (id.count), proc.plain (id.detune), pitchRange, (int) proc.plain (id.mode), (int) proc.plain (id.stack),
               proc.plain (id.blend), proc.plain (id.width));
    float span = std::max (0.05f, pitchRange * 0.5f), maxGain = 0.f;
    for (int k = 0; k < l.count; ++k)
    {
        span = std::max (span, std::abs (l.semis[(size_t) k]));
        maxGain = std::max (maxGain, l.gain[(size_t) k]);
    }
    g.setColour (col::grid);
    g.fillRect (r.getX(), r.getBottom() - 1.f, r.getWidth(), 1.f);
    g.fillRect (r.getCentreX() - 0.5f, r.getY(), 1.f, r.getHeight());
    const bool on = isEnabled();
    for (int k = l.count - 1; k >= 0; --k)
    {
        const float rel = maxGain > 0.f ? l.gain[(size_t) k] / maxGain : 0.f;
        const float x = r.getCentreX() + juce::jlimit (-1.f, 1.f, l.semis[(size_t) k] / span) * (r.getWidth() * 0.5f - 3.f);
        const float h = (r.getHeight() - 4.f) * (0.3f + 0.7f * rel);
        g.setColour (on ? accent (*this).withAlpha (0.3f + 0.7f * rel) : col::textFaint);
        g.fillRoundedRectangle (x - 1.f, r.getBottom() - h, 2.f, h, 1.f);
    }
    g.setColour (on ? col::textDim : col::textFaint);
    g.setFont (label (10.f));
    g.drawText ("VOICES", linesArea.withY (linesArea.getBottom() + kLabelGap).withHeight (kLabelH), juce::Justification::centred, false);
}

void UnisonSettings::resized()
{
    // the selectors on the left, one per row; range and spread on the
    // right, and under them the voices
    const auto b = getLocalBounds().reduced (sp::sm);
    const int gridH = 2 * kCellH + sp::xs;
    const CellGrid gr { b.getX() - kCellInset + sp::sm, b.getCentreY() - gridH / 2 };
    const auto cell = [&] (int col, int row, int span) {
        return juce::Rectangle<int> (gr.x0 + col * kCellW, gr.y0 + row * (kCellH + sp::xs), span * kCellW, kCellH);
    };
    mode.setBounds (cell (0, 0, 2).reduced (kCellInset, 0));
    stack.setBounds (cell (0, 1, 2).reduced (kCellInset, 0));
    range->setBounds (cell (2, 0, 1));
    spread->setBounds (cell (3, 0, 1));
    linesArea = cell (2, 1, 2).reduced (kCellInset, 0).withHeight (kKnobD);
}

// ---------------------------------------------------------------------------
// OscPanel
// ---------------------------------------------------------------------------
OscPanel::OscPanel (PsyBoxProcessor& p, bool b)
    : Section (b ? "Osc B" : "Osc A"), proc (p), isB (b),
      onToggle (p, b ? P::OscBOn : P::OscAOn, "On"),
      type (p, b ? P::OscBType : P::OscAType),
      semi (p, b ? P::OscBCoarse : P::OscACoarse, "Sem"),
      fine (p, b ? P::OscBFine : P::OscAFine, "Fin"),
      unison (p, b ? P::OscBUnison : P::OscAUnison, "Unison"),
      wave (p, b),
      settings (p, b),
      table (p, b ? P::OscBWt : P::OscAWt, "Table"),
      retrig (p, b ? P::OscBRetrig : P::OscARetrig, "Retrig")
{
    addAndMakeVisible (onToggle);
    addAndMakeVisible (type);
    addAndMakeVisible (semi);
    addAndMakeVisible (fine);
    addAndMakeVisible (wave);
    addChildComponent (settings);
    addChildComponent (table);
    addAndMakeVisible (retrig);
    retrig.setTooltip ("Start the waveform at the same phase on every note");

    unison.setCellStyle (true);
    unison.setGear (true);
    unison.setTooltip ("Unison voices, 1 to 16: drag or scroll. The gear opens the spacing, stack, range and spread");
    unison.onGear = [this] {
        showSettings = ! showSettings;
        unison.setGearOn (showSettings);
        settings.setVisible (showSettings);
        wave.setVisible (! showSettings);
    };
    unison.onActive = [this] (bool active) { wave.showVoices (active); };
    addAndMakeVisible (unison);

    shape = ks.add (*this, p, b ? P::OscBShape : P::OscAShape);
    pw = ks.add (*this, p, b ? P::OscBPw : P::OscAPw);
    wtpos = ks.add (*this, p, b ? P::OscBWtPos : P::OscAWtPos, "WT Pos");
    ratio = ks.add (*this, p, b ? P::OscBFmRatio : P::OscAFmRatio);
    fm = ks.add (*this, p, b ? P::OscBFm : P::OscAFm, "Index");
    fb = ks.add (*this, p, b ? P::OscBFmFb : P::OscAFmFb);
    detune = ks.add (*this, p, b ? P::OscBDetune : P::OscADetune);
    blend = ks.add (*this, p, b ? P::OscBBlend : P::OscABlend);
    width = ks.add (*this, p, b ? P::OscBUniWidth : P::OscAUniWidth);
    level = ks.add (*this, p, b ? P::OscBLevel : P::OscALevel, "Level");
    if (b)
    {
        xmod = ks.add (*this, p, P::OscBXMod);
        ring = ks.add (*this, p, P::RingLevel);
        sync = std::make_unique<ToggleDot> (p, P::OscBSync, "Sync");
        sync->setTooltip ("Hard-sync B to A: sweep B's pitch for the classic sync scream");
        addAndMakeVisible (*sync);
    }
    refresh();
    startTimerHz (15);
}

void OscPanel::timerCallback()
{
    if (isShowing()) refresh();
}

void OscPanel::refresh()
{
    const bool on = proc.plain (isB ? P::OscBOn : P::OscAOn) > 0.5f;
    const int t = (int) proc.plain (isB ? P::OscBType : P::OscAType);
    if (t != lastType || on != lastOn)
    {
        lastType = t;
        lastOn = on;
        // the supersaw is seven voices of its own: Detune spreads them, the
        // unison count, blend and width don't apply
        const bool supersaw = t == (int) psy::OscType::Supersaw;
        for (auto& k : ks.knobs)
            k->setEnabled (on && ! (supersaw && (k.get() == blend || k.get() == width)));
        table.setEnabled (on);
        semi.setEnabled (on);
        fine.setEnabled (on);
        unison.setEnabled (on && ! supersaw);
        unison.setOverride (supersaw ? "7" : juce::String());
        settings.setEnabled (on && ! supersaw);
        settings.setType (t);
        if (showSettings && ! (on && ! supersaw))
        {
            // the gear is dead for this type: close the settings it opened
            showSettings = false;
            unison.setGearOn (false);
            settings.setVisible (false);
            wave.setVisible (true);
        }
        resized();
    }
}

void OscPanel::resized()
{
    auto h = header();
    h.removeFromLeft (titleWidth());
    onToggle.setBounds (h.removeFromLeft (onToggle.idealWidth()));
    type.setBounds (h.removeFromRight (cellsW (2) + sp::lg).withSizeKeepingCentre (cellsW (2) + sp::lg, kCtrlH));

    // pitch fields and switches on top, knobs along the bottom, the
    // waveform (or the unison settings) fills the rest
    auto b = body();
    const int gridH = 2 * kCellH + kRowGap;
    const auto gr = gridAt (b.getX(), b.getBottom() - gridH);
    auto top = b.withTrimmedBottom (gridH + sp::md);
    auto strip = top.removeFromTop (kCtrlH);
    top.removeFromTop (sp::sm);
    wave.setBounds (top);
    settings.setBounds (top);

    const int fieldW = std::max (semi.idealWidth(), fine.idealWidth());
    semi.setBounds (strip.removeFromLeft (fieldW));
    strip.removeFromLeft (sp::xs);
    fine.setBounds (strip.removeFromLeft (fieldW));
    if (sync)
    {
        sync->setBounds (strip.removeFromRight (sync->idealWidth()));
        strip.removeFromRight (sp::md);
    }
    retrig.setBounds (strip.removeFromRight (retrig.idealWidth()));

    for (auto& k : ks.knobs) k->setVisible (false);
    table.setVisible (false);
    const auto put = [&] (juce::Component* c, int col, int row) { c->setBounds (gr.cell (col, row)); c->setVisible (true); };
    unison.setBounds (gr.cell (1, 0));
    put (detune, 2, 0);
    put (blend, 3, 0);
    put (width, 4, 0);
    switch (lastType < 0 ? 0 : lastType)
    {
        case 1: break;   // supersaw: Detune is all it has
        case 2: put (wtpos, 0, 0); table.setBounds (gr.captioned (0, 1, 2)); table.setVisible (true); break;
        case 3: put (fm, 0, 0); put (ratio, 0, 1); put (fb, 1, 1); break;
        default: put (shape, 0, 0); put (pw, 0, 1); break;
    }
    if (xmod) put (xmod, 2, 1);
    if (ring) put (ring, 3, 1);
    put (level, 4, 1);
}

// ---------------------------------------------------------------------------
// SubPanel / NoisePanel
// ---------------------------------------------------------------------------
SubPanel::SubPanel (PsyBoxProcessor& p)
    : Section ("Sub"), oct (p, P::SubOct, { "-1", "-2" }), shape (p, P::SubShape, "Wave")
{
    ks.add (*this, p, P::SubLevel, "Level");
    addAndMakeVisible (oct);
    addAndMakeVisible (shape);
}

void SubPanel::resized()
{
    const auto gr = grid();
    ks.knobs[0]->setBounds (gr.cell (0, 0));
    oct.setBounds (gr.control (1, 0));
    captions.clear();
    addCaption ("Octave", gr.cell (1, 0));
    shape.setBounds (gr.captioned (0, 1, 2));
}

NoisePanel::NoisePanel (PsyBoxProcessor& p)
    : Section ("Noise"), direct (p, P::NoiseDirect, "Direct"), type (p, P::NoiseType, "Type")
{
    ks.add (*this, p, P::NoiseLevel, "Level");
    direct.setStacked (true);
    direct.setTooltip ("Noise skips the filters and is added after them: bright grit over a dark sound");
    addAndMakeVisible (direct);
    addAndMakeVisible (type);
}

void NoisePanel::resized()
{
    const auto gr = grid();
    ks.knobs[0]->setBounds (gr.cell (0, 0));
    direct.setBounds (gr.cell (1, 0));
    type.setBounds (gr.captioned (0, 1, 2));
}

// ---------------------------------------------------------------------------
// FilterPanel
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
        routing->setTooltip ("Filter 2 after filter 1 (serial) or beside it (parallel)");
        addAndMakeVisible (*routing);
    }
    refresh();
    startTimerHz (15);
}

void FilterPanel::timerCallback()
{
    if (isShowing()) refresh();
}

void FilterPanel::refresh()
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
    type.setBounds (h.removeFromRight (128).withSizeKeepingCentre (128, kCtrlH));
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
// ModPanel
// ---------------------------------------------------------------------------
ModPanel::ModPanel (PsyBoxProcessor& p, bool phoscyon)
    : Section ("Modulation"), proc (p), envDisplay (p), pitchDisplay (p), lfoDisplay (p), phos (phoscyon)
{
    using S = psy::ModSrc;
    const std::pair<S, const char*> tabList[kNumPages] = { { S::AmpEnv, phos ? "VCA" : "Amp Env" }, { S::FilterEnv, phos ? "MEG" : "Env 1" },
                                                           { S::ModEnv, "Env 2" }, { S::PitchEnv, "Env 3" },
                                                           { S::Lfo1, "LFO 1" }, { S::Lfo2, "LFO 2" }, { S::Lfo3, "LFO 3" } };
    for (int i = 0; i < kNumPages; ++i)
    {
        auto t = std::make_unique<ModSourceHandle> (p, tabList[i].first, tabList[i].second);
        t->setTab (true);
        static const char* const also[kNumPages] = { "", " (it also drives the filters' Env knobs)", "",
                                                     " (it also bends the pitch by its Amount)", "", "", "" };
        static const char* const alsoPhos[kNumPages] = { " (the 303's amp envelope)", " (the 303's filter envelope)", "", "", "", "", "" };
        t->setTooltip ("Click to edit " + juce::String (tabList[i].second) + (phos ? alsoPhos[i] : also[i])
                       + ", drag onto a knob to modulate it");
        t->onClick = [this, i] { show (i); };
        if (phos && i == PitchEnv) addChildComponent (*t);   // the 303 has no pitch envelope
        else addAndMakeVisible (*t);
        tabs.push_back (std::move (t));
    }
    const std::pair<S, const char*> more[] = { { S::Velocity, "Velocity" }, { S::Note, "Note" }, { S::ModWheel, "Mod Wheel" },
                                               { S::Aftertouch, "Aftertouch" }, { S::PitchBend, "Bend" }, { S::Random, "Random" } };
    for (const auto& [src, name] : more)
    {
        auto h = std::make_unique<ModSourceHandle> (p, src, name);
        addAndMakeVisible (*h);
        extras.push_back (std::move (h));
    }

    addChildComponent (envDisplay);
    addChildComponent (pitchDisplay);
    addChildComponent (lfoDisplay);

    static const P envIds[3][4] = { { P::AenvA, P::AenvD, P::AenvS, P::AenvR },
                                    { P::FenvA, P::FenvD, P::FenvS, P::FenvR },
                                    { P::MenvA, P::MenvD, P::MenvS, P::MenvR } };
    if (phos)
    {
        // the 303's envelopes have no ADSR: the knobs that shape them instead
        vcaDisplay = std::make_unique<PhosEnvDisplay> (p, true);
        megDisplay = std::make_unique<PhosEnvDisplay> (p, false);
        addChildComponent (*vcaDisplay);
        addChildComponent (*megDisplay);
        pages[AmpEnv].push_back (ks.add (*this, p, P::PhosAccent, "Accent"));
        pages[AmpEnv].push_back (ks.add (*this, p, P::PhosAccVolume, "Acc. Vol."));
        pages[FilterEnv].push_back (ks.add (*this, p, P::PhosEnvMod, "Envmod"));
        pages[FilterEnv].push_back (ks.add (*this, p, P::PhosDecay, "Decay"));
        pages[FilterEnv].push_back (ks.add (*this, p, P::PhosEnvAttack, "Env. Attack"));
        pages[FilterEnv].push_back (ks.add (*this, p, P::PhosAccDecay, "Acc. Decay"));
        for (int i = 0; i < 4; ++i)
            pages[ModEnv].push_back (ks.add (*this, p, envIds[2][i]));
    }
    else
    {
        for (int e = 0; e < 3; ++e)
            for (int i = 0; i < 4; ++i)
                pages[(size_t) e].push_back (ks.add (*this, p, envIds[e][i]));
        pages[AmpEnv].push_back (ks.add (*this, p, P::AenvCurve));   // the attack's bend: RC charge to swell
        pages[AmpEnv].push_back (ks.add (*this, p, P::AenvStart));   // the level the attack starts from
    }

    if (! phos)
    {
        for (auto [prm, name] : { std::pair { P::PenvAmt, "Amount" }, std::pair { P::PenvAttack, "Attack" },
                                  std::pair { P::PenvDecay, "Decay" }, std::pair { P::PenvCurve, "Curve" } })
            pages[PitchEnv].push_back (ks.add (*this, p, prm, name));
        auto dest = std::make_unique<ParamCombo> (p, P::PenvDest, "Target");
        addAndMakeVisible (*dest);
        pages[PitchEnv].push_back (dest.get());
        owned.push_back (std::move (dest));
    }

    for (int i = 0; i < 3; ++i)
    {
        auto& pg = pages[(size_t) (Lfo1 + i)];
        auto shape = std::make_unique<ParamCombo> (p, psy::lfoParam (i, 0), "Shape");
        addAndMakeVisible (*shape);
        pg.push_back (shape.get());
        owned.push_back (std::move (shape));
        lfoRate[(size_t) i] = ks.add (*this, p, psy::lfoParam (i, 1));
        lfoDiv[(size_t) i] = ks.add (*this, p, psy::lfoParam (i, 3), "Division");
        pg.push_back (lfoRate[(size_t) i]);
        pg.push_back (lfoDiv[(size_t) i]);
        for (auto [off, name, tip] : { std::tuple { 2, "Sync", "Lock the rate to the host tempo" },
                                       std::tuple { 4, "Retrig", "Restart for every note (per voice); off runs free for all voices" } })
        {
            auto t = std::make_unique<ToggleDot> (p, psy::lfoParam (i, off), name);
            t->setStacked (true);
            t->setTooltip (tip);
            addAndMakeVisible (*t);
            pg.push_back (t.get());
            owned.push_back (std::move (t));
        }
        pg.push_back (ks.add (*this, p, psy::lfoParam (i, 5)));
        lastSync[(size_t) i] = proc.plain (psy::lfoParam (i, 2)) > 0.5f;
    }
    show (0);
    startTimerHz (10);
}

void ModPanel::show (int page)
{
    current = page;
    for (int i = 0; i < kNumPages; ++i)
    {
        tabs[(size_t) i]->setSelected (i == page);
        for (auto* c : pages[(size_t) i]) c->setVisible (i == page);
    }
    if (page <= ModEnv) envDisplay.setEnvelope (page);
    envDisplay.setVisible (phos ? page == ModEnv : page <= ModEnv);
    if (phos)
    {
        vcaDisplay->setVisible (page == AmpEnv);
        megDisplay->setVisible (page == FilterEnv);
    }
    pitchDisplay.setVisible (page == PitchEnv);
    lfoDisplay.setVisible (page >= Lfo1);
    if (page >= Lfo1)
    {
        const int l = page - Lfo1;
        lfoDisplay.setLfo (l);
        const bool sync = proc.plain (psy::lfoParam (l, 2)) > 0.5f;
        lastSync[(size_t) l] = sync;
        lfoRate[(size_t) l]->setVisible (! sync);
        lfoDiv[(size_t) l]->setVisible (sync);
    }
    resized();
    repaint();
}

void ModPanel::timerCallback()
{
    if (current < Lfo1 || ! isShowing()) return;
    const int l = current - Lfo1;
    if ((proc.plain (psy::lfoParam (l, 2)) > 0.5f) != lastSync[(size_t) l])
        show (current);
}

void ModPanel::paint (juce::Graphics& g)
{
    // no title: the tabs are the header
    g.setColour (col::textDim);
    g.setFont (label (10.f));
    for (const auto& c : captions)
        g.drawText (c.text.toUpperCase(), c.area, juce::Justification::centred, false);
    g.setColour (col::textFaint);
    g.drawText ("SOURCES", stripCaption, juce::Justification::centredLeft, false);

    // a hint beside the knobs, centred on their dials
    const auto b = body();
    const int knobsY = b.getBottom() - kCtrlH - sp::md - kCellH;
    const int used = current == PitchEnv || current >= Lfo1 ? 6 : (int) pages[(size_t) current].size();
    auto hint = juce::Rectangle<int> (b.getX() + used * kCellW, knobsY, b.getRight() - b.getX() - used * kCellW, kKnobD);
    g.setFont (label (10.5f));
    g.drawFittedText ("Drag a source's arrows onto any knob to modulate it. Drag the ring that appears to set the amount, "
                      "right-click it to invert or remove.", hint.withTrimmedLeft (sp::lg), juce::Justification::centredRight, 3, 1.f);
}

void ModPanel::resized()
{
    auto h = header();
    int x = h.getX();
    for (auto& t : tabs)
    {
        if (phos && t.get() == tabs[PitchEnv].get()) continue;
        const int w = t->idealWidth();
        t->setBounds (x, h.getY(), w, h.getHeight());
        x += w + sp::lg;
    }

    auto b = body();
    auto strip = b.removeFromBottom (kCtrlH);
    b.removeFromBottom (sp::md);
    const auto gr = gridAt (b.getX(), b.getBottom() - kCellH);
    const auto disp = b.withTrimmedBottom (kCellH + sp::md);
    envDisplay.setBounds (disp);
    pitchDisplay.setBounds (disp);
    lfoDisplay.setBounds (disp);
    if (phos)
    {
        vcaDisplay->setBounds (disp);
        megDisplay->setBounds (disp);
    }

    captions.clear();
    for (int e = 0; e <= ModEnv; ++e)
        for (size_t i = 0; i < pages[(size_t) e].size(); ++i)   // ADSR, and the amp's Curve
            pages[(size_t) e][i]->setBounds (gr.cell ((int) i, 0));
    if (! phos)
    {
        for (int i = 0; i < 4; ++i)
            pages[PitchEnv][(size_t) i]->setBounds (gr.cell (i, 0));
        pages[PitchEnv][4]->setBounds (gr.captioned (4, 0, 2));
    }
    for (int l = 0; l < 3; ++l)
    {
        auto& pg = pages[(size_t) (Lfo1 + l)];
        pg[0]->setBounds (gr.captioned (0, 0, 2));   // shape
        pg[1]->setBounds (gr.cell (2, 0));           // rate
        pg[2]->setBounds (gr.cell (2, 0));           // division
        pg[3]->setBounds (gr.cell (3, 0));           // sync
        pg[4]->setBounds (gr.cell (4, 0));           // retrig
        pg[5]->setBounds (gr.cell (5, 0));           // phase
    }

    stripCaption = strip.removeFromLeft (juce::GlyphArrangement::getStringWidthInt (label (10.f), "SOURCES") + sp::lg);
    for (auto& e : extras)
    {
        e->setBounds (strip.removeFromLeft (e->idealWidth()));
        strip.removeFromLeft (sp::lg);
    }
}

// ---------------------------------------------------------------------------
// MacroPanel
// ---------------------------------------------------------------------------
MacroPanel::MacroPanel (PsyBoxProcessor& p) : Section ("Macros"), proc (p)
{
    const P ids[4] = { P::Macro1, P::Macro2, P::Macro3, P::Macro4 };
    for (int i = 0; i < 4; ++i)
    {
        auto* k = ks.add (*this, p, ids[i]);
        k->setCompact (true);
        auto h = std::make_unique<ModSourceHandle> (p, (psy::ModSrc) ((int) psy::ModSrc::Macro1 + i), "Macro " + juce::String (i + 1));
        addAndMakeVisible (*h);
        handles.push_back (std::move (h));
    }
    refresh();
    startTimerHz (8);
}

juce::Rectangle<int> MacroPanel::rowArea (int i) const
{
    auto b = body();
    const int h = b.getHeight() / 4;
    return { b.getX(), b.getY() + i * h, b.getWidth(), h };
}

juce::Rectangle<int> MacroPanel::nameArea (int i) const
{
    const auto r = rowArea (i);
    return { r.getX() + 60, r.getY() + 12, r.getWidth() - 60, 20 };
}

juce::Rectangle<int> MacroPanel::valueArea (int i) const
{
    return nameArea (i).translated (0, 24).withHeight (16);
}

void MacroPanel::timerCallback()
{
    if (isShowing()) refresh();
}

// One snapshot of the parameters for all four labels (getMacroName and
// macroDrivesAnything would each take their own).
void MacroPanel::refresh()
{
    psy::ParamState state;
    for (int i = 0; i < psy::kNumParams; ++i)
        state.v[(size_t) i] = proc.plain ((P) i);
    bool changed = false;
    for (int i = 0; i < 4; ++i)
    {
        const auto label = psy::macroLabel (state, i);
        const bool d = ! label.empty();
        const auto n = proc.isMacroNameTyped (i) ? proc.getMacroName (i)
                                                 : (d ? juce::String (label) : "Macro " + juce::String (i + 1));
        if (n != names[(size_t) i] || d != drives[(size_t) i])
        {
            names[(size_t) i] = n;
            drives[(size_t) i] = d;
            changed = true;
        }
        const int pct = juce::roundToInt (state.v[(size_t) ((int) P::Macro1 + i)] * 100.f);
        if (pct != values[(size_t) i])
        {
            values[(size_t) i] = pct;
            repaint (valueArea (i));   // just the readout
        }
    }
    if (changed) repaint();
}

void MacroPanel::paint (juce::Graphics& g)
{
    Section::paint (g);
    for (int i = 0; i < 4; ++i)
    {
        const auto r = rowArea (i);
        if (i > 0)
        {
            g.setColour (col::line);
            g.fillRect (r.getX(), r.getY(), r.getWidth(), 1);
        }
        g.setColour (drives[(size_t) i] || proc.isMacroNameTyped (i) ? col::text : col::textFaint);
        g.setFont (labelStrong (13.f));
        g.drawText (names[(size_t) i], nameArea (i), juce::Justification::centredLeft, true);
        // value, right of the handle
        g.setColour (col::textFaint);
        g.setFont (mono (10.f));
        g.drawText (juce::String (values[(size_t) i]) + "%", valueArea (i), juce::Justification::centredRight, false);
    }
}

void MacroPanel::resized()
{
    for (int i = 0; i < 4; ++i)
    {
        const auto r = rowArea (i);
        ks.knobs[(size_t) i]->setBounds (r.getX(), r.getY() + 10, 46, 46);
        handles[(size_t) i]->setBounds (valueArea (i).withWidth (handles[(size_t) i]->idealWidth()));
    }
}

void MacroPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    for (int i = 0; i < 4; ++i)
        if (nameArea (i).contains (e.getPosition()))
        {
            editor = std::make_unique<juce::TextEditor>();
            editor->setFont (labelStrong (13.f));
            editor->setInputRestrictions (14);
            editor->setIndents (2, 0);
            editor->setBorder ({});
            editor->setColour (juce::TextEditor::backgroundColourId, col::well);
            editor->setText (names[(size_t) i], false);
            editor->selectAll();
            editor->setTooltip ("Type a name, or clear it for the automatic one");
            const auto commit = [this, i] (bool keep) {
                if (editor == nullptr || ! editor->isVisible()) return;
                const auto text = editor->getText();
                editor->setVisible (false);
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MacroPanel> (this)] {
                    if (safe != nullptr) safe->editor.reset();
                });
                if (keep) proc.setMacroName (i, text);
                refresh();
            };
            editor->onReturnKey = [commit] { commit (true); };
            editor->onEscapeKey = [commit] { commit (false); };
            editor->onFocusLost = [commit] { commit (true); };
            addAndMakeVisible (*editor);
            editor->setBounds (nameArea (i));
            editor->grabKeyboardFocus();
            return;
        }
}

// ---------------------------------------------------------------------------
// VoicingPanel
//   row 0: play mode, voices
//   row 1: glide, octave, accent, velocity
//   row 2 (tall panel): bend range, glide mode (else a toggle in the header)
// ---------------------------------------------------------------------------
VoicingPanel::VoicingPanel (PsyBoxProcessor& p)
    : Section ("Voicing"), mode (p, P::VoiceMode), glideMode (p, P::GlideMode, { "Always", "Legato" }),
      modeMenu (p, P::VoiceMode, "Play Mode"), legatoGlide (p, P::GlideMode, "Legato glide")
{
    addAndMakeVisible (mode);
    addChildComponent (modeMenu);
    addChildComponent (glideMode);
    addAndMakeVisible (legatoGlide);
    glideMode.setTooltip ("Glide into every note, or only between overlapping notes (303 style)");
    legatoGlide.setTooltip ("Glide only between overlapping notes (303 style); off glides into every note");
    knobs = { ks.add (*this, p, P::PolyVoices), ks.add (*this, p, P::Glide), ks.add (*this, p, P::Octave),
              ks.add (*this, p, P::Accent), ks.add (*this, p, P::VelSens) };
    bend = ks.add (*this, p, P::BendRange);
}

void VoicingPanel::resized()
{
    const bool tall = body().getHeight() >= 3 * kCellH + 2 * kRowGap;
    auto h = header();
    legatoGlide.setBounds (h.removeFromRight (legatoGlide.idealWidth()));
    legatoGlide.setVisible (! tall);
    const auto gr = grid();
    captions.clear();
    // tall: the mode as buttons and Bend in the third row; short (Length or
    // Knock underneath): the mode as a menu and Bend beside Voices
    mode.setVisible (tall);
    modeMenu.setVisible (! tall);
    if (tall)
    {
        mode.setBounds (gr.control (0, 0, 3));
        addCaption ("Play Mode", gr.cell (0, 0, 3));
        knobs[0]->setBounds (gr.cell (3, 0));
        bend->setBounds (gr.cell (0, 2));
    }
    else
    {
        modeMenu.setBounds (gr.captioned (0, 0, 2));
        knobs[0]->setBounds (gr.cell (2, 0));
        bend->setBounds (gr.cell (3, 0));
    }
    for (int i = 1; i < 5; ++i)
        knobs[(size_t) i]->setBounds (gr.cell (i - 1, 1));
    glideMode.setVisible (tall);
    glideMode.setBounds (gr.control (1, 2, 3));
    if (tall) addCaption ("Glide", gr.cell (1, 2, 3));
}

// ---------------------------------------------------------------------------
// Length (energy sounds)
// ---------------------------------------------------------------------------
uint64_t SweepDisplay::fingerprint() const
{
    uint64_t h = hashParams ({ P::EnergyMode, P::EnergyBars, P::EnergySec, P::EnergyCurve, P::EnergyDelay, P::EnergyFade, P::EnergyCut })
               ^ (uint64_t) std::lround (proc.getBpm() * 100.0);
    for (int k = 0; k < view.numVoices; ++k)
        h = mixPos (h, view.voices[(size_t) k].sweep);
    return h;
}

void SweepDisplay::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    const bool time = proc.plain (P::EnergyMode) > 0.5f;
    const double seconds = proc.energyLengthSeconds();
    const float bars = psy::energyBarsValue ((int) std::lround (proc.plain (P::EnergyBars)));
    const float ex = std::exp2 (2.5f * proc.plain (P::EnergyCurve));
    const float delay = proc.plain (P::EnergyDelay);
    const float fade = proc.plain (P::EnergyFade);
    const bool cut = proc.plain (P::EnergyCut) > 0.5f;

    auto area = r.reduced (6.f, 6.f);
    const float lenW = area.getWidth() * (cut ? 1.f : 0.86f);   // without the cut, the sound holds after the end
    // bar (or second) lines
    g.setColour (col::grid);
    const float units = time ? (float) seconds : bars * 4.f;   // seconds, or beats
    const float step = time ? (seconds > 20.0 ? 5.f : (seconds > 6.0 ? 1.f : 0.5f)) : (bars >= 16.f ? 16.f : (bars >= 2.f ? 4.f : 1.f));
    for (float u = step; u < units - 1e-3f; u += step)
        g.fillRect (area.getX() + lenW * u / units, area.getY(), 1.f, area.getHeight());

    // the sweep (dim) and the level (accent) over the length
    juce::Path sweepPath, level;
    const int n = 80;
    for (int i = 0; i <= n; ++i)
    {
        const float t = (float) i / (float) n;
        const float s = psy::sweepValue (t, delay, ex);
        const float lv = fade >= 0.f ? 1.f - fade * (1.f - s) : 1.f + fade * s;
        const float x = area.getX() + lenW * t;
        const auto pt = juce::Point<float> (x, area.getBottom() - area.getHeight() * s);
        const auto pl = juce::Point<float> (x, area.getBottom() - area.getHeight() * lv);
        if (i == 0) { sweepPath.startNewSubPath (pt); level.startNewSubPath (area.getX(), area.getBottom()); }
        else sweepPath.lineTo (pt);
        level.lineTo (pl);
    }
    const float endLevel = fade >= 0.f ? 1.f : 1.f + fade;
    if (cut)
    {
        level.lineTo (area.getX() + lenW, area.getBottom());
    }
    else
    {
        level.lineTo (area.getRight(), area.getBottom() - area.getHeight() * endLevel);
        level.lineTo (area.getRight(), area.getBottom());
    }
    level.closeSubPath();
    g.setColour (accent (*this).withAlpha (0.14f));
    g.fillPath (level);
    g.setColour (accent (*this));
    g.strokePath (level, juce::PathStrokeType (1.4f));
    g.setColour (col::text.withAlpha (0.5f));
    g.strokePath (sweepPath, juce::PathStrokeType (1.f));

    // the notes, on the sweep
    for (int k = view.numVoices - 1; k >= 0; --k)
    {
        const float t = view.voices[(size_t) k].sweep;
        drawDot (g, { area.getX() + lenW * t, area.getBottom() - area.getHeight() * psy::sweepValue (t, delay, ex) }, k == 0, r);
    }

    // readout: the length in both units
    g.setColour (col::textDim);
    g.setFont (mono (9.5f));
    const auto barsText = juce::String (time ? seconds * proc.getBpm() / 240.0 : (double) bars, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    const auto text = time ? juce::String (seconds, 2) + " s  ~ " + barsText + " bars"
                           : juce::String (psy::def (P::EnergyBars).format (proc.plain (P::EnergyBars))) + "  = " + juce::String (seconds, 1) + " s";
    g.drawText (text, area.toNearestInt().reduced (2, 0), juce::Justification::topLeft, false);
}

LengthPanel::LengthPanel (PsyBoxProcessor& p)
    : Section ("Length"), proc (p), sweep (p, psy::ModSrc::Sweep, "Sweep"), unit (p, P::EnergyMode),
      cut (p, P::EnergyCut, "Cut"), oneShot (p, P::EnergyOneShot, "One Shot"), display (p)
{
    sweep.setTooltip ("The sweep runs 0 to 100 % over the length. Drag it onto any knob to move that knob with the sound");
    unit.setTooltip ("Length in bars (follows the tempo) or in seconds");
    unit.onPick = [this] (int i) { proc.setEnergyMode ((psy::EnergyMode) i); };
    cut.setStacked (true);
    cut.setTooltip ("Stop the note at the end of the length, however long it is held");
    oneShot.setStacked (true);
    oneShot.setTooltip ("Play the whole length from a tap, like a one-shot sample: letting go of the key doesn't stop it");
    for (juce::Component* c : { (juce::Component*) &sweep, (juce::Component*) &unit, (juce::Component*) &cut, (juce::Component*) &oneShot,
                                (juce::Component*) &display })
        addAndMakeVisible (c);
    bars = ks.add (*this, p, P::EnergyBars);
    seconds = ks.add (*this, p, P::EnergySec);
    curve = ks.add (*this, p, P::EnergyCurve);
    fade = ks.add (*this, p, P::EnergyFade);
    delay = ks.add (*this, p, P::EnergyDelay);   // the sweep waits this share of the length
    lastTime = proc.plain (P::EnergyMode) > 0.5f;
    bars->setVisible (! lastTime);
    seconds->setVisible (lastTime);
    startTimerHz (10);
}

void LengthPanel::timerCallback()
{
    if (! isShowing()) return;
    const bool t = proc.plain (P::EnergyMode) > 0.5f;
    if (t == lastTime) return;
    lastTime = t;
    bars->setVisible (! t);
    seconds->setVisible (t);
}

void LengthPanel::paint (juce::Graphics& g)
{
    Section::paint (g);
}

void LengthPanel::resized()
{
    auto h = header();
    h.removeFromLeft (titleWidth());
    sweep.setBounds (h.removeFromLeft (sweep.idealWidth()));
    unit.setBounds (h.removeFromRight (cellsW (2)).withSizeKeepingCentre (cellsW (2), kCtrlH));
    const auto gr = grid();
    // the length and how the sweep runs over it on top; Cut, One Shot and
    // the picture of it below
    bars->setBounds (gr.cell (0, 0));
    seconds->setBounds (gr.cell (0, 0));
    delay->setBounds (gr.cell (1, 0));
    curve->setBounds (gr.cell (2, 0));
    fade->setBounds (gr.cell (3, 0));
    cut.setBounds (gr.cell (0, 1));
    oneShot.setBounds (gr.cell (1, 1));
    const auto row = gr.cell (2, 1, 2);
    display.setBounds (row.withX (row.getX() + kCellInset).withRight (body().getRight()));
}

// ---------------------------------------------------------------------------
// KnockPanel
// ---------------------------------------------------------------------------
KnockPanel::KnockPanel (PsyBoxProcessor& p) : Section ("Knock"), display (p)
{
    ks.add (*this, p, P::PenvAmt, "Amount");
    ks.add (*this, p, P::PenvAttack, "Attack");
    ks.add (*this, p, P::PenvDecay, "Decay");
    ks.add (*this, p, P::PenvCurve, "Curve");
    addAndMakeVisible (display);
}

void KnockPanel::resized()
{
    const auto gr = grid();
    for (int i = 0; i < 4; ++i)
        ks.knobs[(size_t) i]->setBounds (gr.cell (i, 0));
    display.setBounds (gr.cell (0, 1, 4).withX (body().getX()).withWidth (body().getWidth()));
}
