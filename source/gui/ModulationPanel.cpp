#include "ModulationPanel.h"

namespace hl::gui
{
using namespace juce;
namespace pid = params::id;

namespace
{
    String destinationSection (int d)
    {
        if (d == dsp::destNone)
            return "None";

        const int m = destinationModule (d);
        return m >= 0 ? String (dsp::moduleName (m)) : (d <= dsp::destLfo2Rate ? String ("Modulators") : String ("Global"));
    }
} // namespace

ModulationPanel::ModulationPanel (PanelContext& c)
    : processor (c.processor), state (c.processor.getState())
{
    for (int l = 0; l < 2; ++l)
    {
        addAndMakeVisible (lfoViews[(size_t) l]);
        lfoViews[(size_t) l].draw = [this, l] (Graphics& g, Rectangle<float> r) { drawLfo (g, r, l); };

        lfoShapes[(size_t) l] = std::make_unique<ChoiceBox> (state, pid::lfo (l, "Shape"));
        addAndMakeVisible (*lfoShapes[(size_t) l]);

        auto& rate = addKnob (pid::lfo (l, "Rate"), "Rate");
        lfoRates[(size_t) l] = std::make_unique<SyncableRate> (rate, state, pid::lfo (l, "Sync"), pid::lfo (l, "Div"), colours::accent);
        addAndMakeVisible (*lfoRates[(size_t) l]);
    }

    addAndMakeVisible (envView);
    envView.draw = [this] (Graphics& g, Rectangle<float> r) { drawEnvelope (g, r); };
    addKnob (pid::envAttack, "Attack");
    addKnob (pid::envRelease, "Release");
    addKnob (pid::envGain, "Gain", true);
    for (int m = 0; m < dsp::kNumMacros; ++m)
        addKnob (pid::macro (m), "Macro " + String (m + 1))
            .getSlider().setTooltip ("A free knob to automate: route it to several destinations (right-click any knob, or use the matrix)");

    for (int s = 0; s < dsp::kModSlots; ++s)
    {
        auto& row = rows[(size_t) s];
        row.source = std::make_unique<ChoiceBox> (state, pid::slot (s, "Src"));
        row.destination = std::make_unique<ChoiceBox> (state, pid::slot (s, "Dst"), String(), destinationSection);
        row.amount.setSliderStyle (Slider::LinearBar);
        row.amount.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        row.amount.setVelocityModeParameters (1.0, 1, 0.0, false);
        row.attachment = std::make_unique<APVTS::SliderAttachment> (state, pid::slot (s, "Amt"), row.amount);
        row.amount.setDoubleClickReturnValue (true, 0.0);
        row.amount.setTooltip ("Modulation amount (double-click to zero)");
        row.amount.onValueChange = [this] { repaint (matrixCard); };

        for (auto* comp : std::initializer_list<Component*> { row.source.get(), row.destination.get(), &row.amount })
            addAndMakeVisible (comp);
    }
}

Knob& ModulationPanel::addKnob (const String& paramId, const String& caption, bool bipolar)
{
    knobs.push_back (std::make_unique<Knob> (state, paramId, caption, colours::accent, &processor, bipolar));
    addAndMakeVisible (*knobs.back());
    return *knobs.back();
}

void ModulationPanel::refresh()
{
    for (auto& k : knobs)
        k->refreshModulation();

    for (auto& r : lfoRates)
        r->refresh();

    envHistory[(size_t) envPos] = processor.getMeters().envelope.load();
    envPos = (envPos + 1) % (int) envHistory.size();

    for (auto& v : lfoViews)
        v.repaint();

    envView.repaint();
}

void ModulationPanel::paint (Graphics& g)
{
    auto& m = processor.getMeters();

    for (int l = 0; l < 2; ++l)
    {
        const auto card = drawCard (g, lfoCards[(size_t) l].toFloat(), "LFO " + String (l + 1), colours::accent);
        g.setFont (monoFont (10.0f));
        g.setColour (colours::textDim);
        const float v = std::abs (m.lfoValues[(size_t) l].load()) < 0.005f ? 0.0f : m.lfoValues[(size_t) l].load();
        g.drawText ((v >= 0.0f ? "+" : "") + String (v, 2),
                    card.withY (card.getY() - 20.0f).withHeight (16.0f), Justification::centredRight, false);
    }

    drawCard (g, envCard.toFloat(), "Envelope", colours::accent);
    drawCard (g, macroCard.toFloat(), "Macros", colours::accent);
    const auto matrix = drawCard (g, matrixCard.toFloat(), "Modulation matrix", colours::accent);

    g.setFont (monoFont (10.0f));

    for (int s = 0; s < dsp::kModSlots; ++s)
    {
        const auto& row = rows[(size_t) s];
        const auto b = row.amount.getBounds().toFloat();
        const float v = (float) row.amount.getValue();
        g.setColour (std::abs (v) < 0.5f ? colours::textFaint : colours::text);
        g.drawText ((v > 0 ? "+" : "") + String (roundToInt (v)), Rectangle<float> (b.getRight() + 2.0f, b.getY(), 34.0f, b.getHeight()), Justification::centredRight, false);
    }

    ignoreUnused (matrix);
}

void ModulationPanel::resized()
{
    auto area = getLocalBounds();
    constexpr int gap = 10;

    for (auto& card : lfoCards)
    {
        card = area.removeFromLeft (190);
        area.removeFromLeft (gap);
    }

    envCard = area.removeFromLeft (190);
    area.removeFromLeft (gap);
    macroCard = area.removeFromLeft (100);
    area.removeFromLeft (gap);
    matrixCard = area;

    for (int l = 0; l < 2; ++l)
    {
        auto c = lfoCards[(size_t) l].reduced (12, 8).withTrimmedTop (20);
        lfoViews[(size_t) l].setBounds (c.removeFromTop (jmax (40, c.getHeight() - 132)));
        c.removeFromTop (6);
        lfoShapes[(size_t) l]->setBounds (c.removeFromTop (26));
        c.removeFromTop (4);
        lfoRates[(size_t) l]->setBounds (c);
    }

    {
        auto c = envCard.reduced (12, 8).withTrimmedTop (20);
        envView.setBounds (c.removeFromTop (jmax (40, c.getHeight() - 96)));
        c.removeFromTop (6);
        const int w = c.getWidth() / 3;

        for (int i = 0; i < 3; ++i)
            knobs[(size_t) (2 + i)]->setBounds (c.removeFromLeft (w).reduced (1));
    }

    {
        auto c = macroCard.reduced (8, 8).withTrimmedTop (20);
        const int h = c.getHeight() / dsp::kNumMacros;

        for (int m = 0; m < dsp::kNumMacros; ++m)
            knobs[(size_t) (5 + m)]->setBounds (c.removeFromTop (h).withSizeKeepingCentre (jmin (84, c.getWidth()), h));
    }

    auto m = matrixCard.reduced (12, 8).withTrimmedTop (20);
    const int rowH = jmin (24, m.getHeight() / dsp::kModSlots);

    for (auto& row : rows)
    {
        auto r = m.removeFromTop (rowH);
        row.source->setBounds (r.removeFromLeft (84).reduced (0, 1));
        r.removeFromLeft (4);
        r.removeFromRight (38);
        row.amount.setBounds (r.removeFromRight (jmax (60, r.getWidth() / 3)).reduced (0, 1));
        r.removeFromRight (6);
        row.destination->setBounds (r.reduced (0, 1));
    }
}

void ModulationPanel::drawLfo (Graphics& g, Rectangle<float> area, int index)
{
    g.setColour (colours::well.withAlpha (0.3f));
    g.fillRect (area);
    g.setColour (colours::outline);
    g.drawRect (area, 1.0f);

    const auto plot = area.reduced (4.0f, 6.0f);
    const auto shape = (dsp::LfoShape) jlimit (0, dsp::kNumLfoShapes - 1, (int) getParameterValue (state, pid::lfo (index, "Shape")));
    const float phase = processor.getMeters().lfoPhases[(size_t) index].load();
    const float value = processor.getMeters().lfoValues[(size_t) index].load();
    const bool random = shape == dsp::LfoShape::sampleHold || shape == dsp::LfoShape::smoothRandom;

    g.setColour (colours::gridStrong);
    g.fillRect (Rectangle<float> (plot.getX(), plot.getCentreY(), plot.getWidth(), 1.0f));

    Path p;
    Random rng (7 + (int) shape);
    float held = rng.nextFloat() * 2.0f - 1.0f, from = held, to = rng.nextFloat() * 2.0f - 1.0f;

    for (int i = 0; i <= 160; ++i)
    {
        float t = (float) i / 80.0f; // two cycles

        if (random && std::floor (t) != std::floor ((float) (i - 1) / 80.0f) && i > 0)
        {
            from = to;
            to = rng.nextFloat() * 2.0f - 1.0f;
            held = rng.nextFloat() * 2.0f - 1.0f;
        }

        const float v = dsp::Lfo::evaluate (shape, t - std::floor (t), held, from, to);
        const auto pt = Point<float> (plot.getX() + plot.getWidth() * (float) i / 160.0f, plot.getCentreY() - v * plot.getHeight() * 0.45f);

        if (i == 0)
            p.startNewSubPath (pt);
        else
            p.lineTo (pt);
    }

    g.setColour (colours::ink.withAlpha (0.7f));
    g.strokePath (p, PathStrokeType (1.4f));

    const float x = plot.getX() + plot.getWidth() * phase * 0.5f;
    g.setColour (colours::accent.withAlpha (0.5f));
    g.fillRect (Rectangle<float> (x, plot.getY(), 1.0f, plot.getHeight()));
    g.setColour (colours::accent);
    g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ x, plot.getCentreY() - value * plot.getHeight() * 0.45f }));
}

void ModulationPanel::drawEnvelope (Graphics& g, Rectangle<float> area)
{
    g.setColour (colours::well.withAlpha (0.3f));
    g.fillRect (area);
    g.setColour (colours::outline);
    g.drawRect (area, 1.0f);

    const auto plot = area.reduced (4.0f, 4.0f);
    const int n = (int) envHistory.size();
    Path p;
    p.startNewSubPath (plot.getX(), plot.getBottom());

    for (int i = 0; i < n; ++i)
        p.lineTo (plot.getX() + plot.getWidth() * (float) i / (float) (n - 1),
                  plot.getBottom() - plot.getHeight() * envHistory[(size_t) ((envPos + i) % n)]);

    p.lineTo (plot.getRight(), plot.getBottom());
    p.closeSubPath();
    g.setColour (colours::accent.withAlpha (0.25f));
    g.fillPath (p);
    g.setColour (colours::accent);
    g.strokePath (p, PathStrokeType (1.2f));
}

} // namespace hl::gui
