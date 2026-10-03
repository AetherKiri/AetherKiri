#include "renpy_runtime.h"
#include "engine_api.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <cstdlib>
#else
#include <sys/stat.h>
#endif

#ifndef AETHERKIRI_RENPY_TEST_OVERLAY_SOURCE
#define AETHERKIRI_RENPY_TEST_OVERLAY_SOURCE ""
#endif

namespace {
namespace fs = std::filesystem;

fs::path MakeProject(const fs::path& root) {
    fs::create_directories(root / "game");
    std::ofstream(root / "game" / "script.rpy")
        << "label start:\n    pause\n";
    return root;
}

void WriteFakeLauncher(const fs::path& sdk) {
    fs::create_directories(sdk);
    std::ofstream launcher(sdk / "renpy");
    launcher << "#!/bin/sh\n"
             << "project=$1\n"
             << "printf '%s\\n' \"$AETHERKIRI_RENPY_INPUT\" > \"$project/input-path\"\n"
             << "trap 'exit 0' TERM INT\n"
             << "while :; do sleep 0.01; done\n";
    launcher.close();
#if defined(_WIN32)
    (void)std::system(("chmod +x " + (sdk / "renpy").string()).c_str());
#else
    fs::permissions(sdk / "renpy",
                    fs::perms::owner_exec | fs::perms::owner_read |
                        fs::perms::owner_write,
                    fs::perm_options::add);
#endif
}

bool WaitForFile(const fs::path& path) {
    for (int i = 0; i < 200; ++i) {
        if (fs::is_regular_file(path)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

std::string ReadInput(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

void Send(engine_handle_t handle, uint32_t type, int button = 0,
          int key_code = 0, int modifiers = 0, uint32_t unicode = 0) {
    engine_input_event_t event{};
    event.struct_size = sizeof(event);
    event.type = type;
    event.button = button;
    event.key_code = key_code;
    event.modifiers = modifiers;
    event.unicode_codepoint = unicode;
    assert(engine_send_input(handle, &event) == ENGINE_RESULT_OK);
}

void CheckInputMapping() {
    aetherkiri::renpy::RegisterRuntimeProvider();
    const fs::path root = fs::temp_directory_path() /
                          ("aetherkiri-renpy-input-mapping-" +
                           std::to_string(std::chrono::steady_clock::now()
                                              .time_since_epoch()
                                              .count()));
    const fs::path sdk = root / "sdk";
    std::error_code ec;
    fs::create_directories(root, ec);
    MakeProject(root);
    WriteFakeLauncher(sdk);

    aetherkiri::renpy::RegisterRuntimeProvider();
    engine_create_desc_t desc{};
    desc.struct_size = sizeof(desc);
    desc.api_version = ENGINE_API_VERSION;
    const std::string writable = (root / "host-data").string();
    desc.writable_path_utf8 = writable.c_str();
    engine_handle_t handle = nullptr;
    assert(engine_create(&desc, &handle) == ENGINE_RESULT_OK);

    engine_option_t option{};
    option.key_utf8 = "runtime";
    option.value_utf8 = "renpy";
    assert(engine_set_option(handle, &option) == ENGINE_RESULT_OK);
    const std::string sdk_text = sdk.string();
    option.key_utf8 = "renpy_sdk_path";
    option.value_utf8 = sdk_text.c_str();
    assert(engine_set_option(handle, &option) == ENGINE_RESULT_OK);
    const std::string project = root.string();
    assert(engine_open_game(handle, project.c_str(), nullptr) == ENGINE_RESULT_OK);

    const fs::path input_path_marker = root / "input-path";
    assert(WaitForFile(input_path_marker));
    std::ifstream marker(input_path_marker);
    std::string input_path_text;
    std::getline(marker, input_path_text);
    assert(!input_path_text.empty());
    const fs::path input_path(input_path_text);

    // Aether/Godot pointer buttons are 0=left, 1=right, 2=middle.
    Send(handle, ENGINE_INPUT_EVENT_POINTER_DOWN, 0);
    Send(handle, ENGINE_INPUT_EVENT_POINTER_DOWN, 1);
    Send(handle, ENGINE_INPUT_EVENT_POINTER_DOWN, 2);
    // Hardware key path uses the VK values emitted by main.gd.
    Send(handle, ENGINE_INPUT_EVENT_KEY_DOWN, 0, 0x41,
         0x01 | 0x04 | 0x80, 'A');
    // Godot's native Key enum and modifier-mask path are accepted as well.
    Send(handle, ENGINE_INPUT_EVENT_KEY_DOWN, 0, 4194321,
         (1 << 25) | (1 << 28)); // KEY_RIGHT, shift+ctrl
    Send(handle, ENGINE_INPUT_EVENT_KEY_DOWN, 0, 0x70); // VK_F1
    Send(handle, ENGINE_INPUT_EVENT_KEY_DOWN, 0, 0x08); // VK_BACK
    Send(handle, ENGINE_INPUT_EVENT_KEY_DOWN, 0, 0x09); // VK_TAB
    Send(handle, ENGINE_INPUT_EVENT_KEY_DOWN, 0, 0x0d); // VK_RETURN
    Send(handle, ENGINE_INPUT_EVENT_KEY_DOWN, 0, 0x20); // VK_SPACE
    Send(handle, ENGINE_INPUT_EVENT_KEY_DOWN, 0, 0x00a5); // VK_RALT
    // Existing SDL/Pygame values remain valid for compatibility.
    Send(handle, ENGINE_INPUT_EVENT_KEY_UP, 0, 1073741905);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const std::string lines = ReadInput(input_path);
    assert(lines.find("\"type\":1025,\"attributes\":{\"pos\":[0,0],\"button\":1}") !=
           std::string::npos);
    assert(lines.find("\"button\":3}") != std::string::npos);
    assert(lines.find("\"button\":2}") != std::string::npos);
    assert(lines.find("\"key\":97,\"mod\":195,\"unicode\":\"A\",\"repeat\":true") !=
           std::string::npos);
    assert(lines.find("\"key\":1073741903,\"mod\":195") !=
           std::string::npos);
    assert(lines.find("\"key\":1073741882,\"mod\":0") !=
           std::string::npos);
    assert(lines.find("\"key\":8,\"mod\":0") != std::string::npos);
    assert(lines.find("\"key\":9,\"mod\":0") != std::string::npos);
    assert(lines.find("\"key\":13,\"mod\":0") != std::string::npos);
    assert(lines.find("\"key\":32,\"mod\":0") != std::string::npos);
    assert(lines.find("\"key\":1073742054,\"mod\":0") != std::string::npos);
    assert(lines.find("\"type\":769,\"attributes\":{\"key\":1073741905") !=
           std::string::npos);

    assert(engine_destroy(handle) == ENGINE_RESULT_OK);
    fs::remove_all(root, ec);
}
} // namespace

int main() {
#if defined(_WIN32)
    // The fixture launcher is a POSIX shell script. The provider mapping is
    // platform-independent; the existing provider test covers Windows SDK
    // process startup separately.
    return 0;
#else
    CheckInputMapping();
    return 0;
#endif
}
