# PsySynth

A synthesizer for psytrance sound design. Pick a category, press **Generate**,
and you get a new sound of that kind. It could be an acid line, a zap, a rolling
bass or a riser. Every knob stays live, so a generated patch is a starting point
you can edit as far as you like.

Builds as **VST3**, **Audio Unit** and **Standalone** (macOS).

![PsySynth](docs/screenshot.png)

## Categories

19 kinds of sound, each with its own recipe and accent colour:

Acid · Alien Voice · Atmos · Bubble · Downlifter · Drone · Gurgle · Laser ·
Metallic · Noise FX · Pluck · Psy Lead · Riser · Rolling Bass · Screech ·
Squelch · Stab · Wobble · Zap

No drums. This is for the synth side of a track.

## How it works

- **Generate** runs the category's recipe with a random seed. A recipe is a set
  of ranges and rules: which oscillator types, which filter, how the envelopes
  move, which LFOs and effects are routed where.
- **Mutate** nudges the current sound and stays inside the category's ranges.
  The amount slider sets how far.
- **Lock** a knob (right-click it, or alt-click) and Generate and Mutate leave
  it alone.
- Tick **Arp** next to Generate and every new sound comes with an arp in the
  style of its category: rolling 16ths for basses, tied 303 slides for acid,
  random blips for bubbles, stepped sweeps for risers. The sound itself is the
  same with or without it; only the arp settings differ.
- The same category and seed always give the same sound. Double-click the seed
  in the sidebar to type one in.
- Each category remembers its patch. Switch from Acid to Zap and back and your
  Acid sound is exactly as you left it. The first visit to a category generates
  a fresh sound.
- **Undo** and **Redo** in the top bar step through your Generate and Mutate
  history.
- Macros can be renamed: double-click the label or use the right-click menu.
- Presets are saved to `~/Music/PsySynth/Presets/<Category>/`.

The standalone app has a keyboard strip at the bottom. Click the keys or play
from the computer keyboard (`A W S E D F T G Y H U J K` = C4 up, `Z`/`X` shift
the octave). A MIDI controller works too: *Options → Audio/MIDI Settings*.

## Sound engine

### Oscillators

Two per voice, plus sub, noise and ring mod.

- Analog: continuous saw / pulse / triangle morph with 4-point polyBLEP and
  polyBLAMP, PWM and hard sync with band-limited corrections
- Supersaw: 7 voices with the JP-8000 detune and mix curves
- Wavetable: 8 procedural tables, mip-mapped per octave from FFT spectra
- FM: 2-operator phase modulation with feedback, plus osc A → B FM

### Filters

Two slots, serial or parallel.

- Low-pass and high-pass at 6, 12, 18 and 24 dB/oct (TPT state-variable
  sections with Butterworth tunings)
- Band-pass 12 / 24, notch
- Transistor ladder in LP 12, LP 24, HP and BP modes, with the saturating
  feedback solved by Newton-Raphson each sample
- Diode ladder (303 style), calibrated so the resonant peak sits on the cutoff
- Formant (vowel morph) and positive/negative comb

### Voices and modulation

16-voice poly, mono and legato with glide, 303-style accent and
slide. Voices run at 1x, 2x or 4x and come back through linear-phase half-band
FIR decimators. Drive happens before decimation and uses antiderivative
anti-aliasing.

Amp, filter, mod and pitch envelopes, 3 LFOs (tempo sync, retrigger, S&H,
smooth random) and an 8-slot mod matrix.

Arpeggiator (Voice panel, Arp tab): Up, Down, Up/Down, Random and played order
over 1 to 4 octaves, with rate, gate, swing and hold. Rhythm patterns built for
psy: Straight, Rolling (`-xxx`, the kick keeps the beat), Gallop, Offbeat, Skip,
3-3-2 and a one-bar Broken line. It locks to the host transport while it plays.
Gates of 100 % or more tie: the voice slides to the next note without
retriggering, so 303 slides and long sweeps keep running.

### Effects

Distortion (5 types), bitcrusher, Hilbert frequency shifter,
phaser, chorus / flanger, ping-pong delay and an 8-line FDN reverb.

## Building

Requirements: macOS 11+, CMake 3.24+, Ninja, Xcode command line tools.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This installs the VST3 and AU into `~/Library/Audio/Plug-Ins` and builds the
standalone app at `build/PsySynth_artefacts/Release/Standalone/PsySynth.app`.
JUCE 8 is downloaded automatically. To use a local checkout instead, pass
`-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`.

### Core only

The DSP, engine and generator don't depend on JUCE. They build and test in
seconds:

```sh
cmake -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSYSYNTH_BUILD_PLUGIN=OFF
cmake --build build-core
./build-core/psysynth_tests                  # aliasing, filter response, stability, determinism
./build-core/psysynth_render <out-dir> 3     # 3 WAVs per category
```

## Project layout

```
src/core/dsp        oscillators, filters, shapers, oversampling, effects
src/core/engine     parameters, voices, engine
src/core/generator  categories, recipes, generate / mutate
src/plugin          JUCE processor and editor
src/ui              theme, widgets, panels, displays
tests/              Catch2 tests for the core
tools/              WAV renderer, offscreen UI snapshot tool
```

## License

The plugin links JUCE 8 under the AGPLv3.
