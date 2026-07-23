#pragma once

// Microsoft (Xbox Live) device-code login, producing a Minecraft profile +
// access token. The full chain -- Microsoft OAuth device code -> XBL -> XSTS ->
// Minecraft services login -> profile -- runs on a background thread (it's all
// blocking network I/O and polling), publishing progress through a
// core::SharedValue<LoginState> the render thread snapshots each frame.
//
// Header stays third-party-free (plain std::string/enum, like net/types.h) so
// ui/render can link net and drive the login without seeing curl/simdjson.
//
// Threading: MsaLogin owns its own worker thread; Start()/state()/Cancel()/
// Reset() are all safe to call from the render thread. On Success the render
// thread reads the profile out of state() and commits the account through
// config itself (the worker never touches config -- same contract as
// net::DownloadManager).
#include "core/sync.h"

#include <atomic>
#include <string>
#include <thread>

namespace net {

struct LoginState {
    enum class Phase {
        Idle,          // nothing running
        Requesting,    // asking Microsoft for a device code
        AwaitingUser,  // user_code/verification_uri shown; polling for consent
        Authenticating,// consent given; running XBL/XSTS/Minecraft steps
        Success,       // name/uuid/access_token/refresh_token are filled
        Error,         // error is filled
        Cancelled,
    };
    Phase phase = Phase::Idle;

    // Shown to the user while AwaitingUser.
    std::string user_code;
    std::string verification_uri;
    std::string message; // Microsoft-provided human instruction text

    // Filled on Success.
    std::string name;
    std::string uuid;
    std::string access_token;  // Minecraft access token (for launch --accessToken)
    std::string refresh_token; // Microsoft refresh token (for re-auth later)

    std::string error; // filled on Error
};

class MsaLogin {
public:
    MsaLogin() = default;
    ~MsaLogin();

    MsaLogin(const MsaLogin&) = delete;
    MsaLogin& operator=(const MsaLogin&) = delete;

    // Starts a fresh login on a background thread using `client_id`. If a login
    // is already running it is cancelled and joined first.
    void Start(const std::string& client_id);

    // Latest snapshot for the UI.
    LoginState state() const { return state_.load(); }

    // Signals the worker to stop and joins it; leaves state at Cancelled.
    void Cancel();

    // Returns to Idle (call after committing a Success, or to dismiss an Error).
    void Reset();

private:
    void Run(std::string client_id);
    void StopThread();

    core::SharedValue<LoginState> state_;
    std::thread thread_;
    std::atomic<bool> cancel_{false};
};

} // namespace net
