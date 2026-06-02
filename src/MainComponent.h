#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Components/GridEditorComponent.h"
#include "Components/StateGraphComponent.h"
#include "Core/EventRouter.h"
#include "Core/GridInterpreter.h"
#include "Core/GridModel.h"
#include "Core/TransportEngine.h"
#include "IO/EmbeddedScAudioEngine.h"
#include "IO/OscOutput.h"
#include "Presets/PresetManager.h"
#include "Utility/StatusLog.h"

#include <atomic>
#include <array>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace gridcollider
{
class MainComponent final : public juce::AudioAppComponent,
                            private juce::KeyListener,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void menuLoadComposition();
    void menuSaveComposition();
    void menuSaveCompositionAs();
    void menuToggleMixerView();
    void menuLoadExample(const juce::File& file);
    [[nodiscard]] bool isMixerViewVisible() const noexcept;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

private:
    using juce::Component::keyPressed;

    struct CompositionGrid;

    class MinimalLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void drawButtonBackground(juce::Graphics& graphics,
                                  juce::Button& button,
                                  const juce::Colour& backgroundColour,
                                  bool shouldDrawButtonAsHighlighted,
                                  bool shouldDrawButtonAsDown) override
        {
            auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
            auto colour = backgroundColour;

            if (shouldDrawButtonAsDown)
                colour = colour.brighter(0.16f);
            else if (shouldDrawButtonAsHighlighted)
                colour = colour.brighter(0.08f);

            const auto radius = juce::jmin(6.0f, bounds.getHeight() * 0.32f);
            graphics.setColour(colour);
            graphics.fillRoundedRectangle(bounds, radius);
            graphics.setColour(button.findColour(juce::ComboBox::outlineColourId).withAlpha(button.isEnabled() ? 0.75f : 0.28f));
            graphics.drawRoundedRectangle(bounds, radius, 1.0f);
        }

        void fillTextEditorBackground(juce::Graphics& graphics,
                                      int width,
                                      int height,
                                      juce::TextEditor& textEditor) override
        {
            auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f);
            graphics.setColour(textEditor.findColour(juce::TextEditor::backgroundColourId));
            graphics.fillRoundedRectangle(bounds, 3.0f);
        }

        void drawTextEditorOutline(juce::Graphics& graphics,
                                   int width,
                                   int height,
                                   juce::TextEditor& textEditor) override
        {
            auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f);
            const auto colourId = textEditor.hasKeyboardFocus(true)
                                    ? juce::TextEditor::focusedOutlineColourId
                                    : juce::TextEditor::outlineColourId;
            graphics.setColour(textEditor.findColour(colourId));
            graphics.drawRoundedRectangle(bounds, 3.0f, 1.0f);
        }
    };

    class MixerContentComponent final : public juce::Component
    {
    public:
        MixerContentComponent() { setOpaque(true); }

        void paint(juce::Graphics& graphics) override
        {
            const auto background = juce::Colour::fromRGB(18, 19, 18);
            const auto ink = juce::Colour::fromRGB(226, 230, 216);
            const auto grid = juce::Colour::fromRGB(88, 94, 84).withAlpha(0.28f);

            graphics.fillAll(background);
            for (int x = 0; x < getWidth(); x += 96)
            {
                graphics.setColour(grid);
                graphics.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));
            }
            for (int y = 0; y < getHeight(); y += 28)
            {
                graphics.setColour(grid);
                graphics.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
            }
            graphics.setColour(ink);
            graphics.drawRect(getLocalBounds(), 1);
        }
    };

    class SourceCodeBackdropComponent final : public juce::Component
    {
    public:
        enum class Language
        {
            supercollider,
            cpp
        };

        SourceCodeBackdropComponent();

        void setLanguage(Language newLanguage);
        void setSourceProvider(std::function<juce::String()> provider);
        void paint(juce::Graphics& graphics) override;

    private:
        enum class Kind
        {
            normal,
            comment,
            keyword,
            builtin,
            number,
            string,
            symbol,
            operatorToken,
            bracket
        };

        [[nodiscard]] Kind classifyWord(const juce::String& word) const;
        [[nodiscard]] juce::Colour colourForKind(Kind kind, float alpha) const;

        Language language = Language::supercollider;
        std::function<juce::String()> sourceProvider;
    };

    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    void timerCallback() override;

    void configureTransport();
    void handleTransportTick(const TransportEngine::TickResult& result);
    void appendEvaluatedEventsToLog(const TransportEngine::TickResult& result);
    void stopTransport();
    void configureOscControls();
    void styleOscControls();
    void connectOscFromControls();
    void updateOscStatusUi();
    void configureExampleControls();
    void loadExampleFile(const juce::File& file);
    void loadFirstExample();
    void showExampleMenu();
    void configurePatternControls();
    void stylePatternButton(juce::TextButton& button);
    void showLoadPatternDialog();
    void showSavePatternDialog();
    void showLoadCompositionDialog();
    void showSaveCompositionDialog();
    void loadCompositionFile(const juce::File& file);
    void saveCompositionFile(juce::File file);
    [[nodiscard]] juce::var serialiseComposition() const;
    [[nodiscard]] juce::Result restoreComposition(const juce::var& document);
    [[nodiscard]] juce::String snapshotToText(const GridModel::Snapshot& snapshot) const;
    [[nodiscard]] GridModel::Snapshot snapshotFromText(const juce::String& text, int width, int height) const;
    void loadPatternFile(const juce::File& file, bool addToRecent);
    void savePatternFile(juce::File file);
    void addRecentPatternFile(const juce::File& file);
    void showRecentPatternMenu();
    void triggerEmbeddedScTest();
    void configureTransitionCodePane();
    void styleTransitionCodePane();
    void storeActiveTransitionCode();
    void showActiveTransitionCode();
    [[nodiscard]] juce::String createDefaultTransitionCode(int stateNumber) const;
    void configureEventMonitor();
    void appendEventMonitorLine(const LogEvent& event);
    void refreshEventMonitor();
    void configureTransportControls();
    void styleTransportControls();
    void updateTransportControls();
    void toggleTransportPlayback();
    void resetTransport();
    void applyTransportEditors();
    void advanceStateFromTransitionPane(const TransportEngine::TickResult& result);
    void toggleSelectedStateAdvanceMode();
    void applyStateAdvanceEditor();
    void updateStateAdvanceControls();
    void configureStateGraph();
    void refreshStateGraph();
    void switchToState(int stateIndex);
    void previousState();
    void nextState();
    void addCompositionState();
    void copySelectedState();
    void pasteCopiedState();
    void deleteSelectedState();
    void configureGridSlotControls();
    void styleGridSlotControls();
    void updateGridSlotControls();
    void configureMixerView();
    void styleMixerControls();
    void toggleMixerView();
    void refreshMixerView();
    void applyMixerControl(int stateIndex, int laneIndex, int mixerControlIndex);
    void applyMasterLevel();
    void applyLaneMixToEvents(std::vector<InternalEvent>& events, const CompositionGrid& lane) const;
    void configureLaneCodePane();
    void styleLaneCodePane();
    void storeActiveLane();
    void storeActiveLaneLocked();
    void showActiveLane();
    void toggleSelectedLaneKind();
    void compileSelectedScLane();
    void compileScLanesForState(int stateIndex);
    [[nodiscard]] juce::String createDefaultScLaneCode(int stateNumber, int laneNumber) const;
    [[nodiscard]] juce::String getSynthDefNameFromSource(const juce::String& source, int stateNumber, int laneNumber) const;
    void applyGridSizeEditors();
    void updateGridSizeControls();
    void storeActiveGridSlot();
    void storeActiveGridSlotLocked();
    [[nodiscard]] GridModel::Snapshot makeEmptyGridSnapshot(int columns = GridModel::defaultWidth,
                                                            int rows = GridModel::defaultHeight) const;
    [[nodiscard]] GridEvaluation evaluateActiveState(const TransportEngine::TickContext& context);
    void applyGridTimingEditors();
    void toggleSelectedGridPhaseMode();
    [[nodiscard]] double getPhaseOffsetFrameDelta(double ratio, double degrees) const;
    void resetGridRuntimeClocks();
    [[nodiscard]] std::uint64_t getDisplayGridFrame(std::uint64_t stateFrame) const;
    void switchToGridSlot(int slotIndex);
    void previousGridSlot();
    void nextGridSlot();
    void addGridSlot();
    [[nodiscard]] juce::String getTransportStateText() const;

    struct TransitionChoice
    {
        int targetState = 0;
        double chance = 0.0;
    };

    struct TransitionRules
    {
        std::map<int, int> linear;
        std::map<int, std::vector<TransitionChoice>> weighted;
    };

    [[nodiscard]] TransitionRules parseTransitionRules(juce::String text) const;
    [[nodiscard]] int chooseTransitionTarget(const TransitionRules& rules, int currentState);
    void applyTransitionTarget(int targetState);

    struct MainLayout
    {
        juce::Rectangle<int> header;
        juce::Rectangle<int> transitionPane;
        juce::Rectangle<int> transitionSplitter;
        juce::Rectangle<int> statePane;
        juce::Rectangle<int> gridSplitter;
        juce::Rectangle<int> gridPane;
    };

    enum class SplitterDrag
    {
        none,
        transition,
        grid
    };

    [[nodiscard]] MainLayout calculateMainLayout() const;
    [[nodiscard]] SplitterDrag splitterAt(juce::Point<int> position) const;
    void updateSplitterCursor(juce::Point<int> position);

    struct CompositionGrid
    {
        CompositionGrid() = default;
        CompositionGrid(GridModel::Snapshot initialSnapshot)
            : snapshot(std::move(initialSnapshot))
        {
        }

        enum class Kind
        {
            grid,
            supercollider
        };

        Kind kind = Kind::grid;
        GridModel::Snapshot snapshot;
        juce::String scCode;
        juce::String scSynthName;
        bool scCodeDirty = true;
        double tempoRatio = 1.0;
        bool phaseOffsetEnabled = false;
        double phaseOffsetDegrees = 0.0;
        float mixerLevel = 1.0f;
        float mixerPan = 0.0f;
        std::uint64_t lastEvaluatedFrame = std::numeric_limits<std::uint64_t>::max();
    };

    struct CompositionState
    {
        enum class AdvanceMode
        {
            manual,
            beats,
            bars,
            trigger
        };

        juce::String name;
        double bpm = 120.0;
        AdvanceMode advanceMode = AdvanceMode::manual;
        int advanceInterval = 4;
        juce::String transitionCode;
        std::vector<CompositionGrid> grids;
    };

    [[nodiscard]] juce::String getStateAdvanceModeText(CompositionState::AdvanceMode mode) const;

    GridModel gridModel;
    GridInterpreter gridInterpreter;
    EventRouter eventRouter;
    OscOutput oscOutput;
    EmbeddedScAudioEngine embeddedScAudio;
    PresetManager presetManager;
    StatusLog statusLog;
    StateGraphComponent stateGraph;
    GridEditorComponent gridEditor;
    TransportEngine transportEngine;

    juce::TextEditor oscHostEditor;
    juce::TextEditor oscPortEditor;
    juce::TextButton oscConnectButton;
    juce::ToggleButton oscDebugToggle;
    juce::Label oscStatusLabel;
    juce::TextButton loadExampleButton;
    juce::TextButton loadPatternButton;
    juce::TextButton savePatternButton;
    juce::TextButton recentPatternButton;
    juce::TextButton embeddedScTestButton;
    juce::Label transitionCodeLabel;
    SourceCodeBackdropComponent transitionCodeBackdrop;
    juce::TextEditor transitionCodeEditor;
    juce::TextEditor eventMonitor;
    juce::Label eventMonitorLabel;
    SourceCodeBackdropComponent laneCodeBackdrop;
    juce::TextEditor laneScCodeEditor;

    static constexpr int maximumMixerChannels = 129;
    juce::Viewport mixerViewport;
    MixerContentComponent mixerContent;
    juce::Label mixerLabel;
    std::array<juce::Label, maximumMixerChannels> mixerChannelLabels;
    std::array<juce::Slider, maximumMixerChannels> mixerLevelSliders;
    std::array<juce::Slider, maximumMixerChannels> mixerPanSliders;

    juce::TextButton playPauseButton;
    juce::TextButton stopButton;
    juce::TextButton resetButton;
    juce::TextEditor bpmEditor;
    juce::Label bpmLabel;
    juce::Label stateSlotLabel;
    juce::TextButton previousStateButton;
    juce::TextButton nextStateButton;
    juce::TextButton addStateButton;
    juce::Label stateAdvanceLabel;
    juce::TextButton stateAdvanceModeButton;
    juce::TextEditor stateAdvanceIntervalEditor;
    juce::Label gridSlotLabel;
    juce::TextButton previousGridButton;
    juce::TextButton nextGridButton;
    juce::TextButton addGridButton;
    juce::Label gridRatioLabel;
    juce::TextEditor gridRatioEditor;
    juce::TextButton phaseModeButton;
    juce::TextEditor phaseOffsetEditor;
    juce::TextButton laneKindButton;
    juce::Label gridSizeLabel;
    juce::TextEditor gridColumnsEditor;
    juce::Label gridSizeSeparatorLabel;
    juce::TextEditor gridRowsEditor;
    std::array<juce::TextButton, 8> gridTabButtons;
    MinimalLookAndFeel minimalLookAndFeel;

    std::uint64_t lastTransportFrame = 0;
    int lastTickInBeat = 0;
    bool lastTickWasBeat = false;
    double lastPulseTimeMs = 0.0;
    bool eventMonitorDirty = false;
    bool updatingTransitionCodeEditor = false;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File currentCompositionFile;
    float masterLevel = 0.9f;
    int masterMixerControlIndex = maximumMixerChannels - 1;
    bool mixerViewVisible = false;
    std::uint64_t uiFrameCounter = 0;
    double lastTimerCallbackMs = 0.0;
    double timerDeltaMs = 0.0;
    std::atomic<std::uint64_t> audioCallbackCounter { 0 };
    std::atomic<std::uint64_t> audioSampleCounter { 0 };
    juce::Array<juce::File> recentPatternFiles;
    juce::StringArray eventMonitorLines;
    std::vector<CompositionState> compositionStates;
    std::optional<CompositionState> copiedState;
    int activeStateIndex = 0;
    int activeGridSlot = 0;
    std::uint64_t activeStateEntryFrame = 0;
    int transitionPaneHeight = 150;
    double fsmGridSplitRatio = 0.5;
    int headerHatchAngle = 90;
    int transitionHatchAngle = 45;
    juce::Random transitionRandom;
    SplitterDrag activeSplitterDrag = SplitterDrag::none;
    mutable std::mutex gridRuntimeMutex;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
}
