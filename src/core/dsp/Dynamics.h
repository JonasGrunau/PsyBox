#pragma once

// Feed-forward compressor with two modes, running at the base rate:
//   Normal     one band, downward, soft knee, parallel mix
//   Multiband  three bands with upward and downward compression per band,
//              in the style of OTT and Serum's multiband compressor
//
// What keeps it clean:
//  * Linkwitz-Riley 4 crossover, with the low band passed through the upper
//    split's allpass, so the three bands sum to a flat magnitude response.
//  * The level detector holds the peak of a whole waveform cycle (a running
//    max over the band's longest period), so the gain does not follow the
//    waveform of a low note and add harmonics.
//  * Static curve and gain smoothing both in the log domain, quadratic soft
//    knee (D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range
//    Compressor Design - A Tutorial and Analysis", JAES 60(6), 2012).
//  * Stereo-linked detection, so the image never shifts.
//  * Depth / Mix scale each band's gain instead of blending in a dry copy, so
//    there is no comb filtering against the allpass-shifted bands.
//  * Upward gain is capped and fades out near digital silence, so decaying
//    tails and noise floors are not pumped up.
//  * Every gain, threshold and mix ramps per sample. Switching on, off or
//    between modes crossfades over 10 ms.

#include "Common.h"
#include "Filters.h"

#include <array>

namespace psy::dsp
{

struct CompSettings
{
    bool on = false;
    int mode = 0;                 // 0 Normal, 1 Multiband
    float attackMs = 8.f, releaseMs = 150.f;
    float mix = 1.f;              // Normal: dry/wet. Multiband: depth
    float outDb = 0.f;

    // Normal
    float threshDb = -18.f, ratio = 4.f, kneeDb = 6.f;

    // Multiband
    float down = 1.f, up = 0.5f;  // 0..1 amounts; 1 = 20:1 down, 1:4 up
    float xLow = 120.f, xHigh = 2500.f;
    std::array<float, 3> downDb { -24.f, -24.f, -24.f };
    std::array<float, 3> upDb { -48.f, -48.f, -48.f };
    std::array<float, 3> gainDb { 0.f, 0.f, 0.f };
};

// Per-band readings for the UI, refreshed every block. Normal mode uses band 0.
struct CompMeter
{
    int mode = 0;
    std::array<float, 3> inDb { -200.f, -200.f, -200.f };    // detector level, max over the block
    std::array<float, 3> gainDb { 0.f, 0.f, 0.f };           // compression gain (+ up, - down)
    std::array<float, 3> totalDb { 0.f, 0.f, 0.f };          // with band gain and depth applied
};

namespace dyn
{
    constexpr float kSqrt2 = 1.41421356237f;
    constexpr float kMbKneeDb = 6.f;
    constexpr float kMaxBoostDb = 24.f;
    constexpr float kFloorDb = -78.f;       // upward gain is fully off below this level
    constexpr float kFloorFadeDb = 12.f;    // ... and fully on 12 dB above it
    constexpr float kLowestHz = 30.f;       // longest period the low band / Normal mode holds

    // Downward gain in dB (<= 0) for a level x, soft knee of width w.
    inline float downGain (float x, float thr, float slope, float w) noexcept
    {
        const float d = x - thr;
        if (2.f * d <= -w) return 0.f;
        if (2.f * d < w)
        {
            const float e = d + 0.5f * w;
            return -slope * e * e / (2.f * w);
        }
        return -slope * d;
    }

    // Upward gain in dB (>= 0), mirrored curve below thr, capped, and faded
    // out near silence.
    inline float upGain (float x, float thr, float slope, float w) noexcept
    {
        const float d = thr - x;
        float g;
        if (2.f * d <= -w) return 0.f;
        if (2.f * d < w)
        {
            const float e = d + 0.5f * w;
            g = slope * e * e / (2.f * w);
        }
        else
        {
            g = slope * d;
        }
        const float floorFade = std::clamp ((x - kFloorDb) / kFloorFadeDb, 0.f, 1.f);
        return std::min (g, kMaxBoostDb) * floorFade;
    }

    // Running max of |x| over the last 3/4 to 4/4 of a window, kept in four
    // buckets: no buffer, no allocation, and the window can change any time.
    struct PeakHold
    {
        std::array<float, 4> b {};
        int idx = 0, count = 0, len = 1;

        void setWindow (float seconds, float sampleRate) noexcept
        {
            len = std::max (1, (int) (seconds * sampleRate * 0.25f));
        }
        void reset() noexcept { b.fill (0.f); idx = count = 0; }

        float process (float x) noexcept
        {
            b[(size_t) idx] = std::max (b[(size_t) idx], x);
            const float y = std::max (std::max (b[0], b[1]), std::max (b[2], b[3]));
            if (++count >= len)
            {
                count = 0;
                idx = (idx + 1) & 3;
                b[(size_t) idx] = 0.f;
            }
            return y;
        }
    };

