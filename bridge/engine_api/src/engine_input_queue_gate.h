#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>

#include "engine_api.h"

namespace aetherkiri::engine_api {

// A click can synchronously run an expensive KAG transition. Keep a bounded
// number of complete primary gestures so taps delivered while the engine is
// busy are replayed in order, while still preventing an unbounded stale-click
// backlog after a transition or modal closes.
class PrimaryClickQueueGate {
 public:
  bool should_enqueue(const engine_input_event_t& event) {
    const bool primary_pointer = event.button == 0;
    if (primary_pointer &&
        event.type == ENGINE_INPUT_EVENT_POINTER_DOWN &&
        (primary_down_pending_ ||
         queued_primary_gestures_ >= kMaxQueuedPrimaryGestures)) {
      suppressed_pointer_ids_.insert(event.pointer_id);
      return false;
    }

    if (primary_pointer &&
        event.type == ENGINE_INPUT_EVENT_POINTER_MOVE &&
        suppressed_pointer_ids_.count(event.pointer_id) != 0) {
      return false;
    }

    if (primary_pointer && event.type == ENGINE_INPUT_EVENT_POINTER_UP) {
      if (suppressed_pointer_ids_.erase(event.pointer_id) != 0) {
        return false;
      }
      if (!primary_down_pending_) return false;
      primary_down_pending_ = false;
      ++queued_primary_gestures_;
    }
    if (primary_pointer && event.type == ENGINE_INPUT_EVENT_POINTER_DOWN) {
      primary_down_pending_ = true;
    }
    return true;
  }

  void on_dequeued(const engine_input_event_t& event) {
    if (event.type == ENGINE_INPUT_EVENT_POINTER_DOWN &&
        event.button == 0) {
      primary_down_pending_ = false;
    }
    if (event.type == ENGINE_INPUT_EVENT_POINTER_UP && event.button == 0) {
      if (queued_primary_gestures_ != 0) --queued_primary_gestures_;
    }
  }

  void reset() {
    primary_down_pending_ = false;
    queued_primary_gestures_ = 0;
    suppressed_pointer_ids_.clear();
  }

 private:
  static constexpr size_t kMaxQueuedPrimaryGestures = 8;
  bool primary_down_pending_ = false;
  size_t queued_primary_gestures_ = 0;
  std::unordered_set<int32_t> suppressed_pointer_ids_;
};

}  // namespace aetherkiri::engine_api
