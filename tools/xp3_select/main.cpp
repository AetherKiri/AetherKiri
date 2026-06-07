#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "XP3Archive.h"

namespace fs = std::filesystem;

static constexpr size_t kCopyBlockSize = 128 * 1024;

static std::string normalizePath(const std::string &path) {
    if(path.empty())
        return path;

    std::string expanded = path;
    if(expanded[0] == '~') {
        if(const char *home = std::getenv("HOME"))
            expanded = std::string(home) + expanded.substr(1);
    }

    try {
        return fs::weakly_canonical(fs::path(expanded)).string();
    } catch(...) {
        return fs::absolute(fs::path(expanded)).string();
    }
}

static bool contains(const std::string &value, const std::string &needle) {
    return needle.empty() || value.find(needle) != std::string::npos;
}

static void copyStream(tTJSBinaryStream *src, const fs::path &destFile) {
    fs::create_directories(destFile.parent_path());
    std::ofstream out(destFile, std::ios::binary);
    if(!out)
        throw std::runtime_error("failed to open output: " + destFile.string());

    auto buffer = std::make_unique<tjs_uint8[]>(kCopyBlockSize);
    while(true) {
        const tjs_uint read = src->Read(buffer.get(), kCopyBlockSize);
        if(read == 0)
            break;
        out.write(reinterpret_cast<const char *>(buffer.get()), read);
    }
}

static void usage() {
    std::cerr << "Usage: " << PROGRAM_NAME
              << " [--list] [--match substring] [--output dir] archive.xp3\n";
}

int main(int argc, char **argv) {
    bool listOnly = false;
    std::string match;
    std::string outputDir = ".";
    std::string archivePath;

    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if(arg == "--list") {
            listOnly = true;
        } else if(arg == "--match" && i + 1 < argc) {
            match = argv[++i];
        } else if((arg == "--output" || arg == "-o") && i + 1 < argc) {
            outputDir = argv[++i];
        } else if(arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else if(archivePath.empty()) {
            archivePath = arg;
        } else {
            usage();
            return 1;
        }
    }

    if(archivePath.empty()) {
        usage();
        return 1;
    }

    try {
        std::unique_ptr<tTVPArchive> archive{
            TVPOpenArchive(ttstr{ normalizePath(archivePath) }, false)
        };
        const tjs_uint count = archive->GetCount();
        size_t matched = 0;

        for(tjs_uint i = 0; i < count; ++i) {
            ttstr itemName = archive->GetName(i);
#ifndef _WIN32
            itemName.Replace(TJS_W('\\'), TJS_W('/'), true);
#endif
            std::string name = itemName.AsNarrowStdString();
            if(!contains(name, match))
                continue;

            ++matched;
            if(listOnly) {
                std::cout << name << "\n";
                continue;
            }

            std::unique_ptr<tTJSBinaryStream> stream{
                archive->CreateStreamByIndex(i)
            };
            fs::path dest = fs::path(normalizePath(outputDir)) / fs::path(name);
            copyStream(stream.get(), dest);
            std::cout << dest.string() << "\n";
        }

        std::cerr << "matched: " << matched << "\n";
    } catch(const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    } catch(...) {
        std::cerr << "unknown error\n";
        return 1;
    }

    return 0;
}
