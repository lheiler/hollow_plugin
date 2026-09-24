#pragma once

#include "Widgets.h"
#include "plugin/PluginProcessor.h"

namespace hl::gui
{
/** Signal chain overview: click a module to edit it, drag to reorder, power button to bypass. */
class ChainStrip final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    explicit ChainStrip (HollowAudioProcessor& processor);

    std::function<void (int moduleId)> onModuleSelected;

    void setSelectedModule (int moduleId);
    int getSelectedModule() const noexcept { return selected; }

    /** Re-reads the module order from the processor (e.g. after a preset load). */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> tileBounds (int slot) const;
    int slotAt (float x) const;
    void layoutButtons();

    HollowAudioProcessor& processor;
    dsp::ModuleOrder order;
    int selected = dsp::moduleTrash;
    int draggedModule = -1;
    float dragX = 0.0f;
    bool dragMoved = false;
    std::array<std::unique_ptr<ParamToggle>, dsp::numModules> power;
};

} // namespace hl::gui
