#include <catch2/catch_test_macros.hpp>

#include "onscripter_save_storage.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;
using aetherkiri::onscripter::IsOnsSaveFileName;
using aetherkiri::onscripter::PrepareSaveStorage;

namespace {

std::atomic<uint64_t> g_test_serial{0};

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        path = fs::temp_directory_path() /
            ("aetherkiri-ons-save-test-" +
             std::to_string(g_test_serial.fetch_add(1)));
        fs::remove_all(path);
        fs::create_directories(path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path, error);
    }

    fs::path path;
};

void WriteFile(const fs::path &path, const std::string &contents) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream);
    stream << contents;
    REQUIRE(stream.good());
}

std::string ReadFile(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("ONS save storage uses game savedata and migrates old saves") {
    TemporaryDirectory temporary;
    const fs::path game = temporary.path / "game";
    const fs::path legacy = temporary.path / "legacy";
    WriteFile(legacy / "save1.dat", "legacy-save");
    WriteFile(game / "gloval.sav", "packaged-global");
    WriteFile(game / "arc.nsa", "asset");

    const auto result = PrepareSaveStorage(game, legacy);

    CHECK(result.directory == game / "savedata");
    CHECK(result.using_game_directory);
    CHECK(result.migrated_files == 2);
    CHECK(ReadFile(game / "savedata" / "save1.dat") == "legacy-save");
    CHECK(ReadFile(game / "savedata" / "gloval.sav") == "packaged-global");
    CHECK_FALSE(fs::exists(game / "savedata" / "arc.nsa"));
}

TEST_CASE("ONS save migration never overwrites an existing game save") {
    TemporaryDirectory temporary;
    const fs::path game = temporary.path / "game";
    const fs::path legacy = temporary.path / "legacy";
    WriteFile(game / "savedata" / "save2.dat", "current-save");
    WriteFile(legacy / "save2.dat", "old-save");

    const auto result = PrepareSaveStorage(game, legacy);

    CHECK(result.directory == game / "savedata");
    CHECK(result.migrated_files == 0);
    CHECK(ReadFile(game / "savedata" / "save2.dat") == "current-save");
}

TEST_CASE("ONS save storage falls back when the game path is not writable") {
    TemporaryDirectory temporary;
    const fs::path game_file = temporary.path / "game-file";
    const fs::path legacy = temporary.path / "legacy";
    WriteFile(game_file, "not-a-directory");

    const auto result = PrepareSaveStorage(game_file, legacy);

    CHECK(result.directory == legacy);
    CHECK_FALSE(result.using_game_directory);
    CHECK_FALSE(result.warning.empty());
}

TEST_CASE("ONS save filename filter excludes game assets") {
    CHECK(IsOnsSaveFileName("save0.dat"));
    CHECK(IsOnsSaveFileName("save998.dat"));
    CHECK(IsOnsSaveFileName("envdata"));
    CHECK(IsOnsSaveFileName("kidoku.dat"));
    CHECK_FALSE(IsOnsSaveFileName("save.dat"));
    CHECK_FALSE(IsOnsSaveFileName("saveA.dat"));
    CHECK_FALSE(IsOnsSaveFileName("nscript.dat"));
    CHECK_FALSE(IsOnsSaveFileName("arc.nsa"));
}
