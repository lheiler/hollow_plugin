#include "PluginEditor.h"
#include "Presets.h"

namespace hl
{
using namespace juce;

namespace
{
    constexpr int defaultWidth = 1320, defaultHeight = 840, minWidth = 1180, minHeight = 780, maxWidth = 2400, maxHeight = 1600;
    constexpr int headerHeight = 52, chainHeight = 50, modulationHeight = 232, meterWidth = 220, margin = 12, gap = 10;
}

HollowAudioProcessorEditor::HollowAudioProcessorEditor (HollowAudioProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      chainStrip (p),
      filter1Panel (context, 0),
      filter2Panel (context, 1),
      trashPanel (context),
      convolvePanel (context),
      motionPanel (context),
      degradePanel (context),
      dynamicsPanel (context),
      echoPanel (context),
      modulationPanel (context),
      meterPanel (context),
      settingsPage (context),
      bypassButton (p.getState(), params::id::bypass, "BYPASS", gui::colours::danger)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);
    content.painter = [this] (Graphics& g) { paintContent (g); };

    content.addAndMakeVisible (chainStrip);
    content.addAndMakeVisible (modulationPanel);
    content.addAndMakeVisible (meterPanel);
    content.addAndMakeVisible (bypassButton);

    for (int id = 0; id < dsp::numModules; ++id)
        content.addChildComponent (panelFor (id));

    content.addChildComponent (settingsPage);
    content.addChildComponent (saveDialog);
    content.addChildComponent (dropHint);

    chainStrip.onModuleSelected = [this] (int id)
    {
        showMenu (false);
        selectModule (id);
    };

    // Presets: factory list + the user's saved ones
    content.addAndMakeVisible (presetBox);
    rebuildPresetMenu();
    presetBox.setTooltip ("Factory and saved presets");
    presetBox.beforePopup = [this]
    {
        rebuildPresetMenu();
        syncPresetSelection();
    };
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();

        if (id > userPresetIdOffset && id - userPresetIdOffset - 1 < userPresetFiles.size())
            processor.loadUserPreset (userPresetFiles[id - userPresetIdOffset - 1]);
        else if (id > 0 && id <= userPresetIdOffset)
            processor.loadPreset (id - 1);

        chainStrip.refresh();
    };

    for (auto* b : std::initializer_list<Component*> { &previousPreset, &nextPreset, &saveButton, &diceButton, &menuButton, &undoButton, &redoButton })
        content.addAndMakeVisible (b);

   #if JUCE_MAC
    undoButton.setTooltip ("Undo (Cmd+Z)");
    redoButton.setTooltip ("Redo (Cmd+Shift+Z)");
   #else
    undoButton.setTooltip ("Undo (Ctrl+Z)");
    redoButton.setTooltip ("Redo (Ctrl+Y)");
   #endif
    undoButton.onClick = [this] { processor.undo(); chainStrip.refresh(); };
    redoButton.onClick = [this] { processor.redo(); chainStrip.refresh(); };
    setWantsKeyboardFocus (true);

    previousPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    saveButton.setTooltip ("Save the current sound as a preset");
    saveButton.onClick = [this] { showSaveDialog(); };
    diceButton.setTooltip ("Roll the dice: a random chain, order, algorithms and modulation - a new texture every click");
    diceButton.onClick = [this]
    {
        processor.randomize();
        chainStrip.refresh();
        trashPanel.selectBand (0);
    };
    menuButton.setTooltip ("Settings, preset folder and info");
    menuButton.onClick = [this] { showMenu (! settingsPage.isVisible()); };

    settingsPage.onZoomChanged = [this] (float z) { setZoom (z); };
    settingsPage.onTooltipsChanged = [this] (bool on) { tooltips.setMillisecondsBeforeTipAppears (on ? 700 : 1 << 30); };
    settingsPage.onPresetsChanged = [this] { rebuildPresetMenu(); };
    settingsPage.onClose = [this] { showMenu (false); };

    saveDialog.onCancel = [this] { saveDialog.setVisible (false); };
    saveDialog.onSave = [this] (const String& name)
    {
        if (processor.saveUserPreset (name).existsAsFile())
        {
            rebuildPresetMenu();
            saveDialog.setVisible (false);
        }
    };

    tooltips.setMillisecondsBeforeTipAppears (settings->getTooltips() ? 700 : 1 << 30);

    auto ui = processor.getUiState();
    selectModule (jlimit (0, dsp::numModules - 1, (int) ui.getProperty ("module", (int) dsp::moduleTrash)));

    // The stored size is in unzoomed units
    zoom = settings->getZoom();
    const int w = jlimit (minWidth, maxWidth, (int) ui.getProperty ("width", defaultWidth));
    const int h = jlimit (minHeight, maxHeight, (int) ui.getProperty ("height", defaultHeight));
    setResizable (true, true);
    setResizeLimits (roundToInt (minWidth * zoom), roundToInt (minHeight * zoom), roundToInt (maxWidth * zoom), roundToInt (maxHeight * zoom));
    setSize (roundToInt (w * zoom), roundToInt (h * zoom));

    startTimerHz (30);
}

