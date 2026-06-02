#include "StatusLog.h"

namespace gridcollider
{
void StatusLog::append(juce::String message)
{
    messages.push_back(std::move(message));

    if (messages.size() > maxMessages)
        messages.erase(messages.begin(), messages.begin() + static_cast<std::ptrdiff_t>(messages.size() - maxMessages));
}

juce::String StatusLog::getLatestMessage() const
{
    if (messages.empty())
        return {};

    return messages.back();
}

const std::vector<juce::String>& StatusLog::getMessages() const noexcept
{
    return messages;
}
}
