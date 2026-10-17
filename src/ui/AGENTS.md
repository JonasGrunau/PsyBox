# ui

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-10-17

## Purpose
The editor's visual layer, laid out like Serum: view tabs in the top bar (OSC / FX / MATRIX / ARP / GLOBAL; Global always last), mod sources dragged onto knobs, rings around modulated knobs. The design is monochrome: the only colour on screen is the accent of the selected category, used for values, active states and graphs. Active-tab underlines are neutral (`col::text`), never the accent. Every control binds to exactly one parameter through the processor's APVTS.

## Key Files
| File | Description |
|------|-------------|
| `Theme.h/.cpp` | Design tokens: `theme::col::*` colours, `accent()`/`setAccent()`, fonts (Inter for labels, JetBrains Mono for values, from BinaryData), `drawHeading`, spacing, the cell grid, frame sizes `kTopH=52`, `kSideW=196` |
| `PsyLookAndFeel.h/.cpp` | `LookAndFeel_V4` subclass drawing sliders, combos, buttons and popups in theme style |
| `Widgets.h/.cpp` | Parameter-bound controls. `ParamKnob`: right-click lock/reset/modulation menu, alt-click lock, a `DragAndDropTarget` for mod sources when its param is a mod target (`psy::modTargetFor`), one ring per route between body and track (the `proc.uiSelectedSource` ring on top, editable by dragging; its `KnobSlider` leaves that annulus to the knob), dashed ring while `proc.uiDragSource` is set. `ModSourceHandle`: four-way arrow (+ label) that starts a drag (`moddrag::describe`), lit when the source drives something; in tab mode also a clickable tab. `NumberBox`: a value dragged up/down, scrolled or stepped (inline "SEM +7" or cell style with the caption under it; optional gear calling `onGear`; `onActive` while hovered or dragged; double-click resets, right-click locks; takes mod drops when its param is a target). `moddrag::` helpers (description, names, bipolar sources). `Segmented` (`onPick` replaces the plain parameter write, `setVertical` stacks the choices), `ToggleDot`, `ParamCombo`, `AmountSlider`, `TabStrip`, `IconButton` |
| `Panels.h/.cpp` | Shared chrome: `Section` (titled area; `body()` fills the height), `CellGrid`, `KnobSet`, `Sidebar` (one list: a header per category group, the open group's sounds under its header, closed groups show a dot per sound; Generate, Mutate, Tune to key for kicks, Arp for categories with one, with the KEY row under it (`ArpKey` and `ArpScale` combos), seed), `View` enum + `ViewTabs`, `TopBar` (wordmark, view tabs from x = `kSideW`, undo/redo, presets, the output `Scope`, the patch's Level, Vol) |
| `SynthPanels.h/.cpp` | Sections of the OSC view: `OscPanel` (Serum 2 layout: type combo; SEM/FIN `NumberBox`es with Retrig/Sync; the waveform, or `UnisonSettings` while the gear is on; 5 x 2 cells: the type's main knob, UNISON field, Detune, Blend, Width / the type's other knobs or the Table, B's A>B FM and Ring, Level. Panels are 296 wide), `UnisonSettings` (Mode, Stack, Range, Spread and a voice view), `SubPanel`, `NoisePanel`, `FilterPanel`, `ModPanel` (tabs Amp Env, Env 1 (filter), Env 2 (mod), Env 3 (pitch) + LFO 1-3 are drag handles; curve display, knob row with the amp's attack Curve, the SOURCES strip), `MacroPanel` (four rows: knob, automatic or typed name, handle, value), `VoicingPanel` (tall when nothing sits under it: the mode as buttons, Bend and the glide mode in a third row; short: the mode as a menu so Bend fits beside Voices, the glide mode as a header toggle), `SweepDisplay` + `LengthPanel` (energy: Bars/Time via `proc.setEnergyMode`; Length, Delay, Curve, Fade on top, Cut and One Shot beside the sweep picture; the Sweep handle), `KnockPanel` (basses: pitch env quick controls) |
| `FxPage.h/.cpp` | The FX view, laid out like Serum 2's rack: `Chain` (the nine effects in chain order from `FxOrder`; drag a row to move it: it follows the mouse, the other rows ease out of the way (`rowY`, 60 Hz while moving) and leave its landing slot empty with a dashed outline; `onReorder` writes the new order, a row click switches one, hover lights its strip) and one `Strip` per effect (badge with a drawn glyph, controls in cells after an optional stacked `selector`, a display, the mix column, the `Power` switch). The Distortion strip holds Drive, Tone, Type (a stepped knob, like the delay's division), Low Cut and the Auto (Level) toggle. The Compressor strip is taller (two rows, `CompDisplay`); the others carry a read-only `FxDisplay` (transfer curve, crusher steps, shifted partials, phaser response, chorus sweep, echoes in one bar, reverb tail, stereo fan). A switched-off strip greys out. The strips are laid out and numbered in chain order (`orderAttachment` on `FxOrder`, so undo and presets move them too) |
| `Pages.h/.cpp` | The views: `Page` (hairlines between sections), `OscPage` (top: sub/noise, osc A, osc B, filters; bottom: macros, modulators, voicing + `Extra` Length / Knock / None), `KickPage`, `MatrixPage` (16 rows), `GlobalPage` (only the quality: Live and Render), `ArpPage` (settings, step grid with play / note / accent / slide lanes, template cards with a step preview; a note cell drags up or down, scrolls, and double-click resets it; a template replaces the rhythm and keeps the notes) |
| `Displays.h/.cpp` | `DisplayBase` (low-rate polling timer, redraw only on change), `LiveDisplay` (60 Hz while on screen: reads `proc.getModView` and draws a dot per sounding voice on the curve, the newest with a playhead; base of `EnvDisplay`, `PitchEnvDisplay`, `LfoDisplay` and `SweepDisplay`), `WaveDisplay` (every unison voice, or the supersaw's seven saws, drifting at a rate from its detune, 60 Hz while it moves; the voices as lines over the detune range for a moment after a unison change under the mouse, or while `showVoices(true)`), `FilterDisplay` (a `LiveDisplay`: follows the newest voice's `filterNow`, the knob curve faint underneath), `Scope` (the top bar's output window: triggered scope plus a stereo RMS/peak meter with a held peak readout from `proc.takeOutLevel`; dragging it bounces a WAV). Kick views (`KickDisplay` base): `KickPitchDisplay`, `KickEnvDisplay`, `KickClickDisplay`, `KickWaveDisplay`, `KickDriveDisplay`. `CompDisplay`: the compressor's display (transfer curve or bands; thresholds and splits drag with host gestures) |
| `PhosPanels.h/.cpp` | Phoscyon sections, laid out like the original's front panel in PsyBox style: `PhosSynthPanel` (Wave switch + the 13-knob 303 row; two Couple lines over Resonance → Sweep Amt. and Accent → Acc. Vol., LEDs at the ends, lit while coupled), `PhosFilterPanel` (`PhosFilterDisplay`: the 303 response, live cutoff while a note sounds, Hz in the header), `PhosDistortionPanel` (model combo, a sine through the clipper (`phosDriveWave`, shared with the FX strip), Drive/Color/Dynamics, Pre/Post), `PhosCalibrationPanel` (the trimmers + calibration preset combo), `PhosEnvDisplay` (VCA with the accent's extra, MEG). `PhosPage` in `Pages.cpp` stacks them with macros and the modulators |
| `KickPanels.h/.cpp` | Kick sections: `KickPitchPanel` (note readout in the header), `KickAmpPanel`, `KickClickPanel`, `KickDrivePanel`, `KickWavePanel` (full width, length in the header). `KickPage` shows them as the KICK view |

