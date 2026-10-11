# engine

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-10-05

## Purpose
Turns a `ParamState` plus MIDI events into stereo audio. `Params` is the single source of truth for every parameter. `Voice` renders one note at the oversampled rate. `Engine` handles polyphony/mono/legato, global LFOs, parameter smoothing, the mod matrix, decimation and the FX chain.

## Key Files
| File | Description |
|------|-------------|
| `Params.h` | `PSY_PARAMS(X)` X-macro listing every parameter (id, name, group, mapping, range, default, skew, unit, choices, modulatable). Generates `enum class P`. Also `ParamDef`, `ParamState`, `ModSrc` (… `Macro4`, `Sweep`) / `ModDst` (voice targets, then `F2Drive`, `F2EnvAmt` and the effect targets from `DistDrive`), constants `kNumModSlots=16` (`PSY_MOD_SLOT`), `kNumLfos=3`, `kMaxVoices=16`, index helpers `modSrcParam()`/`lfoParam()`, the Energy params (`EnergyMode` Bars/Time, `EnergyBars`, `EnergySec`, `EnergyCurve`, `EnergyFade`, `EnergyCut`, `EnergyOneShot`: note-offs are ignored and the note ends at its length), `RenderQuality` (last in the table), `modTargetParam`/`modTargetFor`/`modDepthNorm`/`isFxTarget` |
| `Params.cpp` | Choice lists (`choices::…`), `paramDefs()` table, norm↔plain mappings, `format`, `findParam`, `syncDivBeats`, `energyBarsValue`, the target ↔ knob table `kTargets` (with each target's depth as a share of its knob's travel, for the rings) |
| `Voice.h/.cpp` | `VoiceContext` (per-chunk shared state, pre-filtered voice mod slots, the sweep's step/curve/fade/cut) and `Voice`: 2 osc slots (Analog/Supersaw/Wavetable/FM), each a stack of `kMaxUnison` oscillators for unison (laid out by `dsp::UnisonLayout`, cached in `Voice::Unison` until a setting moves; the Supersaw ignores the count), sub, noise, 2 `FilterSlot`s (plus a right-hand pair that runs only while a unison oscillator has Width, copied from the left when it starts; otherwise the voice is mono up to the pan; count 1 renders exactly as before), amp/filter/mod envs, pitch env, per-voice LFOs, glide, the energy sweep (0 → 1 over the length from the note start, `pow(t, 2^(2.5·curve))`; the level follows it by `EnergyFade`; `EnergyCut` releases the note at the end). `sources()` keeps the last mod source values for effect modulation; `filterNow(slot)` the cutoff, reso and vowel each filter was last set to, modulation included. Declicking: the level moves at most 0.5 (a full note) per ms, then a 0.1 ms one-pole (`level1`/`level2`), so instant attacks, retriggers, fast releases and gate chops ramp; a fresh start fades the oscillator mix in over 1 ms before the filters (`onset`); a filter's cutoff closes at most 0.5 oct/ms from no higher than 20 kHz (`closing`, opens at once); `isActive()` stays true until the smoothed level is down; `fadeOut()` takes the voice out in 3 ms, `level()` < `kQuiet` means it can restart in place |
| `Engine.h/.cpp` | `MidiEvent`, `Transport`, `Engine`: `prepare`, `setParams` (target, smoothed internally), `process(l, r, n, events, transport)` (a `bpm` overload remains), keys → arp or voices, output gain `MasterVol + PatchGain` (Level: the patch's own trim set by `levelPatch`) before the soft clip, voice allocation/stealing (`handOff`: a voice still above `Voice::kQuiet` that would restart in place, a mono retrigger or a poly steal, swaps into an idle slot (above `PolyVoices` in poly) and fades out there, and the note starts fresh), note stack, sustain, FX chain, `latencySamples()`. `setOffline(true)` plays at `RenderQuality` instead of `Quality`. `getModView` fills a `ModView` (every sounding voice, newest first: stage and progress of the amp env and Env 1-3, LFO phases, the sweep, each filter's modulated setting) for the editor's moving dots and filter curves. `prepareContext` splits the matrix into voice slots and effect slots and turns the energy length into a per-sample step (bars × 4 beats at the host tempo, or seconds). `modulateFx` writes `fxParams` (effect targets move in their knob's normalised range; macros, wheel, bend, aftertouch and free LFOs are global, the rest follow the newest voice) every 32 samples. `processFx` plays the effects in `fxChain(FxOrder)`; Distortion first runs on the oversampled signal before the decimator, anywhere else as `distLate` at the base rate (both get `DistLowCut` and `DistAuto`, the Jilax clipper's low cut and Auto Level). `EnergyOneShot`: `voiceNoteOff` returns after `dropHeld`, and `prepareContext` sets `sweepCut` so the voice releases at the end of its length |
| `KickVoice.h/.cpp` | Kick engine (`EngineMode::Kick`). `KickSettings::from(ParamState)`, `kickBodyHz()` (key tracking: the note's pitch class within ±6 semitones of Tune), `kickPitchAt()`, and `KickVoice`: a two-stage exponential pitch sweep that lands exactly on target after 3 time constants, punch + attack/hold/decay (`Adsr`), a Noise/Tick/Blip click, an ADAA body saturator (Soft/Tube/Hard), a tone LP, a 3 ms choke (also when its envelope runs out, so the DC blocker's remainder fades instead of dropping). One-shot, mono, phase 0 on every hit. `renderOffline()` renders one hit for the UI and tests |
| `Arpeggiator.h/.cpp` | Tempo-synced arp between MIDI keys and voices: modes, octaves, the 16-step `ArpPattern` (on/accent/slide/note per step + length; params `arp_step*`/`arp_acc*`/`arp_slide*`/`arp_note*` from `PSY_ARP_STEP` in `Params.h`; the note lane adds -24..+24 semitones to the key the mode picked, clamped to MIDI), gate, swing, hold. `sync(ppq)` per block, then `fireDue`/`samplesToNextEvent`/`advance` inside the engine's chunk loop. Slide steps and gate ≥ 100 % emit slide events (no retrigger). `ArpSettings::key/scale` (from `ArpKey`/`ArpScale`): with a key every played note goes through `quantizeToScale` (nearest scale note, down on a tie) |

## For AI Agents

### Working In This Directory
- **Adding a parameter:** add one `X(...)` line to `PSY_PARAMS` in `Params.h`. The enum, JUCE layout, state save and generator ranges pick it up automatically. Then read it in `Voice.cpp`/`Engine.cpp`, add a widget in `src/ui/Panels.cpp`, and optionally set it in recipes.
- **Parameter ids (`"reverb_mix"`) are the save format.** Presets and host state store values by id. Renaming an id silently drops saved values.
- **Order matters for repeated blocks.** `modSrcParam(slot)` assumes Src/Dst/Amt triples starting at `Mod1Src` (`PSY_MOD_SLOT`), and `lfoParam(lfo, off)` assumes 6 params per LFO from `Lfo1Shape`. Insert new params outside those blocks.
- Adding a choice → update the matching `choices::` vector in `Params.cpp`. Mod sources/destinations must match the `ModSrc`/`ModDst` enums, and the `PSY_MOD_SLOT` maxima must be `Count - 1` (`[params]` checks). Append new sources/targets at the end: saved patches store their index.
- **Adding a mod target:** append to `ModDst` and `choices::modDst`, read it in `Voice::render` (voice) or give it a `kTargets` row with its knob (effects are handled by `modulateFx` once they are past `DistDrive`). With a `kTargets` row the knob takes drops and draws rings automatically.
- `Engine::process` and `Voice::render` are audio-thread code. Keep them `noexcept`, with no allocation.
- Quality (1x/2x/4x; `RenderQuality` when offline) changes `osFactor` and latency. The plugin polls `latencySamples()` every block.
- **Kick mode.** `voiceNoteOn` sends notes to the two `kicks` when `EngineMode` is Kick. Note-ons within 1 ms of a fresh hit merge into it, and note-off is ignored. `followEngineMode()` releases the synth voices and chokes the kicks when the mode changes. `ArpSettings::from` clamps the gate below 1 and drops slides in kick mode, because a tie would swallow a kick. After `reset()`, `process()` sets `cur = target` before handling any event, so re-sent notes play the new patch.

### Testing Requirements
- `psybox_tests "[engine]"`: init patch sound/silence, every category renders finite non-silent audio (energy sounds hold one note), all quality modes match, parameter fuzzing. `"[params]"` checks mapping round-trips and the matrix lists. `"[energy]"`: the sweep lasts its bars at the tempo (or its seconds), the fade follows it. `"[fx]"`: effects follow their matrix modulation.
- `psybox_tests "[kick]"`: identical hits, body on the played key, octave independence, the sweep landing, the punch envelope, one-shot, click-free retrigger, chord merge, mode switching, quality independence, no arp ties, fuzzing.

### Common Patterns
- Plain (denormalised) values live in `ParamState::v`. Use `get`, `geti`, `on`. `set` snaps discrete params.
- The generator works in normalised space via `ParamDef::toNorm/toPlain`.

## Dependencies

### Internal
- `../dsp/`: all oscillators, filters, modulators, FX, decimators

## Manual Notes
