#include "EffectPanels.h"

namespace hl::gui
{
using namespace juce;
namespace pid = params::id;

namespace
{
    String divisionFor (APVTS& state, const String& divisionId)
    {
        return dsp::divisionNames()[jlimit (0, dsp::kNumDivisions - 1, (int) getParameterValue (state, divisionId))];
    }

    void drawViewFrame (Graphics& g, Rectangle<float> area)
    {
        g.setColour (colours::well.withAlpha (0.3f));
        g.fillRect (area);
        g.setColour (colours::outline);
        g.drawRect (area, 1.0f);
    }

    void drawLegend (Graphics& g, Rectangle<float> area, std::initializer_list<std::pair<String, Colour>> items)
    {
        g.setFont (font (10.5f));

        for (const auto& [name, colour] : items)
        {
            g.setColour (colour);
            g.fillRect (area.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 2.0f));
            area.removeFromLeft (5.0f);
            g.setColour (colours::textDim);
            const float w = GlyphArrangement::getStringWidth (font (10.5f), name) + 14.0f;
            g.drawText (name, area.removeFromLeft (w), Justification::centredLeft, false);
        }
    }
} // namespace

//==============================================================================
SyncableRate::SyncableRate (Knob& rateKnob, APVTS& s, const String& sid, const String& divisionId, Colour accent)
    : rate (rateKnob), state (s), syncId (sid),
      division (s, divisionId, "Division"),
      sync (s, sid, "Sync", accent)
{
    addAndMakeVisible (rate);
    addChildComponent (division);
    addAndMakeVisible (sync);
    sync.setTooltip ("Lock to the host tempo");
    refresh();
}

void SyncableRate::refresh()
{
    const bool synced = getParameterValue (state, syncId) > 0.5f;

    if (rate.isVisible() == synced)
    {
        rate.setVisible (! synced);
        division.setVisible (synced);
    }
}

void SyncableRate::resized()
{
    auto area = getLocalBounds();
    sync.setBounds (area.removeFromBottom (20).withSizeKeepingCentre (64, 20));
    area.removeFromBottom (2);
    rate.setBounds (area);
    division.setBounds (area.withSizeKeepingCentre (jmin (area.getWidth(), 110), 46));
}

//==============================================================================
MotionPanel::MotionPanel (PanelContext& c)
    : ModulePanel (c, dsp::moduleMotion),
      modes (state, pid::motionMode, { "Chorus", "Flanger", "Phaser", "Vibrato", "Tremolo", "Ring", "Shift" }, 7, colours::forModule (dsp::moduleMotion))
{
    addAndMakeVisible (modes);
    addAndMakeVisible (view);
    view.draw = [this] (Graphics& g, Rectangle<float> r) { drawView (g, r); };

    auto& rateKnob = addKnob (pid::motionRate, "Rate");
    rate = std::make_unique<SyncableRate> (rateKnob, state, pid::motionSync, pid::motionDivision, accent);
    addAndMakeVisible (*rate);
    addKnob (pid::motionDepth, "Depth");
    addKnob (pid::motionFeedback, "Feedback", true);
    addKnob (pid::motionFreq, "Freq");
    addKnob (pid::motionSpread, "Spread");
    addKnob (pid::motionMix, "Mix");
    modes.onChange = [this] (int) { repaint(); };
}

void MotionPanel::refresh()
{
    ModulePanel::refresh();
    rate->refresh();
    view.repaint();
}

void MotionPanel::paint (Graphics& g)
{
    drawCard (g, viewCard.toFloat(), "Motion");
    drawCard (g, controlCard.toFloat(), "Controls");
    drawRoutes (g, routesArea.toFloat());
}

void MotionPanel::resized()
{
    auto area = getLocalBounds();
    controlCard = area.removeFromRight (470);
    area.removeFromRight (10);
    viewCard = area;

    auto v = viewCard.reduced (12, 8).withTrimmedTop (20);
    modes.setBounds (v.removeFromTop (28));
    v.removeFromTop (10);
    view.setBounds (v);

    auto c = controlCard.reduced (12, 8).withTrimmedTop (20);
    routesArea = c.removeFromBottom (64);
    layoutGrid (c, { rate.get(), knobs[1].get(), knobs[2].get(), knobs[3].get(), knobs[4].get(), knobs[5].get() }, 3);
}

