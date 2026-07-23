#include "net/http.h"

#include "net/hash.h"

#include <curl/curl.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string_view>
#include <thread>

namespace net {
namespace {

// Segments smaller than this aren't worth splitting -- the connection-setup
// overhead of extra curl easy handles would outweigh any parallelism gain.
constexpr std::uint64_t kSegmentThreshold = 1024ull * 1024ull; // 1 MiB

constexpr long kConnectTimeoutSeconds = 15L;
constexpr const char* kUserAgent = "Pluma/1.0";

// Process-level CURL share handle: lets every easy handle -- even ones created
// and destroyed per-file across the worker pool -- reuse pooled connections
// (keep-alive), resumed TLS sessions, and cached DNS lookups. Without this,
// each of the thousands of tiny asset files pays a fresh TCP+TLS handshake.
// A share used from multiple threads MUST supply lock/unlock callbacks; one
// mutex per CURL_LOCK_DATA kind (indexed by the enum value curl passes in).
std::mutex g_share_locks[CURL_LOCK_DATA_LAST];
CURLSH* g_share = nullptr;

// Optional outbound proxy (e.g. a local Clash/V2Ray endpoint) applied to every
// network easy handle -- curl does NOT pick up the Windows system proxy on its
// own, so direct connections to blocked/tampered hosts (notably *.xboxlive.com)
// fail with SSL/connect errors. Set from the render thread via net::SetProxy
// (from the net.proxy config key); read by worker threads when building a
// handle, so it's mutex-guarded.
std::mutex g_proxy_mutex;
std::string g_proxy;

std::string CurrentProxy() {
    std::lock_guard<std::mutex> lock(g_proxy_mutex);
    return g_proxy;
}

void ShareLock(CURL*, curl_lock_data data, curl_lock_access, void*) {
    g_share_locks[data].lock();
}

void ShareUnlock(CURL*, curl_lock_data data, void*) {
    g_share_locks[data].unlock();
}

// Process-level curl_global_init/cleanup, run exactly once no matter how many
// times HttpGetString()/DownloadFile() are called -- constructed as a
// function-local static so C++11's thread-safe magic-statics guarantee covers
// the "exactly once, even if the first calls race across worker threads"
// requirement without a separate mutex.
void EnsureGlobalInit() {
    struct Guard {
        Guard() {
            // Pin the TLS backend to each platform's native stack (must precede
            // curl_global_init(); has no effect on a single-backend build, so
            // it's harmless where there's nothing to choose).
            //
            // Windows -> Schannel: it validates against the OS certificate
            // store with zero configuration. (An earlier build pinned mbedTLS
            // here, which has no access to the Windows trust store and no
            // default CA bundle, so every HTTPS handshake failed peer-cert
            // verification -- the "version list refresh failed" symptom.)
            // Linux -> OpenSSL, which uses the distro's system CA store.
#ifdef _WIN32
            curl_global_sslset(CURLSSLBACKEND_SCHANNEL, nullptr, nullptr);
#else
            curl_global_sslset(CURLSSLBACKEND_OPENSSL, nullptr, nullptr);
#endif
            curl_global_init(CURL_GLOBAL_DEFAULT);

            // Share the connection cache, TLS sessions, and DNS cache across
            // every easy handle. Created here (after curl_global_init) so it
            // outlives every transfer and is torn down before global cleanup.
            g_share = curl_share_init();
            if (g_share != nullptr) {
                curl_share_setopt(g_share, CURLSHOPT_LOCKFUNC, ShareLock);
                curl_share_setopt(g_share, CURLSHOPT_UNLOCKFUNC, ShareUnlock);
                curl_share_setopt(g_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
                curl_share_setopt(g_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
                curl_share_setopt(g_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
            }
        }
        ~Guard() {
            if (g_share != nullptr) {
                curl_share_cleanup(g_share);
                g_share = nullptr;
            }
            curl_global_cleanup();
        }
    };
    static Guard guard;
    (void)guard;
}

// Attaches the process-level share handle to an easy handle so it draws from /
// contributes to the shared connection/TLS/DNS caches. No-op if the share
// failed to initialize.
void ApplyShare(CURL* curl) {
    if (g_share != nullptr) curl_easy_setopt(curl, CURLOPT_SHARE, g_share);
    const std::string proxy = CurrentProxy();
    if (!proxy.empty()) curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
}

std::size_t DiscardBody(char*, std::size_t size, std::size_t nmemb, void*) {
    return size * nmemb;
}

size_t AppendToString(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// Parses a "Content-Range: bytes 0-0/12345" response header for the total
// size after the '/' -- the only way to learn the full size of a resource
// from a 206 Partial Content response (its own Content-Length header only
// describes the partial body just returned, not the whole file).
std::size_t CaptureContentRange(char* buffer, std::size_t size, std::size_t nitems, void* userdata) {
    auto* total = static_cast<std::uint64_t*>(userdata);
    const std::size_t bytes = size * nitems;
    const std::string_view line(buffer, bytes);
    constexpr std::string_view kPrefix = "content-range:";
    if (line.size() < kPrefix.size()) return bytes;
    std::string lower(line.substr(0, kPrefix.size()));
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower != kPrefix) return bytes;
    const std::size_t slash = line.find('/');
    if (slash != std::string_view::npos) {
        *total = std::strtoull(std::string(line.substr(slash + 1)).c_str(), nullptr, 10);
    }
    return bytes;
}

// Result of probing whether `url` supports byte-range requests, and its total
// size if it could be determined (from Content-Range on a 206, or
// Content-Length on a plain 200).
struct RangeProbe {
    bool range_supported = false;
    std::uint64_t total_size = 0;
};

RangeProbe ProbeRange(const std::string& url) {
    RangeProbe probe;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) return probe;
    ApplyShare(curl);

    std::uint64_t header_total = 0;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD-like: don't download the (partial) body
    curl_easy_setopt(curl, CURLOPT_RANGE, "0-0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, DiscardBody);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, CaptureContentRange);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_total);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);

    const CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        if (code == 206) {
            probe.range_supported = true;
            probe.total_size = header_total;
        } else if (code == 200) {
            curl_off_t len = -1;
            curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &len);
            if (len > 0) probe.total_size = static_cast<std::uint64_t>(len);
        }
    }
    curl_easy_cleanup(curl);
    return probe;
}

// One segment's worth of progress against the destination file, shared with
// its easy handle's write/xferinfo callbacks via CURLOPT_WRITEDATA/
// CURLOPT_XFERINFODATA/CURLOPT_PRIVATE. `offset`/`length` describe this
// segment's byte range in the destination file (both 0 for the single-
// connection/unranged fallback, in which case the whole file is "segment 0"
// starting at 0 with an unknown length); `written` is how many bytes of that
// range have actually landed on disk so far, which also doubles as the
// resume point after a pause.
struct SegmentState {
    std::FILE* file = nullptr;
    std::uint64_t offset = 0;
    std::uint64_t length = 0; // 0 = unknown (unranged fallback)
    std::uint64_t written = 0;
    bool done = false;
    const DownloadHooks* hooks = nullptr;
};

void SeekSegment(std::FILE* file, std::uint64_t pos) {
#ifdef _WIN32
    _fseeki64(file, static_cast<long long>(pos), SEEK_SET);
#else
    fseeko(file, static_cast<off_t>(pos), SEEK_SET);
#endif
}

// Writes each chunk at its segment's current absolute file offset. Safe to
// share one FILE* across every segment's callback despite the seek+write not
// being atomic: curl_multi drives all of a multi handle's transfers
// cooperatively on a single thread (see RunSegments' poll loop below), so no
// two callbacks -- even for different easy handles -- ever run concurrently.
std::size_t WriteToSegment(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* seg = static_cast<SegmentState*>(userdata);
    const std::size_t total = size * nmemb;
    if (seg->hooks->cancel != nullptr && seg->hooks->cancel->load(std::memory_order_relaxed)) {
        return 0; // short write -> curl fails this transfer with CURLE_WRITE_ERROR
    }
    SeekSegment(seg->file, seg->offset + seg->written);
    const std::size_t wrote = std::fwrite(ptr, 1, total, seg->file);
    seg->written += wrote;
    if (seg->hooks->on_progress) seg->hooks->on_progress(wrote);
    return wrote;
}

// Checked far more often than the write callback fires (libcurl calls this on
// a timer, independent of data arrival), so this is where cancel/pause are
// actually caught promptly even on a stalled connection. Returning non-zero
// aborts the transfer with CURLE_ABORTED_BY_CALLBACK.
int XferInfo(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* seg = static_cast<SegmentState*>(userdata);
    if (seg->hooks->cancel != nullptr && seg->hooks->cancel->load(std::memory_order_relaxed)) return 1;
    if (seg->hooks->pause != nullptr && seg->hooks->pause->load(std::memory_order_relaxed)) return 1;
    return 0;
}

enum class Outcome { Success, Failed, Cancelled };

// Drives one or more segments of `url` to completion via a single curl_multi
// handle polled on the calling thread (no extra OS threads). On a pause, all
// active transfers are aborted (via XferInfo returning 1 above) and this
// function parks in a coarse sleep loop until pause clears or cancel fires,
// then re-issues only the segments that are still short -- each resuming
// from `written` bytes in via an adjusted Range header. `ranged` false means
// the unranged single-connection fallback (server didn't support Range, or
// its size couldn't be determined): a pause there can't be resumed
// byte-exactly, so it just restarts that one connection from scratch.
Outcome RunSegments(const std::string& url, std::vector<SegmentState>& segs,
                    const DownloadHooks& hooks, bool ranged) {
    for (;;) {
        CURLM* multi = curl_multi_init();
        std::vector<CURL*> handles;
        handles.reserve(segs.size());

        for (SegmentState& seg : segs) {
            if (seg.done) continue;
            CURL* easy = curl_easy_init();
            if (easy == nullptr) continue;
            ApplyShare(easy);
            curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
            curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, WriteToSegment);
            curl_easy_setopt(easy, CURLOPT_WRITEDATA, &seg);
            curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, XferInfo);
            curl_easy_setopt(easy, CURLOPT_XFERINFODATA, &seg);
            curl_easy_setopt(easy, CURLOPT_PRIVATE, &seg);
            curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
            curl_easy_setopt(easy, CURLOPT_USERAGENT, kUserAgent);
            if (ranged && seg.length > 0) {
                const std::uint64_t start = seg.offset + seg.written;
                const std::uint64_t end = seg.offset + seg.length - 1;
                char range[64];
                std::snprintf(range, sizeof(range), "%llu-%llu",
                              static_cast<unsigned long long>(start),
                              static_cast<unsigned long long>(end));
                curl_easy_setopt(easy, CURLOPT_RANGE, range);
            }
            curl_multi_add_handle(multi, easy);
            handles.push_back(easy);
        }

        if (handles.empty()) {
            curl_multi_cleanup(multi);
            return Outcome::Success; // every segment was already done (e.g. resumed with nothing left)
        }

        int still_running = 0;
        curl_multi_perform(multi, &still_running);
        while (still_running > 0) {
            int numfds = 0;
            curl_multi_poll(multi, nullptr, 0, 200, &numfds);
            curl_multi_perform(multi, &still_running);
        }

        bool any_hard_failure = false;
        CURLMsg* msg = nullptr;
        int msgs_left = 0;
        while ((msg = curl_multi_info_read(multi, &msgs_left)) != nullptr) {
            if (msg->msg != CURLMSG_DONE) continue;
            SegmentState* seg_ptr = nullptr;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &seg_ptr);
            if (msg->data.result == CURLE_OK) {
                if (seg_ptr != nullptr) seg_ptr->done = true;
            } else if (msg->data.result != CURLE_ABORTED_BY_CALLBACK) {
                any_hard_failure = true;
            }
        }
        for (CURL* easy : handles) {
            curl_multi_remove_handle(multi, easy);
            curl_easy_cleanup(easy);
        }
        curl_multi_cleanup(multi);

