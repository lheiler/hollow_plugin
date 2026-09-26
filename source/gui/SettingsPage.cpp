#include "SettingsPage.h"

namespace hl::gui
{
using namespace juce;
namespace pid = params::id;

namespace
{
    void drawNote (Graphics& g, const String& text, Rectangle<int> area)
    {
        g.setColour (colours::textDim);
        g.setFont (font (12.0f));
        g.drawFittedText (text, area, Justification::topLeft, 8, 1.0f);
    }
} // namespace

//==============================================================================
Segmented::Segmented (StringArray l) : labels (std::move (l)) {}

void Segmented::setSelected (int index)
{
    selected = jlimit (0, labels.size() - 1, index);
    repaint();
}

Rectangle<float> Segmented::cell (int index) const
{
    const float w = (float) getWidth() / (float) jmax (1, labels.size());
    return { (float) index * w, 0.0f, w, (float) getHeight() };
}

void Segmented::paint (Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto r = cell (i).reduced (1.0f, 0.5f);
        const bool on = i == selected;
        g.setColour (on ? colours::ink : colours::panel.withAlpha (0.8f));
        g.fillRect (r);
        g.setColour (on ? colours::ink : colours::outline);
        g.drawRect (r, 1.0f);
        g.setColour (on ? Colours::white : colours::textDim);
        g.setFont (font (11.0f, true));
        g.drawText (labels[i], r, Justification::centred, false);
    }
}

void Segmented::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < labels.size(); ++i)
        if (cell (i).contains (e.position))
        {
            setSelected (i);

            if (onChange != nullptr)
                onChange (i);
        }
}

//==============================================================================
SettingsPage::SettingsPage (PanelContext& c)
    : processor (c.processor),
      autoLevel (c.processor.getState(), pid::autoLevel, "Auto level", colours::ink),
      clipGuard (c.processor.getState(), pid::clipGuard, "Clip guard", colours::ink),
      liveQuality (c.processor.getState(), pid::oversampling, { "1x", "2x", "4x", "8x" }, 4, colours::ink),
      renderQuality (c.processor.getState(), pid::renderOversampling, { "Same as live", "8x" }, 2, colours::ink),
      zoom ({ "80%", "90%", "100%", "110%", "125%", "150%" })
{
    for (auto* comp : std::initializer_list<Component*> { &autoLevel, &clipGuard, &liveQuality, &renderQuality, &zoom, &tooltips,
                                                          &shuffleOrder, &changeModulation, &importButton, &openFolder, &browseButton,
                                                          &changeFolder, &defaultFolder, &closeButton })
        addAndMakeVisible (comp);

    importButton.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Import Hollow presets", File::getSpecialLocation (File::userHomeDirectory),
                                                 String ("*") + HollowAudioProcessor::presetExtension);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                              [safe = Component::SafePointer<SettingsPage> (this)] (const FileChooser& fc)
                              {
                                  if (safe != nullptr && ! fc.getResults().isEmpty())
                                      safe->importFiles (fc.getResults());
                              });
    };

    changeFolder.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Choose where Hollow keeps presets", processor.getUserPresetFolder());
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                              [safe = Component::SafePointer<SettingsPage> (this)] (const FileChooser& fc)
                              {
                                  if (safe == nullptr || fc.getResult() == File())
                                      return;

                                  safe->settings->setPresetFolder (fc.getResult());
                                  safe->status = "Presets are now read from and saved to this folder.";

                                  if (safe->onPresetsChanged != nullptr)
                                      safe->onPresetsChanged();

                                  safe->refresh();
                              });
    };

    defaultFolder.onClick = [this]
    {
        settings->setPresetFolder (File());
        status = "Back to the default folder.";

        if (onPresetsChanged != nullptr)
            onPresetsChanged();

        refresh();
    };

    for (int i = 0; i < (int) std::size (Settings::zoomLevels); ++i)
        if (std::abs (Settings::zoomLevels[i] - settings->getZoom()) < 0.01f)
            zoom.setSelected (i);

    zoom.onChange = [this] (int i)
    {
        settings->setZoom (Settings::zoomLevels[i]);

        if (onZoomChanged != nullptr)
            onZoomChanged (Settings::zoomLevels[i]);
    };

    for (auto* t : { &tooltips, &shuffleOrder, &changeModulation })
        t->setColour (ToggleButton::tickColourId, colours::ink);

    tooltips.setToggleState (settings->getTooltips(), dontSendNotification);
    shuffleOrder.setToggleState (settings->getDiceShufflesOrder(), dontSendNotification);
    changeModulation.setToggleState (settings->getDiceChangesModulation(), dontSendNotification);

    tooltips.onClick = [this]
    {
        settings->setTooltips (tooltips.getToggleState());

        if (onTooltipsChanged != nullptr)
            onTooltipsChanged (tooltips.getToggleState());
    };

    shuffleOrder.onClick = [this] { settings->setDiceShufflesOrder (shuffleOrder.getToggleState()); };
    changeModulation.onClick = [this] { settings->setDiceChangesModulation (changeModulation.getToggleState()); };

    openFolder.onClick = [this]
    {
        const auto folder = processor.getUserPresetFolder();
        folder.createDirectory();
        folder.startAsProcess();
    };

    browseButton.onClick = [this]
    {
        if (onBrowse != nullptr)
            onBrowse();
    };

    closeButton.onClick = [this]
    {
        if (onClose != nullptr)
            onClose();
    };
}

