#include "TrashPanel.h"

namespace hl::gui
{
using namespace juce;
namespace pid = params::id;

namespace
{
    const char* const bandNames[] = { "Low", "Mid", "High" };

    String familyOf (int algo) { return dsp::algoInfo (algo).family; }
}

TrashPanel::TrashPanel (PanelContext& c)
    : ModulePanel (c, dsp::moduleTrash),
      bandCount (state, pid::trashBands, { "1 band", "2 bands", "3 bands" }, 3, colours::forModule (dsp::moduleTrash)),
      algoA (state, pid::trash (0, "AlgoA"), "Algorithm A", familyOf),
      algoB (state, pid::trash (0, "AlgoB"), "Algorithm B", familyOf),
      autoGain (state, pid::trashAutoGain, "Auto gain", colours::ink)
{
    for (auto* comp : std::initializer_list<Component*> { &transfer, &pad, &bandView, &tabs, &bandCount, &algoA, &algoB, &autoGain })
        addAndMakeVisible (comp);

    addKnob (pid::trash (0, "Drive"), "Drive");
    addKnob (pid::trash (0, "Morph"), "Morph A/B");
    addKnob (pid::trash (0, "Bias"), "Bias", true);
    addKnob (pid::trash (0, "Tone"), "Tone");
    addKnob (pid::trash (0, "Mix"), "Mix");
    addKnob (pid::trash (0, "Level"), "Level", true);

    bandCount.onChange = [this] (int)
    {
        if (band >= numBands())
            selectBand (numBands() - 1);

        repaint();
    };

    autoGain.setTooltip ("Keeps the loudness roughly constant while you turn up the drive, so you hear the character, not just level");
    pad.setTooltip ("Drag: left/right morphs between algorithm A and B, up/down sets the drive");
    bandView.setTooltip ("Click a band to edit it, drag the lines to move the crossovers");
    algoA.getComboBox().onChange = [this] { repaint(); };
    algoB.getComboBox().onChange = [this] { repaint(); };

    selectBand ((int) processor.getUiState().getProperty ("trashBand", 0));
}

int TrashPanel::numBands() const
{
    return jlimit (1, dsp::kTrashBands, (int) getParameterValue (state, pid::trashBands) + 1);
}

void TrashPanel::selectBand (int newBand)
{
    band = jlimit (0, numBands() - 1, newBand);
    processor.getUiState().setProperty ("trashBand", band, nullptr);

    const char* suffixes[] = { "Drive", "Morph", "Bias", "Tone", "Mix", "Level" };

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        knobs[i]->setParameter (bandParam (suffixes[i]));
        knobs[i]->setAccent (numBands() > 1 ? colours::forBand (band) : accent);
    }

    algoA.setParameter (bandParam ("AlgoA"));
    algoB.setParameter (bandParam ("AlgoB"));
    algoA.getComboBox().onChange = [this] { repaint(); };
    algoB.getComboBox().onChange = [this] { repaint(); };
    repaint();
}

void TrashPanel::refresh()
{
    ModulePanel::refresh();
    auto& m = processor.getMeters();

    for (int b = 0; b < dsp::kTrashBands; ++b)
    {
        const float p = m.trashPeaks[(size_t) b].exchange (0.0f);
        levels[(size_t) b] = p > levels[(size_t) b] ? p : levels[(size_t) b] * 0.9f;
    }

    if (band >= numBands())
        selectBand (numBands() - 1);

    transfer.level = levels[(size_t) band];
    transfer.repaint();
    pad.repaint();
    bandView.repaint();
}

void TrashPanel::paint (Graphics& g)
{
    drawCard (g, transferCard.toFloat(), "Transfer curve");
    drawCard (g, padCard.toFloat(), "Morph  x  drive");
    drawCard (g, bandsCard.toFloat(), "Bands");

    const String title = numBands() > 1 ? String ("Band  /  ") + bandNames[band] : String ("Full range");
    drawCard (g, bandCard.toFloat(), title, numBands() > 1 ? colours::forBand (band) : colours::textDim);

    // Algorithm descriptions under the combo boxes
    auto info = algoB.getBounds().toFloat().translated (0.0f, (float) algoB.getHeight() + 2.0f).withHeight (32.0f);
    const int a = (int) getParameterValue (state, bandParam ("AlgoA"));
    const int b = (int) getParameterValue (state, bandParam ("AlgoB"));
    const float morph = getParameterValue (state, bandParam ("Morph"));
    g.setColour (colours::textFaint);
    g.setFont (font (11.0f));
    g.drawFittedText (String (dsp::algoInfo (morph < 50.0f ? a : b).description), info.toNearestInt(), Justification::topLeft, 2);

    drawRoutes (g, routesArea.toFloat());
}

