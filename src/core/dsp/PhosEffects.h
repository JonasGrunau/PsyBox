#pragma once

// The Phoscyon engine's effects: the distortion with its models, a four-band
// EQ, a delay with a filtered feedback path, an early/late reflection reverb,
// a chorus and a limiter. Built to behave like the original's from
// measurements of its output; the distortion models are its own presets'
// settings (tilt, clipper, colour filter, preamp range, pre and post filters).

#include "Common.h"
#include "Effects.h"
#include "Filters.h"

#include <array>
#include <cmath>
#include <vector>

namespace psy::dsp
{

// ---------------------------------------------------------------------------
// RBJ biquad (Bristow-Johnson cookbook), transposed direct form II.
// ---------------------------------------------------------------------------
struct Biquad
{
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;

    void reset() noexcept { z1 = z2 = 0.f; }
    void bypass() noexcept { b0 = 1.f; b1 = b2 = a1 = a2 = 0.f; }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }

    // Peaking EQ; width is the bandwidth in octaves between the -dB/2 points.
    void peak (double fs, double hz, double db, double octaves) noexcept
    {
        const double w = 2.0 * kPi * std::min (hz, fs * 0.49) / fs;
        peakQ (fs, hz, db, 1.0 / (2.0 * std::sinh (std::log (2.0) / 2.0 * octaves * w / std::sin (w))));
    }

