#pragma once

#include "Svf.h"

namespace hl::dsp
{
/** Splits a stereo signal into up to four bands with 4th-order Linkwitz-Riley crossovers.

    Bands are phase-aligned with all-pass compensation, so summing the untouched bands gives a
    magnitude-flat (all-pass) response. Crossover frequencies glide smoothly when changed. */
class MultibandSplitter
{
public:
    static constexpr int maxBands = 4;
    static constexpr int maxCrossovers = maxBands - 1;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        smoothCoeff = onePoleCoeff (0.03, sampleRate / updateInterval);

        for (int i = 0; i < maxCrossovers; ++i)
            current[(size_t) i] = target[(size_t) i];

        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        for (auto& ch : channels)
        {
            for (auto& x : ch.xover)
                for (auto& s : x)
                    s.reset();

            for (auto& row : ch.allpass)
                for (auto& s : row)
                    s.reset();
        }

        counter = 0;
    }

    void setNumBands (int n) noexcept
    {
        n = std::clamp (n, 1, maxBands);

        if (n != numBands)
        {
            numBands = n;
            reset();
        }
    }

    int getNumBands() const noexcept { return numBands; }

    /** Sets target crossover frequencies (ascending); they're kept apart by at least 1/3 octave. */
    void setCrossovers (const float* freqs, int count) noexcept
    {
        const double maxF = std::min (20000.0, sampleRate * 0.45);
        double lowest = 20.0;

        for (int i = 0; i < std::min (count, maxCrossovers); ++i)
        {
            const double f = std::clamp ((double) freqs[i], lowest, maxF);
            target[(size_t) i] = std::log (f);
            lowest = f * minRatio;
        }
    }

    /** Frequencies currently in use (after smoothing). */
    double getCrossover (int index) const noexcept { return std::exp (current[(size_t) index]); }

    /** Splits one sample of channel `ch` into `numBands` outputs. */
    void split (int ch, float x, float* bands) noexcept
    {
        auto& c = channels[(size_t) ch];
        const int numXovers = numBands - 1;
        float rest = x;

        for (int i = 0; i < numXovers; ++i)
        {
            float lp1, bp1, hp1, lp, hp, unused1, unused2;
            c.xover[(size_t) i][0].processMulti (xoverCoeffs[(size_t) i], rest, lp1, bp1, hp1);
            c.xover[(size_t) i][1].processMulti (xoverCoeffs[(size_t) i], lp1, lp, unused1, unused2);
            c.xover[(size_t) i][2].processMulti (xoverCoeffs[(size_t) i], hp1, unused1, unused2, hp);
            bands[i] = lp;
            rest = hp;
        }

        bands[numXovers] = rest;

        // Band i must also pass through the all-passes of every later crossover
        for (int b = 0; b < numXovers - 1; ++b)
            for (int j = b + 1; j < numXovers; ++j)
                bands[b] = c.allpass[(size_t) b][(size_t) j].process (allpassCoeffs[(size_t) j], bands[b]);
    }

    /** Call once per sample before splitting both channels. */
    void tick() noexcept
    {
        if (++counter >= updateInterval)
        {
            counter = 0;
            bool changed = false;

            for (int i = 0; i < numBands - 1; ++i)
            {
                const double diff = target[(size_t) i] - current[(size_t) i];

                if (std::abs (diff) > 1.0e-5)
                {
                    current[(size_t) i] += diff * (1.0 - smoothCoeff);
                    changed = true;
                }
                else if (diff != 0.0)
                {
                    current[(size_t) i] = target[(size_t) i];
                    changed = true;
                }
            }

            if (changed)
                updateCoefficients();
        }
    }

    /** Magnitude response of band `band` at `freq` (for display). */
    double bandMagnitude (int band, double freq) const noexcept
    {
        const int numXovers = numBands - 1;
        double m = 1.0;

        // Band b = LP_b( HP_{b-1}( ... HP_0(x) ) ), later crossovers only contribute all-passes
        for (int i = 0; i < std::min (band + 1, numXovers); ++i)
        {
            const auto h = std::abs (lr2Response (xoverCoeffs[(size_t) i], freq, i < band));
            m *= h * h;
        }

        return m;
    }

private:
    static constexpr int updateInterval = 16;
    static constexpr double minRatio = 1.26; // 1/3 octave

    struct ChannelState
    {
        std::array<std::array<SvfState, 3>, maxCrossovers> xover {};
        std::array<std::array<SvfState, maxCrossovers>, maxBands> allpass {};
    };

    std::complex<double> lr2Response (const SvfCoeffs& c, double freq, bool highPass) const noexcept
    {
        const double w = std::tan (kPi * std::min (freq, sampleRate * 0.4999) / sampleRate) / c.g;
        const std::complex<double> s (0.0, w);
        const auto den = s * s + c.k * s + 1.0;
        return highPass ? (s * s) / den : 1.0 / den;
    }

    void updateCoefficients() noexcept
    {
        for (int i = 0; i < maxCrossovers; ++i)
        {
            const double f = std::exp (current[(size_t) i]);
            xoverCoeffs[(size_t) i] = SvfCoeffs::make (SvfShape::lowPass, f, 0.70710678, 0.0, sampleRate);
            allpassCoeffs[(size_t) i] = SvfCoeffs::make (SvfShape::allPass, f, 0.70710678, 0.0, sampleRate);
        }
    }

    double sampleRate = 44100.0;
    int numBands = maxBands;
    int counter = 0;
    float smoothCoeff = 0.0f;
    std::array<double, maxCrossovers> target { std::log (120.0), std::log (1000.0), std::log (6000.0) };
    std::array<double, maxCrossovers> current = target;
    std::array<SvfCoeffs, maxCrossovers> xoverCoeffs {}, allpassCoeffs {};
    std::array<ChannelState, 2> channels {};
};

} // namespace hl::dsp
