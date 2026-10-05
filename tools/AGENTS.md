# tools

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-10-05

## Purpose
Developer executables that let you check sound and UI without a DAW.

## Key Files
| File | Description |
|------|-------------|
| `RenderCategories.cpp` | `psybox_render <out-dir> [seeds-per-category]`: generates patches per category and writes WAVs for listening checks. Energy sounds hold one note for their length (`energy_bars`) plus a tail. Core-only (no JUCE); built when `PSYBOX_BUILD_TESTS=ON` |
| `Snapshot.cpp` | `psybox_snapshot <out-dir> [category-index ...]`: renders the editor offscreen to one PNG per category. `PSYSNAP_VIEW=n` opens a view (0 Osc/Kick, 1 FX, 2 Matrix, 3 Arp, 4 Global), `PSYSNAP_MODTAB=n` a modulator page, `PSYSNAP_TAB=n` switches every tab strip, `PSYSNAP_DRAG=1` shows the drag highlight, `PSYSNAP_DROP="7:oscA_level"` drops mod source 7 on a knob, `PSYSNAP_ARP=1` generates with an arp, `PSYSNAP_PARAMS="comp_on=1,comp_mode=0"` sets plain values after Generate. `PSYSNAP_DNDTEST=1` runs the knob drop and ring-drag checks instead of rendering. Needs JUCE; built when `PSYBOX_BUILD_PLUGIN=ON` |

## For AI Agents

### Working In This Directory
- `psybox_snapshot` compiles the plugin and UI sources directly, not the plugin target. New `src/plugin` or `src/ui` `.cpp` files must be added to its `target_sources` in `CMakeLists.txt` too.
- Use snapshot PNGs to show UI changes. Read the PNG to inspect it.
- Keep `RenderCategories.cpp` JUCE-free so it builds in `build-core`.

### Testing Requirements
- Smoke-run after changes: `./build-core/psybox_render /tmp/psy 1` and check that WAVs appear for all 32 categories (the five kicks play four on the floor on F, F, G and D#; the basses roll or play offbeats on E, E, G, D; energy sounds run their length). With `arp`, basses, kicks and energy sounds keep their own pattern.
- After UI changes: `PSYSNAP_DNDTEST=1 ./build/psybox_snapshot_artefacts/Release/psybox_snapshot /tmp 0` must print only PASS lines.

## Dependencies

### Internal
- `src/core/` (both), `src/plugin/` + `src/ui/` (snapshot)

### External
- JUCE (snapshot only)

## Manual Notes
