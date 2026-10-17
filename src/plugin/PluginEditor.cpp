#include "PluginEditor.h"

using namespace theme;

PsyBoxEditor::Root::Root (PsyBoxProcessor& p)
    : proc (p), top (p), side (p), osc (p), kick (p), phos (p), fx (p), phosFx (p, true), matrix (p), global (p), arp (p)
{
    addAndMakeVisible (top);
    addAndMakeVisible (side);
    for (juce::Component* c : pages())
        addChildComponent (c);
    top.views.onChange = [this] (View v) { showView (v); };
    updateMode();
    showView (View::Main);
    startTimerHz (15);
}

std::vector<View> PsyBoxEditor::Root::available() const
{
    if (kickLayout) return { View::Main, View::Fx, View::Global };
    if (proc.categoryHasArp()) return { View::Main, View::Fx, View::Matrix, View::Arp, View::Global };
    return { View::Main, View::Fx, View::Matrix, View::Global };
}

std::vector<juce::Component*> PsyBoxEditor::Root::pages()
{
    return { &osc, &kick, &phos, &fx, &phosFx, &matrix, &global, &arp };
}

juce::Component* PsyBoxEditor::Root::pageFor (View v) const
{
    switch (v)
    {
        case View::Main:   return kickLayout ? (juce::Component*) &kick : phosLayout ? (juce::Component*) &phos : (juce::Component*) &osc;
        case View::Fx:     return phosLayout ? (juce::Component*) &phosFx : (juce::Component*) &fx;
        case View::Matrix: return (juce::Component*) &matrix;
        case View::Arp:    return (juce::Component*) &arp;
        case View::Global: return (juce::Component*) &global;
        case View::Count:  break;
    }
    return nullptr;
}

void PsyBoxEditor::Root::showView (View v)
{
    const auto views = available();
    if (std::find (views.begin(), views.end(), v) == views.end())
        v = View::Main;   // e.g. the arp page, after switching to a bass
    view = v;
    auto* page = pageFor (v);
    for (juce::Component* c : pages())
        c->setVisible (c == page);
    top.views.setSelected (v);
}

void PsyBoxEditor::Root::updateMode()
{
    // 0 melodic synth (arp page), 1 energy and reverse basses (length), 2 bass, 3 kick, 4 Phoscyon
    const int engine = juce::roundToInt (proc.plain (psy::P::EngineMode));
    const bool kickMode = engine == (int) psy::EngineMode::Kick, phosMode = engine == (int) psy::EngineMode::Phoscyon;
    const int mode = kickMode ? 3 : phosMode ? 4 : (proc.categoryHasArp() ? 0 : (proc.hasLength() ? 1 : 2));
    if (mode == lastMode) return;
    lastMode = mode;
    kickLayout = kickMode;
    phosLayout = phosMode;
    osc.setExtra (mode == 0 || mode == 4 ? OscPage::Extra::None : (mode == 1 ? OscPage::Extra::Length : OscPage::Extra::Knock));
    top.views.setViews (available(), kickLayout ? "Kick" : phosLayout ? "Phoscyon" : "Osc");
    top.resized();
    showView (view);
    repaint();
}

void PsyBoxEditor::Root::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
}

void PsyBoxEditor::Root::resized()
{
    top.setBounds (0, 0, kW, kTopH);
    side.setBounds (0, kTopH, kSideW, kH - kTopH);
    const juce::Rectangle<int> area (kSideW, kTopH, kW - kSideW, kH - kTopH);
    for (juce::Component* c : pages())
        c->setBounds (area);
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
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    ++openEditors;
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf.get());
    setLookAndFeel (&lnf.get());
    addAndMakeVisible (root);
    root.setBounds (0, 0, kW, kH);

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
    if (withKeyboard)
        proc.keyboardState.allNotesOff (0);   // the on-screen keys let go; notes the host holds play on
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
    if (--openEditors == 0)
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void PsyBoxEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    root.setTransform (juce::AffineTransform::scale (s));
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

void PsyBoxEditor::dragOperationStarted (const juce::DragAndDropTarget::SourceDetails& d)
{
    proc.uiDragSource = moddrag::sourceOf (d.description);   // knobs that take it show a ring
}

void PsyBoxEditor::dragOperationEnded (const juce::DragAndDropTarget::SourceDetails&)
{
    proc.uiDragSource = psy::ModSrc::None;
}

void PsyBoxEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    root.updateMode();   // before the accent repaint, so a new kick never flashes the synth layout
    updateAccent();
}
