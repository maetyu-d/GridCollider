#include "OscOutput.h"

#include <limits>

namespace gridcollider
{
OscOutput::OscOutput()
    : host("127.0.0.1")
{
}

OscOutput::~OscOutput()
{
    [[maybe_unused]] const auto disconnected = disconnect();
}

void OscOutput::setEndpoint(juce::String newHost, const int newPort)
{
    const auto wasConnected = connected;

    if (wasConnected)
    {
        [[maybe_unused]] const auto disconnected = disconnect();
    }

    host = std::move(newHost);
    port = juce::jlimit(1, 65535, newPort);

    if (wasConnected)
    {
        [[maybe_unused]] const auto reconnected = connect();
    }
}

bool OscOutput::connect()
{
    [[maybe_unused]] const auto disconnected = disconnect();
    connected = sender.connect(host, port);

    if (debugMode)
        juce::Logger::writeToLog("[GridCollider OSC] connect " + getEndpointDescription() + (connected ? " ok" : " failed"));

    return connected;
}

bool OscOutput::disconnect()
{
    const auto ok = sender.disconnect();
    connected = false;

    if (debugMode)
        juce::Logger::writeToLog("[GridCollider OSC] disconnect " + juce::String(ok ? "ok" : "failed"));

    return ok;
}

void OscOutput::setDebugMode(const bool shouldDebug)
{
    debugMode = shouldDebug;
}

bool OscOutput::isDebugModeEnabled() const noexcept
{
    return debugMode;
}

bool OscOutput::isConnected() const noexcept
{
    return connected;
}

const juce::String& OscOutput::getHost() const noexcept
{
    return host;
}

int OscOutput::getPort() const noexcept
{
    return port;
}

juce::String OscOutput::getEndpointDescription() const
{
    return host + ":" + juce::String(port);
}

juce::String OscOutput::getConnectionStatusText() const
{
    return connected ? "CONNECTED" : "DISCONNECTED";
}

bool OscOutput::sendNoteEvent(const NoteEvent& event)
{
    const auto& fields = event.fields;
    juce::OSCMessage message("/gc/note");
    message.addString(fields.instrumentName);
    message.addInt32(fields.pitch);
    message.addFloat32(fields.velocity);
    message.addInt32(static_cast<juce::int32>(juce::jmin<std::uint64_t>(fields.durationTicks, static_cast<std::uint64_t>(std::numeric_limits<juce::int32>::max()))));
    message.addInt32(fields.sourceCell.column);
    message.addInt32(fields.sourceCell.row);
    message.addInt32(static_cast<juce::int32>(juce::jmin<std::uint64_t>(fields.tick, static_cast<std::uint64_t>(std::numeric_limits<juce::int32>::max()))));

    return sendMessage(std::move(message),
                       "/gc/note " + fields.instrumentName
                           + " " + juce::String(fields.pitch)
                           + " " + juce::String(fields.velocity, 3)
                           + " " + juce::String(fields.durationTicks)
                           + " " + juce::String(fields.sourceCell.column)
                           + " " + juce::String(fields.sourceCell.row)
                           + " " + juce::String(fields.tick));
}

bool OscOutput::sendControlEvent(const ControlEvent& event)
{
    const auto& fields = event.fields;
    const auto target = fields.targetAddress.value_or(fields.instrumentName);
    juce::OSCMessage message("/gc/control");
    message.addString(target);
    message.addString(event.parameterName);
    message.addFloat32(event.value);
    message.addInt32(fields.sourceCell.column);
    message.addInt32(fields.sourceCell.row);
    message.addInt32(static_cast<juce::int32>(juce::jmin<std::uint64_t>(fields.tick, static_cast<std::uint64_t>(std::numeric_limits<juce::int32>::max()))));

    return sendMessage(std::move(message),
                       "/gc/control " + target
                           + " " + event.parameterName
                           + " " + juce::String(event.value, 3)
                           + " " + juce::String(fields.sourceCell.column)
                           + " " + juce::String(fields.sourceCell.row)
                           + " " + juce::String(fields.tick));
}

bool OscOutput::sendTriggerEvent(const TriggerEvent& event)
{
    const auto& fields = event.fields;
    juce::OSCMessage message("/gc/trigger");
    message.addString(event.triggerName);
    message.addInt32(fields.sourceCell.column);
    message.addInt32(fields.sourceCell.row);
    message.addInt32(static_cast<juce::int32>(juce::jmin<std::uint64_t>(fields.tick, static_cast<std::uint64_t>(std::numeric_limits<juce::int32>::max()))));

    return sendMessage(std::move(message),
                       "/gc/trigger " + event.triggerName
                           + " " + juce::String(fields.sourceCell.column)
                           + " " + juce::String(fields.sourceCell.row)
                           + " " + juce::String(fields.tick));
}

bool OscOutput::sendBusRouteEvent(const BusRouteEvent& event)
{
    const auto& fields = event.fields;
    const auto target = fields.targetAddress.value_or(fields.instrumentName);
    const auto triggerName = fields.instrumentName + ":" + target + ":" + event.payload;

    juce::OSCMessage message("/gc/trigger");
    message.addString(triggerName);
    message.addInt32(fields.sourceCell.column);
    message.addInt32(fields.sourceCell.row);
    message.addInt32(static_cast<juce::int32>(juce::jmin<std::uint64_t>(fields.tick, static_cast<std::uint64_t>(std::numeric_limits<juce::int32>::max()))));

    return sendMessage(std::move(message),
                       "/gc/trigger " + triggerName
                           + " " + juce::String(fields.sourceCell.column)
                           + " " + juce::String(fields.sourceCell.row)
                           + " " + juce::String(fields.tick));
}

bool OscOutput::sendGridState(const GridModel::Snapshot& snapshot)
{
    juce::OSCMessage message("/gc/grid");
    message.addInt32(snapshot.width);
    message.addInt32(snapshot.height);
    message.addString(encodeGrid(snapshot));

    return sendMessage(std::move(message),
                       "/gc/grid " + juce::String(snapshot.width)
                           + " " + juce::String(snapshot.height)
                           + " <" + juce::String(static_cast<int>(snapshot.cells.size())) + " chars>");
}

bool OscOutput::sendMessage(juce::OSCMessage message, const juce::String& debugDescription)
{
    if (! connected)
        return false;

    if (debugMode)
        juce::Logger::writeToLog("[GridCollider OSC] " + debugDescription);

    const auto sent = sender.send(message);

    if (! sent && debugMode)
        juce::Logger::writeToLog("[GridCollider OSC] send failed: " + debugDescription);

    return sent;
}

juce::String OscOutput::encodeGrid(const GridModel::Snapshot& snapshot)
{
    juce::String encoded;

    for (int row = 0; row < snapshot.height; ++row)
        for (int column = 0; column < snapshot.width; ++column)
            encoded += juce::String::charToString(static_cast<juce::juce_wchar>(snapshot.getGlyph(column, row)));

    return encoded;
}
}
