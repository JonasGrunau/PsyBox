#include "NoiseBank.h"

#include "Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <future>

namespace psy::dsp
{

// ---- the sinc read ---------------------------------------------------------------

SincTable::SincTable()
{
    constexpr double beta = 8.96;   // Kaiser, about 90 dB
    const auto i0 = [] (double x) {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 60; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    };
    const double half = kTaps / 2;
    h.assign ((size_t) (kPhases + 1) * kTaps, 0.f);
    d.assign (h.size(), 0.f);
    for (int p = 0; p <= kPhases; ++p)
    {
        const double f = (double) p / kPhases;
        double row[kTaps], sum = 0.0;
        for (int k = 0; k < kTaps; ++k)
        {
            const double t = (k - (half - 1)) - f;   // x[i - 19 + k] sits t samples from the read
            const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (kPiD * t) / (kPiD * t);
            const double r = t / half;
            row[k] = std::abs (r) >= 1.0 ? 0.0 : sinc * i0 (beta * std::sqrt (1.0 - r * r)) / i0 (beta);
            sum += row[k];
        }
        for (int k = 0; k < kTaps; ++k) h[(size_t) (p * kTaps + k)] = (float) (row[k] / sum);
    }
    for (int p = 0; p < kPhases; ++p)
        for (int k = 0; k < kTaps; ++k)
            d[(size_t) (p * kTaps + k)] = h[(size_t) ((p + 1) * kTaps + k)] - h[(size_t) (p * kTaps + k)];
}

const SincTable& SincTable::shared()
{
    static const SincTable table;
    return table;
}

// ---- the clip ------------------------------------------------------------------

namespace
{
    // Where a read past either end lands: a loop runs on from its loop
    // start (and, starting at 0, came from its end); a one-shot is silent.
    int wrapped (int i, int frames, bool loop, int from)
    {
        if (i >= 0 && i < frames) return i;
        if (! loop || (i < 0 && from > 0)) return -1;
        const int len = frames - from;
        return i < 0 ? frames + i % len : from + (i - from) % len;
    }

    // The guards: what a read past the ends finds (see wrapped).
    void guard (NoiseClip::Level& lv, bool loop, int from)
    {
        for (auto* ch : { &lv.l, &lv.r })
        {
            if (ch->empty()) continue;
            auto& v = *ch;
            const int n = lv.frames, g = NoiseClip::kGuard;
            for (int k = 0; k < g; ++k)
            {
                const int before = wrapped (k - g, n, loop, from), after = wrapped (n + k, n, loop, from);
                v[(size_t) k] = before < 0 ? 0.f : v[(size_t) (g + before)];
                v[(size_t) (g + n + k)] = after < 0 ? 0.f : v[(size_t) (g + after)];
            }
        }
    }

    // The levels' low-pass: a Kaiser-windowed sinc (100 dB) passing 0.42 of
    // the rate it leads to and stopping at 0.5 of it, so a level holds
    // nothing that folded (a half-band's transition straddles 0.5 and folds
    // the band just under it back in). 161 taps, symmetric.
    const std::vector<float>& decimator()
    {
        static const std::vector<float> taps = [] {
            constexpr int n = 161;
            constexpr double cut = 0.23, beta = 10.06;   // of the input's rate: (0.42 + 0.5) / 4
            const auto i0 = [] (double x) {
                double sum = 1.0, term = 1.0;
                for (int k = 1; k < 60; ++k)
                {
                    term *= (x / (2.0 * k)) * (x / (2.0 * k));
                    sum += term;
                }
                return sum;
            };
            std::vector<double> h (n);
            double sum = 0.0;
            for (int k = 0; k < n; ++k)
            {
                const double t = k - (n - 1) / 2.0, r = t / ((n - 1) / 2.0);
                const double sinc = t == 0.0 ? 2.0 * cut : std::sin (2.0 * kPiD * cut * t) / (kPiD * t);
                h[(size_t) k] = sinc * i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta);
                sum += h[(size_t) k];
            }
            std::vector<float> out (n);
            for (int k = 0; k < n; ++k) out[(size_t) k] = (float) (h[(size_t) k] / sum);
            return out;
        }();
        return taps;
    }

    // Low-passed (decimator()) and, with `step` 2, taken at half the rate:
    // past the ends as wrapped() says.
    std::vector<float> lowpass (const std::vector<float>& x, int frames, bool loop, int from, int step)
    {
        const auto& h = decimator();
        const int c = (int) h.size() / 2;
        const int out = frames / step;
        const auto at = [&] (int i) -> float {
            const int j = wrapped (i, frames, loop, from);
            return j < 0 ? 0.f : x[(size_t) j];
        };
        std::vector<float> y ((size_t) out);
        for (int m = 0; m < out; ++m)
        {
            const int centre = step * m;
            double acc = 0.0;
            if (centre - c >= 0 && centre + c < frames)
                for (int k = 0; k < (int) h.size(); ++k) acc += h[(size_t) k] * x[(size_t) (centre - c + k)];
            else
                for (int k = 0; k < (int) h.size(); ++k) acc += h[(size_t) k] * at (centre - c + k);
            y[(size_t) m] = (float) acc;
        }
        return y;
    }
} // namespace

