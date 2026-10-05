#include "FxPage.h"

#include "dsp/Shapers.h"

#include <complex>

using namespace theme;
using psy::P;

namespace
{
    constexpr int kChainW = 176;
    constexpr int kBadgeX = sp::md, kBadgeW = 112, kBadgeGap = sp::lg;
    constexpr int kMixW = 72, kPowerW = 48, kPowerD = 24;
    constexpr int kDisplayW = 220, kDisplayGap = sp::lg;
    constexpr int kTallExtra = 66;   // the compressor strip is this much taller than the others

    void strokePath (juce::Graphics& g, const juce::Path& p, juce::Colour c, float w = 1.5f)
    {
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Fills under a curve with the accent fading downwards, like the other displays.
    void fillUnder (juce::Graphics& g, const juce::Path& curve, juce::Rectangle<float> a, float alpha = 0.16f)
    {
        juce::Path f (curve);
        f.lineTo (a.getRight(), a.getBottom());
        f.lineTo (a.getX(), a.getBottom());
        f.closeSubPath();
        g.setGradientFill (juce::ColourGradient (accent().withAlpha (alpha), 0.f, a.getY(), accent().withAlpha (0.f), 0.f, a.getBottom(), false));
        g.fillPath (f);
    }

    float logX (juce::Rectangle<float> a, float hz, float lo = 20.f, float hi = 20000.f)
    {
        return a.getX() + a.getWidth() * std::log (juce::jlimit (lo, hi, hz) / lo) / std::log (hi / lo);
    }

    void drawDecades (juce::Graphics& g, juce::Rectangle<float> r, juce::Rectangle<float> a, float lo = 20.f, float hi = 20000.f)
    {
        g.setColour (col::grid);
        for (float hz : { 100.f, 1000.f, 10000.f })
            if (hz > lo && hz < hi) g.fillRect (logX (a, hz, lo, hi), r.getY(), 1.f, r.getHeight());
    }

    // ---------------------------------------------------------------------------
    // The picture on each strip: the distortion's transfer curve, the
    // crusher's steps, the shifted partials, the phaser's notches, the
    // chorus's sweeping delay, the echoes in one bar, the reverb's tail and
    // the stereo spread. Read-only; the knobs set what they show.
    // ---------------------------------------------------------------------------
    class FxDisplay : public DisplayBase
    {
    public:
        enum class Kind { Distortion, Crush, Shift, Phaser, Chorus, Delay, Reverb, Width };
        FxDisplay (PsyBoxProcessor& p, Kind k) : DisplayBase (p), kind (k) { setInterceptsMouseClicks (false, false); }

    private:
        Kind kind;

        float v (P prm) const { return proc.plain (prm); }
        double bpm() const { const double b = proc.getBpm(); return b > 1.0 ? b : 140.0; }

        uint64_t fingerprint() const override
        {
            switch (kind)
            {
                case Kind::Distortion: return hashParams ({ P::DistType, P::DistDrive, P::DistMix });
                case Kind::Crush:      return hashParams ({ P::CrushBits, P::CrushRate, P::CrushMix });
                case Kind::Shift:      return hashParams ({ P::FshiftHz, P::FshiftFb, P::FshiftSpread, P::FshiftMix });
                case Kind::Phaser:     return hashParams ({ P::PhaserDepth, P::PhaserFb, P::PhaserCenter, P::PhaserMix });
                case Kind::Chorus:     return hashParams ({ P::ChorusMode, P::ChorusRate, P::ChorusDepth });
                case Kind::Delay:      return hashParams ({ P::DelaySync, P::DelayDiv, P::DelayMs, P::DelayFb, P::DelayPing, P::DelayMix }) ^ (uint64_t) (bpm() * 100.0);
                case Kind::Reverb:     return hashParams ({ P::ReverbSize, P::ReverbDecay, P::ReverbDamp, P::ReverbPre });
                case Kind::Width:      return hashParams ({ P::WidthAmount, P::WidthStereo, P::WidthMonoHz });
            }
            return 0;
        }

        void paintContent (juce::Graphics& g, juce::Rectangle<float> r) override
        {
            switch (kind)
            {
                case Kind::Distortion: paintDistortion (g, r); break;
                case Kind::Crush:      paintCrush (g, r); break;
                case Kind::Shift:      paintShift (g, r); break;
                case Kind::Phaser:     paintPhaser (g, r); break;
                case Kind::Chorus:     paintChorus (g, r); break;
                case Kind::Delay:      paintDelay (g, r); break;
                case Kind::Reverb:     paintReverb (g, r); break;
                case Kind::Width:      paintWidth (g, r); break;
            }
        }

        // input -1..1 across, output up; the dry diagonal underneath
        void paintDistortion (juce::Graphics& g, juce::Rectangle<float> r)
        {
            drawGrid (g, r, 4, 2);
            const auto a = r.reduced (sp::sm, sp::sm);
            g.setColour (col::lineStrong);
            g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getY(), 1.f);
            const int type = (int) std::lround (v (P::DistType));
            const double gain = psy::dsp::dbToGain (v (P::DistDrive));
            const double mix = v (P::DistMix);
            using S = psy::dsp::AdaaShaper;
            const double peak = std::max ({ std::abs (S::f (gain, type)), std::abs (S::f (-gain, type)), 1e-6 });
            juce::Path p;
            for (int i = 0; i <= 120; ++i)
            {
                const double x = -1.0 + 2.0 * i / 120.0;
                const double y = x + (S::f (x * gain, type) / peak - x) * mix;
                const juce::Point<float> pt (a.getX() + a.getWidth() * (float) i / 120.f,
                                             a.getCentreY() - a.getHeight() * 0.5f * juce::jlimit (-1.f, 1.f, (float) y));
                if (i == 0) p.startNewSubPath (pt);
                else p.lineTo (pt);
            }
            strokePath (g, p, accent());
        }

        // two cycles of a 250 Hz sine at 48 kHz through the crusher, the
        // clean sine underneath: steps show where the crusher really steps
        void paintCrush (juce::Graphics& g, juce::Rectangle<float> r)
        {
            drawGrid (g, r, 4, 2);
            const auto a = r.reduced (sp::sm, sp::sm);
            const float q = std::exp2 (v (P::CrushBits) - 1.f);
            const float factor = std::max (1.f, v (P::CrushRate)), mix = v (P::CrushMix);
            constexpr int n = 384;
            juce::Path dry, wet;
            float cnt = factor - 1.f, held = 0.f, lastY = 0.f;
            for (int i = 0; i <= n; ++i)
            {
                const float x = 0.9f * std::sin (juce::MathConstants<float>::twoPi * 2.f * (float) i / (float) n);
                cnt += 1.f;
                if (cnt >= factor) { cnt -= factor; held = std::round (x * q) / q; }
                const float px = a.getX() + a.getWidth() * (float) i / (float) n;
                const float yd = a.getCentreY() - a.getHeight() * 0.5f * x;
                const float yw = a.getCentreY() - a.getHeight() * 0.5f * (x + (held - x) * mix);
                if (i == 0) { dry.startNewSubPath (px, yd); wet.startNewSubPath (px, yw); }
                else
                {
                    dry.lineTo (px, yd);
                    if (std::abs (yw - lastY) > 0.5f) wet.lineTo (px, lastY);
                    wet.lineTo (px, yw);
                }
                lastY = yw;
            }
            strokePath (g, dry, col::lineStrong, 1.f);
            strokePath (g, wet, accent());
        }

        // a few partials (faint where they were), shifted copies in the accent;
        // feedback shifts them again, spread moves the right channel's copy
        void paintShift (juce::Graphics& g, juce::Rectangle<float> r)
        {
            constexpr float lo = 40.f, hi = 16000.f;
            const auto a = r.reduced (sp::sm, sp::sm);
            drawDecades (g, r, a, lo, hi);
            const float hz = v (P::FshiftHz), fb = v (P::FshiftFb), spread = v (P::FshiftSpread), mix = v (P::FshiftMix);
            const auto bar = [&] (float f, float amp, juce::Colour c) {
                f = std::abs (f);
                if (f < lo || f > hi || amp < 0.02f) return;
                g.setColour (c);
                const float h = a.getHeight() * juce::jmin (amp, 1.f);
                g.fillRect (juce::Rectangle<float> (logX (a, f, lo, hi) - 0.75f, a.getBottom() - h, 1.5f, h));
            };
            for (int k = 1; k <= 8; ++k)
            {
                const float f = 160.f * (float) k, amp = 1.f / std::pow ((float) k, 0.6f);
                bar (f, amp, col::lineStrong);
                bar (f, amp * (1.f - mix), col::textDim);
                float gain = mix;
                for (int j = 1; j <= 4; ++j, gain *= fb)
                {
                    if (spread > 0.02f) bar (f + (float) j * hz * (1.f - 2.f * spread), amp * gain, accent().withAlpha (0.45f));
                    bar (f + (float) j * hz, amp * gain, accent());
                }
            }
        }

        // the response at the sweep's centre (six all-pass stages, feedback,
        // mix), the band the sweep covers shaded behind it
        void paintPhaser (juce::Graphics& g, juce::Rectangle<float> r)
        {
            const auto a = r.reduced (sp::sm, sp::sm);
            drawDecades (g, r, a);
            const float centre = v (P::PhaserCenter), depth = v (P::PhaserDepth), fb = v (P::PhaserFb), mix = v (P::PhaserMix);
            const float x0 = logX (a, centre * std::exp2 (-2.5f * depth)), x1 = logX (a, centre * std::exp2 (2.5f * depth));
            g.setColour (accent().withAlpha (0.07f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, r.getY(), juce::jmax (x1, x0 + 1.f), r.getBottom()));

            constexpr float topDb = 12.f, botDb = -30.f;
            const auto yOf = [&] (float db) { return a.getY() + a.getHeight() * (topDb - juce::jlimit (botDb, topDb, db)) / (topDb - botDb); };
            g.setColour (col::grid);
            g.fillRect (a.getX(), yOf (0.f), a.getWidth(), 1.f);
            juce::Path p;
            const int n = juce::jmax (64, (int) a.getWidth());
            for (int i = 0; i <= n; ++i)
            {
                const float f = 20.f * std::pow (1000.f, (float) i / (float) n);
                const std::complex<float> jw (0.f, f / centre);
                const auto ap = (1.f - jw) / (1.f + jw);
                const auto A = std::pow (ap, 6.f);
                const auto H = (1.f - mix) + mix * A / (1.f - fb * A);
                const float db = 20.f * std::log10 (std::max (std::abs (H), 1e-4f));
                const juce::Point<float> pt (a.getX() + a.getWidth() * (float) i / (float) n, yOf (db));
                if (i == 0) p.startNewSubPath (pt);
                else p.lineTo (pt);
            }
            fillUnder (g, p, a);
            strokePath (g, p, accent());
        }

        // two seconds of the delay time each side sweeps through
        void paintChorus (juce::Graphics& g, juce::Rectangle<float> r)
        {
            drawGrid (g, r, 4, 2);
            const auto a = r.reduced (sp::sm, sp::sm);
            const bool flanger = v (P::ChorusMode) > 0.5f;
            const float rate = v (P::ChorusRate), depth = v (P::ChorusDepth);
            const float base = flanger ? 1.2f : 11.f, mod = flanger ? depth : 6.f * depth;
            const float lo = flanger ? 0.8f : 9.f, hi = flanger ? 2.6f : 19.f;   // the whole sweep range fits
            const int n = juce::jmax (64, (int) a.getWidth());
            for (int c = 1; c >= 0; --c)
            {
                juce::Path p;
                for (int i = 0; i <= n; ++i)
                {
                    const float t = 2.f * (float) i / (float) n;
                    const float ms = base + mod * (0.5f + 0.5f * std::sin (juce::MathConstants<float>::twoPi * (rate * t + 0.25f * (float) c)));
                    const juce::Point<float> pt (a.getX() + a.getWidth() * (float) i / (float) n, a.getBottom() - a.getHeight() * (ms - lo) / (hi - lo));
                    if (i == 0) p.startNewSubPath (pt);
                    else p.lineTo (pt);
                }
                if (c == 0) fillUnder (g, p, a, 0.12f);
                strokePath (g, p, c == 0 ? accent() : accent().withAlpha (0.45f));
            }
        }

        // one bar at the host tempo; echoes above the line are left, below
        // right (ping-pong alternates, otherwise both)
        void paintDelay (juce::Graphics& g, juce::Rectangle<float> r)
        {
            drawGrid (g, r, 4, 2);
            const auto a = r.reduced (sp::sm, sp::sm);
            const double beatMs = 60000.0 / bpm();
            const double ms = v (P::DelaySync) > 0.5f ? psy::syncDivBeats ((int) std::lround (v (P::DelayDiv))) * beatMs : (double) v (P::DelayMs);
            const double bar = 4.0 * beatMs;
            const float fb = v (P::DelayFb), mix = v (P::DelayMix);
            const bool ping = v (P::DelayPing) > 0.5f;
            const float cy = a.getCentreY(), half = a.getHeight() * 0.5f;
            g.setColour (col::grid);
            g.fillRect (a.getX(), cy, a.getWidth(), 1.f);
            const auto tick = [&] (double t, float amp, bool up, bool down, juce::Colour c) {
                const float x = a.getX() + a.getWidth() * (float) (t / bar);
                g.setColour (c);
                const float h = half * std::pow (amp, 0.6f);
                g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (x - 1.25f, up ? cy - h : cy, x + 1.25f, down ? cy + h : cy), 1.25f);
            };
            tick (0.0, 1.f, true, true, col::textFaint);
            float amp = 0.4f + 0.6f * mix;
            for (int k = 1; ms > 1.0 && k * ms <= bar && amp > 0.03f; ++k, amp *= fb)
                tick (k * ms, amp, ! ping || k % 2 == 1, ! ping || k % 2 == 0, accent());
        }

