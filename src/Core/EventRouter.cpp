#include "EventRouter.h"

#include "../IO/OscOutput.h"

#include <type_traits>

namespace gridcollider
{
namespace
{
[[nodiscard]] juce::String cellText(const CellCoordinate cell)
{
    return juce::String(cell.column + 1) + "," + juce::String(cell.row + 1);
}

[[nodiscard]] juce::String paramsText(const ParameterMap& parameters)
{
    if (parameters.empty())
        return {};

    juce::String result = " {";
    bool first = true;

    for (const auto& [key, value] : parameters)
    {
        if (! first)
            result += ", ";

        first = false;
        result += juce::String(key) + "=" + value;
    }

    result += "}";
    return result;
}
}

std::vector<LogEvent> EventRouter::route(const std::vector<InternalEvent>& events,
                                         const GridModel::Snapshot& grid,
                                         OscOutput& oscOutput) const
{
    std::vector<LogEvent> logs;
    logs.reserve(events.size());
    bool gridChanged = false;

    for (const auto& event : events)
    {
        sendOsc(event, grid, oscOutput);
        logs.push_back(makeLogEvent(event));

        if (std::holds_alternative<GridMutationEvent>(event))
            gridChanged = true;
    }

    if (gridChanged)
    {
        [[maybe_unused]] const auto gridSent = oscOutput.sendGridState(grid);
    }

    return logs;
}

void EventRouter::sendOsc(const InternalEvent& event, const GridModel::Snapshot& grid, OscOutput& oscOutput) const
{
    juce::ignoreUnused(grid);

    std::visit([&oscOutput](const auto& typedEvent)
    {
        using Event = std::decay_t<decltype(typedEvent)>;

        if constexpr (std::is_same_v<Event, NoteEvent>)
        {
            [[maybe_unused]] const auto sent = oscOutput.sendNoteEvent(typedEvent);
        }
        else if constexpr (std::is_same_v<Event, ControlEvent>)
        {
            [[maybe_unused]] const auto sent = oscOutput.sendControlEvent(typedEvent);
        }
        else if constexpr (std::is_same_v<Event, TriggerEvent>)
        {
            [[maybe_unused]] const auto sent = oscOutput.sendTriggerEvent(typedEvent);
        }
        else if constexpr (std::is_same_v<Event, BusRouteEvent>)
        {
            [[maybe_unused]] const auto sent = oscOutput.sendBusRouteEvent(typedEvent);
        }
    }, event);
}

LogEvent EventRouter::makeLogEvent(const InternalEvent& event) const
{
    return std::visit([this](const auto& typedEvent) -> LogEvent
    {
        if constexpr (std::is_same_v<std::decay_t<decltype(typedEvent)>, LogEvent>)
            return typedEvent;
        else
            return { typedEvent.fields, describe(typedEvent) };
    }, event);
}

juce::String EventRouter::describe(const InternalEvent& event) const
{
    return std::visit([](const auto& typedEvent) -> juce::String
    {
        using Event = std::decay_t<decltype(typedEvent)>;
        const auto& fields = typedEvent.fields;

        if constexpr (std::is_same_v<Event, NoteEvent>)
        {
            return "note"
                + juce::String(" tick ") + juce::String(fields.tick)
                + " @" + cellText(fields.sourceCell)
                + " inst " + fields.instrumentName
                + " pitch " + juce::String(fields.pitch)
                + " vel " + juce::String(fields.velocity, 2)
                + " dur " + juce::String(fields.durationTicks)
                + paramsText(fields.parameters);
        }
        else if constexpr (std::is_same_v<Event, ControlEvent>)
        {
            return "control"
                + juce::String(" tick ") + juce::String(fields.tick)
                + " @" + cellText(fields.sourceCell)
                + " inst " + fields.instrumentName
                + " " + typedEvent.parameterName
                + "=" + juce::String(typedEvent.value, 3)
                + paramsText(fields.parameters);
        }
        else if constexpr (std::is_same_v<Event, TriggerEvent>)
        {
            return "trigger"
                + juce::String(" tick ") + juce::String(fields.tick)
                + " @" + cellText(fields.sourceCell)
                + " " + typedEvent.triggerName
                + paramsText(fields.parameters);
        }
        else if constexpr (std::is_same_v<Event, BusRouteEvent>)
        {
            return "route"
                + juce::String(" tick ") + juce::String(fields.tick)
                + " @" + cellText(fields.sourceCell)
                + " inst " + fields.instrumentName
                + " -> " + fields.targetAddress.value_or("(none)")
                + " " + typedEvent.payload
                + paramsText(fields.parameters);
        }
        else if constexpr (std::is_same_v<Event, GridMutationEvent>)
        {
            return "grid"
                + juce::String(" tick ") + juce::String(fields.tick)
                + " @" + cellText(fields.sourceCell)
                + " -> " + cellText(typedEvent.targetCell)
                + " '" + juce::String::charToString(static_cast<juce::juce_wchar>(typedEvent.previousGlyph))
                + "' to '" + juce::String::charToString(static_cast<juce::juce_wchar>(typedEvent.newGlyph)) + "'";
        }
        else
        {
            return typedEvent.message;
        }
    }, event);
}
}
