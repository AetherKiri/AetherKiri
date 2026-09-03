#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "engine_api.h"
#include "engine_input_queue_gate.h"
#include "engine_runtime_provider.h"

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

struct FakeRuntime {
  bool opened = false;
  bool paused = false;
  bool platform_response_received = false;
  uint32_t width = 64;
  uint32_t height = 32;
  uint64_t frame_serial = 0;
  std::string error;
  engine_runtime_host_v1_t host{};
};

int32_t FakeProbe(void*, const char* root) {
  return root != nullptr && std::strstr(root, ".artemis-test") != nullptr ? 100 : 0;
}

int32_t ArtemisGateProbe(void*, const char* root) {
  return root != nullptr &&
                 std::strstr(root, ".artemis-debug-gate-test") != nullptr
             ? 100
             : 0;
}

engine_result_t FakeCreate(void*, const engine_runtime_host_v1_t* host,
                           const engine_create_desc_t*, void** out_runtime) {
  if (host == nullptr || out_runtime == nullptr || host->log == nullptr) {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }
  auto* runtime = new FakeRuntime();
  runtime->host = *host;
  *out_runtime = runtime;
  host->log(host->user_data, ENGINE_RUNTIME_LOG_INFO, "fake", "created");
  return ENGINE_RESULT_OK;
}

void FakeDestroy(void* runtime) { delete static_cast<FakeRuntime*>(runtime); }

engine_result_t FakeOpen(void* runtime, const char*, const char*) {
  auto* fake = static_cast<FakeRuntime*>(runtime);
  fake->opened = true;
  if (fake->host.platform_request != nullptr) {
    fake->host.platform_request(fake->host.user_data, "purchase",
                                "sku=test%20product");
  }
  return ENGINE_RESULT_OK;
}

engine_result_t FakeTick(void* runtime, uint32_t) {
  auto* fake = static_cast<FakeRuntime*>(runtime);
  if (!fake->opened || fake->paused) return ENGINE_RESULT_INVALID_STATE;
  ++fake->frame_serial;
  return ENGINE_RESULT_OK;
}

engine_result_t FakePause(void* runtime) {
  static_cast<FakeRuntime*>(runtime)->paused = true;
  return ENGINE_RESULT_OK;
}

engine_result_t FakeResume(void* runtime) {
  static_cast<FakeRuntime*>(runtime)->paused = false;
  return ENGINE_RESULT_OK;
}

engine_result_t FakeSurface(void* runtime, uint32_t width, uint32_t height) {
  auto* fake = static_cast<FakeRuntime*>(runtime);
  fake->width = width;
  fake->height = height;
  return ENGINE_RESULT_OK;
}

engine_result_t FakeFrameDesc(void* runtime, engine_frame_desc_t* desc) {
  if (desc == nullptr || desc->struct_size < sizeof(engine_frame_desc_t)) {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }
  const auto* fake = static_cast<FakeRuntime*>(runtime);
  desc->width = fake->width;
  desc->height = fake->height;
  desc->stride_bytes = fake->width * 4;
  desc->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888;
  desc->frame_serial = fake->frame_serial;
  return ENGINE_RESULT_OK;
}

const char* FakeLastError(void* runtime) {
  return runtime != nullptr ? static_cast<FakeRuntime*>(runtime)->error.c_str() : "";
}

engine_result_t FakeSubmitPlatformResponse(void* runtime,
                                           const char* operation,
                                           const char* argument) {
  auto* fake = static_cast<FakeRuntime*>(runtime);
  if (fake == nullptr || operation == nullptr || argument == nullptr) {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }
  if (std::strcmp(operation, "purchase") != 0 ||
      std::strcmp(argument, "result=1&token=ok") != 0) {
    fake->error = "unexpected platform response";
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }
  fake->platform_response_received = true;
  return ENGINE_RESULT_OK;
}

