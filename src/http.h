/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace http {

namespace fs = std::filesystem;

struct DownloadProgress {
    uint64_t downloaded = 0;
    uint64_t total = 0; // 0 if unknown
};

using ProgressFn = std::function<void(const DownloadProgress&)>;

/** HTTPS/HTTP GET entire body as string. Returns empty and sets err on failure. */
std::string get_string(const std::string& url, std::string* err = nullptr, int timeout_ms = 60000);

/**
 * Download URL to path (creates parent dirs).
 * If expected_sha1 non-empty, skip download when file already matches.
 */
bool download_file(const std::string& url,
                   const fs::path& path,
                   const std::string& expected_sha1 = {},
                   std::string* err = nullptr,
                   ProgressFn progress = {},
                   int timeout_ms = 120000);

} // namespace http
