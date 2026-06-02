#include "GridEditorComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gridcollider
{
namespace
{
constexpr float baseCellWidth = 14.0f;
constexpr float baseCellHeight = 18.0f;
constexpr float defaultZoom = 2.0f;
constexpr float minimumZoom = 0.40f;
constexpr float maximumZoom = 3.0f;
constexpr float zoomStep = 1.12f;
constexpr float outerPadding = 8.0f;
constexpr float topRulerHeight = 24.0f;
constexpr float leftRulerWidth = 46.0f;

[[nodiscard]] int clampIndex(const int value, const int upperExclusive) noexcept
{
    return juce::jlimit(0, juce::jmax(0, upperExclusive - 1), value);
}

[[nodiscard]] bool isShortcutModifier(const juce::ModifierKeys modifiers) noexcept
{
    return modifiers.isCommandDown() || modifiers.isCtrlDown();
}

void drawHatch(juce::Graphics& graphics, juce::Rectangle<float> area, const int angleDegrees, const float spacing, const float alpha)
{
    if (area.isEmpty())
        return;

    juce::ignoreUnused(angleDegrees);
    const auto step = juce::jmax(12.0f, spacing);

    graphics.saveState();
    graphics.reduceClipRegion(area.toNearestInt());

    graphics.setColour(juce::Colour::fromRGB(0, 92, 255).withAlpha(alpha));
    for (auto y = area.getY(); y < area.getBottom(); y += step)
        graphics.drawHorizontalLine(juce::roundToInt(y), area.getX(), area.getRight());

    graphics.setColour(juce::Colour::fromRGB(255, 54, 46).withAlpha(alpha * 0.6f));
    for (auto x = area.getX(); x < area.getRight(); x += step * 2.0f)
        graphics.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());

    graphics.restoreState();
}

[[nodiscard]] juce::Colour colourForGlyph(const char glyph, const float alpha = 1.0f)
{
    if (glyph == '.')
        return juce::Colour::fromRGB(20, 24, 28).withAlpha(alpha * 0.42f);

    if (glyph >= '0' && glyph <= '9')
        return juce::Colour::fromRGB(255, 218, 0).withAlpha(alpha);

    if (glyph >= 'A' && glyph <= 'Z')
        return juce::Colour::fromRGB(0, 92, 255).withAlpha(alpha);

    if (glyph >= 'a' && glyph <= 'z')
        return juce::Colour::fromRGB(0, 190, 78).withAlpha(alpha);

    if (glyph == '*' || glyph == ':' || glyph == ';')
        return juce::Colour::fromRGB(255, 54, 46).withAlpha(alpha);

    return juce::Colour::fromRGB(18, 20, 22).withAlpha(alpha);
}
}

GridEditorComponent::GridEditorComponent(GridModel& modelToDisplay)
    : model(modelToDisplay)
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
}

void GridEditorComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(theme.background);

    const auto layout = getLayout();
    const auto snapshot = model.createSnapshot();
    const auto cellWidth = getCellWidth();
    const auto cellHeight = getCellHeight();

    graphics.setColour(theme.viewportBackground);
    graphics.fillRect(layout.outer);
    drawHatch(graphics, layout.outer, hatchAngle, 18.0f, 0.08f);

    if (rulersVisible)
    {
        graphics.setColour(theme.rulerBackground);
        graphics.fillRect(layout.topRuler);
        graphics.fillRect(layout.leftRuler);
        graphics.fillRect(layout.cornerRuler);
    }

    graphics.saveState();
    graphics.reduceClipRegion(layout.gridViewport.toNearestInt());

    graphics.setColour(theme.viewportBackground);
    graphics.fillRect(layout.gridViewport);
    drawHatch(graphics, layout.gridViewport, hatchAngle, 18.0f, 0.04f);

    const auto firstColumn = juce::jlimit(0,
                                         snapshot.width,
                                         static_cast<int>(std::floor(scrollOffset.x / cellWidth)));
    const auto lastColumn = juce::jlimit(0,
                                        snapshot.width,
                                        static_cast<int>(std::ceil((scrollOffset.x + layout.gridViewport.getWidth()) / cellWidth)) + 1);
    const auto firstRow = juce::jlimit(0,
                                      snapshot.height,
                                      static_cast<int>(std::floor(scrollOffset.y / cellHeight)));
    const auto lastRow = juce::jlimit(0,
                                     snapshot.height,
                                     static_cast<int>(std::ceil((scrollOffset.y + layout.gridViewport.getHeight()) / cellHeight)) + 1);

    if (playheadRow.has_value() && *playheadRow >= firstRow && *playheadRow < lastRow)
    {
        const auto rowBounds = juce::Rectangle<float>(layout.gridViewport.getX(),
                                                     layout.gridViewport.getY() - scrollOffset.y + static_cast<float>(*playheadRow) * cellHeight,
                                                     layout.gridViewport.getWidth(),
                                                     cellHeight);
        graphics.setColour(theme.playhead);
        graphics.fillRect(rowBounds);
    }

    if (hasSelection())
    {
        const auto selected = getSelectedCells();
        const auto selectionBounds = getCellBounds(selected.getX(), selected.getY())
                                         .getUnion(getCellBounds(selected.getRight() - 1, selected.getBottom() - 1));

        graphics.setColour(theme.selection);
        graphics.fillRect(selectionBounds);
        graphics.setColour(theme.selectionBorder);
        graphics.drawRect(selectionBounds, 1.0f);
    }

    graphics.setColour(theme.gridLine);

    for (int column = firstColumn; column <= lastColumn; ++column)
    {
        const auto x = layout.gridViewport.getX() - scrollOffset.x + static_cast<float>(column) * cellWidth;
        graphics.drawVerticalLine(juce::roundToInt(x), layout.gridViewport.getY(), layout.gridViewport.getBottom());
    }

    for (int row = firstRow; row <= lastRow; ++row)
    {
        const auto y = layout.gridViewport.getY() - scrollOffset.y + static_cast<float>(row) * cellHeight;
        graphics.drawHorizontalLine(juce::roundToInt(y), layout.gridViewport.getX(), layout.gridViewport.getRight());
    }

    const auto cursorBounds = getCellBounds(cursor.column, cursor.row).reduced(1.0f);

    if (hasKeyboardFocus(true))
    {
        graphics.setColour(theme.cursor.withAlpha(0.25f));
        graphics.fillRect(cursorBounds);
        graphics.setColour(theme.cursor);
        graphics.drawRect(cursorBounds, 1.5f);
    }
    else
    {
        graphics.setColour(theme.mutedText);
        graphics.drawRect(cursorBounds, 1.0f);
    }

    graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                       juce::jlimit(9.0f, 32.0f, cellHeight * 0.68f),
                                       juce::Font::bold));

    for (int row = firstRow; row < lastRow; ++row)
    {
        for (int column = firstColumn; column < lastColumn; ++column)
        {
            const auto cellBounds = getCellBounds(column, row).toNearestInt();
            const auto isCursor = column == cursor.column && row == cursor.row && hasKeyboardFocus(true);
            const auto glyph = snapshot.getGlyph(column, row);

            if (glyph != '.')
            {
                graphics.setColour(colourForGlyph(glyph, 0.34f));
                graphics.fillRect(cellBounds.reduced(1));
            }

            graphics.setColour(isCursor ? theme.cursorText
                                        : (glyph == '.' ? theme.mutedText : theme.text));
            graphics.drawText(juce::String::charToString(static_cast<juce::juce_wchar>(glyph)),
                              cellBounds,
                              juce::Justification::centred,
                              false);
        }
    }

    graphics.restoreState();

    if (! rulersVisible)
        return;

    graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    graphics.setColour(theme.rulerText);

    const auto columnLabelStep = cellWidth >= 18.0f ? 1 : (cellWidth >= 10.0f ? 4 : 8);

    graphics.saveState();
    graphics.reduceClipRegion(layout.topRuler.toNearestInt());

    for (int column = firstColumn; column < lastColumn; column += columnLabelStep)
    {
        const auto labelBounds = juce::Rectangle<float>(layout.gridViewport.getX() - scrollOffset.x + static_cast<float>(column) * cellWidth,
                                                        layout.topRuler.getY(),
                                                        cellWidth * static_cast<float>(columnLabelStep),
                                                        layout.topRuler.getHeight());
        graphics.drawText(juce::String(column + 1).paddedLeft('0', 2),
                          labelBounds.toNearestInt(),
                          juce::Justification::centred,
                          false);
    }

    graphics.restoreState();
    graphics.saveState();
    graphics.reduceClipRegion(layout.leftRuler.toNearestInt());

    for (int row = firstRow; row < lastRow; ++row)
    {
        const auto labelBounds = juce::Rectangle<float>(layout.leftRuler.getX(),
                                                       layout.gridViewport.getY() - scrollOffset.y + static_cast<float>(row) * cellHeight,
                                                       layout.leftRuler.getWidth() - 6.0f,
                                                       cellHeight);
        graphics.drawText(juce::String(row + 1).paddedLeft('0', 2),
                          labelBounds.toNearestInt(),
                          juce::Justification::centredRight,
                          false);
    }

    graphics.restoreState();
}

