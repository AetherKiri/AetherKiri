#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <vector>

namespace aether::archive_import {

using ProgressCallback = std::function<void(std::uint64_t completed,
                                             std::uint64_t total)>;

struct ProbeResult {
    bool recognized = false;
    bool encrypted = false;
    std::string format;
    std::string error;
};

struct ExtractOptions {
    std::string password;
    std::vector<std::string> passwords;
    std::uint64_t max_total_bytes = UINT64_C(64) * 1024 * 1024 * 1024;
    std::uint32_t max_files = 200000;
    std::uint32_t max_depth = 16;
};

struct ExtractResult {
    bool ok = false;
    bool password_required = false;
    std::string output_path;
    std::string format;
    std::string error;
    std::string pending_archive;
    std::vector<std::string> extracted_archives;
};

ProbeResult Probe(const std::string &path,
                  const ProgressCallback &progress = {});
ExtractResult ExtractRecursive(const std::string &path,
                               const std::string &destination,
                               const ExtractOptions &options,
                               const ProgressCallback &progress = {});

}  // namespace aether::archive_import
