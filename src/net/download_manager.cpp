#include "net/download_manager.h"

#include "net/hash.h"
#include "net/http.h"
#include "net/manifest.h"
#include "net/mirror.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace net {
namespace {

constexpr const char* kOfficialManifestUrl =
    "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
constexpr double kPublishIntervalSeconds = 0.5;

// One file to fetch-and-verify, already resolved to its final destination
// path and candidate (mirror-first) URLs -- built fresh per install phase
// from the parsed VersionDetail/AssetObject data, never persisted.
struct FileJob {
    std::vector<std::string> candidate_urls;
    std::string dest;
    std::uint64_t size = 0;
    std::string sha1;
};

} // namespace

// Per-task live state. Plain data + atomics read/written across the task's
// driver thread and DownloadManager's publish/control-plane calls -- `error`
// is the one non-atomic field, so it's guarded by its own small mutex rather
// than folded into tasks_mutex_ (which only protects all_tasks_' membership,
// not a task's own fields).
struct DownloadManager::Task {
    std::uint64_t id = 0;
    std::string name;
    VersionEntry entry;
    InstallParams params;

    std::atomic<bool> cancel_flag{false};
    std::atomic<bool> pause_flag{false};
    // Set once Cancel()'s cleanup has removed the task's on-disk directory --
    // PublishLoop drops it from tasks() from that point on.
    std::atomic<bool> removed{false};

    std::atomic<int> phase{static_cast<int>(TaskInfo::Phase::Json)};
    std::atomic<int> status{static_cast<int>(TaskInfo::Status::Queued)};
    std::atomic<std::uint64_t> bytes_done{0};
    std::atomic<std::uint64_t> bytes_total{0};
    std::atomic<int> files_done{0};
    std::atomic<int> files_total{0};

    mutable std::mutex error_mutex;
    std::string error;

    void SetError(std::string message) {
        std::lock_guard<std::mutex> lock(error_mutex);
        error = std::move(message);
    }
    std::string GetError() const {
        std::lock_guard<std::mutex> lock(error_mutex);
        return error;
    }
};

namespace {

// Runs `jobs` through up to params.concurrency worker threads pulled off a
// shared index (simple work-stealing queue), each skipping already-verified
// files (net::VerifyFile) and otherwise calling net::DownloadFile with
// params.threads_per_file Range segments. Updates task's bytes_done/
// files_done live as jobs complete. Returns false if the task was cancelled
// (caller checks task->cancel_flag to distinguish that from a genuine
// failure) or if any job failed outright.
bool RunFileJobs(const std::shared_ptr<DownloadManager::Task>& task, std::vector<FileJob> jobs,
                 const InstallParams& params) {
    if (jobs.empty()) return true;

    std::mutex index_mutex;
    std::size_t next_index = 0;
    std::atomic<bool> any_failed{false};

    auto worker = [&] {
        for (;;) {
            if (task->cancel_flag.load(std::memory_order_relaxed)) return;

            FileJob job;
            {
                std::lock_guard<std::mutex> lock(index_mutex);
                if (next_index >= jobs.size()) return;
                job = jobs[next_index++];
            }

            if (net::VerifyFile(job.dest, job.size, job.sha1)) {
                task->files_done.fetch_add(1, std::memory_order_relaxed);
                task->bytes_done.fetch_add(job.size, std::memory_order_relaxed);
                continue;
            }

            net::DownloadHooks hooks;
            hooks.cancel = &task->cancel_flag;
            hooks.pause = &task->pause_flag;
            hooks.on_progress = [task](std::uint64_t delta) {
                task->bytes_done.fetch_add(delta, std::memory_order_relaxed);
            };
            const bool ok = net::DownloadFile(job.candidate_urls, job.dest, job.size, job.sha1,
                                             params.threads_per_file, hooks);
            if (ok) {
                task->files_done.fetch_add(1, std::memory_order_relaxed);
            } else if (!task->cancel_flag.load(std::memory_order_relaxed)) {
                any_failed.store(true, std::memory_order_relaxed);
            }
        }
    };

    const int worker_count =
        std::max(1, std::min(params.concurrency, static_cast<int>(jobs.size())));
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(worker_count));
    for (int i = 0; i < worker_count; ++i) pool.emplace_back(worker);
    for (std::thread& t : pool) t.join();

    if (task->cancel_flag.load(std::memory_order_relaxed)) return false;
    return !any_failed.load(std::memory_order_relaxed);
}

} // namespace

