#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Design tokens. Monochrome base; the only colour on screen is the accent of
// the selected sound category, used for values, active states and graphs.
namespace theme
{
    namespace col
    {
        inline const juce::Colour bg          { 0xff0a0a0b };  // window
        inline const juce::Colour panel       { 0xff0f0f11 };  // section background
        inline const juce::Colour well        { 0xff08080a };  // displays, inset areas
        inline const juce::Colour line        { 0xff1d1d21 };  // hairlines between sections
        inline const juce::Colour lineStrong  { 0xff2a2a30 };  // tracks, outlines
        inline const juce::Colour knob        { 0xff151518 };  // knob body
        inline const juce::Colour knobRim     { 0xff2c2c33 };
        inline const juce::Colour text        { 0xffececee };
        inline const juce::Colour textDim     { 0xff8b8b93 };
        inline const juce::Colour textFaint   { 0xff55555c };
        inline const juce::Colour grid        { 0xff17171b };
    }

    // Current accent (message thread only). Set when the category changes.
    juce::Colour accent();
    void setAccent (juce::Colour c);

    // Fonts: Inter for labels, JetBrains Mono for values.
    juce::Font label (float height = 10.5f);       // small caps style labels
    juce::Font labelStrong (float height = 11.f);
    juce::Font heading (float height = 11.f);      // section titles
    juce::Font title (float height = 18.f);
    juce::Font mono (float height = 11.f);

    // Section heading with tracking, drawn uppercase.
    void drawHeading (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                      juce::Justification j = juce::Justification::centredLeft, juce::Colour c = col::textDim);

    constexpr int kPad = 14;       // inner padding of sections
    constexpr int kHeaderH = 26;   // section header height
}
