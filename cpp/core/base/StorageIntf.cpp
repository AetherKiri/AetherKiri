//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Universal Storage System
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cctype>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <string>
#include "StorageIntf.h"
#include "tjsUtils.h"
#include "MsgIntf.h"
#include "EventIntf.h"
#include "DebugIntf.h"
#include "tjsArray.h"
#include "SysInitIntf.h"
#include "XP3Archive.h"
#include "TickCount.h"
#include "ncbind.hpp"
#include "UtilStreams.h"
#include "impl/ArchiveAutoPathOrder.h"
#include "spdlog/spdlog.h"

#define TVP_DEFAULT_ARCHIVE_CACHE_NUM 128
#define TVP_DEFAULT_AUTOPATH_CACHE_NUM 256
static constexpr tjs_int TVP_MAX_STORAGE_NAME_LENGTH = 1 << 20;
static const tjs_char *TVP_AUTOPATH_CACHE_MISS_MARKER = TJS_W("\x01");
static const char TVP_GFX_EFFECT_COMPAT_SCRIPT[] =
    "// AetherKiri gfxEffect.dll compatibility placeholder.\n"
    "try { Plugins.link(\"gfxEffect.dll\"); } catch(e) { }\n";
static const char TVP_GPU_COMPAT_SCRIPT[] =
    "// AetherKiri GPULayer/D3D compatibility placeholder.\n"
    "try { Plugins.link(\"krkrgles.dll\"); } catch(e) { }\n"
    "try { Plugins.link(\"krkrlive2d.dll\"); } catch(e) { }\n"
    "try { Window.OGLDrawDevice = OGLDrawDevice; } catch(e) { }\n"
    "try { Window.GLESAdaptor = GLESAdaptor; } catch(e) { }\n"
    "function KAGWindow_createDrawDevice() {\n"
    "    var dd = null;\n"
    "    try { dd = new OGLDrawDevice(); } catch(e) { try { dd = new GLESAdaptor(); } catch(e2) { dd = null; } }\n"
    "    try { if(dd !== null) dd.setScreenSize(this.width, this.height); } catch(e) { }\n"
    "    try { this.gpuDrawDevice = dd; } catch(e) { }\n"
    "    try { this.OGLDrawDevice = dd; } catch(e) { }\n"
    "    try { this.GLESAdaptor = dd; } catch(e) { }\n"
    "    try { return new global.Window.BasicDrawDevice(); } catch(e) { }\n"
    "    try { return new global.Window.PassThroughDrawDevice(); } catch(e) { }\n"
    "    return null;\n"
    "}\n"
    "try { KAGWindow.KAGWindow_createDrawDevice = KAGWindow_createDrawDevice; } catch(e) { }\n"
    "try { KAGWindow.prototype.KAGWindow_createDrawDevice = KAGWindow_createDrawDevice; } catch(e) { }\n"
    "try { KAGWindow_createDrawDevice = KAGWindow_createDrawDevice; } catch(e) { }\n";
static const char TVP_D3DEMOTE_COMPAT_PREFIX[] =
    "// AetherKiri D3DEmote/motion.tjs compatibility bridge.\n"
    "try { Plugins.link(\"emoteplayer.dll\"); } catch(e) { }\n";
static const char TVP_LOGWINDOW_COMPAT_SCRIPT[] = R"TJS(
// AetherKiri KAGEX LogWindow.tjs compatibility bridge.
class LogWindowPad extends Pad {
    function LogWindowPad(owner, action, maxline = 300, caption = "KAGEX log") {
        super.Pad();

        this.owner = owner;
        this.action = action;
        this.maxline = maxline;

        borderStyle = bsSizeToolWin;
        color = 0;
        fontColor = 0xFFFFFF;
        fontFace = "monospace";
        readOnly = false;
        wordWrap = true;
        showScrollBars = ssVertical;
        height = 10;
        title = caption;
        clear();

        trigger = new AsyncTrigger(updateText, '');
        with (trigger) .mode = atmAtIdle, .cached = true;
    }

    function finalize() {
        if (!isvalid this) return;
        invalidate trigger if (trigger);
        trigger = void;
        super.finalize(...);
    }

    function clear() {
        lines.clear();
        text = "";
        clearNext = false;
        statusText = "latest log first";
    }

    function setPos(x, y, w, h) {
        left = x if (x !== void);
        top = y if (y !== void);
        setSize(w, h);
    }

    function setSize(w, h) {
        width = w if (w !== void);
        height = h if (h !== void);
    }

    var owner, action;
    var trigger;
    var maxline, lines = [], clearNext;

    function onClose() {
        if (!isvalid this) return;
        invokeOwnerAction("closed");
    }

    function invokeOwnerAction(message, *) {
        if (!isvalid owner) return;
        if (typeof owner[action] == "Object") {
            return owner[action](message, *);
        }
    }

    function showResults(blocks*) {
        var all = [];
        for (var i = 0; i < blocks.count; i++) {
            all.add(blocks[i].join("\n")) if (blocks[i] !== void);
        }
        text = all.join("\n\n");
        statusText = "output";
        clearNext = true;
    }

    function print(shortmsg, fullmsg = void, tag = void) {
        clear() if (clearNext);
        lines.unshift(shortmsg);
        while (lines.count > maxline) lines.pop();
        if (trigger) trigger.trigger();
        else updateText();
    }

    function updateText() {
        if (!isvalid this) return;
        text = lines.join("\n");
    }
}

&global.LogWindow = LogWindowPad;
)TJS";
extern const unsigned char kAetherKiriD3DEmoteTjs[];
extern const std::size_t kAetherKiriD3DEmoteTjsSize;
static tTJSVariant TVPStoragesArchiveUniqueKeyCompat;

namespace {
bool TVPSaveTraceEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_SAVE_TRACE");
        return value && *value && *value != '0';
    }();
    return enabled;
}

bool TVPStorageTraceEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_STORAGE_TRACE");
        return value && *value && *value != '0';
    }();
    return enabled;
}

bool TVPStorageTraceName(const ttstr &name) {
    std::string text = name.AsStdString();
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    if(const char *match = std::getenv("AETHERKIRI_STORAGE_TRACE_MATCH")) {
        std::string filters(match);
        std::transform(filters.begin(), filters.end(), filters.begin(),
                       [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });
        size_t pos = 0;
        while(pos <= filters.size()) {
            size_t comma = filters.find(',', pos);
            std::string token = filters.substr(
                pos, comma == std::string::npos ? std::string::npos : comma - pos);
            token.erase(0, token.find_first_not_of(" \t\r\n"));
            size_t end = token.find_last_not_of(" \t\r\n");
            if(end != std::string::npos)
                token.erase(end + 1);
            else
                token.clear();
            if(!token.empty() && text.find(token) != std::string::npos)
                return true;
            if(comma == std::string::npos)
                break;
            pos = comma + 1;
        }
    }
    return text.find(".pbd") != std::string::npos ||
        text.find("patch2.xp3") != std::string::npos ||
        text.find("aaemo") != std::string::npos ||
        text.find("motion.tjs") != std::string::npos ||
        text.find("d3demote.tjs") != std::string::npos ||
        text.find("logwindow.tjs") != std::string::npos;
}

bool TVPIsSplitEmoteVirtualStorage(const ttstr &name) {
    std::string storage = TVPExtractStorageName(name).AsStdString();
    if(storage.empty()) {
        storage = name.AsStdString();
        const auto slash = storage.find_last_of("/\\");
        if(slash != std::string::npos) {
            storage = storage.substr(slash + 1);
        }
    }
    std::transform(storage.begin(), storage.end(), storage.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    if(storage.rfind("dx_", 0) == 0) {
        storage = storage.substr(3);
    }

    const auto stripSuffix = [](std::string &value,
                                const std::string &suffix) {
        if(value.size() < suffix.size() ||
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) !=
               0) {
            return false;
        }
        value.resize(value.size() - suffix.size());
        return true;
    };
    if(!stripSuffix(storage, ".mtn") && !stripSuffix(storage, ".psb")) {
        stripSuffix(storage, ".mt");
    }
    return storage.size() > 3 &&
        storage.compare(storage.size() - 3, 3, "emo") == 0;
}

// AffineSourceVector treats solid_<colour>.emf/.wmf as virtual vector images.
// They deliberately have no archive entry; the layerExDraw backend
// materialises the solid ARGB source when GdiPlus.Image.load() is called.
// Advertising only the supported naming protocol here lets KAG's normal
// getExistImageName() path select the vector source without pretending that
// arbitrary missing EMF/WMF files exist.
bool TVPIsVirtualSolidVectorStorageImpl(const ttstr &name) {
    std::string storage = TVPExtractStorageName(name).AsStdString();
    if(storage.empty()) {
        storage = name.AsStdString();
        const auto slash = storage.find_last_of("/\\");
        if(slash != std::string::npos) {
            storage = storage.substr(slash + 1);
        }
    }
    std::transform(storage.begin(), storage.end(), storage.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });

    const auto dot = storage.find_last_of('.');
    if(dot == std::string::npos) {
        return false;
    }
    const std::string ext = storage.substr(dot);
    if(ext != ".emf" && ext != ".wmf") {
        return false;
    }
    storage.resize(dot);
    constexpr const char prefix[] = "solid_";
    if(storage.rfind(prefix, 0) != 0 || storage.size() <= sizeof(prefix) - 1) {
        return false;
    }

    std::string token = storage.substr(sizeof(prefix) - 1);
    if(token == "black" || token == "white" || token == "transparent") {
        return true;
    }
    if(!token.empty() && token.front() == '#') {
        token.erase(token.begin());
    } else if(token.size() > 2 && token[0] == '0' && token[1] == 'x') {
        token.erase(0, 2);
    }
    return (token.size() == 6 || token.size() == 8) &&
        std::all_of(token.begin(), token.end(), [](unsigned char ch) {
            return std::isxdigit(ch) != 0;
        });
}
} // namespace

bool TVPIsVirtualSolidVectorStorage(const ttstr &name) {
    return TVPIsVirtualSolidVectorStorageImpl(name);
}

//---------------------------------------------------------------------------
// global variables
//---------------------------------------------------------------------------
// current media ( ex. "http" "ftp" "file" )
ttstr TVPCurrentMedia = TJS_W("file");
// archive delimiter
// this changes '>' from '#' since 2.19 beta 14
tjs_char TVPArchiveDelimiter = '>';
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// statics
//---------------------------------------------------------------------------
static tTJSStaticCriticalSection TVPCreateStreamCS;

