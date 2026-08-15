#ifndef AETHERKIRI_ENGINE_RUNTIME_PROVIDER_H_
#define AETHERKIRI_ENGINE_RUNTIME_PROVIDER_H_

#include "engine_api.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Independent ABI for runtime backends. It may evolve without changing the
 * host-facing engine API ABI. */
#define ENGINE_RUNTIME_PROVIDER_API_VERSION 0x01000000u

typedef enum engine_runtime_log_level_t {
  ENGINE_RUNTIME_LOG_TRACE = 0,
  ENGINE_RUNTIME_LOG_DEBUG = 1,
  ENGINE_RUNTIME_LOG_INFO = 2,
  ENGINE_RUNTIME_LOG_WARNING = 3,
  ENGINE_RUNTIME_LOG_ERROR = 4
} engine_runtime_log_level_t;

typedef void (*engine_runtime_platform_request_fn)(
    void* user_data, const char* operation_utf8, const char* argument_utf8);

#define ENGINE_RUNTIME_FRAGMENT_SHADER_API_VERSION 0x01000000u

typedef struct engine_runtime_shader_image_v1_t {
  uint32_t width;
  uint32_t height;
  uint32_t stride_bytes;
  const uint8_t* pixels_rgba;
  size_t pixels_size;
} engine_runtime_shader_image_v1_t;

typedef struct engine_runtime_shader_texture_v1_t {
  const char* name_utf8;
  engine_runtime_shader_image_v1_t image;
} engine_runtime_shader_texture_v1_t;

typedef struct engine_runtime_shader_constant_v1_t {
  const char* name_utf8;
  const float* values;
  uint32_t value_count;
} engine_runtime_shader_constant_v1_t;

typedef struct engine_runtime_fragment_shader_request_v1_t {
  uint32_t struct_size;
  uint32_t api_version;
  const char* shader_id_utf8;
  const char* fragment_source_utf8;
  engine_runtime_shader_image_v1_t foreground;
  engine_runtime_shader_image_v1_t mask;
  uint32_t mask_uses_alpha;
  float alpha;
  uint32_t color_multiply;
  const engine_runtime_shader_texture_v1_t* textures;
  uint32_t texture_count;
  const engine_runtime_shader_constant_v1_t* constants;
  uint32_t constant_count;
  uint8_t* output_pixels_rgba;
  size_t output_pixels_size;
} engine_runtime_fragment_shader_request_v1_t;

typedef engine_result_t (*engine_runtime_fragment_shader_execute_fn)(
    void* user_data,
    const engine_runtime_fragment_shader_request_v1_t* request,
    char* error_utf8, uint32_t error_size);

typedef struct engine_runtime_fragment_shader_host_v1_t {
  uint32_t struct_size;
  uint32_t api_version;
  void* user_data;
  engine_runtime_fragment_shader_execute_fn execute;
} engine_runtime_fragment_shader_host_v1_t;

typedef struct engine_runtime_host_v1_t {
  uint32_t struct_size;
  uint32_t api_version;
  void* user_data;
  void (*log)(void* user_data, uint32_t level, const char* subsystem_utf8,
              const char* message_utf8);
  uint64_t (*monotonic_time_micros)(void* user_data);
  engine_runtime_platform_request_fn platform_request;
  uint64_t reserved_u64[4];
  void* reserved_ptr[3];
} engine_runtime_host_v1_t;

/*
 * Provider descriptors and every function pointer referenced by them must
 * remain valid for the lifetime of the process. Runtime instances are owned by
 * the provider and are created/destroyed exactly once by the host.
 *
 * probe returns 0 for no match and a positive score for a supported game. The
 * host chooses the highest score, then priority, then registration order.
 */