NoiseClip NoiseClip::make (std::vector<float> left, std::vector<float> right, float sampleRate, bool loop, bool bandLimited, int loopStart)
{
    SincTable::shared();   // built here, never first on the audio path
    NoiseClip c;
    c.rate = sampleRate;
    c.frames = (int) left.size();
    c.loop = loop;
    c.loopStart = loop ? std::clamp (loopStart, 0, std::max (0, c.frames - 2)) : 0;
    c.stereo = ! right.empty() && right.size() == left.size();
    if (c.frames == 0)
        return c;
    std::vector<float> curL = std::move (left), curR = c.stereo ? std::move (right) : std::vector<float>();
    int frames = c.frames;
    int from = c.loopStart;
    if (! bandLimited)   // level 0 gets the levels' top too (0.42 of its rate)
    {
        curL = lowpass (curL, frames, loop, from, 1);
        if (c.stereo) curR = lowpass (curR, frames, loop, from, 1);
    }
    for (int l = 0; l < 12; ++l)
    {
        Level lv;
        lv.frames = frames;
        lv.l.assign ((size_t) (frames + 2 * kGuard), 0.f);
        std::copy (curL.begin(), curL.begin() + frames, lv.l.begin() + kGuard);
        if (c.stereo)
        {
            lv.r.assign ((size_t) (frames + 2 * kGuard), 0.f);
            std::copy (curR.begin(), curR.begin() + frames, lv.r.begin() + kGuard);
        }
        guard (lv, loop, from);
        c.levels.push_back (std::move (lv));
        if (frames < 256)
            break;
        curL = lowpass (curL, frames, loop, from, 2);
        if (c.stereo)
            curR = lowpass (curR, frames, loop, from, 2);
        frames /= 2;
        from /= 2;
    }
    return c;
}

// ---- the bank ------------------------------------------------------------------

namespace
{
    using cd = std::complex<double>;
    using Buf = std::vector<double>;
    constexpr double kRate = 48000.0;
    constexpr double kTop = 20000.0;   // every sample stops here

    // One sample's synthesis: a loop of n frames, everything in it periodic
    // over n (partials on whole cycles, filters run around twice, events
    // wrapped), so it plays seamlessly.
    struct Loop
    {
        int n;
        Rng rng;

        Loop (int length, uint64_t seed) : n (length), rng (seed) {}

        double uni() noexcept { return (double) rng.uniform(); }
        double range (double lo, double hi) noexcept { return lo + (hi - lo) * uni(); }
        double logRange (double lo, double hi) noexcept { return lo * std::pow (hi / lo, uni()); }
        double gauss() noexcept
        {
            const double u1 = std::max (1e-12, uni()), u2 = uni();
            return std::sqrt (-2.0 * std::log (u1)) * std::cos (2.0 * kPiD * u2);
        }
        double seconds() const noexcept { return n / kRate; }

        // The nearest frequency with whole cycles in the loop.
        int cycles (double hz) const noexcept { return std::max (1, (int) std::lround (hz * n / kRate)); }

        // Noise with the magnitude `mag` (hz) and random phases, 20 Hz-20 kHz, rms 1.
        Buf colored (const std::function<double (double)>& mag)
        {
            std::vector<cd> x ((size_t) n, cd (0.0));
            for (int k = 1; k < n / 2; ++k)
            {
                const double hz = k * kRate / n;
                const double g1 = gauss(), g2 = gauss();   // drawn for every bin, so the seed holds
                if (hz < 20.0 || hz > kTop) continue;
                x[(size_t) k] = cd (g1, g2) * mag (hz);
                x[(size_t) (n - k)] = std::conj (x[(size_t) k]);
            }
            fft (x, true);
            Buf y ((size_t) n);
            for (int i = 0; i < n; ++i) y[(size_t) i] = x[(size_t) i].real();
            return normalised (std::move (y));
        }

        // A slow random curve in [0, 1] that loops: `terms` sinusoids of
        // 1..maxCycles cycles over the loop, the slower ones stronger.
        Buf drift (int maxCycles, int terms = 6)
        {
            Buf y ((size_t) n, 0.0);
            for (int t = 0; t < terms; ++t)
            {
                const int c = 1 + (int) (uni() * maxCycles) % maxCycles;
                const double a = 1.0 / std::sqrt ((double) c), ph = uni() * 2.0 * kPiD;
                const double w = 2.0 * kPiD * c / n;
                for (int i = 0; i < n; ++i) y[(size_t) i] += a * std::sin (w * i + ph);
            }
            const auto [lo, hi] = std::minmax_element (y.begin(), y.end());
            const double a = *lo, s = std::max (1e-12, *hi - *lo);
            for (auto& v : y) v = (v - a) / s;
            return y;
        }

        // A sine of whole cycles (amplitude curve optional), added.
        void partial (Buf& y, double hz, double amp, double phase, const Buf* env = nullptr) const
        {
            const int c = cycles (hz);
            if (c * kRate / n > kTop) return;
            const cd step = std::polar (1.0, 2.0 * kPiD * c / n);
            cd z = std::polar (amp, phase);
            for (int i = 0; i < n; ++i)
            {
                if ((i & 4095) == 0) z = std::polar (amp, phase + 2.0 * kPiD * c * (double) i / n);   // no drift
                y[(size_t) i] += z.imag() * (env ? (*env)[(size_t) i] : 1.0);
                z *= step;
            }
        }