void SettingsPage::importFiles (const Array<File>& files)
{
    StringArray problems;
    const auto imported = processor.importPresets (files, &problems);

    if (! imported.isEmpty())
        processor.loadUserPreset (imported.getFirst());

    status = imported.isEmpty() ? String ("Nothing imported.")
                                : "Imported " + String (imported.size()) + (imported.size() == 1 ? " preset" : " presets")
                                      + " and loaded " + imported.getFirst().getFileNameWithoutExtension() + ".";

    if (! problems.isEmpty())
        status << " " << problems.joinIntoString ("; ") << ".";

    if (onPresetsChanged != nullptr)
        onPresetsChanged();

    refresh();
}

void SettingsPage::refresh()
{
    defaultFolder.setEnabled (settings->hasCustomPresetFolder());
    repaint();
}

void SettingsPage::paint (Graphics& g)
{
    // Frosted sheet over the editor
    g.setColour (colours::background.withAlpha (0.95f));
    g.fillRect (getLocalBounds());
    drawLabel (g, "Menu", getLocalBounds().reduced (16, 10).removeFromTop (24).toFloat(), Justification::centredLeft, colours::ink, 12.0f);

    const char* titles[] = { "Level", "Quality", "Interface", "Dice", "Presets", "About" };

    for (size_t i = 0; i < cards.size(); ++i)
        drawCard (g, cards[i].toFloat(), titles[i]);

    const float level = processor.getMeters().autoLevelDb.load();

    // Level: toggle, text, readout / toggle, text (positions match resized())
    {
        auto c = cards[0].reduced (14, 10).withTrimmedTop (22);
        c.removeFromTop (34);
        drawNote (g, "Estimates from the settings how much louder or quieter the patch makes typical music and "
                     "compensates it at the output. Nothing is measured, so it is right before anything plays.", c.removeFromTop (80));
        g.setColour (colours::text);
        g.setFont (monoFont (12.0f));
        g.drawText ("compensating " + String (level > 0.05f ? "+" : "") + String (std::abs (level) < 0.05f ? 0.0f : level, 1) + " dB",
                    c.removeFromTop (20), Justification::centredLeft, false);
        c.removeFromTop (12 + 34);
        drawNote (g, "Soft ceiling at the very end: untouched below -3 dBFS, never above -0.3 dBFS. "
                     "Push the Output knob into it for output clipping.", c.removeFromTop (66));
    }

    // Quality
    {
        auto c = cards[1].reduced (14, 10).withTrimmedTop (22);
        drawLabel (g, "Oversampling", c.removeFromTop (18).toFloat(), Justification::centredLeft, colours::textDim, 9.5f);
        c.removeFromTop (34);
        drawNote (g, "How finely the distortion is computed. Higher is cleaner in the highs and costs CPU; 1x lets it alias "
                     "for a harder, metallic grit. The latency stays the same at every setting.", c.removeFromTop (94));
        drawLabel (g, "When rendering", c.removeFromTop (18).toFloat(), Justification::centredLeft, colours::textDim, 9.5f);
        c.removeFromTop (34);
        drawNote (g, "Bounces can use 8x even if you play at a lower setting.", c.removeFromTop (36));
    }

    // Interface
    {
        auto c = cards[2].reduced (14, 10).withTrimmedTop (22);
        drawLabel (g, "Size", c.removeFromTop (18).toFloat(), Justification::centredLeft, colours::textDim, 9.5f);
        c.removeFromTop (34);
        drawNote (g, "Scales the whole interface. Applies to every Hollow window on this computer.", c.removeFromTop (52));
        c.removeFromTop (34);
        drawNote (g, "Help text when you hover over a control.", c.removeFromTop (36));
    }

    // Dice
    {
        auto c = cards[3].reduced (14, 10).withTrimmedTop (22);
        drawNote (g, "Randomize always rolls which modules are on and their settings. Choose what else it may change:", c.removeFromTop (66));
        c.removeFromTop (34);
        drawNote (g, "the order of the modules in the chain", c.removeFromTop (36));
        c.removeFromTop (34);
        drawNote (g, "LFOs, envelope, macros and the modulation matrix", c.removeFromTop (36));
    }

    // Presets
    {
        auto c = cards[4].reduced (14, 10).withTrimmedTop (22);
        const auto library = processor.getPresetLibrary();
        const int count = library.scan().size(), folders = library.getFolders().size();
        drawNote (g, String (count) + (count == 1 ? " preset" : " presets") + " in " + String (folders) + (folders == 1 ? " folder" : " folders")
                     + (settings->hasCustomPresetFolder() ? ", in your folder" : ", in the default folder"), c.removeFromTop (20));
        g.setColour (colours::text);
        g.setFont (monoFont (11.5f));
        g.drawFittedText (processor.getUserPresetFolder().getFullPathName(), c.removeFromTop (22), Justification::centredLeft, 1, 0.8f);
        c.removeFromTop (8 + 30 + 8 + 30 + 10);
        const auto current = processor.getCurrentUserPreset();
        g.setColour (status.isNotEmpty() ? colours::accent : colours::textDim);
        g.setFont (font (12.0f));
        g.drawFittedText (status.isNotEmpty() ? status
                                              : (current.existsAsFile() ? "Loaded: " + current.getFileNameWithoutExtension()
                                                                        : String ("Save the current sound with SAVE in the header. Import .hollowpreset files "
                                                                                  "(or whole folders) here or by dropping them onto the plugin window.")),
                          c.removeFromTop (36), Justification::topLeft, 2, 1.0f);
    }

    // About
    {
        auto c = cards[5].reduced (14, 10).withTrimmedTop (22);
        const double rate = processor.getSampleRate();
        g.setColour (colours::ink);
        g.setFont (font (24.0f).withExtraKerningFactor (0.06f));
        g.drawText ("hollow", c.removeFromTop (32), Justification::centredLeft, false);

        const int factor = processor.getOversamplingFactor();
        StringArray lines;
        lines.add ("version 0.2");
        lines.add (rate > 0.0 ? "sample rate " + String (rate / 1000.0, 1) + " kHz, set by the host" : String ("sample rate: waiting for the host"));

        if (rate > 0.0)
            lines.add ("latency " + String (processor.getLatencySamples()) + " samples (" + String (1000.0 * processor.getLatencySamples() / rate, 2) + " ms)");

        lines.add (factor > 1 ? "distortion at " + String (factor) + "x oversampling" : String ("distortion without oversampling"));
        lines.add ("fonts: Oxanium, Martian Mono (SIL OFL)");
        lines.add ("built with JUCE");
        g.setFont (monoFont (11.0f));
        g.setColour (colours::textDim);

        for (const auto& line : lines)
            g.drawFittedText (line, c.removeFromTop (18), Justification::centredLeft, 1, 0.85f);
    }
}

