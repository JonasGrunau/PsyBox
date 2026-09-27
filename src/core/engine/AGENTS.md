# engine

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-09-27

## Purpose
Turns a `ParamState` plus MIDI events into stereo audio. `Params` is the single source of truth for every parameter. `Voice` renders one note at the oversampled rate. `Engine` handles polyphony/mono/legato, global LFOs, parameter smoothing, the mod matrix, decimation and the FX chain.

## Key Files
| File | Description |
|------|-------------|
| `Params.h` | `PSY_PARAMS(X)` X-macro listing every parameter (id, name, group, mapping, range, default, skew, unit, choices, modulatable). Generates `enum class P`. Also `ParamDef`, `ParamState`, `ModSrc`/`ModDst`, constants `kNumModSlots=8`, `kNumLfos=3`, `kMaxVoices=16`, index helpers `modSrcParam()`/`lfoParam()` |
| `Params.cpp` | Choice lists (`choices::…`), `paramDefs()` table, norm↔plain mappings, `format`, `findParam`, `syncDivBeats` |
| `Voice.h/.cpp` | `VoiceContext` (per-chunk shared state + pre-filtered mod slots) and `Voice`: 2 osc slots (Analog/Supersaw/Wavetable/FM), sub, noise, 2 `FilterSlot`s, amp/filter/mod envs, pitch env, per-voice LFOs, glide |
| `Engine.h/.cpp` | `MidiEvent`, `Transport`, `Engine`: `prepare`, `setParams` (target, smoothed internally), `process(l, r, n, events, transport)` (a `bpm` overload remains), keys → arp or voices, voice allocation/stealing, note stack, sustain, FX chain, `latencySamples()` |
| `KickVoice.h/.cpp` | Kick engine (`EngineMode::Kick`). `KickSettings::from(ParamState)`, `kickBodyHz()` (key tracking: the note's pitch class within ±6 semitones of Tune), `kickPitchAt()`, and `KickVoice`: a two-stage exponential pitch sweep that lands exactly on target after 3 time constants, punch + attack/hold/decay (`Adsr`), a Noise/Tick/Blip click, an ADAA body saturator (Soft/Tube/Hard), a tone LP, a 3 ms choke. One-shot, mono, phase 0 on every hit. `renderOffline()` renders one hit for the UI and tests |
| `Arpeggiator.h/.cpp` | Tempo-synced arp between MIDI keys and voices: modes, octaves, the 16-step `ArpPattern` (on/accent/slide per step + length; params `arp_step*`/`arp_acc*`/`arp_slide*` from `PSY_ARP_STEP` in `Params.h`), gate, swing, hold. `sync(ppq)` per block, then `fireDue`/`samplesToNextEvent`/`advance` inside the engine's chunk loop. Slide steps and gate ≥ 100 % emit slide events (no retrigger) |

## For AI Agents

### Working In This Directory
- **Adding a parameter:** add one `X(...)` line to `PSY_PARAMS` in `Params.h`. The enum, JUCE layout, state save and generator ranges pick it up automatically. Then read it in `Voice.cpp`/`Engine.cpp`, add a widget in `src/ui/Panels.cpp`, and optionally set it in recipes.
- **Parameter ids (`"reverb_mix"`) are the save format.** Presets and host state store values by id. Renaming an id silently drops saved values.
- **Order matters for repeated blocks.** `modSrcParam(slot)` assumes Src/Dst/Amt triples starting at `Mod1Src`, and `lfoParam(lfo, off)` assumes 6 params per LFO from `Lfo1Shape`. Insert new params outside those blocks.
- Adding a choice → update the matching `choices::` vector in `Params.cpp`. Mod sources/destinations must match the `ModSrc`/`ModDst` enums.
- `Engine::process` and `Voice::render` are audio-thread code. Keep them `noexcept`, with no allocation.
- Quality (1x/2x/4x) changes `osFactor` and latency. The plugin polls `latencySamples()` every block.
- **Kick mode.** `voiceNoteOn` sends notes to the two `kicks` when `EngineMode` is Kick. Note-ons within 1 ms of a fresh hit merge into it, and note-off is ignored. `followEngineMode()` releases the synth voices and chokes the kicks when the mode changes. `ArpSettings::from` clamps the gate below 1 and drops slides in kick mode, because a tie would swallow a kick. After `reset()`, `process()` sets `cur = target` before handling any event, so re-sent notes play the new patch.

### Testing Requirements
- `psysynth_tests "[engine]"`: init patch sound/silence, every category renders finite non-silent audio, all quality modes match, parameter fuzzing. `"[params]"` checks mapping round-trips.
- `psysynth_tests "[kick]"`: identical hits, body on the played key, octave independence, the sweep landing, the punch envelope, one-shot, click-free retrigger, chord merge, mode switching, quality independence, no arp ties, fuzzing.

### Common Patterns
- Plain (denormalised) values live in `ParamState::v`. Use `get`, `geti`, `on`. `set` snaps discrete params.
- The generator works in normalised space via `ParamDef::toNorm/toPlain`.

## Dependencies

### Internal
- `../dsp/`: all oscillators, filters, modulators, FX, decimators

## Manual Notes
