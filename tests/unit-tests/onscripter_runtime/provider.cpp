#include <catch2/catch_test_macros.hpp>

#include "engine_runtime_provider.h"
#include "onscripter_runtime.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("ONScripter registers as one shared runtime provider") {
    aetherkiri::onscripter::RegisterRuntimeProvider();
    aetherkiri::onscripter::RegisterRuntimeProvider();

    bool found = false;
    for(uint32_t index = 0; index < engine_get_runtime_provider_count();
        ++index) {
        char id[64] = {};
        uint32_t bytes_written = 0;
        REQUIRE(engine_get_runtime_provider_id(
                    index, id, sizeof(id), &bytes_written) == ENGINE_RESULT_OK);
        if(std::string(id, bytes_written) == "onscripter") {
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("ONScripter provider probes script roots without creating a player") {
    aetherkiri::onscripter::RegisterRuntimeProvider();
    const auto nonce =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
        ("aetherkiri-ons-provider-" + std::to_string(nonce));
    REQUIRE(fs::create_directories(root));
    {
        std::ofstream marker(root / "0.txt", std::ios::binary);
        REQUIRE(marker.good());
        marker << "*define\n";
    }

    CHECK(engine_probe_runtime_provider("onscripter", root.u8string().c_str()) >
          0);

    std::error_code error;
    fs::remove_all(root, error);
    CHECK_FALSE(error);
}
