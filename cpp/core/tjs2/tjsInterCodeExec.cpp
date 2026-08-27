//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Intermediate Code Execution
//---------------------------------------------------------------------------

#include "tjsCommHead.h"
#include <algorithm>
#include <spdlog/spdlog.h>

#include "tjsInterCodeExec.h"
#include "tjsInterCodeGen.h"
#include "tjsScriptBlock.h"
#include "tjsError.h"
#include "tjs.h"
#include "tjsUtils.h"
#include "tjsNative.h"
#include "tjsArray.h"
#include "tjsDebug.h"
#include "tjsOctPack.h"
#include "tjsGlobalStringMap.h"
#include <chrono>
#include <csignal>
#include <atomic>
#include <cstdio>
#include <set>
#include <mutex>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include <thread>
#include <fmt/format.h>

namespace TJS {
    //---------------------------------------------------------------------------
    // utility functions
    //---------------------------------------------------------------------------
    static constexpr size_t TJS_EXEC_ARG_TRACE_RING_SIZE = 160;
    static constexpr size_t TJS_EXEC_ARG_TRACE_LINE_SIZE = 1024;
    static char TJSExecArgTraceRing[TJS_EXEC_ARG_TRACE_RING_SIZE]
                                  [TJS_EXEC_ARG_TRACE_LINE_SIZE] = {};
    static std::atomic<uint64_t> TJSExecArgTraceIndex{0};

    static void TJSStoreExecArgTrace(std::string line) {
        const uint64_t index =
            TJSExecArgTraceIndex.fetch_add(1, std::memory_order_relaxed);
        std::snprintf(TJSExecArgTraceRing[index % TJS_EXEC_ARG_TRACE_RING_SIZE],
                      TJS_EXEC_ARG_TRACE_LINE_SIZE, "%s", line.c_str());
    }

    extern "C" const char *TJSGetRecentExecArgTrace() {
        static std::string dump;
        dump.clear();

        const uint64_t end =
            TJSExecArgTraceIndex.load(std::memory_order_relaxed);
        const uint64_t start = end > TJS_EXEC_ARG_TRACE_RING_SIZE
                                   ? end - TJS_EXEC_ARG_TRACE_RING_SIZE
                                   : 0;
        for(uint64_t i = start; i < end; ++i) {
            const char *line =
                TJSExecArgTraceRing[i % TJS_EXEC_ARG_TRACE_RING_SIZE];
            if(!line || !*line)
                continue;
            dump += line;
            dump += '\n';
        }
        return dump.c_str();
    }

    static bool TJSSaveTraceEnabled() {
        static const bool enabled = [] {
            const char *save = std::getenv("AETHERKIRI_TJS_SAVE_TRACE");
            const char *scene = std::getenv("AETHERKIRI_TJS_SCENE_TRACE");
            const char *audio = std::getenv("AETHERKIRI_TJS_AUDIO_TRACE");
            const char *replay = std::getenv("AETHERKIRI_TJS_REPLAY_TRACE");
            return (save && *save && *save != '0') ||
                (scene && *scene && *scene != '0') ||
                (audio && *audio && *audio != '0') ||
                (replay && *replay && *replay != '0');
        }();
        return enabled;
    }

    static bool TJSSceneTraceEnabled() {
        static const bool enabled = [] {
            const char *value = std::getenv("AETHERKIRI_TJS_SCENE_TRACE");
            return value && *value && *value != '0';
        }();
        return enabled;
    }

    static bool TJSAudioTraceEnabled() {
        static const bool enabled = [] {
            const char *value = std::getenv("AETHERKIRI_TJS_AUDIO_TRACE");
            return value && *value && *value != '0';
        }();
        return enabled;
    }

    static bool TJSReplayTraceEnabled() {
        static const bool enabled = [] {
            const char *value = std::getenv("AETHERKIRI_TJS_REPLAY_TRACE");
            return value && *value && *value != '0';
        }();
        return enabled;
    }

    static bool TJSThumbnailOwnerTraceEnabled() {
        static const bool enabled = [] {
            const char *value = std::getenv("AETHERKIRI_THUMB_OWNER_TRACE");
            return value && *value && *value != '0';
        }();
        return enabled;
    }

    static void TJSTraceThumbnailOwner(const char *phase,
                                       iTJSDispatch2 *object,
                                       iTJSDispatch2 *objthis,
                                       tjs_error result,
                                       const tTJSVariant *value) {
        if(!TJSThumbnailOwnerTraceEnabled())
            return;
        std::string class_name;
        if(object) {
            tTJSVariant name;
            if(TJS_SUCCEEDED(object->ClassInstanceInfo(TJS_CII_GET, 0,
                                                        &name)))
                class_name = ttstr(name).AsStdString();
        }
        const std::string object_type = object
            ? TJSGetObjectTypeInfo(object).AsStdString()
            : std::string();
        const std::string objthis_type = objthis
            ? TJSGetObjectTypeInfo(objthis).AsStdString()
            : std::string();
        spdlog::info(
            "AetherInternal thumbnail lookup phase={} object={} objthis={} class={} objectType={} objthisType={} hr={} valueType={} valueObj={}",
            phase, static_cast<const void *>(object),
            static_cast<const void *>(objthis), class_name, object_type,
            objthis_type, result, value ? static_cast<int>(value->Type()) : -1,
            value ? static_cast<const void *>(value->AsObjectNoAddRef())
                  : nullptr);
    }

    static bool TJSCrashTraceEnabled() {
        static const bool enabled = [] {
            const char *value = std::getenv("AETHERKIRI_TJS_VM_TRACE");
            return value && *value && *value != '0';
        }();
        return enabled;
    }

    static bool TJSExecArgTraceEnabled() {
        static const bool enabled = [] {
            const char *value = std::getenv("AETHERKIRI_EXEC_ARG_TRACE");
            return value && *value && *value != '0';
        }();
        return enabled;
    }

    static bool TJSFunctionProfileEnabled() {
        static const bool enabled = [] {
            const char *profile =
                std::getenv("AETHERKIRI_MOTION_RENDER_PROFILE");
            return profile && *profile && *profile != '0';
        }();
        return enabled;
    }

    static double TJSFunctionProfileSlowMs() {
        static const double threshold = [] {
            const char *value =
                std::getenv("AETHERKIRI_TJS_FUNCTION_SLOW_MS");
            if(!value || !*value)
                return 50.0;
            char *end = nullptr;
            const double parsed = std::strtod(value, &end);
            return end != value && parsed > 0.0 ? parsed : 50.0;
        }();
        return threshold;
    }

    struct TJSFunctionProfileFrame {
        const tTJSInterCodeContext *context = nullptr;
        iTJSDispatch2 *objthis = nullptr;
        std::chrono::steady_clock::time_point started{};
        double childMs = 0.0;
    };

    // ExecuteAsFunction profiles are inclusive by nature. Keep a per-thread
    // stack so a slow call can also report its self time and nesting depth;
    // this is the information needed to distinguish a slow wrapper from the
    // actual script operation below it without tracing every VM instruction.
    static thread_local std::vector<TJSFunctionProfileFrame>
        TJSFunctionProfileStack;

    class TJSFunctionProfileGuard {
    public:
        TJSFunctionProfileGuard(const tTJSInterCodeContext *context,
                                iTJSDispatch2 *objthis)
            : Context(context), ObjThis(objthis), Enabled(
                  TJSFunctionProfileEnabled()),
              Started(Enabled ? std::chrono::steady_clock::now()
                               : std::chrono::steady_clock::time_point{}),
              StackIndex(0), Pushed(false) {
            if(Enabled) {
                StackIndex = TJSFunctionProfileStack.size();
                TJSFunctionProfileStack.push_back(
                    {Context, ObjThis, Started, 0.0});
                Pushed = true;
            }
        }

        ~TJSFunctionProfileGuard() {
            if(!Enabled || !Context)
                return;
            const double elapsedMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - Started).count();
            double childMs = 0.0;
            size_t depth = 0;
            if(Pushed && StackIndex < TJSFunctionProfileStack.size()) {
                childMs = TJSFunctionProfileStack[StackIndex].childMs;
                depth = StackIndex;
                if(StackIndex > 0) {
                    TJSFunctionProfileStack[StackIndex - 1].childMs +=
                        elapsedMs;
                }
                TJSFunctionProfileStack.pop_back();
            }
            const double selfMs = std::max(0.0, elapsedMs - childMs);
            if(elapsedMs < TJSFunctionProfileSlowMs())
                return;
            if(const auto logger = spdlog::get("core")) {
                logger->info(
                    "tjs function profile: desc={} this={} elapsed_ms={:.3f} "
                    "self_ms={:.3f} child_ms={:.3f} depth={}",
                    Context->GetShortDescriptionWithClassName().AsStdString(),
                    static_cast<const void *>(ObjThis), elapsedMs, selfMs,
                    childMs, depth);
            }
        }

