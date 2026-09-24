#pragma once

#include "Common.h"

namespace hl::dsp
{
/** Linear-phase half-band FIR (length 4k+3) designed with a Kaiser-windowed sinc. */
inline std::vector<double> designHalfband (int numTaps, double beta)
{
    const int centre = (numTaps - 1) / 2;
    std::vector<double> h ((size_t) numTaps, 0.0);
    double evenSum = 0.0;

    for (int i = 0; i < numTaps; ++i)
    {
        const int offset = i - centre;

        if (offset == 0)
            h[(size_t) i] = 0.5;
        else if (offset % 2 != 0)
            h[(size_t) i] = 0.5 * sinc (offset * 0.5) * kaiser ((double) offset / (centre + 1), beta);

        if (i % 2 == 0)
            evenSum += h[(size_t) i];
    }

    // Each polyphase branch must have exactly unity DC gain (after the x2 upsampling gain)
    for (int i = 0; i < numTaps; i += 2)
        h[(size_t) i] *= 0.5 / evenSum;

    return h;
}

/** 2x interpolator: one input sample in, two output samples out. */
class HalfbandUpsampler
{
public:
    void design (int numTaps, double beta)
    {
        const auto h = designHalfband (numTaps, beta);
        taps.clear();

        for (int i = 0; i < numTaps; i += 2)
            taps.push_back ((float) (2.0 * h[(size_t) i]));

        centreDelay = (numTaps - 3) / 4;
        latencyHighRate = (numTaps - 1) / 2;
        history.assign (taps.size() * 2, 0.0f);
        pos = 0;
    }

    void reset() noexcept
    {
        std::fill (history.begin(), history.end(), 0.0f);
        pos = 0;
    }

    int getLatencyAtHighRate() const noexcept { return latencyHighRate; }

    void process (float x, float& y0, float& y1) noexcept
    {
        const int n = (int) taps.size();
        pos = (pos == 0 ? n : pos) - 1;
        history[(size_t) pos] = history[(size_t) (pos + n)] = x;

        const float* hist = history.data() + pos; // hist[q] = x[m - q]
        float acc = 0.0f;

        for (int q = 0; q < n; ++q)
            acc += taps[(size_t) q] * hist[q];

        y0 = acc;
        y1 = hist[centreDelay];
    }

private:
    std::vector<float> taps, history;
    int pos = 0, centreDelay = 0, latencyHighRate = 0;
};

/** 2x decimator: two input samples in, one output sample out. */
class HalfbandDownsampler
{
public:
    void design (int numTaps, double beta)
    {
        const auto h = designHalfband (numTaps, beta);
        taps.clear();

        for (int i = 0; i < numTaps; i += 2)
            taps.push_back ((float) h[(size_t) i]);

        centreDelay = (numTaps - 3) / 4;
        latencyHighRate = (numTaps - 1) / 2;
        evenHistory.assign (taps.size() * 2, 0.0f);
        oddHistory.assign ((size_t) centreDelay + 1, 0.0f);
        evenPos = oddPos = 0;
    }

    void reset() noexcept
    {
        std::fill (evenHistory.begin(), evenHistory.end(), 0.0f);
        std::fill (oddHistory.begin(), oddHistory.end(), 0.0f);
        evenPos = oddPos = 0;
    }

    int getLatencyAtHighRate() const noexcept { return latencyHighRate; }

    float process (float vEven, float vOdd) noexcept
    {
        const int n = (int) taps.size();
        evenPos = (evenPos == 0 ? n : evenPos) - 1;
        evenHistory[(size_t) evenPos] = evenHistory[(size_t) (evenPos + n)] = vEven;

        const float* hist = evenHistory.data() + evenPos;
        float acc = 0.0f;

        for (int q = 0; q < n; ++q)
            acc += taps[(size_t) q] * hist[q];

        // centre tap (0.5) hits the odd sample `centreDelay` pairs back
        const int oddSize = (int) oddHistory.size();
        acc += 0.5f * oddHistory[(size_t) oddPos]; // oldest entry == o[m - k - 1]
        oddHistory[(size_t) oddPos] = vOdd;
        oddPos = (oddPos + 1) % oddSize;

        return acc;
    }

private:
    std::vector<float> taps, evenHistory, oddHistory;
    int evenPos = 0, oddPos = 0, centreDelay = 0, latencyHighRate = 0;
};

/** Linear-phase 4x oversampler (two cascaded half-band stages) for one channel.

    The two inner stages use different lengths so that the total round-trip latency
    is a whole number of base-rate samples. */
class Oversampler4x
{
public:
    static constexpr int factor = 4;

    void prepare()
    {
        upA.design (123, 9.0);   // base -> 2x: steep, ~90 dB
        downA.design (123, 9.0);
        upB.design (19, 7.2);    // 2x -> 4x: relaxed transition
        downB.design (23, 8.6);
        reset();
    }

