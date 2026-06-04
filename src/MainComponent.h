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

#include <algorithm>
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
    void menuToggleInstrumentsView();
    void menuToggleTransitionsView();
    void menuToggleEventMonitorView();
    void menuToggleStateInspectorView();
    void menuLoadExample(const juce::File& file);
    [[nodiscard]] bool isMixerViewVisible() const noexcept;
    [[nodiscard]] bool isArrangementViewVisible() const noexcept;
    [[nodiscard]] bool isInstrumentsViewVisible() const noexcept;
    [[nodiscard]] bool isTransitionsViewVisible() const noexcept;
    [[nodiscard]] bool isEventMonitorViewVisible() const noexcept;
    [[nodiscard]] bool isStateInspectorViewVisible() const noexcept;

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
            int state = -1;
            int lane = -1;
            int x = 10;
            int width = 52;
            juce::String name;
            juce::String output;
            juce::Colour colour;
            bool master = false;
            bool selected = false;
            bool muted = false;
            bool soloed = false;
            float meter = 0.0f;
            float peakHold = 0.0f;
            int peakHoldTicks = 0;
        };

        struct Group
        {
            int state = -1;
            juce::String name;
            int laneCount = 0;
            juce::Colour colour;
            bool collapsed = false;
            juce::Rectangle<int> bounds;
        };

        MixerContentComponent() { setOpaque(true); }

        std::function<void(int, int)> onStripClicked;
        std::function<void(int)> onStateHeaderClicked;

        void setGroups(std::vector<Group> newGroups)
        {
            groups = std::move(newGroups);
            repaint();
        }

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

                auto nextPeak = strip.peakHold;
                auto nextHoldTicks = strip.peakHoldTicks;
                if (next >= strip.peakHold - 0.003f)
                {
                    nextPeak = next;
                    nextHoldTicks = 18;
                }
                else if (nextHoldTicks > 0)
                {
                    --nextHoldTicks;
                }
                else
                {
                    nextPeak = juce::jmax(next, strip.peakHold - 0.018f);
                }

                if (std::abs(strip.meter - next) <= 0.0015f
                    && std::abs(strip.peakHold - nextPeak) <= 0.0015f
                    && strip.peakHoldTicks == nextHoldTicks)
                    continue;

                strip.meter = next;
                strip.peakHold = nextPeak;
                strip.peakHoldTicks = nextHoldTicks;
                repaint(strip.x, 0, strip.width, getHeight());
            }
        }

        void mouseDown(const juce::MouseEvent& event) override
        {
            if (onStateHeaderClicked != nullptr)
            {
                for (const auto& group : groups)
                {
                    if (group.bounds.contains(event.getPosition()))
                    {
                        onStateHeaderClicked(group.state);
                        return;
                    }
                }
            }

            if (onStripClicked == nullptr)
                return;

            for (const auto& strip : strips)
            {
                if (strip.master || strip.state < 0 || strip.lane < 0)
                    continue;

                if (getStripBounds(strip).contains(event.getPosition()))
                {
                    onStripClicked(strip.state, strip.lane);
                    return;
                }
            }
        }

        [[nodiscard]] juce::Rectangle<int> getStripBounds(const Strip& strip) const
        {
            const auto y = strip.master && groups.empty() ? 10 : 42;
            return { strip.x,
                     y,
                     juce::jmax(24, strip.width - 6),
                     juce::jmax(380, contentHeight - y - 10) };
        }

        void paintGroupHeaders(juce::Graphics& graphics,
                               const juce::Colour& ink,
                               const juce::Colour& line)
        {
            graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));

            for (const auto& group : groups)
            {
                if (group.bounds.isEmpty())
                    continue;

                graphics.setColour(group.colour.withAlpha(group.collapsed ? 0.24f : 0.16f));
                graphics.fillRoundedRectangle(group.bounds.toFloat(), 4.0f);
                graphics.setColour(group.colour.withAlpha(0.72f));
                graphics.drawRoundedRectangle(group.bounds.toFloat().reduced(0.5f), 4.0f, 1.0f);

                graphics.setColour(ink.withAlpha(group.collapsed ? 0.90f : 0.76f));
                auto labelArea = group.bounds.reduced(7, 0);
                const auto glyph = group.collapsed ? "+" : "-";
                graphics.drawFittedText(glyph, labelArea.removeFromLeft(13), juce::Justification::centred, 1);
                labelArea.removeFromLeft(2);
                graphics.drawFittedText(group.name
                                            + "  "
                                            + juce::String(group.laneCount)
                                            + (group.laneCount == 1 ? " lane" : " lanes"),
                                        labelArea,
                                        juce::Justification::centredLeft,
                                        1);

                if (! group.collapsed)
                {
                    graphics.setColour(line.withAlpha(0.38f));
                    graphics.drawVerticalLine(group.bounds.getX(),
                                              static_cast<float>(group.bounds.getBottom() + 4),
                                              static_cast<float>(getHeight() - 10));
                }
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

            paintGroupHeaders(graphics, ink, line);

            for (int index = 0; index < static_cast<int>(strips.size()); ++index)
            {
                const auto& channel = strips[static_cast<std::size_t>(index)];
                auto bounds = getStripBounds(channel);

                graphics.setColour(channel.master ? juce::Colour::fromRGB(69, 65, 79)
                                                  : channel.selected ? strip.brighter(0.09f)
                                                                     : strip);
                graphics.fillRect(bounds);
                graphics.setColour(channel.selected ? channel.colour.brighter(0.24f)
                                                    : line.withAlpha(channel.master ? 0.90f : 0.64f));
                graphics.drawRect(bounds, channel.master || channel.selected ? 2 : 1);

                auto colourBand = bounds.removeFromBottom(36);
                graphics.setColour(channel.colour);
                graphics.fillRect(colourBand);

                auto header = bounds.removeFromTop(channel.master ? 56 : 62).reduced(5, 6);
                graphics.setColour(ink);
                graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.2f, juce::Font::bold));
                graphics.drawFittedText(channel.name, header.removeFromTop(20), juce::Justification::centred, 1);

                graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
                graphics.setColour(ink.withAlpha(0.68f));
                graphics.drawFittedText(channel.master ? "STEREO OUT" : channel.output,
                                        header.removeFromTop(14),
                                        juce::Justification::centred,
                                        1);

                if (! channel.master)
                {
                    auto stateRow = header.removeFromTop(16);
                    graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
                    graphics.setColour(channel.muted ? juce::Colour::fromRGB(255, 205, 60) : ink.withAlpha(0.36f));
                    graphics.drawFittedText("M", stateRow.removeFromLeft(stateRow.getWidth() / 2), juce::Justification::centred, 1);
                    graphics.setColour(channel.soloed ? juce::Colour::fromRGB(102, 224, 133) : ink.withAlpha(0.36f));
                    graphics.drawFittedText("S", stateRow, juce::Justification::centred, 1);
                }

                if (! channel.master)
                {
                    const auto knobSize = juce::jmin(34, juce::jmax(24, bounds.getWidth() - 8));
                    const auto knobArea = juce::Rectangle<int>(bounds.getCentreX() - knobSize / 2,
                                                               bounds.getY() + 9,
                                                               knobSize,
                                                               knobSize);
                    graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
                    graphics.setColour(ink.withAlpha(0.44f));
                    graphics.drawFittedText("PAN",
                                            juce::Rectangle<int>(bounds.getX(), bounds.getY() - 3, bounds.getWidth(), 10),
                                            juce::Justification::centred,
                                            1);
                    graphics.setColour(line.withAlpha(0.62f));
                    graphics.fillEllipse(knobArea.toFloat());
                    graphics.setColour(ink.withAlpha(0.28f));
                    graphics.drawEllipse(knobArea.toFloat().reduced(0.5f), 1.0f);
                    graphics.setColour(ink.withAlpha(0.56f));
                    graphics.drawVerticalLine(knobArea.getCentreX(),
                                              static_cast<float>(knobArea.getY() + 4),
                                              static_cast<float>(knobArea.getY() + 10));
                    auto panLetters = juce::Rectangle<int>(bounds.getX(), knobArea.getBottom() - 2, bounds.getWidth(), 10);
                    graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 6.5f, juce::Font::bold));
                    graphics.drawFittedText("L", panLetters.removeFromLeft(panLetters.getWidth() / 3), juce::Justification::centred, 1);
                    graphics.drawFittedText("C", panLetters.removeFromLeft(panLetters.getWidth() / 2), juce::Justification::centred, 1);
                    graphics.drawFittedText("R", panLetters, juce::Justification::centred, 1);
                }

                const auto faderLane = juce::Rectangle<int>(bounds.getCentreX() - 1,
                                                            bounds.getY() + (channel.master ? 26 : 72),
                                                            2,
                                                            juce::jmax(190, bounds.getHeight() - (channel.master ? 90 : 140)));
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

                const auto peakHold = juce::jlimit(0.0f, 1.0f, channel.peakHold);
                if (peakHold > 0.01f)
                {
                    const auto peakY = meterBounds.getBottom()
                                       - static_cast<int>(std::round(static_cast<float>(meterBounds.getHeight()) * peakHold));
                    graphics.setColour(ink.withAlpha(0.88f));
                    graphics.fillRect(meterBounds.withY(peakY).withHeight(2).expanded(1, 0));
                }

                graphics.setColour(juce::Colours::white.withAlpha(0.88f));
                graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::bold));
                graphics.drawFittedText(channel.name, colourBand.reduced(5, 4), juce::Justification::centred, 2);
            }
        }

    private:
        std::vector<Group> groups;
        std::vector<Strip> strips;
        int stripWidth = 112;
        int contentHeight = 420;
    };

    class EventMonitorComponent final : public juce::Component
    {
    public:
        EventMonitorComponent()
        {
            setOpaque(true);
        }

        void setLines(juce::StringArray newLines)
        {
            const auto wasNearBottom = scrollOffsetRows >= getMaxScrollRows() - 2;
            lines = std::move(newLines);

            if (wasNearBottom)
                scrollOffsetRows = getMaxScrollRows();
            else
                scrollOffsetRows = juce::jlimit(0, getMaxScrollRows(), scrollOffsetRows);

            repaint();
        }

        void scrollToEnd()
        {
            scrollOffsetRows = getMaxScrollRows();
            repaint();
        }

        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        {
            const auto rows = juce::jmax(1, juce::roundToInt(std::abs(wheel.deltaY) * 9.0f));
            scrollOffsetRows += wheel.deltaY > 0.0f ? -rows : rows;
            scrollOffsetRows = juce::jlimit(0, getMaxScrollRows(), scrollOffsetRows);
            repaint();
        }

        void resized() override
        {
            scrollOffsetRows = juce::jlimit(0, getMaxScrollRows(), scrollOffsetRows);
        }

        void paint(juce::Graphics& graphics) override
        {
            const auto background = juce::Colour::fromRGB(18, 19, 20);
            const auto rowAlt = juce::Colour::fromRGB(29, 30, 31);
            const auto line = juce::Colour::fromRGB(82, 84, 86);
            const auto text = juce::Colour::fromRGB(235, 236, 232);
            const auto accent = juce::Colour::fromRGB(95, 178, 222);

            graphics.fillAll(background);
            graphics.setColour(line.withAlpha(0.72f));
            graphics.drawRect(getLocalBounds(), 1);
            graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));

            const auto textArea = getLocalBounds().reduced(12, 9).withTrimmedRight(14);
            const auto visibleRows = getVisibleRows();

            for (int row = 0; row < visibleRows; ++row)
            {
                const auto lineIndex = scrollOffsetRows + row;
                if (lineIndex >= lines.size())
                    break;

                auto rowBounds = textArea.withY(textArea.getY() + row * lineHeight).withHeight(lineHeight);
                if ((lineIndex & 1) != 0)
                {
                    graphics.setColour(rowAlt.withAlpha(0.42f));
                    graphics.fillRect(rowBounds.expanded(4, 0));
                }

                graphics.setColour(text.withAlpha(lineIndex == lines.size() - 1 ? 1.0f : 0.86f));
                graphics.drawText(lines[lineIndex], rowBounds, juce::Justification::centredLeft, false);
            }

            drawScrollBar(graphics, accent, line);
        }

    private:
        [[nodiscard]] int getVisibleRows() const
        {
            return juce::jmax(1, (getHeight() - 18) / lineHeight);
        }

        [[nodiscard]] int getMaxScrollRows() const
        {
            return juce::jmax(0, lines.size() - getVisibleRows());
        }

        void drawScrollBar(juce::Graphics& graphics, juce::Colour accent, juce::Colour line) const
        {
            const auto maxScroll = getMaxScrollRows();
            if (maxScroll <= 0)
                return;

            auto track = getLocalBounds().reduced(6, 8).removeFromRight(5);
            graphics.setColour(line.withAlpha(0.30f));
            graphics.fillRoundedRectangle(track.toFloat(), 2.5f);

            const auto visibleRatio = juce::jlimit(0.05f,
                                                   1.0f,
                                                   static_cast<float>(getVisibleRows()) / static_cast<float>(lines.size()));
            const auto thumbHeight = juce::jmax(18, juce::roundToInt(static_cast<float>(track.getHeight()) * visibleRatio));
            const auto travel = juce::jmax(1, track.getHeight() - thumbHeight);
            const auto thumbY = track.getY() + juce::roundToInt(static_cast<float>(travel) * static_cast<float>(scrollOffsetRows) / static_cast<float>(maxScroll));

            graphics.setColour(accent.withAlpha(0.88f));
            graphics.fillRoundedRectangle(track.withY(thumbY).withHeight(thumbHeight).toFloat(), 2.5f);
        }

        juce::StringArray lines;
        int scrollOffsetRows = 0;
        static constexpr int lineHeight = 18;
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
    void configureStateInspectorView();
    void styleStateInspectorView();
    void refreshStateInspectorView();
    void applyStateInspectorEditors();
    void cycleStateInspectorAdvanceMode();
    void applyStateInspectorLaneEditor(int laneIndex);
    void toggleStateInspectorLaneKind(int laneIndex);
    void toggleStateInspectorLanePhase(int laneIndex);
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
    void selectMixerChannel(int stateIndex, int laneIndex);
    void toggleMixerStateCollapsed(int stateIndex);
    void toggleMixerMute(int stateIndex, int laneIndex, int mixerControlIndex);
    void toggleMixerSolo(int stateIndex, int laneIndex, int mixerControlIndex);
    void configureArrangementView();
    void toggleArrangementView();
    void refreshArrangementView();
    void configureInstrumentView();
    void styleInstrumentView();
    void refreshInstrumentView();
    void storeActiveInstrumentEditor();
    void storeActiveUserInstrument();
    void storeActiveDefaultInstrument();
    void storeActiveLaneInstrument();
    void selectInstrumentEditorTarget(int comboId);
    void selectUserInstrument(int index);
    void addUserInstrument();
    void duplicateSelectedInstrument();
    void deleteSelectedUserInstrument();
    void saveSelectedInstrument();
    void resetSelectedDefaultInstrument();
    void compileSelectedUserInstrument();
    void auditionSelectedInstrument();
    void compileEditableDefaultSynthDefs();
    void compileUserInstruments();
    void applyChannelInstrumentEditors();
    void applyChannelMappingsToEngine();
    void initialiseDefaultInstrumentLayer();
    [[nodiscard]] juce::String createDefaultUserInstrumentCode(const juce::String& name) const;
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
        bool mixerMuted = false;
        bool mixerSoloed = false;
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
    EventMonitorComponent eventMonitor;
    juce::Label eventMonitorLabel;
    juce::Component stateInspectorView;
    juce::Label stateInspectorTitleLabel;
    juce::Label stateInspectorMetaLabel;
    juce::Label stateInspectorNameLabel;
    juce::Label stateInspectorBpmLabel;
    juce::Label stateInspectorSignatureLabel;
    juce::Label stateInspectorAdvanceLabel;
    juce::Label stateInspectorTransitionLabel;
    juce::Label stateInspectorLaneHeaderLabel;
    juce::TextEditor stateInspectorNameEditor;
    juce::TextEditor stateInspectorBpmEditor;
    juce::TextEditor stateInspectorTimeSignatureNumeratorEditor;
    juce::TextEditor stateInspectorTimeSignatureDenominatorEditor;
    juce::TextButton stateInspectorAdvanceModeButton;
    juce::TextEditor stateInspectorAdvanceIntervalEditor;
    std::array<juce::Label, 8> stateInspectorLaneLabels;
    std::array<juce::Label, 8> stateInspectorLaneSizeLabels;
    std::array<juce::TextButton, 8> stateInspectorLaneKindButtons;
    std::array<juce::TextEditor, 8> stateInspectorLaneRatioEditors;
    std::array<juce::TextButton, 8> stateInspectorLanePhaseButtons;
    std::array<juce::TextEditor, 8> stateInspectorLanePhaseEditors;
    std::array<juce::TextEditor, 8> stateInspectorLaneLevelEditors;
    std::array<juce::TextEditor, 8> stateInspectorLanePanEditors;
    std::array<juce::TextEditor, 8> stateInspectorLaneInstrumentEditors;
    SourceCodeBackdropComponent laneCodeBackdrop;
    SuperColliderCodeTokeniser scCodeTokeniser;
    juce::CodeDocument transitionCodeDocument;
    juce::CodeDocument laneScCodeDocument;
    juce::CodeDocument instrumentCodeDocument;
    CodeDocumentChangeListener transitionCodeDocumentListener;
    CodeDocumentChangeListener laneScCodeDocumentListener;
    CodeDocumentChangeListener instrumentCodeDocumentListener;
    juce::CodeEditorComponent transitionCodeEditor;
    juce::CodeEditorComponent stateInspectorTransitionEditor;
    juce::CodeEditorComponent laneScCodeEditor;
    juce::CodeEditorComponent instrumentCodeEditor;

    static constexpr int maximumMixerChannels = 129;
    static constexpr int instrumentChannelCount = EmbeddedScAudioEngine::channelCount;
    juce::Viewport mixerViewport;
    MixerContentComponent mixerContent;
    MixerContentComponent mixerMasterContent;
    juce::Viewport arrangementViewport;
    ArrangementContentComponent arrangementContent;
    juce::Component instrumentView;
    juce::Label instrumentViewTitleLabel;
    juce::ComboBox instrumentSelector;
    juce::TextEditor instrumentNameEditor;
    juce::TextButton newInstrumentButton;
    juce::TextButton duplicateInstrumentButton;
    juce::TextButton deleteInstrumentButton;
    juce::TextButton resetInstrumentButton;
    juce::TextButton saveInstrumentButton;
    juce::TextButton compileInstrumentButton;
    juce::TextButton applyInstrumentMapButton;
    juce::Label instrumentAuditionLabel;
    juce::TextEditor auditionPitchEditor;
    juce::TextEditor auditionVelocityEditor;
    juce::TextEditor auditionDurationEditor;
    juce::TextButton auditionButton;
    juce::Label instrumentCodeLabel;
    juce::Label instrumentMapLabel;
    juce::Label instrumentUsedByLabel;
    juce::Viewport instrumentMapViewport;
    juce::Component instrumentMapContent;
    std::array<juce::Label, instrumentChannelCount> instrumentChannelLabels;
    std::array<juce::ComboBox, instrumentChannelCount> instrumentChannelSelectors;
    juce::Label mixerLabel;
    std::array<juce::Label, maximumMixerChannels> mixerChannelLabels;
    std::array<juce::Slider, maximumMixerChannels> mixerLevelSliders;
    std::array<juce::Slider, maximumMixerChannels> mixerPanSliders;
    std::array<juce::TextButton, maximumMixerChannels> mixerMuteButtons;
    std::array<juce::TextButton, maximumMixerChannels> mixerSoloButtons;
    std::array<std::atomic<float>, maximumMixerChannels> mixerMeterPeaks {};
    std::array<float, maximumMixerChannels> mixerMeterDisplay {};
    std::vector<bool> mixerStateCollapsed;

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
    std::atomic<bool> eventMonitorDirty { false };
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
    bool instrumentsViewVisible = false;
    bool transitionsViewVisible = false;
    bool eventMonitorViewVisible = false;
    bool stateInspectorViewVisible = false;
    bool updatingStateInspectorView = false;
    std::uint64_t uiFrameCounter = 0;
    double lastTimerCallbackMs = 0.0;
    double timerDeltaMs = 0.0;
    std::atomic<std::uint64_t> audioCallbackCounter { 0 };
    std::atomic<std::uint64_t> audioSampleCounter { 0 };
    juce::Array<juce::File> recentPatternFiles;
    juce::StringArray eventMonitorLines;
    mutable std::mutex eventMonitorMutex;
    std::vector<CompositionState> compositionStates;
    std::optional<CompositionState> copiedState;
    struct UserInstrument
    {
        juce::String name;
        juce::String code;
    };
    std::vector<UserInstrument> userInstruments;
    std::vector<UserInstrument> defaultInstruments;
    std::array<juce::String, instrumentChannelCount> channelInstrumentMap {};
    struct LaneInstrumentReference
    {
        int stateIndex = -1;
        int laneIndex = -1;
        juce::String label;
        juce::String synthName;
    };
    std::vector<LaneInstrumentReference> instrumentLaneReferences;
    static constexpr int instrumentDefaultComboBaseId = 5000;
    static constexpr int instrumentLaneComboBaseId = 10000;
    int selectedDefaultInstrumentIndex = -1;
    int selectedUserInstrumentIndex = -1;
    int selectedInstrumentLaneReferenceIndex = -1;
    bool updatingInstrumentView = false;
    bool userInstrumentCodeDirty = false;
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