    private:
        const tTJSInterCodeContext *Context;
        iTJSDispatch2 *ObjThis;
        bool Enabled;
        std::chrono::steady_clock::time_point Started;
        size_t StackIndex;
        bool Pushed;
    };

    static char TJSCompatAsciiLower(char ch) {
        return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
    }

    static bool TJSCompatContainsAsciiNoCase(const std::string &text,
                                             const char *pattern) {
        if(!pattern || !*pattern)
            return true;
        const size_t patternLength = std::char_traits<char>::length(pattern);
        if(patternLength > text.size())
            return false;
        for(size_t start = 0; start <= text.size() - patternLength; ++start) {
            bool matches = true;
            for(size_t index = 0; index < patternLength; ++index) {
                if(TJSCompatAsciiLower(text[start + index]) !=
                   TJSCompatAsciiLower(pattern[index])) {
                    matches = false;
                    break;
                }
            }
            if(matches)
                return true;
        }
        return false;
    }

    static bool TJSCompatKAGEnvLayerTypeIntegerProperty(
        const tTJSInterCodeContext *ctx, const tTJSVariant &target,
        const tjs_char *membername, tTJSVariant *result) {
        if(!ctx || !result || !membername || target.Type() != tvtInteger ||
           TJS_strcmp(membername, TJS_W("type")))
            return false;

        const tTVInteger layerType = target.AsInteger();
        if(layerType < 0 || layerType > 28)
            return false;

        if(!TJSCompatContainsAsciiNoCase(
               ctx->GetShortDescriptionWithClassName().AsStdString(),
               "convLayerType"))
            return false;

        // KAG forks place this conversion helper in different script files
        // (for example kagenvimage.tjs or world.tjs). The stable contract is
        // the convLayerType helper itself: integer layer enum values already
        // are normalized, so reading their pseudo-property must be idempotent.
        *result = layerType;
        return true;
    }

    static bool TJSSceneTraceMatches(const std::string &text) {
        if(TJSReplayTraceEnabled()) {
            static const char *replay_patterns[] = {
                "SceneGalleryMode.view",
                "ExSceneGalleryBase",
                "loadFunction",
                "fixScenarioExt",
                "checkLabelExist",
                "checkConvertScenarioLabelExists",
                "loadScenario",
                "GoToLabel",
                "onScenarioLoad",
                "onScenarioLoaded",
                "envstart",
                "donereplay",
                "endrecollection",
                "start.ks",
                "KAGEnvPlayer",
                "StorageData",
                ".scenestart",
                ".sceneplay",
                ".scenecheck",
                ".startScene",
                ".loadScene",
                ".initStorage",
                ".findScene",
                "onScenarioExec",
                "onSkipToLine",
                "PSBFile"
            };
            for(const char *pattern : replay_patterns) {
                if(text.find(pattern) != std::string::npos)
                    return true;
            }
        }
        if(!TJSSceneTraceEnabled())
            return false;
        static const char *patterns[] = {
            "stand",           "Stand",       "StandLayer",
            "StandPSD",        "StandImage",  "StandInformation",
            "KAGEnvImageMapperStand",         "AffineSourceStand",
            "AffineSource",    "AffineMatrix", "AffineLayer",
            "PSDInfo",         "PSBFile",     ".pbd",
            "getStandM",       "getStandF",   "setFaceVis",
            "setCondVis",      "checkVis",
            // Environment layers use the same KAGEnvImage command path as
            // ordinary backgrounds. Keep the opt-in scene trace focused on
            // the stage2 overlay so we can separate a missing script command
            // from a renderer/compositor failure.
            "KAGEnvImage",     "KAGEnvImageMapperStage",
            "stage2",          "envcolor",    "EnvColor",
            "doImageCommand",  "doCommand",   "setImageFile",
            "_setImageFile",   "_setOption",  "setStage"
            ,"LanguageSelectPanel", "ScrollablePulldownBase",
            "DragScrollGroup", "ScrollableSheetUtil", "drawItemView",
            "onUiloaded", "uiloadGetRect", "uiloadPartsImage",
            "reparentItems", "initScrollParams", "resetScrollMax",
            "getItemButton", "updateItemView", "ownerCallback"
            ,"uiloadEntry", "uiloadWithFuncTable", "internalUiloadPacked",
            "uiloadParseMain", "uiloadEvals", "callExtra", "setExtraType",
            "UIListParser.doLine", "doLine", "CustomGalleryModule",
            "changeGroup", "pageGroup", "jumpScroll", "restoreItems",
            "DialogModeManager", "dmproxy", "cmd_func", "cmd",
            "renewItemPage", "_openPageGroup", "_closePageGroup",
            "readCsvDoFirstChar", "readCsvDoLine", "_initPageGroup"
        };
        for(const char *pattern : patterns) {
            if(text.find(pattern) != std::string::npos)
                return true;
        }
        return false;
    }

    static bool TJSAudioTraceMatches(const std::string &text) {
        if(!TJSAudioTraceEnabled())
            return false;
            static const char *patterns[] = {
            "playRandomSE", "getRandomSE", "playSE",    "playse",
            "Sound",        "sound",       "Voice",     "voice",
            "Wave",         "wave",        "sebuf",     "SEBuf",
            "ona",          "Ona",         "profile",   "Profile",
            "patting",      "dokofera",    "storage",   "Storages",
            "flash",        "Flash",        "MoveAction", "beginAction",
            "stopAction",   "chview",       "chframe",    "chv",
            };
        for(const char *pattern : patterns) {
            if(text.find(pattern) != std::string::npos)
                return true;
        }
        return false;
    }

    static bool TJSSaveTraceMatches(const std::string &text) {
        return text.find("Save") != std::string::npos ||
               text.find("save") != std::string::npos ||
               text.find("FileStore") != std::string::npos ||
               text.find("DirectSave") != std::string::npos ||
               text.find("CustomSav") != std::string::npos ||
               text.find("select") != std::string::npos ||
               text.find("selec") != std::string::npos ||
               text.find("onButton") != std::string::npos ||
               text.find("onExecute") != std::string::npos ||
               text.find("onItem") != std::string::npos ||
               text.find("onSysButton") != std::string::npos ||
               text.find("onSel") != std::string::npos ||
               text.find("setItem") != std::string::npos ||
               text.find("getItem") != std::string::npos ||
               text.find("gameSave") != std::string::npos ||
               TJSAudioTraceMatches(text) ||
               TJSSceneTraceMatches(text);
    }

    static bool TJSSaveTraceMemberMatches(const std::string &text) {
        return text == "onButtonClick" || text == "onExecute" ||
               text == "onItem" || text == "onSysButton" ||
               text == "onSel" || text == "onSave" || text == "onLoadF" ||
               text == "sav" || text == "loa" || text == "getCurrent" ||
               text == "isExis" || text == "isEna" || text == "_ask" ||
               text == "setItem" || text == "getItem" ||
               text == "getItemB" || text == "getEditBu" ||
               text == "setupUi" || text == "updateItem" ||
               text == "invoke" || text == "call" ||
               text == "linkNum" || text == "num" || text == "page" ||
               text == "action" || text == "onClick" ||
               text == "play" || text == "open" || text == "stop" ||
               text == "voice" || text == "storage" || text == "buf" ||
               text == "array" || text == "profile" || text == "dress" ||
               text == "chara" || text == "scene" ||
               text == "doCommand" || text == "doImageCommand" ||
               text == "setImageFile" || text == "_setImageFile" ||
               text == "_setOption" || text == "setStage" ||
               text == "setVisible" || text == "setOpacity" ||
               text == "operateRect" || text == "fillRect" ||
               text == "drawItemView" || text == "reparentItems" ||
               text == "initScrollParams" || text == "resetScrollMax" ||
               text == "uiloadGetRect" || text == "uiloadPartsImage" ||
               text == "onUiloaded" || text == "ownerCallback" ||
               TJSSceneTraceMatches(text);
    }

    static std::string TJSSaveTraceVariantString(const tTJSVariant &value) {
        try {
            return ttstr(value).AsStdString();
        } catch(...) {
            return "<unprintable>";
        }
    }

    // The CSV-driven gallery code passes each parsed row as a Dictionary.
    // Keep this opt-in diagnostic deliberately narrow: it lets us inspect the
    // row shape without changing script-visible behavior or dumping arbitrary
    // game data.
    static std::string TJSSaveTraceObjectField(const tTJSVariant &value,
                                               const char *name) {
        if(value.Type() != tvtObject || !name)
            return "<not-object>";
        try {
            const tTJSVariantClosure closure = value.AsObjectClosureNoAddRef();
            if(!closure.Object)
                return "<null-object>";
            tTJSVariant field;
            const ttstr member(name);
            const tjs_error hr = closure.Object->PropGet(
                TJS_IGNOREPROP, member.c_str(), nullptr, &field,
                closure.ObjThis);
            if(TJS_FAILED(hr))
                return "<missing>";
            return TJSSaveTraceVariantString(field);
        } catch(...) {
            return "<error>";
        }
    }

    static std::string TJSSaveTraceGalleryArgFields(const tTJSVariant &value) {
        if(value.Type() != tvtObject)
            return {};
        static const char *fields[] = { "0",
                                        "1",
                                        "2",
                                        "3",
                                        "count",
                                        "name",
                                        "start",
                                        "end",
                                        "tag",
                                        "list",
                                        "line",
                                        "storage",
                                        "target",
                                        "skipto",
                                        "endlabel",
                                        "endstorage",
                                        "doneStorage",
                                        "doneTarget",
                                        "start_storage",
                                        "start_target",
                                        "envplay" };
        std::string out;
        for(const char *field : fields) {
            const std::string fieldValue =
                TJSSaveTraceObjectField(value, field);
            if(fieldValue == "<missing>")
                continue;
            if(!out.empty())
                out += ", ";
            out += field;
            out += "=";
            out += fieldValue;
        }
        return out;
    }

    static std::string TJSSaveTraceThisField(iTJSDispatch2 *objthis,
                                             const char *name) {
        if(!objthis || !name)
            return "<null>";
        try {
            tTJSVariant value;
            const ttstr member(name);
            const tjs_error hr = objthis->PropGet(
                0, member.c_str(), nullptr, &value, objthis);
            if(TJS_FAILED(hr))
                return "<missing>";
            return TJSSaveTraceVariantString(value);
        } catch(...) {
            return "<error>";
        }
    }

    static void TJSTraceSaveFunctionEnter(const tTJSInterCodeContext *ctx,
                                          iTJSDispatch2 *objthis,
                                          tTJSVariant **args,
                                          tjs_int numargs) {
        if(!TJSSaveTraceEnabled() || !ctx)
            return;
        const std::string desc =
            ctx->GetShortDescriptionWithClassName().AsStdString();
        if(!TJSSaveTraceMatches(desc))
            return;
        const bool galleryTarget =
            (TJSSceneTraceEnabled() || TJSReplayTraceEnabled()) &&
            (desc.find("GalleryMode") != std::string::npos ||
             desc.find("ExSceneGalleryBase") != std::string::npos ||
             desc.find("global.loadFunction") != std::string::npos ||
             desc.find("KAGEnvPlayer") != std::string::npos ||
             desc.find("StorageData") != std::string::npos ||
             desc.find("UiBasedPageSheet") != std::string::npos ||
             desc.find("PageSheet") != std::string::npos ||
             desc.find("renewItemPage") != std::string::npos ||
             desc.find("_openPageGroup") != std::string::npos ||
             desc.find("_closePageGroup") != std::string::npos ||
             desc.find("readCsvDoFirstChar") != std::string::npos ||
             desc.find("readCsvDoLine") != std::string::npos ||
             desc.find("_initPageGroup") != std::string::npos);
        static int logged = 0;
        static int galleryLogged = 0;
        if(galleryTarget) {
            if(galleryLogged >= 3000)
                return;
            ++galleryLogged;
        } else {
            if(logged >= 8000)
                return;
            ++logged;
        }
        if(galleryTarget &&
           (desc.find("_closePageGroup") != std::string::npos ||
            desc.find("_openPageGroup") != std::string::npos)) {
            spdlog::info(
                "TJSSaveTrace gallery boundary desc=\"{}\" count={} rowcol={} current={} "
                "miu={} azu={} rio={} eri={} nic={} etc={} binds={} names={}",
                desc, TJSSaveTraceThisField(objthis, "count"),
                TJSSaveTraceThisField(objthis, "_rowcol"),
                TJSSaveTraceThisField(objthis, "_currentPageGroup"),
                TJSSaveTraceThisField(objthis, "pageGroups.miu"),
                TJSSaveTraceThisField(objthis, "pageGroups.azu"),
                TJSSaveTraceThisField(objthis, "pageGroups.rio"),
                TJSSaveTraceThisField(objthis, "pageGroups.eri"),
                TJSSaveTraceThisField(objthis, "pageGroups.nic"),
                TJSSaveTraceThisField(objthis, "pageGroups.etc"),
                TJSSaveTraceThisField(objthis, "pageGroupBinds"),
                TJSSaveTraceThisField(objthis, "pageGroupNames"));
        }
        std::string arg_text;
        const tjs_int trace_arg_count = std::min<tjs_int>(numargs, 4);
        for(tjs_int i = 0; i < trace_arg_count; ++i) {
            if(!arg_text.empty())
                arg_text += ", ";
            arg_text += fmt::format("a{}={}", i,
                                    args && args[i]
                                        ? TJSSaveTraceVariantString(*args[i])
                                        : std::string("<null>"));
            if(galleryTarget && args && args[i]) {
                const std::string fields =
                    TJSSaveTraceGalleryArgFields(*args[i]);
                if(!fields.empty()) {
                    arg_text += " {";
                    arg_text += fields;
                    arg_text += "}";
                }
            }
        }
        spdlog::info("TJSSaveTrace enter desc=\"{}\" args={} [{}] this={}",
                     desc, numargs, arg_text,
                     static_cast<const void *>(objthis));
    }

    static bool TJSSaveTracePropertyContext(const tTJSInterCodeContext *ctx) {
        if(!TJSSaveTraceEnabled() || !ctx)
            return false;
        const std::string desc =
            ctx->GetShortDescriptionWithClassName().AsStdString();
        return TJSSaveTraceMatches(desc);
    }

    static void TJSTraceSavePropertyGet(const tTJSInterCodeContext *ctx,
                                        const char *kind,
                                        const ttstr &name,
                                        const tTJSVariant &value,
                                        tjs_error hr) {
        if(!TJSSaveTraceEnabled() || !ctx)
            return;
        const std::string member = name.AsStdString();
        const bool failed = TJS_FAILED(hr);
        const bool replayCritical = TJSReplayTraceEnabled() &&
            (member == "envplay" || member == "playerExecMode" ||
             member == "forcePlayerExecMode" ||
             member == "envPlayerConvertEnabled");
        const bool interestingFailure =
            failed && TJSSceneTraceEnabled() && TJSSaveTracePropertyContext(ctx);
        if(!replayCritical && !TJSSaveTracePropertyContext(ctx) &&
           !TJSSaveTraceMemberMatches(member) && !interestingFailure) {
            return;
        }
        static int logged = 0;
        if(!replayCritical && !interestingFailure && logged >= 12000)
            return;
        ++logged;
        spdlog::info("TJSSaveTrace prop {} desc=\"{}\" name={} hr={} value={}",
                     kind, ctx->GetShortDescriptionWithClassName().AsStdString(),
                     member, hr, TJS_SUCCEEDED(hr)
                                     ? TJSSaveTraceVariantString(value)
                                     : std::string("<failed>"));
    }

    static void TJSTracePropertyDirectBefore(const tTJSInterCodeContext *ctx,
                                             const ttstr &name,
                                             const tTJSVariant *target,
                                             const tTJSVariantClosure &closure) {
        if(!TJSCrashTraceEnabled() || !ctx)
            return;
        static int logged = 0;
        if(logged >= 20000)
            return;
        ++logged;
        const std::string value = target ? TJSSaveTraceVariantString(*target)
                                         : std::string("<null>");
        spdlog::info(
            "TJSCrashTrace get-direct-before desc=\"{}\" name={} target={} "
            "object={} objthis={}",
            ctx->GetShortDescriptionWithClassName().AsStdString(),
            name.AsStdString(), value, static_cast<const void *>(closure.Object),
            static_cast<const void *>(closure.ObjThis));
    }

    static std::string TJSCrashTraceVariantBrief(const tTJSVariant &value) {
        std::string text = fmt::format("type={}", static_cast<int>(value.Type()));
        if(value.Type() == tvtObject) {
            try {
                const tTJSVariantClosure closure = value.AsObjectClosureNoAddRef();
                text += fmt::format(" object={} objthis={}",
                                    static_cast<const void *>(closure.Object),
                                    static_cast<const void *>(closure.ObjThis));
                if(TJSObjectHashMapEnabled()) {
                    text += fmt::format(" objectFlags=0x{:x} objthisFlags=0x{:x}",
                                        TJSGetObjectHashCheckFlag(closure.Object),
                                        TJSGetObjectHashCheckFlag(closure.ObjThis));
                }
            } catch(...) {
                text += " object=<unavailable>";
            }
        }
        return text;
    }

    static std::string TJSTraceDispatchBrief(iTJSDispatch2 *object) {
        if(!object)
            return "null";
        std::string text = fmt::format("{}", static_cast<void *>(object));
        if(TJSObjectHashMapEnabled()) {
            text += fmt::format(" flags=0x{:x}",
                                TJSGetObjectHashCheckFlag(object));
            ttstr type = TJSGetObjectTypeInfo(object);
            if(!type.IsEmpty())
                text += fmt::format(" type=\"{}\"", type.AsStdString());
        }
        return text;
    }

    static void TJSTraceExecArgs(const tTJSInterCodeContext *ctx,
                                 iTJSDispatch2 *objthis,
                                 tTJSVariant **args,
                                 tjs_int numargs,
                                 tjs_int declArgCount,
                                 tjs_int collapseBase) {
        if(!TJSExecArgTraceEnabled() || !ctx)
            return;

        std::string arg_text;
        const tjs_int trace_arg_count = std::min<tjs_int>(numargs, 6);
        for(tjs_int i = 0; i < trace_arg_count; ++i) {
            if(!arg_text.empty())
                arg_text += ", ";
            arg_text += fmt::format("a{}={}", i,
                                    args && args[i]
                                        ? TJSCrashTraceVariantBrief(*args[i])
                                        : std::string("<null>"));
        }

        TJSStoreExecArgTrace(fmt::format(
            "TJSExecArgTrace enter this={} desc=\"{}\" objthis={} numargs={} "
            "decl={} collapse={} args=[{}]",
            static_cast<const void *>(ctx),
            ctx->GetShortDescriptionWithClassName().AsStdString(),
            TJSTraceDispatchBrief(objthis), numargs, declArgCount, collapseBase,
            arg_text));
    }

    static void TJSTraceVMCopy(const tTJSInterCodeContext *ctx,
                               const tjs_int32 *code_base,
                               const tjs_int32 *code,
                               tTJSVariant *ra) {
        if(!TJSCrashTraceEnabled() || !ctx)
            return;
        static int logged = 0;
        if(logged >= 4000)
            return;
        ++logged;
        const tjs_int dst = TJS_FROM_VM_REG_ADDR(code[1]);
        const tjs_int src = TJS_FROM_VM_REG_ADDR(code[2]);
        const tTJSVariant &dst_value = TJS_GET_VM_REG(ra, code[1]);
        const tTJSVariant &src_value = TJS_GET_VM_REG(ra, code[2]);
        spdlog::info("TJSCrashTrace vm_cp desc=\"{}\" ip={} dst=%{} [{}] src=%{} [{}]",
                     ctx->GetShortDescriptionWithClassName().AsStdString(),
                     static_cast<long long>(code - code_base), dst,
                     TJSCrashTraceVariantBrief(dst_value), src,
                     TJSCrashTraceVariantBrief(src_value));
    }

    static void TJSTraceSavePropertySet(const tTJSInterCodeContext *ctx,
                                        const char *kind,
                                        const ttstr &name,
                                        const tTJSVariant &value,
                                        tjs_error hr) {
        if(!TJSSaveTraceEnabled() || !ctx)
            return;
        const std::string member = name.AsStdString();
        const bool failed = TJS_FAILED(hr);
        const bool replayCritical = TJSReplayTraceEnabled() &&
            (member == "envplay" || member == "playerExecMode" ||
             member == "forcePlayerExecMode" ||
             member == "envPlayerConvertEnabled");
        const bool interestingFailure =
            failed && TJSSceneTraceEnabled() && TJSSaveTracePropertyContext(ctx);
        if(!replayCritical && !TJSSaveTracePropertyContext(ctx) &&
           !TJSSaveTraceMemberMatches(member) && !interestingFailure) {
            return;
        }
        static int logged = 0;
        if(!replayCritical && !interestingFailure && logged >= 12000)
            return;
        ++logged;
        spdlog::info("TJSSaveTrace set {} desc=\"{}\" name={} hr={} value={}",
                     kind, ctx->GetShortDescriptionWithClassName().AsStdString(),
                     member, hr, TJS_SUCCEEDED(hr)
                                     ? TJSSaveTraceVariantString(value)
                                     : std::string("<failed>"));
    }

    static void TJSTraceSaveMemberCall(const tTJSInterCodeContext *ctx,
                                       const char *kind,
                                       const ttstr &name,
                                       tTJSVariant **args,
                                       tjs_int numargs,
                                       tjs_error hr,
                                       iTJSDispatch2 *call_this = nullptr) {
        if(!TJSSaveTraceEnabled() || !ctx)
            return;
        const std::string desc =
            ctx->GetShortDescriptionWithClassName().AsStdString();
        const std::string member = name.AsStdString();
        const bool failed = TJS_FAILED(hr);
        const bool errorImageDiagnostic =
            TJSSceneTraceEnabled() && member == "errorImage";
        const bool galleryCommandDiagnostic =
            TJSSceneTraceEnabled() &&
            (member == "changeGroup" || member == "jumpScroll" ||
             member == "restoreItems" || member == "cmd_func" ||
             member == "dmproxy");
        const bool interestingFailure =
            failed && TJSSceneTraceEnabled() && TJSSaveTraceMatches(desc);
        if(!TJSSaveTraceMatches(desc) && !TJSSaveTraceMemberMatches(member) &&
           !interestingFailure && !errorImageDiagnostic)
            return;
        static int logged = 0;
        if(!interestingFailure && !errorImageDiagnostic &&
           !galleryCommandDiagnostic && logged >= 16000)
            return;
        ++logged;
        std::string arg_text;
        const tjs_int trace_arg_count = std::min<tjs_int>(numargs, 4);
        for(tjs_int i = 0; i < trace_arg_count; ++i) {
            if(!arg_text.empty())
                arg_text += ", ";
            arg_text += fmt::format("a{}={}", i,
                                    args && args[i]
                                        ? TJSSaveTraceVariantString(*args[i])
                                        : std::string("<null>"));
        }
        std::string this_text;
        if(galleryCommandDiagnostic ||
           (TJSSceneTraceEnabled() &&
            desc.find("GalleryMode.renewItemPage") != std::string::npos)) {
            this_text = fmt::format(" this_count={} this_type={}",
                                    call_this
                                        ? TJSSaveTraceThisField(call_this, "count")
                                        : std::string("<null>"),
                                    call_this ? typeid(*call_this).name()
                                              : "<null>");
        }
        spdlog::info("TJSSaveTrace call {} desc=\"{}\" member={} args={} [{}] hr={}{}",
                     kind, desc, member, numargs, arg_text, hr, this_text);
    }

    static void TJSTraceSaveCallOp(const tTJSInterCodeContext *ctx,
                                   const char *op,
                                   const tTJSVariant &target,
                                   tTJSVariant **args,
                                   tjs_int numargs,
                                   tjs_error hr) {
        if(!TJSSaveTraceEnabled() || !ctx)
            return;
        const std::string desc =
            ctx->GetShortDescriptionWithClassName().AsStdString();
        const std::string targetText = TJSSaveTraceVariantString(target);
        const bool failed = TJS_FAILED(hr);
        const bool targetMatch = targetText.find("PSBFile") != std::string::npos ||
                                 targetText.find(".pbd") != std::string::npos ||
                                 TJSAudioTraceMatches(targetText);
        const bool contextMatch = TJSSaveTraceMatches(desc) || targetMatch;
        const bool interestingFailure =
            failed && TJSSceneTraceEnabled() && contextMatch;
        if(!targetMatch && !interestingFailure)
            return;
        static int logged = 0;
        if(!interestingFailure && logged >= 8000)
            return;
        ++logged;
        std::string arg_text;
        const tjs_int trace_arg_count = std::min<tjs_int>(numargs, 4);
        for(tjs_int i = 0; i < trace_arg_count; ++i) {
            if(!arg_text.empty())
                arg_text += ", ";
            arg_text += fmt::format("a{}={}", i,
                                    args && args[i]
                                        ? TJSSaveTraceVariantString(*args[i])
                                        : std::string("<null>"));
        }
        spdlog::info("TJSSaveTrace op {} desc=\"{}\" target={} args={} [{}] hr={}",
                     op, desc, targetText, numargs, arg_text, hr);
    }

    static void ThrowFrom_tjs_error_num(tjs_error hr, tjs_int num) {
        tjs_char buf[34];
        TJS_int_to_str(num, buf);
        TJSThrowFrom_tjs_error(hr, buf);
    }

    //---------------------------------------------------------------------------
    static void ThrowInvalidVMCode() { TJS_eTJSError(TJSInvalidOpecode); }

    //---------------------------------------------------------------------------
    static void GetStringProperty(tTJSVariant *result, const tTJSVariant *str,
                                  const tTJSVariant &member) {
        // processes properties toward strings.
        if(member.Type() != tvtInteger && member.Type() != tvtReal) {
            const tjs_char *name = member.GetString();
            if(!name)
                TJSThrowFrom_tjs_error(TJS_E_MEMBERNOTFOUND, TJS_W(""));

            if(!TJS_strcmp(name, TJS_W("length"))) {
                // get string length
                const tTJSVariantString *s = str->AsStringNoAddRef();
                *result = tTVInteger(s->GetLength());
                return;
            }
            if(name[0] >= TJS_W('0') && name[0] <= TJS_W('9')) {
                const tTJSVariantString *valstr = str->AsStringNoAddRef();
                const tjs_char *s = str->GetString();
                tjs_int n = TJS_atoi(name);
                tjs_int len = valstr->GetLength();
                if(n == len) {
                    *result = tTJSVariant(TJS_W(""));
                    return;
                }
                if(n < 0 || n > len)
                    TJS_eTJSError(TJSRangeError);
                tjs_char bf[2];
                bf[1] = 0;
                bf[0] = s[n];
                *result = tTJSVariant(bf);
                return;
            }

            TJSThrowFrom_tjs_error(TJS_E_MEMBERNOTFOUND, name);
        } else // member.Type() == tvtInteger || member.Type() ==
               // tvtReal
        {
            const tTJSVariantString *valstr = str->AsStringNoAddRef();
            const tjs_char *s = str->GetString();
            tjs_int n = (tjs_int)member.AsInteger();
            tjs_int len = valstr->GetLength();
            if(n == len) {
                *result = tTJSVariant(TJS_W(""));
                return;
            }
            if(n < 0 || n > len)
                TJS_eTJSError(TJSRangeError);
            tjs_char bf[2];
            bf[1] = 0;
            bf[0] = s[n];
            *result = tTJSVariant(bf);
        }
    }

    //---------------------------------------------------------------------------
    static void SetStringProperty(tTJSVariant *param, const tTJSVariant *str,
                                  const tTJSVariant &member) {
        // processes properties toward strings.
        if(member.Type() != tvtInteger && member.Type() != tvtReal) {
            const tjs_char *name = member.GetString();
            if(!name)
                TJSThrowFrom_tjs_error(TJS_E_MEMBERNOTFOUND, TJS_W(""));

            if(!TJS_strcmp(name, TJS_W("length"))) {
                TJSThrowFrom_tjs_error(TJS_E_ACCESSDENYED, TJS_W(""));
            } else if(name[0] >= TJS_W('0') && name[0] <= TJS_W('9')) {
                TJSThrowFrom_tjs_error(TJS_E_ACCESSDENYED, TJS_W(""));
            }

            TJSThrowFrom_tjs_error(TJS_E_MEMBERNOTFOUND, name);
        } else // member.Type() == tvtInteger || member.Type() ==
               // tvtReal
        {
            TJSThrowFrom_tjs_error(TJS_E_ACCESSDENYED, TJS_W(""));
        }
    }

    //---------------------------------------------------------------------------
    static void GetOctetProperty(tTJSVariant *result, const tTJSVariant *octet,
                                 const tTJSVariant &member) {
        // processes properties toward octets.
        if(member.Type() != tvtInteger && member.Type() != tvtReal) {
            const tjs_char *name = member.GetString();
            if(!name)
                TJSThrowFrom_tjs_error(TJS_E_MEMBERNOTFOUND, TJS_W(""));

            if(!TJS_strcmp(name, TJS_W("length"))) {
                // get string length
                tTJSVariantOctet *o = octet->AsOctetNoAddRef();
                if(o)
                    *result = tTVInteger(o->GetLength());
                else
                    *result = tTVInteger(0);
                return;
            }
            if(name[0] >= TJS_W('0') && name[0] <= TJS_W('9')) {
                tTJSVariantOctet *o = octet->AsOctetNoAddRef();
                tjs_int n = TJS_atoi(name);
                tjs_int len = o ? o->GetLength() : 0;
                if(n < 0 || n >= len)
                    TJS_eTJSError(TJSRangeError);
                *result = tTVInteger(o->GetData()[n]);
                return;
            }

            TJSThrowFrom_tjs_error(TJS_E_MEMBERNOTFOUND, name);
        } else // member.Type() == tvtInteger || member.Type() ==
               // tvtReal
        {
            tTJSVariantOctet *o = octet->AsOctetNoAddRef();
            tjs_int n = (tjs_int)member.AsInteger();
            tjs_int len = o ? o->GetLength() : 0;
            if(n < 0 || n >= len)
                TJS_eTJSError(TJSRangeError);
            *result = tTVInteger(o->GetData()[n]);
        }
    }

    //---------------------------------------------------------------------------
    static void SetOctetProperty(tTJSVariant *param, const tTJSVariant *octet,
                                 const tTJSVariant &member) {
        // processes properties toward octets.
        if(member.Type() != tvtInteger && member.Type() != tvtReal) {
            const tjs_char *name = member.GetString();
            if(!name)
                TJSThrowFrom_tjs_error(TJS_E_MEMBERNOTFOUND, TJS_W(""));

            if(!TJS_strcmp(name, TJS_W("length"))) {
                TJSThrowFrom_tjs_error(TJS_E_ACCESSDENYED, TJS_W(""));
            } else if(name[0] >= TJS_W('0') && name[0] <= TJS_W('9')) {
                TJSThrowFrom_tjs_error(TJS_E_ACCESSDENYED, TJS_W(""));
            }

            TJSThrowFrom_tjs_error(TJS_E_MEMBERNOTFOUND, name);
        } else // member.Type() == tvtInteger || member.Type() ==
               // tvtReal
        {
            TJSThrowFrom_tjs_error(TJS_E_ACCESSDENYED, TJS_W(""));
        }
    }

    //---------------------------------------------------------------------------
    // tTJSObjectProxy
    //---------------------------------------------------------------------------
    class tTJSObjectProxy : public iTJSDispatch2 {
        /*
                a class that do:
                1. first access to the Dispatch1
                2. if failed, then access to the Dispatch2
        */
        //	tjs_uint RefCount;

    public:
        tTJSObjectProxy() {
            //		RefCount = 1;
            //		Dispatch1 = nullptr;
            //		Dispatch2 = nullptr;
            // Dispatch1 and Dispatch2 are to be set by subsequent
            // call of
            // SetObjects
        };

        virtual ~tTJSObjectProxy() {
            if(Dispatch1)
                Dispatch1->Release();
            if(Dispatch2)
                Dispatch2->Release();
        };

        void SetObjects(iTJSDispatch2 *dsp1, iTJSDispatch2 *dsp2) {
            Dispatch1 = dsp1;
            Dispatch2 = dsp2;
            if(dsp1)
                dsp1->AddRef();
            if(dsp2)
                dsp2->AddRef();
        }

    private:
        iTJSDispatch2 *Dispatch1;
        iTJSDispatch2 *Dispatch2;

    public:
        tjs_uint AddRef() override { return 1; }

        tjs_uint Release() override { return 1; }

#define OBJ1 ((objthis) ? (objthis) : (Dispatch1))
#define OBJ2 ((objthis) ? (objthis) : (Dispatch2))

        tjs_error FuncCall(tjs_uint32 flag, const tjs_char *membername,
                           tjs_uint32 *hint, tTJSVariant *result,
                           tjs_int numparams, tTJSVariant **param,
                           iTJSDispatch2 *objthis) override {
            tjs_error hr = Dispatch1->FuncCall(flag, membername, hint, result,
                                               numparams, param, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->FuncCall(flag, membername, hint, result,
                                           numparams, param, OBJ2);
            return hr;
        }

        tjs_error FuncCallByNum(tjs_uint32 flag, tjs_int num,
                                tTJSVariant *result, tjs_int numparams,
                                tTJSVariant **param,
                                iTJSDispatch2 *objthis) override {
            tjs_error hr = Dispatch1->FuncCallByNum(flag, num, result,
                                                    numparams, param, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->FuncCallByNum(flag, num, result, numparams,
                                                param, OBJ2);
            return hr;
        }

        tjs_error PropGet(tjs_uint32 flag, const tjs_char *membername,
                          tjs_uint32 *hint, tTJSVariant *result,
                          iTJSDispatch2 *objthis) override {
            tjs_error hr =
                Dispatch1->PropGet(flag, membername, hint, result, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->PropGet(flag, membername, hint, result, OBJ2);
            return hr;
        }

        tjs_error PropGetByNum(tjs_uint32 flag, tjs_int num,
                               tTJSVariant *result,
                               iTJSDispatch2 *objthis) override {
            tjs_error hr = Dispatch1->PropGetByNum(flag, num, result, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->PropGetByNum(flag, num, result, OBJ2);
            return hr;
        }

        tjs_error PropSet(tjs_uint32 flag, const tjs_char *membername,
                          tjs_uint32 *hint, const tTJSVariant *param,
                          iTJSDispatch2 *objthis) override {
            tjs_error hr =
                Dispatch1->PropSet(flag, membername, hint, param, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->PropSet(flag, membername, hint, param, OBJ2);
            return hr;
        }

        tjs_error PropSetByNum(tjs_uint32 flag, tjs_int num,
                               const tTJSVariant *param,
                               iTJSDispatch2 *objthis) override {
            tjs_error hr = Dispatch1->PropSetByNum(flag, num, param, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->PropSetByNum(flag, num, param, OBJ2);
            return hr;
        }

        tjs_error GetCount(tjs_int *result, const tjs_char *membername,
                           tjs_uint32 *hint, iTJSDispatch2 *objthis) override {
            tjs_error hr = Dispatch1->GetCount(result, membername, hint, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->GetCount(result, membername, hint, OBJ2);
            return hr;
        }

        tjs_error GetCountByNum(tjs_int *result, tjs_int num,
                                iTJSDispatch2 *objthis) override {
            tjs_error hr = Dispatch1->GetCountByNum(result, num, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->GetCountByNum(result, num, OBJ2);
            return hr;
        }

        tjs_error PropSetByVS(tjs_uint32 flag, tTJSVariantString *membername,
                              const tTJSVariant *param,
                              iTJSDispatch2 *objthis) override {
            tjs_error hr =
                Dispatch1->PropSetByVS(flag, membername, param, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->PropSetByVS(flag, membername, param, OBJ2);
            return hr;
        }

        tjs_error EnumMembers(tjs_uint32 flag, tTJSVariantClosure *callback,
                              iTJSDispatch2 *objthis) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error DeleteMember(tjs_uint32 flag, const tjs_char *membername,
                               tjs_uint32 *hint,
                               iTJSDispatch2 *objthis) override {
            tjs_error hr =
                Dispatch1->DeleteMember(flag, membername, hint, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->DeleteMember(flag, membername, hint, OBJ2);
            return hr;
        }

        tjs_error DeleteMemberByNum(tjs_uint32 flag, tjs_int num,
                                    iTJSDispatch2 *objthis) override {
            tjs_error hr = Dispatch1->DeleteMemberByNum(flag, num, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->DeleteMemberByNum(flag, num, OBJ2);
            return hr;
        }

        tjs_error Invalidate(tjs_uint32 flag, const tjs_char *membername,
                             tjs_uint32 *hint,
                             iTJSDispatch2 *objthis) override {
            tjs_error hr = Dispatch1->Invalidate(flag, membername, hint, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->Invalidate(flag, membername, hint, OBJ2);
            return hr;
        }

        tjs_error InvalidateByNum(tjs_uint32 flag, tjs_int num,
                                  iTJSDispatch2 *objthis) override {
            tjs_error hr = Dispatch1->InvalidateByNum(flag, num, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->InvalidateByNum(flag, num, OBJ2);
            return hr;
        }

        tjs_error IsValid(tjs_uint32 flag, const tjs_char *membername,
                          tjs_uint32 *hint, iTJSDispatch2 *objthis) override {
            tjs_error hr = Dispatch1->IsValid(flag, membername, hint, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->IsValid(flag, membername, hint, OBJ2);
            return hr;
        }

        tjs_error IsValidByNum(tjs_uint32 flag, tjs_int num,
                               iTJSDispatch2 *objthis) override {
            tjs_error hr = Dispatch1->IsValidByNum(flag, num, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->IsValidByNum(flag, num, OBJ2);
            return hr;
        }

        tjs_error CreateNew(tjs_uint32 flag, const tjs_char *membername,
                            tjs_uint32 *hint, iTJSDispatch2 **result,
                            tjs_int numparams, tTJSVariant **param,
                            iTJSDispatch2 *objthis) override {
            tjs_error hr = Dispatch1->CreateNew(flag, membername, hint, result,
                                                numparams, param, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->CreateNew(flag, membername, hint, result,
                                            numparams, param, OBJ2);
            return hr;
        }

        tjs_error CreateNewByNum(tjs_uint32 flag, tjs_int num,
                                 iTJSDispatch2 **result, tjs_int numparams,
                                 tTJSVariant **param,
                                 iTJSDispatch2 *objthis) override {
            tjs_error hr = Dispatch1->CreateNewByNum(flag, num, result,
                                                     numparams, param, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->CreateNewByNum(flag, num, result, numparams,
                                                 param, OBJ2);
            return hr;
        }

        tjs_error Reserved1() override { return TJS_E_NOTIMPL; }

        tjs_error IsInstanceOf(tjs_uint32 flag, const tjs_char *membername,
                               tjs_uint32 *hint, const tjs_char *classname,
                               iTJSDispatch2 *objthis) override {
            tjs_error hr = Dispatch1->IsInstanceOf(flag, membername, hint,
                                                   classname, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->IsInstanceOf(flag, membername, hint,
                                               classname, OBJ2);
            return hr;
        }

        tjs_error IsInstanceOfByNum(tjs_uint32 flag, tjs_int num,
                                    const tjs_char *classname,
                                    iTJSDispatch2 *objthis) override {
            tjs_error hr =
                Dispatch1->IsInstanceOfByNum(flag, num, classname, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->IsInstanceOfByNum(flag, num, classname, OBJ2);
            return hr;
        }

        tjs_error Operation(tjs_uint32 flag, const tjs_char *membername,
                            tjs_uint32 *hint, tTJSVariant *result,
                            const tTJSVariant *param,
                            iTJSDispatch2 *objthis) override {
            tjs_error hr = Dispatch1->Operation(flag, membername, hint, result,
                                                param, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->Operation(flag, membername, hint, result,
                                            param, OBJ2);
            return hr;
        }

        tjs_error OperationByNum(tjs_uint32 flag, tjs_int num,
                                 tTJSVariant *result, const tTJSVariant *param,
                                 iTJSDispatch2 *objthis) override {
            tjs_error hr =
                Dispatch1->OperationByNum(flag, num, result, param, OBJ1);
            if(hr == TJS_E_MEMBERNOTFOUND && Dispatch1 != Dispatch2)
                return Dispatch2->OperationByNum(flag, num, result, param,
                                                 OBJ2);
            return hr;
        }

        tjs_error NativeInstanceSupport(tjs_uint32 flag, tjs_int32 classid,
                                        iTJSNativeInstance **pointer) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error ClassInstanceInfo(tjs_uint32 flag, tjs_uint num,
                                    tTJSVariant *value) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error Reserved2() override { return TJS_E_NOTIMPL; }

        tjs_error Reserved3() override { return TJS_E_NOTIMPL; }
    };

#undef OBJ1
#undef OBJ2

//---------------------------------------------------------------------------
// tTJSVariantArrayStack
//---------------------------------------------------------------------------
// TODO: adjust TJS_VA_ONE_ALLOC_MIN
#define TJS_VA_ONE_ALLOC_MAX 1024
#define TJS_COMPACT_FREQ 10000
    static tjs_int TJSCompactVariantArrayMagic = 0;
    static std::mutex TJSVariantArrayStackMutex;
    static std::set<tTJSVariantArrayStack *> TJSVariantArrayStacks;

    //---------------------------------------------------------------------------
    tTJSVariantArrayStack::tTJSVariantArrayStack() {
        NumArraysAllocated = NumArraysUsing = 0;
        Arrays = nullptr;
        Current = nullptr;
        OperationDisabledCount = 0;
        CompactVariantArrayMagic = TJSCompactVariantArrayMagic;
        std::lock_guard<std::mutex> lk(TJSVariantArrayStackMutex);
        TJSVariantArrayStacks.insert(this);
    }

    //---------------------------------------------------------------------------
    tTJSVariantArrayStack::~tTJSVariantArrayStack() {
        OperationDisabledCount++;
        tjs_int i;
        for(i = 0; i < NumArraysAllocated; i++) {
            delete[] Arrays[i].Array;
        }
        TJS_free(Arrays), Arrays = nullptr;
        std::lock_guard<std::mutex> lk(TJSVariantArrayStackMutex);
        TJSVariantArrayStacks.erase(this);
    }

    //---------------------------------------------------------------------------
    void tTJSVariantArrayStack::IncreaseVariantArray(tjs_int num) {
        // increase array block
        NumArraysUsing++;
        if(NumArraysUsing > NumArraysAllocated) {
            Arrays = (tVariantArray *)TJS_realloc(
                Arrays, sizeof(tVariantArray) * (NumArraysUsing));
            NumArraysAllocated = NumArraysUsing;
            Current = Arrays + NumArraysUsing - 1;
            Current->Array = new tTJSVariant[num];
        } else {
            Current = Arrays + NumArraysUsing - 1;
        }

        Current->Allocated = num;
        Current->Using = 0;
    }

    //---------------------------------------------------------------------------
    void tTJSVariantArrayStack::DecreaseVariantArray() {
        // decrease array block
        NumArraysUsing--;
        if(NumArraysUsing == 0)
            Current = nullptr;
        else
            Current = Arrays + NumArraysUsing - 1;
    }

    //---------------------------------------------------------------------------
    void tTJSVariantArrayStack::InternalCompact() {
        // minimize variant array block
        OperationDisabledCount++;
        try {
            while(NumArraysAllocated > NumArraysUsing) {
                NumArraysAllocated--;
                delete[] Arrays[NumArraysAllocated].Array;
            }

            if(Current) {
                for(tjs_int i = Current->Using; i < Current->Allocated; i++)
                    Current->Array[i].Clear();
            }

            if(NumArraysUsing == 0) {
                if(Arrays)
                    TJS_free(Arrays), Arrays = nullptr;
                Current = nullptr;
            } else {
                bool availableoffset = false;
                ptrdiff_t offset = 0;
                if(Current != nullptr && Arrays != nullptr) {
                    offset = Current - Arrays;
                    availableoffset = true;
                }

                tVariantArray *arraytmp = (tVariantArray *)TJS_realloc(
                    Arrays, sizeof(tVariantArray) * (NumArraysUsing));
                if(arraytmp != nullptr) {
                    Arrays = arraytmp;
                } else if(NumArraysUsing > 0) {
                    TJS_eTJSError(TJSInternalError);
                }

                if(availableoffset && Arrays != nullptr) {
                    Current = Arrays + offset;
                } else {
                    Current = nullptr;
                }
            }
        } catch(...) {
            OperationDisabledCount--;
            throw;
        }
        OperationDisabledCount--;
    }

    //---------------------------------------------------------------------------
    inline tTJSVariant *tTJSVariantArrayStack::Allocate(tjs_int num) {
        //		tTJSCSH csh(CS);

        if(!OperationDisabledCount && num < TJS_VA_ONE_ALLOC_MAX) {
            if(!Current || Current->Using + num > Current->Allocated) {
                IncreaseVariantArray(TJS_VA_ONE_ALLOC_MAX);
            }
            tTJSVariant *ret = Current->Array + Current->Using;
            Current->Using += num;
            return ret;
        } else {
            return new tTJSVariant[num];
        }
    }

    //---------------------------------------------------------------------------
    inline void tTJSVariantArrayStack::Deallocate(tjs_int num,
                                                  tTJSVariant *ptr) {
        //		tTJSCSH csh(CS);

        if(!OperationDisabledCount && num < TJS_VA_ONE_ALLOC_MAX) {
            Current->Using -= num;
            if(Current->Using == 0) {
                DecreaseVariantArray();
            }
        } else {
            delete[] ptr;
        }

        if(!OperationDisabledCount) {
            if(CompactVariantArrayMagic != TJSCompactVariantArrayMagic) {
                Compact();
                CompactVariantArrayMagic = TJSCompactVariantArrayMagic;
            }
        }
    }

    //---------------------------------------------------------------------------
    // static tjs_int TJSVariantArrayStackRefCount = 0;
    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::TJSVariantArrayStackAddRef() {}

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::TJSVariantArrayStackRelease() {}

    //---------------------------------------------------------------------------
    void TJSVariantArrayStackCompact() { TJSCompactVariantArrayMagic++; }

    //---------------------------------------------------------------------------
    static tTJSVariantArrayStack *TJSGlobalVariantArrayStack = nullptr;

    void TJSSetGlobalVariantArrayStack(tTJSVariantArrayStack *stack) {
        TJSGlobalVariantArrayStack = stack;
    }

    void TJSVariantArrayStackCompactNow() {
        if(TJSGlobalVariantArrayStack)
            TJSGlobalVariantArrayStack->Compact();
    }
    //---------------------------------------------------------------------------
    //---------------------------------------------------------------------------

    //---------------------------------------------------------------------------
    // tTJSInterCodeContext ( class definitions are in
    // tjsInterCodeGen.h )
    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::ExecuteAsFunction(iTJSDispatch2 *objthis,
                                                 tTJSVariant **args,
                                                 tjs_int numargs,
                                                 tTJSVariant *result,
                                                 tjs_int start_ip) {
        if(!GetValidity() || !CodeArea) {
            TJSThrowFrom_tjs_error(TJS_E_INVALIDOBJECT);
        }
        TJSFunctionProfileGuard functionProfile(this, objthis);
        struct tExecutingContextRefGuard {
            tTJSInterCodeContext *Self;
            iTJSDispatch2 *ObjThis;

            tExecutingContextRefGuard(tTJSInterCodeContext *self,
                                      iTJSDispatch2 *objthis)
                : Self(self), ObjThis(objthis) {
                Self->AddRef();
                Self->EnterExecution();
                if(ObjThis)
                    ObjThis->AddRef();
            }

            ~tExecutingContextRefGuard() {
                Self->LeaveExecution();
                if(ObjThis)
                    ObjThis->Release();
                Self->Release();
            }
        } executing_context_ref(this, objthis);

        TJSTraceSaveFunctionEnter(this, objthis, args, numargs);
        tjs_int num_alloc =
            MaxVariableCount + VariableReserveCount + 1 + MaxFrameCount;
        TJSVariantArrayStackAddRef();
        try {
            tTJSVariant *regs = TJSVariantArrayStack->Allocate(num_alloc);
            tTJSVariant *ra =
                regs + MaxVariableCount + VariableReserveCount; // register area

            // objthis-proxy

            tTJSObjectProxy proxy;
            if(objthis) {
                proxy.SetObjects(objthis, CachedTJSEngine->GetGlobalNoAddRef());
                // TODO: caching of objthis-proxy

                ra[-2] = &proxy;
            } else {
                proxy.SetObjects(nullptr, nullptr);

                iTJSDispatch2 *global = CachedTJSEngine->GetGlobalNoAddRef();

                ra[-2].SetObject(global, global);
            }

            /*
                            if(objthis)
                            {
                                    // TODO: caching of objthis-proxy
                                    tTJSObjectProxy *proxy = new
               tTJSObjectProxy(); proxy->SetObjects(objthis,
               CachedTJSEngine->GetGlobalNoAddRef());

                                    ra[-2] = proxy;

                                    proxy->Release();
                            }
                            else
                            {
                                    iTJSDispatch2 *global =
               CachedTJSEngine->GetGlobalNoAddRef();

                                    ra[-2].SetObject(global, global);
                            }
            */
            if(TJSStackTracerEnabled())
                TJSStackTracerPush(this, false);

            // check whether the objthis is deleting
            if(TJSWarnOnExecutionOnDeletingObject && TJSObjectFlagEnabled() &&
               CachedTJSEngine->GetConsoleOutput())
                TJSWarnIfObjectIsDeleting(CachedTJSEngine->GetConsoleOutput(),
                                          objthis);

#ifdef _DEBUG
            ScopeKey oldkey;
            tTJSVariant *oldra = nullptr;
#endif // _DEBUG
            try {
                TJSTraceExecArgs(this, objthis, args, numargs, FuncDeclArgCount,
                                 FuncDeclCollapseBase);
                ra[-1].SetObject(objthis, objthis);
                ra[0].Clear();

                // transfer arguments
                if(numargs >= FuncDeclArgCount) {
                    // given arguments are greater than or equal to
                    // desired arguments
                    if(FuncDeclArgCount) {
                        tTJSVariant *r = ra - 3;
                        tTJSVariant **a = args;
                        tjs_int n = FuncDeclArgCount;
                        while(true) {
                            *r = **(a++);
                            n--;
                            if(!n)
                                break;
                            r--;
                        }
                    }
                } else {
                    // given arguments are less than desired arguments
                    tTJSVariant *r = ra - 3;
                    tTJSVariant **a = args;
                    tjs_int i;
                    for(i = 0; i < numargs; i++)
                        *(r--) = **(a++);
                    for(; i < FuncDeclArgCount; i++)
                        (r--)->Clear();
                }

                // collapse into array when FuncDeclCollapseBase >= 0
                if(FuncDeclCollapseBase >= 0) {
                    tTJSVariant *r =
                        ra - 3 - FuncDeclCollapseBase; // target variant
                    iTJSDispatch2 *dsp = TJSCreateArrayObject();
                    *r = tTJSVariant(dsp, dsp);
                    dsp->Release();

                    if(numargs > FuncDeclCollapseBase) {
                        // there are arguments to store
                        for(tjs_int c = 0, i = FuncDeclCollapseBase;
                            i < numargs; i++, c++)
                            dsp->PropSetByNum(0, c, args[i], dsp);
                    }
                }

                // execute
                ExecuteCode(ra, start_ip, args, numargs, result);
            } catch(...) {
                ra[-2].Clear(); // at least we must clear the object
                                // placed at local stack
                TJSVariantArrayStack->Deallocate(num_alloc, regs);
                if(TJSStackTracerEnabled())
                    TJSStackTracerPop();
                throw;
            }

#ifdef _DEBUG
            DebuggerScopeKey = oldkey;
            DebuggerRegisterArea = oldra;
#endif // _DEBUG
            ra[-2].Clear(); // at least we must clear the object
                            // placed at local stack

            TJSVariantArrayStack->Deallocate(num_alloc, regs);

            if(TJSStackTracerEnabled())
                TJSStackTracerPop();
        } catch(...) {
            TJSVariantArrayStackRelease();
            throw;
        }
        TJSVariantArrayStackRelease();
    }

    //---------------------------------------------------------------------------
    void
    tTJSInterCodeContext::DisplayExceptionGeneratedCode(tjs_int codepos,
                                                        const tTJSVariant *ra) {
        {
            static std::unordered_map<uintptr_t, uint64_t> recentDumps;
            auto now = std::chrono::steady_clock::now().time_since_epoch();
            uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            uintptr_t key = reinterpret_cast<uintptr_t>(Block) ^ static_cast<uintptr_t>(codepos);
            auto it = recentDumps.find(key);
            if(it != recentDumps.end() && nowMs - it->second < 10000) {
                return;
            }
            recentDumps[key] = nowMs;
            if(recentDumps.size() > 256) {
                for(auto ri = recentDumps.begin(); ri != recentDumps.end();) {
                    if(nowMs - ri->second > 30000)
                        ri = recentDumps.erase(ri);
                    else
                        ++ri;
                }
            }
        }

        if(!Block) return;
        tTJS *tjs = CachedTJSEngine;
        ttstr info{ fmt::format(
            "==== An exception occurred at {}, VM ip = {} ==== ",
            GetPositionDescriptionString(codepos).AsNarrowStdString(),
            codepos) };
        tjs_int info_len = info.GetLen();

        tjs->OutputToConsole(info.c_str());
        tjs->OutputToConsole(TJS_W("-- Disassembled VM code --"));
        DisassembleSrcLine(codepos);
        if(TJSSceneTraceEnabled()) {
            tjs_int start = FindSrcLineStartCodePos(codepos);
            Disassemble(
                [](const tjs_char *msg, void *) {
                    spdlog::info("TJSSaveTrace disasm {}",
                                 ttstr(msg).AsStdString());
                },
                nullptr, start, codepos + 1);
        }

        tjs->OutputToConsole(TJS_W("-- Register dump --"));

        const tTJSVariant *ra_start =
            ra - (MaxVariableCount + VariableReserveCount);
        tjs_int ra_count =
            MaxVariableCount + VariableReserveCount + 1 + MaxFrameCount;
        ttstr line;
        for(tjs_int i = 0; i < ra_count; i++) {
            ttstr reg_info = TJS_W("%") +
                ttstr(i - (MaxVariableCount + VariableReserveCount)) +
                TJS_W("=") + TJSVariantToReadableString(ra_start[i]);
            if(line.GetLen() + reg_info.GetLen() + 2 > info_len) {
                tjs->OutputToConsole(line.c_str());
                line = reg_info;
            } else {
                if(!line.IsEmpty())
                    line += TJS_W("  ");
                line += reg_info;
            }
        }

        if(!line.IsEmpty()) {
            tjs->OutputToConsole(line.c_str());
        }

        tjs->OutputToConsoleSeparator(TJS_W("-"), info_len);
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::ThrowScriptException(tTJSVariant &val,
                                                    tTJSScriptBlock *block,
                                                    tjs_int srcpos) {
        tTJSString msg;
        if(val.Type() == tvtObject) {
            try {
                tTJSVariantClosure clo = val.AsObjectClosureNoAddRef();
                if(clo.Object != nullptr) {
                    tTJSVariant v2;
                    static tTJSString message_name(TJS_W("message"));
                    tjs_error hr =
                        clo.PropGet(0, message_name.c_str(),
                                    message_name.GetHint(), &v2, nullptr);
                    if(TJS_SUCCEEDED(hr)) {
                        msg = ttstr(TJS_W("script exception : ")) + ttstr(v2);
                    }
                }
            } catch(...) {
            }
        }

        if(msg.IsEmpty()) {
            msg = TJS_W("script exception");
        }

        TJS_eTJSScriptException(msg, this, srcpos, val);
    }

    //---------------------------------------------------------------------------
    tjs_int
    tTJSInterCodeContext::ExecuteCode(tTJSVariant *ra_org, tjs_int startip,
                                      tTJSVariant **args, tjs_int numargs,
                                      tTJSVariant *result, bool tryCatch) {
        // execute VM codes
        if(TJSReplayTraceEnabled() &&
           GetShortDescriptionWithClassName().AsStdString().find(
               "loadFunction") != std::string::npos)
            spdlog::info("TJSSaveTrace replay ExecuteCode desc=\"{}\" "
                         "startip={} code0={}",
                         GetShortDescriptionWithClassName().AsStdString(),
                         startip, CodeArea ? CodeArea[0] : -1);
        tjs_int32 *codesave;
        try {
            if(!CodeArea) {
                TJSThrowFrom_tjs_error(TJS_E_INVALIDOBJECT);
            }
            tjs_int32 *code = codesave = CodeArea + startip;

            if(TJSStackTracerEnabled())
                TJSStackTracerSetCodePointer(CodeArea, &codesave);

            tTJSVariant *ra = ra_org;
            tTJSVariant *da = DataArea;

            bool flag = false;

            while(true) {
                codesave = code;
                if(TJSReplayTraceEnabled()) {
                    const std::string replayDesc =
                        GetShortDescriptionWithClassName().AsStdString();
                    if(replayDesc.find("ExSceneGalleryBase.loadFunction") !=
                           std::string::npos &&
                       (code - CodeArea) >= 0 && (code - CodeArea) < 260) {
                        static int replayOpsLogged = 0;
                        if(replayOpsLogged < 800) {
                            ++replayOpsLogged;
                            spdlog::info("TJSSaveTrace replay op desc=\"{}\" "
                                         "ip={} opcode={} flag={}",
                                         replayDesc, code - CodeArea, *code,
                                         flag);
                        }
                    }
                }
                switch(*code) {
                    case VM_NOP:
                        code++;
                        break;

                    case VM_CONST:
                        TJS_GET_VM_REG(ra, code[1])
                            .CopyRef(TJS_GET_VM_REG(da, code[2]));
                        code += 3;
                        break;

                    case VM_CP:
                        TJSTraceVMCopy(this, CodeArea, code, ra);
                        TJS_GET_VM_REG(ra, code[1])
                            .CopyRef(TJS_GET_VM_REG(ra, code[2]));
                        code += 3;
                        break;

                    case VM_CL:
                        TJS_GET_VM_REG(ra, code[1]).Clear();
                        code += 2;
                        break;

                    case VM_CCL:
                        ContinuousClear(ra, code);
                        code += 3;
                        break;

                    case VM_TT:
                        flag = TJS_GET_VM_REG(ra, code[1]).operator bool();
                        if(TJSReplayTraceEnabled() &&
                           GetShortDescriptionWithClassName()
                                   .AsStdString()
                                   .find("loadFunction") != std::string::npos)
                            spdlog::info("TJSSaveTrace replay branch op=tt "
                                         "ip={} reg={} value={} flag={}",
                                         code - CodeArea, code[1],
                                         TJSSaveTraceVariantString(
                                             TJS_GET_VM_REG(ra, code[1])),
                                         flag);
                        code += 2;
                        break;

                    case VM_TF:
                        flag = !(TJS_GET_VM_REG(ra, code[1]).operator bool());
                        code += 2;
                        break;

                    case VM_CEQ:
                        flag = TJS_GET_VM_REG(ra, code[1])
                                   .NormalCompare(TJS_GET_VM_REG(ra, code[2]));
                        code += 3;
                        break;

                    case VM_CDEQ:
                        flag = TJS_GET_VM_REG(ra, code[1])
                                   .DiscernCompare(TJS_GET_VM_REG(ra, code[2]));
                        if(TJSReplayTraceEnabled() &&
                           GetShortDescriptionWithClassName()
                                   .AsStdString()
                                   .find("loadFunction") != std::string::npos)
                            spdlog::info("TJSSaveTrace replay branch op=cdeq "
                                         "desc=\"{}\" ip={} lhs={} rhs={} "
                                         "lhs_value={} rhs_value={} flag={}",
                                         GetShortDescriptionWithClassName()
                                             .AsStdString(),
                                         code - CodeArea, code[1], code[2],
                                         TJSSaveTraceVariantString(
                                             TJS_GET_VM_REG(ra, code[1])),
                                         TJSSaveTraceVariantString(
                                             TJS_GET_VM_REG(ra, code[2])),
                                         flag);
                        code += 3;
                        break;

                    case VM_CLT:
                        flag = TJS_GET_VM_REG(ra, code[1])
                                   .GreaterThan(TJS_GET_VM_REG(ra, code[2]));
                        code += 3;
                        break;

                    case VM_CGT:
                        flag = TJS_GET_VM_REG(ra, code[1])
                                   .LittlerThan(TJS_GET_VM_REG(ra, code[2]));
                        code += 3;
                        break;

                    case VM_SETF:
                        TJS_GET_VM_REG(ra, code[1]) = flag;
                        code += 2;
                        break;

                    case VM_SETNF:
                        TJS_GET_VM_REG(ra, code[1]) = !flag;
                        code += 2;
                        break;

                    case VM_LNOT:
                        TJS_GET_VM_REG(ra, code[1]).logicalnot();
                        code += 2;
                        break;

                    case VM_NF:
                        flag = !flag;
                        code++;
                        break;

                    case VM_JF:
                        if(flag)
                            TJS_ADD_VM_CODE_ADDR(code, code[1]);
                        else
                            code += 2;
                        break;

                    case VM_JNF:
                        if(TJSReplayTraceEnabled() &&
                           GetShortDescriptionWithClassName()
                                   .AsStdString()
                                   .find("loadFunction") != std::string::npos)
                            spdlog::info("TJSSaveTrace replay branch op=jnf "
                                         "ip={} target={} flag={}",
                                         code - CodeArea, code[1], flag);
                        if(!flag)
                            TJS_ADD_VM_CODE_ADDR(code, code[1]);
                        else
                            code += 2;
                        break;

                    case VM_JMP:
                        TJS_ADD_VM_CODE_ADDR(code, code[1]);
                        break;

                    case VM_INC:
                        TJS_GET_VM_REG(ra, code[1]).increment();
                        code += 2;
                        break;

                    case VM_INCPD:
                        OperatePropertyDirect0(ra, code, TJS_OP_INC);
                        code += 4;
                        break;

                    case VM_INCPI:
                        OperatePropertyIndirect0(ra, code, TJS_OP_INC);
                        code += 4;
                        break;

                    case VM_INCP:
                        OperateProperty0(ra, code, TJS_OP_INC);
                        code += 3;
                        break;

                    case VM_DEC:
                        TJS_GET_VM_REG(ra, code[1]).decrement();
                        code += 2;
                        break;

                    case VM_DECPD:
                        OperatePropertyDirect0(ra, code, TJS_OP_DEC);
                        code += 4;
                        break;

                    case VM_DECPI:
                        OperatePropertyIndirect0(ra, code, TJS_OP_DEC);
                        code += 4;
                        break;

                    case VM_DECP:
                        OperateProperty0(ra, code, TJS_OP_DEC);
                        code += 3;
                        break;

#define TJS_DEF_VM_P(vmcode, rope)                                             \
    case VM_##vmcode:                                                          \
        TJS_GET_VM_REG(ra, code[1]).rope(TJS_GET_VM_REG(ra, code[2]));         \
        code += 3;                                                             \
        break;                                                                 \
    case VM_##vmcode##PD:                                                      \
        OperatePropertyDirect(ra, code, TJS_OP_##vmcode);                      \
        code += 5;                                                             \
        break;                                                                 \
    case VM_##vmcode##PI:                                                      \
        OperatePropertyIndirect(ra, code, TJS_OP_##vmcode);                    \
        code += 5;                                                             \
        break;                                                                 \
    case VM_##vmcode##P:                                                       \
        OperateProperty(ra, code, TJS_OP_##vmcode);                            \
        code += 4;                                                             \
        break

                        TJS_DEF_VM_P(LOR, logicalorequal);
                        TJS_DEF_VM_P(LAND, logicalandequal);
                        TJS_DEF_VM_P(BOR, operator|=);
                        TJS_DEF_VM_P(BXOR, operator^=);
                        TJS_DEF_VM_P(BAND, operator&=);
                        TJS_DEF_VM_P(SAR, operator>>=);
                        TJS_DEF_VM_P(SAL, operator<<=);
                        TJS_DEF_VM_P(SR, rbitshiftequal);
                        TJS_DEF_VM_P(ADD, operator+=);
                        TJS_DEF_VM_P(SUB, operator-=);
                        TJS_DEF_VM_P(MOD, operator%=);
                        TJS_DEF_VM_P(DIV, operator/=);
                        case VM_IDIV:
                        {
                            const bool trace_idiv =
                                TJSSaveTraceEnabled() && TJSSceneTraceEnabled() &&
                                GetShortDescriptionWithClassName()
                                        .AsStdString()
                                        .find("GalleryMode") != std::string::npos;
                            if(trace_idiv) {
                                spdlog::info(
                                    "TJSSaveTrace idiv desc=\"{}\" lhs={} rhs={} lhs_type={} rhs_type={}",
                                    GetShortDescriptionWithClassName().AsStdString(),
                                    TJSSaveTraceVariantString(TJS_GET_VM_REG(ra, code[1])),
                                    TJSSaveTraceVariantString(TJS_GET_VM_REG(ra, code[2])),
                                    static_cast<int>(TJS_GET_VM_REG(ra, code[1]).Type()),
                                    static_cast<int>(TJS_GET_VM_REG(ra, code[2]).Type()));
                            }
                            TJS_GET_VM_REG(ra, code[1]).idivequal(
                                TJS_GET_VM_REG(ra, code[2]));
                            if(trace_idiv) {
                                spdlog::info(
                                    "TJSSaveTrace idiv result={} desc=\"{}\"",
                                    TJSSaveTraceVariantString(TJS_GET_VM_REG(ra, code[1])),
                                    GetShortDescriptionWithClassName().AsStdString());
                            }
                            code += 3;
                            break;
                        }
                        TJS_DEF_VM_P(MUL, operator*=);

#undef TJS_DEF_VM_P

                    case VM_BNOT:
                        TJS_GET_VM_REG(ra, code[1]).bitnot();
                        code += 2;
                        break;

                    case VM_ASC:
                        CharacterCodeOf(TJS_GET_VM_REG(ra, code[1]));
                        code += 2;
                        break;

                    case VM_CHR:
                        CharacterCodeFrom(TJS_GET_VM_REG(ra, code[1]));
                        code += 2;
                        break;

                    case VM_NUM:
                        TJS_GET_VM_REG(ra, code[1]).tonumber();
                        code += 2;
                        break;

                    case VM_CHS:
                        TJS_GET_VM_REG(ra, code[1]).changesign();
                        code += 2;
                        break;

                    case VM_INV:
                        TJS_GET_VM_REG(ra, code[1]) =
                            TJS_GET_VM_REG(ra, code[1]).Type() != tvtObject
                            ? false
                            : (TJS_GET_VM_REG(ra, code[1])
                                   .AsObjectClosureNoAddRef()
                                   .Invalidate(0, nullptr, nullptr,
                                               ra[-1].AsObjectNoAddRef()) ==
                               TJS_S_TRUE);
                        code += 2;
                        break;

                    case VM_CHKINV:
                        TJS_GET_VM_REG(ra, code[1]) =
                            TJS_GET_VM_REG(ra, code[1]).Type() != tvtObject
                            ? true
                            : TJSIsObjectValid(
                                  TJS_GET_VM_REG(ra, code[1])
                                      .AsObjectClosureNoAddRef()
                                      .IsValid(0, nullptr, nullptr,
                                               ra[-1].AsObjectNoAddRef()));
                        code += 2;
                        break;

                    case VM_INT:
                        TJS_GET_VM_REG(ra, code[1]).ToInteger();
                        code += 2;
                        break;

                    case VM_REAL:
                        TJS_GET_VM_REG(ra, code[1]).ToReal();
                        code += 2;
                        break;

                    case VM_STR:
                        TJS_GET_VM_REG(ra, code[1]).ToString();
                        code += 2;
                        break;

                    case VM_OCTET:
                        TJS_GET_VM_REG(ra, code[1]).ToOctet();
                        code += 2;
                        break;

                    case VM_TYPEOF:
                        TypeOf(TJS_GET_VM_REG(ra, code[1]));
                        code += 2;
                        break;

                    case VM_TYPEOFD:
                        TypeOfMemberDirect(ra, code, TJS_MEMBERMUSTEXIST);
                        code += 4;
                        break;

                    case VM_TYPEOFI:
                        TypeOfMemberIndirect(ra, code, TJS_MEMBERMUSTEXIST);
                        code += 4;
                        break;

                    case VM_EVAL:
                        Eval(TJS_GET_VM_REG(ra, code[1]),
                             TJSEvalOperatorIsOnGlobal
                                 ? nullptr
                                 : ra[-1].AsObjectNoAddRef(),
                             true);
                        code += 2;
                        break;

                    case VM_EEXP:
                        Eval(TJS_GET_VM_REG(ra, code[1]),
                             TJSEvalOperatorIsOnGlobal
                                 ? nullptr
                                 : ra[-1].AsObjectNoAddRef(),
                             false);
                        code += 2;
                        break;

                    case VM_CHKINS:
                        InstanceOf(TJS_GET_VM_REG(ra, code[2]),
                                   TJS_GET_VM_REG(ra, code[1]));
                        code += 3;
                        break;

                    case VM_CALL:
                    case VM_NEW:
                        code += CallFunction(ra, code, args, numargs);
                        break;

                    case VM_CALLD:
                        code += CallFunctionDirect(ra, code, args, numargs);
                        break;

                    case VM_CALLI:
                        code += CallFunctionIndirect(ra, code, args, numargs);
                        break;

                    case VM_GPD:
                        GetPropertyDirect(ra, code, 0);
                        code += 4;
                        break;

                    case VM_GPDS:
                        GetPropertyDirect(ra, code, TJS_IGNOREPROP);
                        code += 4;
                        break;

                    case VM_SPD:
                        SetPropertyDirect(ra, code, 0);
                        code += 4;
                        break;

                    case VM_SPDE:
                        SetPropertyDirect(ra, code, TJS_MEMBERENSURE);
                        code += 4;
                        break;

                    case VM_SPDEH:
                        SetPropertyDirect(ra, code,
                                          TJS_MEMBERENSURE | TJS_HIDDENMEMBER);
                        code += 4;
                        break;

                    case VM_SPDS:
                        SetPropertyDirect(ra, code,
                                          TJS_MEMBERENSURE | TJS_IGNOREPROP);
                        code += 4;
                        break;

                    case VM_GPI:
                        GetPropertyIndirect(ra, code, 0);
                        code += 4;
                        break;

                    case VM_GPIS:
                        GetPropertyIndirect(ra, code, TJS_IGNOREPROP);
                        code += 4;
                        break;

                    case VM_SPI:
                        SetPropertyIndirect(ra, code, 0);
                        code += 4;
                        break;

                    case VM_SPIE:
                        SetPropertyIndirect(ra, code, TJS_MEMBERENSURE);
                        code += 4;
                        break;

                    case VM_SPIS:
                        SetPropertyIndirect(ra, code,
                                            TJS_MEMBERENSURE | TJS_IGNOREPROP);
                        code += 4;
                        break;

                    case VM_GETP:
                        GetProperty(ra, code);
                        code += 3;
                        break;

                    case VM_SETP:
                        SetProperty(ra, code);
                        code += 3;
                        break;

                    case VM_DELD:
                        DeleteMemberDirect(ra, code);
                        code += 4;
                        break;

                    case VM_DELI:
                        DeleteMemberIndirect(ra, code);
                        code += 4;
                        break;

                    case VM_SRV:
                        if(result)
                            result->CopyRef(TJS_GET_VM_REG(ra, code[1]));
                        code += 2;
                        break;

                    case VM_RET:
                        return code + 1 - CodeArea;

                    case VM_ENTRY:
                        code =
                            CodeArea +
                            ExecuteCodeInTryBlock(
                                ra, code - CodeArea + 3, args, numargs, result,
                                TJS_FROM_VM_CODE_ADDR(code[1]) + code -
                                    CodeArea,
                                TJS_FROM_VM_REG_ADDR(code[2]));
                        break;

                    case VM_EXTRY:
                        return code + 1 - CodeArea; // same as ret

                    case VM_THROW:
                        ThrowScriptException(TJS_GET_VM_REG(ra, code[1]), Block,
                                             CodePosToSrcPos(code - CodeArea));
                        code += 2; // actually here not proceed...
                        break;

                    case VM_CHGTHIS:
                        TJS_GET_VM_REG(ra, code[1])
                            .ChangeClosureObjThis(
                                TJS_GET_VM_REG(ra, code[2]).AsObjectNoAddRef());
                        code += 3;
                        break;

                    case VM_GLOBAL:
                        TJS_GET_VM_REG(ra, code[1]) =
                            CachedTJSEngine->GetGlobalNoAddRef();
                        code += 2;
                        break;

                    case VM_ADDCI:
                        AddClassInstanceInfo(ra, code);
                        code += 3;
                        break;

                    case VM_REGMEMBER:
                        RegisterObjectMember(ra[-1].AsObjectNoAddRef());
                        code++;
                        break;

                    case VM_DEBUGGER:
                        TJSNativeDebuggerBreak();
                        code++;
                        break;

                    default:
                        ThrowInvalidVMCode();
                }
            }
        } catch(eTJSSilent &) {
            throw;
        } catch(eTJSScriptError &e) {
            if(tryCatch &&
               (TJSSceneTraceEnabled() || TJSReplayTraceEnabled())) {
                const std::string desc =
                    GetShortDescriptionWithClassName().AsStdString();
                if(TJSSceneTraceMatches(desc)) {
                    spdlog::info("TJSSaveTrace exception desc=\"{}\" msg={} trace={} ip={}",
                                 desc, e.GetMessage().AsStdString(),
                                 e.GetTrace().AsStdString(),
                                 codesave - CodeArea);
                    DisplayExceptionGeneratedCode(codesave - CodeArea, ra_org);
                }
            }
            e.AddTrace(this, codesave - CodeArea);
            throw;
        } catch(eTJS &e) {
            if(tryCatch) {
                if(TJSSceneTraceEnabled() || TJSReplayTraceEnabled()) {
                    const std::string desc =
                        GetShortDescriptionWithClassName().AsStdString();
                    if(TJSSceneTraceMatches(desc)) {
                        spdlog::info("TJSSaveTrace exception desc=\"{}\" msg={} ip={}",
                                     desc, e.GetMessage().AsStdString(),
                                     codesave - CodeArea);
                        DisplayExceptionGeneratedCode(codesave - CodeArea,
                                                      ra_org);
                    }
                }
                spdlog::get("tjs2")->debug(e.GetMessage().AsStdString());
            } else {
                DisplayExceptionGeneratedCode(codesave - CodeArea, ra_org);
            }
            TJS_eTJSScriptError(e.GetMessage(), this, codesave - CodeArea);
        } catch(exception &e) {
            if(tryCatch) {
                if(TJSSceneTraceEnabled() || TJSReplayTraceEnabled()) {
                    const std::string desc =
                        GetShortDescriptionWithClassName().AsStdString();
                    if(TJSSceneTraceMatches(desc)) {
                        spdlog::info("TJSSaveTrace exception desc=\"{}\" msg={} ip={}",
                                     desc, e.what(), codesave - CodeArea);
                        DisplayExceptionGeneratedCode(codesave - CodeArea,
                                                      ra_org);
                    }
                }
                spdlog::get("tjs2")->debug(e.what());
            } else {
                DisplayExceptionGeneratedCode(codesave - CodeArea, ra_org);
            }
            TJS_eTJSScriptError(e.what(), this, codesave - CodeArea);
        } catch(const char *text) {
            if(tryCatch) {
                if(TJSSceneTraceEnabled() || TJSReplayTraceEnabled()) {
                    const std::string desc =
                        GetShortDescriptionWithClassName().AsStdString();
                    if(TJSSceneTraceMatches(desc)) {
                        spdlog::info("TJSSaveTrace exception desc=\"{}\" msg={} ip={}",
                                     desc, text, codesave - CodeArea);
                        DisplayExceptionGeneratedCode(codesave - CodeArea,
                                                      ra_org);
                    }
                }
                spdlog::get("tjs2")->debug(text);
            } else {
                DisplayExceptionGeneratedCode(codesave - CodeArea, ra_org);
            }
            TJS_eTJSScriptError(text, this, codesave - CodeArea);
        }

        return codesave - CodeArea;
    }

    //---------------------------------------------------------------------------
    tjs_int tTJSInterCodeContext::ExecuteCodeInTryBlock(
        tTJSVariant *ra, tjs_int startip, tTJSVariant **args, tjs_int numargs,
        tTJSVariant *result, tjs_int catchip, tjs_int exobjreg) {
        // execute codes in a try-protected block

        try {
            if(TJSStackTracerEnabled())
                TJSStackTracerPush(this, true);
            tjs_int ret;
            try {
                ret = ExecuteCode(ra, startip, args, numargs, result, true);
            } catch(...) {
                if(TJSStackTracerEnabled())
                    TJSStackTracerPop();
                throw;
            }
            if(TJSStackTracerEnabled())
                TJSStackTracerPop();
            return ret;
        } catch(eTJSSilent &) {
            throw;
        } catch(eTJSScriptException &e) {
            if(exobjreg)
                *(ra + exobjreg) = e.GetValue();
            return catchip;
        } catch(eTJSScriptError &e) {
            if(exobjreg) {
                tTJSVariant msg(e.GetMessage());
                tTJSVariant trace(e.GetTrace());
                TJSGetExceptionObject(CachedTJSEngine, ra + exobjreg, msg,
                                      &trace);
            }
            return catchip;
        } catch(eTJS &e) {
            if(exobjreg) {
                tTJSVariant msg(e.GetMessage());
                TJSGetExceptionObject(CachedTJSEngine, ra + exobjreg, msg,
                                      nullptr);
            }
            return catchip;
        } catch(std::exception &e) {
            if(exobjreg) {
                tTJSVariant msg(e.what());
                TJSGetExceptionObject(CachedTJSEngine, ra + exobjreg, msg,
                                      nullptr);
            }
            return catchip;
        } catch(...) {
            if(exobjreg)
                (ra + exobjreg)->Clear();
            return catchip;
        }
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::ContinuousClear(tTJSVariant *ra,
                                               const tjs_int32 *code) {
        tTJSVariant *r = TJS_GET_VM_REG_ADDR(ra, code[1]);
        tTJSVariant *rl = r + code[2]; // code[2] is count ( not reg offset )
        while(r < rl)
            (r++)->Clear();
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::GetPropertyDirect(tTJSVariant *ra,
                                                 const tjs_int32 *code,
                                                 tjs_uint32 flags) const {
        // ra[code[1]] = ra[code[2]][DataArea[ra[code[3]]]];

        tTJSVariant *ra_code2 = TJS_GET_VM_REG_ADDR(ra, code[2]);
        tTJSVariantType type = ra_code2->Type();
        if(type == tvtVoid) {
            TJS_GET_VM_REG_ADDR(ra, code[1])->Clear();
            return;
        }
        if(type == tvtString) {
            GetStringProperty(TJS_GET_VM_REG_ADDR(ra, code[1]), ra_code2,
                              TJS_GET_VM_REG(DataArea, code[3]));
            return;
        }
        if(type == tvtOctet) {
            GetOctetProperty(TJS_GET_VM_REG_ADDR(ra, code[1]), ra_code2,
                             TJS_GET_VM_REG(DataArea, code[3]));
            return;
        }

        tTJSVariant *name = TJS_GET_VM_REG_ADDR(DataArea, code[3]);
        tTJSVariant *dest = TJS_GET_VM_REG_ADDR(ra, code[1]);
        if(TJSCompatKAGEnvLayerTypeIntegerProperty(this, *ra_code2,
                                                   name->GetString(), dest))
            return;

        tTJSVariantClosure clo = ra_code2->AsObjectClosureNoAddRef();
        TJSTracePropertyDirectBefore(this, name->AsStringNoAddRef(), ra_code2,
                                     clo);
        tjs_error hr =
            clo.PropGet(flags, name->GetString(), name->GetHint(),
                        dest,
                        clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
        TJSTraceSavePropertyGet(this, "direct", name->AsStringNoAddRef(), *dest,
                                hr);
        if(TJS_FAILED(hr))
            TJSThrowFrom_tjs_error(
                hr, TJS_GET_VM_REG(DataArea, code[3]).GetString());
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::SetPropertyDirect(tTJSVariant *ra,
                                                 const tjs_int32 *code,
                                                 tjs_uint32 flags) const {
        // ra[code[1]][DataArea[ra[code[2]]]] = ra[code[3]]]

        tTJSVariant *ra_code1 = TJS_GET_VM_REG_ADDR(ra, code[1]);
        tTJSVariantType type = ra_code1->Type();
        if(type == tvtString) {
            SetStringProperty(TJS_GET_VM_REG_ADDR(ra, code[3]), ra_code1,
                              TJS_GET_VM_REG(DataArea, code[2]));
            return;
        }
        if(type == tvtOctet) {
            SetOctetProperty(TJS_GET_VM_REG_ADDR(ra, code[3]), ra_code1,
                             TJS_GET_VM_REG(DataArea, code[2]));
            return;
        }

        tTJSVariantClosure clo = ra_code1->AsObjectClosureNoAddRef();
        tTJSVariant *name = TJS_GET_VM_REG_ADDR(DataArea, code[2]);
        const bool action_prop_trace = [] {
            const char *value = std::getenv("AETHERKIRI_ACTION_PROP_TRACE");
            return value && *value && *value != '0';
        }();
        const std::string member_name = name->AsStringNoAddRef()
                                            ? ttstr(name->AsStringNoAddRef()).AsStdString()
                                            : std::string();
        const bool interesting_action_prop =
            member_name == "opacity" || member_name == "visible" ||
            member_name == "left" || member_name == "top" ||
            member_name == "width" || member_name == "height";
        if(action_prop_trace && interesting_action_prop) {
            spdlog::info(
                "ActionProp.direct before desc=\"{}\" name={} targetType={} "
                "object={} objthis={} value={}",
                GetShortDescriptionWithClassName().AsStdString(), member_name,
                static_cast<int>(ra_code1->Type()),
                static_cast<const void *>(clo.Object),
                static_cast<const void *>(clo.ObjThis),
                TJSSaveTraceVariantString(*TJS_GET_VM_REG_ADDR(ra, code[3])));
        }
        tjs_error hr = clo.PropSetByVS(
            flags, name->AsStringNoAddRef(), TJS_GET_VM_REG_ADDR(ra, code[3]),
            clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
        if(hr == TJS_E_NOTIMPL)
            hr = clo.PropSet(flags, name->GetString(), name->GetHint(),
                             TJS_GET_VM_REG_ADDR(ra, code[3]),
                             clo.ObjThis ? clo.ObjThis
                                         : ra[-1].AsObjectNoAddRef());
        TJSTraceSavePropertySet(this, "direct", name->AsStringNoAddRef(),
                                *TJS_GET_VM_REG_ADDR(ra, code[3]), hr);
        if(action_prop_trace && interesting_action_prop) {
            spdlog::info("ActionProp.direct after desc=\"{}\" name={} hr={}",
                         GetShortDescriptionWithClassName().AsStdString(),
                         member_name, hr);
        }
        if(TJS_FAILED(hr))
            TJSThrowFrom_tjs_error(
                hr, TJS_GET_VM_REG(DataArea, code[2]).GetString());
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::GetProperty(tTJSVariant *ra,
                                           const tjs_int32 *code) {
        // ra[code[1]] = * ra[code[2]]
        if(TJS_GET_VM_REG_ADDR(ra, code[2])->Type() == tvtVoid) {
            TJS_GET_VM_REG_ADDR(ra, code[1])->Clear();
            return;
        }
        tTJSVariantClosure clo =
            TJS_GET_VM_REG_ADDR(ra, code[2])->AsObjectClosureNoAddRef();
        tjs_error hr =
            clo.PropGet(0, nullptr, nullptr, TJS_GET_VM_REG_ADDR(ra, code[1]),
                        clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
        if(TJS_FAILED(hr))
            TJSThrowFrom_tjs_error(hr, nullptr);
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::SetProperty(tTJSVariant *ra,
                                           const tjs_int32 *code) {
        // * ra[code[1]] = ra[code[2]]
        tTJSVariantClosure clo =
            TJS_GET_VM_REG_ADDR(ra, code[1])->AsObjectClosureNoAddRef();
        tjs_error hr =
            clo.PropSet(0, nullptr, nullptr, TJS_GET_VM_REG_ADDR(ra, code[2]),
                        clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
        if(TJS_FAILED(hr))
            TJSThrowFrom_tjs_error(hr, nullptr);
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::GetPropertyIndirect(tTJSVariant *ra,
                                                   const tjs_int32 *code,
                                                   tjs_uint32 flags) {
        // ra[code[1]] = ra[code[2]][ra[code[3]]];

        tTJSVariant *ra_code2 = TJS_GET_VM_REG_ADDR(ra, code[2]);
        tTJSVariantType type = ra_code2->Type();
        if(type == tvtVoid) {
            TJS_GET_VM_REG_ADDR(ra, code[1])->Clear();
            return;
        }
        if(type == tvtString) {
            GetStringProperty(TJS_GET_VM_REG_ADDR(ra, code[1]), ra_code2,
                              TJS_GET_VM_REG(ra, code[3]));
            return;
        }
        if(type == tvtOctet) {
            GetOctetProperty(TJS_GET_VM_REG_ADDR(ra, code[1]), ra_code2,
                             TJS_GET_VM_REG(ra, code[3]));
            return;
        }

        tTJSVariant *ra_code3 = TJS_GET_VM_REG_ADDR(ra, code[3]);
        if(ra_code3->Type() == tvtVoid) {
            TJS_GET_VM_REG_ADDR(ra, code[1])->Clear();
            return;
        }

        tjs_error hr;
        tTJSVariantClosure clo = ra_code2->AsObjectClosureNoAddRef();
        if(ra_code3->Type() != tvtInteger) {
            tTJSVariantString *str = ra_code3->AsString();

            try {
                // TODO: verify here needs hint holding
                tTJSVariant *dest = TJS_GET_VM_REG_ADDR(ra, code[1]);
                hr = clo.PropGet(
                    flags, *str, nullptr, dest,
                    clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
                if(TJS_FAILED(hr))
                    TJSThrowFrom_tjs_error(hr, *str);
            } catch(...) {
                if(str)
                    str->Release();
                throw;
            }
            if(str)
                str->Release();
        } else {
            hr = clo.PropGetByNum(flags, (tjs_int)ra_code3->AsInteger(),
                                  TJS_GET_VM_REG_ADDR(ra, code[1]),
                                  clo.ObjThis ? clo.ObjThis
                                              : ra[-1].AsObjectNoAddRef());
            if(TJS_FAILED(hr))
                ThrowFrom_tjs_error_num(hr, (tjs_int)ra_code3->AsInteger());
        }
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::SetPropertyIndirect(tTJSVariant *ra,
                                                   const tjs_int32 *code,
                                                   tjs_uint32 flags) {
        // ra[code[1]][ra[code[2]]] = ra[code[3]]]

        tTJSVariant *ra_code1 = TJS_GET_VM_REG_ADDR(ra, code[1]);
        tTJSVariantType type = ra_code1->Type();
        if(type == tvtString) {
            SetStringProperty(TJS_GET_VM_REG_ADDR(ra, code[3]),
                              TJS_GET_VM_REG_ADDR(ra, code[1]),
                              TJS_GET_VM_REG(ra, code[2]));
            return;
        }
        if(type == tvtOctet) {
            SetOctetProperty(TJS_GET_VM_REG_ADDR(ra, code[3]),
                             TJS_GET_VM_REG_ADDR(ra, code[1]),
                             TJS_GET_VM_REG(ra, code[2]));
            return;
        }

        tTJSVariantClosure clo = ra_code1->AsObjectClosure();
        tTJSVariant *ra_code2 = TJS_GET_VM_REG_ADDR(ra, code[2]);
        const bool trace_indirect =
            TJSSaveTraceEnabled() && TJSSceneTraceEnabled();
        const auto trace_indirect_result = [&](const std::string &key,
                                                tjs_error hr) {
            if(!trace_indirect)
                return;
            static const std::unordered_set<std::string> interesting_keys = {
                "start", "end", "miu", "azu", "rio", "eri", "nic",
                "etc", "sd", "pageGroupBinds", "pageGroups",
                "pageGroupNames"};
            static int logged = 0;
            if(interesting_keys.find(key) == interesting_keys.end() &&
               logged >= 30000)
                return;
            ++logged;
            spdlog::info(
                "TJSSaveTrace set indirect desc=\"{}\" key={} hr={} "
                "target={} object={} objthis={} flags={} value={}",
                "<static>", key, hr,
                static_cast<const void *>(ra_code1->AsObjectNoAddRef()),
                static_cast<const void *>(clo.Object),
                static_cast<const void *>(clo.ObjThis), flags,
                TJS_SUCCEEDED(hr)
                    ? TJSSaveTraceVariantString(
                          *TJS_GET_VM_REG_ADDR(ra, code[3]))
                    : std::string("<failed>"));
        };
        if(ra_code2->Type() != tvtInteger) {
            tTJSVariantString *str;
            try {
                str = ra_code2->AsString();
            } catch(...) {
                clo.Release();
                throw;
            }

            try {
                tjs_error hr = clo.PropSetByVS(
                    flags, str, TJS_GET_VM_REG_ADDR(ra, code[3]),
                    clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
                if(hr == TJS_E_NOTIMPL)
                    hr = clo.PropSet(
                        flags, *str, nullptr, TJS_GET_VM_REG_ADDR(ra, code[3]),
                        clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
                trace_indirect_result(
                    str ? ttstr(*str).AsStdString() : std::string("<null>"),
                    hr);
                if(TJS_FAILED(hr))
                    TJSThrowFrom_tjs_error(hr, *str);
            } catch(...) {
                if(str)
                    str->Release();
                clo.Release();
                throw;
            }
            if(str)
                str->Release();
            clo.Release();
        } else {

            try {
                tjs_error hr = clo.PropSetByNum(
                    flags, (tjs_int)ra_code2->AsInteger(),
                    TJS_GET_VM_REG_ADDR(ra, code[3]),
                    clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
                trace_indirect_result(
                    fmt::format("{}", (tjs_int)ra_code2->AsInteger()), hr);
                if(TJS_FAILED(hr))
                    ThrowFrom_tjs_error_num(hr, (tjs_int)ra_code2->AsInteger());
            } catch(...) {
                clo.Release();
                throw;
            }
            clo.Release();
        }
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::OperatePropertyDirect(tTJSVariant *ra,
                                                     const tjs_int32 *code,
                                                     tjs_uint32 ope) const {
        // ra[code[1]] = ope(ra[code[2]][DataArea[ra[code[3]]]],
        // /*param=*/ra[code[4]]);

        tTJSVariantClosure clo = TJS_GET_VM_REG(ra, code[2]).AsObjectClosure();
        tjs_error hr;
        try {
            tTJSVariant *name = TJS_GET_VM_REG_ADDR(DataArea, code[3]);
            hr = clo.Operation(
                ope, name->GetString(), name->GetHint(),
                code[1] ? TJS_GET_VM_REG_ADDR(ra, code[1]) : nullptr,
                TJS_GET_VM_REG_ADDR(ra, code[4]),
                clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
        } catch(...) {
            clo.Release();
            throw;
        }
        clo.Release();
        if(TJS_FAILED(hr))
            TJSThrowFrom_tjs_error(
                hr, TJS_GET_VM_REG(DataArea, code[3]).GetString());
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::OperatePropertyIndirect(tTJSVariant *ra,
                                                       const tjs_int32 *code,
                                                       tjs_uint32 ope) {
        // ra[code[1]] = ope(ra[code[2]][ra[code[3]]],
        // /*param=*/ra[code[4]]);

        tTJSVariantClosure clo = TJS_GET_VM_REG(ra, code[2]).AsObjectClosure();
        tTJSVariant *ra_code3 = TJS_GET_VM_REG_ADDR(ra, code[3]);
        if(ra_code3->Type() != tvtInteger) {
            tTJSVariantString *str;
            try {
                str = ra_code3->AsString();
            } catch(...) {
                clo.Release();
                throw;
            }
            try {
                tjs_error hr = clo.Operation(
                    ope, *str, nullptr,
                    code[1] ? TJS_GET_VM_REG_ADDR(ra, code[1]) : nullptr,
                    TJS_GET_VM_REG_ADDR(ra, code[4]),
                    clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
                if(TJS_FAILED(hr))
                    TJSThrowFrom_tjs_error(hr, *str);
            } catch(...) {
                if(str)
                    str->Release();
                clo.Release();
                throw;
            }
            if(str)
                str->Release();
            clo.Release();
        } else {
            tjs_error hr;
            try {
                hr = clo.OperationByNum(
                    ope, (tjs_int)ra_code3->AsInteger(),
                    code[1] ? TJS_GET_VM_REG_ADDR(ra, code[1]) : nullptr,
                    TJS_GET_VM_REG_ADDR(ra, code[4]),
                    clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
                if(TJS_FAILED(hr))
                    ThrowFrom_tjs_error_num(
                        hr, (tjs_int)TJS_GET_VM_REG(ra, code[3]).AsInteger());
            } catch(...) {
                clo.Release();
                throw;
            }
            clo.Release();
        }
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::OperateProperty(tTJSVariant *ra,
                                               const tjs_int32 *code,
                                               tjs_uint32 ope) {
        // ra[code[1]] = ope(ra[code[2]], /*param=*/ra[code[3]]);
        tTJSVariantClosure clo = TJS_GET_VM_REG(ra, code[2]).AsObjectClosure();
        tjs_error hr;
        try {
            hr = clo.Operation(
                ope, nullptr, nullptr,
                code[1] ? TJS_GET_VM_REG_ADDR(ra, code[1]) : nullptr,
                TJS_GET_VM_REG_ADDR(ra, code[3]),
                clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
        } catch(...) {
            clo.Release();
            throw;
        }
        clo.Release();
        if(TJS_FAILED(hr))
            TJSThrowFrom_tjs_error(hr, nullptr);
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::OperatePropertyDirect0(tTJSVariant *ra,
                                                      const tjs_int32 *code,
                                                      tjs_uint32 ope) const {
        // ra[code[1]] = ope(ra[code[2]][DataArea[ra[code[3]]]]);

        tTJSVariantClosure clo = TJS_GET_VM_REG(ra, code[2]).AsObjectClosure();
        tjs_error hr;
        try {
            tTJSVariant *name = TJS_GET_VM_REG_ADDR(DataArea, code[3]);
            hr = clo.Operation(ope, name->GetString(), name->GetHint(),
                               code[1] ? TJS_GET_VM_REG_ADDR(ra, code[1])
                                       : nullptr,
                               nullptr, ra[-1].AsObjectNoAddRef());
        } catch(...) {
            clo.Release();
            throw;
        }
        clo.Release();
        if(TJS_FAILED(hr))
            TJSThrowFrom_tjs_error(
                hr, TJS_GET_VM_REG(DataArea, code[3]).GetString());
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::OperatePropertyIndirect0(tTJSVariant *ra,
                                                        const tjs_int32 *code,
                                                        tjs_uint32 ope) {
        // ra[code[1]] = ope(ra[code[2]][ra[code[3]]]);

        tTJSVariantClosure clo = TJS_GET_VM_REG(ra, code[2]).AsObjectClosure();
        tTJSVariant *ra_code3 = TJS_GET_VM_REG_ADDR(ra, code[3]);
        if(ra_code3->Type() != tvtInteger) {
            tTJSVariantString *str;
            try {
                str = ra_code3->AsString();
            } catch(...) {
                clo.Release();
                throw;
            }
            tjs_error hr;
            try {
                hr = clo.Operation(
                    ope, *str, nullptr,
                    code[1] ? TJS_GET_VM_REG_ADDR(ra, code[1]) : nullptr,
                    nullptr,
                    clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
                if(TJS_FAILED(hr))
                    TJSThrowFrom_tjs_error(hr, *str);
            } catch(...) {
                if(str)
                    str->Release();
                clo.Release();
                throw;
            }
            if(str)
                str->Release();
            clo.Release();
        } else {
            tjs_error hr;
            try {
                hr = clo.OperationByNum(
                    ope, (tjs_int)TJS_GET_VM_REG(ra, code[3]).AsInteger(),
                    code[1] ? TJS_GET_VM_REG_ADDR(ra, code[1]) : nullptr,
                    nullptr,
                    clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
                if(TJS_FAILED(hr))
                    ThrowFrom_tjs_error_num(
                        hr, (tjs_int)TJS_GET_VM_REG(ra, code[3]).AsInteger());
            } catch(...) {
                clo.Release();
                throw;
            }
            clo.Release();
        }
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::OperateProperty0(tTJSVariant *ra,
                                                const tjs_int32 *code,
                                                tjs_uint32 ope) {
        // ra[code[1]] = ope(ra[code[2]]);
        tTJSVariantClosure clo = TJS_GET_VM_REG(ra, code[2]).AsObjectClosure();
        tjs_error hr;
        try {
            hr = clo.Operation(
                ope, nullptr, nullptr,
                code[1] ? TJS_GET_VM_REG_ADDR(ra, code[1]) : nullptr, nullptr,
                clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
        } catch(...) {
            clo.Release();
            throw;
        }
        clo.Release();
        if(TJS_FAILED(hr))
            TJSThrowFrom_tjs_error(hr, nullptr);
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::DeleteMemberDirect(tTJSVariant *ra,
                                                  const tjs_int32 *code) {
        // ra[code[1]] = delete ra[code[2]][DataArea[ra[code[3]]]];

        tTJSVariantClosure clo = TJS_GET_VM_REG(ra, code[2]).AsObjectClosure();
        tjs_error hr;
        try {
            tTJSVariant *name = TJS_GET_VM_REG_ADDR(DataArea, code[3]);
            hr = clo.DeleteMember(0, name->GetString(), name->GetHint(),
                                  ra[-1].AsObjectNoAddRef());
        } catch(...) {
            clo.Release();
            throw;
        }
        clo.Release();
        if(code[1]) {
            if(TJS_FAILED(hr))
                TJS_GET_VM_REG(ra, code[1]) = false;
            else
                TJS_GET_VM_REG(ra, code[1]) = true;
        }
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::DeleteMemberIndirect(tTJSVariant *ra,
                                                    const tjs_int32 *code) {
        // ra[code[1]] = delete ra[code[2]][ra[code[3]]];

        tTJSVariantClosure clo = TJS_GET_VM_REG(ra, code[2]).AsObjectClosure();
        tTJSVariantString *str;
        try {
            str = TJS_GET_VM_REG(ra, code[3]).AsString();
        } catch(...) {
            clo.Release();
            throw;
        }

        try {
            tjs_error hr = clo.DeleteMember(
                0, *str, nullptr,
                clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
            if(code[1]) {
                if(TJS_FAILED(hr))
                    TJS_GET_VM_REG(ra, code[1]) = false;
                else
                    TJS_GET_VM_REG(ra, code[1]) = true;
            }
        } catch(...) {
            if(str)
                str->Release();
            clo.Release();
            throw;
        }
        if(str)
            str->Release();
        clo.Release();
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::TypeOfMemberDirect(tTJSVariant *ra,
                                                  const tjs_int32 *code,
                                                  tjs_uint32 flags) const {
        // ra[code[1]] = typeof ra[code[2]][DataArea[ra[code[3]]]];
        tTJSVariantType type = TJS_GET_VM_REG(ra, code[2]).Type();
        if(type == tvtVoid) {
            TJSTraceThumbnailOwner("void-base", nullptr, nullptr,
                                   TJS_E_MEMBERNOTFOUND, nullptr);
            TJS_GET_VM_REG(ra, code[1]) = TJS_W("undefined");
            return;
        }
        if(type == tvtString) {
            GetStringProperty(TJS_GET_VM_REG_ADDR(ra, code[1]),
                              TJS_GET_VM_REG_ADDR(ra, code[2]),
                              TJS_GET_VM_REG(DataArea, code[3]));
            TypeOf(TJS_GET_VM_REG(ra, code[1]));
            return;
        }
        if(type == tvtOctet) {
            GetOctetProperty(TJS_GET_VM_REG_ADDR(ra, code[1]),
                             TJS_GET_VM_REG_ADDR(ra, code[2]),
                             TJS_GET_VM_REG(DataArea, code[3]));
            TypeOf(TJS_GET_VM_REG(ra, code[1]));
            return;
        }

        tjs_error hr;
        tTJSVariantClosure clo = TJS_GET_VM_REG(ra, code[2]).AsObjectClosure();
        try {
            tTJSVariant *name = TJS_GET_VM_REG_ADDR(DataArea, code[3]);
            const bool thumbnailLookup =
                name && name->GetString() &&
                !TJS_strcmp(name->GetString(), TJS_W("makeThumbnailEffect"));
            if(thumbnailLookup)
                TJSTraceThumbnailOwner("before-direct", clo.Object,
                                       clo.ObjThis, TJS_S_OK, nullptr);
            hr = clo.PropGet(flags, name->GetString(), name->GetHint(),
                             TJS_GET_VM_REG_ADDR(ra, code[1]),
                             clo.ObjThis ? clo.ObjThis
                                         : ra[-1].AsObjectNoAddRef());
            if(thumbnailLookup)
                TJSTraceThumbnailOwner("after-direct", clo.Object,
                                       clo.ObjThis, hr,
                                       TJS_GET_VM_REG_ADDR(ra, code[1]));
        } catch(...) {
            clo.Release();
            throw;
        }
        clo.Release();
        if(hr == TJS_S_OK) {
            TypeOf(TJS_GET_VM_REG(ra, code[1]));
        } else if(hr == TJS_E_MEMBERNOTFOUND) {
            static tTJSString undefined_name(
                TJSMapGlobalStringMap(TJS_W("undefined")));
            TJS_GET_VM_REG(ra, code[1]) = undefined_name;
        } else if(TJS_FAILED(hr))
            TJSThrowFrom_tjs_error(
                hr, TJS_GET_VM_REG(DataArea, code[3]).GetString());
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::TypeOfMemberIndirect(tTJSVariant *ra,
                                                    const tjs_int32 *code,
                                                    tjs_uint32 flags) {
        // ra[code[1]] = typeof ra[code[2]][ra[code[3]]];

        tTJSVariantType type = TJS_GET_VM_REG(ra, code[2]).Type();
        if(type == tvtVoid ||
           TJS_GET_VM_REG(ra, code[3]).Type() == tvtVoid) {
            TJS_GET_VM_REG(ra, code[1]) = TJS_W("undefined");
            return;
        }
        if(type == tvtString) {
            GetStringProperty(TJS_GET_VM_REG_ADDR(ra, code[1]),
                              TJS_GET_VM_REG_ADDR(ra, code[2]),
                              TJS_GET_VM_REG(ra, code[3]));
            TypeOf(ra[code[1]]);
            return;
        }
        if(type == tvtOctet) {
            GetOctetProperty(TJS_GET_VM_REG_ADDR(ra, code[1]),
                             TJS_GET_VM_REG_ADDR(ra, code[2]),
                             TJS_GET_VM_REG(ra, code[3]));
            TypeOf(TJS_GET_VM_REG(ra, code[1]));
            return;
        }

        tjs_error hr;
        tTJSVariantClosure clo = TJS_GET_VM_REG(ra, code[2]).AsObjectClosure();
        if(TJS_GET_VM_REG(ra, code[3]).Type() != tvtInteger) {
            tTJSVariantString *str;
            try {
                str = TJS_GET_VM_REG(ra, code[3]).AsString();
            } catch(...) {
                clo.Release();
                throw;
            }

            try {
                const bool thumbnailLookup =
                    str && !TJS_strcmp(*str, TJS_W("makeThumbnailEffect"));
                if(thumbnailLookup)
                    TJSTraceThumbnailOwner("before-indirect", clo.Object,
                                           clo.ObjThis, TJS_S_OK, nullptr);
                // TODO: verify here needs hint holding
                hr = clo.PropGet(
                    flags, *str, nullptr, TJS_GET_VM_REG_ADDR(ra, code[1]),
                    clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
                if(thumbnailLookup)
                    TJSTraceThumbnailOwner("after-indirect", clo.Object,
                                           clo.ObjThis, hr,
                                           TJS_GET_VM_REG_ADDR(ra, code[1]));
                if(hr == TJS_S_OK) {
                    TypeOf(TJS_GET_VM_REG(ra, code[1]));
                } else // if(hr == TJS_E_MEMBERNOTFOUND)
                {
                    TJS_GET_VM_REG(ra, code[1]) = TJS_W("undefined");
                }
                //			else if(TJS_FAILED(hr))
                // TJSThrowFrom_tjs_error(hr, *str);
            } catch(...) {
                if(str)
                    str->Release();
                clo.Release();
                throw;
            }
            if(str)
                str->Release();
            clo.Release();
        } else {
            try {
                hr = clo.PropGetByNum(
                    flags, (tjs_int)TJS_GET_VM_REG(ra, code[3]).AsInteger(),
                    TJS_GET_VM_REG_ADDR(ra, code[1]),
                    clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
                if(hr == TJS_S_OK) {
                    TypeOf(TJS_GET_VM_REG(ra, code[1]));
                } else if(hr == TJS_E_MEMBERNOTFOUND) {
                    TJS_GET_VM_REG(ra, code[1]) = TJS_W("undefined");
                } else if(TJS_FAILED(hr))
                    ThrowFrom_tjs_error_num(
                        hr, (tjs_int)TJS_GET_VM_REG(ra, code[3]).AsInteger());
            } catch(...) {
                clo.Release();
                throw;
            }
            clo.Release();
        }
    }
//---------------------------------------------------------------------------
// Macros for preparing function argument pointer array.
// code[0] is an argument count;
// -1 for omitting ('...') argument to passing unmodified args from
// the caller. -2 for expanding array to argument
#define TJS_PASS_ARGS_PREPARED_ARRAY_COUNT 20

#define TJS_BEGIN_FUNC_CALL_ARGS(_code)                                        \
    tTJSVariant **pass_args;                                                   \
    tTJSVariant *pass_args_p[TJS_PASS_ARGS_PREPARED_ARRAY_COUNT];              \
    tTJSVariant *pass_args_v = nullptr;                                        \
    tjs_int code_size;                                                         \
    bool alloc_args = false;                                                   \
    try {                                                                      \
        tjs_int pass_args_count = (_code)[0];                                  \
        if(pass_args_count == -1) {                                            \
            /* omitting args; pass intact aguments from the caller             \
             */                                                                \
            pass_args = args;                                                  \
            pass_args_count = numargs;                                         \
            code_size = 1;                                                     \
        } else if(pass_args_count == -2) {                                     \
            tjs_int args_v_count = 0;                                          \
            /* count total argument count */                                   \
            pass_args_count = 0;                                               \
            tjs_int arg_written_count = (_code)[1];                            \
            code_size = arg_written_count * 2 + 2;                             \
            for(tjs_int i = 0; i < arg_written_count; i++) {                   \
                switch((_code)[i * 2 + 2]) {                                   \
                    case fatNormal:                                            \
                        pass_args_count++;                                     \
                        break;                                                 \
                    case fatExpand:                                            \
                        args_v_count += TJSGetArrayElementCount(               \
                            TJS_GET_VM_REG(ra, (_code)[i * 2 + 1 + 2])         \
                                .AsObjectNoAddRef());                          \
                        break;                                                 \
                    case fatUnnamedExpand:                                     \
                        pass_args_count +=                                     \
                            (numargs > FuncDeclUnnamedArgArrayBase)            \
                            ? (numargs - FuncDeclUnnamedArgArrayBase)          \
                            : 0;                                               \
                        break;                                                 \
                }                                                              \
            }                                                                  \
            pass_args_count += args_v_count;                                   \
            /* allocate temporary variant array for Array object */            \
            pass_args_v = new tTJSVariant[args_v_count];                       \
            /* allocate pointer array */                                       \
            if(pass_args_count < TJS_PASS_ARGS_PREPARED_ARRAY_COUNT)           \
                pass_args = pass_args_p;                                       \
            else                                                               \
                pass_args = new tTJSVariant *[pass_args_count],                \
                alloc_args = true;                                             \
            /* create pointer array to pass to callee function */              \
            args_v_count = 0;                                                  \
            pass_args_count = 0;                                               \
            for(tjs_int i = 0; i < arg_written_count; i++) {                   \
                switch((_code)[i * 2 + 2]) {                                   \
                    case fatNormal:                                            \
                        pass_args[pass_args_count++] =                         \
                            TJS_GET_VM_REG_ADDR(ra, (_code)[i * 2 + 1 + 2]);   \
                        break;                                                 \
                    case fatExpand: {                                          \
                        tjs_int count = TJSCopyArrayElementTo(                 \
                            TJS_GET_VM_REG(ra, (_code)[i * 2 + 1 + 2])         \
                                .AsObjectNoAddRef(),                           \
                            pass_args_v + args_v_count, 0, -1);                \
                        for(tjs_int j = 0; j < count; j++)                     \
                            pass_args[pass_args_count++] =                     \
                                pass_args_v + j + args_v_count;                \
                                                                               \
                        args_v_count += count;                                 \
                                                                               \
                        break;                                                 \
                    }                                                          \
                    case fatUnnamedExpand: {                                   \
                        tjs_int count =                                        \
                            (numargs > FuncDeclUnnamedArgArrayBase)            \
                            ? (numargs - FuncDeclUnnamedArgArrayBase)          \
                            : 0;                                               \
                        for(tjs_int j = 0; j < count; j++)                     \
                            pass_args[pass_args_count++] =                     \
                                args[FuncDeclUnnamedArgArrayBase + j];         \
                        break;                                                 \
                    }                                                          \
                }                                                              \
            }                                                                  \
        } else if(pass_args_count <= TJS_PASS_ARGS_PREPARED_ARRAY_COUNT) {     \
            code_size = pass_args_count + 1;                                   \
            pass_args = pass_args_p;                                           \
            for(tjs_int i = 0; i < pass_args_count; i++)                       \
                pass_args[i] = TJS_GET_VM_REG_ADDR(ra, (_code)[1 + i]);        \
        } else {                                                               \
            code_size = pass_args_count + 1;                                   \
            pass_args = new tTJSVariant *[pass_args_count];                    \
            alloc_args = true;                                                 \
            for(tjs_int i = 0; i < pass_args_count; i++)                       \
                pass_args[i] = TJS_GET_VM_REG_ADDR(ra, (_code)[1 + i]);        \
        }

#define TJS_END_FUNC_CALL_ARGS                                                 \
    }                                                                          \
    catch(...) {                                                               \
        if(alloc_args)                                                         \
            delete[] pass_args;                                                \
        if(pass_args_v)                                                        \
            delete[] pass_args_v;                                              \
        throw;                                                                 \
    }                                                                          \
    if(alloc_args)                                                             \
        delete[] pass_args;                                                    \
    if(pass_args_v)                                                            \
        delete[] pass_args_v;

    //---------------------------------------------------------------------------
    tjs_int tTJSInterCodeContext::CallFunction(tTJSVariant *ra,
                                               const tjs_int32 *code,
                                               tTJSVariant **args,
                                               tjs_int numargs) {
        // function calling / create new object
        tjs_error hr;

        TJS_BEGIN_FUNC_CALL_ARGS(code + 3)

        tTJSVariantClosure clo = TJS_GET_VM_REG(ra, code[2]).AsObjectClosure();
        try {
            if(code[0] == VM_CALL) {
                hr = clo.FuncCall(
                    0, nullptr, nullptr,
                    code[1] ? TJS_GET_VM_REG_ADDR(ra, code[1]) : nullptr,
                    pass_args_count, pass_args,
                    clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
            } else {
                iTJSDispatch2 *dsp;
                hr = clo.CreateNew(
                    0, nullptr, nullptr, &dsp, pass_args_count, pass_args,
                    clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
                if(TJS_SUCCEEDED(hr)) {
                    if(dsp) {
                        if(code[1])
                            TJS_GET_VM_REG(ra, code[1]) = tTJSVariant(dsp, dsp);
                        dsp->Release();
                    }
                }
            }
        } catch(...) {
            clo.Release();
            throw;
        }
        clo.Release();
        // TODO: nullptr Check
        TJSTraceSaveCallOp(this, code[0] == VM_CALL ? "call" : "new",
                           TJS_GET_VM_REG(ra, code[2]), pass_args,
                           pass_args_count, hr);

        TJS_END_FUNC_CALL_ARGS

        if(TJS_FAILED(hr))
            TJSThrowFrom_tjs_error(hr, TJS_W(""));

        return code_size + 3;
    }

#undef _code

    //---------------------------------------------------------------------------
    tjs_int tTJSInterCodeContext::CallFunctionDirect(tTJSVariant *ra,
                                                     const tjs_int32 *code,
                                                     tTJSVariant **args,
                                                     tjs_int numargs) {
        tjs_error hr;

        TJS_BEGIN_FUNC_CALL_ARGS(code + 4)

        tTJSVariantType type = TJS_GET_VM_REG(ra, code[2]).Type();
        iTJSDispatch2 *trace_call_this = nullptr;
        tTJSVariant *name = TJS_GET_VM_REG_ADDR(DataArea, code[3]);
        if(type == tvtVoid) {
            if(code[1])
                TJS_GET_VM_REG_ADDR(ra, code[1])->Clear();
            hr = TJS_S_OK;
        } else if(type == tvtString) {
            ProcessStringFunction(
                name->GetString(), TJS_GET_VM_REG(ra, code[2]), pass_args,
                pass_args_count,
                code[1] ? TJS_GET_VM_REG_ADDR(ra, code[1]) : nullptr);
            hr = TJS_S_OK;
        } else if(type == tvtOctet) {
            ProcessOctetFunction(name->GetString(),
                                 TJS_GET_VM_REG(ra, code[2]).AsOctetNoAddRef(),
                                 pass_args, pass_args_count,
                                 code[1] ? TJS_GET_VM_REG_ADDR(ra, code[1])
                                         : nullptr);
            hr = TJS_S_OK;
        } else {
            tTJSVariantClosure clo =
                TJS_GET_VM_REG(ra, code[2]).AsObjectClosure();
            trace_call_this =
                clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef();
            try {
                hr = clo.FuncCall(
                    0, name->GetString(), name->GetHint(),
                    code[1] ? TJS_GET_VM_REG_ADDR(ra, code[1]) : nullptr,
                    pass_args_count, pass_args,
                    clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
            } catch(...) {
                clo.Release();
                throw;
            }
            clo.Release();
        }
        TJSTraceSaveMemberCall(this, "direct", name->AsStringNoAddRef(),
                               pass_args, pass_args_count, hr,
                               trace_call_this);

        TJS_END_FUNC_CALL_ARGS

        if(TJS_FAILED(hr))
            TJSThrowFrom_tjs_error(
                hr, ttstr(TJS_GET_VM_REG(DataArea, code[3])).c_str());

        return code_size + 4;
    }

    //---------------------------------------------------------------------------
    tjs_int tTJSInterCodeContext::CallFunctionIndirect(tTJSVariant *ra,
                                                       const tjs_int32 *code,
                                                       tTJSVariant **args,
                                                       tjs_int numargs) {
        tjs_error hr;

        ttstr name = TJS_GET_VM_REG(ra, code[3]);

        TJS_BEGIN_FUNC_CALL_ARGS(code + 4)

        tTJSVariantType type = TJS_GET_VM_REG(ra, code[2]).Type();
        iTJSDispatch2 *trace_call_this = nullptr;
        if(type == tvtString) {
            ProcessStringFunction(name.c_str(), TJS_GET_VM_REG(ra, code[2]),
                                  pass_args, pass_args_count,
                                  code[1] ? TJS_GET_VM_REG_ADDR(ra, code[1])
                                          : nullptr);
            hr = TJS_S_OK;
        } else if(type == tvtOctet) {
            ProcessOctetFunction(
                name.c_str(), TJS_GET_VM_REG(ra, code[2]).AsOctetNoAddRef(),
                pass_args, pass_args_count,
                code[1] ? TJS_GET_VM_REG_ADDR(ra, code[1]) : nullptr);
            hr = TJS_S_OK;
        } else {
            tTJSVariantClosure clo =
                TJS_GET_VM_REG(ra, code[2]).AsObjectClosure();
            trace_call_this =
                clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef();
            try {
                hr = clo.FuncCall(
                    0, name.c_str(), name.GetHint(),
                    code[1] ? TJS_GET_VM_REG_ADDR(ra, code[1]) : nullptr,
                    pass_args_count, pass_args,
                    clo.ObjThis ? clo.ObjThis : ra[-1].AsObjectNoAddRef());
            } catch(...) {
                clo.Release();
                throw;
            }
            clo.Release();
        }
        TJSTraceSaveMemberCall(this, "indirect", name, pass_args,
                               pass_args_count, hr, trace_call_this);

        TJS_END_FUNC_CALL_ARGS

        if(TJS_FAILED(hr))
            TJSThrowFrom_tjs_error(hr, name.c_str());

        return code_size + 4;
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::AddClassInstanceInfo(tTJSVariant *ra,
                                                    const tjs_int32 *code) {
        iTJSDispatch2 *dsp;
        dsp = TJS_GET_VM_REG(ra, code[1]).AsObjectNoAddRef();
        if(dsp) {
            dsp->ClassInstanceInfo(TJS_CII_ADD, 0,
                                   TJS_GET_VM_REG_ADDR(ra, code[2]));
        } else {
            // ?? must be an error
        }
    }

    //---------------------------------------------------------------------------
    static const tjs_char *StrFuncs[] = {
        TJS_W("charAt"),      TJS_W("indexOf"),   TJS_W("toUpperCase"),
        TJS_W("toLowerCase"), TJS_W("substring"), TJS_W("substr"),
        TJS_W("sprintf"),     TJS_W("replace"),   TJS_W("escape"),
        TJS_W("split"),       TJS_W("trim"),      TJS_W("reverse"),
        TJS_W("repeat")
    };

    enum tTJSStringMethodNameIndex {
        TJSStrMethod_charAt = 0,
        TJSStrMethod_indexOf,
        TJSStrMethod_toUpperCase,
        TJSStrMethod_toLowerCase,
        TJSStrMethod_substring,
        TJSStrMethod_substr,
        TJSStrMethod_sprintf,
        TJSStrMethod_replace,
        TJSStrMethod_escape,
        TJSStrMethod_split,
        TJSStrMethod_trim,
        TJSStrMethod_reverse,
        TJSStrMethod_repeat
    };

#define TJS_STRFUNC_MAX (sizeof(StrFuncs) / sizeof(StrFuncs[0]))
    static tjs_int32 StrFuncHash[TJS_STRFUNC_MAX];
    static bool TJSStrFuncInit = false;

    static void InitTJSStrFunc() {
        TJSStrFuncInit = true;
        for(tjs_int i = 0; i < TJS_STRFUNC_MAX; i++) {
            const tjs_char *p = StrFuncs[i];
            tjs_int32 hash = 0;
            while(*p)
                hash += *p, p++;
            StrFuncHash[i] = hash;
        }
    }

    void tTJSInterCodeContext::ProcessStringFunction(const tjs_char *member,
                                                     const ttstr &target,
                                                     tTJSVariant **args,
                                                     tjs_int numargs,
                                                     tTJSVariant *result) {
        if(!TJSStrFuncInit)
            InitTJSStrFunc();

        tjs_int32 hash;

        if(!member)
            TJSThrowFrom_tjs_error(TJS_E_MEMBERNOTFOUND, TJS_W(""));

        const tjs_char *m = member;
        hash = 0;
        while(*m)
            hash += *m, m++;

        const tjs_char *s = target.c_str(); // target string
        const tjs_int s_len = target.GetLen();

#define TJS_STR_METHOD_IS(_name)                                               \
    (hash == StrFuncHash[TJSStrMethod_##_name] &&                              \
     !TJS_strcmp(StrFuncs[TJSStrMethod_##_name], member))

        if(TJS_STR_METHOD_IS(charAt)) {
            if(numargs != 1)
                TJSThrowFrom_tjs_error(TJS_E_BADPARAMCOUNT);
            if(s_len == 0) {
                if(result)
                    *result = TJS_W("");
                return;
            }
            tjs_int i = (tjs_int)*args[0];
            if(i < 0 || i >= s_len) {
                if(result)
                    *result = TJS_W("");
                return;
            }
            tjs_char bt[2];
            bt[1] = 0;
            bt[0] = s[i];
            if(result)
                *result = bt;
            return;
        } else if(TJS_STR_METHOD_IS(indexOf)) {
            if(numargs != 1 && numargs != 2)
                TJSThrowFrom_tjs_error(TJS_E_BADPARAMCOUNT);
            tTJSVariantString *pstr = args[0]->AsString(); // sub string

            if(!s || !pstr) {
                if(result)
                    *result = (tjs_int)-1;
                if(pstr)
                    pstr->Release();
                return;
            }
            tjs_int start;
            if(numargs == 1) {
                start = 0;
            } else {
                try // integer convertion may raise an exception
                {
                    start = (tjs_int)*args[1];
                } catch(...) {
                    pstr->Release();
                    throw;
                }
            }
            if(start >= s_len) {
                if(result)
                    *result = (tjs_int)-1;
                if(pstr)
                    pstr->Release();
                return;
            }
            const tjs_char *p;
            p = TJS_strstr(s + start, (const tjs_char *)*pstr);
            if(!p) {
                if(result)
                    *result = (tjs_int)-1;
            } else {
                if(result)
                    *result = (tjs_int)(p - s);
            }
            if(pstr)
                pstr->Release();
            return;
        } else if(TJS_STR_METHOD_IS(toUpperCase)) {
            if(numargs != 0)
                TJSThrowFrom_tjs_error(TJS_E_BADPARAMCOUNT);
            if(result) {
                *result = s; // here s is copyed to *result ( not
                             // reference )
                const tjs_char *pstr = result->GetString(); // buffer in *result
                if(pstr) {
                    tjs_char *p =
                        (tjs_char *)pstr; // WARNING!! modification of const
                    while(*p) {
                        if(*p >= TJS_W('a') && *p <= TJS_W('z'))
                            *p += TJS_W('Z') - TJS_W('z');
                        p++;
                    }
                }
            }
            return;
        } else if(TJS_STR_METHOD_IS(toLowerCase)) {
            if(numargs != 0)
                TJSThrowFrom_tjs_error(TJS_E_BADPARAMCOUNT);
            if(result) {
                *result = s;
                const tjs_char *pstr = result->GetString();
                if(pstr) {
                    tjs_char *p =
                        (tjs_char *)pstr; // WARNING!! modification of const
                    while(*p) {
                        if(*p >= TJS_W('A') && *p <= TJS_W('Z'))
                            *p += TJS_W('z') - TJS_W('Z');
                        p++;
                    }
                }
            }
            return;
        } else if(TJS_STR_METHOD_IS(substring) || TJS_STR_METHOD_IS(substr)) {
            if(numargs != 1 && numargs != 2)
                TJSThrowFrom_tjs_error(TJS_E_BADPARAMCOUNT);
            tjs_int start = (tjs_int)*args[0];
            if(start < 0 || start >= s_len) {
                if(result)
                    *result = TJS_W("");
                return;
            }
            tjs_int count;
            if(numargs == 2) {
                count = (tjs_int)*args[1];
                if(count < 0) {
                    if(result)
                        *result = TJS_W("");
                    return;
                }
                if(start + count > s_len)
                    count = s_len - start;
                if(result)
                    *result = ttstr(s + start, count);
                return;
            } else {
                if(result)
                    *result = s + start;
            }
            return;
        } else if(TJS_STR_METHOD_IS(sprintf)) {
            if(result) {
                tTJSVariantString *res;
                res = TJSFormatString(s, numargs, args);
                *result = res;
                if(res)
                    res->Release();
            }
            return;
        } else if(TJS_STR_METHOD_IS(replace)) {
            if(numargs < 2)
                TJSThrowFrom_tjs_error(TJS_E_BADPARAMCOUNT);

            if(args[0]->Type() == tvtString) {
                ttstr pattern(*args[0]);
                ttstr replacement(*args[1]);
                ttstr work(target);
                ttstr res;
                const tjs_char *start = work.c_str();
                const tjs_char *p;
                tjs_int pat_len = pattern.GetLen();
                if(pat_len == 0) {
                    if(result) *result = target;
                    return;
                }
                while((p = TJS_strstr(start, pattern.c_str())) != nullptr) {
                    res += ttstr(start, (tjs_int)(p - start));
                    res += replacement;
                    start = p + pat_len;
                }
                res += start;
                if(result) *result = res;
            } else {
                tTJSVariantClosure clo = args[0]->AsObjectClosureNoAddRef();
                tTJSVariant str = target;
                tTJSVariant *params[] = { &str, args[1] };
                static tTJSString replace_name(TJS_W("replace"));
                clo.FuncCall(0, replace_name.c_str(), replace_name.GetHint(),
                             result, 2, params, nullptr);
            }

            return;
        } else if(TJS_STR_METHOD_IS(escape)) {
            if(result)
                *result = target.EscapeC();

            return;
        } else if(TJS_STR_METHOD_IS(split)) {
            // string.split(pattern, reserved, purgeempty) -->
            // Array.split(pattern, string, reserved, purgeempty)
            if(numargs < 1)
                TJSThrowFrom_tjs_error(TJS_E_BADPARAMCOUNT);

            iTJSDispatch2 *array = TJSCreateArrayObject();
            try {
                tTJSVariant str = target;
                tjs_int arg_count = 2;
                tTJSVariant *params[4] = { args[0], &str };
                if(numargs >= 2) {
                    arg_count++;
                    params[2] = args[1];
                }
                if(numargs >= 3) {
                    arg_count++;
                    params[3] = args[2];
                }
                static tTJSString split_name(TJS_W("split"));
                array->FuncCall(0, split_name.c_str(), split_name.GetHint(),
                                nullptr, arg_count, params, array);

                if(result)
                    *result = tTJSVariant(array, array);
            } catch(...) {
                array->Release();
                throw;
            }
            array->Release();

            return;
        } else if(TJS_STR_METHOD_IS(trim)) {
            if(numargs != 0)
                TJSThrowFrom_tjs_error(TJS_E_BADPARAMCOUNT);
            if(!result)
                return;

            tjs_int w_len = s_len;
            const tjs_char *src = s + s_len - 1;
            /*  s/\s+$//;  */
            while(w_len > 0 && *src > 0x00 && *src <= 0x20) {
                w_len--;
                src--;
            }
            src = s;
            /*  s/^\s+//;  */
            while(w_len > 0 && *src > 0x00 && *src <= 0x20) {
                w_len--;
                src++;
            }

            *result = tTJSString(src, w_len);
            return;
        } else if(TJS_STR_METHOD_IS(reverse)) {
            if(numargs != 0)
                TJSThrowFrom_tjs_error(TJS_E_BADPARAMCOUNT);
            if(!result)
                return;
            if(result) {
                *result = s;
                const tjs_char *pstr = result->GetString();
                if(pstr) {
                    tjs_int w_len = s_len;
                    tjs_char *dest =
                        (tjs_char *)pstr; // WARNING!! modification of const
                    const tjs_char *src = s + s_len - 1;

                    while(w_len--) {
                        *dest++ = *src--;
                    }
                }
            }
            return;
        } else if(TJS_STR_METHOD_IS(repeat)) {
            if(numargs != 1)
                TJSThrowFrom_tjs_error(TJS_E_BADPARAMCOUNT);
            if(!result)
                return;
            tjs_int count = (tjs_int)*args[0];

            if(count <= 0 || s_len <= 0) {
                *result = TJS_W("");
                return;
            }

            const int destLength = s_len * count;
            tTJSString new_str = tTJSString(tTJSStringBufferLength(destLength));
            tjs_char *dest = new_str.Independ();
            while(count--) {
                TJS_strcpy(dest, s);
                dest += s_len;
            }
            *result = new_str;

            return;
        }

#undef TJS_STR_METHOD_IS

        TJSThrowFrom_tjs_error(TJS_E_MEMBERNOTFOUND, member);
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::ProcessOctetFunction(
        const tjs_char *member, const tTJSVariantOctet *target,
        tTJSVariant **args, tjs_int numargs, tTJSVariant *result) {
        if(!member)
            TJSThrowFrom_tjs_error(TJS_E_MEMBERNOTFOUND, TJS_W(""));
        switch(member[0]) {
            case L'u':
                if(!TJS_strcmp(TJS_W("unpack"), member)) {
                    tjs_error err =
                        TJSOctetUnpack(target, args, numargs, result);
                    if(err != TJS_S_OK) {
                        TJSThrowFrom_tjs_error(err);
                    }
                    return;
                }
                break;
        }

        TJSThrowFrom_tjs_error(TJS_E_MEMBERNOTFOUND, member);
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::TypeOf(tTJSVariant &val) {
        // processes TJS2's typeof operator.
        static tTJSString void_name(TJSMapGlobalStringMap(TJS_W("void")));
        static tTJSString Object_name(TJSMapGlobalStringMap(TJS_W("Object")));
        static tTJSString String_name(TJSMapGlobalStringMap(TJS_W("String")));
        static tTJSString Integer_name(TJSMapGlobalStringMap(TJS_W("Integer")));
        static tTJSString Real_name(TJSMapGlobalStringMap(TJS_W("Real")));
        static tTJSString Octet_name(TJSMapGlobalStringMap(TJS_W("Octet")));

        switch(val.Type()) {
            case tvtVoid:
                val = void_name; // differs from TJS1
                break;

            case tvtObject:
                val = Object_name;
                break;

            case tvtString:
                val = String_name;
                break;

            case tvtInteger:
                val = Integer_name; // differs from TJS1
                break;

            case tvtReal:
                val = Real_name; // differs from TJS1
                break;

            case tvtOctet:
                val = Octet_name;
                break;
        }
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::Eval(tTJSVariant &val, iTJSDispatch2 *objthis,
                                    bool resneed) {
        if(objthis)
            objthis->AddRef();
        try {
            tTJSVariant res;
            ttstr str(val);
            if(!str.IsEmpty()) {
                if(resneed)
                    CachedTJSEngine->EvalExpression(str, &res, objthis);
                else
                    CachedTJSEngine->EvalExpression(str, nullptr, objthis);
            }
            if(resneed)
                val = res;
        } catch(...) {
            if(objthis)
                objthis->Release();
            throw;
        }
        if(objthis)
            objthis->Release();
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::CharacterCodeOf(tTJSVariant &val) {
        // puts val's character code on val
        tTJSVariantString *str = val.AsString();
        if(str) {
            const tjs_char *ch = (const tjs_char *)*str;
            val = tTVInteger(ch[0]);
            str->Release();
            return;
        }
        val = tTVInteger(0);
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::CharacterCodeFrom(tTJSVariant &val) {
        tjs_char ch[2];
        ch[0] = static_cast<tjs_char>(val.AsInteger());
        ch[1] = 0;
        val = ch;
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::InstanceOf(const tTJSVariant &name,
                                          tTJSVariant &targ) {
        // checks instance inheritance.
        tTJSVariantString *str = name.AsString();
        if(str) {
            tjs_error hr;
            try {
                hr = TJSDefaultIsInstanceOf(0, targ, (const tjs_char *)*str,
                                            nullptr);
            } catch(...) {
                str->Release();
                throw;
            }
            str->Release();
            if(TJS_FAILED(hr))
                TJSThrowFrom_tjs_error(hr);

            targ = (hr == TJS_S_TRUE);
            return;
        }
        targ = false;
    }

    //---------------------------------------------------------------------------
    void tTJSInterCodeContext::RegisterObjectMember(iTJSDispatch2 *dest) {
        // register this object member to 'dest' (destination object).
        // called when new object is to be created.
        // a class to receive member callback from class

        class tCallback : public tTJSDispatch {
        public:
            iTJSDispatch2 *Dest; // destination object
            tjs_error FuncCall(tjs_uint32 flag, const tjs_char *membername,
                               tjs_uint32 *hint, tTJSVariant *result,
                               tjs_int numparams, tTJSVariant **param,
                               iTJSDispatch2 *objthis) override {
                // *param[0] = name   *param[1] = flags   *param[2] =
                // value
                tjs_uint32 flags = (tjs_int)*param[1];
                if(const char *trace = std::getenv("AETHERKIRI_AFFINE_TRACE");
                   trace && *trace && param[0] &&
                   !TJS_strcmp(param[0]->GetString(), TJS_W("onPaint"))) {
                    spdlog::info(
                        "AffineEnum onPaint flags={} valueType={} valueObj={} valueThis={} dest={} isFn={} isClass={}",
                        flags, static_cast<int>(param[2]->Type()),
                        static_cast<const void *>(param[2]->AsObjectNoAddRef()),
                        static_cast<const void *>(param[2]->AsObjectThisNoAddRef()),
                        static_cast<const void *>(Dest),
                        param[2]->Type() == tvtObject &&
                            param[2]->AsObjectClosureNoAddRef().IsInstanceOf(
                                0, nullptr, nullptr, TJS_W("Function"), nullptr) == TJS_S_TRUE,
                        param[2]->Type() == tvtObject &&
                            param[2]->AsObjectClosureNoAddRef().IsInstanceOf(
                                0, nullptr, nullptr, TJS_W("Class"), nullptr) == TJS_S_TRUE);
                }
                if(!(flags & TJS_STATICMEMBER)) {
                    tTJSVariant val = *param[2];
                    if(val.Type() == tvtObject) {
                        // change object's objthis if the object's
                        // objthis is nullptr
                        //					if(val.AsObjectThisNoAddRef()
                        //== nullptr)
                        val.ChangeClosureObjThis(Dest);
                    }

                    if(Dest->PropSetByVS(TJS_MEMBERENSURE | TJS_IGNOREPROP |
                                             flags,
                                         param[0]->AsStringNoAddRef(), &val,
                                         Dest) == TJS_E_NOTIMPL)
                        Dest->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP | flags,
                                      param[0]->GetString(), nullptr, &val,
                                      Dest);
                }
                if(result)
                    *result = (tjs_int)(1); // returns true
                return TJS_S_OK;
            }
        };

        tCallback callback;
        callback.Dest = dest;

        // enumerate members
        tTJSVariantClosure clo(&callback, (iTJSDispatch2 *)nullptr);
        EnumMembers(TJS_IGNOREPROP, &clo, this);

        if(const char *trace = std::getenv("AETHERKIRI_AFFINE_TRACE");
           trace && *trace && Name && !TJS_strcmp(Name, TJS_W("EnvGraphicLayer")) &&
           dest) {
            tTJSVariant onpaint;
            const tjs_error er = dest->PropGet(
                TJS_IGNOREPROP, TJS_W("onPaint"), nullptr, &onpaint, dest);
            const tjs_error valid =
                dest->IsValid(0, TJS_W("onPaint"), nullptr, dest);
            std::string fn_class;
            if(auto *fn = onpaint.AsObjectNoAddRef()) {
                tTJSVariant fn_name;
                if(TJS_SUCCEEDED(fn->ClassInstanceInfo(TJS_CII_GET, 0,
                                                        &fn_name)))
                    fn_class = ttstr(fn_name).AsStdString();
            }
            spdlog::info(
                "AffineRegister class=EnvGraphicLayer context={} dest={} onPaint_er={} valid={} type={} obj={} fnClass={}",
                static_cast<const void *>(this), static_cast<const void *>(dest), er,
                valid, static_cast<int>(onpaint.Type()),
                static_cast<const void *>(onpaint.AsObjectNoAddRef()), fn_class);
        }
    }
//---------------------------------------------------------------------------
#define TJS_DO_SUPERCLASS_PROXY_BEGIN                                          \
    std::vector<tjs_int> &pointer = SuperClassGetter->SuperClassGetterPointer; \
    if(pointer.size() != 0) {                                                  \
        std::vector<tjs_int>::reverse_iterator i;                              \
        for(i = pointer.rbegin(); i != pointer.rend(); i++) {                  \
            tTJSVariant res;                                                   \
            SuperClassGetter->ExecuteAsFunction(nullptr, nullptr, 0, &res,     \
                                                *i);                           \
            tTJSVariantClosure clo = res.AsObjectClosureNoAddRef();

#define TJS_DO_SUPERCLASS_PROXY_END                                            \
    if(hr != TJS_E_MEMBERNOTFOUND)                                             \
        break;                                                                 \
    }                                                                          \
    }

    tjs_error
    tTJSInterCodeContext::FuncCall(tjs_uint32 flag, const tjs_char *membername,
                                   tjs_uint32 *hint, tTJSVariant *result,
                                   tjs_int numparams, tTJSVariant **param,
                                   iTJSDispatch2 *objthis) {
        if(!GetValidity())
            return TJS_E_INVALIDOBJECT;

        if(membername == nullptr) {
            switch(ContextType) {
                case ctTopLevel:
                    ExecuteAsFunction(
                        objthis ? objthis
                                : CachedTJSEngine->GetGlobalNoAddRef(),
                        nullptr, 0, result, 0);
                    break;

                case ctFunction:
                case ctExprFunction:
                case ctPropertyGetter:
                case ctPropertySetter:
                    ExecuteAsFunction(objthis, param, numparams, result, 0);
                    break;

                case ctClass: // on super class' initialization
                    ExecuteAsFunction(objthis, param, numparams, result, 0);
                    break;

                case ctProperty:
                    return TJS_E_INVALIDTYPE;

                case ctSuperClassGetter:
                    break;
            }

            return TJS_S_OK;
        }

        const bool affine_calc_trace = [] (const tjs_char *name) {
            const char *trace = std::getenv("AETHERKIRI_CALC_TRACE");
            return trace && *trace && *trace != '0' && name &&
                   !TJS_strcmp(name, TJS_W("calcImageMatrix"));
        }(membername);
        const bool affine_onpaint_trace = [] (const tjs_char *name) {
            const char *trace = std::getenv("AETHERKIRI_AFFINE_TRACE");
            return trace && *trace && name &&
                   !TJS_strcmp(name, TJS_W("onPaint"));
        }(membername);
        if(affine_calc_trace) {
            spdlog::info(
                "AffineCalcContext enter this={} name={} context={} super={} superPointers={} objthis={} hint={}",
                static_cast<const void *>(this),
                Name ? ttstr(Name).AsStdString() : std::string(),
                static_cast<int>(ContextType),
                static_cast<const void *>(SuperClassGetter),
                SuperClassGetter
                    ? SuperClassGetter->SuperClassGetterPointer.size()
                    : 0,
                static_cast<const void *>(objthis), hint ? *hint : 0);
        }
        if(affine_onpaint_trace) {
            tTJSVariant local_value;
            const tjs_error local_error =
                PropGet(TJS_IGNOREPROP, membername, hint, &local_value,
                        objthis);
            spdlog::info(
                "AffineContextCall this={} name={} context={} contextName={} localEr={} localType={} localObj={} super={} objthis={}",
                static_cast<const void *>(this), ttstr(membername).AsStdString(),
                static_cast<int>(ContextType),
                Name ? ttstr(Name).AsStdString() : std::string(),
                local_error, static_cast<int>(local_value.Type()),
                static_cast<const void *>(local_value.AsObjectNoAddRef()),
                static_cast<const void *>(SuperClassGetter),
                static_cast<const void *>(objthis));
        }

        tjs_error hr = inherited::FuncCall(flag, membername, hint, result,
                                           numparams, param, objthis);

        if(affine_calc_trace)
            spdlog::info("AffineCalcContext local-result this={} hr={} hint={}",
                         static_cast<const void *>(this), hr,
                         hint ? *hint : 0);

        if(membername != nullptr && hr == TJS_E_MEMBERNOTFOUND &&
           ContextType == ctClass && SuperClassGetter) {
            // look up super class
            TJS_DO_SUPERCLASS_PROXY_BEGIN
            if(affine_calc_trace)
                spdlog::info(
                    "AffineCalcContext proxy this={} getterIndex={} proxy={} proxyThis={}",
                    static_cast<const void *>(this), *i,
                    static_cast<const void *>(clo.Object),
                    static_cast<const void *>(clo.ObjThis));
            hr = clo.FuncCall(flag, membername, hint, result, numparams, param,
                              objthis);
            if(affine_calc_trace)
                spdlog::info("AffineCalcContext proxy-result this={} hr={} hint={}",
                             static_cast<const void *>(this), hr,
                             hint ? *hint : 0);
            TJS_DO_SUPERCLASS_PROXY_END
        }
        if(affine_onpaint_trace) {
            spdlog::info("AffineContextCallResult this={} hr={}",
                         static_cast<const void *>(this), hr);
        }
        return hr;
    }

    //---------------------------------------------------------------------------
    tjs_error tTJSInterCodeContext::PropGet(tjs_uint32 flag,
                                            const tjs_char *membername,
                                            tjs_uint32 *hint,
                                            tTJSVariant *result,
                                            iTJSDispatch2 *objthis) {
        if(!GetValidity())
            return TJS_E_INVALIDOBJECT;

        if(membername == nullptr) {
            if(ContextType == ctProperty) {
                // executed as a property getter
                if(PropGetter)
                    return PropGetter->FuncCall(0, nullptr, nullptr, result, 0,
                                                nullptr, objthis);
                else
                    return TJS_E_ACCESSDENYED;
            }
        }

        tjs_error hr =
            inherited::PropGet(flag, membername, hint, result, objthis);

        if(membername != nullptr && hr == TJS_E_MEMBERNOTFOUND &&
           ContextType == ctClass && SuperClassGetter) {
            // look up super class
            TJS_DO_SUPERCLASS_PROXY_BEGIN
            hr = clo.PropGet(flag, membername, hint, result, objthis);
            TJS_DO_SUPERCLASS_PROXY_END
        }
        return hr;
    }

    //---------------------------------------------------------------------------
    tjs_error tTJSInterCodeContext::PropSet(tjs_uint32 flag,
                                            const tjs_char *membername,
                                            tjs_uint32 *hint,
                                            const tTJSVariant *param,
                                            iTJSDispatch2 *objthis) {
        if(!GetValidity())
            return TJS_E_INVALIDOBJECT;

        if(membername == nullptr) {
            if(ContextType == ctProperty) {
                // executed as a property setter
                if(PropSetter)
                    return PropSetter->FuncCall(
                        0, nullptr, nullptr, nullptr, 1,
                        const_cast<tTJSVariant **>(&param), objthis);
                else
                    return TJS_E_ACCESSDENYED;

                // WARNING!! const tTJSVariant ** -> tTJSVariant**
                // force casting
            }
        }

        tjs_error hr;
        if(membername != nullptr && ContextType == ctClass &&
           SuperClassGetter) {
            tjs_uint32 pseudo_flag =
                (flag & TJS_IGNOREPROP) ? flag : (flag & ~TJS_MEMBERENSURE);
            // member ensuring is temporarily disabled unless
            // TJS_IGNOREPROP

            hr = inherited::PropSet(pseudo_flag, membername, hint, param,
                                    objthis);
            if(hr == TJS_E_MEMBERNOTFOUND) {
                TJS_DO_SUPERCLASS_PROXY_BEGIN
                hr = clo.PropSet(pseudo_flag, membername, hint, param, objthis);
                TJS_DO_SUPERCLASS_PROXY_END
            }

            if(hr == TJS_E_MEMBERNOTFOUND && (flag & TJS_MEMBERENSURE)) {
                // re-ensure the member for "this" object
                hr = inherited::PropSet(flag, membername, hint, param, objthis);
            }
        } else {
            hr = inherited::PropSet(flag, membername, hint, param, objthis);
        }

        return hr;
    }

    //---------------------------------------------------------------------------
    tjs_error
    tTJSInterCodeContext::CreateNew(tjs_uint32 flag, const tjs_char *membername,
                                    tjs_uint32 *hint, iTJSDispatch2 **result,
                                    tjs_int numparams, tTJSVariant **param,
                                    iTJSDispatch2 *objthis) {
        if(!GetValidity())
            return TJS_E_INVALIDOBJECT;

        if(membername == nullptr) {
            if(ContextType != ctClass)
                return TJS_E_INVALIDTYPE;

            iTJSDispatch2 *dsp = new inherited();

            try {
                ExecuteAsFunction(dsp, nullptr, 0, nullptr, 0);
                FuncCall(0, Name, nullptr, nullptr, numparams, param, dsp);
            } catch(...) {
                dsp->Release();
                throw;
            }

            *result = dsp;
            return TJS_S_OK;
        }

        tjs_error hr = inherited::CreateNew(flag, membername, hint, result,
                                            numparams, param, objthis);

        if(membername != nullptr && hr == TJS_E_MEMBERNOTFOUND &&
           ContextType == ctClass && SuperClassGetter) {
            // look up super class
            TJS_DO_SUPERCLASS_PROXY_BEGIN
            hr = clo.CreateNew(flag, membername, hint, result, numparams, param,
                               objthis);
            TJS_DO_SUPERCLASS_PROXY_END
        }
        return hr;
    }

    //---------------------------------------------------------------------------
    tjs_error tTJSInterCodeContext::IsInstanceOf(tjs_uint32 flag,
                                                 const tjs_char *membername,
                                                 tjs_uint32 *hint,
                                                 const tjs_char *classname,
                                                 iTJSDispatch2 *objthis) {
        if(!GetValidity())
            return TJS_E_INVALIDOBJECT;

        if(membername == nullptr) {
            switch(ContextType) {
                case ctTopLevel:
                case ctPropertySetter:
                case ctPropertyGetter:
                case ctSuperClassGetter:
                    break;

                case ctFunction:
                case ctExprFunction:
                    if(!TJS_strcmp(classname, TJS_W("Function")))
                        return TJS_S_TRUE;
                    break;

                case ctProperty:
                    if(!TJS_strcmp(classname, TJS_W("Property")))
                        return TJS_S_TRUE;
                    break;

                case ctClass:
                    if(!TJS_strcmp(classname, TJS_W("Class")))
                        return TJS_S_TRUE;
                    break;
            }
        }

        tjs_error hr =
            inherited::IsInstanceOf(flag, membername, hint, classname, objthis);

        if(membername != nullptr && hr == TJS_E_MEMBERNOTFOUND &&
           ContextType == ctClass && SuperClassGetter) {
            // look up super class
            TJS_DO_SUPERCLASS_PROXY_BEGIN
            hr = clo.IsInstanceOf(flag, membername, hint, classname, objthis);
            TJS_DO_SUPERCLASS_PROXY_END
        }
        return hr;
    }

    //---------------------------------------------------------------------------
    tjs_error tTJSInterCodeContext::GetCount(tjs_int *result,
                                             const tjs_char *membername,
                                             tjs_uint32 *hint,
                                             iTJSDispatch2 *objthis) {
        tjs_error hr = inherited::GetCount(result, membername, hint, objthis);

        if(membername != nullptr && hr == TJS_E_MEMBERNOTFOUND &&
           ContextType == ctClass && SuperClassGetter) {
            // look up super class
            TJS_DO_SUPERCLASS_PROXY_BEGIN
            hr = clo.GetCount(result, membername, hint, objthis);
            TJS_DO_SUPERCLASS_PROXY_END
        }
        return hr;
    }

    //---------------------------------------------------------------------------
    tjs_error tTJSInterCodeContext::DeleteMember(tjs_uint32 flag,
                                                 const tjs_char *membername,
                                                 tjs_uint32 *hint,
                                                 iTJSDispatch2 *objthis) {
        tjs_error hr = inherited::DeleteMember(flag, membername, hint, objthis);

        if(membername != nullptr && hr == TJS_E_MEMBERNOTFOUND &&
           ContextType == ctClass && SuperClassGetter) {
            // look up super class
            TJS_DO_SUPERCLASS_PROXY_BEGIN
            hr = clo.DeleteMember(flag, membername, hint, objthis);
            TJS_DO_SUPERCLASS_PROXY_END
        }
        return hr;
    }

    //---------------------------------------------------------------------------
    tjs_error tTJSInterCodeContext::Invalidate(tjs_uint32 flag,
                                               const tjs_char *membername,
                                               tjs_uint32 *hint,
                                               iTJSDispatch2 *objthis) {
        tjs_error hr = inherited::Invalidate(flag, membername, hint, objthis);

        if(membername != nullptr && hr == TJS_E_MEMBERNOTFOUND &&
           ContextType == ctClass && SuperClassGetter) {
            // look up super class
            TJS_DO_SUPERCLASS_PROXY_BEGIN
            hr = clo.Invalidate(flag, membername, hint, objthis);
            TJS_DO_SUPERCLASS_PROXY_END
        }
        return hr;
    }

    //---------------------------------------------------------------------------
    tjs_error tTJSInterCodeContext::IsValid(tjs_uint32 flag,
                                            const tjs_char *membername,
                                            tjs_uint32 *hint,
                                            iTJSDispatch2 *objthis) {
        tjs_error hr = inherited::IsValid(flag, membername, hint, objthis);

        if(membername != nullptr && hr == TJS_E_MEMBERNOTFOUND &&
           ContextType == ctClass && SuperClassGetter) {
            // look up super class
            TJS_DO_SUPERCLASS_PROXY_BEGIN
            hr = clo.IsValid(flag, membername, hint, objthis);
            TJS_DO_SUPERCLASS_PROXY_END
        }
        return hr;
    }

    //---------------------------------------------------------------------------
    tjs_error tTJSInterCodeContext::Operation(
        tjs_uint32 flag, const tjs_char *membername, tjs_uint32 *hint,
        tTJSVariant *result, const tTJSVariant *param, iTJSDispatch2 *objthis) {
        if(membername == nullptr) {
            if(ContextType == ctProperty) {
                // operation for property object
                return tTJSDispatch::Operation(flag, membername, hint, result,
                                               param, objthis);
            } else {
                return inherited::Operation(flag, membername, hint, result,
                                            param, objthis);
            }
        }

        // tjs_error hr;

        if(membername != nullptr && ContextType == ctClass &&
           SuperClassGetter) {
            tjs_uint32 pseudo_flag =
                (flag & TJS_IGNOREPROP) ? flag : (flag & ~TJS_MEMBERENSURE);

            tjs_error hr = inherited::Operation(pseudo_flag, membername, hint,
                                                result, param, objthis);

            if(hr == TJS_E_MEMBERNOTFOUND) {
                // look up super class
                TJS_DO_SUPERCLASS_PROXY_BEGIN
                hr = clo.Operation(pseudo_flag, membername, hint, result, param,
                                   objthis);
                TJS_DO_SUPERCLASS_PROXY_END
            }

            if(hr == TJS_E_MEMBERNOTFOUND)
                hr = inherited::Operation(flag, membername, hint, result, param,
                                          objthis);

            return hr;
        } else {
            return inherited::Operation(flag, membername, hint, result, param,
                                        objthis);
        }
    }
    //---------------------------------------------------------------------------

} // namespace TJS
