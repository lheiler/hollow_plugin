#include "PresetBrowser.h"

namespace hl::gui
{
using namespace juce;

namespace
{
    constexpr int folderRowHeight = 30, cellHeight = 28, headingHeight = 30, minCellWidth = 200, scrollbarWidth = 8;

   #if JUCE_MAC
    const char* const trashName = "Trash";
    const char* const revealText = "Show in Finder";
   #else
    const char* const trashName = "Recycle Bin";
    const char* const revealText = "Show in Explorer";
   #endif

    String presetCount (int n) { return String (n) + (n == 1 ? " preset" : " presets"); }

    void styleScrollbars (Viewport& view)
    {
        view.setScrollBarThickness (scrollbarWidth);
        view.setScrollBarsShown (true, false);
        view.getVerticalScrollBar().setColour (ScrollBar::thumbColourId, colours::ice);
        view.getVerticalScrollBar().setColour (ScrollBar::trackColourId, Colours::transparentBlack);
    }
} // namespace

//==============================================================================
Prompt::Prompt()
{
    addChildComponent (field);
    addAndMakeVisible (confirmButton);
    addAndMakeVisible (cancelButton);
    setWantsKeyboardFocus (true);

    field.setFont (font (16.0f));
    field.setIndents (10, 6);
    field.setJustification (Justification::centredLeft);
    field.onReturnKey = [this] { confirm(); };
    field.onEscapeKey = [this] { close(); };

    confirmButton.setToggleState (true, dontSendNotification); // drawn as the primary (ink) button
    confirmButton.onClick = [this] { confirm(); };
    cancelButton.onClick = [this] { close(); };
}

void Prompt::ask (const String& newTitle, const String& newMessage, bool withField, const String& text,
                  const String& confirmText, std::function<void (const String&)> onConfirm)
{
    title = newTitle;
    message = newMessage;
    hasField = withField;
    action = std::move (onConfirm);
    confirmButton.setButtonText (confirmText);
    field.setVisible (withField);
    field.setText (text, false);

    setVisible (true);
    toFront (true);
    resized();
    repaint();

    if (withField)
    {
        field.grabKeyboardFocus();
        field.selectAll();
    }
    else
    {
        grabKeyboardFocus();
    }
}

void Prompt::close()
{
    setVisible (false);
    action = nullptr;
}

void Prompt::confirm()
{
    const auto text = field.getText().trim();

    if (hasField && text.isEmpty())
        return;

    auto then = std::move (action);
    close();

    if (then != nullptr)
        then (text);
}

bool Prompt::keyPressed (const KeyPress& key)
{
    if (key == KeyPress::returnKey)
        confirm();
    else if (key == KeyPress::escapeKey)
        close();
    else
        return false;

    return true;
}

Rectangle<int> Prompt::card() const
{
    return getLocalBounds().withSizeKeepingCentre (440, hasField ? 196 : 160);
}

void Prompt::paint (Graphics& g)
{
    g.setColour (colours::ink.withAlpha (0.28f));
    g.fillRect (getLocalBounds());

    auto c = drawCard (g, card().toFloat(), title, colours::ink);

    if (hasField)
        c.removeFromTop (42.0f);

    g.setColour (colours::textDim);
    g.setFont (font (12.5f));
    g.drawFittedText (message, c.removeFromTop (44.0f).toNearestInt(), Justification::topLeft, 3, 1.0f);
}

void Prompt::resized()
{
    auto c = card().reduced (12, 8).withTrimmedTop (20);

    if (hasField)
        field.setBounds (c.removeFromTop (32));

    auto row = c.removeFromBottom (30);
    confirmButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (8);
    cancelButton.setBounds (row.removeFromRight (100));
}

void Prompt::mouseDown (const MouseEvent& e)
{
    if (! card().contains (e.getPosition()))
        close();
}

//==============================================================================
class PresetBrowser::FolderList final : public Component,
                                        public DragAndDropTarget
{
public:
    explicit FolderList (PresetBrowser& o) : owner (o) {}

    struct Row
    {
        String label, folder;
        bool all;
        int count;
    };

    std::vector<Row> rows() const
    {
        std::vector<Row> r;
        r.push_back ({ "All presets", {}, true, owner.entries.size() });
        r.push_back ({ "Top level", {}, false, owner.countIn ({}) });

        for (const auto& f : owner.folders)
            r.push_back ({ f, f, false, owner.countIn (f) });

        return r;
    }

    int getContentHeight() const { return (int) rows().size() * folderRowHeight; }

    void paint (Graphics& g) override
    {
        const auto all = rows();
        const auto currentFolder = owner.processor.getPresetLibrary().folderOf (owner.shownCurrent);
        const bool currentInLibrary = owner.shownCurrent.isAChildOf (owner.processor.getUserPresetFolder());

        for (int i = 0; i < (int) all.size(); ++i)
        {
            const auto& row = all[(size_t) i];
            const auto r = rowBounds (i).toFloat().reduced (0.0f, 1.5f);
            const bool selected = row.all ? owner.showingAll : (! owner.showingAll && owner.selectedFolder == row.folder);

            g.setColour (selected ? colours::ink : (i == hovered ? colours::panelRaised : Colours::transparentBlack));
            g.fillRect (r);

            if (i == dropRow)
            {
                g.setColour (colours::accent);
                g.drawRect (r, 2.0f);
            }

            // The folder the loaded preset lives in gets a small orange mark
            if (! row.all && currentInLibrary && row.folder == currentFolder)
            {
                g.setColour (colours::accent);
                g.fillRect (r.withWidth (3.0f));
            }

            auto text = r.reduced (10.0f, 0.0f);
            g.setColour (selected ? colours::panel.withAlpha (0.7f) : colours::textFaint);
            g.setFont (monoFont (11.0f));
            g.drawText (String (row.count), text.removeFromRight (36.0f), Justification::centredRight, false);

            g.setColour (selected ? Colours::white : (row.all || row.folder.isEmpty() ? colours::textDim : colours::text));
            g.setFont (font (13.5f, selected));
            g.drawFittedText (row.label, text.toNearestInt(), Justification::centredLeft, 1, 0.9f);

            if (i == 1)
            {
                g.setColour (colours::grid);
                g.fillRect (r.getX(), r.getBottom() + 1.0f, r.getWidth(), 1.0f);
            }
        }
    }

    void mouseMove (const MouseEvent& e) override { setHovered (rowAt (e.y)); }
    void mouseExit (const MouseEvent&) override { setHovered (-1); }

    void mouseDown (const MouseEvent& e) override
    {
        const int i = rowAt (e.y);

        if (i < 0)
            return;

        const auto row = rows()[(size_t) i];

        if (row.all)
            owner.selectAll();
        else
            owner.selectFolder (row.folder);

        if (e.mods.isPopupMenu() && row.folder.isNotEmpty())
            owner.showFolderMenu (row.folder);
    }

    bool isInterestedInDragSource (const SourceDetails& details) override { return details.description.isString(); }

    void itemDragMove (const SourceDetails& details) override
    {
        const int i = rowAt (details.localPosition.y);
        const int target = i > 0 ? i : -1; // not onto "All presets"

        if (target != dropRow)
        {
            dropRow = target;
            repaint();
        }
    }

    void itemDragExit (const SourceDetails&) override
    {
        dropRow = -1;
        repaint();
    }

    void itemDropped (const SourceDetails& details) override
    {
        const int i = rowAt (details.localPosition.y);
        dropRow = -1;
        repaint();

        if (i > 0)
            owner.moveTo (File (details.description.toString()), rows()[(size_t) i].folder);
    }

private:
    Rectangle<int> rowBounds (int i) const { return { 0, i * folderRowHeight, getWidth(), folderRowHeight }; }

    int rowAt (int y) const
    {
        const int i = y / folderRowHeight;
        return y >= 0 && i < (int) rows().size() ? i : -1;
    }

    void setHovered (int i)
    {
        if (i != hovered)
        {
            hovered = i;
            repaint();
        }
    }

    PresetBrowser& owner;
    int hovered = -1, dropRow = -1;
};

//==============================================================================
class PresetBrowser::PresetGrid final : public Component
{
public:
    explicit PresetGrid (PresetBrowser& o) : owner (o) {}

