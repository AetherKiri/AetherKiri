/**
 * @file PluginCallTracer.cpp
 * @brief Implementation of the plugin call tracing system.
 */

#include "PluginCallTracer.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>

namespace {
constexpr size_t kMaxDebugListEntries = 64;

void AppendBoundedUnique(std::vector<std::string> &items,
                         const std::string &value) {
    if(value.empty()) return;
    const auto existing = std::find(items.begin(), items.end(), value);
    if(existing != items.end()) items.erase(existing);
    items.push_back(value);
    if(items.size() > kMaxDebugListEntries)
        items.erase(items.begin(), items.begin() + (items.size() - kMaxDebugListEntries));
}
}

// ===========================================================================
// PluginCallTracer singleton
// ===========================================================================

PluginCallTracer &PluginCallTracer::Instance() {
    static PluginCallTracer *instance = []() {
        auto *tracer = new PluginCallTracer();
        std::atexit([]() { PluginCallTracer::Instance().Shutdown(); });
        return tracer;
    }();
    return *instance;
}

void PluginCallTracer::InitLogger(const std::string &logFilePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_shuttingDown || m_loggerInitialized) return;
    m_logFilePath = logFilePath;

    try {
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            logFilePath, /*truncate=*/true);
        m_logger = std::make_shared<spdlog::logger>("plugin_trace", sink);
        m_logger->set_pattern("[%H:%M:%S.%e] %v");
        m_logger->flush_on(spdlog::level::info);
        spdlog::register_logger(m_logger);
        m_loggerInitialized = true;
        m_logger->info("=== Plugin Call Trace Started ===");
    } catch (const std::exception &e) {
        spdlog::warn("PluginCallTracer: failed to create logger at '{}': {}",
                     logFilePath, e.what());
    }
}

void PluginCallTracer::SetLogFilePath(const std::string &logFilePath) {
    std::shared_ptr<spdlog::logger> previous;
    bool reopen = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_shuttingDown || m_logFilePath == logFilePath) return;
        previous = m_logger;
        m_logger.reset();
        m_loggerInitialized = false;
        m_logFilePath = logFilePath;
        reopen = m_enabled;
    }
    if(previous) {
        try {
            previous->flush();
            spdlog::drop("plugin_trace");
        } catch(...) {
        }
    }
    if(reopen) InitLogger(logFilePath);
}

void PluginCallTracer::SetEnabled(bool enabled) {
    std::shared_ptr<spdlog::logger> logger;
    std::string path;
    bool initialize = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_shuttingDown) return;
        m_enabled = enabled;
        initialize = enabled && !m_loggerInitialized && !m_logFilePath.empty();
        path = m_logFilePath;
    }
    if (initialize) {
        InitLogger(path);
    }
    logger = GetActiveLogger();
    if (logger) {
        try {
            logger->info("=== Plugin tracing {} ===",
                         enabled ? "enabled" : "disabled");
            logger->flush();
        } catch (...) {
        }
    }
}

void PluginCallTracer::EnsureLogger() {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_shuttingDown || m_loggerInitialized || m_logFilePath.empty()) return;
        path = m_logFilePath;
    }
    InitLogger(path);
}

void PluginCallTracer::Shutdown() {
    std::shared_ptr<spdlog::logger> logger;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_shuttingDown) return;
        m_shuttingDown = true;
        m_enabled = false;
        logger = m_logger;
        m_logger.reset();
        m_loggerInitialized = false;
    }
    if (logger) {
        try {
            logger->info("=== Plugin tracing disabled for shutdown ===");
            logger->flush();
            spdlog::drop("plugin_trace");
        } catch (...) {
        }
    }
}

std::shared_ptr<spdlog::logger> PluginCallTracer::GetActiveLogger() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_enabled || m_shuttingDown || !m_logger)
        return nullptr;
    return m_logger;
}

