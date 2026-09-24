#pragma once

#include "Common.h"

namespace hl::dsp
{
enum class MotionMode { chorus, flanger, phaser, vibrato, tremolo, ringMod, freqShift, count };

constexpr int kNumMotionModes = (int) MotionMode::count;

inline const char* const* motionModeNames() noexcept
{
    static const char* const names[kNumMotionModes] = { "Chorus", "Flanger", "Phaser", "Vibrato", "Tremolo", "Ring Mod", "Freq Shift" };
    return names;
}

struct MotionSettings
{
    int mode = (int) MotionMode::chorus;
    float rateHz = 0.6f;
    float depth = 0.5f;     // 0..1
    float feedback = 0.0f;  // -1..1
    float freqHz = 440.0f;  // phaser centre, ring carrier, shift amount
    float spread = 0.5f;    // 0..1: stereo LFO offset (1 = opposite phase)
    float mix = 0.5f;
};

/** 90-degree phase splitter (Olli Niemitalo's allpass pairs): I and Q outputs of an analytic signal. */
class HilbertPair
{
public:
    void reset() noexcept
    {
        for (auto& s : stages)
            s = {};

        delayed = 0.0f;
    }

    void process (float x, float& i, float& q) noexcept
    {
        static constexpr float a[8] = { 0.47940086558884f, 0.87621849353931f, 0.97659758950819f, 0.99749925593555f,     // path 1
                                        0.16175849836770f, 0.73302893234149f, 0.94534970032911f, 0.99059915668453f };  // path 2
        float p1 = x, p2 = x;

        for (int s = 0; s < 4; ++s)
            p1 = stages[(size_t) s].process (a[s], p1);

        for (int s = 4; s < 8; ++s)
            p2 = stages[(size_t) s].process (a[s], p2);

        i = delayed;
        delayed = p1;
        q = p2;
    }

private:
    struct Stage
    {
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

        float process (float c, float x) noexcept
        {
            const float y = c * (x + y2) - x2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }
    };

    std::array<Stage, 8> stages {};
    float delayed = 0.0f;
};

/** Modulation effects driven by an internal sine LFO. */
class MotionModule
{
public:
    static constexpr int phaserStages = 6;

    void prepare (double rate, int)
    {
        sampleRate = rate;
        glide = onePoleCoeff (0.01, rate);

        for (auto& d : delays)
            d.prepare ((int) std::ceil (rate * 0.06));

        duck.reset (rate, 0.005);
        duck.setCurrentAndTarget (1.0f);
        activeMode = std::clamp (settings.mode, 0, kNumMotionModes - 1);
        depth = settings.depth;
        mix = settings.mix;
        feedback = settings.feedback;
        logFreq = std::log (std::max (1.0f, settings.freqHz));
        reset();
    }

    void reset()
    {
        for (auto& d : delays)
            d.reset();

        for (auto& ch : channels)
            ch = {};
    }

    void setSettings (const MotionSettings& s) noexcept
    {
        settings = s;
        const int wanted = std::clamp (s.mode, 0, kNumMotionModes - 1);

        if (wanted != activeMode)
            duck.setTarget (0.0f);
        else if (duck.getTarget() < 1.0f)
            duck.setTarget (1.0f);
    }

    /** Aligns the LFO with the host (tempo-synced mode). */
    void setPhase (double newPhase) noexcept { phase = newPhase - std::floor (newPhase); }
    double getPhase() const noexcept { return phase; }

    void process (float* left, float* right, int n) noexcept
    {
        const float k = 1.0f - glide;
        const float fs = (float) sampleRate;
        const double inc = std::clamp ((double) settings.rateHz, 0.0, 40.0) / sampleRate;
        const float tDepth = std::clamp (settings.depth, 0.0f, 1.0f);
        const float tMix = std::clamp (settings.mix, 0.0f, 1.0f);
        const float tFeedback = std::clamp (settings.feedback, -1.0f, 1.0f);
        const float tLogFreq = std::log (std::clamp (settings.freqHz, 1.0f, 20000.0f));
        const float spread = std::clamp (settings.spread, 0.0f, 1.0f);
        float* chans[2] = { left, right };

        for (int i = 0; i < n; ++i)
        {
            depth += (tDepth - depth) * k;
            mix += (tMix - mix) * k;
            feedback += (tFeedback - feedback) * k;
            logFreq += (tLogFreq - logFreq) * k;

            const float d = duck.next();

            if (d <= 0.0f && ! duck.isSmoothing() && activeMode != std::clamp (settings.mode, 0, kNumMotionModes - 1))
            {
                activeMode = std::clamp (settings.mode, 0, kNumMotionModes - 1);
                reset();
                duck.setTarget (1.0f);
            }

            phase += inc;

            if (phase >= 1.0)
                phase -= 1.0;

            const float freq = std::exp (logFreq);
            const float wetAmount = mix * d;

            for (int ch = 0; ch < 2; ++ch)
            {
                const float p = (float) phase + (ch == 1 ? 0.5f * spread : 0.0f);
                const float lfo = std::sin (kTwoPiF * p);
                const float x = chans[ch][i];
                const float y = processSample ((MotionMode) activeMode, ch, x, lfo, p, freq, fs);
                chans[ch][i] = x + wetAmount * (y - x);
            }
        }
    }

private:
    struct ChannelState
    {
        std::array<float, phaserStages> ap {};
        float last = 0.0f;
        double carrier = 0.0;
        HilbertPair hilbert;
    };

