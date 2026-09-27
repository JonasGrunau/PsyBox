#include "PluginEditor.h"

using namespace theme;

namespace
{
    constexpr int kTopH = 52;
    constexpr int kSideW = 196;
    // Rows of the main grid: height and column widths (each row sums to 1084).
    struct RowSpec { int h; int w[3]; };
    constexpr RowSpec kRows[4] = { { 190, { 380, 380, 324 } },
                                   { 190, { 400, 340, 344 } },
                                   { 200, { 400, 330, 354 } },
                                   { 188, { 720, 150, 214 } } };
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
            g.fillRect (x, y + 12, 1, kRows[r].h - 24);
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

PsySynthEditor::PsySynthEditor (PsySynthProcessor& p)
    : AudioProcessorEditor (p), proc (p), root (p)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setLookAndFeel (&lnf);
    updateAccent();
    addAndMakeVisible (root);
    root.setBounds (0, 0, kW, kH);

    setResizable (true, true);
    setResizeLimits (kW * 6 / 10, kH * 6 / 10, kW * 3 / 2, kH * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
    setSize (kW, kH);
    proc.addChangeListener (this);
}

PsySynthEditor::~PsySynthEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void PsySynthEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    root.setTransform (juce::AffineTransform::scale (s));
}

void PsySynthEditor::updateAccent()
{
    const auto c = psy::categoryInfo()[(size_t) proc.getCategory()].colour;
    const auto accent = juce::Colour ((juce::uint32) (0xff000000 | c));
    if (accent != theme::accent())
    {
        theme::setAccent (accent);
        root.repaint();
    }
}

void PsySynthEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateAccent();
}