engine_result_t FakeGetTextInputState(void* runtime,
                                     uint32_t* out_state_flags) {
  if (runtime == nullptr || out_state_flags == nullptr) {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }
  *out_state_flags = ENGINE_TEXT_INPUT_STATE_ACTIVE;
  return ENGINE_RESULT_OK;
}

const engine_runtime_provider_v1_t kFakeProvider = [] {
  engine_runtime_provider_v1_t provider{};
  provider.struct_size = sizeof(provider);
  provider.api_version = ENGINE_RUNTIME_PROVIDER_API_VERSION;
  provider.runtime_id_utf8 = "fake-artemis-test";
  provider.display_name_utf8 = "Fake Artemis test provider";
  provider.priority = 1;
  provider.probe = FakeProbe;
  provider.create = FakeCreate;
  provider.destroy = FakeDestroy;
  provider.open_game = FakeOpen;
  provider.tick = FakeTick;
  provider.pause = FakePause;
  provider.resume = FakeResume;
  provider.set_surface_size = FakeSurface;
  provider.get_frame_desc = FakeFrameDesc;
  provider.get_last_error = FakeLastError;
  provider.submit_platform_response = FakeSubmitPlatformResponse;
  provider.get_text_input_state = FakeGetTextInputState;
  return provider;
}();

const engine_runtime_provider_v1_t kArtemisGateProvider = [] {
  engine_runtime_provider_v1_t provider = kFakeProvider;
  provider.runtime_id_utf8 = "artemis";
  provider.display_name_utf8 = "Artemis Debug gate test provider";
  provider.probe = ArtemisGateProbe;
  return provider;
}();

}  // namespace

TEST_CASE("primary click queue gate bounds rapid primary gestures") {
  aetherkiri::engine_api::PrimaryClickQueueGate gate;
  engine_input_event_t event{};
  event.struct_size = sizeof(event);
  event.button = 0;
  event.pointer_id = 7;

  event.type = ENGINE_INPUT_EVENT_POINTER_DOWN;
  REQUIRE(gate.should_enqueue(event));

  event.type = ENGINE_INPUT_EVENT_POINTER_UP;
  REQUIRE(gate.should_enqueue(event));
  const engine_input_event_t queued_release = event;

  for (size_t click = 0; click < 7; ++click) {
    event.type = ENGINE_INPUT_EVENT_POINTER_DOWN;
    REQUIRE(gate.should_enqueue(event));
    event.type = ENGINE_INPUT_EVENT_POINTER_UP;
    REQUIRE(gate.should_enqueue(event));
  }
  event.type = ENGINE_INPUT_EVENT_POINTER_DOWN;
  REQUIRE_FALSE(gate.should_enqueue(event));
  event.type = ENGINE_INPUT_EVENT_POINTER_MOVE;
  REQUIRE_FALSE(gate.should_enqueue(event));
  event.type = ENGINE_INPUT_EVENT_POINTER_UP;
  REQUIRE_FALSE(gate.should_enqueue(event));

  gate.on_dequeued(queued_release);
  event.type = ENGINE_INPUT_EVENT_POINTER_DOWN;
  REQUIRE(gate.should_enqueue(event));
}

TEST_CASE("primary click queue gate preserves every secondary pointer edge") {
  aetherkiri::engine_api::PrimaryClickQueueGate gate;
  engine_input_event_t event{};
  event.struct_size = sizeof(event);

  // Leave a primary release waiting so a wrongly encoded virtual right click
  // would be coalesced as stale primary input.
  event.type = ENGINE_INPUT_EVENT_POINTER_UP;
  event.button = 0;
  REQUIRE(gate.should_enqueue(event));
  const engine_input_event_t queued_primary_release = event;

  event.button = 1;
  event.type = ENGINE_INPUT_EVENT_POINTER_DOWN;
  REQUIRE(gate.should_enqueue(event));
  event.type = ENGINE_INPUT_EVENT_POINTER_MOVE;
  REQUIRE(gate.should_enqueue(event));
  event.type = ENGINE_INPUT_EVENT_POINTER_UP;
  REQUIRE(gate.should_enqueue(event));

  gate.on_dequeued(queued_primary_release);
}

