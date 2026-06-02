#include "MainComponent.h"

#include <cmath>
#include <regex>

namespace gridcollider
{
MainComponent::SourceCodeBackdropComponent::SourceCodeBackdropComponent()
{
    setInterceptsMouseClicks(false, false);
    setOpaque(false);
}

void MainComponent::SourceCodeBackdropComponent::setLanguage(const Language newLanguage)
{
    language = newLanguage;
    repaint();
}

void MainComponent::SourceCodeBackdropComponent::setSourceProvider(std::function<juce::String()> provider)
{
    sourceProvider = std::move(provider);
    repaint();
}

MainComponent::SourceCodeBackdropComponent::Kind MainComponent::SourceCodeBackdropComponent::classifyWord(const juce::String& word) const
{
    static const juce::StringArray superColliderKeywords {
        "arg", "var", "class", "this", "super", "nil", "true", "false", "inf",
        "if", "while", "for", "do", "case", "switch", "return", "break",
        "continue", "try", "catch", "protect", "new", "value", "play", "add",
        "kr", "ar", "ir"
    };
    static const juce::StringArray superColliderBuiltins {
        "SynthDef", "Synth", "Server", "Routine", "Task", "Pattern", "Pbind",
        "Pseq", "Prand", "Pwhite", "Pfunc", "Env", "EnvGen", "SinOsc",
        "Pulse", "Saw", "WhiteNoise", "Impulse", "Dust", "LFNoise0",
        "LFTri", "BPF", "RLPF", "LPF", "HPF", "FreeVerb", "Pan2", "Out"
    };
    static const juce::StringArray cppKeywords {
        "auto", "bool", "break", "case", "catch", "char", "class", "const",
        "constexpr", "continue", "double", "else", "enum", "false", "float",
        "for", "if", "int", "namespace", "private", "public", "return",
        "static", "struct", "switch", "true", "void", "while"
    };

    if (word.isEmpty())
        return Kind::normal;

    if (word.containsOnly("0123456789."))
        return Kind::number;

    if (language == Language::supercollider)
    {
        if (superColliderKeywords.contains(word))
            return Kind::keyword;
        if (superColliderBuiltins.contains(word))
            return Kind::builtin;
    }
    else if (cppKeywords.contains(word))
    {
        return Kind::keyword;
    }

    return Kind::normal;
}

juce::Colour MainComponent::SourceCodeBackdropComponent::colourForKind(const Kind kind, const float alpha) const
{
    switch (kind)
    {
        case Kind::comment:       return juce::Colour::fromRGB(118, 164, 142).withAlpha(alpha);
        case Kind::keyword:       return juce::Colour::fromRGB(224, 106, 42).withAlpha(alpha);
        case Kind::builtin:       return juce::Colour::fromRGB(96, 142, 196).withAlpha(alpha);
        case Kind::number:        return juce::Colour::fromRGB(205, 178, 70).withAlpha(alpha);
        case Kind::string:        return juce::Colour::fromRGB(111, 187, 112).withAlpha(alpha);
        case Kind::symbol:        return juce::Colour::fromRGB(202, 170, 92).withAlpha(alpha);
        case Kind::operatorToken: return juce::Colour::fromRGB(141, 120, 173).withAlpha(alpha);
        case Kind::bracket:       return juce::Colour::fromRGB(170, 176, 160).withAlpha(alpha);
        case Kind::normal:
        default:                  return juce::Colour::fromRGB(226, 230, 216).withAlpha(alpha);
    }
}

void MainComponent::SourceCodeBackdropComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour::fromRGB(18, 19, 18).withAlpha(0.92f));

    if (! sourceProvider)
        return;

    const auto text = sourceProvider();
    juce::StringArray lines;
    lines.addLines(text);

    const auto lineHeight = 17.0f;
    const auto charWidth = 8.2f;
    const auto maxLines = juce::jmin(lines.size(),
                                     juce::jmin(14, juce::jmax(1, static_cast<int>(std::ceil(static_cast<float>(getHeight()) / lineHeight)))));
    const auto left = 8.0f;
    graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));

    for (int row = 0; row < maxLines; ++row)
    {
        const auto line = lines[row];
        auto tokenStart = -1;
        auto tokenKind = Kind::normal;

        auto flushToken = [&](const int tokenEnd)
        {
            if (tokenStart < 0 || tokenEnd <= tokenStart)
                return;

            const auto token = line.substring(tokenStart, tokenEnd);
            const auto kind = tokenKind == Kind::normal ? classifyWord(token) : tokenKind;
            if (kind != Kind::normal)
            {
                const auto x = left + static_cast<float>(tokenStart) * charWidth;
                const auto y = 4.0f + static_cast<float>(row) * lineHeight;
                const auto w = juce::jmax(8.0f, static_cast<float>(tokenEnd - tokenStart) * charWidth);
                graphics.setColour(colourForKind(kind, 0.26f));
                graphics.fillRect(juce::Rectangle<float>(x, y + 1.0f, w, lineHeight - 3.0f));
            }

            tokenStart = -1;
            tokenKind = Kind::normal;
        };

        for (int column = 0; column <= line.length(); ++column)
        {
            const auto c = column < line.length() ? line[column] : ' ';
            const auto startsComment = column + 1 < line.length() && c == '/' && line[column + 1] == '/';
            const auto isQuote = c == '"' || c == '\'';
            const auto isBracket = c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}';
            const auto isOperator = juce::String("+-*/%=<>|&:;,.~").containsChar(c);
            const auto isWord = juce::CharacterFunctions::isLetterOrDigit(static_cast<juce::juce_wchar>(c)) || c == '_';

            if (startsComment)
            {
                flushToken(column);
                tokenStart = column;
                tokenKind = Kind::comment;
                flushToken(line.length());
                break;
            }

            if (isQuote || isBracket || isOperator)
            {
                flushToken(column);
                tokenStart = column;
                tokenKind = isQuote ? Kind::string : (isBracket ? Kind::bracket : Kind::operatorToken);
                flushToken(column + 1);
                continue;
            }

            if (isWord)
            {
                if (tokenStart < 0)
                    tokenStart = column;
                continue;
            }

            flushToken(column);
        }

        graphics.setColour(juce::Colour::fromRGB(226, 230, 216).withAlpha(0.08f));
        graphics.drawHorizontalLine(juce::roundToInt(4.0f + static_cast<float>(row + 1) * lineHeight),
                                    left,
                                    static_cast<float>(getWidth()) - 8.0f);
    }
}

namespace
{
constexpr int maximumCompositionStates = 16;
constexpr int maximumGridsPerState = 8;
constexpr int outerMargin = 22;
constexpr int headerHeight = 78;
constexpr int headerToTransitionGap = 10;
constexpr int transitionSplitterThickness = 10;
constexpr int gridSplitterThickness = 12;
constexpr int minimumTransitionPaneHeight = 44;
constexpr int minimumLowerPaneHeight = 240;
constexpr int minimumSplitPaneWidth = 180;
constexpr int maximumGridColumns = 64;
constexpr int maximumGridRows = 32;
constexpr double minimumGridTempoRatio = 1.0;
constexpr double maximumGridTempoRatio = 16.0;
constexpr float panelRadius = 5.0f;
constexpr float innerPanelRadius = 3.0f;

[[nodiscard]] juce::Colour lewittPaper() noexcept { return juce::Colour::fromRGB(24, 25, 23); }
[[nodiscard]] juce::Colour lewittPanel() noexcept { return juce::Colour::fromRGB(34, 36, 33); }
[[nodiscard]] juce::Colour lewittInk() noexcept { return juce::Colour::fromRGB(226, 230, 216); }
[[nodiscard]] juce::Colour lewittLine() noexcept { return juce::Colour::fromRGB(94, 99, 89); }
[[nodiscard]] juce::Colour lewittBlue() noexcept { return juce::Colour::fromRGB(96, 142, 196); }
[[nodiscard]] juce::Colour lewittRed() noexcept { return juce::Colour::fromRGB(224, 106, 42); }
[[nodiscard]] juce::Colour lewittYellow() noexcept { return juce::Colour::fromRGB(205, 178, 70); }

void drawHatch(juce::Graphics& graphics,
               juce::Rectangle<int> area,
               const int angleDegrees,
               const float spacing,
               const float alpha)
{
    if (area.isEmpty())
        return;

    juce::ignoreUnused(angleDegrees);

    graphics.saveState();
    graphics.reduceClipRegion(area);

    graphics.setColour(lewittLine().withAlpha(alpha * 0.50f));
    const auto step = juce::jmax(12, static_cast<int>(spacing));
    for (int y = area.getY(); y < area.getBottom(); y += step)
        graphics.drawHorizontalLine(y, static_cast<float>(area.getX()), static_cast<float>(area.getRight()));

    graphics.setColour(lewittLine().withAlpha(alpha * 0.24f));
    for (int x = area.getX(); x < area.getRight(); x += step * 2)
        graphics.drawVerticalLine(x, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));

    graphics.restoreState();
}

void forceBlackEditorText(juce::TextEditor& editor)
{
    editor.setColour(juce::TextEditor::textColourId, lewittInk());
    editor.setColour(juce::TextEditor::highlightedTextColourId, lewittInk());
    editor.setColour(juce::CaretComponent::caretColourId, lewittInk());
    editor.setColour(juce::Label::textColourId, lewittInk());
    editor.setColour(juce::Label::textWhenEditingColourId, lewittInk());
    editor.applyColourToAllText(lewittInk(), true);
}

[[nodiscard]] GridModel::Snapshot resizeSnapshot(GridModel::Snapshot source, const int columns, const int rows)
{
    const auto width = juce::jlimit(1, maximumGridColumns, columns);
    const auto height = juce::jlimit(1, maximumGridRows, rows);
    GridModel::Snapshot resized;
    resized.width = width;
    resized.height = height;
    resized.cells.assign(static_cast<std::size_t>(width * height), GridModel::emptyGlyph);

    for (int row = 0; row < juce::jmin(height, source.height); ++row)
        for (int column = 0; column < juce::jmin(width, source.width); ++column)
            resized.cells[static_cast<std::size_t>(column + row * width)] = source.getGlyph(column, row);

    return resized;
}
}

MainComponent::MainComponent()
    : gridEditor(gridModel)
{
    setLookAndFeel(&minimalLookAndFeel);

    CompositionGrid firstGrid;
    firstGrid.snapshot = gridModel.createSnapshot();

    CompositionState firstState;
    firstState.name = "State 01";
    firstState.transitionCode = createDefaultTransitionCode(1);
    firstState.grids.push_back(std::move(firstGrid));
    compositionStates.push_back(std::move(firstState));

    addAndMakeVisible(stateGraph);
    addAndMakeVisible(gridEditor);
    stateGraph.addKeyListener(this);
    gridEditor.addKeyListener(this);

    GridEditorComponent::Theme gridTheme;
    gridTheme.background = lewittPaper();
    gridTheme.viewportBackground = lewittPanel();
    gridTheme.gridLine = lewittLine().withAlpha(0.22f);
    gridTheme.text = lewittInk();
    gridTheme.mutedText = lewittLine().withAlpha(0.72f);
    gridTheme.rulerBackground = lewittPaper().brighter(0.10f);
    gridTheme.rulerText = lewittInk().withAlpha(0.72f);
    gridTheme.cursor = lewittYellow();
    gridTheme.cursorText = lewittPanel();
    gridTheme.selection = lewittRed().withAlpha(0.20f);
    gridTheme.selectionBorder = lewittRed();
    gridTheme.playhead = lewittBlue().withAlpha(0.16f);
    gridEditor.setTheme(gridTheme);

    configureOscControls();
    configureExampleControls();
    configurePatternControls();
    configureTransitionCodePane();
    configureEventMonitor();
    configureTransportControls();
    configureStateGraph();
    configureGridSlotControls();
    configureLaneCodePane();
    configureMixerView();
    statusLog.append("GridCollider ready");
    statusLog.append("OSC target: " + oscOutput.getEndpointDescription());

    configureTransport();
    setAudioChannels(0, 2);
    startTimerHz(30);

    setOpaque(true);
    setSize(1280, 900);
}

MainComponent::~MainComponent()
{
    stopTimer();
    setLookAndFeel(nullptr);
    transportEngine.stop();
    stateGraph.removeKeyListener(this);
    gridEditor.removeKeyListener(this);
    shutdownAudio();
}

void MainComponent::menuLoadComposition()
{
    showLoadCompositionDialog();
}

void MainComponent::menuSaveComposition()
{
    if (currentCompositionFile == juce::File())
    {
        showSaveCompositionDialog();
        return;
    }

    saveCompositionFile(currentCompositionFile);
}

void MainComponent::menuSaveCompositionAs()
{
    showSaveCompositionDialog();
}

void MainComponent::menuToggleMixerView()
{
    toggleMixerView();
}

void MainComponent::menuLoadExample(const juce::File& file)
{
    loadExampleFile(file);
}

bool MainComponent::isMixerViewVisible() const noexcept
{
    return mixerViewVisible;
}

MainComponent::MainLayout MainComponent::calculateMainLayout() const
{
    MainLayout layout;
    auto bounds = getLocalBounds().reduced(outerMargin);

    layout.header = bounds.removeFromTop(headerHeight);
    bounds.removeFromTop(headerToTransitionGap);

    const auto maximumTransitionHeight = juce::jmax(minimumTransitionPaneHeight,
                                                    bounds.getHeight() - transitionSplitterThickness - minimumLowerPaneHeight);
    const auto transitionHeight = juce::jlimit(minimumTransitionPaneHeight,
                                               maximumTransitionHeight,
                                               transitionPaneHeight);

    layout.transitionPane = bounds.removeFromTop(transitionHeight);
    layout.transitionSplitter = bounds.removeFromTop(transitionSplitterThickness);

    const auto lowerWidth = bounds.getWidth();
    const auto maxStateWidth = juce::jmax(minimumSplitPaneWidth,
                                          lowerWidth - gridSplitterThickness - minimumSplitPaneWidth);
    const auto requestedStateWidth = juce::roundToInt(static_cast<double>(lowerWidth - gridSplitterThickness) * fsmGridSplitRatio);
    const auto stateWidth = juce::jlimit(minimumSplitPaneWidth, maxStateWidth, requestedStateWidth);

    layout.statePane = bounds.removeFromLeft(stateWidth);
    layout.gridSplitter = bounds.removeFromLeft(gridSplitterThickness);
    layout.gridPane = bounds;

    return layout;
}

MainComponent::SplitterDrag MainComponent::splitterAt(const juce::Point<int> position) const
{
    const auto layout = calculateMainLayout();

    if (layout.transitionSplitter.expanded(0, 3).contains(position))
        return SplitterDrag::transition;

    if (mixerViewVisible)
        return SplitterDrag::none;

    if (layout.gridSplitter.expanded(3, 0).contains(position))
        return SplitterDrag::grid;

    return SplitterDrag::none;
}

