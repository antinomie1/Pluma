/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#pragma once

#include <string>
#include <filesystem>

namespace util {
namespace fs = std::filesystem;

/** Lowercase hex SHA-1 of file contents. Empty on failure. */
std::string sha1_file(const fs::path& path);

/** Lowercase hex SHA-1 of memory buffer. */
std::string sha1_bytes(const void* data, size_t len);

bool sha1_equals_file(const fs::path& path, const std::string& expected_hex);
}