// Private plug-ins can publish a logical-storage resolver without making the
// public storage layer know any game-specific filenames. Keep the callback
// list here so placement and stream opening observe the same result. A
// recursion guard lets a resolver probe a concrete candidate through the
// normal storage APIs safely.
static std::mutex TVPStorageResolverMutex;
static std::vector<tTVPStorageResolver> TVPStorageResolvers;
static thread_local unsigned int TVPStorageResolverDepth = 0;

static bool TVPResolveStorageName(const ttstr &requested, ttstr &resolved) {
    if(requested.IsEmpty() || TVPStorageResolverDepth != 0)
        return false;

    std::vector<tTVPStorageResolver> resolvers;
    {
        std::lock_guard<std::mutex> lock(TVPStorageResolverMutex);
        resolvers = TVPStorageResolvers;
    }

    if(TVPStorageTraceEnabled() && TVPStorageTraceName(requested) &&
       requested.AsStdString().find("__pack") != std::string::npos) {
        spdlog::info("StorageTrace resolver-attempt request={} count={}",
                     requested.AsStdString(), resolvers.size());
    }

    ++TVPStorageResolverDepth;
    for(const auto resolver : resolvers) {
        if(resolver == nullptr)
            continue;
        ttstr candidate;
        bool handled = false;
        try {
            handled = resolver(requested, candidate);
        } catch(...) {
            // A compatibility resolver must not make ordinary storage lookup
            // fail. Its own diagnostic, if any, remains in the plugin log.
            handled = false;
        }
        if(handled && !candidate.IsEmpty() && candidate != requested) {
            resolved = candidate;
            --TVPStorageResolverDepth;
            return true;
        }
    }
    --TVPStorageResolverDepth;
    return false;
}

void TVPRegisterStorageResolver(tTVPStorageResolver resolver) {
    if(resolver == nullptr)
        return;
    std::lock_guard<std::mutex> lock(TVPStorageResolverMutex);
    if(std::find(TVPStorageResolvers.begin(), TVPStorageResolvers.end(),
                 resolver) == TVPStorageResolvers.end())
        TVPStorageResolvers.push_back(resolver);
}

void TVPUnregisterStorageResolver(tTVPStorageResolver resolver) {
    if(resolver == nullptr)
        return;
    std::lock_guard<std::mutex> lock(TVPStorageResolverMutex);
    TVPStorageResolvers.erase(
        std::remove(TVPStorageResolvers.begin(), TVPStorageResolvers.end(),
                    resolver),
        TVPStorageResolvers.end());
}
//---------------------------------------------------------------------------

static bool TVPIsGfxEffectCompanionScript(const ttstr &name) {
    ttstr storage = TVPExtractStorageName(name).AsLowerCase();
    return (storage == TJS_W("gfx_fire.tjs") ||
            storage == TJS_W("gfx_flash.tjs")) &&
           (TVPRegisteredPlugins.find(TJS_W("gfxeffect.dll")) !=
                TVPRegisteredPlugins.end() ||
            TVPRegisteredPlugins.find(TJS_W("gfxfire.dll")) !=
                TVPRegisteredPlugins.end() ||
            ncbAutoRegister::HasModule(TJS_W("gfxeffect.dll")) ||
            ncbAutoRegister::HasModule(TJS_W("gfxfire.dll")));
}

static bool TVPIsGpuCompanionScript(const ttstr &name) {
    ttstr storage = TVPExtractStorageName(name).AsLowerCase();
    return storage == TJS_W("gpulayer.tjs") ||
           storage == TJS_W("gpuaffinelayer.tjs") ||
           storage == TJS_W("d3d.tjs") ||
           storage == TJS_W("d3daffinesource.tjs") ||
           storage == TJS_W("d3daffinesourcepicture.tjs") ||
           storage == TJS_W("d3daffinesourceimage.tjs") ||
           storage == TJS_W("d3daffinesourcemotion.tjs") ||
           storage == TJS_W("d3daffinesourcelive2d.tjs") ||
           storage == TJS_W("d3daffinesourceemote.tjs") ||
           storage == TJS_W("affinesourcelive2d.tjs") ||
           storage == TJS_W("live2d.tjs");
}

static bool TVPIsD3DEmoteCompanionScript(const ttstr &name) {
    ttstr storage = TVPExtractStorageName(name).AsLowerCase();
    return storage == TJS_W("motion.tjs") ||
           storage == TJS_W("d3demote.tjs");
}

static bool TVPIsLogWindowCompanionScript(const ttstr &name) {
    ttstr storage = TVPExtractStorageName(name).AsLowerCase();
    return storage == TJS_W("logwindow.tjs");
}

static tTJSBinaryStream *TVPOpenGfxEffectCompanionScript() {
    return new tTVPMemoryStream(
        TVP_GFX_EFFECT_COMPAT_SCRIPT,
        static_cast<tjs_uint>(sizeof(TVP_GFX_EFFECT_COMPAT_SCRIPT) - 1));
}

static tTJSBinaryStream *TVPOpenGpuCompanionScript() {
    return new tTVPMemoryStream(
        TVP_GPU_COMPAT_SCRIPT,
        static_cast<tjs_uint>(sizeof(TVP_GPU_COMPAT_SCRIPT) - 1));
}

static tTJSBinaryStream *TVPOpenLogWindowCompanionScript() {
    return new tTVPMemoryStream(
        TVP_LOGWINDOW_COMPAT_SCRIPT,
        static_cast<tjs_uint>(sizeof(TVP_LOGWINDOW_COMPAT_SCRIPT) - 1));
}

static tTJSBinaryStream *TVPOpenD3DEmoteCompanionScript() {
    auto *stream = new tTVPMemoryStream();
    try {
        stream->Write(TVP_D3DEMOTE_COMPAT_PREFIX,
                      static_cast<tjs_uint>(
                          sizeof(TVP_D3DEMOTE_COMPAT_PREFIX) - 1));
        stream->Write(kAetherKiriD3DEmoteTjs,
                      static_cast<tjs_uint>(kAetherKiriD3DEmoteTjsSize));
        stream->Seek(0, TJS_BS_SEEK_SET);
        return stream;
    } catch(...) {
        delete stream;
        throw;
    }
}

static bool TVPIsRealStorageNoSearchNoNormalize(const ttstr &name);

static bool TVPGetMotionParameterCompanionInfo(const ttstr &name,
                                               ttstr *sourceName) {
    std::string storage = TVPExtractStorageName(name).AsStdString();
    if(storage.empty()) {
        storage = name.AsStdString();
        const auto slash = storage.find_last_of("/\\");
        if(slash != std::string::npos)
            storage = storage.substr(slash + 1);
    }

    std::string lower = storage;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    if(lower.size() <= 11 || lower.rfind("motion_", 0) != 0 ||
       lower.compare(lower.size() - 4, 4, ".tjs") != 0) {
        return false;
    }

    std::string inner = storage.substr(7, storage.size() - 11);
    std::string innerLower = lower.substr(7, lower.size() - 11);
    const auto dot = innerLower.rfind('.');
    if(dot == std::string::npos)
        return false;
    const std::string ext = innerLower.substr(dot);
    if(ext != ".mtn" && ext != ".psb")
        return false;

    if(sourceName)
        *sourceName = ttstr(inner.c_str());
    return true;
}

static bool TVPIsUnprefixedD3DEmoteStorage(const ttstr &storageName) {
    const ttstr lower = storageName.AsLowerCase();
    if(lower.StartsWith(TJS_W("dx_")) ||
       lower.StartsWith(TJS_W("dxlow_")) ||
       lower.GetLen() <= 4) {
        return false;
    }
    return lower.SubString(lower.GetLen() - 4, 4) == TJS_W(".psb");
}

static std::string TVPEscapeTJSStringLiteral(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size());
    for(unsigned char ch : value) {
        switch(ch) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\r': escaped += "\\r"; break;
            case '\n': escaped += "\\n"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(static_cast<char>(ch)); break;
        }
    }
    return escaped;
}

