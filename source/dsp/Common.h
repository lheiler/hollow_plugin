#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hl::dsp
{
constexpr double kPi = 3.14159265358979323846;
constexpr float kTwoPiF = 6.28318530717958647f;
constexpr float kMinusInfDb = -150.0f;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline double dbToGain (double db) noexcept { return std::pow (10.0, db * 0.05); }

inline float gainToDb (float gain, float floorDb = kMinusInfDb) noexcept
{
    return gain > 0.0f ? std::max (floorDb, 20.0f * std::log10 (gain)) : floorDb;
}

/** Coefficient for a one-pole smoother reaching ~63% of a step after `timeSeconds`. */
inline float onePoleCoeff (double timeSeconds, double rate) noexcept
{
    if (timeSeconds <= 0.0 || rate <= 0.0)
        return 0.0f;

    return (float) std::exp (-1.0 / (timeSeconds * rate));
}

/** Rational tanh approximation, exact at 0 and saturating to +-1 (error < 2.5e-3). */
inline float fastTanh (float x) noexcept
{
    if (x > 3.0f)
        return 1.0f;

    if (x < -3.0f)
        return -1.0f;

    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Parameter mapping shared by the DSP (for modulation) and the plugin parameters. */
struct Range
{
    float min = 0.0f, max = 1.0f;
    bool logarithmic = false;

    float toNorm (float v) const noexcept
    {
        v = std::clamp (v, min, max);
        return logarithmic ? std::log (v / min) / std::log (max / min) : (v - min) / (max - min);
    }

    float fromNorm (float n) const noexcept
    {
        n = std::clamp (n, 0.0f, 1.0f);
        return logarithmic ? min * std::pow (max / min, n) : min + n * (max - min);
    }

    /** Moves `v` by `offset` in normalised space. */
    float offset (float v, float normOffset) const noexcept { return fromNorm (toNorm (v) + normOffset); }
};

/** Linear ramp towards a target over a fixed number of samples. */
class LinearSmoother
{
public:
    void reset (double rate, double rampSeconds) noexcept
    {
        rampLength = std::max (1, (int) std::lround (rate * rampSeconds));
        setCurrentAndTarget (target);
    }

    void setCurrentAndTarget (float v) noexcept
    {
        current = target = v;
        countdown = 0;
    }

    void setTarget (float v) noexcept
    {
        if (v == target)
            return;

        target = v;
        countdown = rampLength;
        step = (target - current) / (float) rampLength;
    }

    float next() noexcept
    {
        if (countdown > 0)
        {
            current += step;

            if (--countdown == 0)
                current = target;
        }

        return current;
    }

    bool isSmoothing() const noexcept { return countdown > 0; }
    float getCurrent() const noexcept { return current; }
    float getTarget() const noexcept { return target; }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int countdown = 0, rampLength = 1;
};

/** Ramps from the previous block's value to a new target across exactly one block. */
class BlockRamp
{
public:
    void snap (float v) noexcept { value = target = v; step = 0.0f; }

    void setTarget (float v, int blockLength) noexcept
    {
        value = target;
        target = v;
        step = (target - value) / (float) std::max (1, blockLength);
    }

    float next() noexcept
    {
        value += step;
        return value;
    }

    float getTarget() const noexcept { return target; }

private:
    float value = 0.0f, target = 0.0f, step = 0.0f;
};

/** Integer delay line (single channel) backed by a power-of-two ring buffer. */
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        size = 1;

        while (size < maxDelaySamples + 1)
            size <<= 1;

        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
        delay = std::min (delay, size - 1);
    }

    void setDelay (int samples) noexcept { delay = std::clamp (samples, 0, size - 1); }
    int getDelay() const noexcept { return delay; }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    float process (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        const float y = buffer[(size_t) ((writePos - delay) & mask)];
        writePos = (writePos + 1) & mask;
        return y;
    }

private:
    std::vector<float> buffer { 0.0f };
    int size = 1, mask = 0, writePos = 0, delay = 0;
};