        if (hooks.cancel != nullptr && hooks.cancel->load(std::memory_order_relaxed)) {
            return Outcome::Cancelled;
        }
        if (hooks.pause != nullptr && hooks.pause->load(std::memory_order_relaxed)) {
            if (!ranged) {
                // Can't resume an unranged transfer mid-stream -- restart it
                // from byte 0 once unpaused rather than risk writing new
                // bytes at a stale offset over old ones.
                for (SegmentState& seg : segs) {
                    seg.written = 0;
                    seg.done = false;
                }
            }
            while (hooks.pause->load(std::memory_order_relaxed)) {
                if (hooks.cancel != nullptr && hooks.cancel->load(std::memory_order_relaxed)) {
                    return Outcome::Cancelled;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            continue; // resume: loop back and re-issue whatever is still incomplete
        }
        if (any_hard_failure) {
            return Outcome::Failed;
        }

        bool all_done = true;
        for (const SegmentState& seg : segs) {
            if (!seg.done) { all_done = false; break; }
        }
        if (all_done) return Outcome::Success;
        // A segment finished its curl transfer without an explicit error yet
        // never got marked done (e.g. the server closed early) -- treat as a
        // failure and let the caller's attempt loop decide whether to retry.
        return Outcome::Failed;
    }
}

// Preallocates `dest` to `total_size` bytes (a single seek+1-byte write to
// the last offset, not a real write of every byte) so every segment can
// seek+write independently from the very first callback, then runs the
// segmented (or single-connection) transfer and verifies the result.
// Truncates/recreates `dest` at the start of every attempt -- partial bytes
// from a failed prior attempt are not reused, only a live pause/resume within
// one RunSegments() call preserves progress.
Outcome TryDownloadFromUrl(const std::string& url, const std::string& dest,
                          std::uint64_t expected_size, const std::string& expected_sha1,
                          int segments, const DownloadHooks& hooks) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dest_path(dest);
    if (dest_path.has_parent_path()) {
        fs::create_directories(dest_path.parent_path(), ec);
    }

