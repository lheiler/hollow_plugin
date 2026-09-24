#include "Widgets.h"
#include "plugin/Parameters.h"

namespace hl::gui
{
using namespace juce;

Knob::Knob (APVTS& s, const String& pid, const String& text, Colour accent, ModulationHost* h, bool bipolar)
    : state (s), host (h), caption (text)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setColour (Slider::rotarySliderFillColourId, accent);
    slider.getProperties().set ("bipolar", bipolar);
    slider.onValueChange = [this] { repaint(); };
    slider.onPopupMenu = [this] { showModulationMenu(); };
    addAndMakeVisible (slider);
    setParameter (pid);
}

void Knob::setParameter (const String& pid)
{
    attachment.reset();
    paramId = pid;
    destination = params::destinationForParameter (pid);

    if (auto* p = state.getParameter (paramId))
    {
        attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);
        slider.setTooltip (p->getName (64) + (destination != 0 && host != nullptr ? "  (right-click to modulate)" : ""));
    }

    lastModPos = -1.0f;
    refreshModulation();
    repaint();
}

void Knob::setAccent (Colour accent)
{
    slider.setColour (Slider::rotarySliderFillColourId, accent);
    slider.repaint();
}

void Knob::setCaption (const String& text)
{
    caption = text;
    repaint();
}

void Knob::refreshModulation()
{
    auto& props = slider.getProperties();

    if (host == nullptr || destination == 0)
        return;

    const bool routed = host->isModulated (destination);
    float modPos = -1.0f;

    if (routed)
        if (auto* p = state.getParameter (paramId))
        {
            const float offset = host->getModulationOffset (destination);

            if (std::abs (offset) > 1.0e-4f)
                modPos = jlimit (0.0f, 1.0f, p->getValue() + offset);
        }

    if (routed == lastRouted && std::abs (modPos - lastModPos) < 0.002f)
        return;

    lastRouted = routed;
    lastModPos = modPos;
    props.set ("modRouted", routed);

    if (modPos >= 0.0f)
        props.set ("modPos", modPos);
    else
        props.remove ("modPos");

    slider.repaint();
}

void Knob::showModulationMenu()
{
    if (host == nullptr || destination == 0)
        return;

    PopupMenu menu;
    menu.addSectionHeader ("Modulate " + String (dsp::destinationName (destination)));

    for (int s = 1; s < dsp::numSources; ++s)
        menu.addItem (s, "with " + String (dsp::sourceName (s)));

    menu.addSeparator();
    menu.addItem (100, "Remove modulation", host->isModulated (destination));

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&slider),
                        [safe = Component::SafePointer<Knob> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            if (result == 100)
                                safe->host->clearModulation (safe->destination);
                            else
                                safe->host->addModulation (safe->destination, result);

                            safe->refreshModulation();
                        });
}

void Knob::paint (Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const float textH = jlimit (11.0f, 14.0f, area.getHeight() * 0.16f);

    drawLabel (g, caption, area.removeFromTop (textH + 2.0f), Justification::centred, colours::textDim, textH * 0.78f);

    g.setColour (isEnabled() ? colours::text : colours::textFaint);
    g.setFont (font (textH));
    g.drawText (slider.getTextFromValue (slider.getValue()), area.removeFromBottom (textH + 2.0f), Justification::centred, false);
}

void Knob::resized()
{
    auto area = getLocalBounds();
    const int textH = (int) jlimit (11.0f, 14.0f, (float) area.getHeight() * 0.16f) + 2;
    area.removeFromTop (textH);
    area.removeFromBottom (textH);
    const int size = jmin (area.getWidth(), area.getHeight());
    slider.setBounds (area.withSizeKeepingCentre (size, size));
}

//==============================================================================
ChoiceBox::ChoiceBox (APVTS& s, const String& pid, const String& text, std::function<String (int)> sections)
    : state (s), caption (text), sectionOf (std::move (sections))
{
    addAndMakeVisible (box);
    setParameter (pid);
}

void ChoiceBox::setParameter (const String& pid)
{
    attachment.reset();
    box.clear (dontSendNotification);

    if (auto* p = dynamic_cast<AudioParameterChoice*> (state.getParameter (pid)))
    {
        String lastSection;

        for (int i = 0; i < p->choices.size(); ++i)
        {
            if (sectionOf != nullptr)
            {
                const auto section = sectionOf (i);

                if (section != lastSection)
                {
                    box.addSectionHeading (section);
                    lastSection = section;
                }
            }

            box.addItem (p->choices[i], i + 1);
        }

        attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, pid, box);
        box.setTooltip (p->getName (64));
    }
}

void ChoiceBox::paint (Graphics& g)
{
    if (caption.isNotEmpty())
        drawLabel (g, caption, getLocalBounds().removeFromTop (16).toFloat(), Justification::centredLeft, colours::textDim, 9.5f);
}

void ChoiceBox::resized()
{
    auto area = getLocalBounds();

    if (caption.isNotEmpty())
        area.removeFromTop (16);

    box.setBounds (area.withSizeKeepingCentre (area.getWidth(), jmin (area.getHeight(), 26)));
}

