#include "PresetManager.h"

#include "../Core/GridModel.h"

namespace gridcollider
{
juce::Result PresetManager::load(const juce::File& file, GridModel& model)
{
    if (! file.existsAsFile())
        return juce::Result::fail("File does not exist: " + file.getFullPathName());

    const auto text = file.loadFileAsString();

    if (text.isEmpty())
        return juce::Result::fail("File is empty: " + file.getFullPathName());

    model.clear();

    auto lines = juce::StringArray::fromLines(text);
    const auto rows = juce::jmin(model.getHeight(), lines.size());

    for (int row = 0; row < rows; ++row)
    {
        const auto line = lines[row];
        const auto columns = juce::jmin(model.getWidth(), line.length());

        for (int column = 0; column < columns; ++column)
        {
            const auto character = line[column];
            model.setGlyph(column, row, character >= 32 && character <= 126
                                            ? static_cast<char>(character)
                                            : GridModel::emptyGlyph);
        }
    }

    return juce::Result::ok();
}

juce::Result PresetManager::save(const juce::File& file, const GridModel& model) const
{
    juce::String text;

    for (int row = 0; row < model.getHeight(); ++row)
    {
        text += model.getRowText(row);

        if (row + 1 < model.getHeight())
            text += "\n";
    }

    if (file.replaceWithText(text))
        return juce::Result::ok();

    return juce::Result::fail("Could not write file: " + file.getFullPathName());
}

juce::Array<juce::File> PresetManager::findExampleFiles() const
{
    juce::Array<juce::File> files;
    const auto directory = getExamplesDirectory();

    if (directory.isDirectory())
    {
        directory.findChildFiles(files, juce::File::findFiles, false, "*.orca");
        directory.findChildFiles(files, juce::File::findFiles, false, "*.gridcollider");
    }

    files.sort();
    return files;
}

juce::File PresetManager::getExamplesDirectory() const
{
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);

   #if JUCE_MAC
    const auto bundleResources = executable.getParentDirectory().getParentDirectory().getChildFile("Resources").getChildFile("examples");

    if (bundleResources.isDirectory())
        return bundleResources;
   #endif

    const auto cwdExamples = juce::File::getCurrentWorkingDirectory().getChildFile("examples");

    if (cwdExamples.isDirectory())
        return cwdExamples;

    return executable.getParentDirectory().getChildFile("examples");
}
}
