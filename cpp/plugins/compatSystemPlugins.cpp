#include "CharacterSet.h"
#include "Application.h"
#include "DebugIntf.h"
#include "EventIntf.h"
#include "GraphicsLoaderIntf.h"
#include "LayerIntf.h"
#include "portableResourceBundle.h"
#include "portableRegistry.h"
#include "portableSignatureCheck.h"
#include "StorageIntf.h"
#include "SysInitIntf.h"
#include "TickCount.h"
#include "UtilStreams.h"
#include "WindowImpl.h"
#include "TVPScreen.h"
#include "ncbind.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#if defined(_WIN32)
#define popen _popen
#define pclose _pclose
#include <process.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <vector>

#if defined(AETHERKIRI_HAS_CURL)
#include <curl/curl.h>
#endif

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#if defined(__APPLE__)
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

// -------------------------------------------------------------------------
// Runtime capability introspection
// -------------------------------------------------------------------------
// A KiriKiri script traditionally discovers optional plug-ins by attempting
// a call and interpreting a false/void result.  That is not sufficient for a
// portable host: a module can be registered while only a subset of its
// Win32/Steam/SDK surface is meaningful.  Keep one explicit, script-visible
// table so games can choose a compatible code path without a user-facing
// build switch.  `available` means that the adapter has a useful
// implementation on this host; `unsupported` lists the intentionally
// fail-closed calls.
struct CompatCapabilitySpec {
    const char *module;
    const char *mode;
    bool available;
    std::vector<const char *> implemented;
    std::vector<const char *> unsupported;
    const char *reason;
};

const char *compatPlatformName() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    return "ios";
#else
    return "macos";
#endif
#elif defined(__ANDROID__)
    return "android";
#elif defined(__EMSCRIPTEN__)
    return "web";
#elif defined(__linux__)
    return "linux";
#else
    return "portable";
#endif
}

const std::vector<CompatCapabilitySpec> &compatCapabilitySpecs() {
    static const std::vector<CompatCapabilitySpec> specs = [] {
#if defined(AETHERKIRI_HAS_CURL)
        constexpr bool hasCurl = true;
#else
        constexpr bool hasCurl = false;
#endif
#if defined(AETHERKIRI_HAS_OPENSSL)
        constexpr bool hasOpenSSL = true;
#else
        constexpr bool hasOpenSSL = false;
#endif
#if defined(KRKRZ_ENABLE_DAP)
        constexpr bool hasDap = true;
#else
        constexpr bool hasDap = false;
#endif
#if defined(AETHERKIRI_RICHTEXT_ENABLED)
        constexpr bool hasRichText = true;
#else
        constexpr bool hasRichText = false;
#endif
        std::vector<CompatCapabilitySpec> value = {
            {"systemEx.dll", "portable", true,
             {"registry", "environment", "url", "knownFolder",
              "capabilityIntrospection"},
             {"nativeDpiContext", "nativeDllDirectory"},
             "Aether owns the portable System surface; native Win32 calls are forwarded only when the host provides them."},
            {"resourceRW.dll", "portable", true,
             {"sidecarResources", "ico", "cur", "groupIcon", "version"},
             {"in-placePEMutation"},
             "PE resources use a bounded AKRRES01 sidecar outside a writable Win32 image."},
            {"gamepad.dll", "portable", true,
             {"sdlEnumeration", "buttonState", "axisState", "edgeReset"},
             {"platformSpecificHaptics"},
             "SDL-backed devices are used when the host exposes them; zero devices is a valid state."},
            {"httprequest.dll", "portable", true,
             {"storageRequests", "curlRequests"},
             {"winhttpSpecificOptions"},
             hasCurl ? "libcurl is linked for network requests; Storage remains the fallback." :
                       "Only local/Storage requests are available because libcurl is not linked."},
            {"httpserv.dll", "portable", true,
             {"httpListener", "boundedRequestBody"},
             {"iisIntegration"},
             "The Aether portable HTTP listener owns sockets and request lifetime."},
            {"shellExecute.dll", "portable", true,
             {"externalOpen", "commandExecute"},
             {"shellExecuteVerbFlags"},
             "Shell actions are translated to the active host (open/xdg-open/POSIX)."},
            {"process.dll", "portable", true,
             {"spawn", "terminate", "signal"},
             {"jobObject"},
             "Process helpers use the host POSIX or Win32 process API."},
            {"stdio.dll", "portable", true,
             {"descriptorState", "ttyAllocation"},
             {"win32ConsoleAttachment"},
             "stdio state is represented by inherited descriptors and an optional controlling tty."},
            {"sigcheck.dll", hasOpenSSL ? "portable" : "fail-closed", hasOpenSSL,
             hasOpenSSL ? std::vector<const char *>{"sha256", "rsaPss"} :
                          std::vector<const char *>{},
             {"verificationWithoutCryptoProvider"},
             hasOpenSSL ? "OpenSSL-backed signature verification is enabled." :
                          "No crypto provider is linked; verification fails closed."},
            {"tftSave.dll", "portable", true,
             {"glyphMetrics", "glyphBitmap", "fontCache"}, {},
             "The shared Aether FontService/FreeType rasterizer owns the portable tftSave path."},
            {"windowEx.dll", "portable", true,
             {"cursorClip", "windowEnumeration", "virtualKeys", "dpiForwarding"},
             {"nativeWindowClassMutation"},
             "WindowEx maps logical window/input state to the active Aether host."},
            {"windowExProgress.dll", "portable", true,
             {"progressState", "progressText"}, {"nativeChildControls"},
             "Progress state is retained by the host rather than a Win32 child window."},
            {"krkrsteam.dll", "offline", true,
             {"achievements", "cloud", "language", "screenshotWrite", "capabilityIntrospection"},
             {"screenshotTrigger", "screenshotHook", "broadcasting", "broadcastHook",
              "account", "dlc"},
             "Steamworks is not linked; persistent local achievements/cloud and explicit screenshot writing remain available."},
            {"win32ole.dll", "host-only", false, {},
             {"com", "activex", "events"},
             "COM/ActiveX requires a Windows COM host and is unavailable on portable targets."},
            {"layerExAVI.dll", "host-only", false, {},
             {"aviCapture", "wavCapture"},
             "AVI/WAV capture requires a host media backend and encoder lifetime."},
            {"gameswf.dll", "host-only", false, {}, {"swfPlayback", "swfDraw"},
             "An embedded SWF runtime is not linked."},
            {"videoEncoder.dll", "host-only", false, {}, {"wmvEncode", "directShow"},
             "DirectShow/WMV encoding is not linked."},
            {"wsh.dll", "host-only", false, {}, {"windowsScriptHost"},
             "Windows Script Host is unavailable on portable targets."},
            {"javascript.dll", "reference", false, {}, {"externalJavascriptVm"},
             "An external JavaScript VM is not embedded; TJS remains the sole VM."},
            {"squirrel.dll", "reference", false, {}, {"squirrelVm"},
             "The Squirrel VM is not embedded in the product."},
            {"krkreffekseer.dll", "optional", false, {}, {"effekseer"},
             "Effekseer requires its SDK and renderer lifetime."},
            {"krkrthreepp.dll", "optional", false, {}, {"threepp", "vrm"},
             "threepp/VRM requires its SDK and renderer lifetime."},
        };
        (void)hasDap;
        (void)hasRichText;
        return value;
    }();
    return specs;
}

tTJSVariant makeCompatCapability(const CompatCapabilitySpec &spec) {
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(!dict)
        return tTJSVariant();
    setDict(dict, TJS_W("module"), fromUtf8(spec.module));
    setDict(dict, TJS_W("available"), tTJSVariant(spec.available));
    setDict(dict, TJS_W("mode"), fromUtf8(spec.mode));
    std::vector<ttstr> implemented;
    implemented.reserve(spec.implemented.size());
    for(const char *item : spec.implemented)
        implemented.emplace_back(fromUtf8(item));
    std::vector<ttstr> unsupported;
    unsupported.reserve(spec.unsupported.size());
    for(const char *item : spec.unsupported)
        unsupported.emplace_back(fromUtf8(item));
    setDict(dict, TJS_W("implemented"), makeArray(implemented));
    setDict(dict, TJS_W("unsupported"), makeArray(unsupported));
    setDict(dict, TJS_W("reason"), fromUtf8(spec.reason));
    tTJSVariant result(dict, dict);
    dict->Release();
    return result;
}

std::string normalizeCapabilityModule(const ttstr &value) {
    std::string module = toUtf8(value);
    std::transform(module.begin(), module.end(), module.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    if(module == "steam" || module == "steam.dll")
        module = "krkrsteam.dll";
    else if(module.find('.') == std::string::npos)
        module += ".dll";
    return module;
}

tjs_error TJS_INTF_METHOD getCompatibilityCapabilitiesCb(
    tTJSVariant *result, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(!dict)
        return TJS_E_FAIL;
    setDict(dict, TJS_W("schema"), tTJSVariant(1));
    setDict(dict, TJS_W("platform"), fromUtf8(compatPlatformName()));

    iTJSDispatch2 *features = TJSCreateDictionaryObject();
    if(!features) {
        dict->Release();
        return TJS_E_FAIL;
    }
#if defined(KRKRZ_ENABLE_DAP)
    setDict(features, TJS_W("dap"), tTJSVariant(true));
#else
    setDict(features, TJS_W("dap"), tTJSVariant(false));
#endif
#if defined(AETHERKIRI_HAS_CURL)
    setDict(features, TJS_W("curl"), tTJSVariant(true));
#else
    setDict(features, TJS_W("curl"), tTJSVariant(false));
#endif
#if defined(AETHERKIRI_HAS_OPENSSL)
    setDict(features, TJS_W("openssl"), tTJSVariant(true));
#else
    setDict(features, TJS_W("openssl"), tTJSVariant(false));
#endif
#if defined(AETHERKIRI_RICHTEXT_ENABLED)
    setDict(features, TJS_W("richtext"), tTJSVariant(true));
#else
    setDict(features, TJS_W("richtext"), tTJSVariant(false));
#endif
    tTJSVariant featureValue(features, features);
    features->Release();
    setDict(dict, TJS_W("features"), featureValue);

    iTJSDispatch2 *modules = TJSCreateArrayObject();
    if(!modules) {
        dict->Release();
        return TJS_E_FAIL;
    }
    tjs_int index = 0;
    for(const CompatCapabilitySpec &spec : compatCapabilitySpecs()) {
        tTJSVariant capability = makeCompatCapability(spec);
        modules->PropSetByNum(TJS_MEMBERENSURE, index++, &capability, modules);
    }
    tTJSVariant moduleValue(modules, modules);
    modules->Release();
    setDict(dict, TJS_W("modules"), moduleValue);
    if(result)
        *result = tTJSVariant(dict, dict);
    dict->Release();
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD getCompatibilityCapabilityCb(
    tTJSVariant *result, tjs_int count, tTJSVariant **params,
    iTJSDispatch2 *) {
    if(count < 1 || !params || !params[0])
        return TJS_E_BADPARAMCOUNT;
    const std::string requested = normalizeCapabilityModule(ttstr(*params[0]));
    for(const CompatCapabilitySpec &spec : compatCapabilitySpecs()) {
        if(requested == spec.module) {
            if(result)
                *result = makeCompatCapability(spec);
            return TJS_S_OK;
        }
    }
    CompatCapabilitySpec unknown{
        requested.c_str(), "unknown", false, {}, {"module"},
        "The module is not part of the audited compatibility surface."};
    if(result)
        *result = makeCompatCapability(unknown);
    return TJS_S_OK;
}

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
    constexpr std::size_t kMaxCompatStorageBytes = 256u * 1024u * 1024u;
    if(storage.IsEmpty() || bytes.size() > kMaxCompatStorageBytes)
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
    constexpr std::size_t kMaxCompatStorageBytes = 256u * 1024u * 1024u;
    try {
        std::unique_ptr<tTJSBinaryStream> stream(
            TVPCreateStream(storage, TJS_BS_READ));
        if(!stream)
            return false;
        const tjs_uint64 size64 = stream->GetSize();
        if(size64 > static_cast<tjs_uint64>(kMaxCompatStorageBytes) ||
           size64 > static_cast<tjs_uint64>(static_cast<size_t>(-1)))
            return false;
        bytes.assign(static_cast<size_t>(size64), '\0');
        // ReadBuffer accepts a 32-bit count. Read in bounded chunks so a
        // large (but valid) sidecar cannot wrap the count and truncate data.
        size_t offset = 0;
        constexpr size_t kChunk =
            static_cast<size_t>(std::numeric_limits<tjs_uint>::max());
        while(offset < bytes.size()) {
            const size_t remaining = bytes.size() - offset;
            const tjs_uint count = static_cast<tjs_uint>(std::min(remaining, kChunk));
            stream->ReadBuffer(bytes.data() + offset, count);
            offset += count;
        }
        return true;
    } catch(...) {
        return false;
    }
}

// -------------------------------------------------------------------------
// Portable resourceRW backing store
// -------------------------------------------------------------------------
// PE resources are inherently Win32-specific.  Aether still needs a real
// resourceRW contract on macOS/Linux/Android so scripts can package icons,
// translations and arbitrary RCDATA without silently succeeding and losing
// their data.  The adapter stores those entries in a deterministic sidecar
// (`<target>.aetherres`) using the small, independently-tested AKRRES01
// container.  On Windows this is also a safe fallback for targets that are
// not writable PE images; it never mutates the original executable.

using PortableResourceEntry = AetherKiri::ResourceBundle::Entry;

ttstr resourceSidecarName(const ttstr &target) {
    return target + TJS_W(".aetherres");
}

std::string resourceVariantKey(const tTJSVariant &value) {
    if(value.Type() == tvtInteger)
        return "@" + std::to_string(static_cast<tjs_int64>(value));
    if(value.Type() == tvtString)
        return "=" + toUtf8(value.GetString());
    return {};
}

bool resourceKeyToVariant(const std::string &key, tTJSVariant &value) {
    if(key.size() < 2)
        return false;
    if(key.front() == '@') {
        char *end = nullptr;
        errno = 0;
        const long long number = std::strtoll(key.c_str() + 1, &end, 10);
        if(errno != 0 || !end || *end != '\0')
            return false;
        value = static_cast<tjs_int64>(number);
        return true;
    }
    if(key.front() == '=') {
        value = fromUtf8(key.data() + 1, key.size() - 1);
        return true;
    }
    return false;
}

bool loadPortableResourceEntries(const ttstr &target,
                                 std::vector<PortableResourceEntry> &entries) {
    entries.clear();
    std::string encoded;
    if(!readStorageBytes(resourceSidecarName(target), encoded))
        return false;
    std::vector<std::uint8_t> bytes(encoded.begin(), encoded.end());
    std::string error;
    if(!AetherKiri::ResourceBundle::Decode(bytes, entries, &error)) {
        logCompatOnce(TJS_W("resourceRW.dll"),
                      fromUtf8(error).c_str());
        entries.clear();
        return false;
    }
    return true;
}

bool savePortableResourceEntries(const ttstr &target,
                                 const std::vector<PortableResourceEntry> &entries) {
    std::vector<std::uint8_t> encoded;
    std::string error;
    if(!AetherKiri::ResourceBundle::Encode(entries, encoded, &error)) {
        logCompatOnce(TJS_W("resourceRW.dll"),
                      fromUtf8(error).c_str());
        return false;
    }
    return writeStorageBytes(resourceSidecarName(target),
                             std::string(encoded.begin(), encoded.end()));
}

tTJSVariant makeResourceVariantArray(const std::vector<std::string> &keys) {
    iTJSDispatch2 *array = TJSCreateArrayObject();
    if(!array)
        return tTJSVariant();
    for(tjs_int index = 0; index < static_cast<tjs_int>(keys.size()); ++index) {
        tTJSVariant value;
        if(!resourceKeyToVariant(keys[static_cast<size_t>(index)], value))
            continue;
        array->PropSetByNum(TJS_MEMBERENSURE, index, &value, array);
    }
    tTJSVariant result(array, array);
    array->Release();
    return result;
}

std::string resourceEntryIdentity(const PortableResourceEntry &entry) {
    return entry.type + "\n" + entry.name + "\n" +
        std::to_string(entry.language);
}

// resourceRW accepts both numeric LANGID components and the symbolic names
// exposed by Win32's LANG_*/SUBLANG_* tables.  Keep a compact, dependency-free
// table here so scripts using the documented string form behave identically on
// non-Windows hosts.  Unknown names are rejected instead of being coerced to
// zero (which would unexpectedly select the neutral resource).
int languageNameValue(std::string name, bool primary) {
    for(char &c : name) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if(c == '-') c = '_';
    }
    const char *prefix = primary ? "LANG_" : "SUBLANG_";
    const std::size_t prefixLength = std::strlen(prefix);
    if(name.compare(0, prefixLength, prefix) == 0)
        name.erase(0, prefixLength);

    if(primary) {
        static const std::map<std::string, int> values = {
            {"NEUTRAL", 0}, {"INVARIANT", 127}, {"AFRIKAANS", 54},
            {"ALBANIAN", 28}, {"ARABIC", 1}, {"ARMENIAN", 43},
            {"AZERI", 44}, {"BASQUE", 45}, {"BELARUSIAN", 35},
            {"BENGALI", 69}, {"BULGARIAN", 2}, {"CATALAN", 3},
            {"CHINESE", 4}, {"CROATIAN", 26}, {"CZECH", 5},
            {"DANISH", 6}, {"DUTCH", 19}, {"ENGLISH", 9},
            {"ESTONIAN", 37}, {"FARSI", 41}, {"PERSIAN", 41},
            {"FINNISH", 11}, {"FRENCH", 12}, {"GALICIAN", 86},
            {"GEORGIAN", 55}, {"GERMAN", 7}, {"GREEK", 8},
            {"GUJARATI", 71}, {"HEBREW", 13}, {"HINDI", 57},
            {"HUNGARIAN", 14}, {"ICELANDIC", 15}, {"INDONESIAN", 33},
            {"ITALIAN", 16}, {"JAPANESE", 17}, {"KANNADA", 75},
            {"KAZAKH", 63}, {"KOREAN", 18}, {"LATVIAN", 38},
            {"LITHUANIAN", 39}, {"MALAY", 62}, {"MARATHI", 78},
            {"NORWEGIAN", 20}, {"POLISH", 21}, {"PORTUGUESE", 22},
            {"ROMANIAN", 24}, {"RUSSIAN", 25}, {"SERBIAN", 0x1a},
            {"SLOVAK", 27}, {"SLOVENIAN", 36}, {"SPANISH", 10},
            {"SWEDISH", 29}, {"TAMIL", 73}, {"TELUGU", 74},
            {"THAI", 30}, {"TURKISH", 31}, {"UKRAINIAN", 34},
            {"URDU", 32}, {"VIETNAMESE", 42}, {"WELSH", 82}
        };
        const auto it = values.find(name);
        return it == values.end() ? -1 : it->second;
    }

    static const std::map<std::string, int> values = {
        {"NEUTRAL", 0}, {"DEFAULT", 1}, {"SYS_DEFAULT", 2},
        {"CUSTOM_DEFAULT", 3}, {"TRADITIONAL_CHINESE", 1},
        {"SIMPLIFIED_CHINESE", 2}, {"US", 1}, {"UNITED_STATES", 1},
        {"UK", 2}, {"UNITED_KINGDOM", 2}, {"AUSTRALIAN", 3},
        {"CANADIAN", 4}, {"NEW_ZEALAND", 5}, {"IRELAND", 6},
        {"SOUTH_AFRICAN", 7}, {"JAMAICA", 8}, {"CARIBBEAN", 9},
        {"BELIZE", 10}, {"TRINIDAD", 11}, {"ZIMBABWE", 12},
        {"PHILIPPINES", 13}, {"FRANCE", 1}, {"BELGIAN", 2},
        {"CANADIAN_FRENCH", 3}, {"SWISS", 4}, {"LUXEMBOURG", 5},
        {"MONACO", 6}, {"GERMAN", 1}, {"SWISS_GERMAN", 2},
        {"AUSTRIAN", 3}, {"LUXEMBOURG_GERMAN", 4},
        {"LIECHTENSTEIN", 5}, {"ITALIAN", 1}, {"SWISS_ITALIAN", 2},
        {"JAPANESE", 1}, {"KOREAN", 1}, {"DUTCH", 1},
        {"PORTUGUESE_BRAZILIAN", 1}, {"PORTUGUESE_STANDARD", 2},
        {"SPANISH_TRADITIONAL", 1}, {"SPANISH_MEXICAN", 2},
        {"SPANISH_MODERN", 3}, {"SWEDISH", 1}, {"FINNISH", 1},
        {"NORWEGIAN_BOKMAL", 1}, {"NORWEGIAN_NYNORSK", 2},
        {"RUSSIAN", 1}, {"UKRAINIAN", 1}, {"TURKISH", 1},
        {"VIETNAMESE", 1}, {"ARABIC_SAUDI_ARABIA", 2},
        {"ARABIC_EGYPT", 3}, {"ARABIC_LIBYA", 4},
        {"ARABIC_ALGERIA", 5}, {"ARABIC_MOROCCO", 6},
        {"ARABIC_TUNISIA", 7}, {"ARABIC_OMAN", 8},
        {"ARABIC_YEMEN", 9}, {"ARABIC_SYRIA", 10},
        {"ARABIC_JORDAN", 11}, {"ARABIC_LEBANON", 12},
        {"ARABIC_KUWAIT", 13}, {"ARABIC_UAE", 14},
        {"ARABIC_BAHRAIN", 15}, {"ARABIC_QATAR", 16},
    };
    const auto it = values.find(name);
    return it == values.end() ? -1 : it->second;
}

