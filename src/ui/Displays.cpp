#include "Displays.h"

#include "dsp/Dynamics.h"
#include "dsp/Filters.h"
#include "dsp/Modulators.h"
#include "dsp/Shapers.h"
#include "engine/KickVoice.h"
#include "dsp/Wavetable.h"

#include <cstring>

using namespace theme;
using psy::P;

// ---------------------------------------------------------------------------
void DisplayBase::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (b, 4.f);
    g.setColour (col::line);
    g.drawRoundedRectangle (b.reduced (0.5f), 4.f, 1.f);
    g.saveState();
    g.reduceClipRegion (getLocalBounds().reduced (1));
    paintContent (g, b.reduced (1.f));
    g.restoreState();
}

void DisplayBase::timerCallback()
{
    const uint64_t fp = fingerprint() ^ (uint64_t) theme::accent().getARGB();
    if (fp != lastPrint)
    {
        lastPrint = fp;
        repaint();
    }
}

uint64_t DisplayBase::hashParams (std::initializer_list<P> ps) const
{
    uint64_t h = 1469598103934665603ULL;
    for (auto p : ps)
    {
        float v = proc.plain (p);
        uint32_t bits;
        std::memcpy (&bits, &v, 4);
        h = (h ^ bits) * 1099511628211ULL;
    }
    return h;
}

void DisplayBase::drawGrid (juce::Graphics& g, juce::Rectangle<float> r, int v, int hl) const
{
    g.setColour (col::grid);
    for (int i = 1; i < v; ++i)
        g.fillRect (r.getX() + r.getWidth() * (float) i / (float) v, r.getY(), 1.f, r.getHeight());
    for (int i = 1; i < hl; ++i)
        g.fillRect (r.getX(), r.getY() + r.getHeight() * (float) i / (float) hl, r.getWidth(), 1.f);
}

namespace
{
    void strokeCurve (juce::Graphics& g, const juce::Path& p, juce::Rectangle<float> area, bool fill)
    {
        const auto acc = accent();
        if (fill)
        {
            juce::Path f (p);
            f.lineTo (area.getRight(), area.getBottom());
            f.lineTo (area.getX(), area.getBottom());
            f.closeSubPath();
            g.setGradientFill (juce::ColourGradient (acc.withAlpha (0.16f), 0.f, area.getY(), acc.withAlpha (0.0f), 0.f, area.getBottom(), false));
            g.fillPath (f);
        }
        g.setColour (acc);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
} // namespace

// ---------------------------------------------------------------------------
// LiveDisplay
// ---------------------------------------------------------------------------
void LiveDisplay::timerCallback()
{
    if (! isShowing()) return;
    proc.getModView (view);
    DisplayBase::timerCallback();
}

void LiveDisplay::paint (juce::Graphics& g)
{
    proc.getModView (view);   // the newest positions (also when painted off screen, as in snapshots)
    DisplayBase::paint (g);
}

uint64_t LiveDisplay::mixPos (uint64_t h, float pos)
{
    return (h ^ (uint64_t) std::lround (pos * 4096.f)) * 1099511628211ULL;
}

void LiveDisplay::drawDot (juce::Graphics& g, juce::Point<float> pt, bool newest, juce::Rectangle<float> area) const
{
    const auto acc = accent();
    const auto box = [pt] (float d) { return juce::Rectangle<float> (d, d).withCentre (pt); };
    if (! newest)
    {
        g.setColour (acc.withAlpha (0.6f));
        g.fillEllipse (box (5.f));
        return;
    }
    g.setColour (acc.withAlpha (0.35f));
    g.fillRect (pt.x - 0.5f, area.getY(), 1.f, area.getHeight());   // playhead
    g.setColour (acc.withAlpha (0.22f));
    g.fillEllipse (box (15.f));                                       // glow
    g.setColour (col::text);
    g.fillEllipse (box (7.f));
    g.setColour (acc);
    g.drawEllipse (box (7.f), 1.5f);
}

// ---------------------------------------------------------------------------
// WaveDisplay
// ---------------------------------------------------------------------------
WaveDisplay::WaveDisplay (PsyBoxProcessor& p, bool b) : DisplayBase (p), isB (b) {}

uint64_t WaveDisplay::fingerprint() const
{
    if (isB)
        return hashParams ({ P::OscBOn, P::OscBType, P::OscBShape, P::OscBPw, P::OscBDetune, P::OscBWt, P::OscBWtPos,
                             P::OscBFmRatio, P::OscBFm, P::OscBFmFb, P::OscBSync, P::OscBCoarse });
    return hashParams ({ P::OscAOn, P::OscAType, P::OscAShape, P::OscAPw, P::OscADetune, P::OscAWt, P::OscAWtPos,
                         P::OscAFmRatio, P::OscAFm, P::OscAFmFb });
}

void WaveDisplay::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    drawGrid (g, r, 4, 2);
    const bool on = proc.plain (isB ? P::OscBOn : P::OscAOn) > 0.5f;
    const int type = (int) proc.plain (isB ? P::OscBType : P::OscAType);
    const float shape = proc.plain (isB ? P::OscBShape : P::OscAShape);
    const float pw = proc.plain (isB ? P::OscBPw : P::OscAPw);
    const float det = proc.plain (isB ? P::OscBDetune : P::OscADetune);
    const int table = (int) proc.plain (isB ? P::OscBWt : P::OscAWt);
    const float wtPos = proc.plain (isB ? P::OscBWtPos : P::OscAWtPos);
    const float ratio = proc.plain (isB ? P::OscBFmRatio : P::OscAFmRatio);
    const float index = proc.plain (isB ? P::OscBFm : P::OscAFm);
    const float fb = proc.plain (isB ? P::OscBFmFb : P::OscAFmFb);
    const bool sync = isB && proc.plain (P::OscBSync) > 0.5f;
    const float syncRatio = sync ? std::exp2 (proc.plain (P::OscBCoarse) / 12.f) : 1.f;

    const int n = std::max (64, (int) r.getWidth() * 2);
    const auto& bank = psy::dsp::WavetableBank::shared();
    std::vector<float> y ((size_t) n);

