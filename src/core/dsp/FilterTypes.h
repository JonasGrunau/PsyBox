#pragma once

namespace psy
{
// Filter slot types, index-aligned with choices::filterType. Grouped by
// family; the slope variants give different cutoff curves for the same knob.
enum class FilterType : int
{
    Off,
    LP6, LP12, LP18, LP24,
    HP6, HP12, HP18, HP24,
    BP12, BP24, Notch,
    LadderLP12, Ladder24, LadderHP, LadderBP,
    Diode303, Formant, CombPos, CombNeg,
    Count
};
} // namespace psy
