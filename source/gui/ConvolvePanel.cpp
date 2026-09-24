#include "ConvolvePanel.h"

namespace hl::gui
{
using namespace juce;
namespace pid = params::id;

ConvolvePanel::ConvolvePanel (PanelContext& c)
    : ModulePanel (c, dsp::moduleConvolve),
      reverse (state, pid::convReverse, "Reverse", colours::forModule (dsp::moduleConvolve))
{
    addAndMakeVisible (browser);
    addAndMakeVisible (view);
    addAndMakeVisible (reverse);
    addKnob (pid::convSize, "Size");
    addKnob (pid::convDamp, "Damp");
    addKnob (pid::convMix, "Mix");
    reverse.setTooltip ("Play the impulse backwards: swells that suck into every note");
}

void ConvolvePanel::refresh()
{
    ModulePanel::refresh();
    view.update();
    browser.repaint();
}

void ConvolvePanel::paint (Graphics& g)
{
    drawCard (g, browserCard.toFloat(), "Impulse");
    drawCard (g, viewCard.toFloat(), "Decay  /  frequency response");
    drawRoutes (g, routesArea.toFloat());
}

void ConvolvePanel::resized()
{
    auto area = getLocalBounds();
    browserCard = area.removeFromLeft (440);
    area.removeFromLeft (10);
    viewCard = area;

    browser.setBounds (browserCard.reduced (12, 8).withTrimmedTop (20));

    auto v = viewCard.reduced (12, 8).withTrimmedTop (20);
    controlsArea = v.removeFromBottom (130);
    view.setBounds (v.withTrimmedBottom (8));

    auto controls = controlsArea;
    routesArea = controls.removeFromRight (220).withTrimmedLeft (12);
    auto toggleArea = controls.removeFromRight (110);
    reverse.setBounds (toggleArea.withSizeKeepingCentre (96, 28));
    layoutGrid (controls, { knobs[0].get(), knobs[1].get(), knobs[2].get() }, 3);
}

//==============================================================================
ConvolvePanel::Browser::Browser (ConvolvePanel& owner) : panel (owner)
{
    setTooltip ("Pick an impulse: speaker cabinets, found objects, springs and rooms - all synthesised, so Size really rescales them");
}

void ConvolvePanel::Browser::layoutItems()
{
    items.clear();
    headings.clear();
    const auto area = getLocalBounds().toFloat();
    constexpr int columns = 3;
    constexpr float gap = 4.0f, headingH = 18.0f;

    // count rows to size the cells
    int rows = 0, families = 0;
    String last;

    for (int i = 0, inFamily = 0; i < dsp::kNumImpulses; ++i)
    {
        const String fam = dsp::impulseInfo (i).family;

        if (fam != last)
        {
            ++families;
            last = fam;
            inFamily = 0;
        }

        if (inFamily++ % columns == 0)
            ++rows;
    }

    const float cellH = jmin (30.0f, (area.getHeight() - (float) families * (headingH + 4.0f) - (float) rows * gap) / (float) jmax (1, rows));
    const float cellW = (area.getWidth() - gap * (columns - 1)) / columns;
    float y = area.getY();
    last = {};
    int col = 0;

    for (int i = 0; i < dsp::kNumImpulses; ++i)
    {
        const String fam = dsp::impulseInfo (i).family;

        if (fam != last)
        {
            if (col != 0)
                y += cellH + gap;

            headings.push_back ({ fam, { area.getX(), y, area.getWidth(), headingH } });
            y += headingH + 4.0f;
            last = fam;
            col = 0;
        }

        items.push_back ({ i, { area.getX() + (float) col * (cellW + gap), y, cellW, cellH } });

        if (++col == columns)
        {
            col = 0;
            y += cellH + gap;
        }
    }
}

void ConvolvePanel::Browser::paint (Graphics& g)
{
    layoutItems();
    const int selected = (int) getParameterValue (panel.state, pid::convImpulse);
    const auto colour = panel.accent;

    for (const auto& h : headings)
    {
        drawLabel (g, h.name, h.bounds, Justification::bottomLeft, colours::textFaint, 9.0f);
        g.setColour (colours::grid);
        g.fillRect (h.bounds.withTrimmedTop (h.bounds.getHeight() - 1.0f)
                            .withTrimmedLeft (GlyphArrangement::getStringWidth (font (9.0f, true), h.name) * 1.3f + 8.0f));
    }

    for (const auto& it : items)
    {
        const bool on = it.impulse == selected, hot = it.impulse == hovered;
        const auto r = it.bounds.reduced (0.5f);
        g.setColour (on ? colour : colours::panel.withAlpha (hot ? 1.0f : 0.75f));
        g.fillRect (r);
        g.setColour (on ? colour : (hot ? colours::textFaint : colours::outline));
        g.drawRect (r, 1.0f);
        g.setColour (on ? Colours::white : colours::text);
        g.setFont (font (12.0f, on));
        g.drawFittedText (dsp::impulseInfo (it.impulse).name, r.reduced (6.0f, 0.0f).toNearestInt(), Justification::centredLeft, 1, 0.85f);
    }
}

void ConvolvePanel::Browser::mouseDown (const MouseEvent& e)
{
    for (const auto& it : items)
        if (it.bounds.contains (e.position))
        {
            setParameterValue (panel.state, pid::convImpulse, (float) it.impulse);

            // choosing an impulse implies wanting to hear it
            setParameterValue (panel.state, pid::moduleOn (dsp::moduleConvolve), 1.0f);
            repaint();
            return;
        }
}

void ConvolvePanel::Browser::mouseMove (const MouseEvent& e)
{
    int h = -1;

    for (const auto& it : items)
        if (it.bounds.contains (e.position))
            h = it.impulse;

    if (h != hovered)
    {
        hovered = h;
        repaint();
    }
}

void ConvolvePanel::Browser::mouseExit (const MouseEvent&)
{
    hovered = -1;
    repaint();
}

//==============================================================================
void ConvolvePanel::ImpulseView::update()
{
    auto current = panel.processor.getDisplayImpulse();

    if (current == impulse || current == nullptr)
        return;

    impulse = current;
    const double rate = panel.processor.getSampleRate() > 0.0 ? panel.processor.getSampleRate() : 44100.0;
    const auto& l = (*impulse)[0];
    const auto& r = (*impulse)[1];
    seconds = (double) l.size() / rate;

    // Envelope: peak per column, in dB
    constexpr int columns = 400;
    envelopeL.assign (columns, -100.0f);
    envelopeR.assign (columns, -100.0f);
    float peak = 1.0e-9f;

    for (size_t i = 0; i < l.size(); ++i)
    {
        const auto c = (size_t) jmin (columns - 1, (int) ((double) i / (double) l.size() * columns));
        envelopeL[c] = jmax (envelopeL[c], std::abs (l[i]));
        envelopeR[c] = jmax (envelopeR[c], std::abs (r[i]));
        peak = jmax (peak, jmax (std::abs (l[i]), std::abs (r[i])));
    }

    for (int c = 0; c < columns; ++c)
    {
        envelopeL[(size_t) c] = Decibels::gainToDecibels (envelopeL[(size_t) c] / peak, -72.0f);
        envelopeR[(size_t) c] = Decibels::gainToDecibels (envelopeR[(size_t) c] / peak, -72.0f);
    }

    // Magnitude response on the log frequency axis, lightly smoothed
    const int order = jlimit (10, 17, (int) std::ceil (std::log2 ((double) l.size())));
    dsp::Fft fft (order);
    const int n = fft.getSize();
    std::vector<std::complex<float>> data ((size_t) n);

    for (size_t i = 0; i < l.size() && i < (size_t) n; ++i)
        data[i] = 0.5f * (l[i] + r[i]);

    fft.forward (data.data());
    response.assign (200, -100.0f);

    for (int p = 0; p < 200; ++p)
    {
        const double f0 = 20.0 * std::pow (1000.0, (p - 0.5) / 199.0), f1 = 20.0 * std::pow (1000.0, (p + 0.5) / 199.0);
        const int b0 = jlimit (1, n / 2 - 1, (int) (f0 * n / rate)), b1 = jlimit (b0, n / 2 - 1, (int) (f1 * n / rate));
        double power = 0.0;

        for (int b = b0; b <= b1; ++b)
            power += std::norm (data[(size_t) b]);

        response[(size_t) p] = (float) (10.0 * std::log10 (jmax (1.0e-12, power / (b1 - b0 + 1))));
    }

    const float top = *std::max_element (response.begin(), response.end());

    for (auto& v : response)
        v -= top;

    repaint();
}

void ConvolvePanel::ImpulseView::paint (Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    auto envArea = area.removeFromLeft (area.getWidth() * 0.5f).withTrimmedRight (8.0f);
    auto respArea = area.withTrimmedLeft (8.0f);
    const auto colour = panel.accent;

    for (auto* a : { &envArea, &respArea })
    {
        g.setColour (colours::well.withAlpha (0.35f));
        g.fillRect (*a);
        g.setColour (colours::outline);
        g.drawRect (*a, 1.0f);
    }

    if (impulse == nullptr || envelopeL.empty())
        return;

    // Decay envelope: left channel up, right channel mirrored down
    {
        const auto mid = envArea.getCentreY();
        const float half = envArea.getHeight() * 0.5f - 4.0f;
        Path up, down;
        up.startNewSubPath (envArea.getX(), mid);
        down.startNewSubPath (envArea.getX(), mid);

        for (size_t c = 0; c < envelopeL.size(); ++c)
        {
            const float x = envArea.getX() + envArea.getWidth() * (float) c / (float) (envelopeL.size() - 1);
            up.lineTo (x, mid - half * (1.0f + envelopeL[c] / 72.0f));
            down.lineTo (x, mid + half * (1.0f + envelopeR[c] / 72.0f));
        }

        up.lineTo (envArea.getRight(), mid);
        down.lineTo (envArea.getRight(), mid);
        g.setColour (colour.withAlpha (0.55f));
        g.fillPath (up);
        g.setColour (colour.withAlpha (0.3f));
        g.fillPath (down);
        g.setColour (colours::ink.withAlpha (0.4f));
        g.fillRect (Rectangle<float> (envArea.getX(), mid, envArea.getWidth(), 1.0f));

        g.setFont (monoFont (10.0f));
        g.setColour (colours::textDim);
        const String len = seconds < 1.0 ? String (roundToInt (seconds * 1000.0)) + " ms" : String (seconds, 2) + " s";
        g.drawText (len, envArea.reduced (6.0f, 4.0f), Justification::topRight, false);
        g.drawText ("L", envArea.reduced (6.0f, 4.0f), Justification::topLeft, false);
        g.drawText ("R", envArea.reduced (6.0f, 4.0f), Justification::bottomLeft, false);
    }

    // Frequency response
    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (respArea.toNearestInt());
        drawFrequencyGrid (g, respArea, true);
        Path p;

        for (size_t i = 0; i < response.size(); ++i)
        {
            const float x = respArea.getX() + respArea.getWidth() * (float) i / (float) (response.size() - 1);
            const float y = jmap (jlimit (-48.0f, 3.0f, response[i]), -48.0f, 3.0f, respArea.getBottom() - 14.0f, respArea.getY() + 4.0f);

            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }

        g.setColour (colour);
        g.strokePath (p, PathStrokeType (1.8f));
    }
}

} // namespace hl::gui
