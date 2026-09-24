#pragma once

#include "Crossover.h"
#include "Oversampler.h"
#include "Shapers.h"

namespace hl::dsp
{
constexpr int kTrashBands = 3;

struct TrashBandSettings
{
    int algoA = (int) Algo::tube;
    int algoB = (int) Algo::fuzz;
    float morph = 0.0f;    // 0 = A, 1 = B
    float driveDb = 12.0f;
    float bias = 0.0f;     // -1 .. 1
    float tone = 1.0f;     // 0 = dark (800 Hz), 1 = open
    float mix = 1.0f;
    float levelDb = 0.0f;
};

struct TrashSettings
{
    int numBands = 1;
    std::array<float, kTrashBands - 1> crossovers { 250.0f, 2500.0f };
    bool autoGain = true;
    std::array<TrashBandSettings, kTrashBands> bands {};
};

/** Tone control cutoff: 800 Hz .. 40 kHz (fully open is far above the audio band). */
inline double trashToneCutoff (float tone) noexcept
{
    return 800.0 * std::pow (50.0, (double) std::clamp (tone, 0.0f, 1.0f));
}

/** Multiband waveshaping distortion at 4x oversampling. Each band blends two algorithms. */
class TrashModule
{
public:
    void prepare (double rate, int)
    {
        sampleRate = rate;
        highRate = rate * factor;

        for (auto& o : os)
        {
            o.setFactor (factor);
            o.prepare();
        }

        splitter.prepare (highRate);

        for (auto& dc : dcBlockers)
            dc.prepare (rate, 8.0);

        for (auto& b : bands)
        {
            b.duck.reset (highRate, 0.004);
            b.duck.setCurrentAndTarget (1.0f);
            b.activeA = b.pendingA;
            b.activeB = b.pendingB;
        }

        applySettings (settings, 1, true);
        reset();
    }

    void reset()
    {
        for (auto& dc : dcBlockers)
            dc.reset();

        for (auto& o : os)
            o.reset();

        splitter.reset();

        for (auto& b : bands)
        {
            for (auto& c : b.channels)
                c = {};

            b.msDry = b.msWet = b.blockDry = b.blockWet = 0.0;
            b.blockCount = 0;
            b.dryGain.snap (1.0f);
        }

        for (auto& p : peaks)
            p = 0.0f;
    }

    /** Constant (the 8x round trip) whatever the oversampling factor. */
    int getLatency() const noexcept { return os[0].getLatency(); }

    /** 1, 2, 4 or 8. Call between blocks; changing it resets the distortion's memories (not the latency). */
    void setOversampling (int newFactor) noexcept
    {
        for (auto& o : os)
            o.setFactor (newFactor);

        if (os[0].getFactor() == factor)
            return;

        factor = os[0].getFactor();
        highRate = sampleRate * factor;
        splitter.prepare (highRate);

        for (auto& b : bands)
        {
            b.duck.reset (highRate, 0.004);

            for (auto& c : b.channels)
                c = {};
        }
    }

    int getOversampling() const noexcept { return factor; }

    /** Call once per control block, before process(), with that block's length. */
    void setSettings (const TrashSettings& s, int blockLength) noexcept
    {
        applySettings (s, blockLength, false);
    }

    void process (float* left, float* right, int n) noexcept
    {
        const int numBands = settings.numBands;
        float* chans[2] = { left, right };
        std::array<std::array<float, VariableOversampler::maxFactor>, 2> hi;

        for (int i = 0; i < n; ++i)
        {
            os[0].upsample (left[i], hi[0].data());
            os[1].upsample (right[i], hi[1].data());

            for (int k = 0; k < factor; ++k)
            {
                std::array<float, kTrashBands> bl {}, br {};

                if (numBands > 1)
                {
                    splitter.tick();
                    splitter.split (0, hi[0][(size_t) k], bl.data());
                    splitter.split (1, hi[1][(size_t) k], br.data());
                }
                else
                {
                    bl[0] = hi[0][(size_t) k];
                    br[0] = hi[1][(size_t) k];
                }

                float sumL = 0.0f, sumR = 0.0f;

                for (int b = 0; b < numBands; ++b)
                {
                    auto& band = bands[(size_t) b];
                    band.advance();
                    peaks[(size_t) b] = std::max (peaks[(size_t) b], std::max (std::abs (bl[(size_t) b]), std::abs (br[(size_t) b])));
                    sumL += band.process (0, bl[(size_t) b]);
                    sumR += band.process (1, br[(size_t) b]);
                }

                hi[0][(size_t) k] = sumL;
                hi[1][(size_t) k] = sumR;
            }

            for (int ch = 0; ch < 2; ++ch)
                chans[ch][i] = dcBlockers[(size_t) ch].process (os[(size_t) ch].downsample (hi[(size_t) ch].data()));
        }
    }

