#pragma once

// Every patch at about the same loudness. A patch is played offline the way
// its category is usually played (its audition), its loudest moment is
// measured, and Level (P::PatchGain) is set so that moment lands on one
// target. Generate, Mutate and a new arp pattern level the patch they make;
// a locked Level stays.

#include "Category.h"
#include "Generator.h"
#include "engine/Params.h"

namespace psy
{

// Where every patch's loudest moment lands, in LUFS (before Volume).
constexpr float kTargetLoudness = -16.f;   // where the kicks and basses already sit

// The loudest 400 ms of the patch's audition, K-weighted (ITU-R BS.1770
// momentary loudness), in LUFS, with Level and Volume at 0 dB. The
// audition covers the ways a sound is usually played, and the loudest wins:
// kicks four to the floor on F1; an energy sound held for its length (at most
// two bars, the sweep shrinks with it); anything else a key held for two
// bars and a bar of 16ths, each from C2 to C5 (basses E1 to E3, acid and
// squelch and Phoscyon C1 to C3), plus the arp running when it has one.
float auditionLoudness (const ParamState& patch, Category c);

// Sets Level so the audition lands on kTargetLoudness (within the param's
// range). Leaves a locked Level alone, and a silent patch (below -70 LUFS)
// at the gain it has.
void levelPatch (ParamState& patch, Category c, const LockSet& locks);

} // namespace psy
