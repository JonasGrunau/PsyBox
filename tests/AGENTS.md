# tests

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-09-27

## Purpose
Catch2 v3 unit tests for `psysynth_core` (no JUCE), built as `psysynth_tests` and registered with CTest via `catch_discover_tests`. Most tests are measurement-based: they render audio and check spectra, levels or frequencies instead of exact samples.

## Key Files
| File | Description |
|------|-------------|
| `TestUtil.h` | `testutil::fft`, `spectrumDb` (Blackman-Harris), `worstAliasDb`, `rms`, `peakFrequency` |
| `TestOscillators.cpp` | `[blep]`, `[osc]`, `[wavetable]`: BLEP kernel, saw aliasing at 2x/4x vs naive, square/tri/sync, supersaw, wavetable band-limiting, FM bounds |
| `TestFilters.cpp` | `[filter]`: SVF vs Butterworth, ladder self-oscillation/DC, diode calibration, formant, brutal-modulation stress |
| `TestShapersAndFx.cpp` | `[shaper]`, `[oversampling]`, `[fx]`, `[env]`: ADAA, half-band decimator response, frequency shifter, FX stress, exact ADSR times |
| `TestEngine.cpp` | `[engine]`: plays a 145 BPM 16th-note pattern. Init patch, every category, quality modes, parameter fuzzing |
| `TestGenerator.cpp` | `[generator]`, `[params]`: seed determinism, locks, mutate ranges, mapping round-trips. `[generator][kick]`: engine mode per family, param isolation, the key toggle, kick arps |
| `TestDynamics.cpp` | `[dynamics]`: compressor. Crossover sums flat, static curve and knee, upward lift, exact silence, clean low end (41 Hz THD), 0 % mix identity, stereo link, click-free switching, `[dynamics][stress]` |
| `TestKick.cpp` | `[kick]`: the kick engine. Hits, tuning to the played key (zero crossings and the exact pitch trace), sweep landing, punch, one-shot, retrigger click bound, chord merge, mode switching, quality independence, arp ties, `[kick][stress]` fuzzing |

## For AI Agents

### Working In This Directory
- New test files must be added to `add_executable(psysynth_tests …)` in the root `CMakeLists.txt`.
- Tag every `TEST_CASE` (`[osc]`, `[filter]`, …). Add `[stress]` for fuzz/extreme tests.
- Write the test name as the behaviour claim ("Ladder passes DC with light resonance").
- Include core headers as `"dsp/…"`, `"engine/…"`, `"generator/…"`.
- Prefer spectral/level thresholds from `TestUtil.h` over golden sample data.

### Testing Requirements
```sh
cmake --build build-core && ./build-core/psysynth_tests            # all
./build-core/psysynth_tests "[filter]"                             # by tag
ctest --test-dir build-core                                        # via CTest
```

## Dependencies

### Internal
- `src/core/` (links `psysynth_core`)

### External
- Catch2 v3.7.1 (`Catch2::Catch2WithMain`)

## Manual Notes
