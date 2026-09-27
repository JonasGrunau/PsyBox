# PsySynth

A synth for psytrance sound design. Pick a category, press **Generate**, and
then edit every parameter by hand. It builds as VST3, AU and Standalone.

## Categories

Acid, Squelch, Zap, Laser, Rolling Bass, Psy Lead, Pluck, Stab, Screech,
Alien Voice, Bubble, Gurgle, Wobble, Metallic, Riser, Downlifter, Noise FX,
Atmos, Drone. Each category has its own accent colour.

## Using it

- **Generate**: rolls a new sound in the selected category. Double-clicking a category does the same.
- **Mutate**: nudges the current sound, staying inside the category's ranges.
- **Lock**: right-click a knob (or alt-click it) and Generate/Mutate will leave it alone.
- **Seed**: double-click the seed and type one to get that exact sound back.
- **Undo / Redo** step through your Generate and Mutate history.
- Presets are saved to `~/Music/PsySynth/Presets/<Category>/`.

## DSP

- Oscillators: analog shape morph with 4-point polyBLEP/BLAMP (this includes
  hard sync), a JP-8000-style supersaw, mip-mapped FFT wavetables with 8
  procedural tables, and 2-op FM with feedback.
- Filters: TPT/ZDF state-variable filter, a transistor ladder and a diode
  ladder. Both ladders solve their nonlinear feedback with Newton-Raphson.
  There are also formant and comb filters.
- Voices render at 1x, 2x or 4x and go through linear-phase half-band FIR
  decimation. Distortion runs before decimation and uses ADAA.
- FX: distortion, bitcrusher, Hilbert frequency shifter, phaser,
  chorus/flanger, ping-pong delay and FDN reverb.

## Build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build          # installs to ~/Library/Audio/Plug-Ins
```

JUCE 8 is fetched automatically. To use a local checkout instead, pass
`-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`.

For the core only (DSP tests and offline renders, no JUCE):

```sh
cmake -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSYSYNTH_BUILD_PLUGIN=OFF
cmake --build build-core
./build-core/psysynth_tests
./build-core/psysynth_render <out-dir> 3   # WAVs per category
```

## License

The plugin links JUCE 8 under the AGPLv3.
