#include "DebugIntf.h"
#include "BitmapIntf.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "MsgIntf.h"
#include "StorageIntf.h"
#include "WindowImpl.h"
#include "ncbind.hpp"
#include "portableLayerEffects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <limits>
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
        if(!octet || octet->GetLength() == 0 || !octet->GetData())
            return {};
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
    if(!octet) {
        result->Clear();
        return;
    }
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

// -------------------------------------------------------------------------
// Portable image helpers for the historical layerEx* plug-ins.
//
// The old Windows plug-ins received a writable 32-bit BGRA view of the
// calling Layer.  Keep that ABI boundary in this adapter and put the actual
// pixel algorithms in portableLayerEffects.cpp so they can be tested without
// constructing a complete TJS world.
// -------------------------------------------------------------------------

struct CompatLayerImage {
    tTJSNI_BaseLayer *layer = nullptr;
    tTVPBaseTexture *image = nullptr;
    AetherKiri::LayerEffects::ImageView view;
    tTVPRect clip;
};

bool getCompatLayerImage(iTJSDispatch2 *object, CompatLayerImage &out) {
    if(!object)
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

    void *pixels = layer->GetMainImagePixelBufferForWrite();
    const tjs_int pitch = layer->GetMainImagePixelBufferPitch();
    if(!pixels || pitch < static_cast<tjs_int>(image->GetWidth() * 4))
        return false;

    out.layer = layer;
    out.image = image;
    out.view = {static_cast<tjs_uint8 *>(pixels),
                static_cast<int>(image->GetWidth()),
                static_cast<int>(image->GetHeight()), pitch};
    out.clip = tTVPRect(layer->GetClipLeft(), layer->GetClipTop(),
                        layer->GetClipLeft() + layer->GetClipWidth(),
                        layer->GetClipTop() + layer->GetClipHeight());
    return true;
}

iTVPBaseBitmap *compatBitmapFromVariant(const tTJSVariant &value,
                                        tTJSNI_BaseLayer **sourceLayer = nullptr,
                                        tTJSNI_Bitmap **sourceBitmap = nullptr) {
    if(sourceLayer)
        *sourceLayer = nullptr;
    if(sourceBitmap)
        *sourceBitmap = nullptr;
    if(value.Type() != tvtObject || !value.AsObjectNoAddRef())
        return nullptr;

    iTJSDispatch2 *object = value.AsObjectNoAddRef();
    tTJSNI_BaseLayer *layer = nullptr;
    if(TJS_SUCCEEDED(object->NativeInstanceSupport(
           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
           reinterpret_cast<iTJSNativeInstance **>(&layer))) && layer) {
        if(sourceLayer)
            *sourceLayer = layer;
        return layer->GetMainImage();
    }

    tTJSNI_Bitmap *bitmap = nullptr;
    if(TJS_SUCCEEDED(object->NativeInstanceSupport(
           TJS_NIS_GETINSTANCE, tTJSNC_Bitmap::ClassID,
           reinterpret_cast<iTJSNativeInstance **>(&bitmap))) && bitmap) {
        if(sourceBitmap)
            *sourceBitmap = bitmap;
        return bitmap->GetBitmap();
    }
    return nullptr;
}

void markCompatLayerUpdated(CompatLayerImage &image, const tTVPRect &rect) {
    image.layer->SetImageModified(true);
    image.layer->Update(rect);
}

