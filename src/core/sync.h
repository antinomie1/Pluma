#pragma once

#include <condition_variable>
#include <mutex>

namespace core {

// Mutex-guarded single value for cross-thread publish/snapshot. Used for
// WindowMetrics (platform -> render) and logic State (logic -> render).
template <typename T>
class SharedValue {
public:
    SharedValue() = default;
    explicit SharedValue(const T& initial) : value_(initial) {}

    void store(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ = value;
    }

    T load() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }

    // Read-modify-write under the lock, for partial updates.
    template <typename Fn>
    void update(Fn&& fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        fn(value_);
    }

private:
    mutable std::mutex mutex_;
    T value_{};
};

// Binary gate for parking a thread until signalled. The render thread waits on
// it while the window is minimised; the platform layer opens it on restore and
// stops it at shutdown.
class Gate {
public:
    // Blocks while closed. Returns false once stop() has been called, signalling
    // the waiter to exit.
    bool wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return open_ || stopped_; });
        return !stopped_;
    }

    void open() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            open_ = true;
        }
        cv_.notify_all();
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = false;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool open_ = true;
    bool stopped_ = false;
};

} // namespace core
