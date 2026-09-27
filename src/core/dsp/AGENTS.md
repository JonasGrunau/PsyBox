# dsp

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-09-27

## Purpose
Self-contained audio DSP primitives in namespace `psy::dsp`. Almost everything is header-only; only the wavetable bank has a `.cpp`. Quality is the priority: aliasing is controlled by 4-point polyBLEP/BLAMP, ADAA waveshapers and half-band FIR decimation, and the ladder filters use zero-delay-feedback solved with Newton-Raphson.

## Key Files
| File | Description |
|------|-------------|
| `Common.h` | `fastTanh`, `Rng` (seeded, deterministic), `OnePole`, `DcBlocker`, shared math helpers |
| `Blep.h` | 4-point B-spline BLEP/BLAMP residuals (`blep::step`, `blep::ramp`), `Accumulator`, `Delay2`. Adds 2 samples of output delay |
| `Oscillators.h` | `AnalogOsc` (sine→tri→saw→square morph, hard sync via `SyncInfo`), `Supersaw` (JP-8000 style), `WavetableOsc`, `FmOsc` (2-op + feedback), `Noise` |
| `Wavetable.h/.cpp` | `WavetableBank::shared()`: 8 procedural tables × 16 frames × 10 octave mip levels, built once via FFT, read-only across voices and instances |
| `Filters.h` | `Svf` (TPT/Simper), `Ladder` (transistor), `DiodeLadder` (303), `Formant`, `Comb`, and `FilterSlot`, which switches between them by type index |
| `Shapers.h` | `AdaaShaper`: first-order antiderivative anti-aliased waveshaping (computed in double) |
| `Modulators.h` | `Adsr` (analog-style segments, exact times, click-free retrigger), `PitchEnv`, `Lfo` |
| `Oversampling.h` | `HalfbandDecimator` (Kaiser-designed FIR) and `Decimator` (1x/2x/4x → base rate, reports latency) |
| `Effects.h` | `DelayLine`, `Distortion`, `Bitcrusher`, `FrequencyShifter` (Hilbert), `Phaser`, `ChorusFlanger`, `StereoDelay` (ping-pong), `Reverb` (FDN) |

## For AI Agents

### Working In This Directory
- Keep headers dependency-free (std only). Nothing here may know about `ParamState` or `P`. The engine maps parameters onto these classes.
- Per-sample methods are `noexcept` and must not allocate. Allocation belongs in `prepare`/constructors.
- Oscillators run at the **oversampled** rate inside voices. Effects run at the **base** rate after decimation. Note which rate a new block expects.
- `AnalogOsc` output is delayed 2 samples (BLEP lookahead). Keep other oscillator paths aligned when mixing.
- Header comments cite the source papers (Zavalishin, Simper, Parker et al.). Keep that style when adding algorithms.

### Testing Requirements
- Oscillator/wavetable changes → `psysynth_tests "[osc],[blep],[wavetable]"`. These measure aliasing in dB via FFT (`tests/TestUtil.h`).
- Filter changes → `"[filter]"`. Shaper/FX/decimator/envelope changes → `"[shaper],[fx],[oversampling],[env]"`.
- `[stress]` tests fuzz with extreme settings. New modules should get a finite/bounded stress test.

### Common Patterns
- `prepare(sampleRate)` / `reset()` / per-sample `process(x)` or `next()`.
- Randomness only through `dsp::Rng` so generated patches and noise stay reproducible per seed.

## Dependencies

### Internal
- None (leaf layer)

## Manual Notes