    float fb1 = 0.f, fb2 = 0.f;
    for (int i = 0; i < n; ++i)
    {
        const float t = 2.f * (float) i / (float) n;  // two cycles
        float ph = t - std::floor (t);
        if (sync) { const float s = ph * std::max (1.f, syncRatio); ph = s - std::floor (s); }
        float v = 0.f;
        switch (type)
        {
            case 1:
            {
                static constexpr float off[7] = { -0.11f, -0.063f, -0.0195f, 0.f, 0.0199f, 0.062f, 0.107f };
                for (float o : off)
                {
                    const float pp = t * (1.f + o * det * 3.f) + o * 3.1f;
                    v += 2.f * (pp - std::floor (pp)) - 1.f;
                }
                v *= 0.3f;
                break;
            }
            case 2:
                v = bank.read (table, wtPos, ph, 1.f / 2048.f, 0.45f);
                break;
            case 3:
            {
                const float m = std::sin (psy::dsp::kTwoPi * ph * ratio + fb * 1.4f * 0.5f * (fb1 + fb2));
                fb2 = fb1; fb1 = m;
                v = std::sin (psy::dsp::kTwoPi * ph + index * m);
                break;
            }
            default:
            {
                const float s = juce::jlimit (0.f, 3.f, shape);
                const float sine = std::sin (psy::dsp::kTwoPi * ph);
                const float tri = ph < 0.5f ? 4.f * ph - 1.f : 3.f - 4.f * ph;
                const float saw = 2.f * ph - 1.f;
                const float sq = ph < pw ? 1.f : -1.f;
                if (s < 1.f) v = sine + (tri - sine) * s;
                else if (s < 2.f) v = tri + (saw - tri) * (s - 1.f);
                else v = saw + (sq - saw) * (s - 2.f);
                break;
            }
        }
        y[(size_t) i] = juce::jlimit (-1.2f, 1.2f, v);
    }

    juce::Path p;
    const float mid = r.getCentreY(), amp = r.getHeight() * 0.38f;
    for (int i = 0; i < n; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (float) (n - 1);
        const float yy = mid - y[(size_t) i] * amp;
        if (i == 0) p.startNewSubPath (x, yy);
        else p.lineTo (x, yy);
    }
    if (! on)
    {
        g.setColour (col::textFaint);
        g.strokePath (p, juce::PathStrokeType (1.2f));
        return;
    }
    g.setColour (accent().withAlpha (0.12f));
    g.strokePath (p, juce::PathStrokeType (5.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent());
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ---------------------------------------------------------------------------
// FilterDisplay
// ---------------------------------------------------------------------------
FilterDisplay::FilterDisplay (PsyBoxProcessor& p, int s) : DisplayBase (p), slot (s)
{
    fftData.resize (2 * 4096);
    magDb.resize (2048);
}

uint64_t FilterDisplay::fingerprint() const
{
    if (slot == 0)
        return hashParams ({ P::F1Type, P::F1Cutoff, P::F1Reso, P::F1Drive, P::F1Vowel });
    return hashParams ({ P::F2Type, P::F2Cutoff, P::F2Reso, P::F2Drive, P::F1Vowel });
}

void FilterDisplay::recompute()
{
    const float fs = 48000.f;
    psy::dsp::FilterSlot f;
    f.prepare (fs);
    const int type = (int) proc.plain (slot == 0 ? P::F1Type : P::F2Type);
    f.setType (type);
    const float cutoff = proc.plain (slot == 0 ? P::F1Cutoff : P::F2Cutoff);
    const float reso = proc.plain (slot == 0 ? P::F1Reso : P::F2Reso);
    const float vowel = proc.plain (P::F1Vowel);
    // Small-signal response: the drive stage is bypassed so the curve shows
    // the filter shape rather than a level change.
    f.update (cutoff, std::min (reso, 0.985f), 0.f, vowel);

    std::fill (fftData.begin(), fftData.end(), 0.f);
    const float imp = 1e-3f;  // tiny impulse keeps the nonlinear filters in their linear region
    for (int i = 0; i < 4096; ++i)
        fftData[(size_t) i] = f.process (i == 0 ? imp : 0.f) / imp;
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
    for (int i = 0; i < 2048; ++i)
        magDb[(size_t) i] = juce::Decibels::gainToDecibels (fftData[(size_t) i], -120.f);
}

void FilterDisplay::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    // log frequency grid: 100 Hz, 1 kHz, 10 kHz
    const auto xOf = [&] (float hz) { return r.getX() + r.getWidth() * std::log (hz / 20.f) / std::log (1000.f); };
    g.setColour (col::grid);
    for (float hz : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
        g.fillRect (xOf (hz), r.getY(), 1.f, r.getHeight());
    const float top = 24.f, bottom = -36.f;
    const auto yOf = [&] (float db) { return r.getY() + r.getHeight() * (top - db) / (top - bottom); };
    g.fillRect (r.getX(), yOf (0.f), r.getWidth(), 1.f);

    const int type = (int) proc.plain (slot == 0 ? P::F1Type : P::F2Type);
    if (type == 0)
    {
        g.setColour (col::textFaint);
        g.setFont (heading (9.5f));
        g.drawText ("BYPASSED", r.toNearestInt(), juce::Justification::centred, false);
        return;
    }
    recompute();

    juce::Path p;
    const int w = (int) r.getWidth();
    for (int px = 0; px <= w; ++px)
    {
        const float hz = 20.f * std::pow (1000.f, (float) px / (float) w);
        const float bin = hz / 48000.f * 4096.f;
        const int b0 = juce::jlimit (0, 2046, (int) bin);
        const float fr = bin - (float) b0;
        const float db = magDb[(size_t) b0] + (magDb[(size_t) b0 + 1] - magDb[(size_t) b0]) * fr;
        const float y = juce::jlimit (r.getY() - 2.f, r.getBottom() + 2.f, yOf (db));
        if (px == 0) p.startNewSubPath (r.getX() + (float) px, y);
        else p.lineTo (r.getX() + (float) px, y);
    }
    strokeCurve (g, p, r, true);

    // cutoff marker
    const float cutoff = proc.plain (slot == 0 ? P::F1Cutoff : P::F2Cutoff);
    g.setColour (accent().withAlpha (0.35f));
    g.fillRect (xOf (cutoff), r.getY(), 1.f, r.getHeight());
}

// ---------------------------------------------------------------------------
// EnvDisplay: ADSR drawn to scale on a time axis that fits the envelope.
// ---------------------------------------------------------------------------
uint64_t EnvDisplay::fingerprint() const
{
    uint64_t h = hashParams ({ P::AenvA, P::AenvD, P::AenvS, P::AenvR, P::AenvCurve, P::FenvA, P::FenvD, P::FenvS, P::FenvR,
                               P::MenvA, P::MenvD, P::MenvS, P::MenvR }) + (uint64_t) env;
    for (int k = 0; k < view.numVoices; ++k)
    {
        const auto& e = view.voices[(size_t) k].env[(size_t) env];
        h = mixPos (h, (float) e.stage + e.pos);
    }
    return h;
}

void EnvDisplay::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    static const P ids[3][4] = { { P::AenvA, P::AenvD, P::AenvS, P::AenvR },
                                 { P::FenvA, P::FenvD, P::FenvS, P::FenvR },
                                 { P::MenvA, P::MenvD, P::MenvS, P::MenvR } };
    const float a = proc.plain (ids[env][0]), d = proc.plain (ids[env][1]);
    const float s = proc.plain (ids[env][2]), rel = proc.plain (ids[env][3]);
    // compress long times so short envelopes stay readable
    const auto tw = [] (float ms) { return std::sqrt (std::max (ms, 0.f) + 1.f) - 1.f; };
    const float hold = 0.35f * (tw (a) + tw (d) + tw (rel)) + 4.f;
    const float total = tw (a) + tw (d) + hold + tw (rel);
    drawGrid (g, r, 8, 4);
    auto area = r.reduced (6.f, 8.f);
    const auto X = [&] (float t) { return area.getX() + area.getWidth() * t / total; };
    const auto Y = [&] (float v) { return area.getBottom() - area.getHeight() * v; };

    juce::Path p;
    p.startNewSubPath (X (0), Y (0));
    const int steps = 24;
    // attack: the RC charge, or the amp envelope's bend towards a swell
    const float bend = psy::dsp::attackBend (env == 0 ? proc.plain (P::AenvCurve) : 0.f);
    for (int i = 1; i <= steps; ++i)
    {
        const float u = (float) i / steps;
        p.lineTo (X (tw (a) * u), Y (psy::dsp::attackShape (u, bend)));
    }
    for (int i = 1; i <= steps; ++i)  // decay: exponential
    {
        const float u = (float) i / steps;
        p.lineTo (X (tw (a) + tw (d) * u), Y (s + (1.f - s) * std::exp (-u * 5.f)));
    }
    p.lineTo (X (tw (a) + tw (d) + hold), Y (s));
    for (int i = 1; i <= steps; ++i)
    {
        const float u = (float) i / steps;
        p.lineTo (X (tw (a) + tw (d) + hold + tw (rel) * u), Y (s * std::exp (-u * 5.f) * (1.f - u)));
    }
    strokeCurve (g, p, area, true);

    // stage handles
    g.setColour (col::text);
    for (auto pt : { juce::Point<float> (X (tw (a)), Y (1.f)), juce::Point<float> (X (tw (a) + tw (d)), Y (s)),
                     juce::Point<float> (X (tw (a) + tw (d) + hold), Y (s)) })
        g.fillEllipse (pt.x - 2.5f, pt.y - 2.5f, 5.f, 5.f);

    // the voices, on the curve: x from how far into its stage each one is
    using Stage = psy::dsp::Adsr::Stage;
    for (int k = view.numVoices - 1; k >= 0; --k)
    {
        const auto& e = view.voices[(size_t) k].env[(size_t) env];
        const float u = e.pos;
        juce::Point<float> pt;
        switch ((Stage) e.stage)
        {
            case Stage::Attack:  pt = { X (tw (a) * u), Y (psy::dsp::attackShape (u, bend)) }; break;
            case Stage::Decay:   pt = { X (tw (a) + tw (d) * u), Y (s + (1.f - s) * std::exp (-u * 5.f)) }; break;
            case Stage::Sustain: pt = { X (tw (a) + tw (d)), Y (s) }; break;
            case Stage::Release: pt = { X (tw (a) + tw (d) + hold + tw (rel) * u), Y (s * std::exp (-u * 5.f) * (1.f - u)) }; break;
            case Stage::Idle:    continue;
        }
        drawDot (g, pt, k == 0, r);
    }
}

// ---------------------------------------------------------------------------
uint64_t PitchEnvDisplay::fingerprint() const
{
    uint64_t h = hashParams ({ P::PenvAmt, P::PenvAttack, P::PenvDecay, P::PenvCurve });
    for (int k = 0; k < view.numVoices; ++k)
        h = mixPos (h, (float) view.voices[(size_t) k].env[3].stage + view.voices[(size_t) k].env[3].pos);
    return h;
}

void PitchEnvDisplay::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    drawGrid (g, r, 6, 2);
    const float amt = proc.plain (P::PenvAmt) / 60.f;
    const float a = proc.plain (P::PenvAttack), d = proc.plain (P::PenvDecay), c = proc.plain (P::PenvCurve);
    const float total = std::max (a + d, 1.f) * 1.15f;
    auto area = r.reduced (4.f, 6.f);
    const auto X = [&] (float t) { return area.getX() + area.getWidth() * t / total; };
    const auto Y = [&] (float v) { return area.getCentreY() - area.getHeight() * 0.5f * v; };
    juce::Path p;
    p.startNewSubPath (X (0), Y (0));
    p.lineTo (X (a), Y (amt));
    for (int i = 1; i <= 40; ++i)
    {
        const float u = (float) i / 40.f;
        p.lineTo (X (a + d * u), Y (amt * std::pow (1.f - u, 1.f + c * 7.f)));
    }
    p.lineTo (X (total), Y (0));
    strokeCurve (g, p, area, false);

