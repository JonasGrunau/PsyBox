# generator

**Parent context:** `../AGENTS.md`
**Generated:** 2026-09-27 · **Updated:** 2026-09-27

## Purpose
The "press Generate" feature. Each `Category` has a recipe that describes the *space* of that sound: which modules to use, and value ranges that keep it recognisably acid, zap, etc. `Generator` runs recipes deterministically from a seed, respects locked parameters, and records the ranges so **Mutate** nudges values without leaving the category.

## Key Files
| File | Description |
|------|-------------|
| `Category.h` | `enum class Category` (23 synth + 4 bass + 5 kick + `Count`, new ones appended: saved sessions store the index), `categoryInfo()`: name, short name, sidebar section (`CategoryGroup`), accent colour `0xRRGGBB`, blurb. `CategoryGroup` (Main / Shots / FX / Energy / Background for synths, then Bass, Kick) and `categoryGroupName()`. `categoryDisplayOrder()` sorts by section, then name. `Family` (Synth / Bass / Kick), `familyOf()` (the kick block plus the listed basses), `hasArp(family)` (synth only), `categoriesIn(family)` (sidebar order), `familySize()` |
| `Generator.h` | `LockSet` (bitset over params), `RangeMemory`, `RecipeBuilder` (the recipe API: `set`, `uni`, `pick`, `among`, `on`, `chance`, `route`, `lfo`, `lfoSync`), `recipeFor()`, `Generator::generate/mutate` |
| `Generator.cpp` | `RecipeBuilder` implementation (skips locked params, records ranges) and `Generator` |
| `Recipes.cpp` | One `static void` recipe per category, plus the `recipeFor()` dispatch table. Largest core file |
| `KickRecipes.cpp` | `kickRecipeFor()`: the five kick recipes. They only touch `Kick*` params, `VelSens` and a little global FX. `amp()` runs before `pitch()`, which caps Sweep so the body lands on the key while it is still loud |
| `ArpRecipes.cpp` | Per synth category an arp settings recipe (`arpRecipeFor()`) and a step-pattern recipe (`arpPatternRecipeFor()`, also used by `Generator::generateArpPattern` for the editor's Generate), plus `arpTemplates()`. Basses and kicks map to the empty `noArp`. Runs when `generate(..., withArp)` is set for a synth category, on its own RNG stream, and may only touch `Arp` params, so the sound for a seed never depends on the arp (tested) |

## For AI Agents

### Working In This Directory
- **Determinism is a feature.** The same category + seed + locks must give the same patch (users type seeds back in). Use only the builder's `rng` (`chance`, `rnd`, `uni`, `pick`). No `std::random_device`, time or static state.
- **Locks must not shift the RNG stream.** Locking one param must not reshuffle the others (tested). The builder still consumes randomness for locked params. Keep that when adding builder methods.
- Always write params through `RecipeBuilder` (`set`/`uni`/`pick`/`among`). Writing `state()` directly skips locks and range recording, so Mutate won't touch that param.
- `uni` samples in the parameter's normalised curve (log for Hz, power for times). Give ranges in plain units.
- **Kicks.** `generate()` sets `EngineMode` from the category after the recipes, ignoring locks, and `mutate` never touches it. `generate(..., kickToKey)` sets `KickKeyTrack` for kick categories without drawing randomness, so a seed gives the same kick with the toggle on or off. A new kick needs a `familyOf` case, a recipe in `KickRecipes.cpp` and cases in `recipeFor()`, `arpRecipeFor()` and `arpPatternRecipeFor()` (the arp ones return `noArp`).
- **Basses.** Psy Bass (enum `PsyBass`, index 4, formerly Rolling Bass), Bass Shot, Offbeat Bass and Prog Bass (`Family::Bass`) play the synth engine but, like kicks, never get an arp: `generate()` drops `withArp` for families without `hasArp`, and the processor forces `ArpOn` off for them on every state it applies. Their recipes (`Recipes.cpp`) were fitted to reference samples: harmonics h2-h8 in windows over the note, pitch knock (`bassKnock`), envelope, crest, level. They end in `fxCompBass`, a glue comp whose make-up puts them level with the kicks. Amp attacks start at `kBassAttackLo` (1.5 ms): an instant attack clicks at every note start and on retriggers out of a release tail.
- Adding a category: append to `Category` (never reorder: saved sessions store the index; every family goes at the end), add a `familyOf` case for a bass or kick, add a `categoryInfo()` row with its section (`CategoryGroup`), write a sound recipe (`Recipes.cpp`) plus, for a synth sound, an arp settings and an arp pattern recipe (`ArpRecipes.cpp`), and register all three in `recipeFor()`, `arpRecipeFor()` and `arpPatternRecipeFor()`. `-Wswitch` flags a missing case. Re-space the synth accents so the rainbow stays even: in OKLCH, hue runs 25°→335° in equal steps down the synth list in sidebar order (sections, then A–Z), with L and C following the current palette's curve for that hue. Bass and kick hues sit on the half steps of that grid (none shared between the two families), or some will land on a synth hue; within each tab the hue must still rise down the list, starting warm like the synth tab (`[category]` tests check both). The UI sidebar scrolls and the tests iterate `kNumCategories`, so they need no change; add sustained sounds to the `sustained` list in `TestEngine.cpp` and to the long-note patterns in `tools/RenderCategories.cpp`.

### Testing Requirements
- `psybox_tests "[generator]"`: determinism, lock survival, lock-independence, mutate stays in range.
- `"[engine][generator]"`: every category renders playable audio.
- `"[generator][kick]"`: the engine mode follows the category (even when locked), kick and synth patches keep to their own params, the key toggle only flips key tracking. `"[generator][arp]"`: basses and kicks never get an arp. `"[kick][generator]"` in `TestKick.cpp`: every generated kick lands on the played key while it is still loud.
- Listen: `./build-core/psybox_render out 3`.

## Dependencies

### Internal
- `../engine/Params.h`: `P`, `ParamState`, `ModSrc`/`ModDst`, `LfoShape`
- `../dsp/Common.h`: `Rng`

## Manual Notes
