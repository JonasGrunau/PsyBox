# resources

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-09-27

## Purpose
Binary assets compiled into the plugin through `juce_add_binary_data(psybox_assets …)` in the root `CMakeLists.txt`. They are accessed at runtime as `BinaryData::…`.

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `fonts/` | Inter and JetBrains Mono TTFs + licenses (see `fonts/AGENTS.md`) |

## For AI Agents

### Working In This Directory
- A new asset only gets embedded once it is listed in `juce_add_binary_data`. Files in this folder are not picked up automatically.

## Manual Notes
