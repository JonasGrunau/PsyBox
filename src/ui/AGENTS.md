# ui

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-10-05

## Purpose
The editor's visual layer, laid out like Serum: view tabs in the top bar (OSC / FX / MATRIX / ARP / GLOBAL; Global always last), mod sources dragged onto knobs, rings around modulated knobs. The design is monochrome: the only colour on screen is the accent of the selected category, used for values, active states and graphs. Active-tab underlines are neutral (`col::text`), never the accent. Every control binds to exactly one parameter through the processor's APVTS.

## Key Files
| File | Description |
|------|-------------|
| `Theme.h/.cpp` | Design tokens: `theme::col::*` colours, `accent()`/`setAccent()`, fonts (Inter for labels, JetBrains Mono for values, from BinaryData), `drawHeading`, spacing, the cell grid, frame sizes `kTopH=52`, `kSideW=196` |
| `PsyLookAndFeel.h/.cpp` | `LookAndFeel_V4` subclass drawing sliders, combos, buttons and popups in theme style |
| `Widgets.h/.cpp` | Parameter-bound controls. `ParamKnob`: right-click lock/reset/modulation menu, alt-click lock, a `DragAndDropTarget` for mod sources when its param is a mod target (`psy::modTargetFor`), one ring per route between body and track (the `proc.uiSelectedSource` ring on top, editable by dragging; its `KnobSlider` leaves that annulus to the knob), dashed ring while `proc.uiDragSource` is set. `ModSourceHandle`: four-way arrow (+ label) that starts a drag (`moddrag::describe`), lit when the source drives something; in tab mode also a clickable tab. `moddrag::` helpers (description, names, bipolar sources). `Segmented` (`onPick` replaces the plain parameter write), `ToggleDot`, `ParamCombo`, `AmountSlider`, `TabStrip`, `IconButton` |
| `Panels.h/.cpp` | Shared chrome: `Section` (titled area; `body()` fills the height), `CellGrid`, `KnobSet`, `Sidebar` (one list: a header per category group, the open group's sounds under its header, closed groups show a dot per sound; Generate, Mutate, Tune to key for kicks, Arp for categories with one, seed), `View` enum + `ViewTabs`, `TopBar` (wordmark, view tabs from x = `kSideW`, undo/redo, presets, the output `Scope`, Vol) |
| `SynthPanels.h/.cpp` | Sections of the OSC view: `OscPanel` (type combo, waveform, Retrig/Sync/Table row, 4 x 2 knobs: Level first, then the type's knobs; Coarse, Fine and B's A>B FM and Ring), `SubPanel`, `NoisePanel`, `FilterPanel`, `ModPanel` (tabs Amp/Filter/Mod/Pitch Env + LFO 1-3 are drag handles; curve display, knob row, the SOURCES strip), `MacroPanel` (four rows: knob, automatic or typed name, handle, value), `VoicingPanel` (tall when nothing sits under it: adds Bend and the glide mode), `SweepDisplay` + `LengthPanel` (energy: Bars/Time via `proc.setEnergyMode`, Length, Curve, Fade, Cut, the Sweep handle), `KnockPanel` (basses: pitch env quick controls) |
| `Pages.h/.cpp` | The views: `Page` (hairlines between sections), `OscPage` (top: sub/noise, osc A, osc B, filters; bottom: macros, modulators, voicing + `Extra` Length / Knock / None), `KickPage`, `FxModule` + `FxPage` (nine modules in signal order, 3 rows; a switched-off module greys out; Compressor with `CompDisplay`), `MatrixPage` (16 rows), `GlobalPage` (voice, quality, output, the guide as cards), `ArpPage` (settings, step grid with play / note / accent / slide lanes, template cards with a step preview; a note cell drags up or down, scrolls, and double-click resets it; a template replaces the rhythm and keeps the notes) |
| `Displays.h/.cpp` | `DisplayBase` (low-rate polling timer, redraw only on change) and `WaveDisplay`, `FilterDisplay`, `EnvDisplay`, `PitchEnvDisplay`, `LfoDisplay`, `Scope` (the top bar's output window: triggered scope plus a stereo RMS/peak meter with a held peak readout from `proc.takeOutLevel`; dragging it bounces a WAV). Kick views (`KickDisplay` base): `KickPitchDisplay`, `KickEnvDisplay`, `KickClickDisplay`, `KickWaveDisplay`, `KickDriveDisplay`. `CompDisplay`: the compressor's display (transfer curve or bands; thresholds and splits drag with host gestures) |
| `KickPanels.h/.cpp` | Kick sections: `KickPitchPanel` (note readout in the header), `KickAmpPanel`, `KickClickPanel`, `KickDrivePanel`, `KickWavePanel` (full width, length in the header). `KickPage` shows them as the KICK view |

## For AI Agents

### Working In This Directory
- Use `theme::col` tokens and `theme::accent()`. Never hard-code colours or fonts. Accent is message-thread state.
- Bind controls through widgets (`ParamKnob` etc.), which create APVTS attachments by `psy::def(p).id`. Don't call `setValueNotifyingHost` outside a begin/endChangeGesture pair.
- Layout is in fixed logical pixels (editor is 1280×820 and scales as one piece). Don't add DPI or scale logic per component. Pages fill the 1084×768 area right of the sidebar; most sections are `kRowH` (192) tall, the OSC view's halves 384.
- A new modulatable knob only needs its target in `kTargets` (`Params.cpp`): `ParamKnob` then accepts drops and draws rings by itself. New mod sources need a `ModSourceHandle` somewhere.
- Which views and which OSC-page extra a category gets is decided in `PsyBoxEditor::Root::updateMode` (kicks: Kick/FX/Global; categories with an arp add Arp; energy shows Length, basses Knock).
- Panels whose contents depend on a choice (osc type, filter type, LFO/FX page) use a `juce::Timer` to watch it and relayout. Follow that pattern.
- Displays read `proc.plain(P)` and `copyScope`. They must stay cheap and must not lock the audio thread.

### Testing Requirements
- Build `psybox_snapshot` and render PNGs: `./build/psybox_snapshot_artefacts/Release/psybox_snapshot <out-dir> [category-index ...]`. `PSYSNAP_VIEW=n` picks the view, `PSYSNAP_DRAG=1` shows the drag highlight, `PSYSNAP_DROP="7:oscA_level"` drops a source on a knob. Check against the previous render.
- `PSYSNAP_DNDTEST=1 psybox_snapshot /tmp 0` runs the knob drop and ring-drag checks (prints PASS/FAIL).

### Common Patterns
- Each panel constructor takes `PsyBoxProcessor&` and builds `KnobSet`s of `ParamKnob`s for its parameter group.
- Editor-only state shared by widgets lives on the processor as plain members (`uiSelectedSource`, `uiDragSource`), never in statics (several plugin instances share statics).
- `Sidebar`/`TopBar` are `ChangeListener`s on the processor, so they refresh on generate/preset/category changes.

## Dependencies

### Internal
- `plugin/PluginProcessor.h`: parameter access, generator actions, GUI feeds
- `core/engine/Params.h`, `core/generator/Category.h`: ids, formatting, category colours
- `resources/fonts/` via `BinaryData` (`psybox_assets`)

### External
- JUCE `juce_gui_basics`

## Manual Notes
