//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Utilities for Debugging
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <deque>
#include <algorithm>
#include <ctime>
#include <vector>
#include "DebugIntf.h"
#include "MsgIntf.h"
#include "StorageIntf.h"
#include "SysInitIntf.h"
#include "SysInitImpl.h"
#include "tjsUtils.h"

#ifdef ENABLE_DEBUGGER
#include "tjsDebug.h"
#endif // ENABLE_DEBUGGER

#include "Application.h"
#include "SystemControl.h"

namespace {

// Keep the formatter in Aether's DebugIntf owner, but follow the upstream
// krkrz pretty-print contract for arrays/dictionaries: bounded recursion,
// compact/non-compact output, and cycle detection.  This is intentionally a
// method-level port rather than a second DebugIntf translation unit, because
// the surrounding logging state and native class are Aether-owned.
struct TVPPrettyPrintContext {
    int remaining_depth = 0;
    bool compact = false;
    std::vector<iTJSDispatch2 *> stack;
};

ttstr TVPPrettyIndent(int level) {
    ttstr result;
    for(int i = 0; i < level; ++i)
        result += TJS_W("  ");
    return result;
}

ttstr TVPPrettyPrintImpl(const tTJSVariant &value,
                         TVPPrettyPrintContext &context, int indent);

struct TVPPrettyPrintEnumCallback : public tTJSDispatch {
    TVPPrettyPrintContext *context = nullptr;
    int indent = 0;
    std::vector<std::pair<ttstr, ttstr>> *entries = nullptr;

