# PsyBox

A synthesizer for psytrance sound design. Pick a category, press **Generate**,
and you get a new sound of that kind. It could be an acid line, a zap, a rolling
bass, a riser or a kick. Every knob stays live, so a generated patch is a
starting point you can edit as far as you like.

Builds as **VST3**, **Audio Unit** and **Standalone** (macOS).

![PsyBox](docs/screenshot.png)

## Categories

32 kinds of sound, each with its own recipe, in six groups in the sidebar.
Each group is a header; click one to open it and its sounds fold out under
it, while closed groups show a dot per sound. The accents run as one rainbow
down the whole list: red at the first sound of the first group, through
every group, to magenta at the last kick.

- **Main**: Acid · Pluck · Psy Lead · Stab
- **FX**: Alien Voice · Bubble · Glitch · Gurgle · Laser · Metallic · Screech · Squelch · Wobble · Zap
- **Energy**: Downlifter · Impact · Noise FX · Reverse · Riser · Uplifter
- **Background**: Atmos · Drone · Gate
- **Bass** and **Kick**: below

### Energy

Risers, downlifters, impacts and the rest are one gesture over a **Length**:
in bars by default (it follows the tempo), or in seconds. Switch between
**Bars** and **Time** and the length keeps sounding the same. Energy sounds
have no arp; their Length panel sits under the voicing.

The length drives the **Sweep**, a mod source that climbs from 0 to 100 % from
the note start. The recipes hang pitch, filter and level on it, so changing the
length stretches the whole riser. **Curve** bends the sweep (slow start and a
fast finish, or the other way round), **Fade** makes the level follow it (in
for risers, out for downlifters and impacts), and **Cut** stops the note at the
end however long it is held (Reverse). Drag the Sweep onto any knob to move
that knob with the gesture too. Riser is a synth tone climbing in pitch;
Uplifter is a noise sweep that opens upward.

### Basses

Four bass styles, under the **Bass** tab:

Bass Shot · Offbeat Bass · Prog Bass · Psy Bass

Basses play on the synth engine, mono and dry, and have no arp: you play their
rhythm. Their **Knock** panel (under Voicing) holds the pitch drop at the start
of every note. Each recipe was fitted to reference samples (harmonics over time, pitch
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

A kick category switches the synth to a dedicated kick engine, and the OSC
view becomes the KICK view: the kick's Pitch, Amp, Click, Drive and wave
sections. The FX and Global views stay. Kicks have no arp, macros or matrix.

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
- **Every patch plays about as loud.** After Generate, Mutate or a new arp
  pattern, the patch is played offline the ways it is usually played (a held
  key and 16ths from C2 to C5, the arp, a riser over its length, kicks four to
  the floor) and its loudest moment is measured (K-weighted, 400 ms, like
  LUFS). **Level** (Global view, next to Volume) is set so that moment lands on
  -16 LUFS, so a new patch never jumps out louder than the last. Volume stays
  yours; lock Level to keep your own.
- **Lock** a knob (right-click it, or alt-click) and Generate and Mutate leave
  it alone.
- Tick **Arp** next to Generate and every new Main, FX or Background sound
  comes with an arp in the style of its category: tied 303 slides for acid,
  random blips for bubbles, each with a freshly generated step pattern and a
  line on its note lane, so holding one key already plays a riff. The
  sound itself is the same with or without it; only the arp settings differ.
  Energy sounds, basses and kicks have no arp.
- The same category and seed always give the same sound. Double-click the seed
  in the sidebar to type one in.
- Each category remembers its patch. Switch from Acid to Zap and back and your
  Acid sound is exactly as you left it. The first visit to a category generates
  a fresh sound.
- **Undo** and **Redo** in the top bar step through your Generate and Mutate
  history.
- **Macros**: every Generate gives all four macros a job (cutoff, resonance,
  reverb, drive...) and names them after it: "Cutoff", "Env Amount", "Filter +
  Space". The names follow the matrix, so dragging a macro onto another knob
  renames it too. Double-click a name to type your own; clear it to go back to
  the automatic one.
- The **output window** in the top bar shows the scope and a stereo level
  meter: RMS bars over peak bars, a held peak tick and its value in dB, red
  when the output clips. Drag it out to drop the current sound into your DAW
  as a WAV.
- Presets are saved to `~/Music/PsyBox/Presets/<Category>/`.

The standalone app has a keyboard strip at the bottom. Click the keys or play
from the computer keyboard (`A W S E D F T G Y H U J K` = C4 up, `Z`/`X` shift
the octave). A MIDI controller works too: *Options → Audio/MIDI Settings*.

## The editor

Laid out like Serum. The tabs in the top bar switch views:

- **OSC**: sub and noise, the two oscillators (waveform on top, knobs below),
  the two filters; underneath the macros, the modulators and the voicing, with
  the Length (Energy) or the Knock (Bass) under it.
- **FX**: all nine effects at once, numbered in signal order, each switched on
  in its header. Switched-off effects grey out.
- **Matrix**: the 16 modulation routes as rows: source, amount, target.
- **Arp** (Main, FX, Background): the step editor (play, note, accent and
  slide lanes, a ruler for the loop length), rate, octaves, gate, swing,
  generate a new pattern, and the templates as cards under the steps.
- **Global**: oversampling quality, the full voice settings, output and a
  short guide. Always the last tab.

**Drag and drop modulation.** Every mod source has a four-way arrow: the
envelope and LFO tabs, the macros, the Sweep and the strip of other sources
(velocity, note, mod wheel, aftertouch, bend, random). Drag it onto any knob
that can be modulated (they show a dashed ring while you drag) and a route is
added. The route shows as a ring between the knob body and its track; drag the
ring up or down to set the amount, right-click the knob to invert or remove
it. Sources can modulate each other: drop LFO 2 on LFO 1's rate. Effect knobs
take modulation too; per-voice sources (envelopes, velocity) follow the newest
note there.

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
smooth random), the energy Sweep, 4 macros and a 16-slot mod matrix whose
targets include the effects.

Arpeggiator (Arp view): Up, Down, Up/Down, Random and played order
over 1 to 4 octaves, with rate, gate, swing and hold, playing a 16-step pattern
with per-step accent, slide and note (up to two octaves up or down from the key
the mode picked) and a loop length of 1 to 16 steps. Generate writes a new
pattern in the category's style every time: a rhythm from the category's
figures (303 motifs, rolling, offbeat, 3-3-2, Euclidean loops, fills into the
next bar) and a line in a psy scale (Phrygian, minor, harmonic minor, Phrygian
dominant, Dorian) that repeats and answers itself. Acid sits on the key and
jumps an octave on its accents, leads play broken chords through a
progression, lasers fall down a chord, pads drift between roots. In the Arp
view: paint steps, drag a note up or down (scroll, double-click to reset), set
the length on the ruler, shift, clear, click a template card (Rolling, Gallop,
3-3-2, Acid line...; it keeps your notes) or generate just a new pattern. The arp locks to the host
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

The Noise section's **Direct** switch adds the noise after the filters, for
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
src/ui              theme, widgets, panels, pages (views), displays
tests/              Catch2 tests for the core
tools/              WAV renderer, offscreen UI snapshot tool
```

## License

The plugin links JUCE 8 under the AGPLv3.