## For AI Agents

### Working In This Directory
- Use `theme::col` tokens and `theme::accent()`. Never hard-code colours or fonts. Accent is message-thread state.
- Bind controls through widgets (`ParamKnob` etc.), which create APVTS attachments by `psy::def(p).id`. Don't call `setValueNotifyingHost` outside a begin/endChangeGesture pair.
- Layout is in fixed logical pixels (editor is 1280×820 and scales as one piece). Don't add DPI or scale logic per component. Pages fill the 1084×768 area right of the sidebar; most sections are `kRowH` (192) tall, the OSC view's halves 384.
- A new modulatable knob only needs its target in `kTargets` (`Params.cpp`): `ParamKnob` then accepts drops and draws rings by itself. New mod sources need a `ModSourceHandle` somewhere.
- Which views and which OSC-page extra a category gets is decided in `PsyBoxEditor::Root::updateMode` (kicks: Kick/FX/Global; Phoscyon: the PHOSCYON page and `FxPage (p, true)`, the scrolling eleven-strip rack; categories with an arp add Arp; energy shows Length, basses Knock).
- `FxPage` reads the order from the parameters themselves (`currentOrder`), not `proc.plain`: an attachment's callback runs before the processor's raw values catch up.
- Panels whose contents depend on a choice (osc type, filter type, LFO/FX page) use a `juce::Timer` to watch it and relayout. Follow that pattern.
- Displays read `proc.plain(P)` and `copyScope`. They must stay cheap and must not lock the audio thread.

### Testing Requirements
- Build `psybox_snapshot` and render PNGs: `./build/psybox_snapshot_artefacts/Release/psybox_snapshot <out-dir> [category-index ...]`. `PSYSNAP_VIEW=n` picks the view, `PSYSNAP_DRAG=1` shows the drag highlight, `PSYSNAP_DROP="7:oscA_level"` drops a source on a knob. Check against the previous render.
- `PSYSNAP_DNDTEST=1 psybox_snapshot /tmp 0` runs the knob drop and ring-drag checks (prints PASS/FAIL).
- Phoscyon is category 38: `psybox_snapshot <dir> 38`, its FX rack with `PSYSNAP_VIEW=1` (`PSYSNAP_STRIP=10` scrolls to the limiter).

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
