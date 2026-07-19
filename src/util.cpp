/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#include "util.h"

#include <windows.h>
#include <shellapi.h>

#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <array>
#include <vector>

namespace util {

std::string narrow(const std::wstring& w)
{
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring widen(const std::string& s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

bool file_exists(const fs::path& p)
{
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

bool dir_exists(const fs::path& p)
{
    std::error_code ec;
    return fs::is_directory(p, ec);
}

std::string read_text_file(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_text_file(const fs::path& p, const std::string& data)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary);
    if (!out)
        return false;
    out.write(data.data(), (std::streamsize)data.size());
    return (bool)out;
}

std::vector<std::string> split(const std::string& s, char delim)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

std::string join(const std::vector<std::string>& parts, const std::string& sep)
{
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out += sep;
        out += parts[i];
    }
    return out;
}

std::string replace_all(std::string s, const std::string& from, const std::string& to)
{
    if (from.empty())
        return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string trim(const std::string& s)
{
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n'))
        ++b;
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n'))
        --e;
    return s.substr(b, e - b);
}

// --- minimal MD5 (RFC 1321) for offline UUID ---
namespace {

struct MD5 {
    uint32_t a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476;
    uint64_t total = 0;
    uint8_t buf[64]{};
    size_t buf_len = 0;

    static uint32_t F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
    static uint32_t G(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }
    static uint32_t H(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
    static uint32_t I(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | ~z); }
    static uint32_t rot(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

    void process(const uint8_t block[64])
    {
        static const uint32_t T[64] = {
            0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
            0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
            0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
            0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
            0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
            0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
            0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
            0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
        };
        static const int S[64] = {
            7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
            5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
            4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
            6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
        };
        uint32_t X[16];
        for (int i = 0; i < 16; ++i)
            X[i] = (uint32_t)block[i * 4] | ((uint32_t)block[i * 4 + 1] << 8) |
                   ((uint32_t)block[i * 4 + 2] << 16) | ((uint32_t)block[i * 4 + 3] << 24);
        uint32_t A = a, B = b, C = c, D = d;
        for (int i = 0; i < 64; ++i) {
            uint32_t f, g;
            if (i < 16)      { f = F(B, C, D); g = (uint32_t)i; }
            else if (i < 32) { f = G(B, C, D); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = H(B, C, D); g = (3 * i + 5) % 16; }
            else             { f = I(B, C, D); g = (7 * i) % 16; }
            uint32_t tmp = D;
            D = C;
            C = B;
            B = B + rot(A + f + T[i] + X[g], S[i]);
            A = tmp;
        }
        a += A; b += B; c += C; d += D;
    }

    void update(const uint8_t* data, size_t len)
    {
        total += len;
        while (len > 0) {
            size_t n = 64 - buf_len;
            if (n > len)
                n = len;
            std::memcpy(buf + buf_len, data, n);
            buf_len += n;
            data += n;
            len -= n;
            if (buf_len == 64) {
                process(buf);
                buf_len = 0;
            }
        }
    }

    std::array<uint8_t, 16> finalize()
    {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        pad = 0;
        while (buf_len != 56)
            update(&pad, 1);
        uint8_t lenb[8];
        for (int i = 0; i < 8; ++i)
            lenb[i] = (uint8_t)((bits >> (8 * i)) & 0xff);
        update(lenb, 8);
        std::array<uint8_t, 16> dig{};
        uint32_t regs[4] = { a, b, c, d };
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                dig[i * 4 + j] = (uint8_t)((regs[i] >> (8 * j)) & 0xff);
        return dig;
    }
};

} // namespace

std::string offline_player_uuid(const std::string& username)
{
    const std::string key = "OfflinePlayer:" + username;
    MD5 md5;
    md5.update(reinterpret_cast<const uint8_t*>(key.data()), key.size());
    auto dig = md5.finalize();
    // UUID version 3
    dig[6] = (uint8_t)((dig[6] & 0x0f) | 0x30);
    dig[8] = (uint8_t)((dig[8] & 0x3f) | 0x80);
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out.push_back('-');
        out.push_back(hex[dig[i] >> 4]);
        out.push_back(hex[dig[i] & 0xf]);
    }
    return out;
}

std::string quote_arg(const std::string& arg)
{
    if (arg.find_first_of(" \t\"") == std::string::npos)
        return arg;
    std::string out = "\"";
    for (char c : arg) {
        if (c == '"')
            out += "\\\"";
        else
            out.push_back(c);
    }
    out.push_back('"');
    return out;
}

bool extract_zip(const fs::path& zip, const fs::path& dest, std::string* err)
{
    std::error_code ec;
    fs::create_directories(dest, ec);

    std::wstring wzip = widen(zip.string());
    std::wstring wdest = widen(dest.string());
    std::wstring line = L"tar -xf \"" + wzip + L"\" -C \"" + wdest + L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(line.begin(), line.end());
    buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        if (err)
            *err = "无法运行 tar 解压 natives（错误 " + std::to_string(GetLastError()) + "）";
        return false;
    }
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code != 0) {
        if (err)
            *err = "tar 解压失败，退出码 " + std::to_string(code);
        return false;
    }
    return true;
}

void open_folder(const fs::path& dir)
{
    ShellExecuteW(nullptr, L"open", widen(dir.string()).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string get_env(const char* name)
{
    wchar_t wname[256];
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 256);
    DWORD n = GetEnvironmentVariableW(wname, nullptr, 0);
    if (n == 0)
        return {};
    std::wstring w(n, L'\0');
    GetEnvironmentVariableW(wname, w.data(), n);
    if (!w.empty() && w.back() == L'\0')
        w.pop_back();
    return narrow(w);
}

} // namespace util
