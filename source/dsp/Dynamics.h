#pragma once

#include "Compressor.h"

namespace hl::dsp
{
struct DynamicsSettings
{
    float thresholdDb = -18.0f;
    float ratio = 4.0f;
    float attackMs = 5.0f;
    float releaseMs = 120.0f;
    float makeupDb = 0.0f;
    float gateDb = -80.0f;   // -80 = off
    float mix = 1.0f;
};

/** Gate into a stereo-linked compressor, with parallel mix. Squash the distortion's noise floor
    up (sustain) or chop it off (gate) to reshape textures. */
class DynamicsModule
{
public:
    static constexpr float gateOffDb = -79.9f;

    void prepare (double rate, int)
    {
        sampleRate = rate;
        compressor.prepare (rate);
        gateAttack = onePoleCoeff (0.0005, rate);
        gateRelease = onePoleCoeff (0.06, rate);
        detectRelease = onePoleCoeff (0.02, rate);
        holdSamples = (int) (0.012 * rate);
        mixSmoother.reset (rate, 0.03);
        mixSmoother.setCurrentAndTarget (settings.mix);
        makeupSmoother.reset (rate, 0.03);
        makeupSmoother.setCurrentAndTarget (dbToGain (settings.makeupDb));
        reset();
    }

    void reset() noexcept
    {
        compressor.reset();
        gateEnv = 0.0f;
        gateGain = 1.0f;
        gateOpen = true;
        holdCounter = 0;
        maxReduction = 0.0f;
    }

    void setSettings (const DynamicsSettings& s) noexcept
    {
        settings = s;
        CompressorSettings c;
        c.thresholdDb = s.thresholdDb;
        c.ratio = s.ratio;
        c.attackMs = s.attackMs;
        c.releaseMs = s.releaseMs;
        c.kneeDb = 6.0f;
        c.makeupDb = s.makeupDb;
        compressor.setSettings (c);
        mixSmoother.setTarget (std::clamp (s.mix, 0.0f, 1.0f));
        makeupSmoother.setTarget (dbToGain (s.makeupDb));
    }

    void process (float* left, float* right, int n) noexcept
    {
        const bool gateOn = settings.gateDb > gateOffDb;
        const float openLevel = dbToGain (settings.gateDb);
        const float closeLevel = dbToGain (settings.gateDb - 5.0f);
        const float closedGain = dbToGain (-80.0f);

        for (int i = 0; i < n; ++i)
        {
            const float l = left[i], r = right[i];
            const float peak = std::max (std::abs (l), std::abs (r));
            maxInput = std::max (maxInput, peak);
            float g = 1.0f;

            if (gateOn)
            {
                gateEnv = std::max (peak, detectRelease * gateEnv);

                if (gateEnv > openLevel)
                {
                    gateOpen = true;
                    holdCounter = holdSamples;
                }
                else if (gateEnv < closeLevel && --holdCounter <= 0)
                {
                    gateOpen = false;
                }

                const float target = gateOpen ? 1.0f : closedGain;
                const float coeff = target > gateGain ? gateAttack : gateRelease;
                gateGain = target + (gateGain - target) * coeff;
                g = gateGain;
            }
            else
            {
                gateGain = 1.0f;
            }

            const float reductionDb = compressor.processDb (peak * g, 0.0f, false);
            maxReduction = std::min (maxReduction, reductionDb);
            g *= dbToGain (reductionDb) * makeupSmoother.next();

            const float m = mixSmoother.next();
            left[i] = l + m * (l * g - l);
            right[i] = r + m * (r * g - r);
        }
    }

    float takeMaxReduction() noexcept
    {
        const float r = maxReduction;
        maxReduction = 0.0f;
        return r;
    }

    /** Peak input level since the last call (linear). */
    float takeInputPeak() noexcept
    {
        const float p = maxInput;
        maxInput = 0.0f;
        return p;
    }

    bool isGateOpen() const noexcept { return gateOpen; }

private:
    double sampleRate = 44100.0;
    DynamicsSettings settings;
    CompressorGain compressor;
    LinearSmoother mixSmoother, makeupSmoother;
    float gateAttack = 0.0f, gateRelease = 0.0f, detectRelease = 0.0f;
    float gateEnv = 0.0f, gateGain = 1.0f, maxReduction = 0.0f, maxInput = 0.0f;
    bool gateOpen = true;
    int holdSamples = 0, holdCounter = 0;
};

} // namespace hl::dsp
