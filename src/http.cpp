/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#include "http.h"
#include "sha1.h"
#include "util.h"

#include <windows.h>
#include <winhttp.h>

#include <fstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace http {

namespace {

struct UrlParts {
    bool https = true;
    std::wstring host;
    INTERNET_PORT port = 0;
    std::wstring path;
};

bool parse_url(const std::string& url, UrlParts& out, std::string* err)
{
    std::wstring w = util::widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{};
    wchar_t path[4096]{};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 4096;
    if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc)) {
        if (err)
            *err = "URL 解析失败: " + url;
        return false;
    }
    out.https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    out.host.assign(host, uc.dwHostNameLength);
    out.port = uc.nPort;
    out.path.assign(path, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength && uc.lpszExtraInfo)
        out.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    return true;
}

class Session {
public:
    HINTERNET h_session = nullptr;
    Session()
    {
        h_session = WinHttpOpen(L"mc-minimal-launcher/0.1",
                                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS,
                                0);
    }
    ~Session()
    {
        if (h_session)
            WinHttpCloseHandle(h_session);
    }
    explicit operator bool() const { return h_session != nullptr; }
};

bool http_request(const std::string& url,
                  std::vector<char>& body,
                  uint64_t* content_length,
                  std::string* err,
                  ProgressFn progress,
                  int timeout_ms,
                  bool write_as_stream,
                  const fs::path& out_path)
{
    body.clear();
    UrlParts parts;
    if (!parse_url(url, parts, err))
        return false;

    Session session;
    if (!session) {
        if (err)
            *err = "WinHttpOpen 失败";
        return false;
    }

    WinHttpSetTimeouts(session.h_session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    HINTERNET h_connect = WinHttpConnect(session.h_session, parts.host.c_str(), parts.port, 0);
    if (!h_connect) {
        if (err)
            *err = "连接失败: " + util::narrow(parts.host);
        return false;
    }

    DWORD flags = parts.https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET h_request = WinHttpOpenRequest(
        h_connect, L"GET", parts.path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!h_request) {
        WinHttpCloseHandle(h_connect);
        if (err)
            *err = "OpenRequest 失败";
        return false;
    }

    // Follow redirects
    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(h_request, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));

    if (!WinHttpSendRequest(h_request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(h_request, nullptr)) {
        if (err)
            *err = "请求失败 (" + url + ") err=" + std::to_string(GetLastError());
        WinHttpCloseHandle(h_request);
        WinHttpCloseHandle(h_connect);
        return false;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(h_request,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                        WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300) {
        if (err)
            *err = "HTTP " + std::to_string(status) + " : " + url;
        WinHttpCloseHandle(h_request);
        WinHttpCloseHandle(h_connect);
        return false;
    }

    uint64_t total = 0;
    wchar_t cl_buf[64]{};
    DWORD cl_size = sizeof(cl_buf);
    if (WinHttpQueryHeaders(h_request, WINHTTP_QUERY_CONTENT_LENGTH,
                            WINHTTP_HEADER_NAME_BY_INDEX, cl_buf, &cl_size,
                            WINHTTP_NO_HEADER_INDEX)) {
        total = _wcstoui64(cl_buf, nullptr, 10);
    }
    if (content_length)
        *content_length = total;

    std::ofstream file;
    if (write_as_stream) {
        std::error_code ec;
        fs::create_directories(out_path.parent_path(), ec);
        fs::path tmp = out_path;
        tmp += ".part";
        file.open(tmp, std::ios::binary | std::ios::trunc);
        if (!file) {
            if (err)
                *err = "无法写入: " + tmp.string();
            WinHttpCloseHandle(h_request);
            WinHttpCloseHandle(h_connect);
            return false;
        }
    }

    uint64_t downloaded = 0;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(h_request, &avail)) {
            if (err)
                *err = "QueryDataAvailable 失败";
            WinHttpCloseHandle(h_request);
            WinHttpCloseHandle(h_connect);
            return false;
        }
        if (avail == 0)
            break;

        std::vector<char> chunk(avail);
        DWORD read = 0;
        if (!WinHttpReadData(h_request, chunk.data(), avail, &read)) {
            if (err)
                *err = "ReadData 失败";
            WinHttpCloseHandle(h_request);
            WinHttpCloseHandle(h_connect);
            return false;
        }
        if (read == 0)
            break;

        if (write_as_stream) {
            file.write(chunk.data(), read);
        } else {
            body.insert(body.end(), chunk.begin(), chunk.begin() + read);
        }
        downloaded += read;
        if (progress)
            progress(DownloadProgress{downloaded, total});
    }

    WinHttpCloseHandle(h_request);
    WinHttpCloseHandle(h_connect);

    if (write_as_stream) {
        file.close();
        fs::path tmp = out_path;
        tmp += ".part";
        std::error_code ec;
        fs::remove(out_path, ec);
        fs::rename(tmp, out_path, ec);
        if (ec) {
            if (err)
                *err = "重命名失败: " + out_path.string();
            return false;
        }
    }
    return true;
}

} // namespace

std::string get_string(const std::string& url, std::string* err, int timeout_ms)
{
    std::vector<char> body;
    if (!http_request(url, body, nullptr, err, {}, timeout_ms, false, {}))
        return {};
    return std::string(body.begin(), body.end());
}

bool download_file(const std::string& url,
                   const fs::path& path,
                   const std::string& expected_sha1,
                   std::string* err,
                   ProgressFn progress,
                   int timeout_ms)
{
    if (!expected_sha1.empty() && util::sha1_equals_file(path, expected_sha1)) {
        if (progress) {
            auto sz = util::file_exists(path) ? (uint64_t)fs::file_size(path) : 0;
            progress(DownloadProgress{sz, sz});
        }
        return true;
    }

    std::vector<char> unused;
    if (!http_request(url, unused, nullptr, err, progress, timeout_ms, true, path))
        return false;

    if (!expected_sha1.empty() && !util::sha1_equals_file(path, expected_sha1)) {
        if (err)
            *err = "SHA1 校验失败: " + path.filename().string();
        std::error_code ec;
        fs::remove(path, ec);
        return false;
    }
    return true;
}

} // namespace http
