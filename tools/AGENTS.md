# tools

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-10-18

## Purpose
Developer executables that let you check sound and UI without a DAW.

## Key Files
| File | Description |
|------|-------------|
| `RenderCategories.cpp` | `psybox_render <out-dir> [seeds-per-category] [arp]`: creates the directory, generates patches per category, levels them like the plugin's Generate (`levelPatch`), and writes WAVs for listening checks. Energy sounds hold one note for their length (`energy_bars`) plus a tail. Exits 1 on a bad argument (seeds must be 1 or more) or a file it can't write. Core-only (no JUCE); built when `PSYBOX_BUILD_TESTS=ON` |
| `Snapshot.cpp` | `psybox_snapshot <out-dir> [category-index ...]`: renders the editor offscreen to one PNG per category. `PSYSNAP_VIEW=n` opens a view (0 Osc/Kick, 1 FX, 2 Matrix, 3 Arp, 4 Global), `PSYSNAP_MODTAB=n` a modulator page, `PSYSNAP_TAB=n` switches every tab strip, `PSYSNAP_DRAG=1` shows the drag highlight, `PSYSNAP_DROP="7:oscA_level"` drops mod source 7 on a knob, `PSYSNAP_ARP=1` generates with an arp, `PSYSNAP_PARAMS="comp_on=1,comp_mode=0"` sets plain values after Generate. `PSYSNAP_UNISON=1` opens Osc A's unison settings (gear), `=2` holds its voice lines up. `PSYSNAP_SWAPTEST=<out.f32>` plays a held key through the first category's arp in real time on a second thread while Generate runs twice (at 1.0 s and 2.3 s), writes the mono output and prints where each Generate came in (no editor; check the swaps with a click detector). `PSYSNAP_STRIP=n` (with `PSYSNAP_VIEW=1`) scrolls the Phoscyon FX rack to effect n. `PSYSNAP_CHAINDRAG=n` (with `PSYSNAP_VIEW=1`) holds the chain's first row dragged to row n. `PSYSNAP_DNDTEST=1` runs the knob drop (and SEM field drop), ring-drag and FX chain drag/click checks instead of rendering. `PSYSNAP_LOCKS=1` locks two parameters, `PSYSNAP_KEYS=1` renders the standalone's keyboard strip, `PSYSNAP_SCALE=1` renders at 1x instead of 2x. `PSYSNAP_PLUGINTEST=1` runs `ProcessorTest.cpp` instead. Relative paths are taken from the current directory. Needs JUCE; built when `PSYBOX_BUILD_PLUGIN=ON` |
| `ProcessorTest.cpp` | `runProcessorTests()`, linked into `psybox_snapshot`: deterministic checks of the JUCE processor that `psybox_tests` can't reach (patch swaps from a host state load without a step, CC 123 forgetting the keys, sustain across a swap, host bypass, a MIDI flood keeping note-offs, master volume through Init/undo/redo, redo keeping tweaks, slot macro names across a session, every continuous parameter handing the host back exactly the value it set (auval), the WAV bounce on another thread and its cancel, two open editors each drawing their own accent, offline latency at prepare, the tail length). Prints PASS/FAIL, exits 1 on a failure |

## For AI Agents

### Working In This Directory
- `psybox_snapshot` compiles the plugin and UI sources directly, not the plugin target. New `src/plugin` or `src/ui` `.cpp` files must be added to its `target_sources` in `CMakeLists.txt` too.
- Use snapshot PNGs to show UI changes. Read the PNG to inspect it.
- Keep `RenderCategories.cpp` JUCE-free so it builds in `build-core`.

### Testing Requirements
- Smoke-run after changes: `./build-core/psybox_render /tmp/psy 1` and check that WAVs appear for all 44 categories (the five kicks play four on the floor on F, F, G and D#; the basses roll or play offbeats on E, E, G, D, the reverse basses play the beat before every other kick; Jilax taps a shot every two beats on E3, E3, G3, D3 and One Shot plays each in full; Grid plays an offbeat 16th groove on E4, E4, G4, D4, or with `arp` holds E4 alone; Ghost Stab plays every offbeat on G#3, G#3, B3, F#3 held to just before the kick, or with `arp` holds G#3 alone; Ghost Bass and Jilax Bass roll like Prog Bass; Psy Arp and FM play a 16th line over an E2 pedal, or with `arp` hold E3 alone; Pad holds E minor then C major for two bars each; Perc hits every 16th on C4, the offbeats loudest; energy sounds run their length). With `arp`, basses, kicks and energy sounds keep their own pattern.
- After UI changes: `PSYSNAP_DNDTEST=1 ./build/psybox_snapshot_artefacts/Release/psybox_snapshot /tmp 0` must print only PASS lines (and the macro names).
- After processor changes: `PSYSNAP_PLUGINTEST=1 ./build/psybox_snapshot_artefacts/Release/psybox_snapshot` must print only PASS lines.

## Dependencies

### Internal
- `src/core/` (both), `src/plugin/` + `src/ui/` (snapshot)

### External
- JUCE (snapshot only)

## Manual Notes