void TrashPanel::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (jmax (220, area.getHeight() - 180));
    area.removeFromTop (10);
    bandCard = area;

    transferCard = top.removeFromLeft (300);
    top.removeFromLeft (10);
    padCard = top.removeFromLeft (jmin (280, top.getHeight() + 20));
    top.removeFromLeft (10);
    bandsCard = top;

    transfer.setBounds (transferCard.reduced (12, 8).withTrimmedTop (20));
    pad.setBounds (padCard.reduced (12, 8).withTrimmedTop (20));

    auto bands = bandsCard.reduced (12, 8).withTrimmedTop (20);
    bandCount.setBounds (bands.removeFromTop (26).withWidth (jmin (300, bands.getWidth())));
    bands.removeFromTop (8);
    bandView.setBounds (bands);

    auto b = bandCard.reduced (12, 8).withTrimmedTop (20);
    auto left = b.removeFromLeft (230);
    tabs.setBounds (left.removeFromTop (26));
    left.removeFromTop (6);
    algoA.setBounds (left.removeFromTop (42));
    algoB.setBounds (left.removeFromTop (42));

    auto right = b.removeFromRight (200);
    autoGain.setBounds (right.removeFromTop (26).withWidth (120));
    right.removeFromTop (10);
    routesArea = right;

    b.removeFromLeft (12);
    b.removeFromRight (12);
    std::vector<Component*> items;

    for (auto& k : knobs)
        items.push_back (k.get());

    layoutGrid (b, items, 6);
}

