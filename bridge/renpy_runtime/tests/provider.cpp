#include "renpy_runtime.h"
#include "engine_api.h"
#include "engine_runtime_provider.h"

#include <cassert>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifndef AETHERKIRI_RENPY_TEST_SDK_ROOT
#define AETHERKIRI_RENPY_TEST_SDK_ROOT ""
#endif

#if !defined(_WIN32)
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#endif

namespace {
std::filesystem::path MakeProject() {
    const auto root = std::filesystem::temp_directory_path() /
                      "aetherkiri-renpy-provider-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "game", ec);
    std::ofstream(root / "game" / "script.rpy") << "label start:\n    pass\n";
    return root;
}

#if !defined(_WIN32)
bool ProcessExists(pid_t pid) {
    if (pid <= 0) return false;
    errno = 0;
    return kill(pid, 0) == 0 || errno == EPERM;
}

pid_t ReadPid(const std::filesystem::path& path) {
    for (int i = 0; i < 100; ++i) {
        std::ifstream input(path);
        pid_t pid = -1;
        if (input >> pid && pid > 0) return pid;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return -1;
}

void WriteFakeLauncher(const std::filesystem::path& sdk) {
    std::ofstream launcher(sdk / "renpy");
    launcher << "#!/bin/sh\n"
             << "project=$1\n"
             << "if [ -f \"$project/ignore-term\" ]; then\n"
             << "  (trap '' TERM; while :; do sleep 1; done) &\n"
             << "else\n"
             << "  (while :; do sleep 1; done) &\n"
             << "fi\n"
             << "child=$!\n"
             << "echo $child > \"$project/descendant.pid\"\n"
             << "if [ -f \"$project/exit-leader\" ]; then exit 0; fi\n"
             << "trap 'exit 0' TERM INT\n"
             << "wait $child\n";
    launcher.close();
    std::filesystem::permissions(
        sdk / "renpy",
        std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write,
        std::filesystem::perm_options::add);
}

void CheckProcessGroupShutdown() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
                      "aetherkiri-renpy-lifecycle-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "sdk", ec);
    fs::create_directories(root / "game", ec);
    std::ofstream(root / "game" / "script.rpy") << "label start:\n    pass\n";
    WriteFakeLauncher(root / "sdk");

    auto open = [&](const fs::path& project) {
        engine_create_desc_t desc{};
        desc.struct_size = sizeof(desc);
        desc.api_version = ENGINE_API_VERSION;
        const auto writable = root.u8string();
        desc.writable_path_utf8 = writable.c_str();
        engine_handle_t handle = nullptr;
        assert(engine_create(&desc, &handle) == ENGINE_RESULT_OK);
        engine_option_t option{};
        option.key_utf8 = "runtime";
        option.value_utf8 = "renpy";
        assert(engine_set_option(handle, &option) == ENGINE_RESULT_OK);
        const auto sdk = (root / "sdk").u8string();
        option.key_utf8 = "renpy_sdk_path";
        option.value_utf8 = sdk.c_str();
        assert(engine_set_option(handle, &option) == ENGINE_RESULT_OK);
        const auto path = project.u8string();
        const engine_result_t result = engine_open_game(handle, path.c_str(), nullptr);
        return std::pair<engine_handle_t, engine_result_t>{handle, result};
    };

    // The launcher exits after handing work to a descendant. Tick observes
    // the leader reap, then destruction must still clean up its process group.
    std::ofstream(root / "exit-leader").put('\n');
    std::ofstream(root / "ignore-term").put('\n');
    auto exited = open(root);
    std::filesystem::remove(root / "exit-leader", ec);
    const pid_t exited_child = ReadPid(root / "descendant.pid");
    assert(exited_child > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    (void)engine_tick(exited.first, 0);
    engine_destroy(exited.first);
    for (int i = 0; i < 100 && ProcessExists(exited_child); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(!ProcessExists(exited_child));

    // A stopped process group must be continued before TERM. The bounded
    // shutdown should finish well below the two-second SIGTERM grace period.
    std::filesystem::remove(root / "ignore-term", ec);
    auto paused = open(root);
    assert(paused.second == ENGINE_RESULT_OK);
    const pid_t paused_child = ReadPid(root / "descendant.pid");
    assert(paused_child > 0);
    assert(engine_pause(paused.first) == ENGINE_RESULT_OK);
    const auto started = std::chrono::steady_clock::now();
    engine_destroy(paused.first);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    assert(elapsed.count() < 3500);
    for (int i = 0; i < 100 && ProcessExists(paused_child); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(!ProcessExists(paused_child));
    fs::remove_all(root, ec);
}
#endif

void CheckFrameInputBridge() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
                      ("aetherkiri-renpy-frame-test-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()));
    std::error_code ec;
    fs::create_directories(root / "game", ec);
    std::ofstream(root / "game" / "options.rpy")
        << "define config.name = \"AetherKiri bridge test\"\n"
        << "define config.version = \"1\"\n"
        << "define config.screen_width = 640\n"
        << "define config.screen_height = 360\n"
        << "define config.gl_enable = False\n"
        << "define config.performance_test = False\n";
    std::ofstream(root / "game" / "script.rpy") << R"R(label main_menu:
    jump start
label start:
    scene Solid("#194a72")
    menu:
        "Input bridge continue":
            $ open(renpy.config.gamedir + "/input-ok", "w").write("continue\n")
            $ renpy.quit()
        "Input bridge finish":
            $ open(renpy.config.gamedir + "/input-ok", "w").write("finish\n")
            $ renpy.quit()
)R";
#if defined(_WIN32)
    _putenv_s("SDL_VIDEODRIVER", "dummy");
    _putenv_s("SDL_AUDIODRIVER", "dummy");
    _putenv_s("RENPY_RENDERER", "sw");
#else
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    setenv("RENPY_RENDERER", "sw", 1);
#endif
    engine_create_desc_t desc{};
    desc.struct_size = sizeof(desc);
    desc.api_version = ENGINE_API_VERSION;
    const auto writable = (root / "host-data").u8string();
    desc.writable_path_utf8 = writable.c_str();
    engine_handle_t handle = nullptr;
    assert(engine_create(&desc, &handle) == ENGINE_RESULT_OK);
    engine_option_t option{};
    option.key_utf8 = "runtime";
    option.value_utf8 = "renpy";
    assert(engine_set_option(handle, &option) == ENGINE_RESULT_OK);
    option.key_utf8 = "renpy_sdk_path";
    option.value_utf8 = AETHERKIRI_RENPY_TEST_SDK_ROOT;
    assert(engine_set_option(handle, &option) == ENGINE_RESULT_OK);
    const auto path = root.u8string();
    assert(engine_open_game(handle, path.c_str(), nullptr) == ENGINE_RESULT_OK);
    engine_frame_desc_t frame{};
    frame.struct_size = sizeof(frame);
    bool ready = false;
    for (int i = 0; i < 200; ++i) {
        assert(engine_tick(handle, 16) == ENGINE_RESULT_OK);
        if (engine_get_frame_desc(handle, &frame) == ENGINE_RESULT_OK) {
            ready = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(ready && frame.width == 640 && frame.height == 360 &&
           frame.stride_bytes == 640 * 4 && frame.frame_serial != 0);
    std::vector<uint8_t> pixels(frame.stride_bytes * frame.height);
    assert(engine_read_frame_rgba(handle, pixels.data(), pixels.size()) ==
           ENGINE_RESULT_OK);
    size_t nonzero = 0;
    for (uint8_t value : pixels) nonzero += value != 0;
    assert(nonzero != 0);
    engine_input_event_t event{};
    event.struct_size = sizeof(event);
    event.type = ENGINE_INPUT_EVENT_KEY_DOWN;
    event.key_code = 1073741905;
    assert(engine_send_input(handle, &event) == ENGINE_RESULT_OK);
    event.key_code = 13;
    assert(engine_send_input(handle, &event) == ENGINE_RESULT_OK);
    for (int i = 0; i < 100 && !fs::exists(root / "game" / "input-ok"); ++i) {
        assert(engine_tick(handle, 16) == ENGINE_RESULT_OK);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(fs::exists(root / "game" / "input-ok"));
    assert(engine_destroy(handle) == ENGINE_RESULT_OK);
    assert(!fs::exists(root / "game" / "libs"));
    std::ofstream(root / "game" / "libs").put('\n');
    engine_handle_t blocked = nullptr;
    assert(engine_create(&desc, &blocked) == ENGINE_RESULT_OK);
    option.key_utf8 = "runtime";
    option.value_utf8 = "renpy";
    assert(engine_set_option(blocked, &option) == ENGINE_RESULT_OK);
    option.key_utf8 = "renpy_sdk_path";
    option.value_utf8 = AETHERKIRI_RENPY_TEST_SDK_ROOT;
    assert(engine_set_option(blocked, &option) == ENGINE_RESULT_OK);
    assert(engine_open_game(blocked, path.c_str(), nullptr) ==
           ENGINE_RESULT_NOT_SUPPORTED);
    assert(engine_destroy(blocked) == ENGINE_RESULT_OK);
    fs::remove(root / "game" / "libs", ec);
    fs::remove_all(root, ec);
}
}

int main() {
    const auto project = MakeProject();
    aetherkiri::renpy::RegisterRuntimeProvider();
    assert(engine_get_runtime_provider_count() > 0);
    assert(engine_probe_runtime_provider("renpy", project.u8string().c_str()) > 0);
#if !defined(_WIN32)
    CheckProcessGroupShutdown();
#endif
    CheckFrameInputBridge();
    std::error_code ec;
    std::filesystem::remove_all(project, ec);
    return 0;
}