void SettingsPage::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    auto top = area.removeFromTop (24);
    closeButton.setBounds (top.removeFromRight (90));
    area.removeFromTop (10);

    // 4 x 2 grid: level / quality / interface / dice, then presets and about (two columns each)
    const int gap = 10;
    const int colW = (area.getWidth() - 3 * gap) / 4;
    auto upper = area.removeFromTop ((area.getHeight() - gap) * 3 / 5);
    area.removeFromTop (gap);

    for (int i = 0; i < 4; ++i)
    {
        cards[(size_t) i] = i < 3 ? upper.removeFromLeft (colW) : upper;
        upper.removeFromLeft (gap);
    }

    cards[4] = area.removeFromLeft (2 * colW + gap);
    area.removeFromLeft (gap);
    cards[5] = area;

    {
        auto c = cards[0].reduced (14, 10).withTrimmedTop (22);
        autoLevel.setBounds (c.removeFromTop (26).withWidth (140));
        c.removeFromTop (8 + 80 + 20 + 12);
        clipGuard.setBounds (c.removeFromTop (26).withWidth (140));
    }

    {
        auto c = cards[1].reduced (14, 10).withTrimmedTop (22);
        c.removeFromTop (18);
        liveQuality.setBounds (c.removeFromTop (28));
        c.removeFromTop (6 + 94 + 18);
        renderQuality.setBounds (c.removeFromTop (28));
    }

    {
        auto c = cards[2].reduced (14, 10).withTrimmedTop (22);
        c.removeFromTop (18);
        zoom.setBounds (c.removeFromTop (28));
        c.removeFromTop (6 + 52);
        tooltips.setBounds (c.removeFromTop (26).withWidth (140));
    }

    {
        auto c = cards[3].reduced (14, 10).withTrimmedTop (22);
        c.removeFromTop (66);
        shuffleOrder.setBounds (c.removeFromTop (26).withWidth (170));
        c.removeFromTop (8 + 36);
        changeModulation.setBounds (c.removeFromTop (26).withWidth (170));
    }

    {
        auto c = cards[4].reduced (14, 10).withTrimmedTop (22);
        c.removeFromTop (20 + 22 + 8);
        auto row = c.removeFromTop (30);
        importButton.setBounds (row.removeFromLeft (140));
        row.removeFromLeft (8);
        openFolder.setBounds (row.removeFromLeft (140));
        row.removeFromLeft (8);
        browseButton.setBounds (row.removeFromLeft (150));
        c.removeFromTop (8);
        row = c.removeFromTop (30);
        changeFolder.setBounds (row.removeFromLeft (170));
        row.removeFromLeft (8);
        defaultFolder.setBounds (row.removeFromLeft (110));
    }
}

