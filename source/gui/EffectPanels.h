#pragma once

#include "PanelBase.h"

namespace hl::gui
{
/** A small live display owned by a panel; the panel decides what to draw. */
class Display final : public juce::Component
{
public:
    std::function<void (juce::Graphics&, juce::Rectangle<float>)> draw;
    void paint (juce::Graphics& g) override
    {
        if (draw != nullptr)
            draw (g, getLocalBounds().toFloat());
    }
};

/** Rate knob that turns into a note-division box when tempo sync is on. */
class SyncableRate final : public juce::Component
{
public:
    SyncableRate (Knob& rateKnob, APVTS& state, const juce::String& syncId, const juce::String& divisionId, juce::Colour accent);

    void refresh();
    void resized() override;

private:
    Knob& rate;
    APVTS& state;
    juce::String syncId;
    ChoiceBox division;
    ParamToggle sync;
};

//==============================================================================
class MotionPanel final : public ModulePanel
{
public:
    explicit MotionPanel (PanelContext& context);
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void drawView (juce::Graphics&, juce::Rectangle<float>);

    ChoiceButtons modes;
    Display view;
    std::unique_ptr<SyncableRate> rate;
    juce::Rectangle<int> viewCard, controlCard, routesArea;
};

//==============================================================================
class DegradePanel final : public ModulePanel
{
public:
    explicit DegradePanel (PanelContext& context);
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void drawView (juce::Graphics&, juce::Rectangle<float>);

    Display view;
    static constexpr int historySize = 300;
    std::array<float, historySize> pitch {}, dropout {};
    std::array<bool, historySize> glitch {};
    int historyPos = 0;
    juce::Rectangle<int> viewCard, controlCard, routesArea;
};

//==============================================================================
class DynamicsPanel final : public ModulePanel
{
public:
    explicit DynamicsPanel (PanelContext& context);
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void drawView (juce::Graphics&, juce::Rectangle<float>);

    Display view;
    float inputDb = -100.0f, reductionDb = 0.0f;
    std::array<float, 200> grHistory {};
    int historyPos = 0;
    juce::Rectangle<int> viewCard, controlCard, routesArea;
};

//==============================================================================
class EchoPanel final : public ModulePanel
{
public:
    explicit EchoPanel (PanelContext& context);
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void drawView (juce::Graphics&, juce::Rectangle<float>);

    Display view;
    std::unique_ptr<SyncableRate> time;
    ParamToggle pingPong;
    juce::Rectangle<int> viewCard, controlCard, routesArea;
};

} // namespace hl::gui
