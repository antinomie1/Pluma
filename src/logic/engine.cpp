#include "logic/engine.h"

#include <chrono>

namespace logic {

namespace {
constexpr auto kTickPeriod = std::chrono::milliseconds(50); // 20 Hz
}

Engine::~Engine() { stop(); }

void Engine::start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread(&Engine::run, this);
}

void Engine::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Engine::run() {
    using clock = std::chrono::steady_clock;
    const auto started = clock::now();
    auto next = clock::now();
    std::uint64_t ticks = 0;

    while (running_.load(std::memory_order_relaxed)) {
        ++ticks;
        State state;
        state.ticks = ticks;
        state.uptime_seconds = std::chrono::duration<double>(clock::now() - started).count();
        state_.store(state);

        next += kTickPeriod;
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_until(lock, next, [this] { return !running_.load(std::memory_order_relaxed); });
    }
}

} // namespace logic