    tjs_error TJS_INTF_METHOD FuncCall(
        tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *result,
        tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
        if(numparams < 3)
            return TJS_E_BADPARAMCOUNT;
        const tjs_uint32 flags = static_cast<tjs_int>(*param[1]);
        if(flags & TJS_HIDDENMEMBER) {
            if(result)
                *result = static_cast<tjs_int>(1);
            return TJS_S_OK;
        }
        ttstr key = ttstr(TJS_W("\"")) + ttstr(*param[0]).EscapeC() +
                    TJS_W("\"");
        ttstr val;
        try {
            val = TVPPrettyPrintImpl(*param[2], *context, indent);
        } catch(...) {
            val = TJS_W("(?)");
        }
        if(entries)
            entries->emplace_back(std::move(key), std::move(val));
        if(result)
            *result = static_cast<tjs_int>(1);
        return TJS_S_OK;
    }
};

bool TVPPrettyIsInstanceOf(iTJSDispatch2 *dispatch, const tjs_char *name) {
    if(!dispatch)
        return false;
    try {
        return dispatch->IsInstanceOf(0, nullptr, nullptr, name, dispatch) ==
               TJS_S_TRUE;
    } catch(...) {
        return false;
    }
}

ttstr TVPPrettyPrintArray(iTJSDispatch2 *dispatch,
                          TVPPrettyPrintContext &context, int indent) {
    tTJSVariant count_variant;
    if(TJS_FAILED(dispatch->PropGet(0, TJS_W("count"), nullptr,
                                    &count_variant, dispatch)))
        return TJS_W("(Array)");
    const tjs_int count = static_cast<tjs_int>(count_variant.AsInteger());
    if(count <= 0)
        return TJS_W("[]");
    if(context.remaining_depth <= 0)
        return TJS_W("[...]");

    --context.remaining_depth;
    std::vector<ttstr> elements;
    elements.reserve(static_cast<size_t>(count));
    for(tjs_int i = 0; i < count; ++i) {
        tTJSVariant element;
        if(TJS_FAILED(dispatch->PropGetByNum(0, i, &element, dispatch))) {
            elements.emplace_back(TJS_W("(?)"));
            continue;
        }
        elements.push_back(TVPPrettyPrintImpl(element, context, indent + 1));
    }
    ++context.remaining_depth;

    ttstr result = TJS_W("[");
    if(context.compact) {
        for(size_t i = 0; i < elements.size(); ++i) {
            if(i)
                result += TJS_W(", ");
            result += elements[i];
        }
        result += TJS_W("]");
        return result;
    }
    result += TJS_W("\n");
    const ttstr inner = TVPPrettyIndent(indent + 1);
    const ttstr outer = TVPPrettyIndent(indent);
    for(size_t i = 0; i < elements.size(); ++i) {
        result += inner;
        result += elements[i];
        if(i + 1 < elements.size())
            result += TJS_W(",");
        result += TJS_W("\n");
    }
    result += outer;
    result += TJS_W("]");
    return result;
}

ttstr TVPPrettyPrintDictionary(iTJSDispatch2 *dispatch,
                               TVPPrettyPrintContext &context, int indent) {
    if(context.remaining_depth <= 0)
        return TJS_W("%[...]");

    --context.remaining_depth;
    std::vector<std::pair<ttstr, ttstr>> entries;
    TVPPrettyPrintEnumCallback callback;
    callback.context = &context;
    callback.indent = indent + 1;
    callback.entries = &entries;
    tTJSVariantClosure closure(&callback, nullptr);
    try {
        dispatch->EnumMembers(TJS_IGNOREPROP, &closure, dispatch);
    } catch(...) {
        // A property getter may throw while a debugger is inspecting it.  A
        // partial dictionary is more useful than aborting the entire DAP
        // response.
    }
    ++context.remaining_depth;

    if(entries.empty())
        return TJS_W("%[]");
    ttstr result = TJS_W("%[");
    if(context.compact) {
        for(size_t i = 0; i < entries.size(); ++i) {
            if(i)
                result += TJS_W(", ");
            result += entries[i].first;
            result += TJS_W(" => ");
            result += entries[i].second;
        }
        result += TJS_W("]");
        return result;
    }
    result += TJS_W("\n");
    const ttstr inner = TVPPrettyIndent(indent + 1);
    const ttstr outer = TVPPrettyIndent(indent);
    for(size_t i = 0; i < entries.size(); ++i) {
        result += inner;
        result += entries[i].first;
        result += TJS_W(" => ");
        result += entries[i].second;
        if(i + 1 < entries.size())
            result += TJS_W(",");
        result += TJS_W("\n");
    }
    result += outer;
    result += TJS_W("]");
    return result;
}

ttstr TVPPrettyPrintObject(const tTJSVariant &value,
                           TVPPrettyPrintContext &context, int indent) {
    tTJSVariantClosure closure = value.AsObjectClosureNoAddRef();
    iTJSDispatch2 *dispatch = closure.SelectObjectNoAddRef();
    if(!dispatch)
        return TJS_W("(null)");
    for(iTJSDispatch2 *seen : context.stack) {
        if(seen == dispatch)
            return TJS_W("(recursion)");
    }

    context.stack.push_back(dispatch);
    ttstr result;
    try {
        if(TVPPrettyIsInstanceOf(dispatch, TJS_W("Array")))
            result = TVPPrettyPrintArray(dispatch, context, indent);
        else if(TVPPrettyIsInstanceOf(dispatch, TJS_W("Dictionary")))
            result = TVPPrettyPrintDictionary(dispatch, context, indent);
        else if(TVPPrettyIsInstanceOf(dispatch, TJS_W("Function")))
            result = TJS_W("(function)");
        else if(TVPPrettyIsInstanceOf(dispatch, TJS_W("Class")))
            result = TJS_W("(class)");
        else if(TVPPrettyIsInstanceOf(dispatch, TJS_W("Property")))
            result = TJS_W("(property)");
        else
            result = TJS_W("(object)");
    } catch(...) {
        result = TJS_W("(object)");
    }
    context.stack.pop_back();
    return result;
}

ttstr TVPPrettyPrintImpl(const tTJSVariant &value,
                         TVPPrettyPrintContext &context, int indent) {
    switch(value.Type()) {
    case tvtVoid:
        return TJS_W("(void)");
    case tvtObject:
        return TVPPrettyPrintObject(value, context, indent);
    case tvtString:
    case tvtInteger:
    case tvtReal:
    case tvtOctet:
    default:
        return TJS::TJSVariantToExpressionString(value);
    }
}

} // namespace

