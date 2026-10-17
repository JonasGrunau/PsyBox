# src

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-09-27

## Purpose
All product source, in three layers. `core` knows nothing about JUCE and depends on nothing above it. `plugin` owns the JUCE `AudioProcessor` and bridges parameters/MIDI into the core engine. `ui` holds the editor components, which talk to the processor. `plugin` and `ui` depend on each other (`PluginEditor.h` includes `ui/FxPage.h`; the widgets and displays include `PluginProcessor.h`), so they build together.

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `core/` | JUCE-free DSP, engine and generator (see `core/AGENTS.md`) |
| `plugin/` | JUCE processor + editor shell (see `plugin/AGENTS.md`) |
| `ui/` | Theme, look-and-feel, widgets, panels, displays (see `ui/AGENTS.md`) |

## For AI Agents

### Working In This Directory
- Include paths differ by target. `psybox_core` exposes `src/core`, so core code includes `"engine/Params.h"` or `"dsp/Filters.h"`. Plugin/UI targets add `src`, so they include `"ui/Panels.h"` and also reach core headers via `src/core`.
- Nothing in `core` includes from `plugin` or `ui`. `plugin` and `ui` include each other and the core headers; displays use core DSP directly (`dsp/*`, `KickVoice.h`, `PhosVoice.h`, `PhosFx.h`) to draw curves.

### Common Patterns
- Data flow: `ParamDef` table (core) → APVTS layout (plugin) → widget attachments (ui). The audio thread copies raw APVTS atomics into a `ParamState` each block.

## Dependencies

### External
- JUCE 8 (plugin + ui only)

## Manual Notes
