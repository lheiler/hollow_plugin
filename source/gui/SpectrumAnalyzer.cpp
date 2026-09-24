#include "SpectrumAnalyzer.h"

namespace hl::gui
{
using namespace juce;

SpectrumAnalyzer::SpectrumAnalyzer()
    : ring ((size_t) fftSize, 0.0f), fftData ((size_t) fftSize * 2, 0.0f)
{
    levels.fill (floorDb);
}

float SpectrumAnalyzer::frequencyAt (int point) noexcept
{
    return 20.0f * std::pow (1000.0f, (float) point / (float) (numPoints - 1));
}

bool SpectrumAnalyzer::update (AudioFifo& fifo)
{
    int received = 0;

    fifo.pop ([&] (const float* frame)
    {
        ring[(size_t) ringPos] = frame[0];
        ringPos = (ringPos + 1) % fftSize;
        ++received;
    });

    samplesSinceLastFft += received;

    if (samplesSinceLastFft < fftSize / 4)
    {
        if (received == 0)
        {
            // No audio flowing: let the display fall back to silence
            bool changed = false;

            for (auto& l : levels)
            {
                if (l > floorDb)
                {
                    l = jmax (floorDb, l - 1.5f);
                    changed = true;
                }
            }

            return changed;
        }

        return false;
    }

    samplesSinceLastFft = 0;
    computeSpectrum();
    return true;
}

void SpectrumAnalyzer::computeSpectrum()
{
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = ring[(size_t) ((ringPos + i) % fftSize)];

    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    // Full-scale sine through a Hann window peaks at N/4
    const float norm = 4.0f / (float) fftSize;
    const float binHz = (float) sampleRate / (float) fftSize;
    const int maxBin = fftSize / 2 - 1;

    const auto magnitudeAt = [&] (float bin)
    {
        const int b0 = jlimit (0, maxBin, (int) bin);
        const float frac = bin - (float) b0;
        return fftData[(size_t) b0] * (1.0f - frac) + fftData[(size_t) jmin (maxBin, b0 + 1)] * frac;
    };

    for (int p = 0; p < numPoints; ++p)
    {
        const float f = frequencyAt (p);
        const float lo = f * std::pow (1000.0f, -0.5f / (numPoints - 1)) / binHz;
        const float hi = f * std::pow (1000.0f, 0.5f / (numPoints - 1)) / binHz;

        float mag = 0.0f;

        if (hi - lo < 1.0f)
        {
            mag = magnitudeAt (f / binHz);
        }
        else
        {
            for (int b = (int) std::ceil (lo); b <= jmin (maxBin, (int) hi); ++b)
                mag = jmax (mag, fftData[(size_t) b]);
        }

        // +4.5 dB/octave tilt around 1 kHz so that pink-ish music reads flat
        const float db = Decibels::gainToDecibels (mag * norm, floorDb) + 4.5f * std::log2 (f / 1000.0f);
        auto& level = levels[(size_t) p];
        level = db > level ? level + (db - level) * 0.6f : jmax (db, level - 1.2f);
    }
}

Path SpectrumAnalyzer::createPath (Rectangle<float> area, float minDb, float maxDb, bool closed) const
{
    Path p;
    const auto yFor = [&] (float db) { return jmap (jlimit (minDb, maxDb, db), minDb, maxDb, area.getBottom(), area.getY()); };

    for (int i = 0; i < numPoints; ++i)
    {
        const float x = frequencyToX (frequencyAt (i), area);
        const float y = yFor (levels[(size_t) i]);

        if (i == 0)
        {
            if (closed)
            {
                p.startNewSubPath (x, area.getBottom());
                p.lineTo (x, y);
            }
            else
            {
                p.startNewSubPath (x, y);
            }
        }
        else
        {
            p.lineTo (x, y);
        }
    }

    if (closed)
    {
        p.lineTo (area.getRight(), area.getBottom());
        p.closeSubPath();
    }

    return p;
}

} // namespace hl::gui
