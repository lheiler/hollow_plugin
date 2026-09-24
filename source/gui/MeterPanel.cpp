#include "MeterPanel.h"

namespace hl::gui
{
using namespace juce;
namespace pid = params::id;

namespace
{
    constexpr float meterTopDb = 3.0f, meterBottomDb = -60.0f;

    Colour levelColour (float db)
    {
        if (db > -0.1f)
            return colours::danger;

        if (db > -6.0f)
            return colours::accent;

        return colours::ink;
    }
} // namespace

void MeterPanel::Channel::push (float db) noexcept
{
    level = db > level ? db : jmax (db, level - 1.0f); // ~30 dB/s fall-off

    if (db >= hold)
    {
        hold = db;
        holdFrames = 45;
    }
    else if (--holdFrames <= 0)
    {
        hold = jmax (level, hold - 0.5f);
    }
}

MeterPanel::MeterPanel (PanelContext& c)
    : ctx (c), processor (c.processor),
      inputGain (c.processor.getState(), pid::inputGain, "Input", colours::textDim, &c.processor, true),
      mix (c.processor.getState(), pid::mix, "Mix", colours::ink, &c.processor),
      outputGain (c.processor.getState(), pid::outputGain, "Output", colours::textDim, &c.processor, true),
      autoLevel (c.processor.getState(), pid::autoLevel, "Auto level", colours::ink),
      clipGuard (c.processor.getState(), pid::clipGuard, "Clip guard", colours::ink)
{
    for (auto* comp : std::initializer_list<Component*> { &inputGain, &mix, &outputGain, &autoLevel, &clipGuard })
        addAndMakeVisible (comp);

    mix.getSlider().setTooltip ("Global dry/wet: blend the whole chain against the clean signal (latency-aligned)");
    inputGain.getSlider().setTooltip ("Drive into the chain. With Auto Level on, more input means more grit at the same loudness");
    outputGain.getSlider().setTooltip ("Trim on top of Auto Level; push it into the clip guard for output clipping");
    autoLevel.setTooltip ("Each module estimates from its settings how much louder or quieter it makes the sound; the total is compensated instantly");
    clipGuard.setTooltip ("Soft ceiling: untouched below -3 dBFS, never above -0.3 dBFS");
}

void MeterPanel::refresh()
{
    auto& m = processor.getMeters();

    for (int ch = 0; ch < 2; ++ch)
    {
        input[(size_t) ch].push (Decibels::gainToDecibels (m.inputPeak[(size_t) ch].exchange (0.0f), -100.0f));
        output[(size_t) ch].push (Decibels::gainToDecibels (m.outputPeak[(size_t) ch].exchange (0.0f), -100.0f));
    }

    for (auto* k : { &inputGain, &mix, &outputGain })
        k->refreshModulation();

    repaint (meterArea.getUnion (scopeArea).getUnion (statusArea));
}

void MeterPanel::drawMeterPair (Graphics& g, Rectangle<float> area, const Channel& l, const Channel& r, const String& title)
{
    drawLabel (g, title, area.removeFromTop (16.0f), Justification::centred, colours::textDim, 9.5f);

    const float peak = jmax (l.hold, r.hold);
    g.setColour (peak > -0.1f ? colours::danger : colours::text);
    g.setFont (monoFont (11.5f));
    g.drawText (peak <= -99.0f ? String ("-inf") : String (peak, 1), area.removeFromTop (16.0f), Justification::centred, false);
    area.removeFromTop (4.0f);

    const auto yFor = [&] (float db) { return jmap (jlimit (meterBottomDb, meterTopDb, db), meterBottomDb, meterTopDb, area.getBottom(), area.getY()); };
    const float barW = jmin (10.0f, (area.getWidth() - 6.0f) * 0.5f);
    const float x0 = area.getCentreX() - barW - 3.0f;

    for (int i = 0; i < 2; ++i)
    {
        const auto& ch = i == 0 ? l : r;
        const auto bar = Rectangle<float> (x0 + (float) i * (barW + 6.0f), area.getY(), barW, area.getHeight());

        g.setColour (colours::well.withAlpha (0.8f));
        g.fillRect (bar);

        const float y = yFor (ch.level);
        g.setGradientFill (ColourGradient (colours::accent, 0.0f, yFor (0.0f), colours::steel, 0.0f, yFor (-12.0f), false));
        g.fillRect (bar.withTop (y));
        g.setGradientFill (ColourGradient (colours::steel, 0.0f, yFor (-12.0f), colours::ice, 0.0f, area.getBottom(), false));
        g.fillRect (bar.withTop (jmax (y, yFor (-12.0f))));

        g.setColour (levelColour (ch.hold));
        g.fillRect (bar.withY (yFor (ch.hold)).withHeight (1.0f).expanded (1.0f, 0.0f));
        g.setColour (colours::outline);
        g.drawRect (bar, 1.0f);
    }
}

void MeterPanel::drawScope (Graphics& g, Rectangle<float> area)
{
    drawLabel (g, "Scope", area.removeFromTop (16.0f), Justification::centredLeft, colours::textDim, 9.5f);
    area.removeFromTop (4.0f);
    const auto square = area.withSizeKeepingCentre (jmin (area.getWidth(), area.getHeight()), jmin (area.getWidth(), area.getHeight()));

    g.setColour (colours::well.withAlpha (0.35f));
    g.fillRect (square);
    g.setColour (colours::outline);
    g.drawRect (square, 1.0f);

    const auto c = square.getCentre();
    const float r = square.getWidth() * 0.5f - 4.0f;
    g.setColour (colours::grid);
    g.drawLine (square.getX(), c.y, square.getRight(), c.y, 1.0f);
    g.drawLine (c.x, square.getY(), c.x, square.getBottom(), 1.0f);
    g.setColour (colours::gridStrong.withAlpha (0.6f));
    g.drawLine (c.x - r * 0.7f, c.y - r * 0.7f, c.x + r * 0.7f, c.y + r * 0.7f, 0.6f);
    g.drawLine (c.x - r * 0.7f, c.y + r * 0.7f, c.x + r * 0.7f, c.y - r * 0.7f, 0.6f);

    // Mid/side Lissajous: mono is vertical, wide stereo spreads sideways
    const auto& scope = ctx.scope;
    float peak = 0.05f;

    for (int i = 0; i < 1024; ++i)
        peak = jmax (peak, std::abs (scope.recentLeft (i)), std::abs (scope.recentRight (i)));

    const float scale = r / jmin (1.0f, peak * 1.2f);
    Path trace;

    for (int i = 0; i < 1024; ++i)
    {
        const float lft = scope.recentLeft (i), rgt = scope.recentRight (i);
        const auto pt = Point<float> (c.x + (lft - rgt) * 0.7071f * scale, c.y - (lft + rgt) * 0.7071f * scale);

        if (i == 0)
            trace.startNewSubPath (pt);
        else
            trace.lineTo (pt);
    }

    Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (square.toNearestInt());
    g.setColour (colours::ink.withAlpha (0.55f));
    g.strokePath (trace, PathStrokeType (0.8f));

    g.setFont (monoFont (9.0f));
    g.setColour (colours::textFaint);
    g.drawText ("M", square.reduced (4.0f), Justification::centredTop, false);
    g.drawText ("L", square.reduced (4.0f), Justification::topLeft, false);
    g.drawText ("R", square.reduced (4.0f), Justification::topRight, false);
}

void MeterPanel::paint (Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat(), colours::panel);

