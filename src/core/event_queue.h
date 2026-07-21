#pragma once

#include "core/types.h"

#include <mutex>
#include <vector>

namespace core {

// Multiple-producer (GLFW callbacks on the main thread), single-consumer (the
// render thread) input queue. drain() swaps the internal buffer out, so the
// lock is held only for the swap and never while events are processed.
class EventQueue {
public:
    void push(const InputEvent& event);

    // Moves all queued events into `out` (cleared first). Reuses `out`'s
    // allocation for the next batch to avoid per-frame churn.
    void drain(std::vector<InputEvent>& out);

private:
    std::mutex mutex_;
    std::vector<InputEvent> buffer_;
};

} // namespace core