void GridEditorComponent::resized()
{
    clampScrollOffset();
}

bool GridEditorComponent::keyPressed(const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
    const auto shortcut = isShortcutModifier(modifiers);
    const auto shortcutCharacter = toShortcutCharacter(key);
    const auto keyCode = key.getKeyCode();

    if (shortcut)
    {
        if (shortcutCharacter == 'c')
        {
            copySelectionToClipboard();
            return true;
        }

        if (shortcutCharacter == 'x')
        {
            cutSelectionToClipboard();
            return true;
        }

        if (shortcutCharacter == 'v')
        {
            pasteFromClipboard();
            return true;
        }

        if (shortcutCharacter == 'a')
        {
            selectAll();
            return true;
        }

        if (shortcutCharacter == 'r')
        {
            toggleRulers();
            return true;
        }

        if (shortcutCharacter == 'k' || keyCode == juce::KeyPress::deleteKey || keyCode == juce::KeyPress::backspaceKey)
        {
            clearGrid();
            return true;
        }

        if (shortcutCharacter == '+' || shortcutCharacter == '=')
        {
            zoomIn();
            return true;
        }

        if (shortcutCharacter == '-' || shortcutCharacter == '_')
        {
            zoomOut();
            return true;
        }

        if (shortcutCharacter == '0')
        {
            resetZoom();
            return true;
        }
    }

    const auto extendingSelection = modifiers.isShiftDown();

    if (keyCode == juce::KeyPress::leftKey)
    {
        moveCursor(-1, 0, extendingSelection);
        return true;
    }

    if (keyCode == juce::KeyPress::rightKey)
    {
        moveCursor(1, 0, extendingSelection);
        return true;
    }

    if (keyCode == juce::KeyPress::upKey)
    {
        moveCursor(0, -1, extendingSelection);
        return true;
    }

    if (keyCode == juce::KeyPress::downKey)
    {
        moveCursor(0, 1, extendingSelection);
        return true;
    }

    if (keyCode == juce::KeyPress::homeKey)
    {
        setCursor({ shortcut ? 0 : 0, shortcut ? 0 : cursor.row }, extendingSelection);
        return true;
    }

    if (keyCode == juce::KeyPress::endKey)
    {
        setCursor({ model.getWidth() - 1, shortcut ? model.getHeight() - 1 : cursor.row }, extendingSelection);
        return true;
    }

    if (keyCode == juce::KeyPress::pageUpKey)
    {
        moveCursor(0, -8, extendingSelection);
        return true;
    }

    if (keyCode == juce::KeyPress::pageDownKey)
    {
        moveCursor(0, 8, extendingSelection);
        return true;
    }

    if (keyCode == juce::KeyPress::backspaceKey)
    {
        clearSelectionOrCell(true);
        return true;
    }

    if (keyCode == juce::KeyPress::deleteKey)
    {
        clearSelectionOrCell(false);
        return true;
    }

    if (keyCode == juce::KeyPress::returnKey)
    {
        setCursor({ 0, cursor.row + 1 }, false);
        return true;
    }

    if (keyCode == juce::KeyPress::tabKey)
    {
        moveCursor(modifiers.isShiftDown() ? -1 : 1, 0, false);
        return true;
    }

    if (keyCode == juce::KeyPress::escapeKey)
    {
        selectionAnchor.reset();
        repaint();
        return true;
    }

    if (! modifiers.isCommandDown() && ! modifiers.isCtrlDown() && isEditableAscii(key.getTextCharacter()))
    {
        insertCharacter(static_cast<char>(key.getTextCharacter()));
        return true;
    }

    return false;
}

