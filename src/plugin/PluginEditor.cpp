#include "PluginEditor.h"

using namespace theme;

namespace
{
    constexpr int kTopH = 52;
    constexpr int kSideW = 196;
    // Rows of the main grid: height, number of sections and their widths (each
    // row sums to 1084). Widths follow from content: padding + displays + whole
    // knob cells. Kick patches use their own rows 0-2, and row 3 (FX, macros)
    // is shared. The output scope and meter live in the top bar.
    struct RowSpec { int h; int n; int w[3]; };
    constexpr RowSpec kRows[4] = { { kRowH, 3, { 384, 384, 316 } },
                                   { kRowH, 3, { 366, 366, 352 } },
                                   { kRowH, 3, { 352, 360, 372 } },
                                   { kRowH, 2, { 948, 136, 0 } } };
    constexpr RowSpec kKickRows[4] = { { kRowH, 2, { 542, 542, 0 } },
                                       { kRowH, 2, { 542, 542, 0 } },
                                       { kRowH, 1, { 1084, 0, 0 } },
                                       { kRowH, 2, { 948, 136, 0 } } };
    static_assert (kTopH + 4 * kRowH == PsyBoxEditor::kH);
} // namespace

PsyBoxEditor::Root::Root (PsyBoxProcessor& p)
    : proc (p), top (p), side (p), oscA (p, false), oscB (p, true), mixer (p), f1 (p, 0), f2 (p, 1), voice (p), env (p),
      lfo (p), matrix (p), fx (p), macros (p), kickPitch (p), kickAmp (p), kickClick (p), kickDrive (p), kickWave (p)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &top, &side, &oscA, &oscB, &mixer, &f1, &f2, &voice,
                                                                         &env, &lfo, &matrix, &fx, &macros })
        addAndMakeVisible (c);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &kickPitch, &kickAmp, &kickClick, &kickDrive, &kickWave })
        addChildComponent (c);
    updateMode();
    startTimerHz (15);
}

void PsyBoxEditor::Root::updateMode()
{
    const bool kick = proc.plain (psy::P::EngineMode) > 0.5f;
    const bool arp = psy::hasArp (psy::familyOf (proc.getCategory()));
    if (arp != arpLayout)
    {
        arpLayout = arp;
        voice.setArpAllowed (arp);
    }
    if (kick == kickLayout) return;
    kickLayout = kick;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &oscA, &oscB, &mixer, &f1, &f2, &voice, &env, &lfo, &matrix })
        c->setVisible (! kick);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &kickPitch, &kickAmp, &kickClick, &kickDrive, &kickWave })
        c->setVisible (kick);
    // the macros drive mod-matrix targets, and the kick has none
    macros.setAlpha (kick ? 0.35f : 1.f);
    macros.setInterceptsMouseClicks (! kick, ! kick);
    resized();
    repaint();
}

void PsyBoxEditor::Root::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
}

void PsyBoxEditor::Root::paintOverChildren (juce::Graphics& g)
{
    // hairline grid between sections
    const auto& rows = kickLayout ? kKickRows : kRows;
    g.setColour (col::line);
    int y = kTopH;
    for (int r = 0; r < 4; ++r)
    {
        if (r > 0) g.fillRect (kSideW, y, kW - kSideW, 1);
        int x = kSideW;
        for (int c = 0; c < rows[r].n - 1; ++c)
        {
            x += rows[r].w[c];
            g.fillRect (x, y + kPad, 1, rows[r].h - 2 * kPad);
        }
        y += rows[r].h;
    }
}

void PsyBoxEditor::Root::resized()
{
    top.setBounds (0, 0, kW, kTopH);
    side.setBounds (0, kTopH, kSideW, kH - kTopH);

    juce::Component* synth[4][3] = { { &oscA, &oscB, &mixer }, { &f1, &f2, &voice }, { &env, &lfo, &matrix }, { &fx, &macros, nullptr } };
    juce::Component* kick[4][3] = { { &kickPitch, &kickAmp, nullptr }, { &kickClick, &kickDrive, nullptr },
                                    { &kickWave, nullptr, nullptr }, { &fx, &macros, nullptr } };
    const auto& rows = kickLayout ? kKickRows : kRows;
    auto& grid = kickLayout ? kick : synth;
    int y = kTopH;
    for (int r = 0; r < 4; ++r)
    {
        int x = kSideW;
        for (int c = 0; c < rows[r].n; ++c)
        {
            grid[r][c]->setBounds (x, y, rows[r].w[c], rows[r].h);
            x += rows[r].w[c];
        }
        y += rows[r].h;
    }
}

