#pragma once

#include "Panels.h"

// The Phoscyon engine's sections (EngineMode::Phoscyon), laid out like the
// original's front panel: the long row of 303 knobs with the couple LEDs over
// the pairs they tie together, the distortion, the calibration trims; drawn
// in PsyBox's style, with live displays of the filter and the envelopes.

// Two cycles of a -20 dBFS sine through a distortion model's preamp and
// clipper at `drive`, filling `area` (`dry` draws the clean sine instead).
// The Distortion section and the FX page's strip both show it.
juce::Path phosDriveWave (juce::Rectangle<float> area, int model, float drive, bool dry);

// The 303's resting filter and, while a note sounds, where the envelope and
// the accent sweep have it.
class PhosFilterDisplay : public LiveDisplay
{
public:
    explicit PhosFilterDisplay (PsyBoxProcessor& p) : LiveDisplay (p) {}
    float cutoffNow() const;    // where it sits now: the sounding note's, or at rest

private:
    float liveCutoff() const;   // 0 when silent
    float restingCutoff() const;
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    uint64_t fingerprint() const override;
};

// The main envelope (MEG: a normal note's decay, an accented note's dashed)
// or the VCA's, with a dot where the sounding note is.
class PhosEnvDisplay : public LiveDisplay
{
public:
    PhosEnvDisplay (PsyBoxProcessor& p, bool forVca) : LiveDisplay (p), vca (forVca) {}

private:
    bool vca;
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    uint64_t fingerprint() const override;
};

// The 303 row: Wave, Tuning, Cutoff, Resonance, Envmod, Decay, Accent, then
// Slide Time, Sweep Amt., Env. Attack, Acc. Decay, Acc. Vol., Vib. Speed and
// Vib. Depth. The LEDs over Resonance and Sweep Amt. (and over Accent and
// Acc. Vol.) are the couple switches: lit, the first knob drives the second.
// A line named Couple ties each pair, bright while it is coupled.
class PhosSynthPanel : public Section, private juce::Timer
{
public:
    explicit PhosSynthPanel (PsyBoxProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PsyBoxProcessor& proc;
    Segmented wave;
    KnobSet ks;
    std::vector<std::unique_ptr<ToggleDot>> leds;   // reso, sweep, accent, acc. vol.
    int splitX = 0;
    std::array<bool, 2> coupled {};
    void timerCallback() override;
};

class PhosFilterPanel : public Section, private juce::Timer
{
public:
    explicit PhosFilterPanel (PsyBoxProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PhosFilterDisplay display;
    juce::String shown;
    void timerCallback() override;
};

class PhosDistortionPanel : public Section
{
public:
    explicit PhosDistortionPanel (PsyBoxProcessor&);
    void resized() override;

private:
    ToggleDot power;
    ParamCombo model;
    std::unique_ptr<juce::Component> display;
    Segmented order;
    KnobSet ks;
};

// The trims and parts the original lets you calibrate, and its calibration
// presets (a pick sets all nine; "Custom" once one is moved).
class PhosCalibrationPanel : public Section, private juce::Timer
{
public:
    explicit PhosCalibrationPanel (PsyBoxProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PsyBoxProcessor& proc;
    KnobSet ks;
    ToggleDot clicks;
    juce::ComboBox preset;
    juce::Rectangle<int> note;
    void apply (int index);
    void refresh();   // selects the calibration the trims match; also run once by the constructor
    void timerCallback() override;
};