void GridEditorComponent::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    if (event.mods.isRightButtonDown())
    {
        rightDragPanAnchor = event.position;
        mouseSelectionAnchor.reset();
        return;
    }

    if (auto cell = cellAt(event.position))
    {
        mouseSelectionAnchor = *cell;
        setCursor(*cell, event.mods.isShiftDown());

        if (! event.mods.isShiftDown())
            selectionAnchor = *cell;
    }
}

void GridEditorComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (rightDragPanAnchor.has_value() && event.mods.isRightButtonDown())
    {
        const auto delta = event.position - *rightDragPanAnchor;
        scrollBy(-delta.x, -delta.y);
        rightDragPanAnchor = event.position;
        return;
    }

    if (! mouseSelectionAnchor.has_value())
        return;

    if (auto cell = cellAt(event.position))
    {
        selectionAnchor = mouseSelectionAnchor;
        setCursor(*cell, true);
    }
}

void GridEditorComponent::mouseUp(const juce::MouseEvent&)
{
    mouseSelectionAnchor.reset();
    rightDragPanAnchor.reset();
}

void GridEditorComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (getLayout().gridViewport.contains(event.position))
    {
        const auto direction = wheel.deltaY == 0.0f ? -wheel.deltaX : wheel.deltaY;

        if (direction != 0.0f)
            zoomAt(zoom * std::pow(zoomStep, direction > 0.0f ? 1.0f : -1.0f), event.position);

        return;
    }

    if (isShortcutModifier(event.mods))
    {
        wheel.deltaY > 0.0f ? zoomIn() : zoomOut();
        return;
    }
}

void GridEditorComponent::setTheme(Theme newTheme)
{
    theme = std::move(newTheme);
    repaint();
}

const GridEditorComponent::Theme& GridEditorComponent::getTheme() const noexcept
{
    return theme;
}

void GridEditorComponent::setHatchAngle(const int degrees)
{
    hatchAngle = degrees;
    repaint();
}

void GridEditorComponent::setRulersVisible(const bool shouldShowRulers)
{
    if (rulersVisible == shouldShowRulers)
        return;

    rulersVisible = shouldShowRulers;
    clampScrollOffset();
    repaint();
}

bool GridEditorComponent::areRulersVisible() const noexcept
{
    return rulersVisible;
}

void GridEditorComponent::clearGrid()
{
    model.clear();
    cursor = {};
    selectionAnchor.reset();
    scrollOffset = {};
    repaint();
}

void GridEditorComponent::zoomIn()
{
    setZoom(zoom * zoomStep);
}

void GridEditorComponent::zoomOut()
{
    setZoom(zoom / zoomStep);
}

void GridEditorComponent::resetZoom()
{
    setZoom(defaultZoom);
}

void GridEditorComponent::fitToView()
{
    const auto layout = getLayout();
    const auto contentWidth = static_cast<float>(model.getWidth()) * baseCellWidth;
    const auto contentHeight = static_cast<float>(model.getHeight()) * baseCellHeight;

    if (contentWidth <= 0.0f || contentHeight <= 0.0f
        || layout.gridViewport.getWidth() <= 0.0f || layout.gridViewport.getHeight() <= 0.0f)
    {
        clampScrollOffset();
        return;
    }

    const auto fitZoom = juce::jmin(layout.gridViewport.getWidth() / contentWidth,
                                    layout.gridViewport.getHeight() / contentHeight);
    setZoom(juce::jmin(defaultZoom, fitZoom * defaultZoom));
    scrollOffset = {};
    clampScrollOffset();
    repaint();
}

void GridEditorComponent::setPlayheadRow(std::optional<int> row)
{
    if (row.has_value())
        row = juce::jlimit(0, model.getHeight() - 1, *row);

    if (playheadRow == row)
        return;

    const auto previous = playheadRow;
    playheadRow = row;

    if (previous.has_value())
        repaintRow(*previous);

    if (playheadRow.has_value())
        repaintRow(*playheadRow);
}

void GridEditorComponent::clearPlayhead()
{
    setPlayheadRow(std::nullopt);
}

