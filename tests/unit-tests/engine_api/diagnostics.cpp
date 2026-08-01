#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "engine_api.h"

namespace {

struct Handle {
  engine_handle_t value = nullptr;

  Handle() {
    engine_create_desc_t desc{};
    desc.struct_size = sizeof(desc);
    desc.api_version = ENGINE_API_VERSION;
    desc.writable_path_utf8 = ".";
    desc.cache_path_utf8 = ".";
    REQUIRE(engine_create(&desc, &value) == ENGINE_RESULT_OK);
  }

  ~Handle() { engine_destroy(value); }
};

void Configure(Handle& handle, uint32_t max_events = 2000) {
  engine_diagnostic_config_t config{};
  config.struct_size = sizeof(config);
  config.enabled = 1;
  config.category_mask = ENGINE_DIAGNOSTIC_CATEGORY_ALL;
  config.slow_frame_threshold_us = 20000;
  config.max_events = max_events;
  config.host_monotonic_origin_us = 1000000;
  config.session_id_utf8 = "unit-test";
  REQUIRE(engine_set_diagnostic_config(handle.value, &config) == ENGINE_RESULT_OK);
}

std::string DrainDiagnostics(Handle& handle) {
  std::vector<char> buffer(256 * 1024);
  uint32_t written = 0;
  REQUIRE(engine_drain_diagnostic_events(
              handle.value, buffer.data(), static_cast<uint32_t>(buffer.size()),
              &written) == ENGINE_RESULT_OK);
  return std::string(buffer.data(), written);
}

size_t CountLines(const std::string& value) {
  return static_cast<size_t>(std::count(value.begin(), value.end(), '\n'));
}

}  // namespace

TEST_CASE("diagnostic markers are sequenced and JSON escaped") {
  Handle handle;
  Configure(handle);
  uint64_t first = 0;
  uint64_t second = 0;
  REQUIRE(engine_mark_diagnostic_event(handle.value, "quote\" and\nline\b\f\x01", &first) ==
          ENGINE_RESULT_OK);
  REQUIRE(engine_mark_diagnostic_event(handle.value, "second", &second) ==
          ENGINE_RESULT_OK);
  REQUIRE(second == first + 1);

  const std::string output = DrainDiagnostics(handle);
  REQUIRE(output.find("quote\\\" and\\nline\\b\\f\\u0001") != std::string::npos);
  REQUIRE(output.find("\"event\":\"issue_marker\"") != std::string::npos);
  REQUIRE(output.find("\"monotonic_us\":1") != std::string::npos);
}

TEST_CASE("diagnostic queue is bounded and reports drops") {
  Handle handle;
  Configure(handle, 64);
  for (int index = 0; index < 100; ++index) {
    uint64_t sequence = 0;
    REQUIRE(engine_mark_diagnostic_event(handle.value, "overflow", &sequence) ==
            ENGINE_RESULT_OK);
  }
  const std::string output = DrainDiagnostics(handle);
  REQUIRE(CountLines(output) == 64);
  REQUIRE(output.find("\"queue_dropped\":1") != std::string::npos);
  REQUIRE(output.find("\"queue_dropped\":37") != std::string::npos);
}

TEST_CASE("concurrent markers remain valid and bounded") {
  Handle handle;
  Configure(handle, 512);
  std::atomic<int> failures{0};
  std::vector<std::thread> workers;
  for (int worker = 0; worker < 4; ++worker) {
    workers.emplace_back([&handle, &failures]() {
      for (int index = 0; index < 32; ++index) {
        uint64_t sequence = 0;
        if (engine_mark_diagnostic_event(handle.value, "parallel", &sequence) !=
            ENGINE_RESULT_OK) {
          ++failures;
        }
      }
    });
  }
  for (auto& worker : workers) worker.join();
  REQUIRE(failures == 0);
  REQUIRE(CountLines(DrainDiagnostics(handle)) == 129);  // start + 128 markers
}