void MainComponent::updateSplitterCursor(const juce::Point<int> position)
{
    switch (splitterAt(position))
    {
        case SplitterDrag::transition:
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            break;
        case SplitterDrag::grid:
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            break;
        case SplitterDrag::none:
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
    }
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(lewittPaper());

    const auto layout = calculateMainLayout();
    auto header = layout.header;

    graphics.setColour(lewittPanel());
    graphics.fillRoundedRectangle(layout.header.toFloat(), panelRadius);
    drawHatch(graphics, layout.header.reduced(1), headerHatchAngle, 14.0f, 0.045f);
    if (! mixerViewVisible)
    {
        graphics.fillRoundedRectangle(layout.transitionPane.toFloat(), panelRadius);
        drawHatch(graphics, layout.transitionPane.reduced(1), transitionHatchAngle, 16.0f, 0.035f);
    }

    graphics.setColour(lewittLine());
    graphics.drawRoundedRectangle(layout.header.toFloat().reduced(0.5f), panelRadius, 1.0f);
    if (! mixerViewVisible)
        graphics.drawRoundedRectangle(layout.transitionPane.toFloat().reduced(0.5f), panelRadius, 1.0f);

    auto lowerWorkspace = getLocalBounds().reduced(outerMargin);
    lowerWorkspace.removeFromTop(mixerViewVisible
                                     ? headerHeight + headerToTransitionGap
                                     : headerHeight + headerToTransitionGap + layout.transitionPane.getHeight() + transitionSplitterThickness);

    if (mixerViewVisible)
    {
        auto mixerArea = lowerWorkspace;

        graphics.setColour(lewittPanel());
        graphics.fillRoundedRectangle(mixerArea.toFloat(), panelRadius);
        graphics.setColour(lewittLine());
        graphics.drawRoundedRectangle(mixerArea.toFloat().reduced(0.5f), panelRadius, 1.0f);
    }
    else
    {
        graphics.drawRoundedRectangle(layout.statePane.toFloat().reduced(0.5f), innerPanelRadius, 1.0f);
        graphics.drawRoundedRectangle(layout.gridPane.toFloat().reduced(0.5f), innerPanelRadius, 1.0f);

        graphics.setColour(lewittLine().withAlpha(activeSplitterDrag == SplitterDrag::grid ? 1.0f : 0.55f));
        graphics.drawVerticalLine(layout.gridSplitter.getCentreX(),
                                  static_cast<float>(layout.gridSplitter.getY()),
                                  static_cast<float>(layout.gridSplitter.getBottom()));

        const auto gridHandle = layout.gridSplitter.withSizeKeepingCentre(6, 96);
        graphics.drawRoundedRectangle(gridHandle.toFloat().reduced(0.5f), 2.0f, 1.0f);
    }

    if (! mixerViewVisible)
    {
        graphics.setColour(lewittLine().withAlpha(activeSplitterDrag == SplitterDrag::transition ? 1.0f : 0.55f));
        graphics.drawHorizontalLine(layout.transitionSplitter.getCentreY(),
                                    static_cast<float>(layout.transitionSplitter.getX()),
                                    static_cast<float>(layout.transitionSplitter.getRight()));

        const auto transitionHandle = layout.transitionSplitter.withSizeKeepingCentre(96, 6);
        graphics.drawRoundedRectangle(transitionHandle.toFloat().reduced(0.5f), 2.0f, 1.0f);
    }

    auto headerRows = header.reduced(12, 9);
    auto transportRow = headerRows.removeFromTop(34);
    headerRows.removeFromTop(5);
    auto stateRow = headerRows.removeFromTop(24);

    graphics.setColour(lewittLine().withAlpha(0.45f));
    graphics.drawHorizontalLine(stateRow.getY() - 5, static_cast<float>(header.getX() + 12), static_cast<float>(header.getRight() - 12));

    graphics.setColour(lewittInk());
    graphics.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    graphics.drawText("GridCollider", transportRow.removeFromLeft(220), juce::Justification::centredLeft);

    auto readoutArea = transportRow.removeFromRight(juce::jlimit(300, 430, transportRow.getWidth() / 2));
    auto pulseArea = readoutArea.removeFromRight(22).toFloat();
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - lastPulseTimeMs;
    const auto pulseAlpha = transportEngine.isPlaying()
                                ? juce::jlimit(0.25f, 1.0f, 1.0f - static_cast<float>(elapsed / 180.0))
                                : 0.18f;

    graphics.setColour(lewittInk().withAlpha(pulseAlpha));
    graphics.fillRect(pulseArea.withSizeKeepingCentre(8.0f, 8.0f));

    graphics.setColour(lewittInk());
    graphics.setFont(juce::FontOptions(14.0f));
    graphics.drawFittedText(getTransportStateText(), readoutArea, juce::Justification::centredRight, 1);

    if (! mixerViewVisible)
    {
        auto transitionHeader = layout.transitionPane.reduced(10, 0).removeFromTop(26);
        graphics.setColour(lewittLine().withAlpha(0.45f));
        graphics.drawHorizontalLine(transitionHeader.getBottom(), static_cast<float>(transitionHeader.getX()), static_cast<float>(transitionHeader.getRight()));
    }

}

void MainComponent::resized()
{
    const auto layout = calculateMainLayout();
    auto header = layout.header;
    auto transitionArea = layout.transitionPane;

    auto lowerWorkspace = getLocalBounds().reduced(outerMargin);
    lowerWorkspace.removeFromTop(mixerViewVisible
                                     ? headerHeight + headerToTransitionGap
                                     : headerHeight + headerToTransitionGap + layout.transitionPane.getHeight() + transitionSplitterThickness);

    if (mixerViewVisible)
    {
        auto mixerArea = lowerWorkspace.reduced(0, 0);

        stateGraph.setVisible(false);
        transitionCodeLabel.setVisible(false);
        transitionCodeBackdrop.setVisible(false);
        transitionCodeEditor.setVisible(false);
        mixerViewport.setBounds(mixerArea);
        mixerViewport.setVisible(true);
        mixerViewport.toFront(false);
        refreshMixerView();

        gridEditor.setVisible(false);
        laneCodeBackdrop.setVisible(false);
        laneScCodeEditor.setVisible(false);
        for (auto& button : gridTabButtons)
            button.setVisible(false);
    }
    else
    {
        stateGraph.setVisible(true);
        stateGraph.setBounds(layout.statePane);
        stateGraph.fitToView();
        transitionCodeLabel.setVisible(true);
        transitionCodeBackdrop.setVisible(true);
        transitionCodeEditor.setVisible(true);
        mixerViewport.setVisible(false);
    }

    auto gridArea = layout.gridPane.reduced(12, 10);
    auto gridTabs = gridArea.removeFromTop(28);
    gridArea.removeFromTop(8);
    gridEditor.setBounds(gridArea);
    laneCodeBackdrop.setBounds(gridArea.reduced(3));
    laneScCodeEditor.setBounds(gridArea.reduced(3));
    if (! mixerViewVisible)
        gridEditor.fitToView();

    int visibleTabs = 0;
    for (const auto& button : gridTabButtons)
        if (button.isVisible())
            ++visibleTabs;

    if (! mixerViewVisible && visibleTabs > 0)
    {
        const auto tabWidth = juce::jlimit(52, 94, (gridTabs.getWidth() - (visibleTabs - 1) * 6) / visibleTabs);

        for (auto& button : gridTabButtons)
        {
            if (! button.isVisible())
            {
                button.setBounds({});
                continue;
            }

            button.setBounds(gridTabs.removeFromLeft(tabWidth).withSizeKeepingCentre(tabWidth, 24));
            gridTabs.removeFromLeft(6);
        }
    }

    auto headerRows = header.reduced(12, 9);
    auto transportRow = headerRows.removeFromTop(34);
    headerRows.removeFromTop(5);
    auto stateRow = headerRows.removeFromTop(24);

    juce::Rectangle<int> titleArea = transportRow.removeFromLeft(216);
    const auto topControlHeight = 30;
    playPauseButton.setBounds(transportRow.removeFromLeft(72).withSizeKeepingCentre(72, topControlHeight));
    transportRow.removeFromLeft(8);
    stopButton.setBounds(transportRow.removeFromLeft(56).withSizeKeepingCentre(56, topControlHeight));
    transportRow.removeFromLeft(8);
    resetButton.setBounds(transportRow.removeFromLeft(64).withSizeKeepingCentre(64, topControlHeight));
    transportRow.removeFromLeft(20);
    bpmLabel.setBounds(transportRow.removeFromLeft(34).withSizeKeepingCentre(34, 24));
    bpmEditor.setBounds(transportRow.removeFromLeft(64).withSizeKeepingCentre(64, topControlHeight));
    transportRow.removeFromLeft(10);
    juce::ignoreUnused(titleArea);

    stateRow.removeFromLeft(216);
    auto rightStateRow = stateRow.removeFromRight(452);
    const auto rowControlHeight = 22;

    gridSizeLabel.setBounds(rightStateRow.removeFromLeft(42).withSizeKeepingCentre(42, rowControlHeight));
    rightStateRow.removeFromLeft(4);
    gridColumnsEditor.setBounds(rightStateRow.removeFromLeft(42).withSizeKeepingCentre(42, rowControlHeight));
    gridSizeSeparatorLabel.setBounds(rightStateRow.removeFromLeft(14).withSizeKeepingCentre(14, rowControlHeight));
    gridRowsEditor.setBounds(rightStateRow.removeFromLeft(42).withSizeKeepingCentre(42, rowControlHeight));
    rightStateRow.removeFromLeft(14);
    laneKindButton.setBounds(rightStateRow.removeFromLeft(56).withSizeKeepingCentre(56, rowControlHeight));
    rightStateRow.removeFromLeft(12);
    phaseModeButton.setBounds(rightStateRow.removeFromLeft(68).withSizeKeepingCentre(68, rowControlHeight));
    rightStateRow.removeFromLeft(6);
    phaseOffsetEditor.setBounds(rightStateRow.removeFromLeft(44).withSizeKeepingCentre(44, rowControlHeight));
    rightStateRow.removeFromLeft(12);
    gridRatioLabel.setBounds(rightStateRow.removeFromLeft(26).withSizeKeepingCentre(26, rowControlHeight));
    gridRatioEditor.setBounds(rightStateRow.removeFromLeft(54).withSizeKeepingCentre(54, rowControlHeight));

    stateRow.removeFromRight(18);
    stateSlotLabel.setBounds(stateRow.removeFromLeft(82).withSizeKeepingCentre(82, rowControlHeight));
    stateRow.removeFromLeft(6);
    previousStateButton.setBounds(stateRow.removeFromLeft(26).withSizeKeepingCentre(26, rowControlHeight));
    stateRow.removeFromLeft(5);
    nextStateButton.setBounds(stateRow.removeFromLeft(26).withSizeKeepingCentre(26, rowControlHeight));
    stateRow.removeFromLeft(5);
    addStateButton.setBounds(stateRow.removeFromLeft(26).withSizeKeepingCentre(26, rowControlHeight));
    stateRow.removeFromLeft(16);
    stateAdvanceLabel.setBounds(stateRow.removeFromLeft(34).withSizeKeepingCentre(34, rowControlHeight));
    stateRow.removeFromLeft(6);
    stateAdvanceModeButton.setBounds(stateRow.removeFromLeft(78).withSizeKeepingCentre(78, rowControlHeight));
    stateRow.removeFromLeft(6);
    stateAdvanceIntervalEditor.setBounds(stateRow.removeFromLeft(38).withSizeKeepingCentre(38, rowControlHeight));
    stateRow.removeFromLeft(16);
    gridSlotLabel.setBounds(stateRow.removeFromLeft(78).withSizeKeepingCentre(78, rowControlHeight));
    stateRow.removeFromLeft(6);
    previousGridButton.setBounds(stateRow.removeFromLeft(26).withSizeKeepingCentre(26, rowControlHeight));
    stateRow.removeFromLeft(5);
    nextGridButton.setBounds(stateRow.removeFromLeft(26).withSizeKeepingCentre(26, rowControlHeight));
    stateRow.removeFromLeft(5);
    addGridButton.setBounds(stateRow.removeFromLeft(26).withSizeKeepingCentre(26, rowControlHeight));

    for (auto* component : { static_cast<juce::Component*>(&loadPatternButton),
                             static_cast<juce::Component*>(&savePatternButton),
                             static_cast<juce::Component*>(&recentPatternButton),
                             static_cast<juce::Component*>(&loadExampleButton),
                             static_cast<juce::Component*>(&embeddedScTestButton),
                             static_cast<juce::Component*>(&oscStatusLabel),
                             static_cast<juce::Component*>(&oscHostEditor),
                             static_cast<juce::Component*>(&oscPortEditor),
                             static_cast<juce::Component*>(&oscConnectButton),
                             static_cast<juce::Component*>(&oscDebugToggle),
                             static_cast<juce::Component*>(&eventMonitorLabel),
                             static_cast<juce::Component*>(&eventMonitor) })
    {
        component->setBounds({});
        component->setVisible(false);
    }

    transitionArea = transitionArea.reduced(12, 2);
    transitionCodeLabel.setBounds(transitionArea.removeFromTop(24));
    transitionCodeBackdrop.setBounds(transitionArea.reduced(2, 8));
    transitionCodeEditor.setBounds(transitionArea.reduced(2, 8));
}

void MainComponent::mouseMove(const juce::MouseEvent& event)
{
    updateSplitterCursor(event.getPosition());
}

void MainComponent::mouseExit(const juce::MouseEvent&)
{
    if (activeSplitterDrag == SplitterDrag::none)
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    activeSplitterDrag = splitterAt(event.getPosition());

    if (activeSplitterDrag != SplitterDrag::none)
        updateSplitterCursor(event.getPosition());
}

void MainComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (activeSplitterDrag == SplitterDrag::none)
        return;

    const auto bounds = getLocalBounds().reduced(outerMargin);
    const auto contentTop = bounds.getY() + headerHeight + headerToTransitionGap;

    if (activeSplitterDrag == SplitterDrag::transition)
    {
        const auto availableHeight = bounds.getBottom() - contentTop;
        const auto maximumTransitionHeight = juce::jmax(minimumTransitionPaneHeight,
                                                        availableHeight - transitionSplitterThickness - minimumLowerPaneHeight);
        transitionPaneHeight = juce::jlimit(minimumTransitionPaneHeight,
                                            maximumTransitionHeight,
                                            event.getPosition().y - contentTop);
    }
    else if (activeSplitterDrag == SplitterDrag::grid)
    {
        const auto layout = calculateMainLayout();
        const auto lowerWidth = layout.statePane.getWidth() + layout.gridSplitter.getWidth() + layout.gridPane.getWidth();
        const auto usableWidth = juce::jmax(1, lowerWidth - gridSplitterThickness);
        const auto localX = event.getPosition().x - layout.statePane.getX();
        const auto stateWidth = juce::jlimit(minimumSplitPaneWidth,
                                             juce::jmax(minimumSplitPaneWidth, usableWidth - minimumSplitPaneWidth),
                                             localX);
        fsmGridSplitRatio = static_cast<double>(stateWidth) / static_cast<double>(usableWidth);
    }

    resized();
    repaint();
}

void MainComponent::mouseUp(const juce::MouseEvent& event)
{
    activeSplitterDrag = SplitterDrag::none;
    updateSplitterCursor(event.getPosition());
    repaint();
}

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{
    const auto keyCode = key.getKeyCode();
    const auto shortcut = key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown();
    const auto stateGraphFocused = originatingComponent == &stateGraph || stateGraph.hasKeyboardFocus(true);

    if (stateGraphFocused)
    {
        if (keyCode == juce::KeyPress::deleteKey || keyCode == juce::KeyPress::backspaceKey)
        {
            deleteSelectedState();
            return true;
        }

        const auto shortcutChar = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

        if (shortcut && shortcutChar == 'c')
        {
            copySelectedState();
            return true;
        }

        if (shortcut && shortcutChar == 'v')
        {
            pasteCopiedState();
            return true;
        }
    }

    if (keyCode == juce::KeyPress::F5Key)
    {
        toggleTransportPlayback();
        return true;
    }

    if (keyCode == juce::KeyPress::F6Key)
    {
        stopTransport();
        return true;
    }

    if (keyCode == juce::KeyPress::F7Key)
    {
        resetTransport();
        return true;
    }

    if (keyCode == juce::KeyPress::F8Key)
    {
        showExampleMenu();
        return true;
    }

    if (keyCode == juce::KeyPress::F9Key)
    {
        showLoadPatternDialog();
        return true;
    }

    if (keyCode == juce::KeyPress::F10Key)
    {
        showSavePatternDialog();
        return true;
    }

    if (keyCode == juce::KeyPress::F11Key)
    {
        showRecentPatternMenu();
        return true;
    }

    if (keyCode == juce::KeyPress::F12Key)
    {
        triggerEmbeddedScTest();
        return true;
    }

    if (keyCode == juce::KeyPress::F1Key)
    {
        previousGridSlot();
        return true;
    }

    if (keyCode == juce::KeyPress::F2Key)
    {
        nextGridSlot();
        return true;
    }

    if (keyCode == juce::KeyPress::F3Key)
    {
        addGridSlot();
        return true;
    }

    if (keyCode == juce::KeyPress::F4Key)
    {
        addCompositionState();
        return true;
    }

    if (shortcut && keyCode >= static_cast<int>('1') && keyCode <= static_cast<int>('9'))
    {
        switchToGridSlot(keyCode - static_cast<int>('1'));
        return true;
    }

    return false;
}