DownloadManager::~DownloadManager() { stop(); }

void DownloadManager::start() {
    if (running_.exchange(true)) return;
    publish_thread_ = std::thread(&DownloadManager::PublishLoop, this);
}

void DownloadManager::stop() {
    if (!running_.exchange(false)) return;

    publish_cv_.notify_all();
    if (publish_thread_.joinable()) publish_thread_.join();

    // Signal every live task to stop so their driver threads exit promptly
    // instead of the joins below blocking on an in-progress transfer.
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (auto& task : all_tasks_) {
            task->cancel_flag.store(true, std::memory_order_relaxed);
            task->pause_flag.store(false, std::memory_order_relaxed);
        }
    }
    {
        std::lock_guard<std::mutex> lock(driver_mutex_);
        for (std::thread& t : driver_threads_) {
            if (t.joinable()) t.join();
        }
        driver_threads_.clear();
    }

    std::lock_guard<std::mutex> manifest_lock(manifest_thread_mutex_);
    if (manifest_thread_.joinable()) manifest_thread_.join();
}

void DownloadManager::PublishLoop() {
    // Per-task last-seen byte counter, purely local to this thread -- used to
    // turn the raw cumulative bytes_done into a per-interval speed_bps.
    std::unordered_map<std::uint64_t, std::uint64_t> last_bytes;

    while (running_.load(std::memory_order_relaxed)) {
        std::vector<TaskInfo> snapshot;
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            snapshot.reserve(all_tasks_.size());
            for (const std::shared_ptr<Task>& task : all_tasks_) {
                if (task->removed.load(std::memory_order_relaxed)) continue;

                TaskInfo info;
                info.id = task->id;
                info.name = task->name;
                info.phase = static_cast<TaskInfo::Phase>(task->phase.load(std::memory_order_relaxed));
                const auto stored_status =
                    static_cast<TaskInfo::Status>(task->status.load(std::memory_order_relaxed));
                // Pausing doesn't flip Task::status itself (the driver thread
                // just blocks inside net::DownloadFile) -- surface it here
                // instead, purely for display.
                info.status = (stored_status == TaskInfo::Status::Running &&
                              task->pause_flag.load(std::memory_order_relaxed))
                                  ? TaskInfo::Status::Paused
                                  : stored_status;
                info.bytes_done = task->bytes_done.load(std::memory_order_relaxed);
                info.bytes_total = task->bytes_total.load(std::memory_order_relaxed);
                info.files_done = task->files_done.load(std::memory_order_relaxed);
                info.files_total = task->files_total.load(std::memory_order_relaxed);
                info.error = task->GetError();

                const auto prev_it = last_bytes.find(task->id);
                const std::uint64_t prev = prev_it != last_bytes.end() ? prev_it->second : info.bytes_done;
                const std::uint64_t delta = info.bytes_done > prev ? info.bytes_done - prev : 0;
                info.speed_bps = static_cast<double>(delta) / kPublishIntervalSeconds;
                last_bytes[task->id] = info.bytes_done;

                snapshot.push_back(std::move(info));
            }
        }
        tasks_.store(snapshot);

        std::unique_lock<std::mutex> wait_lock(publish_wait_mutex_);
        publish_cv_.wait_for(wait_lock, std::chrono::duration<double>(kPublishIntervalSeconds),
                             [this] { return !running_.load(std::memory_order_relaxed); });
    }
}

void DownloadManager::RefreshManifest(MirrorMode mirror) {
    if (manifest_.load().status == ManifestSnapshot::Status::Loading) {
        return; // a previous refresh is still in flight
    }
    manifest_.update([](ManifestSnapshot& m) { m.status = ManifestSnapshot::Status::Loading; });

    std::lock_guard<std::mutex> lock(manifest_thread_mutex_);
    // Safe to join without blocking meaningfully: the Loading check above
    // only passes once the previous ManifestLoop() has already stored a
    // terminal status as its very last step, so the thread is effectively
    // already finished by the time we get here.
    if (manifest_thread_.joinable()) manifest_thread_.join();
    manifest_thread_ = std::thread(&DownloadManager::ManifestLoop, this, mirror);
}

void DownloadManager::ManifestLoop(MirrorMode mirror) {
    const std::vector<std::string> urls = net::CandidateUrls(kOfficialManifestUrl, mirror);
    const std::optional<std::string> body = net::HttpGetString(urls);
    if (!body) {
        manifest_.update([](ManifestSnapshot& m) {
            m.status = ManifestSnapshot::Status::Error;
            m.error = "failed to fetch version manifest";
        });
        return;
    }
    manifest_.store(net::ParseManifest(*body));
}

