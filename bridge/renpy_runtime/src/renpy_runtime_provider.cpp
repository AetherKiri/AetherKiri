#include "renpy_runtime.h"
#include "engine_runtime_provider.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#else
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#else
extern char** environ;
#endif
#endif

#ifndef AETHERKIRI_RENPY_SDK_ROOT
#define AETHERKIRI_RENPY_SDK_ROOT ""
#endif
#ifndef AETHERKIRI_RENPY_OVERLAY_SOURCE
#define AETHERKIRI_RENPY_OVERLAY_SOURCE ""
#endif

namespace aetherkiri::renpy {
namespace {
namespace fs = std::filesystem;
#if !defined(_WIN32)
char** ProcessEnvironment() {
#if defined(__APPLE__)
    return *_NSGetEnviron();
#else
    return environ;
#endif
}
#endif

constexpr const char* kOverlayFileName = "aetherkiri-renpy-overlay.py";

fs::path CurrentExecutablePath() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD size = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0) return {};
        if (size < buffer.size() - 1) {
            buffer.resize(size);
            return fs::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    if (_NSGetExecutablePath(nullptr, &size) != -1 || size == 0) return {};
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    return fs::path(buffer.c_str());
#else
    std::error_code ec;
    const fs::path path = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path() : path;
#endif
}

fs::path BundledResourcePath(const char* filename) {
    const fs::path executable = CurrentExecutablePath();
    if (executable.empty()) return {};
    const fs::path executable_dir = executable.parent_path();
#if defined(__APPLE__)
    const std::array<fs::path, 3> candidates = {
        executable_dir / filename,
        executable_dir / ".." / "Resources" / filename,
        executable_dir / ".." / "Frameworks" / filename,
    };
#else
    const std::array<fs::path, 2> candidates = {
        executable_dir / filename,
        executable_dir / "resources" / filename,
    };
#endif
    std::error_code ec;
    for (const fs::path& candidate : candidates) {
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }
    return {};
}

fs::path BundledSdkPath() {
    const fs::path executable = CurrentExecutablePath();
    if (executable.empty()) return {};
    const fs::path executable_dir = executable.parent_path();
#if defined(__APPLE__)
    const std::array<fs::path, 4> candidates = {
        executable_dir / ".." / "Resources" / "renpy-sdk",
        executable_dir / ".." / "Frameworks" / "renpy-sdk",
        executable_dir / "renpy-sdk",
        executable_dir / ".." / "Resources" / "RenPy",
    };
#else
    const std::array<fs::path, 2> candidates = {
        executable_dir / "renpy-sdk",
        executable_dir / "resources" / "renpy-sdk",
    };
#endif
    std::error_code ec;
    for (const fs::path& candidate : candidates) {
        if (fs::is_directory(candidate, ec)) return candidate;
    }
    return {};
}

fs::path ResolveOverlaySource() {
    std::error_code ec;
    if (const char* configured = std::getenv("AETHERKIRI_RENPY_OVERLAY_SOURCE");
        configured && *configured &&
        fs::is_regular_file(fs::u8path(configured), ec)) {
        return fs::u8path(configured);
    }
    const fs::path compiled = fs::u8path(AETHERKIRI_RENPY_OVERLAY_SOURCE);
    if (!compiled.empty() && fs::is_regular_file(compiled, ec)) return compiled;
    const fs::path bundled = BundledResourcePath(kOverlayFileName);
    if (!bundled.empty()) return bundled;
    return {};
}

const char* UnsupportedFrameMessage() {
    return "Ren'Py frame bridge has not published a frame yet";
}

constexpr std::array<uint8_t, 8> kFrameMagic = {
    {'A', 'K', 'R', 'F', '1', '\0', '\0', '\0'}};
constexpr size_t kFrameHeaderSize = 8 + 4 + 4 + 8 + 4;
constexpr uint32_t kFramePixelFormatRgba8888 = ENGINE_PIXEL_FORMAT_RGBA8888;

uint32_t ReadU32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8u) |
           (static_cast<uint32_t>(data[2]) << 16u) |
           (static_cast<uint32_t>(data[3]) << 24u);
}

uint64_t ReadU64(const uint8_t* data) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(data[i]) << (8u * i);
    return value;
}

std::string Hex(uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << value;
    return stream.str();
}

std::string JsonNumber(double value) {
    if (!std::isfinite(value)) return "0";
    std::ostringstream stream;
    stream << std::setprecision(17) << value;
    return stream.str();
}

// The Godot shell translates keys to Windows-style virtual keys and uses
// KiriKiri modifier bits (main.gd's _kirikiri_virtual_key/key_modifiers).
// Ren'Py 8.5.x uses pygame_sdl2's SDL2 keycodes/KMOD_* values. Keep this
// translation independent of SDL headers, which are owned by the child SDK.
constexpr int kPygameScancodeMask = 1 << 30;
constexpr int kPygameKmodShift = 0x0003;
constexpr int kPygameKmodControl = 0x00c0;
constexpr int kPygameKmodAlt = 0x0300;
constexpr int kPygameKmodGui = 0x0c00;
constexpr int kAetherModifierShift = 0x01;
constexpr int kAetherModifierAlt = 0x02;
constexpr int kAetherModifierControl = 0x04;
constexpr int kAetherModifierEcho = 0x80;
constexpr int kGodotModifierShift = 1 << 25;
constexpr int kGodotModifierAlt = 1 << 26;
constexpr int kGodotModifierMeta = 1 << 27;
constexpr int kGodotModifierControl = 1 << 28;
constexpr int kGodotSpecial = 1 << 22;
constexpr int kGodotCodeMask = 0x007fffff;

constexpr int PygameFunctionKey(int function) {
    // SDL2's F1-F12 scancodes are contiguous at 58. F13-F24 start at
    // scancode 104, after the keypad block.
    if (function >= 1 && function <= 12)
        return kPygameScancodeMask + 58 + function - 1;
    if (function >= 13 && function <= 24)
        return kPygameScancodeMask + 104 + function - 13;
    return 0;
}

int MapPointerButtonToPygame(int button) {
    // EngineApi: 0=left, 1=right, 2=middle. pygame: 1=left, 2=middle,
    // 3=right. Do not confuse the already-translated ABI with raw Godot's
    // MouseButton enum (1=left, 2=right, 3=middle).
    switch (button) {
    case 0: return 1;
    case 1: return 3;
    case 2: return 2;
    default: return button > 0 ? button : 1;
    }
}