        // pre-delay gap, early reflections, then the tail decaying to -60 dB
        // over the decay time; damping fades the bright part faster
        void paintReverb (juce::Graphics& g, juce::Rectangle<float> r)
        {
            const auto a = r.reduced (sp::sm, sp::sm);
            const float size = v (P::ReverbSize), decay = juce::jmax (0.05f, v (P::ReverbDecay));
            const float pre = v (P::ReverbPre) * 0.001f, damp = v (P::ReverbDamp);
            const float window = juce::jlimit (0.5f, 40.f, (pre + decay) * 1.1f);
            const float early = 0.01f + 0.07f * size;
            const float cy = a.getCentreY(), half = a.getHeight() * 0.5f;
            g.setColour (col::grid);
            g.fillRect (a.getX(), cy, a.getWidth(), 1.f);
            g.setColour (col::textFaint);
            g.fillRect (a.getX(), a.getY(), 2.f, a.getHeight());   // the dry hit

            juce::Random rnd (0x5eed);
            juce::Path env;
            bool started = false;
            for (float x = a.getX() + 3.f; x < a.getRight(); x += 2.f)
            {
                const float t = window * (x - a.getX()) / a.getWidth() - pre;
                const float noise = rnd.nextFloat();
                if (t < 0.f) continue;
                const float lvl = std::pow (10.f, -3.f * t / decay);
                const float bright = std::pow (10.f, -3.f * t / (decay * (1.f - 0.75f * damp)));
                const float amp = std::sqrt (lvl);
                float h = half * amp * (0.35f + 0.65f * noise);
                if (t < early) h = noise > 0.6f ? half * amp : half * amp * 0.25f;   // sparse early reflections
                g.setColour (accent().withAlpha (0.25f + 0.6f * (lvl > 0.f ? bright / lvl : 0.f)));
                g.fillRect (x, cy - h, 1.f, 2.f * h);
                const juce::Point<float> pt (x, cy - half * amp);
                if (! started) { env.startNewSubPath (pt); started = true; }
                else env.lineTo (pt);
            }
            strokePath (g, env, accent(), 1.2f);
        }