bool parseLanguageValue(const tTJSVariant &value, bool primary, int &out) {
    if(value.Type() == tvtInteger) {
        const tjs_int64 number = static_cast<tjs_int64>(value);
        const tjs_int64 maximum = primary ? 0x3ff : 0x3f;
        if(number < 0 || number > maximum)
            return false;
        out = static_cast<int>(number);
        return true;
    }
    if(value.Type() != tvtString)
        return false;
    std::string name = toUtf8(value.GetString());
    for(char &c : name) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if(c == '-') c = '_';
    }
    // A single BCP-47 argument is a useful extension to the Win32 API and
    // maps directly to the complete LANGID.
    if(primary) {
        static const std::map<std::string, int> tags = {
            {"EN_US", 0x0409}, {"EN_GB", 0x0809}, {"EN_AU", 0x0c09},
            {"EN_CA", 0x1009}, {"ZH_CN", 0x0804}, {"ZH_TW", 0x0404},
            {"ZH_HK", 0x0c04}, {"JA_JP", 0x0411}, {"KO_KR", 0x0412},
            {"FR_FR", 0x040c}, {"DE_DE", 0x0407}, {"ES_ES", 0x0c0a},
            {"PT_BR", 0x0416}, {"RU_RU", 0x0419},
        };
        const auto tag = tags.find(name);
        if(tag != tags.end()) {
            out = tag->second;
            return true;
        }
    }
    const int parsed = languageNameValue(name, primary);
    if(parsed < 0)
        return false;
    out = parsed;
    return true;
}

bool parseResourceLanguageArgs(tjs_int numparams, tTJSVariant **params,
                               std::uint32_t &language) {
    if(numparams < 1 || !params || !params[0])
        return false;
    int primary = 0;
    if(!parseLanguageValue(*params[0], true, primary))
        return false;
    // `parseLanguageValue` also recognizes a complete BCP-47 tag (for
    // example "en-US").  That form is only valid as the sole argument.
    if(numparams == 1 && primary > 0x3ff) {
        language = static_cast<std::uint32_t>(primary);
        return true;
    }
    if(primary < 0 || primary > 0x3ff)
        return false;
    int sub = 0;
    if(numparams > 1) {
        if(!params[1] || !parseLanguageValue(*params[1], false, sub))
            return false;
    }
    if(sub < 0 || sub > 0x3f)
        return false;
    language = (static_cast<std::uint32_t>(sub) << 10) |
        static_cast<std::uint32_t>(primary);
    return true;
}

const PortableResourceEntry *findPortableResource(
    const std::vector<PortableResourceEntry> &entries, const std::string &type,
    const std::string &name, std::uint32_t language) {
    const PortableResourceEntry *neutral = nullptr;
    const PortableResourceEntry *first = nullptr;
    for(const auto &entry : entries) {
        if(entry.type != type || entry.name != name)
            continue;
        if(!first)
            first = &entry;
        if(entry.language == language)
            return &entry;
        if(entry.language == 0)
            neutral = &entry;
    }
    return neutral ? neutral : first;
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

std::string variantBytes(const tTJSVariant *value) {
    if(!value || value->Type() == tvtVoid)
        return {};
    if(value->Type() == tvtOctet) {
        const tTJSVariantOctet *octet = value->AsOctetNoAddRef();
        if(!octet || octet->GetLength() == 0)
            return {};
        if(!octet->GetData())
            return {};
        return std::string(
            reinterpret_cast<const char *>(octet->GetData()),
            static_cast<size_t>(octet->GetLength()));
    }
    return toUtf8(ttstr(*value));
}

#if defined(AETHERKIRI_HAS_CURL)
constexpr std::size_t kMaxHttpBodyBytes = 64u * 1024u * 1024u;
constexpr std::size_t kMaxHttpHeaderBytes = 1024u * 1024u;
struct CurlBuffers {
    std::string headers;
    std::string body;
    const std::atomic<bool> *cancelled = nullptr;
};

size_t curlWriteBody(char *data, size_t size, size_t count, void *opaque) {
    auto *buffers = static_cast<CurlBuffers *>(opaque);
    if(size != 0 && count > std::numeric_limits<size_t>::max() / size)
        return 0;
    const size_t bytes = size * count;
    if(!buffers || (buffers->cancelled &&
                    buffers->cancelled->load(std::memory_order_acquire)) ||
       bytes > kMaxHttpBodyBytes -
                   std::min(kMaxHttpBodyBytes, buffers->body.size()))
        return 0;
    buffers->body.append(data, bytes);
    return bytes;
}

size_t curlWriteHeader(char *data, size_t size, size_t count, void *opaque) {
    auto *buffers = static_cast<CurlBuffers *>(opaque);
    if(size != 0 && count > std::numeric_limits<size_t>::max() / size)
        return 0;
    const size_t bytes = size * count;
    if(!buffers || (buffers->cancelled &&
                    buffers->cancelled->load(std::memory_order_acquire)) ||
       bytes > kMaxHttpHeaderBytes -
                   std::min(kMaxHttpHeaderBytes, buffers->headers.size()))
        return 0;
    buffers->headers.append(data, bytes);
    return bytes;
}

int curlProgress(void *opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto *buffers = static_cast<const CurlBuffers *>(opaque);
    return buffers && buffers->cancelled &&
               buffers->cancelled->load(std::memory_order_acquire)
        ? 1 : 0;
}

std::string finalHttpHeaderBlock(const std::string &raw) {
    if(raw.empty())
        return {};
    // CURLOPT_HEADERFUNCTION receives redirect/interim blocks too. Keep the
    // last HTTP block, which is the one matching the response body.
    size_t blockStart = raw.find("HTTP/");
    size_t cursor = blockStart;
    while(cursor != std::string::npos) {
        const size_t next = raw.find("HTTP/", cursor + 5);
        if(next == std::string::npos)
            break;
        blockStart = next;
        cursor = next;
    }
    if(blockStart == std::string::npos)
        blockStart = 0;
    size_t end = raw.find("\r\n\r\n", blockStart);
    if(end != std::string::npos)
        end += 4;
    else {
        end = raw.find("\n\n", blockStart);
        if(end != std::string::npos)
            end += 2;
        else
            end = raw.size();
    }
    return raw.substr(blockStart, end - blockStart);
}

FetchResult fetchHttpWithCurl(const ttstr &url, const ttstr &method,
                              const std::map<std::string, ttstr> &headers,
                              const std::string &requestBody,
                              const std::atomic<bool> *cancelled = nullptr) {
    FetchResult result;
    static std::once_flag curlInit;
    std::call_once(curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    CURL *handle = curl_easy_init();
    if(!handle) {
        result.statusText = TJS_W("curl initialization failed");
        return result;
    }

    CurlBuffers buffers;
    buffers.cancelled = cancelled;
    struct curl_slist *requestHeaders = nullptr;
    std::size_t requestHeaderBytes = 0;
    bool hasContentLength = false;
    for(const auto &header : headers) {
        const std::string headerValue = toUtf8(header.second);
        if(header.first.empty() || header.first.size() > 8192 ||
           headerValue.size() > 64u * 1024u) {
            result.statusText = TJS_W("HTTP request header is too large");
            curl_slist_free_all(requestHeaders);
            curl_easy_cleanup(handle);
            return result;
        }
        std::string normalizedName = header.first;
        for(char &c : normalizedName)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if(normalizedName == "content-length")
            hasContentLength = true;
        const std::string line = header.first + ": " + headerValue;
        if(line.size() > kMaxHttpHeaderBytes ||
           line.size() > kMaxHttpHeaderBytes -
                              std::min(kMaxHttpHeaderBytes,
                                       requestHeaderBytes)) {
            result.statusText = TJS_W("HTTP request headers are too large");
            curl_slist_free_all(requestHeaders);
            curl_easy_cleanup(handle);
            return result;
        }
        struct curl_slist *next =
            curl_slist_append(requestHeaders, line.c_str());
        if(!next) {
            result.statusText = TJS_W("HTTP request header allocation failed");
            curl_slist_free_all(requestHeaders);
            curl_easy_cleanup(handle);
            return result;
        }
        requestHeaders = next;
        requestHeaderBytes += line.size();
    }
    if(!requestBody.empty() && !hasContentLength) {
        const std::string line = "Content-Length: " +
            std::to_string(requestBody.size());
        if(line.size() > kMaxHttpHeaderBytes ||
           line.size() > kMaxHttpHeaderBytes -
                              std::min(kMaxHttpHeaderBytes,
                                       requestHeaderBytes)) {
            result.statusText = TJS_W("HTTP request headers are too large");
            curl_slist_free_all(requestHeaders);
            curl_easy_cleanup(handle);
            return result;
        }
        struct curl_slist *next =
            curl_slist_append(requestHeaders, line.c_str());
        if(!next) {
            result.statusText = TJS_W("HTTP request header allocation failed");
            curl_slist_free_all(requestHeaders);
            curl_easy_cleanup(handle);
            return result;
        }
        requestHeaders = next;
    }

    if(requestBody.size() > kMaxHttpBodyBytes ||
       requestBody.size() > static_cast<std::size_t>(std::numeric_limits<long>::max())) {
        curl_slist_free_all(requestHeaders);
        curl_easy_cleanup(handle);
        result.statusText = TJS_W("HTTP request body is too large");
        return result;
    }

    const std::string utf8Url = toUtf8(url);
    const std::string utf8Method = toUtf8(method);
    curl_easy_setopt(handle, CURLOPT_URL, utf8Url.c_str());
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &curlWriteBody);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &buffers);
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, &curlWriteHeader);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, &buffers);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, &curlProgress);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &buffers);
    if(requestHeaders)
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, requestHeaders);

    const bool isGet = utf8Method.empty() || utf8Method == "GET";
    const bool isHead = utf8Method == "HEAD";
    if(isHead) {
        curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);
    } else if(!isGet) {
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, utf8Method.c_str());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, requestBody.data());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(requestBody.size()));
    } else if(!requestBody.empty()) {
        // A body on GET is unusual but legal and is used by a few legacy
        // scripts; preserve it instead of silently dropping the payload.
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "GET");
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, requestBody.data());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(requestBody.size()));
    }

    const CURLcode code = curl_easy_perform(handle);
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    result.status = status > 0 && status <= std::numeric_limits<tjs_int>::max()
        ? static_cast<tjs_int>(status)
        : 0;
    result.headers = fromUtf8(finalHttpHeaderBlock(buffers.headers));
    result.body = std::move(buffers.body);
    result.ok = code == CURLE_OK;
    if(code == CURLE_OK) {
        result.statusText = result.status == 0 ? TJS_W("OK") : TJS_W("HTTP");
        const std::string headerBlock = finalHttpHeaderBlock(buffers.headers);
        const size_t lineEnd = headerBlock.find('\n');
        if(lineEnd != std::string::npos) {
            std::string line = headerBlock.substr(0, lineEnd);
            while(!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.pop_back();
            const size_t firstSpace = line.find(' ');
            const size_t secondSpace = firstSpace == std::string::npos
                ? std::string::npos : line.find(' ', firstSpace + 1);
            if(secondSpace != std::string::npos)
                result.statusText = fromUtf8(line.substr(secondSpace + 1));
        }
    } else {
        result.statusText = fromUtf8(curl_easy_strerror(code));
    }
    curl_slist_free_all(requestHeaders);
    curl_easy_cleanup(handle);
    return result;
}
#endif

FetchResult fetchUrlOrStorage(const ttstr &url, const ttstr &method,
                              const std::map<std::string, ttstr> &headers,
                              const std::string &requestBody,
                              const std::atomic<bool> *cancelled = nullptr) {
    FetchResult result;
    const std::string utf8Url = toUtf8(url);
    if(startsWithAscii(utf8Url, "http://") ||
       startsWithAscii(utf8Url, "https://")) {
#if defined(AETHERKIRI_HAS_CURL)
        result = fetchHttpWithCurl(url, method, headers, requestBody, cancelled);
#else
        (void)method;
        (void)headers;
        (void)requestBody;
        (void)cancelled;
        result.statusText = TJS_W("network transport unavailable");
        logCompatOnce(TJS_W("httprequest.dll"),
                      TJS_W("libcurl is not available for HTTP requests"));
#endif
        return result;
    }

    ttstr storage = url;
    if(startsWithAscii(utf8Url, "file://"))
        storage = fromUtf8(utf8Url.substr(7));
    if((!cancelled || !cancelled->load(std::memory_order_acquire)) &&
       readStorageBytes(storage, result.body)) {
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

// Launch a command without blocking the TJS thread.  The Win32 plug-in uses
// CreateProcess/ShellExecute; POSIX hosts use a small fork/exec bridge so the
// returned process can be observed and terminated by Process.  The shell is
// intentionally retained for compatibility with krkrz's target+param shape
// (scripts commonly pass redirections or a quoted command line).
intptr_t spawnShellCommand(const std::string &command,
                           const ttstr &folder = ttstr()) {
#if defined(_WIN32)
    std::string commandLine = command;
    (void)folder; // _spawnl has no portable working-directory argument.
    const intptr_t pid = _spawnl(_P_NOWAIT, "cmd.exe", "cmd.exe", "/C",
                                 commandLine.c_str(), nullptr);
    return pid == static_cast<intptr_t>(-1) ? 0 : pid;
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    (void)command;
    (void)folder;
    logCompatOnce(TJS_W("process.dll"),
                  TJS_W("shell command launch is unavailable on iOS"));
    return 0;
#else
    const pid_t child = ::fork();
    if(child < 0)
        return 0;
    if(child == 0) {
        if(!folder.IsEmpty()) {
            const std::string directory = toUtf8(folder);
            if(::chdir(directory.c_str()) != 0)
                _exit(126);
        }
        ::execl("/bin/sh", "sh", "-c", command.c_str(),
                static_cast<char *>(nullptr));
        _exit(127);
    }
    return static_cast<intptr_t>(child);
#endif
}

bool terminateProcessId(intptr_t process, tjs_int endCode) {
    if(process == 0)
        return false;
#if defined(_WIN32)
    HANDLE handle = ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE,
                                  static_cast<DWORD>(process));
    if(!handle)
        return false;
    const BOOL ok = ::TerminateProcess(handle, static_cast<UINT>(endCode));
    ::CloseHandle(handle);
    return ok != FALSE;
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    (void)endCode;
    return false;
#else
    (void)endCode;
    return ::kill(static_cast<pid_t>(process), SIGTERM) == 0;
#endif
}

bool sendProcessSignal(intptr_t process, bool isBreak) {
    if(process == 0)
        return false;
#if defined(_WIN32)
    // GenerateConsoleCtrlEvent requires a console process group and cannot be
    // safely emulated for an arbitrary ShellExecute target.
    (void)isBreak;
    return false;
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    (void)isBreak;
    return false;
#else
    return ::kill(static_cast<pid_t>(process), isBreak ? SIGQUIT : SIGINT) == 0;
#endif
}

bool openExternal(const ttstr &target, const ttstr &args = ttstr()) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
    (void)target;
    (void)args;
    logCompatOnce(TJS_W("process.dll"),
                  TJS_W("external process launch is unavailable on iOS"));
    return false;
#elif defined(__APPLE__)
    std::string command = "open " + shellQuote(toUtf8(target));
    if(!args.IsEmpty())
        command += " --args " + shellQuote(toUtf8(args));
    return spawnShellCommand(command) != 0;
#elif defined(_WIN32)
    // Keep the Windows shell semantics while still returning a useful launch
    // result.  Process itself uses _spawnl; shellExecute's target may be a
    // document/URL which cmd.exe can delegate to the registered handler.
    return spawnShellCommand(composeCommand(target, args)) != 0;
#else
    // xdg-open handles URLs/documents.  If explicit arguments are supplied,
    // execute the command itself, matching the old shellExecute fallback.
    if(args.IsEmpty())
        return spawnShellCommand("xdg-open " + shellQuote(toUtf8(target))) != 0;
    return spawnShellCommand(composeCommand(target, args)) != 0;
#endif
}

} // namespace

// -------------------------------------------------------------------------
// process.dll
// AETHERKIRI_COMPAT_STUB: POSIX process bridge, not Win32 message-window API.
// -------------------------------------------------------------------------

#define NCB_MODULE_NAME TJS_W("process.dll")

class Process {
public:
    explicit Process(iTJSDispatch2 *owner) : owner_(owner) {
        if(owner_)
            owner_->AddRef();
    }