/** Ring buffer with fractional (4-point Hermite) reads, for modulated delays. */
class FractionalDelay
{
public:
    void prepare (int maxDelaySamples)
    {
        size = 4;

        while (size < maxDelaySamples + 4)
            size <<= 1;

        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    int getMaxDelay() const noexcept { return size - 4; }

    void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** Reads `delay` samples behind the most recently pushed sample (clamped to >= 1). */
    float read (float delay) const noexcept
    {
        delay = std::clamp (delay, 1.0f, (float) (size - 4));
        const int whole = (int) delay;
        const float t = delay - (float) whole;
        const int base = writePos - 1 - whole;

        const float xm1 = buffer[(size_t) ((base + 1) & mask)];
        const float x0 = buffer[(size_t) (base & mask)];
        const float x1 = buffer[(size_t) ((base - 1) & mask)];
        const float x2 = buffer[(size_t) ((base - 2) & mask)];

        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    /** Integer read relative to the write head (0 = most recent). */
    float at (int delay) const noexcept { return buffer[(size_t) ((writePos - 1 - delay) & mask)]; }

private:
    std::vector<float> buffer { 0.0f, 0.0f, 0.0f, 0.0f };
    int size = 4, mask = 3, writePos = 0;
};

/** First-order DC blocking high-pass. */
class DcBlocker
{
public:
    void prepare (double rate, double cutoffHz = 5.0) noexcept
    {
        r = (float) std::exp (-2.0 * kPi * cutoffHz / rate);
        reset();
    }

    void reset() noexcept { x1 = y1 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
};

/** One-pole low-pass (6 dB/oct). */
class OnePole
{
public:
    void setCutoff (double hz, double rate) noexcept { a = (float) std::exp (-2.0 * kPi * std::clamp (hz, 1.0, rate * 0.49) / rate); }
    void reset() noexcept { y = 0.0f; }
    float lowPass (float x) noexcept { return y = x + a * (y - x); }
    float highPass (float x) noexcept { return x - lowPass (x); }

private:
    float a = 0.0f, y = 0.0f;
};

/** Small, fast deterministic RNG (xorshift32). */
class Random
{
public:
    explicit Random (uint32_t seed = 0x9e3779b9u) noexcept : state (seed != 0 ? seed : 1u) {}

    void seed (uint32_t s) noexcept { state = s != 0 ? s : 1u; }

    uint32_t nextInt() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** Uniform in [0, 1). */
    float nextFloat() noexcept { return (float) (nextInt() >> 8) * (1.0f / 16777216.0f); }

    /** Uniform in [-1, 1). */
    float nextBipolar() noexcept { return 2.0f * nextFloat() - 1.0f; }

private:
    uint32_t state;
};

/** Smoothly interpolated random walk in [-1, 1]: a new target every period, cosine-eased. */
class SmoothRandom
{
public:
    explicit SmoothRandom (uint32_t seed = 12345u) noexcept : rng (seed) {}

    void reset() noexcept { phase = 0.0f; from = 0.0f; to = rng.nextBipolar(); }

    float next (float freqHz, float rate) noexcept
    {
        phase += freqHz / rate;

        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            from = to;
            to = rng.nextBipolar();
        }

        const float t = 0.5f - 0.5f * std::cos (phase * 3.14159265f);
        return from + (to - from) * t;
    }

private:
    Random rng;
    float phase = 0.0f, from = 0.0f, to = 0.0f;
};

/** Zeroth-order modified Bessel function, used for Kaiser windows. */
inline double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    const double halfX = 0.5 * x;

    for (int k = 1; k < 64; ++k)
    {
        term *= (halfX / k) * (halfX / k);
        sum += term;

        if (term < sum * 1.0e-12)
            break;
    }

    return sum;
}

/** Kaiser window evaluated at r in [-1, 1] (0 outside). */
inline double kaiser (double r, double beta) noexcept
{
    if (std::abs (r) > 1.0)
        return 0.0;

    return besselI0 (beta * std::sqrt (1.0 - r * r)) / besselI0 (beta);
}

inline double sinc (double x) noexcept
{
    if (std::abs (x) < 1.0e-12)
        return 1.0;

    return std::sin (kPi * x) / (kPi * x);
}

/** Note divisions offered for tempo sync, in quarter-note beats. */
inline constexpr int kNumDivisions = 17;

inline const char* const* divisionNames() noexcept
{
    static const char* const names[kNumDivisions] = { "1/64", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T",
                                                      "1/4", "1/4D", "1/2", "1/2D", "1/1", "2/1", "4/1", "8/1" };
    return names;
}

inline double divisionBeats (int division) noexcept
{
    static const double beats[kNumDivisions] = { 0.0625, 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0,
                                                 1.0, 1.5, 2.0, 3.0, 4.0, 8.0, 16.0, 32.0 };
    return beats[std::clamp (division, 0, kNumDivisions - 1)];
}

} // namespace hl::dsp
