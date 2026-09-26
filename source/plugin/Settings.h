#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace hl
{
/** Preferences shared by every Hollow instance on this machine (not saved with sessions).
    Use through juce::SharedResourcePointer<Settings>. */
class Settings
{
public:
    Settings();

    static constexpr float zoomLevels[] = { 0.8f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f };

    float getZoom() const;
    void setZoom (float zoom);

    bool getTooltips() const { return getBool ("tooltips", true); }
    void setTooltips (bool on) { setBool ("tooltips", on); }

    /** What the dice may change besides the modules' settings. */
    bool getDiceShufflesOrder() const { return getBool ("diceShufflesOrder", true); }
    void setDiceShufflesOrder (bool on) { setBool ("diceShufflesOrder", on); }
    bool getDiceChangesModulation() const { return getBool ("diceChangesModulation", true); }
    void setDiceChangesModulation (bool on) { setBool ("diceChangesModulation", on); }

    /** Where saved presets live: the folder chosen in the menu, else Documents/Hollow/Presets. */
    juce::File getPresetFolder() const;
    void setPresetFolder (const juce::File& folder); // File() = back to the default
    bool hasCustomPresetFolder() const;

    static juce::File getDefaultPresetFolder();

    /** Which factory presets Hollow has already put into a preset folder, so the ones you delete stay deleted.
        `knowsPresetFolder` is false until Hollow has set a folder up once. */
    bool knowsPresetFolder (const juce::File& folder) const;
    juce::StringArray getFactoryPresetsIn (const juce::File& folder) const;
    void setFactoryPresetsIn (const juce::File& folder, const juce::StringArray& names);

   #ifdef HOLLOW_SNAPSHOT_TOOL
    void clearAll() { file->clear(); file->saveIfNeeded(); }
   #endif

private:
    bool getBool (const juce::String& key, bool fallback) const;
    void setBool (const juce::String& key, bool value);

    std::unique_ptr<juce::PropertiesFile> file;
};

} // namespace hl
