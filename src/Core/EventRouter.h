#pragma once

#include "GridModel.h"
#include "InternalEvent.h"

#include <vector>

namespace gridcollider
{
class OscOutput;

class EventRouter
{
public:
    [[nodiscard]] std::vector<LogEvent> route(const std::vector<InternalEvent>& events,
                                              const GridModel::Snapshot& grid,
                                              OscOutput& oscOutput) const;

private:
    [[nodiscard]] LogEvent makeLogEvent(const InternalEvent& event) const;
    [[nodiscard]] juce::String describe(const InternalEvent& event) const;
    void sendOsc(const InternalEvent& event, const GridModel::Snapshot& grid, OscOutput& oscOutput) const;
};
}