int MapKeyCodeToPygame(int code) {
    // Preserve SDL2 non-printable keycodes used by existing direct callers.
    // Low-byte SDL ASCII and Windows VK values overlap; the ABI's VK
    // interpretation takes precedence (e.g. 0x70 means F1, not 'p').
    if (code >= kPygameScancodeMask) return code;
    if (code >= 0x01000000) code &= kGodotCodeMask;

    // The manual Godot probe passes raw Godot 4 non-printable keycodes.
    // Their distinct KEY_SPECIAL range can be recognized unambiguously.
    if (code >= kGodotSpecial && code < kGodotSpecial + 0x1000) {
        const int special = code - kGodotSpecial;
        switch (special) {
        case 1:  return 27;                       // Escape
        case 2:  // Tab
        case 3:  return 9;                        // Backtab
        case 4:  return 8;                        // Backspace
        case 5:  return 13;                       // Return
        case 6:  return kPygameScancodeMask + 88;  // Keypad Enter
        case 7:  return kPygameScancodeMask + 73;  // Insert
        case 8:  return 127;                      // Delete
        case 9:  return kPygameScancodeMask + 72;  // Pause/Break
        case 10: return kPygameScancodeMask + 70;  // Print Screen
        case 11: return kPygameScancodeMask + 154; // SysReq
        case 12: return kPygameScancodeMask + 156; // Clear
        case 13: return kPygameScancodeMask + 74;  // Home
        case 14: return kPygameScancodeMask + 77;  // End
        case 15: return kPygameScancodeMask + 80;  // Left
        case 16: return kPygameScancodeMask + 82;  // Up
        case 17: return kPygameScancodeMask + 79;  // Right
        case 18: return kPygameScancodeMask + 81;  // Down
        case 19: return kPygameScancodeMask + 75;  // Page Up
        case 20: return kPygameScancodeMask + 78;  // Page Down
        case 21: return kPygameScancodeMask + 225; // Shift (left)
        case 22: return kPygameScancodeMask + 224; // Ctrl (left)
        case 23: return kPygameScancodeMask + 227; // Meta (left)
        case 24: return kPygameScancodeMask + 226; // Alt (left)
        case 25: return kPygameScancodeMask + 57;  // Caps Lock
        case 26: return kPygameScancodeMask + 83;  // Num Lock
        case 27: return kPygameScancodeMask + 71;  // Scroll Lock
        case 66: return kPygameScancodeMask + 118; // Menu
        case 69: return kPygameScancodeMask + 117; // Help
        default: break;
        }
        if (special >= 28 && special <= 62)
            return PygameFunctionKey(special - 27); // F1-F35 (SDL has F1-F24)
        if (special >= 129 && special <= 143) {
            static constexpr int keypad[] = {
                85, 84, 86, 99, 87, 98, 89, 90,
                91, 92, 93, 94, 95, 96, 97,
            }; // Multiply, divide, subtract, period, add, 0..9.
            return kPygameScancodeMask + keypad[special - 129];
        }
        return 0;
    }

    switch (code) {
    case 0x08: return 8;                          // Backspace
    case 0x09: return 9;                          // Tab
    case 0x0c: return kPygameScancodeMask + 156;  // Clear
    case 0x0d: return 13;                         // Return
    case 0x10: return kPygameScancodeMask + 225;  // Shift
    case 0x11: return kPygameScancodeMask + 224;  // Ctrl
    case 0x12: return kPygameScancodeMask + 226;  // Alt
    case 0x13: return kPygameScancodeMask + 72;   // Pause
    case 0x14: return kPygameScancodeMask + 57;   // Caps Lock
    case 0x1b: return 27;                         // Escape
    case 0x20: return 32;                         // Space
    case 0x21: return kPygameScancodeMask + 75;   // Page Up
    case 0x22: return kPygameScancodeMask + 78;   // Page Down
    case 0x23: return kPygameScancodeMask + 77;   // End
    case 0x24: return kPygameScancodeMask + 74;   // Home
    case 0x25: return kPygameScancodeMask + 80;   // Left
    case 0x26: return kPygameScancodeMask + 82;   // Up
    case 0x27: return kPygameScancodeMask + 79;   // Right
    case 0x28: return kPygameScancodeMask + 81;   // Down
    case 0x2c: return kPygameScancodeMask + 70;   // Print Screen
    case 0x2d: return kPygameScancodeMask + 73;   // Insert
    case 0x2e: return 127;                        // Delete
    case 0x2f: return kPygameScancodeMask + 117;  // Help
    case 0x5b: return kPygameScancodeMask + 227;  // Left Windows
    case 0x5c: return kPygameScancodeMask + 231;  // Right Windows
    case 0x5d: return kPygameScancodeMask + 101;  // Applications
    case 0x6a: return kPygameScancodeMask + 85;   // Keypad multiply
    case 0x6b: return kPygameScancodeMask + 87;   // Keypad add
    case 0x6c: return kPygameScancodeMask + 133;  // Keypad separator
    case 0x6d: return kPygameScancodeMask + 86;   // Keypad subtract
    case 0x6e: return kPygameScancodeMask + 99;   // Keypad decimal
    case 0x6f: return kPygameScancodeMask + 84;   // Keypad divide
    case 0x90: return kPygameScancodeMask + 83;   // Num Lock
    case 0x91: return kPygameScancodeMask + 71;   // Scroll Lock
    case 0xa0: return kPygameScancodeMask + 225;  // Left Shift
    case 0xa1: return kPygameScancodeMask + 229;  // Right Shift
    case 0xa2: return kPygameScancodeMask + 224;  // Left Ctrl
    case 0xa3: return kPygameScancodeMask + 228;  // Right Ctrl
    case 0xa4: return kPygameScancodeMask + 226;  // Left Alt
    case 0xa5: return kPygameScancodeMask + 230;  // Right Alt
    case 0xba: return ';';
    case 0xbb: return '=';
    case 0xbc: return ',';
    case 0xbd: return '-';
    case 0xbe: return '.';
    case 0xbf: return '/';
    case 0xc0: return '`';
    case 0xdb: return '[';
    case 0xdc: return '\\';
    case 0xdd: return ']';
    case 0xde: return '\'';
    case 0xe2: return '\\';
    default: break;
    }
    if (code >= 0x60 && code <= 0x69)
        return kPygameScancodeMask + (code == 0x60 ? 98 : 89 + code - 0x61);
    if (code >= 0x70 && code <= 0x87)
        return PygameFunctionKey(code - 0x70 + 1);
    if (code >= 'A' && code <= 'Z') return code + ('a' - 'A');
    if (code >= 0x21 && code <= 0x7e) return code;
    return 0; // K_UNKNOWN
}

