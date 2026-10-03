#include "renpy_runtime_ios_adapter.h"

#include <cassert>

namespace {

engine_result_t Create(void*, const engine_runtime_host_v1_t*,
                       const engine_create_desc_t*, void**) {
    return ENGINE_RESULT_NOT_SUPPORTED;
}
void Destroy(void*) {}
engine_result_t Open(void*, const char*, const char*) {
    return ENGINE_RESULT_NOT_SUPPORTED;
}
engine_result_t Tick(void*, uint32_t) { return ENGINE_RESULT_NOT_SUPPORTED; }
engine_result_t Pause(void*) { return ENGINE_RESULT_NOT_SUPPORTED; }
engine_result_t Resume(void*) { return ENGINE_RESULT_NOT_SUPPORTED; }
engine_result_t Close(void*) { return ENGINE_RESULT_NOT_SUPPORTED; }
const char* Error(void*) { return "test adapter"; }

}  // namespace

int main() {
    const auto* contract = renpy_get_ios_launcher_contract();
    assert(contract != nullptr);
    assert(contract->struct_size == sizeof(*contract));
    assert(contract->api_version ==
           AETHERKIRI_RENPY_IOS_LAUNCHER_CONTRACT_API_VERSION);
    assert(contract->mode == RENPY_IOS_LAUNCHER_MODE_BLOCKING_PY_MAIN);
    assert(contract->launcher_symbol_utf8 != nullptr);
    assert(contract->blocking_symbol_utf8 != nullptr);
    assert(contract->limitation_utf8 != nullptr);

    // The default product build has no linked Renios closure.
    assert(renpy_get_ios_inprocess_adapter() == nullptr);

    renpy_ios_inprocess_adapter_v1_t invalid{};
    invalid.struct_size = sizeof(invalid);
    invalid.api_version = 0;
    assert(renpy_install_ios_inprocess_adapter(&invalid) ==
           ENGINE_RESULT_INVALID_ARGUMENT);
    assert(renpy_get_ios_inprocess_adapter() == nullptr);

    renpy_ios_inprocess_adapter_v1_t adapter{};
    adapter.struct_size = sizeof(adapter);
    adapter.api_version = AETHERKIRI_RENPY_IOS_ADAPTER_API_VERSION;
    adapter.create = Create;
    adapter.destroy = Destroy;
    adapter.open_game = Open;
    adapter.tick = Tick;
    adapter.pause = Pause;
    adapter.resume = Resume;
    adapter.close_game = Close;
    adapter.get_last_error = Error;
    assert(renpy_install_ios_inprocess_adapter(&adapter) == ENGINE_RESULT_OK);
    assert(renpy_get_ios_inprocess_adapter() == &adapter);

    // Registration is only a boundary test. Until the provider's complete
    // lifecycle/rendering/input bridge is linked, game open stays unsupported.
    assert(adapter.open_game(nullptr, nullptr, nullptr) ==
           ENGINE_RESULT_NOT_SUPPORTED);

    assert(renpy_install_ios_inprocess_adapter(nullptr) == ENGINE_RESULT_OK);
    assert(renpy_get_ios_inprocess_adapter() == nullptr);
    return 0;
}