typedef struct engine_runtime_provider_v1_t {
  uint32_t struct_size;
  uint32_t api_version;
  const char* runtime_id_utf8;
  const char* display_name_utf8;
  int32_t priority;
  void* provider_user_data;

  int32_t (*probe)(void* provider_user_data, const char* game_root_path_utf8);
  engine_result_t (*create)(void* provider_user_data,
                            const engine_runtime_host_v1_t* host,
                            const engine_create_desc_t* desc,
                            void** out_runtime);
  void (*destroy)(void* runtime);
  engine_result_t (*open_game)(void* runtime,
                               const char* game_root_path_utf8,
                               const char* startup_script_utf8);
  engine_result_t (*tick)(void* runtime, uint32_t delta_ms);

  engine_result_t (*pause)(void* runtime);
  engine_result_t (*resume)(void* runtime);
  engine_result_t (*set_option)(void* runtime, const engine_option_t* option);
  engine_result_t (*set_surface_size)(void* runtime, uint32_t width,
                                      uint32_t height);
  engine_result_t (*get_frame_desc)(void* runtime,
                                    engine_frame_desc_t* out_frame_desc);
  engine_result_t (*read_frame_rgba)(void* runtime, void* out_pixels,
                                     size_t out_pixels_size);
  engine_result_t (*get_godot_native_frame_texture)(
      void* runtime, uint64_t* out_texture_id, uint32_t* out_width,
      uint32_t* out_height, uint64_t* out_frame_serial);
  engine_result_t (*get_host_native_window)(void* runtime,
                                            void** out_window_handle);
  engine_result_t (*get_host_native_view)(void* runtime,
                                          void** out_view_handle);
  engine_result_t (*send_input)(void* runtime,
                                const engine_input_event_t* event);
  engine_result_t (*get_main_menu_json)(void* runtime, char* out_buffer,
                                        uint32_t buffer_size,
                                        uint32_t* out_bytes_written);
  engine_result_t (*activate_menu_item)(void* runtime,
                                        const char* item_path_utf8);
  engine_result_t (*set_render_target_iosurface)(void* runtime,
                                                 uint32_t iosurface_id,
                                                 uint32_t width,
                                                 uint32_t height);
  engine_result_t (*set_render_target_surface)(void* runtime,
                                               void* native_window,
                                               uint32_t width,
                                               uint32_t height);
  engine_result_t (*get_frame_rendered_flag)(void* runtime,
                                             uint32_t* out_rendered);
  engine_result_t (*get_renderer_info)(void* runtime, char* out_buffer,
                                       uint32_t buffer_size);
  engine_result_t (*get_memory_stats)(void* runtime,
                                      engine_memory_stats_t* out_stats);
  engine_result_t (*get_plugin_debug_info)(void* runtime, char* out_buffer,
                                           uint32_t buffer_size,
                                           uint32_t* out_bytes_written);
  const char* (*get_last_error)(void* runtime);

  uint64_t reserved_u64[8];
  /*
   * Completes a request previously emitted through host.platform_request.
   * This occupies the first pointer-sized extension slot, preserving the
   * binary size and offsets of every earlier provider field.
   */
  engine_result_t (*submit_platform_response)(
      void* runtime, const char* operation_utf8, const char* argument_utf8);
  /*
   * Reports whether the runtime currently owns a text-editing field. Hosts
   * use this to activate their native IME only while text input is expected.
   */
  engine_result_t (*get_text_input_state)(void* runtime,
                                          uint32_t* out_state_flags);
  /* Uses one reserved slot without changing this structure's ABI size. */
  engine_result_t (*get_godot_presentation_state)(
      void* runtime, uint32_t* out_state_flags);
  void* reserved_ptr[5];
} engine_runtime_provider_v1_t;

#define ENGINE_RUNTIME_PROVIDER_V1_MIN_SIZE                              \
  (offsetof(engine_runtime_provider_v1_t, tick) +                        \
   sizeof(((engine_runtime_provider_v1_t*)0)->tick))

ENGINE_API_EXPORT engine_result_t engine_register_runtime_provider(
    const engine_runtime_provider_v1_t* provider);

/* Installs the host GPU path used by runtimes whose native shader language
 * cannot be evaluated by their CPU compositor. Passing NULL removes it. */
ENGINE_API_EXPORT engine_result_t
engine_set_runtime_fragment_shader_executor(
    engine_runtime_fragment_shader_execute_fn execute, void* user_data);

ENGINE_API_EXPORT uint32_t engine_get_runtime_provider_count(void);

ENGINE_API_EXPORT engine_result_t engine_get_runtime_provider_id(
    uint32_t index, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written);

/* Returns the provider's positive match score, or zero when the named runtime
 * does not recognize the directory. A negative value reports invalid input or
 * an unavailable provider. The probe does not create or start a runtime. */
ENGINE_API_EXPORT int32_t engine_probe_runtime_provider(
    const char* runtime_id_utf8, const char* game_root_path_utf8);

#if defined(__cplusplus)
}  /* extern "C" */
#endif

#endif  /* AETHERKIRI_ENGINE_RUNTIME_PROVIDER_H_ */