        // a stereo fan: the wedge opens with width and stereoize, the lows
        // under the low-mono split stay on the centre line
        void paintWidth (juce::Graphics& g, juce::Rectangle<float> r)
        {
            const auto a = r.reduced (sp::sm, sp::xs);
            const float rad = juce::jmin (a.getHeight() - 2.f, a.getWidth() * 0.5f - 12.f);
            const juce::Point<float> c (a.getCentreX(), a.getBottom());
            const auto disc = juce::Rectangle<float> (c.x - rad, c.y - rad, 2.f * rad, 2.f * rad);
            constexpr float halfPi = juce::MathConstants<float>::halfPi;

            juce::Path guide;
            guide.addCentredArc (c.x, c.y, rad, rad, 0.f, -halfPi, halfPi, true);
            for (float ang : { -halfPi * 0.5f, 0.f, halfPi * 0.5f })
            {
                guide.startNewSubPath (c);
                guide.lineTo (c.x + rad * std::sin (ang), c.y - rad * std::cos (ang));
            }
            strokePath (g, guide, col::lineStrong, 1.f);

            const float spread = juce::jlimit (0.f, 1.f, 0.5f * v (P::WidthAmount) + 0.35f * v (P::WidthStereo));
            const float ang = juce::jmax (0.02f, spread * halfPi * 0.95f);
            juce::Path wedge;
            wedge.addPieSegment (disc, -ang, ang, 0.f);
            g.setColour (accent().withAlpha (0.16f));
            g.fillPath (wedge);
            strokePath (g, wedge, accent());

            const float lowMono = juce::jlimit (0.f, 1.f, std::log (v (P::WidthMonoHz) / 20.f) / std::log (25.f));
            const float lr = rad * 0.55f * lowMono;
            juce::Path inner;
            inner.addPieSegment (juce::Rectangle<float> (c.x - lr, c.y - lr, 2.f * lr, 2.f * lr), -ang, ang, 0.f);
            g.setColour (col::well);
            g.fillPath (inner);
            g.setColour (accent());
            g.fillRect (c.x - 0.75f, c.y - lr, 1.5f, lr);

            g.setFont (mono (9.f));
            g.setColour (col::textFaint);
            g.drawText ("L", juce::Rectangle<float> (c.x - rad - 12.f, c.y - 10.f, 10.f, 10.f), juce::Justification::centredRight, false);
            g.drawText ("R", juce::Rectangle<float> (c.x + rad + 2.f, c.y - 10.f, 10.f, 10.f), juce::Justification::centredLeft, false);
        }
    };

    // A small picture of each effect for its badge.
    void drawGlyph (juce::Graphics& g, FxPage::Glyph glyph, juce::Rectangle<float> b, juce::Colour c)
    {
        using G = FxPage::Glyph;
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        const auto X = [&] (float u) { return b.getX() + u * b.getWidth(); };
        const auto Y = [&] (float v) { return b.getCentreY() - v * b.getHeight() * 0.5f; };
        const auto curve = [&] (auto fn, int n = 48) {
            juce::Path p;
            for (int i = 0; i <= n; ++i)
            {
                const float u = (float) i / (float) n;
                if (i == 0) p.startNewSubPath (X (u), Y (fn (u)));
                else p.lineTo (X (u), Y (fn (u)));
            }
            return p;
        };
        const auto bar = [&] (float u, float lo, float hi, juce::Colour col) {
            g.setColour (col);
            g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (X (u) - 0.9f, Y (hi), X (u) + 0.9f, Y (lo)), 0.9f);
        };

        switch (glyph)
        {
            case G::Distortion:
                strokePath (g, curve ([&] (float u) { return juce::jlimit (-0.7f, 0.7f, 1.8f * std::sin (twoPi * 2.f * u)); }), c);
                break;
            case G::Crush:
            {
                juce::Path p;
                constexpr int steps = 14;
                for (int i = 0; i < steps; ++i)
                {
                    const float u0 = (float) i / steps, u1 = (float) (i + 1) / steps;
                    const float y = std::round (3.f * std::sin (twoPi * 2.f * (u0 + 0.5f / steps))) / 3.f * 0.85f;
                    if (i == 0) p.startNewSubPath (X (u0), Y (y));
                    else p.lineTo (X (u0), Y (y));
                    p.lineTo (X (u1), Y (y));
                }
                g.setColour (c);
                g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
                break;
            }
            case G::Shift:
                for (int k = 0; k < 3; ++k)
                {
                    const float h = 0.9f - 0.25f * (float) k;
                    bar (0.1f + 0.2f * (float) k, -1.f, -1.f + 2.f * h, c.withAlpha (c.getFloatAlpha() * 0.35f));
                    bar (0.38f + 0.2f * (float) k, -1.f, -1.f + 2.f * h, c);
                }
                break;
            case G::Phaser:
                strokePath (g, curve ([] (float u) {
                    float d = 0.f;
                    for (float n : { 0.22f, 0.5f, 0.78f }) d += std::exp (-std::pow ((u - n) / 0.06f, 2.f));
                    return 0.65f - 1.5f * d;
                }), c);
                break;
            case G::Chorus:
                strokePath (g, curve ([&] (float u) { return 0.65f * std::sin (twoPi * 1.5f * u + 1.2f); }), c.withAlpha (c.getFloatAlpha() * 0.45f));
                strokePath (g, curve ([&] (float u) { return 0.65f * std::sin (twoPi * 1.5f * u); }), c);
                break;
            case G::Compressor:
            {
                juce::Path one;
                one.startNewSubPath (X (0.2f), Y (-0.9f));
                one.lineTo (X (0.8f), Y (0.9f));
                strokePath (g, one, c.withAlpha (c.getFloatAlpha() * 0.35f), 1.f);
                juce::Path p;
                p.startNewSubPath (X (0.2f), Y (-0.9f));
                p.lineTo (X (0.46f), Y (0.15f));
                p.quadraticTo (X (0.54f), Y (0.42f), X (0.8f), Y (0.55f));
                strokePath (g, p, c);
                break;
            }
            case G::Delay:
                for (int k = 0; k < 5; ++k)
                {
                    const float h = 0.9f * std::pow (0.68f, (float) k);
                    bar (0.1f + 0.2f * (float) k, -h, h, c);
                }
                break;
            case G::Reverb:
            {
                bar (0.08f, -0.9f, 0.9f, c);
                juce::Random rnd (7);
                for (float u = 0.17f; u <= 0.95f; u += 0.045f)
                {
                    const float h = 0.75f * std::exp (-3.2f * (u - 0.17f)) * (0.55f + 0.45f * rnd.nextFloat());
                    bar (u, -h, h, c.withAlpha (c.getFloatAlpha() * 0.8f));
                }
                break;
            }
            case G::Width:
            {
                juce::Path p;
                const float cx = b.getCentreX(), cy = b.getCentreY(), h = b.getHeight() * 0.5f;
                p.startNewSubPath (cx, cy - h * 0.7f);
                p.lineTo (cx, cy + h * 0.7f);
                for (float rr : { 0.45f, 0.95f })
                {
                    const float rad = h * rr * 1.6f;
                    p.addCentredArc (cx, cy, rad, rad, 0.f, juce::MathConstants<float>::pi * 0.3f, juce::MathConstants<float>::pi * 0.7f, true);
                    p.addCentredArc (cx, cy, rad, rad, 0.f, -juce::MathConstants<float>::pi * 0.3f, -juce::MathConstants<float>::pi * 0.7f, true);
                }
                strokePath (g, p, c);
                break;
            }
        }
    }
} // namespace

