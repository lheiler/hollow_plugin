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

/** Upward compression (as in OTT): below the threshold the level is lifted towards it by the same ratio,
    scaled by `amount` (0..1), by at most 30 dB, and not at all for near-silence (below -90 dBFS, fading in
    up to -70 dBFS) so digital silence isn't pumped up. Returns gain change in dB (>= 0). */
inline float upwardGainDb (float inDb, float thresholdDb, float ratio, float kneeDb, float amount) noexcept
{
    if (amount <= 0.0f)
        return 0.0f;

    // The downward curve mirrored around the threshold (same soft knee)
    const float lift = std::min (30.0f, -compressorGainDb (2.0f * thresholdDb - inDb, thresholdDb, ratio, kneeDb));
    const float floorFade = std::clamp ((inDb + 90.0f) / 20.0f, 0.0f, 1.0f);
    return amount * lift * floorFade;
}

struct CompressorSettings
{
    float thresholdDb = -20.0f;
    float ratio = 2.0f;
    float attackMs = 10.0f;
    float releaseMs = 150.0f;
    float kneeDb = 6.0f;
    float makeupDb = 0.0f;
    float upward = 0.0f; // 0..1, see upwardGainDb
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
        levelRelease = onePoleCoeff (0.010, sampleRate);
        setSettings (settings);
        reset();
    }

    void reset() noexcept
    {
        envelopeDb = 0.0f;
        heldReduction = 0.0f;
        meanSquare = 0.0f;
        levelEnvelope = 0.0f;
    }

    void setSettings (const CompressorSettings& s) noexcept
    {
        settings = s;
        settings.ratio = std::max (1.0f, s.ratio);
        attackCoeff = onePoleCoeff (std::max (0.05f, s.attackMs) * 0.001, sampleRate);
        releaseCoeff = onePoleCoeff (std::max (1.0f, s.releaseMs) * 0.001, sampleRate);
    }

    /** Feeds one detector sample (peak: max |x| across channels; rms: mean square)
        and returns the gain change in dB (excluding make-up): negative when compressing, positive when
        lifting (upward compression). */
    float processDb (float peakLevel, float meanSquareIn, bool useRms) noexcept
    {
        float level = peakLevel;

        if (useRms)
        {
            meanSquare = rmsCoeff * meanSquare + (1.0f - rmsCoeff) * meanSquareIn;
            level = std::sqrt (meanSquare);
        }

        const float inDb = gainToDb (level, -120.0f);
        float reduction = -compressorGainDb (inDb, settings.thresholdDb, settings.ratio, settings.kneeDb);

        // The lift reads a short peak envelope: sample by sample, every zero crossing would look like silence
        levelEnvelope = std::max (level, levelRelease * levelEnvelope);

        if (settings.upward > 0.0f)
            reduction -= upwardGainDb (gainToDb (levelEnvelope, -120.0f), settings.thresholdDb, settings.ratio, settings.kneeDb, settings.upward);

        // "Smooth decoupled" peak detector on the gain reduction: instant attack with release,
        // followed by attack smoothing. Tracks the static curve accurately for periodic signals.
        // Signed, so a lift (negative reduction) also falls with the attack and rises with the release.
        heldReduction = std::max (reduction, releaseCoeff * heldReduction + (1.0f - releaseCoeff) * reduction);
        envelopeDb = -(attackCoeff * -envelopeDb + (1.0f - attackCoeff) * heldReduction);

        if (std::abs (envelopeDb) < 1.0e-6f)
            envelopeDb = 0.0f;

        return envelopeDb;
    }

    float getMakeupDb() const noexcept { return settings.makeupDb; }
    float getCurrentReductionDb() const noexcept { return envelopeDb; }

private:
    CompressorSettings settings;
    double sampleRate = 44100.0;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f, rmsCoeff = 0.0f, levelRelease = 0.0f;
    float envelopeDb = 0.0f, heldReduction = 0.0f, meanSquare = 0.0f, levelEnvelope = 0.0f;
};

} // namespace hl::dsp