    void peakQ (double fs, double hz, double db, double q) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * kPi * std::min (hz, fs * 0.49) / fs;
        const double alpha = std::sin (w) / (2.0 * std::max (q, 0.01));
        set (1 + alpha * A, -2 * std::cos (w), 1 - alpha * A, 1 + alpha / A, -2 * std::cos (w), 1 - alpha / A);
    }

    void shelf (double fs, double hz, double db, double q, bool high) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * kPi * std::min (hz, fs * 0.49) / fs;
        const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q), sa = 2.0 * std::sqrt (A) * alpha;
        if (high)
            set (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
                 (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
        else
            set (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
                 (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
    }

    void highPass (double fs, double hz, double q) noexcept
    {
        const double w = 2.0 * kPi * std::min (hz, fs * 0.49) / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }

    void lowPass (double fs, double hz, double q) noexcept
    {
        const double w = 2.0 * kPi * std::min (hz, fs * 0.49) / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }
};

// ---------------------------------------------------------------------------
// Distortion. Per model (the original's own settings for it): pre high-pass ->
// pre EQ peak -> tilt -> preamp -> clipper -> make-up -> colour filter ->
// post high-pass -> volume. Measured on the original:
// - the preamp's gain in dB is a straight line over Drive, from the model's
//   preamp minimum to its maximum, scaled and offset per clipper;
// - the diode clipper is a tanh, the overdrive a soft clip with a knee, the
//   RAT a hard clip between an op-amp stage's low-pass and its tone low-pass;
// - the level after the clipper falls a little as the gain rises (make-up);
// - Color, below the middle, fades towards a resonant low-pass at the model's
//   colour frequency; above it, adds a resonant high-pass there (a lift of
//   up to 6 dB on top); the model's range scales how far it goes;
// - Dynamics levels the signal: before the clipper (Pre) the clipping stays
//   as deep however loud the note is, after it (Post) the clipped sound is
//   evened out and lifted.
// ---------------------------------------------------------------------------
class PhosDistortion
{
public:
    enum Clip { Diode, Overdrive, Rat };
    struct Model
    {
        int tiltOn; float tiltDb, tiltHz;
        int clip; float colorHz, colorReso, colorRange, knee, bias, preMin, preMax, preHpHz;
        int preEqOn; float preEqDb, preEqHz, preEqOct, postHpHz, volumeDb;
    };
    static constexpr int kNumModels = 19;

    static const std::array<Model, kNumModels>& models() noexcept
    {
        static const std::array<Model, kNumModels> m { {
            { 1, 5.088f, 1000.f, 0, 2162.97f, 17.2f, 82.4f, 100.f, -12.6903f, 0.f, 66.4f, 69.4338f, 1, 9.024f, 100.f, 1.29066f, 252.f, 2.45524f },   // Diode + Bias
            { 0, 1.44f, 1000.f, 0, 3738.41f, 20.f, 100.f, 100.f, 0.f, 4.4f, 66.4f, 50.891f, 1, 3.264f, 166.147f, 1.8753f, 16.f, 0.551506f },        // Diode + Low-Boost
            { 0, 0.f, 1000.f, 0, 785.341f, 4.4f, 100.f, 100.f, 0.f, 2.8f, 82.4f, 973.67f, 0, -20.736f, 127.341f, 2.f, 0.f, -0.236213f },             // Diode + Low-Cut Extreme
            { 0, 0.f, 1000.f, 0, 2000.f, 44.8f, 100.f, 100.f, 0.f, 0.f, 74.4f, 94.7827f, 0, 0.f, 100.f, 2.f, 60.f, -1.4183f },                       // Diode + Low-Cut Medium
            { 1, 5.376f, 420.727f, 0, 3268.11f, 34.8f, 100.f, 100.f, 0.f, 0.f, 64.8f, 0.f, 1, 6.336f, 366.178f, 3.04218f, 24.f, -0.44158f },         // Diode + Mid-Punch
            { 1, 12.288f, 139.316f, 0, 4345.03f, 20.f, 100.f, 100.f, 0.f, 0.f, 60.4f, 0.f, 0, 0.f, 100.f, 2.f, 0.f, -0.843999f },                    // Diode + Tilt
            { 0, 0.f, 1000.f, 0, 2000.f, 44.8f, 100.f, 100.f, 0.f, 0.f, 74.4f, 0.f, 0, 0.f, 100.f, 2.f, 0.f, -1.4183f },                             // Diode Clean
            { 1, 24.f, 679.203f, 0, 2516.69f, 20.f, 100.f, 48.4f, 0.f, 5.6f, 63.6f, 0.f, 1, 6.72f, 172.226f, 1.5296f, 304.f, 1.93338f },             // Diode Whistle
            { 1, 1.44f, 1000.f, 1, 2107.65f, 20.f, 100.f, 50.8f, 0.f, 8.8f, 84.8f, 16.751f, 1, 4.608f, 257.219f, 3.3559f, 0.f, 1.09438f },           // Fuzzy Overdrive
            { 0, 5.76f, 356.452f, 1, 3121.1f, 26.4f, 100.f, 100.f, -9.9958f, 9.2f, 82.4f, 92.2469f, 0, 0.f, 100.f, 2.f, 108.f, -0.800424f },         // Overdrive + Bias
            { 0, 0.f, 1000.f, 1, 4077.18f, 44.8f, 100.f, 100.f, 0.f, 15.2f, 91.6f, 1000.f, 0, 0.f, 100.f, 2.f, 60.f, 0.f },                          // Overdrive + Low-Cut Extreme
            { 0, 0.f, 1000.f, 1, 2000.f, 44.8f, 100.f, 100.f, 0.f, 15.2f, 91.6f, 94.7827f, 0, 0.f, 100.f, 2.f, 60.f, -1.4183f },                     // Overdrive + Low-Cut Medium
            { 1, 12.0007f, 1000.f, 1, 3905.22f, 20.f, 100.f, 100.f, 0.f, 5.6f, 94.8f, 82.5769f, 1, 3.456f, 127.341f, 1.24182f, 0.f, -0.560387f },   // Overdrive + Tilt Strong
            { 1, 7.0087f, 1000.f, 1, 3905.22f, 20.f, 100.f, 58.f, 0.f, 5.6f, 94.8f, 82.5769f, 0, 3.456f, 127.341f, 1.24182f, 0.f, -0.560387f },     // Overdrive + Tilt Subtle
            { 0, 0.f, 1000.f, 1, 2000.f, 20.f, 100.f, 100.f, 0.f, 3.6f, 98.8f, 0.f, 0, 0.f, 100.f, 2.f, 0.f, -2.00224f },                            // Overdrive Clean
            { 0, 0.f, 1000.f, 1, 243.636f, 67.2f, 53.2f, 100.f, 0.f, 24.4f, 100.f, 217.769f, 0, 0.f, 100.f, 2.f, 44.f, -4.75868f },                  // Overdrive Colorized
            { 1, 3.072f, 575.44f, 1, 1608.08f, 25.2f, 100.f, 50.8f, 0.f, 10.f, 100.f, 0.311636f, 0, 0.f, 100.f, 2.f, 0.f, 1.93337f },                // Overdrive Dirty
            { 1, 7.488f, 285.759f, 2, 2579.37f, 20.f, 100.f, 50.f, 0.f, 4.8f, 71.6f, 102.682f, 0, -4.992f, 554.178f, 1.63366f, 24.f, -0.858571f },   // RAT + Low-Cut
            { 0, 0.f, 1000.f, 2, 2000.f, 20.f, 100.f, 100.f, 0.f, 0.f, 75.2f, 0.f, 0, 0.f, 100.f, 2.f, 0.f, -1.70904f },                             // RAT Clean
        } };
        return m;
    }

    // The constants the measurements set, per clipper (diode, overdrive, RAT):
    // the preamp's dB per unit of the model's range and its offset, the
    // make-up after the clipper (dB = makeup + makeupSlope x preamp dB); the
    // overdrive's knee; the RAT's two low-passes; the bias's scale; how far
    // Color moves its filter and its Q at full resonance; Dynamics' levels
    // and times.
    struct Tuning
    {
        std::array<float, 3> gainScale { 0.983f, 0.565f, 0.901f }, gainOffset { -4.01f, -4.51f, -6.83f };
        std::array<float, 3> makeup { -14.27f, -10.66f, -11.64f }, makeupSlope { -0.133f, -0.228f, -0.300f };
        float kneeScale = 18.1f, ratPreHz = 9750.f, ratPostHz = 20000.f, biasScale = 0.0233f;
        float colorQ0 = 1.1f, colorQSlope = 0.02f;
        float levelPre = -10.5f, levelPost = -13.76f, attackMs = 0.835f, releaseMs = 58.2f;
    };
    static const Tuning& tuning() noexcept { static const Tuning t; return t; }
    // A different set (the fitting tool's); null for the standard one.
    void setTuning (const Tuning* t) noexcept { tune = t != nullptr ? t : &tuning(); lastModel = -1; }

    // The clipper on the preamp's output.
    static float clip (const Model& m, float x, const Tuning& t = tuning()) noexcept
    {
        switch (m.clip)
        {
            case Diode: return fastTanh (x);
            case Overdrive:
            {
                const float k = 1.f + t.kneeScale * m.knee * 0.01f;
                return x / std::pow (1.f + std::pow (std::abs (x), k), 1.f / k);
            }
            default: return std::clamp (x, -1.f, 1.f);
        }
    }

    // The preamp's gain (linear) at a Drive setting, for a model.
    static float preampDb (const Model& m, float drive, const Tuning& t = tuning()) noexcept
    {
        const auto c = (size_t) std::clamp (m.clip, 0, 2);
        return t.gainScale[c] * (m.preMin + (m.preMax - m.preMin) * std::clamp (drive, 0.f, 1.f)) + t.gainOffset[c];
    }
    static float preampGain (const Model& m, float drive, const Tuning& t = tuning()) noexcept { return dbToGain (preampDb (m, drive, t)); }

    void prepare (float sampleRate) noexcept
    {
        fs = sampleRate;
        lastModel = -1;
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : ch)
        {
            for (auto* b : { &c.preHp, &c.preEq, &c.tiltLo, &c.tiltHi, &c.postHp }) b->reset();
            c.color.reset();
            c.ratIn.reset();
            c.ratOut.reset();
        }
        env = 0.f;
    }

    void process (float* l, float* r, int n, int model, float drive, float color, float dynamics, bool post) noexcept
    {
        model = std::clamp (model, 0, kNumModels - 1);
        const auto& m = models()[(size_t) model];
        const auto& t = *tune;
        if (model != lastModel || color != lastColor)
            configure (m, model, color);
        const auto c = (size_t) std::clamp (m.clip, 0, 2);
        const float preDb = preampDb (m, drive, t);
        const float pre = dbToGain (preDb);
        const float out = dbToGain (t.makeup[c] + t.makeupSlope[c] * preDb + m.volumeDb);
        const float bias = m.bias * t.biasScale;
        const float zero = clip (m, bias, t);
        const float wet = std::abs (color) * m.colorRange * 0.01f;
        const float att = 1.f - std::exp (-1.f / (t.attackMs * 0.001f * fs));
        const float rel = 1.f - std::exp (-1.f / (t.releaseMs * 0.001f * fs));
        const float target = dbToGain (post ? t.levelPost : t.levelPre);
        const bool rat = m.clip == Rat;

        for (int i = 0; i < n; ++i)
        {
            float a = l[i], b = r != nullptr ? r[i] : l[i];
            float v[2] = { a, b };
            for (int k = 0; k < 2; ++k)
            {
                auto& s = ch[(size_t) k];
                if (m.preHpHz > 1.f) v[k] = s.preHp.process (v[k]);
                if (m.preEqOn) v[k] = s.preEq.process (v[k]);
                if (m.tiltOn) v[k] = s.tiltHi.process (s.tiltLo.process (v[k]));
            }
            // the leveller's ear: where it sits in the chain
            const auto level = [&] (float x0, float x1) {
                const float pk = std::max (std::abs (x0), std::abs (x1));
                env += (pk - env) * (pk > env ? att : rel);
                return dynamics > 0.f ? std::pow (target / std::max (env, 1e-5f), dynamics) : 1.f;
            };
            if (! post && dynamics > 0.f)
            {
                const float g = std::min (level (v[0], v[1]), 64.f);
                v[0] *= g;
                v[1] *= g;
            }
            for (int k = 0; k < 2; ++k)
            {
                auto& s = ch[(size_t) k];
                float u = v[k] * pre;
                if (rat) u = s.ratIn.lp (u);
                float y = clip (m, u + bias, t) - zero;
                if (rat) y = s.ratOut.lp (y);
                v[k] = y * out;
            }
            if (post && dynamics > 0.f)
            {
                const float g = std::min (level (v[0], v[1]), 16.f);
                v[0] *= g;
                v[1] *= g;
            }
            for (int k = 0; k < 2; ++k)
            {
                auto& s = ch[(size_t) k];
                float y = v[k];
                if (wet > 0.f)
                {
                    const auto o = s.color.process (y);
                    y = color < 0.f ? y + wet * (o.lp - y) : y + wet * o.hp;
                }
                if (m.postHpHz > 1.f) y = s.postHp.process (y);
                v[k] = y;
            }
            l[i] = v[0];
            if (r != nullptr) r[i] = v[1];
        }
    }

private:
    struct Chan
    {
        Biquad preHp, preEq, tiltLo, tiltHi, postHp;
        Svf color;
        TptOnePole ratIn, ratOut;
    };
    std::array<Chan, 2> ch {};
    float fs = 48000.f, lastColor = 0.f, env = 0.f;
    int lastModel = -1;
    const Tuning* tune = &tuning();

    void configure (const Model& m, int model, float color) noexcept
    {
        const auto& t = *tune;
        for (auto& s : ch)
        {
            s.preHp.highPass (fs, std::max (m.preHpHz, 1.f), 0.7071);
            s.preEq.peak (fs, m.preEqHz, m.preEqDb, m.preEqOct);
            s.tiltLo.shelf (fs, m.tiltHz, -m.tiltDb * 0.5, 0.7071, false);
            s.tiltHi.shelf (fs, m.tiltHz, m.tiltDb * 0.5, 0.7071, true);
            s.postHp.highPass (fs, std::max (m.postHpHz, 1.f), 0.7071);
            s.color.setG (prewarp (m.colorHz, fs), 1.f / (t.colorQ0 + t.colorQSlope * m.colorReso));
            s.ratIn.setG (prewarp (t.ratPreHz, fs));
            s.ratOut.setG (prewarp (t.ratPostHz, fs));
        }
        lastModel = model;
        lastColor = color;
    }
};

// ---------------------------------------------------------------------------
// Four-band EQ: four peaking bands (the low and high ones too, as in the
// original). Width in octaves sets the Q as the original's does, measured:
// Q 2.9 at half an octave down to 0.2 at four.
// ---------------------------------------------------------------------------
class PhosEq
{
public:
    struct Band { float db = 0.f, hz = 1000.f, octaves = 1.f; };

    static double qFor (double octaves) noexcept
    {
        static constexpr double w[] = { 0.5, 1.15, 2.0, 2.9, 4.0 }, lq[] = { 1.0657, 0.6775, 0.1939, -0.4385, -1.5945 };
        octaves = std::clamp (octaves, 0.5, 4.0);
        int i = 0;
        while (i < 3 && octaves > w[i + 1]) ++i;
        const double f = (octaves - w[i]) / (w[i + 1] - w[i]);
        return std::exp (lq[i] + (lq[i + 1] - lq[i]) * f);
    }

    void prepare (float sampleRate) noexcept { fs = sampleRate; dirty = true; reset(); }
    void reset() noexcept { for (auto& c : bands) for (auto& b : c) b.reset(); }

    void process (float* l, float* r, int n, const std::array<Band, 4>& set) noexcept
    {
        if (dirty || set[0].db != last[0].db || set[0].hz != last[0].hz || set[0].octaves != last[0].octaves
            || set[1].db != last[1].db || set[1].hz != last[1].hz || set[1].octaves != last[1].octaves
            || set[2].db != last[2].db || set[2].hz != last[2].hz || set[2].octaves != last[2].octaves
            || set[3].db != last[3].db || set[3].hz != last[3].hz || set[3].octaves != last[3].octaves)
            configure (set);
        float* io[2] = { l, r };
        for (int k = 0; k < 2; ++k)
            if (io[k] != nullptr)
                for (int i = 0; i < n; ++i)
                {
                    float v = io[k][i];
                    for (int b = 0; b < 4; ++b)
                        if (set[(size_t) b].db != 0.f) v = bands[(size_t) k][(size_t) b].process (v);
                    io[k][i] = v;
                }
    }

private:
    float fs = 48000.f;
    bool dirty = true;
    std::array<Band, 4> last {};
    std::array<std::array<Biquad, 4>, 2> bands {};

    void configure (const std::array<Band, 4>& set) noexcept
    {
        for (auto& c : bands)
            for (int b = 0; b < 4; ++b)
            {
                const auto& s = set[(size_t) b];
                c[(size_t) b].peakQ (fs, s.hz, s.db, qFor (s.octaves));
            }
        last = set;
        dirty = false;
    }
};

// ---------------------------------------------------------------------------
// Delay: two lines, left and right, each feeding back on itself through an
// optional resonant filter. As measured on the original: Spread delays one
// side by up to half its time (right for positive, left for negative), the
// feedback loses a little per pass, and the mix keeps each side at full level
// up to the middle (50/50 is both at unity).
// ---------------------------------------------------------------------------
class PhosDelay
{
public:
    struct Tuning { float fbScale = 0.899f, fbPow = 1.f, spreadShare = 0.5f, resoQ = 8.f; };
    static const Tuning& tuning() noexcept { static const Tuning t; return t; }
    void setTuning (const Tuning* t) noexcept { tune = t != nullptr ? t : &tuning(); }

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        for (auto& d : lines) d.prepare ((int) (fs * 6.1f));
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : lines) d.reset();
        for (auto& f : filters) f.reset();
        smooth = { -1.f, -1.f };
    }

    // filterType: 0 off, 1 LP, 2 BP, 3 HP. spread -1..1. mix 0..1 (dry/wet).
    void process (float* l, float* r, int n, float msL, float msR, float feedback, int filterType, float cutoffHz,
                  float reso, float spread, float mix) noexcept
    {
        const auto& t = *tune;
        if (spread > 0.f) msR *= 1.f + t.spreadShare * spread;
        else msL *= 1.f - t.spreadShare * spread;
        const std::array<float, 2> want { std::clamp (msL, 1.f, 6000.f) * 0.001f * fs, std::clamp (msR, 1.f, 6000.f) * 0.001f * fs };
        if (smooth[0] < 0.f) smooth = want;
        const float glide = 1.f - std::exp (-1.f / (0.05f * fs));
        const float q = 0.5f + (t.resoQ - 0.5f) * std::clamp (reso, 0.f, 1.f) * std::clamp (reso, 0.f, 1.f);
        for (auto& f : filters)
            f.setG (prewarp (std::clamp (cutoffHz, 20.f, fs * 0.45f), fs), 1.f / q);
        const float fb = t.fbScale * std::pow (std::clamp (feedback, 0.f, 1.f), t.fbPow);
        const float dryG = std::min (1.f, 2.f * (1.f - mix)), wetG = std::min (1.f, 2.f * mix);
        float* io[2] = { l, r != nullptr ? r : l };
        for (int i = 0; i < n; ++i)
        {
            const float in[2] = { io[0][i], r != nullptr ? io[1][i] : io[0][i] };
            float out[2];
            for (int k = 0; k < 2; ++k)
            {
                smooth[(size_t) k] += (want[(size_t) k] - smooth[(size_t) k]) * glide;
                const float d = lines[(size_t) k].read (smooth[(size_t) k]);
                lines[(size_t) k].push (in[k] + fb * shape (filters[(size_t) k], d, filterType));
                out[k] = in[k] * dryG + d * wetG;
            }
            l[i] = out[0];
            if (r != nullptr) r[i] = out[1];
        }
    }

