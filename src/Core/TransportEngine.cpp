#include "TransportEngine.h"

#include <algorithm>
#include <chrono>

namespace gridcollider
{
namespace
{
constexpr double minimumBpm = 20.0;
constexpr double maximumBpm = 360.0;
constexpr double maximumSwingAmount = 0.95;
constexpr int minimumTicksPerBeat = 1;
constexpr int maximumTicksPerBeat = 96;
}

TransportEngine::TransportEngine()
    : timingThread([this] { runLoop(); })
{
}

TransportEngine::~TransportEngine()
{
    {
        const std::lock_guard lock(transportMutex);
        shouldExit = true;
        state = State::stopped;
    }

    transportCondition.notify_all();

    if (timingThread.joinable())
        timingThread.join();
}

void TransportEngine::start()
{
    {
        const std::lock_guard lock(transportMutex);
        state = State::running;
    }

    transportCondition.notify_all();
}

void TransportEngine::stop()
{
    {
        const std::lock_guard lock(transportMutex);
        state = State::stopped;
    }

    transportCondition.notify_all();
}

void TransportEngine::pause()
{
    {
        const std::lock_guard lock(transportMutex);

        if (state == State::running)
            state = State::paused;
    }

    transportCondition.notify_all();
}

void TransportEngine::reset()
{
    const std::lock_guard lock(transportMutex);
    frame = 0;
}

void TransportEngine::play()
{
    start();
}

void TransportEngine::tick()
{
    juce::ignoreUnused(processTick());
}

TransportEngine::TickContext TransportEngine::processTick()
{
    const auto context = nextTickContext();

    EvaluationCallback evaluator;
    TickCallback tickHandler;

    {
        const std::lock_guard lock(callbackMutex);
        evaluator = evaluationCallback;
        tickHandler = tickCallback;
    }

    TickResult result;
    result.context = context;

    if (evaluator)
        result.evaluation = evaluator(context);

    if (tickHandler)
        tickHandler(result);

    return context;
}

void TransportEngine::setBpm(const double newBpm)
{
    {
        const std::lock_guard lock(transportMutex);
        bpm = juce::jlimit(minimumBpm, maximumBpm, newBpm);
    }

    transportCondition.notify_all();
}

void TransportEngine::setSwingAmount(const double newSwingAmount)
{
    {
        const std::lock_guard lock(transportMutex);
        swingAmount = juce::jlimit(0.0, maximumSwingAmount, newSwingAmount);
    }

    transportCondition.notify_all();
}

void TransportEngine::setTicksPerBeat(const int newTicksPerBeat)
{
    {
        const std::lock_guard lock(transportMutex);
        ticksPerBeat = juce::jlimit(minimumTicksPerBeat, maximumTicksPerBeat, newTicksPerBeat);
    }

    transportCondition.notify_all();
}

void TransportEngine::setEvaluationCallback(EvaluationCallback callback)
{
    const std::lock_guard lock(callbackMutex);
    evaluationCallback = std::move(callback);
}

void TransportEngine::setTickCallback(TickCallback callback)
{
    const std::lock_guard lock(callbackMutex);
    tickCallback = std::move(callback);
}

TransportEngine::State TransportEngine::getState() const
{
    const std::lock_guard lock(transportMutex);
    return state;
}

bool TransportEngine::isPlaying() const
{
    return getState() == State::running;
}

bool TransportEngine::isPaused() const
{
    return getState() == State::paused;
}

double TransportEngine::getBpm() const
{
    const std::lock_guard lock(transportMutex);
    return bpm;
}

double TransportEngine::getSwingAmount() const
{
    const std::lock_guard lock(transportMutex);
    return swingAmount;
}

int TransportEngine::getTicksPerBeat() const
{
    const std::lock_guard lock(transportMutex);
    return ticksPerBeat;
}

std::uint64_t TransportEngine::getFrame() const
{
    const std::lock_guard lock(transportMutex);
    return frame;
}

void TransportEngine::runLoop()
{
    auto nextTick = std::chrono::steady_clock::now();

    for (;;)
    {
        {
            std::unique_lock lock(transportMutex);
            transportCondition.wait(lock, [this] { return shouldExit || state == State::running; });

            if (shouldExit)
                return;

            nextTick = std::chrono::steady_clock::now();
        }

        for (;;)
        {
            {
                const std::lock_guard lock(transportMutex);

                if (shouldExit)
                    return;

                if (state != State::running)
                    break;
            }

            const auto context = processTick();
            nextTick += getIntervalAfter(context);

            const auto now = std::chrono::steady_clock::now();
            const auto interval = getIntervalAfter(context);

            if (nextTick < now - interval)
                nextTick = now + interval;

            std::unique_lock lock(transportMutex);
            transportCondition.wait_until(lock, nextTick, [this] { return shouldExit || state != State::running; });

            if (shouldExit)
                return;

            if (state != State::running)
                break;
        }
    }
}

TransportEngine::TickContext TransportEngine::nextTickContext()
{
    const std::lock_guard lock(transportMutex);
    const auto currentFrame = frame++;
    const auto beatTicks = juce::jmax(1, ticksPerBeat);
    const auto tickInBeat = static_cast<int>(currentFrame % static_cast<std::uint64_t>(beatTicks));

    return { currentFrame, tickInBeat, tickInBeat == 0, bpm, swingAmount, beatTicks };
}

std::chrono::steady_clock::duration TransportEngine::getIntervalAfter(const TickContext& context) const
{
    const auto tickSeconds = 60.0 / (context.bpm * static_cast<double>(juce::jmax(1, context.ticksPerBeat)));
    const auto swing = juce::jlimit(0.0, maximumSwingAmount, context.swing);
    const auto swingFactor = (context.frame % 2 == 0) ? (1.0 + swing) : (1.0 - swing);
    return std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(tickSeconds * swingFactor));
}
}
