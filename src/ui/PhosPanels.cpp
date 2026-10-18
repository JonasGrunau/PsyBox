#include "PhosPanels.h"

#include "dsp/PhosEffects.h"
#include "engine/PhosVoice.h"

using namespace theme;
using psy::P;

namespace
{
    // The patch's 303 settings as the editor sees them now.
    psy::PhosSettings settings (const PsyBoxProcessor& p)
    {
        psy::ParamState st;
        for (int i = (int) P::PhosWave; i <= (int) P::PhosNoise; ++i)
            st.v[(size_t) i] = p.plain ((P) i);
        return psy::PhosSettings::from (st);
    }

    juce::String hzText (float hz)
    {
        return hz >= 1000.f ? juce::String (hz / 1000.f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
    }
} // namespace

// ---------------------------------------------------------------------------
// PhosFilterDisplay
// ---------------------------------------------------------------------------
float PhosFilterDisplay::liveCutoff() const
{
    return view.numVoices > 0 ? view.voices[0].filter[0].cutoff : 0.f;
}

float PhosFilterDisplay::restingCutoff() const
{
    return std::exp2 (psy::PhosVoice::cutoffOctaves (settings (proc), {}, 0.f, 0.f, psy::PhosModel::standard()));
}

float PhosFilterDisplay::cutoffNow() const
{
    const float live = liveCutoff();
    return live > 0.f ? live : restingCutoff();
}

uint64_t PhosFilterDisplay::fingerprint() const
{
    auto h = hashParams ({ P::PhosCutoff, P::PhosReso, P::PhosEnvMod, P::PhosTm3, P::PhosC21 });
    return mixPos (h, liveCutoff() / 20000.f);
}

void PhosFilterDisplay::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto a = r.reduced (sp::sm, sp::sm);
    constexpr float lo = 20.f, hi = 20000.f, top = 24.f, bottom = -60.f;
    const auto xOf = [&] (float hz) { return a.getX() + a.getWidth() * std::log (hz / lo) / std::log (hi / lo); };
    // below the floor the curve runs out of the bottom instead of along it
    const auto yOf = [&] (float db) { return a.getY() + a.getHeight() * (top - juce::jlimit (bottom - 24.f, top, db)) / (top - bottom); };
    g.setColour (col::grid);
    for (float hz : { 100.f, 1000.f, 10000.f }) g.fillRect (xOf (hz), r.getY(), 1.f, r.getHeight());
    g.fillRect (a.getX(), yOf (0.f), a.getWidth(), 1.f);
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (a.toNearestInt().expanded (2));

    const auto s = settings (proc);
    const auto curve = [&] (float fc) {
        juce::Path p;
        const int n = juce::jmax (64, (int) a.getWidth() / 2);
        for (int i = 0; i <= n; ++i)
        {
            const float hz = lo * std::pow (hi / lo, (float) i / (float) n);
            // +8 dB: the passband at the reference level, so the peak reads against 0 dB
            const juce::Point<float> pt (xOf (hz), yOf (psy::PhosVoice::responseDb (fc, s.reso, hz) + 8.f));
            if (i == 0) p.startNewSubPath (pt);
            else p.lineTo (pt);
        }
        return p;
    };
    const float resting = restingCutoff();
    const float live = liveCutoff();
    if (live > 0.f)
    {
        strokeLine (g, curve (resting), col::lineStrong, 1.f);
        const auto p = curve (live);
        fillUnder (g, p, a, accent (*this), 0.14f);
        strokeLine (g, p, accent (*this));
        drawDot (g, { xOf (live), yOf (psy::PhosVoice::responseDb (live, s.reso, live) + 8.f) }, true, a);
    }
    else
    {
        const auto p = curve (resting);
        fillUnder (g, p, a, accent (*this), 0.14f);
        strokeLine (g, p, accent (*this));
    }
}

