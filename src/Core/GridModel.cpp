#include "GridModel.h"

#include <algorithm>

namespace gridcollider
{
bool GridModel::Snapshot::isInBounds(const int column, const int row) const noexcept
{
    return column >= 0 && column < width && row >= 0 && row < height;
}

char GridModel::Snapshot::getGlyph(const int column, const int row) const noexcept
{
    if (! isInBounds(column, row))
        return GridModel::emptyGlyph;

    return cells[static_cast<std::size_t>(column + row * width)];
}

GridModel::GridModel()
    : GridModel(defaultWidth, defaultHeight)
{
}

GridModel::GridModel(const int columns, const int rows)
{
    resize(columns, rows);
}

void GridModel::resize(const int columns, const int rows)
{
    const std::lock_guard lock(cellsMutex);
    width = juce::jmax(1, columns);
    height = juce::jmax(1, rows);
    cells.assign(static_cast<std::size_t>(width * height), emptyGlyph);
}

void GridModel::clear(const char fillCharacter)
{
    const std::lock_guard lock(cellsMutex);
    cells.assign(static_cast<std::size_t>(width * height), normaliseGlyph(fillCharacter));
}

int GridModel::getWidth() const noexcept
{
    return width;
}

int GridModel::getHeight() const noexcept
{
    return height;
}

bool GridModel::isInBounds(const int column, const int row) const noexcept
{
    return column >= 0 && column < width && row >= 0 && row < height;
}

char GridModel::getGlyph(const int column, const int row) const noexcept
{
    if (! isInBounds(column, row))
        return emptyGlyph;

    const std::lock_guard lock(cellsMutex);
    return cells[indexFor(column, row)];
}

juce::String GridModel::getRowText(const int row) const
{
    if (row < 0 || row >= height)
        return {};

    const std::lock_guard lock(cellsMutex);
    juce::String text;

    for (int column = 0; column < width; ++column)
        text += juce::String::charToString(static_cast<juce::juce_wchar>(cells[indexFor(column, row)]));

    return text;
}

GridModel::Snapshot GridModel::createSnapshot() const
{
    const std::lock_guard lock(cellsMutex);
    return { width, height, cells };
}

void GridModel::applySnapshot(const Snapshot& snapshot)
{
    if (snapshot.width <= 0 || snapshot.height <= 0)
        return;

    const std::lock_guard lock(cellsMutex);
    width = snapshot.width;
    height = snapshot.height;
    cells = snapshot.cells;

    if (cells.size() != static_cast<std::size_t>(width * height))
        cells.assign(static_cast<std::size_t>(width * height), emptyGlyph);
}

void GridModel::setGlyph(const int column, const int row, const char glyph)
{
    if (! isInBounds(column, row))
        return;

    const std::lock_guard lock(cellsMutex);
    cells[indexFor(column, row)] = normaliseGlyph(glyph);
}

std::size_t GridModel::indexFor(const int column, const int row) const noexcept
{
    return static_cast<std::size_t>(column + row * width);
}

char GridModel::normaliseGlyph(const char glyph) noexcept
{
    const auto value = static_cast<unsigned char>(glyph);

    if (value < 32 || value > 126)
        return emptyGlyph;

    return glyph;
}
}