    ~Process() {
        terminate();
        if(owner_) {
            TVPCancelSourceEvents(owner_);
            owner_->Release();
            owner_ = nullptr;
        }
    }

    static tjs_error TJS_INTF_METHOD factory(Process **result, tjs_int,
                                             tTJSVariant **, iTJSDispatch2 *objthis) {
        if(!result)
            return TJS_E_FAIL;
        *result = new Process(objthis);
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
        const ttstr folder = paramString(2, numparams, param);
        const intptr_t pid = self ? self->start(command, folder) : 0;
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
        intptr_t pid = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pid = pid_;
        }
        if(pid == 0) {
            joinWorker();
            return false;
        }
        const bool sent = terminateProcessId(pid, endCode);
        joinWorker();
        return sent;
    }

    bool sendSignal(bool isBreak = false) {
        std::lock_guard<std::mutex> lock(mutex_);
        return sendProcessSignal(pid_, isBreak);
    }

    tjs_int getStatus() const {
        return status_.load(std::memory_order_acquire);
    }

private:
    intptr_t start(const std::string &command, const ttstr &folder) {
        terminate();
        const intptr_t pid = spawnShellCommand(command, folder);
        if(pid == 0) {
            status_.store(0, std::memory_order_release);
            return 0;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pid_ = pid;
            status_.store(1, std::memory_order_release);
        }
        worker_ = std::thread([this, pid] { waitForProcess(pid); });
        return pid;
    }

    void joinWorker() {
        if(worker_.joinable() &&
           worker_.get_id() != std::this_thread::get_id())
            worker_.join();
    }

    void waitForProcess(intptr_t pid) {
        int exitCode = -1;
#if defined(_WIN32)
        int processStatus = -1;
        if(_cwait(&processStatus, static_cast<intptr_t>(pid), _WAIT_CHILD) == 0)
            exitCode = processStatus;
#elif defined(__APPLE__) && TARGET_OS_IPHONE
        (void)pid;
#else
        int processStatus = 0;
        pid_t waited = -1;
        do {
            waited = ::waitpid(static_cast<pid_t>(pid), &processStatus, 0);
        } while(waited < 0 && errno == EINTR);
        if(waited == static_cast<pid_t>(pid)) {
            if(WIFEXITED(processStatus))
                exitCode = WEXITSTATUS(processStatus);
            else if(WIFSIGNALED(processStatus))
                exitCode = 128 + WTERMSIG(processStatus);
        }
#endif
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(pid_ == pid)
                pid_ = 0;
        }
        status_.store(0, std::memory_order_release);
        if(owner_) {
            tTJSVariant code(exitCode);
            static ttstr eventName(TJS_W("onExecuted"));
            TVPPostEvent(owner_, owner_, eventName, 0, TVP_EPT_POST, 1,
                         &code);
        }
    }

    mutable std::mutex mutex_;
    intptr_t pid_ = 0;
    std::atomic<tjs_int> status_{0};
    std::thread worker_;
    iTJSDispatch2 *owner_ = nullptr;
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
        const bool launched =
            openExternal(paramString(0, numparams, param),
                         paramString(1, numparams, param));
        if(result)
            *result = launched;
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD commandExecute(tTJSVariant *result,
                                                    tjs_int numparams,
                                                    tTJSVariant **param,
                                                    WindowShellCompat *) {
        return ::commandExecuteCb(result, numparams, param, nullptr);
    }

    bool terminateProcess(tjs_int process, tjs_int = 0) {
        return process > 0 && terminateProcessId(static_cast<intptr_t>(process), 0);
    }

    bool commandSendSignal(tjs_int process, bool isBreak = false) {
        return process > 0 &&
               sendProcessSignal(static_cast<intptr_t>(process), isBreak);
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
// Keep the old callback names on System, but put the actual values in the
// shared persistent store so read/write calls survive an engine restart and
// agree with every compatibility alias (registory.dll, systemEx.dll, etc.).
tjs_error TJS_INTF_METHOD readRegValueCb(tTJSVariant *result,
                                         tjs_int numparams,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *) {
    if(numparams < 1 || !param || !param[0])
        return TJS_E_BADPARAMCOUNT;
    if(result) {
        tTJSVariant value;
        if(AetherKiri::PortableRegistryStore::Instance().Read(
               ttstr(*param[0]), value))
            *result = value;
        else
            result->Clear();
    }
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD writeRegValueCb(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *) {
    if(numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    if(!AetherKiri::PortableRegistryStore::Instance().Write(
           ttstr(*param[0]), *param[1])) {
        if(result)
            *result = false;
        return TJS_S_OK;
    }
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
    const bool removed =
        AetherKiri::PortableRegistryStore::Instance().DeleteValue(
            ttstr(*param[0]));
    if(result)
        *result = removed;
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD deleteRegKeyCb(tTJSVariant *result,
                                         tjs_int numparams,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    const bool ok = AetherKiri::PortableRegistryStore::Instance().DeleteKey(
        ttstr(*param[0]));
    if(result)
        *result = ok;
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

tjs_error TJS_INTF_METHOD getOSVersionCb(tTJSVariant *result, tjs_int,
                                         tTJSVariant **, iTJSDispatch2 *) {
    if(result)
        result->Clear();

    tjs_int major = 0;
    tjs_int minor = 0;
    tjs_int build = 0;
    tjs_int platform = 0;
    tjs_int servicePackMajor = 0;
    tjs_int servicePackMinor = 0;
    tjs_int suite = 0;
    tjs_int productType = 1;
    ttstr servicePack;

#if defined(_WIN32)
    // GetVersionEx is intentionally used only as a fallback.  It is the
    // stable API available to the old Windows targets supported by krkrz;
    // newer hosts may virtualise the values, but the shape remains useful to
    // scripts.  RtlGetVersion is not required for the portable adapter.
    OSVERSIONINFOEXW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if(::GetVersionExW(reinterpret_cast<OSVERSIONINFOW *>(&version)) != FALSE) {
        major = static_cast<tjs_int>(version.dwMajorVersion);
        minor = static_cast<tjs_int>(version.dwMinorVersion);
        build = static_cast<tjs_int>(version.dwBuildNumber);
        platform = static_cast<tjs_int>(version.dwPlatformId);
        servicePackMajor = static_cast<tjs_int>(version.wServicePackMajor);
        servicePackMinor = static_cast<tjs_int>(version.wServicePackMinor);
        suite = static_cast<tjs_int>(version.wSuiteMask);
        productType = static_cast<tjs_int>(version.wProductType);
        servicePack = ttstr(reinterpret_cast<const tjs_char *>(
            version.szCSDVersion));
    }
#else
    struct utsname system{};
    if(::uname(&system) == 0) {
        // uname releases are conventionally major.minor.patch[-suffix].
        // Parse only bounded decimal components; preserving a zero for an
        // unparseable component is preferable to overflowing a TJS integer.
        long parsedMajor = 0;
        long parsedMinor = 0;
        long parsedBuild = 0;
        if(std::sscanf(system.release, "%ld.%ld.%ld", &parsedMajor,
                       &parsedMinor, &parsedBuild) >= 1) {
            const auto clampInt = [](long value) -> tjs_int {
                return value < 0
                    ? 0
                    : value > std::numeric_limits<tjs_int>::max()
                        ? std::numeric_limits<tjs_int>::max()
                        : static_cast<tjs_int>(value);
            };
            major = clampInt(parsedMajor);
            minor = clampInt(parsedMinor);
            build = clampInt(parsedBuild);
        }
        servicePack = fromUtf8(system.release);
    }
#endif

    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(!dict)
        return TJS_E_FAIL;
    setDict(dict, TJS_W("major"), tTJSVariant(major));
    setDict(dict, TJS_W("minor"), tTJSVariant(minor));
    setDict(dict, TJS_W("build"), tTJSVariant(build));
    setDict(dict, TJS_W("platform"), tTJSVariant(platform));
    setDict(dict, TJS_W("spmajor"), tTJSVariant(servicePackMajor));
    setDict(dict, TJS_W("spminor"), tTJSVariant(servicePackMinor));
    setDict(dict, TJS_W("servicepack"), tTJSVariant(servicePack));
    setDict(dict, TJS_W("suite"), tTJSVariant(suite));
    setDict(dict, TJS_W("type"), tTJSVariant(productType));
    if(result)
        *result = tTJSVariant(dict, dict);
    dict->Release();
    return TJS_S_OK;
}

std::string lowerAscii(std::string value) {
    for(char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string envOrEmpty(const char *name) {
    if(const char *value = std::getenv(name))
        return value;
    return {};
}

std::string joinKnownFolder(const std::string &base, const char *suffix) {
    if(base.empty())
        return {};
    std::filesystem::path path(base);
    if(suffix && suffix[0])
        path /= suffix;
    return path.lexically_normal().string();
}

// Resolve the portable names used by krkrz's FOLDERID table.  The Windows
// branch also honours the standard environment variables, which keeps this
// adapter useful on old Windows versions where SHGetKnownFolderPath is not
// available.  Unknown names deliberately return an empty result rather than
// inventing a path.
std::string portableKnownFolderPath(const std::string &requested) {
    const std::string name = lowerAscii(requested);
    std::string home = envOrEmpty("HOME");
#if defined(_WIN32)
    if(home.empty())
        home = envOrEmpty("USERPROFILE");
#endif
    if(name == "profile" || name == "userprofile" || name == "home")
        return home;
    if(name == "desktop")
        return joinKnownFolder(home, "Desktop");
    if(name == "documents" || name == "publicdocuments")
        return joinKnownFolder(home, "Documents");
    if(name == "downloads" || name == "publicdownloads")
        return joinKnownFolder(home, "Downloads");
    if(name == "pictures" || name == "publicpictures")
        return joinKnownFolder(home, "Pictures");
    if(name == "music" || name == "publicmusic")
        return joinKnownFolder(home, "Music");
    if(name == "videos" || name == "publicvideos")
        return joinKnownFolder(home, "Videos");
    if(name == "public" || name == "publicdesktop") {
#if defined(__APPLE__)
        return "/Users/Shared";
#elif defined(_WIN32)
        if(const std::string publicRoot = envOrEmpty("PUBLIC");
           !publicRoot.empty())
            return publicRoot;
        return joinKnownFolder(home, "Public");
#else
        return "/tmp";
#endif
    }
    if(name == "roamingappdata" || name == "appdata") {
#if defined(_WIN32)
        if(const std::string value = envOrEmpty("APPDATA"); !value.empty())
            return value;
#elif defined(__APPLE__)
        return joinKnownFolder(home, "Library/Application Support");
#else
        if(const std::string value = envOrEmpty("XDG_CONFIG_HOME");
           !value.empty())
            return value;
        return joinKnownFolder(home, ".config");
#endif
    }
    if(name == "localappdata" || name == "localappdatalow") {
#if defined(_WIN32)
        if(const std::string value = envOrEmpty("LOCALAPPDATA"); !value.empty())
            return value;
#elif defined(__APPLE__)
        return joinKnownFolder(home, "Library/Caches");
#else
        if(const std::string value = envOrEmpty("XDG_DATA_HOME");
           !value.empty())
            return value;
        return joinKnownFolder(home, ".local/share");
#endif
    }
    if(name == "programdata") {
#if defined(_WIN32)
        return envOrEmpty("PROGRAMDATA");
#else
        return "/usr/local/share";
#endif
    }
    if(name == "fonts") {
#if defined(__APPLE__)
        return joinKnownFolder(home, "Library/Fonts");
#elif defined(_WIN32)
        if(const std::string root = envOrEmpty("WINDIR"); !root.empty())
            return joinKnownFolder(root, "Fonts");
        return {};
#else
        return joinKnownFolder(home, ".local/share/fonts");
#endif
    }
    if(name == "temp" || name == "temporary") {
        if(const std::string value = envOrEmpty("TMPDIR"); !value.empty())
            return value;
#if defined(_WIN32)
        if(const std::string value = envOrEmpty("TEMP"); !value.empty())
            return value;
#endif
        return "/tmp";
    }
    if(name == "windows") {
#if defined(_WIN32)
        return envOrEmpty("WINDIR");
#else
        return {};
#endif
    }
    if(name == "system" || name == "systemx86") {
#if defined(_WIN32)
        if(const std::string root = envOrEmpty("WINDIR"); !root.empty())
            return joinKnownFolder(root, "System32");
        return {};
#elif defined(__APPLE__)
        return "/System";
#else
        return "/usr";
#endif
    }
    return {};
}

std::string knownFolderGuidName(const tTJSVariant &value) {
    if(value.Type() != tvtOctet)
        return {};
    const auto *octet = value.AsOctetNoAddRef();
    if(!octet || octet->GetLength() != 16 || !octet->GetData())
        return {};
    const auto *bytes = octet->GetData();
    struct GuidName {
        std::array<std::uint8_t, 16> bytes;
        const char *name;
    };
    // GUIDs are listed in the same network-order byte form accepted by the
    // upstream getKnownFolderPath implementation.
    static constexpr GuidName known[] = {
        {{{0xB4, 0xBF, 0xCC, 0x3A, 0xDB, 0x2C, 0x42, 0x4C, 0xB0, 0x29,
            0x7F, 0xE9, 0x9A, 0x87, 0xC6, 0x41}}, "Desktop"},
        {{{0xF4, 0xDD, 0x39, 0xAD, 0x23, 0x8F, 0x46, 0xAF, 0xAD, 0xB4,
            0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7}}, "Documents"},
        {{{0x37, 0x4D, 0xE2, 0x90, 0x12, 0x3F, 0x45, 0x65, 0x91, 0x64,
            0x39, 0xC4, 0x92, 0x5E, 0x46, 0x7B}}, "Downloads"},
        {{{0x3E, 0xB6, 0x85, 0xDB, 0x65, 0xF9, 0x4C, 0xF6, 0xA0, 0x3A,
            0xE3, 0xEF, 0x65, 0x72, 0x9F, 0x3D}}, "RoamingAppData"},
        {{{0xF1, 0xB3, 0x27, 0x85, 0x6F, 0xBA, 0x4F, 0xCF, 0x9D, 0x55,
            0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91}}, "LocalAppData"},
    };
    for(const GuidName &entry : known) {
        if(std::equal(entry.bytes.begin(), entry.bytes.end(), bytes))
            return entry.name;
    }
    return {};
}

tjs_error TJS_INTF_METHOD getKnownFolderPathCb(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *) {
    if(numparams < 1 || !param || !param[0])
        return TJS_E_BADPARAMCOUNT;
    if(result)
        result->Clear();

    std::string name;
    if(param[0]->Type() == tvtString)
        name = toUtf8(param[0]->GetString());
    else if(param[0]->Type() == tvtOctet)
        name = knownFolderGuidName(*param[0]);
    else
        return TJS_E_INVALIDPARAM;

    if(name.empty()) {
        logCompatOnce(TJS_W("systemEx.dll"),
                      TJS_W("unknown known-folder identifier"));
        return TJS_S_OK;
    }
    const std::string path = portableKnownFolderPath(name);
    if(result && !path.empty())
        *result = fromUtf8(path);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD processApplicationMessagesCb(tTJSVariant *result,
                                                       tjs_int, tTJSVariant **,
                                                       iTJSDispatch2 *) {
    if(result)
        result->Clear();
    // Application::ProcessMessages drains the bounded host queue and advances
    // timers, which is the portable equivalent of krkrz's message pump.
    if(Application)
        Application->ProcessMessages();
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD handleApplicationMessageCb(tTJSVariant *result,
                                                     tjs_int, tTJSVariant **,
                                                     iTJSDispatch2 *) {
    if(result)
        result->Clear();
    // The host queue has no blocking PeekMessage operation.  Processing one
    // bounded batch is the closest observable equivalent and avoids stalling
    // the script thread in an embedded host.
    if(Application)
        Application->ProcessMessages();
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD setDpiAwarenessCb(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *) {
    if(result)
        result->Clear();
    if(numparams < 1 || !param || !param[0])
        return TJS_E_BADPARAMCOUNT;
    const tjs_int64 requested = static_cast<tjs_int64>(*param[0]);
#if defined(_WIN32)
    using SetDpiAwarenessContextProc = HANDLE(WINAPI *)(HANDLE);
    auto proc = reinterpret_cast<SetDpiAwarenessContextProc>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"),
                         "SetThreadDpiAwarenessContext"));
    if(!proc) {
        logCompatOnce(TJS_W("systemEx.dll"),
                      TJS_W("SetThreadDpiAwarenessContext is unavailable"));
        return TJS_S_OK;
    }
    const tjs_int64 previous = reinterpret_cast<tjs_int64>(proc(
        reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(requested))));
    if(result)
        *result = previous;
#else
    (void)requested;
    logCompatOnce(TJS_W("systemEx.dll"),
                  TJS_W("DPI awareness is owned by the portable host"));
#endif
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD setDefaultDllDirectoriesCb(tTJSVariant *result,
                                                      tjs_int numparams,
                                                      tTJSVariant **param,
                                                      iTJSDispatch2 *) {
    if(result)
        *result = false;
    if(numparams < 1 || !param || !param[0])
        return TJS_E_BADPARAMCOUNT;
#if defined(_WIN32)
    using SetDefaultDllDirectoriesProc = BOOL(WINAPI *)(DWORD);
    auto proc = reinterpret_cast<SetDefaultDllDirectoriesProc>(
        ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"),
                         "SetDefaultDllDirectories"));
    if(proc) {
        const auto flags = static_cast<DWORD>(static_cast<tjs_int64>(*param[0]));
        if(result)
            *result = proc(flags) != FALSE;
    }
#else
    logCompatOnce(TJS_W("systemEx.dll"),
                  TJS_W("DLL search-directory policy is unavailable on this host"));
#endif
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD addDllDirectoryCb(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *) {
    if(result)
        *result = static_cast<tjs_int64>(0);
    if(numparams < 1 || !param || !param[0] ||
       param[0]->Type() != tvtString)
        return TJS_E_BADPARAMCOUNT;
#if defined(_WIN32)
    using AddDllDirectoryProc = PVOID(WINAPI *)(PCWSTR);
    auto proc = reinterpret_cast<AddDllDirectoryProc>(
        ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"),
                         "AddDllDirectory"));
    if(proc) {
        const auto cookie = proc(reinterpret_cast<const wchar_t *>(
            param[0]->GetString()));
        if(result)
            *result = static_cast<tjs_int64>(
                reinterpret_cast<std::intptr_t>(cookie));
    }
#else
    (void)param;
    logCompatOnce(TJS_W("systemEx.dll"),
                  TJS_W("AddDllDirectory is unavailable on this host"));
#endif
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD removeDllDirectoryCb(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *) {
    if(result)
        *result = false;
    if(numparams < 1 || !param || !param[0])
        return TJS_E_BADPARAMCOUNT;
#if defined(_WIN32)
    using RemoveDllDirectoryProc = BOOL(WINAPI *)(PVOID);
    auto proc = reinterpret_cast<RemoveDllDirectoryProc>(
        ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"),
                         "RemoveDllDirectory"));
    if(proc) {
        const auto cookie = reinterpret_cast<PVOID>(static_cast<std::intptr_t>(
            static_cast<tjs_int64>(*param[0])));
        if(result)
            *result = proc(cookie) != FALSE;
    }
#else
    (void)param;
    logCompatOnce(TJS_W("systemEx.dll"),
                  TJS_W("RemoveDllDirectory is unavailable on this host"));
#endif
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD writeEnvValueCb(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *) {
    if(numparams < 2 || !param || !param[0] || !param[1] ||
       param[0]->Type() != tvtString || param[0]->GetString()[0] == 0 ||
       param[1]->Type() != tvtString)
        return TJS_E_BADPARAMCOUNT;
    const std::string name = toUtf8(ttstr(*param[0]));
    const char *oldValue = std::getenv(name.c_str());
    if(result) {
        result->Clear();
        if(oldValue)
            *result = fromUtf8(oldValue);
    }
    const std::string value = toUtf8(ttstr(*param[1]));
#if defined(_WIN32)
    const int rc = _putenv_s(name.c_str(), value.c_str());
#else
    const int rc = setenv(name.c_str(), value.c_str(), 1);
#endif
    if(rc != 0 && result)
        result->Clear();
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD expandEnvStringCb(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *) {
    if(numparams < 1 || !param || !param[0])
        return TJS_E_BADPARAMCOUNT;
    std::string text = toUtf8(ttstr(*param[0]));
    std::string out;
    for(size_t i = 0; i < text.size();) {
        if(text[i] == '%') {
            const size_t end = text.find('%', i + 1);
            if(end != std::string::npos && end > i + 1) {
                const std::string key = text.substr(i + 1, end - i - 1);
                if(const char *value = std::getenv(key.c_str()))
                    out += value;
                else
                    out.append(text, i, end - i + 1);
                i = end + 1;
                continue;
            }
        }
        if(text[i] == '$') {
            size_t keyBegin = i + 1;
            size_t end = i + 1;
            if(keyBegin < text.size() && text[keyBegin] == '{') {
                keyBegin++;
                end = text.find('}', keyBegin);
                if(end == std::string::npos || end == keyBegin) {
                    out += text[i++];
                    continue;
                }
                const std::string key = text.substr(keyBegin, end - keyBegin);
                if(const char *value = std::getenv(key.c_str()))
                    out += value;
                else
                    out.append(text, i, end - i + 1);
                i = end + 1;
                continue;
            }
            while(end < text.size() &&
                  (std::isalnum(static_cast<unsigned char>(text[end])) ||
                   text[end] == '_'))
                ++end;
            if(end > keyBegin) {
                const std::string key = text.substr(keyBegin, end - keyBegin);
                if(const char *value = std::getenv(key.c_str()))
                    out += value;
                else
                    out.append(text, i, end - i);
                i = end;
                continue;
            }
        }
        out += text[i++];
    }
    if(result)
        *result = fromUtf8(out);
    return TJS_S_OK;
}

bool isUrlSafe(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

bool urlUtf8Mode(tjs_int numparams, tTJSVariant **param) {
    return numparams < 2 || !param || !param[1] ||
        static_cast<tjs_int>(*param[1]) != 0;
}

tjs_error TJS_INTF_METHOD urlencodeCb(tTJSVariant *result, tjs_int numparams,
                                      tTJSVariant **param, iTJSDispatch2 *) {
    if(numparams < 1 || !param || !param[0] ||
       param[0]->Type() != tvtString)
        return TJS_E_BADPARAMCOUNT;
    const std::string bytes = urlUtf8Mode(numparams, param)
        ? toUtf8(ttstr(*param[0]))
        : ttstr(*param[0]).AsNarrowStdString();
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
    if(numparams < 1 || !param || !param[0] ||
       param[0]->Type() != tvtString)
        return TJS_E_BADPARAMCOUNT;
    const ttstr source(*param[0]);
    const bool utf8 = urlUtf8Mode(numparams, param);
    const std::string text = utf8 ? toUtf8(source) : source.AsNarrowStdString();
    std::string out;
    for(size_t i = 0; i < text.size(); ++i) {
        if(text[i] == '%') {
            if(i + 2 >= text.size())
                return TJS_E_INVALIDPARAM;
            const int hi = fromHex(text[i + 1]);
            const int lo = fromHex(text[i + 2]);
            if(hi < 0 || lo < 0)
                return TJS_E_INVALIDPARAM;
            out.push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
            continue;
        }
        // Unlike application/x-www-form-urlencoded, krkrz's helper leaves
        // '+' untouched.  Games that need a space encode it as %20.
        out.push_back(text[i]);
    }
    if(result) {
        if(utf8) {
            // Reject malformed UTF-8 instead of silently returning an empty
            // string, which is what the old helper did for a bad byte stream.
            if(!out.empty() && TVPUtf8ToWideCharString(
                   out.data(), static_cast<tjs_uint>(out.size()),
                   static_cast<tjs_char *>(nullptr)) <= 0)
                return TJS_E_INVALIDPARAM;
            *result = fromUtf8(out);
        } else {
            *result = ttstr(out.c_str());
        }
    }
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

void registerSystemExConstants() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;
    tTJSVariant systemValue;
    if(TJS_FAILED(global->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("System"),
                                  nullptr, &systemValue, global)) ||
       systemValue.Type() != tvtObject || !systemValue.AsObjectNoAddRef()) {
        global->Release();
        return;
    }
    iTJSDispatch2 *system = systemValue.AsObjectNoAddRef();
    struct Constant {
        const tjs_char *name;
        tjs_int64 value;
    };
    static constexpr Constant constants[] = {
        {TJS_W("dacUnaware"), -1},
        {TJS_W("dacSystemAware"), -2},
        {TJS_W("dacPerMonitorAware"), -3},
        {TJS_W("dacPerMonitorAwareV2"), -4},
        {TJS_W("dacUnawareGdiScaled"), -5},
        {TJS_W("llsApplicationDir"), 0x00000200},
        {TJS_W("llsDefaultDirs"), 0x00001000},
        {TJS_W("llsSystem32"), 0x00000800},
        {TJS_W("llsUserDirs"), 0x00000400},
    };
    for(const Constant &constant : constants) {
        tTJSVariant existing;
        if(TJS_SUCCEEDED(system->PropGet(TJS_MEMBERMUSTEXIST, constant.name,
                                          nullptr, &existing, system)))
            continue;
        tTJSVariant value(constant.value);
        system->PropSet(TJS_MEMBERENSURE, constant.name, nullptr, &value,
                        system);
    }
    global->Release();
}
} // namespace

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("systemEx.dll")
NCB_PRE_REGIST_CALLBACK(registerSystemExConstants);
NCB_ATTACH_FUNCTION(writeRegValue, System, writeRegValueCb);
NCB_ATTACH_FUNCTION(readRegValue, System, readRegValueCb);
NCB_ATTACH_FUNCTION(readEnvValue, System, readEnvValueCb);
NCB_ATTACH_FUNCTION(writeEnvValue, System, writeEnvValueCb);
NCB_ATTACH_FUNCTION(expandEnvString, System, expandEnvStringCb);
NCB_ATTACH_FUNCTION(urlencode, System, urlencodeCb);
NCB_ATTACH_FUNCTION(urldecode, System, urldecodeCb);
NCB_ATTACH_FUNCTION(getOSVersion, System, getOSVersionCb);
NCB_ATTACH_FUNCTION(getKnownFolderPath, System, getKnownFolderPathCb);
NCB_ATTACH_FUNCTION(processApplicationMessages, System,
                    processApplicationMessagesCb);
NCB_ATTACH_FUNCTION(handleApplicationMessage, System,
                    handleApplicationMessageCb);
NCB_ATTACH_FUNCTION(setDpiAwareness, System, setDpiAwarenessCb);
NCB_ATTACH_FUNCTION(setDefaultDllDirectories, System,
                    setDefaultDllDirectoriesCb);
NCB_ATTACH_FUNCTION(addDllDirectory, System, addDllDirectoryCb);
NCB_ATTACH_FUNCTION(removeDllDirectory, System, removeDllDirectoryCb);
NCB_ATTACH_FUNCTION(confirm, System, confirmCb);
NCB_ATTACH_FUNCTION(waitForAppLock, System, waitForAppLockCb);
NCB_ATTACH_FUNCTION(getCompatibilityCapabilities, System,
                    getCompatibilityCapabilitiesCb);
NCB_ATTACH_FUNCTION(getCompatibilityCapability, System,
                    getCompatibilityCapabilityCb);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("registory.dll")
NCB_ATTACH_FUNCTION_WITHTAG(writeRegValue, RegistryCompatWrite, System,
                            writeRegValueCb);
NCB_ATTACH_FUNCTION_WITHTAG(readRegValue, RegistryCompatRead, System,
                            readRegValueCb);
NCB_ATTACH_FUNCTION_WITHTAG(deleteRegValue, RegistryCompatDeleteValue, System,
                            deleteRegValueCb);
NCB_ATTACH_FUNCTION_WITHTAG(writeDeleteValue, RegistryCompatWriteDeleteValue,
                            System, deleteRegValueCb);
NCB_ATTACH_FUNCTION_WITHTAG(deleteRegKey, RegistryCompatDeleteKey, System,
                            deleteRegKeyCb);

// -------------------------------------------------------------------------
// stdio.dll
// Portable stdio bridge.  A console is a descriptor/stream capability on
// POSIX hosts rather than a Win32 console object; preserve the krkrz state
// bits and make missing descriptors an explicit failure.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("stdio.dll")

namespace {
std::atomic<tjs_int> g_stdioState{0};
}

class Stdio {
public:
    static tjs_int getState() {
        tjs_int state = 0;
#if defined(_WIN32)
        if(_fileno(stdin) >= 0)
            state |= 0x01;
        if(_fileno(stdout) >= 0)
            state |= 0x02;
        if(_fileno(stderr) >= 0)
            state |= 0x04;
#else
        if(::fileno(stdin) >= 0)
            state |= 0x01;
        if(::fileno(stdout) >= 0)
            state |= 0x02;
        if(::fileno(stderr) >= 0)
            state |= 0x04;
#endif
        return state | g_stdioState.load(std::memory_order_relaxed);
    }

    bool attachConsole(tjs_int bind = 0) {
        const tjs_int available = getState();
        const tjs_int requested = bind == 0 ? ((~available) & 0x07) : bind;
        if(requested == 0)
            return true;
#if defined(_WIN32)
        // The public Aether build does not compile the Win32 console backend;
        // descriptors already attached by the host are still reported above.
        return false;
#else
        logCompatOnce(TJS_W("stdio.dll"),
                      TJS_W("attachConsole cannot attach a missing POSIX descriptor"));
        return false;
#endif
    }

    bool allocConsole(tjs_int bind = 0) {
        const tjs_int available = getState();
        const tjs_int requested = bind == 0 ? ((~available) & 0x07) : bind;
        if(requested == 0)
            return true;
#if defined(_WIN32)
        return false;
#else
        // A detached command-line process can still have a controlling tty.
        // Reconnect only the explicitly requested missing streams and leave
        // inherited descriptors untouched.
        int tty = ::open("/dev/tty", O_RDWR);
        if(tty < 0) {
            logCompatOnce(TJS_W("stdio.dll"),
                          TJS_W("allocConsole could not open /dev/tty"));
            return false;
        }
        bool ok = true;
        if((requested & 0x01) && ::dup2(tty, STDIN_FILENO) < 0)
            ok = false;
        if((requested & 0x02) && ::dup2(tty, STDOUT_FILENO) < 0)
            ok = false;
        if((requested & 0x04) && ::dup2(tty, STDERR_FILENO) < 0)
            ok = false;
        if(tty > STDERR_FILENO)
            ::close(tty);
        if(ok)
            g_stdioState.fetch_or(requested, std::memory_order_relaxed);
        return ok;
#endif
    }

    bool freeConsole() {
        flush();
        g_stdioState.store(0, std::memory_order_relaxed);
        // Do not close descriptors owned by the embedding process. This is
        // the only safe portable equivalent of detaching a console.
        return true;
    }

    ttstr stdinRead(bool = false) {
        std::string line;
        if(!std::getline(std::cin, line))
            return ttstr();
        return fromUtf8(line);
    }
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
        std::cout.flush();
        std::cerr.flush();
    }
};

NCB_ATTACH_CLASS(Stdio, System) {
    NCB_PROPERTY_RO(stdioState, getState);
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
// Keep the upstream fpslimit contract (0 means the default 1000) while
// avoiding an unsynchronised read if a host changes the property from an
// embedding thread.  The callback itself always runs on the engine loop.
std::atomic<int> g_fpsLimit{1000};

class FpsLimitHook final : public tTVPContinuousEventCallbackIntf {
public:
    void reset() { previousTick_ = TVPGetTickCount(); }

    void OnContinuousCallback(tjs_uint64 tick) override {
        const int limit = g_fpsLimit.load(std::memory_order_relaxed);
        if(limit <= 0) {
            previousTick_ = tick;
            return;
        }

        // The original plug-in uses integer milliseconds.  Preserve that
        // behaviour, but clamp the minimum interval to one millisecond so a
        // malformed/very high value cannot cause a divide-by-zero loop.
        const tjs_uint64 interval =
            limit > 1000 ? 0 : std::max<tjs_uint64>(1, 1000 / limit);
        if(interval != 0 && previousTick_ != 0 &&
           tick < previousTick_ + interval) {
            const tjs_uint64 remaining = previousTick_ + interval - tick;
            std::this_thread::sleep_for(std::chrono::milliseconds(
                static_cast<unsigned long long>(remaining)));
        }
        previousTick_ = TVPGetTickCount();
    }

private:
    tjs_uint64 previousTick_ = 0;
};

FpsLimitHook g_fpsLimitHook;
bool g_fpsLimitHookRegistered = false;

void registerFpsLimitHook() {
    if(g_fpsLimitHookRegistered)
        return;
    TVPStartTickCount();
    g_fpsLimitHook.reset();
    TVPAddContinuousEventHook(&g_fpsLimitHook);
    g_fpsLimitHookRegistered = true;
}

void unregisterFpsLimitHook() {
    if(!g_fpsLimitHookRegistered)
        return;
    TVPRemoveContinuousEventHook(&g_fpsLimitHook);
    g_fpsLimitHookRegistered = false;
}

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
    // tTVPScreen is backed by the same host surface used by the active
    // Window/DrawDevice. Returning its real bounds is important for games
    // that use adjustMonitor to place a borderless overlay; the old adapter
    // returned four zeroes and collapsed every layout to the origin.
    const tjs_int width = std::max(0, tTVPScreen::GetWidth());
    const tjs_int height = std::max(0, tTVPScreen::GetHeight());
    const tjs_int desktopLeft = tTVPScreen::GetDesktopLeft();
    const tjs_int desktopTop = tTVPScreen::GetDesktopTop();
    setDict(dict, TJS_W("x"), tTJSVariant(x));
    setDict(dict, TJS_W("y"), tTJSVariant(y));
    setDict(dict, TJS_W("left"), tTJSVariant(desktopLeft));
    setDict(dict, TJS_W("top"), tTJSVariant(desktopTop));
    setDict(dict, TJS_W("right"), tTJSVariant(desktopLeft + width));
    setDict(dict, TJS_W("bottom"), tTJSVariant(desktopTop + height));
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
    tjs_int getFpsLimit() const {
        return static_cast<tjs_int>(g_fpsLimit.load(std::memory_order_relaxed));
    }
    void setFpsLimit(tjs_int value) {
        g_fpsLimit.store(value > 0 ? value : 1000,
                         std::memory_order_relaxed);
    }
};

NCB_ATTACH_CLASS(SystemFpsLimitCompat, System) {
    NCB_PROPERTY(fpslimit, getFpsLimit, setFpsLimit);
}

NCB_PRE_REGIST_CALLBACK(registerFpsLimitHook);
NCB_POST_UNREGIST_CALLBACK(unregisterFpsLimitHook);

// -------------------------------------------------------------------------
// httprequest.dll and xmlhttprequest.dll
// AETHERKIRI_COMPAT_STUB: portable Storage/curl-backed request surface.
// Requests use the same ready-state contract as krkrz. HTTP work is moved off
// the TJS thread for asynchronous calls and completion is marshalled through
// TVP's event queue.
// -------------------------------------------------------------------------

class SimpleRequestState {
public:
    explicit SimpleRequestState(iTJSDispatch2 *owner = nullptr) : owner_(owner) {
        if(owner_)
            owner_->AddRef();
    }

    virtual ~SimpleRequestState() {
        abort();
        joinWorker();
        if(owner_) {
            TVPCancelSourceEvents(owner_);
            owner_->Release();
            owner_ = nullptr;
        }
    }

    void openRequest(const ttstr &method, const ttstr &url, bool async = false) {
        cancelRequest(false);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            method_ = method;
            url_ = url;
            async_ = async;
            readyState_ = kReadyOpen;
            status_ = 0;
            statusText_.Clear();
            headers_.Clear();
            responseBytes_.clear();
            requestHeaders_.clear();
            pendingCompletion_ = false;
        }
        postReadyState(kReadyOpen);
    }

    void setHeader(const ttstr &name, const ttstr &value) {
        std::lock_guard<std::mutex> lock(mutex_);
        requestHeaders_[toUtf8(name)] = value;
    }

    void sendRequest(const tTJSVariant *data = nullptr,
                     const ttstr &sendStorage = ttstr(),
                     const ttstr &saveStorage = ttstr(),
                     bool asynchronous = false) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(pendingCompletion_ || readyState_ == kReadySent ||
               readyState_ == kReadyReceiving)
                TVPThrowExceptionMessage(TJS_W("already running"));
            if(readyState_ != kReadyOpen)
                TVPThrowExceptionMessage(TJS_W("not open"));
        }
        joinWorker();
        std::string requestBody = variantBytes(data);
        if(!data && !sendStorage.IsEmpty() &&
           !readStorageBytes(sendStorage, requestBody))
            TVPThrowExceptionMessage(TJS_W("sendStorage open failed"));

        RequestJob job;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            job.id = ++requestId_;
            job.method = method_;
            job.url = url_;
            job.headers = requestHeaders_;
            job.body = std::move(requestBody);
            job.saveStorage = saveStorage;
            cancel_.store(false, std::memory_order_release);
            readyState_ = kReadySent;
            pendingCompletion_ = asynchronous;
        }
        postReadyState(kReadySent);
        if(asynchronous)
            worker_ = std::thread([this, job] { runAsync(job); });
        else
            runSync(job);
    }

    void abort() { cancelRequest(true); }

    tjs_int getReadyState() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return readyState_;
    }
    tjs_int getStatus() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }
    ttstr getStatusText() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return statusText_;
    }
    ttstr getResponseText() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return fromUtf8(responseBytes_);
    }
    tTJSVariant getResponseData() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(responseBytes_.empty() ||
           responseBytes_.size() > std::numeric_limits<tjs_uint>::max())
            return tTJSVariant();
        return tTJSVariant(reinterpret_cast<const tjs_uint8 *>(
                               responseBytes_.data()),
                           static_cast<tjs_uint>(responseBytes_.size()));
    }
    ttstr getAllResponseHeaders() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return headers_;
    }
    ttstr getResponseHeader(const tjs_char *name) const {
        if(!name)
            return ttstr();
        std::lock_guard<std::mutex> lock(mutex_);
        std::string needle = toUtf8(ttstr(name));
        for(char &c : needle)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::istringstream in(toUtf8(headers_));
        std::string line;
        while(std::getline(in, line)) {
            const size_t colon = line.find(':');
            if(colon == std::string::npos)
                continue;
            std::string key = line.substr(0, colon);
            for(char &c : key)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if(key != needle)
                continue;
            std::string value = line.substr(colon + 1);
            while(!value.empty() &&
                  (value.front() == ' ' || value.front() == '\t'))
                value.erase(value.begin());
            while(!value.empty() &&
                  (value.back() == '\r' || value.back() == '\n'))
                value.pop_back();
            return fromUtf8(value);
        }
        return ttstr();
    }
    ttstr getContentType() const { return getResponseHeader(TJS_W("Content-Type")); }
    ttstr getContentTypeEncoding() const {
        std::string value = toUtf8(getContentType());
        for(char &c : value)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const size_t charset = value.find("charset=");
        if(charset == std::string::npos)
            return ttstr();
        size_t begin = charset + 8;
        while(begin < value.size() &&
              (value[begin] == ' ' || value[begin] == '\t' ||
               value[begin] == '\'' || value[begin] == '"'))
            ++begin;
        size_t end = begin;
        while(end < value.size() && value[end] != ';' &&
              value[end] != ' ' && value[end] != '\t' &&
              value[end] != '\'' && value[end] != '"')
            ++end;
        return fromUtf8(value.substr(begin, end - begin));
    }
    tjs_int getContentLength() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return responseBytes_.size() >
                static_cast<size_t>(std::numeric_limits<tjs_int>::max())
            ? std::numeric_limits<tjs_int>::max()
            : static_cast<tjs_int>(responseBytes_.size());
    }
    bool getAsync() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return async_;
    }
    ttstr getRequestHeaders() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string result;
        for(const auto &header : requestHeaders_) {
            result += header.first;
            result += ": ";
            result += toUtf8(header.second);
            result += "\r\n";
        }
        return fromUtf8(result);
    }

    tjs_error completeAsync(tjs_int64 id) {
        if(id < 0)
            return TJS_S_OK;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(static_cast<std::uint64_t>(id) != requestId_)
                return TJS_S_OK;
        }
        joinWorker();
        finalizeCompletion(static_cast<std::uint64_t>(id));
        return TJS_S_OK;
    }