// ---------------------------------------------------------------------------
// PhosEnvDisplay
// ---------------------------------------------------------------------------
uint64_t PhosEnvDisplay::fingerprint() const
{
    auto h = hashParams ({ P::PhosDecay, P::PhosAccDecay, P::PhosEnvAttack, P::PhosAccent, P::PhosAccVolume, P::PhosAccCouple,
                           P::PhosAccAEnv });
    if (view.numVoices > 0)
    {
        const auto& e = view.voices[0].env[vca ? 0 : 1];
        h = mixPos (h, e.pos + (float) e.stage);
    }
    return h;
}

void PhosEnvDisplay::paintContent (juce::Graphics& g, juce::Rectangle<float> r)
{
    drawGrid (g, r, 8, 4);
    const auto a = r.reduced (sp::sm, sp::md);
    const auto& M = psy::PhosModel::standard();
    const auto s = settings (proc);
    const float decay = psy::PhosVoice::decayMs (s.decay, M);
    const float accDecay = M.accDecayMs + (M.accDecayMaxMs - M.accDecayMs) * s.accDecay * s.accDecay;
    const float span = vca ? 1200.f : juce::jlimit (300.f, 6000.f, 3.f * std::max (decay, accDecay));   // ms across
    const auto xOf = [&] (float ms) { return a.getX() + a.getWidth() * ms / span; };
    const auto yOf = [&] (float v) { return a.getBottom() - a.getHeight() * juce::jlimit (0.f, 1.f, v); };
    const int n = juce::jmax (64, (int) a.getWidth() / 2);

    if (vca)
    {
        // the gate held for half the view: attack, the long decay, the release
        const float gateMs = span * 0.5f;
        const float accAmt = M.accAmp * (s.accVolumeCouple ? s.accent : s.accVolume) * s.accAmpEnvV / 1.0984f;
        const auto level = [&] (float ms, bool accent) {
            const float att = 1.f - std::exp (-ms / M.vcaAttackMs);
            float v = att * std::exp (-std::min (ms, gateMs) / M.vcaDecayMs);
            if (ms > gateMs) v *= std::exp (-(ms - gateMs) / M.vcaReleaseMs);
            if (accent) v *= 1.f + accAmt * std::exp (-ms / accDecay);
            return v / (1.f + accAmt);
        };
        for (int acc = 1; acc >= 0; --acc)
        {
            juce::Path p;
            for (int i = 0; i <= n; ++i)
            {
                const float ms = span * (float) i / (float) n;
                const juce::Point<float> pt (xOf (ms), yOf (level (ms, acc == 1)));
                if (i == 0) p.startNewSubPath (pt);
                else p.lineTo (pt);
            }
            if (acc == 0)
            {
                fillUnder (g, p, a, accent (*this), 0.14f);
                strokeLine (g, p, theme::accent (*this));
            }
            else
            {
                juce::Path dashed;
                const float dashes[] = { 4.f, 3.f };
                juce::PathStrokeType (1.2f).createDashedStroke (dashed, p, dashes, 2);
                g.setColour (theme::accent (*this).withAlpha (0.5f));
                g.fillPath (dashed);
            }
        }
        g.setColour (col::textFaint);
        g.fillRect (xOf (gateMs), a.getY(), 1.f, a.getHeight());
        return;
    }

    // MEG: from the top (after Env. Attack) down at the decay; the accent's dashed
    const float attackMs = std::max (0.05f, s.envAttack * s.envAttack * M.envAttackMaxMs);
    for (int acc = 1; acc >= 0; --acc)
    {
        const float tau = acc == 1 ? accDecay : decay;
        juce::Path p;
        for (int i = 0; i <= n; ++i)
        {
            const float ms = span * (float) i / (float) n;
            const float v = ms < attackMs ? ms / attackMs : std::exp (-(ms - attackMs) / tau);
            const juce::Point<float> pt (xOf (ms), yOf (v));
            if (i == 0) p.startNewSubPath (pt);
            else p.lineTo (pt);
        }
        if (acc == 0)
        {
            fillUnder (g, p, a, accent (*this), 0.14f);
            strokeLine (g, p, theme::accent (*this));
        }
        else
        {
            juce::Path dashed;
            const float dashes[] = { 4.f, 3.f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, p, dashes, 2);
            g.setColour (theme::accent (*this).withAlpha (0.5f));
            g.fillPath (dashed);
        }
    }
    if (view.numVoices > 0)
    {
        // where the sounding note's envelope is: its level, read back as time
        const float v = juce::jlimit (1e-4f, 1.f, 1.f - view.voices[0].env[1].pos);
        const float ms = attackMs - decay * std::log (v);
        if (ms < span) drawDot (g, { xOf (ms), yOf (v) }, true, a);
    }
}