// ---------------------------------------------------------------------------
// Power
// ---------------------------------------------------------------------------
FxPage::Power::Power (PsyBoxProcessor& p, P onParam)
    : attachment (*p.param (onParam), [this] (float x) { on = x > 0.5f; repaint(); })
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("Switch this effect on or off");
    attachment.sendInitialUpdate();
}

void FxPage::Power::drawIcon (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
{
    // an open ring with a bar through the gap
    const float rad = b.getWidth() * 0.5f;
    juce::Path p;
    p.addCentredArc (b.getCentreX(), b.getCentreY() + rad * 0.08f, rad * 0.82f, rad * 0.82f, 0.f, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
    p.startNewSubPath (b.getCentreX(), b.getY());
    p.lineTo (b.getCentreX(), b.getCentreY());
    strokePath (g, p, c, 1.4f);
}

void FxPage::Power::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool hot = isMouseOver();
    if (on)
    {
        g.setColour (accent().withAlpha (hot ? 0.3f : 0.2f));
        g.fillRoundedRectangle (b, 4.f);
        g.setColour (accent().withAlpha (0.6f));
        g.drawRoundedRectangle (b, 4.f, 1.f);
    }
    else
    {
        g.setColour (hot ? col::textFaint : col::lineStrong);
        g.drawRoundedRectangle (b, 4.f, 1.f);
    }
    drawIcon (g, b.withSizeKeepingCentre (11.f, 11.f), on ? accent() : (hot ? col::textDim : col::textFaint));
}

void FxPage::Power::mouseDown (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (on ? 0.f : 1.f);
}

// ---------------------------------------------------------------------------
// Strip
// ---------------------------------------------------------------------------
FxPage::Strip::Strip (PsyBoxProcessor& p, int i, const juce::String& n, Glyph gl, P onSwitch)
    : proc (p), index (i), name (n), glyph (gl), onParam (onSwitch), power (p, onSwitch)
{
    addAndMakeVisible (power);
    addMouseListener (this, true);   // hover over any control counts as hovering the strip
    startTimerHz (8);
}

ParamKnob* FxPage::Strip::knob (P prm, int col, int row, const juce::String& l)
{
    auto* k = ks.add (*this, proc, prm, l);
    items.push_back ({ k, col, row, 1, false, {} });
    return k;
}

void FxPage::Strip::segmented (P prm, int col, int span, const juce::String& caption)
{
    auto s = std::make_unique<Segmented> (proc, prm);
    addAndMakeVisible (*s);
    items.push_back ({ s.get(), col, 0, span, true, caption });
    owned.push_back (std::move (s));
}

void FxPage::Strip::toggle (P prm, int col, const juce::String& text)
{
    auto t = std::make_unique<ToggleDot> (proc, prm, text);
    t->setStacked (true);
    addAndMakeVisible (*t);
    items.push_back ({ t.get(), col, 0, 1, false, {} });
    owned.push_back (std::move (t));
}

void FxPage::Strip::selector (P prm)
{
    select = std::make_unique<Segmented> (proc, prm);
    select->setVertical (true);
    addAndMakeVisible (*select);
}

ParamKnob* FxPage::Strip::mix (P prm, const juce::String& l)
{
    mixKnobs.push_back (ks.add (*this, proc, prm, l));
    return mixKnobs.back();
}

void FxPage::Strip::display (std::unique_ptr<juce::Component> c, int width)
{
    addAndMakeVisible (*c);
    side = c.get();
    sideW = width;
    owned.push_back (std::move (c));
}

void FxPage::Strip::showWhen (juce::Component* c, std::function<bool()> visible)
{
    conditional.emplace_back (c, std::move (visible));
    c->setVisible (conditional.back().second());
}

void FxPage::Strip::setHighlight (bool h)
{
    if (h == highlight) return;
    highlight = h;
    repaint();
}

void FxPage::Strip::hoverChanged()
{
    const bool h = isMouseOver (true);
    if (h == hovered) return;
    hovered = h;
    if (onHover) onHover (h);
}

int FxPage::Strip::rows() const
{
    int n = 1;
    for (const auto& it : items) n = std::max (n, it.row + 1);
    return n;
}

int FxPage::Strip::columns() const
{
    int n = 0;
    for (const auto& it : items) n = std::max (n, it.col + it.span);
    return n;
}

int FxPage::Strip::gridX() const
{
    return kBadgeX + kBadgeW + kBadgeGap + (select != nullptr ? kSelectorW + kBadgeGap : 0);
}

void FxPage::Strip::timerCallback()
{
    for (auto& [c, visible] : conditional)
        if (const bool vis = visible(); vis != c->isVisible()) c->setVisible (vis);

    // a switched-off effect greys out like a switched-off oscillator
    const bool active = proc.plain (onParam) > 0.5f;
    if ((int) active == shownActive) return;
    shownActive = (int) active;
    for (const auto& it : items)
    {
        it.c->setEnabled (active);
        if (dynamic_cast<ParamKnob*> (it.c) == nullptr) it.c->setAlpha (active ? 1.f : 0.4f);
    }
    for (auto* k : mixKnobs) k->setEnabled (active);
    if (select) select->setAlpha (active ? 1.f : 0.4f);
    if (side) side->setAlpha (active ? 1.f : 0.4f);
    repaint();   // badge and captions
}

void FxPage::Strip::paint (juce::Graphics& g)
{
    const bool active = shownActive != 0;
    if (highlight)
    {
        g.setColour (juce::Colours::white.withAlpha (0.02f));
        g.fillRect (getLocalBounds());
    }

    // badge: number, picture, name
    const auto b = badge.toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (b, 4.f);
    g.setColour (highlight ? col::lineStrong : col::line);
    g.drawRoundedRectangle (b.reduced (0.5f), 4.f, 1.f);
    g.setColour (col::textFaint);
    g.setFont (mono (9.f));
    g.drawText (juce::String (index), badge.reduced (sp::sm, sp::xs + 2).removeFromTop (10), juce::Justification::topLeft, false);
    constexpr int glyphH = 20, nameH = 12, gap = sp::sm;
    const int top = badge.getCentreY() - (glyphH + gap + nameH) / 2 + 2;
    drawGlyph (g, glyph, juce::Rectangle<float> ((float) badge.getCentreX() - 26.f, (float) top, 52.f, (float) glyphH),
               active ? accent() : col::textFaint);
    drawHeading (g, name, { badge.getX(), top + glyphH + gap, badge.getWidth(), nameH }, juce::Justification::centred,
                 active ? col::text : col::textDim);

    g.setColour (col::line);
    g.fillRect (mixColumn.getX(), sp::md, 1, getHeight() - 2 * sp::md);

    g.setColour (active ? col::textDim : col::textFaint);
    g.setFont (label (10.f));
    for (const auto& c : captions)
        g.drawText (c.text.toUpperCase(), c.area, juce::Justification::centred, false);
}

void FxPage::Strip::resized()
{
    const int w = getWidth(), h = getHeight();
    badge = { kBadgeX, sp::sm, kBadgeW, h - 2 * sp::sm };
    mixColumn = { w - kPowerW - kMixW, 0, kMixW, h };
    power.setBounds (juce::Rectangle<int> (w - kPowerW, 0, kPowerW, h).withSizeKeepingCentre (kPowerD, kPowerD));
    const juce::Rectangle<int> mixCell (mixColumn.getCentreX() - kCellW / 2, (h - kCellH) / 2, kCellW, kCellH);
    for (auto* k : mixKnobs) k->setBounds (mixCell);

    if (select)
    {
        const int sh = kCtrlH * select->getNumChoices();
        select->setBounds (kBadgeX + kBadgeW + kBadgeGap, (h - sh) / 2, kSelectorW, sh);
    }

    const int bodyH = rows() == 1 ? kCellH : kBodyH;
    const CellGrid gr { gridX() - kCellInset, (h - bodyH) / 2 };
    captions.clear();
    for (const auto& it : items)
    {
        if (it.control)
        {
            it.c->setBounds (gr.control (it.col, it.row, it.span));
            captions.push_back ({ it.caption, gr.cell (it.col, it.row, it.span).removeFromBottom (kLabelH) });
        }
        else
        {
            it.c->setBounds (gr.cell (it.col, it.row, it.span));
        }
    }

    if (side)
    {
        const int right = mixColumn.getX() - kDisplayGap;
        const int left = sideW > 0 ? right - sideW : gr.cell (columns(), 0).getX() + kCellInset + kDisplayGap;
        side->setBounds (juce::Rectangle<int>::leftTopRightBottom (left, sp::sm, right, h - sp::sm));
    }
}

// ---------------------------------------------------------------------------
// Chain
// ---------------------------------------------------------------------------
FxPage::Chain::Chain (PsyBoxProcessor& p, const std::vector<std::pair<juce::String, P>>& effects)
{
    rows.resize (effects.size());
    for (size_t i = 0; i < effects.size(); ++i)
    {
        auto& r = rows[i];
        r.name = effects[i].first;
        r.attachment = std::make_unique<juce::ParameterAttachment> (*p.param (effects[i].second), [this, i] (float x) {
            rows[i].on = x > 0.5f;
            repaint();
        });
        r.attachment->sendInitialUpdate();
    }
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<int> FxPage::Chain::rowArea (int i) const
{
    return { 0, kPad + kHeaderH + kHeaderGap + i * kRowH, getWidth(), kRowH };
}

int FxPage::Chain::rowAt (juce::Point<int> pt) const
{
    for (int i = 0; i < (int) rows.size(); ++i)
        if (rowArea (i).contains (pt)) return i;
    return -1;
}

int FxPage::Chain::slotAt (float rowTop) const
{
    const float top = (float) rowArea (0).getY();
    return juce::jlimit (0, (int) rows.size() - 1, (int) std::lround ((rowTop - top) / (float) kRowH));
}

psy::FxChain FxPage::Chain::shown() const
{
    if (! dragging || pressed < 0 || slot < 0) return order;
    // the dragged effect taken out and put back in at its slot
    psy::FxChain out {};
    const int effect = order[(size_t) pressed];
    int n = 0;
    for (int i = 0; i < psy::kNumFx; ++i)
        if (i != pressed) out[(size_t) n++] = order[(size_t) i];
    for (int i = psy::kNumFx - 1; i > slot; --i) out[(size_t) i] = out[(size_t) i - 1];
    out[(size_t) slot] = effect;
    return out;
}

void FxPage::Chain::setOrder (const psy::FxChain& o)
{
    order = o;
    if (! placed)
    {
        for (int i = 0; i < psy::kNumFx; ++i) rowY[(size_t) order[(size_t) i]] = (float) rowArea (i).getY();
        placed = true;
    }
    startTimerHz (60);   // rows slide to their new places
    repaint();
}

void FxPage::Chain::timerCallback()
{
    const auto target = shown();
    const int lifted = dragging && pressed >= 0 ? order[(size_t) pressed] : -1;
    bool moving = false;
    for (int i = 0; i < psy::kNumFx; ++i)
    {
        const int fx = target[(size_t) i];
        if (fx == lifted) continue;   // under the mouse
        auto& y = rowY[(size_t) fx];
        const float to = (float) rowArea (i).getY();
        y += (to - y) * 0.35f;
        if (std::abs (to - y) < 0.5f) y = to;
        else moving = true;
    }
    if (! moving && ! dragging) stopTimer();
    repaint();
}

void FxPage::Chain::setHover (int effect)
{
    if (effect == hover) return;
    hover = effect;
    repaint();
    if (onHover) onHover (effect);
}

void FxPage::Chain::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.getPosition());
    setHover (row < 0 ? -1 : order[(size_t) row]);
}

