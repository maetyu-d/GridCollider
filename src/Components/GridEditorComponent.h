#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/GridModel.h"

#include <optional>
#include <vector>

namespace gridcollider
{
class GridEditorComponent final : public juce::Component
{
public:
    struct Theme
    {
        juce::Colour background = juce::Colour::fromRGB(7, 11, 8);
        juce::Colour viewportBackground = juce::Colour::fromRGB(10, 17, 12);
        juce::Colour gridLine = juce::Colour::fromRGB(24, 43, 28);
        juce::Colour text = juce::Colour::fromRGB(184, 255, 196);
        juce::Colour mutedText = juce::Colour::fromRGB(84, 128, 94);
        juce::Colour rulerBackground = juce::Colour::fromRGB(13, 24, 16);
        juce::Colour rulerText = juce::Colour::fromRGB(116, 175, 128);
        juce::Colour cursor = juce::Colour::fromRGB(206, 255, 212);
        juce::Colour cursorText = juce::Colour::fromRGB(4, 10, 5);
        juce::Colour selection = juce::Colour::fromRGBA(75, 180, 96, 76);
        juce::Colour selectionBorder = juce::Colour::fromRGB(96, 220, 118);
        juce::Colour playhead = juce::Colour::fromRGBA(120, 255, 140, 36);
    };

    explicit GridEditorComponent(GridModel& modelToDisplay);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    void setTheme(Theme newTheme);
    [[nodiscard]] const Theme& getTheme() const noexcept;
    void setHatchAngle(int degrees);

    void setRulersVisible(bool shouldShowRulers);
    [[nodiscard]] bool areRulersVisible() const noexcept;

    void clearGrid();
    void copySelectionToClipboard() const;
    void cutSelectionToClipboard();
    void pasteFromClipboard();
    void deleteSelectionOrCell();
    void selectAll();
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    bool undo();
    bool redo();
    void clearUndoHistory();
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitToView();
    void setPlayheadRow(std::optional<int> row);
    void clearPlayhead();
    void repaintRow(int row);

private:
    struct CellPosition
    {
        int column = 0;
        int row = 0;

        [[nodiscard]] bool operator==(const CellPosition& other) const noexcept
        {
            return column == other.column && row == other.row;
        }
    };

    struct Layout
    {
        juce::Rectangle<float> outer;
        juce::Rectangle<float> gridViewport;
        juce::Rectangle<float> topRuler;
        juce::Rectangle<float> leftRuler;
        juce::Rectangle<float> cornerRuler;
    };

    GridModel& model;
    Theme theme;

    CellPosition cursor;
    std::optional<CellPosition> selectionAnchor;
    std::optional<CellPosition> mouseSelectionAnchor;
    std::optional<juce::Point<float>> rightDragPanAnchor;

    juce::Point<float> scrollOffset;
    std::vector<GridModel::Snapshot> undoSnapshots;
    std::vector<GridModel::Snapshot> redoSnapshots;
    std::optional<int> playheadRow;
    float zoom = 1.0f;
    bool rulersVisible = true;
    int hatchAngle = -45;

    [[nodiscard]] Layout getLayout() const;
    [[nodiscard]] float getCellWidth() const noexcept;
    [[nodiscard]] float getCellHeight() const noexcept;
    [[nodiscard]] juce::Rectangle<float> getCellBounds(int column, int row) const;
    [[nodiscard]] std::optional<CellPosition> cellAt(juce::Point<float> position) const;
    [[nodiscard]] juce::Rectangle<int> getSelectedCells() const noexcept;
    [[nodiscard]] bool hasSelection() const noexcept;

    void moveCursor(int deltaColumns, int deltaRows, bool extendingSelection);
    void setCursor(CellPosition position, bool extendingSelection);
    void insertCharacter(char character);
    void pushUndoSnapshot();
    void restoreUndoSnapshot(const GridModel::Snapshot& snapshot);
    void clearSelectionOrCell(bool moveLeftAfterClearing);
    void clearCells(const juce::Rectangle<int>& cellsToClear);
    void toggleRulers();
    void setZoom(float newZoom);
    void zoomAt(float newZoom, juce::Point<float> anchor);
    void scrollBy(float deltaX, float deltaY);
    void clampScrollOffset();
    void ensureCursorVisible();

    [[nodiscard]] static bool isEditableAscii(juce::juce_wchar character) noexcept;
    [[nodiscard]] static char toShortcutCharacter(const juce::KeyPress& key) noexcept;
    [[nodiscard]] static std::vector<juce::String> splitClipboardLines(const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridEditorComponent)
};
}