GridEditorComponent::Layout GridEditorComponent::getLayout() const
{
    Layout layout;
    layout.outer = getLocalBounds().toFloat().reduced(outerPadding);

    auto content = layout.outer.reduced(1.0f);

    if (rulersVisible)
    {
        layout.cornerRuler = content.removeFromLeft(leftRulerWidth).removeFromTop(topRulerHeight);
        layout.leftRuler = juce::Rectangle<float>(layout.outer.getX() + 1.0f,
                                                  layout.outer.getY() + topRulerHeight + 1.0f,
                                                  leftRulerWidth,
                                                  layout.outer.getHeight() - topRulerHeight - 2.0f);
        layout.topRuler = juce::Rectangle<float>(layout.outer.getX() + leftRulerWidth + 1.0f,
                                                 layout.outer.getY() + 1.0f,
                                                 layout.outer.getWidth() - leftRulerWidth - 2.0f,
                                                 topRulerHeight);
        layout.gridViewport = juce::Rectangle<float>(layout.topRuler.getX(),
                                                     layout.leftRuler.getY(),
                                                     layout.topRuler.getWidth(),
                                                     layout.leftRuler.getHeight());
    }
    else
    {
        layout.gridViewport = content;
    }

    return layout;
}

float GridEditorComponent::getCellWidth() const noexcept
{
    return baseCellWidth * zoom;
}

float GridEditorComponent::getCellHeight() const noexcept
{
    return baseCellHeight * zoom;
}

juce::Rectangle<float> GridEditorComponent::getCellBounds(const int column, const int row) const
{
    const auto layout = getLayout();
    const auto cellWidth = getCellWidth();
    const auto cellHeight = getCellHeight();

    return { layout.gridViewport.getX() - scrollOffset.x + static_cast<float>(column) * cellWidth,
             layout.gridViewport.getY() - scrollOffset.y + static_cast<float>(row) * cellHeight,
             cellWidth,
             cellHeight };
}

std::optional<GridEditorComponent::CellPosition> GridEditorComponent::cellAt(const juce::Point<float> position) const
{
    const auto layout = getLayout();

    if (! layout.gridViewport.contains(position))
        return std::nullopt;

    const auto column = static_cast<int>(std::floor((position.x - layout.gridViewport.getX() + scrollOffset.x) / getCellWidth()));
    const auto row = static_cast<int>(std::floor((position.y - layout.gridViewport.getY() + scrollOffset.y) / getCellHeight()));

    if (! model.isInBounds(column, row))
        return std::nullopt;

    return CellPosition { column, row };
}

juce::Rectangle<int> GridEditorComponent::getSelectedCells() const noexcept
{
    const auto anchor = selectionAnchor.value_or(cursor);
    const auto left = juce::jmin(anchor.column, cursor.column);
    const auto top = juce::jmin(anchor.row, cursor.row);
    const auto right = juce::jmax(anchor.column, cursor.column);
    const auto bottom = juce::jmax(anchor.row, cursor.row);

    return { left, top, right - left + 1, bottom - top + 1 };
}

bool GridEditorComponent::hasSelection() const noexcept
{
    return selectionAnchor.has_value();
}

void GridEditorComponent::moveCursor(const int deltaColumns, const int deltaRows, const bool extendingSelection)
{
    setCursor({ cursor.column + deltaColumns, cursor.row + deltaRows }, extendingSelection);
}

void GridEditorComponent::setCursor(CellPosition position, const bool extendingSelection)
{
    position.column = clampIndex(position.column, model.getWidth());
    position.row = clampIndex(position.row, model.getHeight());

    if (extendingSelection)
    {
        if (! selectionAnchor.has_value())
            selectionAnchor = cursor;
    }
    else
    {
        selectionAnchor.reset();
    }

    cursor = position;
    ensureCursorVisible();
    repaint();
}

void GridEditorComponent::insertCharacter(const char character)
{
    const auto target = hasSelection() ? CellPosition { getSelectedCells().getX(), getSelectedCells().getY() } : cursor;

    if (hasSelection())
        clearCells(getSelectedCells());

    selectionAnchor.reset();
    cursor = target;
    model.setGlyph(cursor.column, cursor.row, character);

    if (cursor.column + 1 < model.getWidth())
        setCursor({ cursor.column + 1, cursor.row }, false);
    else if (cursor.row + 1 < model.getHeight())
        setCursor({ 0, cursor.row + 1 }, false);
    else
        repaint();
}