    // the voices: attack, decay, then resting at the end while the note sounds
    for (int k = view.numVoices - 1; k >= 0; --k)
    {
        const auto& e = view.voices[(size_t) k].env[3];
        const auto pt = e.stage == 1 ? juce::Point<float> (X (a * e.pos), Y (amt * e.pos))
                      : e.stage == 2 ? juce::Point<float> (X (a + d * e.pos), Y (amt * std::pow (1.f - e.pos, 1.f + c * 7.f)))
                                     : juce::Point<float> (X (a + d), Y (0));
        drawDot (g, pt, k == 0, r);
    }
}

// ---------------------------------------------------------------------------
uint64_t LfoDisplay::fingerprint() const
{
    uint64_t h = hashParams ({ psy::lfoParam (lfo, 0), psy::lfoParam (lfo, 5) }) + (uint64_t) lfo * 31u;
    for (int k = 0; k < view.numVoices; ++k)
        h = mixPos (h, view.voices[(size_t) k].lfo[(size_t) lfo]);
    return h;
}

void LfoDisplay::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    drawGrid (g, r, 4, 2);
    const int shape = (int) proc.plain (psy::lfoParam (lfo, 0));
    const float phase = proc.plain (psy::lfoParam (lfo, 5));
    auto area = r.reduced (4.f, 8.f);
    juce::Path p;
    psy::dsp::Rng rng (5);
    float held[4], rands[5];
    for (auto& v : rands) v = rng.bipolar();
    for (int i = 0; i < 4; ++i) held[i] = rands[i + 1];
    // two cycles from the start phase: x 0..1 across the display
    const auto valueAt = [&] (float x) {
        const float t = 2.f * x + phase;
        const float ph = t - std::floor (t);
        const int cyc = std::min (3, (int) t);
        switch (shape)
        {
            case 0: return std::sin (psy::dsp::kTwoPi * ph);
            case 1: return ph < 0.25f ? 4.f * ph : (ph < 0.75f ? 2.f - 4.f * ph : 4.f * ph - 4.f);
            case 2: return 2.f * ph - 1.f;
            case 3: return 1.f - 2.f * ph;
            case 4: return ph < 0.5f ? 1.f : -1.f;
            case 5: return held[cyc];
            default:
            {
                const float s = 0.5f - 0.5f * std::cos (psy::dsp::kPi * ph);
                return rands[cyc] + (rands[cyc + 1] - rands[cyc]) * s;
            }
        }
    };
    const auto pointAt = [&] (float x) {
        return juce::Point<float> (area.getX() + area.getWidth() * x, area.getCentreY() - valueAt (x) * area.getHeight() * 0.5f);
    };
    const int n = 200;
    for (int i = 0; i <= n; ++i)
    {
        const auto pt = pointAt ((float) i / (float) n);
        if (i == 0) p.startNewSubPath (pt);
        else p.lineTo (pt);
    }
    strokeCurve (g, p, area, false);

    // the voices; a free-running LFO is one for all of them
    const bool perVoice = proc.plain (psy::lfoParam (lfo, 4)) > 0.5f;
    for (int k = perVoice ? view.numVoices - 1 : std::min (view.numVoices, 1) - 1; k >= 0; --k)
    {
        const float x = std::fmod (view.voices[(size_t) k].lfo[(size_t) lfo] - phase + 2.f, 2.f) * 0.5f;
        drawDot (g, pointAt (x), k == 0, r);
    }
}

