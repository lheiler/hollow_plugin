#pragma once

#include "PluginProcessor.h"
#include "gui/ChainStrip.h"
#include "gui/ConvolvePanel.h"
#include "gui/EffectPanels.h"
#include "gui/FilterPanel.h"
#include "gui/MeterPanel.h"
#include "gui/ModulationPanel.h"
#include "gui/PresetBrowser.h"
#include "gui/SettingsPage.h"
#include "gui/TrashPanel.h"

namespace hl
{
class HollowAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         public juce::FileDragAndDropTarget,
                                         private juce::Timer
{
public:
    explicit HollowAudioProcessorEditor (HollowAudioProcessor&);
    ~HollowAudioProcessorEditor() override;

    void resized() override;

    void selectModule (int moduleId);
    int getSelectedModule() const noexcept { return currentModule; }

    /** Menu page and save prompt (also used by the screenshot tool). */
    void showMenu (bool shouldShow);
    void showSaveDialog();
    void showBrowser (bool shouldShow);

    /** Interface zoom (the layout happens at 100% and is scaled as a whole). */
    void setZoom (float newZoom);

    /** One UI frame: drains the analysis FIFOs and refreshes meters and the visible panel. */
    void tick();

    // Drop .hollowpreset files anywhere on the window to import them
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** Ctrl+Z undo, Ctrl+Y / Ctrl+Shift+Z redo (Cmd on macOS). */
    bool keyPressed (const juce::KeyPress&) override;

private:
    /** Everything lives in here so the whole interface can be zoomed with one transform. */
    struct Content final : public juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter != nullptr) painter (g); }
    };

    /** The preset list re-reads the preset folder every time it opens. */
    struct PresetBox final : public juce::ComboBox
    {
        std::function<void()> beforePopup;

        void showPopup() override
        {
            if (beforePopup != nullptr)
                beforePopup();

            ComboBox::showPopup();
        }
    };

    /** Small curved-arrow button (undo, or mirrored for redo). */
    struct ArrowButton final : public juce::Button
    {
        ArrowButton (const juce::String& name, bool mirrored) : Button (name), mirror (mirrored) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        const bool mirror;
    };

    struct DropHint final : public juce::Component
    {
        DropHint() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
    };

    void syncPresetSelection();

    void timerCallback() override { tick(); }
    void paintContent (juce::Graphics&);
    void layoutContent();
    gui::ModulePanel* panelFor (int moduleId);
    void rebuildPresetMenu();
    void stepPreset (int delta);

    HollowAudioProcessor& processor;
    juce::SharedResourcePointer<Settings> settings;
    gui::LookAndFeel lookAndFeel;
    Content content;
    float zoom = 1.0f;

    gui::SpectrumAnalyzer spectrum;
    gui::ScopeData scope;
    gui::PanelContext context { processor, spectrum, scope };

    gui::ChainStrip chainStrip;
    gui::FilterPanel filter1Panel, filter2Panel;
    gui::TrashPanel trashPanel;
    gui::ConvolvePanel convolvePanel;
    gui::MotionPanel motionPanel;
    gui::DegradePanel degradePanel;
    gui::DynamicsPanel dynamicsPanel;
    gui::EchoPanel echoPanel;
    gui::ModulationPanel modulationPanel;
    gui::MeterPanel meterPanel;
    gui::SettingsPage settingsPage;
    gui::PresetBrowser browser;
    gui::SavePresetDialog saveDialog;
    gui::ParamToggle bypassButton;

    PresetBox presetBox;
    DropHint dropHint;
    ArrowButton undoButton { "Undo", false }, redoButton { "Redo", true };
    juce::TextButton previousPreset { "<" }, nextPreset { ">" }, browseButton { "Browse" }, saveButton { "Save" }, diceButton { "Randomize" },
                     menuButton { "Menu" };
    juce::TooltipWindow tooltips { this, 700 };

    juce::Array<juce::File> userPresetFiles;
    static constexpr int userPresetIdOffset = 1000, browseItemId = 900;

    int currentModule = dsp::moduleTrash;
    juce::String shownPreset;
    double shownRate = -1.0;
    int shownLatency = -1, shownFactor = -1;
    juce::Image backdrop;
    juce::Rectangle<int> headerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HollowAudioProcessorEditor)
};

} // namespace hl
