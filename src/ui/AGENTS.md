# ui

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-09-27

## Purpose
The editor's visual layer. The design is monochrome: the only colour on screen is the accent of the selected category, used for values, active states and graphs. Every control binds to exactly one parameter through the processor's APVTS.

## Key Files
| File | Description |
|------|-------------|
| `Theme.h/.cpp` | Design tokens: `theme::col::*` colours, `accent()`/`setAccent()`, fonts (Inter for labels, JetBrains Mono for values, from BinaryData), `drawHeading`, `kPad=14`, `kHeaderH=26` |
| `PsyLookAndFeel.h/.cpp` | `LookAndFeel_V4` subclass drawing sliders, combos, buttons and popups in theme style |
| `Widgets.h/.cpp` | Parameter-bound controls: `ParamKnob` (right-click lock/reset, alt-click lock), `Segmented`, `ToggleDot`, `ParamCombo`, `AmountSlider`, plus `TabStrip`, `IconButton` |
| `Panels.h/.cpp` | `Section` base (titled area) and all panels: `OscPanel`, `MixerPanel`, `FilterPanel`, `VoicePanel`, `EnvPanel`, `LfoPanel`, `ModMatrixPanel`, `FxPanel`, `MacroPanel`, `OutputPanel`, `Sidebar` (SYNTH / KICK family tabs as a view filter, category list, Generate, Mutate, Tune to key (kick categories only), Arp, seed), `TopBar` (generate/mutate/seed/presets/undo). Largest UI file |
| `Displays.h/.cpp` | `DisplayBase` (low-rate polling timer, redraw only on change) and `WaveDisplay`, `FilterDisplay`, `EnvDisplay`, `PitchEnvDisplay`, `LfoDisplay`, `Scope`. Kick views (`KickDisplay` base): `KickPitchDisplay` (analytic sweep, log Hz), `KickEnvDisplay`, `KickClickDisplay` and `KickWaveDisplay` (render a real hit with `KickVoice::renderOffline` for the last played key, cached by fingerprint), `KickDriveDisplay` (transfer curve). `CompDisplay`: the compressor page's display. Normal shows the transfer curve, Multi the bands on a log-frequency axis. Thresholds and band splits drag directly with host gestures; meters come from `proc.getCompReading` |
| `KickPanels.h/.cpp` | Kick sections: `KickPitchPanel` (note readout in the header), `KickAmpPanel`, `KickClickPanel`, `KickDrivePanel`, `KickWavePanel` (full width, length in the header). The editor's `Root` shows them instead of rows 0-2 when `EngineMode` is Kick; `VoicePanel::setKickMode` keeps only its Arp page; Macros are dimmed |

## For AI Agents

### Working In This Directory
- Use `theme::col` tokens and `theme::accent()`. Never hard-code colours or fonts. Accent is message-thread state.
- Bind controls through widgets (`ParamKnob` etc.), which create APVTS attachments by `psy::def(p).id`. Don't call `setValueNotifyingHost` outside a begin/endChangeGesture pair.
- Layout is in fixed logical pixels (editor is 1280×820 and scales as one piece). Don't add DPI or scale logic per component.
- Panels whose contents depend on a choice (osc type, filter type, LFO/FX page) use a `juce::Timer` to watch it and relayout. Follow that pattern.
- Displays read `proc.plain(P)` and `copyScope`. They must stay cheap and must not lock the audio thread.

### Testing Requirements
- Build `psysynth_snapshot` and render PNGs: `./build/psysynth_snapshot_artefacts/Release/psysynth_snapshot <out-dir> [category-index ...]`. Check against the previous render.

### Common Patterns
- Each panel constructor takes `PsySynthProcessor&` and builds `KnobSet`s of `ParamKnob`s for its parameter group.
- `Sidebar`/`TopBar` are `ChangeListener`s on the processor, so they refresh on generate/preset/category changes.

## Dependencies

### Internal
- `plugin/PluginProcessor.h`: parameter access, generator actions, GUI feeds
- `core/engine/Params.h`, `core/generator/Category.h`: ids, formatting, category colours
- `resources/fonts/` via `BinaryData` (`psysynth_assets`)

### External
- JUCE `juce_gui_basics`

## Manual Notes
