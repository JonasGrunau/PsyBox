#include "KickPanels.h"

#include "engine/KickVoice.h"

using namespace theme;
using psy::P;

namespace
{
    juce::String noteName (float hz)
    {
        const float midi = 69.f + 12.f * std::log2 (hz / 440.f);
        const int n = juce::roundToInt (midi);
        const int cents = juce::roundToInt ((midi - (float) n) * 100.f);
        auto s = juce::MidiMessage::getMidiNoteName (n, true, true, 4);   // middle C = C4, so 29 -> F1
        if (cents != 0) s << (cents > 0 ? " +" : " ") << cents << "c";
        return s;
    }

    // Display on the left, a grid of `cols` cells on the right of the body.
    int gridLeft (juce::Rectangle<int> body, int cols) { return body.getRight() - cellsW (cols); }
} // namespace

// ---------------------------------------------------------------------------
KickPitchPanel::KickPitchPanel (PsyBoxProcessor& p)
    : Section ("Pitch"), proc (p), display (p), keyTrack (p, P::KickKeyTrack, "Key")
{
    addAndMakeVisible (display);
    keyTrack.setStacked (true);
    keyTrack.setTooltip ("The body lands on the key you play (within half an octave of Tune)");
    addAndMakeVisible (keyTrack);
    ks.add (*this, p, P::KickTune);
    ks.add (*this, p, P::KickStart);
    ks.add (*this, p, P::KickKnee);
    ks.add (*this, p, P::KickSnap);
    ks.add (*this, p, P::KickSweep);
    startTimerHz (10);
}

void KickPitchPanel::timerCallback()
{
    if (! isShowing()) return;
    const auto s = psy::KickSettings::from ([this] {
        psy::ParamState st;
        for (int i = (int) P::KickTune; i <= (int) P::KickTone; ++i) st.v[(size_t) i] = proc.plain ((P) i);
        return st;
    }());
    const float hz = psy::kickBodyHz (s, proc.getLastPlayedNote());
    const auto text = noteName (hz) + "  " + juce::String (hz, hz < 100.f ? 1 : 0) + " Hz" + (s.keyTrack ? "" : "  FIXED");
    if (text != readout)
    {
        readout = text;
        repaint (header());
    }
}

void KickPitchPanel::paint (juce::Graphics& g)
{
    Section::paint (g);
    g.setColour (accent (*this));
    g.setFont (mono (10.f));
    g.drawText (readout, header(), juce::Justification::centredRight, false);
}

void KickPitchPanel::resized()
{
    const auto b = body();
    const int gx = gridLeft (b, 3);
    display.setBounds (b.withRight (gx - sp::lg));
    const auto gr = grid (gx);
    // top: where it lands; bottom: how it gets there
    ks.knobs[0]->setBounds (gr.cell (0, 0));   // tune
    ks.knobs[1]->setBounds (gr.cell (1, 0));   // start
    ks.knobs[2]->setBounds (gr.cell (2, 0));   // knee
    keyTrack.setBounds (gr.cell (0, 1));
    ks.knobs[3]->setBounds (gr.cell (1, 1));   // snap
    ks.knobs[4]->setBounds (gr.cell (2, 1));   // sweep
}

// ---------------------------------------------------------------------------
KickAmpPanel::KickAmpPanel (PsyBoxProcessor& p) : Section ("Amp"), display (p)
{
    addAndMakeVisible (display);
    ks.add (*this, p, P::KickAttack);
    ks.add (*this, p, P::KickHold);
    ks.add (*this, p, P::KickDecay);
    ks.add (*this, p, P::KickPunch);
    ks.add (*this, p, P::KickPunchLen, "Punch Len");
    ks.add (*this, p, P::VelSens);
}

void KickAmpPanel::resized()
{
    const auto b = body();
    const int gx = gridLeft (b, 3);
    display.setBounds (b.withRight (gx - sp::lg));
    const auto gr = grid (gx);
    for (int i = 0; i < (int) ks.knobs.size(); ++i)
        ks.knobs[(size_t) i]->setBounds (gr.cell (i % 3, i / 3));
}

// ---------------------------------------------------------------------------
KickClickPanel::KickClickPanel (PsyBoxProcessor& p) : Section ("Click"), type (p, P::KickClickType), display (p)
{
    addAndMakeVisible (type);
    type.setTooltip ("Noise: band-passed noise. Tick: a short knock. Blip: a falling sine");
    addAndMakeVisible (display);
    ks.add (*this, p, P::KickClickLevel);
    ks.add (*this, p, P::KickClickDecay);
    ks.add (*this, p, P::KickClickTone);
}

void KickClickPanel::resized()
{
    auto h = header();
    type.setBounds (h.removeFromRight (cellsW (3)).withSizeKeepingCentre (cellsW (3), kCtrlH));
    const auto b = body();
    const int gx = gridLeft (b, 2);
    display.setBounds (b.withRight (gx - sp::lg));
    const auto gr = grid (gx);
    ks.knobs[0]->setBounds (gr.cell (0, 0));
    ks.knobs[1]->setBounds (gr.cell (1, 0));
    ks.knobs[2]->setBounds (gr.cell (0, 1));
}

// ---------------------------------------------------------------------------
KickDrivePanel::KickDrivePanel (PsyBoxProcessor& p) : Section ("Drive"), type (p, P::KickDriveType), display (p)
{
    addAndMakeVisible (type);
    type.setTooltip ("Saturation on the body: Tube adds even harmonics, Soft and Hard odd ones");
    addAndMakeVisible (display);
    ks.add (*this, p, P::KickDrive);
    ks.add (*this, p, P::KickTone);
}

void KickDrivePanel::resized()
{
    auto h = header();
    type.setBounds (h.removeFromRight (cellsW (3)).withSizeKeepingCentre (cellsW (3), kCtrlH));
    const auto b = body();
    const int gx = gridLeft (b, 2);
    display.setBounds (b.withRight (gx - sp::lg));
    const auto gr = grid (gx);
    ks.knobs[0]->setBounds (gr.cell (0, 0));
    ks.knobs[1]->setBounds (gr.cell (1, 0));
}

// ---------------------------------------------------------------------------
KickWavePanel::KickWavePanel (PsyBoxProcessor& p) : Section ("Kick"), display (p)
{
    addAndMakeVisible (display);
    startTimerHz (8);
}

void KickWavePanel::timerCallback()
{
    if (! isShowing()) return;   // lengthMs() renders a whole kick whenever the patch or key changed
    if (std::abs (display.lengthMs() - shownLength) > 0.5f)
    {
        shownLength = display.lengthMs();
        repaint (header());
    }
}

void KickWavePanel::paint (juce::Graphics& g)
{
    Section::paint (g);
    if (shownLength <= 0.f) return;
    g.setColour (col::textFaint);
    g.setFont (mono (10.f));
    g.drawText ("LENGTH " + juce::String (juce::roundToInt (shownLength)) + " MS", header(), juce::Justification::centredRight, false);
}

void KickWavePanel::resized()
{
    display.setBounds (body());
}
