#pragma once

// Game-process monitor: ticks on its own thread at a fixed rate, polling the
// handles of launched game processes and republishing the still-running set for
// the UI. The render thread hands a freshly-spawned process here via
// TrackGame(); this thread polls them and closes each handle once its process
// exits.
//
// Formerly the standalone `logic` module -- folded into platform since its one
// real job is OS process-liveness polling (platform::IsProcessRunning /
// CloseProcessHandle), squarely the platform layer's domain. Cross-thread
// publish/snapshot goes through core::SharedValue (monitor thread -> render).
#include "core/sync.h"
#include "platform/process.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace platform {

// The still-running game instances, published to the UI (read every frame on
// the render thread via GameMonitor::snapshot()).
struct GameMonitorState {
    std::vector<std::string> running_games;
};

class GameMonitor {
public:
    GameMonitor() = default;
    ~GameMonitor();

    GameMonitor(const GameMonitor&) = delete;
    GameMonitor& operator=(const GameMonitor&) = delete;

    void start();
    void stop();

    GameMonitorState snapshot() const { return state_.load(); }

    // Registers a just-launched game process to monitor. Called from the render
    // thread (ui::BuildFrame's launch handler); the monitor thread takes over
    // polling and closing the handle once the process exits. Thread-safe.
    void TrackGame(const std::string& name, ProcessHandle handle);

private:
    void run();

    core::SharedValue<GameMonitorState> state_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::condition_variable cv_;

    // Watch list of live game processes. Guarded by its own mutex since
    // TrackGame() (render thread) appends while run() (monitor thread) polls.
    struct Game {
        std::string name;
        ProcessHandle handle;
    };
    std::mutex games_mutex_;
    std::vector<Game> games_;
};

} // namespace platform
