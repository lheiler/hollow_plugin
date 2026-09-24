#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace hl::gui
{
/** Supervisor's "digital ethereal" palette: white and icy steel blues, deep navy ink and one warm
    orange accent. In Hollow the orange always means "selected" or "modulated". */
namespace colours
{
    const juce::Colour background { 0xffeef2f6 };
    const juce::Colour panel { 0xfffcfdfe };
    const juce::Colour panelRaised { 0xfff2f5f9 };
    const juce::Colour well { 0xffe3e9ef };
    const juce::Colour outline { 0xffc3cedb };
    const juce::Colour grid { 0xffe5eaf0 };
    const juce::Colour gridStrong { 0xffcfd8e2 };
    const juce::Colour ink { 0xff1b2a40 };
    const juce::Colour text { 0xff142031 };
    const juce::Colour textDim { 0xff53647a };
    const juce::Colour textFaint { 0xff93a2b4 };
    const juce::Colour steel { 0xff3b76b3 };
    const juce::Colour ice { 0xff9fbad6 };
    const juce::Colour accent { 0xfff07f3c };
    const juce::Colour danger { 0xffe2512d };
    const juce::Colour rust { 0xffc8442a };

    juce::Colour forModule (int moduleId);
    juce::Colour forBand (int band);
} // namespace colours

/** Embedded typefaces: Oxanium for the interface, Martian Mono for readouts (both SIL OFL 1.1).
    Shared by all editors; the LookAndFeel keeps them alive. */
struct EmbeddedTypefaces
{
    EmbeddedTypefaces();
    juce::Typeface::Ptr light, regular, semiBold, mono;
};

juce::Font font (float height, bool bold = false);
juce::Font lightFont (float height);
juce::Font monoFont (float height);

/** Small, letter-spaced uppercase label. */
void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                juce::Justification justification = juce::Justification::centredLeft,
                juce::Colour colour = colours::textDim, float height = 10.5f);

/** Frequency <-> x mapping over 20 Hz .. 20 kHz on a log scale. */
float frequencyToX (float hz, juce::Rectangle<float> area) noexcept;
float xToFrequency (float x, juce::Rectangle<float> area) noexcept;

/** Draws frequency grid lines and labels. */
void drawFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> area, bool withLabels);

/** Renders the backdrop (motion-blurred shards, slightly torn like a damaged tape frame). */
juce::Image renderBackdrop (int width, int height, float scale);

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;
    juce::Font getLabelFont (juce::Label&) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;

    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

private:
    juce::SharedResourcePointer<EmbeddedTypefaces> typefaces;
};

} // namespace hl::gui
