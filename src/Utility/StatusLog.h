#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace gridcollider
{
class StatusLog
{
public:
    static constexpr std::size_t maxMessages = 256;

    void append(juce::String message);

    [[nodiscard]] juce::String getLatestMessage() const;
    [[nodiscard]] const std::vector<juce::String>& getMessages() const noexcept;

private:
    std::vector<juce::String> messages;
};
}
