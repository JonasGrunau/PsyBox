# dsp

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-10-17

## Purpose
Self-contained audio DSP primitives in namespace `psy::dsp`. Almost everything is header-only; only the wavetable bank has a `.cpp`. Quality is the priority: aliasing is controlled by 4-point polyBLEP/BLAMP, ADAA waveshapers and half-band FIR decimation, and the ladder filters use zero-delay-feedback solved with Newton-Raphson.

## Key Files
| File | Description |
|------|-------------|
| `Common.h` | `fastTanh`, `Rng` (seeded, deterministic), `OnePole`, `DcBlocker`, shared math helpers |
| `Blep.h` | 4-point B-spline BLEP/BLAMP residuals (`blep::step`, `blep::ramp`), `Accumulator`, `Delay2`. Adds 2 samples of output delay |
| `Oscillators.h` | `AnalogOsc` (sine→tri→saw→square morph, hard sync via `SyncInfo`), `Supersaw` (JP-8000 style; its offsets and gain curves are public for the display), `WavetableOsc`, `FmOsc` (2-op + feedback), `Noise` (white, pink, digital S&H, hiss: white above 10 kHz, 18 dB/oct; `setRate` sets its corner) |
| `Wavetable.h/.cpp` | `WavetableBank::shared()`: 8 procedural tables × 16 frames × 10 octave mip levels, built once via FFT, read-only across voices and instances |
| `Unison.h` | `UnisonLayout`: where each of up to 16 unison voices sits in pitch (detune × range × the Mode curve, plus the Stack), its gain (Blend, normalised to constant power) and pan (Width, alternating sides pair by pair). Voice 0 is the centre (0 and 1 for an even count). `retrigPhase(k)` spreads start phases. Shared by `Voice` and the editor's displays |
| `Filters.h` | `Svf` (TPT/Simper), `Ladder` (transistor), `DiodeLadder` (303), `Formant`, `Comb`, and `FilterSlot`, which switches between them by type index |
| `Shapers.h` | `AdaaShaper`: first-order antiderivative anti-aliased waveshaping (computed in double). `ClipResidual`: what a hard clip at +-1 takes off, `clamp(x) - x`, with the same ADAA (zero inside the ceiling, so adding it trims peaks without averaging the rest) |
| `Modulators.h` | `Adsr` (analog-style segments, exact times, click-free retrigger; `setAttackStart` lifts the attack's starting level (reached in 3 ms; a reversed tail), `setAttackCurve` bends the attack from the RC charge to a swell, `attackShape`/`attackBend` shared with the envelope display; `stageProgress()` says how far into the stage it is, for the editor's moving dots), `PitchEnv` (`getStage`/`stageProgress`), `Lfo` (`phase2()`: the phase over two cycles, as the display draws them; every edge, square, saw or S&H, is rounded by a 0.35 ms one-pole, and `snap()` takes the next value as it is for a fresh voice) |
| `Oversampling.h` | `HalfbandDecimator` (Kaiser-designed FIR) and `Decimator` (1x/2x/4x → base rate, reports latency) |
| `Effects.h` | `DelayLine`, `Distortion` (ADAA shaper, tone low-pass, a post-shaper 24 dB `lowCutHz` that is skipped at 20 Hz or below; the spikes it makes of a clipped wave are trimmed back to the shaper's ceiling (Rectify excepted) after the top is restored, by adding the `ClipResidual` high-passed by the same low cut, so the trim brings no lows back; and `autoLevel`: an RMS follower (15 ms mean square, then 5 ms up / 150 ms down) normalises the signal to -18 dBFS RMS before the shaper and gives the level back after, so the clipping depth doesn't depend on the input level and decays fall through; below 60 kHz a [-0.2, 1.4, -0.2] FIR on the wet signal undoes the ADAA's pair-average droop (wet and dry one sample late)), `Bitcrusher`, `FrequencyShifter` (Hilbert), `Phaser`, `ChorusFlanger`, `StereoDelay` (ping-pong), `Reverb` (FDN), `StereoWidth` (mid/side: Low Mono via LR4 on the side with the matching allpass on the mid, Width, Haas Stereoize; L + R never changes) |
| `Phoscyon.h` | The 303's parts: `PhosOsc` (BLEP saw, and the 303 square made from it: flat top, sagging bottom, duty 0.474 with a 0.33 ms switching delay), `PhosLadder` (diode ladder A(p) = m p⁴ + (6m+1) p³ + (10m+5) p² + (4m+6) p + 1, a high-pass in the feedback path, tanh solved by Newton), `HighPass2` |
| `PhosEffects.h` | Phoscyon's effects, fitted dry-vs-wet against the original: `PhosDistortion` (19 models from its presets: diode, overdrive and RAT clippers, bias, pre/post filters, Color crossfades to a low-pass or adds a high-pass, a Dynamics leveller before or after), `PhosEq` (4 peaks, width table → Q), `PhosChorus` (rate 9.7 e^(6.56(r-1)) Hz, two extra voices for Fatness), `PhosDelay` (feedback filter off/LP/BP/HP with resonance, Spread delays one side), `PhosReverb` (early + late, decay from size and feedback), `PhosLimiter` (stereo-linked peak limiter, 3.3 ms lookahead, 4.29 dB knee at 0 %, dB attack/release (release × 1.14), Gain 1 + 15 × dB/24 in amplitude as the original applies it, soft clip linear to 0.8; `curveDb` for the display). Mix laws: dry min(1, 2(1-m)), wet min(1, 2m) |
| `Dynamics.h` | `Compressor` (Normal: single band; Multiband: 3-band up + down, OTT style) with `CompSettings` in and `CompMeter` out. Parts in `dyn::`: `Crossover3` (LR4, low band allpassed so the bands sum flat), `PeakHold` (4-bucket running max over a band's longest period), soft-knee `downGain` / `upGain` (Giannoulis et al.), `GainSmoother` (dB-domain attack/release). `Ramp` (per-sample ramp for block-rate params) lives in `Common.h` |

## For AI Agents

### Working In This Directory
- Keep headers dependency-free (std only). Nothing here may know about `ParamState` or `P`. The engine maps parameters onto these classes.
- Per-sample methods are `noexcept` and must not allocate. Allocation belongs in `prepare`/constructors.
- Oscillators run at the **oversampled** rate inside voices. Effects run at the **base** rate after decimation. Note which rate a new block expects.
- `AnalogOsc` output is delayed 2 samples (BLEP lookahead). Keep other oscillator paths aligned when mixing.
- Header comments cite the source papers (Zavalishin, Simper, Parker et al.). Keep that style when adding algorithms.

### Testing Requirements
- Oscillator/wavetable changes → `psybox_tests "[osc],[blep],[wavetable]"`. These measure aliasing in dB via FFT (`tests/TestUtil.h`).
- Filter changes → `"[filter]"`. Shaper/FX/decimator/envelope changes → `"[shaper],[fx],[oversampling],[env]"`.
- `[stress]` tests fuzz with extreme settings. New modules should get a finite/bounded stress test.

### Common Patterns
- `prepare(sampleRate)` / `reset()` / per-sample `process(x)` or `next()`.
- Randomness only through `dsp::Rng` so generated patches and noise stay reproducible per seed.

## Dependencies

### Internal
- None (leaf layer)

## Manual Notes
