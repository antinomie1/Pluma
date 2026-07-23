#include "platform/game_monitor.h"

#include <chrono>

namespace platform {

namespace {
constexpr auto kTickPeriod = std::chrono::milliseconds(50); // 20 Hz
}

GameMonitor::~GameMonitor() { stop(); }

void GameMonitor::TrackGame(const std::string& name, ProcessHandle handle) {
    if (handle == 0) return;
    std::lock_guard<std::mutex> lock(games_mutex_);
    games_.push_back(Game{name, handle});
}

void GameMonitor::start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread(&GameMonitor::run, this);
}

void GameMonitor::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void GameMonitor::run() {
    using clock = std::chrono::steady_clock;
    auto next = clock::now();

    while (running_.load(std::memory_order_relaxed)) {
        GameMonitorState state;

        // Poll the watched game processes: keep the still-running ones (and
        // publish their names), drop + release the ones that have exited.
        {
            std::lock_guard<std::mutex> lock(games_mutex_);
            for (auto it = games_.begin(); it != games_.end();) {
                if (IsProcessRunning(it->handle)) {
                    state.running_games.push_back(it->name);
                    ++it;
                } else {
                    CloseProcessHandle(it->handle);
                    it = games_.erase(it);
                }
            }
        }

        state_.store(state);

        next += kTickPeriod;
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_until(lock, next, [this] { return !running_.load(std::memory_order_relaxed); });
    }
}

} // namespace platform
