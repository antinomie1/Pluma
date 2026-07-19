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
#include <vector>
#include <filesystem>

namespace util {

namespace fs = std::filesystem;

std::string narrow(const std::wstring& w);
std::wstring widen(const std::string& s);

bool file_exists(const fs::path& p);
bool dir_exists(const fs::path& p);
std::string read_text_file(const fs::path& p);
bool write_text_file(const fs::path& p, const std::string& data);

std::vector<std::string> split(const std::string& s, char delim);
std::string join(const std::vector<std::string>& parts, const std::string& sep);
std::string replace_all(std::string s, const std::string& from, const std::string& to);
std::string trim(const std::string& s);

/** Java-style UUID.nameUUIDFromBytes for OfflinePlayer:<name> */
std::string offline_player_uuid(const std::string& username);

/** Quote a Windows command-line argument. */
std::string quote_arg(const std::string& arg);

/** Extract zip/jar into dest using Windows tar. */
bool extract_zip(const fs::path& zip, const fs::path& dest, std::string* err);

/** Open a folder in Explorer. */
void open_folder(const fs::path& dir);

std::string get_env(const char* name);

} // namespace util