void FxPage::Chain::mouseDown (const juce::MouseEvent& e)
{
    pressed = rowAt (e.getPosition());
    dragging = false;
    slot = -1;
    if (pressed >= 0) grabOffset = e.position.y - (float) rowArea (pressed).getY();
}

void FxPage::Chain::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed < 0) return;
    if (! dragging && std::abs (e.getDistanceFromDragStartY()) < 4) return;
    if (! placed) setOrder (order);
    dragging = true;
    // the row follows the mouse, inside the list
    dragY = juce::jlimit ((float) rowArea (0).getY(), (float) rowArea ((int) rows.size() - 1).getY(), e.position.y - grabOffset);
    rowY[(size_t) order[(size_t) pressed]] = dragY;
    slot = slotAt (dragY);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    startTimerHz (60);
    repaint();
}

void FxPage::Chain::mouseUp (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    const int from = pressed;
    if (from < 0) return;
    if (! dragging)
    {
        pressed = -1;
        // a click: switch the effect on the row
        auto& r = rows[(size_t) order[(size_t) from]];
        r.attachment->setValueAsCompleteGesture (r.on ? 0.f : 1.f);
        return;
    }
    // it lands where the gap was; the row settles there from the mouse
    const auto moved = shown();
    dragging = false;
    pressed = slot = -1;
    startTimerHz (60);
    if (moved != order && onReorder) onReorder (moved);
    repaint();
}