void MainComponent::timerCallback()
{
    ++uiFrameCounter;
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    if (lastTimerCallbackMs > 0.0)
        timerDeltaMs = nowMs - lastTimerCallbackMs;
    lastTimerCallbackMs = nowMs;

    if (eventMonitorDirty)
        refreshEventMonitor();

    if (transportEngine.isPlaying())
    {
        repaint(0, 0, getWidth(), 92);
        repaint(0, juce::jmax(0, getHeight() - 76), getWidth(), 76);
    }
}

void MainComponent::prepareToPlay(const int samplesPerBlockExpected, const double sampleRate)
{
    if (embeddedScAudio.prepare(sampleRate, samplesPerBlockExpected, 2))
    {
        embeddedScAudio.setMasterLevel(masterLevel);
        statusLog.append("Embedded SuperCollider audio ready");
    }
    else
    {
        statusLog.append("Embedded SuperCollider unavailable: " + embeddedScAudio.getLastError());
    }

    repaint();
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (bufferToFill.buffer == nullptr)
        return;

    juce::AudioBuffer<float> output(bufferToFill.buffer->getArrayOfWritePointers(),
                                    bufferToFill.buffer->getNumChannels(),
                                    bufferToFill.startSample,
                                    bufferToFill.numSamples);
    audioCallbackCounter.fetch_add(1, std::memory_order_relaxed);
    audioSampleCounter.fetch_add(static_cast<std::uint64_t>(bufferToFill.numSamples), std::memory_order_relaxed);
    embeddedScAudio.render(output);
}

void MainComponent::releaseResources()
{
    embeddedScAudio.release();
}

void MainComponent::configureOscControls()
{
    addAndMakeVisible(oscStatusLabel);
    addAndMakeVisible(oscHostEditor);
    addAndMakeVisible(oscPortEditor);
    addAndMakeVisible(oscConnectButton);
    addAndMakeVisible(oscDebugToggle);

    oscHostEditor.setText(oscOutput.getHost(), juce::dontSendNotification);
    oscPortEditor.setText(juce::String(oscOutput.getPort()), juce::dontSendNotification);
    oscConnectButton.setButtonText("CONNECT");
    oscDebugToggle.setButtonText("DEBUG");

    oscHostEditor.onReturnKey = [this] { connectOscFromControls(); };
    oscPortEditor.onReturnKey = [this] { connectOscFromControls(); };

    oscConnectButton.onClick = [this]
    {
        if (oscOutput.isConnected())
        {
            [[maybe_unused]] const auto disconnected = oscOutput.disconnect();
            statusLog.append("OSC disconnected");
        }
        else
        {
            connectOscFromControls();
        }

        updateOscStatusUi();
        repaint();
    };

    oscDebugToggle.onClick = [this]
    {
        oscOutput.setDebugMode(oscDebugToggle.getToggleState());
        statusLog.append(oscOutput.isDebugModeEnabled() ? "OSC debug on" : "OSC debug off");
    };

    styleOscControls();
    updateOscStatusUi();
}

void MainComponent::styleOscControls()
{
    const auto background = lewittPaper();
    const auto outline = lewittLine();
    const auto text = lewittInk();

    for (auto* editor : { &oscHostEditor, &oscPortEditor })
    {
        editor->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        editor->setColour(juce::TextEditor::backgroundColourId, background);
        editor->setColour(juce::TextEditor::outlineColourId, outline);
        editor->setColour(juce::TextEditor::focusedOutlineColourId, text);
        editor->setColour(juce::TextEditor::textColourId, text);
        editor->setColour(juce::TextEditor::highlightColourId, lewittBlue().withAlpha(0.35f));
        editor->setJustification(juce::Justification::centred);
    }

    oscConnectButton.setColour(juce::TextButton::buttonColourId, background);
    oscConnectButton.setColour(juce::TextButton::buttonOnColourId, lewittBlue().withAlpha(0.35f));
    oscConnectButton.setColour(juce::TextButton::textColourOffId, text);
    oscConnectButton.setColour(juce::TextButton::textColourOnId, text);
    oscConnectButton.setColour(juce::ComboBox::outlineColourId, outline);

    oscDebugToggle.setColour(juce::ToggleButton::textColourId, text);
    oscDebugToggle.setColour(juce::ToggleButton::tickColourId, text);
    oscDebugToggle.setColour(juce::ToggleButton::tickDisabledColourId, outline);

    oscStatusLabel.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
    oscStatusLabel.setJustificationType(juce::Justification::centredLeft);
    oscStatusLabel.setColour(juce::Label::textColourId, text);
}

void MainComponent::connectOscFromControls()
{
    const auto host = oscHostEditor.getText().trim();
    const auto port = oscPortEditor.getText().getIntValue();

    oscOutput.setEndpoint(host.isEmpty() ? "127.0.0.1" : host, juce::jlimit(1, 65535, port));

    if (oscOutput.connect())
        statusLog.append("OSC connected to " + oscOutput.getEndpointDescription());
    else
        statusLog.append("OSC connect failed: " + oscOutput.getEndpointDescription());

    updateOscStatusUi();
    repaint();
}

void MainComponent::updateOscStatusUi()
{
    oscConnectButton.setButtonText(oscOutput.isConnected() ? "DISCONNECT" : "CONNECT");
    oscStatusLabel.setText("OSC " + oscOutput.getConnectionStatusText(), juce::dontSendNotification);
    oscStatusLabel.setColour(juce::Label::textColourId, lewittInk());
}

void MainComponent::configureExampleControls()
{
    addAndMakeVisible(loadExampleButton);
    loadExampleButton.setButtonText("EXAMPLE");
    stylePatternButton(loadExampleButton);
    loadExampleButton.onClick = [this] { showExampleMenu(); };
}

void MainComponent::loadExampleFile(const juce::File& file)
{
    if (file.hasFileExtension(".gridcollider") || file.hasFileExtension(".json"))
    {
        loadCompositionFile(file);
        return;
    }

    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();
    storeActiveTransitionCode();
    transportEngine.reset();
    lastTransportFrame = 0;
    lastTickInBeat = 0;
    activeStateEntryFrame = 0;
    gridEditor.clearPlayhead();

    juce::Result result = juce::Result::ok();

    {
        const std::lock_guard lock(gridRuntimeMutex);
        result = presetManager.load(file, gridModel);
    }

    if (result.wasOk())
    {
        storeActiveGridSlot();
        updateGridSlotControls();
        gridEditor.fitToView();
        statusLog.append("Loaded example: " + file.getFileNameWithoutExtension());
        addRecentPatternFile(file);
    }
    else
    {
        statusLog.append("Example load failed: " + result.getErrorMessage());
    }

    if (wasPlaying)
        transportEngine.start();

    repaint();
}

void MainComponent::loadFirstExample()
{
    const auto examples = presetManager.findExampleFiles();

    if (examples.isEmpty())
    {
        statusLog.append("No example files found");
        repaint();
        return;
    }

    loadExampleFile(examples.getFirst());
}

void MainComponent::showExampleMenu()
{
    const auto examples = presetManager.findExampleFiles();

    if (examples.isEmpty())
    {
        statusLog.append("No example files found");
        repaint();
        return;
    }

    juce::PopupMenu menu;

    for (int index = 0; index < examples.size(); ++index)
        menu.addItem(index + 1, examples[index].getFileNameWithoutExtension());

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(loadExampleButton),
                       [this, examples](const int result)
                       {
                           if (result <= 0 || result > examples.size())
                               return;

                           loadExampleFile(examples[result - 1]);
                       });
}

void MainComponent::configurePatternControls()
{
    addAndMakeVisible(loadPatternButton);
    addAndMakeVisible(savePatternButton);
    addAndMakeVisible(recentPatternButton);
    addAndMakeVisible(embeddedScTestButton);

    loadPatternButton.setButtonText("LOAD");
    savePatternButton.setButtonText("SAVE");
    recentPatternButton.setButtonText("RECENT");
    embeddedScTestButton.setButtonText("TEST");

    for (auto* button : { &loadPatternButton, &savePatternButton, &recentPatternButton, &embeddedScTestButton })
        stylePatternButton(*button);

    loadPatternButton.onClick = [this] { showLoadPatternDialog(); };
    savePatternButton.onClick = [this] { showSavePatternDialog(); };
    recentPatternButton.onClick = [this] { showRecentPatternMenu(); };
    embeddedScTestButton.onClick = [this] { triggerEmbeddedScTest(); };
}

void MainComponent::stylePatternButton(juce::TextButton& button)
{
    button.setColour(juce::TextButton::buttonColourId, lewittPanel().brighter(0.08f));
    button.setColour(juce::TextButton::buttonOnColourId, lewittBlue().withAlpha(0.35f));
    button.setColour(juce::TextButton::textColourOffId, lewittInk());
    button.setColour(juce::TextButton::textColourOnId, lewittInk());
    button.setColour(juce::ComboBox::outlineColourId, lewittLine());
}

void MainComponent::showLoadCompositionDialog()
{
    const auto start = currentCompositionFile != juce::File()
                           ? currentCompositionFile.getParentDirectory()
                           : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    fileChooser = std::make_unique<juce::FileChooser>("Load GridCollider composition", start, "*.gridcollider;*.json;*.orca");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser)
                             {
                                 if (safeThis == nullptr)
                                     return;

                                 const auto file = chooser.getResult();

                                 if (file.existsAsFile())
                                     safeThis->loadCompositionFile(file);
                             });
}

void MainComponent::showSaveCompositionDialog()
{
    auto start = currentCompositionFile != juce::File()
                     ? currentCompositionFile
                     : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("GridCollider composition.gridcollider");
    fileChooser = std::make_unique<juce::FileChooser>("Save GridCollider composition", start, "*.gridcollider");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser)
                             {
                                 if (safeThis == nullptr)
                                     return;

                                 auto file = chooser.getResult();

                                 if (file != juce::File())
                                     safeThis->saveCompositionFile(file);
                             });
}

juce::String MainComponent::snapshotToText(const GridModel::Snapshot& snapshot) const
{
    juce::String text;

    for (int row = 0; row < snapshot.height; ++row)
    {
        for (int column = 0; column < snapshot.width; ++column)
            text += juce::String::charToString(snapshot.getGlyph(column, row));

        if (row + 1 < snapshot.height)
            text += "\n";
    }

    return text;
}

GridModel::Snapshot MainComponent::snapshotFromText(const juce::String& text, const int width, const int height) const
{
    auto snapshot = makeEmptyGridSnapshot(width, height);
    const auto lines = juce::StringArray::fromLines(text);

    for (int row = 0; row < juce::jmin(snapshot.height, lines.size()); ++row)
    {
        const auto line = lines[row];

        for (int column = 0; column < juce::jmin(snapshot.width, line.length()); ++column)
        {
            const auto character = line[column];
            snapshot.cells[static_cast<std::size_t>(column + row * snapshot.width)] =
                character >= 32 && character <= 126 ? static_cast<char>(character) : GridModel::emptyGlyph;
        }
    }

    return snapshot;
}

juce::var MainComponent::serialiseComposition() const
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("format", "GridColliderComposition");
    root->setProperty("version", 1);
    root->setProperty("activeState", activeStateIndex);
    root->setProperty("activeLane", activeGridSlot);
    root->setProperty("masterLevel", masterLevel);

    juce::Array<juce::var> statesArray;

    for (const auto& state : compositionStates)
    {
        auto stateObject = std::make_unique<juce::DynamicObject>();
        stateObject->setProperty("name", state.name);
        stateObject->setProperty("bpm", state.bpm);
        stateObject->setProperty("advanceMode", getStateAdvanceModeText(state.advanceMode));
        stateObject->setProperty("advanceInterval", state.advanceInterval);
        stateObject->setProperty("transitionCode", state.transitionCode);

        juce::Array<juce::var> lanesArray;

        for (const auto& lane : state.grids)
        {
            auto laneObject = std::make_unique<juce::DynamicObject>();
            laneObject->setProperty("kind", lane.kind == CompositionGrid::Kind::grid ? "grid" : "supercollider");
            laneObject->setProperty("tempoRatio", lane.tempoRatio);
            laneObject->setProperty("phaseOffsetEnabled", lane.phaseOffsetEnabled);
            laneObject->setProperty("phaseOffsetDegrees", lane.phaseOffsetDegrees);
            laneObject->setProperty("mixerLevel", lane.mixerLevel);
            laneObject->setProperty("mixerPan", lane.mixerPan);
            laneObject->setProperty("scCode", lane.scCode);
            laneObject->setProperty("scSynthName", lane.scSynthName);
            laneObject->setProperty("gridWidth", lane.snapshot.width);
            laneObject->setProperty("gridHeight", lane.snapshot.height);
            laneObject->setProperty("gridText", snapshotToText(lane.snapshot));
            lanesArray.add(juce::var(laneObject.release()));
        }

        stateObject->setProperty("lanes", lanesArray);
        statesArray.add(juce::var(stateObject.release()));
    }

    root->setProperty("states", statesArray);
    return juce::var(root.release());
}

