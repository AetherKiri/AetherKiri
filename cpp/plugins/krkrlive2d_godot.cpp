#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <png.h>
#include <spdlog/spdlog.h>
#include <zlib.h>

#include "DebugIntf.h"
#include "EventIntf.h"
#include "LayerImpl.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "SysInitIntf.h"
#include "ncbind.hpp"
#include "tjs.h"

#include "godot/GodotGpuBridge.h"
#include "godot/GodotRenderManager.h"

#include "bc7_ktx_decode.h"

#include "CubismFramework.hpp"
#include "CubismModelSettingJson.hpp"
#include "Effect/CubismEyeBlink.hpp"
#include "Id/CubismIdManager.hpp"
#include "Math/CubismMatrix44.hpp"
#include "Model/CubismMoc.hpp"
#include "Model/CubismModel.hpp"
#include "Model/CubismModelUserData.hpp"
#include "Model/CubismUserModel.hpp"
#include "Motion/CubismMotion.hpp"
#include "Motion/CubismMotionManager.hpp"
#include "Rendering/CubismRenderer.hpp"
#include "Rendering/csmBlendMode.hpp"

#define NCB_MODULE_NAME TJS_W("krkrlive2d.dll")

using namespace Live2D::Cubism::Framework;

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

CubismRenderer *CubismRenderer::Create(csmUint32, csmUint32) {
    return nullptr;
}

void CubismRenderer::StaticRelease() {}

}}}} // namespace Live2D::Cubism::Framework::Rendering

struct Live2DRenderTarget {
    unsigned int fbo;
    int width;
    int height;
};

Live2DRenderTarget g_live2dRenderTarget = {0, 0, 0};

namespace {

class CubismAllocator final : public ICubismAllocator {
public:
    void *Allocate(const csmSizeType size) override { return std::malloc(size); }
    void Deallocate(void *mem) override { std::free(mem); }
    void *AllocateAligned(const csmSizeType size,
                          const csmUint32 alignment) override {
        const size_t offset = alignment - 1 + sizeof(void *);
        void *raw = std::malloc(size + offset);
        if (!raw) return nullptr;
        auto **aligned = reinterpret_cast<void **>(
            (reinterpret_cast<size_t>(raw) + offset) &
            ~(static_cast<size_t>(alignment) - 1));
        aligned[-1] = raw;
        return aligned;
    }
    void DeallocateAligned(void *mem) override {
        if (mem) std::free(static_cast<void **>(mem)[-1]);
    }
};

CubismAllocator g_cubismAllocator;
bool g_cubismInitialized = false;

void EnsureCubismInitialized() {
    if (g_cubismInitialized) return;
    CubismFramework::Option opt;
    opt.LogFunction = [](const char *msg) { spdlog::debug("Cubism: {}", msg); };
    opt.LoggingLevel = CubismFramework::Option::LogLevel_Warning;
    CubismFramework::StartUp(&g_cubismAllocator, &opt);
    CubismFramework::Initialize();
    g_cubismInitialized = true;
    spdlog::info("krkrlive2d_godot: Cubism SDK initialized");
}

using ZipArchive = std::unordered_map<std::string, std::vector<uint8_t>>;

uint16_t ReadLe16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) |
           static_cast<uint16_t>(p[1] << 8);
}

uint32_t ReadLe32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool InflateRawDeflate(const uint8_t *input, size_t inputSize,
                       std::vector<uint8_t> &output) {
    if (inputSize > std::numeric_limits<uInt>::max() ||
        output.size() > std::numeric_limits<uInt>::max()) {
        return false;
    }

    z_stream stream{};
    stream.next_in =
        const_cast<Bytef *>(reinterpret_cast<const Bytef *>(input));
    stream.avail_in = static_cast<uInt>(inputSize);
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());

    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return false;
    const int ret = inflate(&stream, Z_FINISH);
    const bool ok = ret == Z_STREAM_END &&
                    stream.total_out == static_cast<uLong>(output.size());
    inflateEnd(&stream);
    return ok;
}

bool ExtractZipToMemory(const uint8_t *zipData, size_t zipSize,
                        ZipArchive &out) {
    out.clear();
    if (!zipData || zipSize < 22) return false;

    constexpr uint32_t kEndCentralDirSig = 0x06054b50;
    constexpr uint32_t kCentralFileSig = 0x02014b50;
    constexpr uint32_t kLocalFileSig = 0x04034b50;
    const size_t searchStart =
        zipSize > 0xffffu + 22u ? zipSize - (0xffffu + 22u) : 0;

    size_t eocd = std::string::npos;
    for (size_t pos = zipSize - 22;; --pos) {
        if (ReadLe32(zipData + pos) == kEndCentralDirSig) {
            eocd = pos;
            break;
        }
        if (pos == searchStart) break;
    }
    if (eocd == std::string::npos) return false;

    const uint16_t entryCount = ReadLe16(zipData + eocd + 10);
    const uint32_t centralSize = ReadLe32(zipData + eocd + 12);
    const uint32_t centralOffset = ReadLe32(zipData + eocd + 16);
    if (centralOffset == 0xffffffffu || centralSize == 0xffffffffu) {
        spdlog::warn("krkrlive2d_godot: zip64 Live2D archives are unsupported");
        return false;
    }
    if (static_cast<size_t>(centralOffset) + centralSize > zipSize) {
        return false;
    }

    size_t pos = centralOffset;
    for (uint16_t entry = 0; entry < entryCount; ++entry) {
        if (pos + 46 > zipSize || ReadLe32(zipData + pos) != kCentralFileSig) {
            return false;
        }

        const uint16_t method = ReadLe16(zipData + pos + 10);
        const uint32_t compressedSize = ReadLe32(zipData + pos + 20);
        const uint32_t uncompressedSize = ReadLe32(zipData + pos + 24);
        const uint16_t nameLen = ReadLe16(zipData + pos + 28);
        const uint16_t extraLen = ReadLe16(zipData + pos + 30);
        const uint16_t commentLen = ReadLe16(zipData + pos + 32);
        const uint32_t localOffset = ReadLe32(zipData + pos + 42);
        const size_t nextPos = pos + 46u + nameLen + extraLen + commentLen;
        if (nextPos > zipSize || pos + 46u + nameLen > zipSize) return false;

        std::string name(reinterpret_cast<const char *>(zipData + pos + 46),
                         nameLen);
        pos = nextPos;
        if (name.empty() || name.back() == '/') continue;
        if (uncompressedSize == 0) {
            out[name] = {};
            continue;
        }
        if (static_cast<size_t>(localOffset) + 30u > zipSize ||
            ReadLe32(zipData + localOffset) != kLocalFileSig) {
            return false;
        }
        const uint16_t localNameLen = ReadLe16(zipData + localOffset + 26);
        const uint16_t localExtraLen = ReadLe16(zipData + localOffset + 28);
        const size_t dataOffset = static_cast<size_t>(localOffset) + 30u +
                                  localNameLen + localExtraLen;
        if (dataOffset + compressedSize > zipSize) return false;

        std::vector<uint8_t> bytes(uncompressedSize);
        const uint8_t *compressed = zipData + dataOffset;
        if (method == 0) {
            if (compressedSize != uncompressedSize) return false;
            std::memcpy(bytes.data(), compressed, uncompressedSize);
        } else if (method == 8) {
            if (!InflateRawDeflate(compressed, compressedSize, bytes)) {
                spdlog::warn("krkrlive2d_godot: failed to inflate {}", name);
                continue;
            }
        } else {
            spdlog::warn("krkrlive2d_godot: unsupported zip method {} for {}",
                         method, name);
            continue;
        }
        out[name] = std::move(bytes);
    }
    return !out.empty();
}

std::string NormalizePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.rfind("./", 0) == 0) path.erase(0, 2);
    return path;
}

std::string Basename(const std::string &path) {
    const std::string normalized = NormalizePath(path);
    const size_t slash = normalized.find_last_of('/');
    return slash == std::string::npos ? normalized : normalized.substr(slash + 1);
}

std::string Stem(const std::string &path) {
    std::string base = Basename(path);
    const size_t dot = base.find_last_of('.');
    return dot == std::string::npos ? base : base.substr(0, dot);
}

std::string MotionLookupKey(const std::string &path) {
    std::string key = Stem(path);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return key;
}

ZipArchive::const_iterator FindArchiveEntry(const ZipArchive &archive,
                                            const std::string &name) {
    const std::string normalized = NormalizePath(name);
    auto it = archive.find(normalized);
    if (it != archive.end()) return it;

    const std::string base = Basename(normalized);
    for (auto cand = archive.begin(); cand != archive.end(); ++cand) {
        const std::string candPath = NormalizePath(cand->first);
        if (candPath == normalized || Basename(candPath) == base ||
            (candPath.size() > normalized.size() &&
             candPath.compare(candPath.size() - normalized.size(),
                              normalized.size(), normalized) == 0)) {
            return cand;
        }
    }
    return archive.end();
}

bool LoadStorageBytes(const ttstr &path, std::vector<uint8_t> &out) {
    out.clear();
    try {
        ttstr placed = TVPGetPlacedPath(path);
        const ttstr &resolved = placed.IsEmpty() ? path : placed;
        tTJSBinaryStream *stream = TVPCreateStream(resolved, TJS_BS_READ);
        if (!stream) return false;
        const tjs_uint64 size = stream->GetSize();
        if (size > static_cast<tjs_uint64>(std::numeric_limits<int>::max())) {
            delete stream;
            return false;
        }
        out.resize(static_cast<size_t>(size));
        if (size > 0) {
            stream->ReadBuffer(out.data(), static_cast<tjs_uint>(size));
        }
        delete stream;
        return true;
    } catch (...) {
        return false;
    }
}

ttstr ResolveStorageDir(const ttstr &storage) {
    ttstr placed = TVPGetPlacedPath(storage);
    if (!placed.IsEmpty()) return TVPExtractStoragePath(placed);
    ttstr normalized = TVPNormalizeStorageName(storage);
    return TVPExtractStoragePath(normalized);
}

struct PngMemReader {
    const uint8_t *data = nullptr;
    size_t size = 0;
    size_t offset = 0;
};

void PngReadMemory(png_structp png, png_bytep out, png_size_t count) {
    auto *reader = static_cast<PngMemReader *>(png_get_io_ptr(png));
    if (!reader || reader->offset + count > reader->size) {
        png_error(png, "read past end");
        return;
    }
    std::memcpy(out, reader->data + reader->offset, count);
    reader->offset += count;
}

bool DecodePngRgba(const uint8_t *data, size_t size, aetherkiri::RgbaImage &out) {
    out = aetherkiri::RgbaImage{};
    if (size < 8 || png_sig_cmp(data, 0, 8) != 0) return false;

    png_structp png =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) return false;
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        out = aetherkiri::RgbaImage{};
        return false;
    }

    PngMemReader reader{data, size, 0};
    png_set_read_fn(png, &reader, PngReadMemory);
    png_read_info(png, info);

    const png_uint_32 width = png_get_image_width(png, info);
    const png_uint_32 height = png_get_image_height(png, info);
    png_byte colorType = png_get_color_type(png, info);
    png_byte bitDepth = png_get_bit_depth(png, info);

    if (bitDepth == 16) png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY ||
        colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xff, PNG_FILLER_AFTER);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY ||
        colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    png_read_update_info(png, info);

    const size_t rowBytes = png_get_rowbytes(png, info);
    std::vector<uint8_t> rows(rowBytes * height);
    std::vector<png_bytep> rowPointers(height);
    for (png_uint_32 y = 0; y < height; ++y) {
        rowPointers[y] = rows.data() + static_cast<size_t>(y) * rowBytes;
    }
    png_read_image(png, rowPointers.data());
    png_destroy_read_struct(&png, &info, nullptr);

    out.width = width;
    out.height = height;
    out.pixels.resize(static_cast<size_t>(width) * height * 4u);
    for (png_uint_32 y = 0; y < height; ++y) {
        std::memcpy(out.pixels.data() + static_cast<size_t>(y) * width * 4u,
                    rows.data() + static_cast<size_t>(y) * rowBytes,
                    static_cast<size_t>(width) * 4u);
    }
    return true;
}

bool DecodeTextureRgba(const std::string &path, const uint8_t *data, size_t size,
                       aetherkiri::RgbaImage &out) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".ktx") {
        return aetherkiri::DecodeKtxToRgba(data, size, out);
    }
    return DecodePngRgba(data, size, out);
}