// ---------------------------------------------------------------------------
// PhosSynthPanel
// ---------------------------------------------------------------------------
namespace
{
    constexpr int kWaveW = 64;      // the Saw / Square switch
    constexpr int kGroupGap = sp::xl;
}

PhosSynthPanel::PhosSynthPanel (PsyBoxProcessor& p)
    : Section ("Synthesis"), proc (p), wave (p, P::PhosWave)
{
    wave.setVertical (true);
    wave.setTooltip ("The saw, or the 303's square: made from the saw, its bottom sagging");
    addAndMakeVisible (wave);
    for (auto [prm, name] : { std::pair { P::PhosTune, "Tuning" }, std::pair { P::PhosCutoff, "Cutoff" },
                              std::pair { P::PhosReso, "Resonance" }, std::pair { P::PhosEnvMod, "Envmod" },
                              std::pair { P::PhosDecay, "Decay" }, std::pair { P::PhosAccent, "Accent" },
                              std::pair { P::PhosSlide, "Slide Time" }, std::pair { P::PhosSweep, "Sweep Amt." },
                              std::pair { P::PhosEnvAttack, "Env. Attack" }, std::pair { P::PhosAccDecay, "Acc. Decay" },
                              std::pair { P::PhosAccVolume, "Acc. Vol." }, std::pair { P::PhosVibSpeed, "Vib. Speed" },
                              std::pair { P::PhosVibDepth, "Vib. Depth" } })
        ks.add (*this, p, prm, name);
    for (auto [prm, tip] : { std::pair { P::PhosResoCouple, "Couple: Resonance also sets how fast the accent sweep charges (lit), or Sweep Amt. does it alone" },
                             std::pair { P::PhosResoCouple, "Couple: Resonance also sets how fast the accent sweep charges (lit), or Sweep Amt. does it alone" },
                             std::pair { P::PhosAccCouple, "Couple: Accent also sets the accent's volume (lit), or Acc. Vol. does" },
                             std::pair { P::PhosAccCouple, "Couple: Accent also sets the accent's volume (lit), or Acc. Vol. does" } })
    {
        leds.push_back (std::make_unique<ToggleDot> (p, prm, juce::String()));
        leds.back()->setTooltip (tip);
        addAndMakeVisible (*leds.back());
    }
    startTimerHz (8);
}

void PhosSynthPanel::timerCallback()
{
    if (! isShowing()) return;
    const std::array<bool, 2> now { proc.plain (P::PhosResoCouple) > 0.5f, proc.plain (P::PhosAccCouple) > 0.5f };
    if (now != coupled)
    {
        coupled = now;
        repaint (header());
    }
}

void PhosSynthPanel::paint (juce::Graphics& g)
{
    Section::paint (g);
    // the two groups: the 303's own knobs, then what the original adds
    const auto b = body();
    g.setColour (col::line);
    g.fillRect (splitX, b.getY(), 1, kKnobD + kLabelGap + kLabelH);
    // each pair's LEDs are tied by a line named in the middle, lit while coupled
    const juce::String name ("COUPLE");
    g.setFont (label (9.f));
    const float tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 2.f * (float) sp::sm;
    for (int pair = 0; pair < 2; ++pair)
    {
        const bool on = coupled[(size_t) pair];
        const auto l = leds[(size_t) pair * 2]->getBounds().getCentre().toFloat(), r = leds[(size_t) pair * 2 + 1]->getBounds().getCentre().toFloat();
        const float mid = 0.5f * (l.x + r.x);
        g.setColour (on ? accent (*this).withAlpha (0.35f) : col::line);
        g.fillRect (l.x + 6.f, l.y - 0.5f, mid - 0.5f * tw - l.x - 6.f, 1.f);
        g.fillRect (mid + 0.5f * tw, l.y - 0.5f, r.x - 6.f - mid - 0.5f * tw, 1.f);
        g.setColour (on ? accent (*this) : col::textFaint);
        g.drawText (name, juce::Rectangle<float> (mid - 0.5f * tw, l.y - 6.f, tw, 12.f), juce::Justification::centred, false);
    }
}

