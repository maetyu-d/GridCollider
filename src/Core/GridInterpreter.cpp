#include "GridInterpreter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace gridcollider
{
namespace
{
constexpr std::array<char, 36> base36Keys {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
    'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
    'u', 'v', 'w', 'x', 'y', 'z'
};

[[nodiscard]] char asciiLower(const char glyph) noexcept
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(glyph)));
}

[[nodiscard]] char asciiUpper(const char glyph) noexcept
{
    return static_cast<char>(std::toupper(static_cast<unsigned char>(glyph)));
}

[[nodiscard]] bool isAsciiAlpha(const char glyph) noexcept
{
    return std::isalpha(static_cast<unsigned char>(glyph)) != 0;
}

[[nodiscard]] bool isAsciiUpper(const char glyph) noexcept
{
    return std::isupper(static_cast<unsigned char>(glyph)) != 0;
}

[[nodiscard]] bool isAsciiDigit(const char glyph) noexcept
{
    return std::isdigit(static_cast<unsigned char>(glyph)) != 0;
}

[[nodiscard]] char normaliseGridGlyph(const char glyph) noexcept
{
    const auto value = static_cast<unsigned char>(glyph);
    return value >= 32 && value <= 126 ? glyph : GridModel::emptyGlyph;
}

[[nodiscard]] juce::String glyphString(const char glyph)
{
    return juce::String::charToString(static_cast<juce::juce_wchar>(glyph));
}

[[nodiscard]] int positiveModulo(const int value, const int modulus) noexcept
{
    if (modulus <= 0)
        return 0;

    const auto result = value % modulus;
    return result < 0 ? result + modulus : result;
}

[[nodiscard]] std::uint64_t mixSeed(std::uint64_t value) noexcept
{
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value;
}

[[nodiscard]] std::optional<std::pair<char, int>> transposedNoteFor(const char glyph)
{
    static const std::unordered_map<char, std::pair<char, int>> table {
        { 'A', { 'A', 0 } }, { 'a', { 'a', 0 } }, { 'B', { 'B', 0 } },
        { 'C', { 'C', 0 } }, { 'c', { 'c', 0 } }, { 'D', { 'D', 0 } },
        { 'd', { 'd', 0 } }, { 'E', { 'E', 0 } }, { 'F', { 'F', 0 } },
        { 'f', { 'f', 0 } }, { 'G', { 'G', 0 } }, { 'g', { 'g', 0 } },
        { 'H', { 'A', 0 } }, { 'h', { 'a', 0 } }, { 'I', { 'B', 0 } },
        { 'J', { 'C', 1 } }, { 'j', { 'c', 1 } }, { 'K', { 'D', 1 } },
        { 'k', { 'd', 1 } }, { 'L', { 'E', 1 } }, { 'M', { 'F', 1 } },
        { 'm', { 'f', 1 } }, { 'N', { 'G', 1 } }, { 'n', { 'g', 1 } },
        { 'O', { 'A', 1 } }, { 'o', { 'a', 1 } }, { 'P', { 'B', 1 } },
        { 'Q', { 'C', 2 } }, { 'q', { 'c', 2 } }, { 'R', { 'D', 2 } },
        { 'r', { 'd', 2 } }, { 'S', { 'E', 2 } }, { 'T', { 'F', 2 } },
        { 't', { 'f', 2 } }, { 'U', { 'G', 2 } }, { 'u', { 'g', 2 } },
        { 'V', { 'A', 2 } }, { 'v', { 'a', 2 } }, { 'W', { 'B', 2 } },
        { 'X', { 'C', 3 } }, { 'x', { 'c', 3 } }, { 'Y', { 'D', 3 } },
        { 'y', { 'd', 3 } }, { 'Z', { 'E', 3 } },
        { 'e', { 'F', 0 } }, { 'l', { 'F', 1 } }, { 's', { 'F', 2 } },
        { 'z', { 'F', 3 } }, { 'b', { 'C', 1 } }, { 'i', { 'C', 1 } },
        { 'p', { 'C', 2 } }, { 'w', { 'C', 3 } }
    };

    if (const auto iter = table.find(glyph); iter != table.end())
        return iter->second;

    return std::nullopt;
}

[[nodiscard]] int semitoneFor(const char note) noexcept
{
    switch (note)
    {
        case 'C': return 0;
        case 'c': return 1;
        case 'D': return 2;
        case 'd': return 3;
        case 'E': return 4;
        case 'F': return 5;
        case 'f': return 6;
        case 'G': return 7;
        case 'g': return 8;
        case 'A': return 9;
        case 'a': return 10;
        case 'B': return 11;
        default:  return -1;
    }
}

[[nodiscard]] int midiPitchFor(const char noteGlyph, const int octave) noexcept
{
    const auto transposed = transposedNoteFor(noteGlyph);

    if (! transposed.has_value())
        return -1;

    const auto semitone = semitoneFor(transposed->first);

    if (semitone < 0)
        return -1;

    return juce::jlimit(0, 127, (octave + transposed->second) * 12 + semitone + 24);
}
}

