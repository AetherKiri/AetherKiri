#include "CharacterSet.h"
#include "DebugIntf.h"
#include "StorageIntf.h"
#include "ncbind.hpp"

#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#if defined(_WIN32)
#define popen _popen
#define pclose _pclose
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <vector>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#if defined(__APPLE__)
#include <signal.h>
#include <spawn.h>
#include <TargetConditionals.h>
extern char **environ;
#endif

namespace {

constexpr tjs_int kReadyUninitialized = 0;
constexpr tjs_int kReadyOpen = 1;
constexpr tjs_int kReadySent = 2;
constexpr tjs_int kReadyReceiving = 3;
constexpr tjs_int kReadyLoaded = 4;

std::string toUtf8(const ttstr &text) {
    const tjs_int length = TVPWideCharToUtf8String(text.c_str(), nullptr);
    if(length <= 0)
        return {};
    std::string out(static_cast<size_t>(length), '\0');
    TVPWideCharToUtf8String(text.c_str(), out.data());
    if(!out.empty() && out.back() == '\0')
        out.pop_back();
    return out;
}

ttstr fromUtf8(const char *bytes, size_t length) {
    if(!bytes || length == 0)
        return ttstr();
    const tjs_int wideLen = TVPUtf8ToWideCharString(
        bytes, static_cast<tjs_uint>(length), static_cast<tjs_char *>(nullptr));
    if(wideLen <= 0)
        return ttstr();
    std::vector<tjs_char> wide(static_cast<size_t>(wideLen) + 1, 0);
    TVPUtf8ToWideCharString(bytes, static_cast<tjs_uint>(length), wide.data());
    return ttstr(wide.data());
}

ttstr fromUtf8(const std::string &bytes) {
    return fromUtf8(bytes.data(), bytes.size());
}

ttstr paramString(tjs_int index, tjs_int count, tTJSVariant **params,
                  const tjs_char *fallback = TJS_W("")) {
    if(index < count && params && params[index] &&
       params[index]->Type() != tvtVoid)
        return ttstr(*params[index]);
    return ttstr(fallback);
}

tjs_int paramInt(tjs_int index, tjs_int count, tTJSVariant **params,
                 tjs_int fallback = 0) {
    if(index < count && params && params[index] &&
       params[index]->Type() != tvtVoid)
        return static_cast<tjs_int>(*params[index]);
    return fallback;
}

bool paramBool(tjs_int index, tjs_int count, tTJSVariant **params,
               bool fallback = false) {
    return paramInt(index, count, params, fallback ? 1 : 0) != 0;
}

std::string shellQuote(const std::string &input) {
    std::string out("'");
    for(char c : input) {
        if(c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

std::string composeCommand(const ttstr &target, const ttstr &param = ttstr()) {
    std::string command = toUtf8(target);
    const std::string args = toUtf8(param);
    if(!args.empty()) {
        command += " ";
        command += args;
    }
    return command;
}

void logCompatOnce(const tjs_char *module, const tjs_char *message) {
    static std::map<std::string, bool> emitted;
    const std::string key = toUtf8(ttstr(module) + TJS_W(":") + message);
    if(emitted[key])
        return;
    emitted[key] = true;
    TVPAddLog(ttstr(TJS_W("AetherKiri compat plugin ")) + module + TJS_W(": ") +
              message);
}

void setDict(iTJSDispatch2 *dict, const tjs_char *name,
             const tTJSVariant &value) {
    if(dict)
        dict->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, dict);
}

tTJSVariant makeArray(const std::vector<ttstr> &items) {
    iTJSDispatch2 *array = TJSCreateArrayObject();
    if(!array)
        return tTJSVariant();
    for(tjs_int i = 0; i < static_cast<tjs_int>(items.size()); ++i) {
        tTJSVariant value(items[static_cast<size_t>(i)]);
        array->PropSetByNum(TJS_MEMBERENSURE, i, &value, array);
    }
    tTJSVariant result(array, array);
    array->Release();
    return result;
}

tTJSVariant makeEmptyArray() { return makeArray({}); }

struct CommandResult {
    std::vector<ttstr> lines;
    std::string bytes;
    tjs_int exitCode = -1;
    bool ok = false;
    ttstr message;
};

CommandResult runCommandCapture(const std::string &command) {
    CommandResult result;
    FILE *pipe = popen((command + " 2>&1").c_str(), "r");
    if(!pipe) {
        result.message = fromUtf8(std::strerror(errno));
        return result;
    }

    char buffer[4096];
    std::string pending;
    while(fgets(buffer, sizeof(buffer), pipe)) {
        result.bytes += buffer;
        pending += buffer;
        size_t pos = 0;
        while((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            if(!line.empty() && line.back() == '\r')
                line.pop_back();
            result.lines.push_back(fromUtf8(line));
            pending.erase(0, pos + 1);
        }
    }
    if(!pending.empty())
        result.lines.push_back(fromUtf8(pending));

    const int status = pclose(pipe);
    if(status == -1) {
        result.message = fromUtf8(std::strerror(errno));
        return result;
    }
#if defined(WIFEXITED)
    if(WIFEXITED(status))
        result.exitCode = WEXITSTATUS(status);
    else
        result.exitCode = status;
#else
    result.exitCode = status;
#endif
    result.ok = result.exitCode == 0;
    return result;
}

tTJSVariant commandResultToVariant(const CommandResult &command) {
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(!dict)
        return tTJSVariant();
    setDict(dict, TJS_W("stdout"), makeArray(command.lines));
    setDict(dict, TJS_W("status"),
            tTJSVariant(command.ok ? TJS_W("ok") : TJS_W("failed")));
    setDict(dict, TJS_W("exitcode"), tTJSVariant(command.exitCode));
    if(!command.message.IsEmpty())
        setDict(dict, TJS_W("message"), tTJSVariant(command.message));
    tTJSVariant result(dict, dict);
    dict->Release();
    return result;
}

tjs_error TJS_INTF_METHOD commandExecuteCb(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    const ttstr target = paramString(0, numparams, param);
    const ttstr args = paramString(1, numparams, param);
    if(result)
        *result = commandResultToVariant(runCommandCapture(
            composeCommand(target, args)));
    return TJS_S_OK;
}

bool writeStorageBytes(const ttstr &storage, const std::string &bytes) {
    if(storage.IsEmpty())
        return false;
    try {
        std::unique_ptr<tTJSBinaryStream> stream(
            TVPCreateStream(storage, TJS_BS_WRITE));
        if(!stream)
            return false;
        if(!bytes.empty())
            stream->WriteBuffer(bytes.data(),
                                static_cast<tjs_uint>(bytes.size()));
        return true;
    } catch(...) {
        return false;
    }
}

bool readStorageBytes(const ttstr &storage, std::string &bytes) {
    try {
        std::unique_ptr<tTJSBinaryStream> stream(
            TVPCreateStream(storage, TJS_BS_READ));
        if(!stream)
            return false;
        const tjs_uint64 size64 = stream->GetSize();
        if(size64 > static_cast<tjs_uint64>(static_cast<size_t>(-1)))
            return false;
        bytes.assign(static_cast<size_t>(size64), '\0');
        if(!bytes.empty())
            stream->ReadBuffer(bytes.data(), static_cast<tjs_uint>(bytes.size()));
        return true;
    } catch(...) {
        return false;
    }
}

struct FetchResult {
    tjs_int status = 0;
    ttstr statusText;
    ttstr headers;
    std::string body;
    bool ok = false;
};

bool startsWithAscii(const std::string &text, const char *prefix) {
    const size_t len = std::strlen(prefix);
    return text.size() >= len && text.compare(0, len, prefix) == 0;
}

FetchResult fetchUrlOrStorage(const ttstr &url) {
    FetchResult result;
    const std::string utf8Url = toUtf8(url);
    if(startsWithAscii(utf8Url, "http://") ||
       startsWithAscii(utf8Url, "https://")) {
        const CommandResult curl =
            runCommandCapture("curl -L -s -i " + shellQuote(utf8Url));
        result.body = curl.bytes;
        result.status = curl.ok ? 200 : 0;
        result.statusText = curl.ok ? TJS_W("OK") : TJS_W("curl failed");
        const size_t split = result.body.rfind("\r\n\r\n");
        const size_t split2 = result.body.rfind("\n\n");
        const size_t headerEnd =
            split != std::string::npos ? split + 4
                                       : (split2 != std::string::npos ? split2 + 2
                                                                      : std::string::npos);
        if(headerEnd != std::string::npos) {
            const std::string headerBytes = result.body.substr(0, headerEnd);
            result.headers = fromUtf8(headerBytes);
            result.body.erase(0, headerEnd);
            std::istringstream firstLine(headerBytes);
            std::string http;
            firstLine >> http >> result.status;
            std::string statusText;
            std::getline(firstLine, statusText);
            if(!statusText.empty() && statusText.front() == ' ')
                statusText.erase(0, 1);
            if(!statusText.empty())
                result.statusText = fromUtf8(statusText);
        }
        result.ok = curl.ok;
        return result;
    }

    ttstr storage = url;
    if(startsWithAscii(utf8Url, "file://"))
        storage = fromUtf8(utf8Url.substr(7));
    if(readStorageBytes(storage, result.body)) {
        result.status = 200;
        result.statusText = TJS_W("OK");
        result.ok = true;
    } else {
        result.statusText = TJS_W("not found");
    }
    return result;
}

tjs_error invokeMethodIfPresent(iTJSDispatch2 *target, const tjs_char *name,
                                tjs_int count, tTJSVariant **params) {
    if(!target)
        return TJS_E_FAIL;
    tTJSVariant method;
    if(TJS_FAILED(target->PropGet(TJS_IGNOREPROP, name, nullptr, &method,
                                  target)) ||
       method.Type() != tvtObject)
        return TJS_E_MEMBERNOTFOUND;
    return method.AsObjectClosureNoAddRef().FuncCall(
        0, nullptr, nullptr, nullptr, count, params, target);
}

void openExternal(const ttstr &target, const ttstr &args = ttstr()) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
    (void)target;
    (void)args;
    logCompatOnce(TJS_W("process.dll"),
                  TJS_W("external process launch is unavailable on iOS"));
#elif defined(__APPLE__)
    std::string command = "open " + shellQuote(toUtf8(target));
    if(!args.IsEmpty())
        command += " --args " + toUtf8(args);
    std::system(command.c_str());
#else
    std::system(composeCommand(target, args).c_str());
#endif
}

tjs_int spawnShellCommand(const std::string &command) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
    (void)command;
    logCompatOnce(TJS_W("process.dll"),
                  TJS_W("shell command launch is unavailable on iOS"));
#elif defined(__APPLE__)
    pid_t pid = -1;
    const char *argv[] = {"/bin/sh", "-c", command.c_str(), nullptr};
    if(posix_spawn(&pid, "/bin/sh", nullptr, nullptr,
                   const_cast<char *const *>(argv), environ) == 0)
        return static_cast<tjs_int>(pid);
#else
    std::system((command + " &").c_str());
#endif
    return 0;
}

} // namespace

// -------------------------------------------------------------------------
// process.dll
// AETHERKIRI_COMPAT_STUB: POSIX process bridge, not Win32 message-window API.
// -------------------------------------------------------------------------

#define NCB_MODULE_NAME TJS_W("process.dll")

class Process {
public:
    Process() = default;

