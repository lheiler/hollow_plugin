#pragma once

#include "PanelBase.h"

namespace hl::gui
{
/** Right-hand column: input/output meters, a stereo scope and the global gain/mix controls. */
class MeterPanel final : public juce::Component
{
public:
    explicit MeterPanel (PanelContext& context);

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Channel
    {
        float level = -100.0f, hold = -100.0f;
        int holdFrames = 0;

        void push (float db) noexcept;
    };

    void drawMeterPair (juce::Graphics&, juce::Rectangle<float> area, const Channel& l, const Channel& r, const juce::String& title);
    void drawScope (juce::Graphics&, juce::Rectangle<float> area);

    PanelContext& ctx;
    HollowAudioProcessor& processor;
    std::array<Channel, 2> input, output;
    juce::Rectangle<int> meterArea, scopeArea, statusArea;
    Knob inputGain, mix, outputGain;
    ParamToggle autoLevel, clipGuard;
};

} // namespace hl::gui
