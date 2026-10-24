// Shared by the recipe files (Recipes, JilaxRecipes, GhostRecipes, ArpRecipes):
// the constants and effect blocks they would otherwise each copy. Internal to
// the generator; header-only. Changing a value here moves every recipe that
// uses it, so the seeds of those categories with it.
#pragma once

#include "Generator.h"
#include "dsp/FilterTypes.h"

#include <initializer_list>

namespace psy::recipe
{

// Osc shape indices (OscAShape / OscBShape)
constexpr float kSaw = 2.f, kSquare = 3.f, kTri = 1.f, kSine = 0.f;

// syncDiv indices
enum Div { D4_1, D2_1, D1_1, D1_2, D1_2D, D1_2T, D1_4, D1_4D, D1_4T, D1_8, D1_8D, D1_8T,
           D1_16, D1_16D, D1_16T, D1_32, D1_32T, D1_64 };

inline int ft (FilterType t) { return (int) t; }

// The straight syncDiv that lasts `beats` (a power of two from 1/16 to 16).
inline int divOfBeats (float beats)
{
    for (int i = D4_1; i <= D1_64; ++i)
        if (syncDivBeats (i) == beats) return i;
    return D1_4;
}

// A drawn LFO curve (see dsp::LfoCurve): points of x, y and bend.
struct Pt { float x, y, bend; };
inline void drawLfo (RecipeBuilder& b, int lfo, const Pt* pts, int count)
{
    b.set (lfoParam (lfo, 0), (float) LfoShape::Drawn);
    b.set (lfoPointsParam (lfo), (float) count);
    for (int k = 0; k < count; ++k)
    {
        b.set (lfoPointParam (lfo, k, LfoLane::X), pts[k].x);
        b.set (lfoPointParam (lfo, k, LfoLane::Y), pts[k].y);
        b.set (lfoPointParam (lfo, k, LfoLane::Bend), pts[k].bend);
    }
}
inline void drawLfo (RecipeBuilder& b, int lfo, std::initializer_list<Pt> pts) { drawLfo (b, lfo, pts.begin(), (int) pts.size()); }

inline void fxDelay (RecipeBuilder& b, float p, std::initializer_list<int> divs, float fbLo, float fbHi, float mixLo, float mixHi,
                     float lowCutLo = 150.f, float lowCutHi = 500.f, float highCutLo = 3500.f, float highCutHi = 9000.f)
{
    if (! b.chance (p)) return;
    b.on (P::DelayOn, true);
    b.set (P::DelaySync, 1.f);
    b.pick (P::DelayDiv, divs);
    b.uni (P::DelayFb, fbLo, fbHi);
    b.uni (P::DelayMix, mixLo, mixHi);
    b.on (P::DelayPing, b.chance (0.7f));
    b.uni (P::DelayLowCut, lowCutLo, lowCutHi);
    b.uni (P::DelayHighCut, highCutLo, highCutHi);
}

inline void fxReverb (RecipeBuilder& b, float p, float decLo, float decHi, float mixLo, float mixHi,
                      float sizeLo = 0.4f, float sizeHi = 0.95f, float dampLo = 0.2f, float dampHi = 0.7f,
                      float preLo = 5.f, float preHi = 60.f)
{
    if (! b.chance (p)) return;
    b.on (P::ReverbOn, true);
    b.uni (P::ReverbDecay, decLo, decHi);
    b.uni (P::ReverbMix, mixLo, mixHi);
    b.uni (P::ReverbSize, sizeLo, sizeHi);
    b.uni (P::ReverbDamp, dampLo, dampHi);
    b.uni (P::ReverbPre, preLo, preHi);
}

// The basses' glue: soft knee, gentle ratio, a few dB of gain reduction
// on the body and the make-up that puts them level with the kicks.
inline void fxCompBass (RecipeBuilder& b)
{
    b.on (P::CompOn, true);
    b.set (P::CompMode, (float) CompMode::Normal);
    b.uni (P::CompThresh, -24.f, -20.f);
    b.uni (P::CompRatio, 3.f, 5.f);
    b.uni (P::CompKnee, 6.f, 10.f);
    b.uni (P::CompAttack, 3.f, 8.f);
    b.uni (P::CompRelease, 50.f, 90.f);
    b.set (P::CompMix, 1.f);
    b.uni (P::CompOut, 10.f, 13.f);
}

} // namespace psy::recipe