bool clipCopyRect(const iTVPBaseBitmap *source, iTVPBaseBitmap *destination,
                  tjs_int &sourceX, tjs_int &sourceY, tjs_int &width,
                  tjs_int &height, tjs_int &destinationX,
                  tjs_int &destinationY) {
    if(!source || !destination || !source->Is32BPP() ||
       !destination->Is32BPP() || width <= 0 || height <= 0)
        return false;

    if(sourceX < 0) {
        const tjs_int delta = -sourceX;
        sourceX = 0;
        destinationX += delta;
        width -= delta;
    }
    if(sourceY < 0) {
        const tjs_int delta = -sourceY;
        sourceY = 0;
        destinationY += delta;
        height -= delta;
    }
    if(destinationX < 0) {
        const tjs_int delta = -destinationX;
        destinationX = 0;
        sourceX += delta;
        width -= delta;
    }
    if(destinationY < 0) {
        const tjs_int delta = -destinationY;
        destinationY = 0;
        sourceY += delta;
        height -= delta;
    }
    width = std::min(width, static_cast<tjs_int>(source->GetWidth()) - sourceX);
    height = std::min(height, static_cast<tjs_int>(source->GetHeight()) - sourceY);
    width = std::min(width,
                     static_cast<tjs_int>(destination->GetWidth()) - destinationX);
    height = std::min(height,
                      static_cast<tjs_int>(destination->GetHeight()) - destinationY);
    return width > 0 && height > 0 && sourceX >= 0 && sourceY >= 0 &&
        destinationX >= 0 && destinationY >= 0;
}

bool copyCompatRegion(tTJSNI_BaseLayer *destinationLayer,
                      const iTVPBaseBitmap *source, tjs_int sourceX,
                      tjs_int sourceY, tjs_int width, tjs_int height,
                      tjs_int destinationX, tjs_int destinationY) {
    if(!destinationLayer || !source)
        return false;
    iTVPBaseBitmap *destination = destinationLayer->GetMainImage();
    if(!clipCopyRect(source, destination, sourceX, sourceY, width, height,
                     destinationX, destinationY))
        return false;

    const tTVPRect sourceRect(sourceX, sourceY, sourceX + width,
                              sourceY + height);
    if(!destination->CopyRect(destinationX, destinationY, source, sourceRect,
                              TVP_BB_COPY_MAIN | TVP_BB_COPY_MASK))
        return false;
    destinationLayer->SetImageModified(true);
    destinationLayer->Update(
        tTVPRect(destinationX, destinationY, destinationX + width,
                 destinationY + height));
    return true;
}

iTJSDispatch2 *createCompatBitmapRegion(const iTVPBaseBitmap *source,
                                        tjs_int sourceX, tjs_int sourceY,
                                        tjs_int width, tjs_int height) {
    if(!source || !source->Is32BPP() || width <= 0 || height <= 0 ||
       sourceX < 0 || sourceY < 0 ||
       sourceX + width > static_cast<tjs_int>(source->GetWidth()) ||
       sourceY + height > static_cast<tjs_int>(source->GetHeight()))
        return nullptr;

    iTJSDispatch2 *object = nullptr;
    try {
        object = TVPCreateBitmapObject();
        if(!object)
            return nullptr;
        tTJSNI_Bitmap *bitmap = nullptr;
        if(TJS_FAILED(object->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Bitmap::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&bitmap))) || !bitmap) {
            object->Release();
            return nullptr;
        }
        bitmap->SetSize(static_cast<tjs_uint>(width),
                        static_cast<tjs_uint>(height), false);
        if(!bitmap->GetBitmap()->CopyRect(
               0, 0, source, tTVPRect(sourceX, sourceY, sourceX + width,
                                      sourceY + height),
               TVP_BB_COPY_MAIN | TVP_BB_COPY_MASK)) {
            object->Release();
            return nullptr;
        }
        return object;
    } catch(...) {
        if(object)
            object->Release();
        return nullptr;
    }
}

bool compatNumeric(const tTJSVariant *value) {
    return value && value->Type() != tvtVoid && value->Type() != tvtObject &&
        value->Type() != tvtString && value->Type() != tvtOctet;
}

bool compatStringEquals(const ttstr &value, const tjs_char *expected) {
    return value == ttstr(expected);
}

} // namespace

// Extra modules kept by AetherKiri or mobile/legacy games. These are not a
// direct krkrsdl3 copy: each module either maps onto current core behavior or
// reports an explicit compatibility surface when the old backend is unavailable.

#define NCB_MODULE_NAME TJS_W("zlib.dll")

class ZlibCompat {
public:
    static constexpr std::size_t kMaxBufferBytes = 256u * 1024u * 1024u;

    ttstr getVersion() const { return ttstr(zlibVersion()); }