    float processSample (MotionMode mode, int ch, float x, float lfo, float lfoPhase, float freq, float fs) noexcept
    {
        auto& st = channels[(size_t) ch];
        auto& line = delays[(size_t) ch];
        const float msToSamples = fs * 0.001f;

        switch (mode)
        {
            case MotionMode::chorus:
            {
                const float lfo2 = std::sin (kTwoPiF * (lfoPhase + 0.33f));
                const float d1 = (7.0f + 6.0f * depth * (0.5f + 0.5f * lfo)) * msToSamples;
                const float d2 = (11.0f + 7.0f * depth * (0.5f + 0.5f * lfo2)) * msToSamples;
                const float y = 0.5f * (line.read (d1) + line.read (d2));
                line.push (x + 0.5f * std::abs (feedback) * y);
                return y;
            }

            case MotionMode::flanger:
            {
                const float delay = (0.25f + 5.5f * depth * (0.5f + 0.5f * lfo)) * msToSamples;
                const float y = line.read (delay);
                line.push (x + 0.95f * feedback * fastTanh (y));
                return y;
            }

            case MotionMode::phaser:
            {
                const float fc = std::clamp (freq * std::pow (2.0f, 2.5f * depth * lfo), 20.0f, fs * 0.45f);
                const float t = std::tan (3.14159265f * fc / fs);
                const float c = (t - 1.0f) / (t + 1.0f);
                float v = x + 0.9f * feedback * st.last;

                for (auto& s : st.ap)
                {
                    const float y = c * v + s;
                    s = v - c * y;
                    v = y;
                }

                st.last = fastTanh (v);
                return v;
            }

            case MotionMode::vibrato:
            {
                const float delay = (0.6f + 5.0f * depth * (0.5f + 0.5f * lfo)) * msToSamples;
                line.push (x);
                return line.read (delay);
            }

            case MotionMode::tremolo:
            {
                // feedback > 0 squares the wave off into a chop
                const float sharp = 1.0f + 12.0f * std::max (0.0f, feedback);
                const float shaped = fastTanh (lfo * sharp) / fastTanh (sharp);
                return x * (1.0f - depth * (0.5f - 0.5f * shaped));
            }

            case MotionMode::ringMod:
            {
                const double f = (double) freq * std::pow (2.0, 2.0 * depth * lfo);
                st.carrier += f / fs;
                st.carrier -= std::floor (st.carrier);
                const float offset = ch == 1 ? 0.5f * settings.spread : 0.0f;
                return x * std::sin (kTwoPiF * ((float) st.carrier + offset));
            }

            case MotionMode::freqShift:
            {
                const float sign = ch == 1 ? 1.0f - 2.0f * settings.spread : 1.0f;
                const double shift = sign * (double) freq * (1.0 + 0.5 * depth * lfo);
                st.carrier += shift / fs;
                st.carrier -= std::floor (st.carrier);

                float re, im;
                st.hilbert.process (x + 0.85f * std::abs (feedback) * st.last, re, im);
                const float w = kTwoPiF * (float) st.carrier;
                const float y = re * std::cos (w) + im * std::sin (w);
                st.last = fastTanh (y);
                return y;
            }

            case MotionMode::count:
                break;
        }

        return x;
    }

    double sampleRate = 44100.0, phase = 0.0;
    float glide = 0.0f;
    MotionSettings settings;
    int activeMode = 0;
    float depth = 0.5f, mix = 0.5f, feedback = 0.0f, logFreq = 6.0f;
    LinearSmoother duck;
    std::array<FractionalDelay, 2> delays;
    std::array<ChannelState, 2> channels {};
};

} // namespace hl::dsp