int MapModifiersToPygame(int modifiers) {
    int result = 0;
    if (modifiers & (kAetherModifierShift | kGodotModifierShift))
        result |= kPygameKmodShift;
    if (modifiers & (kAetherModifierAlt | kGodotModifierAlt))
        result |= kPygameKmodAlt;
    if (modifiers & (kAetherModifierControl | kGodotModifierControl))
        result |= kPygameKmodControl;
    if (modifiers & kGodotModifierMeta) result |= kPygameKmodGui;
    // No raw SDL passthrough: Aether's echo=0x80 would become SDL's RCtrl;
    // mouse-held/cancel bits are not keyboard modifiers either.
    return result;
}

bool IsAetherKeyRepeat(int modifiers) {
    return (modifiers & kAetherModifierEcho) != 0;
}

std::string Utf8Codepoint(uint32_t value) {
    if (value == 0 || value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu)) return {};
    std::string result;
    if (value <= 0x7fu) {
        result.push_back(static_cast<char>(value));
    } else if (value <= 0x7ffu) {
        result.push_back(static_cast<char>(0xc0u | (value >> 6u)));
        result.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
    } else if (value <= 0xffffu) {
        result.push_back(static_cast<char>(0xe0u | (value >> 12u)));
        result.push_back(static_cast<char>(0x80u | ((value >> 6u) & 0x3fu)));
        result.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
    } else {
        result.push_back(static_cast<char>(0xf0u | (value >> 18u)));
        result.push_back(static_cast<char>(0x80u | ((value >> 12u) & 0x3fu)));
        result.push_back(static_cast<char>(0x80u | ((value >> 6u) & 0x3fu)));
        result.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
    }
    return result;
}

std::string JsonEscape(const std::string& value) {
    std::ostringstream stream;
    stream << '"';
    for (unsigned char c : value) {
        switch (c) {
        case '"': stream << "\\\""; break;
        case '\\': stream << "\\\\"; break;
        case '\b': stream << "\\b"; break;
        case '\f': stream << "\\f"; break;
        case '\n': stream << "\\n"; break;
        case '\r': stream << "\\r"; break;
        case '\t': stream << "\\t"; break;
        default:
            if (c < 0x20u) {
                stream << "\\u00" << std::hex << std::setw(2)
                       << std::setfill('0') << static_cast<unsigned>(c)
                       << std::dec << std::setfill(' ');
            } else {
                stream << static_cast<char>(c);
            }
            break;
        }
    }
    stream << '"';
    return stream.str();
}

bool HasRenpyProject(const fs::path& root) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return false;
    // A game directory must contain the script directory.  A bare script.rpy
    // is accepted for tiny SDK projects, while a random directory is not.
    return (fs::is_directory(root / "game", ec) &&
            (fs::exists(root / "game" / "script.rpy", ec) ||
             fs::exists(root / "game" / "script.rpyc", ec) ||
             fs::exists(root / "game" / "options.rpy", ec))) ||
           fs::exists(root / "script.rpy", ec);
}

fs::path FindLauncher(const fs::path& sdk) {
#if defined(_WIN32)
    const char* names[] = {"renpy.exe", "renpy"};
#elif defined(__APPLE__)
    const char* names[] = {"renpy", "renpy.sh"};
#else
    const char* names[] = {"renpy", "renpy.sh"};
#endif
    std::error_code ec;
    for (const char* name : names) {
        const fs::path candidate = sdk / name;
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }
    return {};
}

#if defined(_WIN32)
std::wstring QuoteWindowsArg(const std::wstring& arg) {
    // CommandLineToArgvW-compatible quoting. Never pass user input through a
    // shell and preserve trailing backslashes before a closing quote.
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(c);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                        static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, value.data(),
                            static_cast<int>(value.size()), result.data(), size) <= 0)
        return {};
    return result;
}
#endif

class SdkProcess final {
public:
    SdkProcess() = default;
    ~SdkProcess() { Stop(); }
    SdkProcess(const SdkProcess&) = delete;
    SdkProcess& operator=(const SdkProcess&) = delete;

    bool Start(const fs::path& launcher, const fs::path& project,
               const std::vector<std::pair<std::string, std::string>>& environment,
               std::string* error) {
        Stop();
#if defined(_WIN32)
        std::wstring launcher_w = launcher.wstring();
        std::wstring command = QuoteWindowsArg(launcher_w) + L" " +
                               QuoteWindowsArg(project.wstring());
        std::wstring environment_block;
        if (!environment.empty()) {
            std::vector<std::wstring> values;
            LPWCH current = GetEnvironmentStringsW();
            if (current == nullptr) {
                if (error) *error = "failed to read process environment";
                return false;
            }
            for (LPWCH entry = current; *entry != L'\0';
                 entry += std::wcslen(entry) + 1)
                values.emplace_back(entry);
            FreeEnvironmentStringsW(current);
            for (const auto& [key, value] : environment) {
                const std::wstring wide_key = Utf8ToWide(key);
                const std::wstring wide_value = Utf8ToWide(value);
                const std::wstring prefix = wide_key + L"=";
                const auto found = std::find_if(
                    values.begin(), values.end(), [&](const std::wstring& existing) {
                        return existing.compare(0, prefix.size(), prefix) == 0;
                    });
                const std::wstring replacement = prefix + wide_value;
                if (found == values.end()) values.push_back(replacement);
                else *found = replacement;
            }
            for (const std::wstring& value : values) {
                environment_block.append(value);
                environment_block.push_back(L'\0');
            }
            environment_block.push_back(L'\0');
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');
        const DWORD creation_flags = CREATE_NO_WINDOW |
            (environment_block.empty() ? 0u : CREATE_UNICODE_ENVIRONMENT);
        if (!CreateProcessW(launcher_w.c_str(), mutable_command.data(), nullptr,
                            nullptr, FALSE,
                            creation_flags | CREATE_SUSPENDED, nullptr,
                            environment_block.empty() ? nullptr
                                                       : environment_block.data(),
                            &startup, &process)) {
            if (error) *error = "failed to launch Ren'Py SDK: " +
                std::to_string(GetLastError());
            return false;
        }
        // A Ren'Py launcher may hand work to Python helper processes. A Job
        // Object makes those descendants part of this runtime's ownership
        // tree, so closing the runtime cannot leave them behind.
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job == nullptr) {
            TerminateProcess(process.hProcess, 0xC000013A);
            (void)WaitForSingleObject(process.hProcess, 2000);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            if (error) *error = "failed to create Ren'Py process job: " +
                std::to_string(GetLastError());
            return false;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                     &limits, sizeof(limits)) ||
            !AssignProcessToJobObject(job, process.hProcess)) {
            const DWORD failure = GetLastError();
            TerminateProcess(process.hProcess, 0xC000013A);
            (void)WaitForSingleObject(process.hProcess, 2000);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            CloseHandle(job);
            if (error) *error = "failed to own Ren'Py process tree: " +
                std::to_string(failure);
            return false;
        }
        if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
            const DWORD failure = GetLastError();
            TerminateProcess(process.hProcess, 0xC000013A);
            (void)WaitForSingleObject(process.hProcess, 2000);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            CloseHandle(job);
            if (error) *error = "failed to start Ren'Py process: " +
                std::to_string(failure);
            return false;
        }
        CloseHandle(process.hThread);
        process_ = process.hProcess;
        job_ = job;
        return true;
