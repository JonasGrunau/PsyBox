# plugin

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-09-27

## Purpose
The JUCE shell around the core. `PsyBoxProcessor` builds the APVTS from `paramDefs()`, converts JUCE MIDI to `psy::MidiEvent`, runs `psy::Engine`, and owns the message-thread features: generator, locks, undo/redo history, presets and state save. `PsyBoxEditor` is a fixed 1280×820 logical canvas scaled as a whole.

## Key Files
| File | Description |
|------|-------------|
| `PluginProcessor.h` | Public API used by the UI: `param(P)`, `plain(P)`, `generate`/`mutate`/`generateWithSeed`, locks, `undo`/`redo`, presets, `copyScope`, `takeOutLevel` (per-channel peak since the last call and 300 ms RMS), `getCompReading`. `ChangeBroadcaster` notifies the UI of generator/preset changes |
| `PluginProcessor.cpp` | `createLayout()` from `paramDefs()`, `processBlock` (atomics → `ParamState` → engine, scope ring buffer, latency reporting), XML state (`category`, `seed`, `mutate`, `presetName`, `locks`, params by id), presets in `~/Music/PsyBox/Presets/<Category>/` |
| `PluginEditor.h/.cpp` | `PsyBoxEditor` + inner `Root` that lays out TopBar, Sidebar and all panels. Updates the theme accent on category change |

## For AI Agents

### Working In This Directory
- **Thread split.** `processBlock` is the audio thread: relaxed atomic loads, no allocation (MIDI events only `push_back` within reserved capacity). Generator, presets, history and locks are message-thread only. The UI reaches the audio thread only through APVTS and the atomics (`scope`, `outPeak`, `outMeanSquare`, `compMeters`).
- Applying a generated patch goes through `applyState`, which sets APVTS params so the host sees changes. It also enforces the family: `EngineMode` from the category, and `ArpOn` off for basses and kicks (`normaliseFamily` does the same after a state load). Then `pushHistory` and a change broadcast.
- State/preset loading maps by param **id** via `findParam`. Unknown ids are ignored, missing ones keep defaults. Keep that tolerance.
- Both files are also compiled into `psybox_snapshot` (it defines `JucePlugin_Name` itself). Don't depend on plugin-wrapper-only macros without a fallback.

### Testing Requirements
- No automated tests cover this layer. Build the plugin target and check in the Standalone app, or render with `psybox_snapshot`.
- If you touch state save/load: save a preset, reload, and confirm locks, seed and category survive.

## Dependencies

### Internal
- `core/engine/Engine.h`, `core/generator/Generator.h`
- `ui/Panels.h`, `ui/PsyLookAndFeel.h` (editor)

### External
- JUCE: `juce_audio_processors`, `juce_audio_utils`, `juce_dsp`

## Manual Notes