    static tjs_error TJS_INTF_METHOD compressCb(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                ZlibCompat *) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        std::vector<tjs_uint8> input = variantBytes(*param[0]);
        if(input.size() > kMaxBufferBytes ||
           input.size() > static_cast<std::size_t>(std::numeric_limits<uLong>::max()))
            TVPThrowExceptionMessage(TJS_W("zlib input is too large"));
        const int level = paramInt(1, numparams, param, Z_DEFAULT_COMPRESSION);

        uLongf bound = compressBound(static_cast<uLong>(input.size()));
        if(bound == 0 || bound > kMaxBufferBytes)
            TVPThrowExceptionMessage(TJS_W("zlib output is too large"));
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
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        std::vector<tjs_uint8> input = variantBytes(*param[0]);
        if(input.size() > kMaxBufferBytes ||
           input.size() > static_cast<std::size_t>(std::numeric_limits<uLong>::max()))
            TVPThrowExceptionMessage(TJS_W("zlib input is too large"));
        const std::size_t defaultExpected =
            std::min<std::size_t>(kMaxBufferBytes,
                                  std::max<std::size_t>(
                                      input.size() > kMaxBufferBytes / 4
                                          ? kMaxBufferBytes
                                          : input.size() * 4,
                                      1024));
        const tjs_int requested =
            paramInt(1, numparams, param,
                     defaultExpected >
                             static_cast<std::size_t>(
                                 std::numeric_limits<tjs_int>::max())
                         ? std::numeric_limits<tjs_int>::max()
                         : static_cast<tjs_int>(defaultExpected));
        if(requested <= 0 ||
           static_cast<std::size_t>(requested) > kMaxBufferBytes)
            TVPThrowExceptionMessage(TJS_W("invalid zlib output size"));
        uLongf expected = static_cast<uLongf>(requested);

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
            if(expected >= kMaxBufferBytes ||
               expected > std::numeric_limits<uLongf>::max() / 2)
                break;
            expected = std::min<uLongf>(
                expected * 2, static_cast<uLongf>(kMaxBufferBytes));
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
    bool getAvailable() const {
        // kztouch is a bridge into the active Window input surface.  Report
        // availability only after a host form exists; this avoids claiming a
        // touch backend during early bootstrap/headless operation while still
        // allowing the script to toggle the bridge with enabled/enable().
        return TVPMainWindow != nullptr && TVPMainWindow->GetForm() != nullptr;
    }
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
    bool logout() { return false; }
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
    static tjs_error TJS_INTF_METHOD copySubImageCb(tTJSVariant *result,
                                                    tjs_int numparams,
                                                    tTJSVariant **param,
                                                    iTJSDispatch2 *objthis) {
        CompatLayerImage destination;
        if(!getCompatLayerImage(objthis, destination)) {
            setBoolResult(result, false);
            return TJS_S_OK;
        }

        const iTVPBaseBitmap *source = destination.image;
        tjs_int sourceX = 0, sourceY = 0, width = 0, height = 0;
        tjs_int destinationX = 0, destinationY = 0;
        bool parsed = false;

        // Accept both forms used by the historical plug-ins:
        //   copySubImage(source, sx, sy, w, h, dx, dy)
        //   copySubImage(dx, dy, source, sx, sy, w, h)
        if(numparams > 0 && param && param[0] &&
           param[0]->Type() == tvtObject) {
            source = compatBitmapFromVariant(*param[0]);
            if(source && numparams >= 5) {
                sourceX = paramInt(1, numparams, param);
                sourceY = paramInt(2, numparams, param);
                width = paramInt(3, numparams, param,
                                 static_cast<tjs_int>(source->GetWidth()) - sourceX);
                height = paramInt(4, numparams, param,
                                  static_cast<tjs_int>(source->GetHeight()) - sourceY);
                destinationX = paramInt(5, numparams, param);
                destinationY = paramInt(6, numparams, param);
                parsed = true;
            }
        } else if(numparams > 2 && param && param[2] &&
                  param[2]->Type() == tvtObject) {
            source = compatBitmapFromVariant(*param[2]);
            if(source && numparams >= 7) {
                destinationX = paramInt(0, numparams, param);
                destinationY = paramInt(1, numparams, param);
                sourceX = paramInt(3, numparams, param);
                sourceY = paramInt(4, numparams, param);
                width = paramInt(5, numparams, param);
                height = paramInt(6, numparams, param);
                parsed = true;
            }
        } else if(numparams >= 4 &&
                  compatNumeric(param ? param[0] : nullptr)) {
            // In-place form: (sx, sy, w, h, dx, dy).
            sourceX = paramInt(0, numparams, param);
            sourceY = paramInt(1, numparams, param);
            width = paramInt(2, numparams, param);
            height = paramInt(3, numparams, param);
            destinationX = paramInt(4, numparams, param, sourceX);
            destinationY = paramInt(5, numparams, param, sourceY);
            parsed = true;
        }

        const bool copied = parsed && copyCompatRegion(
            destination.layer, source, sourceX, sourceY, width, height,
            destinationX, destinationY);
        setBoolResult(result, copied);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD assignSubImageCb(tTJSVariant *result,
                                                      tjs_int numparams,
                                                      tTJSVariant **param,
                                                      iTJSDispatch2 *objthis) {
        CompatLayerImage destination;
        if(!getCompatLayerImage(objthis, destination) || numparams < 1 ||
           !param || !param[0]) {
            setBoolResult(result, false);
            return TJS_S_OK;
        }

        tTJSNI_BaseLayer *sourceLayer = nullptr;
        const iTVPBaseBitmap *source = compatBitmapFromVariant(
            *param[0], &sourceLayer);
        if(!source || !source->Is32BPP() || source->GetWidth() == 0 ||
           source->GetHeight() == 0) {
            setBoolResult(result, false);
            return TJS_S_OK;
        }

        tjs_int sourceX = 0;
        tjs_int sourceY = 0;
        tjs_int width = static_cast<tjs_int>(source->GetWidth());
        tjs_int height = static_cast<tjs_int>(source->GetHeight());
        tjs_int dx = 0;
        tjs_int dy = 0;
        if(numparams >= 7) {
            sourceX = paramInt(1, numparams, param);
            sourceY = paramInt(2, numparams, param);
            width = paramInt(3, numparams, param);
            height = paramInt(4, numparams, param);
            dx = paramInt(5, numparams, param);
            dy = paramInt(6, numparams, param);
        } else {
            dx = paramInt(1, numparams, param);
            dy = paramInt(2, numparams, param);
        }
        if(sourceX < 0 || sourceY < 0 || width <= 0 || height <= 0 ||
           sourceX > static_cast<tjs_int>(source->GetWidth()) ||
           sourceY > static_cast<tjs_int>(source->GetHeight())) {
            setBoolResult(result, false);
            return TJS_S_OK;
        }
        width = std::min(width,
                         static_cast<tjs_int>(source->GetWidth()) - sourceX);
        height = std::min(height,
                          static_cast<tjs_int>(source->GetHeight()) - sourceY);

        // Resizing a Layer can detach/release its current texture.  Clone an
        // aliased source first so assignSubImage(layer, ...) remains safe.
        iTJSDispatch2 *temporary = nullptr;
        if(sourceLayer == destination.layer) {
            temporary = createCompatBitmapRegion(source, sourceX, sourceY,
                                                  width, height);
            if(!temporary) {
                setBoolResult(result, false);
                return TJS_S_OK;
            }
            source = compatBitmapFromVariant(tTJSVariant(temporary));
            sourceX = 0;
            sourceY = 0;
        }

        // assignSubImage replaces the destination image dimensions.  This is
        // also what makes a freshly-created Layer usable by old scripts.
        if(destination.layer->GetImageWidth() !=
               static_cast<tjs_uint>(width) ||
           destination.layer->GetImageHeight() !=
               static_cast<tjs_uint>(height)) {
            destination.layer->SetHasImage(true);
            destination.layer->SetImageSize(static_cast<tjs_uint>(width),
                                            static_cast<tjs_uint>(height));
        }
        destination = CompatLayerImage{};
        if(!getCompatLayerImage(objthis, destination)) {
            if(temporary)
                temporary->Release();
            setBoolResult(result, false);
            return TJS_S_OK;
        }
        const bool copied = copyCompatRegion(
            destination.layer, source, sourceX, sourceY, width, height, dx, dy);
        if(temporary)
            temporary->Release();
        setBoolResult(result, copied);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD getSubImageCb(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *objthis) {
        CompatLayerImage sourceLayer;
        if(!getCompatLayerImage(objthis, sourceLayer)) {
            if(result)
                result->Clear();
            return TJS_S_OK;
        }
        const iTVPBaseBitmap *source = sourceLayer.image;
        tjs_int offset = 0;
        if(numparams > 0 && param && param[0] &&
           param[0]->Type() == tvtObject) {
            source = compatBitmapFromVariant(*param[0]);
            offset = 1;
        }
        if(!source || numparams < offset + 2) {
            if(result)
                result->Clear();
            return TJS_S_OK;
        }
        const tjs_int sourceX = paramInt(offset, numparams, param);
        const tjs_int sourceY = paramInt(offset + 1, numparams, param);
        const tjs_int width = paramInt(
            offset + 2, numparams, param,
            static_cast<tjs_int>(source->GetWidth()) - sourceX);
        const tjs_int height = paramInt(
            offset + 3, numparams, param,
            static_cast<tjs_int>(source->GetHeight()) - sourceY);
        iTJSDispatch2 *bitmap = createCompatBitmapRegion(
            source, sourceX, sourceY, width, height);
        if(!bitmap) {
            if(result)
                result->Clear();
            return TJS_S_OK;
        }
        if(result)
            *result = tTJSVariant(bitmap, bitmap);
        bitmap->Release();
        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(LayerSubImageCompat, Layer) {
    NCB_METHOD_RAW_CALLBACK(copySubImage, &LayerSubImageCompat::copySubImageCb,
                           0);
    NCB_METHOD_RAW_CALLBACK(assignSubImage,
                            &LayerSubImageCompat::assignSubImageCb, 0);
    NCB_METHOD_RAW_CALLBACK(getSubImage, &LayerSubImageCompat::getSubImageCb,
                           0);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExColor.dll")

class LayerColorCompat {
public:
    static tjs_error TJS_INTF_METHOD colorizeCb(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                iTJSDispatch2 *objthis) {
        if(numparams < 2 || !param) {
            setBoolResult(result, false);
            return TJS_S_OK;
        }
        CompatLayerImage image;
        const bool changed = getCompatLayerImage(objthis, image) &&
            AetherKiri::LayerEffects::applyColorize(
                image.view, image.clip.left, image.clip.top,
                image.clip.get_width(), image.clip.get_height(),
                paramInt(0, numparams, param), paramInt(1, numparams, param),
                numparams > 2 && param[2] ? static_cast<double>(*param[2]) : 1.0);
        if(changed)
            markCompatLayerUpdated(image, image.clip);
        setBoolResult(result, changed);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD adjustColorCb(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *objthis) {
        if(numparams < 1 || !param) {
            setBoolResult(result, false);
            return TJS_S_OK;
        }
        CompatLayerImage image;
        const bool changed = getCompatLayerImage(objthis, image) &&
            AetherKiri::LayerEffects::applyLight(
                image.view, image.clip.left, image.clip.top,
                image.clip.get_width(), image.clip.get_height(),
                paramInt(0, numparams, param), paramInt(1, numparams, param));
        if(changed)
            markCompatLayerUpdated(image, image.clip);
        setBoolResult(result, changed);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD convertColorCb(tTJSVariant *result,
                                                    tjs_int numparams,
                                                    tTJSVariant **param,
                                                    iTJSDispatch2 *objthis) {
        if(numparams < 1 || !param || !param[0]) {
            setBoolResult(result, false);
            return TJS_S_OK;
        }
        bool gray = false;
        bool invert = false;
        if(param[0]->Type() == tvtString) {
            const ttstr mode = param[0]->AsStringNoAddRef();
            gray = compatStringEquals(mode, TJS_W("gray")) ||
                compatStringEquals(mode, TJS_W("grayscale"));
            invert = compatStringEquals(mode, TJS_W("invert"));
        } else if(compatNumeric(param[0])) {
            const tjs_int mode = static_cast<tjs_int>(*param[0]);
            gray = mode == 0;
            invert = mode == 1;
        }
        CompatLayerImage image;
        bool changed = false;
        if(getCompatLayerImage(objthis, image)) {
            if(gray)
                changed = AetherKiri::LayerEffects::applyGrayScale(
                    image.view, image.clip.left, image.clip.top,
                    image.clip.get_width(), image.clip.get_height());
            else if(invert)
                changed = AetherKiri::LayerEffects::applyInvert(
                    image.view, image.clip.left, image.clip.top,
                    image.clip.get_width(), image.clip.get_height());
        }
        if(changed)
            markCompatLayerUpdated(image, image.clip);
        setBoolResult(result, changed);
        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(LayerColorCompat, Layer) {
    NCB_METHOD_RAW_CALLBACK(colorize, &LayerColorCompat::colorizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(adjustColor, &LayerColorCompat::adjustColorCb, 0);
    NCB_METHOD_RAW_CALLBACK(convertColor, &LayerColorCompat::convertColorCb, 0);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExMosaic.dll")

class LayerMosaicCompat {
public:
    static tjs_error TJS_INTF_METHOD mosaicCb(tTJSVariant *result,
                                              tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *objthis) {
        CompatLayerImage image;
        if(!getCompatLayerImage(objthis, image)) {
            setBoolResult(result, false);
            return TJS_S_OK;
        }
        tjs_int offset = 0;
        if(numparams > 0 && param && param[0] &&
           param[0]->Type() == tvtObject)
            offset = 1; // mosaicCopy's source is handled below.
        const tjs_int block = paramInt(offset, numparams, param, 8);
        const bool changed = AetherKiri::LayerEffects::applyMosaic(
            image.view, image.clip.left, image.clip.top,
            image.clip.get_width(), image.clip.get_height(), block);
        if(changed)
            markCompatLayerUpdated(image, image.clip);
        setBoolResult(result, changed);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD mosaicCopyCb(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *objthis) {
        CompatLayerImage destination;
        if(!getCompatLayerImage(objthis, destination)) {
            setBoolResult(result, false);
            return TJS_S_OK;
        }
        if(numparams > 0 && param && param[0] &&
           param[0]->Type() == tvtObject) {
            const iTVPBaseBitmap *source = compatBitmapFromVariant(*param[0]);
            if(!source) {
                setBoolResult(result, false);
                return TJS_S_OK;
            }
            const tjs_int width = std::min<tjs_int>(
                destination.view.width, static_cast<tjs_int>(source->GetWidth()));
            const tjs_int height = std::min<tjs_int>(
                destination.view.height, static_cast<tjs_int>(source->GetHeight()));
            if(!copyCompatRegion(destination.layer, source, 0, 0, width, height,
                                 0, 0)) {
                setBoolResult(result, false);
                return TJS_S_OK;
            }
            destination = CompatLayerImage{};
            if(!getCompatLayerImage(objthis, destination)) {
                setBoolResult(result, false);
                return TJS_S_OK;
            }
        }
        const tjs_int block = paramInt(
            (numparams > 0 && param && param[0] &&
             param[0]->Type() == tvtObject) ? 1 : 0,
            numparams, param, 8);
        const bool changed = AetherKiri::LayerEffects::applyMosaic(
            destination.view, destination.clip.left, destination.clip.top,
            destination.clip.get_width(), destination.clip.get_height(), block);
        if(changed)
            markCompatLayerUpdated(destination, destination.clip);
        setBoolResult(result, changed);
        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(LayerMosaicCompat, Layer) {
    NCB_METHOD_RAW_CALLBACK(mosaic, &LayerMosaicCompat::mosaicCb, 0);
    NCB_METHOD_RAW_CALLBACK(mosaicCopy, &LayerMosaicCompat::mosaicCopyCb, 0);
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
