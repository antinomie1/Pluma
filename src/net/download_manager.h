#pragma once

// Orchestrates the Minecraft install pipeline (version JSON -> client.jar ->
// libraries -> asset index -> asset objects) and the version-manifest fetch,
// structured like platform::GameMonitor (src/platform/game_monitor.h):
// start()/stop() own
// every thread this class spawns, and every cross-thread read the render
// thread does goes through core::SharedValue snapshots -- never a raw lock
// the caller has to know about.
//
// Never touches config::Config -- see CLAUDE.md's render-thread-exclusive,
// no-lock config contract. The render thread reads config and assembles an
// InstallParams to hand over instead (EnqueueInstall's parameter).
#include "core/sync.h"
#include "net/types.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace net {

class DownloadManager {
public:
    DownloadManager() = default;
    ~DownloadManager();

    DownloadManager(const DownloadManager&) = delete;
    DownloadManager& operator=(const DownloadManager&) = delete;

    // Starts the periodic task-publishing thread. Individual install/manifest
    // threads are spawned on demand (EnqueueInstall/RefreshManifest) rather
    // than up front.
    void start();
    // Cancels every live task (so their threads exit promptly instead of
    // stop() blocking on an in-progress multi-gigabyte transfer) and joins
    // every thread this instance ever spawned.
    void stop();

    // Fetches + parses the version manifest asynchronously. A no-op while a
    // previous refresh is still in flight (manifest().status == Loading).
    void RefreshManifest(MirrorMode mirror);
    ManifestSnapshot manifest() const { return manifest_.load(); }

    // Starts a new install pipeline on its own thread; returns immediately
    // with the new task's id (usable with Pause/Resume/Cancel below).
    std::uint64_t EnqueueInstall(const VersionEntry& entry, const InstallParams& params);
    std::vector<TaskInfo> tasks() const { return tasks_.load(); }
    void Pause(std::uint64_t id);
    void Resume(std::uint64_t id);
    // Aborts the task's transfers and recursively deletes its
    // <game_dir>/versions/<instance>/ directory; shared libraries/assets that
    // already passed SHA1 verification are left alone. The task then drops
    // out of tasks().
    void Cancel(std::uint64_t id);

    // Per-task live state. Held via shared_ptr so a task can keep running (and
    // being published) after EnqueueInstall returns, and so Pause/Resume/
    // Cancel can find it by id independent of its driver thread's lifetime.
    // Forward-declared only -- its definition lives entirely in
    // download_manager.cpp, so it stays opaque to every actual caller of this
    // header (ui/render). Public (rather than private) purely so that .cpp's
    // free-function helpers (e.g. RunFileJobs) can name
    // std::shared_ptr<Task> in their own signatures -- C++ access control
    // applies to every use of a qualified name, not just the type's own
    // out-of-line member definition.
    struct Task;

private:
    void PublishLoop();
    void ManifestLoop(MirrorMode mirror);
    void RunInstall(std::shared_ptr<Task> task);

    core::SharedValue<ManifestSnapshot> manifest_;
    core::SharedValue<std::vector<TaskInfo>> tasks_;

    std::atomic<bool> running_{false};

    // Publisher: the one always-on thread (mirrors platform::GameMonitor's tick
    // thread), recomputing every task's speed_bps and republishing the
    // TaskInfo snapshot on a fixed interval.
    std::thread publish_thread_;
    std::mutex publish_wait_mutex_;
    std::condition_variable publish_cv_;

    // Manifest refresh: at most one in-flight thread at a time.
    std::mutex manifest_thread_mutex_;
    std::thread manifest_thread_;

    // Install tasks: all_tasks_ is the lookup table Pause/Resume/Cancel search
    // by id (entries are never erased -- a finished/cancelled task is just
    // filtered out of tasks() via Task::removed); driver_threads_ is only
    // joined, never searched.
    std::mutex tasks_mutex_;
    std::vector<std::shared_ptr<Task>> all_tasks_;
    std::mutex driver_mutex_;
    std::vector<std::thread> driver_threads_;

    std::atomic<std::uint64_t> next_task_id_{1};
};

} // namespace net
