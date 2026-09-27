# core

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-09-27

## Purpose
The `psybox_core` static library: plain C++20, no JUCE. It contains the DSP building blocks, the polyphonic engine that plays them, and the category generator that writes patches. It is also the include root for the target, so headers are referenced as `"dsp/…"`, `"engine/…"`, `"generator/…"`.

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `dsp/` | Header-mostly DSP primitives: oscillators, filters, envelopes, FX, oversampling (see `dsp/AGENTS.md`) |
| `engine/` | Parameter table, voices, engine (MIDI → audio) (see `engine/AGENTS.md`) |
| `generator/` | Categories, recipe builder, per-category recipes (see `generator/AGENTS.md`) |

## For AI Agents

### Working In This Directory
- **No JUCE, no GUI types.** Standard library only.
- Compiled with `-Wall -Wextra -Wno-unused-parameter`. Keep it warning-free.
- Layering inside core: `dsp` depends on nothing, `engine` depends on `dsp`, and `generator` depends on `engine/Params.h` + `dsp/Common.h`.
- Everything on the audio path must be real-time safe. No allocation, locks or I/O in `process`/`render`. Buffers get sized in `prepare`.

### Testing Requirements
- Build with `-DPSYBOX_BUILD_PLUGIN=OFF` and run `psybox_tests` (see `../../tests/AGENTS.md`).

## Dependencies

### External
- C++20 standard library only

## Manual Notes
