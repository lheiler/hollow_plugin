#include "FilterPanel.h"

namespace hl::gui
{
using namespace juce;
namespace pid = params::id;

namespace
{
    constexpr float minDb = -42.0f, maxDb = 30.0f;
    const StringArray typeLabels { "LP 12", "LP 24", "HP 12", "HP 24", "BP 12", "BP 24", "Notch", "Comb +", "Comb -", "Vowel" };
}

FilterPanel::FilterPanel (PanelContext& c, int filterIndex)
    : ModulePanel (c, filterIndex == 0 ? dsp::moduleFilter1 : dsp::moduleFilter2),
      index (filterIndex),
      types (state, pid::filter (filterIndex, "Type"), typeLabels, 5, colours::forModule (filterIndex == 0 ? dsp::moduleFilter1 : dsp::moduleFilter2))
{
    addAndMakeVisible (graph);
    addAndMakeVisible (types);
    addKnob (pid::filter (index, "Cutoff"), "Cutoff");
    addKnob (pid::filter (index, "Reso"), "Resonance");
    addKnob (pid::filter (index, "Drive"), "Drive");
    addKnob (pid::filter (index, "Mix"), "Mix");
    types.onChange = [this] (int) { graph.repaint(); };
    graph.setTooltip ("Drag the node: left/right = cutoff, up/down = resonance. Double-click resets the resonance.");
}

void FilterPanel::refresh()
{
    ModulePanel::refresh();
    graph.repaint();
}

void FilterPanel::paint (Graphics& g)
{
    drawCard (g, graphCard.toFloat(), "Filter " + String (index + 1) + "  /  response");
    auto controls = drawCard (g, controlCard.toFloat(), "Type");
    ignoreUnused (controls);
    drawRoutes (g, routesArea.toFloat());
}

void FilterPanel::resized()
{
    auto area = getLocalBounds();
    controlCard = area.removeFromRight (390);
    area.removeFromRight (10);
    graphCard = area;

    graph.setBounds (graphCard.reduced (12, 8).withTrimmedTop (20));

    auto c = controlCard.reduced (12, 8).withTrimmedTop (20);
    types.setBounds (c.removeFromTop (60));
    c.removeFromTop (14);
    routesArea = c.removeFromBottom (70);
    std::vector<Component*> items;

    for (auto& k : knobs)
        items.push_back (k.get());

    layoutGrid (c, items, 2);
}

//==============================================================================
float FilterPanel::Graph::yFor (float gain) const
{
    const auto p = plot();
    const float db = jlimit (minDb, maxDb, Decibels::gainToDecibels (gain, minDb));
    return jmap (db, minDb, maxDb, p.getBottom(), p.getY());
}

void FilterPanel::Graph::paint (Graphics& g)
{
    const auto area = plot();
    const auto modulated = panel.modulatedSettings().filters[(size_t) panel.index];
    const auto base = panel.processor.readSettings().filters[(size_t) panel.index];
    const double rate = panel.processor.getSampleRate() > 0.0 ? panel.processor.getSampleRate() : 48000.0;
    const bool enabled = getParameterValue (panel.state, params::id::moduleOn (panel.moduleId)) > 0.5f;

    Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area.toNearestInt());
    drawFrequencyGrid (g, area, true);

    // dB grid
    g.setFont (monoFont (9.0f));

    for (float db : { -36.0f, -24.0f, -12.0f, 0.0f, 12.0f, 24.0f })
    {
        const float y = jmap (db, minDb, maxDb, area.getBottom(), area.getY());
        g.setColour (db == 0.0f ? colours::gridStrong : colours::grid);
        g.fillRect (Rectangle<float> (area.getX(), std::round (y), area.getWidth(), 1.0f));
        g.setColour (colours::textFaint);
        g.drawText ((db > 0 ? "+" : "") + String ((int) db), Rectangle<float> (area.getRight() - 30.0f, y - 12.0f, 26.0f, 11.0f), Justification::centredRight, false);
    }

    // Output spectrum as a soft backdrop
    g.setColour (colours::ice.withAlpha (0.35f));
    g.fillPath (panel.ctx.spectrum.createPath (area, -90.0f, 6.0f, true));

