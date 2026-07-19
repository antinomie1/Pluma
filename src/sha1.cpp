/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#include "sha1.h"
#include "util.h"

#include <windows.h>
#include <bcrypt.h>

#include <fstream>
#include <vector>
#include <cctype>

#pragma comment(lib, "bcrypt.lib")

namespace util {

namespace {

std::string to_hex(const unsigned char* dig, size_t n)
{
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.resize(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out[i * 2] = hex[dig[i] >> 4];
        out[i * 2 + 1] = hex[dig[i] & 0xf];
    }
    return out;
}

std::string normalize_hex(std::string s)
{
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

} // namespace

std::string sha1_bytes(const void* data, size_t len)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;
    DWORD obj_len = 0, cb = 0, hash_len = 0;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0)
        return {};
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj_len, sizeof(obj_len), &cb, 0) != 0)
        goto cleanup;
    if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_len, sizeof(hash_len), &cb, 0) != 0)
        goto cleanup;

    {
        std::vector<UCHAR> obj(obj_len);
        std::vector<UCHAR> dig(hash_len);
        if (BCryptCreateHash(alg, &hash, obj.data(), obj_len, nullptr, 0, 0) != 0)
            goto cleanup;
        if (BCryptHashData(hash, (PUCHAR)data, (ULONG)len, 0) != 0)
            goto cleanup;
        if (BCryptFinishHash(hash, dig.data(), hash_len, 0) != 0)
            goto cleanup;
        result = to_hex(dig.data(), hash_len);
    }

cleanup:
    if (hash)
        BCryptDestroyHash(hash);
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    return result;
}

std::string sha1_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::vector<char> buf(1 << 16);
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;
    DWORD obj_len = 0, cb = 0, hash_len = 0;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0)
        return {};
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj_len, sizeof(obj_len), &cb, 0) != 0)
        goto cleanup;
    if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_len, sizeof(hash_len), &cb, 0) != 0)
        goto cleanup;

    {
        std::vector<UCHAR> obj(obj_len);
        std::vector<UCHAR> dig(hash_len);
        if (BCryptCreateHash(alg, &hash, obj.data(), obj_len, nullptr, 0, 0) != 0)
            goto cleanup;
        while (in) {
            in.read(buf.data(), (std::streamsize)buf.size());
            std::streamsize n = in.gcount();
            if (n > 0) {
                if (BCryptHashData(hash, (PUCHAR)buf.data(), (ULONG)n, 0) != 0)
                    goto cleanup;
            }
        }
        if (BCryptFinishHash(hash, dig.data(), hash_len, 0) != 0)
            goto cleanup;
        result = to_hex(dig.data(), hash_len);
    }

cleanup:
    if (hash)
        BCryptDestroyHash(hash);
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    return result;
}

bool sha1_equals_file(const fs::path& path, const std::string& expected_hex)
{
    if (expected_hex.empty())
        return file_exists(path); // no hash -> presence only
    auto got = sha1_file(path);
    if (got.empty())
        return false;
    return got == normalize_hex(expected_hex);
}

} // namespace util
