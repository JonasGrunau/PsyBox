#pragma once

// Stereo effects running at the base sample rate after the voices are summed.

#include "Common.h"
#include "Filters.h"
#include "Oversampling.h"
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

    // delay in samples (>= 1) behind the most recently pushed sample. The
    // delay is split into whole samples and a fraction before it meets the
    // write position, so the fraction keeps its precision on a long line (a
    // float position of 100000 samples would quantise it to 1/32).
    float read (float delay) const noexcept
    {
        const float d = std::clamp (delay, 1.f, (float) (mask - 3));
        const int di = (int) d;
        const float fd = d - (float) di;
        const int i = fd > 0.f ? w - 2 - di : w - 1 - di;   // the sample at or just before the read point
        const float f = fd > 0.f ? 1.f - fd : 0.f;
        return hermite4 (buf[(size_t) ((i - 1) & mask)], buf[(size_t) (i & mask)],
                         buf[(size_t) ((i + 1) & mask)], buf[(size_t) ((i + 2) & mask)], f);
    }
    float readInt (int delay) const noexcept { return buf[(size_t) ((w - 1 - delay) & mask)]; }

private:
    std::vector<float> buf;
    int mask = 0, w = 0;
};

// ---------------------------------------------------------------------------
// On/off of every effect below: the engine keeps calling `process` with `on`
// until `isActive (on)` is false. The wet signal fades over 10 ms (FxSwitch),
// the state is cleared when an effect that was fully off is switched on, so
// no stale delay or reverb audio replays, and while it is off and faded
// nothing is processed (the audio passes untouched, bit for bit).
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
        sw.prepare (sr);
        glideLen = std::max (1, (int) std::lround (0.0007f * sr));
        for (auto& d : dc) d.prepare (sr);
        reset();
    }
    void reset() noexcept
    {
        for (auto& s : sh) s.reset();
        for (auto& t : tone) t.reset();
        for (auto& d : dc) d.reset();
        resetLowCut();
        level = meanSquare = 0.f;
        hist = {};
        sw.reset();
        rGain.reset();
        rScale.reset();
        rMix.reset();
        wasCut = false;
    }

    // True while the distortion still has to run: on, or fading out.
    bool isActive (bool on) noexcept { return sw.isActive (on); }

    // Samples the output lags the input while it runs: the top restoration
    // looks one sample ahead below 60 kHz (wet and dry come out one late).
    int latencySamples() const noexcept { return compensate ? 1 : 0; }

    // lowCutHz: a 24 dB high-pass on the distorted signal, which takes out
    // the lows a clipper makes from a high-passed sound (the difference
    // tones bring the fundamental back). At 20.5 Hz or below it is skipped,
    // so the distortion sounds exactly as without it (and its filters start
    // from rest when it comes on). Cutting the lows
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
    //
    // The drive, the make-up and the mix glide (0.7 ms) to new values, so a
    // control that moves between blocks doesn't step, at any block size.
    void process (float* l, float* r, int n, int type, float driveDb, float toneHz, float mix, float lowCutHz = 20.f,
                  bool autoLevel = false, bool on = true) noexcept
    {
        if (! sw.wake (on, [this] { reset(); }))   // off for a while: start clean
            return;
        const float g = dbToGain (driveDb);
        const float comp = 1.f / std::sqrt (g);
        for (auto& t : tone) t.setG (prewarp (toneHz, fs), 1.41421356f);
        const bool cut = lowCutHz > 20.5f;
        if (cut)
        {
            if (! wasCut)
                resetLowCut();   // the filters sat idle (stale state) while the cut was off
            // two sections of a 4th-order Butterworth
            const float gc = prewarp (std::min (lowCutHz, 0.45f * fs), fs);
            for (auto& f : low1) f.setG (gc, 0.76536686f);
            for (auto& f : low2) f.setG (gc, 1.84775907f);
            for (auto& f : trim1) f.setG (gc, 0.76536686f);
            for (auto& f : trim2) f.setG (gc, 1.84775907f);
        }
        wasCut = cut;
        const bool trim = cut && type != AdaaShaper::Rectify;
        rGain.set (g, glideLen);
        rScale.set (type == AdaaShaper::Fold || type == AdaaShaper::Hard ? 0.8f : std::max (comp, 0.35f) * 1.2f, glideLen);
        rMix.set (mix, glideLen);
        const float avg = 1.f - std::exp (-1.f / (0.015f * fs));
        const float up = 1.f - std::exp (-1.f / (0.005f * fs)), down = 1.f - std::exp (-1.f / (0.15f * fs));
        float* ch[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            const float gain = rGain.next(), scale = rScale.next(), mixNow = rMix.next();
            float pre = 1.f;
            if (autoLevel)
            {
                meanSquare += (0.5f * (l[i] * l[i] + r[i] * r[i]) - meanSquare) * avg;
                meanSquare = flushDenormal (meanSquare);
                level += (meanSquare - level) * (meanSquare > level ? up : down);
                level = flushDenormal (level);
                // at most +40 dB, so silence and tails are not blown up
                pre = std::min (kAutoRef / std::sqrt (level + 1.0e-12f), 100.f);
            }
            const float fade = sw.tick (on);
            for (size_t c = 0; c < 2; ++c)
            {
                const float x = ch[c][i];
                float y = sh[c].process (autoLevel ? x * pre * gain : x * gain, type);
                y = dc[c].process (y);
                y = tone[c].process (y).lp;
                if (cut) y = low2[c].process (low1[c].process (y).hp).hp;
                float dry = x, wet = autoLevel ? y / (pre * gain) : y * scale;
                if (compensate) restoreTop ((int) c, dry, wet);
                if (trim) wet = trimPeaks ((int) c, wet, autoLevel ? 1.f / (pre * gain) : scale);
                ch[c][i] = FxSwitch::blend (x, dry + (wet - dry) * mixNow, fade);
            }
            if (sw.done (on))
            {
                sw.stop();   // the rest of the block passes through
                return;
            }
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
    bool compensate = false, wasCut = false;

    void resetLowCut() noexcept
    {
        for (auto& f : low1) f.reset();
        for (auto& f : low2) f.reset();
        for (auto& f : trim1) f.reset();
        for (auto& f : trim2) f.reset();
        for (auto& t : top) t.reset();
    }

    // The low cut's peaks back down to the shaper's ceiling (in the wet
    // signal's scale), after the top is restored, which raises them too.
    float trimPeaks (int c, float wet, float ceiling) noexcept
    {
        const auto k = (size_t) c;
        const float res = ceiling * top[k].process (wet / ceiling);
        return wet + trim2[k].process (trim1[k].process (res).hp).hp;
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
    FxSwitch sw;
    Glide rGain, rScale, rMix;
    int glideLen = 34;   // samples: 0.7 ms
};

// ---------------------------------------------------------------------------
// The distortion anywhere but first in the chain (Jilax's clipper sits last,
// after the compressor and the tails). The voices' oversampling is behind it
// there, so it brings its own: up to at least 176.4 kHz times the voices'
// factor (Eco 4x, Standard 8x, High 16x at 44.1 and 48 kHz; High is what
// renders and bounces use), the Distortion at that rate, and back down. At
// the base rate the hard clip of a dense stack folded its harmonics back as
// inharmonic fizz, about 13 dB under the top octaves at Jilax's drives.
//
// The first stage up is a full low-pass (Fir2x), flat to 19.5 kHz (20 kHz
// from 45 kHz up) and closed at the base Nyquist, 100 dB: the voices'
// decimator leaves a little folded content between 20 kHz and Nyquist, which
// a half-band lets through and the clip brings down as fizz (-30 dB on a
// clipped saw, under -55 dB with this filter). The first stage down is shut
// from fs - 19.5 kHz, so nothing the clip makes folds into the audio band.
// The stages above 2x are half-bands that guard the band under the base
// Nyquist. Not less than 4x: at 2x the shaper's pair average (ADAA) costs
// 1.9 dB at 18 kHz, at 4x 0.45. Standard at 8x: the low cut's peak trim (a
// second clip, of the spikes the cut raises) settles slowly with the rate,
// its top octave 2 dB high at 4x and 0.7 dB at 8x against 16x on a
// hard-driven stack. At 8x the whole costs about 2 % of a core.
//
// prepare() designs the first stage for the rate (it allocates: the message
// thread); setQuality() only picks the stages, so a quality change on the
// audio thread allocates nothing.
//
// On/off as for the others. The switch blends the input as it arrives with
// the delayed, distorted signal, so nothing jumps when the delay comes and
// goes (a switch while playing combs for its 10 ms).
// ---------------------------------------------------------------------------
class LateDistortion
{
public:
    LateDistortion()
    {
        for (auto& c : ch)
            for (size_t k = 1; k < kStages; ++k)
            {
                c.up[k].design (kQ[k], 100.0);
                c.down[k].design (kQ[k], 0.0, 100.0);
            }
        for (auto& b : buf) b.assign ((size_t) (kChunk << kStages), 0.f);
        tmp.assign ((size_t) (kChunk << kStages), 0.f);
        prepare (48000.f);
        setQuality (2);
    }

    // Designs the first stage for the base rate (allocates).
    void prepare (float sampleRate)
    {
        fs = sampleRate;
        const double pass = std::min (20000.0, 0.442 * fs);
        for (auto& c : ch)
        {
            c.up1.design (pass, 0.5 * fs, 2.0 * fs, 100.0);
            c.down1.design (pass, fs - pass, 2.0 * fs, 100.0);
        }
        setQuality (quality);
    }

    // engineFactor: the oversampling of the voices (1, 2 or 4).
    void setQuality (int engineFactor) noexcept
    {
        quality = std::clamp (engineFactor, 1, 4);
        const float want = 176400.f * (float) quality;
        stages = 0;
        while (stages < (int) kStages && fs * (float) (1 << stages) < 0.999f * want) ++stages;
        inner.prepare (fs * (float) (1 << stages));
        sw.prepare (fs);
        reset();
    }

    void reset() noexcept
    {
        inner.reset();
        for (auto& c : ch)
        {
            c.up1.reset();
            c.down1.reset();
            for (size_t k = 1; k < kStages; ++k)
            {
                c.up[k].reset();
                c.down[k].reset();
            }
        }
        sw.reset();
    }

    bool isActive (bool on) noexcept { return sw.isActive (on); }

    // The oversampling factor it runs at (1 to 16).
    int factor() const noexcept { return 1 << stages; }

    // Samples the output lags the input while it runs, to the nearest. The
    // first stage up delays by half its length at 2x, down by one 2x sample
    // less (it ends on the odd phase of its pair); each half-band above by
    // its q input samples up and 2q - 1 down.
    int latencySamples() const noexcept
    {
        if (stages == 0) return inner.latencySamples();
        double lat = 0.5 * ch[0].up1.delay2x() + 0.5 * (ch[0].down1.delay2x() - 1);
        for (int k = 1; k < stages; ++k)
            lat += (double) kQ[(size_t) k] / (double) (1 << k) + (double) (2 * kQ[(size_t) k] - 1) / (double) (2 << k);
        return (int) std::lround (lat) + inner.latencySamples();
    }

    // The Distortion's controls (see there), with `on` for the switch.
    void process (float* l, float* r, int n, int type, float driveDb, float toneHz, float mix, float lowCutHz,
                  bool autoLevel, bool on) noexcept
    {
        if (! sw.wake (on, [this] { reset(); }))   // off for a while: start clean
            return;
        const int f = 1 << stages;
        for (int pos = 0; pos < n; pos += kChunk)
        {
            const int m = std::min (kChunk, n - pos);
            up (0, l + pos, m);
            up (1, r + pos, m);
            // always on inside: the switch here fades it
            inner.process (buf[0].data(), buf[1].data(), m * f, type, driveDb, toneHz, mix, lowCutHz, autoLevel, true);
            down (0, m);
            down (1, m);
            for (int i = 0; i < m; ++i)
            {
                const float fade = sw.tick (on);
                l[pos + i] = FxSwitch::blend (l[pos + i], buf[0][(size_t) i], fade);
                r[pos + i] = FxSwitch::blend (r[pos + i], buf[1][(size_t) i], fade);
                if (sw.done (on))
                {
                    sw.stop();   // the rest of the block passes through
                    return;
                }
            }
        }
    }

private:
    static constexpr size_t kStages = 4;
    static constexpr std::array<int, kStages> kQ { 0, 6, 4, 4 };   // half-bands to 4x, 8x, 16x: 25, 17 and 17 taps
    static constexpr int kChunk = 64;                            // base samples per pass

    struct Chan
    {
        Fir2x up1, down1;   // base rate <-> 2x
        std::array<HalfbandInterpolator, kStages> up;   // [1..]: 2x <-> 4x and on
        std::array<HalfbandDecimator, kStages> down;
    };
    std::array<Chan, 2> ch;
    std::array<std::vector<float>, 2> buf;   // the oversampled signal, then the wet one
    std::vector<float> tmp;
    Distortion inner;
    FxSwitch sw;
    float fs = 48000.f;
    int stages = 0, quality = 2;

    // m base samples up into buf[c], through tmp so the last stage lands there
    void up (int c, const float* x, int m) noexcept
    {
        auto& s = ch[(size_t) c];
        float* out = buf[(size_t) c].data();
        if (stages == 0)
        {
            std::copy (x, x + m, out);
            return;
        }
        const float* src = x;
        int len = m;
        for (int k = 0; k < stages; ++k)
        {
            float* dst = (stages - 1 - k) % 2 == 0 ? out : tmp.data();
            for (int i = 0; i < len; ++i)
            {
                if (k == 0) s.up1.up (src[i], dst + 2 * i);
                else s.up[(size_t) k].process (src[i], dst + 2 * i);
            }
            src = dst;
            len *= 2;
        }
    }

    // buf[c] back down to m base samples, in place
    void down (int c, int m) noexcept
    {
        auto& s = ch[(size_t) c];
        float* b = buf[(size_t) c].data();
        for (int k = stages - 1; k >= 1; --k)
            s.down[(size_t) k].process (b, b, m << k);
        if (stages > 0) s.down1.down (b, b, m);
    }
};

// ---------------------------------------------------------------------------
class Bitcrusher
{
public:
    // Without it the fade assumes 48 kHz (10 ms = 480 samples).
    void prepare (float sampleRate) noexcept { sw.prepare (sampleRate); reset(); }
    void reset() noexcept { cnt = 0.f; hl = hr = 0.f; sw.reset(); }
    bool isActive (bool on) noexcept { return sw.isActive (on); }

    void process (float* l, float* r, int n, float bits, float factor, float mix, bool on = true) noexcept
    {
        if (! sw.wake (on, [this] { reset(); }))
            return;
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
            const float fade = sw.tick (on);
            l[i] = FxSwitch::blend (l[i], l[i] + (hl - l[i]) * mix, fade);
            r[i] = FxSwitch::blend (r[i], r[i] + (hr - r[i]) * mix, fade);
            if (sw.done (on))
            {
                sw.stop();
                return;
            }
        }
    }

private:
    float cnt = 0.f, hl = 0.f, hr = 0.f;
    FxSwitch sw;
};

// ---------------------------------------------------------------------------
// Frequency shifter: 90-degree allpass pair, single-sideband modulation,
// saturating feedback. Right channel can shift in the opposite direction.
// At 48 kHz and below it is Olli Niemitalo's 8th-order design (4 + 4 allpass
// sections in z^-2). That design keeps the 90 degrees only from about 50 Hz
// (0.002 of Nyquist) up, so at 96 kHz the bass was off by 20 degrees at 20 Hz
// and at 192 kHz by 46: the image of a bass note came back at -10 dB. Above
// 72 kHz the pairs are minimax fits of the phase difference over 20 Hz ..
// 20 kHz at that rate: 4 + 4 sections at 96 kHz (1.6 degrees, as at 48 kHz),
// 5 + 5 above 144 kHz (0.7 degrees).
// ---------------------------------------------------------------------------
class FrequencyShifter
{
public:
    FrequencyShifter() { design (48000.f); }

    void prepare (float sr) { fs = sr; design (sr); sw.prepare (sr); reset(); }
    void reset() noexcept
    {
        for (auto& c : chans) c.clear();
        phaseL = phaseR = 0.0;
        sw.reset();
    }
    bool isActive (bool on) noexcept { return sw.isActive (on); }

    void process (float* l, float* r, int n, float hz, float feedback, float spread, float mix, bool on = true) noexcept
    {
        if (! sw.wake (on, [this] { reset(); }))
            return;
        const double incL = hz / fs;
        const double incR = hz * (1.0 - 2.0 * spread) / fs;
        for (int i = 0; i < n; ++i)
        {
            phaseL += incL;
            phaseL -= std::floor (phaseL);
            phaseR += incR;
            phaseR -= std::floor (phaseR);
            const float fade = sw.tick (on);
            l[i] = FxSwitch::blend (l[i], processChannel (chans[0], l[i], (float) (kTwoPi * phaseL), feedback, mix), fade);
            r[i] = FxSwitch::blend (r[i], processChannel (chans[1], r[i], (float) (kTwoPi * phaseR), feedback, mix), fade);
            if (sw.done (on))
            {
                sw.stop();
                return;
            }
        }
    }

    // Sections per branch and their coefficients (the c of (c - z^-2) / (1 - c z^-2)).
    static constexpr int kMaxSections = 5;
    struct Design { int sections; std::array<float, kMaxSections> a, b; };
    static Design designFor (float sampleRate) noexcept
    {
        if (sampleRate < 72000.f)   // Niemitalo's coefficients, squared
            return { 4, { 0.47940087f, 0.87621849f, 0.97659759f, 0.99749926f, 0.f },
                        { 0.16175850f, 0.73302893f, 0.94534970f, 0.99059916f, 0.f } };
        if (sampleRate < 144000.f)
            return { 4, { 0.9987257717940627f, 0.9861601135136604f, 0.9099723868104307f, 0.5481566551987944f, 0.f },
                        { 0.9949452704526072f, 0.9639416863506184f, 0.7878542497171312f, 0.20166355223692933f, 0.f } };
        return { 5, { 0.9994879036274034f, 0.9956100263837521f, 0.8909628521071478f, 0.9773770867968602f, 0.5512374416162853f },
                    { 0.9981450834756546f, 0.9492655743767564f, 0.7736268272239082f, 0.9900681459298641f, 0.2147766732419359f } };
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
        std::array<Allpass2, kMaxSections> a, b;
        float delayA = 0.f, fb = 0.f;
        void clear() noexcept
        {
            for (auto* set : { &a, &b })
                for (auto& s : *set) s = { s.c, 0.f, 0.f, 0.f, 0.f };
            delayA = fb = 0.f;
        }
    };

    std::array<Chan, 2> chans;
    int sections = 4;
    float fs = 48000.f;
    double phaseL = 0.0, phaseR = 0.0;
    FxSwitch sw;

    void design (float sampleRate) noexcept
    {
        const auto d = designFor (sampleRate);
        sections = d.sections;
        for (auto& ch : chans)
            for (size_t k = 0; k < (size_t) kMaxSections; ++k)
            {
                ch.a[k].c = d.a[k];
                ch.b[k].c = d.b[k];
            }
    }

    float processChannel (Chan& ch, float x, float theta, float feedback, float mix) const noexcept
    {
        const float in = x + fastTanh (ch.fb * feedback);
        float re = in, im = in;
        for (size_t k = 0; k < (size_t) sections; ++k)
        {
            re = ch.a[k].process (re);
            im = ch.b[k].process (im);
        }
        const float reD = ch.delayA;  // one-sample delay on the first branch
        ch.delayA = re;
        const float y = reD * std::cos (theta) + im * std::sin (theta);
        ch.fb = y;
        return x + (y - x) * mix;
    }
};

// ---------------------------------------------------------------------------
// Six-stage TPT allpass phaser, stereo LFO in quadrature. The LFO phase is a
// double (a float one drifts a slow rate), and the two channels share one
// sin / cos pair.
// ---------------------------------------------------------------------------
class Phaser
{
public:
    void prepare (float sr) { fs = sr; sw.prepare (sr); reset(); }
    void reset() noexcept { for (auto& c : st) c.fill (0.f); fb.fill (0.f); lfo = 0.0; sw.reset(); }
    bool isActive (bool on) noexcept { return sw.isActive (on); }

    void process (float* l, float* r, int n, float rate, float depth, float feedback, float centre, float mix, bool on = true) noexcept
    {
        if (! sw.wake (on, [this] { reset(); }))
            return;
        float* ch[2] = { l, r };
        const double inc = (double) rate / (double) fs;
        const float span = depth * 2.5f;
        // without depth the sweep stands still: one coefficient for all samples
        const bool still = span <= 0.f;
        const float gStill = prewarp (std::clamp (centre, 20.f, 0.45f * fs), fs);
        for (int i = 0; i < n; ++i)
        {
            lfo += inc;
            if (lfo >= 1.0) lfo -= 1.0;
            const double a = (double) kTwoPi * lfo;
            const float m[2] = { (float) std::sin (a), (float) std::cos (a) };   // the right channel a quarter cycle on
            const float fade = sw.tick (on);
            for (size_t c = 0; c < 2; ++c)
            {
                const float g = still ? gStill : prewarp (std::clamp (centre * std::exp2 (m[c] * span), 20.f, 0.45f * fs), fs);
                const float G = g / (1.f + g);
                const float in = ch[c][i];
                float x = in + fb[c] * feedback;
                for (auto& s : st[c])
                {
                    const float v = (x - s) * G;
                    const float lp = v + s;
                    s = lp + v;
                    x = 2.f * lp - x;
                }
                fb[c] = flushDenormal (fastTanh (x));
                ch[c][i] = FxSwitch::blend (in, in + (x - in) * mix, fade);
            }
            if (sw.done (on))
            {
                sw.stop();
                return;
            }
        }
    }

private:
    float fs = 48000.f;
    double lfo = 0.0;
    std::array<std::array<float, 6>, 2> st {};
    std::array<float, 2> fb {};
    FxSwitch sw;
};

// ---------------------------------------------------------------------------
// The delay base, the swing and the wet level glide (30 ms) when the control
// or the flanger / chorus switch moves, so a block never steps them.
class ChorusFlanger
{
public:
    void prepare (float sr)
    {
        fs = sr;
        for (auto& d : lines) d.prepare ((int) (0.05f * sr));
        sw.prepare (sr);
        reset();
    }
    void reset() noexcept
    {
        for (auto& d : lines) d.reset();
        lfo = 0.0;
        fb.fill (0.f);
        sw.reset();
        primed = false;
    }
    bool isActive (bool on) noexcept { return sw.isActive (on); }

    void process (float* l, float* r, int n, bool flanger, float rate, float depth, float feedback, float mix, bool on = true) noexcept
    {
        if (! sw.wake (on, [this] { reset(); }))
            return;
        const float baseTarget = flanger ? 1.2f : 11.f;
        const float modTarget = flanger ? 1.0f * depth : 6.f * depth;
        const float wetTarget = flanger ? 1.f : 0.7f;
        if (! primed)
        {
            baseMs.reset (baseTarget);
            modMs.reset (modTarget);
            wetGain.reset (wetTarget);
            primed = true;
        }
        for (auto* o : { &baseMs, &modMs, &wetGain }) o->setTime (0.03f, fs);
        const double inc = (double) rate / (double) fs;
        float* ch[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            lfo += inc;
            if (lfo >= 1.0) lfo -= 1.0;
            const double a = (double) kTwoPi * lfo;
            const float m[2] = { (float) std::sin (a), (float) std::cos (a) };   // the right channel a quarter cycle on
            const float base = baseMs.process (baseTarget), swing = modMs.process (modTarget), wetNow = wetGain.process (wetTarget);
            const float fade = sw.tick (on);
            for (size_t c = 0; c < 2; ++c)
            {
                const float dMs = base + swing * (0.5f + 0.5f * m[c]);
                const float x = ch[c][i];
                auto& line = lines[c];
                line.push (x + fastTanh (fb[c] * feedback));
                const float wet = line.read (dMs * 0.001f * fs);
                fb[c] = wet;
                ch[c][i] = FxSwitch::blend (x, x + (wet - x) * mix * wetNow, fade);
            }
            if (sw.done (on))
            {
                sw.stop();
                return;
            }
        }
    }

private:
    float fs = 48000.f;
    double lfo = 0.0;
    std::array<DelayLine, 2> lines;
    std::array<float, 2> fb {};
    OnePole baseMs, modMs, wetGain;
    bool primed = false;
    FxSwitch sw;
};

// ---------------------------------------------------------------------------
class StereoDelay
{
public:
    void prepare (float sr)
    {
        fs = sr;
        for (auto& d : lines) d.prepare ((int) (2.1f * sr));
        sw.prepare (sr);
        reset();
    }
    void reset() noexcept
    {
        for (auto& d : lines) d.reset();
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        smoothTime.reset (-1.f);
        sw.reset();
    }
    bool isActive (bool on) noexcept { return sw.isActive (on); }

    void process (float* l, float* r, int n, float timeMs, float feedback, bool pingPong, float lowCut, float highCut, float mix,
                  bool on = true) noexcept
    {
        if (! sw.wake (on, [this] { reset(); }))   // off for a while: the old echoes must not come back
            return;
        const float target = std::clamp (timeMs * 0.001f * fs, 1.f, 2.f * fs);
        if (smoothTime.z < 0.f) smoothTime.reset (target);
        smoothTime.setTime (0.08f, fs);
        for (size_t c = 0; c < 2; ++c)
        {
            hp[c].setG (prewarp (lowCut, fs), 1.41421356f);
            lp[c].setG (prewarp (highCut, fs), 1.41421356f);
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
            const float fade = sw.tick (on);
            l[i] = FxSwitch::blend (inL, inL + wl * mix, fade);
            r[i] = FxSwitch::blend (inR, inR + wr * mix, fade);
            if (sw.done (on))
            {
                sw.stop();
                return;
            }
        }
    }

private:
    float fs = 48000.f;
    std::array<DelayLine, 2> lines;
    std::array<Svf, 2> hp, lp;
    OnePole smoothTime;
    FxSwitch sw;
};

// ---------------------------------------------------------------------------
// 8-line feedback delay network reverb: input diffusion (4 allpasses),
// orthogonal Householder feedback matrix, per-line RT60-derived gains with
// one-pole damping, slow delay modulation on two lines against metallic
// ringing, pre-delay. The line lengths, gains, pre-delay, size and damping
// glide (80 ms, damping 20 ms) to their new values, so a control moved
// between blocks neither steps nor clicks. The mix glides too (5 ms): it is
// what a sweep or an envelope modulates, and a voice's sources drop to 0 when
// it ends, so the mix fell back to its knob in one sample, in the middle of
// the tail (a click on a Jilax shot that swells its reverb over the note).
// The damping pole is set for 48 kHz and moved with the rate
// (pole^(48000 / fs)) to keep its corner in Hz.
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
        sw.prepare (sr);
        mixLen = std::max (1, (int) std::lround (0.005f * sr));
        reset();
    }

    void reset() noexcept
    {
        for (auto& l : lines) l.reset();
        for (auto& d : diff) d.line.reset();
        pre.reset();
        damp.fill (0.f);
        modPhase = 0.0;
        sw.reset();
        mixNow.reset();
        primed = false;
    }
    bool isActive (bool on) noexcept { return sw.isActive (on); }

    void process (float* l, float* r, int n, float size, float decaySec, float damping, float preMs, float mix, bool on = true) noexcept
    {
        if (! sw.wake (on, [this] { reset(); }))   // off for a while: the old tail must not come back
            return;
        const float scale = fs / 48000.f * (0.35f + 1.65f * size);
        std::array<float, 8> lenTarget, gainTarget;
        for (size_t i = 0; i < 8; ++i)
        {
            lenTarget[i] = kBaseLen[i] * scale;
            gainTarget[i] = std::pow (10.f, -3.f * lenTarget[i] / (fs * std::max (decaySec, 0.05f)));
        }
        const float dampTarget = std::pow (0.05f + 0.9f * damping, 48000.f / fs);
        const float preTarget = std::max (preMs * 0.001f * fs, 1.f);
        if (! primed)
        {
            len = lenTarget;
            gain = gainTarget;
            dampCoef = dampTarget;
            preSamples = preTarget;
            sizeNow = size;
            primed = true;
        }
        const float glide = 1.f - std::exp (-1.f / (0.08f * fs)), dampGlide = 1.f - std::exp (-1.f / (0.02f * fs));
        const double modInc = 0.37 / (double) fs;
        mixNow.set (mix, mixLen);

        for (int s = 0; s < n; ++s)
        {
            for (size_t i = 0; i < 8; ++i)
            {
                len[i] += (lenTarget[i] - len[i]) * glide;
                gain[i] += (gainTarget[i] - gain[i]) * glide;
            }
            dampCoef += (dampTarget - dampCoef) * dampGlide;
            preSamples += (preTarget - preSamples) * glide;
            sizeNow += (size - sizeNow) * glide;

            pre.push (0.5f * (l[s] + r[s]));
            float x = pre.read (preSamples);

            // input diffusion
            for (size_t k = 0; k < 4; ++k)
            {
                auto& d = diff[k];
                const float dl = kDiffLen[k] * (fs / 48000.f) * (0.6f + 0.4f * sizeNow + 0.4f);
                const float delayed = d.line.read (dl);
                const float v = x + 0.62f * delayed;
                d.line.push (v);
                x = delayed - 0.62f * v;
            }

            modPhase += modInc;
            if (modPhase >= 1.0) modPhase -= 1.0;
            const float mod = (float) std::sin (kTwoPi * modPhase) * 12.f * (fs / 48000.f);

            std::array<float, 8> o;
            for (size_t i = 0; i < 8; ++i)
            {
                float d = len[i];
                if (i == 1) d += mod;
                if (i == 6) d -= mod;
                const float y = lines[i].read (d);
                damp[i] += (1.f - dampCoef) * (y - damp[i]);
                o[i] = damp[i] * gain[i];
            }
            // Householder: y = o - (2/N) * sum(o)
            float sum = 0.f;
            for (float v : o) sum += v;
            sum *= 0.25f;
            for (size_t i = 0; i < 8; ++i)
                lines[i].push (flushDenormal (o[i] - sum + (i % 2 == 0 ? x : -x) * 0.5f));

            const float wl = (o[0] + o[2] + o[4] + o[6]) * 0.6f;
            const float wr = (o[1] + o[3] + o[5] + o[7]) * 0.6f;
            const float fade = sw.tick (on), m = mixNow.next();
            l[s] = FxSwitch::blend (l[s], l[s] * (1.f - 0.4f * m) + wl * m, fade);
            r[s] = FxSwitch::blend (r[s], r[s] * (1.f - 0.4f * m) + wr * m, fade);
            if (sw.done (on))
            {
                sw.stop();
                return;
            }
        }
    }