//==============================================================================
void TrashPanel::TransferView::paint (Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    const auto settings = panel.modulatedSettings();
    const auto base = panel.processor.readSettings();
    const auto& bs = settings.trash.bands[(size_t) panel.band];
    const bool autoGain = settings.trash.autoGain;
    const bool enabled = getParameterValue (panel.state, pid::moduleOn (dsp::moduleTrash)) > 0.5f;
    const auto colour = enabled ? (panel.numBands() > 1 ? colours::forBand (panel.band) : panel.accent) : colours::textFaint;

    constexpr int n = 240;
    std::array<float, n + 1> yMorph {}, yA {}, yB {};
    auto sa = bs, sb = bs;
    sa.morph = 0.0f;
    sb.morph = 1.0f;
    float maxAbs = 1.0f;

    for (int i = 0; i <= n; ++i)
    {
        const float x = -1.0f + 2.0f * (float) i / (float) n;
        yMorph[(size_t) i] = dsp::TrashModule::curve (bs, autoGain, x);
        yA[(size_t) i] = dsp::TrashModule::curve (sa, autoGain, x);
        yB[(size_t) i] = dsp::TrashModule::curve (sb, autoGain, x);
        maxAbs = jmax (maxAbs, std::abs (yMorph[(size_t) i]));
    }

    maxAbs = jmin (maxAbs * 1.05f, 8.0f);
    const auto toX = [&] (float x) { return jmap (x, -1.0f, 1.0f, area.getX(), area.getRight()); };
    const auto toY = [&] (float y) { return jmap (jlimit (-maxAbs, maxAbs, y), -maxAbs, maxAbs, area.getBottom(), area.getY()); };

    // Grid and the used input range (how hard the band is actually hitting the curve)
    const float used = jlimit (0.0f, 1.0f, level);
    g.setColour (colours::accent.withAlpha (0.10f));
    g.fillRect (Rectangle<float>::leftTopRightBottom (toX (-used), area.getY(), toX (used), area.getBottom()));

    for (float v : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
    {
        g.setColour (v == 0.0f ? colours::gridStrong : colours::grid);
        g.fillRect (Rectangle<float> (std::round (toX (v)), area.getY(), 1.0f, area.getHeight()));

        if (std::abs (v) <= maxAbs)
            g.fillRect (Rectangle<float> (area.getX(), std::round (toY (v)), area.getWidth(), 1.0f));
    }

    g.setColour (colours::textFaint.withAlpha (0.5f));
    g.drawLine (toX (-1.0f), toY (-1.0f), toX (1.0f), toY (1.0f), 0.8f);

    const auto pathOf = [&] (const std::array<float, n + 1>& ys)
    {
        Path p;

        for (int i = 0; i <= n; ++i)
        {
            const float x = toX (-1.0f + 2.0f * (float) i / (float) n);

            if (i == 0)
                p.startNewSubPath (x, toY (ys[(size_t) i]));
            else
                p.lineTo (x, toY (ys[(size_t) i]));
        }

        return p;
    };

    if (bs.morph > 0.001f && bs.morph < 0.999f)
    {
        const float dashes[] = { 3.0f, 3.0f };
        Path dashed;
        PathStrokeType (1.0f).createDashedStroke (dashed, pathOf (yA), dashes, 2);
        g.setColour (colours::textFaint);
        g.fillPath (dashed);
        dashed.clear();
        PathStrokeType (1.0f).createDashedStroke (dashed, pathOf (yB), dashes, 2);
        g.setColour (colours::textDim.withAlpha (0.7f));
        g.fillPath (dashed);
    }

    const bool modulated = std::abs (bs.driveDb - base.trash.bands[(size_t) panel.band].driveDb) > 0.05f
                        || std::abs (bs.morph - base.trash.bands[(size_t) panel.band].morph) > 0.002f;
    g.setColour (modulated ? colours::accent : colour);
    g.strokePath (pathOf (yMorph), PathStrokeType (2.2f));

    g.setFont (monoFont (9.5f));
    g.setColour (colours::textFaint);
    g.drawText ("IN", area.withTrimmedTop (area.getHeight() - 12.0f).withTrimmedLeft (area.getWidth() - 18.0f), Justification::centredRight, false);
}

//==============================================================================
void TrashPanel::MorphPad::paint (Graphics& g)
{
    const auto area = plot();
    const auto base = panel.processor.readSettings().trash.bands[(size_t) panel.band];
    const auto live = panel.modulatedSettings().trash.bands[(size_t) panel.band];
    const auto colour = panel.numBands() > 1 ? colours::forBand (panel.band) : panel.accent;

    g.setGradientFill (ColourGradient (colours::well.withAlpha (0.3f), area.getX(), area.getBottom(), colour.withAlpha (0.10f), area.getRight(), area.getY(), false));
    g.fillRect (area);

    for (int i = 1; i < 8; ++i)
    {
        g.setColour (i == 4 ? colours::gridStrong : colours::grid);
        g.fillRect (Rectangle<float> (std::round (area.getX() + area.getWidth() * (float) i / 8.0f), area.getY(), 1.0f, area.getHeight()));
        g.fillRect (Rectangle<float> (area.getX(), std::round (area.getY() + area.getHeight() * (float) i / 8.0f), area.getWidth(), 1.0f));
    }

    g.setColour (colours::outline);
    g.drawRect (area, 1.0f);

    // corner captions: the two algorithms and the drive scale
    auto captions = area.reduced (6.0f, 4.0f);
    g.setFont (font (11.0f, true));
    g.setColour (colours::textDim);
    g.drawText (String ("A  ") + dsp::algoInfo (base.algoA).name, captions.removeFromBottom (14.0f), Justification::centredLeft, false);
    g.drawText (String (dsp::algoInfo (base.algoB).name) + "  B", captions.withTrimmedBottom (-14.0f).removeFromBottom (14.0f), Justification::centredRight, false);
    g.setFont (monoFont (9.5f));
    g.setColour (colours::textFaint);
    g.drawText ("+48 dB", captions.removeFromTop (12.0f), Justification::centredLeft, false);

    const auto posOf = [&] (const dsp::TrashBandSettings& b)
    {
        return Point<float> (jmap (b.morph, area.getX() + 8.0f, area.getRight() - 8.0f),
                             jmap (b.driveDb / dsp::kMaxDriveDb, area.getBottom() - 8.0f, area.getY() + 8.0f));
    };

    const auto p = posOf (base), q = posOf (live);

    if (p.getDistanceFrom (q) > 1.0f)
    {
        g.setColour (colours::accent.withAlpha (0.6f));
        g.drawLine ({ p, q }, 1.0f);
        g.setColour (colours::accent);
        g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre (q));
    }

    g.setColour (colour.withAlpha (0.35f));
    g.fillRect (Rectangle<float> (area.getX(), p.y, area.getWidth(), 1.0f));
    g.fillRect (Rectangle<float> (p.x, area.getY(), 1.0f, area.getHeight()));
    g.setColour (Colours::white);
    g.fillEllipse (Rectangle<float> (16.0f, 16.0f).withCentre (p));
    g.setColour (colour);
    g.drawEllipse (Rectangle<float> (16.0f, 16.0f).withCentre (p), 2.0f);
}