HollowAudioProcessorEditor::~HollowAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

gui::ModulePanel* HollowAudioProcessorEditor::panelFor (int moduleId)
{
    switch (moduleId)
    {
        case dsp::moduleFilter1:  return &filter1Panel;
        case dsp::moduleTrash:    return &trashPanel;
        case dsp::moduleFilter2:  return &filter2Panel;
        case dsp::moduleConvolve: return &convolvePanel;
        case dsp::moduleMotion:   return &motionPanel;
        case dsp::moduleDegrade:  return &degradePanel;
        case dsp::moduleDynamics: return &dynamicsPanel;
        case dsp::moduleEcho:     return &echoPanel;
        default:                  return nullptr;
    }
}

void HollowAudioProcessorEditor::rebuildPresetMenu()
{
    presetBox.clear (dontSendNotification);
    String lastCategory;
    const auto& list = presets::all();

    for (size_t i = 0; i < list.size(); ++i)
    {
        if (String (list[i].category) != lastCategory)
        {
            lastCategory = list[i].category;
            presetBox.addSectionHeading (lastCategory);
        }

        presetBox.addItem (list[i].name, (int) i + 1);
    }

    userPresetFiles = processor.getUserPresets();

    if (! userPresetFiles.isEmpty())
    {
        presetBox.addSectionHeading ("Saved");

        for (int i = 0; i < userPresetFiles.size(); ++i)
            presetBox.addItem (userPresetFiles[i].getFileNameWithoutExtension(), userPresetIdOffset + i + 1);
    }

    shownPreset = {};
}

void HollowAudioProcessorEditor::stepPreset (int delta)
{
    // One list: factory presets, then saved ones
    const int factory = (int) presets::all().size();
    const int total = factory + userPresetFiles.size();
    const int id = presetBox.getSelectedId();
    int index = id > userPresetIdOffset ? factory + id - userPresetIdOffset - 1 : id - 1;

    if (index < 0)
        index = delta > 0 ? -1 : 0;

    index = (index + delta + total) % total;

    if (index < factory)
        processor.loadPreset (index);
    else
        processor.loadUserPreset (userPresetFiles[index - factory]);

    chainStrip.refresh();
}

void HollowAudioProcessorEditor::selectModule (int moduleId)
{
    currentModule = jlimit (0, dsp::numModules - 1, moduleId);

    for (int id = 0; id < dsp::numModules; ++id)
        panelFor (id)->setVisible (id == currentModule);

    chainStrip.setSelectedModule (currentModule);
    processor.getUiState().setProperty ("module", currentModule, nullptr);
}

void HollowAudioProcessorEditor::showMenu (bool shouldShow)
{
    settingsPage.setVisible (shouldShow);
    menuButton.setToggleState (shouldShow, dontSendNotification);

    if (shouldShow)
    {
        settingsPage.toFront (false);
        settingsPage.refresh();
    }
}

void HollowAudioProcessorEditor::showSaveDialog()
{
    StringArray existing;

    for (const auto& f : processor.getUserPresets())
        existing.add (f.getFileNameWithoutExtension());

    const auto current = processor.getPresetName();
    saveDialog.open (current.startsWith ("Random #") || current == "Init" ? String ("My Texture") : current, existing);
}

void HollowAudioProcessorEditor::setZoom (float newZoom)
{
    const auto unzoomed = content.getLocalBounds();
    zoom = newZoom;
    setResizeLimits (roundToInt (minWidth * zoom), roundToInt (minHeight * zoom), roundToInt (maxWidth * zoom), roundToInt (maxHeight * zoom));
    setSize (roundToInt (unzoomed.getWidth() * zoom), roundToInt (unzoomed.getHeight() * zoom));
}

void HollowAudioProcessorEditor::tick()
{
    const double rate = processor.getSampleRate();
    spectrum.setSampleRate (rate);
    spectrum.update (processor.getSpectrumFifo());
    scope.setSampleRate (rate > 0.0 ? rate : 48000.0);
    scope.pull (processor.getScopeFifo());

    meterPanel.refresh();
    chainStrip.refresh();
    modulationPanel.refresh();

    if (settingsPage.isVisible())
        settingsPage.refresh();
    else if (auto* panel = panelFor (currentModule))
        panel->refresh();

    // The host tells the plugin its sample rate (prepareToPlay); the header just follows along
    if (rate != shownRate || processor.getLatencySamples() != shownLatency || processor.getOversamplingFactor() != shownFactor)
    {
        shownRate = rate;
        shownLatency = processor.getLatencySamples();
        shownFactor = processor.getOversamplingFactor();
        content.repaint (headerArea);
    }

    undoButton.setEnabled (processor.canUndo());
    redoButton.setEnabled (processor.canRedo());
    syncPresetSelection();
}