#else
        const std::string exe = launcher.string();
        const std::string root = project.string();
        char* const argv[] = {const_cast<char*>(exe.c_str()),
                              const_cast<char*>(root.c_str()), nullptr};
        std::vector<std::string> environment_storage;
        std::vector<char*> environment_argv;
        if (!environment.empty()) {
            for (char** entry = ProcessEnvironment(); entry != nullptr && *entry != nullptr;
                 ++entry)
                environment_storage.emplace_back(*entry);
            for (const auto& [key, value] : environment) {
                const std::string prefix = key + "=";
                const auto found = std::find_if(
                    environment_storage.begin(), environment_storage.end(),
                    [&](const std::string& existing) {
                        return existing.compare(0, prefix.size(), prefix) == 0;
                    });
                const std::string replacement = prefix + value;
                if (found == environment_storage.end())
                    environment_storage.push_back(replacement);
                else
                    *found = replacement;
            }
            environment_argv.reserve(environment_storage.size() + 1);
            for (std::string& entry : environment_storage)
                environment_argv.push_back(entry.data());
            environment_argv.push_back(nullptr);
        }
        // posix_spawn avoids running C++ code in a post-fork child of the
        // multithreaded Godot host. It also preserves argv boundaries and
        // never invokes a shell. Put the SDK and any children it starts in a
        // private process group so shutdown cannot leave a Python child behind.
        posix_spawnattr_t attributes;
        if (posix_spawnattr_init(&attributes) != 0) {
            if (error) *error = "failed to initialize Ren'Py process attributes";
            return false;
        }
        const short flags = POSIX_SPAWN_SETPGROUP;
        int result = posix_spawnattr_setflags(&attributes, flags);
        if (result == 0) result = posix_spawnattr_setpgroup(&attributes, 0);
        pid_t child = -1;
        if (result == 0) {
            result = posix_spawn(&child, exe.c_str(), nullptr, &attributes,
                                 argv, environment.empty() ? ProcessEnvironment()
                                                             : environment_argv.data());
        }
        (void)posix_spawnattr_destroy(&attributes);
        if (result != 0) {
            if (error) *error = "failed to launch Ren'Py SDK: " +
                std::string(std::strerror(result));
            return false;
        }
        leader_pid_ = child;
        process_group_id_ = child;
        return true;