juce::Result MainComponent::restoreComposition(const juce::var& document)
{
    if (! document.isObject())
        return juce::Result::fail("Composition file is not a JSON object");

    const auto* root = document.getDynamicObject();
    if (root == nullptr || root->getProperty("format").toString() != "GridColliderComposition")
        return juce::Result::fail("Not a GridCollider composition file");

    const auto* statesVar = root->getProperty("states").getArray();
    if (statesVar == nullptr || statesVar->isEmpty())
        return juce::Result::fail("Composition has no states");

    std::vector<CompositionState> loadedStates;
    loadedStates.reserve(static_cast<std::size_t>(juce::jmin(maximumCompositionStates, statesVar->size())));

    for (int stateIndex = 0; stateIndex < juce::jmin(maximumCompositionStates, statesVar->size()); ++stateIndex)
    {
        const auto stateVar = statesVar->getReference(stateIndex);
        const auto* stateObject = stateVar.getDynamicObject();

        if (stateObject == nullptr)
            continue;

        CompositionState state;
        state.name = stateObject->getProperty("name").toString();
        if (state.name.isEmpty())
            state.name = "State " + juce::String(stateIndex + 1).paddedLeft('0', 2);

        state.bpm = juce::jlimit(20.0, 320.0, static_cast<double>(stateObject->getProperty("bpm")));
        state.advanceInterval = juce::jlimit(1, 256, static_cast<int>(stateObject->getProperty("advanceInterval")));
        state.transitionCode = stateObject->getProperty("transitionCode").toString();
        if (state.transitionCode.isEmpty())
            state.transitionCode = createDefaultTransitionCode(stateIndex + 1);

        const auto mode = stateObject->getProperty("advanceMode").toString().toLowerCase();
        if (mode == "beats")
            state.advanceMode = CompositionState::AdvanceMode::beats;
        else if (mode == "bars")
            state.advanceMode = CompositionState::AdvanceMode::bars;
        else if (mode == "trigger")
            state.advanceMode = CompositionState::AdvanceMode::trigger;
        else
            state.advanceMode = CompositionState::AdvanceMode::manual;

        if (const auto* lanesVar = stateObject->getProperty("lanes").getArray())
        {
            for (int laneIndex = 0; laneIndex < juce::jmin(maximumGridsPerState, lanesVar->size()); ++laneIndex)
            {
                const auto laneVar = lanesVar->getReference(laneIndex);
                const auto* laneObject = laneVar.getDynamicObject();

                if (laneObject == nullptr)
                    continue;

                CompositionGrid lane;
                lane.kind = laneObject->getProperty("kind").toString() == "supercollider"
                                ? CompositionGrid::Kind::supercollider
                                : CompositionGrid::Kind::grid;
                lane.tempoRatio = juce::jlimit(minimumGridTempoRatio, maximumGridTempoRatio, static_cast<double>(laneObject->getProperty("tempoRatio")));
                lane.phaseOffsetEnabled = static_cast<bool>(laneObject->getProperty("phaseOffsetEnabled"));
                lane.phaseOffsetDegrees = juce::jlimit(0.0, 360.0, static_cast<double>(laneObject->getProperty("phaseOffsetDegrees")));
                lane.mixerLevel = juce::jlimit(0.0f, 1.25f, static_cast<float>(static_cast<double>(laneObject->getProperty("mixerLevel"))));
                lane.mixerPan = juce::jlimit(-1.0f, 1.0f, static_cast<float>(static_cast<double>(laneObject->getProperty("mixerPan"))));
                lane.scCode = laneObject->getProperty("scCode").toString();
                lane.scSynthName = laneObject->getProperty("scSynthName").toString();
                lane.scCodeDirty = lane.kind == CompositionGrid::Kind::supercollider;
                lane.snapshot = snapshotFromText(laneObject->getProperty("gridText").toString(),
                                                 static_cast<int>(laneObject->getProperty("gridWidth")),
                                                 static_cast<int>(laneObject->getProperty("gridHeight")));
                state.grids.push_back(std::move(lane));
            }
        }

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        loadedStates.push_back(std::move(state));
    }

    if (loadedStates.empty())
        return juce::Result::fail("Composition did not contain readable states");

    compositionStates = std::move(loadedStates);
    activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, static_cast<int>(root->getProperty("activeState")));
    activeGridSlot = juce::jlimit(0,
                                  static_cast<int>(compositionStates[static_cast<std::size_t>(activeStateIndex)].grids.size()) - 1,
                                  static_cast<int>(root->getProperty("activeLane")));
    masterLevel = juce::jlimit(0.0f, 1.25f, static_cast<float>(static_cast<double>(root->getProperty("masterLevel"))));
    return juce::Result::ok();
}

void MainComponent::loadCompositionFile(const juce::File& file)
{
    if (file.hasFileExtension(".orca"))
    {
        loadPatternFile(file, true);
        return;
    }

    const auto wasPlaying = transportEngine.isPlaying();
    if (wasPlaying)
        transportEngine.pause();

    const auto text = file.loadFileAsString();
    juce::var parsed = juce::JSON::parse(text);

    if (parsed.isVoid())
    {
        statusLog.append("Composition load failed: invalid JSON");
        repaint();
        return;
    }

    juce::Result result = juce::Result::ok();
    {
        const std::lock_guard lock(gridRuntimeMutex);
        result = restoreComposition(parsed);

        if (result.wasOk())
        {
            auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];
            gridModel.applySnapshot(state.grids[static_cast<std::size_t>(activeGridSlot)].snapshot);
        }
    }

    if (result.wasOk())
    {
        currentCompositionFile = file;
        transportEngine.reset();
        lastTransportFrame = 0;
        lastTickInBeat = 0;
        activeStateEntryFrame = 0;
        resetGridRuntimeClocks();
        embeddedScAudio.setMasterLevel(masterLevel);
        updateTransportControls();
        updateGridSlotControls();
        showActiveTransitionCode();
        compileScLanesForState(activeStateIndex);
        refreshMixerView();
        gridEditor.fitToView();
        statusLog.append("Loaded composition: " + file.getFileName());
        addRecentPatternFile(file);
    }
    else
    {
        statusLog.append("Composition load failed: " + result.getErrorMessage());
    }

    if (wasPlaying)
        transportEngine.start();

    repaint();
}

void MainComponent::saveCompositionFile(juce::File file)
{
    if (file.getFileExtension().isEmpty())
        file = file.withFileExtension("gridcollider");

    storeActiveGridSlot();
    storeActiveTransitionCode();

    juce::var document;
    {
        const std::lock_guard lock(gridRuntimeMutex);
        document = serialiseComposition();
    }

    const auto json = juce::JSON::toString(document, true);

    if (file.replaceWithText(json))
    {
        currentCompositionFile = file;
        statusLog.append("Saved composition: " + file.getFileName());
        addRecentPatternFile(file);
    }
    else
    {
        statusLog.append("Composition save failed: " + file.getFullPathName());
    }

    repaint();
}

void MainComponent::showLoadPatternDialog()
{
    const auto start = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    fileChooser = std::make_unique<juce::FileChooser>("Load GridCollider pattern", start, "*.orca");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser)
                             {
                                 if (safeThis == nullptr)
                                     return;

                                 const auto file = chooser.getResult();

                                 if (file.existsAsFile())
                                     safeThis->loadPatternFile(file, true);
                             });
}

void MainComponent::showSavePatternDialog()
{
    auto start = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("gridcollider-pattern.orca");
    fileChooser = std::make_unique<juce::FileChooser>("Save GridCollider pattern", start, "*.orca");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser)
                             {
                                 if (safeThis == nullptr)
                                     return;

                                 auto file = chooser.getResult();

                                 if (file != juce::File())
                                     safeThis->savePatternFile(file);
                             });
}

void MainComponent::loadPatternFile(const juce::File& file, const bool addToRecent)
{
    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();
    transportEngine.reset();
    lastTransportFrame = 0;
    lastTickInBeat = 0;
    gridEditor.clearPlayhead();

    juce::Result result = juce::Result::ok();

    {
        const std::lock_guard lock(gridRuntimeMutex);
        result = presetManager.load(file, gridModel);
    }

    if (result.wasOk())
    {
        storeActiveGridSlot();
        updateGridSlotControls();
        gridEditor.fitToView();
        statusLog.append("Loaded pattern: " + file.getFileName());

        if (addToRecent)
            addRecentPatternFile(file);
    }
    else
    {
        statusLog.append("Pattern load failed: " + result.getErrorMessage());
    }

    if (wasPlaying)
        transportEngine.start();

    repaint();
}

void MainComponent::savePatternFile(juce::File file)
{
    if (file.getFileExtension().isEmpty())
        file = file.withFileExtension("orca");

    storeActiveGridSlot();
    juce::Result result = juce::Result::ok();

    {
        const std::lock_guard lock(gridRuntimeMutex);
        result = presetManager.save(file, gridModel);
    }

    if (result.wasOk())
    {
        statusLog.append("Saved pattern: " + file.getFileName());
        addRecentPatternFile(file);
    }
    else
    {
        statusLog.append("Pattern save failed: " + result.getErrorMessage());
    }

    repaint();
}

void MainComponent::addRecentPatternFile(const juce::File& file)
{
    for (int index = recentPatternFiles.size(); --index >= 0;)
    {
        if (recentPatternFiles[index] == file)
            recentPatternFiles.remove(index);
    }

    recentPatternFiles.insert(0, file);

    while (recentPatternFiles.size() > 8)
        recentPatternFiles.removeLast();
}

void MainComponent::showRecentPatternMenu()
{
    juce::PopupMenu menu;

    if (recentPatternFiles.isEmpty())
    {
        menu.addItem(1, "No recent patterns", false);
    }
    else
    {
        for (int index = 0; index < recentPatternFiles.size(); ++index)
            menu.addItem(index + 1, recentPatternFiles[index].getFileName());
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(recentPatternButton),
                       [this](const int result)
                       {
                           if (result <= 0 || result > recentPatternFiles.size())
                               return;

                           loadPatternFile(recentPatternFiles[result - 1], true);
                       });
}

void MainComponent::triggerEmbeddedScTest()
{
    if (! embeddedScAudio.isReady())
    {
        statusLog.append("Embedded SC not ready: " + embeddedScAudio.getLastError());
        repaint();
        return;
    }

    EventFields fields;
    fields.timestampSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    fields.tick = lastTransportFrame;
    fields.sourceCell = { 32, 16 };
    fields.instrumentName = "tone";
    fields.pitch = 60;
    fields.velocity = 0.9f;
    fields.durationTicks = 1;

    embeddedScAudio.setTransport(transportEngine.getBpm(), lastTransportFrame, transportEngine.isPlaying());
    embeddedScAudio.enqueue({ InternalEvent { NoteEvent { fields } } });
    statusLog.append("Embedded SC test note");
    repaint();
}

void MainComponent::configureTransitionCodePane()
{
    addAndMakeVisible(transitionCodeLabel);
    addAndMakeVisible(transitionCodeBackdrop);
    addAndMakeVisible(transitionCodeEditor);

    transitionCodeBackdrop.setLanguage(SourceCodeBackdropComponent::Language::supercollider);
    transitionCodeBackdrop.setSourceProvider([this] { return transitionCodeEditor.getText(); });

    transitionCodeLabel.setText("TRANSITIONS.SC", juce::dontSendNotification);
    transitionCodeLabel.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
    transitionCodeLabel.setJustificationType(juce::Justification::centredLeft);

    transitionCodeEditor.setMultiLine(true);
    transitionCodeEditor.setReturnKeyStartsNewLine(true);
    transitionCodeEditor.setScrollbarsShown(false);
    transitionCodeEditor.setCaretVisible(true);
    transitionCodeEditor.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    transitionCodeEditor.onTextChange = [this]
    {
        if (updatingTransitionCodeEditor)
            return;

        storeActiveTransitionCode();
        refreshStateGraph();
        transitionCodeBackdrop.repaint();
    };

    styleTransitionCodePane();
    showActiveTransitionCode();
}

void MainComponent::styleTransitionCodePane()
{
    transitionCodeLabel.setColour(juce::Label::textColourId, lewittInk());
    transitionCodeEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    transitionCodeEditor.setColour(juce::TextEditor::outlineColourId, lewittLine().withAlpha(0.65f));
    transitionCodeEditor.setColour(juce::TextEditor::focusedOutlineColourId, lewittInk());
    transitionCodeEditor.setColour(juce::TextEditor::highlightColourId, lewittRed().withAlpha(0.34f));
    forceBlackEditorText(transitionCodeEditor);
}

void MainComponent::storeActiveTransitionCode()
{
    const std::lock_guard lock(gridRuntimeMutex);

    if (compositionStates.empty())
        return;

    activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
    compositionStates[static_cast<std::size_t>(activeStateIndex)].transitionCode = transitionCodeEditor.getText();
}

void MainComponent::showActiveTransitionCode()
{
    juce::String code;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            code = createDefaultTransitionCode(1);
        else
        {
            const auto index = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
            auto& state = compositionStates[static_cast<std::size_t>(index)];

            if (state.transitionCode.isEmpty())
                state.transitionCode = createDefaultTransitionCode(index + 1);

            code = state.transitionCode;
        }
    }

    updatingTransitionCodeEditor = true;
    transitionCodeEditor.setText(code, juce::dontSendNotification);
    forceBlackEditorText(transitionCodeEditor);
    transitionCodeBackdrop.repaint();
    updatingTransitionCodeEditor = false;
}

juce::String MainComponent::createDefaultTransitionCode(const int stateNumber) const
{
    const auto nextState = stateNumber >= maximumCompositionStates ? 1 : stateNumber + 1;

    return "// TRANSITIONS.SC - State "
        + juce::String(stateNumber)
        + "\n// This state owns this rule. States are 1-based.\n"
          "// Use a linear edge, weighted edges, or leave maps empty.\n\n"
          "~linear = (\n"
          "    "
        + juce::String(stateNumber)
        + ": "
        + juce::String(nextState)
        + "\n"
          ");\n\n"
          "~weighted = (\n"
          "    // "
        + juce::String(stateNumber)
        + ": [\n"
          "    //     (to: "
        + juce::String(nextState)
        + ", chance: 0.70),\n"
          "    //     (to: "
        + juce::String(stateNumber)
        + ", chance: 0.30)\n"
          "    // ]\n"
          ");\n";
}

void MainComponent::configureEventMonitor()
{
    addAndMakeVisible(eventMonitorLabel);
    addAndMakeVisible(eventMonitor);

    eventMonitorLabel.setText("EVENTS", juce::dontSendNotification);
    eventMonitorLabel.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
    eventMonitorLabel.setJustificationType(juce::Justification::centredLeft);
    eventMonitorLabel.setColour(juce::Label::textColourId, lewittInk());

    eventMonitor.setReadOnly(true);
    eventMonitor.setMultiLine(true);
    eventMonitor.setScrollbarsShown(false);
    eventMonitor.setCaretVisible(false);
    eventMonitor.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    eventMonitor.setColour(juce::TextEditor::backgroundColourId, lewittPaper());
    eventMonitor.setColour(juce::TextEditor::outlineColourId, lewittLine());
    eventMonitor.setColour(juce::TextEditor::focusedOutlineColourId, lewittLine());
    eventMonitor.setColour(juce::TextEditor::textColourId, lewittInk());
    eventMonitor.setColour(juce::TextEditor::highlightColourId, lewittBlue().withAlpha(0.35f));
}

void MainComponent::appendEventMonitorLine(const LogEvent& event)
{
    eventMonitorLines.add("[" + juce::String(event.fields.tick).paddedLeft('0', 4) + "] " + event.message);

    while (eventMonitorLines.size() > 80)
        eventMonitorLines.remove(0);

    eventMonitorDirty = true;
}

void MainComponent::refreshEventMonitor()
{
    eventMonitorDirty = false;
    eventMonitor.setText(eventMonitorLines.joinIntoString("\n"), juce::dontSendNotification);
    eventMonitor.moveCaretToEnd(false);
}

void MainComponent::configureTransport()
{
    transportEngine.setBpm(120.0);
    transportEngine.setTicksPerBeat(4);

    transportEngine.setEvaluationCallback([this](const TransportEngine::TickContext& context)
    {
        return evaluateActiveState(context);
    });

    transportEngine.setTickCallback([safeThis = juce::Component::SafePointer<MainComponent>(this)](const TransportEngine::TickResult& result)
    {
        juce::MessageManager::callAsync([safeThis, result]
        {
            if (safeThis != nullptr)
                safeThis->handleTransportTick(result);
        });
    });
}