void PhosSynthPanel::resized()
{
    // the 303 row spreads over the whole width like the original's: cells as
    // wide as the names need, the original's own knobs, a gap, the added ones
    const auto b = body();
    const int x0 = b.getX();
    wave.setBounds (x0, b.getY() + (kKnobD - 2 * kCtrlH) / 2 + 2, kWaveW, 2 * kCtrlH);
    captions.clear();
    addCaption ("Wave", juce::Rectangle<int> (x0, b.getY(), kWaveW, kCellH));

    const int knobsX = x0 + kWaveW + sp::lg;
    const int cellW = (b.getRight() - knobsX - kGroupGap) / 13;
    const auto cell = [&] (int i) {
        const int x = knobsX + i * cellW + (i >= 6 ? kGroupGap : 0);
        return juce::Rectangle<int> (x, b.getY(), cellW, kCellH);
    };
    for (int i = 0; i < 13; ++i)
        ks.knobs[(size_t) i]->setBounds (cell (i));
    splitX = knobsX + 6 * cellW + kGroupGap / 2;

    // LEDs over Resonance and Sweep Amt. (one row), Accent and Acc. Vol. (under it)
    const auto h = header();
    const auto ledAt = [&] (const ParamKnob& k, int row) {
        const int cx = k.getBounds().getCentreX();
        return juce::Rectangle<int> (cx - 5, h.getY() + (row == 0 ? 0 : 10), 10, 10);
    };
    leds[0]->setBounds (ledAt (*ks.knobs[2], 0));
    leds[1]->setBounds (ledAt (*ks.knobs[7], 0));
    leds[2]->setBounds (ledAt (*ks.knobs[5], 1));
    leds[3]->setBounds (ledAt (*ks.knobs[10], 1));
}

// ---------------------------------------------------------------------------
// PhosFilterPanel
// ---------------------------------------------------------------------------
PhosFilterPanel::PhosFilterPanel (PsyBoxProcessor& p) : Section ("Filter"), display (p)
{
    addAndMakeVisible (display);
    shown = hzText (display.cutoffNow());
    startTimerHz (15);
}

// The readout is worked out here, not in paint(): the display under it repaints
// at 60 Hz while a note sweeps, and that would hide the change from this check.
void PhosFilterPanel::timerCallback()
{
    if (! isShowing()) return;
    const auto now = hzText (display.cutoffNow());
    if (now != shown)
    {
        shown = now;
        repaint (header());
    }
}

void PhosFilterPanel::paint (juce::Graphics& g)
{
    Section::paint (g);
    g.setColour (accent (*this));
    g.setFont (mono (10.f));
    g.drawText (shown, header(), juce::Justification::centredRight, false);
}

void PhosFilterPanel::resized()
{
    display.setBounds (body());
}

// ---------------------------------------------------------------------------
// PhosDistortionPanel
// ---------------------------------------------------------------------------
juce::Path phosDriveWave (juce::Rectangle<float> area, int model, float drive, bool dry)
{
    using D = psy::dsp::PhosDistortion;
    const auto& m = D::models()[(size_t) juce::jlimit (0, D::kNumModels - 1, model)];
    const float gain = 0.1f * D::preampGain (m, drive);
    const float bias = m.bias * D::tuning().biasScale, zero = D::clip (m, bias);
    const int n = juce::jmax (64, (int) area.getWidth() / 2);
    std::vector<float> y ((size_t) n + 1);
    float peak = 1e-6f;
    for (int i = 0; i <= n; ++i)
    {
        const float s = std::sin (juce::MathConstants<float>::twoPi * 2.f * (float) i / (float) n);
        y[(size_t) i] = dry ? s : D::clip (m, s * gain + bias) - zero;
        peak = std::max (peak, std::abs (y[(size_t) i]));
    }
    juce::Path p;
    for (int i = 0; i <= n; ++i)
    {
        const juce::Point<float> pt (area.getX() + area.getWidth() * (float) i / (float) n,
                                     area.getCentreY() - area.getHeight() * 0.5f * y[(size_t) i] / peak);
        if (i == 0) p.startNewSubPath (pt);
        else p.lineTo (pt);
    }
    return p;
}