iTJSDispatch2 *PluginCallTracer::WrapDispatch(const ttstr &className,
                                               const ttstr &memberName,
                                               iTJSDispatch2 *original,
                                               tTJSNativeInstanceType type) {
    if (!original) return nullptr;

    // Always create proxies so that logging can be enabled at any time.
    // Each proxy checks IsEnabled() at call time.
    std::string cn, mn;
    { // Convert ttstr to narrow strings for logging
        tTJSNarrowStringHolder nc(className.c_str());
        tTJSNarrowStringHolder nm(memberName.c_str());
        cn = nc.operator const char *();
        mn = nm.operator const char *();
    }

    if (type == nitProperty) {
        return new PluginPropertyProxy(cn, mn, original);
    } else {
        // nitMethod, nitClass, etc. — all go through FuncCall dispatch
        return new PluginMethodProxy(cn, mn, original);
    }
}

void PluginCallTracer::LogMethodCall(const std::string &className,
                                     const std::string &memberName,
                                     tjs_int numparams,
                                     tTJSVariant **param) {
    if(IsEnabled()) {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        ++m_stats.methodCalls;
    }
    auto logger = GetActiveLogger();
    if (!logger) return;
    std::string msg = className + "." + memberName + "(argc=" +
                      std::to_string(numparams);

    // Append up to 4 argument representations
    const tjs_int maxArgs = numparams > 4 ? 4 : numparams;
    for (tjs_int i = 0; i < maxArgs; ++i) {
        if (param && param[i]) {
            try {
                ttstr s(*param[i]);
                tTJSNarrowStringHolder ns(s.c_str());
                std::string val = ns.operator const char *();
                // Truncate long values
                if (val.size() > 64) val.resize(64);
                msg += ", arg" + std::to_string(i) + "=" + val;
            } catch (...) {
                msg += ", arg" + std::to_string(i) + "=<?>";;
            }
        }
    }
    if (numparams > 4) msg += ", ...";
    msg += ")";

    try {
        logger->info(msg);
    } catch (...) {
    }
}

void PluginCallTracer::LogPropGet(const std::string &className,
                                  const std::string &memberName) {
    if(IsEnabled()) {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        ++m_stats.propertyGets;
    }
    auto logger = GetActiveLogger();
    if (!logger) return;
    try {
        logger->info("{}.{} [GET]", className, memberName);
    } catch (...) {
    }
}

void PluginCallTracer::LogPropSet(const std::string &className,
                                  const std::string &memberName,
                                  const tTJSVariant *value) {
    if(IsEnabled()) {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        ++m_stats.propertySets;
    }
    auto logger = GetActiveLogger();
    if (!logger) return;
    std::string valStr;
    if (value) {
        try {
            ttstr s(*value);
            tTJSNarrowStringHolder ns(s.c_str());
            valStr = ns.operator const char *();
            if (valStr.size() > 64) valStr.resize(64);
        } catch (...) {
            valStr = "<?>";
        }
    } else {
        valStr = "(null)";
    }
    try {
        logger->info("{}.{} [SET] {}", className, memberName, valStr);
    } catch (...) {
    }
}

// ===========================================================================
// PluginMethodProxy
// ===========================================================================

PluginMethodProxy::PluginMethodProxy(const std::string &className,
                                     const std::string &memberName,
                                     iTJSDispatch2 *original)
    : m_className(className), m_memberName(memberName), m_original(original) {
    if (m_original) m_original->AddRef();
}

PluginMethodProxy::~PluginMethodProxy() {
    if (m_original) m_original->Release();
}

tjs_uint PluginMethodProxy::AddRef() { return tTJSDispatch::AddRef(); }
tjs_uint PluginMethodProxy::Release() { return tTJSDispatch::Release(); }

