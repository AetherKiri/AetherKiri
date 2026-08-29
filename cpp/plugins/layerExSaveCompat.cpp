#include "PluginStub.h"
#include "ncbind.hpp"
#include "upstream_bridge/layerExSaveCodecs.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("layerExSave.dll")

namespace {

using ReadPtr = const tjs_uint8 *;
using WritePtr = tjs_uint8 *;

struct LayerImage {
    tjs_int width = 0;
    tjs_int height = 0;
    tjs_int pitch = 0;
    ReadPtr read = nullptr;
    WritePtr write = nullptr;
};

struct ProvinceImage {
    tjs_int width = 0;
    tjs_int height = 0;
    tjs_int pitch = 0;
    const tjs_uint8 *read = nullptr;
};

bool getBoolProp(iTJSDispatch2 *layer, const tjs_char *name) {
    tTJSVariant value;
    return TJS_SUCCEEDED(layer->PropGet(0, name, nullptr, &value, layer)) &&
           value.AsInteger() != 0;
}

tjs_int getIntProp(iTJSDispatch2 *layer, const tjs_char *name) {
    tTJSVariant value;
    if(TJS_FAILED(layer->PropGet(0, name, nullptr, &value, layer)))
        TVPThrowExceptionMessage((ttstr(TJS_W("cannot get Layer.")) + name)
                                     .c_str());
    return static_cast<tjs_int>(value.AsInteger());
}

tjs_intptr_t getPointerProp(iTJSDispatch2 *layer, const tjs_char *name) {
    tTJSVariant value;
    if(TJS_FAILED(layer->PropGet(0, name, nullptr, &value, layer)))
        TVPThrowExceptionMessage((ttstr(TJS_W("cannot get Layer.")) + name)
                                     .c_str());
    // Layer.mainImageBuffer is declared as tjs_intptr_t.  Do not route it
    // through tjs_int (32-bit), otherwise a 64-bit host truncates the address
    // before the upstream codec or the crop helpers can use it.
    return static_cast<tjs_intptr_t>(value.AsInteger());
}

LayerImage getLayerImage(iTJSDispatch2 *layer, bool writable) {
    if(!layer ||
       TJS_FAILED(layer->IsInstanceOf(0, nullptr, nullptr, TJS_W("Layer"),
                                      layer)) ||
       !getBoolProp(layer, TJS_W("hasImage")))
        TVPThrowExceptionMessage(TJS_W("Invalid layer image."));

    LayerImage image;
    image.width = getIntProp(layer, TJS_W("imageWidth"));
    image.height = getIntProp(layer, TJS_W("imageHeight"));
    image.pitch = getIntProp(layer, TJS_W("mainImageBufferPitch"));
    if(image.width <= 0 || image.height <= 0 || image.pitch == 0)
        TVPThrowExceptionMessage(TJS_W("Invalid layer image."));

    const tjs_char *bufferName =
        writable ? TJS_W("mainImageBufferForWrite") : TJS_W("mainImageBuffer");
    auto *buffer = reinterpret_cast<tjs_uint8 *>(
        getPointerProp(layer, bufferName));
    if(!buffer)
        TVPThrowExceptionMessage(TJS_W("Invalid layer image."));
    image.read = buffer;
    image.write = buffer;
    return image;
}

ProvinceImage getProvinceImage(iTJSDispatch2 *layer) {
    if(!layer ||
       TJS_FAILED(layer->IsInstanceOf(0, nullptr, nullptr, TJS_W("Layer"),
                                      layer)))
        TVPThrowExceptionMessage(TJS_W("Invalid layer image."));

    ProvinceImage image;
    image.width = getIntProp(layer, TJS_W("imageWidth"));
    image.height = getIntProp(layer, TJS_W("imageHeight"));
    image.pitch = getIntProp(layer, TJS_W("provinceImageBufferPitch"));
    image.read = reinterpret_cast<const tjs_uint8 *>(
        getPointerProp(layer, TJS_W("provinceImageBuffer")));
    if(image.width <= 0 || image.height <= 0 || image.pitch == 0 ||
       !image.read)
        TVPThrowExceptionMessage(TJS_W("no province image"));
    const auto rowBytes = static_cast<std::int64_t>(image.width);
    const auto absPitch = image.pitch < 0
                              ? -static_cast<std::int64_t>(image.pitch)
                              : static_cast<std::int64_t>(image.pitch);
    if(rowBytes > absPitch)
        TVPThrowExceptionMessage(TJS_W("invalid province image pitch"));
    return image;
}

ReadPtr pixelAt(const LayerImage &image, tjs_int x, tjs_int y) {
    return image.read + static_cast<std::ptrdiff_t>(y) * image.pitch +
           static_cast<std::ptrdiff_t>(x) * 4;
}

const tjs_uint8 *provincePixelAt(const ProvinceImage &image, tjs_int x,
                                 tjs_int y) {
    return image.read + static_cast<std::ptrdiff_t>(y) * image.pitch + x;
}

WritePtr writablePixelAt(const LayerImage &image, tjs_int x, tjs_int y) {
    return image.write + static_cast<std::ptrdiff_t>(y) * image.pitch +
           static_cast<std::ptrdiff_t>(x) * 4;
}

bool nonTransparent(ReadPtr p) { return p[3] != 0; }

bool nonZero(ReadPtr p) {
    return p[0] != 0 || p[1] != 0 || p[2] != 0 || p[3] != 0;
}

bool samePixel(ReadPtr a, ReadPtr b) {
    return a[3] == b[3] &&
           (a[3] == 0 || (a[0] == b[0] && a[1] == b[1] && a[2] == b[2]));
}

void makeRectResult(tTJSVariant *result, tjs_int x, tjs_int y, tjs_int w,
                    tjs_int h) {
    if(!result)
        return;
    ncbDictionaryAccessor dict;
    dict.SetValue(TJS_W("x"), x);
    dict.SetValue(TJS_W("y"), y);
    dict.SetValue(TJS_W("w"), w);
    dict.SetValue(TJS_W("h"), h);
    *result = dict;
}

template <typename Predicate>
tjs_error cropRect(tTJSVariant *result, iTJSDispatch2 *layer,
                   Predicate predicate) {
    const LayerImage image = getLayerImage(layer, false);
    tjs_int minX = image.width;
    tjs_int minY = image.height;
    tjs_int maxX = -1;
    tjs_int maxY = -1;

    for(tjs_int y = 0; y < image.height; ++y) {
        for(tjs_int x = 0; x < image.width; ++x) {
            if(predicate(pixelAt(image, x, y))) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    if(result)
        result->Clear();
    if(maxX < minX || maxY < minY)
        return TJS_S_OK;

    makeRectResult(result, minX, minY, maxX - minX + 1, maxY - minY + 1);
    return TJS_S_OK;
}

void clipRect(const LayerImage &image, tjs_int &left, tjs_int &top,
              tjs_int &width, tjs_int &height) {
    if(left < 0) {
        width += left;
        left = 0;
    }
    if(top < 0) {
        height += top;
        top = 0;
    }
    if(left + width > image.width)
        width = image.width - left;
    if(top + height > image.height)
        height = image.height - top;
}

void callLayerSave(iTJSDispatch2 *layer, const ttstr &filename,
                   const tjs_char *type) {
    tTJSVariant filenameValue(filename);
    tTJSVariant typeValue(type);
    tTJSVariant *args[] = { &filenameValue, &typeValue };
    layer->FuncCall(0, TJS_W("saveLayerImage"), nullptr, nullptr, 2, args,
                    layer);
}

} // namespace

static tjs_error TJS_INTF_METHOD saveLayerImagePng(tTJSVariant *, tjs_int num,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *layer) {
    if(num < 1)
        return TJS_E_BADPARAMCOUNT;
    callLayerSave(layer, param[0]->AsStringNoAddRef(), TJS_W("png"));
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD saveLayerImageTlg5(tTJSVariant *, tjs_int num,
                                                    tTJSVariant **param,
                                                    iTJSDispatch2 *layer) {
    if(num < 1)
        return TJS_E_BADPARAMCOUNT;
    callLayerSave(layer, param[0]->AsStringNoAddRef(), TJS_W("tlg5"));
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD saveLayerImagePngOctet(tTJSVariant *result,
                                                        tjs_int,
                                                        tTJSVariant **,
                                                        iTJSDispatch2 *layer) {
    if(!result)
        return TJS_S_OK;
    const LayerImage image = getLayerImage(layer, false);
    std::vector<std::uint8_t> encoded;
    if(!aether::krkrz::layer_save::encodePng(
           image.read, image.width, image.height, image.pitch, encoded) ||
       encoded.empty() || encoded.size() > std::numeric_limits<tjs_uint>::max()) {
        TVPThrowExceptionMessage(TJS_W("cannot encode layer image"));
    }
    auto *octet = TJSAllocVariantOctet(
        encoded.data(), static_cast<tjs_uint>(encoded.size()));
    if(!octet)
        TVPThrowExceptionMessage(TJS_W("cannot allocate PNG octet"));
    *result = octet;
    octet->Release();
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD getCropRect(tTJSVariant *result, tjs_int,
                                             tTJSVariant **,
                                             iTJSDispatch2 *layer) {
    return cropRect(result, layer, nonTransparent);
}

static tjs_error TJS_INTF_METHOD getCropRectZero(tTJSVariant *result, tjs_int,
                                                 tTJSVariant **,
                                                 iTJSDispatch2 *layer) {
    return cropRect(result, layer, nonZero);
}

static tjs_error TJS_INTF_METHOD getDiffRect(tTJSVariant *result, tjs_int num,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *layer) {
    if(num < 1)
        return TJS_E_BADPARAMCOUNT;

    const LayerImage image = getLayerImage(layer, false);
    const LayerImage base = getLayerImage(param[0]->AsObjectNoAddRef(), false);
    if(image.width != base.width || image.height != base.height)
        TVPThrowExceptionMessage(TJS_W("Different layer size."));

    tjs_int minX = image.width;
    tjs_int minY = image.height;
    tjs_int maxX = -1;
    tjs_int maxY = -1;
    for(tjs_int y = 0; y < image.height; ++y) {
        for(tjs_int x = 0; x < image.width; ++x) {
            if(!samePixel(pixelAt(image, x, y), pixelAt(base, x, y))) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    if(result)
        result->Clear();
    if(maxX < minX || maxY < minY)
        return TJS_S_OK;
    makeRectResult(result, minX, minY, maxX - minX + 1, maxY - minY + 1);
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD getDiffPixel(tTJSVariant *result, tjs_int num,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *layer) {
    if(num < 1)
        return TJS_E_BADPARAMCOUNT;

    const bool fillSame = num >= 2 && param[1]->Type() != tvtVoid;
    const bool fillDiff = num >= 3 && param[2]->Type() != tvtVoid;
    const tjs_uint32 sameColor =
        fillSame ? static_cast<tjs_uint32>(param[1]->AsInteger()) : 0;
    const tjs_uint32 diffColor =
        fillDiff ? static_cast<tjs_uint32>(param[2]->AsInteger()) : 0;

    const LayerImage image = getLayerImage(layer, true);
    const LayerImage base = getLayerImage(param[0]->AsObjectNoAddRef(), false);
    if(image.width != base.width || image.height != base.height)
        TVPThrowExceptionMessage(TJS_W("Different layer size."));

    tTVInteger count = 0;
    for(tjs_int y = 0; y < image.height; ++y) {
        for(tjs_int x = 0; x < image.width; ++x) {
            const bool same = samePixel(pixelAt(image, x, y), pixelAt(base, x, y));
            auto *dst = reinterpret_cast<tjs_uint32 *>(writablePixelAt(image, x, y));
            if(same) {
                if(fillSame)
                    *dst = sameColor;
            } else {
                ++count;
                if(fillDiff)
                    *dst = diffColor;
            }
        }
    }
    if(result)
        *result = count;
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD copyBlueToAlpha(tTJSVariant *, tjs_int num,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *layer) {
    if(num < 1)
        return TJS_E_BADPARAMCOUNT;
    const LayerImage src = getLayerImage(param[0]->AsObjectNoAddRef(), false);
    const LayerImage dst = getLayerImage(layer, true);
    const tjs_int width = std::min(src.width, dst.width);
    const tjs_int height = std::min(src.height, dst.height);
    for(tjs_int y = 0; y < height; ++y)
        for(tjs_int x = 0; x < width; ++x)
            writablePixelAt(dst, x, y)[3] = pixelAt(src, x, y)[0];
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD isBlank(tTJSVariant *result, tjs_int num,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *layer) {
    if(num < 4)
        return TJS_E_BADPARAMCOUNT;

    const LayerImage image = getLayerImage(layer, false);
    tjs_int left = param[0]->AsInteger();
    tjs_int top = param[1]->AsInteger();
    tjs_int width = param[2]->AsInteger();
    tjs_int height = param[3]->AsInteger();
    clipRect(image, left, top, width, height);

    bool blank = true;
    if(width > 0 && height > 0) {
        for(tjs_int y = top; blank && y < top + height; ++y)
            for(tjs_int x = left; x < left + width; ++x)
                if(nonZero(pixelAt(image, x, y))) {
                    blank = false;
                    break;
                }
    }
    if(result)
        *result = blank;
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD clearAlpha(tTJSVariant *, tjs_int num,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *layer) {
    const int threshold = num <= 0 ? 0 : static_cast<int>(param[0]->AsInteger());
    const tjs_uint32 fillColor =
        static_cast<tjs_uint32>((num > 1 ? param[1]->AsInteger() : 0) &
                                0x00ffffff);
    const LayerImage image = getLayerImage(layer, true);
    for(tjs_int y = 0; y < image.height; ++y) {
        for(tjs_int x = 0; x < image.width; ++x) {
            auto *pixel = writablePixelAt(image, x, y);
            if(pixel[3] <= threshold)
                *reinterpret_cast<tjs_uint32 *>(pixel) = fillColor;
        }
    }
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD getAverageColor(tTJSVariant *result,
                                                 tjs_int num,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *layer) {
    if(num < 4)
        return TJS_E_BADPARAMCOUNT;

    const LayerImage image = getLayerImage(layer, false);
    tjs_int left = param[0]->AsInteger();
    tjs_int top = param[1]->AsInteger();
    tjs_int width = param[2]->AsInteger();
    tjs_int height = param[3]->AsInteger();
    clipRect(image, left, top, width, height);
    if(width <= 0 || height <= 0)
        TVPThrowExceptionMessage(TJS_W("invalid layer range"));

    tjs_uint64 a = 0, r = 0, g = 0, b = 0;
    for(tjs_int y = top; y < top + height; ++y) {
        for(tjs_int x = left; x < left + width; ++x) {
            ReadPtr p = pixelAt(image, x, y);
            b += p[0];
            g += p[1];
            r += p[2];
            a += p[3];
        }
    }
    const tjs_uint64 size = static_cast<tjs_uint64>(width) * height;
    const tjs_uint32 color =
        ((a / size) << 24) | ((r / size) << 16) | ((g / size) << 8) |
        (b / size);
    if(result)
        *result = static_cast<tTVInteger>(color);
    return TJS_S_OK;
}

// The following helpers are small, ABI-neutral portions of layerExSave that
// are useful to existing scripts but are not part of the codec translation
// unit.  They operate on Aether's BGRA layer layout and keep the upstream
// observable ordering/packing intact.
static constexpr tjs_uint64 kFnv1aBasis64 = 0xcbf29ce484222325ULL;
static constexpr tjs_uint64 kFnv1aPrime64 = 0x00000100000001b3ULL;

static inline tjs_uint64 fnv1aByte(tjs_uint64 hash, tjs_uint8 value) {
    return (hash ^ value) * kFnv1aPrime64;
}

static tjs_error TJS_INTF_METHOD getFingerPrintValue(
    tTJSVariant *result, tjs_int num, tTJSVariant **param,
    iTJSDispatch2 *layer) {
    const LayerImage image = getLayerImage(layer, false);
    const bool ignoreTransparent =
        num <= 0 || !param || !param[0] || param[0]->Type() == tvtVoid ||
        static_cast<tjs_int>(*param[0]) != 0;

    tjs_uint64 hash = kFnv1aBasis64;
    for(tjs_int y = 0; y < image.height; ++y) {
        const ReadPtr row = image.read +
                            static_cast<std::ptrdiff_t>(y) * image.pitch;
        for(tjs_int x = 0; x < image.width; ++x) {
            const ReadPtr pixel = row + static_cast<std::ptrdiff_t>(x) * 4;
            if(ignoreTransparent && pixel[3] == 0) {
                hash = fnv1aByte(hash, 0);
                hash = fnv1aByte(hash, 0);
                hash = fnv1aByte(hash, 0);
                hash = fnv1aByte(hash, 0);
            } else {
                hash = fnv1aByte(hash, pixel[0]);
                hash = fnv1aByte(hash, pixel[1]);
                hash = fnv1aByte(hash, pixel[2]);
                hash = fnv1aByte(hash, pixel[3]);
            }
        }
    }
    // Include dimensions exactly as the upstream implementation does (four
    // bytes for width followed by four bytes for height).
    for(int shift = 24; shift >= 0; shift -= 8) {
        hash = fnv1aByte(hash,
                         static_cast<tjs_uint8>((image.width >> shift) & 0xff));
        hash = fnv1aByte(hash,
                         static_cast<tjs_uint8>((image.height >> shift) & 0xff));
    }
    if(result)
        *result = static_cast<tTVInteger>(hash);
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD getShrinkVectorOctet(
    tTJSVariant *result, tjs_int num, tTJSVariant **param,
    iTJSDispatch2 *layer) {
    const tjs_int targetWidth =
        num > 0 && param && param[0] && param[0]->Type() != tvtVoid
            ? static_cast<tjs_int>(*param[0])
            : 16;
    const tjs_int targetHeight =
        num > 1 && param && param[1] && param[1]->Type() != tvtVoid
            ? static_cast<tjs_int>(*param[1])
            : 16;
    // Upstream accidentally accepted zero for svh and divided by zero later;
    // reject it at the ABI boundary instead of crashing the script thread.
    if(targetWidth <= 0 || targetHeight <= 0)
        return TJS_E_INVALIDPARAM;

    const LayerImage image = getLayerImage(layer, false);
    const tjs_int stepWidth = (image.width - 1) / targetWidth + 1;
    const tjs_int stepHeight = (image.height - 1) / targetHeight + 1;
    const tjs_int blockColumns = (image.width - 1) / stepWidth + 1;
    const tjs_int blockRows = (image.height - 1) / stepHeight + 1;
    if(static_cast<std::size_t>(blockRows) >
       std::numeric_limits<std::size_t>::max() /
           static_cast<std::size_t>(blockColumns))
        return TJS_E_FAIL;
    const std::size_t blockCount = static_cast<std::size_t>(blockColumns) *
                                   static_cast<std::size_t>(blockRows);
    if(blockCount > std::numeric_limits<std::size_t>::max() / 4 ||
       blockCount * 4 > std::numeric_limits<tjs_uint>::max())
        return TJS_E_FAIL;

    std::vector<tjs_uint8> packed(blockCount * 4, 0);
    std::size_t outputOffset = 0;
    for(tjs_int top = 0; top < image.height; top += stepHeight) {
        const tjs_int blockHeight = std::min(stepHeight, image.height - top);
        for(tjs_int left = 0; left < image.width; left += stepWidth) {
            const tjs_int blockWidth = std::min(stepWidth, image.width - left);
            tjs_uint64 sums[4] = {0, 0, 0, 0};
            for(tjs_int y = 0; y < blockHeight; ++y) {
                const ReadPtr row = image.read +
                                    static_cast<std::ptrdiff_t>(top + y) *
                                        image.pitch;
                for(tjs_int x = 0; x < blockWidth; ++x) {
                    const ReadPtr pixel = row +
                                          static_cast<std::ptrdiff_t>(left + x) *
                                              4;
                    if(pixel[3] != 0) {
                        sums[0] += pixel[0];
                        sums[1] += pixel[1];
                        sums[2] += pixel[2];
                        sums[3] += pixel[3];
                    }
                }
            }
            const tjs_uint64 count = static_cast<tjs_uint64>(blockWidth) *
                                     static_cast<tjs_uint64>(blockHeight);
            const tjs_uint64 bias = count / 2;
            for(int channel = 0; channel < 4; ++channel)
                packed[outputOffset + static_cast<std::size_t>(channel)] =
                    static_cast<tjs_uint8>((sums[channel] + bias) / count);
            outputOffset += 4;
        }
    }

    if(result) {
        auto *octet = TJSAllocVariantOctet(
            packed.empty() ? nullptr : packed.data(),
            static_cast<tjs_uint>(packed.size()));
        if(!octet)
            return TJS_E_FAIL;
        *result = octet;
        octet->Release();
    }
    return TJS_S_OK;
}

struct OctetVectorSums {
    tjs_uint64 correlation = 0;
    tjs_uint64 distance = 0;
    tjs_uint64 normA = 0;
    tjs_uint64 normB = 0;
    tjs_uint64 sumA = 0;
    tjs_uint64 sumB = 0;
    tjs_uint64 differences = 0;
};

static bool checkedAdd(tjs_uint64 &destination, tjs_uint64 value) {
    if(value > std::numeric_limits<tjs_uint64>::max() - destination)
        return false;
    destination += value;
    return true;
}

static bool accumulateOctetVector(const tTJSVariant *first,
                                  const tTJSVariant *second,
                                  OctetVectorSums &sums) {
    if(!first || first->Type() != tvtOctet)
        return false;
    const auto *a = first->AsOctetNoAddRef();
    const auto *b = second && second->Type() == tvtOctet
                        ? second->AsOctetNoAddRef()
                        : nullptr;
    if(!a || !a->GetData())
        return false;
    if(b && (a->GetLength() != b->GetLength() || !b->GetData()))
        return false;

    const tjs_uint length = a->GetLength();
    const tjs_uint8 *dataA = a->GetData();
    const tjs_uint8 *dataB = b ? b->GetData() : nullptr;
    for(tjs_uint index = 0; index < length; ++index) {
        const tjs_uint64 va = dataA[index];
        if(!checkedAdd(sums.sumA, va) || !checkedAdd(sums.normA, va * va))
            return false;
        if(!dataB) {
            if(va != 0)
                ++sums.differences;
            if(!checkedAdd(sums.distance, va * va))
                return false;
            continue;
        }
        const tjs_uint64 vb = dataB[index];
        const tjs_uint64 delta = va > vb ? va - vb : vb - va;
        if(!checkedAdd(sums.sumB, vb) || !checkedAdd(sums.normB, vb * vb) ||
           !checkedAdd(sums.correlation, va * vb) ||
           !checkedAdd(sums.distance, delta * delta))
            return false;
        if(va != vb)
            ++sums.differences;
    }
    return true;
}

static tjs_error TJS_INTF_METHOD octetVectorSum(
    tTJSVariant *result, tjs_int num, tTJSVariant **param,
    iTJSDispatch2 *) {
    if(num < 2 || !param || !param[0] || !param[1])
        return TJS_E_BADPARAMCOUNT;
    OctetVectorSums sums;
    if(!accumulateOctetVector(param[0], param[1], sums))
        return TJS_E_INVALIDPARAM;
    if(!result)
        return TJS_S_OK;

    iTJSDispatch2 *array = TJSCreateArrayObject();
    if(!array)
        return TJS_E_FAIL;
    const tTVInteger values[] = {
        static_cast<tTVInteger>(sums.correlation),
        static_cast<tTVInteger>(sums.distance),
        static_cast<tTVInteger>(sums.normA),
        static_cast<tTVInteger>(sums.normB),
        static_cast<tTVInteger>(sums.sumA),
        static_cast<tTVInteger>(sums.sumB),
        static_cast<tTVInteger>(sums.differences),
    };
    for(tjs_int index = 0; index < 7; ++index) {
        tTJSVariant value(values[index]);
        array->PropSetByNum(TJS_MEMBERENSURE, index, &value, array);
    }
    *result = tTJSVariant(array, array);
    array->Release();
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD oozecolor(tTJSVariant *result, tjs_int num,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *layer) {
    if(num < 1 || !param || !param[0])
        return TJS_E_BADPARAMCOUNT;
    const tjs_int level = static_cast<tjs_int>(*param[0]);
    if(level <= 0)
        TVPThrowExceptionMessage(TJS_W("Invalid level count."));
    const int threshold = std::clamp(
        num > 1 && param[1] ? static_cast<int>(*param[1]) : 1, 1, 255);
    const tjs_uint32 fillColor = static_cast<tjs_uint32>(
        num > 2 && param[2] ? param[2]->AsInteger() : 0);
    const tjs_uint8 fillR = static_cast<tjs_uint8>((fillColor >> 16) & 0xff);
    const tjs_uint8 fillG = static_cast<tjs_uint8>((fillColor >> 8) & 0xff);
    const tjs_uint8 fillB = static_cast<tjs_uint8>(fillColor & 0xff);

    const LayerImage image = getLayerImage(layer, true);
    const std::size_t mapWidth = static_cast<std::size_t>(image.width) + 2;
    const std::size_t mapHeight = static_cast<std::size_t>(image.height) + 2;
    if(mapWidth > std::numeric_limits<std::size_t>::max() / mapHeight)
        return TJS_E_FAIL;
    std::vector<std::int8_t> processed(mapWidth * mapHeight, 0);
    for(tjs_int y = 0; y < image.height; ++y) {
        const ReadPtr row = image.read +
                            static_cast<std::ptrdiff_t>(y) * image.pitch;
        for(tjs_int x = 0; x < image.width; ++x) {
            const ReadPtr pixel = row + static_cast<std::ptrdiff_t>(x) * 4;
            processed[static_cast<std::size_t>(y + 1) * mapWidth + x + 1] =
                pixel[3] >= threshold ? -1 : 0;
            if(pixel[3] < threshold) {
                auto *writable = image.write +
                                 static_cast<std::ptrdiff_t>(y) * image.pitch +
                                 static_cast<std::ptrdiff_t>(x) * 4;
                writable[0] = fillB;
                writable[1] = fillG;
                writable[2] = fillR;
            }
        }
    }

    for(tjs_int pass = 0; pass < level; ++pass) {
        bool changed = false;
        for(tjs_int y = 0; y < image.height; ++y) {
            const std::size_t mapRow = static_cast<std::size_t>(y + 1) *
                                       mapWidth;
            const ReadPtr row = image.read +
                                static_cast<std::ptrdiff_t>(y) * image.pitch;
            for(tjs_int x = 0; x < image.width; ++x) {
                const std::size_t mapIndex = mapRow + x + 1;
                if(processed[mapIndex] != 0)
                    continue;
                const bool up = processed[mapIndex - mapWidth] < 0;
                const bool down = processed[mapIndex + mapWidth] < 0;
                const bool left = processed[mapIndex - 1] < 0;
                const bool right = processed[mapIndex + 1] < 0;
                const int count = static_cast<int>(up) + static_cast<int>(down) +
                                  static_cast<int>(left) + static_cast<int>(right);
                if(count == 0)
                    continue;

                int blue = 0, green = 0, red = 0;
                auto add = [&](const ReadPtr source) {
                    blue += source[0];
                    green += source[1];
                    red += source[2];
                };
                if(up)
                    add(image.read + static_cast<std::ptrdiff_t>(y - 1) *
                            image.pitch + static_cast<std::ptrdiff_t>(x) * 4);
                if(down)
                    add(image.read + static_cast<std::ptrdiff_t>(y + 1) *
                            image.pitch + static_cast<std::ptrdiff_t>(x) * 4);
                if(left)
                    add(row + static_cast<std::ptrdiff_t>(x - 1) * 4);
                if(right)
                    add(row + static_cast<std::ptrdiff_t>(x + 1) * 4);
                auto *writable = image.write +
                                 static_cast<std::ptrdiff_t>(y) * image.pitch +
                                 static_cast<std::ptrdiff_t>(x) * 4;
                writable[0] = static_cast<tjs_uint8>(blue / count);
                writable[1] = static_cast<tjs_uint8>(green / count);
                writable[2] = static_cast<tjs_uint8>(red / count);
                processed[mapIndex] = 1;
                changed = true;
            }
        }
        if(!changed)
            break;
        for(auto &mark : processed)
            if(mark > 0)
                mark = -1;
    }
    if(result)
        result->Clear();
    return TJS_S_OK;
}

static void writeEncodedFile(const ttstr &filename,
                             const std::vector<std::uint8_t> &bytes) {
    if(bytes.empty() || bytes.size() > std::numeric_limits<tjs_uint>::max())
        TVPThrowExceptionMessage(TJS_W("cannot encode image"));
    std::unique_ptr<tTJSBinaryStream> output(
        TVPCreateStream(filename, TJS_BS_WRITE));
    if(!output)
        TVPThrowExceptionMessage((filename + TJS_W(":can't open")).c_str());
    output->Write(bytes.data(), static_cast<tjs_uint>(bytes.size()));
}

static tjs_error TJS_INTF_METHOD saveProvinceImage(
    tTJSVariant *, tjs_int num, tTJSVariant **param, iTJSDispatch2 *layer) {
    if(num < 1 || !param || !param[0] || param[0]->Type() != tvtString)
        return TJS_E_BADPARAMCOUNT;
    const ProvinceImage image = getProvinceImage(layer);
    std::vector<std::uint8_t> encoded;
    if(!aether::krkrz::layer_save::encodeProvincePng(
           image.read, image.width, image.height, image.pitch, encoded))
        TVPThrowExceptionMessage(TJS_W("cannot encode province image"));
    writeEncodedFile(param[0]->AsStringNoAddRef(), encoded);
    return TJS_S_OK;
}

NCB_ATTACH_FUNCTION(saveLayerImageTlg5, Layer, saveLayerImageTlg5);
NCB_ATTACH_FUNCTION(saveLayerImagePng, Layer, saveLayerImagePng);
NCB_ATTACH_FUNCTION(saveLayerImagePngOctet, Layer, saveLayerImagePngOctet);
NCB_ATTACH_FUNCTION(getCropRect, Layer, getCropRect);
NCB_ATTACH_FUNCTION(getCropRectZero, Layer, getCropRectZero);
NCB_ATTACH_FUNCTION(getDiffRect, Layer, getDiffRect);
NCB_ATTACH_FUNCTION(getDiffPixel, Layer, getDiffPixel);
NCB_ATTACH_FUNCTION(copyBlueToAlpha, Layer, copyBlueToAlpha);
NCB_ATTACH_FUNCTION(isBlank, Layer, isBlank);
NCB_ATTACH_FUNCTION(clearAlpha, Layer, clearAlpha);
NCB_ATTACH_FUNCTION(getAverageColor, Layer, getAverageColor);
NCB_ATTACH_FUNCTION(oozeColor, Layer, oozecolor);
NCB_ATTACH_FUNCTION(getFingerPrintValue, Layer, getFingerPrintValue);
NCB_ATTACH_FUNCTION(getShrinkVectorOctet, Layer, getShrinkVectorOctet);
NCB_ATTACH_FUNCTION(saveProvinceImage, Layer, saveProvinceImage);
NCB_ATTACH_FUNCTION(octetVectorSum, Math, octetVectorSum);
