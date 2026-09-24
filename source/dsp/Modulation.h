#pragma once

#include "Common.h"

namespace hl::dsp
{
enum class LfoShape { sine, triangle, rampUp, rampDown, square, sampleHold, smoothRandom, count };

constexpr int kNumLfoShapes = (int) LfoShape::count;

inline const char* const* lfoShapeNames() noexcept
{
    static const char* const names[kNumLfoShapes] = { "Sine", "Triangle", "Ramp Up", "Ramp Down", "Square", "Sample & Hold", "Smooth Random" };
    return names;
}

struct LfoSettings
{
    int shape = (int) LfoShape::sine;
    float rateHz = 1.0f;
    bool sync = false;
    int division = 9; // 1/4
};

struct EnvelopeSettings
{
    float attackMs = 5.0f;
    float releaseMs = 150.0f;
    float gainDb = 0.0f;
};

/** Low-frequency oscillator, bipolar output. Random shapes draw a new value every cycle. */
class Lfo
{
public:
    explicit Lfo (uint32_t seed = 1u) noexcept : rng (seed) { reset(); }

    void reset() noexcept
    {
        phase = 0.0;
        from = 0.0f;
        to = rng.nextBipolar();
        held = rng.nextBipolar();
    }

    /** Free-running advance by `numSamples` at `rateHz`. */
    void advance (int numSamples, double rateHz, double sampleRate) noexcept
    {
        setPhase (phase + rateHz * numSamples / sampleRate);
    }

    /** Host-locked phase (cycles, may exceed 1). Wrapping triggers new random values. */
    void setPhase (double cycles) noexcept
    {
        const double wrapped = cycles - std::floor (cycles);

        if (wrapped < phase - 1.0e-9 || cycles - phase >= 1.0)
        {
            from = to;
            to = rng.nextBipolar();
            held = rng.nextBipolar();
        }

        phase = wrapped;
    }

    double getPhase() const noexcept { return phase; }

    float value (LfoShape shape) const noexcept { return evaluate (shape, (float) phase, held, from, to); }

    /** Shape at an arbitrary phase (display); random shapes use fixed example values. */
    static float evaluate (LfoShape shape, float p, float held = 0.6f, float from = -0.4f, float to = 0.7f) noexcept
    {
        switch (shape)
        {
            case LfoShape::sine:         return std::sin (kTwoPiF * p);
            case LfoShape::triangle:     return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
            case LfoShape::rampUp:       return 2.0f * p - 1.0f;
            case LfoShape::rampDown:     return 1.0f - 2.0f * p;
            case LfoShape::square:       return p < 0.5f ? 1.0f : -1.0f;
            case LfoShape::sampleHold:   return held;
            case LfoShape::smoothRandom: return from + (to - from) * (0.5f - 0.5f * std::cos (p * 3.14159265f));
            case LfoShape::count:        break;
        }

        return 0.0f;
    }

private:
    Random rng;
    double phase = 0.0;
    float from = 0.0f, to = 0.0f, held = 0.0f;
};

/** Peak envelope follower mapped to 0..1 over a 48 dB window below 0 dBFS (plus gain). */
class EnvelopeFollower
{
public:
    void prepare (double rate) noexcept
    {
        sampleRate = rate;
        env = 0.0f;
    }

    void reset() noexcept { env = 0.0f; }

    void process (const float* left, const float* right, int n, const EnvelopeSettings& s) noexcept
    {
        const float att = onePoleCoeff (std::max (0.05f, s.attackMs) * 0.001, sampleRate);
        const float rel = onePoleCoeff (std::max (1.0f, s.releaseMs) * 0.001, sampleRate);
        const float gain = dbToGain (s.gainDb);

        for (int i = 0; i < n; ++i)
        {
            const float x = std::max (std::abs (left[i]), std::abs (right[i])) * gain;
            env = x + (env - x) * (x > env ? att : rel);
        }

        value = std::clamp ((gainToDb (env, -120.0f) + 48.0f) / 48.0f, 0.0f, 1.0f);
    }

    float getValue() const noexcept { return value; }

private:
    double sampleRate = 44100.0;
    float env = 0.0f, value = 0.0f;
};

} // namespace hl::dsp
