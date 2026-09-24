#pragma once

#include "Common.h"

namespace hl::dsp
{
/** Distortion algorithms offered by the Trash module. Order matches the plugin parameter choices. */
enum class Algo
{
    tube, tape, warm, transformer, diode, valve,        // saturate
    softClip, hardClip, asymClip, sineClip,              // clip
    fuzz, octaveFuzz, sputter, buzz,                     // fuzz
    sineFold, triFold, westCoast,                        // fold
    halfWave, fullWave,                                  // rectify
    bitcrush, decimate, muLaw, bitFlip,                  // digital
    cheby2, cheby3, cheby5,                              // harmonic
    count
};

constexpr int kNumAlgos = (int) Algo::count;

struct AlgoInfo
{
    const char* name;
    const char* family;
    const char* description;
};

inline const AlgoInfo& algoInfo (int index) noexcept
{
    static const AlgoInfo infos[kNumAlgos] = {
        { "Tube", "Saturate", "Biased triode curve: warm, asymmetric, even harmonics" },
        { "Tape", "Saturate", "Soft arctangent compression like a hot tape machine" },
        { "Warm", "Saturate", "Smooth symmetric tanh saturation" },
        { "Transformer", "Saturate", "Exponential core saturation, rounded and dense" },
        { "Diode", "Saturate", "Sharp-kneed diode clipper" },
        { "Valve", "Saturate", "Lopsided valve stage: squashes one half-wave only" },
        { "Soft Clip", "Clip", "Cubic soft clipper" },
        { "Hard Clip", "Clip", "Brick-wall digital clipping" },
        { "Asym Clip", "Clip", "Hard clipping with uneven rails" },
        { "Sine Clip", "Clip", "Sine-shaped clipper, brighter than tanh" },
        { "Fuzz", "Fuzz", "Transistor fuzz: heavily compressed, square-ish" },
        { "Octave Fuzz", "Fuzz", "Rectified fuzz that jumps an octave up" },
        { "Sputter", "Fuzz", "Starved, gated fuzz that spits and breaks up on decays" },
        { "Buzz", "Fuzz", "Almost a square wave: pure buzz" },
        { "Sine Fold", "Fold", "Wavefolder: the waveform folds back on itself" },
        { "Tri Fold", "Fold", "Linear triangle folder, glassy and bright" },
        { "West Coast", "Fold", "Phase-warped multi-stage folder" },
        { "Half Wave", "Rectify", "Half-wave rectifier: hollow, even harmonics" },
        { "Full Wave", "Rectify", "Full-wave rectifier: octave up" },
        { "Bitcrush", "Digital", "Bit depth reduction (drive lowers the bits)" },
        { "Decimate", "Digital", "Sample-rate reduction (drive lowers the rate)" },
        { "Mu-Law", "Digital", "8-bit telephone companding grit" },
        { "Bit Flip", "Digital", "Flips low bits of the samples: broken DAC glitch" },
        { "Cheby 2", "Harmonic", "Chebyshev polynomial: pure 2nd harmonic" },
        { "Cheby 3", "Harmonic", "Chebyshev polynomial: pure 3rd harmonic" },
        { "Cheby 5", "Harmonic", "Chebyshev polynomial: pure 5th harmonic" },
    };

    return infos[std::clamp (index, 0, kNumAlgos - 1)];
}

inline bool isDigital (Algo a) noexcept
{
    return a == Algo::bitcrush || a == Algo::decimate || a == Algo::muLaw || a == Algo::bitFlip;
}

constexpr float kMaxDriveDb = 48.0f;

/** Input gain in front of the shaper. Digital algorithms use drive mostly as "amount". */
inline float preGainDb (Algo a, float driveDb) noexcept
{
    return isDigital (a) ? driveDb * 0.25f : driveDb;
}

/** Per-block values derived from drive, shared by both channels. */
struct ShapeParams
{
    float amount = 0.0f;       // 0..1 (drive / max drive)
    float crushLevels = 2048;  // quantisation steps per unit
    float muLevels = 128;
    float decimateStep = 1.0f; // hold-clock increment per sample
    int flipMask = 7;