    void reset() noexcept
    {
        upA.reset();
        upB.reset();
        downA.reset();
        downB.reset();
    }

    /** Round-trip latency in base-rate samples. */
    int getLatency() const noexcept
    {
        const int stageA = upA.getLatencyAtHighRate() + downA.getLatencyAtHighRate();  // at 2x
        const int stageB = upB.getLatencyAtHighRate() + downB.getLatencyAtHighRate();  // at 4x
        return stageA / 2 + stageB / 4;
    }

    void upsample (float x, float* out) noexcept
    {
        float a0, a1;
        upA.process (x, a0, a1);
        upB.process (a0, out[0], out[1]);
        upB.process (a1, out[2], out[3]);
    }

    float downsample (const float* in) noexcept
    {
        const float b0 = downB.process (in[0], in[1]);
        const float b1 = downB.process (in[2], in[3]);
        return downA.process (b0, b1);
    }

private:
    HalfbandUpsampler upA, upB;
    HalfbandDownsampler downA, downB;
};

/** Linear-phase oversampler for 1x, 2x, 4x or 8x (cascaded half-band stages), one channel.

    The reported latency is the 8x one for every factor (lower factors are padded with a plain delay), so
    switching quality never moves the audio in time or disturbs the host's delay compensation. Every
    stage pair has a whole-sample round trip at its rate, so the padding is exact. */
class VariableOversampler
{
public:
    static constexpr int maxFactor = 8;

    void prepare()
    {
        upA.design (123, 9.0);   // base -> 2x: steep, ~90 dB
        downA.design (123, 9.0);
        upB.design (19, 7.2);    // 2x -> 4x: relaxed transition
        downB.design (23, 8.6);
        upC.design (15, 7.0);    // 4x -> 8x: the audio band is a tiny fraction of this rate
        downC.design (19, 8.0);
        pad.prepare (getLatency());
        setFactor (factor);
        reset();
    }

    void reset() noexcept
    {
        for (auto* u : { &upA, &upB, &upC })
            u->reset();

        for (auto* d : { &downA, &downB, &downC })
            d->reset();

        pad.reset();
    }

    /** 1, 2, 4 or 8. Resets the filter memories (call between blocks). */
    void setFactor (int newFactor) noexcept
    {
        newFactor = newFactor >= 8 ? 8 : (newFactor >= 4 ? 4 : (newFactor >= 2 ? 2 : 1));

        if (newFactor != factor)
        {
            factor = newFactor;
            reset();
        }

        pad.setDelay (getLatency() - latencyFor (factor));
    }

    int getFactor() const noexcept { return factor; }

    /** Constant round-trip latency in base-rate samples (the 8x one). */
    int getLatency() const noexcept { return latencyFor (maxFactor); }

    /** Latency of the filters alone for a factor. */
    int latencyFor (int f) const noexcept
    {
        const int a = upA.getLatencyAtHighRate() + downA.getLatencyAtHighRate();
        const int b = upB.getLatencyAtHighRate() + downB.getLatencyAtHighRate();
        const int c = upC.getLatencyAtHighRate() + downC.getLatencyAtHighRate();
        return f <= 1 ? 0 : (f == 2 ? a / 2 : (f == 4 ? a / 2 + b / 4 : a / 2 + b / 4 + c / 8));
    }

    /** Writes getFactor() samples. */
    void upsample (float x, float* out) noexcept
    {
        if (factor == 1)
        {
            out[0] = x;
            return;
        }

        float a[2];
        upA.process (x, a[0], a[1]);

        if (factor == 2)
        {
            out[0] = a[0];
            out[1] = a[1];
            return;
        }

        float b[4];
        upB.process (a[0], b[0], b[1]);
        upB.process (a[1], b[2], b[3]);

        if (factor == 4)
        {
            std::copy_n (b, 4, out);
            return;
        }

        for (int i = 0; i < 4; ++i)
            upC.process (b[i], out[2 * i], out[2 * i + 1]);
    }

    /** Reads getFactor() samples. */
    float downsample (const float* in) noexcept
    {
        float y;

        if (factor == 1)
        {
            y = in[0];
        }
        else if (factor == 2)
        {
            y = downA.process (in[0], in[1]);
        }
        else
        {
            float b[4];

            if (factor == 8)
                for (int i = 0; i < 4; ++i)
                    b[i] = downC.process (in[2 * i], in[2 * i + 1]);
            else
                std::copy_n (in, 4, b);

            // (two statements: argument evaluation order is unspecified and these filters have state)
            const float b0 = downB.process (b[0], b[1]);
            const float b1 = downB.process (b[2], b[3]);
            y = downA.process (b0, b1);
        }

        return pad.process (y);
    }

private:
    HalfbandUpsampler upA, upB, upC;
    HalfbandDownsampler downA, downB, downC;
    DelayLine pad;
    int factor = 4;
};

} // namespace hl::dsp
