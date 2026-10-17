#include "TestUtil.h"
#include "dsp/PhosEffects.h"
#include "engine/Engine.h"
#include "engine/PhosVoice.h"
#include "generator/Generator.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace psy;

namespace
{
    constexpr double kFs = 48000.0;

    // A plain 303: no distortion or effects, unity volume.
    ParamState phosPatch()
    {
        ParamState p;
        p.set (P::EngineMode, (float) EngineMode::Phoscyon);
        p.set (P::MasterVol, 0.f);
        p.set (P::PhosDistOn, 0.f);
        p.set (P::PhosCutoff, 0.5f);
        p.set (P::PhosReso, 0.5f);
        p.set (P::PhosEnvMod, 0.3f);
        p.set (P::PhosDecay, 0.5f);
        return p;
    }

    struct Note { double on, off; int note; float vel; };

    // The loudest sample before the output's soft clip in the last render()
    // (the output itself is always finite and under 0 dBFS).
    float gLastPreClip = 0.f;

    std::vector<float> render (const ParamState& p, double seconds, const std::vector<Note>& notes, double bpm = 140.0)
    {
        Engine e;
        e.setParams (p);
        e.prepare (kFs, 256);
        const int total = (int) (seconds * kFs);
        std::vector<float> l ((size_t) total), r ((size_t) total);
        double ppq = 0.0;
        for (int pos = 0; pos < total; pos += 256)
        {
            const int n = std::min (256, total - pos);
            std::vector<MidiEvent> ev;
            for (const auto& x : notes)
            {
                const int a = (int) (x.on * kFs), b = (int) (x.off * kFs);
                if (b >= pos && b < pos + n) ev.push_back ({ b - pos, MidiEvent::NoteOff, x.note, 0.f });
                if (a >= pos && a < pos + n) ev.push_back ({ a - pos, MidiEvent::NoteOn, x.note, x.vel });
            }
            // by position; at one position a note ends before the next starts
            std::stable_sort (ev.begin(), ev.end(), [] (const MidiEvent& a, const MidiEvent& b) {
                return a.offset != b.offset ? a.offset < b.offset : a.type == MidiEvent::NoteOff && b.type != MidiEvent::NoteOff;
            });
            e.process (l.data() + pos, r.data() + pos, n, ev.data(), (int) ev.size(), Transport { bpm, ppq, true });
            ppq += n / kFs * bpm / 60.0;
        }
        REQUIRE (e.diagnostics().nonFinite == 0);
        gLastPreClip = e.diagnostics().preClipPeak;
        return l;
    }

    double rmsDb (const std::vector<float>& x, double t0, double t1)
    {
        double s = 0.0;
        const auto a = (size_t) (t0 * kFs), b = std::min (x.size(), (size_t) (t1 * kFs));
        for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
        return 10.0 * std::log10 (s / (double) std::max<size_t> (1, b - a) + 1e-20);
    }

    // Autocorrelation pitch over a window (Hz): the first lag that correlates
    // nearly as well as the best one (a near-sine matches at every period).
    double pitchHz (const std::vector<float>& x, double t0, double t1, double lo = 50.0, double hi = 2000.0)
    {
        const auto a = (size_t) (t0 * kFs), n = (size_t) ((t1 - t0) * kFs);
        const auto corr = [&] (int lag) {
            double s = 0.0, e1 = 0.0, e2 = 0.0;
            for (size_t i = 0; i + (size_t) lag < n; ++i)
            {
                s += (double) x[a + i] * x[a + i + (size_t) lag];
                e1 += (double) x[a + i] * x[a + i];
                e2 += (double) x[a + i + (size_t) lag] * x[a + i + (size_t) lag];
            }
            return s / std::sqrt (e1 * e2 + 1e-20);
        };
        const int lo0 = (int) (kFs / hi), hi0 = (int) (kFs / lo);
        std::vector<double> c ((size_t) hi0 + 2, 0.0);
        double best = 0.0;
        for (int lag = lo0; lag <= hi0 + 1; ++lag) best = std::max (best, c[(size_t) lag] = corr (lag));
        int pick = lo0;
        for (int lag = lo0 + 1; lag <= hi0; ++lag)
            if (c[(size_t) lag] >= 0.97 * best && c[(size_t) lag] >= c[(size_t) lag - 1] && c[(size_t) lag] >= c[(size_t) lag + 1]) { pick = lag; break; }
        // refine with a parabola through the neighbours
        const double c0 = c[(size_t) pick - 1], c1 = c[(size_t) pick], c2 = c[(size_t) pick + 1];
        const double off = 0.5 * (c0 - c2) / (c0 - 2.0 * c1 + c2);
        return kFs / ((double) pick + (std::isfinite (off) ? off : 0.0));
    }

