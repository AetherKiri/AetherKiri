// An original, minimal HCB fixture: white canvas, left draw, right erase.
// Emit bytecode directly so this bridge test does not depend on a compiler or
// on the behavior of an upstream demonstration's application logic.
#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

inline void write_input_fixture(const std::filesystem::path& path, bool fading = false) {
    std::vector<uint8_t> b(4);
    std::vector<std::pair<std::string, uint8_t>> calls;
    auto word = [&](uint32_t value, int count) {
        for (int i = 0; i < count; ++i) b.push_back(uint8_t(value >> (i * 8)));
    };
    auto patch = [&](size_t at, uint32_t value) {
        for (int i = 0; i < 4; ++i) b[at+i] = uint8_t(value >> (i * 8));
    };
    auto integer = [&](int32_t value) { b.push_back(10); word(uint32_t(value), 4); };
    auto syscall = [&](const std::string& name, uint8_t argc) {
        size_t id = 0;
        while (id < calls.size() && calls[id].first != name) ++id;
        if (id == calls.size()) calls.emplace_back(name, argc);
        b.push_back(3); word(uint32_t(id), 2);
    };
    auto call = [&](const std::string& name, std::initializer_list<int32_t> args) {
        for (auto value : args) integer(value);
        syscall(name, uint8_t(args.size()));
    };
    b.insert(b.end(), {1, 0, 0}); // init_stack 0 args, 0 locals
    call("ColorSet", {20, 0, 0, 0, 255});
    call("ColorSet", {27, 255, 255, 255, 255});
    call("PrimSetTile", {1, 27, 0, 0, 1024, 640});
    call("PrimGroupIn", {1, 0});
    call("PrimSetTile", {2, 27, 0, 0, 20, 20});
    call("PrimGroupIn", {2, 0});
    call("SaveThumbSize", {80, 50});
    if (fading) {
        call("PrimSetTile", {3, 20, 400, 400, 40, 40});
        call("PrimGroupIn", {3, 0});
        call("MotionAlpha", {3, 0, 255, 800, 0, 0});
    }
    const auto loop = b.size();
    for (auto [bit, color] : {std::pair{2, 20}, std::pair{3, 27}}) {
        syscall("InputGetState", 0); b.push_back(20); // push_return
        integer(bit); b.push_back(31); // bit_test (bit index, not a mask)
        b.push_back(7); const auto skip = b.size(); word(0, 4); // jz
        integer(2); integer(color);
        syscall("InputGetCursX", 0); b.push_back(20);
        syscall("InputGetCursY", 0); b.push_back(20);
        integer(20); integer(20); syscall("PrimSetTile", 6);
        patch(skip, uint32_t(b.size()));
    }
    for (auto [bit, action] : {std::pair{17, "SaveCreate"}, std::pair{18, "SaveWrite"},
                              std::pair{19, "Load"}, std::pair{21, "Load"}}) {
        syscall("InputGetDown", 0); b.push_back(20);
        integer(bit); b.push_back(31);
        b.push_back(7); const auto skip = b.size(); word(0, 4);
        if (bit == 17) call(action, {3, 0}); // capture and commit slot 0
        else if (bit == 18 || bit == 19) call(action, {1}); // reuse prepared payload / load slot 1
        else call(action, {0});
        patch(skip, uint32_t(b.size()));
    }
    syscall("ThreadNext", 0);
    b.push_back(6); word(uint32_t(loop), 4);
    patch(0, uint32_t(b.size()));
    word(4, 4); // entry point
    word(0, 2); word(0, 2); // nonvolatile, volatile globals
    b.push_back(7); b.push_back(0); // 1024 x 640
    b.push_back(1); b.push_back(0); // empty title, including terminator
    word(uint32_t(calls.size()), 2);
    for (const auto& [name, argc] : calls) {
        b.push_back(argc); b.push_back(uint8_t(name.size()+1));
        b.insert(b.end(), name.begin(), name.end()); b.push_back(0);
    }
    word(0, 2); // no custom syscalls
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), b.size());
    if (!out) throw std::runtime_error("write HCB test fixture");
}
