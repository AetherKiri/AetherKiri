#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <minizip/ioapi.h>
#include <minizip/unzip.h>
#include <png.h>
#include <spdlog/spdlog.h>

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

struct MemZipStream {
    const uint8_t *data = nullptr;
    size_t size = 0;
    size_t pos = 0;
};

voidpf ZCALLBACK ZipOpen(voidpf opaque, const char *, int) { return opaque; }

uLong ZCALLBACK ZipRead(voidpf, voidpf stream, void *buf, uLong size) {
    auto *s = static_cast<MemZipStream *>(stream);
    const size_t avail = s->pos < s->size ? s->size - s->pos : 0;
    const size_t n = std::min<size_t>(size, avail);
    if (n > 0) {
        std::memcpy(buf, s->data + s->pos, n);
        s->pos += n;
    }
    return static_cast<uLong>(n);
}

uLong ZCALLBACK ZipWrite(voidpf, voidpf, const void *, uLong) { return 0; }
long ZCALLBACK ZipTell(voidpf, voidpf stream) {
    return static_cast<long>(static_cast<MemZipStream *>(stream)->pos);
}

long ZCALLBACK ZipSeek(voidpf, voidpf stream, uLong offset, int origin) {
    auto *s = static_cast<MemZipStream *>(stream);
    size_t newpos = 0;
    switch (origin) {
        case ZLIB_FILEFUNC_SEEK_SET:
            newpos = offset;
            break;
        case ZLIB_FILEFUNC_SEEK_CUR:
            newpos = s->pos + offset;
            break;
        case ZLIB_FILEFUNC_SEEK_END:
            newpos = s->size + offset;
            break;
        default:
            return -1;
    }
    if (newpos > s->size) return -1;
    s->pos = newpos;
    return 0;
}

int ZCALLBACK ZipClose(voidpf, voidpf) { return 0; }
int ZCALLBACK ZipError(voidpf, voidpf) { return 0; }