    /** Lays the visible presets out in columns for `width` (and at least `minHeight` tall). */
    void rebuild (int width, int minHeight)
    {
        cells.clear();
        headings.clear();

        const auto groups = owner.visibleGroups();
        const bool searching = owner.search.getText().trim().isNotEmpty();
        const bool withHeadings = owner.showingAll || searching;
        const int columns = jmax (1, width / minCellWidth);
        const int cellWidth = width / columns;
        int y = 2;

        for (const auto& group : groups)
        {
            if (withHeadings)
            {
                headings.push_back ({ { 0, y, width, headingHeight }, group.title, group.entries.size() });
                y += headingHeight;
            }

            for (int k = 0; k < group.entries.size(); ++k)
                cells.push_back ({ Rectangle<int> ((k % columns) * cellWidth, y + (k / columns) * cellHeight, cellWidth, cellHeight).reduced (3, 2),
                                   group.entries[k] });

            y += (group.entries.size() + columns - 1) / columns * cellHeight + (withHeadings ? 6 : 0);
        }

        if (cells.empty())
            emptyText = searching ? "Nothing matches \"" + owner.search.getText().trim() + "\"."
                      : owner.showingAll ? String ("No presets yet: save one, import some, or restore the factory presets.")
                                         : String ("Nothing in here yet. Drag presets onto this folder, or save into it.");
        else
            emptyText = {};

        hovered = pressed = -1;
        setSize (width, jmax (y + 6, minHeight));
        repaint();
    }

