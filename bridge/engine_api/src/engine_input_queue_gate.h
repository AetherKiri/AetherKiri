#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
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
        event.type == ENGINE_INPUT_EVENT_POINTER_DOWN) {
      if (primary_down_pointer_id_.has_value()) {
        // Godot can expose one physical press through both its global input
        // callback and a Control callback. A duplicate DOWN for the pointer
        // that already owns the press must not quarantine that same pointer:
        // its following UP is the only edge that can close the accepted DOWN.
        if (*primary_down_pointer_id_ != event.pointer_id) {
          suppressed_pointer_ids_.insert(event.pointer_id);
        }
        return false;
      }
      if (queued_primary_gestures_ >= kMaxQueuedPrimaryGestures) {
        suppressed_pointer_ids_.insert(event.pointer_id);
        return false;
      }

      // Pointer ids are reused (the desktop mouse is always zero and the
      // first mobile contact commonly is too). A missing UP from an older,
      // rejected contact must not poison the next accepted gesture.
      suppressed_pointer_ids_.erase(event.pointer_id);
      primary_down_pointer_id_ = event.pointer_id;
      return true;
    }

    if (primary_pointer &&
        event.type == ENGINE_INPUT_EVENT_POINTER_MOVE &&
        suppressed_pointer_ids_.count(event.pointer_id) != 0 &&
        (!primary_down_pointer_id_.has_value() ||
         *primary_down_pointer_id_ != event.pointer_id)) {
      return false;
    }

    if (primary_pointer && event.type == ENGINE_INPUT_EVENT_POINTER_UP) {
      if (primary_down_pointer_id_.has_value() &&
          *primary_down_pointer_id_ == event.pointer_id) {
        primary_down_pointer_id_.reset();
        suppressed_pointer_ids_.erase(event.pointer_id);
        ++queued_primary_gestures_;
        return true;
      }
      if (suppressed_pointer_ids_.erase(event.pointer_id) != 0) {
        return false;
      }
      return false;
    }
    return true;
  }

  void on_dequeued(const engine_input_event_t& event) {
    if (event.type == ENGINE_INPUT_EVENT_POINTER_UP && event.button == 0) {
      if (queued_primary_gestures_ != 0) --queued_primary_gestures_;
    }
  }

  void reset() {
    primary_down_pointer_id_.reset();
    queued_primary_gestures_ = 0;
    suppressed_pointer_ids_.clear();
  }

 private:
  static constexpr size_t kMaxQueuedPrimaryGestures = 8;
  std::optional<int32_t> primary_down_pointer_id_;
  size_t queued_primary_gestures_ = 0;
  std::unordered_set<int32_t> suppressed_pointer_ids_;
};

}  // namespace aetherkiri::engine_api