// ---------------------------------------------------------------------------
namespace
{
    constexpr int kMeterFps = 40;
    constexpr float kMeterFloorDb = -48.f;
    constexpr float kMeterFallDb = 24.f / (float) kMeterFps;   // 24 dB/s
    constexpr int kHoldFrames = kMeterFps * 3 / 2;             // 1.5 s
    constexpr int kClipFrames = kMeterFps * 2;
    constexpr float kMeterW = 92.f;                            // meter strip at the right of the window
}

Scope::Scope (PsyBoxProcessor& p) : DisplayBase (p)
{
    buf.resize (2048);
    stopTimer();
    startTimerHz (kMeterFps);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Output. Drag out to export this sound as a WAV file");
}

void Scope::timerCallback()
{
    for (int c = 0; c < 2; ++c)
    {
        const auto lv = proc.takeOutLevel (c);
        auto& m = meters[(size_t) c];
        const float pk = juce::Decibels::gainToDecibels (lv.peak, -100.f);
        m.peakDb = std::max (pk, m.peakDb - kMeterFallDb);
        m.rmsDb = juce::Decibels::gainToDecibels (lv.rms, -100.f);
        if (pk >= m.holdDb) { m.holdDb = pk; m.holdFrames = kHoldFrames; }
        else if (m.holdFrames > 0) --m.holdFrames;
        else m.holdDb -= kMeterFallDb;
        if (lv.peak >= 1.f) m.clipFrames = kClipFrames;
        else if (m.clipFrames > 0) --m.clipFrames;
    }
    repaint();
}

void Scope::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6)
        return;
    dragging = true;
    repaint();
    const auto file = proc.bounceToFile();
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                     [this] { dragging = false; repaint(); });
    else
        dragging = false;
}

void Scope::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto meter = r.removeFromRight (kMeterW);
    g.setColour (col::line);
    g.fillRect (meter.getX(), meter.getY() + 4.f, 1.f, meter.getHeight() - 8.f);
    paintMeter (g, meter.withTrimmedLeft (1.f));
    paintWave (g, r);

    if (isMouseOver() || dragging)
    {
        g.setColour (col::well.withAlpha (0.75f));
        g.fillRect (r);
        g.setColour (col::text.withAlpha (dragging ? 0.9f : 0.7f));
        g.setFont (heading (9.5f));
        g.drawText (dragging ? "DROP TO EXPORT WAV" : "DRAG TO EXPORT WAV", r.toNearestInt(), juce::Justification::centred, false);
    }
}

void Scope::paintWave (juce::Graphics& g, juce::Rectangle<float> r)
{
    drawGrid (g, r, 6, 2);
    proc.copyScope (buf.data(), (int) buf.size());

    // trigger on the last rising zero crossing that leaves a full window
    const int win = 1024;
    int start = (int) buf.size() - win;
    for (int i = (int) buf.size() - win - 1; i > 1; --i)
        if (buf[(size_t) i - 1] < 0.f && buf[(size_t) i] >= 0.f) { start = i; break; }

    float pk = 0.f;
    for (int i = 0; i < win; ++i) pk = std::max (pk, std::abs (buf[(size_t) (start + i)]));
    const float gain = pk > 0.02f ? 0.9f / pk : 1.f;  // auto-scale, like a scope on auto

    auto area = r.reduced (4.f, 3.f);
    juce::Path p;
    const int w = (int) area.getWidth();
    for (int px = 0; px <= w; ++px)
    {
        const int i = start + px * win / std::max (1, w);
        const float v = juce::jlimit (-1.f, 1.f, buf[(size_t) std::min (i, (int) buf.size() - 1)] * gain);
        const float y = area.getCentreY() - v * area.getHeight() * 0.5f;
        if (px == 0) p.startNewSubPath (area.getX(), y);
        else p.lineTo (area.getX() + (float) px, y);
    }
    g.setColour (accent().withAlpha (0.12f));
    g.strokePath (p, juce::PathStrokeType (3.f));
    g.setColour (accent());
    g.strokePath (p, juce::PathStrokeType (1.3f));
}

// Two bars (L over R) on a -48..0 dB scale, the held peak of both as a number.
void Scope::paintMeter (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto red = juce::Colour (0xffff5a4a);
    auto area = r.reduced ((float) theme::sp::sm, 0.f);
    const auto text = area.removeFromRight (28.f);
    area.removeFromRight ((float) theme::sp::xs);
    const auto xOf = [&] (float db) {
        return area.getX() + area.getWidth() * juce::jlimit (0.f, 1.f, (db - kMeterFloorDb) / -kMeterFloorDb);
    };

    constexpr float barH = 3.f, gap = 3.f;
    float y = r.getCentreY() - barH - gap * 0.5f;
    float hold = -100.f;
    bool clip = false;
    for (const auto& m : meters)
    {
        const juce::Rectangle<float> track (area.getX(), y, area.getWidth(), barH);
        g.setColour (col::lineStrong);
        g.fillRect (track);
        g.setColour (accent().withAlpha (0.35f));
        g.fillRect (track.withRight (xOf (m.peakDb)));
        g.setColour (accent());
        g.fillRect (track.withRight (xOf (m.rmsDb)));
        if (m.holdDb > kMeterFloorDb)
        {
            g.setColour (m.clipFrames > 0 ? red : col::text.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (std::min (xOf (m.holdDb), track.getRight() - 1.5f), y - 1.f, 1.5f, barH + 2.f));
        }
        hold = std::max (hold, m.holdDb);
        clip = clip || m.clipFrames > 0;
        y += barH + gap;
    }

    g.setColour (clip ? red : col::textDim);
    g.setFont (mono (9.5f));
    g.drawText (hold > kMeterFloorDb ? juce::String (hold, 1) : juce::String ("-inf"), text.toNearestInt(),
                juce::Justification::centredRight, false);
}

