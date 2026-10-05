# tests

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-10-05

## Purpose
Catch2 v3 unit tests for `psybox_core` (no JUCE), built as `psybox_tests` and registered with CTest via `catch_discover_tests`. Most tests are measurement-based: they render audio and check spectra, levels or frequencies instead of exact samples.

## Key Files
| File | Description |
|------|-------------|
| `TestUtil.h` | `testutil::fft`, `spectrumDb` (Blackman-Harris), `worstAliasDb`, `rms`, `peakFrequency` |
| `TestOscillators.cpp` | `[blep]`, `[osc]`, `[wavetable]`: BLEP kernel, saw aliasing at 2x/4x vs naive, square/tri/sync, supersaw, wavetable band-limiting, FM bounds |
| `TestFilters.cpp` | `[filter]`: SVF vs Butterworth, ladder self-oscillation/DC, diode calibration, formant, brutal-modulation stress |
| `TestShapersAndFx.cpp` | `[shaper]`, `[oversampling]`, `[fx]`, `[env]`: ADAA, half-band decimator response, frequency shifter, FX stress, exact ADSR times, the amp attack Curve (swell shape, same time, click-free retrigger). `[fx][width]`: mono sum untouched, Low Mono, Stereoize above the split only, 0 % folds to mono |
| `TestEngine.cpp` | `[engine]`: plays a 145 BPM 16th-note pattern (`play`) or holds one note (`hold`, energy sounds). Init patch, every category, quality modes, parameter fuzzing, Noise Direct, Bass Shot traits (sub share, loudness, mono lows), Psy/Prog/Offbeat bass traits (mono, dry, sub share, crest, level). `[energy]`: the sweep lasts its length in bars at 120 and 145 BPM or in seconds, the level follows the fade. `[fx]`: a macro routed to reverb mix brings in the tail. `[notes]`: a doubled note-on and a voice-mode change while keys are held never leave a note hanging. `[bass]`: reverse basses swell and stop at their length with the key still down |
| `TestGenerator.cpp` | `[generator]`, `[params]`: seed determinism, locks, mutate ranges, mapping round-trips, matrix lists vs enums and target ↔ knob mapping. `[generator][kick]`: engine mode per family, param isolation, the key toggle. `[generator][arp]`: basses, kicks and energy sounds never get an arp; generated lines move off the key and stay in one of the five scales. `[generator][category]`: tabs sorted, A–Z inside, the tabs hold their sounds, one rainbow down the whole sidebar. `[generator][macro]`: macro labels; every synth/bass patch drives four macros. `[generator][energy]`: energy patches use the sweep, the length unit is the user's. `[generator][bass]`: reverse basses are mono, cut at 1/4 or 1/2 bar, and the sub slides a 4th, 5th or octave |
| `TestDynamics.cpp` | `[dynamics]`: compressor. Crossover sums flat, static curve and knee, upward lift, exact silence, clean low end (41 Hz THD), 0 % mix identity, stereo link, click-free switching, `[dynamics][stress]` |
| `TestLoudness.cpp` | `[engine][level]`: Level scales the output like Volume. `[generator][level]`: every category and seed (with and without arp) lands on `kTargetLoudness` with Level in range; leveling is repeatable, ignores the Level and Volume it is given, and leaves a locked Level alone |
| `TestKick.cpp` | `[kick]`: the kick engine. Hits, tuning to the played key (zero crossings and the exact pitch trace), sweep landing, punch, one-shot, retrigger click bound, chord merge, mode switching, quality independence, arp ties, `[kick][stress]` fuzzing |

## For AI Agents

### Working In This Directory
- New test files must be added to `add_executable(psybox_tests …)` in the root `CMakeLists.txt`.
- Tag every `TEST_CASE` (`[osc]`, `[filter]`, …). Add `[stress]` for fuzz/extreme tests.
- Write the test name as the behaviour claim ("Ladder passes DC with light resonance").
- Include core headers as `"dsp/…"`, `"engine/…"`, `"generator/…"`.
- Prefer spectral/level thresholds from `TestUtil.h` over golden sample data.

### Testing Requirements
```sh
cmake --build build-core && ./build-core/psybox_tests            # all
./build-core/psybox_tests "[filter]"                             # by tag
ctest --test-dir build-core                                        # via CTest
```

## Dependencies

### Internal
- `src/core/` (links `psybox_core`)

### External
- Catch2 v3.7.1 (`Catch2::Catch2WithMain`)

## Manual Notes