void MainComponent::configureTransportControls()
{
    addAndMakeVisible(playPauseButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(resetButton);
    addAndMakeVisible(bpmEditor);
    addAndMakeVisible(bpmLabel);

    playPauseButton.onClick = [this] { toggleTransportPlayback(); };
    stopButton.onClick = [this] { stopTransport(); };
    resetButton.onClick = [this] { resetTransport(); };

    bpmLabel.setText("BPM", juce::dontSendNotification);
    bpmEditor.setText(juce::String(transportEngine.getBpm(), 1), juce::dontSendNotification);
    forceBlackEditorText(bpmEditor);
    bpmEditor.onReturnKey = [this] { applyTransportEditors(); };
    bpmEditor.onFocusLost = [this] { applyTransportEditors(); };

    styleTransportControls();
    updateTransportControls();
}

void MainComponent::configureStateGraph()
{
    stateGraph.onStateSelected = [this](const int index) { switchToState(index); };
    stateGraph.onAddStateRequested = [this] { addCompositionState(); };

    addAndMakeVisible(stateSlotLabel);
    addAndMakeVisible(previousStateButton);
    addAndMakeVisible(nextStateButton);
    addAndMakeVisible(addStateButton);
    addAndMakeVisible(stateAdvanceLabel);
    addAndMakeVisible(stateAdvanceModeButton);
    addAndMakeVisible(stateAdvanceIntervalEditor);

    previousStateButton.setButtonText("<");
    nextStateButton.setButtonText(">");
    addStateButton.setButtonText("+");
    stateAdvanceLabel.setText("ADV", juce::dontSendNotification);

    previousStateButton.setTooltip("Previous state");
    nextStateButton.setTooltip("Next state");
    addStateButton.setTooltip("Add state");
    stateAdvanceModeButton.setTooltip("Selected state advance mode");
    stateAdvanceIntervalEditor.setTooltip("Selected state advance interval");

    previousStateButton.onClick = [this] { previousState(); };
    nextStateButton.onClick = [this] { nextState(); };
    addStateButton.onClick = [this] { addCompositionState(); };
    stateAdvanceModeButton.onClick = [this] { toggleSelectedStateAdvanceMode(); };
    stateAdvanceIntervalEditor.onReturnKey = [this] { applyStateAdvanceEditor(); };
    stateAdvanceIntervalEditor.onFocusLost = [this] { applyStateAdvanceEditor(); };

    refreshStateGraph();
}

void MainComponent::refreshStateGraph()
{
    std::vector<StateGraphComponent::StateView> views;
    std::vector<StateGraphComponent::TransitionView> transitions;
    std::vector<juce::String> stateTransitionCodes;

    {
        const std::lock_guard lock(gridRuntimeMutex);
        views.reserve(compositionStates.size());
        stateTransitionCodes.reserve(compositionStates.size());

        for (const auto& state : compositionStates)
        {
            views.push_back({ state.name,
                              static_cast<int>(state.grids.size()),
                              0,
                              state.bpm });
            stateTransitionCodes.push_back(state.transitionCode);
        }

        if (! views.empty())
            views[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(views.size()) - 1, activeStateIndex))].activeGrid = activeGridSlot;
    }

    const auto stateCount = static_cast<int>(views.size());

    for (int stateIndex = 0; stateIndex < static_cast<int>(stateTransitionCodes.size()); ++stateIndex)
    {
        const auto rules = parseTransitionRules(stateTransitionCodes[static_cast<std::size_t>(stateIndex)]);

        for (const auto& [fromState, toState] : rules.linear)
        {
            const auto fromIndex = fromState - 1;
            const auto toIndex = toState - 1;

            if (fromIndex >= 0 && fromIndex < stateCount && toIndex >= 0 && toIndex < stateCount)
                transitions.push_back({ fromIndex, toIndex, 1.0, false });
        }

        for (const auto& [fromState, choices] : rules.weighted)
        {
            const auto fromIndex = fromState - 1;

            if (fromIndex < 0 || fromIndex >= stateCount)
                continue;

            for (const auto& choice : choices)
            {
                const auto toIndex = choice.targetState - 1;

                if (toIndex >= 0 && toIndex < stateCount)
                    transitions.push_back({ fromIndex, toIndex, choice.chance, true });
            }
        }
    }

    stateGraph.setStates(std::move(views), activeStateIndex, transportEngine.isPlaying());
    stateGraph.setTransitions(std::move(transitions));
}

juce::String MainComponent::getStateAdvanceModeText(const CompositionState::AdvanceMode mode) const
{
    switch (mode)
    {
        case CompositionState::AdvanceMode::manual:  return "MANUAL";
        case CompositionState::AdvanceMode::beats:   return "BEATS";
        case CompositionState::AdvanceMode::bars:    return "BARS";
        case CompositionState::AdvanceMode::trigger: return "TRIGGER";
    }

    return "MANUAL";
}

void MainComponent::toggleSelectedStateAdvanceMode()
{
    CompositionState::AdvanceMode mode = CompositionState::AdvanceMode::manual;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            return;

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        switch (state.advanceMode)
        {
            case CompositionState::AdvanceMode::manual:  state.advanceMode = CompositionState::AdvanceMode::beats; break;
            case CompositionState::AdvanceMode::beats:   state.advanceMode = CompositionState::AdvanceMode::bars; break;
            case CompositionState::AdvanceMode::bars:    state.advanceMode = CompositionState::AdvanceMode::trigger; break;
            case CompositionState::AdvanceMode::trigger: state.advanceMode = CompositionState::AdvanceMode::manual; break;
        }

        mode = state.advanceMode;
        state.advanceInterval = juce::jlimit(1, 999, state.advanceInterval);
        activeStateEntryFrame = lastTransportFrame;
    }

    updateStateAdvanceControls();
    statusLog.append("State advance: " + getStateAdvanceModeText(mode));
    repaint();
}

void MainComponent::applyStateAdvanceEditor()
{
    const auto interval = juce::jlimit(1, 999, stateAdvanceIntervalEditor.getText().getIntValue());

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            return;

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        compositionStates[static_cast<std::size_t>(activeStateIndex)].advanceInterval = interval;
        activeStateEntryFrame = lastTransportFrame;
    }

    updateStateAdvanceControls();
    statusLog.append("State advance interval: " + juce::String(interval));
    repaint();
}

void MainComponent::updateStateAdvanceControls()
{
    CompositionState::AdvanceMode mode = CompositionState::AdvanceMode::manual;
    int interval = 4;
    bool hasState = false;

    {
        const std::lock_guard lock(gridRuntimeMutex);
        hasState = ! compositionStates.empty();

        if (hasState)
        {
            const auto index = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
            const auto& state = compositionStates[static_cast<std::size_t>(index)];
            mode = state.advanceMode;
            interval = state.advanceInterval;
        }
    }

    stateAdvanceModeButton.setEnabled(hasState);
    stateAdvanceModeButton.setButtonText(getStateAdvanceModeText(mode));
    stateAdvanceIntervalEditor.setEnabled(hasState
                                          && mode != CompositionState::AdvanceMode::manual
                                          && mode != CompositionState::AdvanceMode::trigger);
    stateAdvanceIntervalEditor.setText(juce::String(interval), juce::dontSendNotification);
    forceBlackEditorText(stateAdvanceIntervalEditor);
}

void MainComponent::styleTransportControls()
{
    const auto background = lewittPanel().brighter(0.08f);
    const auto text = lewittInk();
    const auto outline = lewittLine();

    for (auto* button : { &playPauseButton, &stopButton, &resetButton })
    {
        button->setColour(juce::TextButton::buttonColourId, background);
        button->setColour(juce::TextButton::buttonOnColourId, lewittBlue().withAlpha(0.35f));
        button->setColour(juce::TextButton::textColourOffId, text);
        button->setColour(juce::TextButton::textColourOnId, text);
        button->setColour(juce::ComboBox::outlineColourId, outline);
    }

    bpmEditor.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    bpmEditor.setJustification(juce::Justification::centred);
    bpmEditor.setColour(juce::TextEditor::backgroundColourId, background);
    bpmEditor.setColour(juce::TextEditor::outlineColourId, outline);
    bpmEditor.setColour(juce::TextEditor::focusedOutlineColourId, text);
    bpmEditor.setColour(juce::TextEditor::highlightColourId, lewittBlue().withAlpha(0.35f));
    forceBlackEditorText(bpmEditor);

    for (auto* label : { &bpmLabel })
    {
        label->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
        label->setJustificationType(juce::Justification::centredLeft);
        label->setColour(juce::Label::textColourId, text);
    }
}

void MainComponent::updateTransportControls()
{
    playPauseButton.setButtonText(transportEngine.isPlaying() ? "PAUSE" : "PLAY");
    stopButton.setButtonText("STOP");
    resetButton.setButtonText("RESET");
    bpmEditor.setText(juce::String(transportEngine.getBpm(), 1), juce::dontSendNotification);
    forceBlackEditorText(bpmEditor);
}

void MainComponent::configureGridSlotControls()
{
    addAndMakeVisible(gridSlotLabel);
    addAndMakeVisible(previousGridButton);
    addAndMakeVisible(nextGridButton);
    addAndMakeVisible(addGridButton);
    addAndMakeVisible(gridRatioLabel);
    addAndMakeVisible(gridRatioEditor);
    addAndMakeVisible(phaseModeButton);
    addAndMakeVisible(phaseOffsetEditor);
    addAndMakeVisible(laneKindButton);
    addAndMakeVisible(gridSizeLabel);
    addAndMakeVisible(gridColumnsEditor);
    addAndMakeVisible(gridSizeSeparatorLabel);
    addAndMakeVisible(gridRowsEditor);

    for (int index = 0; index < static_cast<int>(gridTabButtons.size()); ++index)
    {
        auto& button = gridTabButtons[static_cast<std::size_t>(index)];
        addAndMakeVisible(button);
        button.setButtonText(juce::String(index + 1).paddedLeft('0', 2));
        button.setTooltip("Select grid " + juce::String(index + 1));
        button.onClick = [this, index] { switchToGridSlot(index); };
    }

    previousGridButton.setButtonText("<");
    nextGridButton.setButtonText(">");
    addGridButton.setButtonText("+");
    gridRatioLabel.setText("1:", juce::dontSendNotification);
    gridSizeLabel.setText("SIZE", juce::dontSendNotification);
    gridSizeSeparatorLabel.setText("x", juce::dontSendNotification);

    previousGridButton.setTooltip("Previous grid");
    nextGridButton.setTooltip("Next grid");
    addGridButton.setTooltip("Add grid");
    gridRatioEditor.setTooltip("Selected grid tempo ratio denominator");
    phaseModeButton.setTooltip("Selected lane phase mode");
    phaseOffsetEditor.setTooltip("Selected lane phase offset, 0-360 degrees");
    laneKindButton.setTooltip("Swap selected lane between grid and SuperCollider code");
    gridColumnsEditor.setTooltip("Selected grid columns, 1-64");
    gridRowsEditor.setTooltip("Selected grid rows, 1-32");

    previousGridButton.onClick = [this] { previousGridSlot(); };
    nextGridButton.onClick = [this] { nextGridSlot(); };
    addGridButton.onClick = [this] { addGridSlot(); };
    gridRatioEditor.onReturnKey = [this] { applyGridTimingEditors(); };
    gridRatioEditor.onFocusLost = [this] { applyGridTimingEditors(); };
    phaseModeButton.onClick = [this] { toggleSelectedGridPhaseMode(); };
    phaseOffsetEditor.onReturnKey = [this] { applyGridTimingEditors(); };
    phaseOffsetEditor.onFocusLost = [this] { applyGridTimingEditors(); };
    laneKindButton.onClick = [this] { toggleSelectedLaneKind(); };
    gridColumnsEditor.onReturnKey = [this] { applyGridSizeEditors(); };
    gridRowsEditor.onReturnKey = [this] { applyGridSizeEditors(); };
    gridColumnsEditor.onFocusLost = [this] { applyGridSizeEditors(); };
    gridRowsEditor.onFocusLost = [this] { applyGridSizeEditors(); };

    styleGridSlotControls();
    updateGridSlotControls();
}

void MainComponent::configureMixerView()
{
    addAndMakeVisible(mixerViewport);
    mixerViewport.setViewedComponent(&mixerContent, false);
    mixerViewport.setScrollBarsShown(false, true);
    mixerViewport.setVisible(false);

    mixerContent.addAndMakeVisible(mixerLabel);
    mixerLabel.setText("MIXER", juce::dontSendNotification);
    mixerLabel.setVisible(false);

    for (int index = 0; index < maximumMixerChannels; ++index)
    {
        auto& label = mixerChannelLabels[static_cast<std::size_t>(index)];
        auto& level = mixerLevelSliders[static_cast<std::size_t>(index)];
        auto& pan = mixerPanSliders[static_cast<std::size_t>(index)];

        mixerContent.addAndMakeVisible(label);
        mixerContent.addAndMakeVisible(level);
        mixerContent.addAndMakeVisible(pan);

        label.setJustificationType(juce::Justification::centred);
        level.setSliderStyle(juce::Slider::LinearVertical);
        level.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 20);
        level.setRange(0.0, 1.25, 0.01);
        level.setValue(1.0, juce::dontSendNotification);

        pan.setSliderStyle(juce::Slider::LinearHorizontal);
        pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange(-1.0, 1.0, 0.01);
        pan.setValue(0.0, juce::dontSendNotification);

        level.onValueChange = [this, index]
        {
            if (index == masterMixerControlIndex)
                applyMasterLevel();
            else
                applyMixerControl(index / maximumGridsPerState, index % maximumGridsPerState, index);
        };

        pan.onValueChange = [this, index]
        {
            if (index != masterMixerControlIndex)
                applyMixerControl(index / maximumGridsPerState, index % maximumGridsPerState, index);
        };
    }

    styleMixerControls();
    refreshMixerView();
}

void MainComponent::styleMixerControls()
{
    const auto mixerInk = lewittInk();
    const auto mixerMuted = lewittLine().withAlpha(0.55f);
    const auto mixerTrack = lewittInk().withAlpha(0.18f);

    mixerLabel.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
    mixerLabel.setColour(juce::Label::textColourId, mixerInk);

    for (int index = 0; index < maximumMixerChannels; ++index)
    {
        auto& label = mixerChannelLabels[static_cast<std::size_t>(index)];
        auto& level = mixerLevelSliders[static_cast<std::size_t>(index)];
        auto& pan = mixerPanSliders[static_cast<std::size_t>(index)];

        label.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, mixerInk);
        level.setColour(juce::Slider::backgroundColourId, mixerTrack);
        level.setColour(juce::Slider::trackColourId, mixerInk.withAlpha(0.74f));
        level.setColour(juce::Slider::thumbColourId, mixerInk);
        level.setColour(juce::Slider::textBoxTextColourId, mixerInk);
        level.setColour(juce::Slider::textBoxBackgroundColourId, lewittPanel().brighter(0.06f));
        level.setColour(juce::Slider::textBoxOutlineColourId, mixerMuted);
        pan.setColour(juce::Slider::backgroundColourId, mixerTrack);
        pan.setColour(juce::Slider::trackColourId, lewittYellow().withAlpha(0.58f));
        pan.setColour(juce::Slider::thumbColourId, lewittYellow());
    }
}

void MainComponent::toggleMixerView()
{
    mixerViewVisible = ! mixerViewVisible;

    if (! mixerViewVisible)
        showActiveLane();

    refreshMixerView();
    resized();
    repaint();
}