private:
    static constexpr float kBaseLen[8] = { 1117.f, 1301.f, 1493.f, 1669.f, 1867.f, 2053.f, 2251.f, 2437.f };
    static constexpr float kDiffLen[4] = { 142.f, 107.f, 379.f, 277.f };

    struct Diffuser { DelayLine line; };

    float fs = 48000.f;
    double modPhase = 0.0;
    std::array<DelayLine, 8> lines;
    std::array<Diffuser, 4> diff;
    DelayLine pre;
    std::array<float, 8> damp {};
    std::array<float, 8> len {}, gain {};
    float dampCoef = 0.5f, preSamples = 1.f, sizeNow = 0.5f;
    bool primed = false;
    Glide mixNow;
    int mixLen = 240;   // samples: 5 ms
    FxSwitch sw;
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

// ---------------------------------------------------------------------------
// Dimension expander, after the Roland SDD-320 Dimension D: the mid, high-
// passed at 150 Hz so the lows stay mono, feeds one delay line read by four
// taps whose lengths slow sines move in opposite pairs (the D's antiphase
// BBDs). The taps' difference is pure side: + to the left, - to the right,
// so L + R never changes (mono-compatible, nothing cancels in mono) and the
// opposed pitch drifts don't sound as a wobble. Size scales the taps from
// about 2..4.5 ms to 9..21 ms (with a 12 % swing), Mix the side level. The
// tap lengths glide (50 ms) and the mix glides over 32 samples, so a size
// move or the matrix never steps them.
// ---------------------------------------------------------------------------
class Dimension
{
public:
    void prepare (float sr)
    {
        fs = sr;
        line.prepare ((int) (0.03f * sr) + 8);
        sw.prepare (sr);
        reset();
    }