struct GridInterpreter::RuntimeOperator
{
    struct Port
    {
        int x = 0;
        int y = 0;
        bool output = false;
        bool bang = false;
        bool sensitive = false;
        bool hasDefault = false;
        char defaultGlyph = GridModel::emptyGlyph;
        int min = 0;
        int max = 36;
    };

    enum class ResultType
    {
        none,
        value,
        bang
    };

    RuntimeOperator(const int column, const int row, const char source)
        : x(column),
          y(row),
          sourceGlyph(source),
          glyph(isAsciiAlpha(source) && isAsciiUpper(source) ? asciiUpper(source) : source),
          uppercase(isAsciiAlpha(source) && isAsciiUpper(source))
    {
    }

    void addPort(std::string name, Port port)
    {
        ports.insert_or_assign(std::move(name), port);
    }

    [[nodiscard]] Port port(const std::string& name) const
    {
        if (const auto iter = ports.find(name); iter != ports.end())
            return iter->second;

        return {};
    }

    void setValue(const char value)
    {
        resultType = ResultType::value;
        resultGlyph = value;
    }

    void setBang(const bool value)
    {
        resultType = ResultType::bang;
        resultBang = value;
    }

    int x = 0;
    int y = 0;
    char sourceGlyph = GridModel::emptyGlyph;
    char glyph = GridModel::emptyGlyph;
    bool uppercase = false;
    std::unordered_map<std::string, Port> ports;
    ResultType resultType = ResultType::none;
    char resultGlyph = GridModel::emptyGlyph;
    bool resultBang = false;
};

class GridInterpreter::ExecutionContext
{
public:
    ExecutionContext(GridModel::Snapshot sourceGrid, const std::uint64_t sourceFrame)
        : grid(std::move(sourceGrid)),
          locks(static_cast<std::size_t>(juce::jmax(0, grid.width * grid.height)), false),
          timestampSeconds(juce::Time::getMillisecondCounterHiRes() / 1000.0),
          frame(sourceFrame)
    {
        if (grid.cells.size() != static_cast<std::size_t>(juce::jmax(0, grid.width * grid.height)))
            grid.cells.assign(static_cast<std::size_t>(juce::jmax(0, grid.width * grid.height)), GridModel::emptyGlyph);
    }

    [[nodiscard]] bool inBounds(const int x, const int y) const noexcept
    {
        return grid.isInBounds(x, y);
    }

    [[nodiscard]] char glyphAt(const int x, const int y) const noexcept
    {
        return grid.getGlyph(x, y);
    }

    [[nodiscard]] int valueOf(const char glyph) const noexcept
    {
        if (glyph == GridModel::emptyGlyph || glyph == '*')
            return 0;

        const auto lower = asciiLower(glyph);

        for (int index = 0; index < static_cast<int>(base36Keys.size()); ++index)
            if (base36Keys[static_cast<std::size_t>(index)] == lower)
                return index;

        return 0;
    }

    [[nodiscard]] char keyOf(const int value, const bool uppercase = false) const noexcept
    {
        const auto key = base36Keys[static_cast<std::size_t>(positiveModulo(value, 36))];
        return uppercase ? asciiUpper(key) : key;
    }

    void write(const int x, const int y, const char glyph)
    {
        if (! inBounds(x, y))
            return;

        const auto index = static_cast<std::size_t>(x + y * grid.width);
        const auto previousGlyph = grid.cells[index];
        const auto nextGlyph = normaliseGridGlyph(glyph);

        if (previousGlyph == nextGlyph)
            return;

        grid.cells[index] = nextGlyph;
        addGridMutationEvent({ x, y }, previousGlyph, nextGlyph);
    }

    void lock(const int x, const int y)
    {
        if (! inBounds(x, y))
            return;

        locks[static_cast<std::size_t>(x + y * grid.width)] = true;
    }

    [[nodiscard]] bool lockAt(const int x, const int y) const
    {
        if (! inBounds(x, y))
            return false;

        return locks[static_cast<std::size_t>(x + y * grid.width)];
    }

    [[nodiscard]] bool hasNeighbor(const RuntimeOperator& op, const char glyph) const noexcept
    {
        return glyphAt(op.x + 1, op.y) == glyph
            || glyphAt(op.x - 1, op.y) == glyph
            || glyphAt(op.x, op.y + 1) == glyph
            || glyphAt(op.x, op.y - 1) == glyph;
    }

    [[nodiscard]] char listenGlyph(const RuntimeOperator& op, const RuntimeOperator::Port& port) const
    {
        const auto raw = glyphAt(op.x + port.x, op.y + port.y);

        if ((raw == GridModel::emptyGlyph || raw == '*') && port.hasDefault)
            return port.defaultGlyph;

        return raw;
    }

    [[nodiscard]] int listenValue(const RuntimeOperator& op, const RuntimeOperator::Port& port) const
    {
        return juce::jlimit(port.min, port.max, valueOf(listenGlyph(op, port)));
    }

