#pragma once

#include "Panels.h"

// The sections of the OSC page, laid out like Serum: oscillators, sub and
// noise, and the filters on top; macros, the modulators and the voicing
// below. Every knob that can be modulated takes a dragged mod source.

// ---------------------------------------------------------------------------
// The unison settings behind the oscillator's gear (Serum 2): how the voices
// are spaced, which are stacked an octave or a fifth away, how far Detune
// reaches, and how much their shape spreads. Shown in place of the waveform.
// ---------------------------------------------------------------------------
class UnisonSettings : public juce::Component, private juce::Timer
{
public:
    UnisonSettings (PsyBoxProcessor&, bool isB);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setType (int oscType);   // names the spread after what it moves

private:
    PsyBoxProcessor& proc;
    bool isB;
    ParamCombo mode, stack;
    KnobSet ks;
    ParamKnob *range, *spread;
    juce::Rectangle<int> linesArea;   // the voices as lines, Serum 2 style
    uint64_t lastPrint = 0;
    uint64_t print() const;
    void timerCallback() override;
};

// ---------------------------------------------------------------------------
// One oscillator, laid out like Serum 2's: the pitch fields over the
// waveform, then two rows of five cells.
//   row 0: the type's main knob, UNISON (gear: settings), DETUNE, BLEND, WIDTH
//   row 1: the type's other knobs (or the table), B's A>B FM and RING, LEVEL
// ---------------------------------------------------------------------------
class OscPanel : public Section, private juce::Timer
{
public:
    OscPanel (PsyBoxProcessor&, bool isB);
    void resized() override;

private:
    PsyBoxProcessor& proc;
    bool isB;
    ToggleDot onToggle;
    ParamCombo type;
    NumberBox semi, fine, unison;
    WaveDisplay wave;
    UnisonSettings settings;
    KnobSet ks;
    ParamKnob *shape, *pw, *wtpos, *ratio, *fm, *fb, *detune, *blend, *width, *level, *xmod = nullptr, *ring = nullptr;
    ParamCombo table;
    ToggleDot retrig;
    std::unique_ptr<ToggleDot> sync;
    int lastType = -1;
    bool lastOn = true, showSettings = false;
    void timerCallback() override;
};

class SubPanel : public Section
{
public:
    explicit SubPanel (PsyBoxProcessor&);
    void resized() override;

private:
    KnobSet ks;
    Segmented oct;
    ParamCombo shape;
};

class NoisePanel : public Section
{
public:
    explicit NoisePanel (PsyBoxProcessor&);
    void resized() override;

private:
    KnobSet ks;
    ToggleDot direct;   // noise skips the filters
    ParamCombo type;
};

// Response on the left, a 3 x 2 grid on the right.
class FilterPanel : public Section, private juce::Timer
{
public:
    FilterPanel (PsyBoxProcessor&, int slot);
    void resized() override;

private:
    PsyBoxProcessor& proc;
    int slot;
    ParamCombo type;
    FilterDisplay display;
    KnobSet ks;
    ParamKnob *cutoff, *reso, *drive, *env, *key, *vowel = nullptr;
    std::unique_ptr<Segmented> routing;
    int lastType = -1;
    void timerCallback() override;
};

// ---------------------------------------------------------------------------
// The modulators: a tab per envelope and LFO, each tab a drag handle. The
// selected one shows its curve and knobs; the strip at the bottom holds the
// other sources (velocity, note, wheel...).
// ---------------------------------------------------------------------------
class ModPanel : public Section, private juce::Timer
{
public:
    explicit ModPanel (PsyBoxProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void show (int page);

private:
    enum Page : int { AmpEnv, FilterEnv, ModEnv, PitchEnv, Lfo1, Lfo2, Lfo3, kNumPages };
    PsyBoxProcessor& proc;
    std::vector<std::unique_ptr<ModSourceHandle>> tabs, extras;
    EnvDisplay envDisplay;
    PitchEnvDisplay pitchDisplay;
    LfoDisplay lfoDisplay;
    KnobSet ks;
    std::array<std::vector<juce::Component*>, kNumPages> pages;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::array<ParamKnob*, 3> lfoRate {}, lfoDiv {};
    int current = 0;
    std::array<bool, 3> lastSync {};
    juce::Rectangle<int> stripCaption;
    void timerCallback() override;
};

// Four macros, one per row: knob, the name (automatic, or typed with a
// double-click) and a handle to drag the macro onto more knobs.
class MacroPanel : public Section, private juce::Timer
{
public:
    explicit MacroPanel (PsyBoxProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    PsyBoxProcessor& proc;
    KnobSet ks;
    std::vector<std::unique_ptr<ModSourceHandle>> handles;
    std::array<juce::String, 4> names;
    std::array<bool, 4> drives {};
    std::unique_ptr<juce::TextEditor> editor;
    juce::Rectangle<int> rowArea (int i) const;
    juce::Rectangle<int> nameArea (int i) const;
    void timerCallback() override;
};

class VoicingPanel : public Section
{
public:
    explicit VoicingPanel (PsyBoxProcessor&);
    void resized() override;

private:
    Segmented mode, glideMode;
    ParamCombo modeMenu;   // short layout: the mode as a menu, so Bend fits beside Voices
    ToggleDot legatoGlide;
    KnobSet ks;
    std::vector<ParamKnob*> knobs;
    ParamKnob* bend = nullptr;
};

// The energy sweep over the length: the level (fade) and the sweep curve,
// with bar lines in Bars mode, and where each sounding note is on it.
class SweepDisplay : public LiveDisplay
{
public:
    explicit SweepDisplay (PsyBoxProcessor& p) : LiveDisplay (p) {}

private:
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    uint64_t fingerprint() const override;
};

// Energy sounds: how long the gesture runs (bars by default, or seconds),
// its curve, how the level follows it, and whether the note stops at the end.
// The Sweep handle drags the gesture onto any knob.
class LengthPanel : public Section, private juce::Timer
{
public:
    explicit LengthPanel (PsyBoxProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PsyBoxProcessor& proc;
    ModSourceHandle sweep;
    Segmented unit;   // bars or seconds
    KnobSet ks;
    ParamKnob *bars, *seconds, *curve, *fade, *delay;
    ToggleDot cut;
    SweepDisplay display;
    bool lastTime = false;
    void timerCallback() override;
};

// Basses: the knock, the fast pitch drop at the start of every note (the
// pitch envelope, also on the modulators' Env 3 page).
class KnockPanel : public Section
{
public:
    explicit KnockPanel (PsyBoxProcessor&);
    void resized() override;

private:
    KnobSet ks;
    PitchEnvDisplay display;
};
