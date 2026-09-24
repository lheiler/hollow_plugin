#pragma once

#include "Svf.h"

namespace hl::dsp
{
struct EchoSettings
{
    float timeMs = 350.0f;
    float feedback = 0.45f;  // 0 .. 1.2 (above 1 it runs away into saturation)
    float tone = 0.0f;       // -1 dark .. +1 thin
    float drive = 0.2f;      // saturation inside the loop
    float wobble = 0.2f;     // tape wow on the repeats
    bool pingPong = false;
    float mix = 0.3f;
};

/** Tape-style echo: time changes glide (pitch bends), the loop saturates and filters every repeat,
    and feedback past 100% self-oscillates into a bounded wall of noise. */
class EchoModule
{
public:
    static constexpr float maxTimeMs = 2000.0f;

    void prepare (double rate, int)
    {
        sampleRate = rate;
        timeGlide = onePoleCoeff (0.18, rate);
        paramGlide = onePoleCoeff (0.02, rate);

        for (auto& l : lines)
            l.prepare ((int) std::ceil (rate * (maxTimeMs * 0.001 + 0.05)));

        delaySamples = timeToSamples (settings.timeMs);
        feedback = settings.feedback;
        drive = settings.drive;
        mix = settings.mix;
        setSettings (settings);
        reset();
    }

    void reset() noexcept
    {
        for (auto& l : lines)
            l.reset();

        for (auto& c : channels)
            c = {};

        wobblePhase = 0.0;
        wobbleRandom.reset();
    }

    void setSettings (const EchoSettings& s) noexcept
    {
        settings = s;
        const float t = std::clamp (s.tone, -1.0f, 1.0f);
        const double lp = t < 0.0f ? 16000.0 * std::pow (600.0 / 16000.0, (double) -t) : 16000.0;
        const double hp = t > 0.0f ? 40.0 * std::pow (2500.0 / 40.0, (double) t) : 40.0;
        lpCoeffs = SvfCoeffs::make (SvfShape::lowPass, std::min (lp, sampleRate * 0.45), 0.6, 0.0, sampleRate);
        hpCoeffs = SvfCoeffs::make (SvfShape::highPass, hp, 0.6, 0.0, sampleRate);
    }

    void process (float* left, float* right, int n) noexcept
    {
        const float tDelay = timeToSamples (settings.timeMs);
        const float tFeedback = std::clamp (settings.feedback, 0.0f, 1.2f);
        const float tDrive = std::clamp (settings.drive, 0.0f, 1.0f);
        const float tMix = std::clamp (settings.mix, 0.0f, 1.0f);
        const float wobble = std::clamp (settings.wobble, 0.0f, 1.0f);
        const float k = 1.0f - paramGlide, kt = 1.0f - timeGlide;
        const float fs = (float) sampleRate;

        for (int i = 0; i < n; ++i)
        {
            delaySamples += (tDelay - delaySamples) * kt;
            feedback += (tFeedback - feedback) * k;
            drive += (tDrive - drive) * k;
            mix += (tMix - mix) * k;

            wobblePhase += 0.6 / sampleRate;
            wobblePhase -= std::floor (wobblePhase);
            const float wob = wobble * fs * (1.2e-3f * std::sin (kTwoPiF * (float) wobblePhase) + 0.25e-3f * wobbleRandom.next (5.0f, fs));
            const float d = std::max (1.0f, delaySamples + wob);

            const float readL = lines[0].read (d);
            const float readR = lines[1].read (d);
            const float fbL = loop (0, readL), fbR = loop (1, readR);
            const float dryL = left[i], dryR = right[i];

            if (settings.pingPong)
            {
                lines[0].push (0.5f * (dryL + dryR) + fbR);
                lines[1].push (fbL);
            }
            else
            {
                lines[0].push (dryL + fbL);
                lines[1].push (dryR + fbR);
            }

            left[i] = dryL + mix * (readL - dryL);
            right[i] = dryR + mix * (readR - dryR);
        }
    }

private:
    float timeToSamples (float ms) const noexcept { return std::clamp (ms, 1.0f, maxTimeMs) * 0.001f * (float) sampleRate; }

    float loop (int ch, float x) noexcept
    {
        auto& c = channels[(size_t) ch];
        x = c.hp.process (hpCoeffs, x);
        x = c.lp.process (lpCoeffs, x);

        if (drive > 0.0f)
        {
            const float g = dbToGain (18.0f * drive);
            x = fastTanh (x * g) / g;
        }

        return fastTanh (feedback * x);
    }

    struct ChannelState
    {
        SvfState lp, hp;
    };

    double sampleRate = 44100.0, wobblePhase = 0.0;
    float timeGlide = 0.0f, paramGlide = 0.0f;
    EchoSettings settings;
    SvfCoeffs lpCoeffs = SvfCoeffs::make (SvfShape::lowPass, 16000.0, 0.6, 0.0, 44100.0);
    SvfCoeffs hpCoeffs = SvfCoeffs::make (SvfShape::highPass, 40.0, 0.6, 0.0, 44100.0);
    float delaySamples = 1000.0f, feedback = 0.45f, drive = 0.2f, mix = 0.3f;
    SmoothRandom wobbleRandom { 99u };
    std::array<FractionalDelay, 2> lines;
    std::array<ChannelState, 2> channels {};
};

} // namespace hl::dsp