// ---------------------------------------------------------------------------
// Kick displays
// ---------------------------------------------------------------------------
psy::ParamState KickDisplay::patch() const
{
    psy::ParamState s;
    for (int i = 0; i < psy::kNumParams; ++i)
        s.v[(size_t) i] = proc.plain ((P) i);
    return s;
}

uint64_t KickDisplay::fingerprint() const
{
    uint64_t h = hashParams ({ P::VelSens }) ^ ((uint64_t) note() * 0x9E3779B97F4A7C15ULL);
    for (int i = (int) P::KickTune; i <= (int) P::KickTone; ++i)
    {
        float v = proc.plain ((P) i);
        uint32_t bits;
        std::memcpy (&bits, &v, 4);
        h = (h ^ bits) * 1099511628211ULL;
    }
    return h;
}

namespace
{
    constexpr float kKickFs = 48000.f;

    juce::String hzText (float hz)
    {
        return hz >= 1000.f ? juce::String (hz / 1000.f, 1) + "k" : juce::String (juce::roundToInt (hz));
    }

    // Min/max per pixel column, drawn as a filled band around the centre line.
    void drawWave (juce::Graphics& g, const std::vector<float>& x, int count, juce::Rectangle<float> r, float scale)
    {
        const int cols = juce::jmax (1, (int) r.getWidth());
        juce::Path p;
        std::vector<float> lo ((size_t) cols), hi ((size_t) cols);
        for (int c = 0; c < cols; ++c)
        {
            const int a = c * count / cols, b = juce::jmax (a + 1, (c + 1) * count / cols);
            float mn = 0.f, mx = 0.f;
            for (int i = a; i < b && i < count; ++i) { mn = std::min (mn, x[(size_t) i]); mx = std::max (mx, x[(size_t) i]); }
            lo[(size_t) c] = mn;
            hi[(size_t) c] = mx;
        }
        const float cy = r.getCentreY(), half = r.getHeight() * 0.5f * scale;
        p.startNewSubPath (r.getX(), cy - hi[0] * half);
        for (int c = 1; c < cols; ++c) p.lineTo (r.getX() + (float) c, cy - hi[(size_t) c] * half);
        for (int c = cols - 1; c >= 0; --c) p.lineTo (r.getX() + (float) c, cy - lo[(size_t) c] * half);
        p.closeSubPath();
        g.setColour (accent().withAlpha (0.75f));
        g.fillPath (p);
    }
} // namespace

void KickPitchDisplay::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto s = psy::KickSettings::from (patch());
    const int n = note();
    const float body = psy::kickBodyHz (s, n);
    const float totalMs = juce::jlimit (40.f, 400.f, 3.6f * s.sweepMs);
    auto area = r.reduced (6.f, 8.f);
    const float lo = std::log (20.f), hiF = std::log (12000.f);
    const auto X = [&] (float ms) { return area.getX() + area.getWidth() * std::sqrt (ms / totalMs); };   // the drop is fast
    const auto Y = [&] (float hz) { return area.getBottom() - area.getHeight() * (std::log (hz) - lo) / (hiF - lo); };

    g.setFont (mono (9.f));
    for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f })
    {
        g.setColour (col::grid);
        g.fillRect (r.getX(), Y (f), r.getWidth(), 1.f);
        g.setColour (col::textFaint);
        g.drawText (hzText (f), juce::Rectangle<float> (r.getRight() - 32.f, Y (f) - 11.f, 28.f, 10.f), juce::Justification::centredRight, false);
    }

    juce::Path p;
    for (int i = 0; i <= 160; ++i)
    {
        const float ms = totalMs * (float) (i * i) / (160.f * 160.f);
        const float y = Y (juce::jlimit (20.f, 12000.f, psy::kickPitchAt (s, n, ms * 0.001f)));
        if (i == 0) p.startNewSubPath (X (ms), y);
        else p.lineTo (X (ms), y);
    }
    strokeCurve (g, p, area, true);

    // start, knee and body markers
    const float r0 = body / s.tuneHz;
    const float start = s.startHz * r0, knee = juce::jlimit (body, std::max (start, body), s.kneeHz * r0);
    const float kneeMs = 3.f * s.snapMs;
    g.setColour (accent());
    for (auto pt : { juce::Point<float> (X (0.f), Y (start)), juce::Point<float> (X (kneeMs), Y (psy::kickPitchAt (s, n, kneeMs * 0.001f))),
                     juce::Point<float> (X (3.f * s.sweepMs), Y (body)) })
        g.fillEllipse (pt.x - 2.5f, pt.y - 2.5f, 5.f, 5.f);
    g.setColour (col::textDim);
    g.drawText (hzText (start), juce::Rectangle<float> (X (0.f) + 6.f, Y (start) - 16.f, 40.f, 10.f), juce::Justification::centredLeft, false);
    g.drawText (hzText (knee), juce::Rectangle<float> (X (kneeMs) + 6.f, Y (knee) - 12.f, 40.f, 10.f), juce::Justification::centredLeft, false);
}

void KickEnvDisplay::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto st = patch();
    const auto s = psy::KickSettings::from (st);
    const float totalMs = juce::jlimit (60.f, 1500.f, (s.attackMs + s.holdMs + s.decayMs) * 1.1f + 10.f);
    constexpr float fs = 8000.f;   // plenty for an envelope
    const int count = (int) (totalMs * 0.001f * fs);
    if (fingerprint() != cached || (int) env.size() != count)
    {
        env.assign ((size_t) count, 0.f);
        psy::KickVoice::renderOffline (st, note(), 1.f, fs, count, nullptr, env.data());
        cached = fingerprint();
    }
    drawGrid (g, r, 6, 2);
    auto area = r.reduced (4.f, 8.f);
    const auto X = [&] (int i) { return area.getX() + area.getWidth() * (float) i / (float) juce::jmax (1, count - 1); };
    const auto Y = [&] (float v) { return area.getBottom() - area.getHeight() * v; };
    juce::Path p;
    p.startNewSubPath (X (0), Y (0.f));
    for (int i = 0; i < count; i += 2) p.lineTo (X (i), Y (env[(size_t) i]));
    p.lineTo (X (count - 1), Y (0.f));
    strokeCurve (g, p, area, true);

    // where the hold ends and the decay starts
    const float holdEnd = (s.attackMs + s.holdMs) / totalMs;
    g.setColour (col::textFaint);
    g.fillRect (area.getX() + area.getWidth() * holdEnd, area.getY(), 1.f, area.getHeight());
}

