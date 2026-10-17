#include "Wavetable.h"

#include "Common.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>

namespace psy::dsp
{

namespace
{
    using cd = std::complex<double>;

    void fft (std::vector<cd>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = 2.0 * kPiD / (double) len * (inverse ? 1.0 : -1.0);
            const cd wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                cd w (1.0);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const cd u = a[i + k];
                    const cd v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
        if (inverse)
            for (auto& x : a)
                x /= (double) n;
    }

    // A frame is described either by harmonic amplitudes/phases or by a
    // time-domain single cycle; both end up as a spectrum.
    using Spectrum = std::vector<cd>;  // index = harmonic, size kSize

    constexpr int N = WavetableBank::kSize;

    Spectrum fromHarmonics (const std::function<double (int)>& amp, const std::function<double (int)>& phase = nullptr)
    {
        Spectrum s ((size_t) N, cd (0.0));
        for (int h = 1; h <= WavetableBank::kMaxHarmonics; ++h)
        {
            double a = amp (h);
            if (a == 0.0)
                continue;
            double ph = phase ? phase (h) : 0.0;
            if (a < 0.0)  // std::polar needs a non-negative magnitude
            {
                a = -a;
                ph += kPiD;
            }
            // sin(h*w*t + ph) as a complex bin pair
            s[(size_t) h] = std::polar (a * 0.5, ph - kPiD * 0.5);
            s[(size_t) (N - h)] = std::conj (s[(size_t) h]);
        }
        return s;
    }

    Spectrum fromTime (const std::function<double (double)>& cycle)
    {
        // Evaluate at 8x density and box-average to limit aliasing in the
        // source cycle itself before the FFT.
        std::vector<cd> x ((size_t) N);
        for (int i = 0; i < N; ++i)
        {
            double acc = 0.0;
            for (int k = 0; k < 8; ++k)
                acc += cycle (((double) i + (k + 0.5) / 8.0) / N);
            x[(size_t) i] = acc / 8.0;
        }
        fft (x, false);
        for (auto& c : x)
            c *= 2.0 / N;
        x[0] = 0.0;  // no DC
        // store with the same scaling convention as fromHarmonics (amp/2 per bin)
        for (auto& c : x)
            c *= 0.5;
        return x;
    }