//==============================================================================
SavePresetDialog::SavePresetDialog()
{
    addAndMakeVisible (nameEditor);
    addAndMakeVisible (folderBox);
    addAndMakeVisible (save);
    addAndMakeVisible (cancel);

    folderBox.onChange = [this] { repaint(); };

    nameEditor.setFont (font (16.0f));
    nameEditor.setIndents (10, 6);
    nameEditor.setJustification (Justification::centredLeft);
    nameEditor.onReturnKey = [this] { confirm(); };
    nameEditor.onEscapeKey = [this] { if (onCancel != nullptr) onCancel(); };
    nameEditor.onTextChange = [this] { repaint(); };

    save.setToggleState (true, dontSendNotification); // drawn as the primary (ink) button
    save.onClick = [this] { confirm(); };
    cancel.onClick = [this] { if (onCancel != nullptr) onCancel(); };
}

void SavePresetDialog::open (const String& name, const StringArray& folders, const String& folder,
                             std::function<bool (const String&, const String&)> exists)
{
    folderNames = folders;
    existsCheck = std::move (exists);
    folderBox.clear (dontSendNotification);
    folderBox.addItem ("Top level", 1);

    for (int i = 0; i < folderNames.size(); ++i)
        folderBox.addItem (folderNames[i], i + 2);

    folderBox.setSelectedId (folderNames.indexOf (folder) + 2, dontSendNotification);
    nameEditor.setText (name, false);
    nameEditor.selectAll();
    setVisible (true);
    toFront (true);
    nameEditor.grabKeyboardFocus();
    repaint();
}

void SavePresetDialog::confirm()
{
    const auto name = nameEditor.getText().trim();

    if (name.isNotEmpty() && onSave != nullptr)
        onSave (name, chosenFolder());
}

String SavePresetDialog::chosenFolder() const
{
    return folderNames[folderBox.getSelectedId() - 2];
}

Rectangle<int> SavePresetDialog::card() const
{
    return getLocalBounds().withSizeKeepingCentre (420, 214);
}

void SavePresetDialog::paint (Graphics& g)
{
    g.setColour (colours::ink.withAlpha (0.28f));
    g.fillRect (getLocalBounds());

    auto c = drawCard (g, card().toFloat(), "Save preset", colours::ink);
    c.removeFromTop (40);
    drawLabel (g, "Folder", c.removeFromTop (30).withWidth (60.0f), Justification::centredLeft, colours::textDim, 9.5f);
    c.removeFromTop (6);

    const auto name = nameEditor.getText().trim();
    const auto folder = chosenFolder();
    const bool replaces = name.isNotEmpty() && existsCheck != nullptr && existsCheck (folder, name);
    g.setColour (replaces ? colours::accent : colours::textDim);
    g.setFont (font (12.0f));
    g.drawText (replaces ? "A preset with this name is in this folder and will be replaced."
                         : (folder.isEmpty() ? String ("Saved at the top of your presets.") : "Saved in the folder " + folder + "."),
                c.removeFromTop (22), Justification::centredLeft, false);
}

void SavePresetDialog::resized()
{
    auto c = card().reduced (12, 8).withTrimmedTop (20);
    nameEditor.setBounds (c.removeFromTop (32));
    c.removeFromTop (8);
    folderBox.setBounds (c.removeFromTop (30).withTrimmedLeft (64).withWidth (240));
    c.removeFromTop (30);
    auto row = c.removeFromBottom (30);
    save.setBounds (row.removeFromRight (100));
    row.removeFromRight (8);
    cancel.setBounds (row.removeFromRight (100));
}

void SavePresetDialog::mouseDown (const MouseEvent& e)
{
    if (! card().contains (e.getPosition()) && onCancel != nullptr)
        onCancel();
}

} // namespace hl::gui
