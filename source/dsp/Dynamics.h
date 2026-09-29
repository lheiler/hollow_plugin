#pragma once

#include "Compressor.h"
#include "Crossover.h"

namespace hl::dsp
{
constexpr int kDynamicsBands = 3;

struct DynamicsSettings
{
    float thresholdDb = -18.0f;
    float ratio = 4.0f;
    float attackMs = 5.0f;
    float releaseMs = 120.0f;
    float makeupDb = 0.0f;
    float gateDb = -80.0f;   // -80 = off
    float mix = 1.0f;
    float upward = 0.0f;     // 0..1: how far quiet parts are lifted towards the threshold
    bool multiband = false;  // three bands (like OTT / Serum's multiband compressor), each compressed on its own
    std::array<float, kDynamicsBands> bandGainDb {};
};

/** Gate into a stereo-linked compressor (downward and upward), with parallel mix. Single band, or
    three bands split at OTT's crossovers, each with its own compressor and output gain.
    Squash the distortion's noise floor up (sustain) or chop it off (gate) to reshape textures. */
class DynamicsModule
{
public:
    static constexpr float gateOffDb = -79.9f;
    static constexpr float crossovers[kDynamicsBands - 1] = { 88.3f, 2500.0f };

    void prepare (double rate, int)
    {
        sampleRate = rate;
        compressor.prepare (rate);

        for (auto& c : bandCompressors)
            c.prepare (rate);

        splitter.prepare (rate);
        splitter.setNumBands (kDynamicsBands);
        splitter.setCrossovers (crossovers, kDynamicsBands - 1);
        splitter.prepare (rate); // start at the crossovers rather than gliding to them

        gateAttack = onePoleCoeff (0.0005, rate);
        gateRelease = onePoleCoeff (0.06, rate);
        detectRelease = onePoleCoeff (0.02, rate);
        holdSamples = (int) (0.012 * rate);
        mixSmoother.reset (rate, 0.03);
        mixSmoother.setCurrentAndTarget (settings.mix);
        makeupSmoother.reset (rate, 0.03);
        makeupSmoother.setCurrentAndTarget (dbToGain (settings.makeupDb));
        modeSmoother.reset (rate, 0.02);
        modeSmoother.setCurrentAndTarget (settings.multiband ? 1.0f : 0.0f);

        for (size_t b = 0; b < kDynamicsBands; ++b)
        {
            bandGainSmoothers[b].reset (rate, 0.03);
            bandGainSmoothers[b].setCurrentAndTarget (dbToGain (settings.bandGainDb[b]));
        }

        reset();
    }

    void reset() noexcept
    {
        compressor.reset();

        for (auto& c : bandCompressors)
            c.reset();

        splitter.reset();
        gateEnv = 0.0f;
        gateGain = 1.0f;
        gateOpen = true;
        holdCounter = 0;
        maxReduction = 0.0f;
        bandPeaks.fill (0.0f);
        bandGains.fill (0.0f);
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
        c.upward = std::clamp (s.upward, 0.0f, 1.0f);
        compressor.setSettings (c);

        for (auto& bc : bandCompressors)
            bc.setSettings (c);

        mixSmoother.setTarget (std::clamp (s.mix, 0.0f, 1.0f));
        makeupSmoother.setTarget (dbToGain (s.makeupDb));
        modeSmoother.setTarget (s.multiband ? 1.0f : 0.0f);

        for (size_t b = 0; b < kDynamicsBands; ++b)
            bandGainSmoothers[b].setTarget (dbToGain (s.bandGainDb[b]));
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
            float gate = 1.0f;

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
                gate = gateGain;
            }
            else
            {
                gateGain = 1.0f;
            }

            const float makeup = makeupSmoother.next();
            const float m = mixSmoother.next();
            const float mode = modeSmoother.next();
            float outL = 0.0f, outR = 0.0f, multiL = 0.0f, multiR = 0.0f;

            if (mode < 1.0f)
            {
                const float changeDb = compressor.processDb (peak * gate, 0.0f, false);
                maxReduction = std::min (maxReduction, changeDb);
                const float g = gate * dbToGain (changeDb) * makeup;
                outL = l + m * (l * g - l);
                outR = r + m * (r * g - r);
            }

            // Multiband: the splitter always runs, so switching modes never starts it cold
            float bandL[kDynamicsBands], bandR[kDynamicsBands];
            splitter.tick();
            splitter.split (0, l, bandL);
            splitter.split (1, r, bandR);

            if (mode > 0.0f)
            {
                for (size_t b = 0; b < kDynamicsBands; ++b)
                {
                    const float bl = bandL[b], br = bandR[b];
                    const float bandPeak = std::max (std::abs (bl), std::abs (br));
                    bandPeaks[b] = std::max (bandPeaks[b], bandPeak);
                    bandGains[b] = bandCompressors[b].processDb (bandPeak * gate, 0.0f, false);
                    const float g = gate * dbToGain (bandGains[b]) * makeup * bandGainSmoothers[b].next();

                    // mixed band by band, so the dry part stays in phase with the split
                    multiL += bl + m * (bl * g - bl);
                    multiR += br + m * (br * g - br);
                }
            }

            left[i] = outL + mode * (multiL - outL);
            right[i] = outR + mode * (multiR - outR);
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

    /** Multiband: a band's peak level since the last call (linear) and its current gain change (dB, signed). */
    float takeBandPeak (int band) noexcept
    {
        const float p = bandPeaks[(size_t) band];
        bandPeaks[(size_t) band] = 0.0f;
        return p;
    }

    float getBandGainDb (int band) const noexcept { return bandGains[(size_t) band]; }

    bool isGateOpen() const noexcept { return gateOpen; }

private:
    double sampleRate = 44100.0;
    DynamicsSettings settings;
    CompressorGain compressor;
    std::array<CompressorGain, kDynamicsBands> bandCompressors;
    MultibandSplitter splitter;
    LinearSmoother mixSmoother, makeupSmoother, modeSmoother;
    std::array<LinearSmoother, kDynamicsBands> bandGainSmoothers;
    float gateAttack = 0.0f, gateRelease = 0.0f, detectRelease = 0.0f;
    float gateEnv = 0.0f, gateGain = 1.0f, maxReduction = 0.0f, maxInput = 0.0f;
    std::array<float, kDynamicsBands> bandPeaks {}, bandGains {};
    bool gateOpen = true;
    int holdSamples = 0, holdCounter = 0;
};

} // namespace hl::dsp
