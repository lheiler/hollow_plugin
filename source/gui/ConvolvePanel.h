#pragma once

#include "PanelBase.h"

namespace hl::gui
{
/** Convolution editor: impulse browser, impulse/response display, size/damp/mix. */
class ConvolvePanel final : public ModulePanel
{
public:
    explicit ConvolvePanel (PanelContext& context);

    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Browser final : public juce::Component,
                          public juce::SettableTooltipClient
    {
    public:
        explicit Browser (ConvolvePanel& owner);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        struct Item { int impulse; juce::Rectangle<float> bounds; };
        struct Heading { juce::String name; juce::Rectangle<float> bounds; };
        void layoutItems();
        ConvolvePanel& panel;
        std::vector<Item> items;
        std::vector<Heading> headings;
        int hovered = -1;
    };

    class ImpulseView final : public juce::Component
    {
    public:
        explicit ImpulseView (ConvolvePanel& owner) : panel (owner) {}
        void paint (juce::Graphics&) override;
        void update();

    private:
        ConvolvePanel& panel;
        std::shared_ptr<const dsp::StereoImpulse> impulse;
        std::vector<float> envelopeL, envelopeR, response; // dB
        double seconds = 0.0;
    };

    Browser browser { *this };
    ImpulseView view { *this };
    ParamToggle reverse;
    juce::Rectangle<int> browserCard, viewCard, controlsArea, routesArea;
};

} // namespace hl::gui