        // A TPT state-variable filter with its cutoff per sample, run around
        // the loop twice so it starts where it ends. mode 0 LP, 1 BP (unity
        // peak), 2 HP.
        Buf svf (const Buf& x, const std::function<double (int)>& hz, double q, int mode) const
        {
            Buf y ((size_t) n);
            double s1 = 0.0, s2 = 0.0;
            const double k = 1.0 / q;
            for (int pass = 0; pass < 2; ++pass)
                for (int i = 0; i < n; ++i)
                {
                    const double g = std::tan (kPiD * std::min (hz (i), 0.45 * kRate) / kRate);
                    const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
                    const double v3 = x[(size_t) i] - s2;
                    const double v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
                    s1 = 2.0 * v1 - s1;
                    s2 = 2.0 * v2 - s2;
                    if (pass == 1)
                        y[(size_t) i] = mode == 0 ? v2 : mode == 1 ? k * v1 : x[(size_t) i] - k * v1 - v2;
                }
            return y;
        }

        // An event at a random place, wrapped around the end: `render (t)`
        // for t in seconds from its start, for `len` seconds, added at `pan`
        // (equal power, 0 left .. 1 right).
        void event (Buf& l, Buf& r, double pan, double len, const std::function<double (double)>& render)
        {
            const int at = (int) (uni() * n), count = std::min (n, (int) (len * kRate));
            const double gl = std::sqrt (1.0 - pan), gr = std::sqrt (pan);
            for (int i = 0; i < count; ++i)
            {
                const double v = render (i / kRate);
                const auto j = (size_t) ((at + i) % n);
                l[j] += v * gl;
                r[j] += v * gr;
            }
        }

        // A room: the loop convolved around itself with a decaying noise tail
        // (`seconds` to -60 dB, darker as it dies), mixed in.
        void room (Buf& y, double seconds, double mix)
        {
            std::vector<cd> h ((size_t) n, cd (0.0)), x ((size_t) n);
            const double tau = seconds / 6.91;
            double lp = 0.0, e = 0.0;
            const int len = std::min (n, (int) (seconds * kRate));
            for (int i = 0; i < len; ++i)
            {
                const double t = i / kRate, a = std::exp (-t / tau);
                const double c = std::exp (-2.0 * kPiD * (12000.0 * a + 1500.0) / kRate);
                lp = gauss() * (1.0 - c) + lp * c;   // brightness falls with the tail
                h[(size_t) i] = lp * a;
                e += std::norm (h[(size_t) i]);
            }
            for (auto& v : h) v /= std::sqrt (std::max (e, 1e-20));
            for (int i = 0; i < n; ++i) x[(size_t) i] = y[(size_t) i];
            fft (h, false);
            fft (x, false);
            for (int k = 0; k < n; ++k) x[(size_t) k] *= h[(size_t) k];
            fft (x, true);
            const double rmsDry = rms (y);
            Buf wet ((size_t) n);
            for (int i = 0; i < n; ++i) wet[(size_t) i] = x[(size_t) i].real();
            const double g = rmsDry / std::max (rms (wet), 1e-12);
            for (int i = 0; i < n; ++i) y[(size_t) i] = y[(size_t) i] * (1.0 - mix) + wet[(size_t) i] * g * mix;
        }

        // Band-limits to 20 kHz around the loop.
        void limit (Buf& y) const
        {
            std::vector<cd> x ((size_t) n);
            for (int i = 0; i < n; ++i) x[(size_t) i] = y[(size_t) i];
            fft (x, false);
            for (int k = 0; k < n; ++k)
            {
                const double hz = std::min (k, n - k) * kRate / n;
                if (hz > kTop || k == 0) x[(size_t) k] = 0.0;
            }
            fft (x, true);
            for (int i = 0; i < n; ++i) y[(size_t) i] = x[(size_t) i].real();
        }

        static double rms (const Buf& y)
        {
            double a = 0.0;
            for (double v : y) a += v * v;
            return std::sqrt (a / (double) std::max<size_t> (1, y.size()));
        }
        static Buf normalised (Buf y)
        {
            const double r = rms (y);
            if (r > 0.0) for (auto& v : y) v /= r;
            return y;
        }
    };

    // ---- the shapes the samples share ----------------------------------------------

    double peakDb (double hz, double centre, double q, double db)   // a bell in dB
    {
        const double x = std::log2 (hz / centre) * q;
        return db * std::exp (-x * x);
    }
    double fromDb (double db) { return std::pow (10.0, db / 20.0); }
    double lowpass (double hz, double corner, int poles) { return 1.0 / std::sqrt (1.0 + std::pow (hz / corner, 2.0 * poles)); }
    double highpass (double hz, double corner, int poles) { return 1.0 / std::sqrt (1.0 + std::pow (corner / hz, 2.0 * poles)); }