bool ExtractZipToMemory(const uint8_t *zipData, size_t zipSize,
                        ZipArchive &out) {
    out.clear();
    MemZipStream stream{zipData, zipSize, 0};
    zlib_filefunc_def funcs{};
    funcs.zopen_file = ZipOpen;
    funcs.zread_file = ZipRead;
    funcs.zwrite_file = ZipWrite;
    funcs.ztell_file = ZipTell;
    funcs.zseek_file = ZipSeek;
    funcs.zclose_file = ZipClose;
    funcs.zerror_file = ZipError;
    funcs.opaque = &stream;

    unzFile zf = unzOpen2(nullptr, &funcs);
    if (!zf) return false;

    int ret = unzGoToFirstFile(zf);
    while (ret == UNZ_OK) {
        char name[1024];
        unz_file_info info{};
        unzGetCurrentFileInfo(zf, &info, name, sizeof(name), nullptr, 0,
                              nullptr, 0);
        if (info.uncompressed_size > 0 && unzOpenCurrentFile(zf) == UNZ_OK) {
            std::vector<uint8_t> bytes(info.uncompressed_size);
            const int read = unzReadCurrentFile(
                zf, bytes.data(), static_cast<unsigned>(bytes.size()));
            if (read == static_cast<int>(bytes.size())) {
                out[std::string(name)] = std::move(bytes);
            }
            unzCloseCurrentFile(zf);
        }
        ret = unzGoToNextFile(zf);
    }
    unzClose(zf);
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

void SetArrayResult(tTJSVariant *result) {
    if (!result) return;
    iTJSDispatch2 *array = TJSCreateArrayObject();
    *result = tTJSVariant(array, array);
    array->Release();
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
        BuildMaskContexts();
        LoadEyeBlinkAndMotions(archive);

        loaded_ = true;
        visible_ = true;
        int maskedDrawables = 0;
        if (const csmInt32 *maskCounts = GetModel()->GetDrawableMaskCounts()) {
            const csmInt32 drawableCount = GetModel()->GetDrawableCount();
            for (csmInt32 i = 0; i < drawableCount; ++i) {
                if (maskCounts[i] > 0) ++maskedDrawables;
            }
        }
        spdlog::info(
            "krkrlive2d_godot: loaded {} ({} drawables, {} masked, {} textures, {} motions) canvas={:.2f}x{:.2f} canvasPx={}x{} ppu={:.0f}",
            baseName_, GetModel()->GetDrawableCount(), maskedDrawables,
            static_cast<int>(textures_.size()), static_cast<int>(motions_.size()),
            GetModel()->GetCanvasWidth(), GetModel()->GetCanvasHeight(),
            static_cast<int>(GetModel()->GetCanvasWidthPixel()),
            static_cast<int>(GetModel()->GetCanvasHeightPixel()),
            GetModel()->GetPixelsPerUnit());
        return true;
    }

    void SetVisible(bool visible) { visible_ = visible; }
    bool IsVisible() const { return visible_; }
    bool IsLoaded() const { return loaded_ && GetModel() != nullptr; }

    void Progress() { UpdateModel(); }

    void StartMotion(const std::string &group, const std::string &motion) {
        if (!_motionManager) return;
        ACubismMotion *selected = nullptr;
        if (!motion.empty()) {
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
        if (!selected) {
            auto it = firstMotionByGroup_.find(group);
            if (it != firstMotionByGroup_.end()) selected = it->second;
        }
        if (!selected && !motions_.empty()) selected = motions_.begin()->second;
        if (selected) _motionManager->StartMotionPriority(selected, false, 2);
    }

    void StartMotionByIndex(const std::string &group, int index) {
        const std::string key = group + "_" + std::to_string(index);
        auto it = motions_.find(key);
        if (it != motions_.end() && _motionManager) {
            _motionManager->StartMotionPriority(it->second, false, 2);
        } else {
            StartMotion(group, std::string());
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
                motionNames_[group + "/" + Stem(motionPath)] = motion;
            }
        }

        if (_motionManager && !motions_.empty()) {
            _motionManager->StartMotionPriority(motions_.begin()->second,
                                                false, 1);
        }
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
            if (!model->GetDrawableDynamicFlagIsVisible(drawable)) continue;
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
        const float margin = 0.05f;
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
        dst = static_cast<uint8_t>(
            255 - (((255 - static_cast<int>(dst)) *
                    (255 - static_cast<int>(src)) + 127) /
                   255));
    }

    void RasterizeMaskDrawable(MaskContext &context, csmInt32 drawable) {
        CubismModel *model = GetModel();
        if (!model || !model->GetDrawableDynamicFlagIsVisible(drawable)) return;

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
            std::fill(context.pixels.begin(), context.pixels.end(), 0);
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
        if (_motionManager && _motionManager->IsFinished() && !motions_.empty()) {
            _motionManager->StartMotionPriority(motions_.begin()->second,
                                                false, 1);
        }
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

    uint32_t BlendModeForDrawable(csmInt32 drawableIndex) const {
        if (!GetModel()) return 0;
        const csmBlendMode blend = GetModel()->GetDrawableBlendModeType(drawableIndex);
        const csmInt32 color = blend.GetColorBlendType();
        if (color == Live2D::Cubism::Core::csmColorBlendType_Add ||
            color == Live2D::Cubism::Core::csmColorBlendType_AddGlow ||
            color == Live2D::Cubism::Core::csmColorBlendType_AddCompatible) {
            return 1;
        }
        if (color == Live2D::Cubism::Core::csmColorBlendType_Multiply ||
            color == Live2D::Cubism::Core::csmColorBlendType_MultiplyCompatible) {
            return 2;
        }
        return 0;
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
                        BlendModeForDrawable(drawableIndex),
                        model->GetDrawableInvertedMask(drawableIndex));
                } else {
                    ok = bridge->draw_triangles(
                        target->GetGodotGpuHandle(), texture.handle, triCount,
                        &clip, dst.data(), src.data(), opacity,
                        BlendModeForDrawable(drawableIndex));
                }
                dst.clear();
                src.clear();
                mask.clear();
                haveBounds = false;
                return ok;
            };

            for (csmInt32 i = 0; i + 2 < indexCount; i += 3) {
                if (dst.size() >= 64u * 3u) {
                    drewAny = flushChunk() || drewAny;
                }
                for (int j = 0; j < 3; ++j) {
                    const csmUint16 vi = indices[i + j];
                    const float ndcX = projMatrix_.TransformX(positions[vi].X);
                    const float ndcY = projMatrix_.TransformY(positions[vi].Y);
                    const double x = (static_cast<double>(ndcX) * 0.5 + 0.5) *
                                     static_cast<double>(width);
                    const double y = (0.5 - static_cast<double>(ndcY) * 0.5) *
                                     static_cast<double>(height);
                    dst.push_back({x, y});
                    src.push_back({static_cast<double>(uvs[vi].X) *
                                       static_cast<double>(texture.width),
                                   static_cast<double>(1.0f - uvs[vi].Y) *
                                       static_cast<double>(texture.height)});
                    if (useMask) {
                        mask.push_back({
                            ((static_cast<double>(positions[vi].X) -
                              maskContext->boundsX) /
                             maskContext->boundsW) *
                                static_cast<double>(kMaskTextureSize),
                            ((static_cast<double>(positions[vi].Y) -
                              maskContext->boundsY) /
                             maskContext->boundsH) *
                                static_cast<double>(kMaskTextureSize),
                        });
                    }
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
            drewAny = flushChunk() || drewAny;
        }
        return drewAny;
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
    std::unordered_map<std::string, ACubismMotion *> firstMotionByGroup_;
    csmVector<CubismIdHandle> _eyeBlinkIds;
    csmVector<CubismIdHandle> _lipSyncIds;
    CubismMatrix44 projMatrix_;
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
            GodotLive2DModel *oldModel = self->model_;
            self->model_ = newModel;
            if (oldModel) CSM_DELETE(oldModel);
        }
        SetBoolResult(result, ok);
        return TJS_S_OK;
    }

    static tjs_error renderCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                              Live2DModel *self) {
        if (self && self->model_) self->model_->Progress();
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
        if (self && self->model_) self->model_->Progress();
        SetIntResult(result, 1);
        return TJS_S_OK;
    }

    static tjs_error startMotionCb(tTJSVariant *result, tjs_int numparams,
                                   tTJSVariant **param, Live2DModel *self) {
        if (self && self->model_) {
            const std::string group =
                (numparams > 0 && param && param[0])
                    ? ToTTStr(*param[0]).AsStdString()
                    : std::string("main");
            if (numparams > 1 && param && param[1] &&
                param[1]->Type() == tvtInteger) {
                self->model_->StartMotionByIndex(group, ToInt(*param[1], 0));
            } else {
                const std::string motion =
                    (numparams > 1 && param && param[1])
                        ? ToTTStr(*param[1]).AsStdString()
                        : std::string();
                self->model_->StartMotion(group, motion);
            }
        }
        SetIntResult(result, 1);
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
    NCB_METHOD_RAW_CALLBACK(setMosaicParam, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getExpressionCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getExpressionName, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(setExpression, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getExpression, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(fixExpression, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionGroupCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionGroupName, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionName, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(startMotion, &Live2DModel::startMotionCb, 0);
    NCB_METHOD_RAW_CALLBACK(stopMotion, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getCurrentMotions, &Live2DModel::emptyArrayCb, 0);
    NCB_METHOD_RAW_CALLBACK(isPlaying, &Live2DModel::falseCb, 0);
    NCB_METHOD_RAW_CALLBACK(getParameterCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getParameterInfo, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(getParameterValue, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(setParameterValue, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setParameterType, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setDiffParameterValue, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getDiffParameterValue, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(addEyeBlinkId, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(addLipSyncId, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(canSync, &Live2DModel::falseCb, 0);
    NCB_METHOD_RAW_CALLBACK(sync, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getPartCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getPartInfo, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(setPart, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getPartValue, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(setPartValue, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setPartFadeTime, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getEventCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getEventName, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(addVriableMotion, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(delVariableMotion, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableMotionCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableMotionName, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableMotionInfo, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariable, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVariable, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(isMosaicModel, &Live2DModel::falseCb, 0);
    NCB_METHOD_RAW_CALLBACK(reload, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(resetParts, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(resetVariables, &Live2DModel::okCb, 0);
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