ttstr TVPPrettyPrint(const tTJSVariant &variant, int depth, bool compact) {
    TVPPrettyPrintContext context;
    // Prevent an accidentally huge DAP request from causing unbounded
    // recursion while retaining the upstream meaning of non-positive depth.
    context.remaining_depth = std::max(0, std::min(depth, 64));
    context.compact = compact;
    return TVPPrettyPrintImpl(variant, context, 0);
}

//---------------------------------------------------------------------------
// global variables
//---------------------------------------------------------------------------
struct tTVPLogItem {
    ttstr Log;
    ttstr Time;

    tTVPLogItem(const ttstr &log, const ttstr &time) {
        Log = log;
        Time = time;
    }
};

static std::deque<tTVPLogItem> *TVPLogDeque = nullptr;
static tjs_uint TVPLogMaxLines = 2048;

bool TVPAutoLogToFileOnError = true;
bool TVPAutoClearLogOnError = false;
bool TVPLoggingToFile = false;
static tjs_uint TVPLogToFileRollBack = 100;
static ttstr *TVPImportantLogs = nullptr;
static ttstr TVPLogLocation;
ttstr TVPNativeLogLocation;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static bool TVPLogObjectsInitialized = false;

static void TVPEnsureLogObjects() {
    if(TVPLogObjectsInitialized)
        return;
    TVPLogObjectsInitialized = true;

    TVPLogDeque = new std::deque<tTVPLogItem>();
    TVPImportantLogs = new ttstr();
}

//---------------------------------------------------------------------------
static void TVPDestroyLogObjects() {
    if(TVPLogDeque)
        delete TVPLogDeque, TVPLogDeque = nullptr;
    if(TVPImportantLogs)
        delete TVPImportantLogs, TVPImportantLogs = nullptr;
}

//---------------------------------------------------------------------------
tTVPAtExit TVPDestroyLogObjectsAtExit(TVP_ATEXIT_PRI_CLEANUP,
                                      TVPDestroyLogObjects);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void (*TVPOnLog)(const ttstr &line) = nullptr;
// this function is invoked when a line is logged
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPSetOnLog
//---------------------------------------------------------------------------
void TVPSetOnLog(void (*func)(const ttstr &line)) { TVPOnLog = func; }

//---------------------------------------------------------------------------
static std::vector<tTJSVariantClosure> TVPLoggingHandlerVector;

static void TVPCleanupLoggingHandlerVector() {
    // eliminate empty
    std::vector<tTJSVariantClosure>::iterator i;
    for(i = TVPLoggingHandlerVector.begin(); i != TVPLoggingHandlerVector.end();
        i++) {
        if(!i->Object) {
            i->Release();
            i = TVPLoggingHandlerVector.erase(i);
        } else {
            i++;
        }
    }
}

static void TVPDestroyLoggingHandlerVector() {
    TVPSetOnLog(nullptr);
    std::vector<tTJSVariantClosure>::iterator i;
    for(i = TVPLoggingHandlerVector.begin(); i != TVPLoggingHandlerVector.end();
        i++) {
        i->Release();
    }
    TVPLoggingHandlerVector.clear();
}

static tTVPAtExit
    TVPDestroyLoggingHandlerAtExit(TVP_ATEXIT_PRI_PREPARE,
                                   TVPDestroyLoggingHandlerVector);
//---------------------------------------------------------------------------
static bool TVPInDeliverLoggingEvent = false;