void MainComponent::refreshMixerView()
{
    struct Channel
    {
        int state = -1;
        int lane = -1;
        juce::String label;
        float level = 1.0f;
        float pan = 0.0f;
        bool master = false;
    };

    std::vector<Channel> channels;
    {
        const std::lock_guard lock(gridRuntimeMutex);

        for (int stateIndex = 0; stateIndex < static_cast<int>(compositionStates.size()); ++stateIndex)
        {
            const auto& state = compositionStates[static_cast<std::size_t>(stateIndex)];

            for (int laneIndex = 0; laneIndex < static_cast<int>(state.grids.size()); ++laneIndex)
            {
                const auto& lane = state.grids[static_cast<std::size_t>(laneIndex)];
                channels.push_back({ stateIndex,
                                     laneIndex,
                                     "S" + juce::String(stateIndex + 1).paddedLeft('0', 2)
                                         + " L" + juce::String(laneIndex + 1).paddedLeft('0', 2)
                                         + " " + (lane.kind == CompositionGrid::Kind::grid ? "G" : "SC"),
                                     lane.mixerLevel,
                                     lane.mixerPan,
                                     false });
            }
        }
    }

    channels.push_back({ -1, -1, "MASTER", masterLevel, 0.0f, true });
    masterMixerControlIndex = static_cast<int>(channels.size()) - 1;

    const auto channelCount = juce::jmax(1, static_cast<int>(channels.size()));
    const auto availableWidth = juce::jmax(1, mixerViewport.getWidth() - 32);
    const auto stripWidth = juce::jlimit(92, 126, availableWidth / channelCount);
    const auto contentHeight = juce::jmax(420, mixerViewport.getHeight());
    const auto contentWidth = juce::jmax(32 + channelCount * stripWidth, mixerViewport.getWidth());
    mixerContent.setBounds(0, 0, contentWidth, contentHeight);
    mixerContent.repaint();
    mixerLabel.setBounds({});

    for (int index = 0; index < maximumMixerChannels; ++index)
    {
        const auto visible = index < static_cast<int>(channels.size());
        auto& label = mixerChannelLabels[static_cast<std::size_t>(index)];
        auto& level = mixerLevelSliders[static_cast<std::size_t>(index)];
        auto& pan = mixerPanSliders[static_cast<std::size_t>(index)];

        label.setVisible(visible);
        level.setVisible(visible);
        pan.setVisible(visible && ! channels[static_cast<std::size_t>(index)].master);

        if (! visible)
            continue;

        const auto& channel = channels[static_cast<std::size_t>(index)];
        const auto x = 16 + index * stripWidth;
        const auto faderTop = 58;
        const auto faderBottomGap = channel.master ? 42 : 76;
        const auto faderHeight = juce::jmax(220, contentHeight - faderTop - faderBottomGap);
        label.setText(channel.label, juce::dontSendNotification);
        label.setBounds(x, 18, stripWidth - 16, 24);
        level.setBounds(x + (stripWidth - 72) / 2, faderTop, 72, faderHeight);
        pan.setBounds(x + 12, contentHeight - 44, stripWidth - 40, 20);
        level.setValue(channel.level, juce::dontSendNotification);
        pan.setValue(channel.pan, juce::dontSendNotification);

        if (channel.master)
        {
            level.onValueChange = [this] { applyMasterLevel(); };
        }
        else
        {
            level.onValueChange = [this, stateIndex = channel.state, laneIndex = channel.lane, index]
            {
                applyMixerControl(stateIndex, laneIndex, index);
            };
            pan.onValueChange = [this, stateIndex = channel.state, laneIndex = channel.lane, index]
            {
                applyMixerControl(stateIndex, laneIndex, index);
            };
        }
    }
}

void MainComponent::applyMixerControl(const int stateIndex, const int laneIndex, const int mixerControlIndex)
{
    if (stateIndex < 0 || laneIndex < 0)
        return;

    if (mixerControlIndex < 0 || mixerControlIndex >= maximumMixerChannels - 1)
        return;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (stateIndex >= static_cast<int>(compositionStates.size()))
            return;

        auto& state = compositionStates[static_cast<std::size_t>(stateIndex)];

        if (laneIndex >= static_cast<int>(state.grids.size()))
            return;

        auto& lane = state.grids[static_cast<std::size_t>(laneIndex)];
        lane.mixerLevel = static_cast<float>(mixerLevelSliders[static_cast<std::size_t>(mixerControlIndex)].getValue());
        lane.mixerPan = static_cast<float>(mixerPanSliders[static_cast<std::size_t>(mixerControlIndex)].getValue());
    }
}

void MainComponent::applyMasterLevel()
{
    const auto masterIndex = juce::jlimit(0, maximumMixerChannels - 1, masterMixerControlIndex);
    masterLevel = static_cast<float>(mixerLevelSliders[static_cast<std::size_t>(masterIndex)].getValue());
    embeddedScAudio.setMasterLevel(masterLevel);
}

void MainComponent::applyLaneMixToEvents(std::vector<InternalEvent>& events, const CompositionGrid& lane) const
{
    for (auto& event : events)
    {
        std::visit([&lane](auto& typed)
        {
            typed.fields.velocity = juce::jlimit(0.0f, 1.0f, typed.fields.velocity * lane.mixerLevel);
            typed.fields.parameters["pan"] = juce::String(lane.mixerPan, 3);
        }, event);
    }
}

void MainComponent::configureLaneCodePane()
{
    addAndMakeVisible(laneCodeBackdrop);
    addAndMakeVisible(laneScCodeEditor);
    laneCodeBackdrop.setLanguage(SourceCodeBackdropComponent::Language::supercollider);
    laneCodeBackdrop.setSourceProvider([this] { return laneScCodeEditor.getText(); });
    laneScCodeEditor.setMultiLine(true);
    laneScCodeEditor.setReturnKeyStartsNewLine(true);
    laneScCodeEditor.setScrollbarsShown(false);
    laneScCodeEditor.setCaretVisible(true);
    laneScCodeEditor.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    laneScCodeEditor.onTextChange = [this]
    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            return;

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            return;

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        auto& lane = state.grids[static_cast<std::size_t>(activeGridSlot)];

        if (lane.kind == CompositionGrid::Kind::supercollider)
        {
            lane.scCode = laneScCodeEditor.getText();
            lane.scCodeDirty = true;
            laneCodeBackdrop.repaint();
        }
    };
    laneScCodeEditor.onFocusLost = [this] { compileSelectedScLane(); };
    styleLaneCodePane();
    laneCodeBackdrop.setVisible(false);
    laneScCodeEditor.setVisible(false);
}

void MainComponent::styleLaneCodePane()
{
    laneScCodeEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    laneScCodeEditor.setColour(juce::TextEditor::outlineColourId, lewittLine().withAlpha(0.65f));
    laneScCodeEditor.setColour(juce::TextEditor::focusedOutlineColourId, lewittInk());
    laneScCodeEditor.setColour(juce::TextEditor::highlightColourId, lewittBlue().withAlpha(0.34f));
    forceBlackEditorText(laneScCodeEditor);
}

void MainComponent::styleGridSlotControls()
{
    const auto background = lewittPanel().brighter(0.08f);
    const auto text = lewittInk();
    const auto outline = lewittLine();

    for (auto* label : { &stateSlotLabel, &stateAdvanceLabel, &gridSlotLabel, &gridRatioLabel, &gridSizeLabel, &gridSizeSeparatorLabel })
    {
        label->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
        label->setJustificationType(juce::Justification::centredRight);
        label->setColour(juce::Label::textColourId, text);
    }

    for (auto* button : { &previousStateButton, &nextStateButton, &addStateButton,
                          &stateAdvanceModeButton, &previousGridButton, &nextGridButton, &addGridButton, &phaseModeButton, &laneKindButton })
    {
        button->setColour(juce::TextButton::buttonColourId, background);
        button->setColour(juce::TextButton::buttonOnColourId, lewittBlue().withAlpha(0.35f));
        button->setColour(juce::TextButton::textColourOffId, text);
        button->setColour(juce::TextButton::textColourOnId, text);
        button->setColour(juce::ComboBox::outlineColourId, outline);
    }

    for (auto& button : gridTabButtons)
    {
        button.setColour(juce::TextButton::buttonColourId, lewittPanel().withAlpha(0.70f));
        button.setColour(juce::TextButton::buttonOnColourId, lewittInk().withAlpha(0.14f));
        button.setColour(juce::TextButton::textColourOffId, text);
        button.setColour(juce::TextButton::textColourOnId, text);
        button.setColour(juce::ComboBox::outlineColourId, outline);
    }

    for (auto* editor : { &gridRatioEditor, &phaseOffsetEditor })
    {
        editor->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        editor->setJustification(juce::Justification::centred);
        editor->setColour(juce::TextEditor::backgroundColourId, background);
        editor->setColour(juce::TextEditor::outlineColourId, outline);
        editor->setColour(juce::TextEditor::focusedOutlineColourId, lewittYellow());
        editor->setColour(juce::TextEditor::highlightColourId, lewittBlue().withAlpha(0.35f));
        forceBlackEditorText(*editor);
    }

    stateAdvanceIntervalEditor.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    stateAdvanceIntervalEditor.setJustification(juce::Justification::centred);
    stateAdvanceIntervalEditor.setColour(juce::TextEditor::backgroundColourId, background);
    stateAdvanceIntervalEditor.setColour(juce::TextEditor::outlineColourId, outline);
    stateAdvanceIntervalEditor.setColour(juce::TextEditor::focusedOutlineColourId, lewittRed());
    stateAdvanceIntervalEditor.setColour(juce::TextEditor::highlightColourId, lewittBlue().withAlpha(0.35f));
    forceBlackEditorText(stateAdvanceIntervalEditor);

    for (auto* editor : { &gridColumnsEditor, &gridRowsEditor })
    {
        editor->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        editor->setJustification(juce::Justification::centred);
        editor->setColour(juce::TextEditor::backgroundColourId, background);
        editor->setColour(juce::TextEditor::outlineColourId, outline);
        editor->setColour(juce::TextEditor::focusedOutlineColourId, lewittRed());
        editor->setColour(juce::TextEditor::textColourId, text);
        editor->setColour(juce::TextEditor::highlightColourId, lewittBlue().withAlpha(0.35f));
    }
}

void MainComponent::updateGridSlotControls()
{
    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
        {
            stateSlotLabel.setText("STATE 0/0", juce::dontSendNotification);
            gridSlotLabel.setText("GRID 0/0", juce::dontSendNotification);
            previousStateButton.setEnabled(false);
            nextStateButton.setEnabled(false);
            addStateButton.setEnabled(true);
            stateAdvanceModeButton.setEnabled(false);
            stateAdvanceIntervalEditor.setEnabled(false);
            stateAdvanceModeButton.setButtonText("MANUAL");
            stateAdvanceIntervalEditor.setText("4", juce::dontSendNotification);
            forceBlackEditorText(stateAdvanceIntervalEditor);
            previousGridButton.setEnabled(false);
            nextGridButton.setEnabled(false);
            addGridButton.setEnabled(false);
            laneKindButton.setEnabled(false);
            gridRatioEditor.setText("1.0", juce::dontSendNotification);
            phaseOffsetEditor.setText("0", juce::dontSendNotification);
            gridColumnsEditor.setText(juce::String(GridModel::defaultWidth), juce::dontSendNotification);
            gridRowsEditor.setText(juce::String(GridModel::defaultHeight), juce::dontSendNotification);
            phaseModeButton.setButtonText("SYNC");

            for (auto& button : gridTabButtons)
            {
                button.setVisible(false);
                button.setToggleState(false, juce::dontSendNotification);
            }

            return;
        }

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        const auto& grid = state.grids[static_cast<std::size_t>(activeGridSlot)];

        stateSlotLabel.setText("STATE " + juce::String(activeStateIndex + 1) + "/" + juce::String(static_cast<int>(compositionStates.size())),
                               juce::dontSendNotification);
        const auto laneIsGrid = grid.kind == CompositionGrid::Kind::grid;
        gridSlotLabel.setText("LANE " + juce::String(activeGridSlot + 1) + "/" + juce::String(static_cast<int>(state.grids.size())),
                              juce::dontSendNotification);
        gridRatioEditor.setText(juce::String(grid.tempoRatio, 1), juce::dontSendNotification);
        phaseOffsetEditor.setText(juce::String(grid.phaseOffsetDegrees, 0), juce::dontSendNotification);
        forceBlackEditorText(gridRatioEditor);
        forceBlackEditorText(phaseOffsetEditor);
        gridColumnsEditor.setText(juce::String(grid.snapshot.width), juce::dontSendNotification);
        gridRowsEditor.setText(juce::String(grid.snapshot.height), juce::dontSendNotification);
        laneKindButton.setButtonText(laneIsGrid ? "GRID" : "SC");
        stateAdvanceModeButton.setButtonText(getStateAdvanceModeText(state.advanceMode));
        stateAdvanceIntervalEditor.setText(juce::String(state.advanceInterval), juce::dontSendNotification);
        forceBlackEditorText(stateAdvanceIntervalEditor);
        phaseModeButton.setButtonText(grid.phaseOffsetEnabled ? "OFFSET" : "SYNC");
        phaseModeButton.setToggleState(grid.phaseOffsetEnabled, juce::dontSendNotification);

        const auto canSwitchState = compositionStates.size() > 1;
        const auto canSwitch = state.grids.size() > 1;
        previousStateButton.setEnabled(canSwitchState);
        nextStateButton.setEnabled(canSwitchState);
        addStateButton.setEnabled(compositionStates.size() < maximumCompositionStates);
        stateAdvanceModeButton.setEnabled(true);
        stateAdvanceIntervalEditor.setEnabled(state.advanceMode != CompositionState::AdvanceMode::manual
                                              && state.advanceMode != CompositionState::AdvanceMode::trigger);
        laneKindButton.setEnabled(true);
        gridRatioEditor.setEnabled(laneIsGrid);
        phaseModeButton.setEnabled(laneIsGrid);
        phaseOffsetEditor.setEnabled(laneIsGrid && grid.phaseOffsetEnabled);
        gridColumnsEditor.setEnabled(laneIsGrid);
        gridRowsEditor.setEnabled(laneIsGrid);
        previousGridButton.setEnabled(canSwitch);
        nextGridButton.setEnabled(canSwitch);
        addGridButton.setEnabled(state.grids.size() < maximumGridsPerState);

        for (int index = 0; index < static_cast<int>(gridTabButtons.size()); ++index)
        {
            auto& button = gridTabButtons[static_cast<std::size_t>(index)];
            const auto tabVisible = index < static_cast<int>(state.grids.size());
            button.setVisible(tabVisible);
            button.setEnabled(tabVisible);
            button.setToggleState(tabVisible && index == activeGridSlot, juce::dontSendNotification);

            if (tabVisible)
            {
                const auto& tabGrid = state.grids[static_cast<std::size_t>(index)];
                const auto kind = tabGrid.kind == CompositionGrid::Kind::grid ? "G" : "SC";
                button.setButtonText(juce::String(index + 1).paddedLeft('0', 2) + " " + kind);
            }
        }
    }

    showActiveLane();
    refreshStateGraph();
    refreshMixerView();
    resized();
}

void MainComponent::toggleSelectedLaneKind()
{
    storeActiveGridSlot();
    bool isGrid = true;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            return;

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        auto& lane = state.grids[static_cast<std::size_t>(activeGridSlot)];

        if (lane.kind == CompositionGrid::Kind::grid)
        {
            lane.kind = CompositionGrid::Kind::supercollider;

            if (lane.scCode.isEmpty())
                lane.scCode = createDefaultScLaneCode(activeStateIndex + 1, activeGridSlot + 1);

            lane.scCodeDirty = true;
            isGrid = false;
        }
        else
        {
            lane.kind = CompositionGrid::Kind::grid;
            isGrid = true;
        }
    }

    updateGridSlotControls();
    showActiveLane();
    if (! isGrid)
        compileSelectedScLane();
    statusLog.append(isGrid ? "Lane set to grid" : "Lane set to SuperCollider");
    repaint();
}

void MainComponent::applyGridSizeEditors()
{
    const auto columns = juce::jlimit(1, maximumGridColumns, gridColumnsEditor.getText().getIntValue());
    const auto rows = juce::jlimit(1, maximumGridRows, gridRowsEditor.getText().getIntValue());
    bool changed = false;

    storeActiveGridSlot();

    {
        const std::lock_guard lock(gridRuntimeMutex);
        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        auto& grid = state.grids[static_cast<std::size_t>(activeGridSlot)];

        changed = grid.snapshot.width != columns || grid.snapshot.height != rows;
        grid.snapshot = resizeSnapshot(std::move(grid.snapshot), columns, rows);
        grid.lastEvaluatedFrame = std::numeric_limits<std::uint64_t>::max();
        gridModel.applySnapshot(grid.snapshot);
    }

    updateGridSizeControls();
    gridEditor.clearPlayhead();
    gridEditor.fitToView();
    gridEditor.repaint();

    if (changed)
        statusLog.append("Grid size set to " + juce::String(columns) + "x" + juce::String(rows));

    repaint();
}