void ApplyLive2DChromaKey(aetherkiri::RgbaImage &image,
                          const std::string &path) {
    if (image.pixels.empty()) return;

    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".ktx") {
        return;
    }

    size_t keyed = 0;
    for (size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
        const int r = image.pixels[i + 0];
        const int g = image.pixels[i + 1];
        const int b = image.pixels[i + 2];
        if (g >= 180 && g > r + 55 && g > b + 55) {
            image.pixels[i + 3] = 0;
            ++keyed;
        }
    }
    if (keyed > 0) {
        spdlog::info("krkrlive2d_godot: cleared {} chroma-key pixels in {}",
                     keyed, path);
    }
}

ttstr ToTTStr(const tTJSVariant &v) {
    return v.Type() == tvtVoid ? ttstr() : ttstr(v);
}

std::string ToKey(const tTJSVariant &v) {
    return ToTTStr(v).AsStdString();
}

tjs_real ToReal(const tTJSVariant &v, tjs_real fallback = 0.0) {
    switch (v.Type()) {
        case tvtInteger:
        case tvtReal:
            return static_cast<tjs_real>(v);
        default:
            return fallback;
    }
}

tjs_int ToInt(const tTJSVariant &v, tjs_int fallback = 0) {
    switch (v.Type()) {
        case tvtInteger:
            return static_cast<tjs_int>(v);
        case tvtReal:
            return static_cast<tjs_int>(static_cast<tjs_real>(v));
        default:
            return fallback;
    }
}

void SetIntResult(tTJSVariant *result, tjs_int value = 1) {
    if (result) *result = value;
}

void SetBoolResult(tTJSVariant *result, bool value) {
    if (result) *result = static_cast<tjs_int>(value ? 1 : 0);
}

void SetStringResult(tTJSVariant *result, const tjs_char *value = TJS_W("")) {
    if (result) *result = value;
}

void SetResultObject(tTJSVariant *result, iTJSDispatch2 *object) {
    if (result && object) *result = tTJSVariant(object, object);
}

void SetArrayResult(tTJSVariant *result) {
    if (!result) return;
    iTJSDispatch2 *array = TJSCreateArrayObject();
    *result = tTJSVariant(array, array);
    array->Release();
}

iTJSDispatch2 *CreateIdNameDict(const ttstr &id, const ttstr &name) {
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if (!dict) return nullptr;
    tTJSVariant idValue(id);
    tTJSVariant nameValue(name);
    dict->PropSet(TJS_MEMBERENSURE, TJS_W("id"), nullptr, &idValue, dict);
    dict->PropSet(TJS_MEMBERENSURE, TJS_W("name"), nullptr, &nameValue, dict);
    return dict;
}

iTJSDispatch2 *CreateStringArray(const std::vector<ttstr> &items) {
    iTJSDispatch2 *array = TJSCreateArrayObject();
    if (!array) return nullptr;
    for (tjs_int i = 0; i < static_cast<tjs_int>(items.size()); ++i) {
        tTJSVariant value(items[static_cast<size_t>(i)]);
        array->PropSetByNum(TJS_MEMBERENSURE, i, &value, array);
    }
    return array;
}

tjs_error CreateLive2DObject(tTJSVariant *result, const tjs_char *expr) {
    if (!result) return TJS_S_OK;
    try {
        TVPExecuteExpression(ttstr(expr), result);
    } catch (...) {
        result->Clear();
    }
    return TJS_S_OK;
}

tTJSNI_BaseLayer *NativeLayerFromDispatch(iTJSDispatch2 *object) {
    if (!object) return nullptr;
    tTJSNI_BaseLayer *layer = nullptr;
    if (TJS_SUCCEEDED(object->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
            reinterpret_cast<iTJSNativeInstance **>(&layer))) &&
        layer) {
        return layer;
    }
    return nullptr;
}

class GodotLive2DModel;
std::vector<GodotLive2DModel *> g_activeModels;

class GodotLive2DModel final : public CubismUserModel {
public:
    struct Texture {
        uint64_t handle = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> pixels;
    };

    static constexpr int kMaskTextureSize = 512;

    struct MaskContext {
        std::vector<csmInt32> maskDrawables;
        std::vector<csmInt32> clippedDrawables;
        uint64_t handle = 0;
        std::vector<uint8_t> pixels;
        float boundsX = 0.0f;
        float boundsY = 0.0f;
        float boundsW = 1.0f;
        float boundsH = 1.0f;
        bool usingMask = false;
    };

    struct MosaicRect {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    GodotLive2DModel() { g_activeModels.push_back(this); }

    ~GodotLive2DModel() override {
        g_activeModels.erase(
            std::remove(g_activeModels.begin(), g_activeModels.end(), this),
            g_activeModels.end());
        ReleaseTextures();
        for (auto &entry : motions_) {
            ACubismMotion::Delete(entry.second);
        }
        motions_.clear();
        if (setting_) {
            CSM_DELETE(setting_);
            setting_ = nullptr;
        }
    }

    bool LoadFromL2D(const std::vector<uint8_t> &zipData,
                     const std::string &baseName,
                     const ttstr &storageDir) {
        EnsureCubismInitialized();
        baseName_ = baseName;
        storageDir_ = storageDir;

        ZipArchive archive;
        if (!ExtractZipToMemory(zipData.data(), zipData.size(), archive)) {
            spdlog::error("krkrlive2d_godot: failed to extract {}", baseName);
            return false;
        }

        auto jsonIt = FindArchiveEntry(archive, baseName + ".model3.json");
        if (jsonIt == archive.end()) {
            for (auto it = archive.begin(); it != archive.end(); ++it) {
                if (NormalizePath(it->first).find(".model3.json") !=
                    std::string::npos) {
                    jsonIt = it;
                    break;
                }
            }
        }
        if (jsonIt == archive.end()) {
            spdlog::error("krkrlive2d_godot: model3.json not found for {}",
                          baseName);
            return false;
        }

        setting_ = CSM_NEW CubismModelSettingJson(
            jsonIt->second.data(),
            static_cast<csmSizeType>(jsonIt->second.size()));
        if (!setting_) return false;

        std::string mocName = setting_->GetModelFileName()
                                  ? setting_->GetModelFileName()
                                  : "";
        if (mocName.empty()) mocName = baseName + ".moc3";
        auto mocIt = FindArchiveEntry(archive, mocName);
        if (mocIt == archive.end()) {
            spdlog::error("krkrlive2d_godot: moc3 not found: {}", mocName);
            return false;
        }
        LoadModel(mocIt->second.data(),
                  static_cast<csmSizeType>(mocIt->second.size()));
        if (!_moc || !_model) {
            spdlog::error("krkrlive2d_godot: failed to create model: {}",
                          baseName);
            return false;
        }

        csmMap<csmString, csmFloat32> layout;
        if (_modelMatrix && setting_->GetLayoutMap(layout)) {
            _modelMatrix->SetupFromLayout(layout);
        }

        if (!LoadTextures(archive)) return false;
        CaptureDefaultState();
        BuildMaskContexts();
        LoadEyeBlinkAndMotions(archive);
        DetectMosaicDrawables(archive);

        loaded_ = true;
        visible_ = true;
        int maskedDrawables = 0;
        if (const csmInt32 *maskCounts = GetModel()->GetDrawableMaskCounts()) {
            const csmInt32 drawableCount = GetModel()->GetDrawableCount();
            for (csmInt32 i = 0; i < drawableCount; ++i) {
                if (maskCounts[i] > 0) ++maskedDrawables;
            }
        }
        int maskedOffscreens = 0;
        if (const csmInt32 *maskCounts = GetModel()->GetOffscreenMaskCounts()) {
            const csmInt32 offscreenCount = GetModel()->GetOffscreenCount();
            for (csmInt32 i = 0; i < offscreenCount; ++i) {
                if (maskCounts[i] > 0) ++maskedOffscreens;
            }
        }
        spdlog::info(
            "krkrlive2d_godot: loaded {} ({} drawables, {} masked, {} offscreens, {} masked offscreens, {} textures, {} motions) canvas={:.2f}x{:.2f} canvasPx={}x{} ppu={:.0f}",
            baseName_, GetModel()->GetDrawableCount(), maskedDrawables,
            GetModel()->GetOffscreenCount(), maskedOffscreens,
            static_cast<int>(textures_.size()), static_cast<int>(motions_.size()),
            GetModel()->GetCanvasWidth(), GetModel()->GetCanvasHeight(),
            static_cast<int>(GetModel()->GetCanvasWidthPixel()),
            static_cast<int>(GetModel()->GetCanvasHeightPixel()),
            GetModel()->GetPixelsPerUnit());
        LogDrawableBlendStats();
        LogMaskContexts();
        return true;
    }

    void SetVisible(bool visible) { visible_ = visible; }
    bool IsVisible() const { return visible_; }
    bool IsLoaded() const { return loaded_ && GetModel() != nullptr; }
    CubismModel *Cubism() { return GetModel(); }

    void SetMosaicSize(float x, float y) {
        mosaicSizeX_ = x;
        mosaicSizeY_ = y;
    }

    bool HasMosaicDrawables() const { return !mosaicDrawableIndices_.empty(); }

    void Progress() { UpdateModel(); }

    void ResetPartsToDefaults() {
        CubismModel *model = GetModel();
        if (!model) return;
        const csmInt32 count = model->GetPartCount();
        const csmInt32 defaults =
            static_cast<csmInt32>(defaultPartOpacities_.size());
        for (csmInt32 i = 0; i < count; ++i) {
            const csmFloat32 opacity =
                (i < defaults) ? defaultPartOpacities_[static_cast<size_t>(i)]
                               : 1.0f;
            model->SetPartOpacity(i, opacity);
        }
        spdlog::info("krkrlive2d_godot: reset {} parts to defaults", count);
    }

    bool SetPartOpacityByKey(const std::string &key, csmFloat32 value) {
        CubismModel *model = GetModel();
        if (!model || key.empty()) return false;
        CubismIdHandle id = CubismFramework::GetIdManager()->GetId(key.c_str());
        const csmInt32 index = model->GetPartIndex(id);
        if (index < 0) return false;
        model->SetPartOpacity(index, value);
        return true;
    }

    size_t GetMotionGroupCount() const { return motionGroupNames_.size(); }

    std::string GetMotionGroupName(int index) const {
        if (index < 0 || index >= static_cast<int>(motionGroupNames_.size())) {
            return std::string();
        }
        return motionGroupNames_[static_cast<size_t>(index)];
    }

    int GetMotionCount(const std::string &group) const {
        const auto *motions = FindMotionNamesForGroup(group);
        return motions ? static_cast<int>(motions->size()) : 0;
    }

    std::string GetMotionName(const std::string &group, int index) const {
        const auto *motions = FindMotionNamesForGroup(group);
        if (!motions || index < 0 || index >= static_cast<int>(motions->size())) {
            return std::string();
        }
        return (*motions)[static_cast<size_t>(index)];
    }

    void StopMotions() {
        if (_motionManager) _motionManager->StopAllMotions();
    }

    bool MotionsFinished() {
        return !_motionManager || _motionManager->IsFinished();
    }

    bool StartMotionByName(const std::string &motionName) {
        if (!_motionManager || motionName.empty()) return false;
        ACubismMotion *selected = nullptr;
        const std::string keyName = MotionLookupKey(motionName);
        auto exact = motionNames_.find(keyName);
        if (exact != motionNames_.end()) selected = exact->second;
        if (!selected) {
            for (const auto &entry : motionNames_) {
                const std::string &key = entry.first;
                if (key == keyName ||
                    (key.size() > keyName.size() &&
                     key.compare(key.size() - keyName.size(), keyName.size(),
                                 keyName) == 0 &&
                     key[key.size() - keyName.size() - 1] == '/')) {
                    selected = entry.second;
                    break;
                }
            }
        }
        if (!selected) {
            spdlog::warn(
                "krkrlive2d_godot: motion lookup failed name='{}' key='{}' registered={}",
                motionName, keyName, motionNames_.size());
            return false;
        }
        _motionManager->StopAllMotions();
        selected->SetLoop(true);
        _motionManager->StartMotionPriority(selected, false, 2);
        spdlog::info("krkrlive2d_godot: started motion '{}' key='{}'",
                     motionName, keyName);
        return true;
    }