void TrashPanel::MorphPad::setFrom (Point<float> pos)
{
    const auto area = plot().reduced (8.0f);
    const float morph = jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth()) * 100.0f;
    const float drive = jlimit (0.0f, 1.0f, (area.getBottom() - pos.y) / area.getHeight()) * dsp::kMaxDriveDb;

    if (auto* p = panel.state.getParameter (panel.bandParam ("Morph")))
        p->setValueNotifyingHost (p->convertTo0to1 (morph));

    if (auto* p = panel.state.getParameter (panel.bandParam ("Drive")))
        p->setValueNotifyingHost (p->convertTo0to1 (drive));

    repaint();
}

void TrashPanel::MorphPad::mouseDown (const MouseEvent& e)
{
    for (auto* suffix : { "Morph", "Drive" })
        if (auto* p = panel.state.getParameter (panel.bandParam (suffix)))
            p->beginChangeGesture();

    setFrom (e.position);
}

void TrashPanel::MorphPad::mouseDrag (const MouseEvent& e)
{
    setFrom (e.position);
}

void TrashPanel::MorphPad::mouseUp (const MouseEvent&)
{
    for (auto* suffix : { "Morph", "Drive" })
        if (auto* p = panel.state.getParameter (panel.bandParam (suffix)))
            p->endChangeGesture();
}

//==============================================================================
int TrashPanel::BandView::crossoverAt (float x) const
{
    for (int i = 0; i < panel.numBands() - 1; ++i)
        if (std::abs (frequencyToX (getParameterValue (panel.state, pid::trashCrossover (i)), plot()) - x) < 7.0f)
            return i;

    return -1;
}

int TrashPanel::BandView::bandAt (float x) const
{
    const float f = xToFrequency (x, plot());

    for (int i = 0; i < panel.numBands() - 1; ++i)
        if (f < getParameterValue (panel.state, pid::trashCrossover (i)))
            return i;

    return panel.numBands() - 1;
}