    auto area = meterArea.toFloat();
    const float pairW = (area.getWidth() - 30.0f) * 0.5f;
    auto inArea = area.removeFromLeft (pairW);
    auto scale = area.removeFromLeft (30.0f);
    auto outArea = area;

    drawMeterPair (g, inArea, input[0], input[1], "IN");
    drawMeterPair (g, outArea, output[0], output[1], "OUT");

    scale.removeFromTop (36.0f);
    g.setFont (monoFont (9.0f));
    g.setColour (colours::textFaint);

    for (float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
    {
        const float y = jmap (db, meterBottomDb, meterTopDb, scale.getBottom(), scale.getY());
        g.drawText (String ((int) db), Rectangle<float> (scale.getX(), y - 6.0f, scale.getWidth(), 12.0f), Justification::centred, false);
    }

    drawScope (g, scopeArea.toFloat());

    // Auto level readout
    auto& m = processor.getMeters();
    const bool on = autoLevel.getToggleState();
    const float gain = m.autoLevelDb.load();
    g.setFont (monoFont (10.0f));
    g.setColour (colours::textDim);
    const String text = ! on ? String ("level: manual")
                             : "level: " + String (gain > 0.05f ? "+" : "") + String (std::abs (gain) < 0.05f ? 0.0f : gain, 1) + " dB";
    g.drawText (text, statusArea.toFloat(), Justification::centred, false);
}

void MeterPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 10);

    auto knobs = area.removeFromBottom (jmin (230, area.getHeight() / 3));
    auto top = knobs.removeFromTop (knobs.getHeight() / 2);
    inputGain.setBounds (top.removeFromLeft (top.getWidth() / 2));
    outputGain.setBounds (top);
    mix.setBounds (knobs.withSizeKeepingCentre (knobs.getWidth() / 2, knobs.getHeight()));

    area.removeFromBottom (6);
    statusArea = area.removeFromBottom (16);
    auto toggles = area.removeFromBottom (26);
    autoLevel.setBounds (toggles.removeFromLeft (toggles.getWidth() / 2).reduced (2, 0));
    clipGuard.setBounds (toggles.reduced (2, 0));
    area.removeFromBottom (10);
    scopeArea = area.removeFromBottom (jmin (area.getWidth() + 20, area.getHeight() / 2));
    area.removeFromBottom (12);
    meterArea = area;
}

} // namespace hl::gui
