#ifndef AETHERKIRI_RENPY_RUNTIME_IOS_ADAPTER_H_
#define AETHERKIRI_RENPY_RUNTIME_IOS_ADAPTER_H_

#include "engine_runtime_provider.h"

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Boundary between the Renios launcher and the AetherKiri runtime provider.
 *
 * The official Renios prototype owns the process entrypoint in its main.c
 * template. AetherKiri is already running inside Godot, so this adapter must
 * never create a second UIKit/SDL application entrypoint. An
 * eventual implementation is installed by the iOS host and is driven by the
 * host-owned lifecycle below.  The current build intentionally has no
 * implementation; the provider remains ENGINE_RESULT_NOT_SUPPORTED until
 * the complete Renios static-library closure and rendering/input bridge are
 * linked.
 */
#define AETHERKIRI_RENPY_IOS_ADAPTER_API_VERSION 0x01000000u

/* Build-time inspection of the official Renios closure. The shipped
 * launcher_main entrypoint reaches Py_RunMain and owns a blocking Python
 * main loop; it is not a host-tick API. Keep this fact explicit so a caller
 * cannot mistake the linked archives for a playable adapter. */
#define AETHERKIRI_RENPY_IOS_LAUNCHER_CONTRACT_API_VERSION 0x01000000u

typedef enum renpy_ios_launcher_mode_v1_t {
  RENPY_IOS_LAUNCHER_MODE_UNAVAILABLE = 0,
  RENPY_IOS_LAUNCHER_MODE_BLOCKING_PY_MAIN = 1,
  RENPY_IOS_LAUNCHER_MODE_HOST_TICK = 2
} renpy_ios_launcher_mode_v1_t;

typedef struct renpy_ios_launcher_contract_v1_t {
  uint32_t struct_size;
  uint32_t api_version;
  uint32_t mode;
  const char* launcher_symbol_utf8;
  const char* blocking_symbol_utf8;
  const char* limitation_utf8;
} renpy_ios_launcher_contract_v1_t;

/* Returns the static contract discovered by the build probe. The current
 * Renios archive reports BLOCKING_PY_MAIN and therefore cannot be driven by
 * the Godot frame loop. */
ENGINE_API_EXPORT const renpy_ios_launcher_contract_v1_t*
renpy_get_ios_launcher_contract(void);

typedef struct renpy_ios_inprocess_adapter_v1_t {
  uint32_t struct_size;
  uint32_t api_version;
  void* user_data;

  /* Allocate adapter-owned state. This must not create a UIApplication or
   * enter an SDL application loop. */
  engine_result_t (*create)(void* user_data,
                            const engine_runtime_host_v1_t* host,
                            const engine_create_desc_t* desc,
                            void** out_runtime);
  void (*destroy)(void* runtime);

  /* Start Renios on the already-running host loop. */
  engine_result_t (*open_game)(void* runtime,
                               const char* game_root_path_utf8,
                               const char* startup_script_utf8);
  engine_result_t (*tick)(void* runtime, uint32_t delta_ms);
  engine_result_t (*pause)(void* runtime);
  engine_result_t (*resume)(void* runtime);
  engine_result_t (*close_game)(void* runtime);
  const char* (*get_last_error)(void* runtime);
} renpy_ios_inprocess_adapter_v1_t;

/* Installs or removes the host-owned adapter. Passing NULL removes it. */
ENGINE_API_EXPORT engine_result_t renpy_install_ios_inprocess_adapter(
    const renpy_ios_inprocess_adapter_v1_t* adapter);

/* Returns the currently installed adapter, or NULL when the Renios closure is
 * not linked into the app. The returned descriptor is process-lived. */
ENGINE_API_EXPORT const renpy_ios_inprocess_adapter_v1_t*
renpy_get_ios_inprocess_adapter(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* AETHERKIRI_RENPY_RUNTIME_IOS_ADAPTER_H_ */
