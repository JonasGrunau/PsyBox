#pragma once

// Stereo effects running at the base sample rate after the voices are summed.

#include "Common.h"
#include "Filters.h"
#include "Shapers.h"

#include <array>
#include <vector>

namespace psy::dsp
{

// Ring-buffer delay line with Hermite-interpolated fractional reads.
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4)
            size <<= 1;
        buf.assign ((size_t) size, 0.f);
        mask = size - 1;
        w = 0;
    }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.f); }
    void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }

    // delay in samples (>= 1) behind the most recently pushed sample.
    float read (float delay) const noexcept
    {
        const float rp = (float) (w - 1) - std::clamp (delay, 1.f, (float) (mask - 3));
        const int i = (int) std::floor (rp);
        const float f = rp - (float) i;
        const float xm1 = buf[(size_t) ((i - 1) & mask)], x0 = buf[(size_t) (i & mask)];
        const float x1 = buf[(size_t) ((i + 1) & mask)], x2 = buf[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    float readInt (int delay) const noexcept { return buf[(size_t) ((w - 1 - delay) & mask)]; }

private:
    std::vector<float> buf;
    int mask = 0, w = 0;
};

// ---------------------------------------------------------------------------
class Distortion
{
public:
    void prepare (float sr)
    {
        fs = sr;
        // the ADAA shaper averages each pair of samples, which dulls the top
        // at a base rate (-2 dB at 10 kHz, -6 dB at 15 kHz at 44.1 kHz);
        // oversampled, it doesn't matter
        compensate = sr < 60000.f;
        for (auto& d : dc) d.prepare (sr);
        reset();
    }
    void reset() noexcept
    {
        for (auto& s : sh) s.reset();
        for (auto& t : tone) t.reset();
        for (auto& d : dc) d.reset();
        for (auto& f : low1) f.reset();
        for (auto& f : low2) f.reset();
        for (auto& f : trim1) f.reset();
        for (auto& f : trim2) f.reset();
        for (auto& t : top) t.reset();
        level = meanSquare = 0.f;
        hist = {};
    }

    // lowCutHz: a 24 dB high-pass on the distorted signal, which takes out
    // the lows a clipper makes from a high-passed sound (the difference
    // tones bring the fundamental back). At 20 Hz or below it is skipped,
    // so the distortion sounds exactly as without it. Cutting the lows
    // under a clipped wave tilts its flat tops into spikes (about 3.5 dB
    // more crest on a heavily clipped saw stack), so the peaks it raises
    // over the shaper's ceiling are clipped again. Only that clip's
    // residual, cut the same way, is added: clipping the whole signal again
    // would bring the difference tones back. Rectify has no ceiling and
    // keeps its peaks.
    //
    // autoLevel: the drive counts from the sound's own level instead of
    // from full scale. A linked RMS follower (the mean square over about
    // 15 ms, then 5 ms up and 150 ms down) scales the signal to kAutoRef
    // before the shaper and the level is given back after it, so the shaper
    // bites the same on a quiet patch as on a loud one, and a sound that
    // decays keeps its decay instead of being held up at the clip level.
    // Small signals pass at unity gain.
    void process (float* l, float* r, int n, int type, float driveDb, float toneHz, float mix, float lowCutHz = 20.f,
                  bool autoLevel = false) noexcept
    {
        const float g = dbToGain (driveDb);
        const float comp = 1.f / std::sqrt (g);
        for (auto& t : tone) t.setG (prewarp (toneHz, fs), 1.41421356f);
        const bool cut = lowCutHz > 20.5f;
        if (cut)
        {
            // two sections of a 4th-order Butterworth
            const float gc = prewarp (std::min (lowCutHz, 0.45f * fs), fs);
            for (auto& f : low1) f.setG (gc, 0.76536686f);
            for (auto& f : low2) f.setG (gc, 1.84775907f);
            for (auto& f : trim1) f.setG (gc, 0.76536686f);
            for (auto& f : trim2) f.setG (gc, 1.84775907f);
        }
        const bool trim = cut && type != AdaaShaper::Rectify;
        float* ch[2] = { l, r };
        if (autoLevel)
        {
            const float avg = 1.f - std::exp (-1.f / (0.015f * fs));
            const float up = 1.f - std::exp (-1.f / (0.005f * fs)), down = 1.f - std::exp (-1.f / (0.15f * fs));
            for (int i = 0; i < n; ++i)
            {
                meanSquare += (0.5f * (l[i] * l[i] + r[i] * r[i]) - meanSquare) * avg;
                meanSquare = flushDenormal (meanSquare);
                level += (meanSquare - level) * (meanSquare > level ? up : down);
                level = flushDenormal (level);
                // at most +40 dB, so silence and tails are not blown up
                const float pre = std::min (kAutoRef / std::sqrt (level + 1.0e-12f), 100.f);
                for (int c = 0; c < 2; ++c)
                {
                    const float x = ch[c][i];
                    float y = sh[c].process (x * pre * g, type);
                    y = dc[c].process (y);
                    y = tone[c].process (y).lp;
                    if (cut) y = low2[c].process (low1[c].process (y).hp).hp;
                    float dry = x, wet = y / (pre * g);
                    if (compensate) restoreTop (c, dry, wet);
                    if (trim) wet = trimPeaks (c, wet, 1.f / (pre * g));
                    ch[c][i] = dry + (wet - dry) * mix;
                }
            }
            return;
        }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float x = ch[c][i];
                float y = sh[c].process (x * g, type);
                y = dc[c].process (y);
                y = tone[c].process (y).lp;
                if (cut) y = low2[c].process (low1[c].process (y).hp).hp;
                const float scale = type == AdaaShaper::Fold || type == AdaaShaper::Hard ? 0.8f : std::max (comp, 0.35f) * 1.2f;
                float dry = x, wet = y * scale;
                if (compensate) restoreTop (c, dry, wet);
                if (trim) wet = trimPeaks (c, wet, scale);
                ch[c][i] = dry + (wet - dry) * mix;
            }
    }

private:
    static constexpr float kAutoRef = 0.125f;   // -18 dBFS RMS: a sound's peaks sit about at full scale
    static constexpr float kTop = 0.2f;         // the compensation's strength (flattest at 44.1 and 48 kHz)

    // Undoes the ADAA's pair average, (1 + z^-1) / 2, on the wet signal with
    // the symmetric FIR [-k, 1 + 2k, -k] (flat within 0.4 dB to 12 kHz at a
    // 44.1 kHz base rate, from -3.7 dB). It looks one sample ahead, so both
    // the wet and the dry signal come out one sample late.
    struct History { float wet1 = 0.f, wet2 = 0.f, dry1 = 0.f; };
    std::array<History, 2> hist {};
    bool compensate = false;

    // The low cut's peaks back down to the shaper's ceiling (in the wet
    // signal's scale), after the top is restored, which raises them too.
    float trimPeaks (int c, float wet, float ceiling) noexcept
    {
        const float res = ceiling * top[c].process (wet / ceiling);
        return wet + trim2[c].process (trim1[c].process (res).hp).hp;
    }

    void restoreTop (int c, float& dry, float& wet) noexcept
    {
        auto& h = hist[(size_t) c];
        const float w = (1.f + 2.f * kTop) * h.wet1 - kTop * (wet + h.wet2);
        h.wet2 = h.wet1;
        h.wet1 = wet;
        wet = w;
        std::swap (dry, h.dry1);
    }

    float fs = 48000.f;
    float meanSquare = 0.f, level = 0.f;   // auto level: the mean square, then the followed level
    std::array<AdaaShaper, 2> sh;
    std::array<Svf, 2> tone, low1, low2, trim1, trim2;   // trim: the low cut again, on the re-clip's residual
    std::array<ClipResidual, 2> top;
    std::array<DcBlocker, 2> dc;
};