tjs_error PluginMethodProxy::FuncCall(tjs_uint32 flag,
                                      const tjs_char *membername,
                                      tjs_uint32 *hint, tTJSVariant *result,
                                      tjs_int numparams, tTJSVariant **param,
                                      iTJSDispatch2 *objthis) {
    if (!membername && PluginCallTracer::Instance().IsEnabled()) {
        PluginCallTracer::Instance().LogMethodCall(
            m_className, m_memberName, numparams, param);
    }
    return m_original->FuncCall(flag, membername, hint, result, numparams, param, objthis);
}

// --- Delegate everything else ---

tjs_error PluginMethodProxy::FuncCallByNum(tjs_uint32 flag, tjs_int num,
                                           tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
    return m_original->FuncCallByNum(flag, num, result, numparams, param, objthis);
}

tjs_error PluginMethodProxy::PropGet(tjs_uint32 flag,
                                     const tjs_char *membername,
                                     tjs_uint32 *hint, tTJSVariant *result,
                                     iTJSDispatch2 *objthis) {
    return m_original->PropGet(flag, membername, hint, result, objthis);
}

tjs_error PluginMethodProxy::PropGetByNum(tjs_uint32 flag, tjs_int num,
                                          tTJSVariant *result,
                                          iTJSDispatch2 *objthis) {
    return m_original->PropGetByNum(flag, num, result, objthis);
}

tjs_error PluginMethodProxy::PropSet(tjs_uint32 flag,
                                     const tjs_char *membername,
                                     tjs_uint32 *hint, const tTJSVariant *param,
                                     iTJSDispatch2 *objthis) {
    return m_original->PropSet(flag, membername, hint, param, objthis);
}

tjs_error PluginMethodProxy::PropSetByNum(tjs_uint32 flag, tjs_int num,
                                          const tTJSVariant *param,
                                          iTJSDispatch2 *objthis) {
    return m_original->PropSetByNum(flag, num, param, objthis);
}

tjs_error PluginMethodProxy::GetCount(tjs_int *result,
                                      const tjs_char *membername,
                                      tjs_uint32 *hint,
                                      iTJSDispatch2 *objthis) {
    return m_original->GetCount(result, membername, hint, objthis);
}

tjs_error PluginMethodProxy::GetCountByNum(tjs_int *result, tjs_int num,
                                           iTJSDispatch2 *objthis) {
    return m_original->GetCountByNum(result, num, objthis);
}

tjs_error PluginMethodProxy::PropSetByVS(tjs_uint32 flag,
                                         tTJSVariantString *membername,
                                         const tTJSVariant *param,
                                         iTJSDispatch2 *objthis) {
    return m_original->PropSetByVS(flag, membername, param, objthis);
}

tjs_error PluginMethodProxy::EnumMembers(tjs_uint32 flag,
                                         tTJSVariantClosure *callback,
                                         iTJSDispatch2 *objthis) {
    return m_original->EnumMembers(flag, callback, objthis);
}

tjs_error PluginMethodProxy::DeleteMember(tjs_uint32 flag,
                                          const tjs_char *membername,
                                          tjs_uint32 *hint,
                                          iTJSDispatch2 *objthis) {
    return m_original->DeleteMember(flag, membername, hint, objthis);
}

tjs_error PluginMethodProxy::DeleteMemberByNum(tjs_uint32 flag, tjs_int num,
                                               iTJSDispatch2 *objthis) {
    return m_original->DeleteMemberByNum(flag, num, objthis);
}

tjs_error PluginMethodProxy::Invalidate(tjs_uint32 flag,
                                        const tjs_char *membername,
                                        tjs_uint32 *hint,
                                        iTJSDispatch2 *objthis) {
    return m_original->Invalidate(flag, membername, hint, objthis);
}

tjs_error PluginMethodProxy::InvalidateByNum(tjs_uint32 flag, tjs_int num,
                                             iTJSDispatch2 *objthis) {
    return m_original->InvalidateByNum(flag, num, objthis);
}

tjs_error PluginMethodProxy::IsValid(tjs_uint32 flag,
                                     const tjs_char *membername,
                                     tjs_uint32 *hint,
                                     iTJSDispatch2 *objthis) {
    return m_original->IsValid(flag, membername, hint, objthis);
}