void MotionPanel::drawView (Graphics& g, Rectangle<float> area)
{
    drawViewFrame (g, area);
    const auto s = modulatedSettings();
    const int mode = s.motion.mode;
    const bool enabled = getParameterValue (state, pid::moduleOn (moduleId)) > 0.5f;
    const float phase = processor.getMeters().motionPhase.load();
    const float spread = s.motion.spread;
    auto plot = area.reduced (14.0f, 26.0f);

    static const char* axis[] = { "delay time", "delay time", "notch frequency", "pitch", "level", "carrier frequency", "shift amount" };
    const String rateText = s.motionSync ? divisionFor (state, pid::motionDivision) + " note"
                                         : String (s.motion.rateHz, s.motion.rateHz < 1.0f ? 2 : 1) + " Hz  /  " + String (1.0f / jmax (0.01f, s.motion.rateHz), 2) + " s cycle";

    g.setFont (font (12.0f, true));
    g.setColour (enabled ? accent : colours::textFaint);
    g.drawText (String (dsp::motionModeNames()[jlimit (0, dsp::kNumMotionModes - 1, mode)]).toUpperCase(), area.reduced (12.0f, 6.0f).removeFromTop (16.0f), Justification::centredLeft, false);
    g.setFont (monoFont (10.5f));
    g.setColour (colours::textDim);
    g.drawText (rateText, area.reduced (12.0f, 6.0f).removeFromTop (16.0f), Justification::centredRight, false);
    drawLegend (g, area.reduced (12.0f, 6.0f).removeFromBottom (14.0f), { { "left", colours::ink }, { "right", colours::steel } });
    g.setFont (monoFont (9.5f));
    g.setColour (colours::textFaint);
    g.drawText (String ("y: ") + axis[jlimit (0, 6, mode)], area.reduced (12.0f, 6.0f).removeFromBottom (14.0f), Justification::centredRight, false);

    // Two cycles of the LFO for each side, with the current position
    const float depth = jmax (0.05f, s.motion.depth);
    const auto yFor = [&] (float v) { return plot.getCentreY() - v * depth * plot.getHeight() * 0.45f; };
    const auto shapeAt = [&] (float p)
    {
        const float lfo = std::sin (dsp::kTwoPiF * p);

        if (mode == (int) dsp::MotionMode::tremolo && s.motion.feedback > 0.0f)
        {
            const float sharp = 1.0f + 12.0f * s.motion.feedback;
            return dsp::fastTanh (lfo * sharp) / dsp::fastTanh (sharp);
        }

        return lfo;
    };

    g.setColour (colours::gridStrong);
    g.fillRect (Rectangle<float> (plot.getX(), plot.getCentreY(), plot.getWidth(), 1.0f));

    for (int ch = 0; ch < 2; ++ch)
    {
        const float offset = ch == 1 ? 0.5f * spread : 0.0f;
        Path p;

        for (int i = 0; i <= 200; ++i)
        {
            const float t = (float) i / 200.0f * 2.0f;
            const auto pt = Point<float> (plot.getX() + plot.getWidth() * (float) i / 200.0f, yFor (shapeAt (t + offset)));

            if (i == 0)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);
        }

        g.setColour ((ch == 0 ? colours::ink : colours::steel).withAlpha (enabled ? 0.9f : 0.3f));
        g.strokePath (p, PathStrokeType (ch == 0 ? 1.8f : 1.4f));

        const float x = plot.getX() + plot.getWidth() * phase * 0.5f;
        const float y = yFor (shapeAt (phase + offset));
        g.setColour (ch == 0 ? colours::ink : colours::steel);
        g.fillEllipse (Rectangle<float> (8.0f, 8.0f).withCentre ({ x, y }));
    }

    g.setColour (colours::accent.withAlpha (0.6f));
    g.fillRect (Rectangle<float> (plot.getX() + plot.getWidth() * phase * 0.5f, plot.getY(), 1.0f, plot.getHeight()));
}

//==============================================================================
DegradePanel::DegradePanel (PanelContext& c)
    : ModulePanel (c, dsp::moduleDegrade)
{
    dropout.fill (1.0f);
    addAndMakeVisible (view);
    view.draw = [this] (Graphics& g, Rectangle<float> r) { drawView (g, r); };

    addKnob (pid::degradeWow, "Wow");
    addKnob (pid::degradeFlutter, "Flutter");
    addKnob (pid::degradeAge, "Age");
    addKnob (pid::degradeNoise, "Hiss");
    addKnob (pid::degradeCrackle, "Crackle");
    addKnob (pid::degradeDropout, "Dropouts");
    addKnob (pid::degradeGlitch, "Glitch");
    addKnob (pid::degradeMix, "Mix");

    knobs[6]->getSlider().setTooltip ("Probability of stutters, reverses and half-speed repeats on a 16th-note grid (follows the host tempo)");
}