static tTJSBinaryStream *TVPOpenMotionParameterCompanionScript(
    const ttstr &sourceName) {
    // KAG's world.tjs treats motion_<asset>.psb.tjs as an expression whose
    // result describes the image source. Returning an empty dictionary makes
    // checkAnimImageData() erase the original filename before it reaches
    // MotionResourceManager. Preserve that filename so .PSB is routed to
    // MotionAffineSourceLayer and, in turn, Motion.EmotePlayer.
    const std::string script =
        "%[\"storage\" => \"" +
        TVPEscapeTJSStringLiteral(sourceName.AsStdString()) + "\"]";
    auto *stream = new tTVPMemoryStream();
    try {
        stream->Write(script.data(), static_cast<tjs_uint>(script.size()));
        stream->Seek(0, TJS_BS_SEEK_SET);
        return stream;
    } catch(...) {
        delete stream;
        throw;
    }
}

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// utilities
//---------------------------------------------------------------------------
ttstr TVPStringFromBMPUnicode(const tjs_uint16 *src, tjs_int maxlen) {
    // convert to ttstr from BMP unicode
    if(sizeof(tjs_char) == 2) {
        // sizeof(tjs_char) is 2 (windows native)
        if(maxlen == -1)
            return ttstr((const tjs_char *)src);
        else
            return ttstr((const tjs_char *)src, maxlen);
    } else if(sizeof(tjs_char) == 4) {
        // sizeof(tjs_char) is 4 (UCS32)
        // FIXME: NOT TESTED CODE
        tjs_int len = 0;
        const tjs_uint16 *p = src;
        while(*p)
            len++, p++;
        if(maxlen != -1 && len > maxlen)
            len = maxlen;
        ttstr ret((tTJSStringBufferLength)(len));
        tjs_char *dest = ret.Independ();
        p = src;
        while(len && *p) {
            *dest = *p;
            dest++;
            p++;
            len--;
        }
        *dest = 0;
        ret.FixLen();
        return ret;
    }
    return (const tjs_char *)TVPTjsCharMustBeTwoOrFour;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPStorageMediaManager
//---------------------------------------------------------------------------
class tTVPStorageMediaManager {
    class tMediaNameString : public tTJSString {
    public:
        bool operator==(const tMediaNameString &rhs) const {
            const tjs_char *l_p = c_str();
            const tjs_char *r_p = rhs.c_str();

            while(*l_p && *r_p) {
                if(*l_p == TJS_W(':'))
                    break;
                if(*r_p == TJS_W(':'))
                    break;
                if(*l_p != *r_p)
                    break;
                l_p++;
                r_p++;
            }
            if((*l_p == TJS_W(':') || *l_p == 0) &&
               (*r_p == TJS_W(':') || *r_p == 0))
                return true;
            return false;
        }
    };

    class tHashFunc {
    public:
        static tjs_uint32 Make(const tMediaNameString &key) {
            if(key.IsEmpty())
                return 0;
            const tjs_char *str = key.c_str();
            tjs_uint32 ret = 0;
            while(*str && *str != ':') {
                ret += *str;
                ret += (ret << 10);
                ret ^= (ret >> 6);
                str++;
            }
            ret += (ret << 3);
            ret ^= (ret >> 11);
            ret += (ret << 15);
            if(!ret)
                ret = (tjs_uint32)-1;
            return ret;
        }
    };

    class tMediaRecord {
    public:
        ttstr CurrentDomain;
        ttstr CurrentPath;
        tTJSRefHolder<iTVPStorageMedia> MediaIntf;
        tjs_int MediaNameLen;
        //		bool IsCaseSensitive;

        tMediaRecord(iTVPStorageMedia *media) :
            MediaIntf(media), CurrentDomain("."), CurrentPath("/") {
            ttstr name;
            media->GetName(name);
            MediaNameLen = name.GetLen();
        /*IsCaseSensitive = media->IsCaseSensitive();*/ }

        const tjs_char *GetDomainAndPath(const ttstr &name) const {
            return name.c_str() + MediaNameLen + 3;
            // 3 = strlen("://")
        }
    };

    typedef tTJSHashTable<tMediaNameString, tMediaRecord, tHashFunc, 16>
        tHashTable;

    tHashTable HashTable;

public:
    tTVPStorageMediaManager();

    ~tTVPStorageMediaManager();

private:
    static void ThrowUnsupportedMediaType(const ttstr &name);

    tMediaRecord *GetMediaRecord(const ttstr &name);

public:
    void Register(iTVPStorageMedia *media);

    void Unregister(iTVPStorageMedia *media);

    ttstr NormalizeStorageName(const ttstr &name, ttstr *ret_media = nullptr,
                               ttstr *ret_domain = nullptr,
                               ttstr *ret_path = nullptr);

    void SetCurrentDirectory(const ttstr &name);

    static ttstr ExtractMediaName(const ttstr &name);

    bool CheckExistentStorage(const ttstr &name);

    tTJSBinaryStream *Open(const ttstr &name, tjs_uint32 flags);

    void GetListAt(const ttstr &name, iTVPStorageLister *lister);

    ttstr GetLocallyAccessibleName(const ttstr &name);
} TVPStorageMediaManager;

//---------------------------------------------------------------------------
tTVPStorageMediaManager::tTVPStorageMediaManager() {
    iTVPStorageMedia *filemedia = TVPCreateFileMedia();
    Register(filemedia);
    filemedia->Release();

    iTVPStorageMedia *arcmedia = TVPCreateArcMedia();
    Register(arcmedia);
    arcmedia->Release();
}

//---------------------------------------------------------------------------
tTVPStorageMediaManager::~tTVPStorageMediaManager() {}

//---------------------------------------------------------------------------
void tTVPStorageMediaManager::ThrowUnsupportedMediaType(const ttstr &name) {
    TVPThrowExceptionMessage(TVPUnsupportedMediaName, ExtractMediaName(name));
}

//---------------------------------------------------------------------------
tTVPStorageMediaManager::tMediaRecord *
tTVPStorageMediaManager::GetMediaRecord(const ttstr &name) {
    tMediaRecord *rec = HashTable.Find(*(tMediaNameString *)&name);
    if(!rec)
        ThrowUnsupportedMediaType(name);
    return rec;
}

//---------------------------------------------------------------------------
void tTVPStorageMediaManager::Register(iTVPStorageMedia *media) {
    ttstr medianame;
    media->GetName(medianame);

    tMediaRecord *rec = HashTable.Find(*(tMediaNameString *)&medianame);
    if(rec) return;

    tMediaRecord new_rec(media);

    HashTable.Add(*(tMediaNameString *)&medianame, new_rec);
}

//---------------------------------------------------------------------------
void tTVPStorageMediaManager::Unregister(iTVPStorageMedia *media) {
    ttstr medianame;
    media->GetName(medianame);

    tMediaRecord *rec = HashTable.Find(*(tMediaNameString *)&medianame);
    if(!rec)
        TVPThrowExceptionMessage(TVPMediaNameIsNotRegistered, medianame);
    HashTable.Delete(*(tMediaNameString *)&medianame);
}

//---------------------------------------------------------------------------
ttstr tTVPStorageMediaManager::NormalizeStorageName(const ttstr &name,
                                                    ttstr *ret_media,
                                                    ttstr *ret_domain,
                                                    ttstr *ret_path) {
    // Normalize storage name.

    // storage name is basically in following form:
    // media://domain/path

    // media is sort of access method, like "file", "http" ...etc.
    // domain represents in which computer the data is.
    // path is where the data is in the computer.

    // empty check
    if(name.IsEmpty())
        return name; // empty name is empty name
    if(name.GetLen() > TVP_MAX_STORAGE_NAME_LENGTH)
        TVPThrowExceptionMessage(TVPInvalidPathName,
                                 TJS_W("<storage path too long>"));

    // pre-normalize
    const tjs_char *pca; //, *pcb, *pcc;
    tjs_char *pa, *pb, *pc;

    ttstr tmp(name);
    TVPPreNormalizeStorageName(tmp);

    // unify path delimiter
    pa = tmp.Independ();
    while(*pa) {
        if(*pa == TJS_W('\\'))
            *pa = TJS_W('/');
        pa++;
    }

    // save in-archive storage name and normalize it
    ttstr inarchive_name;
    bool inarc_name_found = false;
    pca = tmp.c_str();
    pa = TJS_strchr(pca, TVPArchiveDelimiter);
    if(pa) {
        inarchive_name = ttstr(pa + 1);
        tTVPArchive::NormalizeInArchiveStorageName(inarchive_name);
        inarc_name_found = true;
        tmp = ttstr(pca, (int)(pa - pca));
    }
    if(tmp.IsEmpty())
        TVPThrowExceptionMessage(TVPInvalidPathName, name);

    // split the name into media, domain, path
    // (and guess what component is omitted)
    ttstr media, domain, path;

    // - find media name
    //   media name is: /^[A-Za-z]+:/
    pa = pb = tmp.Independ();
    while(*pa) {
#ifdef WIN32
        if(*pa == TJS_W(':'))
            break;
#else
        if(!((*pa >= TJS_W('A') && *pa <= TJS_W('Z')) ||
             (*pa >= TJS_W('a') && *pa <= TJS_W('z'))))
            break;
#endif
        pa++;
    }

    if(*pa == TJS_W(':')) {
        // media name found
        media = ttstr(pb, (int)(pa - pb));
        pa++;
    } else {
        pa = pb;
    }

    // - find domain name
    // at this place, pa may point one of following:
    //  ///path        (domain is omitted)
    //  //domain/path  (none is omitted)
    //  /path          (domain is omitted)
    //  relative-path  (domain and current path are omitted)

    if(pa[0] == TJS_W('/')) {
        if(pa[1] == TJS_W('/')) {
            if(pa[2] == TJS_W('/')) {
                // slash count 3: domain is ommited
                pa += 2;
            } else {
                // slash count 2: none is omitted
                pa += 2;
                // find '/' as a domain delimiter
                pc = TJS_strchr(pa, TJS_W('/'));
                if(!pc)
                    TVPThrowExceptionMessage(TVPInvalidPathName, name);
                domain = ttstr(pa, (int)(pc - pa));
                pa = pc;
            }
        } else {
            // slash count 1: domain is omitted
            ;
            //
        }
    }

    // - get path name
    path = pa;

    // supply omitted and normalize
    if(media.IsEmpty()) {
        media = TVPCurrentMedia;
        if(media.IsEmpty()) media = TJS_W("file");
    } else {
        // normalize media name ( make them all small )
        //        tjs_char *p = media.Independ();
        //        while(*p) {
        //            if(*p >= TJS_W('A') && *p <= TJS_W('Z'))
        //                *p += (TJS_W('a') - TJS_W('A'));
        //            p++;
        //        }
    }

    tMediaRecord *mediarec = GetMediaRecord(media);

    if(domain.IsEmpty())
        domain = mediarec->CurrentDomain;
    mediarec->MediaIntf.GetObjectNoAddRef()->NormalizeDomainName(domain);

    if(path.IsEmpty()) {
        path = TJS_W("/");
    } else if(path.c_str()[0] != TJS_W('/')) {
        path = mediarec->CurrentPath + path;
    }
    mediarec->MediaIntf.GetObjectNoAddRef()->NormalizePathName(path);

    // compress redudant path accesses
    if(inarc_name_found) {
        tjs_char tmp[2];
        tmp[0] = TVPArchiveDelimiter;
        tmp[1] = 0;
        path += tmp + inarchive_name;
    }

    pa = pb = pc = path.Independ(); // pa = read pointer, pb = write
                                    // pointer, pc = start
    tjs_int dot_count = -1;

    while(true) {
        if(*pa == TVPArchiveDelimiter || *pa == TJS_W('/') || *pa == 0) {
            tjs_char delim = 0;

            if(*pa && dot_count == 0) {
                // duplicated slashes
                pb--;
            } else if(dot_count > 0) {
                pb--;
                while(pb >= pc) {
                    if(*pb == TJS_W('/') || *pb == TVPArchiveDelimiter) {
                        dot_count--;
                        if(dot_count == 0) {
                            delim = *pb;
                            break;
                        }
                        if(*pb == TVPArchiveDelimiter)
                            TVPThrowExceptionMessage(TVPInvalidPathName, name);
                    }
                    pb--;
                }
                if(pb < pc)
                    TVPThrowExceptionMessage(TVPInvalidPathName, name);
            }

            if(!delim)
                *pb = *pa;
            else
                *pb = delim;
            if(*pa == 0)
                break;
            pb++;
            pa++;
            dot_count = 0;
        } else if(*pa == TJS_W('.')) {
            *(pb++) = *(pa++);
            if(dot_count != -1)
                dot_count++;
        } else {
            *(pb++) = *(pa++);
            dot_count = -1;
        }
    }

    path.FixLen();

    // merge and return normalize storage name
    if(ret_media)
        *ret_media = media;
    if(ret_domain)
        *ret_domain = domain;
    if(ret_path)
        *ret_path = path;

    tmp = media + TJS_W("://") + domain + path;

    return tmp;
}

static ttstr FixMissingPathDelimiter(const ttstr& name) {
    ttstr correctedName = name;
    tjs_char lastchar = correctedName.GetLastChar();
    
    // Trim unexpectedly appended trailing spaces or double quotes from scripts
    while (correctedName.GetLen() > 0 && (lastchar == TJS_W(' ') || lastchar == TJS_W('\t') || lastchar == TJS_W('\r') || lastchar == TJS_W('\n') || lastchar == TJS_W('\"'))) {
        correctedName = ttstr(correctedName.c_str(), correctedName.GetLen() - 1);
        lastchar = correctedName.GetLastChar();
    }

    if(correctedName.GetLen() > 0 && lastchar != TVPArchiveDelimiter && lastchar != TJS_W('/') && lastchar != TJS_W('\\')) {
        if (correctedName.GetLen() > 4) {
            ttstr ext = ttstr(correctedName.c_str() + correctedName.GetLen() - 4).AsLowerCase();
            if(ext == TJS_W(".xp3") || ext == TJS_W(".tpm") || ext == TJS_W(".apk") || ext == TJS_W(".zip")) {
                correctedName += TVPArchiveDelimiter;
            } else {
                correctedName += TJS_W('/');
            }
        } else {
            correctedName += TJS_W('/');
        }
        TVPAddLog(TJS_W("(info) Automatically patched missing path delimiter: ") + correctedName);
    }
    return correctedName;
}

//---------------------------------------------------------------------------
void tTVPStorageMediaManager::SetCurrentDirectory(const ttstr &name) {
    ttstr fixedName = FixMissingPathDelimiter(name);

    ttstr media, domain, path;
    NormalizeStorageName(fixedName, &media, &domain, &path);

    tMediaRecord *rec = GetMediaRecord(media);
    rec->CurrentDomain = domain;
    rec->CurrentPath = path;
    TVPCurrentMedia = media;
}

//---------------------------------------------------------------------------
ttstr tTVPStorageMediaManager::ExtractMediaName(const ttstr &name) {
    // extract media name from normalized storage named "name".
    // returned media name does not contain colon.

    const tjs_char *p = name.c_str();
    const tjs_char *po = p;
    while(*p && *p != TJS_W(':'))
        p++;
    return { po, (size_t)(p - po) };
}

//---------------------------------------------------------------------------
bool tTVPStorageMediaManager::CheckExistentStorage(const ttstr &name) {
    // gateway for CheckExistentStorage
    // name must not be an in-archive storage name
    if(name.IsEmpty())
        return false;
    tMediaRecord *rec = GetMediaRecord(name);
    return rec->MediaIntf.GetObjectNoAddRef()->CheckExistentStorage(
        rec->GetDomainAndPath(name));
}

//---------------------------------------------------------------------------
tTJSBinaryStream *tTVPStorageMediaManager::Open(const ttstr &name,
                                                tjs_uint32 flags) {
    // gateway for Open
    // name must not be an in-archive storage name
    tMediaRecord *rec = GetMediaRecord(name);
    return rec->MediaIntf.GetObjectNoAddRef()->Open(rec->GetDomainAndPath(name),
                                                    flags);
}

//---------------------------------------------------------------------------
void tTVPStorageMediaManager::GetListAt(const ttstr &name,
                                        iTVPStorageLister *lister) {
    // gateway for GetListAt
    // name must not be an in-archive storage name
    tMediaRecord *rec = GetMediaRecord(name);
    /*return */ rec->MediaIntf.GetObjectNoAddRef()->GetListAt(
        rec->GetDomainAndPath(name), lister);
}

//---------------------------------------------------------------------------
ttstr tTVPStorageMediaManager::GetLocallyAccessibleName(const ttstr &name) {
    // gateway for GetLocallyAccessibleName
    // name must not be an in-archive storage name
    tMediaRecord *rec = GetMediaRecord(name);
    ttstr dname = rec->GetDomainAndPath(name);
    rec->MediaIntf.GetObjectNoAddRef()->GetLocallyAccessibleName(dname);
    return dname;
}

//---------------------------------------------------------------------------
void TVPGetListAt(const ttstr &name, iTVPStorageLister *lister) {
    TVPStorageMediaManager.GetListAt(name, lister);
}

//---------------------------------------------------------------------------
void TVPRegisterStorageMedia(iTVPStorageMedia *media) {
    TVPStorageMediaManager.Register(media);
}

//---------------------------------------------------------------------------
void TVPUnregisterStorageMedia(iTVPStorageMedia *media) {
    TVPStorageMediaManager.Unregister(media);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPNormalizeStorgeName : storage name normalization
//---------------------------------------------------------------------------
ttstr TVPNormalizeStorageName(const ttstr &_name)
// TODO: check what is done in TVPNormalizeStorageName
{
    return TVPStorageMediaManager.NormalizeStorageName(_name);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPSetCurrentDirectory
//---------------------------------------------------------------------------
void TVPSetCurrentDirectory(const ttstr &_name) {
    TVPStorageMediaManager.SetCurrentDirectory(_name);
    TVPClearStorageCaches();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetLocalName and TVPGetLocallyAccessibleName
//---------------------------------------------------------------------------
void TVPGetLocalName(ttstr &name) {
    ttstr tmp = TVPGetLocallyAccessibleName(name);
    if(tmp.IsEmpty())
        TVPThrowExceptionMessage(TVPCannotGetLocalName, name);
    name = tmp;
}

//---------------------------------------------------------------------------
ttstr TVPGetLocallyAccessibleName(const ttstr &name) {
    if(TJS_strchr(name.c_str(), TVPArchiveDelimiter))
        return TJS_W("");
    // in-archive storage is always not accessible from local file
    // system
    return TVPStorageMediaManager.GetLocallyAccessibleName(name);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPArchive
//---------------------------------------------------------------------------
void tTVPArchive::NormalizeInArchiveStorageName(ttstr &name) {
    // normalization of in-archive storage name does :
    if(name.IsEmpty())
        return;

    // make all characters small
    // change '\\' to '/'
    tjs_char *ptr = name.Independ();
    while(*ptr) {
        if(*ptr >= TJS_W('A') && *ptr <= TJS_W('Z'))
            *ptr += TJS_W('a') - TJS_W('A');
        else if(*ptr == TJS_W('\\'))
            *ptr = TJS_W('/');
        ptr++;
    }

    // eliminate duplicated slashes
    ptr = name.Independ();
    tjs_char *org_ptr = ptr;
    tjs_char *dest = ptr;
    while(*ptr) {
        if(*ptr != TJS_W('/')) {
            *dest = *ptr;
            ptr++;
            dest++;
        } else {
            if(ptr != org_ptr) {
                *dest = *ptr;
                ptr++;
                dest++;
            }
            while(*ptr == TJS_W('/'))
                ptr++;
        }
    }
    *dest = 0;

    name.FixLen();
}

//---------------------------------------------------------------------------
void tTVPArchive::AddToHash() {
    // enter all names to the hash table
    tjs_uint Count = GetCount();
    tjs_uint i;
    for(i = 0; i < Count; i++) {
        ttstr name = GetName(i);
        NormalizeInArchiveStorageName(name);
        Hash.Add(name, i);
    }
}

//---------------------------------------------------------------------------
tTJSBinaryStream *tTVPArchive::CreateStream(const ttstr &name) {
    if(name.IsEmpty())
        return nullptr;

    if(!Init) {
        Init = true;
        AddToHash();
    }

    tjs_uint *p = Hash.Find(name);
    if(!p)
        TVPThrowExceptionMessage(TVPStorageInArchiveNotFound, name,
                                 ArchiveName);

    return CreateStreamByIndex(*p);
}

//---------------------------------------------------------------------------
bool tTVPArchive::IsExistent(const ttstr &name) {
    if(name.IsEmpty())
        return false;

    if(!Init) {
        Init = true;
        AddToHash();
    }

    return Hash.Find(name) != nullptr;
}

//---------------------------------------------------------------------------
tjs_int tTVPArchive::GetFirstIndexStartsWith(const ttstr &prefix) {
    // returns first index which have 'prefix' at start of the name.
    // returns -1 if the target is not found.
    // the item must be sorted by ttstr::operator < , otherwise this
    // function will not work propertly.
    tjs_uint total_count = GetCount();
    tjs_int s = 0, e = total_count;
    while(e - s > 1) {
        tjs_int m = (e + s) / 2;
        if(!(GetName(m) < prefix)) {
            // m is after or at the target
            e = m;
        } else {
            // m is before the target
            s = m;
        }
    }

    // at this point, s or s+1 should point the target.
    // be certain.
    if(s >= (tjs_int)total_count)
        return -1; // out of the index
    if(GetName(s).StartsWith(prefix))
        return s;
    s++;
    if(s >= (tjs_int)total_count)
        return -1; // out of the index
    if(GetName(s).StartsWith(prefix))
        return s;
    return -1;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPArchiveCache
//---------------------------------------------------------------------------
class tTVPArchiveCache {
    typedef tTJSRefHolder<tTVPArchive> tHolder;
    tTJSHashCache<ttstr, tHolder> ArchiveCache;
    tTJSCriticalSection CS;

public:
    tTVPArchiveCache() : ArchiveCache(TVP_DEFAULT_ARCHIVE_CACHE_NUM) {}

    ~tTVPArchiveCache() = default;

    void SetMaxCount(tjs_int maxcount) {
        if(maxcount < 1)
            maxcount = 1;
        tTJSCSH csh(CS);
        ArchiveCache.SetMaxCount(maxcount);
    }

    void Clear() {
        // releases all elements
        tTJSCSH csh(CS);
        ArchiveCache.Clear();
    }

    tjs_uint GetCount() {
        tTJSCSH csh(CS);
        return ArchiveCache.GetCount();
    }

    tjs_uint GetMaxCount() {
        tTJSCSH csh(CS);
        return ArchiveCache.GetMaxCount();
    }

    tTVPArchive *Get(ttstr name) {
        name = TVPNormalizeStorageName(name);
        tTJSCSH csh(CS);
        tjs_uint32 hash = tTJSHashCache<ttstr, tHolder>::MakeHash(name);
        tHolder *ptr = ArchiveCache.FindAndTouchWithHash(name, hash);
        if(ptr) {
            return ptr->GetObject();
        }

        TVPAddLog(ttstr(TJS_W("(info) ArchiveCache miss: ")) + name);

        if(!TVPIsExistentStorageNoSearch(name)) {
            TVPThrowExceptionMessage(TVPCannotFindStorage, name);
        }

        tTVPArchive *arc = TVPOpenArchive(name, true);
        if(!arc) {
            TVPThrowExceptionMessage(TVPCannotFindStorage, name);
        }
        tHolder holder(arc);
        ArchiveCache.AddWithHash(name, hash, holder);
        return arc;
    }

private:
} TVPArchiveCache;

void TVPClearArchiveCache() { TVPArchiveCache.Clear(); }
tjs_uint TVPGetArchiveCacheCount() { return TVPArchiveCache.GetCount(); }
tjs_uint TVPGetArchiveCacheLimit() { return TVPArchiveCache.GetMaxCount(); }
void TVPSetArchiveCacheCount(tjs_uint max_count) {
    TVPArchiveCache.SetMaxCount((tjs_int)max_count);
}

static tTVPAtExit TVPClearArchiveCacheAtExit(TVP_ATEXIT_PRI_SHUTDOWN,
                                             TVPClearArchiveCache);
//---------------------------------------------------------------------------

static bool TVPIsRealStorageNoSearchNoNormalize(const ttstr &name) {
    const tjs_char *sharp_pos = TJS_strchr(name.c_str(), TVPArchiveDelimiter);
    if(sharp_pos) {
        ttstr arcname(name, (int)(sharp_pos - name.c_str()));

        tTVPArchive *arc = TVPArchiveCache.Get(arcname);
        bool ret;
        try {
            ttstr in_arc_name(sharp_pos + 1);
            tTVPArchive::NormalizeInArchiveStorageName(in_arc_name);
            ret = arc->IsExistent(in_arc_name);
        } catch(...) {
            arc->Release();
            throw;
        }
        arc->Release();
        return ret;
    }

    return TVPStorageMediaManager.CheckExistentStorage(name);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPIsExistentStorageNoSearch
//---------------------------------------------------------------------------
bool TVPIsExistentStorageNoSearchNoNormalize(const ttstr &name) {
    // does name contain > ?
    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);

    if(TVPIsRealStorageNoSearchNoNormalize(name))
        return true;

    if(TVPIsGfxEffectCompanionScript(name))
        return true;
    if(TVPIsGpuCompanionScript(name))
        return true;
    return false;
}

//---------------------------------------------------------------------------
bool TVPIsExistentStorageNoSearch(const ttstr &_name) {
    return TVPIsExistentStorageNoSearchNoNormalize(
        TVPNormalizeStorageName(_name));
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExtractStorageExt
//---------------------------------------------------------------------------
ttstr TVPExtractStorageExt(const ttstr &name) {
    // extract an extension from name.
    // returned string will contain extension delimiter ( '.' ),
    // except for missing extension of the input string. ( returns
    // nullptr string when input string does not have an extension )

    const tjs_char *s = name.c_str();
    tjs_int slen = name.GetLen();
    const tjs_char *p = s + slen;
    p--;
    while(p >= s) {
        if(*p == TJS_W('\\'))
            break;
        if(*p == TJS_W('/'))
            break;
        if(*p == TVPArchiveDelimiter)
            break;
        if(*p == TJS_W('.')) {
            // found extension delimiter
            size_t extlen = (slen - (p - s));
            return { p, extlen };
        }

        p--;
    }

    // not found
    return {};
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExtractStorageName
//---------------------------------------------------------------------------
ttstr TVPExtractStorageName(const ttstr &name) {
    // extract "name"'s storage name ( excluding path ) and return it.
    const tjs_char *s = name.c_str();
    tjs_int slen = name.GetLen();
    const tjs_char *p = s + slen;
    p--;
    while(p >= s) {
        if(*p == TJS_W('\\'))
            break;
        if(*p == TJS_W('/'))
            break;
        if(*p == TVPArchiveDelimiter)
            break;

        p--;
    }

    p++;
    if(p == s)
        return name;
    else
        return { p, (size_t)(slen - (p - s)) };
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExtractStoragePath
//---------------------------------------------------------------------------
ttstr TVPExtractStoragePath(const ttstr &name) {
    // extract "name"'s path ( including last delimiter ) and return
    // it.
    const tjs_char *s = name.c_str();
    tjs_int slen = name.GetLen();
    const tjs_char *p = s + slen;
    p--;
    while(p >= s) {
        if(*p == TJS_W('\\'))
            break;
        if(*p == TJS_W('/'))
            break;
        if(*p == TVPArchiveDelimiter)
            break;

        p--;
    }

    p++;
    return { s, (size_t)(p - s) };
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPChopStorageExt
//---------------------------------------------------------------------------
extern ttstr TVPChopStorageExt(const ttstr &name) {
    // chop storage's extension and return it.
    const tjs_char *s = name.c_str();
    tjs_int slen = name.GetLen();
    const tjs_char *p = s + slen;
    p--;
    while(p >= s) {
        if(*p == TJS_W('\\'))
            break;
        if(*p == TJS_W('/'))
            break;
        if(*p == TVPArchiveDelimiter)
            break;
        if(*p == TJS_W('.')) {
            // found extension delimiter
            return { s, (size_t)(p - s) };
        }

        p--;
    }

    // not found
    return name;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Auto search path support
//---------------------------------------------------------------------------
// Voice-heavy titles can mount millions of archive entries. With 1024
// buckets, a first lookup for each new voice name walks a chain containing
// thousands of entries and shows up as 30-40 ms in getExistVoice(). Keep the
// existing ordered chained table, but size its bucket array so those lookups
// remain effectively constant-time. The table nodes already dominate memory
// at that scale; the larger bucket array adds only a few MiB.
#define TVP_AUTO_PATH_HASH_SIZE 65536
std::vector<ttstr> TVPAutoPathList;
tTJSHashCache<ttstr, ttstr> TVPAutoPathCache(TVP_DEFAULT_AUTOPATH_CACHE_NUM);
tTJSHashTable<ttstr, ttstr, tTJSHashFunc<ttstr>, TVP_AUTO_PATH_HASH_SIZE>
    TVPAutoPathTable;
bool AutoPathTableInit = false;

//---------------------------------------------------------------------------
static void TVPInvalidateAutoPathTable() {
    TVPAutoPathTable.Clear();
    AutoPathTableInit = false;
}

//---------------------------------------------------------------------------
static void TVPClearAutoPathSearchCache() { TVPAutoPathCache.Clear(); }

//---------------------------------------------------------------------------
static void TVPClearAutoPathCache() {
    TVPAutoPathCache.Clear();
    TVPInvalidateAutoPathTable();
}

//---------------------------------------------------------------------------
struct tTVPClearAutoPathCacheCallback : public tTVPCompactEventCallbackIntf {
    void OnCompact(tjs_int level) override {
        if(level >= TVP_COMPACT_LEVEL_DEACTIVATE) {
            tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);
            TVPClearAutoPathSearchCache();
        }
    }
} static TVPClearAutoPathCacheCallback;

static bool TVPClearAutoPathCacheCallbackInit = false;

static bool TVPGetProjectRelativeAutoPath(const ttstr &path,
                                          ttstr &relative) {
    if(TVPProjectDir.IsEmpty() ||
       TJS_strchr(path.c_str(), TVPArchiveDelimiter))
        return false;

    ttstr projectRoot = TVPNormalizeStorageName(
        FixMissingPathDelimiter(TVPProjectDir));
    if(path.GetLen() <= projectRoot.GetLen() ||
       !path.StartsWith(projectRoot))
        return false;

    relative = path.SubString(projectRoot.GetLen(),
                              path.GetLen() - projectRoot.GetLen());
    if(relative.IsEmpty() ||
       TJS_strchr(relative.c_str(), TVPArchiveDelimiter))
        return false;

    tTVPArchive::NormalizeInArchiveStorageName(relative);
    return !relative.IsEmpty();
}

static bool TVPArchiveAutoPathMatches(const ttstr &path,
                                      const ttstr &relative) {
    return TVPArchiveAutoPathDirectoryMatches(
        std::u16string_view(
            reinterpret_cast<const char16_t *>(path.c_str()),
            static_cast<size_t>(path.GetLen())),
        std::u16string_view(
            reinterpret_cast<const char16_t *>(relative.c_str()),
            static_cast<size_t>(relative.GetLen())),
        static_cast<char16_t>(TVPArchiveDelimiter));
}

static ttstr TVPFindExactArchiveAutoPath(const ttstr &normalized) {
    ttstr relative;
    if(!TVPGetProjectRelativeAutoPath(normalized, relative))
        return {};

    const ttstr relativeDirectory = TVPExtractStoragePath(relative);
    if(relativeDirectory.IsEmpty())
        return {};

    const ttstr storageName = TVPExtractStorageName(relative);
    // Auto paths use Kirikiri's last-added-path-wins ordering.  Preserve it
    // while restricting candidates to the directory explicitly requested by
    // the script.
    for(auto it = TVPAutoPathList.rbegin(); it != TVPAutoPathList.rend();
        ++it) {
        if(!TVPArchiveAutoPathMatches(*it, relativeDirectory))
            continue;
        const ttstr candidate = *it + storageName;
        if(TVPIsRealStorageNoSearchNoNormalize(candidate))
            return candidate;
    }
    return {};
}

// Older KAG save screens stored their continue/quick-save/slot screenshots as
// BMPs, while newer scripts may probe the same basename with a JPG suffix.
// Keep this compatibility rule at storage resolution so an existence probe
// and the subsequent graphic load observe the same legacy file.  The
// basename and savedata directory checks are deliberately narrow; ordinary
// game resources must retain the normal explicit-extension semantics.
static bool TVPStorageNameLooksLegacySaveThumbnail(const ttstr &name) {
    const ttstr file_name = TVPExtractStorageName(name);
    std::string file = TVPChopStorageExt(file_name).AsStdString();
    std::transform(file.begin(), file.end(), file.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });

    bool known_save_name = file == "data_continue";
    if(!known_save_name && file.rfind("data_quick_", 0) == 0) {
        const std::string slot = file.substr(std::strlen("data_quick_"));
        known_save_name = !slot.empty() &&
            std::all_of(slot.begin(), slot.end(),
                        [](unsigned char ch) { return std::isdigit(ch); });
    }
    if(!known_save_name && file.rfind("data_", 0) == 0) {
        const std::size_t separator = file.find('_', 5);
        if(separator > 5 && separator + 1 < file.size()) {
            const std::string save = file.substr(5, separator - 5);
            const std::string slot = file.substr(separator + 1);
            known_save_name =
                std::all_of(save.begin(), save.end(),
                            [](unsigned char ch) { return std::isdigit(ch); }) &&
                std::all_of(slot.begin(), slot.end(),
                            [](unsigned char ch) { return std::isdigit(ch); });
        }
    }
    if(!known_save_name)
        return false;

    std::string path = TVPExtractStoragePath(name).AsStdString();
    std::transform(path.begin(), path.end(), path.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return path.find("savedata") != std::string::npos;
}

ttstr TVPFindLegacySaveThumbnail(const ttstr &name) {
    if(!TVPStorageNameLooksLegacySaveThumbnail(name))
        return {};

    const ttstr normalized = TVPNormalizeStorageName(name);

    const ttstr ext = TVPExtractStorageExt(normalized);
    if(ext.IsEmpty())
        return {}; // extensionless requests already use graphic guessing

    static constexpr const char *raster_extensions[] = {
        ".bmp",  ".jpg",  ".jpeg", ".png",  ".tlg",  ".tlg5", ".tlg6",
        ".webp", ".jxr",  ".bpg",  ".pvr",  ".jif",  ".dib",  ".amv",
    };
    std::string requested_ext = ext.AsStdString();
    std::transform(requested_ext.begin(), requested_ext.end(),
                   requested_ext.begin(), [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    bool known_raster = false;
    for(const char *candidate : raster_extensions) {
        if(requested_ext == candidate) {
            known_raster = true;
            break;
        }
    }
    if(!known_raster)
        return {}; // never alias a save-state or metadata file to an image

    const ttstr stem = TVPChopStorageExt(normalized);
    for(const char *extension : raster_extensions) {
        const ttstr candidate = stem + ttstr(extension);
        if(candidate == normalized ||
           !TVPIsRealStorageNoSearchNoNormalize(candidate))
            continue;
        return candidate;
    }
    return {};
}

//---------------------------------------------------------------------------
void TVPAddAutoPath(const ttstr &name) {
    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);

    ttstr fixedName = FixMissingPathDelimiter(name);
    ttstr normalized = TVPNormalizeStorageName(fixedName);

    // Sibling XP3 archives are mounted before startup.tjs runs. When a game
    // later adds a project-relative search path such as system/ or main/,
    // mirror that ordering onto matching directories inside those archives.
    // This preserves Kirikiri's last-added-path-wins behavior while keeping
    // loose project files above their archived counterparts.
    std::vector<ttstr> archivedPeers;
    ttstr relative;
    if(TVPGetProjectRelativeAutoPath(normalized, relative)) {
        for(auto it = TVPAutoPathList.begin(); it != TVPAutoPathList.end();) {
            if(TVPArchiveAutoPathMatches(*it, relative)) {
                archivedPeers.push_back(*it);
                it = TVPAutoPathList.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto i =
        std::find(TVPAutoPathList.begin(), TVPAutoPathList.end(), normalized);
    const bool moved = i != TVPAutoPathList.end();
    if(moved)
        TVPAutoPathList.erase(i);
    TVPAutoPathList.insert(TVPAutoPathList.end(), archivedPeers.begin(),
                           archivedPeers.end());
    TVPAutoPathList.push_back(normalized);

    if(TVPStorageTraceEnabled() && TVPStorageTraceName(normalized)) {
        spdlog::info(
            "StorageTrace addAutoPath request={} normalized={} moved={} archive_peers={} count={}",
            name.AsStdString(), normalized.AsStdString(), moved,
            archivedPeers.size(), TVPAutoPathList.size());
    }

    TVPClearAutoPathCache();
}

//---------------------------------------------------------------------------
void TVPRemoveAutoPath(const ttstr &name) {
    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);

    ttstr fixedName = FixMissingPathDelimiter(name);
    ttstr normalized = TVPNormalizeStorageName(fixedName);

    auto i =
        std::find(TVPAutoPathList.begin(), TVPAutoPathList.end(), normalized);
    if(i != TVPAutoPathList.end())
        TVPAutoPathList.erase(i);

    TVPClearAutoPathCache();
}

//---------------------------------------------------------------------------
static tjs_uint TVPRebuildAutoPathTable() {
    // rebuild auto path table
    if(AutoPathTableInit)
        return 0;

    // Storage probes may be triggered by log callbacks while building this
    // table. Let the nested lookup use entries collected so far rather than
    // recursively clearing and rebuilding the same table.
    static thread_local bool rebuilding = false;
    if(rebuilding)
        return 0;
    rebuilding = true;
    struct tRebuildGuard {
        bool &active;
        ~tRebuildGuard() { active = false; }
    } guard{ rebuilding };

    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);

    TVPAutoPathTable.Clear();

    tjs_uint64 tick = TVPGetTickCount();
    TVPAddLog((const tjs_char *)TVPInfoRebuildingAutoPath);

    tjs_uint totalcount = 0;

    std::vector<ttstr>::iterator it;
    for(it = TVPAutoPathList.begin(); it != TVPAutoPathList.end(); it++) {
        const ttstr &path = *it;
        tjs_uint count = 0;

        const tjs_char *sharp_pos =
            TJS_strchr(path.c_str(), TVPArchiveDelimiter);
        if(sharp_pos) {
            // this storagename indicates a file in an archive

            ttstr arcname(path, (int)(sharp_pos - path.c_str()));
            ttstr in_arc_name(sharp_pos + 1);
            tTVPArchive::NormalizeInArchiveStorageName(in_arc_name);
            tjs_int in_arc_name_len = in_arc_name.GetLen();

            tTVPArchive *arc = nullptr;
            try {
                arc = TVPArchiveCache.Get(arcname);
            } catch(...) {
                TVPAddLog(ttstr(TJS_W("(warning) Cannot open archive: ")) + arcname);
                continue;
            }
            if(!arc) continue;

            try {
                tjs_uint storagecount = arc->GetCount();

                tjs_int i = arc->GetFirstIndexStartsWith(in_arc_name);
                if(i != -1) {
                    for(; i < (tjs_int)storagecount; i++) {
                        ttstr name = arc->GetName(i);
                        tTVPArchive::NormalizeInArchiveStorageName(name);

                        if(name.StartsWith(in_arc_name)) {
                            if(!TJS_strchr(name.c_str() + in_arc_name_len,
                                           TJS_W('/'))) {
                                ttstr sname = TVPExtractStorageName(name);
                                TVPAutoPathTable.Add(sname, path);
                                if(TVPStorageTraceEnabled() &&
                                   TVPStorageTraceName(sname)) {
                                    spdlog::info(
                                        "StorageTrace table archive short={} path={} full={}",
                                        sname.AsStdString(), path.AsStdString(),
                                        name.AsStdString());
                                }
                                count++;
                            }
                        } else {
                            break;
                        }
                    }
                }
            } catch(...) {
                arc->Release();
                throw;
            }
            arc->Release();
        } else {
            // normal folder
            class tLister : public iTVPStorageLister {
            public:
                std::vector<ttstr> list;

                void Add(const ttstr &file) override { list.push_back(file); }
            } lister;

            TVPStorageMediaManager.GetListAt(path, &lister);
            for(auto &i : lister.list) {
                TVPAutoPathTable.Add(i, path);
                if(TVPStorageTraceEnabled() && TVPStorageTraceName(i)) {
                    spdlog::info("StorageTrace table folder short={} path={}",
                                 i.AsStdString(), path.AsStdString());
                }
                count++;
            }
        }

        //		TVPAddLog(ttstr(TJS_W("(info) Path ")) + path +
        // TJS_W("
        // contains ")
        //+ 			ttstr((tjs_int)count) + TJS_W(" file(s)."));

        totalcount += count;
    }

    tjs_uint64 endtick = TVPGetTickCount();

    TVPAddLog(ttstr(TJS_W("(info) Total ")) + ttstr((tjs_int)totalcount) +
              TJS_W(" file(s) found, ") +
              ttstr((tjs_int)TVPAutoPathTable.GetCount()) +
              TJS_W(" file(s) activated.") + TJS_W(" (") +
              ttstr((tjs_int)(endtick - tick)) + TJS_W("ms)"));

    AutoPathTableInit = true;

    return totalcount;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetPlacedPath
//---------------------------------------------------------------------------
ttstr TVPGetPlacedPath(const ttstr &name) {
    // search path and return the path which the "name" is placed.
    // returned name is normalized. returns empty string if the
    // storage is not found.
#if 0 // needn't
    if(!TVPClearAutoPathCacheCallbackInit)
    {
        TVPAddCompactEventHook(&TVPClearAutoPathCacheCallback);
        TVPClearAutoPathCacheCallbackInit = true;
    }
#endif

    // Check for internal plugins registered via NCB even when no physical
    // file exists on disk. Motion/emote scripts use getPlacedPath() through
    // CanLoadPlugin(), so internal modules must resolve here as well.
    {
        ttstr storage = TVPExtractStorageName(name).AsLowerCase();
        if(!storage.IsEmpty() &&
           (TVPRegisteredPlugins.find(storage) != TVPRegisteredPlugins.end() ||
            ncbAutoRegister::HasModule(storage))) {
            return TVPNormalizeStorageName(name);
        }
    }

    ttstr normalized(TVPNormalizeStorageName(name));

    if(TVPIsSplitEmoteVirtualStorage(name)) {
        if(TVPStorageTraceEnabled() && TVPStorageTraceName(name)) {
            spdlog::info("StorageTrace virtual split-emote request={} normalized={}",
                         name.AsStdString(), normalized.AsStdString());
        }
        return normalized;
    }

    // Some Yuzusoft scripts probe motion_<asset>.psb.tjs via
    // Storages.isExistentStorage() before Scripts.evalStorage(). Handle the
    // virtual companion before consulting the auto path cache, otherwise the
    // first failed physical lookup can poison the cache with a miss.
    ttstr motionSourceName;
    if(TVPGetMotionParameterCompanionInfo(name, &motionSourceName)) {
        bool motionSourceExists = false;
        ttstr sourceInSamePath = TVPExtractStoragePath(normalized) +
            motionSourceName;
        if(!sourceInSamePath.IsEmpty() &&
           TVPIsRealStorageNoSearchNoNormalize(sourceInSamePath)) {
            motionSourceExists = true;
        } else {
            TVPRebuildAutoPathTable();
            motionSourceExists = TVPAutoPathTable.Find(motionSourceName) !=
                nullptr;
        }
        if(motionSourceExists) {
            TVPAutoPathCache.Add(name, normalized);
            if(TVPStorageTraceEnabled() && TVPStorageTraceName(name)) {
                spdlog::info(
                    "StorageTrace virtual motion-parameter request={} source={} normalized={}",
                    name.AsStdString(), motionSourceName.AsStdString(),
                    normalized.AsStdString());
            }
            return normalized;
        }
    }

    // Give private compatibility plug-ins one chance to translate a logical
    // storage name (for example a PackinOne virtual UI atlas) to a concrete
    // archive entry. This runs before the placement lock and miss cache: a
    // resolver may safely probe the concrete candidate through normal storage
    // APIs, and a previous miss cannot hide a later plugin registration.
    ttstr resolved;
    if(TVPResolveStorageName(name, resolved)) {
        ttstr placed = TVPGetPlacedPath(resolved);
        if(!placed.IsEmpty()) {
            TVPAutoPathCache.Add(name, placed);
            if(TVPStorageTraceEnabled() && TVPStorageTraceName(name)) {
                spdlog::info(
                    "StorageTrace resolver request={} resolved={} placed={}",
                    name.AsStdString(), resolved.AsStdString(),
                    placed.AsStdString());
            }
            return placed;
        }
    }

    ttstr *incache = TVPAutoPathCache.FindAndTouch(name);
    if(incache) {
        if(TVPStorageTraceEnabled() && TVPStorageTraceName(name)) {
            spdlog::info("StorageTrace cache request={} result={}",
                         name.AsStdString(), incache->AsStdString());
        }
        if(*incache == TVP_AUTOPATH_CACHE_MISS_MARKER)
            return {};
        return *incache; // found in cache
    }

    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);

    bool found = TVPIsRealStorageNoSearchNoNormalize(normalized);
    if(found) {
        // found in current folder
        TVPAutoPathCache.Add(name, normalized);
        if(TVPStorageTraceEnabled() && TVPStorageTraceName(name)) {
            spdlog::info("StorageTrace direct request={} normalized={}",
                         name.AsStdString(), normalized.AsStdString());
        }
        return normalized;
    }

    // Preserve the old save-thumbnail convention when a script explicitly
    // asks for a missing raster suffix (for example data_quick_01.jpg) but
    // only the same-stem BMP is present in savedata/. This must happen before
    // the normal auto-path miss is cached, otherwise the failed JPG probe
    // would prevent the legacy candidate from being considered later.
    if(ttstr legacy = TVPFindLegacySaveThumbnail(normalized);
       !legacy.IsEmpty()) {
        if(TVPStorageTraceEnabled() && TVPStorageTraceName(name)) {
            spdlog::info(
                "StorageTrace legacy save thumbnail request={} resolved={}",
                name.AsStdString(), legacy.AsStdString());
        }
        return legacy;
    }

    // A normalized project-relative path cannot be opened directly when the
    // project is backed by a sibling XP3.  Before falling back to the legacy
    // short-name auto-path table, try the same directory inside mounted
    // archives.  This prevents identically named portrait/standing resources
    // in different directories from shadowing one another.
    if(ttstr exactArchivePath = TVPFindExactArchiveAutoPath(normalized);
       !exactArchivePath.IsEmpty()) {
        TVPAutoPathCache.Add(name, exactArchivePath);
        if(TVPStorageTraceEnabled() && TVPStorageTraceName(name)) {
            spdlog::info(
                "StorageTrace exact-archive request={} normalized={} found={}",
                name.AsStdString(), normalized.AsStdString(),
                exactArchivePath.AsStdString());
        }
        return exactArchivePath;
    }

    // not found in current folder
    // search through auto path table

    ttstr storagename = TVPExtractStorageName(normalized);

    TVPRebuildAutoPathTable(); // ensure auto path table
    ttstr *result = TVPAutoPathTable.Find(storagename);
    if(result) {
        // found in table
        ttstr found = *result + storagename;
        TVPAutoPathCache.Add(name, found);
        if(TVPStorageTraceEnabled() && TVPStorageTraceName(name)) {
            spdlog::info(
                "StorageTrace table-hit request={} short={} base={} found={}",
                name.AsStdString(), storagename.AsStdString(),
                result->AsStdString(), found.AsStdString());
        }
        return found;
    }

    // Older E-mote scene scripts can route through AffineSourceMotion even
    // though the package only contains the DirectX-exported PSB. Those
    // scripts probe the logical, unprefixed name first and, when compiled to
    // TJS bytecode, cannot be amended by the source-level compatibility
    // patch. Match libgame's D3D resource lookup by resolving a missing
    // <name>.psb to dx_<name>.psb (or the low-spec export), while preserving
    // an actual unprefixed file when one exists.
    if(TVPIsUnprefixedD3DEmoteStorage(storagename)) {
        const ttstr storagePath = TVPExtractStoragePath(normalized);
        const ttstr aliases[] = {
            ttstr(TJS_W("dx_")) + storagename,
            ttstr(TJS_W("dxlow_")) + storagename,
        };
        for(const auto &alias : aliases) {
            ttstr found;
            const ttstr inSamePath = storagePath + alias;
            if(!inSamePath.IsEmpty() &&
               TVPIsRealStorageNoSearchNoNormalize(inSamePath)) {
                found = inSamePath;
            } else if(ttstr *aliasPath = TVPAutoPathTable.Find(alias)) {
                found = *aliasPath + alias;
            }
            if(found.IsEmpty()) {
                continue;
            }

            TVPAutoPathCache.Add(name, found);
            if(TVPStorageTraceEnabled() && TVPStorageTraceName(name)) {
                spdlog::info(
                    "StorageTrace d3d-emote-alias request={} short={} "
                    "alias={} found={}",
                    name.AsStdString(), storagename.AsStdString(),
                    alias.AsStdString(), found.AsStdString());
            }
            return found;
        }
    }

    if(TVPIsD3DEmoteCompanionScript(name) ||
       TVPIsLogWindowCompanionScript(name) ||
       TVPIsGfxEffectCompanionScript(name) || TVPIsGpuCompanionScript(name))
        return normalized;

    motionSourceName.Clear();
    if(TVPGetMotionParameterCompanionInfo(name, &motionSourceName)) {
        bool motionSourceExists = false;
        ttstr sourceInSamePath = TVPExtractStoragePath(normalized) +
            motionSourceName;
        if(!sourceInSamePath.IsEmpty() &&
           TVPIsRealStorageNoSearchNoNormalize(sourceInSamePath)) {
            motionSourceExists = true;
        } else {
            motionSourceExists = TVPAutoPathTable.Find(motionSourceName) !=
                nullptr;
        }
        if(motionSourceExists) {
            TVPAutoPathCache.Add(name, normalized);
            if(TVPStorageTraceEnabled() && TVPStorageTraceName(name)) {
                spdlog::info(
                    "StorageTrace virtual motion-parameter request={} source={} normalized={}",
                    name.AsStdString(), motionSourceName.AsStdString(),
                    normalized.AsStdString());
            }
            return normalized;
        }
    }

    // not found
    TVPAutoPathCache.Add(name, TVP_AUTOPATH_CACHE_MISS_MARKER);
    if(TVPStorageTraceEnabled() && TVPStorageTraceName(name)) {
        spdlog::info("StorageTrace miss request={} short={}",
                     name.AsStdString(), storagename.AsStdString());
    }
    return {};
}
//---------------------------------------------------------------------------
// TVPSearchPlacedPath
//---------------------------------------------------------------------------
ttstr TVPSearchPlacedPath(const ttstr &name) {
    ttstr place = TVPGetPlacedPath(name);
    if(place.IsEmpty())
        TVPThrowExceptionMessage(TVPCannotFindStorage, name);
    return place;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPIsExistentStorage
//---------------------------------------------------------------------------
bool TVPIsExistentStorage(const ttstr &name) {
    if(TVPIsVirtualSolidVectorStorage(name)) {
        if(TVPStorageTraceEnabled() && TVPStorageTraceName(name)) {
            spdlog::info("StorageTrace virtual solid-vector exists request={}",
                         name.AsStdString());
        }
        return true;
    }
    if(!TVPGetPlacedPath(name).IsEmpty())
        return true;
    if(TVPIsSplitEmoteVirtualStorage(name))
        return true;
    ttstr pure = TVPExtractStorageName(name);
    if(pure.GetLen() > 4) {
        ttstr ext = ttstr(pure.c_str() + pure.GetLen() - 4).AsLowerCase();
        if(ext == TJS_W(".dll") || ext == TJS_W(".tpm"))
            return ncbAutoRegister::HasModule(pure);
    }

    // Bridge motion parameter checks used by some scripts:
    // motion_<name>.tjs should be treated as existent when the actual
    // <name>.mtn/.psb resource exists in storage.
    const auto storageName = TVPExtractStorageName(name).AsStdString();
    if(storageName.size() > 11) {
        std::string lower = storageName;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });
        if(lower.rfind("motion_", 0) == 0 &&
           lower.substr(lower.size() - 4) == ".tjs") {
            const auto inner = storageName.substr(7, storageName.size() - 11);
            const auto dot = inner.rfind('.');
            if(dot != std::string::npos) {
                std::string ext = inner.substr(dot);
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char ch) {
                                   return static_cast<char>(std::tolower(ch));
                               });
                if((ext == ".mtn" || ext == ".psb") &&
                   !TVPGetPlacedPath(ttstr(inner.c_str())).IsEmpty()) {
                    return true;
                }
            }
        }
    }
    return false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateStream
//---------------------------------------------------------------------------
static tTJSBinaryStream *_TVPCreateStream(const ttstr &_name,
                                          tjs_uint32 flags) {
    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);

    if(std::getenv("AETHERKIRI_STORAGE_TRACE") != nullptr &&
       _name.AsStdString().find("langselect_auto.func") != std::string::npos)
        spdlog::info("StorageTrace create-stream request={} flags={}",
                     _name.AsStdString(), flags);

    ttstr name;

    tjs_uint32 access = flags & TJS_BS_ACCESS_MASK;
    if(access == TJS_BS_WRITE || access == TJS_BS_APPEND ||
       access == TJS_BS_UPDATE)
        name = TVPNormalizeStorageName(_name);
    else
        name = TVPGetPlacedPath(_name); // file must exist

    if(std::getenv("AETHERKIRI_STORAGE_TRACE") != nullptr &&
       _name.AsStdString().find("langselect_auto.func") != std::string::npos)
        spdlog::info("StorageTrace create-stream placed={} access={}",
                     name.AsStdString(), access);

    if(TVPSaveTraceEnabled() && access != TJS_BS_READ) {
        spdlog::info("SaveTrace TVPCreateStream request={} normalized={} flags={} access={}",
                     _name.AsStdString(), name.AsStdString(), flags, access);
    }

    if(name.IsEmpty()) {
        if(access >= 1)
            TVPRemoveFromStorageCache(_name);
        TVPThrowExceptionMessage(TVPCannotOpenStorage, _name);
    }

    if(access == TJS_BS_READ && TVPIsGfxEffectCompanionScript(name) &&
       !TVPIsRealStorageNoSearchNoNormalize(name))
        return TVPOpenGfxEffectCompanionScript();
    if(access == TJS_BS_READ && TVPIsD3DEmoteCompanionScript(name) &&
       !TVPIsRealStorageNoSearchNoNormalize(name))
        return TVPOpenD3DEmoteCompanionScript();
    if(access == TJS_BS_READ && TVPIsLogWindowCompanionScript(name) &&
       !TVPIsRealStorageNoSearchNoNormalize(name))
        return TVPOpenLogWindowCompanionScript();
    if(access == TJS_BS_READ && TVPIsGpuCompanionScript(name) &&
       !TVPIsRealStorageNoSearchNoNormalize(name))
        return TVPOpenGpuCompanionScript();
    if(access == TJS_BS_READ && TVPIsSplitEmoteVirtualStorage(name) &&
       !TVPIsRealStorageNoSearchNoNormalize(name)) {
        if(TVPStorageTraceEnabled() && TVPStorageTraceName(name)) {
            spdlog::info("StorageTrace open virtual split-emote: {}",
                         name.AsStdString());
        }
        return new tTVPMemoryStream();
    }
    ttstr motionSourceName;
    if(access == TJS_BS_READ &&
       TVPGetMotionParameterCompanionInfo(name, &motionSourceName) &&
       !TVPIsRealStorageNoSearchNoNormalize(name)) {
        if(TVPStorageTraceEnabled() && TVPStorageTraceName(name)) {
            spdlog::info(
                "StorageTrace open virtual motion-parameter: {} source={}",
                name.AsStdString(), motionSourceName.AsStdString());
        }
        return TVPOpenMotionParameterCompanionScript(motionSourceName);
    }

    // does name contain > ?
    const tjs_char *sharp_pos = TJS_strchr(name.c_str(), TVPArchiveDelimiter);
    if(sharp_pos) {
        // this storagename indicates a file in an archive
        if((flags & TJS_BS_ACCESS_MASK) != TJS_BS_READ)
            TVPThrowExceptionMessage(TVPCannotWriteToArchive);

        ttstr arcname(name, (int)(sharp_pos - name.c_str()));

        tTVPArchive *arc;
        tTJSBinaryStream *stream;
        arc = TVPArchiveCache.Get(arcname);
        try {
            ttstr in_arc_name(sharp_pos + 1);
            tTVPArchive::NormalizeInArchiveStorageName(in_arc_name);
            stream = arc->CreateStream(in_arc_name);
        } catch(...) {
            arc->Release();
            if(access >= 1)
                TVPRemoveFromStorageCache(_name);
            throw;
        }
        if(access >= 1)
            TVPRemoveFromStorageCache(_name);
        arc->Release();
        return stream;
    }

    tTJSBinaryStream *stream;
    try {
        stream = TVPStorageMediaManager.Open(name, flags);
    } catch(...) {
        if(access >= 1)
            TVPRemoveFromStorageCache(_name);
        throw;
    }
    if(access >= 1)
        TVPRemoveFromStorageCache(_name);
    return stream;
}

tTJSBinaryStream *TVPCreateStream(const ttstr &_name, tjs_uint32 flags) {
    try {
        return _TVPCreateStream(_name, flags);
    } catch(eTJSScriptException &e) {
        if(TJS_strchr(_name.c_str(), '#'))
            e.AppendMessage(
                TJS_W("[") +
                TVPFormatMessage(TVPFilenameContainsSharpWarn, _name) +
                TJS_W("]"));
        throw;
    } catch(eTJSScriptError &e) {
        if(TJS_strchr(_name.c_str(), '#'))
            e.AppendMessage(
                TJS_W("[") +
                TVPFormatMessage(TVPFilenameContainsSharpWarn, _name) +
                TJS_W("]"));
        throw;
    } catch(eTJSError &e) {
        if(TJS_strchr(_name.c_str(), '#'))
            e.AppendMessage(
                TJS_W("[") +
                TVPFormatMessage(TVPFilenameContainsSharpWarn, _name) +
                TJS_W("]"));
        throw;
    } catch(...) {
        // check whether the filename contains '#' (former delimiter
        // for archive filename before 2.19 beta 14)
        if(TJS_strchr(_name.c_str(), '#'))
            TVPAddLog(TVPFormatMessage(TVPFilenameContainsSharpWarn, _name));
        throw;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPClearStorageCaches
//---------------------------------------------------------------------------
void TVPClearStorageCaches() {
    TVPClearXP3SegmentCache();
    TVPClearAutoPathCache();
}

void TVPResetAutoPathsForGameSession() {
    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);
    TVPAutoPathList.clear();
    TVPClearXP3SegmentCache();
    TVPClearAutoPathCache();
}
//---------------------------------------------------------------------------

void TVPSetAutoPathCacheMaxCount(tjs_uint max_count) {
    if(max_count < 1)
        max_count = 1;
    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);
    TVPAutoPathCache.SetMaxCount(max_count);
}

tjs_uint TVPGetAutoPathCacheCount() {
    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);
    return TVPAutoPathCache.GetCount();
}

tjs_uint TVPGetAutoPathCacheLimit() {
    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);
    return TVPAutoPathCache.GetMaxCount();
}

tjs_uint TVPGetAutoPathTableCount() {
    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);
    return TVPAutoPathTable.GetCount();
}

void TVPRemoveFromStorageCache(const ttstr &name) {
    TVPAutoPathCache.Delete(name);
}

//---------------------------------------------------------------------------
// tTJSNC_Storages
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Storages::ClassID = -1;

tTJSNC_Storages::tTJSNC_Storages() :
    inherited(TJS_W("Storages")){
        // registration of native members

        TJS_BEGIN_NATIVE_MEMBERS(Storages) TJS_DECL_EMPTY_FINALIZE_METHOD
            //----------------------------------------------------------------------

            //-- methods

            //----------------------------------------------------------------------
            TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ addAutoPath){
                if(numparams < 1) return TJS_E_BADPARAMCOUNT;

ttstr path = *param[0];

TVPAddAutoPath(path);

if(result)
    result->Clear();

return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ addAutoPath)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ addAutoToolsPath) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr path = *param[0];
    TVPAddAutoPath(path);

    if(result)
        result->Clear();

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ addAutoToolsPath)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ addArchive) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr path = *param[0];
    TVPAddAutoPath(path);

    if(result)
        result->Clear();

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ addArchive)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setDefaultPath) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr path = *param[0];
    TVPAddAutoPath(path);
    TVPSetCurrentDirectory(path);

    if(result)
        result->Clear();

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ setDefaultPath)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ removeAutoPath) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr path = *param[0];

    TVPRemoveAutoPath(path);

    if(result)
        result->Clear();

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ removeAutoPath)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getFullPath) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr path = *param[0];

    if(result)
        *result = TVPNormalizeStorageName(path);

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getFullPath)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getPlacedPath) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr path = *param[0];

    if(result)
        *result = TVPGetPlacedPath(path);

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getPlacedPath)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ isExistentStorage) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr path = *param[0];

    if(result)
        *result = (tjs_int)TVPIsExistentStorage(path);

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ isExistentStorage)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ isExistentStorageNoSearchNoNormalize) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr path = *param[0];

    if(result)
        *result =
            (tjs_int)TVPIsExistentStorageNoSearchNoNormalize(path);

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ isExistentStorageNoSearchNoNormalize)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(archiveUniqueKey) {
    TJS_BEGIN_NATIVE_PROP_GETTER {
        if(TVPStoragesArchiveUniqueKeyCompat.Type() == tvtVoid) {
            iTJSDispatch2 *array = TJSCreateArrayObject();
            if(!array)
                return TJS_E_FAIL;
            TVPStoragesArchiveUniqueKeyCompat = tTJSVariant(array, array);
            array->Release();
        }
        *result = TVPStoragesArchiveUniqueKeyCompat;
        return TJS_S_OK;
    }
    TJS_END_NATIVE_PROP_GETTER

    TJS_BEGIN_NATIVE_PROP_SETTER {
        TVPStoragesArchiveUniqueKeyCompat = *param;
        return TJS_S_OK;
    }
    TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(archiveUniqueKey)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ extractStorageExt) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr path = *param[0];

    if(result)
        *result = TVPExtractStorageExt(path);

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ extractStorageExt)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ extractStorageName) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr path = *param[0];

    if(result)
        *result = TVPExtractStorageName(path);

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ extractStorageName)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ extractStoragePath) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr path = *param[0];

    if(result)
        *result = TVPExtractStoragePath(path);

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ extractStoragePath)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ chopStorageExt) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr path = *param[0];

    if(result)
        *result = TVPChopStorageExt(path);

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ chopStorageExt)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ clearArchiveCache) {
    TVPClearArchiveCache();
    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ clearArchiveCache)
//----------------------------------------------------------------------
TJS_END_NATIVE_MEMBERS
}

//---------------------------------------------------------------------------
tTJSNativeInstance *tTJSNC_Storages::CreateNativeInstance() {
    // this class cannot create an instance
    TVPThrowExceptionMessage(TVPCannotCreateInstance);

    return nullptr;
}
//---------------------------------------------------------------------------