    static ShapeParams make (float driveDb, double sampleRate) noexcept
    {
        ShapeParams p;
        p.amount = std::clamp (driveDb / kMaxDriveDb, 0.0f, 1.0f);
        const float a = p.amount;
        p.crushLevels = std::pow (2.0f, 11.0f - 10.0f * a);                 // 12 bits .. 2 bits
        p.muLevels = std::pow (2.0f, 7.0f - 5.5f * a);                      // 8 bits .. 2.5 bits
        p.decimateStep = (float) (24000.0 * std::pow (60.0 / 24000.0, (double) a) / sampleRate);
        p.flipMask = (1 << (3 + (int) std::lround (11.0f * a))) - 1;        // 3 .. 14 bits
        return p;
    }
};

struct ShaperState
{
    float hold = 0.0f, phase = 1.0f;

    void reset() noexcept { hold = 0.0f; phase = 1.0f; }
};

namespace detail
{
    inline float chebyInput (float x) noexcept { return std::clamp (x, -1.0f, 1.0f); }
}

/** One sample through algorithm `a`. `x` already includes drive and bias. */
inline float shape (Algo a, float x, const ShapeParams& p, ShaperState& s) noexcept
{
    switch (a)
    {
        case Algo::tube:
        {
            constexpr float b = 0.35f, tb = 0.33637554f, norm = 1.0f / (1.0f - tb * tb);
            return (fastTanh (x + b) - tb) * norm;
        }

        case Algo::tape:        return 0.63661977f * std::atan (1.57079633f * x);
        case Algo::warm:        return fastTanh (x);
        case Algo::transformer: return x >= 0.0f ? 1.0f - std::exp (-x) : std::exp (x) - 1.0f;

        case Algo::diode:
        {
            const float x2 = x * x;
            return x / std::sqrt (std::sqrt (1.0f + x2 * x2));
        }

        case Algo::valve:       return x >= 0.0f ? x / (1.0f + x) : x / (1.0f - 0.3f * x);

        case Algo::softClip:
        {
            const float u = std::clamp (x * (1.0f / 1.5f), -1.0f, 1.0f);
            return 1.5f * (u - u * u * u * (1.0f / 3.0f));
        }

        case Algo::hardClip:    return std::clamp (x, -1.0f, 1.0f);
        case Algo::asymClip:    return std::clamp (x, -0.55f, 1.0f);
        case Algo::sineClip:    return std::abs (x) < 1.57079633f ? std::sin (x) : (x > 0.0f ? 1.0f : -1.0f);

        case Algo::fuzz:        return fastTanh (5.0f * x + 0.25f);
        case Algo::octaveFuzz:  return fastTanh (4.0f * std::abs (x));

        case Algo::sputter:
        {
            constexpr float gate = 0.18f;
            const float m = std::abs (x) - gate;
            return m <= 0.0f ? 0.0f : (x > 0.0f ? 1.0f : -1.0f) * fastTanh (6.0f * m);
        }

        case Algo::buzz:        return x >= 0.0f ? 1.0f - std::exp (-10.0f * x) : std::exp (10.0f * x) - 1.0f;

        case Algo::sineFold:    return std::sin (x);

        case Algo::triFold:
        {
            float t = (x + 1.0f) * 0.25f;
            t -= std::floor (t);
            return 1.0f - 4.0f * std::abs (t - 0.5f);
        }

        case Algo::westCoast:   return std::sin (x + 0.6f * std::sin (2.0f * x));

        case Algo::halfWave:
        {
            const float y = fastTanh (x);
            return y > 0.0f ? y : 0.08f * y;
        }

        case Algo::fullWave:    return std::abs (fastTanh (x));

        case Algo::bitcrush:
        {
            const float u = std::clamp (x, -1.0f, 1.0f);
            return std::round (u * p.crushLevels) / p.crushLevels;
        }

        case Algo::decimate:
        {
            s.phase += p.decimateStep;

            if (s.phase >= 1.0f)
            {
                s.phase -= std::floor (s.phase);
                s.hold = std::clamp (x, -2.0f, 2.0f);
            }

            return s.hold;
        }

        case Algo::muLaw:
        {
            constexpr float mu = 255.0f, invLog = 1.0f / 5.5451774f; // 1 / ln(256)
            const float u = std::clamp (x, -1.0f, 1.0f);
            const float c = std::log1p (mu * std::abs (u)) * invLog;
            const float q = std::round (c * p.muLevels) / p.muLevels;
            return (u >= 0.0f ? 1.0f : -1.0f) * (std::pow (1.0f + mu, q) - 1.0f) * (1.0f / mu);
        }

        case Algo::bitFlip:
        {
            const int v = (int) std::lround (std::clamp (x, -1.0f, 1.0f) * 32767.0f);
            return (float) (v ^ p.flipMask) * (1.0f / 32768.0f);
        }

        case Algo::cheby2: { const float u = detail::chebyInput (x); return 2.0f * u * u - 1.0f; }
        case Algo::cheby3: { const float u = detail::chebyInput (x); return u * (4.0f * u * u - 3.0f); }

        case Algo::cheby5:
        {
            const float u = detail::chebyInput (x), u2 = u * u;
            return u * (16.0f * u2 * u2 - 20.0f * u2 + 5.0f);
        }

        case Algo::count:
            break;
    }

    return x;
}

/** Stateless evaluation for curves and DC offsets (decimation shows as a straight line). */
inline float shapeStatic (Algo a, float x, const ShapeParams& p) noexcept
{
    ShaperState s;
    s.phase = 0.0f;
    ShapeParams q = p;
    q.decimateStep = 1.0f;
    return shape (a, x, q, s);
}

//==============================================================================
/** Level compensation per algorithm and drive, measured once with a -12 dBFS sine so that
    turning drive up changes the character more than the loudness. */
class AutoGainTable
{
public:
    static constexpr int numSteps = 25; // 0, 2, 4, ... 48 dB