    /** Peak band input levels since the last call (for the curve display). */
    std::array<float, kTrashBands> takePeaks() noexcept
    {
        auto p = peaks;
        peaks.fill (0.0f);
        return p;
    }

    /** Transfer curve of one band as the display should show it: normalised input -> output. */
    static float curve (const TrashBandSettings& b, bool autoGain, float x) noexcept
    {
        const auto shapeOne = [&] (int algoIndex)
        {
            const auto a = (Algo) std::clamp (algoIndex, 0, kNumAlgos - 1);
            const auto p = ShapeParams::make (b.driveDb, 192000.0);
            const float bias = b.bias * kBiasScale;
            const float y = shapeStatic (a, x * dbToGain (preGainDb (a, b.driveDb)) + bias, p) - shapeStatic (a, bias, p);
            return y;
        };

        const float yA = b.morph < 1.0f ? shapeOne (b.algoA) : 0.0f;
        const float yB = b.morph > 0.0f ? shapeOne (b.algoB) : 0.0f;
        const float y = yA + b.morph * (yB - yA);
        return y * dbToGain (b.levelDb + (autoGain ? compensation (b) : 0.0f));
    }

    static float compensation (const TrashBandSettings& b) noexcept
    {
        const auto& table = AutoGainTable::get();
        const auto a = (Algo) std::clamp (b.algoA, 0, kNumAlgos - 1);
        const auto bb = (Algo) std::clamp (b.algoB, 0, kNumAlgos - 1);
        const float ca = table.compensationDb (a, b.driveDb), cb = table.compensationDb (bb, b.driveDb);
        return ca + b.morph * (cb - ca);
    }

    static constexpr float kBiasScale = 0.5f;

private:
    struct ChannelState
    {
        ShaperState a, b;
        SvfState tone;
    };

    struct Band
    {
        int pendingA = 0, pendingB = 10, activeA = 0, activeB = 10;
        ShapeParams paramsA, paramsB;
        BlockRamp gainA, gainB, morph, bias, dcA, dcB, mix, outGain, dryGain;
        LinearSmoother duck;
        SvfCoeffs toneCoeffs = SvfCoeffs::make (SvfShape::lowPass, 20000.0, 0.7071, 0.0, 192000.0);
        std::array<ChannelState, 2> channels {};
        float curMorph = 0.0f, curGainA = 1.0f, curGainB = 1.0f, curBias = 0.0f, curDcA = 0.0f, curDcB = 0.0f;
        float curMix = 1.0f, curOut = 1.0f, curDuck = 1.0f, curDry = 1.0f;

        // Loudness of the clean and the distorted signal, for the equal-loudness blend
        double blockDry = 0.0, blockWet = 0.0, msDry = 0.0, msWet = 0.0;
        int blockCount = 0;

        void advance() noexcept
        {
            curGainA = gainA.next();
            curGainB = gainB.next();
            curMorph = morph.next();
            curBias = bias.next();
            curDcA = dcA.next();
            curDcB = dcB.next();
            curMix = mix.next();
            curOut = outGain.next();
            curDuck = duck.next();
            curDry = dryGain.next();
        }

        float process (int ch, float x) noexcept
        {
            auto& c = channels[(size_t) ch];
            float wet;

            if (curMorph <= 0.0f)
            {
                wet = shape ((Algo) activeA, x * curGainA + curBias, paramsA, c.a) - curDcA;
            }
            else if (curMorph >= 1.0f)
            {
                wet = shape ((Algo) activeB, x * curGainB + curBias, paramsB, c.b) - curDcB;
            }
            else
            {
                const float ya = shape ((Algo) activeA, x * curGainA + curBias, paramsA, c.a) - curDcA;
                const float yb = shape ((Algo) activeB, x * curGainB + curBias, paramsB, c.b) - curDcB;
                wet = ya + curMorph * (yb - ya);
            }

            wet = c.tone.process (toneCoeffs, wet) * curOut;
            blockDry += (double) x * x;
            blockWet += (double) wet * wet;
            ++blockCount;

            // The clean part of a parallel blend follows the distorted part's loudness, so pushing the
            // input makes the band dirtier instead of letting the clean signal take over
            // (fading in over the first quarter of the mix knob, so a touch of dirt keeps the clean at unity)
            const float follow = std::min (1.0f, 4.0f * curMix * curDuck);
            const float dry = x * (1.0f - follow * (1.0f - curDry));
            return dry + curDuck * curMix * (wet - dry);
        }

