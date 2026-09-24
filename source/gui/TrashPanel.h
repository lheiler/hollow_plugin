#pragma once

#include "PanelBase.h"

namespace hl::gui
{
/** The distortion editor: transfer curve, morph/drive pad, bands and per-band controls. */
class TrashPanel final : public ModulePanel
{
public:
    explicit TrashPanel (PanelContext& context);

    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    void selectBand (int band);

private:
    class TransferView final : public juce::Component
    {
    public:
        explicit TransferView (TrashPanel& owner) : panel (owner) {}
        void paint (juce::Graphics&) override;
        float level = 0.0f;

    private:
        TrashPanel& panel;
    };

    class MorphPad final : public juce::Component,
                           public juce::SettableTooltipClient
    {
    public:
        explicit MorphPad (TrashPanel& owner) : panel (owner) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (1.0f); }
        void setFrom (juce::Point<float>);
        TrashPanel& panel;
    };

    class BandView final : public juce::Component,
                           public juce::SettableTooltipClient
    {
    public:
        explicit BandView (TrashPanel& owner) : panel (owner) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (1.0f); }
        int crossoverAt (float x) const;
        int bandAt (float x) const;
        TrashPanel& panel;
        int dragging = -1;
    };

    class BandTabs final : public juce::Component
    {
    public:
        explicit BandTabs (TrashPanel& owner) : panel (owner) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        TrashPanel& panel;
    };

    int numBands() const;
    juce::String bandParam (const char* suffix) const { return params::id::trash (band, suffix); }

    int band = 0;
    TransferView transfer { *this };
    MorphPad pad { *this };
    BandView bandView { *this };
    BandTabs tabs { *this };
    ChoiceButtons bandCount;
    ChoiceBox algoA, algoB;
    ParamToggle autoGain;
    std::array<float, dsp::kTrashBands> levels {};
    juce::Rectangle<int> transferCard, padCard, bandsCard, bandCard, routesArea;
};

} // namespace hl::gui
