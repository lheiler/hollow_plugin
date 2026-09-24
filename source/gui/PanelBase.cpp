#include "PanelBase.h"

namespace hl::gui
{
using namespace juce;

bool ScopeData::pull (AudioFifo& fifo)
{
    bool any = false;

    fifo.pop ([&] (const float* frame)
    {
        any = true;
        left[(size_t) recentPos] = frame[0];
        right[(size_t) recentPos] = frame[1];
        recentPos = (recentPos + 1) % recentSize;

        const float m = 0.5f * (frame[0] + frame[1]);

        if (counter == 0)
            building = { m, m };

        building.min = jmin (building.min, m);
        building.max = jmax (building.max, m);

        if (++counter >= samplesPerColumn)
        {
            columns[(size_t) columnPos] = building;
            columnPos = (columnPos + 1) % numColumns;
            counter = 0;
            ++columnsAdded;
        }
    });

    return any;
}

int destinationModule (int d) noexcept
{
    using namespace dsp;

    if (d >= destF1Cutoff && d <= destF1Mix)            return moduleFilter1;
    if (d >= destTrashDrive && d <= destTrashMix)       return moduleTrash;
    if (d >= destF2Cutoff && d <= destF2Mix)            return moduleFilter2;
    if (d == destConvMix)                               return moduleConvolve;
    if (d >= destMotionRate && d <= destMotionMix)      return moduleMotion;
    if (d >= destDegradeWow && d <= destDegradeMix)     return moduleDegrade;
    if (d >= destDynThreshold && d <= destDynMix)       return moduleDynamics;
    if (d >= destEchoTime && d <= destEchoMix)          return moduleEcho;
    return -1;
}

//==============================================================================
ModulePanel::ModulePanel (PanelContext& c, int id)
    : ctx (c), processor (c.processor), state (c.processor.getState()), moduleId (id), accent (colours::forModule (id))
{
}

void ModulePanel::refresh()
{
    for (auto& k : knobs)
        k->refreshModulation();
}

Knob& ModulePanel::addKnob (const String& paramId, const String& caption, bool bipolar)
{
    knobs.push_back (std::make_unique<Knob> (state, paramId, caption, accent, &processor, bipolar));
    addAndMakeVisible (*knobs.back());
    return *knobs.back();
}

void ModulePanel::layoutGrid (Rectangle<int> area, const std::vector<Component*>& items, int columns)
{
    const int count = (int) items.size();
    const int rows = (count + columns - 1) / jmax (1, columns);
    const int cellW = area.getWidth() / jmax (1, columns);
    const int cellH = area.getHeight() / jmax (1, rows);

    for (int i = 0; i < count; ++i)
        if (items[(size_t) i] != nullptr)
            items[(size_t) i]->setBounds (Rectangle<int> (area.getX() + (i % columns) * cellW, area.getY() + (i / columns) * cellH, cellW, cellH).reduced (3));
}

void ModulePanel::drawRoutes (Graphics& g, Rectangle<float> area) const
{
    StringArray lines;
    auto& s = state;

    for (int slot = 0; slot < dsp::kModSlots; ++slot)
    {
        const int src = (int) getParameterValue (s, params::id::slot (slot, "Src"));
        const int dst = (int) getParameterValue (s, params::id::slot (slot, "Dst"));
        const float amt = getParameterValue (s, params::id::slot (slot, "Amt"));

        if (src != dsp::sourceNone && dst != dsp::destNone && destinationModule (dst) == moduleId && std::abs (amt) > 0.5f)
        {
            auto target = String (dsp::destinationName (dst));
            const auto prefix = String (dsp::moduleName (moduleId)) + " ";

            if (target.startsWith (prefix))
                target = target.substring (prefix.length());

            lines.add (String (dsp::sourceName (src)) + "  >  " + target + "  " + (amt > 0 ? "+" : "") + String (roundToInt (amt)) + "%");
        }
    }

    drawLabel (g, "Modulation", area.removeFromTop (14.0f), Justification::centredLeft, colours::textFaint, 9.0f);

    g.setFont (monoFont (10.5f));

    if (lines.isEmpty())
    {
        g.setColour (colours::textFaint);
        g.drawText ("none  -  right-click a knob", area.removeFromTop (15.0f), Justification::centredLeft, true);
        return;
    }

    for (const auto& line : lines)
    {
        auto row = area.removeFromTop (15.0f);

        if (row.getBottom() > area.getBottom() + 15.0f)
            break;

        g.setColour (colours::accent);
        g.fillEllipse (row.removeFromLeft (8.0f).withSizeKeepingCentre (4.0f, 4.0f));
        g.setColour (colours::textDim);
        g.drawText (line, row, Justification::centredLeft, true);
    }
}

dsp::ChainSettings ModulePanel::modulatedSettings() const
{
    auto s = processor.readSettings();
    auto& m = processor.getMeters();

    for (int d = 1; d < dsp::numDestinations; ++d)
    {
        const float off = m.modOffsets[(size_t) d].load (std::memory_order_relaxed);

        if (off != 0.0f)
            dsp::applyModulation (s, d, off);
    }

    return s;
}

} // namespace hl::gui
