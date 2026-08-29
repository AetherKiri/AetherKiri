#include "tjsCommHead.h"
#include "ReplFileChannel.h"

#include "../base/CharacterSet.h"
#include "../base/ScriptMgnIntf.h"
#include "../base/SysInitIntf.h"
#include "DebugIntf.h"
#include "MsgIntf.h"
#include "tjsError.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr std::uintmax_t kMaxCommandBytes = 2u * 1024u * 1024u;
constexpr int kPrettyPrintDepth = 4;

struct ChannelState {
    std::mutex mutex;
    fs::path directory;
    bool active = false;
    bool draining = false;
    bool have_pending_signature = false;
    std::uintmax_t pending_size = 0;
    fs::file_time_type pending_mtime{};
};

ChannelState State;

std::string ToUtf8(const ttstr &value) {
    std::string result;
    tjs_string wide(value.c_str());
    if(!TVPUtf16ToUtf8(result, wide))
        return {};
    return result;
}

bool FromUtf8(const std::string &value, ttstr &result) {
    tjs_string wide;
    if(!TVPUtf8ToUtf16(wide, value)) {
        result.Clear();
        return false;
    }
    result = ttstr(wide.c_str());
    return true;
}

fs::path CommandPath(const fs::path &directory, const char *name) {
    return directory / name;
}

bool ReadCommand(const fs::path &path, std::string &content,
                 std::uintmax_t &size, fs::file_time_type &mtime,
                 bool &too_large) {
    too_large = false;
    std::error_code ec;
    if(!fs::is_regular_file(path, ec))
        return false;
    size = fs::file_size(path, ec);
    if(ec)
        return false;
    mtime = fs::last_write_time(path, ec);
    if(ec)
        return false;
    if(size > kMaxCommandBytes) {
        too_large = true;
        return false;
    }

    std::ifstream stream(path, std::ios::binary);
    if(!stream)
        return false;
    content.resize(static_cast<std::size_t>(size));
    if(size != 0)
        stream.read(content.data(), static_cast<std::streamsize>(size));
    if(!stream && !stream.eof()) {
        content.clear();
        return false;
    }
    // A writer using the documented tmp->rename protocol is complete before
    // cmd becomes visible.  If a host writes cmd in place, a size/mtime change
    // is treated as a new command and the next frame will read it again.
    return static_cast<std::uintmax_t>(content.size()) == size;
}

bool WriteWholeFile(const fs::path &path, const std::string &content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if(!stream)
        return false;
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    stream.flush();
    return static_cast<bool>(stream);
}

bool WriteResponse(const fs::path &directory, const std::string &content) {
    const fs::path response = CommandPath(directory, "resp");
    const fs::path temporary = CommandPath(directory, "resp.tmp");
    if(!WriteWholeFile(temporary, content))
        return false;

    std::error_code ec;
    fs::remove(response, ec);
    ec.clear();
    fs::rename(temporary, response, ec);
    if(!ec)
        return true;

    // Some filesystems do not permit rename-over-existing even after remove
    // (or briefly report a stale directory entry).  The fallback still writes
    // a complete response rather than exposing a partial JSON document.
    ec.clear();
    return WriteWholeFile(response, content);
}

ttstr ErrorMessage(const ttstr &message) {
    return message.IsEmpty() ? ttstr(TJS_W("Unknown TJS error")) : message;
}

bool IsExpression(const ttstr &script) {
    tTJS *engine = TVPGetScriptEngine();
    if(!engine)
        return false;

    // CompileScript is used only as a parser probe.  Aether's stream ABI is
    // RAII based, unlike krkrz's iTJSBinaryStream::Destruct contract.
    class NullStream final : public tTJSBinaryStream {
    public:
        tjs_uint64 Seek(tjs_int64, int) override { return 0; }
        tjs_uint Read(void *, tjs_uint) override { return 0; }
        tjs_uint Write(const void *, tjs_uint size) override { return size; }
        void SetEndOfStorage() override {}
        tjs_uint64 GetSize() override { return 0; }
    } sink;

    try {
        engine->CompileScript(script.c_str(), &sink, true, false, true,
                              TJS_W("repl"), 0);
        return true;
    } catch(...) {
        return false;
    }
}