    void paint (Graphics& g) override
    {
        for (const auto& h : headings)
        {
            auto r = h.bounds.toFloat().withTrimmedTop (8.0f).reduced (3.0f, 0.0f);
            g.setColour (colours::textFaint);
            g.setFont (monoFont (10.5f));
            g.drawText (String (h.count), r.removeFromRight (40.0f), Justification::centredRight, false);
            drawLabel (g, h.title, r, Justification::centredLeft, colours::ink, 10.5f);
            g.setColour (colours::gridStrong);
            g.fillRect (r.getX(), r.getBottom() - 1.0f, (float) getWidth() - 6.0f, 1.0f);
        }

        for (int i = 0; i < (int) cells.size(); ++i)
        {
            const auto& cell = cells[(size_t) i];
            const auto& entry = owner.entries.getReference (cell.entry);
            const bool current = entry.file == owner.shownCurrent;
            const auto r = cell.bounds.toFloat();

            g.setColour (current ? colours::panel : (i == hovered ? colours::panelRaised : colours::panel.withAlpha (0.55f)));
            g.fillRect (r);
            g.setColour (current ? colours::accent : (i == hovered ? colours::textFaint : colours::grid));
            g.drawRect (r, 1.0f);

            if (current)
            {
                g.setColour (colours::accent);
                g.fillRect (r.withWidth (3.0f));
            }

            g.setColour (current ? colours::ink : colours::text);
            g.setFont (font (13.0f, current));
            g.drawFittedText (entry.getName(), r.reduced (10.0f, 0.0f).toNearestInt(), Justification::centredLeft, 1, 0.85f);
        }

        if (emptyText.isNotEmpty())
        {
            g.setColour (colours::textDim);
            g.setFont (font (13.0f));
            g.drawFittedText (emptyText, getLocalBounds().reduced (20).withHeight (80), Justification::centred, 2, 1.0f);
        }
    }

    void mouseMove (const MouseEvent& e) override { setHovered (cellAt (e.getPosition())); }
    void mouseExit (const MouseEvent&) override { setHovered (-1); }

    void mouseDown (const MouseEvent& e) override
    {
        pressed = cellAt (e.getPosition());
        dragging = false;

        if (pressed >= 0 && e.mods.isPopupMenu())
        {
            owner.showPresetMenu (fileOf (pressed));
            pressed = -1;
        }
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (pressed < 0 || dragging || e.getDistanceFromDragStart() < 6)
            return;

        dragging = true;

        if (auto* container = DragAndDropContainer::findParentDragContainerFor (this))
            container->startDragging (fileOf (pressed).getFullPathName(), this, ScaledImage (dragImage (owner.entries[cells[(size_t) pressed].entry].getName())));
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (! dragging && pressed >= 0 && cellAt (e.getPosition()) == pressed)
            owner.load (fileOf (pressed), false);

        pressed = -1;
        dragging = false;
    }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        if (const int i = cellAt (e.getPosition()); i >= 0 && ! e.mods.isPopupMenu())
            owner.load (fileOf (i), true);
    }

private:
    struct Cell
    {
        Rectangle<int> bounds;
        int entry;
    };

