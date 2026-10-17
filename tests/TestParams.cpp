#include "engine/Params.h"

#include <catch2/catch_test_macros.hpp>

using namespace psy;

TEST_CASE ("Typing what a parameter shows gives that value back", "[params]")
{
    // Hosts let you type a value into an automation lane or a generic editor;
    // what they hand over is text in the units format() prints.
    for (const auto& d : paramDefs())
    {
        for (const float n : { 0.f, 0.13f, 0.25f, 0.5f, 0.77f, 1.f })
        {
            const float v = d.snap (d.toPlain (n));
            const auto shown = d.format (v);
            const float back = d.parse (shown);
            INFO (d.id << ": " << v << " shows as \"" << shown << "\", parses as " << back);
            // the same text again, or (at a unit switch such as 999.6 ms -> "1000 ms")
            // within the display's rounding
            const bool sameText = d.format (back) == shown;
            const bool close = std::abs (back - v) <= 0.006f * std::max (std::abs (v), d.max - d.min);
            CHECK ((sameText || close));
        }
        if (d.mapping == Mapping::Choice)
            for (int i = 0; i < d.numChoices(); ++i)
                CHECK (d.parse ((*d.choices)[(size_t) i]) == (float) i);
    }
}

TEST_CASE ("Typed values follow their units", "[params]")
{
    CHECK (def (P::F1Cutoff).parse ("1.2 kHz") == def (P::F1Cutoff).snap (1200.f));
    CHECK (def (P::F1Cutoff).parse ("1,2kHz") == def (P::F1Cutoff).snap (1200.f));   // a comma is a decimal point
    CHECK (def (P::F1Cutoff).parse ("440") == def (P::F1Cutoff).snap (440.f));
    CHECK (def (P::AenvR).parse ("2.00 s") == def (P::AenvR).snap (2000.f));
    CHECK (def (P::AenvR).parse ("250 ms") == def (P::AenvR).snap (250.f));
    CHECK (def (P::ReverbMix).parse ("50%") == 0.5f);
    CHECK (def (P::ReverbMix).parse ("50") == 0.5f);
    CHECK (def (P::ReverbMix).parse ("250%") == 1.f);   // clamped
    CHECK (def (P::ReverbDecay).parse ("500 ms") == def (P::ReverbDecay).snap (0.5f));
    CHECK (def (P::PhosC13).parse ("1.50 uF") == def (P::PhosC13).snap (1500.f));
    CHECK (def (P::PhosC13).parse ("1.50 mF") == def (P::PhosC13).snap (1.5e6f));
    CHECK (def (P::ReverbMix).parse ("loud") == def (P::ReverbMix).def);   // no number: the default
    CHECK (def (P::OscAOn).parse ("Off") == 0.f);
    CHECK (def (P::OscAOn).parse ("on") == 1.f);
}