static void _TVPDeliverLoggingEvent(const ttstr &line) // internal
{
    if(!TVPInDeliverLoggingEvent) {
        TVPInDeliverLoggingEvent = true;
        try {
            if(TVPLoggingHandlerVector.size()) {
                bool emptyflag = false;
                tTJSVariant vline(line);
                tTJSVariant *pvline[] = { &vline };
                for(auto &i : TVPLoggingHandlerVector) {
                    if(i.Object) {
                        tjs_error er;
                        try {
                            er = i.FuncCall(0, nullptr, nullptr, nullptr, 1,
                                            pvline, nullptr);
                        } catch(...) {
                            // failed
                            i.Release();
                            i.Object = i.ObjThis = nullptr;
                            throw;
                        }
                        if(TJS_FAILED(er)) {
                            // failed
                            i.Release();
                            i.Object = i.ObjThis = nullptr;
                            emptyflag = true;
                        }
                    } else {
                        emptyflag = true;
                    }
                }

                if(emptyflag) {
                    // the array has empty cell
                    TVPCleanupLoggingHandlerVector();
                }
            }

            if(TVPLoggingHandlerVector.empty()) {
                TVPSetOnLog(nullptr);
            }
        } catch(...) {
            TVPInDeliverLoggingEvent = false;
            throw;
        }
        TVPInDeliverLoggingEvent = false;
    }
}

//---------------------------------------------------------------------------
static void TVPAddLoggingHandler(tTJSVariantClosure clo) {
    std::vector<tTJSVariantClosure>::iterator i;
    i = std::find(TVPLoggingHandlerVector.begin(),
                  TVPLoggingHandlerVector.end(), clo);
    if(i == TVPLoggingHandlerVector.end()) {
        clo.AddRef();
        TVPLoggingHandlerVector.push_back(clo);
        TVPSetOnLog(&_TVPDeliverLoggingEvent);
    }
}