    struct Heading
    {
        Rectangle<int> bounds;
        String title;
        int count;
    };

    File fileOf (int cell) const { return owner.entries[cells[(size_t) cell].entry].file; }

    int cellAt (Point<int> p) const
    {
        for (int i = 0; i < (int) cells.size(); ++i)
            if (cells[(size_t) i].bounds.contains (p))
                return i;

        return -1;
    }

    void setHovered (int i)
    {
        if (i != hovered)
        {
            hovered = i;
            repaint();
        }
    }

    static Image dragImage (const String& name)
    {
        const auto f = font (13.0f, true);
        const int w = jmin (260, (int) GlyphArrangement::getStringWidth (f, name) + 28);
        Image image (Image::ARGB, w, 26, true);
        Graphics g (image);
        g.setColour (colours::ink.withAlpha (0.92f));
        g.fillRect (image.getBounds());
        g.setColour (colours::accent);
        g.fillRect (0, 0, 3, 26);
        g.setColour (Colours::white);
        g.setFont (f);
        g.drawFittedText (name, image.getBounds().reduced (12, 0), Justification::centredLeft, 1, 0.85f);
        return image;
    }

    PresetBrowser& owner;
    std::vector<Cell> cells;
    std::vector<Heading> headings;
    String emptyText;
    int hovered = -1, pressed = -1;
    bool dragging = false;
};

//==============================================================================
PresetBrowser::PresetBrowser (HollowAudioProcessor& p)
    : processor (p),
      folderList (std::make_unique<FolderList> (*this)),
      grid (std::make_unique<PresetGrid> (*this))
{
    folderView.setViewedComponent (folderList.get(), false);
    gridView.setViewedComponent (grid.get(), false);
    styleScrollbars (folderView);
    styleScrollbars (gridView);

    for (auto* c : std::initializer_list<Component*> { &folderView, &gridView, &search, &newFolder, &renameFolder, &deleteFolder,
                                                      &importButton, &restoreButton, &openFolder, &closeButton })
        addAndMakeVisible (c);

    addChildComponent (prompt);

    search.setFont (font (14.0f));
    search.setIndents (10, 5);
    search.setJustification (Justification::centredLeft);
    search.setTextToShowWhenEmpty ("Search presets", colours::textFaint);
    search.onTextChange = [this]
    {
        gridView.setViewPosition (0, 0);
        resized();
        repaint();
    };
    search.onEscapeKey = [this] { search.clear(); search.onTextChange(); };

    newFolder.setTooltip ("New folder");
    renameFolder.setTooltip ("Rename the selected folder");
    deleteFolder.setTooltip (String ("Delete the selected folder and its presets (they go to the ") + trashName + ")");
    importButton.setTooltip ("Copy .hollowpreset files into your presets (into their category's folder)");
    restoreButton.setTooltip ("Put back any factory presets that are missing");
    openFolder.setTooltip ("Open the preset folder on your computer");

    newFolder.onClick = [this] { askNewFolder ({}); };
    renameFolder.onClick = [this] { askRenameFolder (selectedFolder); };
    deleteFolder.onClick = [this] { askDeleteFolder (selectedFolder); };
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };

    importButton.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Import Hollow presets", File::getSpecialLocation (File::userHomeDirectory),
                                                 String ("*") + PresetLibrary::extension);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                              [safe = Component::SafePointer<PresetBrowser> (this)] (const FileChooser& fc)
                              {
                                  if (safe != nullptr && ! fc.getResults().isEmpty())
                                      safe->importFiles (fc.getResults());
                              });
    };

    restoreButton.onClick = [this]
    {
        const int n = processor.installFactoryPresets (true);
        changed (n > 0 ? "Restored " + presetCount (n) + " from the factory set." : String ("All factory presets are here."));
    };

    openFolder.onClick = [this]
    {
        const auto folder = processor.getUserPresetFolder();
        folder.createDirectory();
        folder.startAsProcess();
    };
}