TEST_CASE("Artemis runtime is compiled but beta-gated in product builds") {
  const engine_result_t registration =
      engine_register_runtime_provider(&kArtemisGateProvider);
  REQUIRE(registration == ENGINE_RESULT_OK);

  Handle handle;
  engine_option_t runtime_option{};
  runtime_option.key_utf8 = "runtime";
  runtime_option.value_utf8 = "artemis";
  REQUIRE(engine_set_option(handle.value, &runtime_option) == ENGINE_RESULT_OK);
#if defined(NDEBUG)
  REQUIRE(engine_open_game(handle.value, ".artemis-debug-gate-test",
                           "first.iet") == ENGINE_RESULT_NOT_SUPPORTED);
  REQUIRE(std::string(engine_get_last_error(handle.value)) ==
          "Artemis runtime requires active beta access");
  engine_option_t beta_option{};
  beta_option.key_utf8 = "artemis_beta_allowed";
  beta_option.value_utf8 = "1";
  REQUIRE(engine_set_option(handle.value, &beta_option) == ENGINE_RESULT_OK);
#endif
  REQUIRE(engine_open_game(handle.value, ".artemis-debug-gate-test",
                           "first.iet") == ENGINE_RESULT_OK);
}

TEST_CASE("versioned runtime provider is selected and routed end to end") {
  REQUIRE(engine_register_runtime_provider(&kFakeProvider) == ENGINE_RESULT_OK);
  REQUIRE(engine_register_runtime_provider(&kFakeProvider) == ENGINE_RESULT_OK);
  REQUIRE(engine_get_runtime_provider_count() >= 1);

  Handle handle;
  engine_option_t runtime_option{};
  runtime_option.key_utf8 = "runtime";
  runtime_option.value_utf8 = "fake-artemis-test";
  REQUIRE(engine_set_option(handle.value, &runtime_option) == ENGINE_RESULT_OK);
  REQUIRE(engine_open_game(handle.value, ".artemis-test", "first.iet") ==
          ENGINE_RESULT_OK);
  std::array<char, 64> operation{};
  std::array<char, 64> argument{};
  uint32_t available = 0;
  REQUIRE(engine_poll_platform_request(
              handle.value, operation.data(),
              static_cast<uint32_t>(operation.size()), argument.data(),
              static_cast<uint32_t>(argument.size()), &available) ==
          ENGINE_RESULT_OK);
  REQUIRE(available == 1);
  REQUIRE(std::string(operation.data()) == "purchase");
  REQUIRE(std::string(argument.data()) == "sku=test%20product");
  REQUIRE(engine_submit_platform_response(
              handle.value, "purchase", "result=1&token=ok") ==
          ENGINE_RESULT_OK);
  REQUIRE(engine_set_surface_size(handle.value, 320, 180) == ENGINE_RESULT_OK);
  REQUIRE(engine_tick(handle.value, 16) == ENGINE_RESULT_OK);

  engine_frame_desc_t frame{};
  frame.struct_size = sizeof(frame);
  REQUIRE(engine_get_frame_desc(handle.value, &frame) == ENGINE_RESULT_OK);
  REQUIRE(frame.width == 320);
  REQUIRE(frame.height == 180);
  REQUIRE(frame.frame_serial == 1);
  REQUIRE(engine_pause(handle.value) == ENGINE_RESULT_OK);
  REQUIRE(engine_tick(handle.value, 16) == ENGINE_RESULT_INVALID_STATE);
  REQUIRE(engine_resume(handle.value) == ENGINE_RESULT_OK);
  engine_text_input_state_t text_input_state{};
  text_input_state.struct_size = sizeof(text_input_state);
  REQUIRE(engine_get_text_input_state(handle.value, &text_input_state) ==
          ENGINE_RESULT_OK);
  REQUIRE(text_input_state.ime_active == 1);
  REQUIRE(text_input_state.attention_point_valid == 1);
  REQUIRE(text_input_state.attention_x == 160);
  REQUIRE(text_input_state.attention_y == 90);
}

