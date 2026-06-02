#pragma once

#include <juce_core/juce_core.h>

#include "GridModel.h"
#include "InternalEvent.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace gridcollider
{
struct GridEvaluation
{
    std::vector<InternalEvent> events;
    GridModel::Snapshot grid;
};

class GridInterpreter
{
public:
    struct RuntimeOperator;
    class ExecutionContext;

    using OperatorFunction = std::function<void(ExecutionContext&, RuntimeOperator&)>;

    struct OperatorDefinition
    {
        juce::String name;
        bool alwaysRuns = false;
        OperatorFunction run;
    };

    GridInterpreter();

    [[nodiscard]] GridEvaluation evaluate(const GridModel::Snapshot& snapshot, std::uint64_t frame) const;

private:
    void registerOperators();
    void addOperator(char glyph, juce::String name, bool alwaysRuns, OperatorFunction run);

    std::unordered_map<char, OperatorDefinition> registry;
};
}