void DegradePanel::refresh()
{
    ModulePanel::refresh();
    auto& m = processor.getMeters();
    pitch[(size_t) historyPos] = m.degradePitch.load();
    dropout[(size_t) historyPos] = m.dropoutGain.load();
    glitch[(size_t) historyPos] = m.glitching.load();
    historyPos = (historyPos + 1) % historySize;
    view.repaint();
}

void DegradePanel::paint (Graphics& g)
{
    drawCard (g, viewCard.toFloat(), "Transport");
    drawCard (g, controlCard.toFloat(), "Damage");
    drawRoutes (g, routesArea.toFloat());
}

void DegradePanel::resized()
{
    auto area = getLocalBounds();
    controlCard = area.removeFromRight (470);
    area.removeFromRight (10);
    viewCard = area;
    view.setBounds (viewCard.reduced (12, 8).withTrimmedTop (20));

    auto c = controlCard.reduced (12, 8).withTrimmedTop (20);
    routesArea = c.removeFromBottom (64);
    std::vector<Component*> items;

    for (auto& k : knobs)
        items.push_back (k.get());

    layoutGrid (c, items, 4);
}

void DegradePanel::drawView (Graphics& g, Rectangle<float> area)
{
    drawViewFrame (g, area);
    auto plot = area.reduced (10.0f, 24.0f);
    const int n = historySize;
    const float colW = plot.getWidth() / (float) n;

    // glitch events as orange blocks (newest on the right)
    for (int i = 0; i < n; ++i)
        if (glitch[(size_t) ((historyPos + i) % n)])
        {
            g.setColour (colours::accent.withAlpha (0.16f));
            g.fillRect (Rectangle<float> (plot.getX() + (float) i * colW, plot.getY(), colW + 0.5f, plot.getHeight()));
        }

    // output waveform over the same ~3 s
    const auto& scope = ctx.scope;
    const int columns = ScopeData::numColumns;
    const float half = plot.getHeight() * 0.42f;
    g.setColour (colours::ink.withAlpha (0.45f));

    for (int c = 0; c < columns; ++c)
    {
        const auto& col = scope.column (columns - 1 - c);
        const float x = plot.getX() + plot.getWidth() * (float) c / (float) columns;
        const float y0 = plot.getCentreY() - half * jlimit (-1.0f, 1.0f, col.max);
        const float y1 = plot.getCentreY() - half * jlimit (-1.0f, 1.0f, col.min);
        g.fillRect (Rectangle<float> (x, y0, jmax (1.0f, plot.getWidth() / (float) columns), jmax (1.0f, y1 - y0)));
    }

    // pitch deviation (cents) and dropout gain traces
    const auto trace = [&] (const std::array<float, historySize>& data, std::function<float (float)> toY)
    {
        Path p;

        for (int i = 0; i < n; ++i)
        {
            const auto pt = Point<float> (plot.getX() + ((float) i + 0.5f) * colW, toY (data[(size_t) ((historyPos + i) % n)]));

            if (i == 0)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);
        }

        return p;
    };

    g.setColour (colours::steel);
    g.strokePath (trace (dropout, [&] (float gain) { return plot.getY() + 2.0f + plot.getHeight() * 0.45f * (1.0f - jlimit (0.0f, 1.0f, gain)); }), PathStrokeType (1.4f));
    g.setColour (colours::accent);
    g.strokePath (trace (pitch, [&] (float cents) { return plot.getCentreY() - jlimit (-60.0f, 60.0f, cents) / 60.0f * half; }), PathStrokeType (1.6f));

    drawLegend (g, area.reduced (12.0f, 6.0f).removeFromTop (14.0f),
                { { "pitch drift", colours::accent }, { "dropouts", colours::steel }, { "output", colours::ink.withAlpha (0.45f) } });
    g.setFont (monoFont (9.5f));
    g.setColour (colours::textFaint);
    g.drawText ("+/-60 cents", area.reduced (12.0f, 6.0f).removeFromBottom (14.0f), Justification::centredRight, false);

    const auto s = modulatedSettings().degrade;
    g.drawText ("glitch " + String (roundToInt (s.glitch * 100.0f)) + "%  /  3 s window", area.reduced (12.0f, 6.0f).removeFromBottom (14.0f), Justification::centredLeft, false);
}