    static tjs_error TJS_INTF_METHOD factory(Process **result, tjs_int,
                                             tTJSVariant **, iTJSDispatch2 *) {
        *result = new Process();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD executeCb(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               Process *self) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        const std::string command =
            composeCommand(paramString(0, numparams, param),
                           paramString(1, numparams, param));
        const tjs_int pid = spawnShellCommand(command);
        if(self) {
            self->pid_ = pid;
            self->status_ = pid != 0 ? 1 : -1;
        }
        if(result)
            *result = pid != 0;
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD commandExecuteCb(tTJSVariant *result,
                                                      tjs_int numparams,
                                                      tTJSVariant **param,
                                                      Process *) {
        return ::commandExecuteCb(result, numparams, param, nullptr);
    }

    bool terminate(tjs_int endCode = 0) {
#if defined(__APPLE__)
        if(pid_ > 0)
            return kill(static_cast<pid_t>(pid_), SIGTERM) == 0;
#endif
        (void)endCode;
        return false;
    }

    bool sendSignal(bool isBreak = false) {
#if defined(__APPLE__)
        if(pid_ > 0)
            return kill(static_cast<pid_t>(pid_), isBreak ? SIGINT : SIGTERM) ==
                   0;
#endif
        return false;
    }

    tjs_int getStatus() const { return status_; }

private:
    tjs_int pid_ = 0;
    tjs_int status_ = 0;
};

NCB_REGISTER_CLASS(Process) {
    Factory(&Process::factory);
    NCB_METHOD_RAW_CALLBACK(execute, &Process::executeCb, 0);
    NCB_METHOD_RAW_CALLBACK(commandExecute, &Process::commandExecuteCb, 0);
    NCB_METHOD(terminate);
    NCB_METHOD(sendSignal);
    NCB_PROPERTY_RO(status, getStatus);
}

NCB_ATTACH_FUNCTION_WITHTAG(commandExecute, ProcessCompatCommand, System,
                            commandExecuteCb);

// -------------------------------------------------------------------------
// shellExecute.dll
// AETHERKIRI_COMPAT_STUB: maps ShellExecute to macOS open/POSIX commands.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("shellExecute.dll")

class WindowShellCompat {
public:
    static tjs_error TJS_INTF_METHOD shellExecute(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  WindowShellCompat *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        openExternal(paramString(0, numparams, param),
                     paramString(1, numparams, param));
        if(result)
            *result = static_cast<tjs_int>(1);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD commandExecute(tTJSVariant *result,
                                                    tjs_int numparams,
                                                    tTJSVariant **param,
                                                    WindowShellCompat *) {
        return ::commandExecuteCb(result, numparams, param, nullptr);
    }

    bool terminateProcess(tjs_int process, tjs_int = 0) {
#if defined(__APPLE__)
        return process > 0 && kill(static_cast<pid_t>(process), SIGTERM) == 0;
#else
        return false;
#endif
    }

    bool commandSendSignal(tjs_int process, bool isBreak = false) {
#if defined(__APPLE__)
        return process > 0 &&
               kill(static_cast<pid_t>(process), isBreak ? SIGINT : SIGTERM) ==
                   0;
#else
        return false;
#endif
    }
};

NCB_ATTACH_CLASS(WindowShellCompat, Window) {
    NCB_METHOD_RAW_CALLBACK(shellExecute, &WindowShellCompat::shellExecute, 0);
    NCB_METHOD_RAW_CALLBACK(commandExecute, &WindowShellCompat::commandExecute,
                            0);
    NCB_METHOD(terminateProcess);
    NCB_METHOD(commandSendSignal);
}

NCB_ATTACH_FUNCTION_WITHTAG(commandExecute, ShellCompatCommand, System,
                            commandExecuteCb);

// -------------------------------------------------------------------------
// systemEx.dll and registory.dll
// AETHERKIRI_COMPAT_STUB: real env/url helpers plus non-Windows registry sink.
// -------------------------------------------------------------------------

namespace {
std::map<std::string, tTJSVariant> g_registryCompat;

tjs_error TJS_INTF_METHOD writeRegValueCb(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *) {
    if(numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    g_registryCompat[toUtf8(ttstr(*param[0]))] = *param[1];
    logCompatOnce(TJS_W("registory.dll"),
                  TJS_W("registry writes are stored in-memory on this platform"));
    if(result)
        *result = true;
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD deleteRegValueCb(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    g_registryCompat.erase(toUtf8(ttstr(*param[0])));
    if(result)
        *result = true;
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD deleteRegKeyCb(tTJSVariant *result,
                                         tjs_int numparams,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    const std::string prefix = toUtf8(ttstr(*param[0]));
    for(auto it = g_registryCompat.begin(); it != g_registryCompat.end();) {
        if(it->first.rfind(prefix, 0) == 0)
            it = g_registryCompat.erase(it);
        else
            ++it;
    }
    if(result)
        *result = true;
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD readEnvValueCb(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param, iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    if(result) {
        const char *value = std::getenv(toUtf8(ttstr(*param[0])).c_str());
        if(value)
            *result = fromUtf8(value, std::strlen(value));
        else
            result->Clear();
    }
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD writeEnvValueCb(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *) {
    if(numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    const std::string name = toUtf8(ttstr(*param[0]));
    const std::string value = toUtf8(ttstr(*param[1]));
#if defined(_WIN32)
    const int rc = _putenv_s(name.c_str(), value.c_str());
#else
    const int rc = setenv(name.c_str(), value.c_str(), 1);
#endif
    if(result)
        *result = rc == 0;
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD expandEnvStringCb(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    std::string text = toUtf8(ttstr(*param[0]));
    std::string out;
    for(size_t i = 0; i < text.size();) {
        if(text[i] == '%' && text.find('%', i + 1) != std::string::npos) {
            const size_t end = text.find('%', i + 1);
            const std::string key = text.substr(i + 1, end - i - 1);
            const char *value = std::getenv(key.c_str());
            if(value)
                out += value;
            i = end + 1;
        } else if(text[i] == '$') {
            size_t end = i + 1;
            while(end < text.size() &&
                  (std::isalnum(static_cast<unsigned char>(text[end])) ||
                   text[end] == '_'))
                ++end;
            const std::string key = text.substr(i + 1, end - i - 1);
            const char *value = std::getenv(key.c_str());
            if(value)
                out += value;
            i = end;
        } else {
            out += text[i++];
        }
    }
    if(result)
        *result = fromUtf8(out);
    return TJS_S_OK;
}

bool isUrlSafe(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

tjs_error TJS_INTF_METHOD urlencodeCb(tTJSVariant *result, tjs_int numparams,
                                      tTJSVariant **param, iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    const std::string bytes = toUtf8(ttstr(*param[0]));
    std::ostringstream out;
    const char *hex = "0123456789ABCDEF";
    for(unsigned char c : bytes) {
        if(isUrlSafe(c))
            out << static_cast<char>(c);
        else {
            out << '%';
            out << hex[(c >> 4) & 0xf] << hex[c & 0xf];
        }
    }
    if(result)
        *result = fromUtf8(out.str());
    return TJS_S_OK;
}

int fromHex(char c) {
    if(c >= '0' && c <= '9')
        return c - '0';
    if(c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if(c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

tjs_error TJS_INTF_METHOD urldecodeCb(tTJSVariant *result, tjs_int numparams,
                                      tTJSVariant **param, iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    const std::string text = toUtf8(ttstr(*param[0]));
    std::string out;
    for(size_t i = 0; i < text.size(); ++i) {
        if(text[i] == '%' && i + 2 < text.size()) {
            const int hi = fromHex(text[i + 1]);
            const int lo = fromHex(text[i + 2]);
            if(hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i] == '+' ? ' ' : text[i]);
    }
    if(result)
        *result = fromUtf8(out);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD confirmCb(tTJSVariant *result, tjs_int,
                                    tTJSVariant **, iTJSDispatch2 *) {
    logCompatOnce(TJS_W("systemEx.dll"),
                  TJS_W("System.confirm returns true in headless/native compat mode"));
    if(result)
        *result = true;
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD waitForAppLockCb(tTJSVariant *result, tjs_int,
                                           tTJSVariant **, iTJSDispatch2 *) {
    if(result)
        *result = true;
    return TJS_S_OK;
}
} // namespace

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("systemEx.dll")
NCB_ATTACH_FUNCTION(writeRegValue, System, writeRegValueCb);
NCB_ATTACH_FUNCTION(readEnvValue, System, readEnvValueCb);
NCB_ATTACH_FUNCTION(writeEnvValue, System, writeEnvValueCb);
NCB_ATTACH_FUNCTION(expandEnvString, System, expandEnvStringCb);
NCB_ATTACH_FUNCTION(urlencode, System, urlencodeCb);
NCB_ATTACH_FUNCTION(urldecode, System, urldecodeCb);
NCB_ATTACH_FUNCTION(confirm, System, confirmCb);
NCB_ATTACH_FUNCTION(waitForAppLock, System, waitForAppLockCb);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("registory.dll")
NCB_ATTACH_FUNCTION_WITHTAG(writeRegValue, RegistryCompatWrite, System,
                            writeRegValueCb);
NCB_ATTACH_FUNCTION_WITHTAG(deleteRegValue, RegistryCompatDeleteValue, System,
                            deleteRegValueCb);
NCB_ATTACH_FUNCTION_WITHTAG(writeDeleteValue, RegistryCompatWriteDeleteValue,
                            System, deleteRegValueCb);
NCB_ATTACH_FUNCTION_WITHTAG(deleteRegKey, RegistryCompatDeleteKey, System,
                            deleteRegKeyCb);

// -------------------------------------------------------------------------
// stdio.dll
// AETHERKIRI_COMPAT_STUB: maps console APIs to host stdio/log streams.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("stdio.dll")

class Stdio {
public:
    bool attachConsole(tjs_int = 0) { return true; }
    bool allocConsole(tjs_int = 0) { return true; }
    bool freeConsole() { return true; }
    ttstr stdinRead(bool = false) { return ttstr(); }
    void stdoutWrite(const tjs_char *text, bool = false) {
        if(text)
            std::fputs(toUtf8(ttstr(text)).c_str(), stdout);
    }
    void stderrWrite(const tjs_char *text, bool = false) {
        if(text)
            std::fputs(toUtf8(ttstr(text)).c_str(), stderr);
    }
    void flush() {
        std::fflush(stdout);
        std::fflush(stderr);
    }
};

NCB_ATTACH_CLASS(Stdio, System) {
    NCB_METHOD(attachConsole);
    NCB_METHOD(allocConsole);
    NCB_METHOD(freeConsole);
    NCB_METHOD_DIFFER(stdin, stdinRead);
    NCB_METHOD_DIFFER(stdout, stdoutWrite);
    NCB_METHOD_DIFFER(stderr, stderrWrite);
    NCB_METHOD(flush);
}

// -------------------------------------------------------------------------
// htmlhelp.dll
// AETHERKIRI_COMPAT_STUB: opens help URLs with the host shell.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("htmlhelp.dll")

class HtmlHelpCompat {
public:
    HtmlHelpCompat() = default;
    void displayTopic(const tjs_char *url) {
        if(url)
            openExternal(ttstr(url));
    }
};

NCB_REGISTER_CLASS_DIFFER(HtmlHelp, HtmlHelpCompat) {
    Constructor();
    NCB_METHOD(displayTopic);
}

// -------------------------------------------------------------------------
// adjustMonitor.dll and fpslimit.dll
// AETHERKIRI_COMPAT_STUB: single-display/window-loop compatible surfaces.
// -------------------------------------------------------------------------

namespace {
tjs_int g_fpsLimit = 1000;

tjs_error TJS_INTF_METHOD adjustMoniCb(tTJSVariant *result, tjs_int numparams,
                                       tTJSVariant **param, iTJSDispatch2 *) {
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(!dict)
        return TJS_E_FAIL;
    tjs_int x = 0;
    tjs_int y = 0;
    if(numparams > 0 && param && param[0] && param[0]->Type() == tvtObject) {
        iTJSDispatch2 *src = param[0]->AsObjectNoAddRef();
        tTJSVariant value;
        if(TJS_SUCCEEDED(src->PropGet(TJS_IGNOREPROP, TJS_W("left2"),
                                      nullptr, &value, src)))
            x = static_cast<tjs_int>(value);
        else if(TJS_SUCCEEDED(src->PropGet(TJS_IGNOREPROP, TJS_W("left"),
                                           nullptr, &value, src)))
            x = static_cast<tjs_int>(value);
        if(TJS_SUCCEEDED(src->PropGet(TJS_IGNOREPROP, TJS_W("top2"), nullptr,
                                      &value, src)))
            y = static_cast<tjs_int>(value);
        else if(TJS_SUCCEEDED(src->PropGet(TJS_IGNOREPROP, TJS_W("top"),
                                           nullptr, &value, src)))
            y = static_cast<tjs_int>(value);
    }
    setDict(dict, TJS_W("x"), tTJSVariant(x));
    setDict(dict, TJS_W("y"), tTJSVariant(y));
    setDict(dict, TJS_W("left"), tTJSVariant(0));
    setDict(dict, TJS_W("top"), tTJSVariant(0));
    setDict(dict, TJS_W("right"), tTJSVariant(0));
    setDict(dict, TJS_W("bottom"), tTJSVariant(0));
    if(result)
        *result = tTJSVariant(dict, dict);
    dict->Release();
    return TJS_S_OK;
}
} // namespace

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("adjustMonitor.dll")
NCB_REGISTER_FUNCTION(AdjustMoni, adjustMoniCb);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("fpslimit.dll")

class SystemFpsLimitCompat {
public:
    tjs_int getFpsLimit() const { return g_fpsLimit; }
    void setFpsLimit(tjs_int value) { g_fpsLimit = value > 0 ? value : 1000; }
};

NCB_ATTACH_CLASS(SystemFpsLimitCompat, System) {
    NCB_PROPERTY(fpslimit, getFpsLimit, setFpsLimit);
}

// -------------------------------------------------------------------------
// httprequest.dll and xmlhttprequest.dll
// AETHERKIRI_COMPAT_STUB: synchronous Storage/curl-backed request surface.
// -------------------------------------------------------------------------

class SimpleRequestState {
public:
    void openRequest(const ttstr &method, const ttstr &url, bool async = false) {
        method_ = method;
        url_ = url;
        async_ = async;
        readyState_ = kReadyOpen;
        status_ = 0;
        statusText_.Clear();
        headers_.Clear();
        responseBytes_.clear();
    }

    void setHeader(const ttstr &name, const ttstr &value) {
        requestHeaders_[toUtf8(name)] = value;
    }

    void sendRequest(const ttstr &storeStorage = ttstr()) {
        readyState_ = kReadySent;
        FetchResult fetched = fetchUrlOrStorage(url_);
        readyState_ = kReadyReceiving;
        status_ = fetched.status;
        statusText_ = fetched.statusText;
        headers_ = fetched.headers;
        responseBytes_ = fetched.body;
        if(!storeStorage.IsEmpty())
            writeStorageBytes(storeStorage, responseBytes_);
        readyState_ = kReadyLoaded;
    }

    void abort() {
        readyState_ = kReadyUninitialized;
        status_ = 0;
        responseBytes_.clear();
    }

    tjs_int getReadyState() const { return readyState_; }
    tjs_int getStatus() const { return status_; }
    ttstr getStatusText() const { return statusText_; }
    ttstr getResponseText() const { return fromUtf8(responseBytes_); }
    tTJSVariant getResponseData() const {
        if(responseBytes_.empty())
            return tTJSVariant();
        return tTJSVariant(
            reinterpret_cast<const tjs_uint8 *>(responseBytes_.data()),
            static_cast<tjs_uint>(responseBytes_.size()));
    }
    ttstr getAllResponseHeaders() const { return headers_; }
    ttstr getResponseHeader(const tjs_char *name) const {
        if(!name)
            return ttstr();
        const std::string needle = toUtf8(ttstr(name));
        std::istringstream in(toUtf8(headers_));
        std::string line;
        while(std::getline(in, line)) {
            const size_t colon = line.find(':');
            if(colon == std::string::npos)
                continue;
            std::string key = line.substr(0, colon);
            for(char &c : key)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::string want = needle;
            for(char &c : want)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if(key == want) {
                std::string value = line.substr(colon + 1);
                while(!value.empty() &&
                      (value.front() == ' ' || value.front() == '\t'))
                    value.erase(value.begin());
                return fromUtf8(value);
            }
        }
        return ttstr();
    }
    ttstr getContentType() const { return getResponseHeader(TJS_W("Content-Type")); }
    ttstr getContentTypeEncoding() const { return ttstr(); }
    tjs_int getContentLength() const {
        return static_cast<tjs_int>(responseBytes_.size());
    }
    bool getAsync() const { return async_; }

private:
    ttstr method_;
    ttstr url_;
    bool async_ = false;
    tjs_int readyState_ = kReadyUninitialized;
    tjs_int status_ = 0;
    ttstr statusText_;
    ttstr headers_;
    std::string responseBytes_;
    std::map<std::string, ttstr> requestHeaders_;
};

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("httprequest.dll")

class HttpRequestCompat : public SimpleRequestState {
public:
    static tjs_error TJS_INTF_METHOD factory(HttpRequestCompat **result,
                                             tjs_int, tTJSVariant **,
                                             iTJSDispatch2 *) {
        *result = new HttpRequestCompat();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD openCb(tTJSVariant *, tjs_int numparams,
                                            tTJSVariant **param,
                                            HttpRequestCompat *self) {
        if(!self || numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        self->openRequest(paramString(0, numparams, param),
                          paramString(1, numparams, param), false);
        return TJS_S_OK;
    }

    void setRequestHeader(const tjs_char *name, const tjs_char *value) {
        setHeader(name ? ttstr(name) : ttstr(), value ? ttstr(value) : ttstr());
    }

    static tjs_error TJS_INTF_METHOD sendCb(tTJSVariant *, tjs_int numparams,
                                            tTJSVariant **param,
                                            HttpRequestCompat *self) {
        if(!self)
            return TJS_E_FAIL;
        self->sendRequest(paramString(1, numparams, param));
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD sendStorageCb(tTJSVariant *,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   HttpRequestCompat *self) {
        if(!self)
            return TJS_E_FAIL;
        self->sendRequest(paramString(1, numparams, param));
        return TJS_S_OK;
    }

    void abort() { SimpleRequestState::abort(); }
    ttstr getAllResponseHeaders() const {
        return SimpleRequestState::getAllResponseHeaders();
    }
    ttstr getResponseHeader(const tjs_char *name) const {
        return SimpleRequestState::getResponseHeader(name);
    }
    ttstr getResponseText(const tjs_char * = nullptr) const {
        return SimpleRequestState::getResponseText();
    }
    tTJSVariant getResponse() const { return getResponseData(); }
};

NCB_REGISTER_CLASS_DIFFER(HttpRequest, HttpRequestCompat) {
    Factory(&HttpRequestCompat::factory);
    Variant(TJS_W("UNINITIALIZED"), kReadyUninitialized);
    Variant(TJS_W("OPEN"), kReadyOpen);
    Variant(TJS_W("SENT"), kReadySent);
    Variant(TJS_W("RECEIVING"), kReadyReceiving);
    Variant(TJS_W("LOADED"), kReadyLoaded);
    NCB_METHOD_RAW_CALLBACK(open, &HttpRequestCompat::openCb, 0);
    NCB_METHOD(setRequestHeader);
    NCB_METHOD_RAW_CALLBACK(send, &HttpRequestCompat::sendCb, 0);
    NCB_METHOD_RAW_CALLBACK(sendSync, &HttpRequestCompat::sendCb, 0);
    NCB_METHOD_RAW_CALLBACK(sendStorage, &HttpRequestCompat::sendStorageCb, 0);
    NCB_METHOD_RAW_CALLBACK(sendStorageSync, &HttpRequestCompat::sendStorageCb,
                            0);
    NCB_METHOD(abort);
    NCB_METHOD(getAllResponseHeaders);
    NCB_METHOD(getResponseHeader);
    NCB_METHOD(getResponseText);
    NCB_PROPERTY_RO(readyState, getReadyState);
    NCB_PROPERTY_RO(response, getResponse);
    NCB_PROPERTY_RO(responseData, getResponseData);
    NCB_PROPERTY_RO(status, getStatus);
    NCB_PROPERTY_RO(statusText, getStatusText);
    NCB_PROPERTY_RO(contentType, getContentType);
    NCB_PROPERTY_RO(contentTypeEncoding, getContentTypeEncoding);
    NCB_PROPERTY_RO(contentLength, getContentLength);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("xmlhttprequest.dll")

class XMLHttpRequestCompat : public SimpleRequestState {
public:
    XMLHttpRequestCompat() = default;

    static tjs_error TJS_INTF_METHOD factory(XMLHttpRequestCompat **result,
                                             tjs_int, tTJSVariant **,
                                             iTJSDispatch2 *) {
        *result = new XMLHttpRequestCompat();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD openCb(tTJSVariant *, tjs_int numparams,
                                            tTJSVariant **param,
                                            XMLHttpRequestCompat *self) {
        if(!self || numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        self->openRequest(paramString(0, numparams, param),
                          paramString(1, numparams, param),
                          paramBool(2, numparams, param, true));
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD sendCb(tTJSVariant *, tjs_int,
                                            tTJSVariant **,
                                            XMLHttpRequestCompat *self) {
        if(!self)
            return TJS_E_FAIL;
        self->sendRequest();
        return TJS_S_OK;
    }

    void setRequestHeader(const tjs_char *name, const tjs_char *value) {
        setHeader(name ? ttstr(name) : ttstr(), value ? ttstr(value) : ttstr());
    }
    ttstr printRequestHeaders() const { return ttstr(); }
    ttstr getResponseHeader(const tjs_char *name) const {
        return SimpleRequestState::getResponseHeader(name);
    }
    void abort() { SimpleRequestState::abort(); }
    void executeCallback() {}
    ttstr getResponseText() const { return SimpleRequestState::getResponseText(); }
};

NCB_REGISTER_CLASS_DIFFER(XMLHttpRequest, XMLHttpRequestCompat) {
    Factory(&XMLHttpRequestCompat::factory);
    NCB_METHOD_RAW_CALLBACK(open, &XMLHttpRequestCompat::openCb, 0);
    NCB_METHOD_RAW_CALLBACK(send, &XMLHttpRequestCompat::sendCb, 0);
    NCB_METHOD(setRequestHeader);
    NCB_METHOD(printRequestHeaders);
    NCB_METHOD(getResponseHeader);
    NCB_METHOD(abort);
    NCB_METHOD(executeCallback);
    NCB_PROPERTY_RO(readyState, getReadyState);
    NCB_PROPERTY_RO(responseText, getResponseText);
    NCB_PROPERTY_RO(status, getStatus);
    NCB_PROPERTY_RO(statusText, getStatusText);
}

// -------------------------------------------------------------------------
// javascript.dll and squirrel.dll
// AETHERKIRI_COMPAT_STUB: public API surface without embedding extra VMs.
// -------------------------------------------------------------------------

namespace {
tjs_error TJS_INTF_METHOD unsupportedScriptCb(tTJSVariant *result, tjs_int,
                                             tTJSVariant **, iTJSDispatch2 *) {
    logCompatOnce(TJS_W("script-vm"),
                  TJS_W("external script VM is not embedded in AetherKiri"));
    if(result)
        result->Clear();
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD clearTrueCb(tTJSVariant *result, tjs_int,
                                      tTJSVariant **, iTJSDispatch2 *) {
    if(result)
        *result = true;
    return TJS_S_OK;
}
} // namespace

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("javascript.dll")
NCB_ATTACH_FUNCTION(execJS, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(execStorageJS, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(enableDebugJS, Scripts, clearTrueCb);
NCB_ATTACH_FUNCTION(processDebugJS, Scripts, clearTrueCb);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("squirrel.dll")
NCB_ATTACH_FUNCTION(loadSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(execSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(loadStorageSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(execStorageSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(callSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(compileSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(compileStorageSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(saveSQ, Scripts, clearTrueCb);
NCB_ATTACH_FUNCTION(toSQString, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(registerSQ, Scripts, clearTrueCb);
NCB_ATTACH_FUNCTION(unregisterSQ, Scripts, clearTrueCb);
NCB_ATTACH_FUNCTION(forkSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(forkStorageSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(driveSQ, Scripts, clearTrueCb);
NCB_ATTACH_FUNCTION(triggerSQ, Scripts, clearTrueCb);
NCB_ATTACH_FUNCTION(compareSQ, Scripts, unsupportedScriptCb);

class SQFunction {
public:
    SQFunction() = default;
    tTJSVariant call() { return tTJSVariant(); }
};

class SQContinuous {
public:
    SQContinuous() = default;
    void start() { running_ = true; }
    void stop() { running_ = false; }
    bool getRunning() const { return running_; }

private:
    bool running_ = false;
};

NCB_REGISTER_CLASS(SQFunction) {
    Constructor();
    NCB_METHOD(call);
}

NCB_REGISTER_CLASS(SQContinuous) {
    Constructor();
    NCB_METHOD(start);
    NCB_METHOD(stop);
    NCB_PROPERTY_RO(running, getRunning);
}

// -------------------------------------------------------------------------
// messenger.dll, msgreceiver.dll, tasktray.dll, sigcheck.dll
// AETHERKIRI_COMPAT_STUB: method surfaces, no native Win32 message pump.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("messenger.dll")

class WindowMessengerCompat {
public:
    tjs_int registerUserMessageReceiver(tjs_int, tjs_int, tTJSVariant,
                                        tTJSVariant) {
        return 1;
    }
    bool sendUserMessage(tjs_int, tjs_int = 0, tjs_int = 0) { return true; }
    bool sendMessage(const tjs_char *, const tjs_char *) { return true; }
};

NCB_ATTACH_CLASS(WindowMessengerCompat, Window) {
    NCB_METHOD(registerUserMessageReceiver);
    NCB_METHOD(sendUserMessage);
    NCB_METHOD(sendMessage);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("msgreceiver.dll")

NCB_ATTACH_FUNCTION_WITHTAG(startMessageReceiver, MsgReceiverStart, Window,
                            clearTrueCb);
NCB_ATTACH_FUNCTION_WITHTAG(stopMessageReceiver, MsgReceiverStop, Window,
                            clearTrueCb);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("tasktray.dll")

class WindowTasktrayCompat {
public:
    bool showTasktrayIcon(const tjs_char * = nullptr) { return true; }
    bool hideTasktrayIcon() { return true; }
    bool setTasktrayIcon(const tjs_char * = nullptr) { return true; }
    bool popupTasktrayInfo(const tjs_char *, const tjs_char *, const tjs_char *,
                           tjs_int = 0) {
        return true;
    }
};

NCB_ATTACH_CLASS(WindowTasktrayCompat, Window) {
    NCB_METHOD(showTasktrayIcon);
    NCB_METHOD(hideTasktrayIcon);
    NCB_METHOD(setTasktrayIcon);
    NCB_METHOD(popupTasktrayInfo);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("sigcheck.dll")

class WindowSigCheckCompat {
public:
    tjs_int checkSignature(const tjs_char *, const tjs_char *,
                           tTJSVariant info = tTJSVariant()) {
        const tjs_int handler = ++lastHandler_;
        tTJSVariant h(handler), percent(100), ok(1), message{ ttstr() };
        tTJSVariant *progressParams[] = {&h, &info, &percent};
        tTJSVariant *doneParams[] = {&h, &info, &ok, &message};
        invokeMethodIfPresent(owner_, TJS_W("onCheckSignatureProgress"), 3,
                              progressParams);
        invokeMethodIfPresent(owner_, TJS_W("onCheckSignatureDone"), 4,
                              doneParams);
        return handler;
    }
    bool cancelCheckSignature(tjs_int) { return true; }
    bool stopCheckSignature(tjs_int) { return true; }
    void setOwner(iTJSDispatch2 *owner) { owner_ = owner; }

private:
    iTJSDispatch2 *owner_ = nullptr;
    tjs_int lastHandler_ = 0;
};

NCB_GET_INSTANCE_HOOK(WindowSigCheckCompat) {
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj) {
            obj = new ClassT();
            obj->setOwner(objthis);
            SetNativeInstance(objthis, obj);
        }
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(WindowSigCheckCompat, Window) {
    NCB_METHOD(checkSignature);
    NCB_METHOD(cancelCheckSignature);
    NCB_METHOD(stopCheckSignature);
}

// -------------------------------------------------------------------------
// oleclass.dll and win32ole.dll
// AETHERKIRI_COMPAT_STUB: COM/ActiveX is not available on macOS.
// -------------------------------------------------------------------------

class WIN32OLECompat {
public:
    WIN32OLECompat() = default;
    explicit WIN32OLECompat(const tjs_char *) {
        logCompatOnce(TJS_W("win32ole.dll"),
                      TJS_W("COM automation is unavailable on this platform"));
    }
    tTJSVariant invoke(const tjs_char *) { return tTJSVariant(); }
    void set(const tjs_char *, tTJSVariant) {}
    tTJSVariant get(const tjs_char *) { return tTJSVariant(); }
    tTJSVariant getConstant(tTJSVariant = tTJSVariant()) { return tTJSVariant(); }
    bool addEvent(const tjs_char *, tTJSVariant) { return false; }
};

class ActiveXCompat : public WIN32OLECompat {
public:
    ActiveXCompat() = default;
    void setPos(tjs_int, tjs_int) {}
    void setSize(tjs_int, tjs_int) {}
    void setExternalUI() {}
};

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("win32ole.dll")

NCB_REGISTER_CLASS_DIFFER(WIN32OLE, WIN32OLECompat) {
    Constructor();
    NCB_CONSTRUCTOR((const tjs_char *));
    NCB_METHOD(invoke);
    NCB_METHOD(set);
    NCB_METHOD(get);
    NCB_METHOD(getConstant);
    NCB_METHOD(addEvent);
}

NCB_REGISTER_CLASS_DIFFER(ActiveX, ActiveXCompat) {
    Constructor();
    NCB_METHOD(setPos);
    NCB_METHOD(setSize);
    NCB_METHOD(setExternalUI);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("oleclass.dll")

namespace {
tjs_error TJS_INTF_METHOD createOleClassCb(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *) {
    logCompatOnce(TJS_W("oleclass.dll"),
                  TJS_W("COM automation is unavailable on this platform"));
    if(result) {
        ttstr expr = TJS_W("new WIN32OLE(");
        if(numparams > 0)
            expr += TJS_W("\"") + paramString(0, numparams, param) + TJS_W("\"");
        expr += TJS_W(")");
        try {
            TVPExecuteExpression(expr, result);
        } catch(...) {
            result->Clear();
        }
    }
    return TJS_S_OK;
}
} // namespace

static void loadWin32OleCompat() {
    try {
        ncbAutoRegister::LoadModule(TJS_W("win32ole.dll"));
    } catch(...) {
    }
}
NCB_PRE_REGIST_CALLBACK(loadWin32OleCompat);
NCB_ATTACH_FUNCTION(createOleClass, Scripts, createOleClassCb);
NCB_ATTACH_FUNCTION(createActiveXClass, Scripts, createOleClassCb);

// -------------------------------------------------------------------------
// resourceRW.dll
// AETHERKIRI_COMPAT_STUB: PE resources are not available; storage-like shell.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("resourceRW.dll")

class ResourceReader {
public:
    static tjs_error TJS_INTF_METHOD factory(ResourceReader **result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *) {
        auto *reader = new ResourceReader();
        if(numparams > 0)
            reader->open(param[0]->GetString());
        *result = reader;
        return TJS_S_OK;
    }
    void open(const tjs_char *file) { file_ = file ? file : TJS_W(""); }
    void close() { file_.Clear(); }
    tjs_int setLang(tjs_int primary, tjs_int sub) {
        lang_ = (sub << 10) | primary;
        return lang_;
    }
    bool isExistentResource(tTJSVariant, tTJSVariant) { return false; }
    ttstr readToText(tTJSVariant, tTJSVariant, bool = false) { return ttstr(); }
    tTJSVariant readToOctet(tTJSVariant, tTJSVariant) { return tTJSVariant(); }
    tjs_int readToFile(tTJSVariant, tTJSVariant, const tjs_char *) { return 0; }
    tTJSVariant enumTypes() { return makeEmptyArray(); }
    tTJSVariant enumNames(tTJSVariant) { return makeEmptyArray(); }
    tTJSVariant enumLangs(tTJSVariant, tTJSVariant) { return makeEmptyArray(); }

private:
    ttstr file_;
    tjs_int lang_ = 0;
};

class ResourceWriter {
public:
    static tjs_error TJS_INTF_METHOD factory(ResourceWriter **result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *) {
        auto *writer = new ResourceWriter();
        if(numparams > 0)
            writer->open(param[0]->GetString(),
                         paramBool(1, numparams, param, false));
        *result = writer;
        return TJS_S_OK;
    }
    void open(const tjs_char *file, bool = false) {
        file_ = file ? file : TJS_W("");
    }
    bool close(bool = true) {
        file_.Clear();
        return true;
    }
    tjs_int setLang(tjs_int primary, tjs_int sub) {
        lang_ = (sub << 10) | primary;
        return lang_;
    }
    bool clear(tTJSVariant, tTJSVariant) { return true; }
    bool writeFromText(tTJSVariant, tTJSVariant, const tjs_char *, bool = false) {
        return true;
    }
    bool writeFromFile(tTJSVariant, tTJSVariant, const tjs_char *) {
        return true;
    }
    bool writeFromOctet(tTJSVariant, tTJSVariant, tTJSVariant) { return true; }

private:
    ttstr file_;
    tjs_int lang_ = 0;
};

NCB_REGISTER_CLASS(ResourceReader) {
    Factory(&ResourceReader::factory);
    NCB_METHOD(open);
    NCB_METHOD(close);
    NCB_METHOD(setLang);
    NCB_METHOD(isExistentResource);
    NCB_METHOD(readToText);
    NCB_METHOD(readToFile);
    NCB_METHOD(readToOctet);
    NCB_METHOD(enumTypes);
    NCB_METHOD(enumNames);
    NCB_METHOD(enumLangs);
}

NCB_REGISTER_CLASS(ResourceWriter) {
    Factory(&ResourceWriter::factory);
    NCB_METHOD(open);
    NCB_METHOD(close);
    NCB_METHOD(setLang);
    NCB_METHOD(clear);
    NCB_METHOD(writeFromText);
    NCB_METHOD(writeFromFile);
    NCB_METHOD(writeFromOctet);
}
