#include "Settings.h"

namespace hl
{
using namespace juce;

Settings::Settings()
{
    PropertiesFile::Options options;
    options.applicationName = "Hollow";
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
    return File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Hollow").getChildFile ("Presets");
}

} // namespace hl