// ---------------------------------------------------------------------------
class Bitcrusher
{
public:
    void reset() noexcept { cnt = 0.f; hl = hr = 0.f; }

    void process (float* l, float* r, int n, float bits, float factor, float mix) noexcept
    {
        const float q = std::exp2 (bits - 1.f);
        for (int i = 0; i < n; ++i)
        {
            cnt += 1.f;
            if (cnt >= factor)
            {
                cnt -= factor;
                hl = std::round (l[i] * q) / q;
                hr = std::round (r[i] * q) / q;
            }
            l[i] += (hl - l[i]) * mix;
            r[i] += (hr - r[i]) * mix;
        }
    }

private:
    float cnt = 0.f, hl = 0.f, hr = 0.f;
};

// ---------------------------------------------------------------------------
// Frequency shifter: 90-degree allpass pair (Olli Niemitalo's 8th-order
// design, flat within 0.7 deg over 20 Hz..20 kHz), single-sideband modulation,
// saturating feedback. Right channel can shift in the opposite direction.
// ---------------------------------------------------------------------------
class FrequencyShifter
{
public:
    void prepare (float sr) { fs = sr; reset(); }
    void reset() noexcept
    {
        for (auto& c : chans) c = {};
        phaseL = phaseR = 0.0;
    }

    void process (float* l, float* r, int n, float hz, float feedback, float spread, float mix) noexcept
    {
        const double incL = hz / fs;
        const double incR = hz * (1.0 - 2.0 * spread) / fs;
        for (int i = 0; i < n; ++i)
        {
            phaseL += incL;
            phaseL -= std::floor (phaseL);
            phaseR += incR;
            phaseR -= std::floor (phaseR);
            l[i] = processChannel (chans[0], l[i], (float) (kTwoPi * phaseL), feedback, mix);
            r[i] = processChannel (chans[1], r[i], (float) (kTwoPi * phaseR), feedback, mix);
        }
    }

private:
    struct Allpass2
    {
        float c = 0.f, x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f;
        float process (float x) noexcept
        {
            const float y = c * (x + y2) - x2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = flushDenormal (y);
            return y;
        }
    };