    // Only probe when segmenting could actually help: it's a full extra HTTP
    // round-trip whose sole purpose is to feed the Range-split path, which is
    // skipped for anything at/below kSegmentThreshold. The asset index already
    // gives expected_size, so for the thousands of tiny asset files (known
    // size, well under the threshold) the probe is pure latency -- skip it and
    // go straight to a single unranged transfer. Unknown size (0) still probes,
    // since it might turn out to be a large, segment-worthy file.
    const bool want_segments =
        segments > 1 && (expected_size == 0 || expected_size > kSegmentThreshold);
    RangeProbe probe;
    if (want_segments) probe = ProbeRange(url);
    const std::uint64_t total_size = probe.total_size > 0 ? probe.total_size : expected_size;
    const bool can_segment = probe.range_supported && total_size > kSegmentThreshold && segments > 1;
    const int actual_segments = can_segment ? segments : 1;

    // One initial attempt plus one retry (matches the plan's "verify SHA1,
    // delete+retry once on mismatch" -- extended here to also cover a hard
    // transfer failure, which is the same "start clean and try again" shape).
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (hooks.cancel != nullptr && hooks.cancel->load(std::memory_order_relaxed)) {
            return Outcome::Cancelled;
        }

        std::FILE* file = std::fopen(dest.c_str(), "w+b");
        if (file == nullptr) return Outcome::Failed;
        if (total_size > 0) {
            SeekSegment(file, total_size - 1);
            const char zero = 0;
            std::fwrite(&zero, 1, 1, file);
        }
        SeekSegment(file, 0);