// ---------------------------------------------------------------------------

void PsyBoxEditor::Keys::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> r, bool down, bool over,
                                          juce::Colour, juce::Colour)
{
    const auto body = r.withTrimmedRight (1.f);   // 1 px gap between keys shows the background
    g.setColour (down ? accent() : (over ? juce::Colour (0xff232328) : juce::Colour (0xff1a1a1e)));
    g.fillRect (body);
    if (note % 12 == 0)
    {
        g.setColour (down ? col::bg : col::textFaint);
        g.setFont (mono (9.5f));
        g.drawText (getWhiteNoteText (note), body.withTrimmedBottom (4.f).toNearestInt(), juce::Justification::centredBottom, false);
    }
}

void PsyBoxEditor::Keys::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> r, bool down, bool over, juce::Colour)
{
    g.setColour (col::bg);
    g.fillRect (r.expanded (1.f, 0.f).withTrimmedTop (-1.f).withTrimmedBottom (-1.f));   // outline in the gap colour
    g.setColour (down ? accent() : (over ? juce::Colour (0xff141418) : juce::Colour (0xff0d0d10)));
    g.fillRect (r);
}

PsyBoxEditor::PsyBoxEditor (PsyBoxProcessor& p)
    : AudioProcessorEditor (p), proc (p), root (p),
      withKeyboard (forceKeyboard || p.wrapperType == juce::AudioProcessor::wrapperType_Standalone),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard), patternEditor (p)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setLookAndFeel (&lnf);
    addAndMakeVisible (root);
    root.setBounds (0, 0, kW, kH);
    addChildComponent (patternEditor);
    patternEditor.setBounds (0, 0, kW, kH);
    root.voicePanel().onEditPattern = [this] { patternEditor.open(); };

    if (withKeyboard)
    {
        // Click the keys, or play from the computer keyboard (A W S E D ... = C4 up).
        keyboard.setAvailableRange (36, 95);          // C2..B6: five octaves = 35 white keys
        keyboard.setKeyWidth ((float) kW / 35.f);     // exactly fills the window width
        keyboard.setScrollButtonsVisible (false);
        keyboard.setKeyPressBaseOctave (4);
        keyboard.setOctaveForMiddleC (4);
        keyboard.setVelocity (0.8f, true);
        keyboard.setWantsKeyboardFocus (true);
        keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, col::bg);
        keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, col::bg);   // base fill = gap colour
        keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (keyboard);
    }
    updateAccent();

    setResizable (true, true);
    setResizeLimits (kW * 6 / 10, totalH() * 6 / 10, kW * 3 / 2, totalH() * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) totalH());
    setSize (kW, totalH());
    proc.addChangeListener (this);
    if (withKeyboard)
    {
        std::function<void (juce::Component&)> noFocus = [&] (juce::Component& c) {
            c.setMouseClickGrabsKeyboardFocus (false);
            c.setWantsKeyboardFocus (false);
            for (auto* child : c.getChildren()) noFocus (*child);
        };
        noFocus (root);
        noFocus (patternEditor);
        startTimerHz (5);
    }
}

void PsyBoxEditor::timerCallback()
{
    if (! keyboard.isShowing() || keyboard.hasKeyboardFocus (false) || ! juce::Process::isForegroundProcess())
        return;
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    // leave focus alone while text is being typed (seed, macro names, preset name)
    if (dynamic_cast<juce::TextEditor*> (focused) != nullptr)
        return;
    if (focused == nullptr || focused == this || isParentOf (focused))
        keyboard.grabKeyboardFocus();
}

void PsyBoxEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
}

PsyBoxEditor::~PsyBoxEditor()
{
    proc.keyboardState.allNotesOff (0);
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void PsyBoxEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    root.setTransform (juce::AffineTransform::scale (s));
    patternEditor.setTransform (juce::AffineTransform::scale (s));
    if (withKeyboard)
    {
        // same logical layout as the panels: full width, 1 px hairline above
        keyboard.setBounds (0, kH + 1, kW, kKeysH - 1);
        keyboard.setTransform (juce::AffineTransform::scale (s));
    }
}

void PsyBoxEditor::updateAccent()
{
    const auto c = psy::categoryInfo()[(size_t) proc.getCategory()].colour;
    const auto accent = juce::Colour ((juce::uint32) (0xff000000 | c));
    if (accent != theme::accent() || withKeyboard)
    {
        theme::setAccent (accent);
        root.repaint();
        keyboard.repaint();
    }
}

void PsyBoxEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    root.updateMode();   // before the accent repaint, so a new kick never flashes the synth layout
    updateAccent();
}