//==============================================================================
DynamicsPanel::DynamicsPanel (PanelContext& c)
    : ModulePanel (c, dsp::moduleDynamics)
{
    addAndMakeVisible (view);
    view.draw = [this] (Graphics& g, Rectangle<float> r) { drawView (g, r); };

    addKnob (pid::dynThreshold, "Threshold");
    addKnob (pid::dynRatio, "Ratio");
    addKnob (pid::dynAttack, "Attack");
    addKnob (pid::dynRelease, "Release");
    addKnob (pid::dynMakeup, "Makeup");
    addKnob (pid::dynGate, "Gate");
    addKnob (pid::dynMix, "Mix");
}

void DynamicsPanel::refresh()
{
    ModulePanel::refresh();
    auto& m = processor.getMeters();
    const float in = Decibels::gainToDecibels (m.dynInput.exchange (0.0f), -100.0f);
    const float gr = m.dynReduction.exchange (0.0f);
    inputDb = in > inputDb ? in : jmax (in, inputDb - 1.5f);
    reductionDb = gr < reductionDb ? gr : jmin (0.0f, reductionDb + 0.8f);
    grHistory[(size_t) historyPos] = reductionDb;
    historyPos = (historyPos + 1) % (int) grHistory.size();
    view.repaint();
}

void DynamicsPanel::paint (Graphics& g)
{
    drawCard (g, viewCard.toFloat(), "Transfer  /  gain reduction");
    drawCard (g, controlCard.toFloat(), "Compressor + gate");
    drawRoutes (g, routesArea.toFloat());
}

void DynamicsPanel::resized()
{
    auto area = getLocalBounds();
    controlCard = area.removeFromRight (470);
    area.removeFromRight (10);
    viewCard = area;
    view.setBounds (viewCard.reduced (12, 8).withTrimmedTop (20));

    auto c = controlCard.reduced (12, 8).withTrimmedTop (20);
    routesArea = c.removeFromBottom (64);
    std::vector<Component*> items;

    for (auto& k : knobs)
        items.push_back (k.get());

    layoutGrid (c, items, 4);
}