    // One-pole in dB: attack while the gain falls, release while it rises.
    // That also fits upward gain, whose boost drops fast when the signal gets
    // loud and comes back slowly when it gets quiet.
    struct GainSmoother
    {
        float g = 0.f, aAtt = 0.f, aRel = 0.f;

        void setTimes (float attackMs, float releaseMs, float sampleRate) noexcept
        {
            aAtt = std::exp (-1000.f / (std::max (attackMs, 0.01f) * sampleRate));
            aRel = std::exp (-1000.f / (std::max (releaseMs, 0.1f) * sampleRate));
        }
        void reset() noexcept { g = 0.f; }

        float process (float target) noexcept
        {
            const float a = target < g ? aAtt : aRel;
            g = target + a * (g - target);
            return g;
        }
    };

    // Linear per-sample ramp to a value that arrives once per block.
    struct Ramp
    {
        float cur = 0.f, step = 0.f;
        bool init = false;

        void set (float target, int n) noexcept
        {
            if (! init) { cur = target; init = true; }
            step = (target - cur) / (float) std::max (n, 1);
        }
        void reset() noexcept { init = false; step = 0.f; }
        float next() noexcept { cur += step; return cur; }
    };

    // 3-band Linkwitz-Riley 4 crossover for one channel.
    //   low  = LP4(f1), then the f2 allpass     mid = LP4(f2) of HP4(f1)
    //   high = HP4(f2) of HP4(f1)
    // LP4 + HP4 of the same split is the 2nd-order Butterworth allpass, so
    // low + mid + high = AP(f1) * AP(f2): flat magnitude, only phase shifts.
    struct Crossover3
    {
        Svf a, aLp, aHp;   // split at f1: shared first stage, then one per side
        Svf b, bLp, bHp;   // split at f2 on the upper part
        Svf ap;            // f2 allpass on the low band

        void set (float g1, float g2) noexcept
        {
            for (auto* s : { &a, &aLp, &aHp }) s->setG (g1, kSqrt2);
            for (auto* s : { &b, &bLp, &bHp, &ap }) s->setG (g2, kSqrt2);
        }
        void reset() noexcept
        {
            for (auto* s : { &a, &aLp, &aHp, &b, &bLp, &bHp, &ap }) s->reset();
        }
        void flush() noexcept
        {
            for (auto* s : { &a, &aLp, &aHp, &b, &bLp, &bHp, &ap })
            {
                s->ic1 = flushDenormal (s->ic1);
                s->ic2 = flushDenormal (s->ic2);
            }
        }

        void process (float x, std::array<float, 3>& out) noexcept
        {
            const auto s1 = a.process (x);
            const float low = aLp.process (s1.lp).lp;
            const float rest = aHp.process (s1.hp).hp;
            const auto s2 = b.process (rest);
            out[1] = bLp.process (s2.lp).lp;
            out[2] = bHp.process (s2.hp).hp;
            out[0] = low - 2.f * kSqrt2 * ap.process (low).bp;
        }
    };
} // namespace dyn

// ---------------------------------------------------------------------------
class Compressor
{
public:
    void prepare (float sampleRate)
    {
        fs = sampleRate;
        fadeStep = 1.f / (0.01f * fs);
        reset();
    }

    void reset() noexcept
    {
        resetMode();
        fade = 0.f;
        running = false;
    }

    // True while the compressor still has to run: on, or fading out.
    bool isActive (bool on) const noexcept { return on || fade > 0.f; }

    const CompMeter& meter() const noexcept { return meters; }

    void process (float* l, float* r, int n, const CompSettings& s) noexcept
    {
        if (! running)
        {
            if (! s.on) return;
            mode = s.mode;
            resetMode();
            running = true;
        }

        setBlock (s, n);
        meters.mode = mode;
        meters.inDb.fill (-200.f);

        for (int i = 0; i < n; ++i)
        {
            // Fade the wet signal out on off / mode change, switch, fade back in.
            const float target = s.on && s.mode == mode ? 1.f : 0.f;
            fade = target > fade ? std::min (target, fade + fadeStep) : std::max (target, fade - fadeStep);

            const float xl = l[i], xr = r[i];
            float yl, yr;
            if (mode == 0) processNormal (xl, xr, yl, yr);
            else processMulti (xl, xr, yl, yr);

            l[i] = xl + fade * (yl - xl);
            r[i] = xr + fade * (yr - xr);

            if (fade <= 0.f)
            {
                if (! s.on)
                {
                    running = false;   // stays bypassed until switched on again, then starts clean
                    return;
                }
                if (s.mode != mode)
                {
                    mode = s.mode;
                    resetMode();
                    setBlock (s, n - i - 1);
                }
            }
        }

        xover[0].flush();
        xover[1].flush();
        for (auto& sm : smooth) sm.g = flushDenormal (sm.g);
    }

private:
    float fs = 48000.f, fadeStep = 1.f / 480.f, fade = 0.f;
    int mode = 0;
    bool running = false;

