//
// Created by lidong on 25-6-21.
//

#include <catch2/catch_session.hpp>

#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>

bool TVPIsConsoleLogFileEnabled() { return false; }

int main(int argc, char *argv[]) {

    // Production keeps ApplyColorMap_a out of the default GPU allowlist to
    // avoid thousands of tiny glyph dispatches. Contract tests still cover
    // that opt-in route while retaining all normal default fast paths.
#if defined(_WIN32)
    _putenv_s("AETHERKIRI_GODOT_GPU_RECT_FASTPATH",
              "default,ApplyColorMap_a");
#else
    setenv("AETHERKIRI_GODOT_GPU_RECT_FASTPATH",
           "default,ApplyColorMap_a", 1);
#endif

    static auto core_logger = spdlog::stdout_color_mt("core");
    static auto tjs2_logger = spdlog::stdout_color_mt("tjs2");
    static auto plugin_logger = spdlog::stdout_color_mt("plugin");

    int result = Catch::Session().run(argc, argv);

    return result;
}