void KickClickDisplay::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto st = patch();
    const auto s = psy::KickSettings::from (st);
    const float ms = juce::jlimit (3.f, 40.f, s.clickMs * 1.2f);
    const int count = (int) (ms * 0.001f * kKickFs);
    if (fingerprint() != cached || (int) click.size() != count)
    {
        // the click alone: the hit with it minus the hit without it
        std::vector<float> without ((size_t) count);
        click.assign ((size_t) count, 0.f);
        psy::KickVoice::renderOffline (st, note(), 1.f, kKickFs, count, click.data());
        st.set (P::KickClickLevel, 0.f);
        psy::KickVoice::renderOffline (st, note(), 1.f, kKickFs, count, without.data());
        for (int i = 0; i < count; ++i) click[(size_t) i] -= without[(size_t) i];
        cached = fingerprint();
    }
    drawGrid (g, r, 4, 2);
    if (s.clickLevel <= 0.f)
    {
        g.setColour (col::textFaint);
        g.setFont (label (10.f));
        g.drawText ("NO CLICK", r, juce::Justification::centred, false);
        return;
    }
    // scaled to its own peak: this view is about the shape, Level sets the loudness
    float pk = 1e-6f;
    for (float v : click) pk = std::max (pk, std::abs (v));
    drawWave (g, click, count, r.reduced (4.f, 6.f), 0.9f / pk);
}

void KickDriveDisplay::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto s = psy::KickSettings::from (patch());
    drawGrid (g, r, 4, 4);
    auto area = r.reduced (8.f);
    g.setColour (col::lineStrong);
    g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getY(), 1.f);   // clean
    const double gain = psy::dsp::dbToGain (s.driveDb);
    const double makeup = 1.0 / std::max ({ std::abs (psy::dsp::AdaaShaper::f (gain, s.shaper)), std::abs (psy::dsp::AdaaShaper::f (-gain, s.shaper)), 1e-6 });
    juce::Path p;
    for (int i = 0; i <= 100; ++i)
    {
        const double x = -1.0 + 2.0 * i / 100.0;
        const float y = (float) (psy::dsp::AdaaShaper::f (x * gain, s.shaper) * makeup);
        const float px = area.getX() + area.getWidth() * (float) i / 100.f;
        const float py = area.getCentreY() - area.getHeight() * 0.5f * juce::jlimit (-1.f, 1.f, y);
        if (i == 0) p.startNewSubPath (px, py);
        else p.lineTo (px, py);
    }
    strokeCurve (g, p, area, false);
}

void KickWaveDisplay::ensure() const
{
    if (fingerprint() == cached && ! wave.empty()) return;
    const auto st = patch();
    const auto s = psy::KickSettings::from (st);
    totalMs = juce::jlimit (150.f, 1200.f, (s.attackMs + s.holdMs + s.decayMs) * 1.15f + 20.f);
    const int count = (int) (totalMs * 0.001f * kKickFs);
    wave.assign ((size_t) count, 0.f);
    hz.assign ((size_t) count, 0.f);
    psy::KickVoice::renderOffline (st, note(), 1.f, kKickFs, count, wave.data(), nullptr, hz.data());
    int last = 0;
    for (int i = 0; i < count; ++i)
        if (std::abs (wave[(size_t) i]) > 0.0005f) last = i;   // -60 dB below the 0.5 body peak
    length = (float) last * 1000.f / kKickFs;
    cached = fingerprint();
}

void KickWaveDisplay::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    ensure();
    const auto s = psy::KickSettings::from (patch());
    const int count = (int) wave.size();

    auto area = r.reduced (6.f, 6.f).withTrimmedBottom (12.f);
    // time ruler
    const float step = totalMs > 600.f ? 100.f : 50.f;
    g.setFont (mono (9.f));
    for (float t = step; t < totalMs; t += step)
    {
        const float x = area.getX() + area.getWidth() * t / totalMs;
        g.setColour (col::grid);
        g.fillRect (x, r.getY(), 1.f, area.getBottom() - r.getY());
        g.setColour (col::textFaint);
        g.drawText (juce::String (juce::roundToInt (t)), juce::Rectangle<float> (x - 20.f, area.getBottom() + 1.f, 40.f, 11.f),
                    juce::Justification::centred, false);
    }
    g.setColour (col::grid);
    g.fillRect (area.getX(), area.getCentreY(), area.getWidth(), 1.f);

    drawWave (g, wave, count, area, 1.9f);

    // pitch sweep on a log axis over the wave, faint
    const float lo = std::log (25.f), hiF = std::log (10000.f);
    juce::Path p;
    const int cols = juce::jmax (2, (int) area.getWidth());
    for (int c = 0; c < cols; ++c)
    {
        const int i = c * (count - 1) / (cols - 1);
        const float f = juce::jlimit (25.f, 10000.f, hz[(size_t) i] > 0.f ? hz[(size_t) i] : psy::kickBodyHz (s, note()));
        const float y = area.getBottom() - area.getHeight() * (std::log (f) - lo) / (hiF - lo);
        if (c == 0) p.startNewSubPath (area.getX(), y);
        else p.lineTo (area.getX() + (float) c, y);
    }
    g.setColour (col::text.withAlpha (0.35f));
    g.strokePath (p, juce::PathStrokeType (1.f));
}

// ---------------------------------------------------------------------------
// CompDisplay
// ---------------------------------------------------------------------------
namespace
{
    constexpr float kMbBottomDb = -72.f;    // Multiband level axis: 0 dB at the top
    constexpr float kNormLoDb = -60.f;      // Normal: -60..0 dB in and out
    constexpr float kLoHz = 20.f, kHiHz = 20000.f;
    constexpr float kGrabPx = 6.f;
    constexpr float kGrRangeDb = 24.f;      // full height of the Normal gain-reduction bar
}

juce::Rectangle<float> CompDisplay::plot() const
{
    const auto r = getLocalBounds().toFloat().reduced (1.f);
    if (multi()) return r.withTrimmedTop (15.f).withTrimmedBottom (13.f);   // readouts above, splits below
    return r.reduced (8.f).withTrimmedRight (8.f);                           // gain-reduction bar on the right
}

float CompDisplay::xOfHz (float hz) const
{
    const auto p = plot();
    return p.getX() + p.getWidth() * std::log (hz / kLoHz) / std::log (kHiHz / kLoHz);
}

