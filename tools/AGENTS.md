# tools

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-09-27

## Purpose
Developer executables that let you check sound and UI without a DAW.

## Key Files
| File | Description |
|------|-------------|
| `RenderCategories.cpp` | `psybox_render <out-dir> [seeds-per-category]`: generates patches per category and writes WAVs for listening checks. Core-only (no JUCE); built when `PSYBOX_BUILD_TESTS=ON` |
| `Snapshot.cpp` | `psybox_snapshot <out-dir> [category-index ...]`: renders the editor offscreen to one PNG per category. `PSYSNAP_TAB=n` switches every tab strip, `PSYSNAP_PARAMS="comp_on=1,comp_mode=0"` sets plain values after Generate. Needs JUCE; built when `PSYBOX_BUILD_PLUGIN=ON` |

## For AI Agents

### Working In This Directory
- `psybox_snapshot` compiles the plugin and UI sources directly, not the plugin target. New `src/plugin` or `src/ui` `.cpp` files must be added to its `target_sources` in `CMakeLists.txt` too.
- Use snapshot PNGs to show UI changes. Read the PNG to inspect it.
- Keep `RenderCategories.cpp` JUCE-free so it builds in `build-core`.

### Testing Requirements
- Smoke-run after changes: `./build-core/psybox_render /tmp/psy 1` and check that WAVs appear for all 32 categories (the five kicks play four on the floor on F, F, G and D#; the basses roll or play offbeats on E, E, G, D). With `arp`, basses and kicks keep their own pattern.

## Dependencies

### Internal
- `src/core/` (both), `src/plugin/` + `src/ui/` (snapshot)

### External
- JUCE (snapshot only)

## Manual Notes