void TrashPanel::BandView::paint (Graphics& g)
{
    const auto area = plot();
    const int n = panel.numBands();

    Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area.toNearestInt());
    drawFrequencyGrid (g, area, true);

    float lastX = area.getX();

    for (int b = 0; b < n; ++b)
    {
        const float x = b < n - 1 ? frequencyToX (getParameterValue (panel.state, pid::trashCrossover (b)), area) : area.getRight();
        const auto region = Rectangle<float>::leftTopRightBottom (lastX, area.getY(), x, area.getBottom());
        const auto c = n > 1 ? colours::forBand (b) : panel.accent;
        g.setColour (c.withAlpha (b == panel.band ? 0.13f : 0.04f));
        g.fillRect (region);

        const auto algoName = String (dsp::algoInfo ((int) getParameterValue (panel.state, params::id::trash (b, "AlgoA"))).name);
        const float drive = getParameterValue (panel.state, params::id::trash (b, "Drive"));
        g.setColour (b == panel.band ? c : colours::textFaint);
        g.setFont (font (11.5f, b == panel.band));
        g.drawText (algoName, region.reduced (6.0f).removeFromTop (14.0f), Justification::centredLeft, true);
        g.setFont (monoFont (10.0f));
        g.drawText ("+" + String (roundToInt (drive)) + " dB", region.reduced (6.0f).withTrimmedTop (16.0f).removeFromTop (12.0f), Justification::centredLeft, true);

        // level bar of the band input
        const float lvl = jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (panel.levels[(size_t) b], -60.0f) + 60.0f) / 60.0f);
        g.setColour (c.withAlpha (0.5f));
        g.fillRect (Rectangle<float> (region.getX() + 6.0f, area.getBottom() - 18.0f, (region.getWidth() - 12.0f) * lvl, 3.0f));
        lastX = x;
    }

    g.setColour (colours::ink.withAlpha (0.5f));
    g.strokePath (panel.ctx.spectrum.createPath (area, -90.0f, 6.0f, false), PathStrokeType (1.0f));

    for (int i = 0; i < n - 1; ++i)
    {
        const float x = frequencyToX (getParameterValue (panel.state, pid::trashCrossover (i)), area);
        g.setColour (i == dragging ? colours::accent : colours::ink);
        g.fillRect (Rectangle<float> (x - 0.5f, area.getY(), 1.0f, area.getHeight()));
        g.fillRect (Rectangle<float> (x - 4.0f, area.getCentreY() - 10.0f, 8.0f, 20.0f));
        g.setColour (Colours::white);
        g.fillRect (Rectangle<float> (x - 0.5f, area.getCentreY() - 6.0f, 1.0f, 12.0f));
    }
}

void TrashPanel::BandView::mouseDown (const MouseEvent& e)
{
    dragging = crossoverAt (e.position.x);

    if (dragging >= 0)
    {
        if (auto* p = panel.state.getParameter (pid::trashCrossover (dragging)))
            p->beginChangeGesture();
    }
    else
    {
        panel.selectBand (bandAt (e.position.x));
    }
}

void TrashPanel::BandView::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0)
        return;

    float f = xToFrequency (e.position.x, plot());

    // keep the crossovers in order, at least a third of an octave apart
    if (dragging == 0 && panel.numBands() > 2)
        f = jmin (f, getParameterValue (panel.state, pid::trashCrossover (1)) / 1.26f);
    else if (dragging == 1)
        f = jmax (f, getParameterValue (panel.state, pid::trashCrossover (0)) * 1.26f);

    if (auto* p = panel.state.getParameter (pid::trashCrossover (dragging)))
        p->setValueNotifyingHost (p->convertTo0to1 (jlimit (40.0f, 12000.0f, f)));

    repaint();
}

void TrashPanel::BandView::mouseUp (const MouseEvent&)
{
    if (dragging >= 0)
        if (auto* p = panel.state.getParameter (pid::trashCrossover (dragging)))
            p->endChangeGesture();

    dragging = -1;
    repaint();
}

void TrashPanel::BandView::mouseMove (const MouseEvent& e)
{
    setMouseCursor (crossoverAt (e.position.x) >= 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::PointingHandCursor);
}

//==============================================================================
void TrashPanel::BandTabs::paint (Graphics& g)
{
    const int n = panel.numBands();
    const auto area = getLocalBounds().toFloat();
    const float w = area.getWidth() / (float) n;

    for (int b = 0; b < n; ++b)
    {
        const auto r = Rectangle<float> (area.getX() + (float) b * w, area.getY(), w, area.getHeight()).reduced (1.0f, 0.5f);
        const bool on = b == panel.band;
        const auto c = n > 1 ? colours::forBand (b) : panel.accent;
        g.setColour (on ? c : colours::panel.withAlpha (0.8f));
        g.fillRect (r);
        g.setColour (on ? c : colours::outline);
        g.drawRect (r, 1.0f);
        g.setColour (on ? Colours::white : colours::textDim);
        g.setFont (font (10.5f, true).withExtraKerningFactor (0.12f));
        g.drawText (n == 1 ? String ("FULL RANGE") : String (bandNames[b]).toUpperCase(), r, Justification::centred, false);
    }
}

void TrashPanel::BandTabs::mouseDown (const MouseEvent& e)
{
    const int n = panel.numBands();
    panel.selectBand (jlimit (0, n - 1, (int) (e.position.x / ((float) getWidth() / (float) n))));
}

} // namespace hl::gui
