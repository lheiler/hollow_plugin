#pragma once

#include "PanelBase.h"
#include "plugin/Settings.h"

namespace hl::gui
{
/** Row of mutually exclusive buttons that isn't bound to a parameter. */
class Segmented final : public juce::Component
{
public:
    Segmented (juce::StringArray labels);

    std::function<void (int)> onChange;
    void setSelected (int index);
    int getSelected() const noexcept { return selected; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cell (int index) const;
    juce::StringArray labels;
    int selected = 0;
};

/** The menu page: output safety, interface, dice behaviour, preset folder and about. */
class SettingsPage final : public juce::Component
{
public:
    explicit SettingsPage (PanelContext& context);

    std::function<void (float)> onZoomChanged;
    std::function<void (bool)> onTooltipsChanged;
    std::function<void()> onPresetsChanged;
    std::function<void()> onClose;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    HollowAudioProcessor& processor;
    juce::SharedResourcePointer<Settings> settings;

    void importFiles (const juce::Array<juce::File>& files);

    ParamToggle autoLevel, clipGuard;
    ChoiceButtons liveQuality, renderQuality;
    Segmented zoom;
    juce::ToggleButton tooltips { "Tooltips" }, shuffleOrder { "Shuffle order" }, changeModulation { "Modulation" };
    juce::TextButton importButton { "Import..." }, openFolder { "Open folder" }, deletePreset { "Delete current" },
                     changeFolder { "Change folder..." }, defaultFolder { "Default" }, closeButton { "Close" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;

    std::array<juce::Rectangle<int>, 6> cards;

public:
    /** Imports preset files (also used for drag and drop onto the editor). */
    void importPresetFiles (const juce::Array<juce::File>& files) { importFiles (files); }
};

/** "Save preset" prompt shown over the whole editor. */
class SavePresetDialog final : public juce::Component
{
public:
    SavePresetDialog();

    std::function<void (const juce::String&)> onSave;
    std::function<void()> onCancel;

    /** Shows the dialog with `name` prefilled; `existing` lists names that would be replaced. */
    void open (const juce::String& name, juce::StringArray existing);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void confirm();
    juce::Rectangle<int> card() const;

    juce::TextEditor nameEditor;
    juce::TextButton save { "Save" }, cancel { "Cancel" };
    juce::StringArray existingNames;
};

} // namespace hl::gui