void DynamicsPanel::drawView (Graphics& g, Rectangle<float> area)
{
    const auto s = modulatedSettings().dynamics;
    const bool enabled = getParameterValue (state, pid::moduleOn (moduleId)) > 0.5f;
    const auto colour = enabled ? accent : colours::textFaint;

    auto square = area.removeFromLeft (jmin (area.getWidth() * 0.45f, area.getHeight())).reduced (2.0f);
    area.removeFromLeft (14.0f);
    auto meter = area.removeFromRight (28.0f);
    area.removeFromRight (10.0f);
    auto history = area;

    // Static curve
    drawViewFrame (g, square);
    constexpr float lo = -60.0f, hi = 6.0f;
    const auto toX = [&] (float db) { return jmap (jlimit (lo, hi, db), lo, hi, square.getX(), square.getRight()); };
    const auto toY = [&] (float db) { return jmap (jlimit (lo, hi, db), lo, hi, square.getBottom(), square.getY()); };

    for (float db = -48.0f; db <= 0.0f; db += 12.0f)
    {
        g.setColour (db == 0.0f ? colours::gridStrong : colours::grid);
        g.fillRect (Rectangle<float> (std::round (toX (db)), square.getY(), 1.0f, square.getHeight()));
        g.fillRect (Rectangle<float> (square.getX(), std::round (toY (db)), square.getWidth(), 1.0f));
    }

    g.setColour (colours::textFaint.withAlpha (0.6f));
    g.drawLine (toX (lo), toY (lo), toX (hi), toY (hi), 0.8f);

    const auto outFor = [&] (float in)
    {
        if (s.gateDb > dsp::DynamicsModule::gateOffDb && in < s.gateDb)
            return -100.0f;

        return in + dsp::compressorGainDb (in, s.thresholdDb, s.ratio, 6.0f) + s.makeupDb;
    };

    Path curve;

    for (int i = 0; i <= 200; ++i)
    {
        const float in = lo + (hi - lo) * (float) i / 200.0f;
        const auto pt = Point<float> (toX (in), toY (outFor (in)));

        if (i == 0)
            curve.startNewSubPath (pt);
        else
            curve.lineTo (pt);
    }

    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (square.toNearestInt());
        g.setColour (colour);
        g.strokePath (curve, PathStrokeType (2.0f));

        if (inputDb > lo)
        {
            const auto dot = Point<float> (toX (inputDb), toY (outFor (inputDb)));
            g.setColour (colours::accent);
            g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (dot));
        }

        g.setColour (colours::ink.withAlpha (0.5f));
        g.fillRect (Rectangle<float> (toX (s.thresholdDb), square.getY(), 1.0f, square.getHeight()));

        if (s.gateDb > dsp::DynamicsModule::gateOffDb)
        {
            g.setColour (colours::steel.withAlpha (0.12f));
            g.fillRect (Rectangle<float>::leftTopRightBottom (square.getX(), square.getY(), toX (s.gateDb), square.getBottom()));
        }
    }

    // Gain reduction history and meter
    drawViewFrame (g, history);
    const int n = (int) grHistory.size();
    Path gr;

    for (int i = 0; i < n; ++i)
    {
        const float v = grHistory[(size_t) ((historyPos + i) % n)];
        const auto pt = Point<float> (history.getX() + history.getWidth() * (float) i / (float) (n - 1),
                                      history.getY() + history.getHeight() * jlimit (0.0f, 1.0f, -v / 24.0f));

        if (i == 0)
            gr.startNewSubPath (history.getX(), history.getY());

        gr.lineTo (pt);
    }

    gr.lineTo (history.getRight(), history.getY());
    gr.closeSubPath();
    g.setColour (colour.withAlpha (0.25f));
    g.fillPath (gr);

    g.setFont (monoFont (9.5f));
    g.setColour (colours::textFaint);

    for (float db : { 6.0f, 12.0f, 18.0f })
    {
        const float y = history.getY() + history.getHeight() * db / 24.0f;
        g.setColour (colours::grid);
        g.fillRect (Rectangle<float> (history.getX(), y, history.getWidth(), 1.0f));
        g.setColour (colours::textFaint);
        g.drawText ("-" + String ((int) db), Rectangle<float> (history.getX() + 4.0f, y - 12.0f, 30.0f, 11.0f), Justification::centredLeft, false);
    }

    drawViewFrame (g, meter);
    g.setColour (colours::accent);
    g.fillRect (meter.reduced (3.0f).withHeight (meter.reduced (3.0f).getHeight() * jlimit (0.0f, 1.0f, -reductionDb / 24.0f)));
    g.setColour (colours::text);
    g.drawText (String (reductionDb, 1), meter.withY (meter.getBottom() - 14.0f).withHeight (14.0f).expanded (12.0f, 0.0f), Justification::centred, false);
}

//==============================================================================
EchoPanel::EchoPanel (PanelContext& c)
    : ModulePanel (c, dsp::moduleEcho),
      pingPong (state, pid::echoPingPong, "Ping-pong", colours::forModule (dsp::moduleEcho))
{
    addAndMakeVisible (view);
    addAndMakeVisible (pingPong);
    view.draw = [this] (Graphics& g, Rectangle<float> r) { drawView (g, r); };

    auto& timeKnob = addKnob (pid::echoTime, "Time");
    time = std::make_unique<SyncableRate> (timeKnob, state, pid::echoSync, pid::echoDivision, accent);
    addAndMakeVisible (*time);
    addKnob (pid::echoFeedback, "Feedback");
    addKnob (pid::echoTone, "Tone", true);
    addKnob (pid::echoDrive, "Drive");
    addKnob (pid::echoWobble, "Wobble");
    addKnob (pid::echoMix, "Mix");

    knobs[1]->getSlider().setTooltip ("Above 100% the echo runs away and self-oscillates - the loop saturates so it stays bounded");
}

void EchoPanel::refresh()
{
    ModulePanel::refresh();
    time->refresh();
    view.repaint();
}

void EchoPanel::paint (Graphics& g)
{
    drawCard (g, viewCard.toFloat(), "Repeats");
    drawCard (g, controlCard.toFloat(), "Tape echo");
    drawRoutes (g, routesArea.toFloat());
}

