#pragma once

#include "Theme.h"
#include "plugin/AudioFifo.h"

#include <juce_dsp/juce_dsp.h>

namespace hl::gui
{
/** FFT spectrum of a mono FIFO, resampled onto log-spaced display points (UI thread only). */
class SpectrumAnalyzer
{
public:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int numPoints = 240;
    static constexpr float floorDb = -120.0f;

    SpectrumAnalyzer();

    void setSampleRate (double newRate) noexcept { sampleRate = newRate > 0.0 ? newRate : 48000.0; }

    /** Drains the FIFO and updates the smoothed spectrum. Returns true if it changed. */
    bool update (AudioFifo& fifo);

    static float frequencyAt (int point) noexcept;
    float levelAt (int point) const noexcept { return levels[(size_t) point]; }

    /** Path of the spectrum mapped onto `area` with `minDb` at the bottom and `maxDb` at the top. */
    juce::Path createPath (juce::Rectangle<float> area, float minDb, float maxDb, bool closed) const;

private:
    void computeSpectrum();

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::vector<float> ring, fftData;
    int ringPos = 0, samplesSinceLastFft = 0;
    double sampleRate = 48000.0;
    std::array<float, numPoints> levels;
};

} // namespace hl::gui