protected:
    virtual void postProgress(bool, tjs_real) {}

    iTJSDispatch2 *eventOwner() const { return owner_; }

    void postReadyState(tjs_int state) {
        if(!owner_)
            return;
        tTJSVariant value(state);
        static ttstr eventName(TJS_W("onReadyStateChange"));
        TVPPostEvent(owner_, owner_, eventName, 0, TVP_EPT_POST, 1, &value);
    }

private:
    struct RequestJob {
        std::uint64_t id = 0;
        ttstr method;
        ttstr url;
        std::map<std::string, ttstr> headers;
        std::string body;
        ttstr saveStorage;
    };

    void runSync(const RequestJob &job) {
        FetchResult fetched = fetchUrlOrStorage(job.url, job.method, job.headers,
                                                job.body, &cancel_);
        storeResult(job, std::move(fetched));
        finalizeCompletion(job.id);
    }

    void runAsync(const RequestJob job) {
        FetchResult fetched = fetchUrlOrStorage(job.url, job.method, job.headers,
                                                job.body, &cancel_);
        if(cancel_.load(std::memory_order_acquire))
            return;
        storeResult(job, std::move(fetched));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(job.id != requestId_ || cancel_.load(std::memory_order_acquire))
                return;
        }
        tTJSVariant id(static_cast<tjs_int64>(job.id));
        static ttstr eventName(TJS_W("__aether_http_complete"));
        if(owner_)
            TVPPostEvent(owner_, owner_, eventName, 0, TVP_EPT_POST, 1, &id);
    }

    void storeResult(const RequestJob &job, FetchResult fetched) {
        if(!job.saveStorage.IsEmpty() &&
           !writeStorageBytes(job.saveStorage, fetched.body))
            logCompatOnce(TJS_W("httprequest.dll"),
                          TJS_W("response saveStorage could not be written"));
        std::lock_guard<std::mutex> lock(mutex_);
        if(job.id != requestId_ || cancel_.load(std::memory_order_acquire))
            return;
        status_ = fetched.status;
        statusText_ = fetched.statusText;
        headers_ = fetched.headers;
        responseBytes_ = std::move(fetched.body);
    }

    void finalizeCompletion(std::uint64_t id) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(id != requestId_ || cancel_.load(std::memory_order_acquire))
                return;
            pendingCompletion_ = false;
            readyState_ = kReadyReceiving;
        }
        postReadyState(kReadyReceiving);
        postProgress(false, 100.0);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(id != requestId_ || cancel_.load(std::memory_order_acquire))
                return;
            readyState_ = kReadyLoaded;
        }
        postReadyState(kReadyLoaded);
    }

    void cancelRequest(bool notify) {
        cancel_.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++requestId_;
            pendingCompletion_ = false;
            readyState_ = kReadyUninitialized;
            status_ = notify ? -1 : 0;
            statusText_.Clear();
            headers_.Clear();
            responseBytes_.clear();
        }
        static ttstr eventName(TJS_W("__aether_http_complete"));
        if(owner_)
            TVPCancelEvents(owner_, owner_, eventName, 0);
        joinWorker();
    }

    void joinWorker() {
        if(worker_.joinable() &&
           worker_.get_id() != std::this_thread::get_id())
            worker_.join();
    }

    mutable std::mutex mutex_;
    ttstr method_;
    ttstr url_;
    bool async_ = false;
    tjs_int readyState_ = kReadyUninitialized;
    tjs_int status_ = 0;
    ttstr statusText_;
    ttstr headers_;
    std::string responseBytes_;
    std::map<std::string, ttstr> requestHeaders_;
    std::uint64_t requestId_ = 0;
    bool pendingCompletion_ = false;
    std::atomic<bool> cancel_{false};
    std::thread worker_;
    iTJSDispatch2 *owner_ = nullptr;
};

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("httprequest.dll")

