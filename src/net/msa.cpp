#include "net/msa.h"

#include "net/http.h"

#include <simdjson.h>

#include <chrono>
#include <string>
#include <string_view>
#include <thread>

namespace net {
namespace {

// Endpoints (see the "编写启动器" wiki tutorial's Microsoft-login section).
constexpr const char* kDeviceCodeUrl =
    "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
constexpr const char* kTokenUrl =
    "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
constexpr const char* kXblUrl = "https://user.auth.xboxlive.com/user/authenticate";
constexpr const char* kXstsUrl = "https://xsts.auth.xboxlive.com/xsts/authorize";
constexpr const char* kMcLoginUrl =
    "https://api.minecraftservices.com/authentication/login_with_xbox";
constexpr const char* kMcProfileUrl = "https://api.minecraftservices.com/minecraft/profile";

const std::vector<std::string> kFormHeaders = {
    "Content-Type: application/x-www-form-urlencoded"};
const std::vector<std::string> kJsonHeaders = {"Content-Type: application/json",
                                               "Accept: application/json"};

// Pulls a top-level string field out of a JSON body; returns "" if absent.
std::string JsonString(simdjson::dom::element doc, const char* key) {
    std::string_view sv;
    if (doc[key].get(sv)) return std::string();
    return std::string(sv);
}

bool Is2xx(long status) { return status >= 200 && status < 300; }

// Human-readable reason from an error response body: the OAuth
// error_description / error fields, the Xbox XErr code, else a trimmed body
// snippet. Surfaces WHY a request failed (e.g. the AADSTS code behind a 401)
// instead of just the HTTP status.
std::string ErrorDetail(const std::string& body) {
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    if (!parser.parse(body).get(doc)) {
        std::string d = JsonString(doc, "error_description");
        if (!d.empty()) return d;
        d = JsonString(doc, "error");
        if (!d.empty()) return d;
        d = JsonString(doc, "Message"); // Xbox Live / XSTS put the reason here
        int64_t xerr = 0;
        const bool has_xerr = !doc["XErr"].get(xerr);
        if (!d.empty()) return has_xerr ? (d + " (XErr " + std::to_string(xerr) + ")") : d;
        if (has_xerr) return "XErr " + std::to_string(xerr);
    }
    if (body.empty()) return std::string();
    return body.size() > 300 ? body.substr(0, 300) : body;
}

// "(HTTP <status>) <detail>" for a failed response. Status 0 means the transfer
// never completed (connect/TLS/timeout/reset) -- report curl's transport reason
// then, since there's no HTTP body.
std::string HttpFail(const HttpResponse& resp) {
    if (resp.status == 0) {
        return "no response: " +
               (resp.error.empty() ? std::string("connection failed") : resp.error);
    }
    return "HTTP " + std::to_string(resp.status) + ": " + ErrorDetail(resp.body);
}

// Sleeps `seconds` in short slices, returning false as soon as cancel flips.
bool SleepCancellable(int seconds, const std::atomic<bool>& cancel) {
    for (int i = 0; i < seconds * 10; ++i) {
        if (cancel.load(std::memory_order_relaxed)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return true;
}

// --- Individual auth steps. Each returns "" for the wanted token/value on
// failure and writes a short reason into `error` (left empty on success). ---

struct DeviceCode {
    std::string device_code, user_code, verification_uri, message;
    int interval = 5;
    int expires_in = 900;
    std::string error;
};

DeviceCode RequestDeviceCode(const std::string& client_id) {
    DeviceCode dc;
    const std::string body = "client_id=" + UrlEncode(client_id) +
                             "&scope=" + UrlEncode("XboxLive.signin offline_access");
    const HttpResponse resp = HttpRequest("POST", kDeviceCodeUrl, body, kFormHeaders);
    if (!Is2xx(resp.status)) {
        dc.error = "device code request failed (" + HttpFail(resp) + ")";
        return dc;
    }
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    if (parser.parse(resp.body).get(doc)) {
        dc.error = "device code response parse error";
        return dc;
    }
    dc.device_code = JsonString(doc, "device_code");
    dc.user_code = JsonString(doc, "user_code");
    dc.verification_uri = JsonString(doc, "verification_uri");
    dc.message = JsonString(doc, "message");
    int64_t v = 0;
    if (!doc["interval"].get(v)) dc.interval = static_cast<int>(v);
    if (!doc["expires_in"].get(v)) dc.expires_in = static_cast<int>(v);
    if (dc.device_code.empty() || dc.user_code.empty()) {
        dc.error = "device code response incomplete";
    }
    return dc;
}

struct MsToken {
    std::string access_token, refresh_token;
    enum class Poll { Success, Pending, Failed } poll = Poll::Failed;
    std::string error;
};

MsToken PollToken(const std::string& client_id, const std::string& device_code) {
    MsToken t;
    const std::string body =
        "grant_type=" + UrlEncode("urn:ietf:params:oauth:grant-type:device_code") +
        "&client_id=" + UrlEncode(client_id) + "&device_code=" + UrlEncode(device_code);
    const HttpResponse resp = HttpRequest("POST", kTokenUrl, body, kFormHeaders);

    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    if (parser.parse(resp.body).get(doc)) {
        t.error = "token response parse error";
        return t;
    }
    if (Is2xx(resp.status)) {
        t.access_token = JsonString(doc, "access_token");
        t.refresh_token = JsonString(doc, "refresh_token");
        t.poll = t.access_token.empty() ? MsToken::Poll::Failed : MsToken::Poll::Success;
        if (t.access_token.empty()) t.error = "token response had no access_token";
        return t;
    }
    // Non-2xx: an OAuth error object. "authorization_pending"/"slow_down" mean
    // keep polling; anything else is terminal.
    const std::string err = JsonString(doc, "error");
    if (err == "authorization_pending" || err == "slow_down") {
        t.poll = MsToken::Poll::Pending;
    } else {
        t.poll = MsToken::Poll::Failed;
        t.error = "token poll failed (" + HttpFail(resp) + ")";
    }
    return t;
}

// XBL -> (token, userhash).
struct Xbl {
    std::string token, uhs, error;
};
Xbl AuthenticateXbl(const std::string& ms_access_token) {
    Xbl x;
    const std::string body =
        "{\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\","
        "\"RpsTicket\":\"d=" +
        ms_access_token +
        "\"},\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}";
    const HttpResponse resp = HttpRequest("POST", kXblUrl, body, kJsonHeaders);
    if (!Is2xx(resp.status)) {
        x.error = "Xbox Live auth failed (" + HttpFail(resp) + ")";
        return x;
    }
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    if (parser.parse(resp.body).get(doc)) {
        x.error = "Xbox Live response parse error";
        return x;
    }
    x.token = JsonString(doc, "Token");
    simdjson::dom::array xui;
    if (!doc["DisplayClaims"]["xui"].get(xui)) {
        for (simdjson::dom::element claim : xui) {
            std::string_view uhs;
            if (!claim["uhs"].get(uhs)) {
                x.uhs = std::string(uhs);
                break;
            }
        }
    }
    if (x.token.empty() || x.uhs.empty()) x.error = "Xbox Live response incomplete";
    return x;
}

// XSTS -> token (reuses the XBL user hash).
std::string AuthorizeXsts(const std::string& xbl_token, std::string& error) {
    const std::string body =
        "{\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"" + xbl_token +
        "\"]},\"RelyingParty\":\"rp://api.minecraftservices.com/\",\"TokenType\":\"JWT\"}";
    const HttpResponse resp = HttpRequest("POST", kXstsUrl, body, kJsonHeaders);
    if (!Is2xx(resp.status)) {
        error = "XSTS authorization failed (" + HttpFail(resp) + ")";
        return std::string();
    }
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    if (parser.parse(resp.body).get(doc)) {
        error = "XSTS response parse error";
        return std::string();
    }
    const std::string token = JsonString(doc, "Token");
    if (token.empty()) error = "XSTS response had no token";
    return token;
}

// Minecraft services login -> Minecraft access token.
std::string LoginMinecraft(const std::string& uhs, const std::string& xsts_token,
                           std::string& error) {
    const std::string body =
        "{\"identityToken\":\"XBL3.0 x=" + uhs + ";" + xsts_token + "\"}";
    const HttpResponse resp = HttpRequest("POST", kMcLoginUrl, body, kJsonHeaders);
    if (!Is2xx(resp.status)) {
        error = "Minecraft login failed (" + HttpFail(resp) + ")";
        return std::string();
    }
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    if (parser.parse(resp.body).get(doc)) {
        error = "Minecraft login response parse error";
        return std::string();
    }
    const std::string token = JsonString(doc, "access_token");
    if (token.empty()) error = "Minecraft login had no access_token";
    return token;
}

// Profile -> (uuid, name). A 404 here means the account doesn't own Minecraft.
bool FetchProfile(const std::string& mc_token, std::string& uuid, std::string& name,
                  std::string& error) {
    const std::vector<std::string> headers = {"Authorization: Bearer " + mc_token};
    const HttpResponse resp = HttpRequest("GET", kMcProfileUrl, std::string(), headers);
    if (resp.status == 404) {
        error = "this account does not own Minecraft";
        return false;
    }
    if (!Is2xx(resp.status)) {
        error = "profile fetch failed (" + HttpFail(resp) + ")";
        return false;
    }
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    if (parser.parse(resp.body).get(doc)) {
        error = "profile response parse error";
        return false;
    }
    uuid = JsonString(doc, "id");
    name = JsonString(doc, "name");
    if (uuid.empty() || name.empty()) {
        error = "profile response incomplete";
        return false;
    }
    return true;
}

} // namespace

MsaLogin::~MsaLogin() { StopThread(); }

void MsaLogin::StopThread() {
    cancel_.store(true, std::memory_order_relaxed);
    if (thread_.joinable()) thread_.join();
}

void MsaLogin::Start(const std::string& client_id) {
    StopThread();
    cancel_.store(false, std::memory_order_relaxed);
    LoginState s;
    s.phase = LoginState::Phase::Requesting;
    state_.store(s);
    thread_ = std::thread(&MsaLogin::Run, this, client_id);
}

void MsaLogin::Cancel() {
    StopThread();
    LoginState s;
    s.phase = LoginState::Phase::Cancelled;
    state_.store(s);
}

void MsaLogin::Reset() { state_.store(LoginState{}); }

void MsaLogin::Run(std::string client_id) {
    auto fail = [this](const std::string& msg) {
        LoginState s;
        s.phase = LoginState::Phase::Error;
        s.error = msg;
        state_.store(s);
    };

    const DeviceCode dc = RequestDeviceCode(client_id);
    if (cancel_.load(std::memory_order_relaxed)) return;
    if (!dc.error.empty()) {
        fail(dc.error);
        return;
    }

    {
        LoginState s;
        s.phase = LoginState::Phase::AwaitingUser;
        s.user_code = dc.user_code;
        s.verification_uri = dc.verification_uri;
        s.message = dc.message;
        state_.store(s);
    }

    // Poll for consent until the device code expires.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(dc.expires_in);
    std::string ms_access_token, ms_refresh_token;
    for (;;) {
        if (!SleepCancellable(dc.interval, cancel_)) return;
        if (std::chrono::steady_clock::now() >= deadline) {
            fail("login timed out -- code expired");
            return;
        }
        const MsToken t = PollToken(client_id, dc.device_code);
        if (cancel_.load(std::memory_order_relaxed)) return;
        if (t.poll == MsToken::Poll::Success) {
            ms_access_token = t.access_token;
            ms_refresh_token = t.refresh_token;
            break;
        }
        if (t.poll == MsToken::Poll::Failed) {
            fail(t.error.empty() ? "login was declined" : t.error);
            return;
        }
        // Pending -- keep the code shown and loop.
    }

    {
        LoginState s = state_.load();
        s.phase = LoginState::Phase::Authenticating;
        state_.store(s);
    }

    const Xbl xbl = AuthenticateXbl(ms_access_token);
    if (cancel_.load(std::memory_order_relaxed)) return;
    if (!xbl.error.empty()) {
        fail(xbl.error);
        return;
    }

    std::string error;
    const std::string xsts = AuthorizeXsts(xbl.token, error);
    if (cancel_.load(std::memory_order_relaxed)) return;
    if (!error.empty()) {
        fail(error);
        return;
    }

    const std::string mc_token = LoginMinecraft(xbl.uhs, xsts, error);
    if (cancel_.load(std::memory_order_relaxed)) return;
    if (!error.empty()) {
        fail(error);
        return;
    }

    std::string uuid, name;
    if (!FetchProfile(mc_token, uuid, name, error)) {
        fail(error);
        return;
    }
    if (cancel_.load(std::memory_order_relaxed)) return;

    LoginState s;
    s.phase = LoginState::Phase::Success;
    s.name = name;
    s.uuid = uuid;
    s.access_token = mc_token;
    s.refresh_token = ms_refresh_token;
    state_.store(s);
}

} // namespace net
