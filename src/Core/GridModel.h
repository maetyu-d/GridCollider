#pragma once

#include <juce_core/juce_core.h>

#include <mutex>
#include <vector>

namespace gridcollider
{
class GridModel
{
public:
    struct Snapshot
    {
        int width = 0;
        int height = 0;
        std::vector<char> cells;

        [[nodiscard]] bool isInBounds(int column, int row) const noexcept;
        [[nodiscard]] char getGlyph(int column, int row) const noexcept;
    };

    static constexpr int defaultWidth = 32;
    static constexpr int defaultHeight = 32;
    static constexpr char emptyGlyph = '.';

    GridModel();
    GridModel(int columns, int rows);

    void resize(int columns, int rows);
    void clear(char fillCharacter = emptyGlyph);

    [[nodiscard]] int getWidth() const noexcept;
    [[nodiscard]] int getHeight() const noexcept;
    [[nodiscard]] bool isInBounds(int column, int row) const noexcept;
    [[nodiscard]] char getGlyph(int column, int row) const noexcept;
    [[nodiscard]] juce::String getRowText(int row) const;
    [[nodiscard]] Snapshot createSnapshot() const;

    void applySnapshot(const Snapshot& snapshot);
    void setGlyph(int column, int row, char glyph);

private:
    [[nodiscard]] std::size_t indexFor(int column, int row) const noexcept;
    [[nodiscard]] static char normaliseGlyph(char glyph) noexcept;

    int width = defaultWidth;
    int height = defaultHeight;
    std::vector<char> cells;
    mutable std::mutex cellsMutex;
};
}
