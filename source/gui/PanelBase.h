#pragma once

#include "SpectrumAnalyzer.h"
#include "Widgets.h"
#include "plugin/PluginProcessor.h"

namespace hl::gui
{
/** Recent output audio for the scope displays (UI thread only). */
class ScopeData
{
public:
    static constexpr int numColumns = 600;
    static constexpr int recentSize = 2048;

    void setSampleRate (double rate) noexcept { samplesPerColumn = juce::jmax (16, (int) (rate * 3.0 / numColumns)); } // ~3 s on screen

    /** Drains the stereo FIFO. Returns true if anything arrived. */
    bool pull (AudioFifo& fifo);

    struct Column
    {
        float min = 0.0f, max = 0.0f;
    };

    /** Column `age` steps back from the newest one (0 = newest). */
    const Column& column (int age) const noexcept { return columns[(size_t) ((columnPos - 1 - age + 2 * numColumns) % numColumns)]; }

    float recentLeft (int age) const noexcept { return left[(size_t) ((recentPos - 1 - age + 2 * recentSize) % recentSize)]; }
    float recentRight (int age) const noexcept { return right[(size_t) ((recentPos - 1 - age + 2 * recentSize) % recentSize)]; }
    int getColumnsAdded() const noexcept { return columnsAdded; }

private:
    std::array<Column, numColumns> columns {};
    std::array<float, recentSize> left {}, right {};
    int columnPos = 0, recentPos = 0, samplesPerColumn = 240, counter = 0, columnsAdded = 0;
    Column building;
};

struct PanelContext
{
    HollowAudioProcessor& processor;
    SpectrumAnalyzer& spectrum;
    ScopeData& scope;
};

/** Which module a modulation destination belongs to (-1 for global ones). */
int destinationModule (int destination) noexcept;

/** Base for the module editors: owns knobs, refreshes their modulation rings, lists routes. */
class ModulePanel : public juce::Component
{
public:
    ModulePanel (PanelContext& context, int moduleId);

    /** Called at UI rate while visible. */
    virtual void refresh();

protected:
    Knob& addKnob (const juce::String& paramId, const juce::String& caption, bool bipolar = false);

    /** Lays knobs out in equal cells across `area` (optionally in several rows). */
    static void layoutGrid (juce::Rectangle<int> area, const std::vector<juce::Component*>& items, int columns);

    /** Lists the matrix routes that act on this module. */
    void drawRoutes (juce::Graphics& g, juce::Rectangle<float> area) const;

    /** Settings as the audio thread currently sees them (base parameters + live modulation). */
    dsp::ChainSettings modulatedSettings() const;

    PanelContext& ctx;
    HollowAudioProcessor& processor;
    APVTS& state;
    const int moduleId;
    const juce::Colour accent;
    std::vector<std::unique_ptr<Knob>> knobs;
};

} // namespace hl::gui
