#pragma once

#include "Common.h"

#include <complex>

namespace hl::dsp
{
/** Minimal in-place iterative radix-2 complex FFT. */
class Fft
{
public:
    explicit Fft (int order)
        : size (1 << order), twiddles ((size_t) size / 2), bitReversed ((size_t) size)
    {
        for (int i = 0; i < size / 2; ++i)
            twiddles[(size_t) i] = std::polar (1.0f, (float) (-2.0 * kPi * i / size));

        for (int i = 0; i < size; ++i)
        {
            int r = 0;

            for (int b = 0; b < order; ++b)
                r |= ((i >> b) & 1) << (order - 1 - b);

            bitReversed[(size_t) i] = r;
        }
    }

    int getSize() const noexcept { return size; }

    void forward (std::complex<float>* data) const noexcept
    {
        for (int i = 0; i < size; ++i)
            if (i < bitReversed[(size_t) i])
                std::swap (data[i], data[bitReversed[(size_t) i]]);

        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len / 2;
            const int step = size / len;

            for (int start = 0; start < size; start += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    const auto t = twiddles[(size_t) (k * step)] * data[start + k + half];
                    data[start + k + half] = data[start + k] - t;
                    data[start + k] += t;
                }
            }
        }
    }

    /** Inverse transform, including the 1/N scaling. */
    void inverse (std::complex<float>* data) const noexcept
    {
        for (int i = 0; i < size; ++i)
            data[i] = std::conj (data[i]);

        forward (data);
        const float scale = 1.0f / (float) size;

        for (int i = 0; i < size; ++i)
            data[i] = std::conj (data[i]) * scale;
    }

private:
    int size;
    std::vector<std::complex<float>> twiddles;
    std::vector<int> bitReversed;
};

inline int fftOrderFor (int size) noexcept
{
    int order = 0;

    while ((1 << order) < size)
        ++order;

    return order;
}

} // namespace hl::dsp