TEST_CASE("legacy startup drain remains independent") {
  Handle handle;
  Configure(handle);
  REQUIRE(engine_open_game(handle.value, ".", nullptr) == ENGINE_RESULT_OK);
  char legacy[1024] = {};
  uint32_t written = 0;
  REQUIRE(engine_drain_startup_logs(handle.value, legacy, sizeof(legacy), &written) ==
          ENGINE_RESULT_OK);
  REQUIRE(std::string(legacy, written).find("engine_open_game => OK") !=
          std::string::npos);

  uint64_t sequence = 0;
  REQUIRE(engine_mark_diagnostic_event(handle.value, "after-open", &sequence) ==
          ENGINE_RESULT_OK);
  REQUIRE(DrainDiagnostics(handle).find("after-open") != std::string::npos);
}

TEST_CASE("text input state validates lifecycle and ABI size") {
  Handle handle;
  char text[8] = {};
  uint32_t text_bytes = 0;
  engine_text_input_state_t state{};
  state.struct_size = sizeof(state);
  REQUIRE(engine_get_text_input_state(handle.value, &state) ==
          ENGINE_RESULT_INVALID_STATE);
  REQUIRE(engine_copy_text_input_text(handle.value, text, sizeof(text),
                                      &text_bytes) ==
          ENGINE_RESULT_INVALID_STATE);

  engine_text_input_state_t too_small{};
  too_small.struct_size = sizeof(too_small) - 1;
  REQUIRE(engine_get_text_input_state(handle.value, &too_small) ==
          ENGINE_RESULT_INVALID_ARGUMENT);
  REQUIRE(engine_get_text_input_state(handle.value, nullptr) ==
          ENGINE_RESULT_INVALID_ARGUMENT);
  REQUIRE(engine_copy_text_input_text(handle.value, nullptr, sizeof(text),
                                      &text_bytes) ==
          ENGINE_RESULT_INVALID_ARGUMENT);
  REQUIRE(engine_copy_text_input_text(handle.value, text, 0, &text_bytes) ==
          ENGINE_RESULT_INVALID_ARGUMENT);
  REQUIRE(engine_copy_text_input_text(handle.value, text, sizeof(text),
                                      nullptr) ==
          ENGINE_RESULT_INVALID_ARGUMENT);

  REQUIRE(engine_open_game(handle.value, ".", nullptr) == ENGINE_RESULT_OK);
  state = {};
  state.struct_size = sizeof(state);
  REQUIRE(engine_get_text_input_state(handle.value, &state) == ENGINE_RESULT_OK);
  REQUIRE(state.struct_size == sizeof(state));
  REQUIRE(state.ime_active == 0);
  REQUIRE(state.ime_mode == 0);
  REQUIRE(state.attention_point_valid == 0);
  REQUIRE(state.attention_x == 0);
  REQUIRE(state.attention_y == 0);
  REQUIRE(state.text_available == 0);
  REQUIRE(state.text_utf8_bytes == 0);
  REQUIRE(state.selection_start == 0);
  REQUIRE(state.selection_end == 0);
  text[0] = 'x';
  text_bytes = 99;
  REQUIRE(engine_copy_text_input_text(handle.value, text, sizeof(text),
                                      &text_bytes) == ENGINE_RESULT_OK);
  REQUIRE(text_bytes == 0);
  REQUIRE(text[0] == '\0');
}

TEST_CASE("plugin debug snapshot is bounded JSON and validates buffers") {
  Handle handle;
  engine_option_t trace_option{};
  trace_option.key_utf8 = "plugin_trace";
  trace_option.value_utf8 = "1";
  REQUIRE(engine_set_option(handle.value, &trace_option) == ENGINE_RESULT_OK);
  std::vector<char> buffer(64 * 1024);
  uint32_t written = 0;
  REQUIRE(engine_get_plugin_debug_info(
              handle.value, buffer.data(), static_cast<uint32_t>(buffer.size()),
              &written) == ENGINE_RESULT_OK);
  const std::string output(buffer.data(), written);
  REQUIRE_FALSE(output.empty());
  REQUIRE(output.front() == '{');
  REQUIRE(output.back() == '}');
  REQUIRE(output.find("\"method_calls\":") != std::string::npos);
  REQUIRE(output.find("\"loaded_plugins\":[") != std::string::npos);
  REQUIRE(output.find("\"tracing_enabled\":true") != std::string::npos);

  char too_small[2] = {};
  written = 99;
  REQUIRE(engine_get_plugin_debug_info(handle.value, too_small, sizeof(too_small),
                                       &written) == ENGINE_RESULT_INVALID_ARGUMENT);
  REQUIRE(written == 0);
}