private:
    float fs = 48000.f;
    std::array<float, 2> smooth { -1.f, -1.f };
    std::array<DelayLine, 2> lines;
    std::array<Svf, 2> filters;
    const Tuning* tune = &tuning();

    static float shape (Svf& f, float x, int type) noexcept
    {
        if (type <= 0) return x;
        const auto o = f.process (x);
        return type == 1 ? o.lp : type == 2 ? o.bp * f.k : o.hp;   // band-pass at unity in its centre
    }
};

// ---------------------------------------------------------------------------
// Reverb: a pre-delay, early reflections (taps spread over the early room's
// size, smeared by diffusion) and a late tail (an eight-line feedback delay
// network sized by the late room, with its own diffusion and slow
// modulation), mixed early against late, then dry against wet like the delay
// (each side at full level up to the middle). The tail's decay time is the
// original's, measured over Late Size and Feedback; Brightness damps it.
// ---------------------------------------------------------------------------
class PhosReverb
{
public:
    struct Settings
    {
        float preMs = 0.f, earlySize = 1.1f, earlyDiff = 0.4f, lateSize = 5.1f, lateDiff = 0.45f, feedback = 0.5f;
        float blend = 0.2f, modulation = 0.47f, earlyBright = 0.7f, lateBright = 0.7f, mix = 0.3f;
    };

