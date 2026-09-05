#include "DebugIntf.h"
#include "MsgIntf.h"
#include "StorageIntf.h"
#include "ncbind.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <zlib.h>

#if defined(AETHERKIRI_INTERNAL_LEGACY_PLUGINS)
extern "C" tTJSBinaryStream *
AetherInternalWrapLz4ReadStream(tTJSBinaryStream *source);
#endif

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

namespace {

void setBoolResult(tTJSVariant *result, bool value = true) {
    if(result)
        *result = value;
}

void setIntResult(tTJSVariant *result, tjs_int value = 0) {
    if(result)
        *result = value;
}

ttstr paramString(tjs_int index, tjs_int numparams, tTJSVariant **param,
                  const tjs_char *fallback = TJS_W("")) {
    if(index < numparams && param && param[index] &&
       param[index]->Type() != tvtVoid)
        return param[index]->AsStringNoAddRef();
    return ttstr(fallback);
}

bool paramBool(tjs_int index, tjs_int numparams, tTJSVariant **param,
               bool fallback = false) {
    if(index < numparams && param && param[index] &&
       param[index]->Type() != tvtVoid)
        return static_cast<bool>(*param[index]);
    return fallback;
}

tjs_int paramInt(tjs_int index, tjs_int numparams, tTJSVariant **param,
                 tjs_int fallback = 0) {
    if(index < numparams && param && param[index] &&
       param[index]->Type() != tvtVoid)
        return static_cast<tjs_int>(*param[index]);
    return fallback;
}

std::string toUtf8(const ttstr &value) { return value.AsStdString(); }

std::vector<tjs_uint8> variantBytes(const tTJSVariant &value) {
    if(value.Type() == tvtOctet) {
        tTJSVariantOctet *octet = value.AsOctetNoAddRef();
        const auto *data =
            reinterpret_cast<const tjs_uint8 *>(octet->GetData());
        return std::vector<tjs_uint8>(data, data + octet->GetLength());
    }

    std::string text = toUtf8(value.AsStringNoAddRef());
    return std::vector<tjs_uint8>(text.begin(), text.end());
}

void setOctetResult(tTJSVariant *result, const std::vector<tjs_uint8> &bytes) {
    if(!result)
        return;
    tTJSVariantOctet *octet = TJSAllocVariantOctet(
        bytes.empty() ? nullptr : bytes.data(),
        static_cast<tjs_uint>(bytes.size()));
    *result = octet;
    octet->Release();
}

tjs_error TJS_INTF_METHOD returnTrueCb(tTJSVariant *result, tjs_int,
                                       tTJSVariant **, iTJSDispatch2 *) {
    setBoolResult(result, true);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD returnFalseCb(tTJSVariant *result, tjs_int,
                                        tTJSVariant **, iTJSDispatch2 *) {
    setBoolResult(result, false);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD returnZeroCb(tTJSVariant *result, tjs_int,
                                       tTJSVariant **, iTJSDispatch2 *) {
    setIntResult(result, 0);
    return TJS_S_OK;
}

void propSet(iTJSDispatch2 *dispatch, const tjs_char *name,
             const tTJSVariant &value) {
    if(dispatch && name)
        dispatch->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, dispatch);
}

void logOnce(const tjs_char *module, const tjs_char *message) {
    static std::vector<ttstr> emitted;
    ttstr key = ttstr(module) + TJS_W(":") + message;
    if(std::find(emitted.begin(), emitted.end(), key) != emitted.end())
        return;
    emitted.push_back(key);
    TVPAddLog(ttstr(TJS_W("AetherKiri compat ")) + module + TJS_W(": ") +
              message);
}

ttstr lzfsInnerPath(const ttstr &name) {
    const tjs_char *raw = name.c_str();
    const tjs_char *slash = TJS_strchr(raw, TJS_W('/'));
    ttstr path = slash ? ttstr(slash + 1) : name;
    while(path.GetLen() >= 2 && path[0] == TJS_W('.') &&
          (path[1] == TJS_W('/') || path[1] == TJS_W('\\'))) {
        path = ttstr(path.c_str() + 2);
    }
    return path;
}

class LzfsStorageMedia : public iTVPStorageMedia {
public:
    void AddRef() override { ++refCount_; }
    void Release() override {
        if(refCount_ == 1)
            delete this;
        else
            --refCount_;
    }

    void GetName(ttstr &name) override { name = TJS_W("lzfs"); }
    void NormalizeDomainName(ttstr &) override {}
    void NormalizePathName(ttstr &) override {}

    bool CheckExistentStorage(const ttstr &name) override {
        ttstr path = lzfsInnerPath(name);
        return !TVPGetPlacedPath(path).IsEmpty();
    }

    tTJSBinaryStream *Open(const ttstr &name, tjs_uint32 flags) override {
        ttstr path = lzfsInnerPath(name);
        logOnce(TJS_W("lzfs.dll"),
                TJS_W("mapping lzfs storage to AetherKiri Storage"));
        auto *source = TVPCreateStream(path, flags);
        if(!source || (flags & TJS_BS_ACCESS_MASK) != TJS_BS_READ)
            return source;
#if defined(AETHERKIRI_INTERNAL_LEGACY_PLUGINS)
        return AetherInternalWrapLz4ReadStream(source);
#else
        return source;
#endif
    }

    void GetListAt(const ttstr &, iTVPStorageLister *) override {}

    void GetLocallyAccessibleName(ttstr &name) override {
        name = TVPGetLocallyAccessibleName(lzfsInnerPath(name));
    }

private:
    virtual ~LzfsStorageMedia() = default;
    tjs_int refCount_ = 1;
};

LzfsStorageMedia *gLzfsMedia = nullptr;

void registerLzfsMedia() {
    if(gLzfsMedia)
        return;
    gLzfsMedia = new LzfsStorageMedia();
    TVPRegisterStorageMedia(gLzfsMedia);
#if defined(AETHERKIRI_INTERNAL_LEGACY_PLUGINS)
    logOnce(TJS_W("lzfs.dll"), TJS_W("registered LZ4 storage media"));
#else
    logOnce(TJS_W("lzfs.dll"), TJS_W("registered passthrough storage media"));
#endif
}

void unregisterLzfsMedia() {
    if(!gLzfsMedia)
        return;
    TVPUnregisterStorageMedia(gLzfsMedia);
    gLzfsMedia->Release();
    gLzfsMedia = nullptr;
}

} // namespace

// Extra modules kept by AetherKiri or mobile/legacy games. These are not a
// direct krkrsdl3 copy: each module either maps onto current core behavior or
// reports an explicit compatibility surface when the old backend is unavailable.

#define NCB_MODULE_NAME TJS_W("zlib.dll")

class ZlibCompat {
public:
    ttstr getVersion() const { return ttstr(zlibVersion()); }