    bool StartMotion(const std::string &group, const std::string &motion) {
        if (!_motionManager) return false;
        ACubismMotion *selected = nullptr;
        if (!motion.empty()) {
            if (StartMotionByName(motion)) return true;
            auto named = motionNames_.find(group + "/" + motion);
            if (named != motionNames_.end()) selected = named->second;
            if (!selected) {
                for (const auto &entry : motionNames_) {
                    if (entry.first.find(group + "/") == 0 &&
                        entry.first.find(motion) != std::string::npos) {
                        selected = entry.second;
                        break;
                    }
                }
            }
        }
        if (!selected && motion.empty()) {
            auto it = firstMotionByGroup_.find(group);
            if (it != firstMotionByGroup_.end()) selected = it->second;
        }
        if (!selected && motion.empty() && !motions_.empty()) {
            selected = motions_.begin()->second;
        }
        if (selected) {
            _motionManager->StopAllMotions();
            selected->SetLoop(true);
            _motionManager->StartMotionPriority(selected, false, 2);
            spdlog::info("krkrlive2d_godot: started motion group='{}' motion='{}'",
                         group, motion);
            return true;
        }
        spdlog::warn("krkrlive2d_godot: motion start failed group='{}' motion='{}'",
                     group, motion);
        return false;
    }

    bool StartMotionByIndex(const std::string &group, int index) {
        const std::string key = group + "_" + std::to_string(index);
        auto it = motions_.find(key);
        if (it != motions_.end() && _motionManager) {
            _motionManager->StopAllMotions();
            it->second->SetLoop(true);
            _motionManager->StartMotionPriority(it->second, false, 2);
            spdlog::info("krkrlive2d_godot: started motion group='{}' index={}",
                         group, index);
            return true;
        } else {
            return StartMotion(group, std::string());
        }
    }

    bool RenderToLayer(tTJSNI_BaseLayer *layer, bool clearTarget) {
        if (!IsLoaded() || !visible_ || !layer) return false;
        tTVPBaseTexture *image = layer->GetMainImage();
        if (!image) return false;
        auto *target = dynamic_cast<GodotTexture2D *>(image->GetTexture());
        if (!target || !target->EnsureGpuHandle()) return false;

        const int width = target->GetWidth();
        const int height = target->GetHeight();
        if (width <= 0 || height <= 0) return false;
        if (clearTarget) {
            const tTVPRect full(0, 0, width, height);
            target->ClearGpu(0x00000000u, full);
        }

        UpdateModel();
        UpdateProjection(width, height);
        UpdateMasks();

        const bool drew = DrawModelToTexture(target, width, height);
        if (drew) {
            target->MarkGpuDirty();
            target->UploadCpuToGpu(true);
            layer->Update(false);
        }
        return drew;
    }

private:
    bool LoadTextures(const ZipArchive &archive) {
        const auto *bridge = TVPGodotGpuBridgeGet();
        if (!bridge || !bridge->create_rgba) {
            spdlog::warn("krkrlive2d_godot: Godot GPU bridge unavailable");
            return false;
        }

        const csmInt32 textureCount = setting_->GetTextureCount();
        textures_.resize(static_cast<size_t>(std::max<csmInt32>(textureCount, 0)));
        for (csmInt32 i = 0; i < textureCount; ++i) {
            std::string path = setting_->GetTextureFileName(i)
                                   ? setting_->GetTextureFileName(i)
                                   : "";
            if (path.empty()) continue;
            const std::string ktxPath = aetherkiri::WithKtxExtension(path);
            aetherkiri::RgbaImage image;
            std::string loadedPath;

            for (const std::string &archivePath : {ktxPath, path}) {
                auto texIt = FindArchiveEntry(archive, archivePath);
                if (texIt == archive.end()) continue;
                if (!DecodeTextureRgba(archivePath, texIt->second.data(),
                                       texIt->second.size(), image)) {
                    spdlog::warn("krkrlive2d_godot: texture decode failed: {}",
                                 archivePath);
                    continue;
                }
                ApplyLive2DChromaKey(image, archivePath);
                loadedPath = archivePath;
                break;
            }

            if (loadedPath.empty()) {
                std::vector<uint8_t> externalBytes;
                std::vector<ttstr> candidates;
                for (const std::string &candidatePath : {ktxPath, path}) {
                    if (!storageDir_.IsEmpty())
                        candidates.push_back(storageDir_ +
                                             ttstr(candidatePath.c_str()));
                    if (!TVPProjectDir.IsEmpty())
                        candidates.push_back(TVPProjectDir +
                                             ttstr(candidatePath.c_str()));
                }

                ttstr externalPath;
                for (const auto &candidate : candidates) {
                    if (LoadStorageBytes(candidate, externalBytes)) {
                        externalPath = candidate;
                        const std::string externalName =
                            TVPExtractStorageName(candidate).AsStdString();
                        if (!DecodeTextureRgba(externalName,
                                               externalBytes.data(),
                                               externalBytes.size(), image)) {
                            spdlog::warn(
                                "krkrlive2d_godot: external texture decode failed: {}",
                                externalPath.AsStdString());
                            externalPath = ttstr();
                            externalBytes.clear();
                            continue;
                        }
                        ApplyLive2DChromaKey(image, externalName);
                        break;
                    }
                }

                if (!externalPath.IsEmpty()) {
                    spdlog::info(
                        "krkrlive2d_godot: loaded external texture {}",
                        externalPath.AsStdString());
                    loadedPath = externalPath.AsStdString();
                } else {
                    spdlog::warn(
                        "krkrlive2d_godot: texture not found: {} or {} (dir={}, project={})",
                        ktxPath, path, storageDir_.AsStdString(),
                        TVPProjectDir.AsStdString());
                    continue;
                }
            }

            const uint64_t handle =
                bridge->create_rgba(image.width, image.height,
                                    image.pixels.data(), image.width * 4u);
            if (handle == 0) {
                spdlog::warn("krkrlive2d_godot: texture upload failed: {}",
                             loadedPath);
                continue;
            }
            spdlog::info("krkrlive2d_godot: loaded texture {} ({}x{})",
                         loadedPath, image.width, image.height);
            Texture texture;
            texture.handle = handle;
            texture.width = image.width;
            texture.height = image.height;
            texture.pixels = std::move(image.pixels);
            textures_[static_cast<size_t>(i)] = std::move(texture);
        }
        return std::any_of(textures_.begin(), textures_.end(),
                           [](const Texture &tex) { return tex.handle != 0; });
    }

    std::string MaskKey(const csmInt32 *masks, csmInt32 count) const {
        std::vector<csmInt32> sorted;
        sorted.reserve(static_cast<size_t>(count));
        for (csmInt32 i = 0; i < count; ++i) sorted.push_back(masks[i]);
        std::sort(sorted.begin(), sorted.end());
        std::string key;
        for (csmInt32 value : sorted) {
            key += std::to_string(value);
            key += ",";
        }
        return key;
    }

    void BuildMaskContexts() {
        CubismModel *model = GetModel();
        if (!model) return;
        const csmInt32 drawableCount = model->GetDrawableCount();
        drawableMaskContext_.assign(static_cast<size_t>(drawableCount), -1);

        const csmInt32 *maskCounts = model->GetDrawableMaskCounts();
        const csmInt32 **masks = model->GetDrawableMasks();
        if (!maskCounts || !masks) return;

        const auto *bridge = TVPGodotGpuBridgeGet();
        if (!bridge || !bridge->create_rgba || !bridge->update_rgba) {
            return;
        }

        std::unordered_map<std::string, int> contextByKey;
        std::vector<uint8_t> empty(static_cast<size_t>(kMaskTextureSize) *
                                   kMaskTextureSize * 4u, 0);
        for (csmInt32 drawable = 0; drawable < drawableCount; ++drawable) {
            if (maskCounts[drawable] <= 0 || !masks[drawable]) continue;
            const std::string key = MaskKey(masks[drawable], maskCounts[drawable]);
            int contextIndex = -1;
            auto found = contextByKey.find(key);
            if (found == contextByKey.end()) {
                MaskContext context;
                context.maskDrawables.reserve(
                    static_cast<size_t>(maskCounts[drawable]));
                for (csmInt32 i = 0; i < maskCounts[drawable]; ++i) {
                    context.maskDrawables.push_back(masks[drawable][i]);
                }
                std::sort(context.maskDrawables.begin(),
                          context.maskDrawables.end());
                context.pixels = empty;
                context.handle = bridge->create_rgba(
                    kMaskTextureSize, kMaskTextureSize, context.pixels.data(),
                    kMaskTextureSize * 4u);
                if (context.handle == 0) continue;
                contextIndex = static_cast<int>(maskContexts_.size());
                contextByKey.emplace(key, contextIndex);
                maskContexts_.push_back(std::move(context));
            } else {
                contextIndex = found->second;
            }
            if (contextIndex >= 0) {
                maskContexts_[static_cast<size_t>(contextIndex)]
                    .clippedDrawables.push_back(drawable);
                drawableMaskContext_[static_cast<size_t>(drawable)] =
                    contextIndex;
            }
        }

        if (!maskContexts_.empty()) {
            spdlog::info("krkrlive2d_godot: prepared {} mask contexts",
                         maskContexts_.size());
        }
    }

    void LoadEyeBlinkAndMotions(const ZipArchive &archive) {
        const csmInt32 eyeBlinkCount = setting_->GetEyeBlinkParameterCount();
        for (csmInt32 i = 0; i < eyeBlinkCount; ++i) {
            CubismIdHandle id = setting_->GetEyeBlinkParameterId(i);
            if (id) _eyeBlinkIds.PushBack(id);
        }
        if (_eyeBlinkIds.GetSize() > 0) _eyeBlink = CubismEyeBlink::Create(setting_);

        const csmInt32 lipSyncCount = setting_->GetLipSyncParameterCount();
        for (csmInt32 i = 0; i < lipSyncCount; ++i) {
            CubismIdHandle id = setting_->GetLipSyncParameterId(i);
            if (id) _lipSyncIds.PushBack(id);
        }

        const csmInt32 groupCount = setting_->GetMotionGroupCount();
        for (csmInt32 g = 0; g < groupCount; ++g) {
            const char *groupName = setting_->GetMotionGroupName(g);
            if (!groupName) continue;
            const std::string group(groupName);
            motionGroupNames_.push_back(group);
            auto &groupMotions = motionNamesByGroup_[group];
            const csmInt32 motionCount = setting_->GetMotionCount(groupName);
            for (csmInt32 m = 0; m < motionCount; ++m) {
                const char *motionFile = setting_->GetMotionFileName(groupName, m);
                if (!motionFile) continue;
                const std::string motionPath(motionFile);
                auto motionIt = FindArchiveEntry(archive, motionPath);
                if (motionIt == archive.end()) continue;
                auto *motion = static_cast<CubismMotion *>(CubismMotion::Create(
                    motionIt->second.data(),
                    static_cast<csmSizeType>(motionIt->second.size())));
                if (!motion) continue;
                const csmFloat32 fadeIn =
                    setting_->GetMotionFadeInTimeValue(groupName, m);
                const csmFloat32 fadeOut =
                    setting_->GetMotionFadeOutTimeValue(groupName, m);
                if (fadeIn >= 0.0f) motion->SetFadeInTime(fadeIn);
                if (fadeOut >= 0.0f) motion->SetFadeOutTime(fadeOut);
                motion->SetEffectIds(_eyeBlinkIds, _lipSyncIds);

                const std::string key = group + "_" + std::to_string(m);
                motions_[key] = motion;
                firstMotionByGroup_.try_emplace(group, motion);
                const std::string stem = Stem(motionPath);
                groupMotions.push_back(stem);
                const std::string lookupKey = MotionLookupKey(motionPath);
                motionNames_[MotionLookupKey(group + "/" + stem)] = motion;
                motionNames_[MotionLookupKey(stem)] = motion;
                motionNames_[lookupKey] = motion;
                spdlog::debug(
                    "krkrlive2d_godot: registered motion group='{}' file='{}' key='{}'",
                    group, motionPath, lookupKey);
            }
        }
        spdlog::info(
            "krkrlive2d_godot: registered {} motion groups and {} motions; waiting for script startMotion",
            motionGroupNames_.size(), motions_.size());
    }

    void CaptureDefaultState() {
        defaultPartOpacities_.clear();
        CubismModel *model = GetModel();
        if (!model) return;
        const csmInt32 partCount = model->GetPartCount();
        defaultPartOpacities_.reserve(static_cast<size_t>(partCount));
        for (csmInt32 i = 0; i < partCount; ++i) {
            defaultPartOpacities_.push_back(model->GetPartOpacity(i));
        }
    }

    static bool EqualsAsciiIgnoreCase(const char *lhs, const char *rhs) {
        if (!lhs || !rhs) return false;
        while (*lhs && *rhs) {
            if (std::tolower(static_cast<unsigned char>(*lhs)) !=
                std::tolower(static_cast<unsigned char>(*rhs))) {
                return false;
            }
            ++lhs;
            ++rhs;
        }
        return *lhs == '\0' && *rhs == '\0';
    }