std::uint64_t DownloadManager::EnqueueInstall(const VersionEntry& entry, const InstallParams& params) {
    auto task = std::make_shared<Task>();
    task->id = next_task_id_.fetch_add(1, std::memory_order_relaxed);
    task->name = params.instance_name;
    task->entry = entry;
    task->params = params;
    task->status.store(static_cast<int>(TaskInfo::Status::Queued), std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        all_tasks_.push_back(task);
    }
    {
        std::lock_guard<std::mutex> lock(driver_mutex_);
        driver_threads_.emplace_back([this, task] { RunInstall(task); });
    }
    return task->id;
}

void DownloadManager::Pause(std::uint64_t id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    for (const std::shared_ptr<Task>& task : all_tasks_) {
        if (task->id == id) {
            task->pause_flag.store(true, std::memory_order_relaxed);
            return;
        }
    }
}

void DownloadManager::Resume(std::uint64_t id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    for (const std::shared_ptr<Task>& task : all_tasks_) {
        if (task->id == id) {
            task->pause_flag.store(false, std::memory_order_relaxed);
            return;
        }
    }
}

void DownloadManager::Cancel(std::uint64_t id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    for (const std::shared_ptr<Task>& task : all_tasks_) {
        if (task->id == id) {
            task->pause_flag.store(false, std::memory_order_relaxed); // wake a paused worker so it observes cancel
            task->cancel_flag.store(true, std::memory_order_relaxed);
            return;
        }
    }
}

