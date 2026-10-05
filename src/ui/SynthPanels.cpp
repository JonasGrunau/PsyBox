#include "SynthPanels.h"

using namespace theme;
using psy::P;

// ---------------------------------------------------------------------------
// OscPanel
// ---------------------------------------------------------------------------
OscPanel::OscPanel (PsyBoxProcessor& p, bool b)
    : Section (b ? "Osc B" : "Osc A"), proc (p), isB (b),
      onToggle (p, b ? P::OscBOn : P::OscAOn, "On"),
      type (p, b ? P::OscBType : P::OscAType),
      wave (p, b),
      table (p, b ? P::OscBWt : P::OscAWt),
      retrig (p, b ? P::OscBRetrig : P::OscARetrig, "Retrig")
{
    addAndMakeVisible (onToggle);
    addAndMakeVisible (type);
    addAndMakeVisible (wave);
    addChildComponent (table);
    addAndMakeVisible (retrig);
    retrig.setTooltip ("Start the waveform at the same phase on every note");
    shape = ks.add (*this, p, b ? P::OscBShape : P::OscAShape);
    pw = ks.add (*this, p, b ? P::OscBPw : P::OscAPw);
    detune = ks.add (*this, p, b ? P::OscBDetune : P::OscADetune);
    wtpos = ks.add (*this, p, b ? P::OscBWtPos : P::OscAWtPos, "Position");
    ratio = ks.add (*this, p, b ? P::OscBFmRatio : P::OscAFmRatio);
    fm = ks.add (*this, p, b ? P::OscBFm : P::OscAFm, "Index");
    fb = ks.add (*this, p, b ? P::OscBFmFb : P::OscAFmFb);
    level = ks.add (*this, p, b ? P::OscBLevel : P::OscALevel, "Level");
    coarse = ks.add (*this, p, b ? P::OscBCoarse : P::OscACoarse);
    fine = ks.add (*this, p, b ? P::OscBFine : P::OscAFine);
    if (b)
    {
        xmod = ks.add (*this, p, P::OscBXMod);
        ring = ks.add (*this, p, P::RingLevel);
        sync = std::make_unique<ToggleDot> (p, P::OscBSync, "Sync");
        sync->setTooltip ("Hard-sync B to A: sweep B's pitch for the classic sync scream");
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
    type.setBounds (h.removeFromRight (cellsW (2) + sp::lg).withSizeKeepingCentre (cellsW (2) + sp::lg, kCtrlH));

    // knobs along the bottom, switches above them, the waveform fills the rest
    auto b = body();
    const int gridH = 2 * kCellH + kRowGap;
    const auto gr = gridAt (b.getX(), b.getBottom() - gridH);
    auto top = b.withTrimmedBottom (gridH + sp::md);
    auto switches = top.removeFromBottom (kCtrlH);
    top.removeFromBottom (sp::sm);
    wave.setBounds (top);

    retrig.setBounds (switches.removeFromLeft (retrig.idealWidth()));
    if (sync)
    {
        switches.removeFromLeft (sp::lg);
        sync->setBounds (switches.removeFromLeft (sync->idealWidth()));
    }
    table.setBounds (switches.removeFromRight (cellsW (2) + sp::xl));

    for (auto& k : ks.knobs) k->setVisible (false);
    table.setVisible (false);
    const auto put = [&] (juce::Component* c, int col, int row) { c->setBounds (gr.cell (col, row)); c->setVisible (true); };
    // Level first, then the knobs of the oscillator type, so no row has a gap
    put (level, 0, 0);
    switch (lastType < 0 ? 0 : lastType)
    {
        case 1: put (detune, 1, 0); break;
        case 2: put (wtpos, 1, 0); table.setVisible (true); break;
        case 3: put (ratio, 1, 0); put (fm, 2, 0); put (fb, 3, 0); break;
        default: put (shape, 1, 0); put (pw, 2, 0); break;
    }
    put (coarse, 0, 1);
    put (fine, 1, 1);
    if (xmod) put (xmod, 2, 1);
    if (ring) put (ring, 3, 1);
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
ModPanel::ModPanel (PsyBoxProcessor& p) : Section ("Modulation"), proc (p), envDisplay (p), pitchDisplay (p), lfoDisplay (p)
{
    using S = psy::ModSrc;
    const std::pair<S, const char*> tabList[kNumPages] = { { S::AmpEnv, "Amp Env" }, { S::FilterEnv, "Filter Env" },
                                                           { S::ModEnv, "Mod Env" }, { S::PitchEnv, "Pitch Env" },
                                                           { S::Lfo1, "LFO 1" }, { S::Lfo2, "LFO 2" }, { S::Lfo3, "LFO 3" } };
    for (int i = 0; i < kNumPages; ++i)
    {
        auto t = std::make_unique<ModSourceHandle> (p, tabList[i].first, tabList[i].second);
        t->setTab (true);
        t->setTooltip ("Click to edit " + juce::String (tabList[i].second) + ", drag onto a knob to modulate it");
        t->onClick = [this, i] { show (i); };
        addAndMakeVisible (*t);
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
    for (int e = 0; e < 3; ++e)
        for (int i = 0; i < 4; ++i)
            pages[(size_t) e].push_back (ks.add (*this, p, envIds[e][i]));

    for (auto [prm, name] : { std::pair { P::PenvAmt, "Amount" }, std::pair { P::PenvAttack, "Attack" },
                              std::pair { P::PenvDecay, "Decay" }, std::pair { P::PenvCurve, "Curve" } })
        pages[PitchEnv].push_back (ks.add (*this, p, prm, name));
    auto dest = std::make_unique<ParamCombo> (p, P::PenvDest, "Target");
    addAndMakeVisible (*dest);
    pages[PitchEnv].push_back (dest.get());
    owned.push_back (std::move (dest));

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
    envDisplay.setVisible (page <= ModEnv);
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
    if (current < Lfo1) return;
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
    const int used = current == PitchEnv ? 6 : (current >= Lfo1 ? 6 : 4);
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

    captions.clear();
    for (int e = 0; e <= ModEnv; ++e)
        for (int i = 0; i < 4; ++i)
            pages[(size_t) e][(size_t) i]->setBounds (gr.cell (i, 0));
    for (int i = 0; i < 4; ++i)
        pages[PitchEnv][(size_t) i]->setBounds (gr.cell (i, 0));
    pages[PitchEnv][4]->setBounds (gr.captioned (4, 0, 2));
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
    timerCallback();
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

void MacroPanel::timerCallback()
{
    bool changed = false;
    for (int i = 0; i < 4; ++i)
    {
        const auto n = proc.getMacroName (i);
        const bool d = proc.macroDrivesAnything (i);
        if (n != names[(size_t) i] || d != drives[(size_t) i])
        {
            names[(size_t) i] = n;
            drives[(size_t) i] = d;
            changed = true;
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
        const auto line = nameArea (i).translated (0, 24).withHeight (16);
        g.drawText (juce::String (juce::roundToInt (proc.plain ((P) ((int) P::Macro1 + i)) * 100.f)) + "%", line,
                    juce::Justification::centredRight, false);
    }
}

void MacroPanel::resized()
{
    for (int i = 0; i < 4; ++i)
    {
        const auto r = rowArea (i);
        ks.knobs[(size_t) i]->setBounds (r.getX(), r.getY() + 10, 46, 46);
        const auto line = nameArea (i).translated (0, 24).withHeight (16);
        handles[(size_t) i]->setBounds (line.withWidth (handles[(size_t) i]->idealWidth()));
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
                timerCallback();
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
      legatoGlide (p, P::GlideMode, "Legato glide")
{
    addAndMakeVisible (mode);
    addChildComponent (glideMode);
    addAndMakeVisible (legatoGlide);
    glideMode.setTooltip ("Glide into every note, or only between overlapping notes (303 style)");
    legatoGlide.setTooltip ("Glide only between overlapping notes (303 style); off glides into every note");
    knobs = { ks.add (*this, p, P::PolyVoices), ks.add (*this, p, P::Glide), ks.add (*this, p, P::Octave),
              ks.add (*this, p, P::Accent), ks.add (*this, p, P::VelSens) };
    extra = { ks.add (*this, p, P::BendRange) };
}

void VoicingPanel::resized()
{
    const bool tall = body().getHeight() >= 3 * kCellH + 2 * kRowGap;
    auto h = header();
    legatoGlide.setBounds (h.removeFromRight (legatoGlide.idealWidth()));
    legatoGlide.setVisible (! tall);
    const auto gr = grid();
    captions.clear();
    mode.setBounds (gr.control (0, 0, 3));
    addCaption ("Play Mode", gr.cell (0, 0, 3));
    knobs[0]->setBounds (gr.cell (3, 0));
    for (int i = 1; i < 5; ++i)
        knobs[(size_t) i]->setBounds (gr.cell (i - 1, 1));
    for (size_t i = 0; i < extra.size(); ++i)
    {
        extra[i]->setVisible (tall);
        extra[i]->setBounds (gr.cell ((int) i, 2));
    }
    glideMode.setVisible (tall);
    glideMode.setBounds (gr.control (1, 2, 3));
    if (tall) addCaption ("Glide", gr.cell (1, 2, 3));
}

// ---------------------------------------------------------------------------
// Length (energy sounds)
// ---------------------------------------------------------------------------
uint64_t SweepDisplay::fingerprint() const
{
    return hashParams ({ P::EnergyMode, P::EnergyBars, P::EnergySec, P::EnergyCurve, P::EnergyFade, P::EnergyCut })
         ^ (uint64_t) std::lround (proc.getBpm() * 100.0);
}

void SweepDisplay::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    const bool time = proc.plain (P::EnergyMode) > 0.5f;
    const double seconds = proc.energyLengthSeconds();
    const float bars = psy::energyBarsValue ((int) std::lround (proc.plain (P::EnergyBars)));
    const float ex = std::exp2 (2.5f * proc.plain (P::EnergyCurve));
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
        const float s = std::pow (t, ex);
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
    g.setColour (accent().withAlpha (0.14f));
    g.fillPath (level);
    g.setColour (accent());
    g.strokePath (level, juce::PathStrokeType (1.4f));
    g.setColour (col::text.withAlpha (0.5f));
    g.strokePath (sweepPath, juce::PathStrokeType (1.f));

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
      cut (p, P::EnergyCut, "Cut"), display (p)
{
    sweep.setTooltip ("The sweep runs 0 to 100 % over the length. Drag it onto any knob to move that knob with the sound");
    unit.setTooltip ("Length in bars (follows the tempo) or in seconds");
    unit.onPick = [this] (int i) { proc.setEnergyMode ((psy::EnergyMode) i); };
    cut.setStacked (true);
    cut.setTooltip ("Stop the note at the end of the length, however long it is held");
    for (juce::Component* c : { (juce::Component*) &sweep, (juce::Component*) &unit, (juce::Component*) &cut, (juce::Component*) &display })
        addAndMakeVisible (c);
    bars = ks.add (*this, p, P::EnergyBars);
    seconds = ks.add (*this, p, P::EnergySec);
    curve = ks.add (*this, p, P::EnergyCurve);
    fade = ks.add (*this, p, P::EnergyFade);
    lastTime = proc.plain (P::EnergyMode) > 0.5f;
    bars->setVisible (! lastTime);
    seconds->setVisible (lastTime);
    startTimerHz (10);
}

void LengthPanel::timerCallback()
{
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
    bars->setBounds (gr.cell (0, 0));
    seconds->setBounds (gr.cell (0, 0));
    curve->setBounds (gr.cell (1, 0));
    fade->setBounds (gr.cell (2, 0));
    cut.setBounds (gr.cell (3, 0));
    const auto row = gr.cell (0, 1, 4);
    display.setBounds (row.withX (body().getX()).withWidth (body().getWidth()));
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