    static tjs_error TJS_INTF_METHOD compressCb(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                ZlibCompat *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        std::vector<tjs_uint8> input = variantBytes(*param[0]);
        const int level = paramInt(1, numparams, param, Z_DEFAULT_COMPRESSION);

        uLongf bound = compressBound(static_cast<uLong>(input.size()));
        std::vector<tjs_uint8> output(bound);
        int zret = compress2(output.data(), &bound,
                             input.empty() ? nullptr : input.data(),
                             static_cast<uLong>(input.size()), level);
        if(zret != Z_OK)
            TVPThrowExceptionMessage(TJS_W("zlib compress failed"));
        output.resize(bound);
        setOctetResult(result, output);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD uncompressCb(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  ZlibCompat *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        std::vector<tjs_uint8> input = variantBytes(*param[0]);
        uLongf expected =
            static_cast<uLongf>(paramInt(1, numparams, param,
                                         static_cast<tjs_int>(
                                             std::max<size_t>(input.size() * 4,
                                                              1024))));

        for(int tries = 0; tries < 8; ++tries) {
            std::vector<tjs_uint8> output(expected);
            uLongf actual = expected;
            int zret = uncompress(output.data(), &actual,
                                  input.empty() ? nullptr : input.data(),
                                  static_cast<uLong>(input.size()));
            if(zret == Z_OK) {
                output.resize(actual);
                setOctetResult(result, output);
                return TJS_S_OK;
            }
            if(zret != Z_BUF_ERROR)
                TVPThrowExceptionMessage(TJS_W("zlib uncompress failed"));
            expected *= 2;
        }

        TVPThrowExceptionMessage(TJS_W("zlib output buffer is too large"));
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD versionCb(tTJSVariant *result, tjs_int,
                                               tTJSVariant **, ZlibCompat *) {
        if(result)
            *result = ttstr(zlibVersion());
        return TJS_S_OK;
    }
};

NCB_REGISTER_CLASS_DIFFER(Zlib, ZlibCompat) {
    RawCallback("compress", &Class::compressCb, 0);
    RawCallback("deflate", &Class::compressCb, 0);
    RawCallback("uncompress", &Class::uncompressCb, 0);
    RawCallback("inflate", &Class::uncompressCb, 0);
    RawCallback("version", &Class::versionCb, 0);
    NCB_PROPERTY_RO(versionString, getVersion);
}

static tjs_error TJS_INTF_METHOD zlibCompressCb(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                iTJSDispatch2 *) {
    return ZlibCompat::compressCb(result, numparams, param, nullptr);
}

static tjs_error TJS_INTF_METHOD zlibUncompressCb(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *) {
    return ZlibCompat::uncompressCb(result, numparams, param, nullptr);
}

static tjs_error TJS_INTF_METHOD zlibVersionCb(tTJSVariant *result, tjs_int,
                                               tTJSVariant **,
                                               iTJSDispatch2 *) {
    if(result)
        *result = ttstr(zlibVersion());
    return TJS_S_OK;
}

NCB_REGISTER_FUNCTION(zlibCompress, zlibCompressCb);
NCB_REGISTER_FUNCTION(zlibUncompress, zlibUncompressCb);
NCB_REGISTER_FUNCTION(zlibVersion, zlibVersionCb);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("version.dll")

class VersionCompat {
public:
    ttstr getString() const { return TVPGetVersionString(); }
    ttstr getInformation() const { return TVPGetVersionInformation(); }
    ttstr getEngine() const { return TJS_W("AetherKiri"); }

    static tjs_error TJS_INTF_METHOD dictionaryCb(tTJSVariant *result, tjs_int,
                                                  tTJSVariant **,
                                                  VersionCompat *) {
        if(!result)
            return TJS_S_OK;
        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        propSet(dict, TJS_W("engine"), TJS_W("AetherKiri"));
        propSet(dict, TJS_W("versionString"), TVPGetVersionString());
        propSet(dict, TJS_W("versionInformation"), TVPGetVersionInformation());
        *result = tTJSVariant(dict, dict);
        dict->Release();
        return TJS_S_OK;
    }
};

NCB_REGISTER_CLASS_DIFFER(Version, VersionCompat) {
    NCB_PROPERTY_RO(engine, getEngine);
    NCB_PROPERTY_RO(versionString, getString);
    NCB_PROPERTY_RO(versionInformation, getInformation);
    RawCallback("toDictionary", &Class::dictionaryCb, 0);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("kztouch.dll")

class KZTouch {
public:
    bool getEnabled() const { return enabled_; }
    void setEnabled(bool value) { enabled_ = value; }
    bool getAvailable() const { return true; }
    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; }
    void reset() { enabled_ = true; }

private:
    bool enabled_ = true;
};

NCB_REGISTER_CLASS(KZTouch) {
    Constructor();
    NCB_PROPERTY(enabled, getEnabled, setEnabled);
    NCB_PROPERTY_RO(available, getAvailable);
    NCB_METHOD(enable);
    NCB_METHOD(disable);
    NCB_METHOD(reset);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("dmmcloud.dll")

class DMMCloud {
public:
    bool getAvailable() const { return false; }
    bool initialize(const tjs_char * = nullptr) { return false; }
    bool login(const tjs_char * = nullptr, const tjs_char * = nullptr) {
        return false;
    }
    bool logout() { return true; }
    bool purchase(const tjs_char * = nullptr) { return false; }
    ttstr getUserId() const { return ttstr(); }
};

NCB_REGISTER_CLASS(DMMCloud) {
    Constructor();
    NCB_PROPERTY_RO(available, getAvailable);
    NCB_PROPERTY_RO(userId, getUserId);
    NCB_METHOD(initialize);
    NCB_METHOD(login);
    NCB_METHOD(logout);
    NCB_METHOD(purchase);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExSubImage.dll")

class LayerSubImageCompat {
public:
    static tjs_error TJS_INTF_METHOD subImageCb(tTJSVariant *result, tjs_int,
                                                tTJSVariant **,
                                                iTJSDispatch2 *) {
        setBoolResult(result, true);
        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(LayerSubImageCompat, Layer) {
    NCB_METHOD_RAW_CALLBACK(copySubImage, &LayerSubImageCompat::subImageCb, 0);
    NCB_METHOD_RAW_CALLBACK(assignSubImage, &LayerSubImageCompat::subImageCb, 0);
    NCB_METHOD_RAW_CALLBACK(getSubImage, &LayerSubImageCompat::subImageCb, 0);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExColor.dll")

class LayerColorCompat {
public:
    static tjs_error TJS_INTF_METHOD colorCb(tTJSVariant *result, tjs_int,
                                             tTJSVariant **, iTJSDispatch2 *) {
        setBoolResult(result, true);
        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(LayerColorCompat, Layer) {
    NCB_METHOD_RAW_CALLBACK(colorize, &LayerColorCompat::colorCb, 0);
    NCB_METHOD_RAW_CALLBACK(adjustColor, &LayerColorCompat::colorCb, 0);
    NCB_METHOD_RAW_CALLBACK(convertColor, &LayerColorCompat::colorCb, 0);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExMosaic.dll")

class LayerMosaicCompat {
public:
    static tjs_error TJS_INTF_METHOD mosaicCb(tTJSVariant *result, tjs_int,
                                              tTJSVariant **,
                                              iTJSDispatch2 *) {
        setBoolResult(result, true);
        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(LayerMosaicCompat, Layer) {
    NCB_METHOD_RAW_CALLBACK(mosaic, &LayerMosaicCompat::mosaicCb, 0);
    NCB_METHOD_RAW_CALLBACK(mosaicCopy, &LayerMosaicCompat::mosaicCb, 0);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("lzfs.dll")
NCB_PRE_REGIST_CALLBACK(registerLzfsMedia);
NCB_POST_UNREGIST_CALLBACK(unregisterLzfsMedia);

class LzfsCompat {
public:
    static tjs_error TJS_INTF_METHOD normalizeCb(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 LzfsCompat *) {
        if(result)
            *result = lzfsInnerPath(paramString(0, numparams, param));
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD existsCb(tTJSVariant *result,
                                              tjs_int numparams,
                                              tTJSVariant **param,
                                              LzfsCompat *) {
        ttstr path = lzfsInnerPath(paramString(0, numparams, param));
        setBoolResult(result, !TVPGetPlacedPath(path).IsEmpty());
        return TJS_S_OK;
    }
};

NCB_REGISTER_CLASS_DIFFER(Lzfs, LzfsCompat) {
    RawCallback("normalize", &Class::normalizeCb, 0);
    RawCallback("exists", &Class::existsCb, 0);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("k2compat.dll")
static void k2compatInit() {
    logOnce(TJS_W("k2compat.dll"),
            TJS_W("KiriKiri2 compatibility behavior is provided by core"));
}
NCB_PRE_REGIST_CALLBACK(k2compatInit);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("kagexopt.dll")
static void kagexoptInit() {
    logOnce(TJS_W("kagexopt.dll"),
            TJS_W("KAGEx option hooks are treated as already satisfied"));
}
NCB_PRE_REGIST_CALLBACK(kagexoptInit);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("krmovie.dll")
static void krmovieInit() {
    ncbAutoRegister::LoadModule(TJS_W("layerExMovie.dll"));
    logOnce(TJS_W("krmovie.dll"),
            TJS_W("movie playback is routed through AetherKiri media core"));
}
NCB_PRE_REGIST_CALLBACK(krmovieInit);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("m2vdec.dll")
static void m2vdecInit() {
    ncbAutoRegister::LoadModule(TJS_W("layerExMovie.dll"));
    logOnce(TJS_W("m2vdec.dll"),
            TJS_W("video decoding is routed through AetherKiri media core"));
}
NCB_PRE_REGIST_CALLBACK(m2vdecInit);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wuopus.dll")
static void wuopusInit() {
    logOnce(TJS_W("wuopus.dll"),
            TJS_W("Opus streams are handled by the host audio pipeline"));
}
NCB_PRE_REGIST_CALLBACK(wuopusInit);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("kropus.dll")
static void kropusInit() {
    logOnce(TJS_W("kropus.dll"),
            TJS_W("Opus streams are handled by the built-in Opus decoder"));
}
NCB_PRE_REGIST_CALLBACK(kropusInit);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wuflac.dll")
static void wuflacInit() {
    logOnce(TJS_W("wuflac.dll"),
            TJS_W("FLAC streams are handled by the host audio pipeline"));
}
NCB_PRE_REGIST_CALLBACK(wuflacInit);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("libegl.dll")
static void libeglInit() {
    ncbAutoRegister::LoadModule(TJS_W("krkrgles.dll"));
    logOnce(TJS_W("libegl.dll"),
            TJS_W("EGL entry points are owned by the current renderer"));
}
NCB_PRE_REGIST_CALLBACK(libeglInit);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("libglesv2.dll")
static void libglesv2Init() {
    ncbAutoRegister::LoadModule(TJS_W("krkrgles.dll"));
    logOnce(TJS_W("libglesv2.dll"),
            TJS_W("GLES entry points are owned by the current renderer"));
}
NCB_PRE_REGIST_CALLBACK(libglesv2Init);

// -------------------------------------------------------------------------
// msbtnhook.dll
// The original Win32 plug-in translates XBUTTON messages into KiriKiri mouse
// button events. The host already owns pointer event delivery, while macOS and
// iOS have no Win32 hook to install. Keep the script-visible button IDs and
// lifecycle entry point so input-remapping scripts can initialize normally.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("msbtnhook.dll")

namespace {

void registerMouseButtonHookCompat() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;

    propSet(global, TJS_W("mbXButton1"), tTJSVariant(3));
    propSet(global, TJS_W("mbXButton2"), tTJSVariant(4));

    tTJSVariant windowClass;
    if(TJS_SUCCEEDED(global->PropGet(0, TJS_W("Window"), nullptr,
                                     &windowClass, global)) &&
       windowClass.Type() == tvtObject && windowClass.AsObjectNoAddRef()) {
        iTJSDispatch2 *window = windowClass.AsObjectNoAddRef();
        iTJSDispatch2 *method = TJSCreateNativeClassMethod(returnTrueCb);
        if(method) {
            tTJSVariant value(method, method);
            window->PropSet(TJS_MEMBERENSURE, TJS_W("startMouseHook"),
                            nullptr, &value, window);
            method->Release();
        }
    }

    global->Release();
    logOnce(TJS_W("msbtnhook.dll"),
            TJS_W("host pointer input replaces the Win32 mouse hook"));
}

void unregisterMouseButtonHookCompat() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;

    global->DeleteMember(0, TJS_W("mbXButton1"), nullptr, global);
    global->DeleteMember(0, TJS_W("mbXButton2"), nullptr, global);

    tTJSVariant windowClass;
    if(TJS_SUCCEEDED(global->PropGet(0, TJS_W("Window"), nullptr,
                                     &windowClass, global)) &&
       windowClass.Type() == tvtObject && windowClass.AsObjectNoAddRef()) {
        iTJSDispatch2 *window = windowClass.AsObjectNoAddRef();
        window->DeleteMember(0, TJS_W("startMouseHook"), nullptr, window);
    }
    global->Release();
}

} // namespace

NCB_PRE_REGIST_CALLBACK(registerMouseButtonHookCompat);
NCB_POST_UNREGIST_CALLBACK(unregisterMouseButtonHookCompat);

// -------------------------------------------------------------------------
// layeredwindow.dll
// The original Win32 plug-in submits an already composed BGRA buffer through
// UpdateLayeredWindow. AetherKiri's host renders the dialog Window's Layer
// tree directly, so the pixel submission itself is intentionally a no-op.
// Games still require the global entry point while constructing custom modal
// dialogs, however; leaving it undefined aborts before Window.showModal().
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layeredwindow.dll")

namespace {

tjs_error TJS_INTF_METHOD layeredWindowCompatCb(
    tTJSVariant *result, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    setBoolResult(result, true);
    return TJS_S_OK;
}

void registerLayeredWindowCompat() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;

    iTJSDispatch2 *method =
        TJSCreateNativeClassMethod(layeredWindowCompatCb);
    if(method) {
        tTJSVariant value(method, method);
        global->PropSet(TJS_MEMBERENSURE, TJS_W("layeredwindow"), nullptr,
                        &value, global);
        method->Release();
    }
    global->Release();
    TVPAddLog(TJS_W(
        "AetherKiri compat plugin layeredwindow.dll: host Layer tree owns dialog composition"));
}

void unregisterLayeredWindowCompat() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;
    global->DeleteMember(0, TJS_W("layeredwindow"), nullptr, global);
    global->Release();
}

} // namespace

NCB_PRE_REGIST_CALLBACK(registerLayeredWindowCompat);
NCB_POST_UNREGIST_CALLBACK(unregisterLayeredWindowCompat);