void GridEditorComponent::clearSelectionOrCell(const bool moveLeftAfterClearing)
{
    if (hasSelection())
    {
        const auto selected = getSelectedCells();
        clearCells(selected);
        selectionAnchor.reset();
        setCursor({ selected.getX(), selected.getY() }, false);
        return;
    }

    model.setGlyph(cursor.column, cursor.row, GridModel::emptyGlyph);

    if (! moveLeftAfterClearing)
    {
        repaint();
        return;
    }

    if (cursor.column > 0)
    {
        setCursor({ cursor.column - 1, cursor.row }, false);
        return;
    }

    if (cursor.row > 0)
    {
        setCursor({ model.getWidth() - 1, cursor.row - 1 }, false);
        return;
    }

    repaint();
}

void GridEditorComponent::clearCells(const juce::Rectangle<int>& cellsToClear)
{
    for (int row = cellsToClear.getY(); row < cellsToClear.getBottom(); ++row)
        for (int column = cellsToClear.getX(); column < cellsToClear.getRight(); ++column)
            model.setGlyph(column, row, GridModel::emptyGlyph);
}

void GridEditorComponent::copySelectionToClipboard() const
{
    const auto selected = getSelectedCells();
    juce::String clipboardText;

    for (int row = selected.getY(); row < selected.getBottom(); ++row)
    {
        for (int column = selected.getX(); column < selected.getRight(); ++column)
            clipboardText += juce::String::charToString(static_cast<juce::juce_wchar>(model.getGlyph(column, row)));

        if (row + 1 < selected.getBottom())
            clipboardText += "\n";
    }

    juce::SystemClipboard::copyTextToClipboard(clipboardText);
}

void GridEditorComponent::cutSelectionToClipboard()
{
    copySelectionToClipboard();
    clearSelectionOrCell(false);
}

void GridEditorComponent::pasteFromClipboard()
{
    auto lines = splitClipboardLines(juce::SystemClipboard::getTextFromClipboard());

    if (lines.empty())
        return;

    const auto start = hasSelection() ? CellPosition { getSelectedCells().getX(), getSelectedCells().getY() } : cursor;
    int pastedWidth = 0;
    int pastedHeight = 0;

    if (hasSelection())
        clearCells(getSelectedCells());

    for (int row = 0; row < static_cast<int>(lines.size()); ++row)
    {
        const auto targetRow = start.row + row;

        if (targetRow >= model.getHeight())
            break;

        pastedHeight = row + 1;

        for (int index = 0; index < lines[static_cast<std::size_t>(row)].length(); ++index)
        {
            const auto character = lines[static_cast<std::size_t>(row)][index];
            const auto targetColumn = start.column + index;

            if (targetColumn >= model.getWidth())
                break;

            if (isEditableAscii(character))
            {
                model.setGlyph(targetColumn, targetRow, static_cast<char>(character));
                pastedWidth = juce::jmax(pastedWidth, index + 1);
            }
        }
    }

    selectionAnchor = start;
    cursor = { clampIndex(start.column + juce::jmax(0, pastedWidth - 1), model.getWidth()),
               clampIndex(start.row + juce::jmax(0, pastedHeight - 1), model.getHeight()) };

    ensureCursorVisible();
    repaint();
}

void GridEditorComponent::selectAll()
{
    selectionAnchor = CellPosition {};
    cursor = { model.getWidth() - 1, model.getHeight() - 1 };
    ensureCursorVisible();
    repaint();
}

void GridEditorComponent::toggleRulers()
{
    setRulersVisible(! rulersVisible);
}

void GridEditorComponent::setZoom(const float newZoom)
{
    zoom = juce::jlimit(minimumZoom, maximumZoom, newZoom);
    clampScrollOffset();
    ensureCursorVisible();
    repaint();
}

void GridEditorComponent::zoomAt(const float newZoom, juce::Point<float> anchor)
{
    const auto layout = getLayout();

    if (! layout.gridViewport.contains(anchor))
        anchor = layout.gridViewport.getCentre();

    const auto oldZoom = zoom;
    const auto clampedZoom = juce::jlimit(minimumZoom, maximumZoom, newZoom);

    if (std::abs(oldZoom - clampedZoom) < 0.0001f)
        return;

    const auto localAnchor = anchor - layout.gridViewport.getPosition();
    const auto scale = clampedZoom / oldZoom;
    const auto contentAnchor = scrollOffset + localAnchor;

    zoom = clampedZoom;
    scrollOffset = contentAnchor * scale - localAnchor;
    clampScrollOffset();
    repaint();
}