    // RT60 (s) at Late Size 0, .25 .. 1 (5.1 to 51.1 m) and Feedback 0, .25, .5, .75, .9, 1
    static float decaySeconds (float size01, float feedback) noexcept
    {
        static constexpr float rt[5][6] = { { 0.40f, 0.50f, 0.86f, 1.89f, 4.55f, 9.f },
                                            { 0.57f, 0.78f, 1.57f, 3.75f, 9.26f, 18.f },
                                            { 0.87f, 1.69f, 3.39f, 8.08f, 19.3f, 38.f },
                                            { 1.82f, 3.44f, 7.13f, 16.97f, 40.7f, 80.f },
                                            { 4.51f, 8.01f, 16.02f, 37.f, 88.f, 170.f } };
        static constexpr float fbAt[6] = { 0.f, 0.25f, 0.5f, 0.75f, 0.9f, 1.f };
        const float sp = std::clamp (size01, 0.f, 1.f) * 4.f;
        const int si = std::min ((int) sp, 3);
        const float sf = sp - (float) si;
        feedback = std::clamp (feedback, 0.f, 1.f);
        int fi = 0;
        while (fi < 4 && feedback > fbAt[fi + 1]) ++fi;
        const float ff = (feedback - fbAt[fi]) / (fbAt[fi + 1] - fbAt[fi]);
        const auto at = [&] (int s, int f) { return std::log (rt[s][f]); };
        const float lo = at (si, fi) + (at (si, fi + 1) - at (si, fi)) * ff;
        const float hi = at (si + 1, fi) + (at (si + 1, fi + 1) - at (si + 1, fi)) * ff;
        return std::exp (lo + (hi - lo) * sf);
    }

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        pre.prepare ((int) (fs * 1.05f));
        early.prepare ((int) (fs * 0.2f));
        for (auto& d : late) d.prepare ((int) (fs * 0.35f));
        for (auto& d : diff) d.prepare ((int) (fs * 0.05f));
        reset();
    }

    void reset() noexcept
    {
        pre.reset();
        early.reset();
        for (auto& d : late) d.reset();
        for (auto& d : diff) d.reset();
        lp.fill (0.f);
        eLp = { 0.f, 0.f };
        phase = 0.0;
    }

    void process (float* l, float* r, int n, const Settings& s) noexcept
    {
        // speed of sound: 343 m/s; reflections arrive within the room's crossing time
        const float eSpan = s.earlySize / 343.f * fs;
        const float lSpan = (0.012f + 0.6f * s.lateSize / 343.f) * fs;
        static constexpr float eTaps[8] = { 0.13f, 0.21f, 0.34f, 0.42f, 0.55f, 0.67f, 0.83f, 1.f };
        static constexpr float eGain[8] = { 0.9f, -0.75f, 0.68f, -0.6f, 0.52f, -0.45f, 0.38f, -0.3f };
        static constexpr float lLen[8] = { 0.53f, 0.61f, 0.67f, 0.73f, 0.79f, 0.86f, 0.93f, 1.f };
        const float rt = decaySeconds ((s.lateSize - 5.1f) / 46.f, s.feedback);
        std::array<float, 8> g {};
        for (int k = 0; k < 8; ++k)
            g[(size_t) k] = std::pow (10.f, -3.f * lLen[k] * lSpan / fs / rt);
        const float eDamp = 1.f - std::exp (-kTwoPi * (1500.f * std::pow (10.f, s.earlyBright)) / fs);
        const float lDamp = 1.f - std::exp (-kTwoPi * (1200.f * std::pow (10.f, 1.1f * s.lateBright)) / fs);
        const float dryG = std::min (1.f, 2.f * (1.f - s.mix)), wetG = std::min (1.f, 2.f * s.mix);
        const float eMix = 1.f - s.blend, lMix = s.blend;
        const double modInc = 0.7 / fs;
        const float modDepth = s.modulation * 0.002f * fs;
        const float ad = 0.3f + 0.45f * s.lateDiff, ed = 0.25f + 0.5f * s.earlyDiff;

        for (int i = 0; i < n; ++i)
        {
            const float inL = l[i], inR = r != nullptr ? r[i] : inL;
            pre.push (0.5f * (inL + inR));
            const float x = pre.read (std::max (1.f, s.preMs * 0.001f * fs));
            // early: taps, the odd ones to the left, the even to the right
            early.push (x);
            float el = 0.f, er = 0.f;
            for (int k = 0; k < 8; ++k)
            {
                const float v = early.read (std::max (1.f, eTaps[k] * eSpan)) * eGain[k];
                (k % 2 == 0 ? el : er) += v;
            }
            el = allpass (diff[0], el, 0.011f * fs, ed);
            er = allpass (diff[1], er, 0.0137f * fs, ed);
            eLp[0] += (el - eLp[0]) * eDamp;
            eLp[1] += (er - eLp[1]) * eDamp;

            // late: an FDN fed by the diffused input, each line losing its share of the decay
            phase += modInc;
            const float mod = (float) std::sin (kTwoPi * phase) * modDepth;
            float in = allpass (diff[2], x, 0.0071f * fs, ad);
            in = allpass (diff[3], in, 0.0113f * fs, ad);
            std::array<float, 8> o;
            for (int k = 0; k < 8; ++k)
            {
                const float len = std::max (8.f, lLen[k] * lSpan + (k % 2 == 0 ? mod : -mod));
                o[(size_t) k] = late[(size_t) k].read (len);
                lp[(size_t) k] += (o[(size_t) k] * g[(size_t) k] - lp[(size_t) k]) * lDamp;
            }
            // Householder mix: y = x - 2/N sum
            float sum = 0.f;
            for (float v : lp) sum += v;
            sum *= 0.25f;
            for (int k = 0; k < 8; ++k)
                late[(size_t) k].push (in * 0.35f + (lp[(size_t) k] - sum));
            const float ll = (o[0] + o[2] + o[4] + o[6]) * 0.5f, lr = (o[1] + o[3] + o[5] + o[7]) * 0.5f;

            l[i] = inL * dryG + (eMix * eLp[0] + lMix * ll) * wetG;
            if (r != nullptr) r[i] = inR * dryG + (eMix * eLp[1] + lMix * lr) * wetG;
        }
    }