    static const AutoGainTable& get()
    {
        static const AutoGainTable table;
        return table;
    }

    /** Compensation in dB for algorithm `a` at `driveDb`. */
    float compensationDb (Algo a, float driveDb) const noexcept
    {
        const float pos = std::clamp (driveDb, 0.0f, kMaxDriveDb) / kMaxDriveDb * (numSteps - 1);
        const int i0 = std::min ((int) pos, numSteps - 2);
        const float t = pos - (float) i0;
        const auto& row = db[(size_t) a];
        return row[(size_t) i0] + t * (row[(size_t) i0 + 1] - row[(size_t) i0]);
    }

private:
    AutoGainTable()
    {
        constexpr int n = 8192, skip = 512;
        constexpr double rate = 192000.0, freq = 997.0, amp = 0.25;
        std::vector<float> input ((size_t) n);

        for (int i = 0; i < n; ++i)
            input[(size_t) i] = (float) (amp * std::sin (2.0 * kPi * freq * i / rate));

        const double rmsIn = amp / std::sqrt (2.0);

        for (int a = 0; a < kNumAlgos; ++a)
        {
            const auto algo = (Algo) a;

            for (int step = 0; step < numSteps; ++step)
            {
                const float driveDb = kMaxDriveDb * (float) step / (float) (numSteps - 1);
                const auto params = ShapeParams::make (driveDb, rate);
                const float g = dbToGain (preGainDb (algo, driveDb));
                ShaperState state;
                double sum = 0.0, sumSq = 0.0;

                for (int i = 0; i < n; ++i)
                {
                    const double y = shape (algo, input[(size_t) i] * g, params, state);

                    if (i >= skip)
                    {
                        sum += y;
                        sumSq += y * y;
                    }
                }

                const double count = n - skip;
                const double mean = sum / count;
                const double rmsOut = std::sqrt (std::max (1.0e-12, sumSq / count - mean * mean));
                db[(size_t) a][(size_t) step] = (float) std::clamp (20.0 * std::log10 (rmsIn / rmsOut), -30.0, 24.0);
            }
        }
    }

    std::array<std::array<float, numSteps>, kNumAlgos> db {};
};

} // namespace hl::dsp