    void DetectMosaicDrawables(const ZipArchive &archive) {
        mosaicDrawableIndices_.clear();
        mosaicParentPartIndices_.clear();
        mosaicParentOpacityDefaults_.clear();
        mosaicRects_.clear();

        if (!GetModel() || !setting_) return;

        std::string userDataPath;
        if (setting_->GetUserDataFile()) {
            csmString s = setting_->GetUserDataFile();
            userDataPath = s.GetRawString();
        }
        if (userDataPath.empty()) userDataPath = baseName_ + ".userdata3.json";

        auto userDataIt = FindArchiveEntry(archive, userDataPath);
        if (userDataIt == archive.end()) return;

        CubismModelUserData *userData = CubismModelUserData::Create(
            userDataIt->second.data(),
            static_cast<csmSizeInt>(userDataIt->second.size()));
        if (!userData) return;

        std::unordered_set<csmInt32> drawableSet;
        std::unordered_set<csmInt32> partSet;
        const auto &nodes = userData->GetArtMeshUserDatas();
        for (csmUint32 i = 0; i < nodes.GetSize(); ++i) {
            const auto *node = nodes[i];
            if (!node) continue;
            if (!EqualsAsciiIgnoreCase(node->Value.GetRawString(), "mosaic")) {
                continue;
            }

            const csmInt32 drawableIndex =
                GetModel()->GetDrawableIndex(node->TargetId);
            if (drawableIndex < 0) continue;

            if (drawableSet.insert(drawableIndex).second) {
                mosaicDrawableIndices_.push_back(drawableIndex);
            }

            const csmInt32 partIndex =
                GetModel()->GetDrawableParentPartIndex(
                    static_cast<csmUint32>(drawableIndex));
            if (partIndex >= 0 && partSet.insert(partIndex).second) {
                mosaicParentPartIndices_.push_back(partIndex);
                mosaicParentOpacityDefaults_[partIndex] =
                    GetModel()->GetPartOpacity(partIndex);
            }
        }
        CubismModelUserData::Delete(userData);

        if (!mosaicDrawableIndices_.empty()) {
            spdlog::info(
                "krkrlive2d_godot: detected {} mosaic drawables ({} parent parts) in {}",
                static_cast<int>(mosaicDrawableIndices_.size()),
                static_cast<int>(mosaicParentPartIndices_.size()), baseName_);
        }
    }

    bool IsMosaicEnabled() const {
        if (mosaicDrawableIndices_.empty()) return false;
        const float x = (mosaicSizeX_ > 0.0f) ? mosaicSizeX_ : mosaicSizeY_;
        const float y = (mosaicSizeY_ > 0.0f) ? mosaicSizeY_ : mosaicSizeX_;
        return x >= 2.0f || y >= 2.0f;
    }

    int GetMosaicBlockX() const {
        float value = (mosaicSizeX_ > 0.0f) ? mosaicSizeX_ : mosaicSizeY_;
        value = std::clamp(value, 1.0f, 256.0f);
        return std::max(1, static_cast<int>(std::lround(value)));
    }

    int GetMosaicBlockY() const {
        float value = (mosaicSizeY_ > 0.0f) ? mosaicSizeY_ : mosaicSizeX_;
        value = std::clamp(value, 1.0f, 256.0f);
        return std::max(1, static_cast<int>(std::lround(value)));
    }

    static bool RectsOverlapOrTouch(const MosaicRect &a, const MosaicRect &b,
                                    int pad) {
        const int ax1 = a.x + a.w + pad;
        const int ay1 = a.y + a.h + pad;
        const int bx1 = b.x + b.w + pad;
        const int by1 = b.y + b.h + pad;
        return !(ax1 < b.x || bx1 < a.x || ay1 < b.y || by1 < a.y);
    }

    static MosaicRect MergeRects(const MosaicRect &a, const MosaicRect &b) {
        const int x0 = std::min(a.x, b.x);
        const int y0 = std::min(a.y, b.y);
        const int x1 = std::max(a.x + a.w, b.x + b.w);
        const int y1 = std::max(a.y + a.h, b.y + b.h);
        return MosaicRect{x0, y0, x1 - x0, y1 - y0};
    }

    void CollectMosaicRects(int width, int height) {
        mosaicRects_.clear();
        CubismModel *model = GetModel();
        if (!model || mosaicDrawableIndices_.empty() || width <= 0 || height <= 0) {
            return;
        }

        constexpr int pad = 2;
        for (const csmInt32 drawableIndex : mosaicDrawableIndices_) {
            if (drawableIndex < 0 ||
                drawableIndex >= model->GetDrawableCount() ||
                !model->GetDrawableDynamicFlagIsVisible(drawableIndex)) {
                continue;
            }

            const csmInt32 vertexCount =
                model->GetDrawableVertexCount(drawableIndex);
            const auto *positions =
                model->GetDrawableVertexPositions(drawableIndex);
            if (vertexCount <= 0 || !positions) continue;

            float minX = std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float maxY = std::numeric_limits<float>::lowest();

            for (csmInt32 i = 0; i < vertexCount; ++i) {
                const float ndcX = projMatrix_.TransformX(positions[i].X);
                const float ndcY = projMatrix_.TransformY(positions[i].Y);
                const float px =
                    (ndcX * 0.5f + 0.5f) * static_cast<float>(width);
                const float py =
                    (0.5f - ndcY * 0.5f) * static_cast<float>(height);
                minX = std::min(minX, px);
                minY = std::min(minY, py);
                maxX = std::max(maxX, px);
                maxY = std::max(maxY, py);
            }

            if (maxX <= minX || maxY <= minY) continue;
            const int x0 = std::max(0, static_cast<int>(std::floor(minX)) - pad);
            const int y0 = std::max(0, static_cast<int>(std::floor(minY)) - pad);
            const int x1 =
                std::min(width, static_cast<int>(std::ceil(maxX)) + pad);
            const int y1 =
                std::min(height, static_cast<int>(std::ceil(maxY)) + pad);
            if (x1 <= x0 || y1 <= y0) continue;
            mosaicRects_.push_back(MosaicRect{x0, y0, x1 - x0, y1 - y0});
        }

        bool merged = true;
        while (merged) {
            merged = false;
            for (size_t i = 0; i < mosaicRects_.size() && !merged; ++i) {
                for (size_t j = i + 1; j < mosaicRects_.size(); ++j) {
                    if (!RectsOverlapOrTouch(mosaicRects_[i], mosaicRects_[j],
                                             2)) {
                        continue;
                    }
                    mosaicRects_[i] = MergeRects(mosaicRects_[i], mosaicRects_[j]);
                    mosaicRects_.erase(mosaicRects_.begin() + j);
                    merged = true;
                    break;
                }
            }
        }
    }

    bool ApplyMosaicPostEffect(GodotTexture2D *target, int width, int height) {
        if (!IsMosaicEnabled() || !target || target->GetGodotGpuHandle() == 0) {
            return false;
        }
        const auto *bridge = TVPGodotGpuBridgeGet();
        if (!bridge || !bridge->mosaic_rects) return false;

        CollectMosaicRects(width, height);
        if (mosaicRects_.empty()) return false;

        std::vector<tTVPRect> rects;
        rects.reserve(mosaicRects_.size());
        for (const MosaicRect &rect : mosaicRects_) {
            rects.emplace_back(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h);
        }
        return bridge->mosaic_rects(
            target->GetGodotGpuHandle(), rects.data(),
            static_cast<uint32_t>(rects.size()),
            static_cast<uint32_t>(GetMosaicBlockX()),
            static_cast<uint32_t>(GetMosaicBlockY()));
    }

    void ReleaseTextures() {
        const auto *bridge = TVPGodotGpuBridgeGet();
        for (Texture &texture : textures_) {
            if (texture.handle != 0 && bridge && bridge->release_texture) {
                bridge->release_texture(texture.handle);
            }
            texture = Texture{};
        }
        for (MaskContext &context : maskContexts_) {
            if (context.handle != 0 && bridge && bridge->release_texture) {
                bridge->release_texture(context.handle);
            }
            context = MaskContext{};
        }
        maskContexts_.clear();
        drawableMaskContext_.clear();
    }

    static float Edge(float ax, float ay, float bx, float by,
                      float px, float py) {
        return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
    }

    static uint8_t SampleAlphaBilinear(const Texture &texture,
                                       float srcX, float srcY) {
        if (texture.pixels.empty() || texture.width == 0 ||
            texture.height == 0) {
            return 0;
        }
        const float x = std::clamp(srcX - 0.5f, 0.0f,
                                   static_cast<float>(texture.width - 1));
        const float y = std::clamp(srcY - 0.5f, 0.0f,
                                   static_cast<float>(texture.height - 1));
        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const int x1 = std::min<int>(x0 + 1, texture.width - 1);
        const int y1 = std::min<int>(y0 + 1, texture.height - 1);
        const float fx = x - static_cast<float>(x0);
        const float fy = y - static_cast<float>(y0);
        const auto alphaAt = [&](int px, int py) -> float {
            const size_t offset =
                (static_cast<size_t>(py) * texture.width + px) * 4u + 3u;
            return static_cast<float>(texture.pixels[offset]);
        };
        const float a00 = alphaAt(x0, y0);
        const float a10 = alphaAt(x1, y0);
        const float a01 = alphaAt(x0, y1);
        const float a11 = alphaAt(x1, y1);
        const float a0 = a00 + (a10 - a00) * fx;
        const float a1 = a01 + (a11 - a01) * fx;
        return static_cast<uint8_t>(
            std::clamp(std::lround(a0 + (a1 - a0) * fy), 0l, 255l));
    }

    bool UpdateMaskBounds(MaskContext &context) {
        CubismModel *model = GetModel();
        if (!model) return false;

        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        bool haveBounds = false;
        for (csmInt32 drawable : context.clippedDrawables) {
            // Cubism computes clipping bounds from the clipped meshes even when
            // a drawable is currently invisible; visibility only controls final
            // drawable output, not mask-space layout.
            const csmInt32 vertexCount =
                model->GetDrawableVertexCount(drawable);
            const auto *positions = model->GetDrawableVertexPositions(drawable);
            if (vertexCount <= 0 || !positions) continue;
            for (csmInt32 i = 0; i < vertexCount; ++i) {
                const float x = positions[i].X;
                const float y = positions[i].Y;
                if (!haveBounds) {
                    minX = maxX = x;
                    minY = maxY = y;
                    haveBounds = true;
                } else {
                    minX = std::min(minX, x);
                    minY = std::min(minY, y);
                    maxX = std::max(maxX, x);
                    maxY = std::max(maxY, y);
                }
            }
        }
        if (!haveBounds) return false;

        float width = maxX - minX;
        float height = maxY - minY;
        if (width <= 0.00001f || height <= 0.00001f) return false;

        if (model->IsBlendModeEnabled()) {
            // Cubism forces high precision masks for blend-mode models. In that
            // path small clipped bounds are not stretched to fill the mask
            // texture; they keep model pixel density and are anchored at the
            // clipped bounds origin.
            constexpr float margin = 0.05f;
            const float ppu = std::max(model->GetPixelsPerUnit(), 1.0f);
            const float maskPixelW = static_cast<float>(kMaskTextureSize);
            const float maskPixelH = static_cast<float>(kMaskTextureSize);
            float scaleX = ppu / maskPixelW;
            float scaleY = ppu / maskPixelH;

            if (width * ppu > maskPixelW) {
                minX -= width * margin;
                maxX += width * margin;
                width = maxX - minX;
                scaleX = 1.0f / std::max(width, 0.00001f);
            }
            if (height * ppu > maskPixelH) {
                minY -= height * margin;
                maxY += height * margin;
                height = maxY - minY;
                scaleY = 1.0f / std::max(height, 0.00001f);
            }

            context.boundsX = minX;
            context.boundsY = minY;
            context.boundsW = std::max(1.0f / scaleX, 0.00001f);
            context.boundsH = std::max(1.0f / scaleY, 0.00001f);
            return true;
        }

        constexpr float margin = 0.05f;
        minX -= width * margin;
        maxX += width * margin;
        minY -= height * margin;
        maxY += height * margin;
        width = maxX - minX;
        height = maxY - minY;

        context.boundsX = minX;
        context.boundsY = minY;
        context.boundsW = std::max(width, 0.00001f);
        context.boundsH = std::max(height, 0.00001f);
        return true;
    }

    static void CompositeMaskAlpha(uint8_t &dst, uint8_t src) {
        dst = static_cast<uint8_t>((static_cast<int>(dst) *
                                    (255 - static_cast<int>(src)) + 127) /
                                   255);
    }