    void reset() noexcept
    {
        line.reset();
        hp1.reset();
        hp2.reset();
        for (size_t k = 0; k < 4; ++k) phase[k] = 0.25 * (double) k;
        base.reset (-1.f);
        mixNow.reset();
        sw.reset();
    }

    bool isActive (bool on) noexcept { return sw.isActive (on); }

    void process (float* l, float* r, int n, float size, float mix, bool on = true) noexcept
    {
        if (! sw.wake (on, [this] { reset(); }))
            return;
        const float baseMs = 2.f + 7.f * clamp01 (size);
        if (base.z < 0.f) base.reset (baseMs);
        base.setTime (0.05f, fs);
        mixNow.set (clamp01 (mix), 32);
        const float g = prewarp (150.f, fs);
        hp1.setG (g, 1.41421356f);
        hp2.setG (g, 1.41421356f);
        for (int i = 0; i < n; ++i)
        {
            const float xl = l[i], xr = r[i];
            const float m = 0.5f * (xl + xr);
            line.push (flushDenormal (hp2.process (hp1.process (m).hp).hp));
            const float b = base.process (baseMs) * 0.001f * fs;
            float side = 0.f;
            for (size_t k = 0; k < 4; ++k)
            {
                phase[k] += kRate[k] / (double) fs;
                if (phase[k] >= 1.0) phase[k] -= 1.0;
                const float lfo = (float) std::sin (kTwoPi * phase[k]);
                // taps 0 and 2 swing against 1 and 3, and the pairs subtract
                const float d = b * kTap[k] * (1.f + 0.12f * (k % 2 == 0 ? lfo : -lfo));
                side += (k % 2 == 0 ? 1.f : -1.f) * line.read (d);
            }
            side *= 0.4f * mixNow.next();
            const float f = sw.tick (on);
            l[i] = FxSwitch::blend (xl, xl + side, f);
            r[i] = FxSwitch::blend (xr, xr - side, f);
            if (sw.done (on))
            {
                sw.stop();
                return;
            }
        }
        for (auto* s : { &hp1, &hp2 })
        {
            s->ic1 = flushDenormal (s->ic1);
            s->ic2 = flushDenormal (s->ic2);
        }
    }

private:
    static constexpr float kTap[4] = { 1.f, 1.43f, 1.87f, 2.33f };
    static constexpr double kRate[4] = { 0.29, 0.37, 0.43, 0.51 };   // Hz

    float fs = 48000.f;
    DelayLine line;
    Svf hp1, hp2;
    std::array<double, 4> phase {};
    OnePole base;
    Glide mixNow;
    FxSwitch sw;
};

} // namespace psy::dsp