void FxPage::Chain::paintRow (juce::Graphics& g, int effect, juce::Rectangle<int> a, int place, bool lit, bool band) const
{
    const auto& r = rows[(size_t) effect];
    if (lit && band)
    {
        g.setColour (juce::Colours::white.withAlpha (0.03f));
        g.fillRect (a);
    }
    // a grip in the margin on hover: the row can be dragged
    if (lit)
    {
        const float gx = (float) a.getX() + 5.f, gy = (float) a.getCentreY() - 5.f;
        g.setColour (col::textFaint);
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 2; ++x)
                g.fillEllipse (gx + 4.f * (float) x, gy + 4.f * (float) y, 2.f, 2.f);
    }
    a.removeFromLeft (kPad);
    a.removeFromRight (kPad);
    g.setColour (r.on ? accent() : col::lineStrong);
    g.fillRoundedRectangle (juce::Rectangle<float> ((float) a.getX(), (float) a.getCentreY() - 7.f, 2.f, 14.f), 1.f);
    a.removeFromLeft (sp::md);
    g.setColour (col::textFaint);
    g.setFont (mono (10.f));
    g.drawText (juce::String (place + 1), a.removeFromLeft (sp::lg), juce::Justification::centredLeft, false);
    Power::drawIcon (g, a.removeFromRight (12).withSizeKeepingCentre (11, 11).toFloat(),
                     r.on ? accent() : (lit ? col::textDim : col::textFaint));
    drawHeading (g, r.name, a, juce::Justification::centredLeft, r.on ? col::text : (lit ? col::textDim : col::textFaint));
}

