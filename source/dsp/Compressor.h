#pragma once

#include "Common.h"

namespace hl::dsp
{
/** Static soft-knee compression curve (Giannoulis, Massberg & Reiss 2012). Returns gain change in dB (<= 0). */
inline float compressorGainDb (float inDb, float thresholdDb, float ratio, float kneeDb) noexcept
{
    const float over = inDb - thresholdDb;
    const float slope = 1.0f / ratio - 1.0f;

    if (kneeDb > 0.0f && 2.0f * std::abs (over) <= kneeDb)
    {
        const float x = over + 0.5f * kneeDb;
        return slope * x * x / (2.0f * kneeDb);
    }

    return over > 0.0f ? slope * over : 0.0f;
}

struct CompressorSettings
{
    float thresholdDb = -20.0f;
    float ratio = 2.0f;
    float attackMs = 10.0f;
    float releaseMs = 150.0f;
    float kneeDb = 6.0f;
    float makeupDb = 0.0f;
};

/** Feed-forward, log-domain compressor gain stage with branching attack/release smoothing.
    Detection is external so bands can share linked stereo detection. */
class CompressorGain
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        rmsCoeff = onePoleCoeff (0.010, sampleRate);
        setSettings (settings);
        reset();
    }

    void reset() noexcept
    {
        envelopeDb = 0.0f;
        heldReduction = 0.0f;
        meanSquare = 0.0f;
    }

    void setSettings (const CompressorSettings& s) noexcept
    {
        settings = s;
        settings.ratio = std::max (1.0f, s.ratio);
        attackCoeff = onePoleCoeff (std::max (0.05f, s.attackMs) * 0.001, sampleRate);
        releaseCoeff = onePoleCoeff (std::max (1.0f, s.releaseMs) * 0.001, sampleRate);
    }

    /** Feeds one detector sample (peak: max |x| across channels; rms: mean square)
        and returns the gain change in dB (excluding make-up). */
    float processDb (float peakLevel, float meanSquareIn, bool useRms) noexcept
    {
        float level = peakLevel;

        if (useRms)
        {
            meanSquare = rmsCoeff * meanSquare + (1.0f - rmsCoeff) * meanSquareIn;
            level = std::sqrt (meanSquare);
        }

        const float inDb = gainToDb (level, -120.0f);
        const float reduction = -compressorGainDb (inDb, settings.thresholdDb, settings.ratio, settings.kneeDb);

        // "Smooth decoupled" peak detector on the gain reduction: instant attack with release,
        // followed by attack smoothing. Tracks the static curve accurately for periodic signals.
        heldReduction = std::max (reduction, releaseCoeff * heldReduction + (1.0f - releaseCoeff) * reduction);
        envelopeDb = -(attackCoeff * -envelopeDb + (1.0f - attackCoeff) * heldReduction);

        if (envelopeDb > -1.0e-6f)
            envelopeDb = 0.0f;

        return envelopeDb;
    }

    float getMakeupDb() const noexcept { return settings.makeupDb; }
    float getCurrentReductionDb() const noexcept { return envelopeDb; }

private:
    CompressorSettings settings;
    double sampleRate = 44100.0;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f, rmsCoeff = 0.0f;
    float envelopeDb = 0.0f, heldReduction = 0.0f, meanSquare = 0.0f;
};

} // namespace hl::dsp
