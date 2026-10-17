# plugin

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-10-18

## Purpose
The JUCE shell around the core. `PsyBoxProcessor` builds the APVTS from `paramDefs()`, converts JUCE MIDI to `psy::MidiEvent`, runs `psy::Engine`, and owns the message-thread features: generator, locks, undo/redo history, presets, state save, macro names and the modulation API the knobs use. `PsyBoxEditor` is a fixed 1280×820 logical canvas scaled as a whole, and the drag-and-drop container for mod sources.

## Key Files
| File | Description |
|------|-------------|
| `PluginProcessor.h` | Public API used by the UI: `param(P)`, `plain(P)`, `generate`/`mutate`/`generateWithSeed`, locks, `undo`/`redo`, presets, `copyScope`, `takeOutLevel` (per-channel peak since the last call and 300 ms RMS), `getCompReading`. Category helpers (`categoryHasArp` (synth categories except energy sounds and Jilax), `hasLength` (energy sounds, reverse basses and Jilax: the Length panel), `getBpm`). Macro names: automatic from `psy::macroLabel` unless typed (`setMacroName`; empty or the automatic text goes back to automatic). Modulation: `getModRoutes`, `modRoutesTo` (no allocation, knobs poll it), `addModulation` (reuses an existing source/target route), `removeModulation`, `beginModAmountChange`, `setModAmount`, `endModAmountChange`. `setEnergyMode` converts the length between bars and seconds at the current tempo; `energyLengthSeconds`. Editor state: `uiSelectedSource`, `uiDragSource`. `ChangeBroadcaster` notifies the UI of generator/preset/route changes. `getTailLengthSeconds` adds the release, the echoes to -60 dB and the reverb of the current patch (capped at 60 s). `presetFileFor` says where `savePreset` writes a name (the UI asks before overwriting) |
| `PluginProcessor.cpp` | `createLayout()` from `paramDefs()` (host text → value through `ParamDef::parse`, the inverse of `format`), `processBlock` (atomics → `ParamState` → engine, `engine.setOffline (isNonRealtime())` so host renders use the Render quality (also before `prepare`, so the latency reported at the start is the render's), scope ring buffer, latency reporting, the `ModView` under a try-locked `SpinLock` for `getModView`), `processBlockBypassed` (silent, follows the keys; the next block restarts like a swap), XML state (`stateVersion`, `category`, `seed`, `mutate`, `presetName`, `locks`, typed macro names `macro1..4` on the root, per-category slots with their macro names as `macroName1..4` (a slot's params are properties by id, so `macro1` is Macro 1's value there), params by id), presets in `~/Music/PsyBox/Presets/<Category>/` (stepping goes from the preset file the patch came from). Metadata writes and `getStateInformation` hold `stateLock` (hosts may save from another thread); never hold it around a parameter change. The bounce holds energy sounds for their length, plays at the Render quality and never writes over an earlier bounce (`getNonexistentSibling`, flats in the note name). `applyState` keeps the master volume and both qualities (global settings, not part of a sound), so Generate, Init, undo/redo, presets and category slots never change them |
| `PluginEditor.h/.cpp` | `PsyBoxEditor` (`DragAndDropContainer`: `dragOperationStarted/Ended` set `proc.uiDragSource`) + inner `Root`: TopBar, Sidebar and the pages, switched by the view tabs (`showView`). `updateMode` picks the views and the OSC page's extra (Length / Knock / None) for the category; in Phoscyon mode the first tab is PHOSCYON (`PhosPage`) and FX shows the Phoscyon rack (`phosFx`). Updates the theme accent on category change |

## For AI Agents

### Working In This Directory
- **Thread split.** `processBlock` is the audio thread: relaxed atomic loads, no allocation (MIDI events only `push_back` within reserved capacity). Generator, presets, history and locks are message-thread only. The UI reaches the audio thread only through APVTS, the atomics (`scope`, `outPeak`, `outMeanSquare`, `compMeters`, `arpStep`, `lastPlayedNote`, `lastBpm`) and the `ModView` behind a `SpinLock` the audio thread only try-locks. MIDI: CC 120/123 are tested before other controllers (they are controllers too); controllers never take the last quarter of the reserved event space, so note events always fit.
- Generate (`generateWithSeed`), Mutate and `generateArpPattern` run `psy::levelPatch` on the new patch before `applyState`, so every patch lands on one loudness (Level, `P::PatchGain`; a locked Level stays). Loading presets, sessions or history never re-levels.
- **Patch swaps.** Every swap (Generate, Mutate, undo/redo, presets, init, category switch, a host `setStateInformation`) holds a `PatchChange` (RAII, nests) around its `applyState`. `processBlock` fades the old sound out over 10 ms on the old values, stays silent while `openChanges > 0` (at most a second), then `engine.reset()` and replays what sounded with a ~2.7 ms fade-in: the sustain pedal, the held keys and the keys the pedal held (let go again). Fade lengths follow the sample rate. A block reads the parameters, then checks the epoch again: a change that opened meanwhile fades out on the old values. A change during the fade-in fades out from the current gain. Generate and Mutate compute and level the patch (50-140 ms) *before* opening the change, so the old sound plays on meanwhile. Before 2026-10-17 the restart came after the 10 ms fade regardless, played the old patch again and took the new values mid-note.
- Applying a generated patch goes through `applyState`, which sets APVTS params so the host sees changes. It also enforces the family: `EngineMode` from the category, and `ArpOn` off where `hasArp(category)` is false (basses, kicks, energy; `normaliseFamily` does the same after a state load). Then `pushHistory` and a change broadcast.
- Generate clears typed macro names (the new macros are named automatically); undo/redo and category slots carry them. Older sessions stored "Macro N" for untouched names; `readMacroNames` treats that as automatic.
- State/preset loading maps by param **id** via `findParam`. Unknown ids are ignored, missing ones keep defaults. Keep that tolerance.
- Both files are also compiled into `psybox_snapshot` (it defines `JucePlugin_Name` itself). Don't depend on plugin-wrapper-only macros without a fallback.

### Testing Requirements
- `PSYSNAP_PLUGINTEST=1 ./build/psybox_snapshot_artefacts/Release/psybox_snapshot` runs `tools/ProcessorTest.cpp`: deterministic checks of swaps (host state load without a step, CC 123, sustain, bypass), MIDI floods, the global settings undo/init keep, redo keeping tweaks, slot macro names across a session, offline latency at prepare and the tail. It must print only PASS lines. For anything else, build the plugin target and check in the Standalone app.
- If you touch state save/load: save a preset, reload, and confirm locks, seed and category survive.

## Dependencies

### Internal
- `core/engine/Engine.h`, `core/generator/Generator.h`
- `ui/FxPage.h` (which brings the other panels), `ui/PsyLookAndFeel.h` (editor; one `SharedResourcePointer` look for every open editor, the default look until the last one closes)

### External
- JUCE: `juce_audio_processors`, `juce_audio_utils`, `juce_dsp`

## Manual Notes