void DownloadManager::RunInstall(std::shared_ptr<Task> task) {
    namespace fs = std::filesystem;

    task->status.store(static_cast<int>(TaskInfo::Status::Running), std::memory_order_relaxed);
    task->phase.store(static_cast<int>(TaskInfo::Phase::Json), std::memory_order_relaxed);

    const fs::path instance_dir =
        fs::path(task->params.game_dir) / "versions" / task->params.instance_name;
    const fs::path version_json_path = instance_dir / (task->params.instance_name + ".json");
    const fs::path client_jar_path = instance_dir / (task->params.instance_name + ".jar");

    auto fail = [&](const std::string& message) {
        task->SetError(message);
        task->status.store(static_cast<int>(TaskInfo::Status::Error), std::memory_order_relaxed);
    };
    // Cancel cleanup: only the *instance's own* versions/<name>/ directory is
    // removed -- shared libraries/assets that already passed SHA1
    // verification are left in place for reuse by other instances (plan's
    // "取消清理" requirement).
    auto cancel_cleanup = [&] {
        std::error_code ec;
        fs::remove_all(instance_dir, ec);
        task->removed.store(true, std::memory_order_relaxed);
    };

    // 1. Version JSON -- the exact bytes fetched are written to disk as-is;
    // this is the file the launcher itself will later read.
    const std::vector<std::string> json_urls = net::CandidateUrls(task->entry.url, task->params.mirror);
    const std::optional<std::string> json_body = net::HttpGetString(json_urls);
    if (task->cancel_flag.load(std::memory_order_relaxed)) { cancel_cleanup(); return; }
    if (!json_body) { fail("failed to fetch version JSON"); return; }

    std::error_code ec;
    fs::create_directories(instance_dir, ec);
    {
        std::ofstream out(version_json_path, std::ios::binary | std::ios::trunc);
        if (!out) { fail("failed to write version JSON"); return; }
        out.write(json_body->data(), static_cast<std::streamsize>(json_body->size()));
    }
    task->files_done.fetch_add(1, std::memory_order_relaxed);
    task->bytes_done.fetch_add(json_body->size(), std::memory_order_relaxed);

    const VersionDetail detail = net::ParseVersionJson(*json_body);
    if (detail.client.url.empty()) { fail("version JSON missing downloads.client"); return; }

    // Running totals: json + client + libraries + asset index are known now;
    // asset objects are added once the asset index itself has been fetched
    // and parsed (step 4) -- bytes_total/files_total simply grow at that
    // point, which the progress bar tolerates fine (it's read live).
    task->files_total.store(2 + static_cast<int>(detail.libraries.size()) + 1,
                            std::memory_order_relaxed);
    std::uint64_t bytes_total =
        static_cast<std::uint64_t>(json_body->size()) + detail.client.size + detail.asset_index.size;
    for (const LibraryArtifact& lib : detail.libraries) bytes_total += lib.size;
    task->bytes_total.store(bytes_total, std::memory_order_relaxed);

    // 2. client.jar -- a single (potentially large) file, so its own
    // threads_per_file Range segments do the parallelism here rather than
    // RunFileJobs' worker count (which only matters once there's more than
    // one file in flight).
    task->phase.store(static_cast<int>(TaskInfo::Phase::Client), std::memory_order_relaxed);
    {
        std::vector<FileJob> jobs;
        jobs.push_back(FileJob{net::CandidateUrls(detail.client.url, task->params.mirror),
                               client_jar_path.string(), detail.client.size, detail.client.sha1});
        if (!RunFileJobs(task, std::move(jobs), task->params)) {
            if (task->cancel_flag.load(std::memory_order_relaxed)) { cancel_cleanup(); return; }
            fail("failed to download client.jar");
            return;
        }
    }

    // 3. Libraries -- already OS-rule-filtered by ParseVersionJson(). Already-
    // verified files (shared with a previous install) are skipped inside
    // RunFileJobs via net::VerifyFile.
    task->phase.store(static_cast<int>(TaskInfo::Phase::Libraries), std::memory_order_relaxed);
    {
        std::vector<FileJob> jobs;
        jobs.reserve(detail.libraries.size());
        for (const LibraryArtifact& lib : detail.libraries) {
            const std::string dest = (fs::path(task->params.game_dir) / "libraries" / lib.path).string();
            jobs.push_back(FileJob{net::CandidateUrls(lib.url, task->params.mirror), dest, lib.size, lib.sha1});
        }
        if (!RunFileJobs(task, std::move(jobs), task->params)) {
            if (task->cancel_flag.load(std::memory_order_relaxed)) { cancel_cleanup(); return; }
            fail("failed to download one or more libraries");
            return;
        }
    }

    // 4. Asset index, then every referenced asset object.
    task->phase.store(static_cast<int>(TaskInfo::Phase::Assets), std::memory_order_relaxed);
    const fs::path asset_index_path =
        fs::path(task->params.game_dir) / "assets" / "indexes" / (detail.asset_index.id + ".json");
    {
        std::vector<FileJob> jobs;
        jobs.push_back(FileJob{net::CandidateUrls(detail.asset_index.url, task->params.mirror),
                               asset_index_path.string(), detail.asset_index.size,
                               detail.asset_index.sha1});
        if (!RunFileJobs(task, std::move(jobs), task->params)) {
            if (task->cancel_flag.load(std::memory_order_relaxed)) { cancel_cleanup(); return; }
            fail("failed to download asset index");
            return;
        }
    }

    std::ifstream index_in(asset_index_path, std::ios::binary);
    std::ostringstream index_buffer;
    index_buffer << index_in.rdbuf();
    const std::vector<AssetObject> objects = net::ParseAssetIndex(index_buffer.str());

    task->files_total.fetch_add(static_cast<int>(objects.size()), std::memory_order_relaxed);
    std::uint64_t assets_bytes = 0;
    for (const AssetObject& obj : objects) assets_bytes += obj.size;
    task->bytes_total.fetch_add(assets_bytes, std::memory_order_relaxed);

    {
        std::vector<FileJob> jobs;
        jobs.reserve(objects.size());
        for (const AssetObject& obj : objects) {
            if (obj.hash.size() < 2) continue; // malformed entry -- skip rather than crash on substr
            const std::string h2 = obj.hash.substr(0, 2);
            const std::string dest =
                (fs::path(task->params.game_dir) / "assets" / "objects" / h2 / obj.hash).string();
            const std::string official_url = "https://resources.download.minecraft.net/" + h2 + "/" + obj.hash;
            jobs.push_back(FileJob{net::CandidateUrls(official_url, task->params.mirror), dest,
                                   obj.size, obj.hash});
        }
        if (!RunFileJobs(task, std::move(jobs), task->params)) {
            if (task->cancel_flag.load(std::memory_order_relaxed)) { cancel_cleanup(); return; }
            fail("failed to download one or more assets");
            return;
        }
    }

    task->phase.store(static_cast<int>(TaskInfo::Phase::Done), std::memory_order_relaxed);
    task->status.store(static_cast<int>(TaskInfo::Status::Done), std::memory_order_relaxed);
}

} // namespace net
