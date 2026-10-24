#pragma once

#include "Common.h"

#include <complex>
#include <utility>
#include <vector>

namespace psy::dsp
{

// In-place radix-2 complex FFT (the size a power of two); the inverse
// divides by n. Used where tables are built (wavetables, the noise bank),
// never on the audio path.
inline void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    using cd = std::complex<double>;
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

} // namespace psy::dsp