        std::vector<SegmentState> seg_states(static_cast<std::size_t>(actual_segments));
        const std::uint64_t seg_size =
            can_segment ? (total_size / static_cast<std::uint64_t>(actual_segments)) : 0;
        for (int i = 0; i < actual_segments; ++i) {
            SegmentState& seg = seg_states[static_cast<std::size_t>(i)];
            seg.file = file;
            seg.hooks = &hooks;
            if (can_segment) {
                const std::uint64_t start = seg_size * static_cast<std::uint64_t>(i);
                const std::uint64_t end =
                    (i == actual_segments - 1) ? (total_size - 1) : (start + seg_size - 1);
                seg.offset = start;
                seg.length = end - start + 1;
            }
        }

        const Outcome outcome = RunSegments(url, seg_states, hooks, can_segment);
        std::fclose(file);

        if (outcome == Outcome::Cancelled) {
            fs::remove(dest, ec);
            return Outcome::Cancelled;
        }
        if (outcome != Outcome::Success) {
            continue; // hard failure -- retry once from scratch against the same URL
        }
        if (expected_sha1.empty() || VerifyFile(dest, total_size, expected_sha1)) {
            return Outcome::Success;
        }
        // SHA1 mismatch -- loop around for the one allotted retry.
    }

    fs::remove(dest, ec);
    return Outcome::Failed;
}

} // namespace

