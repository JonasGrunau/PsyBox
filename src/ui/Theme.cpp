#include "Theme.h"

#include "BinaryData.h"

namespace theme
{
namespace
{
    juce::Colour currentAccent { 0xfff2e205 };

    struct Typefaces
    {
        juce::Typeface::Ptr regular, medium, semibold, mono, monoMedium;
        Typefaces()
        {
            regular = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
            medium = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize);
            semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
            mono = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize);
            monoMedium = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize);
        }
    };

    const Typefaces& faces()
    {
        static const Typefaces t;
        return t;
    }

    juce::Font make (const juce::Typeface::Ptr& tf, float h, float tracking = 0.f)
    {
        return juce::Font (juce::FontOptions (tf).withHeight (h).withKerningFactor (tracking));
    }
} // namespace

juce::Colour accent() { return currentAccent; }
void setAccent (juce::Colour c) { currentAccent = c; }

juce::Font label (float h) { return make (faces().medium, h, 0.06f); }
juce::Font labelStrong (float h) { return make (faces().semibold, h, 0.06f); }
juce::Font heading (float h) { return make (faces().semibold, h, 0.16f); }
juce::Font title (float h) { return make (faces().semibold, h, 0.01f); }
juce::Font mono (float h) { return make (faces().mono, h); }

void strokeLine (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width)
{
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void fillUnder (juce::Graphics& g, const juce::Path& curve, juce::Rectangle<float> a, float alpha)
{
    juce::Path f (curve);
    f.lineTo (a.getRight(), a.getBottom());
    f.lineTo (a.getX(), a.getBottom());
    f.closeSubPath();
    g.setGradientFill (juce::ColourGradient (accent().withAlpha (alpha), 0.f, a.getY(), accent().withAlpha (0.f), 0.f, a.getBottom(), false));
    g.fillPath (f);
}

void drawHeading (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Justification j, juce::Colour c)
{
    g.setColour (c);
    g.setFont (heading());
    g.drawText (text.toUpperCase(), area, j, false);
}

} // namespace theme
