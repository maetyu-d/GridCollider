#include <juce_gui_basics/juce_gui_basics.h>

#include "MainComponent.h"
#include "Presets/PresetManager.h"

namespace
{
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
                             public juce::MenuBarModel
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
            return { "File", "View", "Help" };
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
            }
            else if (menuIndex == 1)
            {
                const auto mixerVisible = mainComponent != nullptr && mainComponent->isMixerViewVisible();
                const auto arrangementVisible = mainComponent != nullptr && mainComponent->isArrangementViewVisible();
                menu.addItem(9, "Main", true, mainComponent != nullptr && ! mixerVisible && ! arrangementVisible);
                menu.addSeparator();
                menu.addItem(10, "Mixer", true, mixerVisible);
                menu.addItem(11, "Arrangement", true, arrangementVisible);
            }
            else if (menuIndex == 2)
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
            else if (menuItemID == 9)
                mainComponent->menuShowMainView();
            else if (menuItemID == 10)
                mainComponent->menuToggleMixerView();
            else if (menuItemID == 11)
                mainComponent->menuToggleArrangementView();
            else if (menuItemID >= exampleMenuBaseId && menuItemID < exampleMenuBaseId + exampleFiles.size())
                mainComponent->menuLoadExample(exampleFiles[menuItemID - exampleMenuBaseId]);

            menuItemsChanged();
        }

    private:
        static constexpr int exampleMenuBaseId = 1000;

        gridcollider::MainComponent* mainComponent = nullptr;
        gridcollider::PresetManager presetManager;
        juce::Array<juce::File> exampleFiles;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};
}

START_JUCE_APPLICATION(GridColliderApplication)