namespace
{
    // The model's drive on a sine, like the FX strip.
    class DriveCurve : public DisplayBase
    {
    public:
        explicit DriveCurve (PsyBoxProcessor& p) : DisplayBase (p) { setInterceptsMouseClicks (false, false); }

    private:
        uint64_t fingerprint() const override { return hashParams ({ P::PhosDistModel, P::PhosDistDrive, P::PhosDistOn }); }
        void paintContent (juce::Graphics& g, juce::Rectangle<float> r) override
        {
            drawGrid (g, r, 4, 2);
            const auto a = r.reduced (sp::sm, sp::sm);
            const int model = (int) std::lround (proc.plain (P::PhosDistModel));
            strokeLine (g, phosDriveWave (a, model, 0.f, true), col::lineStrong, 1.f);
            strokeLine (g, phosDriveWave (a, model, proc.plain (P::PhosDistDrive), false), proc.plain (P::PhosDistOn) > 0.5f ? accent (*this) : col::textFaint);
        }
    };
} // namespace

PhosDistortionPanel::PhosDistortionPanel (PsyBoxProcessor& p)
    : Section ("Distortion"), power (p, P::PhosDistOn, "On"), model (p, P::PhosDistModel), order (p, P::PhosDistOrder)
{
    addAndMakeVisible (power);
    model.setTooltip ("The distortion model: a diode, overdrive or RAT clipper with its own filters around it");
    addAndMakeVisible (model);
    display = std::make_unique<DriveCurve> (p);
    addAndMakeVisible (*display);
    order.setTooltip ("Dynamics before the clipper (Pre) or after it (Post)");
    addAndMakeVisible (order);
    ks.add (*this, p, P::PhosDistDrive, "Drive");
    ks.add (*this, p, P::PhosDistColor, "Color");
    ks.add (*this, p, P::PhosDistDyn, "Dynamics");
}

void PhosDistortionPanel::resized()
{
    auto h = header();
    h.removeFromLeft (titleWidth());
    power.setBounds (h.removeFromLeft (power.idealWidth()));
    model.setBounds (h.removeFromRight (cellsW (4)).withSizeKeepingCentre (cellsW (4), kCtrlH));

    const auto b = body();
    const int gx = b.getRight() - cellsW (3);
    display->setBounds (b.withRight (gx - sp::xl));
    const auto gr = grid (gx);
    ks.knobs[0]->setBounds (gr.cell (0, 0));
    ks.knobs[1]->setBounds (gr.cell (1, 0));
    ks.knobs[2]->setBounds (gr.cell (2, 0));
    order.setBounds (gr.control (1, 1, 2));
    captions.clear();
    addCaption ("Order", gr.cell (1, 1, 2));
}