bool HollowAudioProcessorEditor::keyPressed (const KeyPress& key)
{
    if (key == KeyPress ('z', ModifierKeys::commandModifier, 0))
    {
        processor.undo();
        chainStrip.refresh();
        return true;
    }

    if (key == KeyPress ('y', ModifierKeys::commandModifier, 0) || key == KeyPress ('z', ModifierKeys::commandModifier | ModifierKeys::shiftModifier, 0))
    {
        processor.redo();
        chainStrip.refresh();
        return true;
    }

    return false; // everything else goes to the host
}

void HollowAudioProcessorEditor::ArrowButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (gui::colours::panel.withAlpha (down ? 1.0f : (highlighted ? 0.95f : 0.75f)));
    g.fillRect (r);
    g.setColour (highlighted && isEnabled() ? gui::colours::textFaint : gui::colours::outline);
    g.drawRect (r, 1.0f);

    // a hook-shaped arrow: arc over the top, head on the left (undo) or right (redo)
    const auto c = r.getCentre().translated (0.0f, 1.5f);
    const float rad = jmin (r.getWidth(), r.getHeight()) * 0.24f;
    Path arc;
    arc.addCentredArc (c.x, c.y, rad, rad, 0.0f, -MathConstants<float>::halfPi * 1.6f, MathConstants<float>::halfPi, true);
    const auto tip = c.getPointOnCircumference (rad, -MathConstants<float>::halfPi * 1.6f);
    Path head;
    head.addTriangle (tip.translated (-3.5f, -1.0f), tip.translated (2.5f, -3.5f), tip.translated (1.0f, 3.0f));

    if (mirror)
    {
        const auto flip = AffineTransform::scale (-1.0f, 1.0f, c.x, c.y);
        arc.applyTransform (flip);
        head.applyTransform (flip);
    }

    g.setColour (isEnabled() ? gui::colours::ink : gui::colours::textFaint);
    g.strokePath (arc, PathStrokeType (1.4f));
    g.fillPath (head);
}

void HollowAudioProcessorEditor::syncPresetSelection()
{
    const auto name = processor.getPresetName();
    const auto userFile = processor.getCurrentUserPreset();
    const auto key = name + "|" + userFile.getFullPathName();

    if (key != shownPreset)
    {
        int id = 0;

        if (userFile != File())
        {
            if (userPresetFiles.indexOf (userFile) < 0 && userFile.existsAsFile())
                rebuildPresetMenu(); // saved just now, or from another window

            const int index = userPresetFiles.indexOf (userFile);
            id = index >= 0 ? userPresetIdOffset + index + 1 : 0;
        }
        else
        {
            const auto& list = presets::all();

            for (size_t i = 0; i < list.size(); ++i)
                if (name == list[i].name)
                    id = (int) i + 1;
        }

        shownPreset = key;

        if (id > 0)
            presetBox.setSelectedId (id, dontSendNotification);
        else
            presetBox.setText (name, dontSendNotification);
    }
}

void HollowAudioProcessorEditor::paintContent (Graphics& g)
{
    const float scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();

    if (backdrop.isNull() || backdrop.getWidth() != roundToInt ((float) content.getWidth() * scale)
                          || backdrop.getHeight() != roundToInt ((float) content.getHeight() * scale))
        backdrop = gui::renderBackdrop (content.getWidth(), content.getHeight(), scale);

    g.drawImage (backdrop, content.getLocalBounds().toFloat());

    // Wordmark: just the name, set in Oxanium with a little air between the letters
    auto header = headerArea.toFloat();
    auto logo = header.removeFromLeft (200.0f);
    const auto markFont = gui::font (27.0f).withExtraKerningFactor (0.06f);
    const String name ("hollow");
    const float nameWidth = GlyphArrangement::getStringWidth (markFont, name);

    g.setColour (gui::colours::ink);
    g.setFont (markFont);
    g.drawText (name, logo.withTrimmedBottom (2.0f), Justification::centredLeft, false);

    // Hairline rule under the header with a heavy segment under the logo
    const float ruleY = (float) headerArea.getBottom() - 1.0f;
    g.setColour (gui::colours::ink.withAlpha (0.55f));
    g.fillRect (Rectangle<float> ((float) margin, ruleY, (float) content.getWidth() - 2.0f * margin, 1.0f));
    g.setColour (gui::colours::ink);
    g.fillRect (Rectangle<float> ((float) headerArea.getX(), ruleY - 1.0f, nameWidth, 3.0f));
    g.setColour (gui::colours::accent);
    g.fillRect (Rectangle<float> ((float) headerArea.getX() + nameWidth + 4.0f, ruleY - 1.0f, 18.0f, 3.0f));

    const double rate = processor.getSampleRate();
    auto info = header.withTrimmedRight (116.0f);
    g.setColour (gui::colours::textDim);
    g.setFont (gui::monoFont (10.5f));

    if (rate > 0.0)
    {
        g.drawText (String (rate / 1000.0, 1) + " KHZ  /  LATENCY " + String (1000.0 * processor.getLatencySamples() / rate, 1) + " MS",
                    info.removeFromTop (info.getHeight() * 0.5f).withTrimmedTop (6.0f), Justification::bottomRight, false);
    }

    const int factor = processor.getOversamplingFactor();
    g.setColour (gui::colours::textFaint);
    g.drawText ("V0.2  /  8 MODULES  /  " + (factor > 1 ? String (factor) + "X OVERSAMPLED" : String ("NO OVERSAMPLING")),
                info.withTrimmedBottom (6.0f), Justification::topRight, false);
}

