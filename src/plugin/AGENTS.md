# plugin

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-10-05

## Purpose
The JUCE shell around the core. `PsyBoxProcessor` builds the APVTS from `paramDefs()`, converts JUCE MIDI to `psy::MidiEvent`, runs `psy::Engine`, and owns the message-thread features: generator, locks, undo/redo history, presets, state save, macro names and the modulation API the knobs use. `PsyBoxEditor` is a fixed 1280×820 logical canvas scaled as a whole, and the drag-and-drop container for mod sources.

## Key Files
| File | Description |
|------|-------------|
| `PluginProcessor.h` | Public API used by the UI: `param(P)`, `plain(P)`, `generate`/`mutate`/`generateWithSeed`, locks, `undo`/`redo`, presets, `copyScope`, `takeOutLevel` (per-channel peak since the last call and 300 ms RMS), `getCompReading`. Category helpers (`categoryHasArp`, `hasLength` (energy sounds and reverse basses: the Length panel), `getBpm`). Macro names: automatic from `psy::macroLabel` unless typed (`setMacroName`; empty or the automatic text goes back to automatic). Modulation: `getModRoutes`, `modRoutesTo` (no allocation, knobs poll it), `addModulation` (reuses an existing source/target route), `removeModulation`, `begin/set/endModAmountChange`. `setEnergyMode` converts the length between bars and seconds at the current tempo; `energyLengthSeconds`. Editor state: `uiSelectedSource`, `uiDragSource`. `ChangeBroadcaster` notifies the UI of generator/preset/route changes |
| `PluginProcessor.cpp` | `createLayout()` from `paramDefs()`, `processBlock` (atomics → `ParamState` → engine, `engine.setOffline (isNonRealtime())` so host renders use the Render quality, scope ring buffer, latency reporting, the `ModView` under a try-locked `SpinLock` for `getModView`), XML state (`category`, `seed`, `mutate`, `presetName`, `locks`, typed macro names `macro1..4`, per-category slots with their macro names, params by id), presets in `~/Music/PsyBox/Presets/<Category>/`. The bounce holds energy sounds for their length and plays at the Render quality. `applyState` keeps both qualities (global settings, not part of a sound) |
| `PluginEditor.h/.cpp` | `PsyBoxEditor` (`DragAndDropContainer`: `dragOperationStarted/Ended` set `proc.uiDragSource`) + inner `Root`: TopBar, Sidebar and the pages, switched by the view tabs (`showView`). `updateMode` picks the views and the OSC page's extra (Length / Knock / None) for the category; in Phoscyon mode the first tab is PHOSCYON (`PhosPage`) and FX shows the Phoscyon rack (`phosFx`). Updates the theme accent on category change |

## For AI Agents

### Working In This Directory
- **Thread split.** `processBlock` is the audio thread: relaxed atomic loads, no allocation (MIDI events only `push_back` within reserved capacity). Generator, presets, history and locks are message-thread only. The UI reaches the audio thread only through APVTS and the atomics (`scope`, `outPeak`, `outMeanSquare`, `compMeters`).
- Generate (`generateWithSeed`), Mutate and `generateArpPattern` run `psy::levelPatch` on the new patch before `applyState`, so every patch lands on one loudness (Level, `P::PatchGain`; a locked Level stays). Loading presets, sessions or history never re-levels.
- Applying a generated patch goes through `applyState`, which sets APVTS params so the host sees changes. It also enforces the family: `EngineMode` from the category, and `ArpOn` off where `hasArp(category)` is false (basses, kicks, energy; `normaliseFamily` does the same after a state load). Then `pushHistory` and a change broadcast.
- Generate clears typed macro names (the new macros are named automatically); undo/redo and category slots carry them. Older sessions stored "Macro N" for untouched names; `readMacroNames` treats that as automatic.
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
