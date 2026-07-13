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
