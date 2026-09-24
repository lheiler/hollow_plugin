#include "ChainStrip.h"

namespace hl::gui
{
using namespace juce;

namespace
{
    constexpr float endLabelWidth = 30.0f;
    constexpr float gap = 12.0f;
    constexpr float powerSize = 20.0f;
}

ChainStrip::ChainStrip (HollowAudioProcessor& p)
    : processor (p), order (p.getModuleOrder())
{
    for (int id = 0; id < dsp::numModules; ++id)
    {
        auto& b = power[(size_t) id];
        b = std::make_unique<ParamToggle> (processor.getState(), params::id::moduleOn (id), String(), colours::forModule (id), "power");
        b->onClick = [this] { repaint(); };
        addAndMakeVisible (*b);
    }

    setTooltip ("Click a module to edit it, drag it to change the order of the chain");
}

void ChainStrip::setSelectedModule (int moduleId)
{
    selected = jlimit (0, dsp::numModules - 1, moduleId);
    repaint();
}

void ChainStrip::refresh()
{
    const auto current = processor.getModuleOrder();

    if (current != order && draggedModule < 0)
    {
        order = current;
        layoutButtons();
    }

    repaint();
}

Rectangle<float> ChainStrip::tileBounds (int slot) const
{
    auto area = getLocalBounds().toFloat().reduced (0.0f, 7.0f);
    area.removeFromLeft (endLabelWidth);
    area.removeFromRight (endLabelWidth);
    constexpr int numTiles = dsp::numModules;
    const float w = (area.getWidth() - gap * (float) (numTiles - 1)) / (float) numTiles;
    return { area.getX() + (float) slot * (w + gap), area.getY(), w, area.getHeight() };
}

int ChainStrip::slotAt (float x) const
{
    for (int s = 0; s < dsp::numModules; ++s)
        if (x < tileBounds (s).getRight() + gap * 0.5f)
            return s;

    return dsp::numModules - 1;
}

void ChainStrip::layoutButtons()
{
    for (int slot = 0; slot < dsp::numModules; ++slot)
    {
        const auto tile = tileBounds (slot);
        power[(size_t) order[(size_t) slot]]->setBounds (Rectangle<float> (powerSize, powerSize)
                                                             .withCentre ({ tile.getX() + 8.0f + powerSize * 0.5f, tile.getCentreY() })
                                                             .toNearestInt());
    }
}

void ChainStrip::resized()
{
    layoutButtons();
}

void ChainStrip::paint (Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    drawLabel (g, "In", area.withWidth (endLabelWidth), Justification::centred, colours::textFaint, 9.5f);
    drawLabel (g, "Out", area.withTrimmedLeft (area.getWidth() - endLabelWidth), Justification::centred, colours::textFaint, 9.5f);

    for (int slot = 0; slot < dsp::numModules; ++slot)
    {
        const int id = order[(size_t) slot];
        const auto tile = tileBounds (slot);
        const auto moduleColour = colours::forModule (id);
        const bool enabled = power[(size_t) id]->getToggleState();
        const bool isSelected = id == selected;
        const bool isDragged = id == draggedModule && dragMoved;

        drawPanel (g, tile, isSelected ? Colours::white : colours::panel);

        // module colour as a fine rule along the bottom; the selected tile gets the orange mark
        g.setColour (enabled ? moduleColour : moduleColour.withAlpha (0.2f));
        g.fillRect (tile.reduced (8.0f, 0.0f).removeFromBottom (5.0f).withHeight (enabled ? 2.0f : 1.0f));

        if (isSelected)
        {
            g.setColour (colours::accent);
            g.fillRect (tile.withHeight (2.0f).reduced (8.0f, 0.0f));
        }

        if (isDragged)
        {
            g.setColour (colours::accent.withAlpha (0.12f));
            g.fillRect (tile);
        }

        auto text = tile.withTrimmedLeft (8.0f + powerSize + 6.0f).withTrimmedRight (6.0f);
        g.setColour (enabled ? colours::text : colours::textFaint);
        g.setFont (isSelected ? font (13.5f, true) : font (13.5f));
        g.drawText (String (dsp::moduleName (id)).toLowerCase(), text, Justification::centredLeft, true);

        if (slot < dsp::numModules - 1)
        {
            const float x0 = tile.getRight() + 2.0f, x1 = tile.getRight() + gap - 2.0f, cy = tile.getCentreY();
            g.setColour (colours::textFaint);
            g.fillRect (Rectangle<float> (x0, cy, x1 - x0, 1.0f));
            g.fillRect (Rectangle<float> (x1 - 1.0f, cy - 2.0f, 1.0f, 5.0f));
        }
    }
}

void ChainStrip::mouseDown (const MouseEvent& e)
{
    for (int slot = 0; slot < dsp::numModules; ++slot)
    {
        if (tileBounds (slot).contains (e.position))
        {
            draggedModule = order[(size_t) slot];
            dragMoved = false;
            setSelectedModule (draggedModule);

            if (onModuleSelected)
                onModuleSelected (draggedModule);

            return;
        }
    }
}

void ChainStrip::mouseDrag (const MouseEvent& e)
{
    if (draggedModule < 0 || e.getDistanceFromDragStart() < 6)
        return;

    dragX = e.position.x;
    const int target = slotAt (e.position.x);
    const auto from = (int) std::distance (order.begin(), std::find (order.begin(), order.end(), draggedModule));
    dragMoved = true;

    if (target != from)
    {
        // Move (not swap): the dragged module slides into place and the rest keep their relative order
        auto newOrder = order;
        [[maybe_unused]] const int id = newOrder[(size_t) from];

        if (target > from)
            std::rotate (newOrder.begin() + from, newOrder.begin() + from + 1, newOrder.begin() + target + 1);
        else
            std::rotate (newOrder.begin() + target, newOrder.begin() + from, newOrder.begin() + from + 1);

        jassert (newOrder[(size_t) target] == id);
        order = newOrder;
        layoutButtons();
    }

    repaint();
}

void ChainStrip::mouseUp (const MouseEvent&)
{
    if (draggedModule >= 0 && dragMoved)
        processor.setModuleOrder (order);

    draggedModule = -1;
    dragMoved = false;
    repaint();
}

} // namespace hl::gui