void MainComponent::updateGridSizeControls()
{
    gridColumnsEditor.setText(juce::String(juce::jlimit(1, maximumGridColumns, gridModel.getWidth())),
                              juce::dontSendNotification);
    gridRowsEditor.setText(juce::String(juce::jlimit(1, maximumGridRows, gridModel.getHeight())),
                           juce::dontSendNotification);
}

void MainComponent::storeActiveGridSlot()
{
    const std::lock_guard lock(gridRuntimeMutex);
    storeActiveGridSlotLocked();
}

void MainComponent::storeActiveLane()
{
    storeActiveGridSlot();
}

void MainComponent::storeActiveLaneLocked()
{
    storeActiveGridSlotLocked();
}

void MainComponent::storeActiveGridSlotLocked()
{
    if (compositionStates.empty())
    {
        CompositionState state;
        state.name = "State 01";
        state.transitionCode = createDefaultTransitionCode(1);
        state.grids.push_back({ gridModel.createSnapshot() });
        compositionStates.push_back(std::move(state));
        activeStateIndex = 0;
        activeGridSlot = 0;
        return;
    }

    activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
    auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

    if (state.grids.empty())
        state.grids.push_back({ makeEmptyGridSnapshot() });

    activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
    auto& lane = state.grids[static_cast<std::size_t>(activeGridSlot)];

    if (lane.kind == CompositionGrid::Kind::grid)
        lane.snapshot = gridModel.createSnapshot();
    else if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        lane.scCode = laneScCodeEditor.getText();
        lane.scCodeDirty = true;
    }
}

GridModel::Snapshot MainComponent::makeEmptyGridSnapshot(const int columns, const int rows) const
{
    GridModel::Snapshot snapshot;
    snapshot.width = juce::jlimit(1, maximumGridColumns, columns);
    snapshot.height = juce::jlimit(1, maximumGridRows, rows);
    snapshot.cells.assign(static_cast<std::size_t>(snapshot.width * snapshot.height), GridModel::emptyGlyph);
    return snapshot;
}

void MainComponent::showActiveLane()
{
    bool showGrid = true;
    bool shouldCompile = false;
    juce::String code;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (! compositionStates.empty())
        {
            activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
            auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

            if (state.grids.empty())
                state.grids.push_back({ makeEmptyGridSnapshot() });

            activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
            auto& lane = state.grids[static_cast<std::size_t>(activeGridSlot)];
            showGrid = lane.kind == CompositionGrid::Kind::grid;

            if (showGrid)
                gridModel.applySnapshot(lane.snapshot);
            else
            {
                if (lane.scCode.isEmpty())
                    lane.scCode = createDefaultScLaneCode(activeStateIndex + 1, activeGridSlot + 1);

                code = lane.scCode;
                shouldCompile = lane.scCodeDirty;
            }
        }
    }

    gridEditor.setVisible(showGrid);
    laneCodeBackdrop.setVisible(! showGrid);
    laneScCodeEditor.setVisible(! showGrid);
    laneKindButton.setButtonText(showGrid ? "GRID" : "SC");

    if (showGrid)
    {
        gridEditor.fitToView();
        gridEditor.repaint();
    }
    else
    {
        laneScCodeEditor.setText(code, juce::dontSendNotification);
        forceBlackEditorText(laneScCodeEditor);
        laneCodeBackdrop.repaint();

        if (shouldCompile)
            compileSelectedScLane();
    }
}

juce::String MainComponent::createDefaultScLaneCode(const int stateNumber, const int laneNumber) const
{
    const auto name = "gc_s" + juce::String(stateNumber) + "_l" + juce::String(laneNumber);

    return "SynthDef(\\"
        + name
        + R"SC(, { |out = 0, pitch = 60, amp = 0.25, sustain = 0.35, pan = 0|
    var freq = pitch.midicps;
    var env = EnvGen.kr(Env.perc(0.004, sustain), doneAction: 2);
    var sig = SinOsc.ar(freq) * env * amp;
    Out.ar(out, Pan2.ar(sig, pan));
});
)SC";
}

juce::String MainComponent::getSynthDefNameFromSource(const juce::String& source, const int stateNumber, const int laneNumber) const
{
    std::smatch match;
    const auto text = source.toStdString();

    if (std::regex_search(text, match, std::regex(R"(SynthDef\s*\(\s*\\([A-Za-z_][A-Za-z0-9_]*))")))
        return juce::String(match[1].str());

    return "gc_s" + juce::String(stateNumber) + "_l" + juce::String(laneNumber);
}

void MainComponent::compileSelectedScLane()
{
    juce::String code;
    juce::String name;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            return;

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            return;

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        auto& lane = state.grids[static_cast<std::size_t>(activeGridSlot)];

        if (lane.kind != CompositionGrid::Kind::supercollider)
            return;

        lane.scCode = laneScCodeEditor.getText();
        lane.scSynthName = getSynthDefNameFromSource(lane.scCode, activeStateIndex + 1, activeGridSlot + 1);
        code = lane.scCode;
        name = lane.scSynthName;
        lane.scCodeDirty = false;
    }

    if (embeddedScAudio.loadSynthDef(name, code))
        statusLog.append("Loaded SynthDef: " + name);
    else
        statusLog.append("SynthDef load failed: " + embeddedScAudio.getLastError());

    repaint();
}

void MainComponent::compileScLanesForState(const int stateIndex)
{
    struct PendingSynth
    {
        juce::String name;
        juce::String code;
    };

    std::vector<PendingSynth> pending;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (stateIndex < 0 || stateIndex >= static_cast<int>(compositionStates.size()))
            return;

        auto& state = compositionStates[static_cast<std::size_t>(stateIndex)];

        for (int laneIndex = 0; laneIndex < static_cast<int>(state.grids.size()); ++laneIndex)
        {
            auto& lane = state.grids[static_cast<std::size_t>(laneIndex)];

            if (lane.kind != CompositionGrid::Kind::supercollider)
                continue;

            if (lane.scCode.isEmpty())
                lane.scCode = createDefaultScLaneCode(stateIndex + 1, laneIndex + 1);

            lane.scSynthName = getSynthDefNameFromSource(lane.scCode, stateIndex + 1, laneIndex + 1);
            lane.scCodeDirty = false;
            pending.push_back({ lane.scSynthName, lane.scCode });
        }
    }

    for (const auto& synth : pending)
    {
        if (! embeddedScAudio.loadSynthDef(synth.name, synth.code))
            statusLog.append("SynthDef load failed: " + embeddedScAudio.getLastError());
    }
}

void MainComponent::switchToState(const int stateIndex)
{
    if (stateIndex < 0 || stateIndex >= static_cast<int>(compositionStates.size()))
    {
        statusLog.append("No state " + juce::String(stateIndex + 1));
        repaint();
        return;
    }

    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();
    storeActiveTransitionCode();

    double stateBpm = 120.0;
    int gridCount = 1;

    {
        const std::lock_guard lock(gridRuntimeMutex);
        activeStateIndex = stateIndex;
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        gridCount = static_cast<int>(state.grids.size());
        stateBpm = state.bpm;
        gridModel.applySnapshot(state.grids[static_cast<std::size_t>(activeGridSlot)].snapshot);
    }

    transportEngine.setBpm(stateBpm);
    transportEngine.reset();
    lastTransportFrame = 0;
    lastTickInBeat = 0;
    gridEditor.clearPlayhead();
    gridEditor.fitToView();
    gridEditor.repaint();
    resetGridRuntimeClocks();

    if (wasPlaying)
        transportEngine.start();

    updateTransportControls();
    updateGridSlotControls();
    updateStateAdvanceControls();
    showActiveTransitionCode();
    compileScLanesForState(activeStateIndex);
    statusLog.append("Selected state " + juce::String(activeStateIndex + 1) + " with " + juce::String(gridCount) + " grids");
    repaint();
}

void MainComponent::previousState()
{
    if (compositionStates.size() <= 1)
    {
        statusLog.append("Only one state");
        repaint();
        return;
    }

    switchToState((activeStateIndex + static_cast<int>(compositionStates.size()) - 1) % static_cast<int>(compositionStates.size()));
}

void MainComponent::nextState()
{
    if (compositionStates.size() <= 1)
    {
        statusLog.append("Only one state");
        repaint();
        return;
    }

    switchToState((activeStateIndex + 1) % static_cast<int>(compositionStates.size()));
}

void MainComponent::addCompositionState()
{
    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.size() >= maximumCompositionStates)
        {
            statusLog.append("Composition already has 16 states");

            if (wasPlaying)
                transportEngine.start();

            repaint();
            return;
        }

        CompositionGrid grid;
        grid.snapshot = makeEmptyGridSnapshot();

        CompositionState state;
        state.name = "State " + juce::String(static_cast<int>(compositionStates.size()) + 1).paddedLeft('0', 2);
        state.bpm = transportEngine.getBpm();
        state.transitionCode = createDefaultTransitionCode(static_cast<int>(compositionStates.size()) + 1);
        state.grids.push_back(std::move(grid));

        compositionStates.push_back(std::move(state));
        activeStateIndex = static_cast<int>(compositionStates.size()) - 1;
        activeGridSlot = 0;
        gridModel.applySnapshot(compositionStates.back().grids.front().snapshot);
    }

    transportEngine.reset();
    lastTransportFrame = 0;
    lastTickInBeat = 0;
    activeStateEntryFrame = 0;
    gridEditor.clearPlayhead();
    gridEditor.fitToView();
    gridEditor.repaint();
    resetGridRuntimeClocks();

    if (wasPlaying)
        transportEngine.start();

    updateGridSlotControls();
    updateStateAdvanceControls();
    showActiveTransitionCode();
    statusLog.append("Added state " + juce::String(activeStateIndex + 1));
    repaint();
}

void MainComponent::copySelectedState()
{
    storeActiveGridSlot();
    storeActiveTransitionCode();

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            return;

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        copiedState = compositionStates[static_cast<std::size_t>(activeStateIndex)];
    }

    statusLog.append("Copied state " + juce::String(activeStateIndex + 1));
    repaint();
}

void MainComponent::pasteCopiedState()
{
    if (! copiedState.has_value())
    {
        statusLog.append("No copied state");
        repaint();
        return;
    }

    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();
    storeActiveTransitionCode();

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.size() >= maximumCompositionStates)
        {
            statusLog.append("Composition already has 16 states");

            if (wasPlaying)
                transportEngine.start();

            repaint();
            return;
        }

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto pasted = *copiedState;
        pasted.name = "State " + juce::String(static_cast<int>(compositionStates.size()) + 1).paddedLeft('0', 2);

        if (pasted.grids.empty())
            pasted.grids.push_back({ makeEmptyGridSnapshot() });

        const auto insertIndex = activeStateIndex + 1;
        compositionStates.insert(compositionStates.begin() + insertIndex, std::move(pasted));
        activeStateIndex = insertIndex;
        activeGridSlot = 0;
        activeStateEntryFrame = lastTransportFrame;
        gridModel.applySnapshot(compositionStates[static_cast<std::size_t>(activeStateIndex)].grids.front().snapshot);
    }

    resetGridRuntimeClocks();
    gridEditor.clearPlayhead();
    gridEditor.fitToView();

    if (wasPlaying)
        transportEngine.start();

    updateTransportControls();
    updateGridSlotControls();
    updateStateAdvanceControls();
    showActiveTransitionCode();
    refreshStateGraph();
    statusLog.append("Pasted state " + juce::String(activeStateIndex + 1));
    repaint();
}

void MainComponent::deleteSelectedState()
{
    if (compositionStates.size() <= 1)
    {
        statusLog.append("Cannot delete the only state");
        repaint();
        return;
    }

    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();
    storeActiveTransitionCode();

    int deletedState = activeStateIndex + 1;
    double stateBpm = transportEngine.getBpm();

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.size() <= 1)
            return;

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        deletedState = activeStateIndex + 1;
        compositionStates.erase(compositionStates.begin() + activeStateIndex);
        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);

        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        activeStateEntryFrame = lastTransportFrame;
        stateBpm = state.bpm;
        gridModel.applySnapshot(state.grids[static_cast<std::size_t>(activeGridSlot)].snapshot);
    }

    transportEngine.setBpm(stateBpm);
    resetGridRuntimeClocks();
    gridEditor.clearPlayhead();
    gridEditor.fitToView();

    if (wasPlaying)
        transportEngine.start();

    updateTransportControls();
    updateGridSlotControls();
    updateStateAdvanceControls();
    showActiveTransitionCode();
    refreshStateGraph();
    statusLog.append("Deleted state " + juce::String(deletedState));
    repaint();
}

void MainComponent::switchToGridSlot(const int slotIndex)
{
    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();

    int gridCount = 0;

    {
        const std::lock_guard lock(gridRuntimeMutex);
        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];
        gridCount = static_cast<int>(state.grids.size());

        if (slotIndex < 0 || slotIndex >= gridCount)
        {
            statusLog.append("No grid " + juce::String(slotIndex + 1) + " in this state");

            if (wasPlaying)
                transportEngine.start();

            repaint();
            return;
        }

        activeGridSlot = slotIndex;
    }

    transportEngine.reset();
    lastTransportFrame = 0;
    lastTickInBeat = 0;
    gridEditor.clearPlayhead();
    gridEditor.fitToView();
    gridEditor.repaint();
    resetGridRuntimeClocks();

    if (wasPlaying)
        transportEngine.start();

    updateGridSlotControls();
    showActiveLane();
    statusLog.append("Selected lane " + juce::String(activeGridSlot + 1) + "/" + juce::String(gridCount));
    repaint();
}

void MainComponent::previousGridSlot()
{
    const auto gridCount = compositionStates.empty() ? 0 : static_cast<int>(compositionStates[static_cast<std::size_t>(activeStateIndex)].grids.size());

    if (gridCount <= 1)
    {
        statusLog.append("Only one grid in this state");
        repaint();
        return;
    }

    switchToGridSlot((activeGridSlot + gridCount - 1) % gridCount);
}

void MainComponent::nextGridSlot()
{
    const auto gridCount = compositionStates.empty() ? 0 : static_cast<int>(compositionStates[static_cast<std::size_t>(activeStateIndex)].grids.size());

    if (gridCount <= 1)
    {
        statusLog.append("Only one grid in this state");
        repaint();
        return;
    }

    switchToGridSlot((activeGridSlot + 1) % gridCount);
}

void MainComponent::addGridSlot()
{
    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();

    {
        const std::lock_guard lock(gridRuntimeMutex);
        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.size() >= maximumGridsPerState)
        {
            statusLog.append("State already has 8 grids");

            if (wasPlaying)
                transportEngine.start();

            repaint();
            return;
        }

        CompositionGrid grid;
        grid.snapshot = makeEmptyGridSnapshot();
        state.grids.push_back(std::move(grid));
        activeGridSlot = static_cast<int>(state.grids.size()) - 1;
        gridModel.applySnapshot(state.grids.back().snapshot);
    }

    transportEngine.reset();
    lastTransportFrame = 0;
    lastTickInBeat = 0;
    gridEditor.clearPlayhead();
    gridEditor.fitToView();
    gridEditor.repaint();
    resetGridRuntimeClocks();

    if (wasPlaying)
        transportEngine.start();

    updateGridSlotControls();
    statusLog.append("Added grid " + juce::String(activeGridSlot + 1));
    repaint();
}

