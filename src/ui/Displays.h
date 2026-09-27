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
    explicit DisplayBase (PsyBoxProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

protected:
    PsyBoxProcessor& proc;
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
    WaveDisplay (PsyBoxProcessor&, bool oscB);

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
    FilterDisplay (PsyBoxProcessor&, int slot);

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
    explicit EnvDisplay (PsyBoxProcessor& p) : DisplayBase (p) {}
    void setEnvelope (int index) { env = index; lastPrint = 0; repaint(); }

private:
    int env = 0;  // 0 amp, 1 filter, 2 mod
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    uint64_t fingerprint() const override;
};

class PitchEnvDisplay : public DisplayBase
{
public:
    explicit PitchEnvDisplay (PsyBoxProcessor& p) : DisplayBase (p) {}

private:
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    uint64_t fingerprint() const override;
};

class LfoDisplay : public DisplayBase
{
public:
    explicit LfoDisplay (PsyBoxProcessor& p) : DisplayBase (p) {}
    void setLfo (int index) { lfo = index; lastPrint = 0; repaint(); }

private:
    int lfo = 0;
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    uint64_t fingerprint() const override;
};

// ---------------------------------------------------------------------------
// Kick displays. They read the patch the way the kick engine does, and the
// envelope, click and wave views render a real hit offline, for the key last
// played (key tracking moves the whole sweep).
// ---------------------------------------------------------------------------
class KickDisplay : public DisplayBase
{
public:
    explicit KickDisplay (PsyBoxProcessor& p) : DisplayBase (p) {}

protected:
    psy::ParamState patch() const;
    int note() const { return proc.getLastPlayedNote(); }
    uint64_t fingerprint() const override;
};

// Pitch over time on a log-frequency axis: start, knee and the tuned body.
class KickPitchDisplay : public KickDisplay
{
public:
    using KickDisplay::KickDisplay;

private:
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
};

// Amp envelope of one hit: punch, attack, hold, decay.
class KickEnvDisplay : public KickDisplay
{
public:
    using KickDisplay::KickDisplay;

private:
    std::vector<float> env;
    uint64_t cached = 0;
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
};

// The click layer on its own, over its first milliseconds.
class KickClickDisplay : public KickDisplay
{
public:
    using KickDisplay::KickDisplay;

private:
    std::vector<float> click;
    uint64_t cached = 0;
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
};

// The body saturator's transfer curve at the current drive.
class KickDriveDisplay : public KickDisplay
{
public:
    using KickDisplay::KickDisplay;

private:
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
};

// The whole hit as a waveform with a time ruler and the pitch sweep on top.
class KickWaveDisplay : public KickDisplay
{
public:
    using KickDisplay::KickDisplay;
    float lengthMs() const { ensure(); return length; }   // to -60 dB below the body

private:
    mutable std::vector<float> wave, hz;
    mutable uint64_t cached = 0;
    mutable float length = 0.f, totalMs = 0.f;
    void ensure() const;   // re-renders the hit when the patch or key changed
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
};

// Output window: oscilloscope with zero-crossing trigger, plus a stereo level
// meter (RMS bar over a peak bar, held peak tick and readout, red on clip).
// Click and drag out of it to drop the current sound as a WAV file.
class Scope : public DisplayBase, public juce::SettableTooltipClient
{
public:
    explicit Scope (PsyBoxProcessor& p);
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; repaint(); }

private:
    struct Meter { float peakDb = -100.f, rmsDb = -100.f, holdDb = -100.f; int holdFrames = 0, clipFrames = 0; };
    std::vector<float> buf;
    std::array<Meter, 2> meters;
    bool dragging = false;
    void timerCallback() override;
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    void paintWave (juce::Graphics&, juce::Rectangle<float>);
    void paintMeter (juce::Graphics&, juce::Rectangle<float>);
    uint64_t fingerprint() const override { return 0; }
};

// Compressor. Normal mode shows the transfer curve, Multiband shows the three
// bands on a log-frequency axis with their thresholds and level meters. Drag a
// threshold (or, in Multiband, a band split) to set it; double-click resets
// the one under the mouse.
class CompDisplay : public DisplayBase
{
public:
    explicit CompDisplay (PsyBoxProcessor& p) : DisplayBase (p) {}
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    // What a mouse position grabs: a parameter, and for thresholds the
    // partner it pushes along (down and up threshold of the same band).
    struct Target { psy::P param = psy::P::Count, partner = psy::P::Count; bool isDown = false; };
    Target hover, drag;
    float grabOffset = 0.f;

    bool multi() const { return proc.plain (psy::P::CompMode) > 0.5f; }
    juce::Rectangle<float> plot() const;
    float xOfHz (float hz) const;
    float hzOfX (float x) const;
    float yOfDb (float db) const;
    float dbOfY (float y) const;
    float xOfDb (float db) const;   // Normal mode input axis
    float dbOfX (float x) const;
    int bandAt (float x) const;
    Target targetAt (juce::Point<float>) const;
    void setPlain (psy::P, float plain);

    void timerCallback() override;
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    void paintMulti (juce::Graphics&, juce::Rectangle<float>, bool on);
    void paintNormal (juce::Graphics&, juce::Rectangle<float>, bool on);
    uint64_t fingerprint() const override;
};