    void output(RuntimeOperator& op, const char glyph, const RuntimeOperator::Port& port)
    {
        if (glyph == 0)
            return;

        const auto shouldUpperCase = port.sensitive && outputShouldUpperCase(op);
        write(op.x + port.x, op.y + port.y, shouldUpperCase ? asciiUpper(glyph) : glyph);
    }

    void bang(RuntimeOperator& op, const bool shouldBang, const RuntimeOperator::Port& port)
    {
        if (shouldBang)
        {
            ParameterMap parameters;
            parameters["targetCell"] = juce::String(op.x + port.x + 1) + "," + juce::String(op.y + port.y + 1);
            addTriggerEvent("grid",
                            "bang",
                            std::move(parameters),
                            "grid://" + juce::String(op.x + port.x + 1) + "," + juce::String(op.y + port.y + 1));
        }

        write(op.x + port.x, op.y + port.y, shouldBang ? '*' : GridModel::emptyGlyph);
        lock(op.x + port.x, op.y + port.y);
    }

    void move(RuntimeOperator& op, const int deltaX, const int deltaY)
    {
        const auto targetX = op.x + deltaX;
        const auto targetY = op.y + deltaY;

        if (! inBounds(targetX, targetY) || glyphAt(targetX, targetY) != GridModel::emptyGlyph)
        {
            write(op.x, op.y, '*');
            return;
        }

        write(op.x, op.y, GridModel::emptyGlyph);
        op.x = targetX;
        op.y = targetY;
        write(op.x, op.y, op.glyph);
        lock(op.x, op.y);
    }

    void finalize(RuntimeOperator& op)
    {
        for (const auto& [name, port] : op.ports)
        {
            juce::ignoreUnused(name);

            if (! port.bang)
                lock(op.x + port.x, op.y + port.y);
        }

        const auto outputPort = op.port("output");

        if (! outputPort.output)
            return;

        if (op.resultType == RuntimeOperator::ResultType::bang)
            bang(op, op.resultBang, outputPort);
        else if (op.resultType == RuntimeOperator::ResultType::value)
            output(op, op.resultGlyph, outputPort);
    }

    void setCurrentSource(const int x, const int y) noexcept
    {
        currentSource = { x, y };
    }

    [[nodiscard]] EventFields makeFields(juce::String instrumentName = {},
                                         const int pitch = -1,
                                         const float velocity = 0.0f,
                                         const std::uint64_t durationTicks = 0,
                                         ParameterMap parameters = {},
                                         std::optional<juce::String> targetAddress = std::nullopt) const
    {
        EventFields fields;
        fields.timestampSeconds = timestampSeconds;
        fields.tick = frame;
        fields.sourceCell = currentSource;
        fields.instrumentName = std::move(instrumentName);
        fields.pitch = pitch;
        fields.velocity = velocity;
        fields.durationTicks = durationTicks;
        fields.parameters = std::move(parameters);
        fields.targetAddress = std::move(targetAddress);
        return fields;
    }

    void addNoteEvent(juce::String instrumentName,
                      const int pitch,
                      const float velocity,
                      const std::uint64_t durationTicks,
                      ParameterMap parameters,
                      std::optional<juce::String> targetAddress = std::nullopt)
    {
        events.push_back(NoteEvent { makeFields(std::move(instrumentName),
                                                pitch,
                                                velocity,
                                                durationTicks,
                                                std::move(parameters),
                                                std::move(targetAddress)) });
    }

    void addControlEvent(juce::String instrumentName,
                         juce::String parameterName,
                         const float value,
                         ParameterMap parameters,
                         std::optional<juce::String> targetAddress = std::nullopt)
    {
        events.push_back(ControlEvent { makeFields(std::move(instrumentName),
                                                   -1,
                                                   0.0f,
                                                   0,
                                                   std::move(parameters),
                                                   std::move(targetAddress)),
                                       std::move(parameterName),
                                       value });
    }

    void addTriggerEvent(juce::String instrumentName,
                         juce::String triggerName,
                         ParameterMap parameters = {},
                         std::optional<juce::String> targetAddress = std::nullopt)
    {
        events.push_back(TriggerEvent { makeFields(std::move(instrumentName),
                                                   -1,
                                                   0.0f,
                                                   0,
                                                   std::move(parameters),
                                                   std::move(targetAddress)),
                                        std::move(triggerName) });
    }

    void addBusRouteEvent(juce::String instrumentName,
                          juce::String payload,
                          ParameterMap parameters = {},
                          std::optional<juce::String> targetAddress = std::nullopt)
    {
        events.push_back(BusRouteEvent { makeFields(std::move(instrumentName),
                                                    -1,
                                                    0.0f,
                                                    0,
                                                    std::move(parameters),
                                                    std::move(targetAddress)),
                                         std::move(payload) });
    }

    void addLogEvent(juce::String message, ParameterMap parameters = {})
    {
        events.push_back(LogEvent { makeFields("log", -1, 0.0f, 0, std::move(parameters)), std::move(message) });
    }