    // Spectral centroid of a window (Hz).
    double centroid (const std::vector<float>& x, double t0, double t1)
    {
        const auto a = (size_t) (t0 * kFs);
        size_t n = 1;
        while (n * 2 <= (size_t) ((t1 - t0) * kFs)) n *= 2;
        std::vector<std::complex<double>> s (n);
        for (size_t i = 0; i < n; ++i) s[i] = x[a + i] * (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) n));
        testutil::fft (s);
        double num = 0.0, den = 0.0;
        for (size_t i = 1; i < n / 2; ++i)
        {
            const double f = (double) i * kFs / (double) n, pw = std::norm (s[i]);
            num += f * pw;
            den += pw;
        }
        return num / (den + 1e-20);
    }
} // namespace

TEST_CASE ("The 303 plays the key in tune, and Tuning moves it", "[phoscyon]")
{
    auto p = phosPatch();
    p.set (P::PhosCutoff, 0.f);
    p.set (P::PhosReso, 0.f);
    p.set (P::PhosEnvMod, 0.f);
    const auto a = render (p, 0.6, { { 0.0, 0.55, 69, 0.4f } });
    CHECK (std::abs (pitchHz (a, 0.2, 0.4) - 440.0) < 1.5);
    p.set (P::PhosTune, 12.f);
    const auto up = render (p, 0.6, { { 0.0, 0.55, 57, 0.4f } });
    CHECK (std::abs (pitchHz (up, 0.2, 0.4) - 440.0) < 1.5);
}

TEST_CASE ("Overlapping notes slide without a retrigger, at the Slide Time's speed", "[phoscyon]")
{
    auto p = phosPatch();
    p.set (P::PhosCutoff, 0.3f);
    p.set (P::PhosReso, 0.f);
    const std::vector<Note> legato { { 0.0, 0.45, 57, 0.4f }, { 0.3, 1.1, 69, 0.4f } };
    p.set (P::PhosSlide, 1.f);
    const auto slow = render (p, 1.2, legato);
    p.set (P::PhosSlide, 0.25f);
    const auto fast = render (p, 1.2, legato);
    // fast arrives within ~20 ms; the slow glide is still on its way 30 ms in
    CHECK (std::abs (pitchHz (fast, 0.33, 0.37) - 440.0) < 10.0);
    CHECK (pitchHz (slow, 0.31, 0.35) < 380.0);
    CHECK (std::abs (pitchHz (slow, 0.95, 1.05) - 440.0) < 3.0);   // a 100 ms glide: there by 0.6 s on
    // no new attack: the level doesn't jump where the second key comes in
    CHECK (rmsDb (fast, 0.3, 0.33) < rmsDb (fast, 0.25, 0.29) + 1.5);
}

TEST_CASE ("Accented notes (velocity 64 and up) hit louder and brighter", "[phoscyon]")
{
    auto p = phosPatch();
    const auto plain = render (p, 0.4, { { 0.0, 0.2, 45, 0.45f } });
    const auto accent = render (p, 0.4, { { 0.0, 0.2, 45, 0.55f } });
    CHECK (rmsDb (accent, 0.0, 0.06) > rmsDb (plain, 0.0, 0.06) + 3.0);
    CHECK (centroid (accent, 0.0, 0.05) > centroid (plain, 0.0, 0.05));
    // the Accent knob at 0 takes it out
    p.set (P::PhosAccent, 0.f);
    const auto off = render (p, 0.4, { { 0.0, 0.2, 45, 0.55f } });
    CHECK (std::abs (rmsDb (off, 0.0, 0.06) - rmsDb (plain, 0.0, 0.06)) < 1.0);
}

TEST_CASE ("Accented 16ths start without a click, however much accent is left over", "[phoscyon][click]")
{
    // A fast line of accents with a long accent decay: each note starts while
    // the last one's accent still rings. The onset may not be any rougher (in
    // the second difference, a high-band proxy) than the note's own body.
    auto p = phosPatch();
    p.set (P::PhosAccDecay, 1.f);
    p.set (P::PhosCutoff, 0.3f);
    std::vector<Note> line;
    const double step = 0.125;
    for (int i = 0; i < 12; ++i)
        line.push_back ({ i * step, i * step + 0.06, 45, (i % 3 == 2) ? 0.45f : 0.9f });   // accent, accent, plain
    const auto out = render (p, 12 * step + 0.2, line, 120.0);
    const auto rough = [&] (double t0, double t1) {
        double m = 0.0;
        for (int i = std::max (1, (int) (t0 * kFs)); i < std::min ((int) out.size() - 1, (int) (t1 * kFs)); ++i)
            m = std::max (m, (double) std::abs (out[(size_t) i + 1] - 2.f * out[(size_t) i] + out[(size_t) i - 1]));
        return m;
    };
    for (int i = 1; i < 12; ++i)
    {
        INFO ("note " << i);
        const double t = i * step;
        CHECK (rough (t - 0.002, t + 0.002) < 2.0 * rough (t + 0.005, t + 0.05));
    }
}

