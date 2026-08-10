#include "onscripter_save_storage.h"

#include <atomic>
#include <cctype>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace aetherkiri::onscripter {
namespace {

std::atomic<uint64_t> g_probe_serial{0};

void AppendWarning(std::string &warning, const std::string &message) {
    if (!warning.empty()) {
        warning += "; ";
    }
    warning += message;
}

bool PrepareWritableDirectory(const fs::path &directory,
                              std::string &warning) {
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) {
        AppendWarning(warning, "cannot create " + directory.u8string() +
            ": " + error.message());
        return false;
    }

    const fs::path probe = directory /
        (".aetherkiri-save-probe-" +
         std::to_string(g_probe_serial.fetch_add(1)));
    std::ofstream stream(probe, std::ios::binary | std::ios::trunc);
    if (!stream) {
        AppendWarning(warning, "cannot write " + directory.u8string());
        return false;
    }
    stream.put('\0');
    stream.close();
    if (!stream) {
        AppendWarning(warning, "cannot finish a write in " +
            directory.u8string());
        fs::remove(probe, error);
        return false;
    }
    fs::remove(probe, error);
    return true;
}

uint32_t MigrateSaveFiles(const fs::path &source,
                          const fs::path &destination,
                          std::string &warning) {
    std::error_code error;
    if (!fs::is_directory(source, error) ||
        fs::equivalent(source, destination, error)) {
        return 0;
    }
    error.clear();

    uint32_t copied = 0;
    fs::directory_iterator iterator(source, error);
    const fs::directory_iterator end;
    if (error) {
        AppendWarning(warning, "cannot inspect old saves in " +
            source.u8string() + ": " + error.message());
        return 0;
    }
    while (iterator != end) {
        const fs::directory_entry entry = *iterator;
        if (entry.is_regular_file(error) && IsOnsSaveFileName(entry.path())) {
            error.clear();
            const fs::path target = destination / entry.path().filename();
            const bool copied_now = fs::copy_file(
                entry.path(), target, fs::copy_options::skip_existing, error);
            if (copied_now) {
                ++copied;
            } else if (error) {
                AppendWarning(warning, "cannot migrate " +
                    entry.path().filename().u8string() + ": " +
                    error.message());
            }
        }
        error.clear();
        iterator.increment(error);
        if (error) {
            AppendWarning(warning, "cannot continue reading old saves in " +
                source.u8string() + ": " + error.message());
            break;
        }
    }
    return copied;
}

} // namespace

bool IsOnsSaveFileName(const fs::path &path) {
    const std::string name = path.filename().string();
    if (name == "envdata" || name == "gloval.sav" ||
        name == "kidoku.dat" || name == "NScrllog.dat" ||
        name == "NScrflog.dat" || name == "stdout.txt" ||
        name == "stderr.txt") {
        return true;
    }
    constexpr const char *prefix = "save";
    constexpr const char *suffix = ".dat";
    if (name.size() <= 4 + 4 || name.compare(0, 4, prefix) != 0 ||
        name.compare(name.size() - 4, 4, suffix) != 0) {
        return false;
    }
    for (size_t index = 4; index + 4 < name.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(name[index]))) {
            return false;
        }
    }
    return true;
}

SaveStorageResult PrepareSaveStorage(const fs::path &game_root,
                                     const fs::path &legacy_directory) {
    SaveStorageResult result;
    const fs::path preferred = game_root / "savedata";
    if (PrepareWritableDirectory(preferred, result.warning)) {
        result.directory = preferred;
        result.using_game_directory = true;
        // The app-owned store contains saves produced by previous AetherKiri
        // builds and should win over packaged root-level defaults. Existing
        // files in savedata always win over both sources.
        result.migrated_files += MigrateSaveFiles(
            legacy_directory, preferred, result.warning);
        result.migrated_files += MigrateSaveFiles(
            game_root, preferred, result.warning);
        return result;
    }

    if (PrepareWritableDirectory(legacy_directory, result.warning)) {
        result.directory = legacy_directory;
        result.migrated_files += MigrateSaveFiles(
            game_root, legacy_directory, result.warning);
    }
    return result;
}

} // namespace aetherkiri::onscripter
