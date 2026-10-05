#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace psy::dsp
{

// ---------------------------------------------------------------------------
// The voices of one unison oscillator, laid out like Serum 2: where each one
// sits in pitch, how loud it is and where it sits in the stereo field. The
// engine and the editor's display share it, so the picture is the sound.
//
// Voice 0 is the centre; with an even count voices 0 and 1 are the centre
// pair. The others come in pairs, one above and one below, further out with
// each pair, so dropping the count drops the outermost voices first.
// ---------------------------------------------------------------------------
struct UnisonLayout
{
    static constexpr int kMax = 16;

    int count = 1;
    std::array<float, kMax> pos {};     // even spacing, -1..1 (the stereo and spread position)
    std::array<float, kMax> semis {};   // detune plus stack, in semitones
    std::array<float, kMax> gain {};    // blend, normalised so the level stays put
    std::array<float, kMax> pan {};     // -1..1, already scaled by the width
    std::array<bool, kMax> centre {};

    // mode: Linear, Super, Exp, Inv, Random. stack: Off, 12, 12+7, 24,
    // Centre -12, Centre -24 (see psy::UniMode / psy::UniStack).
    void compute (int n, float detune, float rangeSemis, int mode, int stack, float blend, float width) noexcept
    {
        count = std::clamp (n, 1, kMax);
        if (count == 1)
        {
            pos[0] = semis[0] = pan[0] = 0.f;
            gain[0] = 1.f;
            centre[0] = true;
            return;
        }

        const bool even = count % 2 == 0;
        const float half = detune * rangeSemis * 0.5f;
        float power = 0.f;
        for (int k = 0; k < count; ++k)
        {
            // pair 0 is the centre pair of an even count; an odd count's
            // centre is voice 0 on its own
            const int pair = even ? k / 2 : (k - 1) / 2;
            const float side = (even ? k % 2 == 0 : (k - 1) % 2 == 0) ? 1.f : -1.f;
            const bool isCentre = even ? k < 2 : k == 0;
            const float u = isCentre && ! even ? 0.f
                          : side * (even ? (float) (2 * pair + 1) : (float) (2 * (pair + 1))) / (float) (count - 1);

            pos[(size_t) k] = u;
            centre[(size_t) k] = isCentre;
            semis[(size_t) k] = std::copysign (curve (std::abs (u), mode, k), u) * half + stackSemis (stack, k, even, isCentre);
            // alternate the sides pair by pair, so both get high and low voices
            pan[(size_t) k] = width * u * ((pair % 2 == 0) ? 1.f : -1.f);
            const float g = isCentre ? 1.f : blend;
            gain[(size_t) k] = g;
            power += g * g;
        }
        const float norm = power > 0.f ? 1.f / std::sqrt (power) : 1.f;
        for (int k = 0; k < count; ++k)
            gain[(size_t) k] *= norm;
    }

    // Start phase of voice k when the oscillator retriggers: the centre at 0,
    // the others spread out so the voices don't all line up on the attack.
    static double retrigPhase (int k) noexcept
    {
        const double ph = (double) k * 0.6180339887498949;
        return k == 0 ? 0.0 : ph - std::floor (ph);
    }

private:
    static float curve (float a, int mode, int k) noexcept
    {
        switch (mode)
        {
            case 1: return std::pow (a, 1.5f);   // Super: the inner voices close, like the JP-8000
            case 2: return a * a * std::sqrt (a); // Exp: a tight cluster with far outliers
            case 3: return std::sqrt (a);         // Inv: pushed out towards the edges
            case 4:                               // Random: fixed per voice, the outermost pair at the edge
            {
                static constexpr float r[kMax] = { 0.11f, 0.14f, 0.37f, 0.81f, 0.18f, 0.66f, 0.92f, 0.29f,
                                                   0.55f, 0.08f, 0.74f, 0.44f, 0.97f, 0.23f, 0.61f, 0.86f };
                return a >= 0.999f ? 1.f : r[k];
            }
            default: return a;
        }
    }

    static float stackSemis (int stack, int k, bool even, bool isCentre) noexcept
    {
        if (isCentre)
            return stack == 4 ? -12.f : (stack == 5 ? -24.f : 0.f);
        // pairs outside the centre, counted from 0
        const int q = even ? k / 2 - 1 : (k - 1) / 2;
        switch (stack)
        {
            case 1: return q % 2 == 0 ? 12.f : 0.f;
            case 2: return q % 2 == 0 ? 12.f : 7.f;
            case 3: return q % 2 == 0 ? 12.f : 24.f;
            default: return 0.f;
        }
    }
};

} // namespace psy::dsp