TEST_CASE("surface request made before provider selection is replayed") {
  REQUIRE(engine_register_runtime_provider(&kFakeProvider) == ENGINE_RESULT_OK);

  Handle handle;
  engine_option_t runtime_option{};
  runtime_option.key_utf8 = "runtime";
  runtime_option.value_utf8 = "fake-artemis-test";
  REQUIRE(engine_set_option(handle.value, &runtime_option) == ENGINE_RESULT_OK);
  REQUIRE(engine_set_surface_size(handle.value, 1920, 1080) ==
          ENGINE_RESULT_OK);
  REQUIRE(engine_open_game(handle.value, ".artemis-test", "first.iet") ==
          ENGINE_RESULT_OK);

  engine_frame_desc_t frame{};
  frame.struct_size = sizeof(frame);
  REQUIRE(engine_get_frame_desc(handle.value, &frame) == ENGINE_RESULT_OK);
  REQUIRE(frame.width == 1920u);
  REQUIRE(frame.height == 1080u);
  REQUIRE(frame.stride_bytes == 1920u * 4u);
}

TEST_CASE("standalone media is routed through the legacy host service") {
  REQUIRE(engine_register_runtime_provider(&kFakeProvider) == ENGINE_RESULT_OK);
  Handle handle;
  engine_option_t runtime_option{};
  runtime_option.key_utf8 = "runtime";
  runtime_option.value_utf8 = "fake-artemis-test";
  REQUIRE(engine_set_option(handle.value, &runtime_option) == ENGINE_RESULT_OK);
  REQUIRE(engine_open_game(handle.value, ".artemis-test", "first.iet") ==
          ENGINE_RESULT_OK);
  engine_media_handle_t media = nullptr;
  REQUIRE(engine_media_open(handle.value, "missing-video.mp4", &media) ==
          ENGINE_RESULT_NOT_SUPPORTED);
  REQUIRE(media == nullptr);
  REQUIRE(std::string(engine_get_last_error(handle.value)) ==
          "standalone media playback is not supported in stub builds");
}

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

TEST_CASE("text translation state is disabled without the private provider") {
  REQUIRE(engine_is_text_translation_available() == 0);
  REQUIRE(engine_get_text_translation_state() ==
          ENGINE_TEXT_TRANSLATION_DISABLED);
  engine_text_translation_stats_t translation_stats{};
  translation_stats.struct_size = sizeof(translation_stats);
  REQUIRE(engine_get_text_translation_stats(&translation_stats) ==
          ENGINE_RESULT_OK);
  REQUIRE(translation_stats.state == ENGINE_TEXT_TRANSLATION_DISABLED);
  REQUIRE(translation_stats.backend == ENGINE_TEXT_TRANSLATION_BACKEND_NONE);
  REQUIRE(translation_stats.model_tensor_bytes == 0);
  REQUIRE(engine_get_text_translation_stats(nullptr) ==
          ENGINE_RESULT_INVALID_ARGUMENT);
  engine_text_translation_stats_t short_translation_stats{};
  short_translation_stats.struct_size = sizeof(uint32_t);
  REQUIRE(engine_get_text_translation_stats(&short_translation_stats) ==
          ENGINE_RESULT_INVALID_ARGUMENT);
  REQUIRE(engine_prefetch_text_utf8("kirikiri", "テスト") == ENGINE_RESULT_OK);
  REQUIRE(engine_set_text_translation_skipping("kirikiri", 1) ==
          ENGINE_RESULT_OK);
  REQUIRE(engine_prefetch_text_utf8(nullptr, "テスト") ==
          ENGINE_RESULT_INVALID_ARGUMENT);
  REQUIRE(engine_prefetch_text_utf8("kirikiri", nullptr) ==
          ENGINE_RESULT_INVALID_ARGUMENT);
  REQUIRE(engine_set_text_translation_skipping(nullptr, 1) ==
          ENGINE_RESULT_INVALID_ARGUMENT);
}
