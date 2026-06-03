#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

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
#include <cmath>
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
    void menuExportStereoWav();
    void menuShowMainView();
    void menuToggleMixerView();
    void menuToggleArrangementView();
    void menuLoadExample(const juce::File& file);
    [[nodiscard]] bool isMixerViewVisible() const noexcept;
    [[nodiscard]] bool isArrangementViewVisible() const noexcept;

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
        struct Strip
        {
            juce::String name;
            juce::String output;
            juce::Colour colour;
            bool master = false;
            float meter = 0.0f;
        };

        MixerContentComponent() { setOpaque(true); }

        void setStrips(std::vector<Strip> newStrips, int newStripWidth, int newContentHeight)
        {
            strips = std::move(newStrips);
            stripWidth = newStripWidth;
            contentHeight = newContentHeight;
            repaint();
        }

        void setMeters(const std::vector<float>& meters)
        {
            const auto count = juce::jmin(static_cast<int>(strips.size()), static_cast<int>(meters.size()));

            for (int index = 0; index < count; ++index)
            {
                auto& strip = strips[static_cast<std::size_t>(index)];
                const auto next = juce::jlimit(0.0f, 1.0f, meters[static_cast<std::size_t>(index)]);

                if (std::abs(strip.meter - next) <= 0.0015f)
                    continue;

                strip.meter = next;
                repaint(10 + index * stripWidth, 0, stripWidth, getHeight());
            }
        }

        void paint(juce::Graphics& graphics) override
        {
            const auto paper = juce::Colour::fromRGB(42, 43, 42);
            const auto strip = juce::Colour::fromRGB(87, 88, 85);
            const auto ink = juce::Colour::fromRGB(242, 242, 236);
            const auto line = juce::Colour::fromRGB(24, 25, 24);
            const auto faint = juce::Colour::fromRGB(140, 142, 136);

            graphics.fillAll(paper);
            graphics.setColour(faint.withAlpha(0.10f));
            for (int y = 26; y < getHeight(); y += 34)
                graphics.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));

            for (int index = 0; index < static_cast<int>(strips.size()); ++index)
            {
                const auto& channel = strips[static_cast<std::size_t>(index)];
                const auto x = 10 + index * stripWidth;
                auto bounds = juce::Rectangle<int>(x, 10, stripWidth - 8, juce::jmax(380, contentHeight - 20));

                graphics.setColour(channel.master ? juce::Colour::fromRGB(69, 65, 79) : strip);
                graphics.fillRect(bounds);
                graphics.setColour(line.withAlpha(channel.master ? 0.90f : 0.64f));
                graphics.drawRect(bounds, channel.master ? 2 : 1);

                auto colourBand = bounds.removeFromBottom(44);
                graphics.setColour(channel.colour);
                graphics.fillRect(colourBand);

                auto header = bounds.removeFromTop(72).reduced(6, 7);
                graphics.setColour(ink);
                graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
                graphics.drawFittedText(channel.name, header.removeFromTop(23), juce::Justification::centred, 1);

                graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain));
                graphics.setColour(ink.withAlpha(0.68f));
                graphics.drawFittedText(channel.master ? "STEREO OUT" : channel.output,
                                        header.removeFromTop(16),
                                        juce::Justification::centred,
                                        1);

                if (! channel.master)
                {
                    const auto panGuide = juce::Rectangle<int>(bounds.getX() + 13, bounds.getY() + 18, bounds.getWidth() - 26, 14);
                    graphics.setColour(line.withAlpha(0.35f));
                    graphics.drawHorizontalLine(panGuide.getCentreY(), static_cast<float>(panGuide.getX()), static_cast<float>(panGuide.getRight()));
                    graphics.drawVerticalLine(panGuide.getCentreX(), static_cast<float>(panGuide.getY()), static_cast<float>(panGuide.getBottom()));
                    graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 7.5f, juce::Font::plain));
                    graphics.setColour(ink.withAlpha(0.55f));
                    graphics.drawFittedText("L", panGuide.withTrimmedRight(panGuide.getWidth() - 10), juce::Justification::centredLeft, 1);
                    graphics.drawFittedText("R", panGuide.withTrimmedLeft(panGuide.getWidth() - 10), juce::Justification::centredRight, 1);
                }

                const auto faderLane = juce::Rectangle<int>(bounds.getCentreX() - 1,
                                                            bounds.getY() + (channel.master ? 32 : 62),
                                                            2,
                                                            juce::jmax(180, bounds.getHeight() - (channel.master ? 114 : 144)));
                graphics.setColour(line.withAlpha(0.28f));
                graphics.fillRect(faderLane.expanded(7, 0));
                graphics.setColour(line.withAlpha(0.42f));
                for (int tick = 0; tick <= 8; ++tick)
                {
                    const auto y = faderLane.getY() + tick * faderLane.getHeight() / 8;
                    const auto tickWidth = tick % 2 == 0 ? 10.0f : 6.0f;
                    graphics.drawHorizontalLine(y,
                                                static_cast<float>(faderLane.getCentreX()) - tickWidth,
                                                static_cast<float>(faderLane.getCentreX()) - 4.0f);
                    graphics.drawHorizontalLine(y,
                                                static_cast<float>(faderLane.getCentreX()) + 4.0f,
                                                static_cast<float>(faderLane.getCentreX()) + tickWidth);
                }

                const auto meterBounds = juce::Rectangle<int>(bounds.getRight() - 15,
                                                              faderLane.getY(),
                                                              5,
                                                              faderLane.getHeight());
                graphics.setColour(line.withAlpha(0.52f));
                graphics.fillRect(meterBounds);

                const auto meterLevel = juce::jlimit(0.0f, 1.0f, channel.meter);
                const auto meterHeight = static_cast<int>(std::round(static_cast<float>(meterBounds.getHeight()) * meterLevel));
                auto meterFill = meterBounds.withY(meterBounds.getBottom() - meterHeight).withHeight(meterHeight);
                const auto hot = meterLevel > 0.86f;
                const auto warm = meterLevel > 0.66f;
                graphics.setColour(hot ? juce::Colour::fromRGB(255, 75, 75)
                                       : warm ? juce::Colour::fromRGB(255, 214, 74)
                                              : juce::Colour::fromRGB(82, 220, 110));
                graphics.fillRect(meterFill);

                graphics.setColour(juce::Colours::white.withAlpha(0.88f));
                graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::bold));
                graphics.drawFittedText(channel.name, colourBand.reduced(5, 4), juce::Justification::centred, 2);
            }
        }

    private:
        std::vector<Strip> strips;
        int stripWidth = 112;
        int contentHeight = 420;
    };

    class ArrangementContentComponent final : public juce::Component
    {
    public:
        struct State
        {
            juce::String name;
            int laneCount = 0;
            int bars = 4;
            int numerator = 4;
            int denominator = 4;
            double bpm = 120.0;
            bool selected = false;
            juce::Colour colour;
            std::vector<juce::Colour> laneColours;
        };

        struct Edge
        {
            int from = 0;
            int to = 0;
            double chance = 1.0;
            bool weighted = false;
        };

        ArrangementContentComponent() { setOpaque(true); }
        void setArrangement(std::vector<State> newStates, std::vector<Edge> newEdges, int selectedIndex);
        void paint(juce::Graphics& graphics) override;

    private:
        std::vector<State> states;
        std::vector<Edge> edges;
        int selectedStateIndex = 0;
    };

    class SuperColliderCodeTokeniser final : public juce::CodeTokeniser
    {
    public:
        enum TokenType
        {
            tokenType_error = 0,
            tokenType_comment,
            tokenType_keyword,
            tokenType_builtin,
            tokenType_identifier,
            tokenType_number,
            tokenType_string,
            tokenType_symbol,
            tokenType_operator,
            tokenType_bracket,
            tokenType_punctuation
        };

        int readNextToken(juce::CodeDocument::Iterator& source) override;
        juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override;
    };

    class CodeDocumentChangeListener final : public juce::CodeDocument::Listener
    {
    public:
        void setCallback(std::function<void()> newCallback) { callback = std::move(newCallback); }
        void codeDocumentTextInserted(const juce::String&, int) override { notify(); }
        void codeDocumentTextDeleted(int, int) override { notify(); }

    private:
        void notify()
        {
            if (callback)
                callback();
        }

        std::function<void()> callback;
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
    void showExportWavDialog(double durationSeconds);
    void exportStereoWav(juce::File file, double durationSeconds);
    void finishRealtimeWavExport();
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
    void applyStateTimeSignatureEditors();
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
    void refreshMixerMeters();
    void configureArrangementView();
    void toggleArrangementView();
    void refreshArrangementView();
    void applyMixerControl(int stateIndex, int laneIndex, int mixerControlIndex, bool force = false);
    void applyMasterLevel();
    void applyLaneMixToEvents(std::vector<InternalEvent>& events, const CompositionGrid& lane, float transitionGain = 1.0f) const;
    void configureLaneCodePane();
    void styleLaneCodePane();
    void storeActiveLane();
    void storeActiveLaneLocked();
    void showActiveLane();
    void toggleSelectedLaneKind();
    void compileSelectedScLane();
    void compileScLanesForState(int stateIndex);
    void compileScLanesForAllStates();
    [[nodiscard]] juce::String createDefaultScLaneCode(int stateNumber, int laneNumber) const;
    [[nodiscard]] std::vector<juce::String> getSynthDefNamesFromSource(const juce::String& source,
                                                                       int stateNumber,
                                                                       int laneNumber) const;
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
        juce::String condition;
    };

    struct TransitionRules
    {
        std::map<int, int> linear;
        std::map<int, std::vector<TransitionChoice>> weighted;
        std::vector<int> cycle;
        std::map<juce::String, std::vector<TransitionChoice>> triggers;
    };

    struct TransitionContext
    {
        std::uint64_t frame = 0;
        std::uint64_t stateFrame = 0;
        int state = 1;
        int beat = 0;
        int bar = 0;
        juce::String triggerName;
    };

    [[nodiscard]] TransitionRules parseTransitionRules(juce::String text) const;
    [[nodiscard]] bool transitionConditionMatches(juce::String condition, const TransitionContext& context) const;
    [[nodiscard]] int chooseTransitionTarget(const TransitionRules& rules, int currentState, const TransitionContext& context);
    void applyTransitionTarget(int targetState);
    bool applyTransitionTargetForTransport(int targetState, std::uint64_t transitionFrame, double& stateBpmOut);

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
        int timeSignatureNumerator = 4;
        int timeSignatureDenominator = 4;
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
    juce::TextEditor eventMonitor;
    juce::Label eventMonitorLabel;
    SourceCodeBackdropComponent laneCodeBackdrop;
    SuperColliderCodeTokeniser scCodeTokeniser;
    juce::CodeDocument transitionCodeDocument;
    juce::CodeDocument laneScCodeDocument;
    CodeDocumentChangeListener transitionCodeDocumentListener;
    CodeDocumentChangeListener laneScCodeDocumentListener;
    juce::CodeEditorComponent transitionCodeEditor;
    juce::CodeEditorComponent laneScCodeEditor;

    static constexpr int maximumMixerChannels = 129;
    juce::Viewport mixerViewport;
    MixerContentComponent mixerContent;
    juce::Viewport arrangementViewport;
    ArrangementContentComponent arrangementContent;
    juce::Label mixerLabel;
    std::array<juce::Label, maximumMixerChannels> mixerChannelLabels;
    std::array<juce::Slider, maximumMixerChannels> mixerLevelSliders;
    std::array<juce::Slider, maximumMixerChannels> mixerPanSliders;
    std::array<juce::TextButton, maximumMixerChannels> mixerMuteButtons;
    std::array<juce::TextButton, maximumMixerChannels> mixerSoloButtons;
    std::array<std::atomic<float>, maximumMixerChannels> mixerMeterPeaks {};
    std::array<float, maximumMixerChannels> mixerMeterDisplay {};

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
    juce::Label stateTimeSignatureLabel;
    juce::TextEditor stateTimeSignatureNumeratorEditor;
    juce::Label stateTimeSignatureSeparatorLabel;
    juce::TextEditor stateTimeSignatureDenominatorEditor;
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
    bool updatingLaneCodeEditor = false;
    bool pendingLaneCodeCompile = false;
    double lastLaneCodeEditMs = 0.0;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File currentCompositionFile;
    float masterLevel = 0.9f;
    int masterMixerControlIndex = maximumMixerChannels - 1;
    double currentAudioSampleRate = 44100.0;
    int currentAudioBlockSize = 512;
    std::atomic<bool> exportInProgress { false };
    std::atomic<bool> exportCaptureActive { false };
    std::atomic<bool> exportCaptureComplete { false };
    std::atomic<int64_t> exportCaptureWritePosition { 0 };
    int64_t exportCaptureTargetSamples = 0;
    double exportCaptureSampleRate = 44100.0;
    bool exportStartedTransport = false;
    juce::File exportCaptureFile;
    std::unique_ptr<juce::AudioBuffer<float>> exportCaptureBuffer;
    std::mutex exportCaptureMutex;
    bool mixerViewVisible = false;
    bool arrangementViewVisible = false;
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
    std::atomic<bool> pendingTransitionUiRefresh { false };
    std::atomic<int> pendingTransitionUiState { -1 };
    int transitionPaneHeight = 150;
    double fsmGridSplitRatio = 0.5;
    juce::Random transitionRandom;
    SplitterDrag activeSplitterDrag = SplitterDrag::none;
    mutable std::mutex gridRuntimeMutex;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
}