std::optional<std::string> HttpGetString(const std::vector<std::string>& candidate_urls) {
    EnsureGlobalInit();
    for (const std::string& url : candidate_urls) {
        CURL* curl = curl_easy_init();
        if (curl == nullptr) continue;
        ApplyShare(curl);

        std::string body;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AppendToString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);

        const CURLcode res = curl_easy_perform(curl);
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && code >= 200 && code < 300) {
            return body;
        }
    }
    return std::nullopt;
}

HttpResponse HttpRequest(const std::string& method, const std::string& url,
                         const std::string& body, const std::vector<std::string>& headers) {
    EnsureGlobalInit();
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) return response;
    ApplyShare(curl);

    curl_slist* header_list = nullptr;
    for (const std::string& h : headers) {
        header_list = curl_slist_append(header_list, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AppendToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    if (header_list != nullptr) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        // COPYPOSTFIELDS copies the body into curl's own buffer, so `body` need
        // not outlive the call; POSTFIELDSIZE must be set for bodies that may
        // contain NULs (none here, but it's the documented safe pairing).
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, body.c_str());
    }

    const CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    if (res != CURLE_OK) {
        response.status = 0;
        response.error = curl_easy_strerror(res);
    }
    if (header_list != nullptr) curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return response;
}

void SetProxy(const std::string& proxy) {
    std::lock_guard<std::mutex> lock(g_proxy_mutex);
    g_proxy = proxy;
}

std::string UrlEncode(const std::string& value) {
    EnsureGlobalInit();
    CURL* curl = curl_easy_init();
    if (curl == nullptr) return value;
    char* escaped = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
    std::string out = escaped != nullptr ? std::string(escaped) : value;
    if (escaped != nullptr) curl_free(escaped);
    curl_easy_cleanup(curl);
    return out;
}

bool DownloadFile(const std::vector<std::string>& candidate_urls, const std::string& dest,
                  std::uint64_t expected_size, const std::string& expected_sha1, int segments,
                  const DownloadHooks& hooks) {
    EnsureGlobalInit();
    for (const std::string& url : candidate_urls) {
        if (hooks.cancel != nullptr && hooks.cancel->load(std::memory_order_relaxed)) {
            return false;
        }
        const Outcome outcome =
            TryDownloadFromUrl(url, dest, expected_size, expected_sha1, segments, hooks);
        if (outcome == Outcome::Success) return true;
        if (outcome == Outcome::Cancelled) return false;
        // Failed -- fall through and try the next candidate URL, if any.
    }
    return false;
}

} // namespace net