        /** Once per control block: updates the clean path's gain from the measured loudness. */
        float updateDryGain (double highRate) noexcept
        {
            if (blockCount > 0)
            {
                const double a = std::exp (-(double) blockCount / (0.3 * highRate));
                msDry = a * msDry + (1.0 - a) * blockDry / blockCount;
                msWet = a * msWet + (1.0 - a) * blockWet / blockCount;
            }

            blockDry = blockWet = 0.0;
            blockCount = 0;

            if (msDry < 1.0e-9)
                return dryGain.getTarget(); // silence: hold

            // Only ever turns the clean part down (never boosts noise), at most by 36 dB
            return (float) std::clamp (std::sqrt (msWet / msDry), 0.015848931, 1.0);
        }
    };

    void applySettings (const TrashSettings& s, int blockLength, bool snap) noexcept
    {
        settings = s;
        settings.numBands = std::clamp (s.numBands, 1, kTrashBands);
        splitter.setNumBands (settings.numBands);
        splitter.setCrossovers (settings.crossovers.data(), kTrashBands - 1);

        const int len = blockLength * factor;

        for (int i = 0; i < kTrashBands; ++i)
        {
            auto& b = bands[(size_t) i];
            const auto& t = settings.bands[(size_t) i];
            b.pendingA = std::clamp (t.algoA, 0, kNumAlgos - 1);
            b.pendingB = std::clamp (t.algoB, 0, kNumAlgos - 1);

            if (snap)
            {
                b.activeA = b.pendingA;
                b.activeB = b.pendingB;
            }
            else if (b.pendingA != b.activeA || b.pendingB != b.activeB)
            {
                // Algorithm change: duck to dry, swap while silent, come back
                if (b.duck.getCurrent() <= 0.0f && ! b.duck.isSmoothing())
                {
                    b.activeA = b.pendingA;
                    b.activeB = b.pendingB;

                    for (auto& c : b.channels)
                    {
                        c.a.reset();
                        c.b.reset();
                    }

                    b.duck.setTarget (1.0f);
                }
                else
                {
                    b.duck.setTarget (0.0f);
                }
            }
            else if (b.duck.getTarget() < 1.0f)
            {
                b.duck.setTarget (1.0f); // changed back before the swap happened
            }

            const auto algoA = (Algo) b.activeA, algoB = (Algo) b.activeB;
            b.paramsA = ShapeParams::make (t.driveDb, highRate);
            b.paramsB = b.paramsA;

            const float bias = std::clamp (t.bias, -1.0f, 1.0f) * kBiasScale;
            const float morph = std::clamp (t.morph, 0.0f, 1.0f);
            TrashBandSettings active = t;
            active.algoA = b.activeA;
            active.algoB = b.activeB;
            active.morph = morph;
            const float comp = settings.autoGain ? compensation (active) : 0.0f;

            const float targets[] = { dbToGain (preGainDb (algoA, t.driveDb)), dbToGain (preGainDb (algoB, t.driveDb)), morph, bias,
                                      shapeStatic (algoA, bias, b.paramsA), shapeStatic (algoB, bias, b.paramsB),
                                      std::clamp (t.mix, 0.0f, 1.0f), dbToGain (t.levelDb + comp) };
            BlockRamp* ramps[] = { &b.gainA, &b.gainB, &b.morph, &b.bias, &b.dcA, &b.dcB, &b.mix, &b.outGain };

            if (snap)
                b.dryGain.snap (1.0f);
            else
                b.dryGain.setTarget (b.updateDryGain (highRate), len);

            for (int r = 0; r < 8; ++r)
            {
                if (snap)
                    ramps[r]->snap (targets[r]);
                else
                    ramps[r]->setTarget (targets[r], len);
            }

            b.toneCoeffs = SvfCoeffs::make (SvfShape::lowPass, std::min (trashToneCutoff (t.tone), highRate * 0.45), 0.7071, 0.0, highRate);
        }
    }

    double sampleRate = 44100.0, highRate = 176400.0;
    int factor = 4;
    TrashSettings settings;
    std::array<VariableOversampler, 2> os;
    MultibandSplitter splitter;
    std::array<Band, kTrashBands> bands {};
    std::array<DcBlocker, 2> dcBlockers;
    std::array<float, kTrashBands> peaks {};
};

} // namespace hl::dsp
