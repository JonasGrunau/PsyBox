# PsyBox

**Generated:** 2026-09-27 · **Updated:** 2026-10-05

## Purpose
PsyBox is a psytrance sound-design synthesizer (VST3 / AU / Standalone, JUCE 8). You pick one of 35 categories in six sidebar groups (Main, FX, Energy, Background, Bass, Kick), press **Generate**, and a seeded recipe fills every parameter and names the four macros after what they drive. Each parameter stays editable afterwards in a Serum-style editor (OSC / FX / MATRIX / ARP / GLOBAL views, mod sources dragged onto knobs). Energy sounds run over a length in bars or seconds instead of an arp. The code is split in two. `src/core` is JUCE-free C++20 (DSP, engine, generator) that builds and tests in seconds. `src/plugin` + `src/ui` wrap it in a JUCE processor and editor.

## Key Files
| File | Description |
|------|-------------|
| `CMakeLists.txt` | Targets: `psybox_core` (static lib), `PsyBox` plugin, `psybox_snapshot`, `psybox_tests`, `psybox_render`. Options `PSYBOX_BUILD_PLUGIN`, `PSYBOX_BUILD_TESTS`, `PSYBOX_COPY_PLUGIN` (off in CI), cache var `PSYBOX_VERSION` (release tag sets it). Static MSVC runtime |
| `README.md` | User-facing feature list, DSP summary, build commands |
| `.gitignore` | Ignores `build*/`, `.DS_Store`, `.omc/`, `.cache/` |
| `CLAUDE.md` | `@AGENTS.md` import so stock Claude Code loads this file |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `src/` | All product source (see `src/AGENTS.md`) |
| `tests/` | Catch2 unit tests for the core (see `tests/AGENTS.md`) |
| `tools/` | Offline WAV renderer and editor PNG snapshot tool (see `tools/AGENTS.md`) |
| `resources/` | Bundled fonts compiled into the plugin (see `resources/AGENTS.md`) |
| `packaging/` | Installers: `macos/build-pkg.sh` (pkgbuild + productbuild, one choice per format) and `windows/PsyBox.iss` (Inno Setup) |
| `.github/workflows/` | `release.yml`: a `v*` tag builds the universal macOS pkg + VST3/AU zip and the Windows setup + VST3 zip, then creates the GitHub release |

Not source: `build/` (plugin build), `build-core/` (core-only build), `.cache/clangd/` (clangd index, ignored), `.omc/`.

## For AI Agents

### Working In This Directory
- **Core-only loop is fastest.** Use it for any DSP/engine/generator change:
  ```sh
  cmake -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSYBOX_BUILD_PLUGIN=OFF
  cmake --build build-core && ./build-core/psybox_tests
  ```
- Full plugin: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`. The build copies the plugin into `~/Library/Audio/Plug-Ins` (`COPY_PLUGIN_AFTER_BUILD`). JUCE 8.0.15 and Catch2 v3.7.1 come in through FetchContent. Pass `-DFETCHCONTENT_SOURCE_DIR_JUCE=...` to use a local JUCE checkout.
- New `.cpp` files must be added to `CMakeLists.txt` by hand (no globbing). UI/plugin sources are listed **twice**: once for `PsyBox` and once for `psybox_snapshot`.
- Never include JUCE from `src/core`. That boundary is the reason the core target exists.
- Don't edit `build*/` or `.cache/`.

### Testing Requirements
- Run `./build-core/psybox_tests` after every core change. Filter by tag, e.g. `psybox_tests "[filter]"`.
- UI changes: build `psybox_snapshot` and render PNGs (see `tools/AGENTS.md`). You can review the UI without a DAW.
- Sound changes: `./build-core/psybox_render <out-dir> 3` writes WAVs per category.

### Common Patterns
- Namespaces: `psy` (engine/generator), `psy::dsp` (DSP), `theme` (UI tokens). Plugin/UI classes are global.
- JUCE-style formatting: space before call parens `foo (x)`, 4-space indent, braces on their own line, `noexcept` on audio-path methods.
- Parameters are defined once in the X-macro `PSY_PARAMS` in `src/core/engine/Params.h`. Engine, generator and JUCE layout all derive from it.

## Dependencies

### External
- JUCE 8.0.15 (AGPLv3): plugin, GUI, `juce_dsp`
- Catch2 v3.7.1: tests
- C++20, CMake ≥ 3.24, Ninja, macOS deployment target 11.0

## Manual Notes

Notes under this heading are written by humans and preserved on regeneration.