    void RasterizeMaskDrawable(MaskContext &context, csmInt32 drawable) {
        CubismModel *model = GetModel();
        if (!model) return;

        const csmInt32 textureIndex = model->GetDrawableTextureIndex(drawable);
        if (textureIndex < 0 ||
            textureIndex >= static_cast<csmInt32>(textures_.size())) {
            return;
        }
        const Texture &texture = textures_[static_cast<size_t>(textureIndex)];
        if (texture.pixels.empty()) return;

        const csmInt32 indexCount = model->GetDrawableVertexIndexCount(drawable);
        const csmUint16 *indices = model->GetDrawableVertexIndices(drawable);
        const auto *positions = model->GetDrawableVertexPositions(drawable);
        const auto *uvs = model->GetDrawableVertexUvs(drawable);
        if (indexCount <= 0 || !indices || !positions || !uvs) return;

        const auto maskPoint = [&](csmUint16 vi) -> std::array<float, 2> {
            return {
                ((positions[vi].X - context.boundsX) / context.boundsW) *
                    static_cast<float>(kMaskTextureSize),
                ((positions[vi].Y - context.boundsY) / context.boundsH) *
                    static_cast<float>(kMaskTextureSize),
            };
        };
        const auto srcPoint = [&](csmUint16 vi) -> std::array<float, 2> {
            return {
                uvs[vi].X * static_cast<float>(texture.width),
                (1.0f - uvs[vi].Y) * static_cast<float>(texture.height),
            };
        };

        for (csmInt32 i = 0; i + 2 < indexCount; i += 3) {
            const csmUint16 i0 = indices[i + 0];
            const csmUint16 i1 = indices[i + 1];
            const csmUint16 i2 = indices[i + 2];
            const auto p0 = maskPoint(i0);
            const auto p1 = maskPoint(i1);
            const auto p2 = maskPoint(i2);
            const float area = Edge(p0[0], p0[1], p1[0], p1[1],
                                    p2[0], p2[1]);
            if (std::fabs(area) < 0.00001f) continue;

            const int left = std::max(0, static_cast<int>(
                                             std::floor(std::min(
                                                 {p0[0], p1[0], p2[0]}))) -
                                             1);
            const int top = std::max(0, static_cast<int>(
                                            std::floor(std::min(
                                                {p0[1], p1[1], p2[1]}))) -
                                            1);
            const int right = std::min(kMaskTextureSize, static_cast<int>(
                                                             std::ceil(std::max(
                                                                 {p0[0], p1[0], p2[0]}))) +
                                                             1);
            const int bottom = std::min(kMaskTextureSize, static_cast<int>(
                                                              std::ceil(std::max(
                                                                  {p0[1], p1[1], p2[1]}))) +
                                                              1);
            if (right <= left || bottom <= top) continue;

            const auto s0 = srcPoint(i0);
            const auto s1 = srcPoint(i1);
            const auto s2 = srcPoint(i2);
            for (int y = top; y < bottom; ++y) {
                for (int x = left; x < right; ++x) {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;
                    const float w0 = Edge(p1[0], p1[1], p2[0], p2[1], px, py) /
                                     area;
                    const float w1 = Edge(p2[0], p2[1], p0[0], p0[1], px, py) /
                                     area;
                    const float w2 = Edge(p0[0], p0[1], p1[0], p1[1], px, py) /
                                     area;
                    if (w0 < -0.0001f || w1 < -0.0001f || w2 < -0.0001f) {
                        continue;
                    }
                    const float sx = s0[0] * w0 + s1[0] * w1 + s2[0] * w2;
                    const float sy = s0[1] * w0 + s1[1] * w1 + s2[1] * w2;
                    const uint8_t alpha = SampleAlphaBilinear(texture, sx, sy);
                    if (alpha == 0) continue;
                    const size_t offset =
                        (static_cast<size_t>(y) * kMaskTextureSize + x) * 4u;
                    context.pixels[offset + 0] = 255;
                    context.pixels[offset + 1] = 255;
                    context.pixels[offset + 2] = 255;
                    CompositeMaskAlpha(context.pixels[offset + 3], alpha);
                }
            }
        }
    }

    void UpdateMasks() {
        if (maskContexts_.empty()) return;
        const auto *bridge = TVPGodotGpuBridgeGet();
        if (!bridge || !bridge->update_rgba) return;

        const tTVPRect full(0, 0, kMaskTextureSize, kMaskTextureSize);
        for (MaskContext &context : maskContexts_) {
            context.usingMask = UpdateMaskBounds(context);
            std::fill(context.pixels.begin(), context.pixels.end(), 255);
            if (context.usingMask) {
                for (csmInt32 maskDrawable : context.maskDrawables) {
                    RasterizeMaskDrawable(context, maskDrawable);
                }
            }
            bridge->update_rgba(context.handle, context.pixels.data(),
                                kMaskTextureSize * 4u, &full);
        }
    }

    void UpdateModel() {
        if (!IsLoaded()) return;
        auto now = std::chrono::steady_clock::now();
        float dt = 0.016f;
        if (lastUpdateTime_.time_since_epoch().count() > 0) {
            dt = std::chrono::duration<float>(now - lastUpdateTime_).count();
            if (dt < 0.001f) dt = 0.001f;
            if (dt > 0.1f) dt = 0.1f;
        }
        lastUpdateTime_ = now;

        GetModel()->LoadParameters();
        if (_motionManager) _motionManager->UpdateMotion(GetModel(), dt);
        GetModel()->SaveParameters();
        if (_eyeBlink) _eyeBlink->UpdateParameters(GetModel(), dt);
        GetModel()->Update();
    }

    void UpdateProjection(int width, int height) {
        projMatrix_.LoadIdentity();
        if (!GetModel() || !_modelMatrix) return;

        const float cw = GetModel()->GetCanvasWidth();
        const float ch = GetModel()->GetCanvasHeight();
        if (cw <= 0.0f || ch <= 0.0f || width <= 0 || height <= 0) return;

        const float modelAspect = cw / ch;
        const float targetAspect = static_cast<float>(width) /
                                   static_cast<float>(height);
        if (modelAspect > targetAspect) {
            projMatrix_.Scale(1.0f, targetAspect / modelAspect);
        } else {
            projMatrix_.Scale(modelAspect / targetAspect, 1.0f);
        }
        CubismMatrix44 modelProjection;
        modelProjection.LoadIdentity();
        modelProjection.Scale(ch / cw, 1.0f);
        modelProjection.MultiplyByMatrix(_modelMatrix);
        projMatrix_.MultiplyByMatrix(&modelProjection);
    }

    uint32_t BlendFlagsForDrawable(csmInt32 drawableIndex) const {
        if (!GetModel()) return 0;
        const csmBlendMode blend = GetModel()->GetDrawableBlendModeType(drawableIndex);
        return (static_cast<uint32_t>(blend.GetColorBlendType()) & 0xffu) |
               ((static_cast<uint32_t>(blend.GetAlphaBlendType()) & 0xffu)
                << 8u);
    }

    void LogDrawableBlendStats() {
        CubismModel *model = GetModel();
        if (!model) return;

        const csmInt32 drawableCount = model->GetDrawableCount();
        const csmInt32 *renderOrders = model->GetRenderOrders();
        std::map<csmInt32, int> colorCounts;
        std::map<csmInt32, int> alphaCounts;
        std::map<csmInt32, int> orderCounts;
        int nonDefaultBlend = 0;
        int duplicateOrders = 0;
        int nonDefaultColors = 0;
        int cullingDrawables = 0;

        for (csmInt32 i = 0; i < drawableCount; ++i) {
            const csmBlendMode blend = model->GetDrawableBlendModeType(i);
            const csmInt32 color = blend.GetColorBlendType();
            const csmInt32 alpha = blend.GetAlphaBlendType();
            ++colorCounts[color];
            ++alphaCounts[alpha];
            if (renderOrders) ++orderCounts[renderOrders[i]];
            if (model->GetDrawableCulling(i) != 0) ++cullingDrawables;

            const auto multiply = model->GetDrawableMultiplyColor(i);
            const auto screen = model->GetDrawableScreenColor(i);
            const bool customColor =
                std::fabs(multiply.X - 1.0f) > 0.0001f ||
                std::fabs(multiply.Y - 1.0f) > 0.0001f ||
                std::fabs(multiply.Z - 1.0f) > 0.0001f ||
                std::fabs(screen.X) > 0.0001f ||
                std::fabs(screen.Y) > 0.0001f ||
                std::fabs(screen.Z) > 0.0001f;
            if (customColor) ++nonDefaultColors;

            if (color != Live2D::Cubism::Core::csmColorBlendType_Normal ||
                alpha != Live2D::Cubism::Core::csmAlphaBlendType_Over ||
                customColor) {
                ++nonDefaultBlend;
                if (nonDefaultBlend <= 80) {
                    const CubismIdHandle id = model->GetDrawableId(i);
                    const char *idText =
                        id ? id->GetString().GetRawString() : "<null>";
                    spdlog::info(
                        "krkrlive2d_godot: drawable blend idx={} id={} order={} color={} alpha={} mul={:.3f},{:.3f},{:.3f},{:.3f} screen={:.3f},{:.3f},{:.3f},{:.3f}",
                        i, idText, renderOrders ? renderOrders[i] : i, color,
                        alpha, multiply.X, multiply.Y, multiply.Z, multiply.W,
                        screen.X, screen.Y, screen.Z, screen.W);
                }
            }
        }

        for (const auto &entry : orderCounts) {
            if (entry.second > 1) ++duplicateOrders;
        }

        const auto mapToString = [](const std::map<csmInt32, int> &values) {
            std::ostringstream out;
            bool first = true;
            for (const auto &entry : values) {
                if (!first) out << ",";
                first = false;
                out << entry.first << ":" << entry.second;
            }
            return out.str();
        };
        spdlog::info(
            "krkrlive2d_godot: drawable blend stats colors=[{}] alphas=[{}] non_default={} custom_colors={} duplicate_orders={} culling={}",
            mapToString(colorCounts), mapToString(alphaCounts),
            nonDefaultBlend, nonDefaultColors, duplicateOrders,
            cullingDrawables);
    }

    void LogMaskContexts() {
        CubismModel *model = GetModel();
        if (!model || maskContexts_.empty()) return;

        const csmInt32 *renderOrders = model->GetRenderOrders();
        const auto drawableLabel = [&](csmInt32 drawable) {
            std::ostringstream out;
            const CubismIdHandle id = model->GetDrawableId(drawable);
            out << drawable << ":"
                << (id ? id->GetString().GetRawString() : "<null>");
            if (renderOrders) out << "@" << renderOrders[drawable];
            if (model->GetDrawableInvertedMask(drawable)) out << "!";
            return out.str();
        };

        for (size_t i = 0; i < maskContexts_.size(); ++i) {
            const MaskContext &context = maskContexts_[i];
            std::ostringstream masks;
            for (size_t j = 0; j < context.maskDrawables.size(); ++j) {
                if (j > 0) masks << ",";
                masks << drawableLabel(context.maskDrawables[j]);
            }
            std::ostringstream clipped;
            for (size_t j = 0; j < context.clippedDrawables.size(); ++j) {
                if (j > 0) clipped << ",";
                clipped << drawableLabel(context.clippedDrawables[j]);
            }
            spdlog::info(
                "krkrlive2d_godot: mask context {} masks=[{}] clipped=[{}]",
                i, masks.str(), clipped.str());
        }

        for (const csmInt32 drawable : mosaicDrawableIndices_) {
            if (drawable < 0 || drawable >= model->GetDrawableCount()) continue;
            const CubismIdHandle id = model->GetDrawableId(drawable);
            spdlog::info(
                "krkrlive2d_godot: mosaic drawable idx={} id={} order={} mask_context={} inverted={}",
                drawable, id ? id->GetString().GetRawString() : "<null>",
                renderOrders ? renderOrders[drawable] : drawable,
                drawable < static_cast<csmInt32>(drawableMaskContext_.size())
                    ? drawableMaskContext_[drawable]
                    : -1,
                model->GetDrawableInvertedMask(drawable));
        }
    }

