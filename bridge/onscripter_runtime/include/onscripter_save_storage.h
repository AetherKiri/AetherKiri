#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace aetherkiri::onscripter {

struct SaveStorageResult {
    std::filesystem::path directory;
    bool using_game_directory = false;
    uint32_t migrated_files = 0;
    std::string warning;
};

bool IsOnsSaveFileName(const std::filesystem::path &path);

// Prefer a save directory beside the game so saves travel with an imported
// package. The app-owned directory remains a fallback for read-only media.
// Existing files are copied without overwriting newer destination files.
SaveStorageResult PrepareSaveStorage(
    const std::filesystem::path &game_root,
    const std::filesystem::path &legacy_directory);

} // namespace aetherkiri::onscripter
