# PsyBox

A synthesizer for psytrance sound design. Pick a category, press **Generate**,
and you get a new sound of that kind. It could be an acid line, a zap, a rolling
bass, a riser or a kick. Every knob stays live, so a generated patch is a
starting point you can edit as far as you like.

Builds as **VST3**, **Audio Unit** and **Standalone** (macOS).

![PsyBox](docs/screenshot.png)

## Categories

32 kinds of sound in three families, each with its own recipe and accent
colour. The tabs at the top of the sidebar switch between **Synth**, **Bass**
and **Kick**; the list scrolls when it is longer than the space it has.

23 synth sounds, listed in sections by their role in a track:

- **Main**: Acid · Psy Lead · Squelch
- **Shots**: Pluck · Stab · Zap
- **FX**: Alien Voice · Bubble · Glitch · Gurgle · Laser · Metallic · Screech · Wobble
- **Energy**: Downlifter · Impact · Noise FX · Reverse · Riser · Uplifter
- **Background**: Atmos · Drone · Gate

Riser is a synth tone climbing in pitch; Uplifter is a noise sweep that opens
upward.

### Basses

Four bass styles, under the **Bass** tab:

Bass Shot · Offbeat Bass · Prog Bass · Psy Bass

Basses play on the synth engine, mono and dry, and have no arp: you play their
rhythm. Each recipe was fitted to reference samples (harmonics over time, pitch
knock, envelope, loudness, stereo):

- **Psy Bass**: the rolling 16th-note psy bass. A saw through two low-passes in
  series (one follows the key, one fixed near 250 Hz), a filter burst and a
  pitch knock on every 16th, full level for the note and a dead stop.
- **Prog Bass**: rounder and darker. One 12 dB low-pass fixed near 120 Hz
  whatever the key, a softer burst and knock.
- **Offbeat Bass**: one note between the kicks. A big pitch knock, then a
  low-pass that closes from a bright pluck to almost a sine, and a slow tail
  after the key is let go.
- **Bass Shot**: the progressive-trance one-shot. A saw over a body
  oscillator, a kick-like pitch drop, saturation and direct noise grit,
  multiband compression, and width above a mono low end.

A glue compressor with make-up gain puts the basses level with the kicks.

### Kicks

Five kick styles, under the **Kick** tab:

Dark Forest Kick · Full-On Kick · Goa Kick · Hi-Tech Kick · Progressive Kick

A kick category switches the synth to a dedicated kick engine, and the upper
sections of the window turn into the kick's Pitch, Amp, Click, Drive and wave
views. The effects and macros stay where they are. Kicks have no arp.

- **Tuned to your key.** With **Key** on (the default), the kick's body lands on
  the key you play. Any octave of the key gives the same kick, placed within
  half an octave of **Tune**. Turn Key off and every note plays at Tune. The
  **Tune to key** switch under Generate decides whether new kicks come with Key
  on; it only shows in kick categories.
- **Pitch** falls in two exponential stages: from **Start** to the **Knee** in
  about 3 × **Snap**, then into the body in about 3 × **Sweep**. Both stages land
  exactly on their target, so the body is in tune rather than slowly sagging.
- **Amp**: **Attack**, **Hold** and **Decay** shape the body. **Punch** keeps the
  first milliseconds at full level while the body fades in, which gives the
  knock of a progressive kick.
- **Click**: band-passed noise, a short tick or a falling blip, with level,
  decay and tone.
- **Drive**: Soft, Tube or Hard saturation on the body, oversampled with the
  rest of the engine, plus a tone low-pass.
- Every hit starts at the same phase, so repeated kicks are identical. A new hit
  fades the previous one out over 3 ms instead of cutting it. Drag the output
  window out to get the kick as a one-shot WAV.

The Progressive recipe is tuned against measured progressive kicks. The pitch
falls from about 2 kHz through a knee of about 330 Hz into a 38–53 Hz body. The
body holds flat to about 160 ms and is down 30 dB by about 220 ms.