//---------------------------------------------------------------------------
static void TVPRemoveLoggingHandler(tTJSVariantClosure clo) {
    std::vector<tTJSVariantClosure>::iterator i;
    i = std::find(TVPLoggingHandlerVector.begin(),
                  TVPLoggingHandlerVector.end(), clo);
    if(i != TVPLoggingHandlerVector.end()) {
        i->Release();
        i->Object = i->ObjThis = nullptr;
    }

    if(!TVPInDeliverLoggingEvent) {
        TVPCleanupLoggingHandlerVector();
        if(TVPLoggingHandlerVector.empty()) {
            TVPSetOnLog(nullptr);
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// log stream holder
//---------------------------------------------------------------------------
class tTVPLogStreamHolder {
    FILE *Stream;
    bool Alive;
    bool OpenFailed;

public:
    tTVPLogStreamHolder() {
        Stream = nullptr;
        Alive = true;
        OpenFailed = false;
    }

    ~tTVPLogStreamHolder() {
        if(Stream)
            fclose(Stream);
        Alive = false;
    }

private:
    void Open(const tjs_nchar *mode);

public:
    void Clear(); // clear log stream
    void Log(const ttstr &text); // log given text

    void Reopen() {
        if(Stream)
            fclose(Stream);
        Stream = nullptr;
        Alive = false;
        OpenFailed = false;
    } // reopen log stream

} static TVPLogStreamHolder;

//---------------------------------------------------------------------------
void tTVPLogStreamHolder::Open(const tjs_nchar *mode) {
    if(OpenFailed)
        return; // no more try

    try {
        ttstr filename;
        if(TVPLogLocation.GetLen() == 0) {
            Stream = nullptr;
            OpenFailed = true;
        } else {
            // no log location specified
            filename = TVPNativeLogLocation + TJS_W("/krkr.console.log");
            TVPEnsureDataPathDirectory();
            std::string _filename = filename.AsStdString();
            Stream = fopen(_filename.c_str(), mode);
            if(!Stream)
                OpenFailed = true;
        }

        if(Stream) {
            fseek(Stream, 0, SEEK_END);
            if(ftell(Stream) == 0) {
                // write BOM
                // TODO: 32-bit unicode support
                fwrite(TJS_N("\xff\xfe"), 1, 2,
                       Stream); // indicate unicode text
            }

#ifdef TJS_TEXT_OUT_CRLF
            ttstr separator(TVPSeparatorCRLF);
#else
            ttstr separator(TVPSeparatorCR);
#endif
            Log(separator);

            static tjs_char timebuf[80];

            tm *struct_tm;
            time_t timer;
            timer = time(&timer);

            struct_tm = localtime(&timer);
            TJS_strftime(timebuf, 79, TJS_W("%#c"), struct_tm);

            Log(ttstr(TJS_W("Logging to ")) + ttstr(filename) +
                TJS_W(" started on ") + timebuf);
        }
    } catch(...) {
        OpenFailed = true;
    }
}

//---------------------------------------------------------------------------
void tTVPLogStreamHolder::Clear() {
    // clear log text
    if(Stream)
        fclose(Stream);

    Open(TJS_N("wb"));
}

//---------------------------------------------------------------------------
void tTVPLogStreamHolder::Log(const ttstr &text) {
    if(!Stream)
        Open(TJS_N("ab"));

    try {
        if(Stream) {
            size_t len = text.GetLen() * sizeof(tjs_char);
            if(len != fwrite(text.c_str(), 1, len, Stream)) {
                // cannot write
                fclose(Stream);
                OpenFailed = true;
                return;
            }
#ifdef TJS_TEXT_OUT_CRLF
            fwrite(TJS_W("\r\n"), 1, 2 * sizeof(tjs_char), Stream);
#else
            fwrite(TJS_W("\n"), 1, 1 * sizeof(tjs_char), Stream);
#endif

            // flush
            fflush(Stream);
        }
    } catch(...) {
        try {
            if(Stream)
                fclose(Stream);
        } catch(...) {
        }

        OpenFailed = true;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPAddLog
//---------------------------------------------------------------------------
void TVPAddLog(const ttstr &line, bool appendtoimportant) {
    // add a line to the log.
    // exceeded lines over TVPLogMaxLines are eliminated.
    // this function is not thread-safe ...

    TVPEnsureLogObjects();
    if(!TVPLogDeque)
        return; // log system is shuttingdown
    if(!TVPImportantLogs)
        return; // log system is shuttingdown

    static time_t prevlogtime = 0;
    static ttstr prevtimebuf;
    static tjs_char timebuf[40];

    tm *struct_tm;
    time_t timer;
    timer = time(&timer);

    if(prevlogtime != timer) {
        struct_tm = localtime(&timer);
        TJS_strftime(timebuf, 39, TJS_W("%H:%M:%S"), struct_tm);
        prevlogtime = timer;
        prevtimebuf = timebuf;
    }

    TVPLogDeque->emplace_back(line, prevtimebuf);

    if(appendtoimportant) {
#ifdef TJS_TEXT_OUT_CRLF
        *TVPImportantLogs +=
            ttstr(timebuf) + TJS_W(" ! ") + line + TJS_W("\r\n");
#else
        *TVPImportantLogs += ttstr(timebuf) + TJS_W(" ! ") + line + TJS_W("\n");
#endif
    }
    while(TVPLogDeque->size() >= TVPLogMaxLines + 100) {
        auto i = TVPLogDeque->begin();
        TVPLogDeque->erase(i, i + 100);
    }

    // FIXME: need fix get timebuf
    // FIXME: remove prefix `2` log message: 2 xxxx
    //    tjs_int timebuflen = (tjs_int)TJS_strlen(timebuf);
    ttstr buf((tTJSStringBufferLength)(/*timebuflen + 1 +*/ line.GetLen()));
    tjs_char *p = buf.Independ();
    //    TJS_strcpy(p, timebuf);
    //    p += timebuflen;
    *p = TJS_W(' ');
    p++;
    TJS_strcpy(p, line.c_str());
    if(TVPOnLog)
        TVPOnLog(buf);

    Application->PrintConsole(buf, appendtoimportant);

    if(TVPLoggingToFile) {
        extern bool TVPIsConsoleLogFileEnabled();
        if(TVPIsConsoleLogFileEnabled())
            TVPLogStreamHolder.Log(buf);
    }
}

//---------------------------------------------------------------------------
void TVPAddLog(const ttstr &line) { TVPAddLog(line, false); }

//---------------------------------------------------------------------------
void TVPAddImportantLog(const ttstr &line) { TVPAddLog(line, true); }

//---------------------------------------------------------------------------
ttstr TVPGetImportantLog() {
    if(!TVPImportantLogs)
        return {};
    return *TVPImportantLogs;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetLastLog : get last n lines of the log ( each line is
// spearated with
// '\n'/'\r\n' )
//---------------------------------------------------------------------------
ttstr TVPGetLastLog(tjs_uint n) {
    TVPEnsureLogObjects();
    if(!TVPLogDeque)
        return TJS_W(""); // log system is shuttingdown

    tjs_uint len = 0;
    auto size = (tjs_uint)TVPLogDeque->size();
    if(n > size)
        n = size;
    if(n == 0)
        return {};
    auto i = TVPLogDeque->end();
    i -= n;
    tjs_uint c;
    for(c = 0; c < n; c++, i++) {
#ifdef TJS_TEXT_OUT_CRLF
        len += i->Time.GetLen() + 1 + i->Log.GetLen() + 2;
#else
        len += i->Time.GetLen() + 1 + i->Log.GetLen() + 1;
#endif
    }

    ttstr buf((tTJSStringBufferLength)len);
    tjs_char *p = buf.Independ();

    i = TVPLogDeque->end();
    i -= n;
    for(c = 0; c < n; c++) {
        TJS_strcpy(p, i->Time.c_str());
        p += i->Time.GetLen();
        *p = TJS_W(' ');
        p++;
        TJS_strcpy(p, i->Log.c_str());
        p += i->Log.GetLen();
#ifdef TJS_TEXT_OUT_CRLF
        *p = TJS_W('\r');
        p++;
        *p = TJS_W('\n');
        p++;
#else
        *p = TJS_W('\n');
        p++;
#endif
        i++;
    }
    return buf;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPStartLogToFile
//---------------------------------------------------------------------------
void TVPStartLogToFile(bool clear) {
    TVPEnsureLogObjects();
    if(!TVPImportantLogs)
        return; // log system is shuttingdown

    if(TVPLoggingToFile)
        return; // already logging
    if(clear)
        TVPLogStreamHolder.Clear();

    // log last lines

    TVPLogStreamHolder.Log(*TVPImportantLogs);

#ifdef TJS_TEXT_OUT_CRLF
    ttstr separator(
        TJS_W("\r\n") TJS_W("--------------------------------------------------"
                            "----------------------------\r\n"));
#else
    ttstr separator(TJS_W("\n")
                        TJS_W("------------------------------------------"
                              "------------------------------------\n"));
#endif

    TVPLogStreamHolder.Log(separator);

    ttstr content = TVPGetLastLog(TVPLogToFileRollBack);
    TVPLogStreamHolder.Log(content);

    //
    TVPLoggingToFile = true;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPOnError
//---------------------------------------------------------------------------
void TVPOnError() {
    if(TVPAutoLogToFileOnError)
        TVPStartLogToFile(TVPAutoClearLogOnError);
    // TVPOnErrorHook();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPSetLogLocation
//---------------------------------------------------------------------------
void TVPSetLogLocation(const ttstr &loc) {
    TVPLogLocation = TVPNormalizeStorageName(loc);

    ttstr native = TVPGetLocallyAccessibleName(TVPLogLocation);
    if(native.IsEmpty()) {
        TVPNativeLogLocation.Clear();
        TVPLogLocation.Clear();
    } else {
        TVPNativeLogLocation = native;
        if(TVPNativeLogLocation[TVPNativeLogLocation.length() - 1] !=
           TJS_W('/'))
            TVPNativeLogLocation += TJS_W("/");
    }

    TVPLogStreamHolder.Reopen();

    // check force logging option
    tTJSVariant val;
    if(TVPGetCommandLine(TJS_W("-forcelog"), &val)) {
        ttstr str(val);
        if(str == TJS_W("yes")) {
            TVPLoggingToFile = false;
            TVPStartLogToFile(false);
        } else if(str == TJS_W("clear")) {
            TVPLoggingToFile = false;
            TVPStartLogToFile(true);
        }
    }
    if(TVPGetCommandLine(TJS_W("-logerror"), &val)) {
        ttstr str(val);
        if(str == TJS_W("no")) {
            TVPAutoClearLogOnError = false;
            TVPAutoLogToFileOnError = false;
        } else if(str == TJS_W("clear")) {
            TVPAutoClearLogOnError = true;
            TVPAutoLogToFileOnError = true;
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Debug
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Debug::ClassID = -1;

tTJSNC_Debug::tTJSNC_Debug() : tTJSNativeClass(TJS_W("Debug")) {
    TJS_BEGIN_NATIVE_MEMBERS(Debug)
    TJS_DECL_EMPTY_FINALIZE_METHOD
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(
        /*TJS class name*/ Debug) {
        return TJS_S_OK;
    }
    TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Debug)
    //----------------------------------------------------------------------

    //-- methods

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ message) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(numparams == 1) {
            TVPAddLog(*param[0]);
        } else {
            // display the arguments separated with ", "
            ttstr args;
            for(int i = 0; i < numparams; i++) {
                if(i != 0)
                    args += TJS_W(", ");
                args += ttstr(*param[i]);
            }
            TVPAddLog(args);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ message)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ notice) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(numparams == 1) {
            TVPAddImportantLog(*param[0]);
        } else {
            // display the arguments separated with ", "
            ttstr args;
            for(int i = 0; i < numparams; i++) {
                if(i != 0)
                    args += TJS_W(", ");
                args += ttstr(*param[i]);
            }
            TVPAddImportantLog(args);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ notice)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ startLogToFile) {
        bool clear = false;

        if(numparams >= 1)
            clear = param[0]->operator bool();

        TVPStartLogToFile(clear);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ startLogToFile)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ logAsError) {
        TVPOnError();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ logAsError)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ addLoggingHandler) {
        // add function to logging handler list

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();

        TVPAddLoggingHandler(clo);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(
        /*func. name*/ addLoggingHandler)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(
        /*func. name*/ removeLoggingHandler) {
        // remove function from logging handler list

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();

        TVPRemoveLoggingHandler(clo);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(
        /*func. name*/ removeLoggingHandler)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getLastLog) {
        tjs_uint lines = TVPLogMaxLines + 100;

        if(numparams >= 1)
            lines = (tjs_uint)param[0]->AsInteger();

        if(result)
            *result = TVPGetLastLog(lines);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getLastLog)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ prettyPrint) {
        // Match the krkrz Debug.prettyPrint(value [, depth = 2 [, compact]])
        // contract while keeping the Aether-native Debug class as the sole
        // registration owner.
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        int depth = 2;
        bool compact = false;
        if(numparams >= 2)
            depth = static_cast<int>(param[1]->AsInteger());
        if(numparams >= 3)
            compact = param[2]->operator bool();
        if(result)
            *result = TVPPrettyPrint(*param[0], depth, compact);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ prettyPrint)
    //---------------------------------------------------------------------------

    //-- properies

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_PROP_DECL(logLocation){
        TJS_BEGIN_NATIVE_PROP_GETTER{ *result = TVPLogLocation;
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TVPSetLogLocation(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(logLocation)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(logToFileOnError){
    TJS_BEGIN_NATIVE_PROP_GETTER{ *result = TVPAutoLogToFileOnError;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TVPAutoLogToFileOnError = param->operator bool();
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(logToFileOnError)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(clearLogFileOnError){
    TJS_BEGIN_NATIVE_PROP_GETTER{ *result = TVPAutoClearLogOnError;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TVPAutoClearLogOnError = param->operator bool();
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(clearLogFileOnError)
//----------------------------------------------------------------------

//----------------------------------------------------------------------
TJS_END_NATIVE_MEMBERS

// put version information to DMS
#if 0
    TVPAddImportantLog(TVPGetVersionInformation());
    TVPAddImportantLog(ttstr(TVPVersionInformation2));
#endif
} // end of tTJSNC_Debug::tTJSNC_Debug
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TJS2 Console Output Gateway
//---------------------------------------------------------------------------
class tTVPTJS2ConsoleOutputGateway : public iTJSConsoleOutput {
    void ExceptionPrint(const tjs_char *msg) override { TVPAddLog(msg); }

    void Print(const tjs_char *msg) override { TVPAddLog(msg); }
} static TVPTJS2ConsoleOutputGateway;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TJS2 Dump Output Gateway
//---------------------------------------------------------------------------
static ttstr TVPDumpOutFileName;
static FILE *TVPDumpOutFile = nullptr; // use traditional output routine
//---------------------------------------------------------------------------
class tTVPTJS2DumpOutputGateway : public iTJSConsoleOutput {
    void ExceptionPrint(const tjs_char *msg) override { Print(msg); }

    void Print(const tjs_char *msg) override {
        if(TVPDumpOutFile) {
            fwrite(msg, 1, TJS_strlen(msg) * sizeof(tjs_char), TVPDumpOutFile);
#ifdef TJS_TEXT_OUT_CRLF
            fwrite(TJS_W("\r\n"), 1, 2 * sizeof(tjs_char), TVPDumpOutFile);
#else
            fwrite(TJS_W("\n"), 1, 1 * sizeof(tjs_char), TVPDumpOutFile);
#endif
        }
    }
} static TVPTJS2DumpOutputGateway;

//---------------------------------------------------------------------------
void TVPTJS2StartDump() {
#if 0
    ttstr filename = ExePath() + TJS_W(".dump.txt");
    TVPDumpOutFileName = filename;
    TVPDumpOutFile = _wfopen(filename.c_str(), TJS_W("wb+"));
    if(TVPDumpOutFile)
    {
        // TODO: 32-bit unicode support
        fwrite(TJS_N("\xff\xfe"), 1, 2, TVPDumpOutFile); // indicate unicode text
    }
#endif
}

//---------------------------------------------------------------------------
void TVPTJS2EndDump() {
#if 0
    if (TVPDumpOutFile)
    {
        fclose(TVPDumpOutFile), TVPDumpOutFile = nullptr;
        TVPAddLog(ttstr(TJS_W("Dumped to ")) + TVPDumpOutFileName);
    }
#endif
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// console interface retrieving functions
//---------------------------------------------------------------------------
iTJSConsoleOutput *TVPGetTJS2ConsoleOutputGateway() {
    return &TVPTJS2ConsoleOutputGateway;
}

//---------------------------------------------------------------------------
iTJSConsoleOutput *TVPGetTJS2DumpOutputGateway() {
    return &TVPTJS2DumpOutputGateway;
}
//---------------------------------------------------------------------------

/*
//---------------------------------------------------------------------------
// on-error hook
//---------------------------------------------------------------------------
void TVPOnErrorHook()
{
        if(TVPMainForm) TVPMainForm->NotifySystemError();
}
//---------------------------------------------------------------------------
*/