#endif
    }

    bool Running(std::string* error) {
#if defined(_WIN32)
        if (process_ == nullptr) return false;
        const DWORD result = WaitForSingleObject(process_, 0);
        if (result == WAIT_TIMEOUT) return true;
        if (result == WAIT_OBJECT_0) {
            DWORD exit_code = 0;
            GetExitCodeProcess(process_, &exit_code);
            if (error) *error = "Ren'Py SDK exited with status " +
                std::to_string(exit_code);
            CloseHandle(process_); process_ = nullptr;
            return false;
        }
        if (error) *error = "failed to query Ren'Py SDK process";
        return false;
#else
        if (leader_pid_ <= 0) return false;
        int status = 0;
        pid_t result = -1;
        do {
            result = waitpid(leader_pid_, &status, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (result == 0) return true;
        if (result == leader_pid_) {
            leader_pid_ = -1;
            if (error) {
                if (WIFEXITED(status)) *error = "Ren'Py SDK exited with status " +
                    std::to_string(WEXITSTATUS(status));
                else if (WIFSIGNALED(status)) *error = "Ren'Py SDK terminated by signal " +
                    std::to_string(WTERMSIG(status));
                else *error = "Ren'Py SDK exited";
            }
            return false;
        }
        if (result < 0 && errno != EINTR && error) *error = std::strerror(errno);
        return result == 0;
#endif
    }

    void Pause(bool pause) {
#if defined(_WIN32)
        // The Windows SDK does not expose a stable suspend API; leave process
        // ownership intact and let the caller receive NOT_SUPPORTED.
        (void)pause;
#else
        if (leader_pid_ > 0) (void)kill(process_group_id_ > 0 ? -process_group_id_
                                                              : leader_pid_,
                                           pause ? SIGSTOP : SIGCONT);
#endif
    }

    void Stop() {
#if defined(_WIN32)
        if (process_ != nullptr) {
            if (WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) {
                TerminateProcess(process_, 0xC000013A);
                WaitForSingleObject(process_, 2000);
            }
            CloseHandle(process_); process_ = nullptr;
        }
        if (job_ != nullptr) {
            // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE terminates any descendants
            // that outlived the launcher, including after its handle closed.
            CloseHandle(job_); job_ = nullptr;
        }
#else
        StopPosix();
#endif
    }

    bool valid() const {
#if defined(_WIN32)
        return process_ != nullptr;
#else
        return leader_pid_ > 0;
#endif
    }

private:
#if defined(_WIN32)
    HANDLE process_ = nullptr;
    HANDLE job_ = nullptr;
#else
    pid_t leader_pid_ = -1;
    pid_t process_group_id_ = -1;
#endif

#if !defined(_WIN32)
    void StopPosix() {
        const pid_t leader = leader_pid_;
        const pid_t group = process_group_id_;
        if (leader <= 0 && group <= 0) return;

        auto group_exists = [group] {
            if (group <= 0) return false;
            errno = 0;
            const int result = kill(-group, 0);
            return result == 0 || errno == EPERM;
        };
        auto reap_leader = [this] {
            if (leader_pid_ <= 0) return false;
            int status = 0;
            for (;;) {
                const pid_t result = waitpid(leader_pid_, &status, WNOHANG);
                if (result == leader_pid_ || (result < 0 && errno == ECHILD)) {
                    leader_pid_ = -1;
                    return true;
                }
                if (result < 0 && errno == EINTR) continue;
                return false;
            }
        };

        // SIGSTOP also stops the leader, so always continue the group before
        // asking it to terminate. Otherwise a paused SDK survives the grace
        // period and is only killed, skipping normal signal handling.
        if (group_exists()) {
            (void)kill(-group, SIGCONT);
            (void)kill(-group, SIGTERM);
        } else {
            (void)reap_leader();
        }

        // Bound graceful shutdown. We only signal a group while kill(..., 0)
        // confirms that the original process group still exists; this avoids
        // sending a stale PGID to an unrelated process after it disappears.
        for (int i = 0; i < 200; ++i) {
            (void)reap_leader();
            if (!group_exists()) {
                leader_pid_ = -1;
                process_group_id_ = -1;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (group_exists()) {
            (void)kill(-group, SIGCONT);
            (void)kill(-group, SIGKILL);
        }
        // A leader that ignored SIGTERM is still our child; reap it without
        // waiting on arbitrary descendants.
        if (leader_pid_ > 0) {
            for (;;) {
                const pid_t result = waitpid(leader_pid_, nullptr, 0);
                if (result == leader_pid_ || (result < 0 && errno == ECHILD)) break;
                if (result < 0 && errno == EINTR) continue;
                break;
            }
        }
        leader_pid_ = -1;
        process_group_id_ = -1;
    }
#endif
};

struct Runtime {
    engine_runtime_host_v1_t host{};
    fs::path sdk_root = fs::path(AETHERKIRI_RENPY_SDK_ROOT);
    bool sdk_path_explicit = false;
    fs::path project;
    fs::path bridge_root;
    fs::path staged_module;
    fs::path staged_loader;
    fs::path frame_path;
    fs::path input_path;
    fs::path error_path;
    bool staged_libs_directory = false;
    uint32_t frame_width = 0;
    uint32_t frame_height = 0;
    uint64_t frame_serial = 0;
    std::vector<uint8_t> frame_pixels;
    std::mutex frame_mutex;
    SdkProcess process;
    bool opened = false;
    bool paused = false;
    std::string error;

    ~Runtime() {
        process.Stop();
        CleanupBridge();
    }

    void CleanupBridge() {
        std::error_code ec;
        if (!staged_module.empty()) fs::remove(staged_module, ec);
        ec.clear();
        if (!staged_loader.empty()) fs::remove(staged_loader, ec);
        ec.clear();
        if (staged_libs_directory) {
            const fs::path libs = project / "game" / "libs";
            fs::remove(libs, ec);
        }
        ec.clear();
        if (!frame_path.empty()) fs::remove(frame_path, ec);
        ec.clear();
        if (!input_path.empty()) fs::remove(input_path, ec);
        ec.clear();
        if (!error_path.empty()) fs::remove(error_path, ec);
        ec.clear();
        if (!bridge_root.empty()) fs::remove_all(bridge_root, ec);
        staged_module.clear();
        staged_loader.clear();
        bridge_root.clear();
        frame_path.clear();
        input_path.clear();
        error_path.clear();
        staged_libs_directory = false;
    }
};

Runtime* Cast(void* value) { return static_cast<Runtime*>(value); }

std::atomic<uint64_t> g_bridge_counter{0};

uint64_t CurrentProcessIdValue() {
#if defined(_WIN32)
    return static_cast<uint64_t>(GetCurrentProcessId());
#else
    return static_cast<uint64_t>(getpid());
#endif
}

bool WriteTextFile(const fs::path& path, const std::string& text,
                   std::string* error) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        if (error) *error = "failed to stage Ren'Py overlay file: " + path.string();
        return false;
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.flush();
    if (!stream) {
        if (error) *error = "failed to write Ren'Py overlay file: " + path.string();
        return false;
    }
    return true;
}

bool PrepareBridge(Runtime& runtime) {
    runtime.CleanupBridge();
    const fs::path source = ResolveOverlaySource();
    std::error_code ec;
    if (source.empty() || !fs::is_regular_file(source, ec)) {
        runtime.error =
            "Ren'Py overlay source is unavailable in this build or bundle; "
            "frame bridge cannot be activated";
        return false;
    }
    std::ifstream source_stream(source, std::ios::binary);
    if (!source_stream) {
        runtime.error = "failed to read Ren'Py overlay source: " + source.string();
        return false;
    }
    const std::string source_text((std::istreambuf_iterator<char>(source_stream)),
                                  std::istreambuf_iterator<char>());
    if (source_text.empty()) {
        runtime.error = "Ren'Py overlay source is empty";
        return false;
    }

    const fs::path libs = runtime.project / "game" / "libs";
    if (fs::exists(libs, ec)) {
        if (!fs::is_directory(libs, ec)) {
            runtime.error = "Ren'Py game/libs exists but is not a directory";
            return false;
        }
    } else {
        ec.clear();
        if (!fs::create_directory(libs, ec)) {
            runtime.error = "Ren'Py game/libs is not writable: " + ec.message();
            return false;
        }
        runtime.staged_libs_directory = true;
    }

    const uint64_t id = g_bridge_counter.fetch_add(1) + 1;
    const std::string token = "ak_" + Hex(CurrentProcessIdValue()) + "_" +
                              Hex(id) + "_" +
                              Hex(static_cast<uint64_t>(
                                  std::chrono::steady_clock::now()
                                      .time_since_epoch()
                                      .count()));
    runtime.staged_module = libs / ("aetherkiri_overlay_" + token + ".py");
    runtime.staged_loader = libs / ("aetherkiri_loader_" + token + ".rpe.py");
    runtime.frame_path = fs::temp_directory_path() /
                         ("aetherkiri-renpy-frame-" + token + ".bin");
    runtime.input_path = fs::temp_directory_path() /
                         ("aetherkiri-renpy-input-" + token + ".jsonl");
    runtime.error_path = fs::temp_directory_path() /
                         ("aetherkiri-renpy-error-" + token + ".log");
    runtime.bridge_root = fs::temp_directory_path() /
                          ("aetherkiri-renpy-bridge-" + token);
    ec.clear();
    if (!fs::create_directory(runtime.bridge_root, ec)) {
        if (fs::is_directory(runtime.bridge_root, ec)) {
            runtime.error = "Ren'Py bridge transport directory already exists";
        } else {
            runtime.error =
                "failed to create Ren'Py bridge transport directory: " +
                ec.message();
        }
        runtime.CleanupBridge();
        return false;
    }

    const std::string module_name = runtime.staged_module.stem().string();
    const std::string loader =
        "import os, sys\n"
        "if os.environ.get('AETHERKIRI_RENPY_OVERLAY') == '1':\n"
        "    sys.dont_write_bytecode = True\n"
        "    sys.path.insert(0, os.path.dirname(__file__))\n"
        "    import " + module_name + "\n"
        "    " + module_name + ".install()\n";
    if (!WriteTextFile(runtime.staged_module, source_text, &runtime.error) ||
        !WriteTextFile(runtime.staged_loader, loader, &runtime.error)) {
        runtime.CleanupBridge();
        return false;
    }
    std::ofstream(runtime.input_path, std::ios::binary | std::ios::trunc).close();
    if (!fs::exists(runtime.input_path, ec)) {
        runtime.error = "failed to create Ren'Py input transport";
        runtime.CleanupBridge();
        return false;
    }
    return true;
}

bool RefreshFrame(Runtime& runtime) {
    if (runtime.frame_path.empty()) return false;
    std::ifstream stream(runtime.frame_path, std::ios::binary);
    if (!stream) return false;
    std::array<uint8_t, kFrameHeaderSize> header{};
    stream.read(reinterpret_cast<char*>(header.data()),
                static_cast<std::streamsize>(header.size()));
    if (stream.gcount() != static_cast<std::streamsize>(header.size())) return false;
    if (!std::equal(kFrameMagic.begin(), kFrameMagic.end(), header.begin())) {
        runtime.error = "Ren'Py frame transport has an invalid magic header";
        return false;
    }
    const uint32_t width = ReadU32(header.data() + 8);
    const uint32_t height = ReadU32(header.data() + 12);
    const uint64_t serial = ReadU64(header.data() + 16);
    const uint32_t payload_size = ReadU32(header.data() + 24);
    const uint64_t expected = static_cast<uint64_t>(width) * height * 4u;
    if (width == 0 || height == 0 || serial == 0 || expected != payload_size ||
        expected > 256u * 1024u * 1024u) {
        runtime.error = "Ren'Py frame transport has invalid dimensions or payload";
        return false;
    }
    std::vector<uint8_t> pixels(payload_size);
    stream.read(reinterpret_cast<char*>(pixels.data()),
                static_cast<std::streamsize>(pixels.size()));
    if (stream.gcount() != static_cast<std::streamsize>(pixels.size())) return false;
    {
        std::lock_guard<std::mutex> guard(runtime.frame_mutex);
        if (serial <= runtime.frame_serial) return true;
        runtime.frame_width = width;
        runtime.frame_height = height;
        runtime.frame_serial = serial;
        runtime.frame_pixels = std::move(pixels);
        runtime.error.clear();
    }
    return true;
}

void RefreshDiagnostics(Runtime& runtime) {
    if (runtime.error_path.empty()) return;
    std::ifstream stream(runtime.error_path, std::ios::binary);
    if (!stream) return;
    std::string text((std::istreambuf_iterator<char>(stream)),
                     std::istreambuf_iterator<char>());
    if (text.size() > 4096) text.erase(0, text.size() - 4096);
    if (!text.empty()) runtime.error = "Ren'Py overlay: " + text;
}

bool AppendInput(Runtime& runtime, const std::string& line) {
    if (runtime.input_path.empty()) return false;
    std::ofstream stream(runtime.input_path, std::ios::binary | std::ios::app);
    if (!stream) {
        runtime.error = "failed to open Ren'Py input transport";
        return false;
    }
    stream << line << '\n';
    stream.flush();
    if (!stream) {
        runtime.error = "failed to write Ren'Py input transport";
        return false;
    }
    return true;
}

int32_t Probe(void*, const char* path) {
    if (!path || !*path) return 0;
    try { return HasRenpyProject(fs::u8path(path)) ? 65 : 0; }
    catch (...) { return 0; }
}

engine_result_t Create(void*, const engine_runtime_host_v1_t* host,
                       const engine_create_desc_t* desc, void** output) {
    if (!host || !desc || !output || host->struct_size < sizeof(*host) ||
        desc->struct_size < sizeof(*desc)) return ENGINE_RESULT_INVALID_ARGUMENT;
    *output = nullptr;
    try {
        auto runtime = std::make_unique<Runtime>();
        runtime->host = *host;
        *output = runtime.release();
        return ENGINE_RESULT_OK;
    } catch (...) { return ENGINE_RESULT_INTERNAL_ERROR; }
}
void Destroy(void* value) { delete Cast(value); }

engine_result_t Open(void* value, const char* path, const char* startup) {
    if (!value || !path) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& runtime = *Cast(value);
    if (runtime.opened) {
        runtime.error = "Ren'Py game is already open";
        return ENGINE_RESULT_INVALID_STATE;
    }
    (void)startup;  // Ren'Py selects its own script from game/options.rpy.
    runtime.project = fs::u8path(path);
    std::error_code ec;
    if (const char* configured = std::getenv("AETHERKIRI_RENPY_SDK_ROOT");
        configured && *configured && !runtime.sdk_path_explicit) {
        runtime.sdk_root = fs::u8path(configured);
    } else if (!runtime.sdk_path_explicit && (runtime.sdk_root.empty() ||
               !fs::is_directory(runtime.sdk_root, ec))) {
        const fs::path bundled = BundledSdkPath();
        if (!bundled.empty()) runtime.sdk_root = bundled;
    }
    if (!HasRenpyProject(runtime.project)) {
        runtime.error = "directory is not a Ren'Py project (expected game/script.rpy)";
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    if (runtime.sdk_root.empty() || !fs::is_directory(runtime.sdk_root, ec)) {
        runtime.error = "Ren'Py SDK path is not configured; set AETHERKIRI_RENPY_SDK_ROOT";
        return ENGINE_RESULT_NOT_SUPPORTED;
    }
    const fs::path launcher = FindLauncher(runtime.sdk_root);
    if (launcher.empty()) {
        runtime.error = "Ren'Py SDK launcher not found under configured SDK path";
        return ENGINE_RESULT_NOT_SUPPORTED;
    }
    if (!PrepareBridge(runtime)) return ENGINE_RESULT_NOT_SUPPORTED;
    const std::vector<std::pair<std::string, std::string>> environment = {
        {"AETHERKIRI_RENPY_OVERLAY", "1"},
        {"AETHERKIRI_RENPY_FRAME", runtime.frame_path.string()},
        {"AETHERKIRI_RENPY_INPUT", runtime.input_path.string()},
        {"AETHERKIRI_RENPY_ERROR", runtime.error_path.string()},
        {"RENPY_RENDERER", "sw"},
    };
    if (!runtime.process.Start(launcher, runtime.project, environment,
                               &runtime.error)) {
        runtime.CleanupBridge();
        return ENGINE_RESULT_IO_ERROR;
    }
    if (!runtime.process.Running(&runtime.error)) {
        runtime.error = runtime.error.empty()
            ? "Ren'Py SDK exited during startup" : runtime.error;
        runtime.CleanupBridge();
        return ENGINE_RESULT_IO_ERROR;
    }
    runtime.opened = true;
    return ENGINE_RESULT_OK;
}

engine_result_t Tick(void* value, uint32_t) {
    if (!value) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& runtime = *Cast(value);
    if (!runtime.opened) return ENGINE_RESULT_INVALID_STATE;
    if (!runtime.process.Running(&runtime.error)) {
        runtime.opened = false;
        runtime.CleanupBridge();
        return ENGINE_RESULT_INVALID_STATE;
    }
    (void)RefreshFrame(runtime);
    RefreshDiagnostics(runtime);
    return ENGINE_RESULT_OK;
}
engine_result_t Pause(void* value) {
    if (!value) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& runtime = *Cast(value);
    if (!runtime.opened || !runtime.process.valid()) return ENGINE_RESULT_INVALID_STATE;
#if defined(_WIN32)
    runtime.error = "Ren'Py process pause is unavailable on Windows";
    return ENGINE_RESULT_NOT_SUPPORTED;
#else
    runtime.process.Pause(true); runtime.paused = true; return ENGINE_RESULT_OK;
#endif
}
engine_result_t Resume(void* value) {
    if (!value) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& runtime = *Cast(value);
    if (!runtime.opened || !runtime.process.valid()) return ENGINE_RESULT_INVALID_STATE;
#if defined(_WIN32)
    runtime.error = "Ren'Py process resume is unavailable on Windows";
    return ENGINE_RESULT_NOT_SUPPORTED;
#else
    runtime.process.Pause(false); runtime.paused = false; return ENGINE_RESULT_OK;
#endif
}
engine_result_t Option(void* value, const engine_option_t* option) {
    if (!value || !option || !option->key_utf8 || !option->value_utf8)
        return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& runtime = *Cast(value);
    const std::string key = option->key_utf8;
    if (key == "renpy_sdk_path") {
        if (runtime.opened) {
            runtime.error = "renpy_sdk_path must be set before opening a game";
            return ENGINE_RESULT_INVALID_STATE;
        }
        runtime.sdk_root = fs::u8path(option->value_utf8);
        runtime.sdk_path_explicit = true;
        return ENGINE_RESULT_OK;
    }
    return key.rfind("renpy_", 0) == 0 ? ENGINE_RESULT_NOT_SUPPORTED
                                       : ENGINE_RESULT_OK;
}
engine_result_t Resize(void* value, uint32_t width, uint32_t height) {
    if (!value || width == 0 || height == 0) return ENGINE_RESULT_INVALID_ARGUMENT;
    Cast(value)->error =
        "Ren'Py SDK surface size is controlled by game/options.rpy";
    return ENGINE_RESULT_NOT_SUPPORTED;
}
engine_result_t Frame(void* value, engine_frame_desc_t* frame) {
    if (!value || !frame || frame->struct_size < sizeof(*frame))
        return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& runtime = *Cast(value);
    (void)RefreshFrame(runtime);
    RefreshDiagnostics(runtime);
    std::lock_guard<std::mutex> guard(runtime.frame_mutex);
    if (runtime.frame_pixels.empty()) {
        runtime.error = UnsupportedFrameMessage();
        return ENGINE_RESULT_NOT_SUPPORTED;
    }
    frame->width = runtime.frame_width;
    frame->height = runtime.frame_height;
    frame->stride_bytes = runtime.frame_width * 4u;
    frame->pixel_format = kFramePixelFormatRgba8888;
    frame->frame_serial = runtime.frame_serial;
    return ENGINE_RESULT_OK;
}
engine_result_t Read(void* value, void* pixels, size_t size) {
    if (!value || !pixels || size == 0) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& runtime = *Cast(value);
    (void)RefreshFrame(runtime);
    RefreshDiagnostics(runtime);
    std::lock_guard<std::mutex> guard(runtime.frame_mutex);
    if (runtime.frame_pixels.empty()) {
        runtime.error = UnsupportedFrameMessage();
        return ENGINE_RESULT_NOT_SUPPORTED;
    }
    if (size < runtime.frame_pixels.size()) {
        runtime.error = "Ren'Py frame buffer is too small";
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    std::memcpy(pixels, runtime.frame_pixels.data(), runtime.frame_pixels.size());
    return ENGINE_RESULT_OK;
}
engine_result_t NativeFrame(void* value, uint64_t* texture, uint32_t* width,
                            uint32_t* height, uint64_t* serial) {
    if (!value || !texture || !width || !height || !serial)
        return ENGINE_RESULT_INVALID_ARGUMENT;
    Cast(value)->error =
        "Ren'Py native GPU texture import is unavailable; use RGBA frame data";
    return ENGINE_RESULT_NOT_SUPPORTED;
}
engine_result_t Input(void* value, const engine_input_event_t* event) {
    if (!value || !event || event->struct_size < sizeof(*event))
        return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& runtime = *Cast(value);
    if (!runtime.opened) return ENGINE_RESULT_INVALID_STATE;
    int event_type = 0;
    std::ostringstream attrs;
    switch (event->type) {
    case ENGINE_INPUT_EVENT_POINTER_DOWN:
        event_type = 1025;  // pygame.MOUSEBUTTONDOWN
        attrs << "\"pos\":[" << JsonNumber(event->x) << ','
              << JsonNumber(event->y) << "],\"button\":"
              << MapPointerButtonToPygame(event->button);
        break;
    case ENGINE_INPUT_EVENT_POINTER_MOVE:
        event_type = 1024;  // pygame.MOUSEMOTION
        attrs << "\"pos\":[" << JsonNumber(event->x) << ','
              << JsonNumber(event->y) << "],\"rel\":["
              << JsonNumber(event->delta_x) << ',' << JsonNumber(event->delta_y)
              << "]";
        break;
    case ENGINE_INPUT_EVENT_POINTER_UP:
        event_type = 1026;  // pygame.MOUSEBUTTONUP
        attrs << "\"pos\":[" << JsonNumber(event->x) << ','
              << JsonNumber(event->y) << "],\"button\":"
              << MapPointerButtonToPygame(event->button);
        break;
    case ENGINE_INPUT_EVENT_POINTER_SCROLL:
        event_type = 1027;  // pygame.MOUSEWHEEL
        attrs << "\"x\":" << JsonNumber(event->delta_x)
              << ",\"y\":" << JsonNumber(event->delta_y)
              << ",\"precise_x\":" << JsonNumber(event->delta_x)
              << ",\"precise_y\":" << JsonNumber(event->delta_y);
        break;
    case ENGINE_INPUT_EVENT_KEY_DOWN:
        event_type = 768;  // pygame.KEYDOWN
        attrs << "\"key\":" << MapKeyCodeToPygame(event->key_code)
              << ",\"mod\":" << MapModifiersToPygame(event->modifiers)
              << ",\"unicode\":"
              << JsonEscape(Utf8Codepoint(event->unicode_codepoint))
              << ",\"repeat\":"
              << (IsAetherKeyRepeat(event->modifiers) ? "true" : "false");
        break;
    case ENGINE_INPUT_EVENT_KEY_UP:
        event_type = 769;  // pygame.KEYUP
        attrs << "\"key\":" << MapKeyCodeToPygame(event->key_code)
              << ",\"mod\":" << MapModifiersToPygame(event->modifiers)
              << ",\"unicode\":"
              << JsonEscape(Utf8Codepoint(event->unicode_codepoint))
              << ",\"repeat\":false";
        break;
    case ENGINE_INPUT_EVENT_TEXT_INPUT:
        event_type = 771;  // pygame.TEXTINPUT
        attrs << "\"text\":"
              << JsonEscape(Utf8Codepoint(event->unicode_codepoint));
        break;
    case ENGINE_INPUT_EVENT_BACK:
        event_type = 768;  // pygame.KEYDOWN / Escape
        attrs << "\"key\":27,\"mod\":0,\"unicode\":\"\"";
        break;
    default:
        runtime.error = "unsupported Ren'Py input event type";
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    std::ostringstream line;
    line << "{\"type\":" << event_type << ",\"attributes\":{"
         << attrs.str() << "}}";
    return AppendInput(runtime, line.str()) ? ENGINE_RESULT_OK
                                            : ENGINE_RESULT_IO_ERROR;
}
engine_result_t Rendered(void* value, uint32_t* rendered) {
    if (!value || !rendered) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& runtime = *Cast(value);
    (void)RefreshFrame(runtime);
    std::lock_guard<std::mutex> guard(runtime.frame_mutex);
    *rendered = runtime.frame_serial != 0 ? 1u : 0u;
    return ENGINE_RESULT_OK;
}
engine_result_t Renderer(void* value, char* buffer, uint32_t size) {
    if (!value || !buffer || size == 0) return ENGINE_RESULT_INVALID_ARGUMENT;
    const char* text = "Ren'Py official SDK subprocess (AKRF1 RGBA bridge)";
    const size_t required = std::strlen(text) + 1;
    if (size < required) return ENGINE_RESULT_INVALID_ARGUMENT;
    std::memcpy(buffer, text, required);
    return ENGINE_RESULT_OK;
}
engine_result_t TextInput(void* value, uint32_t* flags) {
    if (!value || !flags) return ENGINE_RESULT_INVALID_ARGUMENT;
    *flags = 0; return ENGINE_RESULT_OK;
}
const char* Error(void* value) {
    return value ? Cast(value)->error.c_str() : "Invalid Ren'Py runtime";
}

const engine_runtime_provider_v1_t& Provider() {
    static const engine_runtime_provider_v1_t provider = [] {
        engine_runtime_provider_v1_t p{};
        p.struct_size = sizeof(p);
        p.api_version = ENGINE_RUNTIME_PROVIDER_API_VERSION;
        p.runtime_id_utf8 = "renpy";
        p.display_name_utf8 = "Ren'Py (official SDK)";
        p.priority = 60;
        p.probe = Probe; p.create = Create; p.destroy = Destroy;
        p.open_game = Open; p.tick = Tick; p.pause = Pause; p.resume = Resume;
        p.set_option = Option; p.set_surface_size = Resize;
        p.get_frame_desc = Frame; p.read_frame_rgba = Read;
        p.get_godot_native_frame_texture = NativeFrame;
        p.send_input = Input; p.get_frame_rendered_flag = Rendered;
        p.get_renderer_info = Renderer; p.get_text_input_state = TextInput;
        p.get_last_error = Error;
        return p;
    }();
    return provider;
}
}  // namespace

void RegisterRuntimeProvider() {
    static std::once_flag once;
    std::call_once(once, [] { engine_register_runtime_provider(&Provider()); });
}

}  // namespace aetherkiri::renpy