    void addGridMutationEvent(const CellCoordinate targetCell, const char previousGlyph, const char newGlyph)
    {
        ParameterMap parameters;
        parameters["previous"] = glyphString(previousGlyph);
        parameters["next"] = glyphString(newGlyph);

        events.push_back(GridMutationEvent { makeFields("grid", -1, 0.0f, 0, std::move(parameters)),
                                             targetCell,
                                             previousGlyph,
                                             newGlyph });
    }

    [[nodiscard]] char variableValue(const char key) const
    {
        if (const auto iter = variables.find(key); iter != variables.end())
            return iter->second;

        return GridModel::emptyGlyph;
    }

    void setVariable(const char key, const char value)
    {
        variables[key] = value;
    }

    [[nodiscard]] int deterministicRandom(const RuntimeOperator& op, const int a, const int b) const
    {
        const auto low = juce::jmin(a, b);
        const auto high = juce::jmax(a, b);

        if (low == high)
            return low;

        const auto range = static_cast<std::uint64_t>(high - low + 1);
        const auto seed = mixSeed(frame
                                  ^ (static_cast<std::uint64_t>(op.x + 1) * 0x9e3779b185ebca87ULL)
                                  ^ (static_cast<std::uint64_t>(op.y + 1) * 0xc2b2ae3d27d4eb4fULL)
                                  ^ (static_cast<std::uint64_t>(a + 37) << 17)
                                  ^ (static_cast<std::uint64_t>(b + 73) << 31));

        return low + static_cast<int>(seed % range);
    }

    GridModel::Snapshot grid;
    std::vector<bool> locks;
    std::unordered_map<char, char> variables;
    std::vector<InternalEvent> events;
    CellCoordinate currentSource;
    double timestampSeconds = 0.0;
    std::uint64_t frame = 0;

private:
    [[nodiscard]] bool outputShouldUpperCase(const RuntimeOperator& op) const
    {
        const auto value = glyphAt(op.x + 1, op.y);
        return isAsciiAlpha(value) && isAsciiUpper(value);
    }
};

namespace
{
using Op = GridInterpreter::RuntimeOperator;
using Context = GridInterpreter::ExecutionContext;
using Port = GridInterpreter::RuntimeOperator::Port;

[[nodiscard]] Port port(const int x, const int y)
{
    Port result;
    result.x = x;
    result.y = y;
    return result;
}

[[nodiscard]] Port valuePort(const int x, const int y, const int min = 0, const int max = 36)
{
    auto result = port(x, y);
    result.min = min;
    result.max = max;
    return result;
}

[[nodiscard]] Port defaultValuePort(const int x, const int y, const char defaultGlyph, const int min = 0, const int max = 36)
{
    auto result = valuePort(x, y, min, max);
    result.hasDefault = true;
    result.defaultGlyph = defaultGlyph;
    return result;
}

[[nodiscard]] Port outputPort(const int x, const int y, const bool sensitive = false)
{
    auto result = port(x, y);
    result.output = true;
    result.sensitive = sensitive;
    return result;
}

[[nodiscard]] Port bangPort(const int x, const int y)
{
    auto result = outputPort(x, y);
    result.bang = true;
    return result;
}

void setBinaryValuePorts(Op& op, const bool sensitive = true)
{
    op.addPort("a", valuePort(-1, 0));
    op.addPort("b", valuePort(1, 0));
    op.addPort("output", outputPort(0, 1, sensitive));
}

void emitMidiLikeEvent(Context& ctx, Op& op, const juce::String& kind)
{
    if (! ctx.hasNeighbor(op, '*'))
        return;

    const auto channelGlyph = ctx.listenGlyph(op, op.port("channel"));
    const auto octaveGlyph = ctx.listenGlyph(op, op.port("octave"));
    const auto noteGlyph = ctx.listenGlyph(op, op.port("note"));
    const auto hasCompactTail = noteGlyph == GridModel::emptyGlyph || noteGlyph == '*';
    const auto compactPitchOnly = channelGlyph != GridModel::emptyGlyph
                                  && channelGlyph != '*'
                                  && ! isAsciiDigit(channelGlyph)
                                  && (octaveGlyph == GridModel::emptyGlyph || octaveGlyph == '*');
    const auto compactChannelPitch = channelGlyph != GridModel::emptyGlyph
                                     && channelGlyph != '*'
                                     && octaveGlyph != GridModel::emptyGlyph
                                     && octaveGlyph != '*'
                                     && ! isAsciiDigit(octaveGlyph)
                                     && hasCompactTail;
    const auto compactNote = compactPitchOnly || compactChannelPitch;

    if (! compactNote
        && (channelGlyph == GridModel::emptyGlyph
            || octaveGlyph == GridModel::emptyGlyph
            || noteGlyph == GridModel::emptyGlyph
            || isAsciiDigit(noteGlyph)))
        return;

    const auto channel = compactChannelPitch ? ctx.valueOf(channelGlyph)
                                             : compactPitchOnly ? (kind == "mono" ? 1 : 0)
                                                                : ctx.listenValue(op, op.port("channel"));

    if (channel > 15)
        return;

    const auto octave = compactNote ? 5 : ctx.listenValue(op, op.port("octave"));
    const auto note = compactChannelPitch ? octaveGlyph : compactPitchOnly ? channelGlyph : noteGlyph;
    const auto velocity = compactNote ? 6 : ctx.listenValue(op, op.port("velocity"));
    const auto length = compactNote ? 4 : ctx.listenValue(op, op.port("length"));

    ParameterMap parameters;
    parameters["channel"] = juce::String(channel);
    parameters["octave"] = juce::String(octave);
    parameters["note"] = glyphString(note);
    parameters["velocityRaw"] = juce::String(velocity);
    parameters["durationRaw"] = juce::String(length);

    ctx.addNoteEvent(kind,
                     midiPitchFor(note, octave),
                     static_cast<float>(velocity) / 16.0f,
                     static_cast<std::uint64_t>(length),
                     std::move(parameters),
                     "midi://channel/" + juce::String(channel));
}

juce::String readMessage(Context& ctx, Op& op, const int startOffset)
{
    juce::String message;

    for (int offset = startOffset; offset <= 36; ++offset)
    {
        const auto glyph = ctx.glyphAt(op.x + offset, op.y);
        ctx.lock(op.x + offset, op.y);

        if (glyph == GridModel::emptyGlyph)
            break;

        message += glyphString(glyph);
    }

    return message;
}
}