TEST_CASE ("The Phoscyon distortion doesn't fold a driven high line back", "[phoscyon][fx]")
{
    // a 2.6 kHz sine through the hard RAT clipper makes a square: at 48 kHz its
    // 11th harmonic would fold back only ~21 dB under the fundamental;
    // oversampled 4x, the folds stay more than 30 dB down
    dsp::PhosDistortion d;
    d.prepare ((float) kFs);
    const size_t n = 1 << 15;
    const double f0 = 2625.0;   // an exact bin, so harmonics land on bins
    std::vector<float> l (n), r (n);
    for (size_t i = 0; i < n; ++i)
        l[i] = r[i] = 0.5f * (float) std::sin (2.0 * std::numbers::pi * f0 * (double) i / kFs);
    d.process (l.data(), r.data(), (int) n, 18, 0.9f, 0.f, 0.f, false);
    const std::vector<float> tail (l.begin() + 4096, l.begin() + 4096 + 16384);
    const auto db = testutil::spectrumDb (tail, 16384);
    double harm = -300.0, other = -300.0;
    for (size_t b = 4; b < db.size() - 4; ++b)
    {
        const double hz = (double) b * kFs / 16384.0;
        const double k = hz / f0;
        if (std::abs (k - std::round (k)) * f0 < 30.0) harm = std::max (harm, db[b]);
        else other = std::max (other, db[b]);
    }
    INFO ("harmonics " << harm << " dB, aliases " << other << " dB");
    CHECK (other < harm - 30.0);
}

TEST_CASE ("Cutoff and Envmod open the filter, Decay holds it open longer", "[phoscyon]")
{
    auto p = phosPatch();
    p.set (P::PhosEnvMod, 0.f);
    p.set (P::PhosCutoff, 0.1f);
    const auto dark = render (p, 0.5, { { 0.0, 0.45, 45, 0.4f } });
    p.set (P::PhosCutoff, 0.8f);
    const auto bright = render (p, 0.5, { { 0.0, 0.45, 45, 0.4f } });
    CHECK (centroid (bright, 0.2, 0.4) > 1.5 * centroid (dark, 0.2, 0.4));

    p.set (P::PhosCutoff, 0.1f);
    p.set (P::PhosEnvMod, 0.9f);
    p.set (P::PhosDecay, 0.f);
    const auto shortDecay = render (p, 0.6, { { 0.0, 0.55, 45, 0.4f } });
    p.set (P::PhosDecay, 1.f);
    const auto longDecay = render (p, 0.6, { { 0.0, 0.55, 45, 0.4f } });
    CHECK (centroid (longDecay, 0.25, 0.4) > 1.5 * centroid (shortDecay, 0.25, 0.4));
}

TEST_CASE ("Phoscyon patches play the 303 with a 303 line", "[phoscyon][generator]")
{
    Generator g;
    for (uint32_t seed = 1; seed <= 12; ++seed)
    {
        INFO ("seed " << seed);
        auto p = g.generate (Category::Phoscyon, seed, ParamState(), LockSet(), true);
        CHECK (p.geti (P::EngineMode) == (int) EngineMode::Phoscyon);
        CHECK (p.on (P::ArpOn));
        CHECK (p.geti (P::ArpDiv) == 12);   // 1/16
        const auto out = render (p, 2.0, { { 0.0, 1.9, 45, 0.8f } });
        CHECK (rmsDb (out, 0.2, 1.8) > -60.0);
    }
}

TEST_CASE ("The arp's plain steps stay under the accent, its accents hit it", "[phoscyon][arp]")
{
    auto p = phosPatch();
    p.set (P::ArpOn, 1.f);
    p.set (P::ArpDiv, 12.f);
    p.set (P::ArpGate, 0.5f);
    ArpPattern::parse ("Xx").writeTo (p);
    const auto out = render (p, 1.0, { { 0.0, 0.95, 45, 1.f } }, 120.0);
    const double step = 0.125;   // a 16th at 120
    // accented, plain, accented, plain: the accented steps are louder
    CHECK (rmsDb (out, 2 * step, 2 * step + 0.04) > rmsDb (out, 3 * step, 3 * step + 0.04) + 3.0);
}