tjs_error PluginMethodProxy::IsValidByNum(tjs_uint32 flag, tjs_int num,
                                          iTJSDispatch2 *objthis) {
    return m_original->IsValidByNum(flag, num, objthis);
}

tjs_error PluginMethodProxy::CreateNew(tjs_uint32 flag,
                                       const tjs_char *membername,
                                       tjs_uint32 *hint,
                                       iTJSDispatch2 **result,
                                       tjs_int numparams,
                                       tTJSVariant **param,
                                       iTJSDispatch2 *objthis) {
    return m_original->CreateNew(flag, membername, hint, result, numparams, param, objthis);
}

tjs_error PluginMethodProxy::CreateNewByNum(tjs_uint32 flag, tjs_int num,
                                            iTJSDispatch2 **result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *objthis) {
    return m_original->CreateNewByNum(flag, num, result, numparams, param, objthis);
}

tjs_error PluginMethodProxy::Reserved1() {
    return m_original->Reserved1();
}

tjs_error PluginMethodProxy::IsInstanceOf(tjs_uint32 flag,
                                          const tjs_char *membername,
                                          tjs_uint32 *hint,
                                          const tjs_char *classname,
                                          iTJSDispatch2 *objthis) {
    return m_original->IsInstanceOf(flag, membername, hint, classname, objthis);
}

tjs_error PluginMethodProxy::IsInstanceOfByNum(tjs_uint32 flag, tjs_int num,
                                               const tjs_char *classname,
                                               iTJSDispatch2 *objthis) {
    return m_original->IsInstanceOfByNum(flag, num, classname, objthis);
}

tjs_error PluginMethodProxy::Operation(tjs_uint32 flag,
                                       const tjs_char *membername,
                                       tjs_uint32 *hint, tTJSVariant *result,
                                       const tTJSVariant *param,
                                       iTJSDispatch2 *objthis) {
    return m_original->Operation(flag, membername, hint, result, param, objthis);
}

tjs_error PluginMethodProxy::OperationByNum(tjs_uint32 flag, tjs_int num,
                                            tTJSVariant *result,
                                            const tTJSVariant *param,
                                            iTJSDispatch2 *objthis) {
    return m_original->OperationByNum(flag, num, result, param, objthis);
}

tjs_error PluginMethodProxy::NativeInstanceSupport(tjs_uint32 flag,
                                                   tjs_int32 classid,
                                                   iTJSNativeInstance **pointer) {
    return m_original->NativeInstanceSupport(flag, classid, pointer);
}

tjs_error PluginMethodProxy::ClassInstanceInfo(tjs_uint32 flag, tjs_uint num,
                                               tTJSVariant *value) {
    return m_original->ClassInstanceInfo(flag, num, value);
}

tjs_error PluginMethodProxy::Reserved2() { return m_original->Reserved2(); }
tjs_error PluginMethodProxy::Reserved3() { return m_original->Reserved3(); }

// ===========================================================================
// PluginPropertyProxy
// ===========================================================================

PluginPropertyProxy::PluginPropertyProxy(const std::string &className,
                                         const std::string &memberName,
                                         iTJSDispatch2 *original)
    : m_className(className), m_memberName(memberName), m_original(original) {
    if (m_original) m_original->AddRef();
}

PluginPropertyProxy::~PluginPropertyProxy() {
    if (m_original) m_original->Release();
}

tjs_uint PluginPropertyProxy::AddRef() { return tTJSDispatch::AddRef(); }
tjs_uint PluginPropertyProxy::Release() { return tTJSDispatch::Release(); }

tjs_error PluginPropertyProxy::PropGet(tjs_uint32 flag,
                                       const tjs_char *membername,
                                       tjs_uint32 *hint, tTJSVariant *result,
                                       iTJSDispatch2 *objthis) {
    if (!membername && PluginCallTracer::Instance().IsEnabled()) {
        PluginCallTracer::Instance().LogPropGet(m_className, m_memberName);
    }
    return m_original->PropGet(flag, membername, hint, result, objthis);
}

