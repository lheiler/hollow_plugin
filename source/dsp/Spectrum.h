#pragma once

#include "Common.h"

#include <complex>

/** A coarse (1/3-octave) power spectrum, used to estimate how loud a patch will be without listening. */
namespace hl::dsp::loudness
{
constexpr int kBands = 31; // 20 Hz .. 20 kHz
using Spectrum = std::array<double, kBands>;

/** Median music spectrum from Supervisor's corpus analysis (supervisor/source/dsp/TargetCurves.h,
    7,992 released tracks of the Free Music Archive set): power spectral density on 1/6-octave steps
    from 20 Hz, with a +4.5 dB/oct display tilt (pivot 1 kHz). */
constexpr float musicMedianDb[61] = {
    -28.06f, -27.30f, -23.11f, -20.90f, -19.02f, -18.27f, -12.19f, -11.34f, -7.11f, -6.17f,
    -3.94f, -2.74f, -1.90f, -1.40f, -0.97f, -0.69f, -0.58f, -0.25f, -0.04f, 0.56f,
    0.88f, 0.91f, 0.59f, 0.85f, 1.13f, 1.46f, 1.58f, 1.49f, 1.47f, 1.64f,
    1.90f, 1.86f, 1.40f, 0.91f, 0.67f, 0.58f, 0.42f, 0.34f, 0.22f, -0.11f,
    -0.46f, -0.65f, -0.79f, -1.17f, -1.53f, -1.84f, -2.43f, -3.09f, -3.62f, -4.12f,
    -4.54f, -5.08f, -5.72f, -6.49f, -7.49f, -8.95f, -10.63f, -12.69f, -15.97f, -21.75f,
    -28.70f };

/** Band centres, loudness weights (BS.1770 K-weighting) and the typical-music energy per band. */
struct Grid
{
    static constexpr int subPoints = 6; // responses are averaged across each band so combs don't alias
    Spectrum freq {}, kWeight {}, music {};

    static const Grid& get()
    {
        static const Grid grid;
        return grid;
    }

    static double subFrequency (double centre, int j) noexcept
    {
        return centre * std::pow (2.0, ((j + 0.5) / subPoints - 0.5) / 3.0);
    }

private:
    Grid()
    {
        const auto mag2 = [] (double b0, double b1, double b2, double a1, double a2, double w)
        {
            const auto z = std::polar (1.0, -w);
            return std::norm ((b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z));
        };

        double sum = 0.0;

        for (int i = 0; i < kBands; ++i)
        {
            const double f = 20.0 * std::pow (2.0, i / 3.0);
            freq[(size_t) i] = std::min (19500.0, f);
            const double w = 2.0 * kPi * freq[(size_t) i] / 48000.0;
            kWeight[(size_t) i] = mag2 (1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585, w)
                                * mag2 (1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621, w);

            // energy per 1/3 octave = density x bandwidth: undo the display tilt, add +3 dB/oct
            music[(size_t) i] = std::pow (10.0, (musicMedianDb[2 * i] - 1.5 * std::log2 (f / 1000.0)) / 10.0);
            sum += music[(size_t) i];
        }

        for (auto& m : music)
            m /= sum;
    }
};

inline double totalPower (const Spectrum& s) noexcept
{
    double p = 0.0;

    for (auto v : s)
        p += v;

    return p;
}

/** K-weighted power (what a loudness meter reads). */
inline double loudnessPower (const Spectrum& s) noexcept
{
    const auto& g = Grid::get();
    double p = 0.0;

    for (int i = 0; i < kBands; ++i)
        p += g.kWeight[(size_t) i] * s[(size_t) i];

    return p;
}

/** Typical music at a given loudness (dB, K-weighted power). */
inline Spectrum musicAt (double loudnessDb)
{
    const auto& g = Grid::get();
    Spectrum s = g.music;
    const double k = std::pow (10.0, loudnessDb / 10.0) / loudnessPower (s);

    for (auto& v : s)
        v *= k;

    return s;
}

/** Average power gain of a response (magnitude as a function of Hz) across band `i`. */
template <typename Magnitude>
double bandGain (Magnitude&& magnitude, int i)
{
    const double centre = Grid::get().freq[(size_t) i];
    double m2 = 0.0;

    for (int j = 0; j < Grid::subPoints; ++j)
    {
        const double m = magnitude (Grid::subFrequency (centre, j));
        m2 += m * m;
    }

    return m2 / Grid::subPoints;
}

/** An impulse's response per band: average |H|^2 and average Re(H) (how much of the wet is in phase
    with the dry, which decides whether mixing them adds up or cancels). */
struct ImpulseBands
{
    Spectrum power {}, real {};
};

template <typename Fft>
ImpulseBands impulseBands (const std::vector<float>& left, const std::vector<float>& right, double rate)
{
    const size_t len = std::min (left.size(), right.size());
    int order = 0;

    while ((1 << order) < (int) len)
        ++order;

    Fft fft (std::max (order, 10));
    const int n = fft.getSize();
    std::vector<std::complex<float>> data ((size_t) n);

    for (size_t i = 0; i < len && i < (size_t) n; ++i)
        data[i] = 0.5f * (left[i] + right[i]);

    fft.forward (data.data());
    ImpulseBands bands;

    for (int i = 0; i < kBands; ++i)
    {
        const double centre = Grid::get().freq[(size_t) i];
        const int lo = std::clamp ((int) std::ceil (centre * std::pow (2.0, -1.0 / 6.0) * n / rate), 1, n / 2 - 1);
        const int hi = std::clamp ((int) std::floor (centre * std::pow (2.0, 1.0 / 6.0) * n / rate), lo, n / 2 - 1);
        double power = 0.0, real = 0.0;

        for (int bin = lo; bin <= hi; ++bin)
        {
            power += std::norm (data[(size_t) bin]);
            real += data[(size_t) bin].real();
        }

        bands.power[(size_t) i] = power / (hi - lo + 1);
        bands.real[(size_t) i] = real / (hi - lo + 1);
    }

    return bands;
}

} // namespace hl::dsp::loudness
