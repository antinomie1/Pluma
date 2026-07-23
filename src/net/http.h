#pragma once

// libcurl wrapper: the only file in `net` that ever includes <curl/curl.h>.
// Exposes plain std::string/std::vector/std::function/std::atomic<bool>* --
// no CURL*/CURLM* types leak into this header, matching net's "UI/render
// link net but never see curl" convention.
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace net {

// Progress/cancel/pause hooks a caller supplies to DownloadFile(). `cancel`
// and `pause` are polled from libcurl's xferinfo callback (so on the same
// thread DownloadFile() runs on), not written to by DownloadFile() itself --
// the caller (DownloadManager) owns them per-task and flips them from the
// render thread.
struct DownloadHooks {
    std::function<void(std::uint64_t delta_bytes)> on_progress;
    std::atomic<bool>* cancel = nullptr;
    std::atomic<bool>* pause = nullptr;
};

// Tries `candidate_urls` in order (mirror first, official fallback -- see
// net::CandidateUrls) and returns the first response body that succeeds, or
// std::nullopt if every candidate failed. Used for the version manifest and
// per-version JSON, which are small enough to hold entirely in memory.
std::optional<std::string> HttpGetString(const std::vector<std::string>& candidate_urls);

// One request's outcome: the HTTP status code (0 if the transfer never
// completed) and the response body. Used by the Microsoft-login flow, which
// needs to see the status/body of individual POSTs (a 400 with an
// "authorization_pending" body is a normal poll result, not a failure).
struct HttpResponse {
    long status = 0;      // HTTP status, or 0 if the transfer never completed
    std::string body;
    std::string error;    // libcurl transport error (e.g. connect/TLS/timeout) when status == 0
};

// A single HTTP request. `method` is "GET"/"POST"; `body` is sent as-is (empty
// for GET); each `headers` entry is a full "Name: value" line (e.g.
// "Content-Type: application/json"). No mirror/retry logic -- these endpoints
// (Microsoft/Xbox/Minecraft auth) are fixed hosts, not mirrored downloads.
HttpResponse HttpRequest(const std::string& method, const std::string& url,
                         const std::string& body, const std::vector<std::string>& headers);

// application/x-www-form-urlencoded percent-encoding of one value (via curl's
// own escaper), for building auth form bodies safely.
std::string UrlEncode(const std::string& value);

// Sets the outbound proxy (e.g. "http://127.0.0.1:7890") applied to every
// subsequent request/download; "" disables it. curl doesn't use the OS system
// proxy on its own, so this is how the app routes through a local proxy (needed
// where *.xboxlive.com is blocked/tampered). Call from the render thread (reads
// the net.proxy config key); it's safe against the download worker threads.
void SetProxy(const std::string& proxy);

// Downloads one file, trying `candidate_urls` in order for the *first*
// segment probe (a later candidate is only tried if the current one's HEAD/
// Range probe fails outright -- once a candidate is confirmed reachable, the
// whole transfer runs against it). Splits into `segments` HTTP Range-based
// connections (driven by a single curl_multi handle on the calling thread --
// no extra OS threads) when the server supports Range and `expected_size`
// clears the segment-worthiness threshold; otherwise falls back to a single
// sequential connection. Verifies the finished file's SHA1 against
// `expected_sha1` (skipped if empty) and retries the whole download once on
// mismatch. Returns false on unrecoverable failure (all candidates
// exhausted, cancelled, or the retried SHA1 still mismatches).
bool DownloadFile(const std::vector<std::string>& candidate_urls, const std::string& dest,
                  std::uint64_t expected_size, const std::string& expected_sha1, int segments,
                  const DownloadHooks& hooks);

} // namespace net
