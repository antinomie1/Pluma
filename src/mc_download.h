/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class DownloadMirror {
    Official = 0,
    BMCLAPI = 1,
};

enum class RemoteVersionType {
    Release,
    Snapshot,
    OldBeta,
    OldAlpha,
    Other,
};

struct RemoteVersion {
    std::string id;
    std::string type; // release / snapshot / ...
    std::string url;
    std::string sha1;
    RemoteVersionType type_enum = RemoteVersionType::Other;
};

struct DownloadStatus {
    bool busy = false;
    bool cancel_requested = false;
    float overall_progress = 0.f; // 0..1
    std::string phase;            // e.g. "libraries"
    std::string current_file;
    std::string message;
    std::string error;
    int files_done = 0;
    int files_total = 0;
};

class McDownloadService {
public:
    McDownloadService() = default;
    ~McDownloadService();

    McDownloadService(const McDownloadService&) = delete;
    McDownloadService& operator=(const McDownloadService&) = delete;

    /** Fetch remote version list (blocking). */
    bool fetch_manifest(DownloadMirror mirror, std::string* err);

    const std::vector<RemoteVersion>& remote_versions() const { return remote_versions_; }
    std::string latest_release() const { return latest_release_; }
    std::string latest_snapshot() const { return latest_snapshot_; }

    /** Start background install of version_id into game_dir. */
    bool start_install(const std::string& game_dir,
                       const std::string& version_id,
                       DownloadMirror mirror);

    void request_cancel();
    bool busy() const { return busy_.load(); }

    DownloadStatus snapshot() const;

    static std::string mirror_url(DownloadMirror m, const std::string& original_url);

private:
    void worker(std::string game_dir, std::string version_id, DownloadMirror mirror);
    void set_status(const DownloadStatus& s);
    bool should_cancel() const;

    std::vector<RemoteVersion> remote_versions_;
    std::string latest_release_;
    std::string latest_snapshot_;

    mutable std::mutex mu_;
    DownloadStatus status_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancel_{false};
    std::thread thread_;
};
