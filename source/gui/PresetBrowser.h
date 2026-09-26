#pragma once

#include "PanelBase.h"

namespace hl::gui
{
/** Small in-window question: a title, a line of text, optionally a name field, and a confirm button. */
class Prompt final : public juce::Component
{
public:
    Prompt();

    /** `withField` shows a text field prefilled with `text`; the result goes to `onConfirm`. */
    void ask (const juce::String& title, const juce::String& message, bool withField, const juce::String& text,
              const juce::String& confirmText, std::function<void (const juce::String&)> onConfirm);
    void close();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void confirm();
    juce::Rectangle<int> card() const;

    juce::String title, message;
    bool hasField = false;
    juce::TextEditor field;
    juce::TextButton confirmButton, cancelButton { "Cancel" };
    std::function<void (const juce::String&)> action;
};

/** The preset library as a page: folders on the left, presets on the right. Click a preset to load it,
    double-click to load and close, drag it onto a folder to move it, right-click to rename, move or delete. */
class PresetBrowser final : public juce::Component,
                            public juce::DragAndDropContainer
{
public:
    explicit PresetBrowser (HollowAudioProcessor& processor);
    ~PresetBrowser() override;

    std::function<void()> onClose, onLibraryChanged, onPresetLoaded;

    /** Rescans the preset folder (when the page opens and after every change). */
    void refresh();

    /** Follows the loaded preset (cheap; called from the UI timer). */
    void followCurrent();

    void importFiles (const juce::Array<juce::File>& files);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class FolderList;
    class PresetGrid;

    struct Group
    {
        juce::String folder, title;
        juce::Array<int> entries; // indices into `entries`
    };

    std::vector<Group> visibleGroups() const;
    int countIn (const juce::String& folder) const;

    void selectAll();
    void selectFolder (const juce::String& folder);
    void load (const juce::File& file, bool thenClose);
    void moveTo (const juce::File& file, const juce::String& folder);
    void showPresetMenu (const juce::File& file);
    void showFolderMenu (const juce::String& folder);
    void askRename (const juce::File& file);
    void askNewFolder (const juce::File& thenMove);
    void askRenameFolder (const juce::String& folder);
    void askDeleteFolder (const juce::String& folder);
    void changed (const juce::String& newStatus);
    void updateButtons();

    HollowAudioProcessor& processor;
    juce::Array<PresetLibrary::Entry> entries;
    juce::StringArray folders;
    bool showingAll = true;
    juce::String selectedFolder; // when not showing all: "" = top level

    std::unique_ptr<FolderList> folderList;
    std::unique_ptr<PresetGrid> grid;
    juce::Viewport folderView, gridView;
    juce::TextEditor search;
    juce::TextButton newFolder { "New" }, renameFolder { "Rename" }, deleteFolder { "Delete" }, importButton { "Import..." },
                     restoreButton { "Restore factory" }, openFolder { "Open folder" }, closeButton { "Close" };
    Prompt prompt;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;
    juce::File shownCurrent;
    juce::Rectangle<int> foldersCard, presetsCard, statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};

} // namespace hl::gui
