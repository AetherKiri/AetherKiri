#pragma once

#include <cstdint>
#include <unordered_set>

#include "engine_api.h"

namespace aetherkiri::engine_api {

// A click can synchronously run an expensive KAG transition. Once a complete
// primary click is queued for the next engine tick, coalesce further primary
// gestures instead of replaying stale clicks after the transition finishes.
class PrimaryClickQueueGate {
 public:
  bool should_enqueue(const engine_input_event_t& event) {
    const bool primary_pointer = event.button == 0;
    if (primary_pointer &&
        event.type == ENGINE_INPUT_EVENT_POINTER_DOWN &&
        primary_release_pending_) {
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
      primary_release_pending_ = true;
    }
    return true;
  }

  void on_dequeued(const engine_input_event_t& event) {
    if (event.type == ENGINE_INPUT_EVENT_POINTER_UP && event.button == 0) {
      primary_release_pending_ = false;
    }
  }

  void reset() {
    primary_release_pending_ = false;
    suppressed_pointer_ids_.clear();
  }

 private:
  bool primary_release_pending_ = false;
  std::unordered_set<int32_t> suppressed_pointer_ids_;
};

}  // namespace aetherkiri::engine_api
