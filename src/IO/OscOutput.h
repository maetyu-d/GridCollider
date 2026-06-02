#pragma once

#include <juce_core/juce_core.h>
#include <juce_osc/juce_osc.h>

#include "../Core/GridModel.h"
#include "../Core/InternalEvent.h"

namespace gridcollider
{
class OscOutput
{
public:
    OscOutput();
    ~OscOutput();

    void setEndpoint(juce::String host, int port);
    [[nodiscard]] bool connect();
    [[nodiscard]] bool disconnect();

    void setDebugMode(bool shouldDebug);
    [[nodiscard]] bool isDebugModeEnabled() const noexcept;

    [[nodiscard]] bool isConnected() const noexcept;

    [[nodiscard]] const juce::String& getHost() const noexcept;
    [[nodiscard]] int getPort() const noexcept;
    [[nodiscard]] juce::String getEndpointDescription() const;
    [[nodiscard]] juce::String getConnectionStatusText() const;

    [[nodiscard]] bool sendNoteEvent(const NoteEvent& event);
    [[nodiscard]] bool sendControlEvent(const ControlEvent& event);
    [[nodiscard]] bool sendTriggerEvent(const TriggerEvent& event);
    [[nodiscard]] bool sendBusRouteEvent(const BusRouteEvent& event);
    [[nodiscard]] bool sendGridState(const GridModel::Snapshot& snapshot);

private:
    [[nodiscard]] bool sendMessage(juce::OSCMessage message, const juce::String& debugDescription);
    [[nodiscard]] static juce::String encodeGrid(const GridModel::Snapshot& snapshot);

    juce::String host;
    int port = 57120;
    bool connected = false;
    bool debugMode = false;
    juce::OSCSender sender;
};
}
