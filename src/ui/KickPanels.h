#pragma once

#include "Panels.h"

// The kick engine's sections. The editor shows them in place of the
// oscillator, mixer, filter, envelope, LFO and matrix sections whenever the
// patch is a kick (EngineMode::Kick).

// Pitch sweep: Tune (the body), key tracking, start, snap, knee, sweep.
// The header shows the note the body lands on for the key last played.
class KickPitchPanel : public Section, private juce::Timer
{
public:
    explicit KickPitchPanel (PsySynthProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PsySynthProcessor& proc;
    KickPitchDisplay display;
    KnobSet ks;
    ToggleDot keyTrack;
    juce::String readout;
    void timerCallback() override;
};

class KickAmpPanel : public Section
{
public:
    explicit KickAmpPanel (PsySynthProcessor&);
    void resized() override;

private:
    KickEnvDisplay display;
    KnobSet ks;
};

class KickClickPanel : public Section
{
public:
    explicit KickClickPanel (PsySynthProcessor&);
    void resized() override;

private:
    Segmented type;
    KickClickDisplay display;
    KnobSet ks;
};

class KickDrivePanel : public Section
{
public:
    explicit KickDrivePanel (PsySynthProcessor&);
    void resized() override;

private:
    Segmented type;
    KickDriveDisplay display;
    KnobSet ks;
};

// The whole hit, full width, with its length in the header.
class KickWavePanel : public Section, private juce::Timer
{
public:
    explicit KickWavePanel (PsySynthProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    KickWaveDisplay display;
    float shownLength = -1.f;
    void timerCallback() override;
};