    void MaybeLogDrawableOrderWindow(int width, int height) {
        CubismModel *model = GetModel();
        if (!model || width <= 0 || height <= 0 ||
            mosaicDrawableIndices_.empty() || orderWindowLogCount_ >= 6) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (orderWindowLogCount_ > 0 &&
            now - lastOrderWindowLog_ < std::chrono::seconds(2)) {
            return;
        }
        lastOrderWindowLog_ = now;
        ++orderWindowLogCount_;

        const csmInt32 *renderOrders = model->GetRenderOrders();
        csmInt32 centerOrder = 90;
        if (renderOrders && !mosaicDrawableIndices_.empty()) {
            const csmInt32 marker = mosaicDrawableIndices_.front();
            if (marker >= 0 && marker < model->GetDrawableCount()) {
                centerOrder = renderOrders[marker];
            }
        }
        const csmInt32 minOrder = centerOrder - 25;
        const csmInt32 maxOrder = centerOrder + 25;

        std::vector<std::pair<csmInt32, csmInt32>> ordered;
        const csmInt32 drawableCount = model->GetDrawableCount();
        ordered.reserve(static_cast<size_t>(drawableCount));
        for (csmInt32 i = 0; i < drawableCount; ++i) {
            ordered.emplace_back(renderOrders ? renderOrders[i] : i, i);
        }
        std::sort(ordered.begin(), ordered.end());

        spdlog::info(
            "krkrlive2d_godot: order window snapshot {} model={} center_order={} range={}..{}",
            orderWindowLogCount_, baseName_, centerOrder, minOrder, maxOrder);

        for (const auto &entry : ordered) {
            const csmInt32 order = entry.first;
            if (order < minOrder || order > maxOrder) continue;

            const csmInt32 drawable = entry.second;
            const CubismIdHandle id = model->GetDrawableId(drawable);
            const bool visible =
                model->GetDrawableDynamicFlagIsVisible(drawable);
            const bool mosaic =
                std::find(mosaicDrawableIndices_.begin(),
                          mosaicDrawableIndices_.end(),
                          drawable) != mosaicDrawableIndices_.end();
            const csmInt32 part = model->GetDrawableParentPartIndex(
                static_cast<csmUint32>(drawable));
            const float partOpacity =
                part >= 0 ? model->GetPartOpacity(part) : -1.0f;
            const int maskIndex =
                drawable < static_cast<csmInt32>(drawableMaskContext_.size())
                    ? drawableMaskContext_[static_cast<size_t>(drawable)]
                    : -1;
            const uint32_t blend = BlendFlagsForDrawable(drawable);

            int x0 = 0;
            int y0 = 0;
            int w = 0;
            int h = 0;
            if (visible) {
                const csmInt32 vertexCount =
                    model->GetDrawableVertexCount(drawable);
                const auto *positions =
                    model->GetDrawableVertexPositions(drawable);
                if (vertexCount > 0 && positions) {
                    float minX = std::numeric_limits<float>::max();
                    float minY = std::numeric_limits<float>::max();
                    float maxX = std::numeric_limits<float>::lowest();
                    float maxY = std::numeric_limits<float>::lowest();
                    for (csmInt32 i = 0; i < vertexCount; ++i) {
                        const float ndcX = projMatrix_.TransformX(positions[i].X);
                        const float ndcY = projMatrix_.TransformY(positions[i].Y);
                        const float px =
                            (ndcX * 0.5f + 0.5f) * static_cast<float>(width);
                        const float py =
                            (0.5f - ndcY * 0.5f) * static_cast<float>(height);
                        minX = std::min(minX, px);
                        minY = std::min(minY, py);
                        maxX = std::max(maxX, px);
                        maxY = std::max(maxY, py);
                    }
                    if (maxX > minX && maxY > minY) {
                        x0 = static_cast<int>(std::floor(minX));
                        y0 = static_cast<int>(std::floor(minY));
                        w = static_cast<int>(std::ceil(maxX - minX));
                        h = static_cast<int>(std::ceil(maxY - minY));
                    }
                }
            }

            spdlog::info(
                "krkrlive2d_godot: order drawable snap={} order={} idx={} id={}{} vis={} op={:.3f} part={} part_op={:.3f} mask={} inv={} blend=0x{:x} bounds={}x{}+{},{}",
                orderWindowLogCount_, order, drawable,
                id ? id->GetString().GetRawString() : "<null>",
                mosaic ? " mosaic" : "", visible,
                model->GetDrawableOpacity(drawable), part, partOpacity,
                maskIndex, model->GetDrawableInvertedMask(drawable), blend, w,
                h, x0, y0);
        }
    }

    bool DrawModelToTexture(GodotTexture2D *target, int width, int height) {
        const auto *bridge = TVPGodotGpuBridgeGet();
        if (!bridge || !bridge->draw_triangles || !target ||
            target->GetGodotGpuHandle() == 0) {
            return false;
        }

        CubismModel *model = GetModel();
        const csmInt32 drawableCount = model->GetDrawableCount();
        const csmInt32 *renderOrders = model->GetRenderOrders();
        std::vector<std::pair<csmInt32, csmInt32>> drawables;
        drawables.reserve(static_cast<size_t>(drawableCount));
        for (csmInt32 i = 0; i < drawableCount; ++i) {
            drawables.emplace_back(renderOrders ? renderOrders[i] : i, i);
        }
        std::sort(drawables.begin(), drawables.end());

        MaybeLogDrawableOrderWindow(width, height);

        bool drewAny = false;
        for (const auto &ordered : drawables) {
            const csmInt32 drawableIndex = ordered.second;
            if (!model->GetDrawableDynamicFlagIsVisible(drawableIndex)) continue;
            const csmInt32 textureIndex =
                model->GetDrawableTextureIndex(drawableIndex);
            if (textureIndex < 0 ||
                textureIndex >= static_cast<csmInt32>(textures_.size())) {
                continue;
            }
            const Texture &texture = textures_[static_cast<size_t>(textureIndex)];
            if (texture.handle == 0 || texture.width == 0 ||
                texture.height == 0) {
                continue;
            }

            MaskContext *maskContext = nullptr;
            if (drawableIndex >= 0 &&
                drawableIndex <
                    static_cast<csmInt32>(drawableMaskContext_.size())) {
                const int contextIndex =
                    drawableMaskContext_[static_cast<size_t>(drawableIndex)];
                if (contextIndex >= 0 &&
                    contextIndex <
                        static_cast<int>(maskContexts_.size())) {
                    maskContext =
                        &maskContexts_[static_cast<size_t>(contextIndex)];
                }
            }
            if (maskContext && (!maskContext->usingMask ||
                                maskContext->handle == 0)) {
                continue;
            }
            const bool useMask =
                maskContext != nullptr && bridge->draw_masked_triangles != nullptr;

            const csmInt32 indexCount =
                model->GetDrawableVertexIndexCount(drawableIndex);
            const csmUint16 *indices =
                model->GetDrawableVertexIndices(drawableIndex);
            const auto *positions =
                model->GetDrawableVertexPositions(drawableIndex);
            const auto *uvs = model->GetDrawableVertexUvs(drawableIndex);
            if (indexCount <= 0 || !indices || !positions || !uvs) continue;

            std::vector<tTVPPointD> dst;
            std::vector<tTVPPointD> src;
            std::vector<tTVPPointD> mask;
            dst.reserve(64u * 3u);
            src.reserve(64u * 3u);
            mask.reserve(64u * 3u);
            double minX = 0.0;
            double minY = 0.0;
            double maxX = 0.0;
            double maxY = 0.0;
            bool haveBounds = false;

            const auto flushChunk = [&]() {
                const uint32_t triCount =
                    static_cast<uint32_t>(dst.size() / 3u);
                if (triCount == 0 || !haveBounds) return false;
                tTVPRect clip(
                    std::max(0, static_cast<int>(std::floor(minX)) - 1),
                    std::max(0, static_cast<int>(std::floor(minY)) - 1),
                    std::min(width, static_cast<int>(std::ceil(maxX)) + 1),
                    std::min(height, static_cast<int>(std::ceil(maxY)) + 1));
                if (clip.right <= clip.left || clip.bottom <= clip.top) {
                    dst.clear();
                    src.clear();
                    haveBounds = false;
                    return false;
                }
                bool ok = false;
                const float opacity =
                    std::clamp(model->GetDrawableOpacity(drawableIndex),
                               0.0f, 1.0f);
                if (useMask && mask.size() == dst.size()) {
                    ok = bridge->draw_masked_triangles(
                        target->GetGodotGpuHandle(), texture.handle,
                        maskContext->handle, triCount, &clip, dst.data(),
                        src.data(), mask.data(), opacity,
                        BlendFlagsForDrawable(drawableIndex),
                        model->GetDrawableInvertedMask(drawableIndex));
                } else {
                    ok = bridge->draw_triangles(
                        target->GetGodotGpuHandle(), texture.handle, triCount,
                        &clip, dst.data(), src.data(), opacity,
                        BlendFlagsForDrawable(drawableIndex));
                }
                dst.clear();
                src.clear();
                mask.clear();
                haveBounds = false;
                return ok;
            };

            const bool culling = model->GetDrawableCulling(drawableIndex) != 0;
            for (csmInt32 i = 0; i + 2 < indexCount; i += 3) {
                if (dst.size() >= 64u * 3u) {
                    drewAny = flushChunk() || drewAny;
                }
                std::array<tTVPPointD, 3> triDst{};
                std::array<tTVPPointD, 3> triSrc{};
                std::array<tTVPPointD, 3> triMask{};
                for (int j = 0; j < 3; ++j) {
                    const csmUint16 vi = indices[i + j];
                    const float ndcX = projMatrix_.TransformX(positions[vi].X);
                    const float ndcY = projMatrix_.TransformY(positions[vi].Y);
                    const double x = (static_cast<double>(ndcX) * 0.5 + 0.5) *
                                     static_cast<double>(width);
                    const double y = (0.5 - static_cast<double>(ndcY) * 0.5) *
                                     static_cast<double>(height);
                    triDst[static_cast<size_t>(j)] = {x, y};
                    triSrc[static_cast<size_t>(j)] = {
                        static_cast<double>(uvs[vi].X) *
                            static_cast<double>(texture.width),
                        static_cast<double>(1.0f - uvs[vi].Y) *
                            static_cast<double>(texture.height)};
                    if (useMask) {
                        triMask[static_cast<size_t>(j)] = {
                            ((static_cast<double>(positions[vi].X) -
                              maskContext->boundsX) /
                             maskContext->boundsW) *
                                static_cast<double>(kMaskTextureSize),
                            ((static_cast<double>(positions[vi].Y) -
                              maskContext->boundsY) /
                             maskContext->boundsH) *
                                static_cast<double>(kMaskTextureSize),
                        };
                    }
                }

                const double area = static_cast<double>(Edge(
                    static_cast<float>(triDst[0].x),
                    static_cast<float>(triDst[0].y),
                    static_cast<float>(triDst[1].x),
                    static_cast<float>(triDst[1].y),
                    static_cast<float>(triDst[2].x),
                    static_cast<float>(triDst[2].y)));
                if (culling && area <= 0.00001) {
                    continue;
                }

                for (int j = 0; j < 3; ++j) {
                    const auto &point = triDst[static_cast<size_t>(j)];
                    dst.push_back(point);
                    src.push_back(triSrc[static_cast<size_t>(j)]);
                    if (useMask) mask.push_back(triMask[static_cast<size_t>(j)]);
                    if (!haveBounds) {
                        minX = maxX = point.x;
                        minY = maxY = point.y;
                        haveBounds = true;
                    } else {
                        minX = std::min(minX, point.x);
                        minY = std::min(minY, point.y);
                        maxX = std::max(maxX, point.x);
                        maxY = std::max(maxY, point.y);
                    }
                }
            }
            drewAny = flushChunk() || drewAny;
        }
        return drewAny;
    }

    const std::vector<std::string> *FindMotionNamesForGroup(
        const std::string &group) const {
        auto it = motionNamesByGroup_.find(group);
        if (it != motionNamesByGroup_.end()) return &it->second;
        if (group == "main") {
            it = motionNamesByGroup_.find(std::string());
            if (it != motionNamesByGroup_.end()) return &it->second;
        }
        return nullptr;
    }

    bool loaded_ = false;
    bool visible_ = true;
    std::string baseName_;
    ttstr storageDir_;
    CubismModelSettingJson *setting_ = nullptr;
    std::vector<Texture> textures_;
    std::vector<MaskContext> maskContexts_;
    std::vector<int> drawableMaskContext_;
    std::unordered_map<std::string, ACubismMotion *> motions_;
    std::unordered_map<std::string, ACubismMotion *> motionNames_;
    std::vector<std::string> motionGroupNames_;
    std::unordered_map<std::string, std::vector<std::string>> motionNamesByGroup_;
    std::unordered_map<std::string, ACubismMotion *> firstMotionByGroup_;
    std::vector<csmFloat32> defaultPartOpacities_;
    csmVector<CubismIdHandle> _eyeBlinkIds;
    csmVector<CubismIdHandle> _lipSyncIds;
    CubismMatrix44 projMatrix_;
    std::vector<csmInt32> mosaicDrawableIndices_;
    std::vector<csmInt32> mosaicParentPartIndices_;
    std::unordered_map<csmInt32, csmFloat32> mosaicParentOpacityDefaults_;
    std::vector<MosaicRect> mosaicRects_;
    float mosaicSizeX_ = 24.0f;
    float mosaicSizeY_ = 24.0f;
    int orderWindowLogCount_ = 0;
    std::chrono::steady_clock::time_point lastOrderWindowLog_;
    std::chrono::steady_clock::time_point lastUpdateTime_;
};

} // namespace