tjs_error PluginPropertyProxy::PropSet(tjs_uint32 flag,
                                       const tjs_char *membername,
                                       tjs_uint32 *hint,
                                       const tTJSVariant *param,
                                       iTJSDispatch2 *objthis) {
    if (!membername && PluginCallTracer::Instance().IsEnabled()) {
        PluginCallTracer::Instance().LogPropSet(m_className, m_memberName, param);
    }
    return m_original->PropSet(flag, membername, hint, param, objthis);
}

tjs_error PluginPropertyProxy::FuncCall(tjs_uint32 flag,
                                        const tjs_char *membername,
                                        tjs_uint32 *hint, tTJSVariant *result,
                                        tjs_int numparams, tTJSVariant **param,
                                        iTJSDispatch2 *objthis) {
    return m_original->FuncCall(flag, membername, hint, result, numparams, param, objthis);
}

// --- Delegate everything else ---

tjs_error PluginPropertyProxy::FuncCallByNum(tjs_uint32 flag, tjs_int num,
                                             tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *objthis) {
    return m_original->FuncCallByNum(flag, num, result, numparams, param, objthis);
}

tjs_error PluginPropertyProxy::PropGetByNum(tjs_uint32 flag, tjs_int num,
                                            tTJSVariant *result,
                                            iTJSDispatch2 *objthis) {
    return m_original->PropGetByNum(flag, num, result, objthis);
}

tjs_error PluginPropertyProxy::PropSetByNum(tjs_uint32 flag, tjs_int num,
                                            const tTJSVariant *param,
                                            iTJSDispatch2 *objthis) {
    return m_original->PropSetByNum(flag, num, param, objthis);
}

tjs_error PluginPropertyProxy::GetCount(tjs_int *result,
                                        const tjs_char *membername,
                                        tjs_uint32 *hint,
                                        iTJSDispatch2 *objthis) {
    return m_original->GetCount(result, membername, hint, objthis);
}

tjs_error PluginPropertyProxy::GetCountByNum(tjs_int *result, tjs_int num,
                                             iTJSDispatch2 *objthis) {
    return m_original->GetCountByNum(result, num, objthis);
}

tjs_error PluginPropertyProxy::PropSetByVS(tjs_uint32 flag,
                                           tTJSVariantString *membername,
                                           const tTJSVariant *param,
                                           iTJSDispatch2 *objthis) {
    return m_original->PropSetByVS(flag, membername, param, objthis);
}

tjs_error PluginPropertyProxy::EnumMembers(tjs_uint32 flag,
                                           tTJSVariantClosure *callback,
                                           iTJSDispatch2 *objthis) {
    return m_original->EnumMembers(flag, callback, objthis);
}

tjs_error PluginPropertyProxy::DeleteMember(tjs_uint32 flag,
                                            const tjs_char *membername,
                                            tjs_uint32 *hint,
                                            iTJSDispatch2 *objthis) {
    return m_original->DeleteMember(flag, membername, hint, objthis);
}

tjs_error PluginPropertyProxy::DeleteMemberByNum(tjs_uint32 flag, tjs_int num,
                                                 iTJSDispatch2 *objthis) {
    return m_original->DeleteMemberByNum(flag, num, objthis);
}

tjs_error PluginPropertyProxy::Invalidate(tjs_uint32 flag,
                                          const tjs_char *membername,
                                          tjs_uint32 *hint,
                                          iTJSDispatch2 *objthis) {
    return m_original->Invalidate(flag, membername, hint, objthis);
}

tjs_error PluginPropertyProxy::InvalidateByNum(tjs_uint32 flag, tjs_int num,
                                               iTJSDispatch2 *objthis) {
    return m_original->InvalidateByNum(flag, num, objthis);
}

