#include "core/event_queue.h"

namespace core {

void EventQueue::push(const InputEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.push_back(event);
}

void EventQueue::drain(std::vector<InputEvent>& out) {
    out.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.swap(out);
}

} // namespace core
