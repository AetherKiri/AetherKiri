#include "renpy_runtime.h"
#include "engine_api.h"
#include "engine_runtime_provider.h"

#include <cassert>
#include <cstring>

int main() {
    aetherkiri::renpy::RegisterRuntimeProvider();
    assert(engine_probe_runtime_provider("renpy", "/not-a-project") == 0);

    engine_create_desc_t desc{};
    desc.struct_size = sizeof(desc);
    desc.api_version = ENGINE_API_VERSION;
    engine_handle_t handle = nullptr;
    assert(engine_create(&desc, &handle) == ENGINE_RESULT_OK);

    engine_option_t option{};
    option.key_utf8 = "runtime";
    option.value_utf8 = "renpy";
    assert(engine_set_option(handle, &option) == ENGINE_RESULT_OK);
    assert(engine_open_game(handle, "/not-a-project", nullptr) ==
           ENGINE_RESULT_NOT_SUPPORTED);
    const char* error = engine_get_last_error(handle);
    assert(error && std::strstr(error, "mobile") != nullptr);
    assert(engine_destroy(handle) == ENGINE_RESULT_OK);
    return 0;
}