PresetBrowser::~PresetBrowser()
{
    folderView.setViewedComponent (nullptr, false);
    gridView.setViewedComponent (nullptr, false);
}

//==============================================================================
int PresetBrowser::countIn (const String& folder) const
{
    int n = 0;

    for (const auto& e : entries)
        n += e.folder == folder ? 1 : 0;

    return n;
}

std::vector<PresetBrowser::Group> PresetBrowser::visibleGroups() const
{
    std::vector<Group> groups;
    const auto query = search.getText().trim();
    const bool searching = query.isNotEmpty();

    const auto add = [&] (const String& folder)
    {
        Group group { folder, folder.isEmpty() ? String ("Top level") : folder, {} };

        for (int i = 0; i < entries.size(); ++i)
            if (entries.getReference (i).folder == folder && (! searching || entries.getReference (i).getName().containsIgnoreCase (query)))
                group.entries.add (i);

        if (! group.entries.isEmpty() || ! (searching || showingAll))
            groups.push_back (std::move (group));
    };

    if (searching || showingAll)
    {
        add ({});

        for (const auto& f : folders)
            add (f);
    }
    else
    {
        add (selectedFolder);
    }

    return groups;
}

void PresetBrowser::refresh()
{
    const auto library = processor.getPresetLibrary();
    entries = library.scan();
    folders = library.getFolders();

    if (! showingAll && selectedFolder.isNotEmpty() && ! folders.contains (selectedFolder))
        showingAll = true;

    shownCurrent = processor.getCurrentUserPreset();
    updateButtons();
    resized();
    repaint();
}

void PresetBrowser::followCurrent()
{
    const auto current = processor.getCurrentUserPreset();

    if (current != shownCurrent)
    {
        shownCurrent = current;
        grid->repaint();
        folderList->repaint();
    }
}

void PresetBrowser::updateButtons()
{
    const bool folderSelected = ! showingAll && selectedFolder.isNotEmpty();
    renameFolder.setEnabled (folderSelected);
    deleteFolder.setEnabled (folderSelected);
}

void PresetBrowser::selectAll()
{
    showingAll = true;
    search.clear();
    gridView.setViewPosition (0, 0);
    updateButtons();
    resized();
    repaint();
}

void PresetBrowser::selectFolder (const String& folder)
{
    showingAll = false;
    selectedFolder = folder;
    search.clear();
    gridView.setViewPosition (0, 0);
    updateButtons();
    resized();
    repaint();
}

void PresetBrowser::changed (const String& newStatus)
{
    status = newStatus;
    refresh();

    if (onLibraryChanged != nullptr)
        onLibraryChanged();
}

//==============================================================================
void PresetBrowser::load (const File& file, bool thenClose)
{
    if (! processor.loadUserPreset (file))
    {
        changed ("Couldn't read " + file.getFileName() + ".");
        return;
    }

    followCurrent();

    if (onPresetLoaded != nullptr)
        onPresetLoaded();

    if (thenClose && onClose != nullptr)
        onClose();
}

void PresetBrowser::moveTo (const File& file, const String& folder)
{
    const auto name = file.getFileNameWithoutExtension();
    const auto moved = processor.movePreset (file, folder);
    const auto where = folder.isEmpty() ? String ("the top level") : folder;

    if (moved == File())
        changed ("Couldn't move " + name + ".");
    else if (moved == file)
        changed (name + " is already in " + where + ".");
    else
        changed (name + " moved to " + where + (moved.getFileNameWithoutExtension() != name ? " as " + moved.getFileNameWithoutExtension() : String()) + ".");
}

void PresetBrowser::importFiles (const Array<File>& files)
{
    StringArray problems;
    const auto imported = processor.importPresets (files, &problems);
    String text = imported.isEmpty() ? String ("Nothing imported.") : "Imported " + presetCount (imported.size()) + ".";

    if (! problems.isEmpty())
        text << " " << problems.joinIntoString ("; ") << ".";

    showingAll = true;
    changed (text);
}