void FxPage::Chain::paint (juce::Graphics& g)
{
    const juce::Rectangle<int> head (kPad, kPad, getWidth() - 2 * kPad, kHeaderH);
    drawHeading (g, "Chain", head);
    int count = 0;
    for (const auto& r : rows) count += r.on ? 1 : 0;
    g.setColour (col::textFaint);
    g.setFont (mono (10.f));
    g.drawText (juce::String (count) + " ON", head, juce::Justification::centredRight, false);

    if (! placed)
        for (int i = 0; i < psy::kNumFx; ++i) rowY[(size_t) order[(size_t) i]] = (float) rowArea (i).getY();

    const auto now = shown();
    const int lifted = dragging && pressed >= 0 ? order[(size_t) pressed] : -1;
    if (lifted >= 0 && slot >= 0)
    {
        // the empty place it will land in
        const auto gap = rowArea (slot).reduced (kPad - 4, 3).toFloat();
        g.setColour (accent().withAlpha (0.06f));
        g.fillRoundedRectangle (gap, 3.f);
        g.setColour (accent().withAlpha (0.5f));
        juce::Path outline;
        outline.addRoundedRectangle (gap.reduced (0.5f), 3.f);
        juce::Path dashed;
        const float dashes[] = { 3.f, 3.f };
        juce::PathStrokeType (1.f).createDashedStroke (dashed, outline, dashes, 2);
        g.fillPath (dashed);
    }
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const int fx = now[(size_t) i];
        if (fx == lifted) continue;
        const auto a = rowArea (i).withY ((int) std::lround (rowY[(size_t) fx]));
        paintRow (g, fx, a, i, ! dragging && fx == hover);
    }
    if (lifted >= 0)
    {
        // the row in hand, over the others
        const auto a = rowArea (0).withY ((int) std::lround (rowY[(size_t) lifted])).withTrimmedLeft (2).withTrimmedRight (kPad - 6);
        g.setColour (col::panel.brighter (0.06f));
        g.fillRoundedRectangle (a.toFloat(), 3.f);
        g.setColour (col::lineStrong);
        g.drawRoundedRectangle (a.toFloat().reduced (0.5f), 3.f, 1.f);
        paintRow (g, lifted, rowArea (0).withY (a.getY()), slot, true, false);
    }

    g.setColour (col::textFaint);
    g.setFont (label (10.5f));
    g.drawFittedText ("Signal runs from 1 to 9.\nClick a row to switch it,\ndrag it to move it.",
                      getLocalBounds().reduced (kPad).removeFromBottom (48), juce::Justification::bottomLeft, 3);
}