    // Vowel formants (Hz) a, e, i, o, u.
    constexpr double kVowel[5][3] = { { 800, 1150, 2900 }, { 400, 2000, 2550 }, { 300, 2250, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
    double formant (double hz, double vowel)   // vowel 0..4, between the five
    {
        const int a = std::clamp ((int) vowel, 0, 3);
        const double f = std::clamp (vowel - a, 0.0, 1.0);
        double g = 0.0;
        for (int k = 0; k < 3; ++k)
        {
            const double c = kVowel[a][k] + (kVowel[a + 1][k] - kVowel[a][k]) * f;
            g += fromDb (peakDb (hz, c, 5.0 - k, 24.0 - 6.0 * k));
        }
        return g * lowpass (hz, 4000.0, 2);
    }

    struct Raw { Buf l, r; int n; };

    // ---- the twenty samples ------------------------------------------------------------

    Raw analogPink (Loop& s)
    {
        auto x = s.colored ([] (double f) { return fromDb (peakDb (f, 3500.0, 1.0, 3.0)) * lowpass (f, 12000.0, 2) / std::sqrt (f); });
        return { x, {}, s.n };
    }

    Raw analogWhite (Loop& s)
    {
        auto x = s.colored ([] (double f) { return highpass (f, 150.0, 1) * lowpass (f, 15000.0, 1); });
        return { x, {}, s.n };
    }

    // A high-passed noise through a stereo chorus: two moving taps per side.
    Raw junoChorus (Loop& s)
    {
        const auto x = s.colored ([] (double f) { return highpass (f, 300.0, 2) * lowpass (f, 11000.0, 1); });
        Buf l ((size_t) s.n), r ((size_t) s.n);
        const int c = s.cycles (0.55);
        const auto tap = [&] (double i, double phase) {
            const double d = (4.0 + 2.0 * std::sin (2.0 * kPiD * c * i / s.n + phase)) * 0.001 * kRate;
            const double p = i - d + s.n;
            const int a = (int) p;
            const double f = p - a;
            return x[(size_t) (a % s.n)] * (1.0 - f) + x[(size_t) ((a + 1) % s.n)] * f;
        };
        for (int i = 0; i < s.n; ++i)
        {
            l[(size_t) i] = x[(size_t) i] + 0.8 * tap (i, 0.0);
            r[(size_t) i] = x[(size_t) i] + 0.8 * tap (i, kPiD);
        }
        return { l, r, s.n };
    }

    // An 8-bit LFSR read every 8 samples (the SID's noise at 6 kHz): grainy and digital.
    Raw bitNoise (Loop& s)
    {
        Buf y ((size_t) s.n);
        uint32_t lfsr = 0x7ffff8u;
        double held = 0.0;
        for (int i = 0; i < s.n; ++i)
        {
            if ((i & 7) == 0)
            {
                const uint32_t bit = ((lfsr >> 22) ^ (lfsr >> 17)) & 1u;
                lfsr = ((lfsr << 1) | bit) & 0x7fffffu;
                held = (double) ((lfsr >> 15) & 0xffu) / 127.5 - 1.0;
            }
            y[(size_t) i] = held;
        }
        s.limit (y);
        return { y, {}, s.n };
    }

    // Two breaths in the loop: darker in, brighter out, a formant shape.
    Raw breath (Loop& s)
    {
        const auto shape = [] (double f, double tilt) {
            return (fromDb (peakDb (f, 650.0, 1.5, 6.0)) + fromDb (peakDb (f, 1500.0, 1.2, 4.0)) + fromDb (peakDb (f, 2700.0, 1.5, 6.0)))
                   * highpass (f, 250.0, 2) * lowpass (f, tilt, 2);
        };
        const auto dark = s.colored ([&] (double f) { return shape (f, 2500.0); });
        const auto bright = s.colored ([&] (double f) { return shape (f, 7000.0); });
        Buf y ((size_t) s.n);
        for (int i = 0; i < s.n; ++i)
        {
            const double ph = std::fmod (2.0 * i / s.n, 1.0);
            const double e = std::pow (std::sin (kPiD * ph), 2.0);
            y[(size_t) i] = (0.15 + 0.85 * e) * (dark[(size_t) i] * (1.0 - e) + bright[(size_t) i] * e);
        }
        return { y, {}, s.n };
    }

    Raw shush (Loop& s)
    {
        auto x = s.colored ([] (double f) { return fromDb (peakDb (f, 5200.0, 1.2, 14.0)) * highpass (f, 2200.0, 3) * lowpass (f, 11000.0, 2); });
        const auto e = s.drift (2, 3);
        for (int i = 0; i < s.n; ++i) x[(size_t) i] *= 0.7 + 0.3 * e[(size_t) i];
        return { x, {}, s.n };
    }

    Raw airCan (Loop& s)
    {
        auto x = s.colored ([] (double f) { return fromDb (peakDb (f, 8200.0, 1.0, 10.0)) * highpass (f, 3800.0, 2); });
        const auto e = s.drift (40, 8);
        for (int i = 0; i < s.n; ++i) x[(size_t) i] *= 0.85 + 0.15 * e[(size_t) i];
        return { x, {}, s.n };
    }

    // A hum on C2: odd harmonics strong, a buzz gated by the cycle, a slow wobble.
    Raw mainsHum (Loop& s)
    {
        const double f0 = NoiseBank::kRootHz * 0.5;
        Buf y ((size_t) s.n, 0.0);
        for (int h = 1; f0 * h < 3200.0; ++h)
            s.partial (y, f0 * h, (h % 2 ? 1.0 : 0.45) / std::pow (h, 0.75), s.uni() * 2.0 * kPiD);
        const auto buzz = s.colored ([] (double f) { return highpass (f, 1500.0, 2) * lowpass (f, 7000.0, 2); });
        const int c = s.cycles (f0 * 2.0);
        const auto wob = s.drift (3, 3);
        for (int i = 0; i < s.n; ++i)
        {
            const double gate = std::pow (std::abs (std::sin (kPiD * c * (double) i / s.n)), 8.0);
            y[(size_t) i] = (y[(size_t) i] * 0.35 + 0.08 * gate * buzz[(size_t) i]) * (0.85 + 0.15 * wob[(size_t) i]);
        }
        return { y, {}, s.n };
    }

    // Free-bar chimes on C pentatonic, struck now and then, in a small room.
    Raw windChimes (Loop& s)
    {
        static constexpr double kNotes[] = { 0, 2, 4, 7, 9, 12, 14, 16, 19 };
        static constexpr double kModes[4][3] = { { 1.0, 1.0, 1.4 }, { 2.756, 0.5, 0.7 }, { 5.404, 0.25, 0.35 }, { 8.933, 0.12, 0.18 } };
        Buf l ((size_t) s.n, 0.0), r ((size_t) s.n, 0.0);
        const int strikes = 16;
        for (int k = 0; k < strikes; ++k)
        {
            const double f = NoiseBank::kRootHz * 8.0 * std::pow (2.0, kNotes[(int) (s.uni() * 9) % 9] / 12.0);
            const double amp = s.range (0.3, 1.0), pan = s.range (0.1, 0.9);
            double ph[4];
            for (auto& p : ph) p = s.uni() * 2.0 * kPiD;
            s.event (l, r, pan, 3.0, [&] (double t) {
                double v = 0.0;
                for (int m = 0; m < 4; ++m)
                    if (f * kModes[m][0] < kTop)
                        v += kModes[m][1] * std::exp (-t / kModes[m][2]) * std::sin (2.0 * kPiD * f * kModes[m][0] * t + ph[m]);
                return v * amp * std::min (1.0, t * kRate / 24.0);
            });
        }
        s.room (l, 2.2, 0.3);
        s.room (r, 2.2, 0.3);
        return { l, r, s.n };
    }

    // Partials off any series, each side its own, breathing slowly.
    Raw inharmonic (Loop& s)
    {
        Buf l ((size_t) s.n, 0.0), r ((size_t) s.n, 0.0);
        for (int k = 0; k < 11; ++k)
        {
            const double f = s.logRange (180.0, 4200.0), a = 1.0 / std::sqrt (1.0 + k);
            const auto el = s.drift (3, 3), er = s.drift (3, 3);
            s.partial (l, f, a, s.uni() * 2.0 * kPiD, &el);
            s.partial (r, f * s.range (0.999, 1.001), a, s.uni() * 2.0 * kPiD, &er);
        }
        return { l, r, s.n };
    }

    // A struck bar's partials on C2, each doubled a fraction of a hertz apart (it beats).
    Raw metalDrone (Loop& s)
    {
        static constexpr double kBar[] = { 1.0, 2.756, 5.404, 8.933, 13.34, 18.64 };
        const double f0 = NoiseBank::kRootHz * 0.5;
        Buf l ((size_t) s.n, 0.0), r ((size_t) s.n, 0.0);
        for (int k = 0; k < 6; ++k)
        {
            const double a = 1.0 / (1.0 + 0.6 * k);
            for (auto* ch : { &l, &r })
            {
                const double beat = s.range (0.3, 1.4);
                s.partial (*ch, f0 * kBar[k], a, s.uni() * 2.0 * kPiD);
                s.partial (*ch, f0 * kBar[k] + beat, a * 0.8, s.uni() * 2.0 * kPiD);
            }
        }
        const auto grit = s.colored ([] (double f) { return lowpass (f, 2000.0, 2) * highpass (f, 80.0, 1); });
        const auto grit2 = s.colored ([] (double f) { return lowpass (f, 2000.0, 2) * highpass (f, 80.0, 1); });
        const double g = 0.1 * Loop::rms (l);
        for (int i = 0; i < s.n; ++i)
        {
            l[(size_t) i] += g * grit[(size_t) i];
            r[(size_t) i] += g * grit2[(size_t) i];
        }
        return { l, r, s.n };
    }

    // High partials that twinkle (fast, peaky amplitudes), a faint high air.
    Raw glass (Loop& s)
    {
        Buf l ((size_t) s.n, 0.0), r ((size_t) s.n, 0.0);
        for (int k = 0; k < 16; ++k)
        {
            const double f = s.logRange (1500.0, 9000.0);
            for (auto* ch : { &l, &r })
            {
                auto e = s.drift (60, 4);
                for (auto& v : e) v = v * v * v;
                s.partial (*ch, f, 1.0, s.uni() * 2.0 * kPiD, &e);
            }
        }
        const auto air = s.colored ([] (double f) { return highpass (f, 6000.0, 2) * lowpass (f, 13000.0, 2); });
        const double g = 0.15 * Loop::rms (l);
        for (int i = 0; i < s.n; ++i)
        {
            l[(size_t) i] += g * air[(size_t) i];
            r[(size_t) i] += g * air[(size_t) (s.n - 1 - i)];
        }
        return { l, r, s.n };
    }

    // Pink noise through a band that wanders low (shared, a little apart per
    // side), its octave above, gusts and a low rumble.
    Raw darkWind (Loop& s)
    {
        const auto centre = s.drift (5, 6), gust = s.drift (8, 6), rumbleIn = s.colored ([] (double f) { return lowpass (f, 120.0, 2); });
        Raw out { {}, {}, s.n };
        for (int side = 0; side < 2; ++side)
        {
            const auto x = s.colored ([] (double f) { return 1.0 / std::sqrt (f); });
            const double offset = side == 0 ? 0.94 : 1.06;
            const auto hz = [&] (int i) { return 150.0 * std::pow (6.0, centre[(size_t) i]) * offset; };
            const auto a = s.svf (x, hz, 2.0, 1);
            const auto b = s.svf (x, [&] (int i) { return 2.1 * hz (i); }, 3.0, 1);
            Buf y ((size_t) s.n);
            for (int i = 0; i < s.n; ++i)
                y[(size_t) i] = (a[(size_t) i] + 0.4 * b[(size_t) i]) * (0.3 + 0.7 * gust[(size_t) i]) + 0.25 * rumbleIn[(size_t) i];
            (side == 0 ? out.l : out.r) = std::move (y);
        }
        return out;
    }

    // Whistles: white noise through three narrow bands that glide on their
    // own, over a dark bed (wind through a gap).
    Raw howl (Loop& s)
    {
        Raw out { Buf ((size_t) s.n, 0.0), Buf ((size_t) s.n, 0.0), s.n };
        const auto bed = s.colored ([] (double f) { return lowpass (f, 500.0, 2) / std::sqrt (f); });
        for (int k = 0; k < 3; ++k)
        {
            const auto glide = s.drift (4, 4), level = s.drift (6, 4);
            const double lo = s.logRange (300.0, 700.0);
            for (auto* ch : { &out.l, &out.r })
            {
                const auto x = s.colored ([] (double) { return 1.0; });
                const auto y = s.svf (x, [&] (int i) { return lo * std::pow (2.5, glide[(size_t) i]); }, 28.0, 1);
                for (int i = 0; i < s.n; ++i) (*ch)[(size_t) i] += y[(size_t) i] * (0.2 + level[(size_t) i]);
            }
        }
        const double g = 0.35 * Loop::rms (out.l);
        for (int i = 0; i < s.n; ++i)
        {
            out.l[(size_t) i] += g * bed[(size_t) i];
            out.r[(size_t) i] += g * bed[(size_t) ((i + s.n / 3) % s.n)];
        }
        return out;
    }

    // C2's harmonics with each one's level wandering (the spectrum evolves),
    // two copies a fraction of a hertz apart per side (it chorus-beats).
    Raw polarDrone (Loop& s)
    {
        const double f0 = NoiseBank::kRootHz * 0.5;
        Raw out { Buf ((size_t) s.n, 0.0), Buf ((size_t) s.n, 0.0), s.n };
        for (int h = 1; f0 * h < 3500.0; ++h)
        {
            auto e = s.drift (4, 4);
            for (auto& v : e) v = v * v;
            const double a = 1.0 / std::pow ((double) h, 0.9);
            for (auto* ch : { &out.l, &out.r })
                for (int c = 0; c < 2; ++c)
                    s.partial (*ch, f0 * h + (c - 0.5) * s.range (0.3, 0.8) * std::sqrt ((double) h), a, s.uni() * 2.0 * kPiD, &e);
        }
        const auto air = s.colored ([] (double f) { return std::exp (-std::pow (std::log2 (f / 2000.0), 2.0)); });
        const double g = 0.06 * Loop::rms (out.l);
        for (int i = 0; i < s.n; ++i)
        {
            out.l[(size_t) i] += g * air[(size_t) i];
            out.r[(size_t) i] += g * air[(size_t) ((i + s.n / 2) % s.n)];
        }
        return out;
    }

    // Bubbles (a sine that rises as it dies, 300-1500 Hz) over a low stream.
    Raw water (Loop& s)
    {
        Raw out { Buf ((size_t) s.n, 0.0), Buf ((size_t) s.n, 0.0), s.n };
        for (int k = 0; k < 220; ++k)
        {
            const double f = s.logRange (300.0, 1500.0), tau = s.range (0.012, 0.04), amp = s.logRange (0.1, 1.0), pan = s.uni();
            const double rise = s.range (0.15, 0.6);
            double phase = 0.0;
            s.event (out.l, out.r, pan, tau * 6.0, [&] (double t) {
                phase += 2.0 * kPiD * f * (1.0 + rise * t / (tau * 6.0)) / kRate;
                return amp * std::exp (-t / tau) * std::sin (phase) * std::min (1.0, t * kRate / 16.0);
            });
        }
        const auto stream = s.colored ([] (double f) { return lowpass (f, 400.0, 2) * highpass (f, 60.0, 1) / std::sqrt (f); });
        const double g = 0.5 * Loop::rms (out.l);
        for (int i = 0; i < s.n; ++i)
        {
            out.l[(size_t) i] += g * stream[(size_t) i];
            out.r[(size_t) i] += g * stream[(size_t) ((i + s.n / 2) % s.n)];
        }
        s.room (out.l, 1.2, 0.25);
        s.room (out.r, 1.2, 0.25);
        return out;
    }

    // Crackles (clicks ringing at 2-6 kHz, in bursts) over a buzz on C2 that
    // flares with them.
    Raw electric (Loop& s)
    {
        Raw out { Buf ((size_t) s.n, 0.0), Buf ((size_t) s.n, 0.0), s.n };
        Buf activity ((size_t) s.n, 0.0);
        for (int burst = 0; burst < 9; ++burst)
        {
            const double centre = s.uni() * s.n, width = s.range (0.05, 0.3) * kRate;
            const int clicks = 20 + (int) (s.uni() * 60);
            for (int c = 0; c < clicks; ++c)
            {
                const double f = s.logRange (2000.0, 6500.0), amp = s.logRange (0.05, 0.5), pan = s.uni();
                const int at = (int) (centre + s.gauss() * width + 4.0 * s.n) % s.n;
                for (int i = 0; i < (int) (0.012 * kRate); ++i)
                {
                    const double t = i / kRate, v = amp * std::exp (-t / 0.0022) * std::sin (2.0 * kPiD * f * t);
                    const size_t j = (size_t) ((at + i) % s.n);
                    out.l[j] += v * std::sqrt (1.0 - pan);
                    out.r[j] += v * std::sqrt (pan);
                }
            }
            for (int i = 0; i < s.n; ++i)
            {
                const double d = std::min (std::abs (i - centre), s.n - std::abs (i - centre)) / (2.0 * width);
                activity[(size_t) i] += std::exp (-d * d);
            }
        }
        const double f0 = NoiseBank::kRootHz * 0.5;
        Buf buzz ((size_t) s.n, 0.0);
        for (int h = 1; f0 * h < 6000.0; ++h) s.partial (buzz, f0 * h, 1.0 / h, 0.0);
        const double g = 2.0 * Loop::rms (out.l) / std::max (Loop::rms (buzz), 1e-12);
        for (int i = 0; i < s.n; ++i)
        {
            const double a = 0.15 + std::min (1.0, activity[(size_t) i]);
            out.l[(size_t) i] += g * a * buzz[(size_t) i];
            out.r[(size_t) i] += g * a * buzz[(size_t) i];
        }
        return out;
    }

    // Voices on C3 singing one vowel into the next (a, o, u, e), with a little
    // vibrato and breath, spread across the stereo field.
    Raw mantra (Loop& s)
    {
        const double f0 = NoiseBank::kRootHz;
        Raw out { Buf ((size_t) s.n, 0.0), Buf ((size_t) s.n, 0.0), s.n };
        const auto vowel = s.drift (3, 3);
        static constexpr double kPath[] = { 0.0, 3.0, 4.0, 1.0 };   // a, o, u, e
        const auto vowelAt = [&] (int i) {
            const double v = vowel[(size_t) i] * 2.999;
            const int a = (int) v;
            return kPath[a] + (kPath[a + 1] - kPath[a]) * (v - a);
        };
        constexpr int kBlock = 256;
        const int blocks = s.n / kBlock;
        std::vector<Buf> gains;   // per harmonic, per block
        const int harmonics = (int) (4500.0 / f0);
        for (int h = 1; h <= harmonics; ++h)
        {
            Buf g ((size_t) blocks + 1);
            for (int b = 0; b <= blocks; ++b) g[(size_t) b] = formant (f0 * h, vowelAt ((b * kBlock) % s.n)) / std::sqrt ((double) h);
            gains.push_back (std::move (g));
        }
        for (int voice = 0; voice < 4; ++voice)
        {
            const double f = (double) s.cycles (f0 * std::pow (2.0, s.range (-9.0, 9.0) / 1200.0)) * kRate / s.n;
            const int vc = s.cycles (s.range (4.6, 5.6));
            const double depth = f * 0.004 / (vc * kRate / s.n), vph = s.uni() * 2.0 * kPiD, pan = 0.15 + 0.7 * (voice / 3.0);
            const double w = 2.0 * kPiD * f / kRate;
            for (int i = 0; i < s.n; ++i)
            {
                const double phi = w * i + depth * std::sin (2.0 * kPiD * vc * (double) i / s.n + vph);
                const int b = i / kBlock;
                const double fr = (i % kBlock) / (double) kBlock;
                double v = 0.0;
                const cd step = std::polar (1.0, phi);
                cd z = step;
                for (int h = 1; h <= harmonics; ++h)
                {
                    const auto& g = gains[(size_t) h - 1];
                    v += z.imag() * (g[(size_t) b] + (g[(size_t) b + 1] - g[(size_t) b]) * fr);
                    z *= step;
                }
                out.l[(size_t) i] += v * std::sqrt (1.0 - pan);
                out.r[(size_t) i] += v * std::sqrt (pan);
            }
        }
        const auto air = s.colored ([] (double f) { return formant (f, 0.0) * highpass (f, 400.0, 2); });
        const double g = 0.12 * Loop::rms (out.l);
        for (int i = 0; i < s.n; ++i)
        {
            out.l[(size_t) i] += g * air[(size_t) i];
            out.r[(size_t) i] += g * air[(size_t) ((i + s.n / 2) % s.n)];
        }
        return out;
    }

    // Calls of a few chirps (a sine gliding an octave, some trilled) in a room, over faint wind.
    Raw birds (Loop& s)
    {
        Raw out { Buf ((size_t) s.n, 0.0), Buf ((size_t) s.n, 0.0), s.n };
        for (int call = 0; call < 26; ++call)
        {
            const double pan = s.uni(), base = s.logRange (2200.0, 5500.0), amp = s.logRange (0.3, 1.0);
            const int chirps = 1 + (int) (s.uni() * 6);
            const double len = s.range (0.04, 0.14), gap = s.range (0.03, 0.09), sweep = s.range (-1.0, 1.0);
            const double trill = s.uni() < 0.3 ? s.range (18.0, 32.0) : 0.0;
            double phase = 0.0;
            s.event (out.l, out.r, pan, chirps * (len + gap), [&] (double t) {
                const double u = std::fmod (t, len + gap);
                if (u >= len) return 0.0;
                const double x = u / len;
                const double f = base * std::pow (2.0, sweep * x) * (1.0 + (trill > 0.0 ? 0.08 * std::sin (2.0 * kPiD * trill * u) : 0.0));
                phase += 2.0 * kPiD * f / kRate;
                return amp * std::pow (std::sin (kPiD * x), 2.0) * std::sin (phase);
            });
        }
        s.room (out.l, 1.6, 0.3);
        s.room (out.r, 1.6, 0.3);
        const auto wind = s.colored ([] (double f) { return lowpass (f, 900.0, 2) / std::sqrt (f); });
        const double g = 0.08 * Loop::rms (out.l);
        for (int i = 0; i < s.n; ++i)
        {
            out.l[(size_t) i] += g * wind[(size_t) i];
            out.r[(size_t) i] += g * wind[(size_t) ((i + s.n / 2) % s.n)];
        }
        return out;
    }

    // Dense drops (short high noise ticks), a few plinks, a soft hiss bed, a small room.
    Raw rain (Loop& s)
    {
        Raw out { Buf ((size_t) s.n, 0.0), Buf ((size_t) s.n, 0.0), s.n };
        const auto ticks = s.colored ([] (double f) { return highpass (f, 2000.0, 2) * lowpass (f, 12000.0, 1); });
        for (int k = 0; k < 3000; ++k)
        {
            const int at = (int) (s.uni() * s.n);
            const double amp = s.range (0.2, 1.0), pan = s.uni(), tau = s.range (0.0006, 0.0018);
            for (int i = 0; i < (int) (tau * 6.0 * kRate); ++i)
            {
                const size_t j = (size_t) ((at + i) % s.n);
                const double v = amp * std::exp (-i / (tau * kRate)) * ticks[j];
                out.l[j] += v * std::sqrt (1.0 - pan);
                out.r[j] += v * std::sqrt (pan);
            }
        }
        for (int k = 0; k < 60; ++k)
        {
            const double f = s.logRange (1000.0, 3200.0), amp = s.logRange (0.2, 0.8), pan = s.uni();
            s.event (out.l, out.r, pan, 0.06, [&] (double t) { return amp * std::exp (-t / 0.01) * std::sin (2.0 * kPiD * f * t); });
        }
        const auto bed = s.colored ([] (double f) { return highpass (f, 1000.0, 1) * lowpass (f, 9000.0, 1); });
        const double g = 0.7 * Loop::rms (out.l);
        for (int i = 0; i < s.n; ++i)
        {
            out.l[(size_t) i] += g * bed[(size_t) i];
            out.r[(size_t) i] += g * bed[(size_t) ((i + s.n / 2) % s.n)];
        }
        s.room (out.l, 0.8, 0.2);
        s.room (out.r, 0.8, 0.2);
        return out;
    }

    struct Recipe { const char* name; int lengthLog2; Raw (*make) (Loop&); };
    constexpr Recipe kRecipes[NoiseBank::kCount] = {
        { "Analog Pink", 17, analogPink }, { "Analog White", 17, analogWhite }, { "Juno Chorus", 17, junoChorus },
        { "Bit Noise", 17, bitNoise },     { "Breath", 17, breath },             { "Shush", 17, shush },
        { "Air Can", 17, airCan },         { "Mains Hum", 17, mainsHum },        { "Wind Chimes", 18, windChimes },
        { "Inharmonic", 17, inharmonic },  { "Metal Drone", 17, metalDrone },    { "Glass", 17, glass },
        { "Dark Wind", 18, darkWind },     { "Howl", 18, howl },                 { "Polar Drone", 18, polarDrone },
        { "Water", 18, water },            { "Electric", 18, electric },         { "Mantra", 18, mantra },
        { "Birds", 18, birds },            { "Rain", 18, rain },
    };

    // Band-limited, at one loudness (rms -14 dBFS over both sides), peaks under -0.5 dBFS.
    NoiseClip finish (Raw raw, Loop& s)
    {
        s.limit (raw.l);
        if (! raw.r.empty()) s.limit (raw.r);
        double e = Loop::rms (raw.l) * Loop::rms (raw.l), peak = 0.0;
        if (! raw.r.empty()) e = 0.5 * (e + Loop::rms (raw.r) * Loop::rms (raw.r));
        for (double v : raw.l) peak = std::max (peak, std::abs (v));
        for (double v : raw.r) peak = std::max (peak, std::abs (v));
        const double g = std::min (0.2 / std::sqrt (std::max (e, 1e-20)), 0.944 / std::max (peak, 1e-12));
        std::vector<float> l (raw.l.size()), r (raw.r.size());
        for (size_t i = 0; i < l.size(); ++i) l[i] = (float) (raw.l[i] * g);
        for (size_t i = 0; i < r.size(); ++i) r[i] = (float) (raw.r[i] * g);
        return NoiseClip::make (std::move (l), std::move (r), (float) kRate, true, true);   // already under 20 kHz
    }
} // namespace

NoiseBank::NoiseBank()
{
    std::vector<std::future<NoiseClip>> jobs;
    for (int i = 0; i < kCount; ++i)
        jobs.push_back (std::async (std::launch::async, [i] {
            Loop s (1 << kRecipes[i].lengthLog2, 0x6E6F697365ULL + (uint64_t) i * 7919u);
            return finish (kRecipes[i].make (s), s);
        }));
    for (int i = 0; i < kCount; ++i)
        clips[(size_t) i] = jobs[(size_t) i].get();
}

const char* NoiseBank::name (int index) noexcept
{
    return kRecipes[std::clamp (index, 0, kCount - 1)].name;
}

const NoiseBank& NoiseBank::shared()
{
    static const NoiseBank bank;
    return bank;
}

} // namespace psy::dsp