void PresetBrowser::showPresetMenu (const File& file)
{
    const auto current = processor.getPresetLibrary().folderOf (file);
    const auto choices = folders;
    PopupMenu menu, moveMenu;

    moveMenu.addItem (100, "Top level", current.isNotEmpty(), current.isEmpty());

    for (int i = 0; i < choices.size(); ++i)
        moveMenu.addItem (101 + i, choices[i], choices[i] != current, choices[i] == current);

    moveMenu.addSeparator();
    moveMenu.addItem (99, "New folder...");

    menu.addSectionHeader (file.getFileNameWithoutExtension());
    menu.addItem (1, "Load");
    menu.addItem (2, "Rename...");
    menu.addSubMenu ("Move to", moveMenu);
    menu.addItem (3, revealText);
    menu.addSeparator();
    menu.addItem (4, String ("Delete (to the ") + trashName + ")");

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (grid.get()).withTargetScreenArea (Rectangle<int> (Desktop::getMousePosition(), Desktop::getMousePosition() + Point<int> (1, 1))),
                        [safe = Component::SafePointer<PresetBrowser> (this), file, choices] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            if (result == 1)
                                safe->load (file, false);
                            else if (result == 2)
                                safe->askRename (file);
                            else if (result == 3)
                                file.revealToUser();
                            else if (result == 4)
                                safe->changed (safe->processor.deleteUserPreset (file)
                                                   ? file.getFileNameWithoutExtension() + " went to the " + trashName + "."
                                                   : "Couldn't delete " + file.getFileNameWithoutExtension() + ".");
                            else if (result == 99)
                                safe->askNewFolder (file);
                            else if (result == 100)
                                safe->moveTo (file, {});
                            else if (result > 100 && result - 101 < choices.size())
                                safe->moveTo (file, choices[result - 101]);
                        });
}

void PresetBrowser::showFolderMenu (const String& folder)
{
    PopupMenu menu;
    menu.addSectionHeader (folder);
    menu.addItem (1, "Rename...");
    menu.addItem (2, revealText);
    menu.addSeparator();
    menu.addItem (3, "Delete...");

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (folderList.get()).withTargetScreenArea (Rectangle<int> (Desktop::getMousePosition(), Desktop::getMousePosition() + Point<int> (1, 1))),
                        [safe = Component::SafePointer<PresetBrowser> (this), folder] (int result)
                        {
                            if (safe == nullptr)
                                return;

                            if (result == 1)
                                safe->askRenameFolder (folder);
                            else if (result == 2)
                                safe->processor.getPresetLibrary().getFolder (folder).revealToUser();
                            else if (result == 3)
                                safe->askDeleteFolder (folder);
                        });
}

void PresetBrowser::askRename (const File& file)
{
    prompt.ask ("Rename preset", "The file is renamed too.", true, file.getFileNameWithoutExtension(), "Rename",
                [this, file] (const String& name)
                {
                    const auto renamed = processor.renamePreset (file, name);
                    changed (renamed != File() ? "Renamed to " + renamed.getFileNameWithoutExtension() + "."
                                               : "Couldn't rename " + file.getFileNameWithoutExtension() + ".");
                });
}

void PresetBrowser::askNewFolder (const File& thenMove)
{
    const auto message = thenMove != File() ? "A new folder for " + thenMove.getFileNameWithoutExtension() + "."
                                             : String ("Folders show up as submenus in the preset list.");

    prompt.ask ("New folder", message, true, {}, "Create", [this, thenMove] (const String& name)
    {
        const auto clean = PresetLibrary::cleanName (name);

        if (! processor.getPresetLibrary().createFolder (clean))
        {
            changed (clean.isEmpty() ? String ("That name doesn't work as a folder name.") : "A folder called " + clean + " exists already.");
            return;
        }

        if (thenMove != File())
        {
            moveTo (thenMove, clean);
        }
        else
        {
            showingAll = false;
            selectedFolder = clean;
            changed ("Created the folder " + clean + ". Drag presets onto it.");
        }
    });
}