class HttpRequestCompat : public SimpleRequestState {
public:
    explicit HttpRequestCompat(iTJSDispatch2 *owner) : SimpleRequestState(owner) {}

    static tjs_error TJS_INTF_METHOD factory(HttpRequestCompat **result,
                                             tjs_int, tTJSVariant **,
                                             iTJSDispatch2 *objthis) {
        if(!result)
            return TJS_E_FAIL;
        *result = new HttpRequestCompat(objthis);
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
        self->sendRequest(numparams > 0 ? param[0] : nullptr, ttstr(),
                          paramString(1, numparams, param), true);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD sendSyncCb(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                HttpRequestCompat *self) {
        if(!self)
            return TJS_E_FAIL;
        self->sendRequest(numparams > 0 ? param[0] : nullptr, ttstr(),
                          paramString(1, numparams, param), false);
        if(result)
            *result = self->getStatus();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD sendStorageCb(tTJSVariant *,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   HttpRequestCompat *self) {
        if(!self)
            return TJS_E_FAIL;
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        self->sendRequest(nullptr, paramString(0, numparams, param),
                          paramString(1, numparams, param), true);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD sendStorageSyncCb(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
        HttpRequestCompat *self) {
        if(!self)
            return TJS_E_FAIL;
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        self->sendRequest(nullptr, paramString(0, numparams, param),
                          paramString(1, numparams, param), false);
        if(result)
            *result = self->getStatus();
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

    // krkrz exposes these helpers as static HttpRequest methods.  They are
    // intentionally implemented at this adapter boundary instead of pulling
    // the Win32-only Base64.cpp into the process: the wire format is
    // platform-neutral, while Aether's octet/string ownership remains the
    // single ABI owner.  The decoder is strict about alphabet/padding and
    // bounded to the same size class as HTTP bodies so a malformed script
    // cannot trigger an unbounded allocation.
    static tjs_error TJS_INTF_METHOD encodeBase64Cb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        iTJSDispatch2 *) {
        constexpr std::size_t kMaxBytes = 64u * 1024u * 1024u;
        if(count < 1 || !params || !params[0])
            return TJS_E_BADPARAMCOUNT;
        std::vector<std::uint8_t> bytes;
        if(params[0]->Type() == tvtOctet) {
            const auto *octet = params[0]->AsOctetNoAddRef();
            if(!octet || octet->GetLength() > kMaxBytes ||
               (octet->GetLength() > 0 && !octet->GetData()))
                return TJS_E_INVALIDPARAM;
            if(octet->GetLength() > 0)
                bytes.assign(octet->GetData(),
                             octet->GetData() + octet->GetLength());
        } else if(params[0]->Type() == tvtString) {
            const ttstr text(*params[0]);
            const bool narrow = count > 1 && params[1] &&
                ((params[1]->Type() == tvtInteger &&
                  static_cast<tjs_int64>(*params[1]) == 0) ||
                 (params[1]->Type() == tvtString &&
                  ttstr(*params[1]) == TJS_W("ACP")));
            std::string encoded;
            if(narrow) {
                const tjs_int length = text.GetNarrowStrLen();
                if(length < 0 || static_cast<std::size_t>(length) > kMaxBytes)
                    return TJS_E_INVALIDPARAM;
                encoded.resize(static_cast<std::size_t>(length));
                if(length > 0)
                    text.ToNarrowStr(encoded.data(), length);
            } else {
                const tjs_int length = TVPWideCharToUtf8String(
                    text.c_str(), nullptr);
                if(length < 0 || static_cast<std::size_t>(length) > kMaxBytes)
                    return TJS_E_INVALIDPARAM;
                encoded.resize(static_cast<std::size_t>(length));
                if(length > 0 && TVPWideCharToUtf8String(text.c_str(),
                                                          encoded.data()) < 0)
                    return TJS_E_INVALIDPARAM;
            }
            bytes.assign(encoded.begin(), encoded.end());
        } else {
            return TJS_E_INVALIDPARAM;
        }

        static constexpr char kAlphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        if(bytes.size() > (std::numeric_limits<std::size_t>::max() - 2) / 4 * 3)
            return TJS_E_INVALIDPARAM;
        std::string encoded;
        encoded.reserve(((bytes.size() + 2) / 3) * 4);
        for(std::size_t offset = 0; offset < bytes.size(); offset += 3) {
            const std::size_t remaining = std::min<std::size_t>(3,
                                                                 bytes.size() - offset);
            const std::uint32_t a = bytes[offset];
            const std::uint32_t b = remaining > 1 ? bytes[offset + 1] : 0;
            const std::uint32_t c = remaining > 2 ? bytes[offset + 2] : 0;
            encoded.push_back(kAlphabet[(a >> 2) & 0x3f]);
            encoded.push_back(kAlphabet[((a & 0x03) << 4) | ((b >> 4) & 0x0f)]);
            encoded.push_back(remaining > 1
                                 ? kAlphabet[((b & 0x0f) << 2) | ((c >> 6) & 0x03)]
                                 : '=');
            encoded.push_back(remaining > 2 ? kAlphabet[c & 0x3f] : '=');
        }
        if(result)
            *result = fromUtf8(encoded);
        return TJS_S_OK;
    }

    static int base64Value(unsigned char value) {
        if(value >= 'A' && value <= 'Z')
            return value - 'A';
        if(value >= 'a' && value <= 'z')
            return value - 'a' + 26;
        if(value >= '0' && value <= '9')
            return value - '0' + 52;
        if(value == '+')
            return 62;
        if(value == '/')
            return 63;
        return -1;
    }

    static bool decodeBase64Bytes(const std::uint8_t *input, std::size_t length,
                                  std::vector<std::uint8_t> &output) {
        constexpr std::size_t kMaxBytes = 64u * 1024u * 1024u;
        output.clear();
        if(length > kMaxBytes * 2 + 16)
            return false;
        std::string compact;
        compact.reserve(length);
        for(std::size_t i = 0; i < length; ++i) {
            const unsigned char c = input[i];
            if(c == ' ' || c == '\t' || c == '\r' || c == '\n')
                continue;
            compact.push_back(static_cast<char>(c));
        }
        if(compact.empty())
            return true;
        if((compact.size() & 3u) != 0 ||
           compact.size() / 4u > (kMaxBytes + 2u) / 3u)
            return false;
        output.reserve((compact.size() / 4u) * 3u);
        for(std::size_t i = 0; i < compact.size(); i += 4) {
            const unsigned char c0 = static_cast<unsigned char>(compact[i]);
            const unsigned char c1 = static_cast<unsigned char>(compact[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(compact[i + 2]);
            const unsigned char c3 = static_cast<unsigned char>(compact[i + 3]);
            const int v0 = base64Value(c0);
            const int v1 = base64Value(c1);
            if(v0 < 0 || v1 < 0)
                return false;
            const bool pad2 = c2 == '=';
            const bool pad3 = c3 == '=';
            if(pad2 && !pad3)
                return false;
            const int v2 = pad2 ? 0 : base64Value(c2);
            const int v3 = pad3 ? 0 : base64Value(c3);
            if(v2 < 0 || v3 < 0 ||
               ((pad2 || pad3) && i + 4 != compact.size()))
                return false;
            if(pad2 && (v1 & 0x0f) != 0)
                return false;
            if(pad3 && !pad2 && (v2 & 0x03) != 0)
                return false;
            output.push_back(static_cast<std::uint8_t>((v0 << 2) | (v1 >> 4)));
            if(!pad2)
                output.push_back(static_cast<std::uint8_t>((v1 << 4) | (v2 >> 2)));
            if(!pad3)
                output.push_back(static_cast<std::uint8_t>((v2 << 6) | v3));
            if(output.size() > kMaxBytes)
                return false;
        }
        return true;
    }

    static tjs_error TJS_INTF_METHOD decodeBase64Cb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        iTJSDispatch2 *) {
        if(count < 1 || !params || !params[0])
            return TJS_E_BADPARAMCOUNT;
        const std::uint8_t *data = nullptr;
        std::size_t length = 0;
        std::string text;
        if(params[0]->Type() == tvtOctet) {
            const auto *octet = params[0]->AsOctetNoAddRef();
            if(!octet || (octet->GetLength() > 0 && !octet->GetData()))
                return TJS_E_INVALIDPARAM;
            data = octet->GetData();
            length = octet->GetLength();
        } else if(params[0]->Type() == tvtString) {
            text = toUtf8(ttstr(*params[0]));
            data = reinterpret_cast<const std::uint8_t *>(text.data());
            length = text.size();
        } else {
            return TJS_E_INVALIDPARAM;
        }
        std::vector<std::uint8_t> decoded;
        if(!decodeBase64Bytes(data, length, decoded))
            return TJS_E_INVALIDPARAM;
        if(result) {
            if(decoded.empty())
                result->Clear();
            else
                *result = tTJSVariant(decoded.data(),
                                      static_cast<tjs_uint>(decoded.size()));
        }
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD completeCb(tTJSVariant *, tjs_int n,
                                                tTJSVariant **p,
                                                HttpRequestCompat *self) {
        if(!self || n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;
        return self->completeAsync(static_cast<tjs_int64>(*p[0]));
    }

protected:
    void postProgress(bool upload, tjs_real percent) override {
        if(!ownerForEvents())
            return;
        tTJSVariant params[2];
        params[0] = upload;
        params[1] = percent;
        static ttstr eventName(TJS_W("onProgress"));
        TVPPostEvent(ownerForEvents(), ownerForEvents(), eventName, 0,
                     TVP_EPT_POST, 2, params);
    }

private:
    iTJSDispatch2 *ownerForEvents() const { return eventOwner(); }
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
    NCB_METHOD_RAW_CALLBACK(sendSync, &HttpRequestCompat::sendSyncCb, 0);
    NCB_METHOD_RAW_CALLBACK(sendStorage, &HttpRequestCompat::sendStorageCb, 0);
    NCB_METHOD_RAW_CALLBACK(sendStorageSync,
                            &HttpRequestCompat::sendStorageSyncCb, 0);
    NCB_METHOD_RAW_CALLBACK(__aether_http_complete,
                            &HttpRequestCompat::completeCb, 0);
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
    RawCallback(TJS_W("encodeBase64"), &Class::encodeBase64Cb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("decodeBase64"), &Class::decodeBase64Cb,
                TJS_STATICMEMBER);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("xmlhttprequest.dll")

class XMLHttpRequestCompat : public SimpleRequestState {
public:
    explicit XMLHttpRequestCompat(iTJSDispatch2 *owner)
        : SimpleRequestState(owner) {}

    static tjs_error TJS_INTF_METHOD factory(XMLHttpRequestCompat **result,
                                             tjs_int, tTJSVariant **,
                                             iTJSDispatch2 *objthis) {
        if(!result)
            return TJS_E_FAIL;
        *result = new XMLHttpRequestCompat(objthis);
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

    static tjs_error TJS_INTF_METHOD sendCb(tTJSVariant *, tjs_int numparams,
                                            tTJSVariant **param,
                                            XMLHttpRequestCompat *self) {
        if(!self)
            return TJS_E_FAIL;
        self->sendRequest(numparams > 0 ? param[0] : nullptr, ttstr(), ttstr(),
                          self->getAsync());
        return TJS_S_OK;
    }

    void setRequestHeader(const tjs_char *name, const tjs_char *value) {
        setHeader(name ? ttstr(name) : ttstr(), value ? ttstr(value) : ttstr());
    }
    ttstr printRequestHeaders() const { return getRequestHeaders(); }
    ttstr getResponseHeader(const tjs_char *name) const {
        return SimpleRequestState::getResponseHeader(name);
    }
    void abort() { SimpleRequestState::abort(); }
    void executeCallback() {
        const tjs_int state = getReadyState();
        if(state != kReadyUninitialized)
            postReadyState(state);
    }
    ttstr getResponseText() const { return SimpleRequestState::getResponseText(); }

    static tjs_error TJS_INTF_METHOD completeCb(tTJSVariant *, tjs_int n,
                                                tTJSVariant **p,
                                                XMLHttpRequestCompat *self) {
        if(!self || n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;
        return self->completeAsync(static_cast<tjs_int64>(*p[0]));
    }
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
    NCB_METHOD_RAW_CALLBACK(__aether_http_complete,
                            &XMLHttpRequestCompat::completeCb, 0);
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

tjs_error TJS_INTF_METHOD unsupportedFalseCb(tTJSVariant *result, tjs_int,
                                             tTJSVariant **, iTJSDispatch2 *) {
    logCompatOnce(TJS_W("script-vm"),
                  TJS_W("requested external script-VM operation is unavailable"));
    if(result)
        *result = false;
    return TJS_S_OK;
}
} // namespace

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("javascript.dll")
NCB_ATTACH_FUNCTION(execJS, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(execStorageJS, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(enableDebugJS, Scripts, unsupportedFalseCb);
NCB_ATTACH_FUNCTION(processDebugJS, Scripts, unsupportedFalseCb);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("squirrel.dll")
NCB_ATTACH_FUNCTION(loadSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(execSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(loadStorageSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(execStorageSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(callSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(compileSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(compileStorageSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(saveSQ, Scripts, unsupportedFalseCb);
NCB_ATTACH_FUNCTION(toSQString, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(registerSQ, Scripts, unsupportedFalseCb);
NCB_ATTACH_FUNCTION(unregisterSQ, Scripts, unsupportedFalseCb);
NCB_ATTACH_FUNCTION(forkSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(forkStorageSQ, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(driveSQ, Scripts, unsupportedFalseCb);
NCB_ATTACH_FUNCTION(triggerSQ, Scripts, unsupportedFalseCb);
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

// messenger.dll, msgreceiver.dll and tasktray.dll are implemented by
// portableWindowMessaging.cpp. They share the host Window receiver chain and
// therefore must not also register the old no-op methods here.

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("sigcheck.dll")

class WindowSigCheckCompat {
public:
    tjs_int checkSignature(const tjs_char *filename, const tjs_char *publicKey,
                           tTJSVariant info = tTJSVariant()) {
        return checker_ ? checker_->checkSignature(filename, publicKey, info)
                        : -1;
    }
    bool cancelCheckSignature(tjs_int handler) {
        return checker_ && checker_->cancelCheckSignature(handler);
    }
    bool stopCheckSignature(tjs_int handler) {
        return checker_ && checker_->stopCheckSignature(handler);
    }
    void setOwner(iTJSDispatch2 *owner) {
        if(checker_)
            return;
        checker_ = std::make_unique<AetherKiri::PortableSignatureCheck>(owner);
    }

private:
    std::unique_ptr<AetherKiri::PortableSignatureCheck> checker_;
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
// -------------------------------------------------------------------------
// The pinned krkrz implementation edits PE resources through Win32.  That is
// not a usable contract on the other Aether targets, so the adapter keeps the
// same TJS surface and persists resources in a deterministic sidecar.  A
// native PE editor can still be supplied by a host in the future; the TJS
// contract below never silently reports success without retaining data.

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("resourceRW.dll")

// Win32 resource type constants are part of the public resourceRW script
// contract (the upstream plug-in publishes them as globals).  Keep the
// numeric values available on every host so a script can use rtVersion,
// rtGroupIcon, ... without branching on the platform.  The portable sidecar
// and the native PE path below use the same integer keys.
namespace {

struct ResourceTypeConstant {
    const tjs_char *name;
    tjs_int value;
};

constexpr ResourceTypeConstant kResourceTypeConstants[] = {
    {TJS_W("rtCursor"), 1},       {TJS_W("rtBitmap"), 2},
    {TJS_W("rtIcon"), 3},         {TJS_W("rtMenu"), 4},
    {TJS_W("rtDialog"), 5},       {TJS_W("rtString"), 6},
    {TJS_W("rtFontDir"), 7},      {TJS_W("rtFont"), 8},
    {TJS_W("rtAccelerator"), 9},  {TJS_W("rtRcData"), 10},
    {TJS_W("rtMessageTable"), 11}, {TJS_W("rtGroupCursor"), 12},
    {TJS_W("rtGroupIcon"), 14},   {TJS_W("rtVersion"), 16},
    {TJS_W("rtDlgInclude"), 240}, {TJS_W("rtPlugPlay"), 19},
    {TJS_W("rtVxd"), 20},         {TJS_W("rtAniCursor"), 21},
    {TJS_W("rtAniIcon"), 22},     {TJS_W("rtHtml"), 23},
    {TJS_W("rtManifest"), 24},
};

// Keep ownership information for the compatibility constants.  A game or a
// native plug-in may publish a value with the same name; in that case this
// adapter must leave it untouched both while loading and while unloading.
std::vector<const ResourceTypeConstant *> g_resourceTypeConstantsInstalled;

void registerResourceTypeConstants() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;
    for(const auto &constant : kResourceTypeConstants) {
        bool present = false;
        try {
            tTJSVariant existing;
            present = TJS_SUCCEEDED(global->PropGet(
                TJS_MEMBERMUSTEXIST | TJS_IGNOREPROP, constant.name, nullptr,
                &existing, global));
        } catch(...) {
            present = false;
        }
        if(present)
            continue;

        try {
            tTJSVariant value(constant.value);
            if(TJS_SUCCEEDED(global->PropSet(
                   TJS_MEMBERENSURE | TJS_IGNOREPROP, constant.name, nullptr,
                   &value, global))) {
                if(std::find(g_resourceTypeConstantsInstalled.begin(),
                             g_resourceTypeConstantsInstalled.end(),
                             &constant) ==
                   g_resourceTypeConstantsInstalled.end())
                    g_resourceTypeConstantsInstalled.push_back(&constant);
            }
        } catch(...) {
            // A read-only host global should not prevent resourceRW from
            // loading its actual resource implementation.
        }
    }
    global->Release();
}

void unregisterResourceTypeConstants() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;
    for(const ResourceTypeConstant *constant : g_resourceTypeConstantsInstalled) {
        if(!constant)
            continue;
        try {
            // Preserve a value that the script or another plug-in replaced
            // after registration; only remove the value we still own.
            tTJSVariant current;
            if(TJS_SUCCEEDED(global->PropGet(
                   TJS_MEMBERMUSTEXIST | TJS_IGNOREPROP, constant->name,
                   nullptr, &current, global)) &&
               current.Type() != tvtVoid &&
               static_cast<tjs_int64>(current) == constant->value) {
                global->DeleteMember(0, constant->name, nullptr, global);
            }
        } catch(...) {
            // Unloading a compatibility module must remain best effort.
        }
    }
    g_resourceTypeConstantsInstalled.clear();
    global->Release();
}

} // namespace

NCB_PRE_REGIST_CALLBACK(registerResourceTypeConstants);
NCB_POST_UNREGIST_CALLBACK(unregisterResourceTypeConstants);

#if defined(_WIN32)

// The original resourceRW edits PE images.  On Windows we can preserve that
// behavior exactly and still keep the portable sidecar fallback for targets
// that are not writable PE files.  Native resources are copied into the same
// bounded Entry representation used by the sidecar, which keeps the TJS
// methods and language/name selection identical on all platforms.
struct NativeResourceEnumContext {
    HMODULE module = nullptr;
    std::vector<PortableResourceEntry> *entries = nullptr;
    std::string type;
    bool ok = true;
    std::size_t count = 0;
};

constexpr std::size_t kMaxNativeResourceEntries = 65536;

std::string nativeResourceKey(LPCWSTR value) {
    if(IS_INTRESOURCE(value))
        return "@" + std::to_string(static_cast<unsigned long long>(
            reinterpret_cast<ULONG_PTR>(value)));
    if(!value)
        return {};
    return "=" + toUtf8(ttstr(reinterpret_cast<const tjs_char *>(value)));
}

bool nativeResourceNames(const tTJSVariant &value, LPCWSTR &native,
                         std::wstring &storage) {
    native = nullptr;
    storage.clear();
    if(value.Type() == tvtInteger) {
        const tjs_int64 number = static_cast<tjs_int64>(value);
        if(number < 0 || number > 0xffff)
            return false;
        native = MAKEINTRESOURCEW(static_cast<WORD>(number));
        return true;
    }
    if(value.Type() != tvtString)
        return false;
    const ttstr text = value.GetString();
    storage.assign(reinterpret_cast<const wchar_t *>(text.c_str()));
    native = storage.c_str();
    return !storage.empty();
}

BOOL CALLBACK enumNativeLanguages(HMODULE module, LPCWSTR type,
                                  LPCWSTR name, WORD language,
                                  LONG_PTR parameter) {
    auto *context = reinterpret_cast<NativeResourceEnumContext *>(parameter);
    if(!context || !context->entries || context->count >= kMaxNativeResourceEntries)
        return FALSE;
    HRSRC resource = FindResourceExW(module, type, name, language);
    if(!resource) {
        context->ok = false;
        return FALSE;
    }
    const DWORD size = SizeofResource(module, resource);
    if(static_cast<std::size_t>(size) > AetherKiri::ResourceBundle::kMaxPayloadBytes) {
        context->ok = false;
        return FALSE;
    }
    HGLOBAL loaded = LoadResource(module, resource);
    const void *bytes = loaded ? LockResource(loaded) : nullptr;
    if(size != 0 && !bytes) {
        context->ok = false;
        return FALSE;
    }
    PortableResourceEntry entry;
    entry.type = context->type;
    entry.name = nativeResourceKey(name);
    entry.language = static_cast<std::uint32_t>(language);
    if(entry.type.empty() || entry.name.empty()) {
        context->ok = false;
        return FALSE;
    }
    if(size != 0) {
        const auto *begin = static_cast<const std::uint8_t *>(bytes);
        entry.bytes.assign(begin, begin + size);
    }
    context->entries->push_back(std::move(entry));
    ++context->count;
    return TRUE;
}

BOOL CALLBACK enumNativeNames(HMODULE module, LPCWSTR type, LPWSTR name,
                              LONG_PTR parameter) {
    auto *context = reinterpret_cast<NativeResourceEnumContext *>(parameter);
    if(!context)
        return FALSE;
    context->type = nativeResourceKey(type);
    if(context->type.empty()) {
        context->ok = false;
        return FALSE;
    }
    if(!EnumResourceLanguagesW(module, type, name, enumNativeLanguages,
                                parameter)) {
        // A resource type with no language entries is unusual, but the
        // Win32 API reports ERROR_RESOURCE_TYPE_NOT_FOUND for it.  Treat only
        // our explicit bounds/lookup failures as fatal.
        if(!context->ok)
            return FALSE;
    }
    return TRUE;
}

BOOL CALLBACK enumNativeTypes(HMODULE module, LPWSTR type, LONG_PTR parameter) {
    if(!EnumResourceNamesW(module, type, enumNativeNames, parameter)) {
        auto *context = reinterpret_cast<NativeResourceEnumContext *>(parameter);
        if(context && !context->ok)
            return FALSE;
    }
    return TRUE;
}

bool loadNativeResourceEntries(const ttstr &target,
                               std::vector<PortableResourceEntry> &entries,
                               bool &opened) {
    opened = false;
    ttstr local(target);
    try {
        TVPGetLocalName(local);
    } catch(...) {
    }
    const std::wstring path(reinterpret_cast<const wchar_t *>(local.c_str()));
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
                                     LOAD_LIBRARY_AS_DATAFILE);
    if(!module)
        return false;
    opened = true;
    NativeResourceEnumContext context;
    context.module = module;
    context.entries = &entries;
    context.ok = true;
    EnumResourceTypesW(module, enumNativeTypes,
                       reinterpret_cast<LONG_PTR>(&context));
    const bool ok = context.ok;
    FreeLibrary(module);
    return ok;
}

bool updateNativeResource(HANDLE handle, const tTJSVariant &type,
                          const tTJSVariant &name, WORD language,
                          const std::vector<std::uint8_t> &bytes) {
    if(!handle || bytes.size() > std::numeric_limits<DWORD>::max())
        return false;
    LPCWSTR nativeType = nullptr;
    LPCWSTR nativeName = nullptr;
    std::wstring typeStorage;
    std::wstring nameStorage;
    if(!nativeResourceNames(type, nativeType, typeStorage) ||
       !nativeResourceNames(name, nativeName, nameStorage))
        return false;
    LPVOID data = bytes.empty() ? nullptr :
        const_cast<std::uint8_t *>(bytes.data());
    return UpdateResourceW(handle, nativeType, nativeName, language, data,
                           static_cast<DWORD>(bytes.size())) != FALSE;
}

#endif // defined(_WIN32)

class ResourceReader {
public:
    static tjs_error TJS_INTF_METHOD factory(ResourceReader **result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *) {
        if(!result)
            return TJS_E_FAIL;
        auto *reader = new ResourceReader();
        if(!reader)
            return TJS_E_FAIL;
        if(numparams > 0 && param && param[0])
            reader->open(param[0]->GetString());
        *result = reader;
        return TJS_S_OK;
    }

    void open(const tjs_char *file) {
        close();
        file_ = file ? file : TJS_W("");
        bool nativeOpened = false;
#if defined(_WIN32)
        std::vector<PortableResourceEntry> nativeEntries;
        const bool nativeEnumerated =
            loadNativeResourceEntries(file_, nativeEntries, nativeOpened);
        if(nativeEnumerated)
            entries_ = std::move(nativeEntries);
#endif
        // A sidecar is authoritative only when the target is not a readable
        // PE image.  This prevents a stale fallback sidecar from masking
        // resources that were successfully read from an executable/DLL.
        if(!nativeOpened
#if defined(_WIN32)
           || !nativeEnumerated
#endif
        ) {
            loaded_ = loadPortableResourceEntries(file_, entries_);
        } else {
            loaded_ = true;
        }
    }

    void close() {
        file_.Clear();
        entries_.clear();
        loaded_ = false;
    }

    static tjs_error TJS_INTF_METHOD setLangCb(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               ResourceReader *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(!parseResourceLanguageArgs(numparams, param, self->lang_))
            return TJS_E_INVALIDPARAM;
        if(result)
            *result = static_cast<tjs_int>(self->lang_);
        return TJS_S_OK;
    }

    bool isExistentResource(tTJSVariant type, tTJSVariant name) {
        if(!loaded_)
            return false;
        const std::string typeKey = resourceVariantKey(type);
        const std::string nameKey = resourceVariantKey(name);
        if(typeKey.empty() || nameKey.empty())
            return false;
        return findPortableResource(entries_, typeKey, nameKey, lang_) != nullptr;
    }

    ttstr readToText(tTJSVariant type, tTJSVariant name, bool utf8 = false) {
        const PortableResourceEntry *entry = find(type, name);
        if(!entry)
            return ttstr();
        if(utf8)
            return fromUtf8(reinterpret_cast<const char *>(entry->bytes.data()),
                            entry->bytes.size());
        return decodeUnicode(entry->bytes);
    }

    tTJSVariant readToOctet(tTJSVariant type, tTJSVariant name) {
        const PortableResourceEntry *entry = find(type, name);
        if(!entry || entry->bytes.size() >
               static_cast<std::size_t>(std::numeric_limits<tjs_uint>::max()))
            return tTJSVariant();
        return tTJSVariant(entry->bytes.empty() ? nullptr : entry->bytes.data(),
                           static_cast<tjs_uint>(entry->bytes.size()));
    }

    tjs_int readToFile(tTJSVariant type, tTJSVariant name,
                       const tjs_char *file) {
        const PortableResourceEntry *entry = find(type, name);
        if(!entry || !file || !file[0] || entry->bytes.size() >
               static_cast<std::size_t>(std::numeric_limits<tjs_uint>::max()))
            return 0;
        std::string bytes;
        if(!entry->bytes.empty())
            bytes.assign(reinterpret_cast<const char *>(entry->bytes.data()),
                         entry->bytes.size());
        if(!writeStorageBytes(ttstr(file), bytes))
            return 0;
        return static_cast<tjs_int>(entry->bytes.size());
    }

    tTJSVariant enumTypes() {
        std::set<std::string> values;
        if(loaded_)
            for(const auto &entry : entries_)
                values.insert(entry.type);
        return makeResourceVariantArray(
            std::vector<std::string>(values.begin(), values.end()));
    }

    tTJSVariant enumNames(tTJSVariant type) {
        std::set<std::string> values;
        const std::string typeKey = resourceVariantKey(type);
        if(loaded_ && !typeKey.empty())
            for(const auto &entry : entries_)
                if(entry.type == typeKey)
                    values.insert(entry.name);
        return makeResourceVariantArray(
            std::vector<std::string>(values.begin(), values.end()));
    }

    tTJSVariant enumLangs(tTJSVariant type, tTJSVariant name) {
        std::set<std::string> values;
        const std::string typeKey = resourceVariantKey(type);
        const std::string nameKey = resourceVariantKey(name);
        if(loaded_ && !typeKey.empty() && !nameKey.empty()) {
            for(const auto &entry : entries_) {
                if(entry.type == typeKey && entry.name == nameKey)
                    values.insert("@" + std::to_string(entry.language));
            }
        }
        return makeResourceVariantArray(
            std::vector<std::string>(values.begin(), values.end()));
    }

private:
    const PortableResourceEntry *find(const tTJSVariant &type,
                                      const tTJSVariant &name) const {
        if(!loaded_)
            return nullptr;
        const std::string typeKey = resourceVariantKey(type);
        const std::string nameKey = resourceVariantKey(name);
        if(typeKey.empty() || nameKey.empty())
            return nullptr;
        return findPortableResource(entries_, typeKey, nameKey, lang_);
    }

    static ttstr decodeUnicode(const std::vector<std::uint8_t> &bytes) {
        if(bytes.empty())
            return ttstr();
        std::size_t offset = 0;
        if(bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe)
            offset = 2;
        if((bytes.size() - offset) < 2)
            return fromUtf8(reinterpret_cast<const char *>(bytes.data()),
                            bytes.size());
        const std::size_t units = (bytes.size() - offset) / 2;
        std::u16string text;
        text.reserve(units);
        for(std::size_t i = 0; i < units; ++i) {
            const std::size_t p = offset + i * 2;
            const char16_t unit = static_cast<char16_t>(
                static_cast<std::uint16_t>(bytes[p]) |
                (static_cast<std::uint16_t>(bytes[p + 1]) << 8));
            if(unit == 0)
                break;
            text.push_back(unit);
        }
        return ttstr(text.c_str(), static_cast<tjs_int>(text.size()));
    }

    ttstr file_;
    std::uint32_t lang_ = 0;
    std::vector<PortableResourceEntry> entries_;
    bool loaded_ = false;
};

class ResourceWriter {
public:
    static tjs_error TJS_INTF_METHOD factory(ResourceWriter **result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *) {
        if(!result)
            return TJS_E_FAIL;
        auto *writer = new ResourceWriter();
        if(!writer)
            return TJS_E_FAIL;
        if(numparams > 0 && param && param[0])
            writer->open(param[0]->GetString(),
                         paramBool(1, numparams, param, false));
        *result = writer;
        return TJS_S_OK;
    }
    void open(const tjs_char *file, bool clean = false) {
        close(false);
        file_ = file ? file : TJS_W("");
        opened_ = !file_.IsEmpty();
        dirty_ = false;
        entries_.clear();
        nativeWriter_ = false;
#if defined(_WIN32)
        if(opened_) {
            ttstr local(file_);
            try {
                TVPGetLocalName(local);
            } catch(...) {
            }
            const std::wstring path(
                reinterpret_cast<const wchar_t *>(local.c_str()));
            nativeHandle_ = BeginUpdateResourceW(path.c_str(), clean ? TRUE : FALSE);
            if(nativeHandle_) {
                nativeWriter_ = true;
            }
        }
#endif
        if(opened_ && !nativeWriter_ && !clean)
            loadPortableResourceEntries(file_, entries_);
        if(clean)
            dirty_ = true;
    }

    bool close(bool write = true) {
        if(!opened_)
            return true;
        bool ok = true;
#if defined(_WIN32)
        if(nativeWriter_) {
            const BOOL discard = (write && dirty_) ? FALSE : TRUE;
            ok = nativeHandle_ && EndUpdateResourceW(nativeHandle_, discard) != FALSE;
            nativeHandle_ = nullptr;
            nativeWriter_ = false;
        } else
#endif
        if(write && dirty_)
            ok = savePortableResourceEntries(file_, entries_);
        file_.Clear();
        entries_.clear();
        opened_ = false;
        dirty_ = false;
        return ok;
    }

    static tjs_error TJS_INTF_METHOD setLangCb(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               ResourceWriter *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(!parseResourceLanguageArgs(numparams, param, self->lang_))
            return TJS_E_INVALIDPARAM;
        if(result)
            *result = static_cast<tjs_int>(self->lang_);
        return TJS_S_OK;
    }

    bool clear(tTJSVariant type, tTJSVariant name) {
        if(!opened_)
            return false;
#if defined(_WIN32)
        if(nativeWriter_) {
            if(!updateNativeResource(nativeHandle_, type, name,
                                     static_cast<WORD>(lang_), {}))
                return false;
            dirty_ = true;
            return true;
        }
#endif
        const std::string typeKey = resourceVariantKey(type);
        const std::string nameKey = resourceVariantKey(name);
        if(typeKey.empty() || nameKey.empty())
            return false;
        const auto oldSize = entries_.size();
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                      [&](const PortableResourceEntry &entry) {
                                          return entry.type == typeKey &&
                                                 entry.name == nameKey &&
                                                 entry.language == lang_;
                                      }),
                       entries_.end());
        if(entries_.size() != oldSize)
            dirty_ = true;
        return entries_.size() != oldSize;
    }

    bool writeFromText(tTJSVariant type, tTJSVariant name,
                       const tjs_char *text, bool utf8 = false) {
        if(!text)
            return false;
        const ttstr value(text);
        std::vector<std::uint8_t> bytes;
        if(utf8) {
            const std::string encoded = toUtf8(value);
            if(encoded.size() > AetherKiri::ResourceBundle::kMaxPayloadBytes)
                return false;
            bytes.assign(encoded.begin(), encoded.end());
        } else {
            if(value.length() < 0 ||
               static_cast<std::size_t>(value.length()) >
                   (AetherKiri::ResourceBundle::kMaxPayloadBytes - 4) / 2)
                return false;
            bytes.reserve(2 + value.length() * 2 + 2);
            bytes.push_back(0xff);
            bytes.push_back(0xfe);
            for(tjs_int i = 0; i < value.length(); ++i) {
                const std::uint16_t unit = value.c_str()[i];
                bytes.push_back(static_cast<std::uint8_t>(unit & 0xffu));
                bytes.push_back(static_cast<std::uint8_t>(unit >> 8));
            }
            bytes.push_back(0);
            bytes.push_back(0);
        }
#if defined(_WIN32)
        if(nativeWriter_) {
            if(!updateNativeResource(nativeHandle_, type, name,
                                     static_cast<WORD>(lang_), bytes))
                return false;
            dirty_ = true;
            return true;
        }
#endif
        return upsert(type, name, std::move(bytes));
    }

    bool writeFromFile(tTJSVariant type, tTJSVariant name,
                       const tjs_char *file) {
        if(!file || !file[0])
            return false;
        std::string data;
        if(!readStorageBytes(ttstr(file), data))
            return false;
        std::vector<std::uint8_t> bytes(data.begin(), data.end());
#if defined(_WIN32)
        if(nativeWriter_) {
            if(!updateNativeResource(nativeHandle_, type, name,
                                     static_cast<WORD>(lang_), bytes))
                return false;
            dirty_ = true;
            return true;
        }
#endif
        return upsert(type, name, std::move(bytes));
    }

    bool writeFromOctet(tTJSVariant type, tTJSVariant name,
                        tTJSVariant octet) {
        if(octet.Type() != tvtOctet)
            return false;
        const tTJSVariantOctet *value = octet.AsOctetNoAddRef();
        if(!value)
            return false;
        const tjs_uint length = value->GetLength();
        if(length > AetherKiri::ResourceBundle::kMaxPayloadBytes ||
           (length > 0 && !value->GetData()))
            return false;
        std::vector<std::uint8_t> bytes =
            length == 0
                ? std::vector<std::uint8_t>()
                : std::vector<std::uint8_t>(
                      value->GetData(), value->GetData() + length);
#if defined(_WIN32)
        if(nativeWriter_) {
            if(!updateNativeResource(nativeHandle_, type, name,
                                     static_cast<WORD>(lang_), bytes))
                return false;
            dirty_ = true;
            return true;
        }
#endif
        return upsert(type, name, std::move(bytes));
    }

private:
    bool upsert(const tTJSVariant &type, const tTJSVariant &name,
                std::vector<std::uint8_t> bytes) {
        if(!opened_ || bytes.size() >
               AetherKiri::ResourceBundle::kMaxPayloadBytes)
            return false;
        const std::string typeKey = resourceVariantKey(type);
        const std::string nameKey = resourceVariantKey(name);
        if(typeKey.empty() || nameKey.empty())
            return false;
        for(auto &entry : entries_) {
            if(entry.type == typeKey && entry.name == nameKey &&
               entry.language == lang_) {
                if(entry.bytes == bytes)
                    return true;
                entry.bytes = std::move(bytes);
                dirty_ = true;
                return true;
            }
        }
        PortableResourceEntry entry;
        entry.type = typeKey;
        entry.name = nameKey;
        entry.language = lang_;
        entry.bytes = std::move(bytes);
        entries_.push_back(std::move(entry));
        dirty_ = true;
        return true;
    }

    ttstr file_;
    std::uint32_t lang_ = 0;
    std::vector<PortableResourceEntry> entries_;
    bool opened_ = false;
    bool dirty_ = false;
#if defined(_WIN32)
    HANDLE nativeHandle_ = nullptr;
#endif
    bool nativeWriter_ = false;
};

NCB_REGISTER_CLASS(ResourceReader) {
    Factory(&ResourceReader::factory);
    NCB_METHOD(open);
    NCB_METHOD(close);
    NCB_METHOD_RAW_CALLBACK(setLang, &ResourceReader::setLangCb, 0);
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
    NCB_METHOD_RAW_CALLBACK(setLang, &ResourceWriter::setLangCb, 0);
    NCB_METHOD(clear);
    NCB_METHOD(writeFromText);
    NCB_METHOD(writeFromFile);
    NCB_METHOD(writeFromOctet);
}

// -------------------------------------------------------------------------
// krkrsteam.dll
// -------------------------------------------------------------------------
// krkrz's implementation is coupled to the proprietary Steamworks SDK and
// cannot be linked into a portable Aether build.  This adapter deliberately
// implements the useful offline contract (achievements, language and a local
// cloud namespace) and reports SDK-only features as unavailable.  State is
// persisted through the engine storage API, so it works for non-Steam copies
// of a game without pretending that an online Steam account exists.

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("krkrsteam.dll")

namespace {

constexpr std::size_t kSteamStateMaxBytes = 128u * 1024u * 1024u;
constexpr std::size_t kSteamFieldMaxBytes = 1u * 1024u * 1024u;
constexpr std::size_t kSteamMaxRecords = 4096;

char hexDigit(unsigned int value) {
    static const char digits[] = "0123456789abcdef";
    return digits[value & 0xfu];
}

std::string steamHexEncode(const std::string &value) {
    std::string out;
    out.reserve(value.size() * 2);
    for(const unsigned char c : value) {
        out.push_back(hexDigit(c >> 4));
        out.push_back(hexDigit(c));
    }
    return out;
}

std::string steamHexEncode(const std::vector<std::uint8_t> &value) {
    std::string out;
    out.reserve(value.size() * 2);
    for(const std::uint8_t c : value) {
        out.push_back(hexDigit(c >> 4));
        out.push_back(hexDigit(c));
    }
    return out;
}

int hexValue(char value) {
    if(value >= '0' && value <= '9')
        return value - '0';
    if(value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if(value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

bool steamHexDecode(const std::string &encoded, std::string &value,
                    std::size_t maxBytes = kSteamFieldMaxBytes) {
    if(encoded.size() % 2 != 0 || encoded.size() / 2 > maxBytes)
        return false;
    value.clear();
    value.resize(encoded.size() / 2);
    for(std::size_t i = 0; i < value.size(); ++i) {
        const int hi = hexValue(encoded[i * 2]);
        const int lo = hexValue(encoded[i * 2 + 1]);
        if(hi < 0 || lo < 0)
            return false;
        value[i] = static_cast<char>((hi << 4) | lo);
    }
    return true;
}

bool steamHexDecode(const std::string &encoded, std::vector<std::uint8_t> &value,
                    std::size_t maxBytes = kSteamStateMaxBytes) {
    std::string bytes;
    if(!steamHexDecode(encoded, bytes, maxBytes))
        return false;
    value.assign(bytes.begin(), bytes.end());
    return true;
}

std::vector<std::string> steamSplitTabs(const std::string &line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while(true) {
        const std::size_t end = line.find('\t', start);
        fields.push_back(line.substr(start, end == std::string::npos
                                              ? std::string::npos
                                              : end - start));
        if(end == std::string::npos)
            break;
        start = end + 1;
    }
    return fields;
}

bool steamParseInt64(const std::string &text, std::int64_t &value) {
    if(text.empty())
        return false;
    char *end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if(errno != 0 || !end || *end != '\0')
        return false;
    value = static_cast<std::int64_t>(parsed);
    return true;
}

ttstr steamStatePath() {
    ttstr root = TVPDataPath;
    if(root.IsEmpty())
        root = TVPGetAppPath();
    if(!root.IsEmpty() && root.c_str()[root.length() - 1] != TJS_W('/') &&
       root.c_str()[root.length() - 1] != TJS_W('\\'))
        root += TJS_W('/');
    return root + TJS_W("aether-steam-state.tsv");
}

std::string steamNormalizeCloudName(const ttstr &name) {
    std::string value = toUtf8(name);
    if(value.empty() || value.size() > kSteamFieldMaxBytes)
        return {};
    std::transform(value.begin(), value.end(), value.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    if(value == "." || value == ".." || value.find('/') != std::string::npos ||
       value.find('\\') != std::string::npos)
        return {};
    for(const unsigned char c : value)
        if(c < 0x20 || c == 0x7f)
            return {};
    return value;
}

std::string steamAchievementName(const tTJSVariant &value) {
    if(value.Type() == tvtString)
        return toUtf8(ttstr(value.GetString()));
    return {};
}

tTJSVariant makeSteamDictionary(const std::vector<std::pair<const tjs_char *,
                                                              tTJSVariant>> &values) {
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(!dict)
        return tTJSVariant();
    for(const auto &item : values)
        setDict(dict, item.first, item.second);
    tTJSVariant result(dict, dict);
    dict->Release();
    return result;
}

class SteamCompatState {
public:
    struct Achievement {
        bool achieved = false;
        std::int64_t unlockTime = 0;
    };
    struct CloudFile {
        std::vector<std::uint8_t> data;
        std::int64_t time = 0;
    };

    static bool ensureLoaded() {
        std::lock_guard<std::mutex> guard(mutex_);
        return ensureLoadedLocked();
    }

    static bool initialized() {
        std::lock_guard<std::mutex> guard(mutex_);
        ensureLoadedLocked();
        return loaded_;
    }

    static std::vector<std::string> achievementNames() {
        std::lock_guard<std::mutex> guard(mutex_);
        ensureLoadedLocked();
        std::vector<std::string> names;
        for(const auto &item : achievements_)
            names.push_back(item.first);
        return names;
    }

    static bool getAchievement(const std::string &name, Achievement &value) {
        std::lock_guard<std::mutex> guard(mutex_);
        ensureLoadedLocked();
        const auto found = achievements_.find(name);
        if(found == achievements_.end())
            return false;
        value = found->second;
        return true;
    }

    static bool setAchievement(const std::string &name, bool achieved) {
        if(name.empty() || name.size() > kSteamFieldMaxBytes)
            return false;
        std::lock_guard<std::mutex> guard(mutex_);
        ensureLoadedLocked();
        const auto previous = achievements_.find(name);
        const Achievement old = previous == achievements_.end()
                                     ? Achievement{}
                                     : previous->second;
        const bool existed = previous != achievements_.end();
        Achievement next = old;
        next.achieved = achieved;
        next.unlockTime = achieved ? currentTime() : 0;
        achievements_[name] = next;
        if(saveLocked())
            return true;
        if(existed)
            achievements_[name] = old;
        else
            achievements_.erase(name);
        return false;
    }

    static bool clearAchievement(const std::string &name) {
        std::lock_guard<std::mutex> guard(mutex_);
        ensureLoadedLocked();
        const auto found = achievements_.find(name);
        if(found == achievements_.end())
            return false;
        const Achievement old = found->second;
        achievements_.erase(found);
        if(saveLocked())
            return true;
        achievements_[name] = old;
        return false;
    }

    static bool cloudEnabled() {
        std::lock_guard<std::mutex> guard(mutex_);
        ensureLoadedLocked();
        return cloudEnabled_;
    }

    static void setCloudEnabled(bool enabled) {
        std::lock_guard<std::mutex> guard(mutex_);
        ensureLoadedLocked();
        cloudEnabled_ = enabled;
        if(!saveLocked())
            logCompatOnce(TJS_W("krkrsteam.dll"),
                          TJS_W("could not persist cloudEnabled"));
    }

    static std::vector<std::string> cloudNames() {
        std::lock_guard<std::mutex> guard(mutex_);
        ensureLoadedLocked();
        std::vector<std::string> names;
        for(const auto &item : cloudFiles_)
            names.push_back(item.first);
        return names;
    }

    static bool cloudInfo(const std::string &name, CloudFile &value) {
        std::lock_guard<std::mutex> guard(mutex_);
        ensureLoadedLocked();
        const auto found = cloudFiles_.find(name);
        if(found == cloudFiles_.end())
            return false;
        value = found->second;
        return true;
    }

    static bool deleteCloud(const std::string &name) {
        std::lock_guard<std::mutex> guard(mutex_);
        ensureLoadedLocked();
        const auto found = cloudFiles_.find(name);
        if(found == cloudFiles_.end())
            return false;
        const CloudFile old = found->second;
        cloudFiles_.erase(found);
        if(saveLocked())
            return true;
        cloudFiles_[name] = old;
        return false;
    }

    static bool copyCloud(const std::string &source, const std::string &dest) {
        if(source.empty() || dest.empty() || source == dest)
            return false;
        std::lock_guard<std::mutex> guard(mutex_);
        ensureLoadedLocked();
        const auto found = cloudFiles_.find(source);
        if(found == cloudFiles_.end())
            return false;
        const auto previous = cloudFiles_.find(dest);
        const bool existed = previous != cloudFiles_.end();
        const CloudFile old = existed ? previous->second : CloudFile{};
        cloudFiles_[dest] = found->second;
        cloudFiles_[dest].time = currentTime();
        if(saveLocked())
            return true;
        if(existed)
            cloudFiles_[dest] = old;
        else
            cloudFiles_.erase(dest);
        return false;
    }

    static std::uint64_t cloudBytes() {
        std::lock_guard<std::mutex> guard(mutex_);
        ensureLoadedLocked();
        std::uint64_t total = 0;
        for(const auto &item : cloudFiles_)
            total += item.second.data.size();
        return total;
    }

    static bool readCloud(const std::string &name,
                          std::vector<std::uint8_t> &data) {
        std::lock_guard<std::mutex> guard(mutex_);
        ensureLoadedLocked();
        const auto found = cloudFiles_.find(name);
        if(found == cloudFiles_.end())
            return false;
        data = found->second.data;
        return true;
    }

    static bool writeCloud(const std::string &name,
                           const std::vector<std::uint8_t> &data) {
        if(name.empty() || name.size() > kSteamFieldMaxBytes ||
           data.size() > kSteamStateMaxBytes)
            return false;
        std::lock_guard<std::mutex> guard(mutex_);
        ensureLoadedLocked();
        const auto previous = cloudFiles_.find(name);
        const bool existed = previous != cloudFiles_.end();
        const CloudFile old = existed ? previous->second : CloudFile{};
        cloudFiles_[name] = CloudFile{data, currentTime()};
        if(saveLocked())
            return true;
        if(existed)
            cloudFiles_[name] = old;
        else
            cloudFiles_.erase(name);
        return false;
    }

private:
    static std::int64_t currentTime() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    static bool ensureLoadedLocked() {
        if(loaded_)
            return true;
        loaded_ = true;
        achievements_.clear();
        cloudFiles_.clear();
        cloudEnabled_ = true;
        std::string encoded;
        if(!readStorageBytes(steamStatePath(), encoded))
            return true;
        if(encoded.size() > kSteamStateMaxBytes ||
           encoded.compare(0, 11, "AKRSTEAM1\n") != 0) {
            logCompatOnce(TJS_W("krkrsteam.dll"),
                          TJS_W("ignoring invalid local Steam state"));
            return true;
        }
        std::istringstream stream(encoded.substr(11));
        std::string line;
        while(std::getline(stream, line)) {
            if(line.size() > kSteamFieldMaxBytes * 3)
                return true;
            const auto fields = steamSplitTabs(line);
            if(fields.empty() || fields[0] == "")
                continue;
            if(fields[0] == "E" && fields.size() == 2) {
                cloudEnabled_ = fields[1] == "1";
                continue;
            }
            if(fields[0] == "A" && fields.size() == 4 &&
               achievements_.size() < kSteamMaxRecords) {
                std::string name;
                std::int64_t time = 0;
                if(steamHexDecode(fields[1], name) && steamParseInt64(fields[2], time) &&
                   (fields[3] == "0" || fields[3] == "1"))
                    achievements_[std::move(name)] =
                        Achievement{fields[3] == "1", time};
                continue;
            }
            if(fields[0] == "C" && fields.size() == 4 &&
               cloudFiles_.size() < kSteamMaxRecords) {
                std::string name;
                std::vector<std::uint8_t> data;
                std::int64_t time = 0;
                if(steamHexDecode(fields[1], name) &&
                   steamHexDecode(fields[3], data, kSteamStateMaxBytes) &&
                   steamParseInt64(fields[2], time) &&
                   !name.empty() && name.size() <= kSteamFieldMaxBytes)
                    cloudFiles_[std::move(name)] =
                        CloudFile{std::move(data), time};
            }
        }
        return true;
    }

    static bool saveLocked() {
        std::string encoded("AKRSTEAM1\nE\t");
        encoded += cloudEnabled_ ? "1\n" : "0\n";
        for(const auto &item : achievements_) {
            encoded += "A\t" + steamHexEncode(item.first) + "\t" +
                       std::to_string(item.second.unlockTime) + "\t" +
                       (item.second.achieved ? "1\n" : "0\n");
        }
        for(const auto &item : cloudFiles_) {
            encoded += "C\t" + steamHexEncode(item.first) + "\t" +
                       std::to_string(item.second.time) + "\t" +
                       steamHexEncode(item.second.data) + "\n";
        }
        if(encoded.size() > kSteamStateMaxBytes)
            return false;
        return writeStorageBytes(steamStatePath(), encoded);
    }

    static inline std::mutex mutex_;
    static inline bool loaded_ = false;
    static inline bool cloudEnabled_ = true;
    static inline std::map<std::string, Achievement> achievements_;
    static inline std::map<std::string, CloudFile> cloudFiles_;
};

bool steamGetAchievementKey(tjs_int count, tTJSVariant **params,
                            std::string &name) {
    if(count < 1 || !params || !params[0])
        return false;
    if(params[0]->Type() == tvtString) {
        name = steamAchievementName(*params[0]);
        return !name.empty() && name.size() <= kSteamFieldMaxBytes;
    }
    if(params[0]->Type() != tvtInteger)
        return false;
    const tjs_int64 index = static_cast<tjs_int64>(*params[0]);
    if(index < 0)
        return false;
    const auto names = SteamCompatState::achievementNames();
    if(static_cast<std::uint64_t>(index) >= names.size())
        return false;
    name = names[static_cast<std::size_t>(index)];
    return true;
}

bool steamScreenshotMode(const ttstr &location, ttstr &mode) {
    std::string path = toUtf8(location);
    if(path.empty())
        return false;
    std::string extension;
    try {
        extension = std::filesystem::path(path).extension().string();
    } catch(...) {
        return false;
    }
    if(!extension.empty() && extension.front() == '.')
        extension.erase(extension.begin());
    extension = lowerAscii(extension);
    if(extension.empty())
        extension = "png";
    else if(extension == "jpeg" || extension == "jif")
        extension = "jpg";
    // TVPSaveImage accepts the extension as a mode and preserves the
    // historical quality/default handling for jpg and the TLG variants.
    mode = fromUtf8(extension);
    return TVPGetSaveOption(mode, nullptr);
}

bool steamWriteScreenshot(iTJSDispatch2 *object, const ttstr &location) {
    if(!object || location.IsEmpty())
        return false;
    tTJSNI_BaseLayer *layer = nullptr;
    if(TJS_FAILED(object->NativeInstanceSupport(
           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
           reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer)
        return false;
    tTVPBaseTexture *image = layer->GetMainImage();
    if(!image || !image->Is32BPP() || image->GetWidth() == 0 ||
       image->GetHeight() == 0)
        return false;
    ttstr mode;
    if(!steamScreenshotMode(location, mode)) {
        logCompatOnce(TJS_W("krkrsteam.dll"),
                      TJS_W("screenshot location has no supported image format"));
        return false;
    }
    try {
        TVPSaveImage(location, mode, image, nullptr);
        return true;
    } catch(...) {
        logCompatOnce(TJS_W("krkrsteam.dll"),
                      TJS_W("could not save local screenshot"));
        return false;
    }
}

} // namespace

class SteamCompat {
public:
    static tjs_error requestInitializeCb(tTJSVariant *result, tjs_int,
                                         tTJSVariant **, iTJSDispatch2 *) {
        if(result)
            *result = SteamCompatState::ensureLoaded();
        return TJS_S_OK;
    }

    static tjs_error getAchievementCb(tTJSVariant *result, tjs_int count,
                                      tTJSVariant **params, iTJSDispatch2 *) {
        std::string name;
        if(!steamGetAchievementKey(count, params, name)) {
            if(result)
                result->Clear();
            return TJS_S_OK;
        }
        SteamCompatState::Achievement value;
        if(!SteamCompatState::getAchievement(name, value)) {
            if(result)
                result->Clear();
            return TJS_S_OK;
        }
        if(result) {
            *result = makeSteamDictionary({
                {TJS_W("ach"), fromUtf8(name)},
                {TJS_W("name"), fromUtf8(name)},
                {TJS_W("desc"), ttstr()},
                {TJS_W("hidden"), tTJSVariant(false)},
                {TJS_W("achieved"), tTJSVariant(value.achieved)},
                {TJS_W("unlockTime"), tTJSVariant(value.unlockTime)},
            });
        }
        return TJS_S_OK;
    }

    static tjs_error setAchievementCb(tTJSVariant *result, tjs_int count,
                                      tTJSVariant **params, iTJSDispatch2 *) {
        std::string name;
        if(count < 1 || !params || !params[0] ||
           (params[0]->Type() != tvtString && params[0]->Type() != tvtInteger)) {
            if(result)
                *result = false;
            return TJS_S_OK;
        }
        if(params[0]->Type() == tvtInteger) {
            if(!steamGetAchievementKey(count, params, name)) {
                if(result)
                    *result = false;
                return TJS_S_OK;
            }
        } else {
            name = steamAchievementName(*params[0]);
        }
        if(result)
            *result = SteamCompatState::setAchievement(name, true);
        return TJS_S_OK;
    }

    static tjs_error clearAchievementCb(tTJSVariant *result, tjs_int count,
                                        tTJSVariant **params, iTJSDispatch2 *) {
        std::string name;
        if(!steamGetAchievementKey(count, params, name)) {
            if(result)
                *result = false;
            return TJS_S_OK;
        }
        if(result)
            *result = SteamCompatState::clearAchievement(name);
        return TJS_S_OK;
    }

    static tjs_error getLanguageCb(tTJSVariant *result, tjs_int,
                                   tTJSVariant **, iTJSDispatch2 *) {
        std::string language = "english";
        if(const char *env = std::getenv("LANG")) {
            std::string value(env);
            const std::size_t end = value.find_first_of("_.-");
            if(end != std::string::npos)
                value.erase(end);
            std::transform(value.begin(), value.end(), value.begin(), [](char c) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            });
            if(!value.empty()) {
                if(value == "en") value = "english";
                else if(value == "ja") value = "japanese";
                else if(value == "zh") value = "schinese";
                language = value;
            }
        }
        if(result)
            *result = fromUtf8(language);
        return TJS_S_OK;
    }

    static tjs_error getInitializedCb(tTJSVariant *result, tjs_int,
                                      tTJSVariant **, iTJSDispatch2 *) {
        if(result)
            *result = SteamCompatState::initialized();
        return TJS_S_OK;
    }

    static tjs_error getAchievementsCountCb(tTJSVariant *result, tjs_int,
                                            tTJSVariant **, iTJSDispatch2 *) {
        if(result)
            *result = static_cast<tjs_int>(SteamCompatState::achievementNames().size());
        return TJS_S_OK;
    }

    static tjs_error getCloudEnabledCb(tTJSVariant *result, tjs_int,
                                       tTJSVariant **, iTJSDispatch2 *) {
        if(result)
            *result = SteamCompatState::cloudEnabled();
        return TJS_S_OK;
    }

    static tjs_error setCloudEnabledCb(tTJSVariant *, tjs_int count,
                                       tTJSVariant **params, iTJSDispatch2 *) {
        SteamCompatState::setCloudEnabled(
            count > 0 && params && params[0] && static_cast<bool>(*params[0]));
        return TJS_S_OK;
    }

    static tjs_error getCloudFileCountCb(tTJSVariant *result, tjs_int,
                                         tTJSVariant **, iTJSDispatch2 *) {
        if(result)
            *result = static_cast<tjs_int>(SteamCompatState::cloudNames().size());
        return TJS_S_OK;
    }

    static tjs_error getCloudQuotaCb(tTJSVariant *result, tjs_int,
                                     tTJSVariant **, iTJSDispatch2 *) {
        constexpr std::uint64_t total = 1024ull * 1024ull * 1024ull;
        const std::uint64_t used = SteamCompatState::cloudBytes();
        const std::uint64_t available = used < total ? total - used : 0;
        if(result)
            *result = makeSteamDictionary({
                {TJS_W("total"), tTJSVariant(static_cast<tjs_int64>(total))},
                {TJS_W("available"), tTJSVariant(static_cast<tjs_int64>(available))},
            });
        return TJS_S_OK;
    }

    static tjs_error getCloudFileInfoCb(tTJSVariant *result, tjs_int count,
                                        tTJSVariant **params, iTJSDispatch2 *) {
        if(count < 1 || !params || !params[0] ||
           params[0]->Type() != tvtInteger) {
            if(result) result->Clear();
            return TJS_S_OK;
        }
        const tjs_int64 index = static_cast<tjs_int64>(*params[0]);
        const auto names = SteamCompatState::cloudNames();
        if(index < 0 || static_cast<std::uint64_t>(index) >= names.size()) {
            if(result) result->Clear();
            return TJS_S_OK;
        }
        SteamCompatState::CloudFile file;
        if(!SteamCompatState::cloudInfo(names[static_cast<std::size_t>(index)], file)) {
            if(result) result->Clear();
            return TJS_S_OK;
        }
        if(result)
            *result = makeSteamDictionary({
                {TJS_W("filename"), fromUtf8(names[static_cast<std::size_t>(index)])},
                {TJS_W("size"), tTJSVariant(static_cast<tjs_int64>(file.data.size()))},
                {TJS_W("time"), tTJSVariant(file.time)},
            });
        return TJS_S_OK;
    }

    static tjs_error getCapabilitiesCb(tTJSVariant *result, tjs_int,
                                       tTJSVariant **, iTJSDispatch2 *) {
        if(result)
            *result = makeSteamDictionary({
                {TJS_W("backend"), fromUtf8("offline")},
                {TJS_W("initialized"),
                 tTJSVariant(SteamCompatState::initialized())},
                {TJS_W("achievements"), tTJSVariant(true)},
                {TJS_W("cloud"), tTJSVariant(true)},
                {TJS_W("language"), tTJSVariant(true)},
                {TJS_W("screenshotWrite"), tTJSVariant(true)},
                {TJS_W("screenshotTrigger"), tTJSVariant(false)},
                {TJS_W("screenshotHook"), tTJSVariant(false)},
                {TJS_W("broadcasting"), tTJSVariant(false)},
                {TJS_W("broadcastHook"), tTJSVariant(false)},
                {TJS_W("account"), tTJSVariant(false)},
                {TJS_W("dlc"), tTJSVariant(false)},
                {TJS_W("reason"), fromUtf8(
                    "Steamworks SDK/account is not linked; local persistence "
                    "and direct screenshot writing are enabled")},
            });
        return TJS_S_OK;
    }

    static tjs_error deleteCloudFileCb(tTJSVariant *result, tjs_int count,
                                       tTJSVariant **params, iTJSDispatch2 *) {
        if(count < 1 || !params || !params[0]) {
            if(result) *result = false;
            return TJS_S_OK;
        }
        const std::string name = steamNormalizeCloudName(ttstr(*params[0]));
        if(result) *result = !name.empty() && SteamCompatState::deleteCloud(name);
        return TJS_S_OK;
    }

    static tjs_error copyCloudFileCb(tTJSVariant *result, tjs_int count,
                                     tTJSVariant **params, iTJSDispatch2 *) {
        if(count < 2 || !params || !params[0] || !params[1]) {
            if(result) *result = false;
            return TJS_S_OK;
        }
        const std::string source = steamNormalizeCloudName(ttstr(*params[0]));
        const std::string dest = steamNormalizeCloudName(ttstr(*params[1]));
        if(result) *result = !source.empty() && !dest.empty() &&
            SteamCompatState::copyCloud(source, dest);
        return TJS_S_OK;
    }

    static tjs_error unsupportedCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *) {
        if(result)
            *result = false;
        return TJS_S_OK;
    }

    static tjs_error writeScreenshotCb(tTJSVariant *result, tjs_int count,
                                       tTJSVariant **params, iTJSDispatch2 *) {
        if(result)
            *result = false;
        if(count < 2 || !params || !params[0] || !params[1] ||
           params[0]->Type() != tvtObject || params[1]->Type() != tvtString)
            return TJS_S_OK;
        if(result)
            *result = steamWriteScreenshot(params[0]->AsObjectNoAddRef(),
                                            ttstr(*params[1]));
        return TJS_S_OK;
    }

    static tjs_error notSubscribedCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                     iTJSDispatch2 *) {
        if(result)
            *result = false;
        return TJS_S_OK;
    }

    static tjs_error dlcCountCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                iTJSDispatch2 *) {
        if(result)
            *result = 0;
        return TJS_S_OK;
    }

    static tjs_error dlcDataCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                               iTJSDispatch2 *) {
        if(result)
            result->Clear();
        return TJS_S_OK;
    }
};

NCB_REGISTER_CLASS_DIFFER(Steam, SteamCompat) {
    RawCallback(TJS_W("requestInitialize"), &Class::requestInitializeCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("getAchievement"), &Class::getAchievementCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("setAchievement"), &Class::setAchievementCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("clearAchievement"), &Class::clearAchievementCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("getLanguage"), &Class::getLanguageCb, TJS_STATICMEMBER);
    RawCallback(TJS_W("initialized"), &Class::getInitializedCb, (int)0,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("achievementsCount"), &Class::getAchievementsCountCb,
                (int)0, TJS_STATICMEMBER);
    RawCallback(TJS_W("cloudEnabled"), &Class::getCloudEnabledCb,
                &Class::setCloudEnabledCb, TJS_STATICMEMBER);
    RawCallback(TJS_W("cloudFileCount"), &Class::getCloudFileCountCb,
                (int)0, TJS_STATICMEMBER);
    RawCallback(TJS_W("capabilities"), &Class::getCapabilitiesCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("getCloudQuota"), &Class::getCloudQuotaCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("getCloudFileInfo"), &Class::getCloudFileInfoCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("deleteCloudFile"), &Class::deleteCloudFileCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("copyCloudFile"), &Class::copyCloudFileCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("triggerScreenshot"), &Class::unsupportedCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("hookScreenshots"), &Class::unsupportedCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("writeScreenshot"), &Class::writeScreenshotCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("isBroadcasting"), &Class::unsupportedCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("hookBroadcasting"), &Class::unsupportedCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("isIsSubscribedApp"), &Class::notSubscribedCb,
                TJS_STATICMEMBER);
    // Keep the historical typo accepted by older scripts, but also expose
    // krkrz's documented spelling.  Both remain explicit SDK-unavailable
    // checks rather than reporting ownership on a non-Steam build.
    RawCallback(TJS_W("isDlcInstalled"), &Class::notSubscribedCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("ssDlcInstalled"), &Class::notSubscribedCb,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("getDLCCount"), &Class::dlcCountCb, TJS_STATICMEMBER);
    RawCallback(TJS_W("getDLCData"), &Class::dlcDataCb, TJS_STATICMEMBER);
}

namespace {

// Make the upstream "steam://filename" storage contract useful without a
// Steamworks SDK.  The stream is backed by the same persistent state as the
// Steam class and commits on destruction, matching SteamRemoteStorage's
// close/commit behavior closely enough for save games and patch scripts.
class SteamCloudStream final : public tTVPMemoryStream {
public:
    SteamCloudStream(std::string name, std::vector<std::uint8_t> initial,
                     bool writable)
        : name_(std::move(name)), writable_(writable) {
        if(initial.size() > static_cast<std::size_t>(
                               std::numeric_limits<tjs_uint>::max()))
            return;
        SetSize(static_cast<tjs_uint>(initial.size()));
        if(!initial.empty())
            Write(initial.data(), static_cast<tjs_uint>(initial.size()));
        SetPosition(0);
    }

    ~SteamCloudStream() override {
        if(!writable_)
            return;
        const tjs_uint64 size = GetSize();
        if(size > static_cast<tjs_uint64>(
                      std::numeric_limits<tjs_uint>::max())) {
            logCompatOnce(TJS_W("krkrsteam.dll"),
                          TJS_W("steam cloud stream exceeds host size limit"));
            return;
        }
        const auto *buffer = static_cast<const std::uint8_t *>(
            GetInternalBuffer());
        std::vector<std::uint8_t> data;
        if(size > 0 && buffer)
            data.assign(buffer, buffer + static_cast<std::size_t>(size));
        if(!SteamCompatState::writeCloud(name_, data))
            logCompatOnce(TJS_W("krkrsteam.dll"),
                          TJS_W("could not persist steam cloud stream"));
    }

private:
    std::string name_;
    bool writable_ = false;
};

class SteamStorageMedia final : public iTVPStorageMedia {
public:
    void AddRef() override { ++refCount_; }
    void Release() override {
        if(refCount_ == 1)
            delete this;
        else
            --refCount_;
    }
    void GetName(ttstr &name) override { name = TJS_W("steam"); }
    void NormalizeDomainName(ttstr &name) override { name.ToLowerCase(); }
    void NormalizePathName(ttstr &name) override { name.ToLowerCase(); }

    bool CheckExistentStorage(const ttstr &name) override {
        const std::string key = steamNormalizeCloudName(name);
        SteamCompatState::CloudFile info;
        return !key.empty() && SteamCompatState::cloudInfo(key, info);
    }

    tTJSBinaryStream *Open(const ttstr &name, tjs_uint32 flags) override {
        const std::string key = steamNormalizeCloudName(name);
        if(key.empty())
            TVPThrowExceptionMessage(TJS_W("invalid steam storage name:%1"), name);

        std::vector<std::uint8_t> data;
        const bool exists = SteamCompatState::readCloud(key, data);
        if((flags & TJS_BS_ACCESS_MASK) == TJS_BS_READ && !exists)
            TVPThrowExceptionMessage(TJS_W("steam storage not found:%1"), name);
        const bool writable = (flags & TJS_BS_ACCESS_MASK) != TJS_BS_READ;
        if(!exists && !writable)
            TVPThrowExceptionMessage(TJS_W("cannot open steam storage:%1"), name);
        if(!writable)
            return new SteamCloudStream(key, std::move(data), false);
        if((flags & TJS_BS_ACCESS_MASK) == TJS_BS_WRITE)
            data.clear();
        auto *stream = new SteamCloudStream(key, std::move(data), true);
        if((flags & TJS_BS_ACCESS_MASK) == TJS_BS_APPEND)
            stream->Seek(0, TJS_BS_SEEK_END);
        return stream;
    }

    void GetListAt(const ttstr &name, iTVPStorageLister *lister) override {
        if(!lister || (!name.IsEmpty() && name != TJS_W("/")))
            return;
        for(const auto &item : SteamCompatState::cloudNames())
            lister->Add(fromUtf8(item));
    }

    void GetLocallyAccessibleName(ttstr &name) override { name.Clear(); }

private:
    tjs_int refCount_ = 1;
};

SteamStorageMedia *gSteamStorage = nullptr;

void registerSteamStorage() {
    if(!gSteamStorage) {
        gSteamStorage = new SteamStorageMedia();
        TVPRegisterStorageMedia(gSteamStorage);
    }
}

void unregisterSteamStorage() {
    if(gSteamStorage) {
        TVPUnregisterStorageMedia(gSteamStorage);
        gSteamStorage->Release();
        gSteamStorage = nullptr;
    }
}

} // namespace

NCB_PRE_REGIST_CALLBACK(registerSteamStorage);
NCB_POST_UNREGIST_CALLBACK(unregisterSteamStorage);
