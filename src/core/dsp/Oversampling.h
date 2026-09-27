#pragma once

// Linear-phase half-band FIR decimators. The voices render directly at the
// oversampled rate, so only the way down is needed: one decimator per output
// channel, shared by all voices (instead of one per voice).
//
// Taps are designed at startup with a Kaiser window. For the 2x stage the
// transition band is 20 kHz .. 28 kHz at 96 kHz (100 dB stopband): anything
// that would fold below 20 kHz is removed. The 4x->2x stage only needs to
// protect the 0..20 kHz band, so its transition is much wider and it is short.

#include "Common.h"

#include <array>
#include <vector>

namespace psy::dsp
{

class HalfbandDecimator
{
public:
    // q: number of non-zero odd tap pairs. Length N = 4q + 1, latency 2q input
    // samples = q output samples.
    void design (int q, double transitionFraction, double attenuationDb)
    {
        const int N = 4 * q + 1;
        const int c = (N - 1) / 2;
        const double beta = attenuationDb > 50.0 ? 0.1102 * (attenuationDb - 8.7)
                                                 : 0.5842 * std::pow (attenuationDb - 21.0, 0.4) + 0.07886 * (attenuationDb - 21.0);
        (void) transitionFraction;  // length chosen by the caller for this transition

        const auto i0 = [] (double x) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 50; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };

        std::vector<double> h ((size_t) N);
        double dc = 0.0;
        for (int n = 0; n < N; ++n)
        {
            const int m = n - c;
            const double sinc = m == 0 ? 0.5 : std::sin (0.5 * kPiD * m) / (kPiD * m);
            const double r = (2.0 * n) / (N - 1) - 1.0;
            const double w = i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta);
            h[(size_t) n] = sinc * w;
            dc += h[(size_t) n];
        }
        for (auto& v : h)
            v /= dc;

        centre = (float) h[(size_t) c];
        taps.clear();
        for (int k = 1; k <= c; k += 2)
            taps.push_back ((float) h[(size_t) (c + k)]);  // symmetric pairs at +-k

        latencyIn = c;
        const int histSize = N + 8;
        hist.assign ((size_t) histSize * 2, 0.f);
        histLen = histSize;
        pos = 0;
    }

    void reset() noexcept
    {
        std::fill (hist.begin(), hist.end(), 0.f);
        pos = 0;
    }

    int latencyInputSamples() const noexcept { return latencyIn; }

    // in: 2*numOut samples, out: numOut samples (may alias `in`).
    void process (const float* in, float* out, int numOut) noexcept
    {
        const int c = latencyIn;
        for (int m = 0; m < numOut; ++m)
        {
            push (in[2 * m]);
            push (in[2 * m + 1]);
            // Newest sample is at index pos-1 in the doubled history.
            const float* x = hist.data() + pos + histLen - 1;  // x[0] newest, x[-j] older
            float acc = centre * x[-c];
            for (size_t k = 0; k < taps.size(); ++k)
            {
                const int off = 2 * (int) k + 1;
                acc += taps[k] * (x[-(c - off)] + x[-(c + off)]);
            }
            out[m] = acc;
        }
    }

private:
    std::vector<float> taps;
    float centre = 0.5f;
    std::vector<float> hist;  // doubled ring buffer, contiguous reads
    int histLen = 0, pos = 0, latencyIn = 0;

    void push (float v) noexcept
    {
        hist[(size_t) pos] = v;
        hist[(size_t) (pos + histLen)] = v;
        pos = (pos + 1) % histLen;
    }
};

// Decimates a block rendered at 1x, 2x or 4x down to the base rate.
class Decimator
{
public:
    void prepare (int maxBaseBlock)
    {
        stage2.design (20, 0.0833, 100.0);  // 81 taps
        stage4.design (6, 0.29, 100.0);     // 25 taps
        scratch.assign ((size_t) maxBaseBlock * 2 + 16, 0.f);
    }

    void reset() noexcept
    {
        stage2.reset();
        stage4.reset();
    }

    // Latency in base-rate samples for an oversampling factor.
    int latency (int factor) const noexcept
    {
        if (factor == 2) return stage2.latencyInputSamples() / 2;
        if (factor == 4) return stage2.latencyInputSamples() / 2 + (stage4.latencyInputSamples() + 2) / 4;
        return 0;
    }

    void process (const float* in, float* out, int numBase, int factor) noexcept
    {
        if (factor == 1)
        {
            std::copy (in, in + numBase, out);
        }
        else if (factor == 2)
        {
            stage2.process (in, out, numBase);
        }
        else
        {
            stage4.process (in, scratch.data(), numBase * 2);
            stage2.process (scratch.data(), out, numBase);
        }
    }

private:
    HalfbandDecimator stage2, stage4;
    std::vector<float> scratch;
};

} // namespace psy::dsp