tjs_error PluginPropertyProxy::IsValid(tjs_uint32 flag,
                                       const tjs_char *membername,
                                       tjs_uint32 *hint,
                                       iTJSDispatch2 *objthis) {
    return m_original->IsValid(flag, membername, hint, objthis);
}

tjs_error PluginPropertyProxy::IsValidByNum(tjs_uint32 flag, tjs_int num,
                                            iTJSDispatch2 *objthis) {
    return m_original->IsValidByNum(flag, num, objthis);
}

tjs_error PluginPropertyProxy::CreateNew(tjs_uint32 flag,
                                         const tjs_char *membername,
                                         tjs_uint32 *hint,
                                         iTJSDispatch2 **result,
                                         tjs_int numparams,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *objthis) {
    return m_original->CreateNew(flag, membername, hint, result, numparams, param, objthis);
}

tjs_error PluginPropertyProxy::CreateNewByNum(tjs_uint32 flag, tjs_int num,
                                              iTJSDispatch2 **result,
                                              tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *objthis) {
    return m_original->CreateNewByNum(flag, num, result, numparams, param, objthis);
}

tjs_error PluginPropertyProxy::Reserved1() {
    return m_original->Reserved1();
}

tjs_error PluginPropertyProxy::IsInstanceOf(tjs_uint32 flag,
                                            const tjs_char *membername,
                                            tjs_uint32 *hint,
                                            const tjs_char *classname,
                                            iTJSDispatch2 *objthis) {
    return m_original->IsInstanceOf(flag, membername, hint, classname, objthis);
}

tjs_error PluginPropertyProxy::IsInstanceOfByNum(tjs_uint32 flag, tjs_int num,
                                                 const tjs_char *classname,
                                                 iTJSDispatch2 *objthis) {
    return m_original->IsInstanceOfByNum(flag, num, classname, objthis);
}

tjs_error PluginPropertyProxy::Operation(tjs_uint32 flag,
                                         const tjs_char *membername,
                                         tjs_uint32 *hint, tTJSVariant *result,
                                         const tTJSVariant *param,
                                         iTJSDispatch2 *objthis) {
    return m_original->Operation(flag, membername, hint, result, param, objthis);
}

tjs_error PluginPropertyProxy::OperationByNum(tjs_uint32 flag, tjs_int num,
                                              tTJSVariant *result,
                                              const tTJSVariant *param,
                                              iTJSDispatch2 *objthis) {
    return m_original->OperationByNum(flag, num, result, param, objthis);
}

tjs_error PluginPropertyProxy::NativeInstanceSupport(tjs_uint32 flag,
                                                     tjs_int32 classid,
                                                     iTJSNativeInstance **pointer) {
    return m_original->NativeInstanceSupport(flag, classid, pointer);
}

tjs_error PluginPropertyProxy::ClassInstanceInfo(tjs_uint32 flag, tjs_uint num,
                                                 tTJSVariant *value) {
    return m_original->ClassInstanceInfo(flag, num, value);
}

tjs_error PluginPropertyProxy::Reserved2() { return m_original->Reserved2(); }
tjs_error PluginPropertyProxy::Reserved3() { return m_original->Reserved3(); }

// ===========================================================================
// Registration phase logging
// ===========================================================================

static const char *TypeToStr(tTJSNativeInstanceType type) {
    switch (type) {
    case nitMethod:   return "method";
    case nitProperty: return "property";
    case nitClass:    return "class";
    default:          return "unknown";
    }
}

void PluginCallTracer::LogRegistrationStart() {
    auto logger = GetActiveLogger();
    if (!logger) return;
    try {
        logger->info("");
        logger->info("====== Plugin Registration ======");
    } catch (...) {
    }
}

void PluginCallTracer::LogModuleStart(const std::string &moduleName) {
    auto logger = GetActiveLogger();
    if (!logger) return;
    try {
        logger->info("--- Module: {} ---", moduleName);
    } catch (...) {
    }
}