private:
    float fs = 48000.f;
    DelayLine pre, early;
    std::array<DelayLine, 8> late;
    std::array<DelayLine, 4> diff;
    std::array<float, 8> lp {};
    std::array<float, 2> eLp {};
    double phase = 0.0;

    static float allpass (DelayLine& d, float x, float len, float g) noexcept
    {
        const float delayed = d.read (len);
        const float v = x + g * delayed;
        d.push (v);
        return delayed - g * v;
    }
};

// ---------------------------------------------------------------------------
// Chorus: modulated delay lines around an offset, the right side's LFO
// shifted by Spatial, two more voices for Fatness, a high-pass on the wet
// signal, its own output level, and the mix like the delay's. Ranges as
// measured on the original: the offset up to ~9 ms, a swing of +-4.6 ms at
// full depth, the rate exponential from a few hundredths of a hertz to ~10 Hz.
// ---------------------------------------------------------------------------
class PhosChorus
{
public:
    void prepare (float sampleRate)
    {
        fs = sampleRate;
        for (auto& d : lines) d.prepare ((int) (fs * 0.08f));
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : lines) d.reset();
        for (auto& h : hp) h.reset();
        phase = 0.0;
    }

    static float rateHz (float rate) noexcept { return 9.7f * std::exp (6.56f * (std::clamp (rate, 0.f, 1.f) - 1.f)); }

    void process (float* l, float* r, int n, float offset, float depth, float rate, float spatial, float fatness,
                  float hpCut, float outDb, float mix) noexcept
    {
        const float baseMs = 0.5f + 8.4f * offset;
        const float swingMs = 4.6f * depth;
        const double inc = rateHz (rate) / fs;
        const float shift = spatial * 0.25f;
        const float hpHz = 20.f * std::pow (100.f, hpCut);
        for (auto& h : hp) h.setG (prewarp (hpHz, fs));
        const float outG = dbToGain (outDb);
        const float dryG = std::min (1.f, 2.f * (1.f - mix)), wetG = std::min (1.f, 2.f * mix) * outG;
        const float fat = std::clamp (fatness, 0.f, 1.f);
        for (int i = 0; i < n; ++i)
        {
            phase += inc;
            phase -= std::floor (phase);
            const float inL = l[i], inR = r != nullptr ? r[i] : inL;
            lines[0].push (inL);
            lines[1].push (inR);
            const auto tap = [&] (int side, double ph, float scale) {
                const float m = (float) std::sin (kTwoPi * (ph + (side == 1 ? shift : 0.f)));
                return lines[(size_t) side].read (std::max (1.f, (baseMs + 1.f + swingMs * scale * m) * 0.001f * fs));
            };
            float wl = tap (0, phase, 1.f), wr = tap (1, phase, 1.f);
            if (fat > 0.f)
            {
                // two more voices a third of a cycle apart, a little deeper
                wl += fat * (tap (0, phase + 1.0 / 3.0, 1.3f) + tap (0, phase + 2.0 / 3.0, 1.6f));
                wr += fat * (tap (1, phase + 1.0 / 3.0, 1.3f) + tap (1, phase + 2.0 / 3.0, 1.6f));
                const float norm = 1.f / std::sqrt (1.f + 2.f * fat * fat);
                wl *= norm;
                wr *= norm;
            }
            wl = hp[0].hp (wl);
            wr = hp[1].hp (wr);
            l[i] = inL * dryG + wl * wetG;
            if (r != nullptr) r[i] = inR * dryG + wr * wetG;
        }
    }