float CompDisplay::hzOfX (float x) const
{
    const auto p = plot();
    return kLoHz * std::pow (kHiHz / kLoHz, juce::jlimit (0.f, 1.f, (x - p.getX()) / p.getWidth()));
}

float CompDisplay::yOfDb (float db) const
{
    const auto p = plot();
    return p.getY() + p.getHeight() * db / (multi() ? kMbBottomDb : kNormLoDb);
}

float CompDisplay::dbOfY (float y) const
{
    const auto p = plot();
    return (y - p.getY()) / p.getHeight() * (multi() ? kMbBottomDb : kNormLoDb);
}

float CompDisplay::xOfDb (float db) const
{
    const auto p = plot();
    return p.getX() + p.getWidth() * (db - kNormLoDb) / -kNormLoDb;
}

float CompDisplay::dbOfX (float x) const
{
    const auto p = plot();
    return kNormLoDb - (x - p.getX()) / p.getWidth() * kNormLoDb;
}

int CompDisplay::bandAt (float x) const
{
    if (x < xOfHz (proc.plain (P::MbXLow))) return 0;
    return x < xOfHz (proc.plain (P::MbXHigh)) ? 1 : 2;
}

CompDisplay::Target CompDisplay::targetAt (juce::Point<float> pt) const
{
    if (! getLocalBounds().toFloat().contains (pt)) return {};
    if (! multi()) return { P::CompThresh, P::Count, true };   // the threshold follows the input axis

    if (std::abs (pt.x - xOfHz (proc.plain (P::MbXLow))) <= kGrabPx) return { P::MbXLow, P::Count, false };
    if (std::abs (pt.x - xOfHz (proc.plain (P::MbXHigh))) <= kGrabPx) return { P::MbXHigh, P::Count, false };
    const int b = bandAt (pt.x);
    const auto down = psy::mbDownParam (b), up = psy::mbUpParam (b);
    const float dd = std::abs (pt.y - yOfDb (proc.plain (down)));
    const float du = std::abs (pt.y - yOfDb (proc.plain (up)));
    return dd <= du ? Target { down, up, true } : Target { up, down, false };
}

void CompDisplay::setPlain (P p, float plain)
{
    proc.param (p)->setValueNotifyingHost (psy::def (p).toNorm (plain));
}

void CompDisplay::mouseMove (const juce::MouseEvent& e)
{
    const auto t = targetAt (e.position);
    if (t.param != hover.param) { hover = t; repaint(); }
    const bool sideways = t.param == P::MbXLow || t.param == P::MbXHigh || t.param == P::CompThresh;
    setMouseCursor (sideways ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
}

void CompDisplay::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

void CompDisplay::mouseDown (const juce::MouseEvent& e)
{
    drag = targetAt (e.position);
    if (drag.param == P::Count) return;
    for (auto p : { drag.param, drag.partner })
        if (p != P::Count) proc.param (p)->beginChangeGesture();

    // Grab relative to the handle, so a click never makes the value jump.
    const float v = proc.plain (drag.param);
    if (drag.param == P::MbXLow || drag.param == P::MbXHigh) grabOffset = e.position.x - xOfHz (v);
    else if (drag.param == P::CompThresh) grabOffset = e.position.x - xOfDb (v);
    else grabOffset = e.position.y - yOfDb (v);
}

void CompDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (drag.param == P::Count) return;
    if (drag.param == P::MbXLow)
        setPlain (P::MbXLow, std::min (hzOfX (e.position.x - grabOffset), 0.5f * proc.plain (P::MbXHigh)));
    else if (drag.param == P::MbXHigh)
        setPlain (P::MbXHigh, std::max (hzOfX (e.position.x - grabOffset), 2.f * proc.plain (P::MbXLow)));
    else if (drag.param == P::CompThresh)
        setPlain (P::CompThresh, dbOfX (e.position.x - grabOffset));
    else
    {
        const auto& d = psy::def (drag.param);
        const float v = juce::jlimit (d.min, d.max, dbOfY (e.position.y - grabOffset));
        setPlain (drag.param, v);
        // up threshold <= down threshold: the dragged one pushes the other along
        const float other = proc.plain (drag.partner);
        if (drag.isDown ? other > v : other < v) setPlain (drag.partner, v);
    }
    repaint();
}

void CompDisplay::mouseUp (const juce::MouseEvent&)
{
    for (auto p : { drag.param, drag.partner })
        if (p != P::Count) proc.param (p)->endChangeGesture();
    drag = {};
    repaint();
}

void CompDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto t = targetAt (e.position);
    if (t.param == P::Count) return;
    for (auto p : { t.param, t.partner })
        if (p != P::Count) proc.param (p)->beginChangeGesture();
    const float v = psy::def (t.param).def;
    setPlain (t.param, v);
    if (t.partner != P::Count)
    {
        const float other = proc.plain (t.partner);
        if (t.isDown ? other > v : other < v) setPlain (t.partner, v);
    }
    for (auto p : { t.param, t.partner })
        if (p != P::Count) proc.param (p)->endChangeGesture();
    repaint();
}

void CompDisplay::timerCallback()
{
    if (proc.plain (P::CompOn) > 0.5f) repaint();   // live meters
    else DisplayBase::timerCallback();
}

uint64_t CompDisplay::fingerprint() const
{
    return hashParams ({ P::CompOn, P::CompMode, P::CompThresh, P::CompRatio, P::CompKnee, P::MbXLow, P::MbXHigh,
                         P::MbLowDown, P::MbMidDown, P::MbHighDown, P::MbLowUp, P::MbMidUp, P::MbHighUp })
         ^ ((uint64_t) hover.param << 40) ^ ((uint64_t) drag.param << 50);
}

void CompDisplay::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    const bool on = proc.plain (P::CompOn) > 0.5f;
    if (multi()) paintMulti (g, r, on);
    else paintNormal (g, r, on);
    if (! on)
    {
        g.setColour (col::well.withAlpha (0.4f));   // still editable, visibly off
        g.fillRect (r);
    }
}