class Live2DMatrix {
public:
    Live2DMatrix() = default;
    static tjs_error setMatrixCb(tTJSVariant *result, tjs_int,
                                 tTJSVariant **, Live2DMatrix *) {
        SetIntResult(result, 1);
        return TJS_S_OK;
    }
};

class Live2DDevice {
public:
    Live2DDevice() = default;
    void beginScene() {}
    void endScene() {}
    void onBeginScene() {}
    void onEndScene() {}
    static tjs_error renderCb(tTJSVariant *result, tjs_int,
                              tTJSVariant **, Live2DDevice *) {
        SetIntResult(result, 1);
        return TJS_S_OK;
    }
};

class Live2DModel {
public:
    Live2DModel() = default;
    ~Live2DModel() {
        if (model_) {
            CSM_DELETE(model_);
            model_ = nullptr;
        }
    }

    static tjs_error okCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                          Live2DModel *) {
        SetIntResult(result, 1);
        return TJS_S_OK;
    }

    static tjs_error zeroCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                            Live2DModel *) {
        SetIntResult(result, 0);
        return TJS_S_OK;
    }

    static tjs_error falseCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                             Live2DModel *) {
        SetBoolResult(result, false);
        return TJS_S_OK;
    }

    static tjs_error loadCb(tTJSVariant *result, tjs_int numparams,
                            tTJSVariant **param, Live2DModel *self) {
        if (!self || numparams <= 0 || !param || !param[0]) {
            SetBoolResult(result, false);
            return TJS_S_OK;
        }

        const ttstr storage = ToTTStr(*param[0]);
        std::vector<uint8_t> bytes;
        if (!LoadStorageBytes(storage, bytes)) {
            TVPAddLog(ttstr(TJS_W("krkrlive2d_godot: failed to read Live2D model: ")) +
                      storage);
            SetBoolResult(result, false);
            return TJS_S_OK;
        }

        EnsureCubismInitialized();
        GodotLive2DModel *newModel = CSM_NEW GodotLive2DModel();
        if (!newModel) {
            SetBoolResult(result, false);
            return TJS_S_OK;
        }
        const std::string baseName = Stem(storage.AsStdString());
        const ttstr storageDir = ResolveStorageDir(storage);
        spdlog::info("krkrlive2d_godot: load {} dir={}",
                     storage.AsStdString(), storageDir.AsStdString());
        const bool ok = newModel->LoadFromL2D(bytes, baseName, storageDir);
        if (!ok) {
            CSM_DELETE(newModel);
        } else {
            newModel->SetMosaicSize(static_cast<float>(self->mosaicX_),
                                    static_cast<float>(self->mosaicY_));
            GodotLive2DModel *oldModel = self->model_;
            self->model_ = newModel;
            if (oldModel) CSM_DELETE(oldModel);
        }
        SetBoolResult(result, ok);
        return TJS_S_OK;
    }

    static tjs_error renderCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                              Live2DModel *self) {
        if (self && self->model_) {
            self->model_->Progress();
            if (self->playing_ && self->model_->MotionsFinished()) {
                self->playing_ = false;
            }
        }
        SetIntResult(result, 1);
        return TJS_S_OK;
    }

    static tjs_error showCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                            Live2DModel *self) {
        if (self && self->model_) self->model_->SetVisible(true);
        SetIntResult(result, 1);
        return TJS_S_OK;
    }

    static tjs_error hideCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                            Live2DModel *self) {
        if (self && self->model_) self->model_->SetVisible(false);
        SetIntResult(result, 1);
        return TJS_S_OK;
    }

    static tjs_error progressCb(tTJSVariant *result, tjs_int,
                                tTJSVariant **, Live2DModel *self) {
        if (self && self->model_) {
            self->model_->Progress();
            if (self->playing_ && self->model_->MotionsFinished()) {
                self->playing_ = false;
            }
        }
        SetIntResult(result, 1);
        return TJS_S_OK;
    }

    static tjs_error getMotionGroupCountCb(tTJSVariant *result, tjs_int,
                                           tTJSVariant **, Live2DModel *self) {
        SetIntResult(result, self && self->model_
                                 ? static_cast<tjs_int>(
                                       self->model_->GetMotionGroupCount())
                                 : 0);
        return TJS_S_OK;
    }

    static tjs_error getMotionGroupNameCb(tTJSVariant *result, tjs_int numparams,
                                          tTJSVariant **param,
                                          Live2DModel *self) {
        if (!result || !self || !self->model_) return TJS_S_OK;
        const tjs_int index =
            (numparams > 0 && param && param[0]) ? ToInt(*param[0], 0) : 0;
        const std::string name =
            self->model_->GetMotionGroupName(static_cast<int>(index));
        *result = ttstr(name.c_str());
        return TJS_S_OK;
    }

    static tjs_error getMotionCountCb(tTJSVariant *result, tjs_int numparams,
                                      tTJSVariant **param, Live2DModel *self) {
        if (!result || !self || !self->model_) return TJS_S_OK;
        const std::string group =
            (numparams > 0 && param && param[0])
                ? ToTTStr(*param[0]).AsStdString()
                : std::string("main");
        SetIntResult(result, self->model_->GetMotionCount(group));
        return TJS_S_OK;
    }

    static tjs_error getMotionNameCb(tTJSVariant *result, tjs_int numparams,
                                     tTJSVariant **param, Live2DModel *self) {
        if (!result || !self || !self->model_) return TJS_S_OK;
        const std::string group =
            (numparams > 0 && param && param[0])
                ? ToTTStr(*param[0]).AsStdString()
                : std::string("main");
        const tjs_int index =
            (numparams > 1 && param && param[1]) ? ToInt(*param[1], 0) : 0;
        const std::string name =
            self->model_->GetMotionName(group, static_cast<int>(index));
        *result = ttstr(name.c_str());
        return TJS_S_OK;
    }

    static tjs_error startMotionCb(tTJSVariant *result, tjs_int numparams,
                                   tTJSVariant **param, Live2DModel *self) {
        if (self && self->model_) {
            bool started = false;
            self->currentMotions_.clear();
            spdlog::info("krkrlive2d_godot: startMotion called params={}",
                         numparams);
            if (numparams == 1 && param && param[0]) {
                const ttstr motion = ToTTStr(*param[0]);
                const std::string motionName = motion.AsStdString();
                if (!self->model_->StartMotionByName(motionName)) {
                    started = self->model_->StartMotion(std::string(), motionName);
                } else {
                    started = true;
                }
                if (started) self->currentMotions_.push_back(motion);
            } else if (numparams > 1 && param && param[1] &&
                       param[1]->Type() == tvtInteger) {
                const std::string group = param[0]
                                              ? ToTTStr(*param[0]).AsStdString()
                                              : std::string();
                started =
                    self->model_->StartMotionByIndex(group, ToInt(*param[1], 0));
                if (started && param[0]) {
                    self->currentMotions_.push_back(ToTTStr(*param[0]));
                }
            } else {
                const std::string group =
                    (numparams > 0 && param && param[0])
                        ? ToTTStr(*param[0]).AsStdString()
                        : std::string();
                const std::string motion =
                    (numparams > 1 && param && param[1])
                        ? ToTTStr(*param[1]).AsStdString()
                        : std::string();
                started = self->model_->StartMotion(group, motion);
                if (started && !motion.empty()) {
                    self->currentMotions_.push_back(ToTTStr(*param[1]));
                }
            }
            self->playing_ = started;
        }
        SetIntResult(result, 1);
        return TJS_S_OK;
    }

    static tjs_error setMosaicParamCb(tTJSVariant *result, tjs_int numparams,
                                      tTJSVariant **param, Live2DModel *self) {
        if (self && param) {
            if (numparams > 0 && param[0]) {
                self->mosaicX_ = ToReal(*param[0], self->mosaicX_);
            }
            if (numparams > 1 && param[1]) {
                self->mosaicY_ = ToReal(*param[1], self->mosaicY_);
            }
            if (self->model_) {
                self->model_->SetMosaicSize(
                    static_cast<float>(self->mosaicX_),
                    static_cast<float>(self->mosaicY_));
            }
        }
        SetIntResult(result, 1);
        return TJS_S_OK;
    }

    static tjs_error isMosaicModelCb(tTJSVariant *result, tjs_int,
                                     tTJSVariant **, Live2DModel *self) {
        SetBoolResult(result, self && self->model_ &&
                                  self->model_->HasMosaicDrawables());
        return TJS_S_OK;
    }

    static tjs_error stopMotionCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                  Live2DModel *self) {
        if (self) {
            self->playing_ = false;
            self->currentMotions_.clear();
            if (self->model_) self->model_->StopMotions();
        }
        SetBoolResult(result, true);
        return TJS_S_OK;
    }

    static tjs_error getCurrentMotionsCb(tTJSVariant *result, tjs_int,
                                         tTJSVariant **, Live2DModel *self) {
        if (!result || !self) return TJS_S_OK;
        iTJSDispatch2 *array = CreateStringArray(self->currentMotions_);
        SetResultObject(result, array);
        if (array) array->Release();
        return TJS_S_OK;
    }

    static tjs_error isPlayingCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                 Live2DModel *self) {
        SetBoolResult(result, self && self->playing_);
        return TJS_S_OK;
    }

    static tjs_error getParameterCountCb(tTJSVariant *result, tjs_int,
                                         tTJSVariant **, Live2DModel *self) {
        if (!result) return TJS_S_OK;
        if (self && self->model_ && self->model_->Cubism()) {
            *result = self->model_->Cubism()->GetParameterCount();
        } else {
            *result = static_cast<tjs_int>(0);
        }
        return TJS_S_OK;
    }

    static tjs_error getParameterInfoCb(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        Live2DModel *self) {
        if (!result || !self || !self->model_ || !self->model_->Cubism()) {
            if (result) result->Clear();
            return TJS_S_OK;
        }
        const tjs_int index =
            (numparams > 0 && param && param[0]) ? ToInt(*param[0], 0) : 0;
        CubismModel *model = self->model_->Cubism();
        if (index >= 0 && index < model->GetParameterCount()) {
            const char *id =
                model->GetParameterId(index)->GetString().GetRawString();
            const ttstr tid(id);
            iTJSDispatch2 *dict = CreateIdNameDict(tid, tid);
            SetResultObject(result, dict);
            if (dict) dict->Release();
        } else {
            result->Clear();
        }
        return TJS_S_OK;
    }

    static tjs_error getParameterValueCb(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param,
                                         Live2DModel *self) {
        if (!result || !self || !param || numparams <= 0) return TJS_S_OK;
        const std::string key = ToKey(*param[0]);
        if (self->model_ && self->model_->Cubism()) {
            CubismIdHandle id = CubismFramework::GetIdManager()->GetId(key.c_str());
            const csmInt32 index = self->model_->Cubism()->GetParameterIndex(id);
            if (index >= 0) {
                *result = static_cast<tjs_real>(
                    self->model_->Cubism()->GetParameterValue(index));
                return TJS_S_OK;
            }
        }
        auto stored = self->parameterValues_.find(key);
        *result = stored == self->parameterValues_.end() ? 0.0 : stored->second;
        return TJS_S_OK;
    }

    static tjs_error setParameterValueCb(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param,
                                         Live2DModel *self) {
        if (self && param && numparams > 1) {
            const std::string key = ToKey(*param[0]);
            const tjs_real value = ToReal(*param[1], 0.0);
            if (self->model_ && self->model_->Cubism()) {
                CubismIdHandle id =
                    CubismFramework::GetIdManager()->GetId(key.c_str());
                const csmInt32 index =
                    self->model_->Cubism()->GetParameterIndex(id);
                if (index >= 0) {
                    self->model_->Cubism()->SetParameterValue(
                        index, static_cast<csmFloat32>(value));
                }
            }
            self->parameterValues_[key] = value;
        }
        SetBoolResult(result, true);
        return TJS_S_OK;
    }

    static tjs_error setParameterTypeCb(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        Live2DModel *self) {
        if (self && param && numparams > 1) {
            self->parameterTypes_[ToKey(*param[0])] = ToInt(*param[1], 0);
        }
        SetBoolResult(result, true);
        return TJS_S_OK;
    }

    static tjs_error setDiffParameterValueCb(tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             Live2DModel *self) {
        if (self && param && numparams > 1) {
            self->diffParameterValues_[ToKey(*param[0])] =
                ToReal(*param[1], 0.0);
        }
        SetBoolResult(result, true);
        return TJS_S_OK;
    }

    static tjs_error getDiffParameterValueCb(tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             Live2DModel *self) {
        if (!result || !self || !param || numparams <= 0) return TJS_S_OK;
        auto stored = self->diffParameterValues_.find(ToKey(*param[0]));
        *result = stored == self->diffParameterValues_.end() ? 0.0
                                                             : stored->second;
        return TJS_S_OK;
    }

    static tjs_error getPartCountCb(tTJSVariant *result, tjs_int,
                                    tTJSVariant **, Live2DModel *self) {
        if (!result) return TJS_S_OK;
        if (self && self->model_ && self->model_->Cubism()) {
            *result = self->model_->Cubism()->GetPartCount();
        } else {
            *result = static_cast<tjs_int>(0);
        }
        return TJS_S_OK;
    }

    static tjs_error getPartInfoCb(tTJSVariant *result, tjs_int numparams,
                                   tTJSVariant **param, Live2DModel *self) {
        if (!result || !self || !self->model_ || !self->model_->Cubism()) {
            if (result) result->Clear();
            return TJS_S_OK;
        }
        const tjs_int index =
            (numparams > 0 && param && param[0]) ? ToInt(*param[0], 0) : 0;
        CubismModel *model = self->model_->Cubism();
        if (index >= 0 && index < model->GetPartCount()) {
            const char *id = model->GetPartId(index)->GetString().GetRawString();
            const ttstr tid(id);
            iTJSDispatch2 *dict = CreateIdNameDict(tid, tid);
            SetResultObject(result, dict);
            if (dict) dict->Release();
        } else {
            result->Clear();
        }
        return TJS_S_OK;
    }

    static tjs_error getPartValueCb(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param, Live2DModel *self) {
        if (!result || !self || !param || numparams <= 0) return TJS_S_OK;
        const std::string key = ToKey(*param[0]);
        if (self->model_ && self->model_->Cubism()) {
            CubismIdHandle id = CubismFramework::GetIdManager()->GetId(key.c_str());
            const csmInt32 index = self->model_->Cubism()->GetPartIndex(id);
            if (index >= 0) {
                *result = static_cast<tjs_real>(
                    self->model_->Cubism()->GetPartOpacity(index));
                return TJS_S_OK;
            }
        }
        auto stored = self->partValues_.find(key);
        *result = stored == self->partValues_.end() ? 1.0 : stored->second;
        return TJS_S_OK;
    }

    static tjs_error setPartValueCb(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param, Live2DModel *self) {
        if (self && param && numparams > 1) {
            const std::string key = ToKey(*param[0]);
            const tjs_real value = ToReal(*param[1], 1.0);
            bool applied = false;
            if (self->model_) {
                applied = self->model_->SetPartOpacityByKey(
                    key, static_cast<csmFloat32>(value));
            }
            self->partValues_[key] = value;
            const std::string logKey =
                key + "=" + std::to_string(static_cast<int>(value * 1000.0));
            if (self->loggedPartSets_.insert(logKey).second) {
                spdlog::info("krkrlive2d_godot: setPart key='{}' value={:.3f} applied={}",
                             key, value, applied);
            }
        }
        SetBoolResult(result, true);
        return TJS_S_OK;
    }

    static tjs_error setPartFadeTimeCb(tTJSVariant *result, tjs_int numparams,
                                       tTJSVariant **param,
                                       Live2DModel *self) {
        if (self && param && numparams > 0) {
            self->partFadeTime_ = ToReal(*param[0], self->partFadeTime_);
        }
        SetBoolResult(result, true);
        return TJS_S_OK;
    }

    static tjs_error getVariableCb(tTJSVariant *result, tjs_int numparams,
                                   tTJSVariant **param, Live2DModel *self) {
        if (!result || !self || !param || numparams <= 0) return TJS_S_OK;
        auto stored = self->variables_.find(ToKey(*param[0]));
        if (stored == self->variables_.end()) {
            result->Clear();
        } else {
            *result = stored->second;
        }
        return TJS_S_OK;
    }

    static tjs_error setVariableCb(tTJSVariant *result, tjs_int numparams,
                                   tTJSVariant **param, Live2DModel *self) {
        if (self && param && numparams > 1) {
            self->variables_[ToKey(*param[0])] = *param[1];
        }
        SetBoolResult(result, true);
        return TJS_S_OK;
    }

    static tjs_error resetPartsCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                  Live2DModel *self) {
        if (self) {
            self->partValues_.clear();
            self->loggedPartSets_.clear();
            if (self->model_) self->model_->ResetPartsToDefaults();
        }
        SetBoolResult(result, true);
        return TJS_S_OK;
    }

    static tjs_error resetVariablesCb(tTJSVariant *result, tjs_int,
                                      tTJSVariant **, Live2DModel *self) {
        if (self) self->variables_.clear();
        SetBoolResult(result, true);
        return TJS_S_OK;
    }

    static tjs_error cloneCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                             Live2DModel *) {
        return CreateLive2DObject(result, TJS_W("new Live2DModel()"));
    }

    static tjs_error getDeviceCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                 Live2DModel *) {
        return CreateLive2DObject(result, TJS_W("new Live2DDevice()"));
    }

    static tjs_error setDeviceCb(tTJSVariant *, tjs_int, tTJSVariant **,
                                 Live2DModel *) {
        return TJS_S_OK;
    }

    static tjs_error emptyStringCb(tTJSVariant *result, tjs_int,
                                   tTJSVariant **, Live2DModel *) {
        SetStringResult(result);
        return TJS_S_OK;
    }

    static tjs_error emptyArrayCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                  Live2DModel *) {
        SetArrayResult(result);
        return TJS_S_OK;
    }

