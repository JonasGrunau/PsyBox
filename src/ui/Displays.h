#pragma once

#include "Theme.h"
#include "plugin/PluginProcessor.h"

#include <juce_dsp/juce_dsp.h>

// Displays poll their parameters at a low rate and only redraw when
// something they show has changed.

class DisplayBase : public juce::Component,
                    protected juce::Timer
{
public:
    explicit DisplayBase (PsySynthProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

protected:
    PsySynthProcessor& proc;
    virtual void paintContent (juce::Graphics&, juce::Rectangle<float>) = 0;
    virtual uint64_t fingerprint() const = 0;  // redraw when this changes
    void timerCallback() override;
    uint64_t lastPrint = 0;

    uint64_t hashParams (std::initializer_list<psy::P>) const;
    void drawGrid (juce::Graphics&, juce::Rectangle<float>, int vLines, int hLines) const;
};

// One oscillator's waveform, for any of the four types.
class WaveDisplay : public DisplayBase
{
public:
    WaveDisplay (PsySynthProcessor&, bool oscB);

private:
    bool isB;
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    uint64_t fingerprint() const override;
};

// Magnitude response of a filter slot, measured by running the real DSP
// filter on an impulse and taking its FFT (so what you see is what you hear).
class FilterDisplay : public DisplayBase
{
public:
    FilterDisplay (PsySynthProcessor&, int slot);

private:
    int slot;
    juce::dsp::FFT fft { 12 };
    std::vector<float> fftData;
    std::vector<float> magDb;
    void recompute();
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    uint64_t fingerprint() const override;
};

class EnvDisplay : public DisplayBase
{
public:
    explicit EnvDisplay (PsySynthProcessor& p) : DisplayBase (p) {}
    void setEnvelope (int index) { env = index; lastPrint = 0; repaint(); }

private:
    int env = 0;  // 0 amp, 1 filter, 2 mod
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    uint64_t fingerprint() const override;
};

class PitchEnvDisplay : public DisplayBase
{
public:
    explicit PitchEnvDisplay (PsySynthProcessor& p) : DisplayBase (p) {}

private:
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    uint64_t fingerprint() const override;
};

class LfoDisplay : public DisplayBase
{
public:
    explicit LfoDisplay (PsySynthProcessor& p) : DisplayBase (p) {}
    void setLfo (int index) { lfo = index; lastPrint = 0; repaint(); }

private:
    int lfo = 0;
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    uint64_t fingerprint() const override;
};

// Output oscilloscope with zero-crossing trigger, plus a peak meter.
// Click and drag out of it to drop the current sound as a WAV file.
class Scope : public DisplayBase, public juce::SettableTooltipClient
{
public:
    explicit Scope (PsySynthProcessor& p);
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; repaint(); }

private:
    std::vector<float> buf;
    float peakHold = 0.f;
    bool dragging = false;
    void timerCallback() override { repaint(); }
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    uint64_t fingerprint() const override { return 0; }
};
