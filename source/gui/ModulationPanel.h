#pragma once

#include "EffectPanels.h"

namespace hl::gui
{
/** Bottom strip: two LFOs, the envelope follower, the macro and the modulation matrix. */
class ModulationPanel final : public juce::Component
{
public:
    explicit ModulationPanel (PanelContext& context);

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct MatrixRow
    {
        std::unique_ptr<ChoiceBox> source, destination;
        juce::Slider amount;
        std::unique_ptr<APVTS::SliderAttachment> attachment;
    };

    void drawLfo (juce::Graphics&, juce::Rectangle<float>, int index);
    void drawEnvelope (juce::Graphics&, juce::Rectangle<float>);
    Knob& addKnob (const juce::String& paramId, const juce::String& caption, bool bipolar = false);

    HollowAudioProcessor& processor;
    APVTS& state;

    std::array<Display, 2> lfoViews;
    std::array<std::unique_ptr<ChoiceBox>, 2> lfoShapes;
    std::array<std::unique_ptr<SyncableRate>, 2> lfoRates;
    Display envView;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::array<MatrixRow, dsp::kModSlots> rows;
    std::array<float, 120> envHistory {};
    int envPos = 0;
    std::array<juce::Rectangle<int>, 2> lfoCards;
    juce::Rectangle<int> envCard, macroCard, matrixCard;
};

} // namespace hl::gui
