#pragma once

#include "Theme.h"
#include "plugin/ModulationHost.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace hl::gui
{
using APVTS = juce::AudioProcessorValueTreeState;

/** Rotary knob with a caption above and the parameter's value text below. Shows live modulation
    as an orange arc, and offers "modulate with..." on right-click. */
class Knob final : public juce::Component
{
public:
    Knob (APVTS& state, const juce::String& paramId, const juce::String& caption, juce::Colour accent,
          ModulationHost* host = nullptr, bool bipolar = false);

    void setParameter (const juce::String& paramId);
    void setAccent (juce::Colour accent);
    void setCaption (const juce::String& caption);
    const juce::String& getParameterId() const noexcept { return paramId; }
    juce::Slider& getSlider() noexcept { return slider; }

    /** Call at UI rate: updates the modulation ring. */
    void refreshModulation();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class KnobSlider final : public juce::Slider
    {
    public:
        std::function<void()> onPopupMenu;

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && onPopupMenu != nullptr)
                onPopupMenu();
            else
                Slider::mouseDown (e);
        }
    };

    void showModulationMenu();

    APVTS& state;
    ModulationHost* host;
    KnobSlider slider;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
    juce::String paramId, caption;
    int destination = 0;
    float lastModPos = -1.0f;
    bool lastRouted = false;
};

/** Combo box bound to a choice parameter. `sectionOf` (optional) groups items under headings. */
class ChoiceBox final : public juce::Component
{
public:
    ChoiceBox (APVTS& state, const juce::String& paramId, const juce::String& caption = {},
               std::function<juce::String (int)> sectionOf = {});

    void setParameter (const juce::String& paramId);
    juce::ComboBox& getComboBox() noexcept { return box; }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    APVTS& state;
    juce::ComboBox box;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
    juce::String caption;
    std::function<juce::String (int)> sectionOf;
};

/** Toggle button bound to a bool parameter. style: "pill" (default) or "power". */
class ParamToggle final : public juce::ToggleButton
{
public:
    ParamToggle (APVTS& state, const juce::String& paramId, const juce::String& text, juce::Colour accent, const juce::String& style = "pill");

    void setParameter (const juce::String& paramId);

private:
    APVTS& state;
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

/** A choice parameter shown as a grid of buttons (segmented control). */
class ChoiceButtons final : public juce::Component,
                            public juce::SettableTooltipClient
{
public:
    ChoiceButtons (APVTS& state, const juce::String& paramId, juce::StringArray labels, int columns, juce::Colour accent);

    void setParameter (const juce::String& paramId);
    int getSelected() const noexcept { return selected; }
    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cell (int index) const;
    int indexAt (juce::Point<float> p) const;

    APVTS& state;
    juce::StringArray labels;
    int columns;
    juce::Colour accent;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::RangedAudioParameter* parameter = nullptr;
    int selected = 0, hovered = -1;
};

/** Sets a parameter from the UI as a single undoable host gesture. */
void setParameterValue (APVTS& state, const juce::String& paramId, float plainValue);

/** Current plain value of a parameter. */
float getParameterValue (APVTS& state, const juce::String& paramId);

/** Draws a frosted panel with crop marks. */
void drawPanel (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour fill = colours::panel);

/** Panel with a small title label; returns the content area below the title. */
juce::Rectangle<float> drawCard (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title,
                                 juce::Colour titleColour = colours::textDim);

} // namespace hl::gui