bool Evaluate(const ttstr &script, tTJSVariant &result, ttstr &error) {
    if(script.IsEmpty()) {
        error = TJS_W("REPL command is empty");
        return false;
    }

    try {
        if(IsExpression(script))
            TVPExecuteExpression(script, TJS_W("repl"), 0, &result);
        else
            TVPExecuteScript(script, TJS_W("repl"), 0, &result);
        return true;
    } catch(eTJSScriptError &exception) {
        error = exception.GetMessage();
    } catch(eTJS &exception) {
        error = exception.GetMessage();
    } catch(...) {
        error = TJS_W("Unknown exception while evaluating REPL command");
    }
    error = ErrorMessage(error);
    return false;
}

std::string MakeResponse(bool ok, const std::string &result,
                         const std::string &error, const char *kind) {
    std::string response;
    response.reserve(result.size() + error.size() + 96);
    response += "{\"protocol\":1,\"ok\":";
    response += ok ? "true" : "false";
    response += ",\"kind\":\"";
    response += TVPRepl::JsonEscape(kind ? kind : "eval");
    response += "\",\"result\":\"";
    response += TVPRepl::JsonEscape(result);
    response += "\",\"error\":\"";
    response += TVPRepl::JsonEscape(error);
    response += "\"}";
    return response;
}

void Respond(const fs::path &directory, bool ok, const ttstr &result,
             const ttstr &error, const char *kind) {
    const std::string result8 = ToUtf8(result);
    const std::string error8 = ToUtf8(error);
    if(!WriteResponse(directory, MakeResponse(ok, result8, error8, kind)))
        TVPAddImportantLog(TJS_W("REPL: failed to write response"));
}

bool ResolveDirectory(fs::path &directory) {
    tTJSVariant value;
    if(!TVPGetCommandLineNoInit(TJS_W("-replfile"), &value))
        return false;
    const ttstr option(value);
    if(TVPRepl::IsDisabledOption(option))
        return false;

    const std::string utf8 = ToUtf8(option);
    if(utf8.empty())
        return false;
    directory = fs::u8path(utf8).lexically_normal();
    return !directory.empty();
}

} // namespace

namespace TVPRepl {

std::string JsonEscape(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size() + 16);
    for(const unsigned char byte : value) {
        switch(byte) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if(byte < 0x20) {
                static constexpr char digits[] = "0123456789abcdef";
                escaped += "\\u00";
                escaped.push_back(digits[(byte >> 4) & 0xf]);
                escaped.push_back(digits[byte & 0xf]);
            } else {
                escaped.push_back(static_cast<char>(byte));
            }
            break;
        }
    }
    return escaped;
}

bool IsDisabledOption(const ttstr &value) {
    ttstr normalized = value;
    normalized.ToLowerCase();
    return normalized.IsEmpty() || normalized == TJS_W("no") ||
           normalized == TJS_W("off") || normalized == TJS_W("false") ||
           normalized == TJS_W("0");
}

} // namespace TVPRepl

bool TVPReplFileChannelActive() {
    std::lock_guard<std::mutex> lock(State.mutex);
    return State.active;
}

void TVPCreateREPL() {
#if defined(__EMSCRIPTEN__) || defined(WEB)
    // Browser hosts expose their own devtools transport.  Keep the symbol so
    // lifecycle code stays portable, but do not touch the host filesystem.
    return;
#else
    fs::path directory;
    if(!ResolveDirectory(directory))
        return;

    std::error_code ec;
    fs::create_directories(directory, ec);
    if(ec) {
        TVPAddImportantLog(TJS_W("REPL: cannot create -replfile directory"));
        return;
    }

    std::lock_guard<std::mutex> lock(State.mutex);
    if(State.active)
        return;
    State.directory = std::move(directory);
    State.active = true;
    State.have_pending_signature = false;
    TVPAddImportantLog(TJS_W("REPL file channel enabled (-replfile)"));
#endif
}

