#include "Settings.h"

namespace hl
{
using namespace juce;

Settings::Settings()
{
    PropertiesFile::Options options;
   #ifdef HOLLOW_SNAPSHOT_TOOL
    options.applicationName = "HollowSnapshot"; // the test harness never touches your settings
   #else
    options.applicationName = "Hollow";
   #endif
    options.filenameSuffix = "settings";
    options.folderName = "Hollow";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = PropertiesFile::storeAsXML;
    file = std::make_unique<PropertiesFile> (options);
}

float Settings::getZoom() const
{
    const float zoom = (float) file->getDoubleValue ("zoom", 1.0);

    for (float z : zoomLevels)
        if (std::abs (z - zoom) < 0.01f)
            return z;

    return 1.0f;
}

void Settings::setZoom (float zoom)
{
    file->setValue ("zoom", zoom);
    file->saveIfNeeded();
}

bool Settings::getBool (const String& key, bool fallback) const
{
    return file->getBoolValue (key, fallback);
}

void Settings::setBool (const String& key, bool value)
{
    file->setValue (key, value);
    file->saveIfNeeded();
}

File Settings::getPresetFolder() const
{
    const auto custom = file->getValue ("presetFolder");

    if (custom.isNotEmpty() && File::isAbsolutePath (custom))
        return File (custom);

    return getDefaultPresetFolder();
}

void Settings::setPresetFolder (const File& folder)
{
    if (folder == File() || folder == getDefaultPresetFolder())
        file->removeValue ("presetFolder");
    else
        file->setValue ("presetFolder", folder.getFullPathName());

    file->saveIfNeeded();
}

bool Settings::hasCustomPresetFolder() const
{
    return file->getValue ("presetFolder").isNotEmpty();
}

File Settings::getDefaultPresetFolder()
{
   #ifdef HOLLOW_SNAPSHOT_TOOL
    return File::getSpecialLocation (File::tempDirectory).getChildFile ("HollowSnapshot").getChildFile ("Presets");
   #else
    return File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Hollow").getChildFile ("Presets");
   #endif
}

namespace
{
    XmlElement* findFolder (XmlElement& folders, const File& folder)
    {
        for (auto* e : folders.getChildWithTagNameIterator ("Folder"))
            if (File (e->getStringAttribute ("path")) == folder)
                return e;

        return nullptr;
    }
} // namespace

bool Settings::knowsPresetFolder (const File& folder) const
{
    const auto folders = file->getXmlValue ("presetFolders");
    return folders != nullptr && findFolder (*folders, folder) != nullptr;
}

StringArray Settings::getFactoryPresetsIn (const File& folder) const
{
    StringArray names;

    if (const auto folders = file->getXmlValue ("presetFolders"))
        if (auto* e = findFolder (*folders, folder))
            names.addTokens (e->getStringAttribute ("factory"), "|", {});

    names.removeEmptyStrings();
    return names;
}

void Settings::setFactoryPresetsIn (const File& folder, const StringArray& names)
{
    auto folders = file->getXmlValue ("presetFolders");

    if (folders == nullptr)
        folders = std::make_unique<XmlElement> ("PresetFolders");

    auto* e = findFolder (*folders, folder);

    if (e == nullptr)
    {
        e = folders->createNewChildElement ("Folder");
        e->setAttribute ("path", folder.getFullPathName());
    }

    e->setAttribute ("factory", names.joinIntoString ("|"));
    file->setValue ("presetFolders", folders.get());
    file->saveIfNeeded();
}

} // namespace hl
