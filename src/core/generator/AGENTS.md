# generator

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-09-27

## Purpose
The "press Generate" feature. Each `Category` has a recipe that describes the *space* of that sound: which modules to use, and value ranges that keep it recognisably acid, zap, etc. `Generator` runs recipes deterministically from a seed, respects locked parameters, and records the ranges so **Mutate** nudges values without leaving the category.

## Key Files
| File | Description |
|------|-------------|
| `Category.h` | `enum class Category` (24 + `Count`, new ones appended: saved sessions store the index) and `categoryInfo()`: name, short name, accent colour `0xRRGGBB`, blurb |
| `Generator.h` | `LockSet` (bitset over params), `RangeMemory`, `RecipeBuilder` (the recipe API: `set`, `uni`, `pick`, `among`, `on`, `chance`, `route`, `lfo`, `lfoSync`), `recipeFor()`, `Generator::generate/mutate` |
| `Generator.cpp` | `RecipeBuilder` implementation (skips locked params, records ranges) and `Generator` |
| `Recipes.cpp` | One `static void` recipe per category, plus the `recipeFor()` dispatch table. Largest core file |
| `ArpRecipes.cpp` | Per category an arp settings recipe (`arpRecipeFor()`) and a step-pattern recipe (`arpPatternRecipeFor()`, also used by `Generator::generateArpPattern` for the editor's Generate), plus `arpTemplates()`. Runs when `generate(..., withArp)` is set, on its own RNG stream, and may only touch `Arp` params, so the sound for a seed never depends on the arp (tested) |

## For AI Agents

### Working In This Directory
- **Determinism is a feature.** The same category + seed + locks must give the same patch (users type seeds back in). Use only the builder's `rng` (`chance`, `rnd`, `uni`, `pick`). No `std::random_device`, time or static state.
- **Locks must not shift the RNG stream.** Locking one param must not reshuffle the others (tested). The builder still consumes randomness for locked params. Keep that when adding builder methods.
- Always write params through `RecipeBuilder` (`set`/`uni`/`pick`/`among`). Writing `state()` directly skips locks and range recording, so Mutate won't touch that param.
- `uni` samples in the parameter's normalised curve (log for Hz, power for times). Give ranges in plain units.
- Adding a category: append to `Category` (never reorder: saved sessions store the index), add a `categoryInfo()` row, write a sound recipe (`Recipes.cpp`) plus an arp settings and an arp pattern recipe (`ArpRecipes.cpp`), and register all three in `recipeFor()`, `arpRecipeFor()` and `arpPatternRecipeFor()`. `-Wswitch` flags a missing case. Re-space every accent so the rainbow stays even: in OKLCH, hue runs 25°→335° in equal steps down the alphabetical list, with L and C following the current palette's curve for that hue. The UI sidebar scrolls and the tests iterate `kNumCategories`, so they need no change; add sustained sounds to the `sustained` list in `TestEngine.cpp` and to the long-note patterns in `tools/RenderCategories.cpp`.

### Testing Requirements
- `psysynth_tests "[generator]"`: determinism, lock survival, lock-independence, mutate stays in range.
- `"[engine][generator]"`: every category renders playable audio.
- Listen: `./build-core/psysynth_render out 3`.

## Dependencies

### Internal
- `../engine/Params.h`: `P`, `ParamState`, `ModSrc`/`ModDst`, `LfoShape`
- `../dsp/Common.h`: `Rng`

## Manual Notes
