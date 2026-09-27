#include "PluginEditor.h"

using namespace theme;

namespace
{
    constexpr int kTopH = 52;
    constexpr int kSideW = 196;
    // Rows of the main grid: height and column widths (each row sums to 1084).
    // Widths follow from content: padding + displays + whole knob cells.
    struct RowSpec { int h; int w[3]; };
    constexpr RowSpec kRows[4] = { { kRowH, { 384, 384, 316 } },
                                   { kRowH, { 366, 366, 352 } },
                                   { kRowH, { 352, 360, 372 } },
                                   { kRowH, { 568, 136, 380 } } };
    static_assert (kTopH + 4 * kRowH == PsySynthEditor::kH);
} // namespace

PsySynthEditor::Root::Root (PsySynthProcessor& p)
    : top (p), side (p), oscA (p, false), oscB (p, true), mixer (p), f1 (p, 0), f2 (p, 1), voice (p), env (p),
      lfo (p), matrix (p), fx (p), macros (p), output (p)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &top, &side, &oscA, &oscB, &mixer, &f1, &f2, &voice,
                                                                         &env, &lfo, &matrix, &fx, &macros, &output })
        addAndMakeVisible (c);
}

void PsySynthEditor::Root::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
}

void PsySynthEditor::Root::paintOverChildren (juce::Graphics& g)
{
    // hairline grid between sections
    g.setColour (col::line);
    int y = kTopH;
    for (int r = 0; r < 4; ++r)
    {
        if (r > 0) g.fillRect (kSideW, y, kW - kSideW, 1);
        int x = kSideW;
        for (int c = 0; c < 2; ++c)
        {
            x += kRows[r].w[c];
            g.fillRect (x, y + kPad, 1, kRows[r].h - 2 * kPad);
        }
        y += kRows[r].h;
    }
}

void PsySynthEditor::Root::resized()
{
    top.setBounds (0, 0, kW, kTopH);
    side.setBounds (0, kTopH, kSideW, kH - kTopH);

    juce::Component* grid[4][3] = { { &oscA, &oscB, &mixer }, { &f1, &f2, &voice }, { &env, &lfo, &matrix }, { &fx, &macros, &output } };
    int y = kTopH;
    for (int r = 0; r < 4; ++r)
    {
        int x = kSideW;
        for (int c = 0; c < 3; ++c)
        {
            grid[r][c]->setBounds (x, y, kRows[r].w[c], kRows[r].h);
            x += kRows[r].w[c];
        }
        y += kRows[r].h;
    }
}

// ---------------------------------------------------------------------------

void PsySynthEditor::Keys::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> r, bool down, bool over,
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

void PsySynthEditor::Keys::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> r, bool down, bool over, juce::Colour)
{
    g.setColour (col::bg);
    g.fillRect (r.expanded (1.f, 0.f).withTrimmedTop (-1.f).withTrimmedBottom (-1.f));   // outline in the gap colour
    g.setColour (down ? accent() : (over ? juce::Colour (0xff141418) : juce::Colour (0xff0d0d10)));
    g.fillRect (r);
}

PsySynthEditor::PsySynthEditor (PsySynthProcessor& p)
    : AudioProcessorEditor (p), proc (p), root (p),
      withKeyboard (forceKeyboard || p.wrapperType == juce::AudioProcessor::wrapperType_Standalone),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setLookAndFeel (&lnf);
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

void PsySynthEditor::timerCallback()
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

void PsySynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
}

PsySynthEditor::~PsySynthEditor()
{
    proc.keyboardState.allNotesOff (0);
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void PsySynthEditor::resized()
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

void PsySynthEditor::updateAccent()
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

void PsySynthEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateAccent();
}