GridInterpreter::GridInterpreter()
{
    registerOperators();
}

GridEvaluation GridInterpreter::evaluate(const GridModel::Snapshot& snapshot, const std::uint64_t frame) const
{
    ExecutionContext context(snapshot, frame);

    struct ParsedOperator
    {
        int x = 0;
        int y = 0;
        char glyph = GridModel::emptyGlyph;
    };

    std::vector<ParsedOperator> parsed;
    parsed.reserve(static_cast<std::size_t>(juce::jmax(0, snapshot.width * snapshot.height)));

    for (int y = 0; y < snapshot.height; ++y)
    {
        for (int x = 0; x < snapshot.width; ++x)
        {
            const auto glyph = snapshot.getGlyph(x, y);

            if (glyph != GridModel::emptyGlyph)
                parsed.push_back({ x, y, glyph });
        }
    }

    for (const auto& item : parsed)
    {
        if (context.lockAt(item.x, item.y))
            continue;

        const auto key = isAsciiAlpha(item.glyph) ? asciiLower(item.glyph) : item.glyph;
        const auto iter = registry.find(key);

        if (iter == registry.end())
            continue;

        const auto uppercase = isAsciiAlpha(item.glyph) && isAsciiUpper(item.glyph);

        if (! uppercase && ! iter->second.alwaysRuns && ! context.hasNeighbor(RuntimeOperator(item.x, item.y, item.glyph), '*'))
            continue;

        RuntimeOperator op(item.x, item.y, item.glyph);
        context.setCurrentSource(item.x, item.y);
        iter->second.run(context, op);
        context.finalize(op);
    }

    return { context.events, context.grid };
}

