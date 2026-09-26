#include "PresetLibrary.h"

namespace hl
{
using namespace juce;

namespace
{
    String wildcard() { return String ("*") + PresetLibrary::extension; }

    Array<File> sortedByName (Array<File> files)
    {
        std::sort (files.begin(), files.end(), [] (const File& a, const File& b)
        {
            return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0;
        });

        return files;
    }

    /** Presets and folders go to the Recycle Bin / Trash, so a slip can be undone there.
        The test harness deletes for real instead of filling the bin. */
    bool discard (const File& f)
    {
       #ifdef HOLLOW_SNAPSHOT_TOOL
        return f.isDirectory() ? f.deleteRecursively() : f.deleteFile();
       #else
        return f.moveToTrash() || (f.isDirectory() ? f.deleteRecursively() : f.deleteFile());
       #endif
    }
} // namespace

String PresetLibrary::cleanName (const String& name)
{
    // Legal on every system, and no leading dots (hidden) or trailing dots/spaces (Windows drops them)
    auto clean = File::createLegalFileName (name.trim()).trimCharactersAtStart (". ").trimCharactersAtEnd (". ");
    return clean.substring (0, 100).trim();
}

bool PresetLibrary::isPresetFile (const File& file)
{
    if (! file.existsAsFile() || ! file.hasFileExtension (extension) || file.getSize() > 1024 * 1024)
        return false;

    const auto xml = XmlDocument::parse (file);
    return xml != nullptr && xml->hasTagName ("HollowPreset");
}

String PresetLibrary::getCategory (const File& file)
{
    XmlDocument doc (file);
    const auto rootElement = doc.getDocumentElement (true); // just the outer element
    return rootElement != nullptr && rootElement->hasTagName ("HollowPreset") ? rootElement->getStringAttribute ("category").trim() : String();
}

StringArray PresetLibrary::getFolders() const
{
    StringArray names;

    for (const auto& dir : root.findChildFiles (File::findDirectories, false))
        if (! dir.getFileName().startsWithChar ('.') && ! dir.isHidden())
            names.add (dir.getFileName());

    names.sortNatural();
    return names;
}

Array<PresetLibrary::Entry> PresetLibrary::scan() const
{
    Array<Entry> entries;

    for (const auto& f : sortedByName (root.findChildFiles (File::findFiles, false, wildcard())))
        entries.add ({ f, {} });

    for (const auto& folder : getFolders())
        for (const auto& f : sortedByName (root.getChildFile (folder).findChildFiles (File::findFiles, false, wildcard())))
            entries.add ({ f, folder });

    return entries;
}

String PresetLibrary::folderOf (const File& preset) const
{
    const auto parent = preset.getParentDirectory();
    return parent != root && parent.getParentDirectory() == root ? parent.getFileName() : String();
}

File PresetLibrary::freeFile (const String& folder, const String& name) const
{
    const auto dir = getFolder (folder);
    auto target = dir.getChildFile (name + extension);

    for (int n = 2; target.exists(); ++n)
        target = dir.getChildFile (name + " (" + String (n) + ")" + extension);

    return target;
}

File PresetLibrary::move (const File& preset, const String& folder) const
{
    if (! preset.existsAsFile() || ! preset.isAChildOf (root))
        return {};

    if (preset.getParentDirectory() == getFolder (folder))
        return preset;

    if (! getFolder (folder).createDirectory())
        return {};

    const auto target = freeFile (folder, preset.getFileNameWithoutExtension());
    return preset.moveFileTo (target) ? target : File();
}

File PresetLibrary::rename (const File& preset, const String& newName) const
{
    const auto clean = cleanName (newName);

    if (clean.isEmpty() || ! preset.existsAsFile() || ! preset.isAChildOf (root))
        return {};

    const auto folder = folderOf (preset);
    auto target = preset;

    if (clean != preset.getFileNameWithoutExtension())
    {
        // A change of case only is still the same file on Windows and macOS
        target = clean.equalsIgnoreCase (preset.getFileNameWithoutExtension()) ? getFolder (folder).getChildFile (clean + extension)
                                                                                 : freeFile (folder, clean);

        if (! preset.moveFileTo (target))
            return {};
    }

    // The name inside the file is what the header shows once it's loaded
    if (auto xml = XmlDocument::parse (target); xml != nullptr && xml->hasTagName ("HollowPreset"))
    {
        xml->setAttribute ("name", newName.trim());
        xml->writeTo (target);
    }

    return target;
}

bool PresetLibrary::remove (const File& preset) const
{
    return preset.existsAsFile() && preset.isAChildOf (root) && preset.hasFileExtension (extension) && discard (preset);
}

bool PresetLibrary::createFolder (const String& name) const
{
    const auto clean = cleanName (name);
    return clean.isNotEmpty() && ! root.getChildFile (clean).exists() && root.getChildFile (clean).createDirectory().wasOk();
}

bool PresetLibrary::renameFolder (const String& from, const String& to) const
{
    const auto clean = cleanName (to);
    const auto source = root.getChildFile (from);

    if (from.isEmpty() || clean.isEmpty() || ! source.isDirectory() || source.getParentDirectory() != root)
        return false;

    if (clean == from)
        return true;

    const auto target = root.getChildFile (clean);

    if (target.exists() && ! clean.equalsIgnoreCase (from))
        return false; // never merge into (or replace) another folder

    return source.moveFileTo (target);
}

bool PresetLibrary::removeFolder (const String& name) const
{
    const auto dir = root.getChildFile (name);
    return name.isNotEmpty() && dir.isDirectory() && dir.getParentDirectory() == root && discard (dir);
}

Array<File> PresetLibrary::import (const Array<File>& files, StringArray* problems) const
{
    Array<File> result;

    if (! root.createDirectory())
    {
        if (problems != nullptr)
            problems->add ("can't create " + root.getFullPathName());

        return result;
    }

    for (const auto& source : files)
    {
        if (! isPresetFile (source))
        {
            if (problems != nullptr)
                problems->add (source.getFileName() + " is not a Hollow preset");

            continue;
        }

        if (source.isAChildOf (root))
        {
            result.add (source); // already in the library
            continue;
        }

        // Already imported (under any name, in any folder)? Then just point at that copy
        File identical;

        for (const auto& existing : scan())
            if (existing.file.getSize() == source.getSize() && existing.file.hasIdenticalContentTo (source))
                identical = existing.file;

        if (identical != File())
        {
            result.add (identical);
            continue;
        }

        const auto folder = cleanName (getCategory (source));

        if (! getFolder (folder).createDirectory())
        {
            if (problems != nullptr)
                problems->add ("can't create the folder " + folder);

            continue;
        }

        const auto target = freeFile (folder, source.getFileNameWithoutExtension());

        if (source.copyFileTo (target))
            result.add (target);
        else if (problems != nullptr)
            problems->add ("couldn't copy " + source.getFileName());
    }

    return result;
}

int PresetLibrary::sortIntoCategories() const
{
    int moved = 0;

    for (const auto& f : root.findChildFiles (File::findFiles, false, wildcard()))
        if (const auto folder = cleanName (getCategory (f)); folder.isNotEmpty() && move (f, folder) != File())
            ++moved;

    return moved;
}

} // namespace hl