private:
    float fs = 48000.f;
    std::array<DelayLine, 2> lines;
    std::array<TptOnePole, 2> hp;
    double phase = 0.0;
};

// ---------------------------------------------------------------------------
// Limiter: a stereo-linked peak compressor with 3.3 ms of lookahead (the
// audio is delayed, the detector is not). Gain reduction follows a soft or
// hard knee curve in dB and is smoothed by one-pole attack and release in
// dB. Gain is applied as 1 + 15 x (dB / 24) in amplitude, so the original's
// "+12 dB" lifts by 18.6 dB. The soft clip is linear to 0.8 and bends into
// a ceiling at 1.
// ---------------------------------------------------------------------------
class PhosLimiter
{
public:
    static constexpr float lookaheadMs = 3.3f, kneeWidthDb = 4.29f, releaseScale = 1.14f, clipKnee = 0.8f;

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        look = std::max (1, (int) std::lround (lookaheadMs * 0.001f * fs));
        for (auto& b : buf) b.assign ((size_t) look, 0.f);
        reset();
    }

    void reset() noexcept
    {
        gr = 0.f;
        pos = 0;
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.f);
    }

    void process (float* l, float* r, int n, float knee, float ratio, float attackMs, float releaseMs, float threshDb,
                  float gainDb, bool softClip) noexcept
    {
        if (buf[0].empty()) return;
        const float att = std::exp (-1.f / (std::max (attackMs, 0.01f) * 0.001f * fs));
        const float rel = std::exp (-1.f / (std::max (releaseMs * releaseScale, 1.f) * 0.001f * fs));
        const float width = (1.f - knee) * kneeWidthDb;
        const float slope = 1.f - 1.f / std::max (ratio, 1.f);
        const float makeup = 1.f + 15.f * gainDb / 24.f;
        for (int i = 0; i < n; ++i)
        {
            const float inL = l[i], inR = r != nullptr ? r[i] : 0.f;
            const float want = reductionDb (gainToDb (std::max (std::abs (inL), std::abs (inR))) - threshDb, width, slope);
            gr = want > gr ? att * gr + (1.f - att) * want : rel * gr + (1.f - rel) * want;
            const float g = dbToGain (-gr) * makeup;
            float a = buf[0][(size_t) pos] * g, b = buf[1][(size_t) pos] * g;
            buf[0][(size_t) pos] = inL;
            buf[1][(size_t) pos] = inR;
            pos = pos + 1 == look ? 0 : pos + 1;
            if (softClip) { a = clip (a); b = clip (b); }
            l[i] = a;
            if (r != nullptr) r[i] = b;
        }
    }

    float reduction() const noexcept { return gr; }

    // the static curve: the output level of a steady input level, in dB
    static float curveDb (float inDb, float knee, float ratio, float threshDb, float gainDb, bool softClip) noexcept
    {
        const float red = reductionDb (inDb - threshDb, (1.f - knee) * kneeWidthDb, 1.f - 1.f / std::max (ratio, 1.f));
        const float amp = dbToGain (inDb - red) * (1.f + 15.f * gainDb / 24.f);
        return gainToDb (softClip ? clip (amp) : amp);
    }

private:
    static float reductionDb (float over, float width, float slope) noexcept
    {
        if (width > 0.f && std::abs (over) < width * 0.5f)
            return slope * (over + width * 0.5f) * (over + width * 0.5f) / (2.f * width);
        return over > 0.f ? slope * over : 0.f;
    }

    static float clip (float x) noexcept
    {
        const float m = std::abs (x);
        if (m <= clipKnee) return x;
        const float y = clipKnee + (1.f - clipKnee) * fastTanh ((m - clipKnee) / (1.f - clipKnee));
        return x < 0.f ? -y : y;
    }

    float fs = 48000.f, gr = 0.f;
    int look = 0, pos = 0;
    std::array<std::vector<float>, 2> buf;
};

} // namespace psy::dsp