void TVPDestroyREPL() {
    std::lock_guard<std::mutex> lock(State.mutex);
    State.active = false;
    State.draining = false;
    State.have_pending_signature = false;
    State.directory.clear();
}

void TVPDrainREPL() {
#if defined(__EMSCRIPTEN__) || defined(WEB)
    return;
#else
    fs::path directory;
    {
        std::lock_guard<std::mutex> lock(State.mutex);
        if(!State.active || State.draining)
            return;
        State.draining = true;
        directory = State.directory;
    }

    // All evaluation happens on the caller (Aether VM main) thread.  The
    // guard is released on every path, including exceptions from filesystem
    // implementations or host callbacks.
    const auto finish = [&] {
        std::lock_guard<std::mutex> lock(State.mutex);
        State.draining = false;
    };

    // Keep the reentrancy guard correct even if a host filesystem or logging
    // callback unexpectedly throws.  The channel is polled once per frame;
    // leaving `draining` set would permanently disable it for the process.
    struct DrainGuard final {
        decltype(finish) &Finish;
        ~DrainGuard() noexcept {
            try {
                Finish();
            } catch(...) {
                // Destructors must not turn a recoverable host/filesystem
                // failure into std::terminate.  The next frame can retry.
            }
        }
    } guard{finish};

    try {
        std::error_code ec;
        const fs::path response = CommandPath(directory, "resp");
        const fs::path command = CommandPath(directory, "cmd");
        if(fs::exists(response, ec) || !fs::exists(command, ec)) {
            return;
        }

        std::string script8;
        std::uintmax_t size = 0;
        fs::file_time_type mtime{};
        bool too_large = false;
        if(!ReadCommand(command, script8, size, mtime, too_large)) {
            if(too_large) {
                fs::remove(command, ec);
                Respond(directory, false, ttstr(),
                        TJS_W("REPL command exceeds 2 MiB"), "error");
                std::lock_guard<std::mutex> lock(State.mutex);
                State.have_pending_signature = false;
            } else {
                // Wait for a stable in-place writer; documented rename-based
                // writers are already stable on first observation.
                std::lock_guard<std::mutex> lock(State.mutex);
                State.have_pending_signature = false;
            }
            return;
        }

        bool wait_for_stable_command = false;
        {
            std::lock_guard<std::mutex> lock(State.mutex);
            if(State.have_pending_signature && State.pending_size == size &&
               State.pending_mtime == mtime) {
                State.have_pending_signature = false;
            } else {
                State.have_pending_signature = true;
                State.pending_size = size;
                State.pending_mtime = mtime;
                wait_for_stable_command = true;
            }
        }
        if(wait_for_stable_command) {
            return;
        }

        ec.clear();
        if(!fs::remove(command, ec) || ec) {
            Respond(directory, false, ttstr(),
                    TJS_W("REPL command could not be claimed"), "error");
            return;
        }
        ttstr script;
        if(!FromUtf8(script8, script)) {
            Respond(directory, false, ttstr(),
                    TJS_W("REPL command is not valid UTF-8"), "error");
            return;
        }

        if(script == TJS_W(".help") || script == TJS_W(".capabilities")) {
            Respond(directory, true,
                    TJS_W("TJS expression/statement evaluation; protocol=1"),
                    ttstr(), "help");
            return;
        }
        if(script == TJS_W(".ping")) {
            Respond(directory, true, TJS_W("pong"), ttstr(), "ping");
            return;
        }

        tTJSVariant result;
        ttstr error;
        const bool ok = Evaluate(script, result, error);
        const ttstr printed = ok ? TVPPrettyPrint(result, kPrettyPrintDepth,
                                                   false)
                                 : ttstr();
        Respond(directory, ok, printed, error, ok ? "eval" : "error");
    } catch(...) {
        Respond(directory, false, ttstr(),
                TJS_W("REPL channel failed while processing command"),
                "error");
    }
#endif
}
