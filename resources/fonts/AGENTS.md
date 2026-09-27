# fonts

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-09-27

## Purpose
UI typefaces loaded by `src/ui/Theme.cpp`: Inter for labels and headings, JetBrains Mono for values.

## Key Files
| File | Description |
|------|-------------|
| `Inter-Regular.ttf`, `Inter-Medium.ttf`, `Inter-SemiBold.ttf` | Label / heading weights |
| `JetBrainsMono-Regular.ttf`, `JetBrainsMono-Medium.ttf` | Value readouts |
| `Inter-LICENSE.txt`, `JetBrainsMono-OFL.txt` | SIL OFL licenses. Must ship with the fonts |

## For AI Agents

### Working In This Directory
- Renaming a font changes its `BinaryData` symbol. Update `Theme.cpp` and `CMakeLists.txt` together.
- Keep the license files next to the fonts.

## Manual Notes
