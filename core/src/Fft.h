#pragma once

#include <complex>
#include <vector>

namespace silt
{
// In-place iterative radix-2 FFT (size a power of two). Off the audio thread only.
inline void fft (std::vector<std::complex<float>>& x)
{
    const size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (x[i], x[j]);
    }
    for (size_t length = 2; length <= n; length <<= 1)
    {
        const float angle = -6.2831853f / (float) length;
        const std::complex<float> step (std::cos (angle), std::sin (angle));
        for (size_t i = 0; i < n; i += length)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (size_t k = 0; k < length / 2; ++k)
            {
                const auto a = x[i + k], b = x[i + k + length / 2] * w;
                x[i + k] = a + b;
                x[i + k + length / 2] = a - b;
                w *= step;
            }
        }
    }
}
} // namespace silt