void PluginCallTracer::LogRegistration(const ttstr &className,
                                       const ttstr &memberName,
                                       tTJSNativeInstanceType type,
                                       tjs_uint32 flags) {
    auto logger = GetActiveLogger();
    if (!logger) return;
    tTJSNarrowStringHolder nc(className.c_str());
    tTJSNarrowStringHolder nm(memberName.c_str());
    std::string cn = nc.operator const char *();
    std::string mn = nm.operator const char *();
    const char *ts = TypeToStr(type);
    bool isStatic = (flags & TJS_STATICMEMBER) != 0;

    try {
        logger->info("  [{}] {}.{}{}", ts, cn, mn,
                     isStatic ? " (static)" : "");
    } catch (...) {
    }
}

void PluginCallTracer::LogRegistrationEnd() {
    auto logger = GetActiveLogger();
    if (!logger) return;
    try {
        logger->info("====== Registration Complete ======");
        logger->info("");
        logger->flush();
    } catch (...) {
    }
}

void PluginCallTracer::LogPluginLoad(const std::string &name, bool success,
                                     const char *stub) {
    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        if(success) {
            ++m_stats.loadSucceeded;
            AppendBoundedUnique(m_stats.loadedPlugins, name);
        } else {
            ++m_stats.loadFailed;
            AppendBoundedUnique(m_stats.failedPlugins, name);
            if(stub) {
                ++m_stats.loadFallback;
                AppendBoundedUnique(m_stats.fallbackPlugins,
                                    name + " -> " + stub);
            }
        }
    }
    auto logger = GetActiveLogger();
    if (!logger) return;
    try {
        if (success) {
            logger->info("[Plugin] {} loaded OK", name);
        } else if (stub) {
            logger->info("[Plugin] {} MISSING → fallback: {}", name, stub);
        } else {
            logger->info("[Plugin] {} MISSING (no fallback)", name);
        }
        logger->flush();
    } catch (...) {
    }
}

void PluginCallTracer::LogMissingMember(const tjs_char *membername,
                                         const char *operation,
                                         iTJSDispatch2 *obj) {
    tTJSNarrowStringHolder ns(membername);
    std::string className;
    if (obj) {
        // Try to get the first class name from ClassInstanceInfo
        tTJSVariant val;
        if (TJS_SUCCEEDED(obj->ClassInstanceInfo(TJS_CII_GET, 0, &val))) {
            ttstr cn(val);
            tTJSNarrowStringHolder nc(cn.c_str());
            className = nc.operator const char *();
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        ++m_stats.missingMembers;
        std::string item = className.empty() ? std::string{} : className + ".";
        item += ns.operator const char *();
        item += " [";
        item += operation ? operation : "unknown";
        item += "]";
        AppendBoundedUnique(m_stats.recentMissingMembers, item);
    }

    if (auto logger = GetActiveLogger()) {
        try {
            if (className.empty()) {
                logger->info("[MISSING] {} \"{}\"", operation,
                             ns.operator const char *());
            } else {
                logger->info("[MISSING] {}.{} \"{}\"", className, operation,
                             ns.operator const char *());
            }
        } catch (...) {
        }
    }

    const char *verbose_missing = std::getenv("AETHERKIRI_TRACE_MISSING_MEMBERS");
    if (verbose_missing && *verbose_missing) {
        if (className.empty()) {
            spdlog::debug("TJS missing member {} at {}", ns.operator const char *(), operation);
        } else {
            spdlog::debug("TJS missing member {}.{} at {}", className, ns.operator const char *(), operation);
        }
    }
}

PluginDebugSnapshot PluginCallTracer::GetDebugSnapshot() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    auto snapshot = m_stats;
    snapshot.tracingEnabled = IsEnabled();
    return snapshot;
}

void PluginCallTracer::ResetDebugStats() {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats = PluginDebugSnapshot{};
    m_stats.tracingEnabled = IsEnabled();
}