void PresetBrowser::askRenameFolder (const String& folder)
{
    if (folder.isEmpty())
        return;

    prompt.ask ("Rename folder", "Its presets stay in it.", true, folder, "Rename", [this, folder] (const String& name)
    {
        const auto clean = PresetLibrary::cleanName (name);

        if (processor.renamePresetFolder (folder, name))
        {
            if (! showingAll && selectedFolder == folder)
                selectedFolder = clean;

            changed ("Renamed the folder to " + clean + ".");
        }
        else
        {
            changed ("Couldn't rename " + folder + (clean.isNotEmpty() && folders.contains (clean, true) ? ": that folder exists already." : "."));
        }
    });
}

void PresetBrowser::askDeleteFolder (const String& folder)
{
    if (folder.isEmpty())
        return;

    const int n = countIn (folder);
    const auto message = "The folder " + folder + (n > 0 ? " and its " + presetCount (n) + " go" : String (" goes"))
                       + " to the " + trashName + ".";

    prompt.ask ("Delete folder", message, false, {}, "Delete", [this, folder] (const String&)
    {
        if (processor.deletePresetFolder (folder))
        {
            if (selectedFolder == folder)
                showingAll = true;

            changed ("Deleted the folder " + folder + ".");
        }
        else
        {
            changed ("Couldn't delete the folder " + folder + ".");
        }
    });
}

//==============================================================================
void PresetBrowser::paint (Graphics& g)
{
    // Frosted sheet over the editor
    g.setColour (colours::background.withAlpha (0.97f));
    g.fillRect (getLocalBounds());

    auto top = getLocalBounds().reduced (16, 10).removeFromTop (24).toFloat();
    drawLabel (g, "Presets", top.removeFromLeft (80.0f), Justification::centredLeft, colours::ink, 12.0f);
    g.setColour (colours::textDim);
    g.setFont (font (12.5f));
    g.drawText (presetCount (entries.size()) + " in " + String (folders.size()) + (folders.size() == 1 ? " folder" : " folders"),
                top, Justification::centredLeft, false);

    drawCard (g, foldersCard.toFloat(), "Folders");

    const bool searching = search.getText().trim().isNotEmpty();
    drawCard (g, presetsCard.toFloat(), searching ? String ("Search") : showingAll ? String ("All presets")
                                                                    : (selectedFolder.isEmpty() ? String ("Top level") : selectedFolder));

    g.setColour (status.isNotEmpty() ? colours::accent : colours::textDim);
    g.setFont (font (12.0f));
    g.drawFittedText (status.isNotEmpty() ? status : String ("Click to load, double-click to load and close. Drag a preset onto a folder to move it; "
                                                             "right-click for more."),
                      statusArea, Justification::centredLeft, 2, 1.0f);
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    auto top = area.removeFromTop (24);
    closeButton.setBounds (top.removeFromRight (90));
    area.removeFromTop (10);

    foldersCard = area.removeFromLeft (272);
    area.removeFromLeft (10);
    presetsCard = area;

    {
        auto c = foldersCard.reduced (12, 8).withTrimmedTop (22);
        auto buttons = c.removeFromBottom (28);
        c.removeFromBottom (8);
        folderView.setBounds (c);

        const int w = (buttons.getWidth() - 12) / 3;
        newFolder.setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (6);
        renameFolder.setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (6);
        deleteFolder.setBounds (buttons);
    }

    {
        auto c = presetsCard.reduced (12, 8);
        search.setBounds (c.removeFromTop (28).removeFromRight (260));
        c.removeFromTop (8);

        auto bottom = c.removeFromBottom (28);
        c.removeFromBottom (8);
        importButton.setBounds (bottom.removeFromLeft (110));
        bottom.removeFromLeft (6);
        restoreButton.setBounds (bottom.removeFromLeft (150));
        bottom.removeFromLeft (6);
        openFolder.setBounds (bottom.removeFromLeft (120));
        bottom.removeFromLeft (14);
        statusArea = bottom;
        gridView.setBounds (c);
    }

    const auto scroll = gridView.getViewPosition();
    folderList->setSize (folderView.getWidth() - scrollbarWidth, jmax (folderList->getContentHeight(), folderView.getHeight()));
    grid->rebuild (gridView.getWidth() - scrollbarWidth, gridView.getHeight());
    gridView.setViewPosition (scroll);
    prompt.setBounds (getLocalBounds());
}

} // namespace hl::gui