void CompDisplay::paintMulti (juce::Graphics& g, juce::Rectangle<float> r, bool on)
{
    const auto p = plot();
    const auto acc = accent();
    const float splits[2] = { proc.plain (P::MbXLow), proc.plain (P::MbXHigh) };
    const float xs[4] = { r.getX(), xOfHz (splits[0]), xOfHz (splits[1]), r.getRight() };

    g.setColour (col::grid);
    for (float db = -12.f; db > kMbBottomDb; db -= 12.f)
        g.fillRect (r.getX(), yOfDb (db), r.getWidth(), 1.f);

    g.setFont (mono (9.f));
    static const char* names[3] = { "LOW", "MID", "HIGH" };
    for (int b = 0; b < 3; ++b)
    {
        const float x0 = xs[b], x1 = xs[b + 1], cx = 0.5f * (x0 + x1);
        const float down = proc.plain (psy::mbDownParam (b));
        const float yDown = yOfDb (down), yUp = yOfDb (std::min (proc.plain (psy::mbUpParam (b)), down));
        const juce::Rectangle<float> column (x0, p.getY(), x1 - x0, p.getHeight());

        // above the down threshold gets pushed down, below the up threshold lifted
        g.setColour (acc.withAlpha (on ? 0.11f : 0.05f));
        g.fillRect (column.withBottom (yDown));
        g.setColour (acc.withAlpha (on ? 0.05f : 0.025f));
        g.fillRect (column.withTop (yUp));

        const auto readout = juce::Rectangle<float> (x0, r.getY() + 2.f, x1 - x0, 11.f);
        if (on)
        {
            const auto m = proc.getCompReading (b);
            const float bw = juce::jlimit (3.f, 6.f, (x1 - x0) * 0.1f);
            const auto bar = [&] (float x, float db, juce::Colour c) {
                const float y = juce::jlimit (p.getY(), p.getBottom(), yOfDb (db));
                g.setColour (c);
                g.fillRect (juce::Rectangle<float> (x, y, bw, p.getBottom() - y));
            };
            bar (cx - bw - 1.f, m.inDb, col::textDim.withAlpha (0.5f));        // in
            bar (cx + 1.f, m.inDb + m.totalDb, acc.withAlpha (0.9f));           // out
            const bool moving = std::abs (m.gainDb) >= 0.05f;
            g.setColour (moving ? col::text : col::textFaint);
            g.drawText ((m.gainDb > 0.f ? "+" : "") + juce::String (moving ? m.gainDb : 0.f, 1), readout, juce::Justification::centred, false);
        }
        else
        {
            g.setColour (col::textFaint);
            g.drawText (names[b], readout, juce::Justification::centred, false);
        }

        // thresholds: a line across the band and a handle in the middle
        const auto line = [&] (float y, P prm, float alpha) {
            const bool hot = hover.param == prm || drag.param == prm;
            g.setColour (acc.withAlpha (alpha * (on ? 1.f : 0.55f)));
            g.fillRect (x0 + 2.f, y - (hot ? 1.f : 0.5f), x1 - x0 - 4.f, hot ? 2.f : 1.f);
            const float hw = hot ? 14.f : 10.f;
            g.fillRoundedRectangle (cx - 0.5f * hw, y - 2.f, hw, 4.f, 2.f);
        };
        line (yUp, psy::mbUpParam (b), 0.6f);
        line (yDown, psy::mbDownParam (b), 1.f);
    }

    for (int i = 0; i < 2; ++i)
    {
        const auto prm = i == 0 ? P::MbXLow : P::MbXHigh;
        const bool hot = hover.param == prm || drag.param == prm;
        g.setColour (hot ? col::textDim : col::lineStrong);
        g.fillRect (xs[i + 1] - (hot ? 1.f : 0.5f), r.getY(), hot ? 2.f : 1.f, r.getHeight());
        g.setColour (hot ? col::text : col::textFaint);
        g.drawText (hzText (splits[i]), juce::Rectangle<float> (xs[i + 1] - 20.f, p.getBottom() + 2.f, 40.f, 10.f),
                    juce::Justification::centred, false);
    }
}

void CompDisplay::paintNormal (juce::Graphics& g, juce::Rectangle<float> r, bool on)
{
    const auto p = plot();
    const auto acc = accent();
    const float thr = proc.plain (P::CompThresh), knee = proc.plain (P::CompKnee);
    const float slope = 1.f - 1.f / std::max (proc.plain (P::CompRatio), 1.f);
    const auto out = [&] (float in) { return in + psy::dsp::dyn::downGain (in, thr, slope, knee); };

    g.setColour (col::grid);
    for (float db = -12.f; db > kNormLoDb; db -= 12.f)
    {
        g.fillRect (xOfDb (db), p.getY(), 1.f, p.getHeight());
        g.fillRect (p.getX(), yOfDb (db), p.getWidth(), 1.f);
    }
    g.setColour (col::lineStrong);
    g.drawLine (xOfDb (kNormLoDb), yOfDb (kNormLoDb), xOfDb (0.f), yOfDb (0.f), 1.f);   // 1:1

    juce::Path curve;
    for (int i = 0; i <= 120; ++i)
    {
        const float in = kNormLoDb * (1.f - (float) i / 120.f);
        const juce::Point<float> pt (xOfDb (in), yOfDb (out (in)));
        if (i == 0) curve.startNewSubPath (pt);
        else curve.lineTo (pt);
    }
    strokeCurve (g, curve, p, true);

    // threshold handle and value
    const bool hot = hover.param == P::CompThresh || drag.param == P::CompThresh;
    const juce::Point<float> tp (xOfDb (thr), yOfDb (out (thr)));
    const float d = hot ? 8.f : 6.f;
    g.setColour (acc);
    g.fillEllipse (tp.x - 0.5f * d, tp.y - 0.5f * d, d, d);
    g.setFont (mono (9.f));
    g.setColour (hot ? col::text : col::textDim);
    const bool leftSide = tp.x > p.getCentreX();
    g.drawText (juce::String (thr, 1) + " dB", juce::Rectangle<float> (leftSide ? tp.x - 58.f : tp.x + 6.f, tp.y - 14.f, 52.f, 10.f),
                leftSide ? juce::Justification::centredRight : juce::Justification::centredLeft, false);

    if (! on) return;
    const auto m = proc.getCompReading (0);
    if (m.inDb > kNormLoDb)
    {
        const juce::Point<float> lp (xOfDb (juce::jmin (m.inDb, 0.f)), yOfDb (juce::jlimit (kNormLoDb, 0.f, m.inDb + m.gainDb)));
        g.setColour (col::text);
        g.fillEllipse (lp.x - 2.5f, lp.y - 2.5f, 5.f, 5.f);
    }
    // gain reduction, hanging from the top
    const float gr = juce::jlimit (0.f, kGrRangeDb, -m.gainDb);
    const juce::Rectangle<float> track (r.getRight() - 7.f, p.getY(), 3.f, p.getHeight());
    g.setColour (col::grid);
    g.fillRect (track);
    g.setColour (acc);
    g.fillRect (track.withHeight (track.getHeight() * gr / kGrRangeDb));
    g.setColour (gr >= 0.05f ? col::text : col::textFaint);
    g.drawText ("-" + juce::String (gr, 1), juce::Rectangle<float> (p.getX() + 2.f, p.getY(), 40.f, 10.f), juce::Justification::centredLeft, false);
}