private:
    GodotLive2DModel *model_ = nullptr;
    tjs_real mosaicX_ = 24.0;
    tjs_real mosaicY_ = 24.0;
    bool playing_ = false;
    tjs_real partFadeTime_ = 0.0;
    std::vector<ttstr> currentMotions_;
    std::unordered_map<std::string, tjs_real> parameterValues_;
    std::unordered_map<std::string, tjs_real> diffParameterValues_;
    std::unordered_map<std::string, tjs_real> partValues_;
    std::unordered_map<std::string, tjs_int> parameterTypes_;
    std::unordered_set<std::string> loggedPartSets_;
    std::unordered_map<std::string, tTJSVariant> variables_;
};

extern "C" bool TVPGodotLive2DRenderToLayer(iTJSDispatch2 *layerDispatch) {
    tTJSNI_BaseLayer *layer = NativeLayerFromDispatch(layerDispatch);
    if (!layer) return false;
    bool rendered = false;
    bool cleared = false;
    for (GodotLive2DModel *model : g_activeModels) {
        if (model && model->IsLoaded() && model->IsVisible()) {
            const bool drew = model->RenderToLayer(layer, !cleared);
            rendered = drew || rendered;
            cleared = drew || cleared;
        }
    }
    return rendered;
}

NCB_REGISTER_CLASS(Live2DMatrix) {
    Constructor();
    NCB_METHOD_RAW_CALLBACK(setMatrix, &Live2DMatrix::setMatrixCb, 0);
}

NCB_REGISTER_CLASS(Live2DDevice) {
    Constructor();
    NCB_METHOD(beginScene);
    NCB_METHOD(endScene);
    NCB_METHOD(onBeginScene);
    NCB_METHOD(onEndScene);
    NCB_METHOD_RAW_CALLBACK(render, &Live2DDevice::renderCb, 0);
}

NCB_REGISTER_CLASS(Live2DModel) {
    Constructor();
    NCB_PROPERTY_RAW_CALLBACK(device, Live2DModel::getDeviceCb,
                              Live2DModel::setDeviceCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &Live2DModel::renderCb, 0);
    NCB_METHOD_RAW_CALLBACK(show, &Live2DModel::showCb, 0);
    NCB_METHOD_RAW_CALLBACK(hide, &Live2DModel::hideCb, 0);
    NCB_METHOD_RAW_CALLBACK(progress, &Live2DModel::progressCb, 0);
    NCB_METHOD_RAW_CALLBACK(load, &Live2DModel::loadCb, 0);
    NCB_METHOD_RAW_CALLBACK(clone, &Live2DModel::cloneCb, 0);
    NCB_METHOD_RAW_CALLBACK(setScale, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getScale, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVoiceValue, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVoiceWeight, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVoiceMode, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setBlinkingInterval, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setBlinkingSettings, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setBlinkingMode, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMosaicParam, &Live2DModel::setMosaicParamCb, 0);
    NCB_METHOD_RAW_CALLBACK(getExpressionCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getExpressionName, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(setExpression, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getExpression, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(fixExpression, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionGroupCount,
                            &Live2DModel::getMotionGroupCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionGroupName,
                            &Live2DModel::getMotionGroupNameCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionCount, &Live2DModel::getMotionCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionName, &Live2DModel::getMotionNameCb, 0);
    NCB_METHOD_RAW_CALLBACK(startMotion, &Live2DModel::startMotionCb, 0);
    NCB_METHOD_RAW_CALLBACK(stopMotion, &Live2DModel::stopMotionCb, 0);
    NCB_METHOD_RAW_CALLBACK(getCurrentMotions,
                            &Live2DModel::getCurrentMotionsCb, 0);
    NCB_METHOD_RAW_CALLBACK(isPlaying, &Live2DModel::isPlayingCb, 0);
    NCB_METHOD_RAW_CALLBACK(getParameterCount,
                            &Live2DModel::getParameterCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getParameterInfo,
                            &Live2DModel::getParameterInfoCb, 0);
    NCB_METHOD_RAW_CALLBACK(getParameterValue,
                            &Live2DModel::getParameterValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(setParameterValue,
                            &Live2DModel::setParameterValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(setParameterType,
                            &Live2DModel::setParameterTypeCb, 0);
    NCB_METHOD_RAW_CALLBACK(setDiffParameterValue,
                            &Live2DModel::setDiffParameterValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(getDiffParameterValue,
                            &Live2DModel::getDiffParameterValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(addEyeBlinkId, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(addLipSyncId, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(canSync, &Live2DModel::falseCb, 0);
    NCB_METHOD_RAW_CALLBACK(sync, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getPartCount, &Live2DModel::getPartCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getPartInfo, &Live2DModel::getPartInfoCb, 0);
    NCB_METHOD_RAW_CALLBACK(setPart, &Live2DModel::setPartValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(getPartValue, &Live2DModel::getPartValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(setPartValue, &Live2DModel::setPartValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(setPartFadeTime,
                            &Live2DModel::setPartFadeTimeCb, 0);
    NCB_METHOD_RAW_CALLBACK(getEventCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getEventName, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(addVriableMotion, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(delVariableMotion, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableMotionCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableMotionName, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableMotionInfo, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariable, &Live2DModel::getVariableCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVariable, &Live2DModel::setVariableCb, 0);
    NCB_METHOD_RAW_CALLBACK(isMosaicModel, &Live2DModel::isMosaicModelCb, 0);
    NCB_METHOD_RAW_CALLBACK(reload, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(resetParts, &Live2DModel::resetPartsCb, 0);
    NCB_METHOD_RAW_CALLBACK(resetVariables, &Live2DModel::resetVariablesCb, 0);
    NCB_METHOD_RAW_CALLBACK(resetExpressionVariables, &Live2DModel::okCb, 0);
}

extern "C" void TVPRegisterKrkrLive2DPluginAnchor() {
    ncbAutoRegister::RegisterInternalPluginEntry(
        TJS_W("krkrlive2d.dll"),
        ncbAutoRegister::ClassRegist,
        &ncbNativeClassAutoRegister_Live2DMatrix);
    ncbAutoRegister::RegisterInternalPluginEntry(
        TJS_W("krkrlive2d.dll"),
        ncbAutoRegister::ClassRegist,
        &ncbNativeClassAutoRegister_Live2DDevice);
    ncbAutoRegister::RegisterInternalPluginEntry(
        TJS_W("krkrlive2d.dll"),
        ncbAutoRegister::ClassRegist,
        &ncbNativeClassAutoRegister_Live2DModel);
}