//==============================================================================
bool HollowAudioProcessorEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (HollowAudioProcessor::presetExtension))
            return true;

    return false;
}

void HollowAudioProcessorEditor::fileDragEnter (const StringArray&, int, int)
{
    dropHint.setVisible (true);
    dropHint.toFront (false);
}

void HollowAudioProcessorEditor::fileDragExit (const StringArray&)
{
    dropHint.setVisible (false);
}

void HollowAudioProcessorEditor::filesDropped (const StringArray& files, int, int)
{
    dropHint.setVisible (false);
    Array<File> presetFiles;

    for (const auto& f : files)
        if (f.endsWithIgnoreCase (HollowAudioProcessor::presetExtension))
            presetFiles.add (File (f));

    settingsPage.importPresetFiles (presetFiles);
    chainStrip.refresh();
}

void HollowAudioProcessorEditor::DropHint::paint (Graphics& g)
{
    g.setColour (gui::colours::background.withAlpha (0.75f));
    g.fillRect (getLocalBounds());
    const auto r = getLocalBounds().toFloat().reduced (24.0f);
    const float dashes[] = { 8.0f, 6.0f };
    Path border, dashed;
    border.addRectangle (r);
    PathStrokeType (2.0f).createDashedStroke (dashed, border, dashes, 2);
    g.setColour (gui::colours::accent);
    g.fillPath (dashed);
    g.setColour (gui::colours::ink);
    g.setFont (gui::font (22.0f));
    g.drawText ("Drop to import presets", r, Justification::centred, false);
}

void HollowAudioProcessorEditor::resized()
{
    content.setTransform (AffineTransform::scale (zoom));
    content.setBounds (0, 0, roundToInt ((float) getWidth() / zoom), roundToInt ((float) getHeight() / zoom));
    layoutContent();

    auto ui = processor.getUiState();
    ui.setProperty ("width", content.getWidth(), nullptr);
    ui.setProperty ("height", content.getHeight(), nullptr);
}

void HollowAudioProcessorEditor::layoutContent()
{
    auto area = content.getLocalBounds().reduced (margin);

    headerArea = area.removeFromTop (headerHeight).withTrimmedLeft (6);
    bypassButton.setBounds (headerArea.withLeft (headerArea.getRight() - 104).withSizeKeepingCentre (100, 28));

    auto row = headerArea.withTrimmedLeft (194).withSizeKeepingCentre (headerArea.getWidth() - 194, 28);
    previousPreset.setBounds (row.removeFromLeft (28));
    row.removeFromLeft (4);
    presetBox.setBounds (row.removeFromLeft (240));
    row.removeFromLeft (4);
    nextPreset.setBounds (row.removeFromLeft (28));
    row.removeFromLeft (10);
    saveButton.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (6);
    diceButton.setBounds (row.removeFromLeft (106));
    row.removeFromLeft (6);
    menuButton.setBounds (row.removeFromLeft (76));
    row.removeFromLeft (10);
    undoButton.setBounds (row.removeFromLeft (28));
    row.removeFromLeft (4);
    redoButton.setBounds (row.removeFromLeft (28));

    area.removeFromTop (gap);
    meterPanel.setBounds (area.removeFromRight (meterWidth));
    area.removeFromRight (margin);

    settingsPage.setBounds (area);
    chainStrip.setBounds (area.removeFromTop (chainHeight));
    area.removeFromTop (gap);
    modulationPanel.setBounds (area.removeFromBottom (modulationHeight));
    area.removeFromBottom (gap);

    for (int id = 0; id < dsp::numModules; ++id)
        panelFor (id)->setBounds (area);

    saveDialog.setBounds (content.getLocalBounds());
    dropHint.setBounds (content.getLocalBounds());
}

} // namespace hl