void MainComponent::applyGridTimingEditors()
{
    const auto requestedRatio = gridRatioEditor.getText().getDoubleValue();
    const auto ratio = juce::jlimit(minimumGridTempoRatio,
                                    maximumGridTempoRatio,
                                    requestedRatio > 0.0 ? requestedRatio : 1.0);
    const auto requestedPhase = phaseOffsetEditor.getText().getDoubleValue();
    const auto phaseOffset = juce::jlimit(0.0, 360.0, requestedPhase);

    {
        const std::lock_guard lock(gridRuntimeMutex);
        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        auto& grid = state.grids[static_cast<std::size_t>(activeGridSlot)];
        grid.tempoRatio = ratio;
        grid.phaseOffsetDegrees = phaseOffset;
        grid.lastEvaluatedFrame = std::numeric_limits<std::uint64_t>::max();
    }

    updateGridSlotControls();
    statusLog.append("Lane timing set to 1:" + juce::String(ratio, 1) + " phase " + juce::String(phaseOffset, 0));
    repaint();
}

void MainComponent::toggleSelectedGridPhaseMode()
{
    bool offsetEnabled = false;

    {
        const std::lock_guard lock(gridRuntimeMutex);
        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        auto& grid = state.grids[static_cast<std::size_t>(activeGridSlot)];
        grid.phaseOffsetEnabled = ! grid.phaseOffsetEnabled;
        grid.lastEvaluatedFrame = std::numeric_limits<std::uint64_t>::max();
        offsetEnabled = grid.phaseOffsetEnabled;
    }

    updateGridSlotControls();
    statusLog.append(offsetEnabled ? "Lane phase offset enabled" : "Lane phase synced");
    repaint();
}

double MainComponent::getPhaseOffsetFrameDelta(const double ratio, const double degrees) const
{
    const auto clampedRatio = juce::jlimit(minimumGridTempoRatio, maximumGridTempoRatio, ratio);
    const auto clampedDegrees = juce::jlimit(0.0, 360.0, degrees);
    return clampedRatio * (clampedDegrees / 360.0);
}

void MainComponent::resetGridRuntimeClocks()
{
    const std::lock_guard lock(gridRuntimeMutex);

    for (auto& state : compositionStates)
        for (auto& grid : state.grids)
            grid.lastEvaluatedFrame = std::numeric_limits<std::uint64_t>::max();
}

std::uint64_t MainComponent::getDisplayGridFrame(const std::uint64_t stateFrame) const
{
    const std::lock_guard lock(gridRuntimeMutex);

    if (compositionStates.empty())
        return stateFrame;

    const auto stateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
    const auto& state = compositionStates[static_cast<std::size_t>(stateIndex)];

    if (state.grids.empty())
        return stateFrame;

    const auto gridIndex = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
    const auto& grid = state.grids[static_cast<std::size_t>(gridIndex)];
    const auto ratio = juce::jlimit(minimumGridTempoRatio, maximumGridTempoRatio, grid.tempoRatio);
    const auto phaseOffset = grid.phaseOffsetEnabled ? getPhaseOffsetFrameDelta(ratio, grid.phaseOffsetDegrees) : 0.0;
    return static_cast<std::uint64_t>(std::floor((static_cast<double>(stateFrame) + phaseOffset) / ratio));
}

GridEvaluation MainComponent::evaluateActiveState(const TransportEngine::TickContext& context)
{
    const std::lock_guard lock(gridRuntimeMutex);
    storeActiveGridSlotLocked();

    GridEvaluation combined;

    if (compositionStates.empty())
        return combined;

    activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
    auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

    if (state.grids.empty())
        state.grids.push_back({ makeEmptyGridSnapshot() });

    activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);

    for (int index = 0; index < static_cast<int>(state.grids.size()); ++index)
    {
        auto& grid = state.grids[static_cast<std::size_t>(index)];

        if (grid.kind != CompositionGrid::Kind::grid)
            continue;

        const auto ratio = juce::jlimit(minimumGridTempoRatio, maximumGridTempoRatio, grid.tempoRatio);
        const auto phaseOffset = grid.phaseOffsetEnabled ? getPhaseOffsetFrameDelta(ratio, grid.phaseOffsetDegrees) : 0.0;
        const auto gridFrame = static_cast<std::uint64_t>(std::floor((static_cast<double>(context.frame) + phaseOffset) / ratio));

        if (gridFrame == grid.lastEvaluatedFrame)
            continue;

        grid.lastEvaluatedFrame = gridFrame;
        auto evaluation = gridInterpreter.evaluate(grid.snapshot, gridFrame);

        if (evaluation.grid.width > 0 && evaluation.grid.height > 0)
            grid.snapshot = evaluation.grid;

        applyLaneMixToEvents(evaluation.events, grid);
        combined.events.insert(combined.events.end(), evaluation.events.begin(), evaluation.events.end());

        if (index == activeGridSlot)
        {
            combined.grid = grid.snapshot;
            gridModel.applySnapshot(grid.snapshot);
        }
    }

    if (combined.grid.width <= 0 || combined.grid.height <= 0)
        combined.grid = state.grids[static_cast<std::size_t>(activeGridSlot)].snapshot;

    embeddedScAudio.setTransport(context.bpm, context.frame, true);
    embeddedScAudio.enqueue(combined.events);

    return combined;
}

void MainComponent::toggleTransportPlayback()
{
    applyTransportEditors();

    if (transportEngine.isPlaying())
    {
        transportEngine.pause();
        statusLog.append("Transport paused");
    }
    else
    {
        transportEngine.start();
        statusLog.append("Transport running");
    }

    updateTransportControls();
    refreshStateGraph();
    repaint();
}

void MainComponent::resetTransport()
{
    transportEngine.reset();
    lastTransportFrame = 0;
    lastTickInBeat = 0;
    activeStateEntryFrame = 0;
    gridEditor.clearPlayhead();
    resetGridRuntimeClocks();
    statusLog.append("Transport reset");
    updateTransportControls();
    refreshStateGraph();
    repaint();
}

void MainComponent::applyTransportEditors()
{
    const auto bpm = bpmEditor.getText().getDoubleValue();
    transportEngine.setBpm(bpm > 0.0 ? bpm : 120.0);
    bpmEditor.setText(juce::String(transportEngine.getBpm(), 1), juce::dontSendNotification);
    forceBlackEditorText(bpmEditor);

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (! compositionStates.empty())
        {
            activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
            compositionStates[static_cast<std::size_t>(activeStateIndex)].bpm = transportEngine.getBpm();
        }
    }

    refreshStateGraph();
}

MainComponent::TransitionRules MainComponent::parseTransitionRules(juce::String text) const
{
    TransitionRules rules;
    juce::StringArray lines;
    lines.addLines(text);
    juce::String uncommented;

    for (auto line : lines)
    {
        const auto commentStart = line.indexOf("//");

        if (commentStart >= 0)
            line = line.substring(0, commentStart);

        uncommented += line + "\n";
    }

    const auto source = uncommented.toStdString();
    std::smatch blockMatch;

    if (std::regex_search(source, blockMatch, std::regex(R"(~linear\s*=\s*\(([\s\S]*?)\);)")))
    {
        const auto block = blockMatch[1].str();
        const std::regex entryPattern(R"((\d+)\s*:\s*(\d+))");

        for (auto iterator = std::sregex_iterator(block.begin(), block.end(), entryPattern);
             iterator != std::sregex_iterator();
             ++iterator)
        {
            const auto from = juce::String((*iterator)[1].str()).getIntValue();
            const auto to = juce::String((*iterator)[2].str()).getIntValue();

            if (from > 0 && to > 0)
                rules.linear[from] = to;
        }
    }

    if (std::regex_search(source, blockMatch, std::regex(R"(~weighted\s*=\s*\(([\s\S]*?)\);)")))
    {
        const auto block = blockMatch[1].str();
        const std::regex statePattern(R"((\d+)\s*:\s*\[([\s\S]*?)\])");
        const std::regex choicePattern(R"(to\s*:\s*(\d+)\s*,\s*chance\s*:\s*([0-9]*\.?[0-9]+))");

        for (auto stateIterator = std::sregex_iterator(block.begin(), block.end(), statePattern);
             stateIterator != std::sregex_iterator();
             ++stateIterator)
        {
            const auto from = juce::String((*stateIterator)[1].str()).getIntValue();
            const auto choicesBlock = (*stateIterator)[2].str();
            std::vector<TransitionChoice> choices;

            for (auto choiceIterator = std::sregex_iterator(choicesBlock.begin(), choicesBlock.end(), choicePattern);
                 choiceIterator != std::sregex_iterator();
                 ++choiceIterator)
            {
                const auto to = juce::String((*choiceIterator)[1].str()).getIntValue();
                const auto chance = juce::String((*choiceIterator)[2].str()).getDoubleValue();

                if (to > 0 && chance > 0.0)
                    choices.push_back({ to, chance });
            }

            if (from > 0 && ! choices.empty())
                rules.weighted[from] = std::move(choices);
        }
    }

    return rules;
}

int MainComponent::chooseTransitionTarget(const TransitionRules& rules, const int currentState)
{
    if (const auto weighted = rules.weighted.find(currentState); weighted != rules.weighted.end())
    {
        double total = 0.0;

        for (const auto& choice : weighted->second)
            total += juce::jmax(0.0, choice.chance);

        if (total > 0.0)
        {
            const auto roll = transitionRandom.nextDouble() * total;
            double sum = 0.0;

            for (const auto& choice : weighted->second)
            {
                sum += juce::jmax(0.0, choice.chance);

                if (roll <= sum)
                    return choice.targetState;
            }

            return weighted->second.back().targetState;
        }
    }

    if (const auto linear = rules.linear.find(currentState); linear != rules.linear.end())
        return linear->second;

    return currentState;
}

void MainComponent::applyTransitionTarget(const int targetState)
{
    const auto targetIndex = targetState - 1;
    double stateBpm = transportEngine.getBpm();

    if (targetIndex < 0)
        return;

    storeActiveGridSlot();

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (targetIndex >= static_cast<int>(compositionStates.size()))
            return;

        if (targetIndex == activeStateIndex)
            return;

        activeStateIndex = targetIndex;
        activeStateEntryFrame = lastTransportFrame;
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        stateBpm = state.bpm;

        for (auto& grid : state.grids)
            grid.lastEvaluatedFrame = std::numeric_limits<std::uint64_t>::max();

        gridModel.applySnapshot(state.grids[static_cast<std::size_t>(activeGridSlot)].snapshot);
    }

    transportEngine.setBpm(stateBpm);
    gridEditor.clearPlayhead();
    gridEditor.fitToView();
    updateTransportControls();
    updateGridSlotControls();
    updateStateAdvanceControls();
    showActiveTransitionCode();
    compileScLanesForState(activeStateIndex);
    statusLog.append("Transition -> state " + juce::String(targetState));
    repaint();
}

void MainComponent::advanceStateFromTransitionPane(const TransportEngine::TickResult& result)
{
    if (result.context.frame == 0 || compositionStates.size() <= 1)
        return;

    CompositionState::AdvanceMode mode = CompositionState::AdvanceMode::manual;
    int interval = 1;
    juce::String transitionCode;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            return;

        const auto stateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        const auto& state = compositionStates[static_cast<std::size_t>(stateIndex)];
        mode = state.advanceMode;
        interval = juce::jlimit(1, 999, state.advanceInterval);
        transitionCode = state.transitionCode;
    }

    bool shouldAdvance = false;

    switch (mode)
    {
        case CompositionState::AdvanceMode::manual:
            return;

        case CompositionState::AdvanceMode::beats:
        {
            if (! result.context.isBeat)
                return;

            const auto elapsedFrames = result.context.frame >= activeStateEntryFrame
                                           ? result.context.frame - activeStateEntryFrame
                                           : 0;
            const auto elapsedBeats = elapsedFrames / static_cast<std::uint64_t>(juce::jmax(1, result.context.ticksPerBeat));
            shouldAdvance = elapsedBeats >= static_cast<std::uint64_t>(interval);
            break;
        }

        case CompositionState::AdvanceMode::bars:
        {
            if (! result.context.isBeat)
                return;

            constexpr int beatsPerBar = 4;
            const auto elapsedFrames = result.context.frame >= activeStateEntryFrame
                                           ? result.context.frame - activeStateEntryFrame
                                           : 0;
            const auto framesPerBar = static_cast<std::uint64_t>(juce::jmax(1, result.context.ticksPerBeat) * beatsPerBar);
            const auto elapsedBars = elapsedFrames / framesPerBar;
            shouldAdvance = elapsedBars >= static_cast<std::uint64_t>(interval);
            break;
        }

        case CompositionState::AdvanceMode::trigger:
        {
            for (const auto& event : result.evaluation.events)
            {
                if (const auto* trigger = std::get_if<TriggerEvent>(&event))
                {
                    const auto name = trigger->triggerName.trim().toLowerCase();

                    if (name == "advance" || name == "next" || name == "transition")
                    {
                        shouldAdvance = true;
                        break;
                    }
                }
            }

            break;
        }
    }

    if (! shouldAdvance)
        return;

    const auto rules = parseTransitionRules(transitionCode);

    if (rules.linear.empty() && rules.weighted.empty())
        return;

    const auto currentState = activeStateIndex + 1;
    const auto targetState = chooseTransitionTarget(rules, currentState);

    if (targetState != currentState)
        applyTransitionTarget(targetState);
    else
        activeStateEntryFrame = result.context.frame;
}

void MainComponent::handleTransportTick(const TransportEngine::TickResult& result)
{
    lastTransportFrame = result.context.frame;
    lastTickInBeat = result.context.tickInBeat;
    lastTickWasBeat = result.context.isBeat;
    lastPulseTimeMs = juce::Time::getMillisecondCounterHiRes();

    if (gridModel.getHeight() > 0)
        gridEditor.setPlayheadRow(static_cast<int>(getDisplayGridFrame(result.context.frame) % static_cast<std::uint64_t>(gridModel.getHeight())));

    for (const auto& event : result.evaluation.events)
    {
        if (const auto* mutation = std::get_if<GridMutationEvent>(&event))
            gridEditor.repaintRow(mutation->targetCell.row);
    }

    appendEvaluatedEventsToLog(result);
    advanceStateFromTransitionPane(result);
    refreshStateGraph();
    repaint(0, 0, getWidth(), 92);
    repaint(0, juce::jmax(0, getHeight() - 76), getWidth(), 76);
}

void MainComponent::appendEvaluatedEventsToLog(const TransportEngine::TickResult& result)
{
    const auto routedLogs = eventRouter.route(result.evaluation.events, result.evaluation.grid, oscOutput);

    if (routedLogs.empty())
    {
        if (result.context.isBeat)
            statusLog.append("Tick " + juce::String(result.context.frame) + ": no events");
        return;
    }

    juce::String message = "Tick " + juce::String(result.context.frame) + ": ";
    const auto eventsToShow = juce::jmin(3, static_cast<int>(routedLogs.size()));

    for (int index = 0; index < eventsToShow; ++index)
    {
        if (index > 0)
            message += " | ";

        message += routedLogs[static_cast<std::size_t>(index)].message;
    }

    for (const auto& event : routedLogs)
        appendEventMonitorLine(event);

    if (static_cast<int>(routedLogs.size()) > eventsToShow)
        message += " | +" + juce::String(static_cast<int>(routedLogs.size()) - eventsToShow);

    statusLog.append(message);
}

void MainComponent::stopTransport()
{
    transportEngine.stop();
    gridEditor.clearPlayhead();
    statusLog.append("Transport stopped");
    updateTransportControls();
    refreshStateGraph();
    repaint();
}

juce::String MainComponent::getTransportStateText() const
{
    juce::String state;

    switch (transportEngine.getState())
    {
        case TransportEngine::State::running: state = "PLAY"; break;
        case TransportEngine::State::paused:  state = "PAUSE"; break;
        case TransportEngine::State::stopped: state = "STOP"; break;
    }

    return state
        + "  " + juce::String(transportEngine.getBpm(), 1) + " BPM"
        + "  TPB " + juce::String(transportEngine.getTicksPerBeat())
        + "  TICK " + juce::String(lastTickInBeat + 1)
        + "  FRAME " + juce::String(lastTransportFrame).paddedLeft('0', 4);
}
}
