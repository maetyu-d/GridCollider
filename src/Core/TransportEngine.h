#pragma once

#include "GridInterpreter.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace gridcollider
{
class TransportEngine
{
public:
    enum class State
    {
        stopped,
        running,
        paused
    };

    struct TickContext
    {
        std::uint64_t frame = 0;
        int tickInBeat = 0;
        bool isBeat = true;
        double bpm = 120.0;
        double swing = 0.0;
        int ticksPerBeat = 4;
    };

    struct TickResult
    {
        TickContext context;
        GridEvaluation evaluation;
    };

    using EvaluationCallback = std::function<GridEvaluation(const TickContext&)>;
    using TickCallback = std::function<void(const TickResult&)>;

    TransportEngine();
    ~TransportEngine();

    void start();
    void stop();
    void pause();
    void reset();

    void play();
    void tick();

    void setBpm(double newBpm);
    void setSwingAmount(double newSwingAmount);
    void setTicksPerBeat(int newTicksPerBeat);
    void setEvaluationCallback(EvaluationCallback callback);
    void setTickCallback(TickCallback callback);

    [[nodiscard]] State getState() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool isPaused() const;
    [[nodiscard]] double getBpm() const;
    [[nodiscard]] double getSwingAmount() const;
    [[nodiscard]] int getTicksPerBeat() const;
    [[nodiscard]] std::uint64_t getFrame() const;

private:
    void runLoop();
    [[nodiscard]] TickContext processTick();
    [[nodiscard]] TickContext nextTickContext();
    [[nodiscard]] std::chrono::steady_clock::duration getIntervalAfter(const TickContext& context) const;

    mutable std::mutex transportMutex;
    std::condition_variable transportCondition;
    std::thread timingThread;
    bool shouldExit = false;

    State state = State::stopped;
    double bpm = 120.0;
    double swingAmount = 0.0;
    int ticksPerBeat = 4;
    std::uint64_t frame = 0;

    mutable std::mutex callbackMutex;
    EvaluationCallback evaluationCallback;
    TickCallback tickCallback;
};
}