void EchoPanel::resized()
{
    auto area = getLocalBounds();
    controlCard = area.removeFromRight (470);
    area.removeFromRight (10);
    viewCard = area;
    view.setBounds (viewCard.reduced (12, 8).withTrimmedTop (20));

    auto c = controlCard.reduced (12, 8).withTrimmedTop (20);
    auto bottom = c.removeFromBottom (64);
    pingPong.setBounds (bottom.removeFromRight (110).withSizeKeepingCentre (100, 26));
    routesArea = bottom;
    layoutGrid (c, { time.get(), knobs[1].get(), knobs[2].get(), knobs[3].get(), knobs[4].get(), knobs[5].get() }, 3);
}

void EchoPanel::drawView (Graphics& g, Rectangle<float> area)
{
    drawViewFrame (g, area);
    auto s = modulatedSettings();
    const float bpm = processor.getMeters().bpm.load();

    if (s.echoSync)
        s.echo.timeMs = (float) jmin (2000.0, 60000.0 / bpm * dsp::divisionBeats (s.echoDivision));

    const bool enabled = getParameterValue (state, pid::moduleOn (moduleId)) > 0.5f;
    const auto colour = enabled ? accent : colours::textFaint;
    auto plot = area.reduced (14.0f, 26.0f);
    const float totalMs = jlimit (500.0f, 6000.0f, s.echo.timeMs * 10.5f);
    const auto toX = [&] (float ms) { return plot.getX() + plot.getWidth() * ms / totalMs; };
    const bool pp = s.echo.pingPong;
    const float mid = pp ? plot.getCentreY() : plot.getBottom();
    const float h = pp ? plot.getHeight() * 0.5f : plot.getHeight();

    g.setColour (colours::gridStrong);
    g.fillRect (Rectangle<float> (plot.getX(), mid, plot.getWidth(), 1.0f));

    // time grid in 100 ms / 1 s steps
    for (float ms = 0.0f; ms < totalMs; ms += totalMs > 2500.0f ? 500.0f : 100.0f)
    {
        g.setColour (colours::grid);
        g.fillRect (Rectangle<float> (std::round (toX (ms)), plot.getY(), 1.0f, plot.getHeight()));
    }

    // dry hit
    g.setColour (colours::ink);
    g.fillRect (Rectangle<float> (toX (0.0f), mid - h * 0.9f, 3.0f, h * 0.9f));

    // Each repeat loses level to feedback and loop saturation; above 100% it grows until the loop clips
    const float loopGain = s.echo.feedback * (1.0f - 0.2f * s.echo.drive);
    const float mixScale = jlimit (0.0f, 1.0f, s.echo.mix * 2.0f);
    float amp = 1.0f;

    for (int k = 1; toX (s.echo.timeMs * (float) k) < plot.getRight(); ++k)
    {
        if (k > 1)
            amp = jmin (1.0f, amp * loopGain);

        const float a = amp * mixScale;

        if (a < 0.01f)
            break;

        const float x = toX (s.echo.timeMs * (float) k);
        const bool right = pp && (k % 2 == 0);
        const float barH = h * 0.9f * a;
        const float w = 3.0f + 5.0f * s.echo.wobble;
        const auto bar = right ? Rectangle<float> (x - w * 0.5f, mid, w, barH) : Rectangle<float> (x - w * 0.5f, mid - barH, w, barH);

        // darker tone = the repeats lose their top end
        const float brightness = jlimit (0.2f, 1.0f, 1.0f + 0.12f * (float) k * jmin (0.0f, s.echo.tone));
        g.setColour ((s.echo.feedback > 1.0f && k > 3 ? colours::accent : colour).withAlpha (brightness));
        g.fillRect (bar);
    }

    g.setFont (monoFont (10.5f));
    g.setColour (colours::textDim);
    const String timeText = s.echoSync ? divisionFor (state, pid::echoDivision) + " note (" + String (roundToInt (s.echo.timeMs)) + " ms)"
                                       : String (roundToInt (s.echo.timeMs)) + " ms";
    g.drawText (timeText + "  /  feedback " + String (roundToInt (s.echo.feedback * 100.0f)) + "%" + (s.echo.feedback > 1.0f ? "  RUNAWAY" : ""),
                area.reduced (12.0f, 6.0f).removeFromTop (14.0f), Justification::centredLeft, false);

    if (pp)
    {
        g.setColour (colours::textFaint);
        g.drawText ("L", area.reduced (6.0f, 26.0f), Justification::topRight, false);
        g.drawText ("R", area.reduced (6.0f, 26.0f), Justification::bottomRight, false);
    }
}

} // namespace hl::gui
