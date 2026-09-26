#pragma once

#include <juce_core/juce_core.h>

namespace hl
{
/** The preset folder on disk. Presets sit at the top level or in folders one level down; each folder is a
    category in the preset menu and the browser. Nothing is ever overwritten (a clash gets " (2)") and
    deleting moves things to the Recycle Bin / Trash. Message thread. */
class PresetLibrary
{
public:
    explicit PresetLibrary (const juce::File& rootFolder) : root (rootFolder) {}

    static constexpr const char* extension = ".hollowpreset";

    struct Entry
    {
        juce::File file;
        juce::String folder; // empty = top level

        juce::String getName() const { return file.getFileNameWithoutExtension(); }
    };

    const juce::File& getRoot() const noexcept { return root; }

    /** Every preset in menu order: the top level first, then the folders A-Z, names A-Z within each. */
    juce::Array<Entry> scan() const;

    /** The folders (empty ones too), A-Z. */
    juce::StringArray getFolders() const;

    juce::File getFolder (const juce::String& folder) const { return folder.isEmpty() ? root : root.getChildFile (folder); }

    /** "Drive" for Presets/Drive/x.hollowpreset, empty at the top level. */
    juce::String folderOf (const juce::File& preset) const;

    /** Arranging. Each returns the preset's new file, or File() if nothing happened. */
    juce::File move (const juce::File& preset, const juce::String& folder) const;
    juce::File rename (const juce::File& preset, const juce::String& newName) const; // also renames it inside the file
    bool remove (const juce::File& preset) const;

    bool createFolder (const juce::String& name) const;
    bool renameFolder (const juce::String& from, const juce::String& to) const;
    bool removeFolder (const juce::String& name) const; // with the presets in it

    /** Copies presets in, each into the folder its file names as category (top level if none). A preset that is
        already in the library with identical content isn't copied again. Returns the resulting files, in order. */
    juce::Array<juce::File> import (const juce::Array<juce::File>& files, juce::StringArray* problems = nullptr) const;

    /** Moves top-level presets that name a category into that folder (for presets imported before folders). */
    int sortIntoCategories() const;

    /** A free file for `name` in `folder`: "name.hollowpreset", else "name (2).hollowpreset", ... */
    juce::File freeFile (const juce::String& folder, const juce::String& name) const;

    static bool isPresetFile (const juce::File& file);
    static juce::String getCategory (const juce::File& file);

    /** A name that works as a file or folder name everywhere (empty if nothing usable is left). */
    static juce::String cleanName (const juce::String& name);

private:
    juce::File root;
};

} // namespace hl