// ---------------------------------------------------------------------------
// FxPage
// ---------------------------------------------------------------------------
FxPage::FxPage (PsyBoxProcessor& p)
    : orderAttachment (*p.param (P::FxOrder), [this] (float code) {
          // the chain and the rack follow the order (also on undo or a preset)
          order = psy::fxChain ((int) std::lround (code));
          if (chain == nullptr) return;
          chain->setOrder (order);
          for (int i = 0; i < psy::kNumFx; ++i)
              strips[(size_t) order[(size_t) i]]->setIndex (i + 1);
          resized();
          repaint();
      })
{
    using K = FxDisplay::Kind;
    std::vector<std::pair<juce::String, P>> effects;
    const auto add = [&] (const char* n, Glyph gl, P onParam) {
        strips.push_back (std::make_unique<Strip> (p, (int) strips.size() + 1, n, gl, onParam));
        effects.emplace_back (n, onParam);
        addAndMakeVisible (*strips.back());
        return strips.back().get();
    };
    const auto picture = [&p] (K k) { return std::make_unique<FxDisplay> (p, k); };

    auto* dist = add ("Distortion", Glyph::Distortion, P::DistOn);
    dist->knob (P::DistDrive, 0);
    dist->knob (P::DistTone, 1);
    dist->segmented (P::DistType, 2, 5, "Type");
    dist->mix (P::DistMix);
    dist->display (picture (K::Distortion), kDisplayW);

    auto* crush = add ("Crush", Glyph::Crush, P::CrushOn);
    crush->knob (P::CrushBits, 0);
    crush->knob (P::CrushRate, 1, 0, "Rate");
    crush->mix (P::CrushMix);
    crush->display (picture (K::Crush), kDisplayW);

    auto* shift = add ("Freq Shift", Glyph::Shift, P::FshiftOn);
    shift->knob (P::FshiftHz, 0);
    shift->knob (P::FshiftFb, 1);
    shift->knob (P::FshiftSpread, 2);
    shift->mix (P::FshiftMix);
    shift->display (picture (K::Shift), kDisplayW);

    auto* phaser = add ("Phaser", Glyph::Phaser, P::PhaserOn);
    phaser->knob (P::PhaserRate, 0);
    phaser->knob (P::PhaserDepth, 1);
    phaser->knob (P::PhaserFb, 2);
    phaser->knob (P::PhaserCenter, 3);
    phaser->mix (P::PhaserMix);
    phaser->display (picture (K::Phaser), kDisplayW);

    auto* chorus = add ("Chorus", Glyph::Chorus, P::ChorusOn);
    chorus->selector (P::ChorusMode);
    chorus->knob (P::ChorusRate, 0);
    chorus->knob (P::ChorusDepth, 1);
    chorus->knob (P::ChorusFb, 2);
    chorus->mix (P::ChorusMix);
    chorus->display (picture (K::Chorus), kDisplayW);

    // Compressor: Normal  Thresh Ratio Knee Output / Attack Release
    //             Multi   Low Mid High Output / Attack Release Down Up
    auto* comp = add ("Compressor", Glyph::Compressor, P::CompOn);
    tall = comp;
    comp->selector (P::CompMode);
    const auto multi = [&p] { return p.plain (P::CompMode) > 0.5f; };
    const auto normal = [multi] { return ! multi(); };
    for (auto [prm, col] : { std::pair { P::CompThresh, 0 }, std::pair { P::CompRatio, 1 }, std::pair { P::CompKnee, 2 } })
        comp->showWhen (comp->knob (prm, col), normal);
    for (auto [prm, col, row] : { std::tuple { P::MbLowGain, 0, 0 }, std::tuple { P::MbMidGain, 1, 0 }, std::tuple { P::MbHighGain, 2, 0 },
                                  std::tuple { P::CompDown, 2, 1 }, std::tuple { P::CompUp, 3, 1 } })
        comp->showWhen (comp->knob (prm, col, row), multi);
    comp->knob (P::CompOut, 3);
    comp->knob (P::CompAttack, 0, 1);
    comp->knob (P::CompRelease, 1, 1);
    comp->showWhen (comp->mix (P::CompMix, "Mix"), normal);
    comp->showWhen (comp->mix (P::CompMix, "Depth"), multi);
    comp->display (std::make_unique<CompDisplay> (p));

    auto* delay = add ("Delay", Glyph::Delay, P::DelayOn);
    const auto synced = [&p] { return p.plain (P::DelaySync) > 0.5f; };
    delay->showWhen (delay->knob (P::DelayMs, 0), [synced] { return ! synced(); });
    delay->showWhen (delay->knob (P::DelayDiv, 0, 0, "Division"), synced);
    delay->knob (P::DelayFb, 1);
    delay->knob (P::DelayLowCut, 2);
    delay->knob (P::DelayHighCut, 3);
    delay->toggle (P::DelaySync, 4, "Sync");
    delay->toggle (P::DelayPing, 5, "Ping-Pong");
    delay->mix (P::DelayMix);
    delay->display (picture (K::Delay), kDisplayW);

    auto* reverb = add ("Reverb", Glyph::Reverb, P::ReverbOn);
    reverb->knob (P::ReverbSize, 0);
    reverb->knob (P::ReverbDecay, 1);
    reverb->knob (P::ReverbDamp, 2);
    reverb->knob (P::ReverbPre, 3);
    reverb->mix (P::ReverbMix);
    reverb->display (picture (K::Reverb), kDisplayW);

    auto* width = add ("Width", Glyph::Width, P::WidthOn);
    width->knob (P::WidthStereo, 0);
    width->knob (P::WidthTime, 1);
    width->knob (P::WidthMonoHz, 2);
    width->mix (P::WidthAmount);
    width->display (picture (K::Width), kDisplayW);

    // hovering a chain row lights its strip and the other way round
    chain = std::make_unique<Chain> (p, effects);
    addAndMakeVisible (*chain);
    chain->onHover = [this] (int row) {
        for (size_t i = 0; i < strips.size(); ++i) strips[i]->setHighlight ((int) i == row);
    };
    for (size_t i = 0; i < strips.size(); ++i)
        strips[i]->onHover = [this, i] (bool in) {
            strips[i]->setHighlight (in);
            if (in) chain->setHover ((int) i);
            else if (! chain->isMouseOver()) chain->setHover (-1);
        };
    chain->onReorder = [this] (const psy::FxChain& moved) {
        const int code = psy::fxOrderCode (moved);
        if (code >= 0) orderAttachment.setValueAsCompleteGesture ((float) code);
    };
    orderAttachment.sendInitialUpdate();
}

void FxPage::resized()
{
    const int h = getHeight();
    chain->setBounds (0, 0, kChainW, h);
    const int x = kChainW + 1, w = getWidth() - x;
    const int rowH = (h - kTallExtra) / (int) strips.size();
    int y = 0;
    for (int fx : order)   // in chain order
    {
        auto& s = strips[(size_t) fx];
        const int sh = s.get() == tall ? h - rowH * ((int) strips.size() - 1) : rowH;
        s->setBounds (x, y, w, sh);
        y += sh;
    }
}

void FxPage::paintOverChildren (juce::Graphics& g)
{
    g.setColour (col::line);
    g.fillRect (kChainW, 0, 1, getHeight());
    for (size_t i = 0; i + 1 < order.size(); ++i)
    {
        const auto b = strips[(size_t) order[i]]->getBounds();
        g.fillRect (b.getX(), b.getBottom() - 1, b.getWidth(), 1);
    }
}
