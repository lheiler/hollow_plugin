#pragma once

#include "PanelBase.h"

namespace hl::gui
{
/** Filter 1 / Filter 2 editor: response over the live spectrum, draggable cutoff/resonance node. */
class FilterPanel final : public ModulePanel
{
public:
    FilterPanel (PanelContext& context, int filterIndex);

    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Graph final : public juce::Component,
                        public juce::SettableTooltipClient
    {
    public:
        explicit Graph (FilterPanel& owner) : panel (owner) {}

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (1.0f, 4.0f); }
        float yFor (float gain) const;

        FilterPanel& panel;
        float startReso = 0.0f;
        bool dragging = false;
    };

    const int index;
    Graph graph { *this };
    ChoiceButtons types;
    juce::Rectangle<int> graphCard, controlCard, routesArea;
};

} // namespace hl::gui