    double gauss (double x, double mu, double sigma) { return std::exp (-0.5 * (x - mu) * (x - mu) / (sigma * sigma)); }
} // namespace

WavetableBank::WavetableBank()
{
    data.assign ((size_t) kTables * kFrames * kLevels * (kSize + 4), 0.f);
    build();
}

const WavetableBank& WavetableBank::shared()
{
    static const WavetableBank bank;
    return bank;
}

void WavetableBank::build()
{
    const auto designFrame = [] (int table, double m) -> Spectrum {  // m in [0,1]
        switch (table)
        {
            case 0:  // Harmonic Sweep: sine opening up to a full saw
            {
                const double cutoff = 1.0 + std::pow (m, 2.2) * 120.0;
                return fromHarmonics ([=] (int h) { return (1.0 / h) * (h <= 1 ? 1.0 : std::exp (-(h - 1) / cutoff)); });
            }
            case 1:  // PWM Stack: pulse narrowing from square to needle
            {
                const double pw = 0.5 - 0.46 * m;
                return fromHarmonics ([=] (int h) { return 2.0 / (h * kPiD) * std::sin (h * kPiD * pw); });
            }
            case 2:  // Vox: formant bumps travelling a-e-i-o-u (f0 ~ 110 Hz)
            {
                static const double f1[5] = { 730, 530, 270, 570, 300 };
                static const double f2[5] = { 1090, 1840, 2290, 840, 870 };
                static const double f3[5] = { 2440, 2480, 3010, 2410, 2240 };
                const double pos = m * 4.0;
                const int i = std::min ((int) pos, 3);
                const double fr = pos - i;
                const double F1 = (f1[i] + (f1[i + 1] - f1[i]) * fr) / 110.0;
                const double F2 = (f2[i] + (f2[i + 1] - f2[i]) * fr) / 110.0;
                const double F3 = (f3[i] + (f3[i + 1] - f3[i]) * fr) / 110.0;
                return fromHarmonics ([=] (int h) {
                    return (1.0 / std::sqrt ((double) h))
                         * (gauss (h, F1, 1.2) + 0.6 * gauss (h, F2, 1.8) + 0.3 * gauss (h, F3, 2.5) + 0.02);
                });
            }
            case 3:  // Metallic: sparse, clangorous partial sets with a moving emphasis
                return fromHarmonics (
                    [=] (int h) {
                        const bool keep = (h == 1) || (h % 3 == 1 && h < 60) || (h == 7 || h == 11 || h == 13 || h == 17 || h == 23);
                        if (! keep)
                            return 0.0;
                        const double centre = 2.0 + m * 40.0;
                        return (h == 1 ? 0.6 : 0.0) + gauss (std::log2 ((double) h), std::log2 (centre), 0.8) / std::sqrt ((double) h);
                    },
                    [] (int h) { return h * 1.2345; });
            case 4:  // Digital Grit: sine quantised to fewer and fewer steps
            {
                const double steps = std::max (2.0, 64.0 * std::pow (1.0 - m, 2.0) + 2.0);
                return fromTime ([=] (double t) { return std::round (std::sin (2.0 * kPiD * t) * steps) / steps; });
            }
            case 5:  // Sync Sweep: hard-synced saw, slave ratio 1 -> 8
            {
                const double ratio = 1.0 + m * 7.0;
                return fromTime ([=] (double t) {
                    const double s = t * ratio;
                    return 2.0 * (s - std::floor (s)) - 1.0;
                });
            }
            case 6:  // Bell Spectra: strong odd partials with shifting balance
                return fromHarmonics ([=] (int h) {
                    static const int partials[] = { 1, 2, 3, 5, 7, 9, 11, 14, 19 };
                    for (int k = 0; k < 9; ++k)
                        if (partials[k] == h)
                            return std::pow (0.72, k * (1.0 - m)) * (k % 2 == 0 ? 1.0 : 0.5 + m);
                    return 0.0;
                });
            default:  // Acid Morph: saw -> tanh-squashed saw -> square
            {
                const double drive = 1.0 + m * 12.0;
                return fromTime ([=] (double t) {
                    const double saw = 2.0 * t - 1.0;
                    const double shaped = std::tanh (drive * saw) / std::tanh (drive);
                    return (1.0 - m) * saw + m * shaped;
                });
            }
        }
    };

    std::vector<cd> buf ((size_t) N);
    for (int t = 0; t < kTables; ++t)
    {
        for (int f = 0; f < kFrames; ++f)
        {
            const Spectrum spec = designFrame (t, (double) f / (kFrames - 1));

            // Normalise every frame (at level 0) to the same peak.
            float peak = 0.f;
            for (int l = 0; l < kLevels; ++l)
            {
                const int maxH = kMaxHarmonics >> l;
                for (int i = 0; i < N; ++i)
                {
                    const int h = i <= N / 2 ? i : N - i;
                    buf[(size_t) i] = (h >= 1 && h <= maxH) ? spec[(size_t) i] : cd (0.0);
                }
                fft (buf, true);
                float* out = slot (t, f, l);
                for (int i = 0; i < N; ++i)
                    out[i] = (float) (buf[(size_t) i].real() * N);
                if (l == 0)
                    for (int i = 0; i < N; ++i)
                        peak = std::max (peak, std::abs (out[i]));
            }
            const float g = peak > 1e-9f ? 0.9f / peak : 0.f;
            for (int l = 0; l < kLevels; ++l)
            {
                float* out = slot (t, f, l);
                for (int i = 0; i < N; ++i)
                    out[i] *= g;
                out[N] = out[0];
                out[N + 1] = out[1];
                out[N + 2] = out[2];
                out[N + 3] = out[N - 1];  // guard for index -1
            }
        }
    }
}

const float* WavetableBank::frame (int table, int f) const noexcept
{
    return slot (std::clamp (table, 0, kTables - 1), std::clamp (f, 0, kFrames - 1), 0);
}

float WavetableBank::readFrame (int t, int f, int l0, float darker, float phase) const noexcept
{
    // The two lowest levels (256 and 512 harmonics, for the low notes) hold
    // partials up to an eighth and a quarter of the table's rate, where the
    // 4-point Hermite read images at -60 .. -76 dB; they get the 6-point,
    // 5th-order Lagrange read, which is at the -92 dB floor of the test
    // there at 2x. Higher levels have far fewer harmonics per table sample
    // and keep the cheaper Hermite.
    const auto read = [phase] (const float* tab, int level) {
        const float x = phase * (float) N;
        const int i = (int) x;
        const float fr = x - (float) i;
        if (level > 1)
        {
            const float xm1 = i == 0 ? tab[N + 3] : tab[i - 1];
            return hermite4 (xm1, tab[i], tab[i + 1], tab[i + 2], fr);
        }
        // points i-2 .. i+3 (the table is periodic: the guard samples cover the right side)
        const float y0 = tab[i < 2 ? N + i - 2 : i - 2], y1 = i == 0 ? tab[N + 3] : tab[i - 1];
        return lagrange6 (y0, y1, tab[i], tab[i + 1], tab[i + 2], tab[i + 3], fr);
    };

    const float a = read (slot (t, f, l0), l0);
    if (darker <= 0.f || l0 + 1 >= kLevels)
        return a;
    return a + (read (slot (t, f, l0 + 1), l0 + 1) - a) * darker;
}

float WavetableBank::read (int table, float position, float phase, float inc, float harmonicLimit, float foldLimit) const noexcept
{
    table = std::clamp (table, 0, kTables - 1);
    phase = phase - std::floor (phase);
    if (phase >= 1.f)
        phase = 0.f;

    // Level l holds 512 >> l harmonics, whose top partial sits at (512 >> l) x inc
    // of the rate. A note wants the level whose top is at the target limit; the
    // pitch falls between two levels, and the brighter one (floor of levelF)
    // reaches up to 2^frac x the target. It may only be heard while its top
    // partial stays under the fold bound, above which a partial lands in
    // the audible band after decimation (0.79 at 2x, 0.9 at 4x). So the
    // crossfade to the darker level is finished by the point where the
    // brighter one reaches that bound, instead of spreading over a whole
    // octave: below that point the sound is the same as before.
    const float fold = foldLimit > 0.f ? foldLimit : std::min (harmonicLimit * 1.27f, 0.88f);
    const float target = std::min (harmonicLimit, fold * 0.933f);
    const float fadeSpan = std::min (1.f, std::log2 (fold / target));
    const float allowed = std::max (target / std::max (inc, 1e-7f), 1.f);
    const float levelF = std::clamp (std::log2 ((float) kMaxHarmonics / allowed), 0.f, (float) (kLevels - 1));
    const int l0 = std::min ((int) levelF, kLevels - 1);
    const float darker = std::min (1.f, (levelF - (float) l0) / fadeSpan);

    const float fp = std::clamp (position, 0.f, 1.f) * (kFrames - 1);
    const int f0 = std::min ((int) fp, kFrames - 2);
    const float ff = fp - (float) f0;

    const float a = readFrame (table, f0, l0, darker, phase);
    if (ff <= 1e-4f)
        return a;
    const float b = readFrame (table, f0 + 1, l0, darker, phase);
    return a + (b - a) * ff;
}

} // namespace psy::dsp