//==============================================================================
ParamToggle::ParamToggle (APVTS& s, const String& pid, const String& text, Colour accent, const String& style)
    : ToggleButton (text), state (s)
{
    setColour (ToggleButton::tickColourId, accent);
    getProperties().set ("style", style);
    setParameter (pid);
}

void ParamToggle::setParameter (const String& pid)
{
    attachment.reset();

    if (auto* p = state.getParameter (pid))
    {
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, pid, *this);
        setTooltip (p->getName (64));
    }
}

//==============================================================================
ChoiceButtons::ChoiceButtons (APVTS& s, const String& pid, StringArray l, int cols, Colour a)
    : state (s), labels (std::move (l)), columns (jmax (1, cols)), accent (a)
{
    setParameter (pid);
}

void ChoiceButtons::setParameter (const String& pid)
{
    attachment.reset();
    parameter = state.getParameter (pid);

    if (parameter != nullptr)
    {
        attachment = std::make_unique<ParameterAttachment> (*parameter, [this] (float v)
        {
            selected = roundToInt (v);
            repaint();

            if (onChange != nullptr)
                onChange (selected);
        });
        attachment->sendInitialUpdate();
        setTooltip (parameter->getName (64));
    }
}

Rectangle<float> ChoiceButtons::cell (int index) const
{
    const int rows = (labels.size() + columns - 1) / columns;
    const float gap = 4.0f;
    const auto area = getLocalBounds().toFloat();
    const float w = (area.getWidth() - gap * (float) (columns - 1)) / (float) columns;
    const float h = (area.getHeight() - gap * (float) (rows - 1)) / (float) jmax (1, rows);
    const int row = index / columns, col = index % columns;
    return { (float) col * (w + gap), (float) row * (h + gap), w, h };
}

int ChoiceButtons::indexAt (Point<float> p) const
{
    for (int i = 0; i < labels.size(); ++i)
        if (cell (i).contains (p))
            return i;

    return -1;
}

void ChoiceButtons::paint (Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto r = cell (i).reduced (0.5f);
        const bool on = i == selected, hot = i == hovered;

        g.setColour (on ? accent : colours::panel.withAlpha (hot ? 1.0f : 0.8f));
        g.fillRect (r);
        g.setColour (on ? accent : (hot ? colours::textFaint : colours::outline));
        g.drawRect (r, 1.0f);

        g.setColour (on ? Colours::white : colours::textDim);
        g.setFont (font (jmin (11.0f, r.getHeight() * 0.48f), true).withExtraKerningFactor (0.1f));
        g.drawFittedText (labels[i].toUpperCase(), r.reduced (3.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.8f);
    }
}

void ChoiceButtons::mouseDown (const MouseEvent& e)
{
    const int i = indexAt (e.position);

    if (i >= 0 && attachment != nullptr)
        attachment->setValueAsCompleteGesture ((float) i);
}

void ChoiceButtons::mouseMove (const MouseEvent& e)
{
    const int i = indexAt (e.position);

    if (i != hovered)
    {
        hovered = i;
        repaint();
    }
}

void ChoiceButtons::mouseExit (const MouseEvent&)
{
    hovered = -1;
    repaint();
}

//==============================================================================
void setParameterValue (APVTS& state, const String& paramId, float plainValue)
{
    if (auto* p = state.getParameter (paramId))
    {
        const float normalised = p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (plainValue));

        if (std::abs (normalised - p->getValue()) < 1.0e-7f)
            return;

        p->beginChangeGesture();
        p->setValueNotifyingHost (normalised);
        p->endChangeGesture();
    }
}

float getParameterValue (APVTS& state, const String& paramId)
{
    if (auto* v = state.getRawParameterValue (paramId))
        return v->load();

    return 0.0f;
}

void drawPanel (Graphics& g, Rectangle<float> area, Colour fill)
{
    // Frosted glass card over the backdrop, hairline edge, crop marks at the corners
    g.setColour (fill.withAlpha (0.86f));
    g.fillRect (area);
    g.setColour (colours::outline.withAlpha (0.9f));
    g.drawRect (area, 1.0f);

    const float m = 7.0f;
    g.setColour (colours::ink.withAlpha (0.55f));

    for (auto [corner, dx, dy] : { std::tuple { area.getTopLeft(), 1.0f, 1.0f }, std::tuple { area.getTopRight(), -1.0f, 1.0f },
                                   std::tuple { area.getBottomLeft(), 1.0f, -1.0f }, std::tuple { area.getBottomRight(), -1.0f, -1.0f } })
    {
        const auto c = corner + Point<float> (dx < 0 ? -1.0f : 0.0f, dy < 0 ? -1.0f : 0.0f);
        g.fillRect (Rectangle<float> (dx > 0 ? c.x : c.x - m + 1.0f, c.y, m, 1.0f));
        g.fillRect (Rectangle<float> (c.x, dy > 0 ? c.y : c.y - m + 1.0f, 1.0f, m));
    }
}

Rectangle<float> drawCard (Graphics& g, Rectangle<float> area, const String& title, Colour titleColour)
{
    drawPanel (g, area);
    auto content = area.reduced (12.0f, 8.0f);
    drawLabel (g, title, content.removeFromTop (16.0f), Justification::centredLeft, titleColour, 9.5f);
    content.removeFromTop (4.0f);
    return content;
}

} // namespace hl::gui