// ---------------------------------------------------------------------------
// PhosCalibrationPanel
// ---------------------------------------------------------------------------
PhosCalibrationPanel::PhosCalibrationPanel (PsyBoxProcessor& p)
    : Section ("Calibration"), proc (p), clicks (p, P::PhosClicks, "Clicks")
{
    for (auto [prm, name] : { std::pair { P::PhosTm3, "TM3 Cutoff" }, std::pair { P::PhosC21, "C21/C22" },
                              std::pair { P::PhosAccFEnv, "Acc. Filter" }, std::pair { P::PhosAccAEnv, "Acc. Amp" },
                              std::pair { P::PhosC13, "C13 Accent" }, std::pair { P::PhosTm5, "TM5 Width" },
                              std::pair { P::PhosPwShift, "PW Shift" }, std::pair { P::PhosNoise, "BA662 Noise" } })
        ks.add (*this, p, prm, name);
    clicks.setStacked (true);
    clicks.setTooltip ("BA662 Clicks: the VCA's click when a note starts");
    addAndMakeVisible (clicks);

    preset.setTextWhenNothingSelected ("Custom");
    int id = 1;
    for (const auto& c : psy::phosCalibrations())
        preset.addItem (c.name, id++);
    preset.setTooltip ("The original's calibrations: the reference unit and some well-worn ones");
    preset.onChange = [this] { if (preset.getSelectedId() > 0) apply (preset.getSelectedId() - 1); };
    addAndMakeVisible (preset);
    refresh();
    startTimerHz (4);
}

void PhosCalibrationPanel::apply (int index)
{
    const auto& c = psy::phosCalibrations()[(size_t) index];
    const auto set = [this] (P prm, float plain) {
        auto* p = proc.param (prm);
        p->beginChangeGesture();
        p->setValueNotifyingHost (psy::def (prm).toNorm (plain));
        p->endChangeGesture();
    };
    set (P::PhosTm3, c.tm3);
    set (P::PhosC21, c.c21);
    set (P::PhosAccFEnv, c.fenv);
    set (P::PhosAccAEnv, c.aenv);
    set (P::PhosC13, c.c13);
    set (P::PhosTm5, c.tm5);
    set (P::PhosPwShift, c.pw);
    set (P::PhosClicks, c.clicks ? 1.f : 0.f);
    set (P::PhosNoise, c.noise);
}

void PhosCalibrationPanel::timerCallback()
{
    if (isShowing()) refresh();
}

void PhosCalibrationPanel::refresh()
{
    // the preset the trims match, if any
    int match = 0;
    const auto near = [this] (P prm, float v) { return std::abs (proc.plain (prm) - v) <= 0.002f * std::max (1.f, std::abs (v)); };
    for (size_t i = 0; i < psy::phosCalibrations().size() && match == 0; ++i)
    {
        const auto& c = psy::phosCalibrations()[i];
        if (near (P::PhosTm3, c.tm3) && near (P::PhosC21, c.c21) && near (P::PhosAccFEnv, c.fenv) && near (P::PhosAccAEnv, c.aenv)
            && near (P::PhosC13, c.c13) && near (P::PhosTm5, c.tm5) && near (P::PhosPwShift, c.pw)
            && (proc.plain (P::PhosClicks) > 0.5f) == c.clicks && near (P::PhosNoise, c.noise))
            match = (int) i + 1;
    }
    if (match != preset.getSelectedId())
        preset.setSelectedId (match, juce::dontSendNotification);
}

void PhosCalibrationPanel::paint (juce::Graphics& g)
{
    Section::paint (g);
    g.setColour (col::textFaint);
    g.setFont (label (10.5f));
    g.drawFittedText ("The 303's trimmers and parts, as on the original's calibration page. "
                      "Ideal TB303 is the reference unit; the others are worn or modded ones.",
                      note, juce::Justification::centredLeft, 3, 1.f);
}

void PhosCalibrationPanel::resized()
{
    auto h = header();
    preset.setBounds (h.removeFromRight (cellsW (4)).withSizeKeepingCentre (cellsW (4), kCtrlH));
    // the parts' names are long: wider cells than the usual grid
    constexpr int cellW = 76;
    const auto b = body();
    const auto cell = [&] (int i) { return juce::Rectangle<int> (b.getX() - kCellInset + i * cellW, b.getY(), cellW, kCellH); };
    for (int i = 0; i < 7; ++i)
        ks.knobs[(size_t) i]->setBounds (cell (i));
    clicks.setBounds (cell (7));
    ks.knobs[7]->setBounds (cell (8));
    note = juce::Rectangle<int>::leftTopRightBottom (cell (9).getX() + sp::xl, b.getY(), b.getRight(), b.getY() + kKnobD);
}
