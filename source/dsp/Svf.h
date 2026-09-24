#pragma once

#include "Common.h"

#include <complex>

namespace hl::dsp
{
/** Topology-preserving-transform state variable filter (Andrew Simper / Cytomic).
    Stable under fast modulation, so coefficients can be updated every sample. */
enum class SvfShape
{
    bell,
    lowShelf,
    highShelf,
    lowPass,
    highPass,
    bandPass,
    notch,
    allPass
};

struct SvfCoeffs
{
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
    double g = 0.0, k = 1.0; // kept for response evaluation

    static SvfCoeffs make (SvfShape shape, double freq, double q, double gainDb, double sampleRate) noexcept
    {
        freq = std::clamp (freq, 1.0, sampleRate * 0.49);
        q = std::max (q, 0.025);

        const double A = std::pow (10.0, gainDb / 40.0);
        double g = std::tan (kPi * freq / sampleRate);
        double k = 1.0 / q;
        double m0 = 0.0, m1 = 0.0, m2 = 0.0;

        switch (shape)
        {
            case SvfShape::bell:      k = 1.0 / (q * A); m0 = 1.0; m1 = k * (A * A - 1.0); break;
            case SvfShape::lowShelf:  g /= std::sqrt (A); m0 = 1.0; m1 = k * (A - 1.0); m2 = A * A - 1.0; break;
            case SvfShape::highShelf: g *= std::sqrt (A); m0 = A * A; m1 = k * (1.0 - A) * A; m2 = 1.0 - A * A; break;
            case SvfShape::lowPass:   m2 = 1.0; break;
            case SvfShape::highPass:  m0 = 1.0; m1 = -k; m2 = -1.0; break;
            case SvfShape::bandPass:  m1 = k; break; // unity gain at the centre frequency
            case SvfShape::notch:     m0 = 1.0; m1 = -k; break;
            case SvfShape::allPass:   m0 = 1.0; m1 = -2.0 * k; break;
        }

        SvfCoeffs c;
        const double a1 = 1.0 / (1.0 + g * (g + k));
        c.a1 = (float) a1;
        c.a2 = (float) (g * a1);
        c.a3 = (float) (g * g * a1);
        c.m0 = (float) m0;
        c.m1 = (float) m1;
        c.m2 = (float) m2;
        c.g = g;
        c.k = k;
        return c;
    }

    /** Exact complex response of the discretised filter at `freq`. */
    std::complex<double> response (double freq, double sampleRate) const noexcept
    {
        // The TPT SVF equals the bilinear transform of
        // H(s) = (m0 s^2 + (m0 k + m1) s + (m0 + m2)) / (s^2 + k s + 1), with s = j tan(pi f / fs) / g
        const double w = std::tan (kPi * std::min (freq, sampleRate * 0.4999) / sampleRate) / g;
        const std::complex<double> s (0.0, w);
        const auto num = (double) m0 * s * s + ((double) m0 * k + (double) m1) * s + ((double) m0 + (double) m2);
        const auto den = s * s + k * s + 1.0;
        return num / den;
    }
};

struct SvfState
{
    float ic1 = 0.0f, ic2 = 0.0f;

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    float process (const SvfCoeffs& c, float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
    }

    /** Runs the filter and returns the raw low / band / high outputs. */
    void processMulti (const SvfCoeffs& c, float v0, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = v0 - (float) c.k * v1 - v2;
    }
};

/** Per-sample SVF with a saturating band-pass integrator: resonance squashes like an analog
    filter instead of running away, which is what makes the filters "scream" nicely. */
struct DrivenSvf
{
    float ic1 = 0.0f, ic2 = 0.0f;

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    /** g = tan(pi fc / fs), k = 1 / Q. `limit` is the level at which the resonant state saturates. */
    void process (float v0, float g, float k, float limit, float& lp, float& bp, float& hp) noexcept
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1, a3 = g * a2;
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = limit * fastTanh ((2.0f * v1 - ic1) / limit);
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = v0 - k * v1 - v2;
    }
};

} // namespace hl::dsp
