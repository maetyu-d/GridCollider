#pragma once

#include <juce_core/juce_core.h>

namespace gridcollider
{
class GridModel;

class PresetManager
{
public:
    [[nodiscard]] juce::Result load(const juce::File& file, GridModel& model);
    [[nodiscard]] juce::Result save(const juce::File& file, const GridModel& model) const;

    [[nodiscard]] juce::Array<juce::File> findExampleFiles() const;
    [[nodiscard]] juce::File getExamplesDirectory() const;
};
}
