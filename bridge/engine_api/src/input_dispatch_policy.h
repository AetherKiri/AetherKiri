#pragma once

#include <cstdint>

#include "engine_api.h"

inline bool EngineInputCompletesAction(uint32_t event_type) {
  return event_type == ENGINE_INPUT_EVENT_POINTER_UP ||
         event_type == ENGINE_INPUT_EVENT_POINTER_SCROLL ||
         event_type == ENGINE_INPUT_EVENT_KEY_UP ||
         event_type == ENGINE_INPUT_EVENT_BACK;
}
