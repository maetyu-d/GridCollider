#include <juce_gui_basics/juce_gui_basics.h>

#include "MainComponent.h"
#include "Presets/PresetManager.h"

namespace
{
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
                             juce::Colour::fromRGB(10, 12, 10),
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
            }
            else if (menuIndex == 1)
            {
                menu.addItem(10, "Mixer", true, mainComponent != nullptr && mainComponent->isMixerViewVisible());
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
                                             exampleFiles[index].getFileNameWithoutExtension());
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
            else if (menuItemID == 10)
                mainComponent->menuToggleMixerView();
            else if (menuItemID >= exampleMenuBaseId && menuItemID < exampleMenuBaseId + exampleFiles.size())
                mainComponent->menuLoadExample(exampleFiles[menuItemID - exampleMenuBaseId]);
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