void GridEditorComponent::scrollBy(const float deltaX, const float deltaY)
{
    scrollOffset.x += deltaX;
    scrollOffset.y += deltaY;
    clampScrollOffset();
    repaint();
}

void GridEditorComponent::clampScrollOffset()
{
    const auto layout = getLayout();
    const auto contentWidth = static_cast<float>(model.getWidth()) * getCellWidth();
    const auto contentHeight = static_cast<float>(model.getHeight()) * getCellHeight();
    const auto maxX = juce::jmax(0.0f, contentWidth - layout.gridViewport.getWidth());
    const auto maxY = juce::jmax(0.0f, contentHeight - layout.gridViewport.getHeight());

    scrollOffset.x = juce::jlimit(0.0f, maxX, scrollOffset.x);
    scrollOffset.y = juce::jlimit(0.0f, maxY, scrollOffset.y);
}

void GridEditorComponent::ensureCursorVisible()
{
    const auto layout = getLayout();
    const auto cellWidth = getCellWidth();
    const auto cellHeight = getCellHeight();
    const auto cursorLeft = static_cast<float>(cursor.column) * cellWidth;
    const auto cursorRight = cursorLeft + cellWidth;
    const auto cursorTop = static_cast<float>(cursor.row) * cellHeight;
    const auto cursorBottom = cursorTop + cellHeight;

    if (cursorLeft < scrollOffset.x)
        scrollOffset.x = cursorLeft;
    else if (cursorRight > scrollOffset.x + layout.gridViewport.getWidth())
        scrollOffset.x = cursorRight - layout.gridViewport.getWidth();

    if (cursorTop < scrollOffset.y)
        scrollOffset.y = cursorTop;
    else if (cursorBottom > scrollOffset.y + layout.gridViewport.getHeight())
        scrollOffset.y = cursorBottom - layout.gridViewport.getHeight();

    clampScrollOffset();
}

void GridEditorComponent::repaintRow(const int row)
{
    if (row < 0 || row >= model.getHeight())
        return;

    const auto layout = getLayout();
    const auto cellHeight = getCellHeight();
    const auto y = layout.gridViewport.getY() - scrollOffset.y + static_cast<float>(row) * cellHeight;
    auto rowBounds = juce::Rectangle<float>(layout.gridViewport.getX(),
                                            y,
                                            layout.gridViewport.getWidth(),
                                            cellHeight).expanded(2.0f, 1.0f)
                                                        .getIntersection(layout.gridViewport);

    if (! rowBounds.isEmpty())
        repaint(rowBounds.toNearestInt());
}

bool GridEditorComponent::isEditableAscii(const juce::juce_wchar character) noexcept
{
    return character >= 32 && character <= 126;
}

char GridEditorComponent::toShortcutCharacter(const juce::KeyPress& key) noexcept
{
    auto character = key.getTextCharacter();

    if (character == 0 && key.getKeyCode() >= 0 && key.getKeyCode() <= 127)
        character = static_cast<juce::juce_wchar>(key.getKeyCode());

    if (character >= 'A' && character <= 'Z')
        character = static_cast<juce::juce_wchar>(character - 'A' + 'a');

    return character >= 0 && character <= 127 ? static_cast<char>(character) : '\0';
}

std::vector<juce::String> GridEditorComponent::splitClipboardLines(const juce::String& text)
{
    std::vector<juce::String> lines;
    juce::String currentLine;

    for (int index = 0; index < text.length(); ++index)
    {
        const auto character = text[index];

        if (character == '\r' || character == '\n')
        {
            if (character == '\r' && index + 1 < text.length() && text[index + 1] == '\n')
                ++index;

            lines.push_back(currentLine);
            currentLine = {};
            continue;
        }

        currentLine += juce::String::charToString(character);
    }

    if (text.isNotEmpty() && text.getLastCharacter() != '\r' && text.getLastCharacter() != '\n')
        lines.push_back(currentLine);

    return lines;
}
}
