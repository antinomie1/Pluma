#pragma once

// SHA1 file verification, backed by a small self-contained SHA-1 in hash.cpp
// (no external crypto library). Header exposes only std::string/std::uint64_t.
#include <cstdint>
#include <string>

namespace net {

// Streams `path` through SHA1 in small chunks and returns the lowercase hex
// digest, or "" if the file couldn't be opened/read.
std::string Sha1File(const std::string& path);

// The "already downloaded, skip it" check the download pipeline uses before
// touching the network for any artifact: cheapest-first checks (file exists,
// then its size matches `expected_size`) before the expensive SHA1 pass.
// `expected_size` of 0 skips the size precheck (unknown/not reported).
bool VerifyFile(const std::string& path, std::uint64_t expected_size,
                const std::string& expected_sha1);

// Derives Minecraft's offline-mode player UUID for `name`, matching the
// vanilla client's UUID.nameUUIDFromBytes(("OfflinePlayer:" + name).getBytes):
// an MD5 of that string with the RFC-4122 version-3/variant bits set,
// formatted as the canonical 8-4-4-4-12 hex string. Backed by a small
// self-contained MD5 in hash.cpp (same "no external crypto" reasoning as the
// SHA-1 above -- offline UUIDs are not security-sensitive, just a well-known
// deterministic mapping the ecosystem agrees on).
std::string OfflineUuid(const std::string& name);

} // namespace net
