#include <juce_gui_basics/juce_gui_basics.h>

#include "MainComponent.h"
#include "Presets/PresetManager.h"

namespace
{
namespace CommandIDs
{
constexpr juce::CommandID editUndo = 0x2000;
constexpr juce::CommandID editRedo = 0x2001;
constexpr juce::CommandID editCut = 0x2002;
constexpr juce::CommandID editCopy = 0x2003;
constexpr juce::CommandID editPaste = 0x2004;
constexpr juce::CommandID editDuplicate = 0x2005;
constexpr juce::CommandID editDelete = 0x2006;
constexpr juce::CommandID editSelectAll = 0x2007;
}

juce::String exampleMenuTitleFor(const juce::File& file)
{
    auto title = file.getFileNameWithoutExtension().replaceCharacter('-', ' ');

    for (int index = 0; index < title.length(); ++index)
    {
        if (index == 0 || title[index - 1] == ' ')
            title = title.replaceSection(index, 1, juce::String::charToString(juce::CharacterFunctions::toUpperCase(title[index])));
    }

    return title;
}

class GridColliderApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "GridCollider"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override {}

private:
    class MainWindow final : public juce::DocumentWindow,
                             public juce::MenuBarModel,
                             public juce::ApplicationCommandTarget
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                             juce::Colour::fromRGB(28, 29, 30),
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            setResizeLimits(1040, 760, 3200, 2200);
            auto* component = new gridcollider::MainComponent();
            mainComponent = component;
            setContentOwned(component, true);
            setApplicationCommandManagerToWatch(&commandManager);
            commandManager.registerAllCommandsForTarget(this);
            commandManager.setFirstCommandTarget(this);
            addKeyListener(commandManager.getKeyMappings());
            setWantsKeyboardFocus(true);
           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu(this);
           #else
            setMenuBar(this);
           #endif
            centreWithSize(1280, 900);
            setVisible(true);
        }

        ~MainWindow() override
        {
            removeKeyListener(commandManager.getKeyMappings());
            commandManager.setFirstCommandTarget(nullptr);
            setApplicationCommandManagerToWatch(nullptr);
           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu(nullptr);
           #else
            setMenuBar(nullptr);
           #endif
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        juce::StringArray getMenuBarNames() override
        {
            return { "File", "Edit", "View", "Help" };
        }

        juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String&) override
        {
            juce::PopupMenu menu;

            if (menuIndex == 0)
            {
                menu.addItem(1, "Load...");
                menu.addItem(2, "Save");
                menu.addItem(3, "Save As...");
                menu.addSeparator();
                menu.addItem(4, "Export Stereo WAV...");
                menu.addItem(5, "Export State Stems...");
                menu.addItem(6, "Export Lane Stems...");
                menu.addSeparator();
                menu.addItem(7, "About GridCollider");
            }
            else if (menuIndex == 1)
            {
                menu.addCommandItem(&commandManager, CommandIDs::editUndo);
                menu.addCommandItem(&commandManager, CommandIDs::editRedo);
                menu.addSeparator();
                menu.addCommandItem(&commandManager, CommandIDs::editCut);
                menu.addCommandItem(&commandManager, CommandIDs::editCopy);
                menu.addCommandItem(&commandManager, CommandIDs::editPaste);
                menu.addCommandItem(&commandManager, CommandIDs::editSelectAll);
                menu.addSeparator();
                menu.addCommandItem(&commandManager, CommandIDs::editDuplicate);
                menu.addCommandItem(&commandManager, CommandIDs::editDelete);
            }
            else if (menuIndex == 2)
            {
                const auto mixerVisible = mainComponent != nullptr && mainComponent->isMixerViewVisible();
                const auto arrangementVisible = mainComponent != nullptr && mainComponent->isArrangementViewVisible();
                const auto instrumentsVisible = mainComponent != nullptr && mainComponent->isInstrumentsViewVisible();
                const auto transitionsVisible = mainComponent != nullptr && mainComponent->isTransitionsViewVisible();
                const auto eventMonitorVisible = mainComponent != nullptr && mainComponent->isEventMonitorViewVisible();
                const auto stateInspectorVisible = mainComponent != nullptr && mainComponent->isStateInspectorViewVisible();
                menu.addItem(9, "Main", true, mainComponent != nullptr && ! mixerVisible && ! arrangementVisible && ! instrumentsVisible && ! transitionsVisible && ! eventMonitorVisible && ! stateInspectorVisible);
                menu.addSeparator();
                menu.addItem(10, "Mixer", true, mixerVisible);
                menu.addItem(11, "Arrangement", true, arrangementVisible);
                menu.addItem(12, "Instruments", true, instrumentsVisible);
                menu.addItem(13, "Transitions Editor", true, transitionsVisible);
                menu.addItem(14, "Event Monitor", true, eventMonitorVisible);
                menu.addItem(15, "State Inspector", true, stateInspectorVisible);
            }
            else if (menuIndex == 3)
            {
                exampleFiles = presetManager.findExampleFiles();

                juce::PopupMenu examplesMenu;

                if (exampleFiles.isEmpty())
                {
                    examplesMenu.addItem(1000, "No Examples Found", false);
                }
                else
                {
                    for (int index = 0; index < exampleFiles.size(); ++index)
                        examplesMenu.addItem(exampleMenuBaseId + index,
                                             exampleMenuTitleFor(exampleFiles[index]));
                }

                menu.addSubMenu("Examples", examplesMenu);
            }

            return menu;
        }

        void menuItemSelected(const int menuItemID, int) override
        {
            if (menuItemID == 7)
            {
                auto* app = juce::JUCEApplication::getInstance();
                const auto name = app != nullptr ? app->getApplicationName() : juce::String("GridCollider");
                const auto version = app != nullptr ? app->getApplicationVersion() : juce::String("0.1.0");

                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                                                       "About " + name,
                                                       name + "\nVersion " + version + "\nby matd.space");
                return;
            }

            if (mainComponent == nullptr)
                return;

            if (menuItemID == 1)
                mainComponent->menuLoadComposition();
            else if (menuItemID == 2)
                mainComponent->menuSaveComposition();
            else if (menuItemID == 3)
                mainComponent->menuSaveCompositionAs();
            else if (menuItemID == 4)
                mainComponent->menuExportStereoWav();
            else if (menuItemID == 5)
                mainComponent->menuExportStateStems();
            else if (menuItemID == 6)
                mainComponent->menuExportLaneStems();
            else if (menuItemID == 9)
                mainComponent->menuShowMainView();
            else if (menuItemID == 10)
                mainComponent->menuToggleMixerView();
            else if (menuItemID == 11)
                mainComponent->menuToggleArrangementView();
            else if (menuItemID == 12)
                mainComponent->menuToggleInstrumentsView();
            else if (menuItemID == 13)
                mainComponent->menuToggleTransitionsView();
            else if (menuItemID == 14)
                mainComponent->menuToggleEventMonitorView();
            else if (menuItemID == 15)
                mainComponent->menuToggleStateInspectorView();
            else if (menuItemID >= exampleMenuBaseId && menuItemID < exampleMenuBaseId + exampleFiles.size())
                mainComponent->menuLoadExample(exampleFiles[menuItemID - exampleMenuBaseId]);

            menuItemsChanged();
        }

        juce::ApplicationCommandTarget* getNextCommandTarget() override
        {
            return nullptr;
        }

        void getAllCommands(juce::Array<juce::CommandID>& commands) override
        {
            commands.addArray({ CommandIDs::editUndo,
                                CommandIDs::editRedo,
                                CommandIDs::editCut,
                                CommandIDs::editCopy,
                                CommandIDs::editPaste,
                                CommandIDs::editSelectAll,
                                CommandIDs::editDuplicate,
                                CommandIDs::editDelete });
        }

        void getCommandInfo(const juce::CommandID commandID, juce::ApplicationCommandInfo& result) override
        {
            const auto canEdit = mainComponent != nullptr;

            switch (commandID)
            {
                case CommandIDs::editUndo:
                    result.setInfo("Undo", "Undo the last edit", "Edit", 0);
                    result.addDefaultKeypress('z', juce::ModifierKeys::commandModifier);
                    result.setActive(canEdit && mainComponent->canMenuEditUndo());
                    break;

                case CommandIDs::editRedo:
                    result.setInfo("Redo", "Redo the last undone edit", "Edit", 0);
                    result.addDefaultKeypress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
                    result.setActive(canEdit && mainComponent->canMenuEditRedo());
                    break;

                case CommandIDs::editCut:
                    result.setInfo("Cut", "Cut the selected item or text", "Edit", 0);
                    result.addDefaultKeypress('x', juce::ModifierKeys::commandModifier);
                    result.setActive(canEdit);
                    break;

                case CommandIDs::editCopy:
                    result.setInfo("Copy", "Copy the selected item or text", "Edit", 0);
                    result.addDefaultKeypress('c', juce::ModifierKeys::commandModifier);
                    result.setActive(canEdit);
                    break;

                case CommandIDs::editPaste:
                    result.setInfo("Paste", "Paste into the current editor or lane/state context", "Edit", 0);
                    result.addDefaultKeypress('v', juce::ModifierKeys::commandModifier);
                    result.setActive(canEdit && mainComponent->canMenuEditPaste());
                    break;

                case CommandIDs::editSelectAll:
                    result.setInfo("Select All", "Select all text or grid cells in the current editor", "Edit", 0);
                    result.addDefaultKeypress('a', juce::ModifierKeys::commandModifier);
                    result.setActive(canEdit && mainComponent->canMenuEditSelectAll());
                    break;

                case CommandIDs::editDuplicate:
                    result.setInfo("Duplicate", "Duplicate the current selection, state, or lane", "Edit", 0);
                    result.addDefaultKeypress('d', juce::ModifierKeys::commandModifier);
                    result.setActive(canEdit);
                    break;

                case CommandIDs::editDelete:
                    result.setInfo("Delete", "Delete the current selection, state, or lane", "Edit", 0);
                    result.addDefaultKeypress(juce::KeyPress::deleteKey, {});
                    result.setActive(canEdit);
                    break;

                default:
                    break;
            }
        }

        bool perform(const InvocationInfo& info) override
        {
            if (mainComponent == nullptr)
                return false;

            switch (info.commandID)
            {
                case CommandIDs::editUndo:      mainComponent->menuEditUndo(); break;
                case CommandIDs::editRedo:      mainComponent->menuEditRedo(); break;
                case CommandIDs::editCut:       mainComponent->menuEditCut(); break;
                case CommandIDs::editCopy:      mainComponent->menuEditCopy(); break;
                case CommandIDs::editPaste:     mainComponent->menuEditPaste(); break;
                case CommandIDs::editSelectAll: mainComponent->menuEditSelectAll(); break;
                case CommandIDs::editDuplicate: mainComponent->menuEditDuplicate(); break;
                case CommandIDs::editDelete:    mainComponent->menuEditDelete(); break;
                default: return false;
            }

            menuItemsChanged();
            return true;
        }

    private:
        static constexpr int exampleMenuBaseId = 1000;

        juce::ApplicationCommandManager commandManager;
        gridcollider::MainComponent* mainComponent = nullptr;
        gridcollider::PresetManager presetManager;
        juce::Array<juce::File> exampleFiles;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};
}

START_JUCE_APPLICATION(GridColliderApplication)