TEST_CASE ("Every distortion model plays and stays bounded", "[phoscyon][fx]")
{
    auto p = phosPatch();
    p.set (P::PhosDistOn, 1.f);
    p.set (P::PhosDistDrive, 0.8f);
    for (int m = 0; m < dsp::PhosDistortion::kNumModels; ++m)
    {
        INFO ("model " << m);
        p.set (P::PhosDistModel, (float) m);
        const auto out = render (p, 0.4, { { 0.0, 0.3, 45, 0.6f } });
        CHECK (rmsDb (out, 0.05, 0.25) > -50.0);
        float pk = 0.f;
        for (float v : out) pk = std::max (pk, std::abs (v));
        CHECK (pk < 1.f);
    }
}

TEST_CASE ("The Phoscyon chain puts its EQ and limiter in their slots", "[phoscyon][fx]")
{
    CHECK (phosFxChain (0, 1, 10) == PhosFxChain { 0, kPhosEq, 1, 2, 3, 4, 5, 6, 7, 8, kPhosLimiter });
    for (auto [code, eq, lim] : { std::tuple { 0, 1, 10 }, std::tuple { 1234, 0, 5 }, std::tuple { 362879, 10, 0 }, std::tuple { 40320, 4, 3 } })
    {
        const auto c = phosFxChain (code, eq, lim);
        int code2 = -1, eq2 = -1, lim2 = -1;
        phosFxSlots (c, code2, eq2, lim2);
        CHECK (code2 == code);
        CHECK (eq2 == eq);
        CHECK (lim2 == lim);
    }
}

TEST_CASE ("The Phoscyon EQ lifts its band, the limiter holds the peaks", "[phoscyon][fx]")
{
    auto p = phosPatch();
    p.set (P::PhosCutoff, 1.f);
    const auto flat = render (p, 0.5, { { 0.0, 0.45, 45, 0.4f } });
    p.set (P::PhosEqOn, 1.f);
    p.set (P::PhosEqMidHighGain, 18.f);
    p.set (P::PhosEqMidHighFreq, 2000.f);
    const auto lifted = render (p, 0.5, { { 0.0, 0.45, 45, 0.4f } });
    CHECK (centroid (lifted, 0.1, 0.4) > centroid (flat, 0.1, 0.4) * 1.2);

    p.set (P::PhosEqOn, 0.f);
    p.set (P::PhosLimOn, 1.f);
    p.set (P::PhosLimThresh, -30.f);
    p.set (P::PhosLimRatio, 40.f);
    const auto held = render (p, 0.5, { { 0.0, 0.45, 45, 0.6f } });
    CHECK (rmsDb (held, 0.1, 0.4) < rmsDb (flat, 0.1, 0.4) - 3.0);
}

TEST_CASE ("Switching away from the 303 silences it", "[phoscyon]")
{
    auto p = phosPatch();
    Engine e;
    e.setParams (p);
    e.prepare (kFs, 256);
    std::vector<float> l (256), r (256);
    MidiEvent on { 0, MidiEvent::NoteOn, 45, 0.5f };
    e.process (l.data(), r.data(), 256, &on, 1, 140.0);
    for (int i = 0; i < 20; ++i) e.process (l.data(), r.data(), 256, nullptr, 0, 140.0);
    CHECK (e.activeVoices() == 1);
    p.set (P::EngineMode, (float) EngineMode::Synth);
    e.setParams (p);
    for (int i = 0; i < 20; ++i) e.process (l.data(), r.data(), 256, nullptr, 0, 140.0);
    CHECK (e.activeVoices() == 0);
}

TEST_CASE ("Fuzzed Phoscyon patches stay finite", "[phoscyon][stress]")
{
    dsp::Rng rng (77);
    for (int run = 0; run < 40; ++run)
    {
        auto p = phosPatch();
        for (int i = (int) P::PhosWave; i <= (int) P::PhosLimSlot; ++i)
            p.set ((P) i, def ((P) i).toPlain (rng.uniform()));
        p.set (P::FxOrder, (float) rng.integer (0, 362879));
        const auto out = render (p, 0.5, { { 0.0, 0.2, 30 + run, 0.3f + 0.02f * (float) run }, { 0.1, 0.4, 50, 0.9f } });
        INFO ("run " << run);
        CHECK (gLastPreClip < 16.f);   // +24 dBFS
    }
}