## How it works

- **Generate** runs the category's recipe with a random seed. A recipe is a set
  of ranges and rules: which oscillator types, which filter, how the envelopes
  move, which LFOs and effects are routed where.
- **Mutate** nudges the current sound and stays inside the category's ranges.
  The amount slider sets how far.
- **Lock** a knob (right-click it, or alt-click) and Generate and Mutate leave
  it alone.
- Tick **Arp** next to Generate and every new synth sound comes with an arp in
  the style of its category: tied 303 slides for acid, random blips for
  bubbles, stepped sweeps for risers, each with a freshly generated step
  pattern. The sound itself is the same with or without it; only the arp
  settings differ. Basses and kicks have no arp.
- The same category and seed always give the same sound. Double-click the seed
  in the sidebar to type one in.
- Each category remembers its patch. Switch from Acid to Zap and back and your
  Acid sound is exactly as you left it. The first visit to a category generates
  a fresh sound.
- **Undo** and **Redo** in the top bar step through your Generate and Mutate
  history.
- Macros can be renamed: double-click the label or use the right-click menu.
- The **output window** in the top bar shows the scope and a stereo level
  meter: RMS bars over peak bars, a held peak tick and its value in dB, red
  when the output clips. Drag it out to drop the current sound into your DAW
  as a WAV.
- Presets are saved to `~/Music/PsyBox/Presets/<Category>/`.

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
over 1 to 4 octaves, with rate, gate, swing and hold, playing a 16-step pattern
with per-step accent and slide and a loop length of 1 to 16 steps. Generate
writes a new pattern in the category's style every time (acid lines get
accents and slides, bubbles loop at odd lengths). Click the pattern grid to open the step editor: paint steps, set the
length on the ruler, shift, clear, start from a template (Rolling, Gallop,
3-3-2, Acid line...) or generate just a new pattern. The arp locks to the host
transport while it plays. Slides, and gates of 100 % or more, tie: the voice
glides to the next note without retriggering, so 303 slides and long sweeps
keep running.

### Effects

Distortion (5 types), bitcrusher, Hilbert frequency shifter,
phaser, chorus / flanger, compressor, ping-pong delay, an 8-line FDN reverb
and a stereo width stage, in that order.

The compressor has two modes. **Normal** is a single-band, soft-knee compressor
with a parallel mix. **Multi** is a three-band upward + downward compressor in
the OTT / Serum style: drag each band's down and up thresholds and the two
band splits in the display, set the band gains, Depth, Down and Up amounts.
It is built to stay clean: Linkwitz-Riley bands that sum flat, a detector
that holds a whole waveform cycle (no distortion on low notes), stereo-linked
detection, Depth applied per band (no comb filtering against the band
filters), upward gain that fades out near silence, and click-free switching.

**Width** comes last, so it also cleans up delay and reverb. **Low Mono** folds
everything under the split to mono (a Linkwitz-Riley split on the side
channel only). **Width** scales the stereo above it, and **Stereoize** makes
stereo out of mono voices (a short Haas side from the mid). Left + right
never changes, so the mix stays mono-compatible.

The mixer's **Noise direct** switch adds the noise after the filters, for
bright grit over a dark sound.

## Building

Requirements: macOS 11+, CMake 3.24+, Ninja, Xcode command line tools.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This installs the VST3 and AU into `~/Library/Audio/Plug-Ins` and builds the
standalone app at `build/PsyBox_artefacts/Release/Standalone/PsyBox.app`.
JUCE 8 is downloaded automatically. To use a local checkout instead, pass
`-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`.

### Core only

The DSP, engine and generator don't depend on JUCE. They build and test in
seconds:

```sh
cmake -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSYBOX_BUILD_PLUGIN=OFF
cmake --build build-core
./build-core/psybox_tests                  # aliasing, filter response, stability, determinism
./build-core/psybox_render <out-dir> 3     # 3 WAVs per category
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
