#include <catch2/catch_test_macros.hpp>

#include "tjsCommHead.h"
#include "Application.h"
#include "ArchiveAutoPathOrder.h"
#include "SysInitImpl.h"
#include "StorageImpl.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

class TemporaryProjectPath {
public:
    TemporaryProjectPath() {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path = std::filesystem::temp_directory_path() /
               ("AetherKiri-Project-" + std::to_string(unique) + ".xp3");
    }

    ~TemporaryProjectPath() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    std::filesystem::path path;
};

class NativeProjectPathScope {
public:
    NativeProjectPathScope() : original(TVPNativeProjectDir) {}
    ~NativeProjectPathScope() { TVPNativeProjectDir = original; }

private:
    ttstr original;
};

} // namespace

TEST_CASE("archive auto paths keep the package root at highest priority") {
    std::set<std::u16string, tTVPArchiveAutoPathDirectoryLess> directories{
        u"", u"tools/", u"assets/", u"tools/internal/"};

    const std::vector<std::u16string> ordered(directories.begin(),
                                               directories.end());
    REQUIRE(ordered.size() == 4);
    CHECK(ordered[0] == u"assets/");
    CHECK(ordered[1] == u"tools/");
    CHECK(ordered[2] == u"tools/internal/");
    CHECK(ordered[3].empty());
}

TEST_CASE("archive auto paths preserve directories for qualified resources") {
    const std::u16string archive =
        u"file://./game/data.xp3>fgimage/yr/se2a/";

    CHECK(TVPArchiveAutoPathDirectoryMatches(
        archive, u"fgimage/yr/se2a/"));
    CHECK_FALSE(TVPArchiveAutoPathDirectoryMatches(
        archive, u"image/fyr/se2a/"));
    CHECK_FALSE(TVPArchiveAutoPathDirectoryMatches(
        u"file://./game/fgimage/yr/se2a/", u"fgimage/yr/se2a/"));
}

TEST_CASE("project archive detection falls back to the exact native path") {
    TemporaryProjectPath project;
    {
        std::ofstream output(project.path, std::ios::binary);
        REQUIRE(output.good());
        output << "XP3";
    }

    CHECK(TVPIsProjectStorageFile(
        TJS_W(""), ttstr(project.path.string())));
    CHECK(TVPGetNativeProjectDirectory(ttstr(project.path.string())) ==
          ttstr(project.path.parent_path().string()));

    std::error_code error;
    std::filesystem::remove(project.path, error);
    REQUIRE_FALSE(error);
    REQUIRE(std::filesystem::create_directory(project.path));

    CHECK_FALSE(TVPIsProjectStorageFile(
        TJS_W(""), ttstr(project.path.string())));
    CHECK(TVPGetNativeProjectDirectory(ttstr(project.path.string())) ==
          ttstr(project.path.string()));
}

TEST_CASE("application home directory follows the active project") {
    NativeProjectPathScope restoreProjectPath;
    const auto root = std::filesystem::temp_directory_path();
    const auto first = root / "AetherKiri-First-Project";
    const auto second = root / "AetherKiri-Second-Project";

    TVPNativeProjectDir = ttstr(first.string());
    const auto firstHomes = TVPGetApplicationHomeDirectory();
    REQUIRE(firstHomes.size() == 1);
    CHECK(firstHomes.front() == first.string());

    TVPNativeProjectDir = ttstr(second.string());
    const auto secondHomes = TVPGetApplicationHomeDirectory();
    REQUIRE(secondHomes.size() == 1);
    CHECK(secondHomes.front() == second.string());
}
