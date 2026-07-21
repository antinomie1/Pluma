#pragma once

#include "core/sync.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace logic {

// Business-logic state published to the UI. Skeleton only: a tick counter and
// uptime prove the logic thread is live and decoupled from rendering.
struct State {
    std::uint64_t ticks = 0;
    double uptime_seconds = 0.0;
};

// Ticks on its own thread at a fixed rate, independent of the render loop.
class Engine {
public:
    Engine() = default;
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void start();
    void stop();

    State snapshot() const { return state_.load(); }

private:
    void run();

    core::SharedValue<State> state_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace logic
