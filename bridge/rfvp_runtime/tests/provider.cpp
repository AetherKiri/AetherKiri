#include "engine_runtime_provider.h"
#include "rfvp_runtime_provider.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "fixture.h"

namespace fs = std::filesystem;
static const engine_runtime_provider_v1_t* provider;
extern "C" engine_result_t engine_register_runtime_provider(const engine_runtime_provider_v1_t* p) {
    provider = p; return ENGINE_RESULT_OK;
}
static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Instance {
    void* value = nullptr;
    explicit Instance(const fs::path& saves) {
        engine_runtime_host_v1_t host{};
        host.struct_size = sizeof(host); host.api_version = ENGINE_RUNTIME_PROVIDER_API_VERSION;
        host.log = [](void*, uint32_t level, const char*, const char* text) {
            if (level >= ENGINE_RUNTIME_LOG_WARNING || std::getenv("RFVP_TRACE")) std::cerr << text << '\n';
        };
        auto writable = saves.u8string();
        engine_create_desc_t desc{};
        desc.struct_size = sizeof(desc); desc.api_version = ENGINE_API_VERSION;
        desc.writable_path_utf8 = writable.c_str();
        check(provider->create(nullptr, &host, &desc, &value) == ENGINE_RESULT_OK, "create");
    }
    ~Instance() { if (value) provider->destroy(value); }
    void ok(engine_result_t code) {
        if (code != ENGINE_RESULT_OK) throw std::runtime_error(provider->get_last_error(value));
    }
    engine_result_t open_result(const fs::path& path) {
        // Match engine_open_game_async: macOS gives std::thread a much smaller
        // stack than main(). Tick, input and destruction stay on the host thread.
        engine_result_t result = ENGINE_RESULT_INTERNAL_ERROR;
        const auto utf8 = path.u8string();
        std::thread startup([&] {
            result = provider->open_game(value, utf8.c_str(), nullptr);
        });
        startup.join();
        return result;
    }
    void open(const fs::path& path) {
        ok(open_result(path));
    }
    void step(int count = 1) { while (count--) ok(provider->tick(value, 16)); }
    engine_frame_desc_t frame() {
        engine_frame_desc_t d{}; d.struct_size = sizeof(d);
        ok(provider->get_frame_desc(value, &d)); return d;
    }
    std::vector<uint8_t> pixels() {
        auto d = frame(); std::vector<uint8_t> bytes(d.stride_bytes * d.height);
        ok(provider->read_frame_rgba(value, bytes.data(), bytes.size())); return bytes;
    }
    void pointer(uint32_t type, int button = 0, uint32_t modifiers = 0) {
        engine_input_event_t e{}; e.struct_size = sizeof(e); e.type = type;
        e.x = 200; e.y = 200; e.button = button; e.modifiers = modifiers;
        ok(provider->send_input(value, &e));
    }
    void key(int code) {
        engine_input_event_t e{}; e.struct_size = sizeof(e); e.key_code = code;
        e.type = ENGINE_INPUT_EVENT_KEY_DOWN; ok(provider->send_input(value, &e));
        step();
        e.type = ENGINE_INPUT_EVENT_KEY_UP; ok(provider->send_input(value, &e));
        step(2);
    }
};
int main(int argc, char** argv) {
    // Use only open-source and original test fixtures. Commercial games are
    // exercised through the application's normal library UI, not this runner.
    const fs::path demo = RFVP_TEST_DEMO;
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path scratch = fs::temp_directory_path() / fs::u8path("aether-rfvp-test-游戏-" + unique);
    try {
        fs::create_directory(scratch);
        aetherkiri::rfvp::RegisterRuntimeProvider();
        check(provider && std::string(provider->runtime_id_utf8) == "rfvp", "registration");
        check(provider->probe(nullptr, demo.u8string().c_str()) > 0, "demo probe");
        check(provider->probe(nullptr, scratch.u8string().c_str()) == 0, "empty probe");
        {
            std::ofstream(scratch / "bad.hcb") << "not an HCB";
            check(provider->probe(nullptr, scratch.u8string().c_str()) == 0, "invalid HCB probe");
        }
        {
            Instance painter(scratch);
            painter.open(demo);
            painter.step(2);
            uint64_t texture = 0, serial = 0;
            uint32_t width = 0, height = 0;
            check(provider->get_godot_native_frame_texture(
                      painter.value, &texture, &width, &height, &serial) ==
                      ENGINE_RESULT_NOT_SUPPORTED,
                  "standalone provider uses CPU fallback without a GPU bridge");
            auto pixels = painter.pixels();
            check(pixels[(200 * 1024 + 200) * 4] > 200, "upstream painter renders its canvas");
        }
        {
            Instance forced_gpu(scratch);
            engine_option_t option{};
            option.key_utf8 = "rfvp_renderer";
            option.value_utf8 = "invalid";
            check(provider->set_option(forced_gpu.value, &option) ==
                      ENGINE_RESULT_INVALID_ARGUMENT,
                  "invalid renderer");
            option.value_utf8 = "gpu";
            forced_gpu.ok(provider->set_option(forced_gpu.value, &option));
            check(forced_gpu.open_result(demo) != ENGINE_RESULT_OK,
                  "forced GPU rejects a missing bridge");
        }
        const auto fixture = scratch / "input.hcb";
        write_input_fixture(fixture);
        {
            Instance game(scratch);
            engine_option_t option{}; option.key_utf8 = "rfvp_encoding"; option.value_utf8 = "invalid";
            check(provider->set_option(game.value, &option) == ENGINE_RESULT_INVALID_ARGUMENT, "invalid encoding");
            option.value_utf8 = "sjis"; game.ok(provider->set_option(game.value, &option));
            game.open(fixture);
            game.step(8);
            auto frame = game.frame();
            check(frame.width == 1024 && frame.height == 640 && frame.stride_bytes == 4096, "native RGBA layout");
            auto before = game.pixels();
            const size_t pixel = (200 * frame.width + 200) * 4;
            check(before[pixel] > 200 && before[pixel+3] == 255, "test canvas is visible");
            game.pointer(ENGINE_INPUT_EVENT_POINTER_DOWN); game.step(2);
            game.pointer(ENGINE_INPUT_EVENT_POINTER_UP); game.step(2);
            auto after = game.pixels();
            check(after[pixel] < 40, "left pointer draws black");
            game.key(116); // F5: save black tile and VM state
            bool saved = false;
            for (const auto& entry : fs::recursive_directory_iterator(scratch / "rfvp")) {
                if (entry.path().filename() == "rfvp_s000.bin") saved = fs::file_size(entry.path()) > 16000;
            }
            check(saved, "script saves snapshot and thumbnail under host writable root");
            game.key(117); // F6: a later menu SaveWrite reuses the prepared payload
            bool second_saved = false;
            for (const auto& entry : fs::recursive_directory_iterator(scratch / "rfvp")) {
                if (entry.path().filename() == "rfvp_s001.bin") second_saved = fs::file_size(entry.path()) > 16000;
            }
            check(second_saved, "completed save must not block a subsequent SaveWrite");
            game.pointer(ENGINE_INPUT_EVENT_POINTER_DOWN, 1); game.step(2);
            game.pointer(ENGINE_INPUT_EVENT_POINTER_UP, 1); game.step(2);
            check(game.pixels()[pixel] > 200, "right pointer erases");
            game.key(118); game.step(80); // F7: load the menu-written slot
            check(game.pixels()[pixel] < 40, "second slot restores the prepared gameplay snapshot");
            game.pointer(ENGINE_INPUT_EVENT_POINTER_DOWN, 1); game.step(2);
            game.pointer(ENGINE_INPUT_EVENT_POINTER_UP, 1); game.step(2);
            game.key(120); game.step(80); // F9: restore black tile, allow dissolve to finish
            check(game.pixels()[pixel] < 40, "script load restores drawn tile");
            game.pointer(ENGINE_INPUT_EVENT_POINTER_DOWN, 1); game.step();
            game.pointer(ENGINE_INPUT_EVENT_POINTER_UP, 1); game.step();
            game.pointer(ENGINE_INPUT_EVENT_POINTER_DOWN);
            game.pointer(ENGINE_INPUT_EVENT_POINTER_UP, 0, ENGINE_INPUT_MODIFIER_POINTER_CANCEL);
            game.step();
            check(game.pixels()[pixel] > 200, "cancelled pointer does not leave a held button");
            auto serial = game.frame().frame_serial;
            game.ok(provider->pause(game.value)); game.step(2);
            check(game.frame().frame_serial == serial, "pause freezes frame clock");
            game.ok(provider->resume(game.value)); game.step();
            check(game.frame().frame_serial > serial, "resume advances frame clock");
            auto small = std::vector<uint8_t>(4);
            check(provider->read_frame_rgba(game.value, small.data(), small.size()) != ENGINE_RESULT_OK, "short frame buffer rejected");
            {
                Instance second(scratch);
                check(provider->open_game(second.value, demo.u8string().c_str(), nullptr) != ENGINE_RESULT_OK,
                    "process-global rfvp state must not be shared by simultaneous games");
            }
            if (argc == 2) {
                std::ofstream image(argv[1], std::ios::binary);
                image << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
                auto pixels = game.pixels();
                for (size_t i = 0; i < pixels.size(); i += 4) image.write(reinterpret_cast<char*>(pixels.data()+i), 3);
            }
        }
        check(!fs::exists(demo / "save"), "do not write saves into the read-only game fixture");
        {
            Instance reopened(scratch);
            reopened.open(fixture);
            reopened.step(2);
            reopened.key(120); reopened.step(80);
            check(reopened.pixels()[(200 * 1024 + 200) * 4] < 40, "save survives close and reopen");
        }
        check(fs::is_directory(scratch / "rfvp"), "host save root used");
        {
            const auto fade_fixture = scratch / "fade.hcb";
            write_input_fixture(fade_fixture, true);
            Instance game(scratch);
            game.open(fade_fixture);
            game.step(2);
            const size_t pixel = (410 * 1024 + 410) * 4;
            check(game.pixels()[pixel] > 200, "save point is near the start of the fade");
            game.key(116); // Save while the tile is still fading in.
            game.step(80);
            check(game.pixels()[pixel] < 40, "fade reaches its destination before load");
            game.key(120); game.step(80);
            check(game.pixels()[pixel] < 40, "load resumes in-flight fade instead of freezing its alpha");
        }
        fs::remove_all(scratch); // Only the unique test-owned directory above.
        std::cout << "rfvp provider: PASS (probe, pixels, input, save/load, pause, isolation, reopen)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "rfvp provider: FAIL: " << e.what() << "\nTest artifacts: " << scratch << '\n';
        return 1;
    }
}