    const auto curve = [&] (const dsp::FilterSettings& s)
    {
        Path p;

        for (int i = 0; i <= 300; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / 300.0f;
            const float y = yFor (dsp::FilterModule::magnitude (s, xToFrequency (x, area), rate));

            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }

        return p;
    };

    const auto colour = enabled ? panel.accent : colours::textFaint;
    const bool isModulated = std::abs (modulated.cutoff - base.cutoff) > 0.5f || std::abs (modulated.reso - base.reso) > 0.002f
                          || std::abs (modulated.mix - base.mix) > 0.002f;

    if (isModulated)
    {
        g.setColour (colour.withAlpha (0.35f));
        g.strokePath (curve (base), PathStrokeType (1.2f));
    }

    const auto live = curve (modulated);
    Path fill (live);
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(), area.getBottom());
    fill.closeSubPath();
    g.setGradientFill (ColourGradient (colour.withAlpha (0.16f), 0.0f, area.getY(), colour.withAlpha (0.02f), 0.0f, area.getBottom(), false));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (live, PathStrokeType (2.0f));

    if (base.type == (int) dsp::FilterType::vowel)
    {
        g.setFont (font (12.0f, true));
        const char* vowels[] = { "A", "E", "I", "O", "U" };

        for (int v = 0; v < 5; ++v)
        {
            const float x = area.getX() + area.getWidth() * (float) v / 4.0f;
            g.setColour (colours::textFaint);
            g.drawText (vowels[v], Rectangle<float> (x - 10.0f, area.getY() + 4.0f, 20.0f, 14.0f), Justification::centred, false);
        }
    }

    // Node: base position, and an orange ghost where modulation has pushed it
    const auto nodeAt = [&] (const dsp::FilterSettings& s)
    {
        const float x = frequencyToX (s.cutoff, area);
        return Point<float> (x, jmap (s.reso, 0.0f, 1.0f, area.getBottom() - 20.0f, area.getY() + 20.0f));
    };

    if (isModulated)
    {
        g.setColour (colours::accent);
        g.drawEllipse (Rectangle<float> (10.0f, 10.0f).withCentre (nodeAt (modulated)), 1.5f);
    }

    const auto node = nodeAt (base);
    g.setColour (Colours::white);
    g.fillEllipse (Rectangle<float> (14.0f, 14.0f).withCentre (node));
    g.setColour (colour);
    g.drawEllipse (Rectangle<float> (14.0f, 14.0f).withCentre (node), dragging ? 2.5f : 1.5f);
    g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre (node));
}

void FilterPanel::Graph::mouseDown (const MouseEvent&)
{
    dragging = true;
    startReso = getParameterValue (panel.state, params::id::filter (panel.index, "Reso"));

    for (auto* suffix : { "Cutoff", "Reso" })
        if (auto* p = panel.state.getParameter (params::id::filter (panel.index, suffix)))
            p->beginChangeGesture();
}

void FilterPanel::Graph::mouseDrag (const MouseEvent& e)
{
    const auto area = plot();

    if (auto* p = panel.state.getParameter (params::id::filter (panel.index, "Cutoff")))
        p->setValueNotifyingHost (p->convertTo0to1 (jlimit (20.0f, 20000.0f, xToFrequency (e.position.x, area))));

    if (auto* p = panel.state.getParameter (params::id::filter (panel.index, "Reso")))
        p->setValueNotifyingHost (p->convertTo0to1 (jlimit (0.0f, 100.0f, startReso - (float) e.getDistanceFromDragStartY() * 0.35f)));

    repaint();
}

void FilterPanel::Graph::mouseUp (const MouseEvent&)
{
    dragging = false;

    for (auto* suffix : { "Cutoff", "Reso" })
        if (auto* p = panel.state.getParameter (params::id::filter (panel.index, suffix)))
            p->endChangeGesture();

    repaint();
}

void FilterPanel::Graph::mouseDoubleClick (const MouseEvent&)
{
    setParameterValue (panel.state, params::id::filter (panel.index, "Reso"), 20.0f);
}

} // namespace hl::gui