    std::array<dyn::Crossover3, 2> xover;
    std::array<dyn::PeakHold, 3> hold;
    std::array<dyn::GainSmoother, 3> smooth;
    std::array<dyn::Ramp, 3> rDown, rUp, rBandGain;
    dyn::Ramp rThresh, rKnee, rSlope, rMix, rOut, rDownSlope, rUpSlope;
    CompMeter meters;

    void resetMode() noexcept
    {
        for (auto& x : xover) x.reset();
        for (auto& h : hold) h.reset();
        for (auto& sm : smooth) sm.reset();
        for (auto* set : { &rDown, &rUp, &rBandGain })
            for (auto& rp : *set) rp.reset();
        for (auto* rp : { &rThresh, &rKnee, &rSlope, &rMix, &rOut, &rDownSlope, &rUpSlope }) rp->reset();
        meters = {};
    }

    // Per-block setup: filter coefficients, detector windows, times, ramps.
    void setBlock (const CompSettings& s, int n) noexcept
    {
        if (n <= 0) return;
        rMix.set (clamp01 (s.mix), n);
        rOut.set (dbToGain (s.outDb), n);

        if (mode == 0)
        {
            hold[0].setWindow (1.f / dyn::kLowestHz, fs);
            smooth[0].setTimes (s.attackMs, s.releaseMs, fs);
            rThresh.set (s.threshDb, n);
            rKnee.set (std::max (s.kneeDb, 0.f), n);
            rSlope.set (1.f - 1.f / std::max (s.ratio, 1.f), n);
            return;
        }

        const float f1 = std::clamp (s.xLow, 20.f, 0.2f * fs);
        const float f2 = std::clamp (s.xHigh, 2.f * f1, 0.45f * fs);
        for (auto& x : xover) x.set (prewarp (f1, fs), prewarp (f2, fs));

        // Each band holds its own longest period (LR4 skirts reach ~0.7 x the edge).
        hold[0].setWindow (1.f / dyn::kLowestHz, fs);
        hold[1].setWindow (1.f / (0.7f * f1), fs);
        hold[2].setWindow (1.f / (0.7f * f2), fs);

        // Lows move slower, highs faster, as in most multiband designs.
        static constexpr float timeScale[3] = { 1.5f, 1.f, 0.6f };
        for (int b = 0; b < 3; ++b)
        {
            smooth[(size_t) b].setTimes (s.attackMs * timeScale[b], s.releaseMs * timeScale[b], fs);
            rDown[(size_t) b].set (s.downDb[(size_t) b], n);
            rUp[(size_t) b].set (std::min (s.upDb[(size_t) b], s.downDb[(size_t) b]), n);
            rBandGain[(size_t) b].set (s.gainDb[(size_t) b], n);
        }
        rDownSlope.set (0.95f * clamp01 (s.down), n);
        rUpSlope.set (0.75f * clamp01 (s.up), n);
    }

    void processNormal (float xl, float xr, float& yl, float& yr) noexcept
    {
        const float lvl = gainToDb (hold[0].process (std::max (std::abs (xl), std::abs (xr))));
        const float target = dyn::downGain (lvl, rThresh.next(), rSlope.next(), rKnee.next());
        const float g = smooth[0].process (target);
        const float mix = rMix.next();
        const float gain = (1.f + mix * (dbToGain (g) - 1.f)) * rOut.next();
        yl = xl * gain;
        yr = xr * gain;

        meters.inDb[0] = std::max (meters.inDb[0], lvl);
        meters.gainDb[0] = g;
        meters.totalDb[0] = gainToDb (gain);
    }

    void processMulti (float xl, float xr, float& yl, float& yr) noexcept
    {
        std::array<float, 3> bl, br;
        xover[0].process (xl, bl);
        xover[1].process (xr, br);

        const float downSlope = rDownSlope.next(), upSlope = rUpSlope.next();
        const float depth = rMix.next();
        yl = yr = 0.f;
        for (size_t b = 0; b < 3; ++b)
        {
            const float lvl = gainToDb (hold[b].process (std::max (std::abs (bl[b]), std::abs (br[b]))));
            const float target = dyn::downGain (lvl, rDown[b].next(), downSlope, dyn::kMbKneeDb)
                               + dyn::upGain (lvl, rUp[b].next(), upSlope, dyn::kMbKneeDb);
            const float g = smooth[b].process (target);
            const float w = 1.f + depth * (dbToGain (g + rBandGain[b].next()) - 1.f);
            yl += bl[b] * w;
            yr += br[b] * w;

            meters.inDb[b] = std::max (meters.inDb[b], lvl);
            meters.gainDb[b] = g;
            meters.totalDb[b] = gainToDb (std::abs (w));
        }
        const float out = rOut.next();
        yl *= out;
        yr *= out;
    }
};

} // namespace psy::dsp