void GridInterpreter::registerOperators()
{
    addOperator('a', "add", false, [](Context& ctx, Op& op)
    {
        setBinaryValuePorts(op);
        op.setValue(ctx.keyOf(ctx.listenValue(op, op.port("a")) + ctx.listenValue(op, op.port("b"))));
    });

    addOperator('b', "subtract", false, [](Context& ctx, Op& op)
    {
        setBinaryValuePorts(op);
        op.setValue(ctx.keyOf(std::abs(ctx.listenValue(op, op.port("b")) - ctx.listenValue(op, op.port("a")))));
    });

    addOperator('c', "clock", false, [](Context& ctx, Op& op)
    {
        op.addPort("rate", valuePort(-1, 0, 1));
        op.addPort("mod", valuePort(1, 0));
        op.addPort("output", outputPort(0, 1, true));

        const auto rate = ctx.listenValue(op, op.port("rate"));
        const auto mod = ctx.listenValue(op, op.port("mod"));
        op.setValue(ctx.keyOf(mod <= 0 ? 0 : static_cast<int>((ctx.frame / static_cast<std::uint64_t>(rate)) % static_cast<std::uint64_t>(mod))));
    });

    addOperator('d', "delay", false, [](Context& ctx, Op& op)
    {
        op.addPort("rate", valuePort(-1, 0, 1));
        op.addPort("mod", valuePort(1, 0, 1));
        op.addPort("output", bangPort(0, 1));

        const auto rate = ctx.listenValue(op, op.port("rate"));
        const auto mod = ctx.listenValue(op, op.port("mod"));
        op.setBang(mod == 1 || (rate > 0 && mod > 0 && ctx.frame % static_cast<std::uint64_t>(mod * rate) == 0));
    });

    addOperator('e', "east", false, [](Context& ctx, Op& op) { ctx.move(op, 1, 0); });

    addOperator('f', "if", false, [](Context& ctx, Op& op)
    {
        op.addPort("a", port(-1, 0));
        op.addPort("b", port(1, 0));
        op.addPort("output", bangPort(0, 1));
        op.setBang(ctx.listenGlyph(op, op.port("a")) == ctx.listenGlyph(op, op.port("b")));
    });

    addOperator('g', "generator", false, [](Context& ctx, Op& op)
    {
        op.addPort("x", valuePort(-3, 0));
        op.addPort("y", valuePort(-2, 0));
        op.addPort("len", valuePort(-1, 0, 1));

        const auto len = ctx.listenValue(op, op.port("len"));
        const auto x = ctx.listenValue(op, op.port("x"));
        const auto y = ctx.listenValue(op, op.port("y")) + 1;

        for (int offset = 0; offset < len; ++offset)
        {
            const auto input = port(offset + 1, 0);
            const auto output = outputPort(x + offset, y);
            op.addPort("in" + std::to_string(offset), input);
            op.addPort("out" + std::to_string(offset), output);
            ctx.output(op, ctx.listenGlyph(op, input), output);
        }
    });

    addOperator('h', "halt", false, [](Context& ctx, Op& op)
    {
        op.addPort("output", outputPort(0, 1));
        ctx.lock(op.x, op.y + 1);
        op.setValue(ctx.keyOf(ctx.listenValue(op, op.port("output"))));
    });

    addOperator('i', "increment", false, [](Context& ctx, Op& op)
    {
        op.addPort("step", valuePort(-1, 0));
        op.addPort("mod", valuePort(1, 0));
        op.addPort("output", outputPort(0, 1, true));

        const auto step = ctx.listenValue(op, op.port("step"));
        const auto mod = ctx.listenValue(op, op.port("mod"));
        const auto value = ctx.listenValue(op, op.port("output"));
        op.setValue(mod > 0 ? ctx.keyOf((value + step) % mod) : '0');
    });

    addOperator('j', "jumper", false, [](Context& ctx, Op& op)
    {
        const auto value = ctx.glyphAt(op.x, op.y - 1);

        if (value == 'J')
            return;

        int offset = 0;

        while (ctx.inBounds(op.x, op.y + offset))
        {
            ++offset;

            if (ctx.glyphAt(op.x, op.y + offset) != op.glyph)
                break;
        }

        op.addPort("input", port(0, -1));
        op.addPort("output", outputPort(0, offset));
        op.setValue(value);
    });

    addOperator('k', "konkat", false, [](Context& ctx, Op& op)
    {
        op.addPort("len", valuePort(-1, 0, 1));
        const auto len = ctx.listenValue(op, op.port("len"));

        for (int offset = 0; offset < len; ++offset)
        {
            const auto key = ctx.glyphAt(op.x + offset + 1, op.y);
            ctx.lock(op.x + offset + 1, op.y);

            if (key == GridModel::emptyGlyph)
                continue;

            const auto input = port(offset + 1, 0);
            const auto output = outputPort(offset + 1, 1);
            op.addPort("in" + std::to_string(offset), input);
            op.addPort("out" + std::to_string(offset), output);
            ctx.output(op, ctx.variableValue(key), output);
        }
    });

    addOperator('l', "lesser", false, [](Context& ctx, Op& op)
    {
        setBinaryValuePorts(op);
        op.setValue(ctx.keyOf(juce::jmin(ctx.listenValue(op, op.port("a")), ctx.listenValue(op, op.port("b")))));
    });

    addOperator('m', "multiply", false, [](Context& ctx, Op& op)
    {
        setBinaryValuePorts(op);
        op.setValue(ctx.keyOf(ctx.listenValue(op, op.port("a")) * ctx.listenValue(op, op.port("b"))));
    });

    addOperator('n', "north", false, [](Context& ctx, Op& op) { ctx.move(op, 0, -1); });

    addOperator('o', "read", false, [](Context& ctx, Op& op)
    {
        op.addPort("x", valuePort(-2, 0));
        op.addPort("y", valuePort(-1, 0));
        op.addPort("output", outputPort(0, 1));

        const auto x = ctx.listenValue(op, op.port("x"));
        const auto y = ctx.listenValue(op, op.port("y"));
        const auto read = port(x + 1, y);
        op.addPort("read", read);
        op.setValue(ctx.listenGlyph(op, read));
    });

    addOperator('p', "push", false, [](Context& ctx, Op& op)
    {
        op.addPort("key", valuePort(-2, 0));
        op.addPort("len", valuePort(-1, 0, 1));
        op.addPort("val", port(1, 0));

        const auto len = ctx.listenValue(op, op.port("len"));
        const auto key = ctx.listenValue(op, op.port("key"));

        for (int offset = 0; offset < len; ++offset)
            ctx.lock(op.x + offset, op.y + 1);

        op.addPort("output", outputPort(key % len, 1));
        op.setValue(ctx.listenGlyph(op, op.port("val")));
    });

    addOperator('q', "query", false, [](Context& ctx, Op& op)
    {
        op.addPort("x", valuePort(-3, 0));
        op.addPort("y", valuePort(-2, 0));
        op.addPort("len", valuePort(-1, 0, 1));

        const auto len = ctx.listenValue(op, op.port("len"));
        const auto x = ctx.listenValue(op, op.port("x"));
        const auto y = ctx.listenValue(op, op.port("y"));

        for (int offset = 0; offset < len; ++offset)
        {
            const auto input = port(x + offset + 1, y);
            const auto output = outputPort(offset - len + 1, 1);
            op.addPort("in" + std::to_string(offset), input);
            op.addPort("out" + std::to_string(offset), output);
            ctx.output(op, ctx.listenGlyph(op, input), output);
        }
    });

    addOperator('r', "random", false, [](Context& ctx, Op& op)
    {
        setBinaryValuePorts(op);
        op.setValue(ctx.keyOf(ctx.deterministicRandom(op, ctx.listenValue(op, op.port("a")), ctx.listenValue(op, op.port("b")))));
    });

    addOperator('s', "south", false, [](Context& ctx, Op& op) { ctx.move(op, 0, 1); });

    addOperator('t', "track", false, [](Context& ctx, Op& op)
    {
        op.addPort("key", valuePort(-2, 0));
        op.addPort("len", valuePort(-1, 0, 1));
        op.addPort("output", outputPort(0, 1));

        const auto len = ctx.listenValue(op, op.port("len"));
        const auto key = ctx.listenValue(op, op.port("key"));

        for (int offset = 0; offset < len; ++offset)
            ctx.lock(op.x + offset + 1, op.y);

        const auto value = port((key % len) + 1, 0);
        op.addPort("val", value);
        op.setValue(ctx.listenGlyph(op, value));
    });

    addOperator('u', "uclid", false, [](Context& ctx, Op& op)
    {
        op.addPort("step", valuePort(-1, 0, 0));
        op.addPort("max", valuePort(1, 0, 1));
        op.addPort("output", bangPort(0, 1));

        const auto step = ctx.listenValue(op, op.port("step"));
        const auto max = ctx.listenValue(op, op.port("max"));
        const auto bucket = (step * static_cast<int>((ctx.frame + static_cast<std::uint64_t>(max - 1)) % static_cast<std::uint64_t>(max))) % max + step;
        op.setBang(bucket >= max);
    });

    addOperator('v', "variable", false, [](Context& ctx, Op& op)
    {
        op.addPort("write", port(-1, 0));
        op.addPort("read", port(1, 0));

        const auto write = ctx.listenGlyph(op, op.port("write"));
        const auto read = ctx.listenGlyph(op, op.port("read"));

        if (write != GridModel::emptyGlyph)
        {
            ctx.setVariable(write, read);
            return;
        }

        if (read != GridModel::emptyGlyph)
        {
            op.addPort("output", outputPort(0, 1));
            op.setValue(ctx.variableValue(read));
        }
    });

    addOperator('w', "west", false, [](Context& ctx, Op& op) { ctx.move(op, -1, 0); });

    addOperator('x', "write", false, [](Context& ctx, Op& op)
    {
        op.addPort("x", valuePort(-2, 0));
        op.addPort("y", valuePort(-1, 0));
        op.addPort("val", port(1, 0));

        const auto x = ctx.listenValue(op, op.port("x"));
        const auto y = ctx.listenValue(op, op.port("y")) + 1;
        op.addPort("output", outputPort(x, y));
        op.setValue(ctx.listenGlyph(op, op.port("val")));
    });

    addOperator('y', "jymper", false, [](Context& ctx, Op& op)
    {
        const auto value = ctx.glyphAt(op.x - 1, op.y);

        if (value == 'Y')
            return;

        int offset = 0;

        while (ctx.inBounds(op.x + offset, op.y))
        {
            ++offset;

            if (ctx.glyphAt(op.x + offset, op.y) != op.glyph)
                break;
        }

        op.addPort("input", port(-1, 0));
        op.addPort("output", outputPort(offset, 0));
        op.setValue(value);
    });

    addOperator('z', "lerp", false, [](Context& ctx, Op& op)
    {
        op.addPort("rate", valuePort(-1, 0));
        op.addPort("target", valuePort(1, 0));
        op.addPort("output", outputPort(0, 1, true));

        const auto rate = ctx.listenValue(op, op.port("rate"));
        const auto target = ctx.listenValue(op, op.port("target"));
        const auto value = ctx.listenValue(op, op.port("output"));
        const auto delta = value <= target - rate ? rate : value >= target + rate ? -rate : target - value;
        op.setValue(ctx.keyOf(value + delta));
    });

    addOperator('*', "bang", true, [](Context& ctx, Op& op)
    {
        ctx.write(op.x, op.y, GridModel::emptyGlyph);
    });

    addOperator('#', "comment", true, [](Context& ctx, Op& op)
    {
        for (int x = op.x + 1; x <= ctx.grid.width; ++x)
        {
            ctx.lock(x, op.y);

            if (ctx.glyphAt(x, op.y) == op.glyph)
                break;
        }

        ctx.lock(op.x, op.y);
    });

    addOperator('$', "self", true, [](Context& ctx, Op& op)
    {
        const auto message = readMessage(ctx, op, 1);

        if (message.isEmpty() || ! ctx.hasNeighbor(op, '*'))
            return;

        ParameterMap parameters;
        parameters["command"] = message;
        ctx.addBusRouteEvent("self", message, std::move(parameters), "self://commander");
    });

    addOperator(':', "midi", true, [](Context& ctx, Op& op)
    {
        op.addPort("channel", valuePort(1, 0));
        op.addPort("octave", valuePort(2, 0, 0, 8));
        op.addPort("note", port(3, 0));
        op.addPort("velocity", defaultValuePort(4, 0, 'f', 0, 16));
        op.addPort("length", defaultValuePort(5, 0, '1', 0, 32));
        emitMidiLikeEvent(ctx, op, "midi");
    });

    addOperator('%', "mono", true, [](Context& ctx, Op& op)
    {
        op.addPort("channel", valuePort(1, 0));
        op.addPort("octave", valuePort(2, 0, 0, 8));
        op.addPort("note", port(3, 0));
        op.addPort("velocity", defaultValuePort(4, 0, 'f', 0, 16));
        op.addPort("length", defaultValuePort(5, 0, '1', 0, 32));
        emitMidiLikeEvent(ctx, op, "mono");
    });

    addOperator('!', "cc", true, [](Context& ctx, Op& op)
    {
        op.addPort("channel", valuePort(1, 0));
        op.addPort("knob", valuePort(2, 0, 0));
        op.addPort("value", valuePort(3, 0, 0));

        if (! ctx.hasNeighbor(op, '*') || ctx.listenGlyph(op, op.port("channel")) == GridModel::emptyGlyph || ctx.listenGlyph(op, op.port("knob")) == GridModel::emptyGlyph)
            return;

        const auto channel = ctx.listenValue(op, op.port("channel"));

        if (channel > 15)
            return;

        const auto value = static_cast<int>(std::ceil((127.0 * static_cast<double>(ctx.listenValue(op, op.port("value")))) / 35.0));
        const auto knob = ctx.listenValue(op, op.port("knob"));

        ParameterMap parameters;
        parameters["channel"] = juce::String(channel);
        parameters["knob"] = juce::String(knob);
        parameters["valueRaw"] = juce::String(ctx.listenValue(op, op.port("value")));
        parameters["valueMidi"] = juce::String(value);

        ctx.addControlEvent("midi",
                            "cc" + juce::String(knob),
                            static_cast<float>(value) / 127.0f,
                            std::move(parameters),
                            "midi://channel/" + juce::String(channel));
    });

    addOperator('?', "pb", true, [](Context& ctx, Op& op)
    {
        op.addPort("channel", valuePort(1, 0, 0, 15));
        op.addPort("lsb", valuePort(2, 0, 0));
        op.addPort("msb", valuePort(3, 0, 0));

        if (! ctx.hasNeighbor(op, '*') || ctx.listenGlyph(op, op.port("channel")) == GridModel::emptyGlyph || ctx.listenGlyph(op, op.port("lsb")) == GridModel::emptyGlyph)
            return;

        const auto lsb = static_cast<int>(std::ceil((127.0 * static_cast<double>(ctx.listenValue(op, op.port("lsb")))) / 35.0));
        const auto msb = static_cast<int>(std::ceil((127.0 * static_cast<double>(ctx.listenValue(op, op.port("msb")))) / 35.0));
        const auto channel = ctx.listenValue(op, op.port("channel"));

        ParameterMap parameters;
        parameters["channel"] = juce::String(channel);
        parameters["lsb"] = juce::String(lsb);
        parameters["msb"] = juce::String(msb);

        ctx.addControlEvent("midi",
                            "pitchbend",
                            static_cast<float>((msb << 7) + lsb) / 16383.0f,
                            std::move(parameters),
                            "midi://channel/" + juce::String(channel));
    });

    addOperator('=', "osc", true, [](Context& ctx, Op& op)
    {
        op.addPort("path", port(1, 0));
        const auto message = readMessage(ctx, op, 2);

        if (! ctx.hasNeighbor(op, '*'))
            return;

        const auto path = ctx.listenGlyph(op, op.port("path"));

        if (path == GridModel::emptyGlyph)
            return;

        juce::String values;

        for (int index = 0; index < message.length(); ++index)
        {
            if (index > 0)
                values += ",";

            values += juce::String(ctx.valueOf(static_cast<char>(message[index])));
        }

        ParameterMap parameters;
        parameters["path"] = "/" + glyphString(path);
        parameters["values"] = values;
        parameters["raw"] = message;

        ctx.addBusRouteEvent("osc", values, std::move(parameters), "/" + glyphString(path));
    });

    addOperator(';', "udp", true, [](Context& ctx, Op& op)
    {
        const auto message = readMessage(ctx, op, 1);

        if (message.isEmpty() || ! ctx.hasNeighbor(op, '*'))
            return;

        ParameterMap parameters;
        parameters["message"] = message;
        ctx.addBusRouteEvent("udp", message, std::move(parameters), "udp://default");
    });
}

void GridInterpreter::addOperator(char glyph, juce::String name, const bool alwaysRuns, OperatorFunction run)
{
    registry.insert_or_assign(glyph, OperatorDefinition { std::move(name), alwaysRuns, std::move(run) });
}
}