    struct Chan
    {
        std::array<Allpass2, 4> a, b;
        float delayA = 0.f, fb = 0.f;
        bool init = false;
    };

    std::array<Chan, 2> chans;
    float fs = 48000.f;
    double phaseL = 0.0, phaseR = 0.0;

    static float processChannel (Chan& ch, float x, float theta, float feedback, float mix) noexcept
    {
        if (! ch.init)
        {
            static constexpr float ca[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
            static constexpr float cb[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
            for (int k = 0; k < 4; ++k)
            {
                ch.a[(size_t) k].c = ca[k] * ca[k];
                ch.b[(size_t) k].c = cb[k] * cb[k];
            }
            ch.init = true;
        }
        const float in = x + fastTanh (ch.fb * feedback);
        float re = in, im = in;
        for (auto& s : ch.a) re = s.process (re);
        for (auto& s : ch.b) im = s.process (im);
        const float reD = ch.delayA;  // one-sample delay on the first branch
        ch.delayA = re;
        const float y = reD * std::cos (theta) + im * std::sin (theta);
        ch.fb = y;
        return x + (y - x) * mix;
    }
};

// ---------------------------------------------------------------------------
// Six-stage TPT allpass phaser, stereo LFO in quadrature.
// ---------------------------------------------------------------------------
class Phaser
{
public:
    void prepare (float sr) { fs = sr; reset(); }
    void reset() noexcept { for (auto& c : st) c.fill (0.f); fb.fill (0.f); lfo = 0.f; }

    void process (float* l, float* r, int n, float rate, float depth, float feedback, float centre, float mix) noexcept
    {
        float* ch[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            lfo += rate / fs;
            if (lfo >= 1.f) lfo -= 1.f;
            for (int c = 0; c < 2; ++c)
            {
                const float m = std::sin (kTwoPi * (lfo + 0.25f * (float) c));
                const float f = std::clamp (centre * std::exp2 (m * depth * 2.5f), 20.f, 0.45f * fs);
                const float g = prewarp (f, fs);
                const float G = g / (1.f + g);
                float x = ch[c][i] + fb[(size_t) c] * feedback;
                for (auto& s : st[(size_t) c])
                {
                    const float v = (x - s) * G;
                    const float lp = v + s;
                    s = lp + v;
                    x = 2.f * lp - x;
                }
                fb[(size_t) c] = flushDenormal (fastTanh (x));
                ch[c][i] += (x - ch[c][i]) * mix;
            }
        }
    }

private:
    float fs = 48000.f, lfo = 0.f;
    std::array<std::array<float, 6>, 2> st {};
    std::array<float, 2> fb {};
};

// ---------------------------------------------------------------------------
class ChorusFlanger
{
public:
    void prepare (float sr)
    {
        fs = sr;
        for (auto& d : lines) d.prepare ((int) (0.05f * sr));
        reset();
    }
    void reset() noexcept { for (auto& d : lines) d.reset(); lfo = 0.f; fb.fill (0.f); }

    void process (float* l, float* r, int n, bool flanger, float rate, float depth, float feedback, float mix) noexcept
    {
        const float baseMs = flanger ? 1.2f : 11.f;
        const float modMs = flanger ? 1.0f * depth : 6.f * depth;
        float* ch[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            lfo += rate / fs;
            if (lfo >= 1.f) lfo -= 1.f;
            for (int c = 0; c < 2; ++c)
            {
                const float m = std::sin (kTwoPi * (lfo + 0.25f * (float) c));
                const float dMs = baseMs + modMs * (0.5f + 0.5f * m);
                const float x = ch[c][i];
                auto& line = lines[(size_t) c];
                line.push (x + fastTanh (fb[(size_t) c] * feedback));
                const float wet = line.read (dMs * 0.001f * fs);
                fb[(size_t) c] = wet;
                ch[c][i] = x + (wet - x) * mix * (flanger ? 1.f : 0.7f);
            }
        }
    }

private:
    float fs = 48000.f, lfo = 0.f;
    std::array<DelayLine, 2> lines;
    std::array<float, 2> fb {};
};

// ---------------------------------------------------------------------------
class StereoDelay
{
public:
    void prepare (float sr)
    {
        fs = sr;
        for (auto& d : lines) d.prepare ((int) (2.1f * sr));
        reset();
    }
    void reset() noexcept
    {
        for (auto& d : lines) d.reset();
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        smoothTime.reset (-1.f);
    }

    void process (float* l, float* r, int n, float timeMs, float feedback, bool pingPong, float lowCut, float highCut, float mix) noexcept
    {
        const float target = std::clamp (timeMs * 0.001f * fs, 1.f, 2.f * fs);
        if (smoothTime.z < 0.f) smoothTime.reset (target);
        smoothTime.setTime (0.08f, fs);
        for (int c = 0; c < 2; ++c)
        {
            hp[(size_t) c].setG (prewarp (lowCut, fs), 1.41421356f);
            lp[(size_t) c].setG (prewarp (highCut, fs), 1.41421356f);
        }
        for (int i = 0; i < n; ++i)
        {
            const float d = smoothTime.process (target);
            float wl = lines[0].read (d), wr = lines[1].read (d);
            wl = lp[0].process (hp[0].process (wl).hp).lp;
            wr = lp[1].process (hp[1].process (wr).hp).lp;
            const float inL = l[i], inR = r[i];
            if (pingPong)
            {
                const float mono = 0.5f * (inL + inR);
                lines[0].push (flushDenormal (mono + fastTanh (wr * feedback)));
                lines[1].push (flushDenormal (fastTanh (wl * feedback)));
            }
            else
            {
                lines[0].push (flushDenormal (inL + fastTanh (wl * feedback)));
                lines[1].push (flushDenormal (inR + fastTanh (wr * feedback)));
            }
            l[i] = inL + wl * mix;
            r[i] = inR + wr * mix;
        }
    }

private:
    float fs = 48000.f;
    std::array<DelayLine, 2> lines;
    std::array<Svf, 2> hp, lp;
    OnePole smoothTime;
};

// ---------------------------------------------------------------------------
// 8-line feedback delay network reverb: input diffusion (4 allpasses),
// orthogonal Householder feedback matrix, per-line RT60-derived gains with
// one-pole damping, slow delay modulation on two lines against metallic
// ringing, pre-delay.
// ---------------------------------------------------------------------------
class Reverb
{
public:
    void prepare (float sr)
    {
        fs = sr;
        const float scale = sr / 48000.f;
        for (int i = 0; i < 8; ++i)
            lines[(size_t) i].prepare ((int) (kBaseLen[i] * 2.2f * scale) + 64);
        for (int i = 0; i < 4; ++i)
            diff[(size_t) i].line.prepare ((int) (kDiffLen[i] * scale * 1.5f) + 8);
        pre.prepare ((int) (0.26f * sr));
        reset();
    }

    void reset() noexcept
    {
        for (auto& l : lines) l.reset();
        for (auto& d : diff) d.line.reset();
        pre.reset();
        damp.fill (0.f);
        modPhase = 0.f;
    }

    void process (float* l, float* r, int n, float size, float decaySec, float damping, float preMs, float mix) noexcept
    {
        const float scale = fs / 48000.f * (0.35f + 1.65f * size);
        std::array<float, 8> len;
        std::array<float, 8> gain;
        for (int i = 0; i < 8; ++i)
        {
            len[(size_t) i] = kBaseLen[i] * scale;
            gain[(size_t) i] = std::pow (10.f, -3.f * len[(size_t) i] / (fs * std::max (decaySec, 0.05f)));
        }
        const float dampCoef = 0.05f + 0.9f * damping;
        const float preSamples = std::max (preMs * 0.001f * fs, 1.f);

        for (int s = 0; s < n; ++s)
        {
            pre.push (0.5f * (l[s] + r[s]));
            float x = pre.read (preSamples);

            // input diffusion
            for (int k = 0; k < 4; ++k)
            {
                auto& d = diff[(size_t) k];
                const float dl = kDiffLen[k] * (fs / 48000.f) * (0.6f + 0.4f * size + 0.4f);
                const float delayed = d.line.read (dl);
                const float v = x + 0.62f * delayed;
                d.line.push (v);
                x = delayed - 0.62f * v;
            }

            modPhase += 0.37f / fs;
            if (modPhase >= 1.f) modPhase -= 1.f;
            const float mod = std::sin (kTwoPi * modPhase) * 12.f * (fs / 48000.f);

            std::array<float, 8> o;
            for (int i = 0; i < 8; ++i)
            {
                float d = len[(size_t) i];
                if (i == 1) d += mod;
                if (i == 6) d -= mod;
                const float y = lines[(size_t) i].read (d);
                damp[(size_t) i] += (1.f - dampCoef) * (y - damp[(size_t) i]);
                o[(size_t) i] = damp[(size_t) i] * gain[(size_t) i];
            }
            // Householder: y = o - (2/N) * sum(o)
            float sum = 0.f;
            for (float v : o) sum += v;
            sum *= 0.25f;
            for (int i = 0; i < 8; ++i)
                lines[(size_t) i].push (flushDenormal (o[(size_t) i] - sum + (i % 2 == 0 ? x : -x) * 0.5f));

            const float wl = (o[0] + o[2] + o[4] + o[6]) * 0.6f;
            const float wr = (o[1] + o[3] + o[5] + o[7]) * 0.6f;
            l[s] = l[s] * (1.f - 0.4f * mix) + wl * mix;
            r[s] = r[s] * (1.f - 0.4f * mix) + wr * mix;
        }
    }

private:
    static constexpr float kBaseLen[8] = { 1117.f, 1301.f, 1493.f, 1669.f, 1867.f, 2053.f, 2251.f, 2437.f };
    static constexpr float kDiffLen[4] = { 142.f, 107.f, 379.f, 277.f };

    struct Diffuser { DelayLine line; };

    float fs = 48000.f, modPhase = 0.f;
    std::array<DelayLine, 8> lines;
    std::array<Diffuser, 4> diff;
    DelayLine pre;
    std::array<float, 8> damp {};
};

// ---------------------------------------------------------------------------
// Stereo width with a mono low end, in mid/side.
//   Low Mono  below the split the side is removed: an LR4 high-pass on S, and
//             the mid gets the matching allpass, so the output is exactly the
//             (allpassed) input minus the low side and nothing else changes.
//   Width     scales the side above the split (0 = mono, 1 = as is, 2 = double).
//   Stereoize makes side out of the mid for mono sources: the high-passed mid,
//             delayed a few ms (Haas). It goes to L with + and R with -, so
//             L + R never changes: the mix stays mono-compatible.
// On/off crossfades over 10 ms.
// ---------------------------------------------------------------------------
class StereoWidth
{
public:
    void prepare (float sr)
    {
        fs = sr;
        fadeStep = 1.f / (0.01f * sr);
        line.prepare ((int) (0.035f * sr) + 8);
        reset();
    }

    void reset() noexcept
    {
        for (auto* s : { &sHp1, &sHp2, &mAp, &mHp1, &mHp2 }) s->reset();
        line.reset();
        time.reset (-1.f);
        rWidth.reset();
        rStereo.reset();
        fade = 0.f;
        running = false;
    }

    bool isActive (bool on) const noexcept { return on || fade > 0.f; }

    void process (float* l, float* r, int n, bool on, float width, float stereoize, float timeMs, float monoHz) noexcept
    {
        if (! running)
        {
            if (! on) return;
            reset();   // off for a while: start clean
            running = true;
        }
        const float g = prewarp (std::clamp (monoHz, 10.f, 0.2f * fs), fs);
        for (auto* s : { &sHp1, &sHp2, &mAp, &mHp1, &mHp2 }) s->setG (g, kSqrt2);
        rWidth.set (std::max (width, 0.f), n);
        rStereo.set (clamp01 (stereoize), n);
        const float delay = std::clamp (timeMs * 0.001f * fs, 1.f, 0.03f * fs);
        if (time.z < 0.f) time.reset (delay);
        time.setTime (0.05f, fs);

        for (int i = 0; i < n; ++i)
        {
            fade = on ? std::min (1.f, fade + fadeStep) : std::max (0.f, fade - fadeStep);
            const float xl = l[i], xr = r[i];
            const float m = 0.5f * (xl + xr), s = 0.5f * (xl - xr);

            const float sHi = sHp2.process (sHp1.process (s).hp).hp;
            const float mAll = m - 2.f * kSqrt2 * mAp.process (m).bp;   // LP4 + HP4 of the mid
            line.push (mHp2.process (mHp1.process (m).hp).hp);
            const float side = rWidth.next() * sHi + rStereo.next() * line.read (time.process (delay));

            l[i] = xl + fade * (mAll + side - xl);
            r[i] = xr + fade * (mAll - side - xr);
            if (fade <= 0.f && ! on)
            {
                running = false;   // the rest of the block passes through
                return;
            }
        }
        for (auto* s : { &sHp1, &sHp2, &mAp, &mHp1, &mHp2 })
        {
            s->ic1 = flushDenormal (s->ic1);
            s->ic2 = flushDenormal (s->ic2);
        }
    }

private:
    static constexpr float kSqrt2 = 1.41421356237f;
    float fs = 48000.f, fadeStep = 1.f / 480.f, fade = 0.f;
    bool running = false;
    Svf sHp1, sHp2, mAp, mHp1, mHp2;
    DelayLine line;
    OnePole time;
    Ramp rWidth, rStereo;
};

} // namespace psy::dsp
