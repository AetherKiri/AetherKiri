#include "GodotRenderManager.h"

#include "GodotGpuBridge.h"
#include "../LayerBitmapIntf.h"
#include "MsgIntf.h"
#include "tjsHashSearch.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace {

int BytesPerPixel(TVPTextureFormat::e format) {
    switch (format) {
        case TVPTextureFormat::Gray:
            return 1;
        case TVPTextureFormat::RGB:
            return 3;
        case TVPTextureFormat::RGBA:
            return 4;
        default:
            return 4;
    }
}

void CopyRect(uint8_t *dst, int dst_pitch, const uint8_t *src, int src_pitch,
              int bytes_per_pixel, const tTVPRect &rc) {
    const int width_bytes = std::max(0, rc.get_width()) * bytes_per_pixel;
    for (int y = rc.top; y < rc.bottom; ++y) {
        std::memcpy(dst + y * dst_pitch + rc.left * bytes_per_pixel,
                    src + (y - rc.top) * src_pitch,
                    static_cast<size_t>(width_bytes));
    }
}

std::mutex g_method_stats_mutex;
std::unordered_map<std::string, uint64_t> g_method_stats;
uint64_t g_texture_create_count = 0;
uint64_t g_software_fallback_count = 0;
uint64_t g_gpu_fastpath_count = 0;
std::atomic<bool> g_gpu_fastpath_enabled{true};
std::unordered_map<std::string, uint64_t> g_gpu_method_stats;
std::unordered_map<std::string, uint64_t> g_copy_fallback_stats;

void CountMethodFallback(iTVPRenderMethod *method) {
    std::lock_guard<std::mutex> lock(g_method_stats_mutex);
    const std::string name = method != nullptr ? method->GetName() : "(null)";
    g_method_stats[name] += 1;
    g_software_fallback_count += 1;
}

void CountGpuFastPath(const std::string &name) {
    std::lock_guard<std::mutex> lock(g_method_stats_mutex);
    g_gpu_method_stats[name] += 1;
    g_gpu_fastpath_count += 1;
}

void CountCopyFallbackReason(const std::string &reason) {
    std::lock_guard<std::mutex> lock(g_method_stats_mutex);
    g_copy_fallback_stats[reason] += 1;
}

bool TraceGpuFallback() {
    const char *value = std::getenv("AETHERKIRI_GODOT_GPU_TRACE_FALLBACK");
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

bool IsGpuRectFastPathEnabled(const char *name) {
    if (!g_gpu_fastpath_enabled.load(std::memory_order_relaxed)) {
        return false;
    }
    const auto is_default_enabled = [&]() {
        return std::strcmp(name, "FillARGB") == 0 ||
               std::strcmp(name, "Copy") == 0 ||
               std::strcmp(name, "RemoveConstOpacity") == 0 ||
               std::strcmp(name, "AlphaBlend") == 0 ||
               std::strcmp(name, "AlphaBlend_a") == 0 ||
               std::strcmp(name, "AlphaBlend_d") == 0 ||
               std::strcmp(name, "ConstAlphaBlend_d") == 0 ||
               std::strcmp(name, "ConstAlphaBlend_SD") == 0 ||
               std::strcmp(name, "ConstAlphaBlend_SD_d") == 0 ||
               std::strcmp(name, "CopyColor") == 0;
    };
    const char *value = std::getenv("AETHERKIRI_GODOT_GPU_RECT_FASTPATH");
    if (value == nullptr || value[0] == '\0') return is_default_enabled();
    const std::string setting(value);
    if (setting == "0" || setting == "off" || setting == "none") return false;
    if (setting == "1" || setting == "all" || setting == "default") {
        return is_default_enabled();
    }

    size_t start = 0;
    while (start < setting.size()) {
        const size_t end = setting.find_first_of(",;: ", start);
        const std::string token =
            setting.substr(start, end == std::string::npos ? end : end - start);
        if (token == name) return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

int GpuRectMinArea() {
    const char *value = std::getenv("AETHERKIRI_GODOT_GPU_RECT_MIN_AREA");
    // Very small blend rects are usually glyphs or UI fragments. Dispatching
    // one RenderingDevice pass per glyph is slower than letting the CPU text
    // path update the layer and uploading the dirty texture once.
    constexpr int kDefaultGpuRectMinArea = 2048;
    if (value == nullptr || value[0] == '\0') return kDefaultGpuRectMinArea;
    char *end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value || parsed < 0) return kDefaultGpuRectMinArea;
    return static_cast<int>(std::min<long>(parsed, 1 << 30));
}

int GpuRectMinAreaForMethod(const char *name) {
    if (name != nullptr &&
        (std::strcmp(name, "AlphaBlend") == 0 ||
         std::strcmp(name, "AlphaBlend_a") == 0 ||
         std::strcmp(name, "AlphaBlend_d") == 0)) {
        const char *value =
            std::getenv("AETHERKIRI_GODOT_GPU_ALPHA_RECT_MIN_AREA");
        constexpr int kDefaultAlphaRectMinArea = 0;
        if (value == nullptr || value[0] == '\0') return kDefaultAlphaRectMinArea;
        char *end = nullptr;
        long parsed = std::strtol(value, &end, 10);
        if (end == value || parsed < 0) return kDefaultAlphaRectMinArea;
        return static_cast<int>(std::min<long>(parsed, 1 << 30));
    }
    return GpuRectMinArea();
}

bool IsGpuRectLargeEnoughForMethod(const tTVPRect &rect, const char *name) {
    return rect.get_width() > 0 && rect.get_height() > 0 &&
           rect.get_width() * rect.get_height() >= GpuRectMinAreaForMethod(name);
}

bool IsOpaqueAlphaBlendCopyEnabled() {
    const char *value = std::getenv("AETHERKIRI_GODOT_GPU_OPAQUE_COPY");
    return value == nullptr || value[0] == '\0' || std::strcmp(value, "0") != 0;
}

bool RectAbsSizeMatches(const tTVPRect &dst, const tTVPRect &src) {
    return dst.get_width() > 0 && dst.get_height() > 0 &&
           std::abs(src.get_width()) == dst.get_width() &&
           std::abs(src.get_height()) == dst.get_height();
}

bool IsFullTextureRect(const tTVPRect &rc, int width, int height) {
    return rc.left <= 0 && rc.top <= 0 && rc.right >= width && rc.bottom >= height;
}

bool ScanOpaqueRgba(const void *pixel, int pitch, int width, int height) {
    if (pixel == nullptr || width <= 0 || height <= 0) return false;
    const auto *bytes = static_cast<const uint8_t *>(pixel);
    const int stride = pitch > 0 ? pitch : width * 4;
    for (int y = 0; y < height; ++y) {
        const uint8_t *row = bytes + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
            if (row[x * 4 + 3] != 0xff) return false;
        }
    }
    return true;
}

} // namespace

iTVPRenderManager *TVPGetSoftwareRenderManager();

GodotRenderMethod::GodotRenderMethod(iTVPRenderMethod *delegate)
    : delegate_(delegate) {}

int GodotRenderMethod::EnumParameterID(const char *name) {
    return delegate_ != nullptr ? delegate_->EnumParameterID(name) : -1;
}
void GodotRenderMethod::SetParameterUInt(int id, unsigned int Value) {
    if (delegate_) delegate_->SetParameterUInt(id, Value);
}
void GodotRenderMethod::SetParameterInt(int id, int Value) {
    if (delegate_) delegate_->SetParameterInt(id, Value);
}
void GodotRenderMethod::SetParameterPtr(int id, const void *Value) {
    if (delegate_) delegate_->SetParameterPtr(id, Value);
}
void GodotRenderMethod::SetParameterFloat(int id, float Value) {
    if (delegate_) delegate_->SetParameterFloat(id, Value);
}
void GodotRenderMethod::SetParameterColor4B(int id, unsigned int clr) {
    color_ = clr;
    if (delegate_) delegate_->SetParameterColor4B(id, clr);
}
void GodotRenderMethod::SetParameterOpa(int id, int Value) {
    opacity_ = Value;
    if (delegate_) delegate_->SetParameterOpa(id, Value);
}
void GodotRenderMethod::SetParameterFloatArray(int id, float *Value, int nElem) {
    if (delegate_) delegate_->SetParameterFloatArray(id, Value, nElem);
}
iTVPRenderMethod *GodotRenderMethod::SetBlendFuncSeparate(
    int func, int srcRGB, int dstRGB, int srcAlpha, int dstAlpha) {
    if (delegate_) {
        delegate_->SetBlendFuncSeparate(func, srcRGB, dstRGB, srcAlpha, dstAlpha);
    }
    return this;
}
bool GodotRenderMethod::IsBlendTarget() {
    return delegate_ == nullptr || delegate_->IsBlendTarget();
}

iTVPRenderManager *GodotRenderManager::SoftwareDelegate() {
    if (software_delegate_ == nullptr) {
        software_delegate_ = TVPGetSoftwareRenderManager();
    }
    return software_delegate_;
}

GodotTexture2D::GodotTexture2D(const void *pixel, int pitch, unsigned int w,
                               unsigned int h, TVPTextureFormat::e format)
    : iTVPTexture2D(static_cast<tjs_int>(w), static_cast<tjs_int>(h)),
      format_(format),
      pitch_(pitch > 0 ? pitch : static_cast<int>(w) * BytesPerPixel(format)) {
    pixels_.resize(static_cast<size_t>(pitch_) * h);
    if (pixel != nullptr) {
        const int src_pitch = pitch > 0 ? pitch : pitch_;
        const auto *src = static_cast<const uint8_t *>(pixel);
        for (unsigned int y = 0; y < h; ++y) {
            std::memcpy(pixels_.data() + static_cast<size_t>(y) * pitch_,
                        src + static_cast<size_t>(y) * src_pitch,
                        static_cast<size_t>(std::min(pitch_, src_pitch)));
        }
        SetOpacityFromPixels(pixels_.data(), pitch_);
    } else {
        MarkTransparentKnown();
    }
    MarkCpuDirty();
}

GodotTexture2D::~GodotTexture2D() { ReleaseGpuHandle(); }

void GodotTexture2D::EnsureCpuStorage() {
    const size_t required = static_cast<size_t>(pitch_) * Height;
    if (pixels_.size() != required) {
        pixels_.assign(required, 0);
    }
}

void GodotTexture2D::DiscardCpuStorage() {
    if (pixels_.empty()) return;
    std::vector<uint8_t>().swap(pixels_);
}

void GodotTexture2D::SetOpacityFromPixels(const void *pixel, int pitch) {
    (void)pixel;
    (void)pitch;
    MarkOpacityUnknown();
}

void GodotTexture2D::MarkOpacityUnknown() {
    opacity_known_ = false;
    opaque_ = false;
}

void GodotTexture2D::MarkTransparentKnown() {
    opacity_known_ = true;
    opaque_ = false;
}

void GodotTexture2D::MarkOpaqueKnown() {
    opacity_known_ = true;
    opaque_ = true;
}

void GodotTexture2D::CreateGpuHandle(const void *pixel, int pitch) {
    if (format_ != TVPTextureFormat::RGBA) return;
    const auto *bridge = TVPGodotGpuBridgeGet();
    if (bridge == nullptr || bridge->create_rgba == nullptr) return;
    const void *src = pixel != nullptr ? pixel :
        (pixels_.empty() ? nullptr : pixels_.data());
    const uint32_t stride = static_cast<uint32_t>(
        pixel != nullptr && pitch > 0 ? pitch : pitch_);
    gpu_handle_ = bridge->create_rgba(static_cast<uint32_t>(Width),
                                      static_cast<uint32_t>(Height),
                                      src, stride);
    if (gpu_handle_ == 0) {
        return;
    }
    gpu_dirty_ = false;
    cpu_dirty_ = false;
    DiscardCpuStorage();
}

bool GodotTexture2D::EnsureGpuHandle() {
    if (gpu_handle_ == 0) {
        CreateGpuHandle(nullptr, 0);
    } else if (cpu_dirty_) {
        const auto *bridge = TVPGodotGpuBridgeGet();
        if (bridge == nullptr || bridge->update_rgba == nullptr ||
            format_ != TVPTextureFormat::RGBA || pixels_.empty()) {
            return false;
        }
        const tTVPRect full_rect(0, 0, Width, Height);
        if (!bridge->update_rgba(gpu_handle_, pixels_.data(),
                                 static_cast<uint32_t>(pitch_), &full_rect)) {
            return false;
        }
        gpu_dirty_ = false;
        cpu_dirty_ = false;
        DiscardCpuStorage();
    }
    return gpu_handle_ != 0;
}

void GodotTexture2D::ReleaseGpuHandle() {
    if (gpu_handle_ == 0) return;
    const auto *bridge = TVPGodotGpuBridgeGet();
    if (bridge != nullptr && bridge->release_texture != nullptr) {
        bridge->release_texture(gpu_handle_);
    }
    gpu_handle_ = 0;
    gpu_dirty_ = false;
    cpu_dirty_ = false;
}

void GodotTexture2D::EnsureCpuReadable() {
    if (cpu_dirty_) {
        EnsureCpuStorage();
        return;
    }
    if (gpu_handle_ == 0) {
        EnsureCpuStorage();
        return;
    }
    if (!gpu_dirty_ && !pixels_.empty()) return;
    EnsureCpuStorage();
    const auto *bridge = TVPGodotGpuBridgeGet();
    if (bridge != nullptr && bridge->read_rgba != nullptr &&
        bridge->read_rgba(gpu_handle_, pixels_.data(), pixels_.size(),
                          static_cast<uint32_t>(pitch_))) {
        gpu_dirty_ = false;
    }
}

const void *GodotTexture2D::GetScanLineForRead(tjs_uint l) {
    EnsureCpuReadable();
    if (l >= static_cast<tjs_uint>(Height) || pixels_.empty()) return nullptr;
    return pixels_.data() + static_cast<size_t>(l) * pitch_;
}

void *GodotTexture2D::GetScanLineForWrite(tjs_uint l) {
    EnsureCpuReadable();
    if (l >= static_cast<tjs_uint>(Height) || pixels_.empty()) return nullptr;
    MarkCpuDirty();
    return pixels_.data() + static_cast<size_t>(l) * pitch_;
}

void GodotTexture2D::Update(const void *pixel, TVPTextureFormat::e format,
                            int pitch, const tTVPRect &rc) {
    if (pixel == nullptr) return;
    const bool full_rect = IsFullTextureRect(rc, Width, Height);
    if (!full_rect) {
        EnsureCpuReadable();
    } else {
        EnsureCpuStorage();
    }
    format_ = format;
    const int bpp = BytesPerPixel(format_);
    const int src_pitch = pitch > 0 ? pitch : rc.get_width() * bpp;
    CopyRect(pixels_.data(), pitch_, static_cast<const uint8_t *>(pixel),
             src_pitch, bpp, rc);
    if (full_rect) {
        SetOpacityFromPixels(pixels_.data(), pitch_);
    } else {
        MarkOpacityUnknown();
    }
    MarkCpuDirty();
}

uint32_t GodotTexture2D::GetPoint(int x, int y) {
    if (x < 0 || y < 0 || x >= Width || y >= Height || format_ != TVPTextureFormat::RGBA) {
        return 0;
    }
    EnsureCpuReadable();
    uint32_t value = 0;
    std::memcpy(&value, pixels_.data() + static_cast<size_t>(y) * pitch_ + x * 4, 4);
    return value;
}

void GodotTexture2D::SetPoint(int x, int y, uint32_t clr) {
    if (x < 0 || y < 0 || x >= Width || y >= Height || format_ != TVPTextureFormat::RGBA) {
        return;
    }
    EnsureCpuReadable();
    std::memcpy(pixels_.data() + static_cast<size_t>(y) * pitch_ + x * 4, &clr, 4);
    MarkCpuDirty();
}

void GodotTexture2D::SetSize(unsigned int w, unsigned int h) {
    ReleaseGpuHandle();
    Width = static_cast<tjs_int>(w);
    Height = static_cast<tjs_int>(h);
    pitch_ = static_cast<int>(w) * BytesPerPixel(format_);
    pixels_.assign(static_cast<size_t>(pitch_) * h, 0);
    MarkTransparentKnown();
    MarkCpuDirty();
}

bool GodotTexture2D::ClearGpu(uint32_t rgba, const tTVPRect &rc) {
    if (gpu_handle_ == 0 || format_ != TVPTextureFormat::RGBA) return false;
    const auto *bridge = TVPGodotGpuBridgeGet();
    if (bridge == nullptr || bridge->clear_rgba == nullptr) return false;
    if (!bridge->clear_rgba(gpu_handle_, rgba, &rc)) return false;
    gpu_dirty_ = true;
    cpu_dirty_ = false;
    if (IsFullTextureRect(rc, Width, Height)) {
        ((rgba >> 24) & 0xffu) == 0xffu ? MarkOpaqueKnown()
                                         : MarkTransparentKnown();
    } else {
        MarkOpacityUnknown();
    }
    return true;
}

bool GodotTexture2D::CopyGpuFrom(GodotTexture2D *src, const tTVPRect &dst_rc,
                                 const tTVPRect &src_rc) {
    if (src == nullptr || gpu_handle_ == 0 || src->gpu_handle_ == 0) {
        return false;
    }
    const auto *bridge = TVPGodotGpuBridgeGet();
    if (bridge == nullptr || bridge->copy_rect == nullptr) return false;
    if (!bridge->copy_rect(gpu_handle_, src->gpu_handle_, &dst_rc, &src_rc)) {
        return false;
    }
    gpu_dirty_ = true;
    cpu_dirty_ = false;
    if (IsFullTextureRect(dst_rc, Width, Height) &&
        IsFullTextureRect(src_rc, src->Width, src->Height)) {
        opacity_known_ = src->opacity_known_;
        opaque_ = src->opaque_;
    } else {
        MarkOpacityUnknown();
    }
    return true;
}

bool GodotTexture2D::CopyTrianglesGpuFrom(GodotTexture2D *src,
                                          uint32_t triangle_count,
                                          const tTVPRect &clip_rc,
                                          const tTVPPointD *dst_points,
                                          const tTVPPointD *src_points) {
    if (src == nullptr || triangle_count == 0 || dst_points == nullptr ||
        src_points == nullptr || gpu_handle_ == 0 || src->gpu_handle_ == 0) {
        return false;
    }
    const auto *bridge = TVPGodotGpuBridgeGet();
    if (bridge == nullptr || bridge->copy_triangles == nullptr) return false;
    if (!bridge->copy_triangles(gpu_handle_, src->gpu_handle_, triangle_count,
                                &clip_rc, dst_points, src_points)) {
        return false;
    }
    gpu_dirty_ = true;
    cpu_dirty_ = false;
    MarkOpacityUnknown();
    return true;
}

bool GodotTexture2D::BlendGpuFrom(GodotTexture2D *src, const tTVPRect &dst_rc,
                                  const tTVPRect &src_rc, uint32_t mode,
                                  int opacity, uint32_t color) {
    if (src == nullptr || gpu_handle_ == 0 || src->gpu_handle_ == 0) {
        return false;
    }
    if (src == this && mode != TVP_GODOT_GPU_BLEND_REMOVE_CONST_OPACITY) {
        return false;
    }
    const auto *bridge = TVPGodotGpuBridgeGet();
    if (bridge == nullptr || bridge->blend_rect == nullptr) return false;
    if (!bridge->blend_rect(gpu_handle_, src->gpu_handle_, &dst_rc, &src_rc,
                            mode, opacity, color)) {
        return false;
    }
    gpu_dirty_ = true;
    cpu_dirty_ = false;
    MarkOpacityUnknown();
    return true;
}

bool GodotTexture2D::BlendGpuFrom2(GodotTexture2D *src1, GodotTexture2D *src2,
                                   const tTVPRect &dst_rc,
                                   const tTVPRect &src1_rc,
                                   const tTVPRect &src2_rc, uint32_t mode,
                                   int opacity, uint32_t color) {
    if (src1 == nullptr || src2 == nullptr || gpu_handle_ == 0 ||
        src1->gpu_handle_ == 0 || src2->gpu_handle_ == 0) {
        return false;
    }
    const auto *bridge = TVPGodotGpuBridgeGet();
    if (bridge == nullptr || bridge->blend_rect2 == nullptr) return false;
    if (!bridge->blend_rect2(gpu_handle_, src1->gpu_handle_, src2->gpu_handle_,
                             &dst_rc, &src1_rc, &src2_rc, mode, opacity,
                             color)) {
        return false;
    }
    gpu_dirty_ = true;
    cpu_dirty_ = false;
    MarkOpacityUnknown();
    return true;
}

bool GodotTexture2D::UploadCpuToGpu() {
    if (!cpu_dirty_) {
        if (gpu_dirty_) {
            const auto *bridge = TVPGodotGpuBridgeGet();
            if (bridge != nullptr && bridge->flush != nullptr) {
                return bridge->flush();
            }
        }
        return true;
    }
    if (!EnsureGpuHandle() || format_ != TVPTextureFormat::RGBA || pixels_.empty()) {
        return false;
    }
    const auto *bridge = TVPGodotGpuBridgeGet();
    if (bridge == nullptr || bridge->update_rgba == nullptr) return false;
    const tTVPRect full_rect(0, 0, Width, Height);
    if (!bridge->update_rgba(gpu_handle_, pixels_.data(),
                             static_cast<uint32_t>(pitch_), &full_rect)) {
        return false;
    }
    gpu_dirty_ = false;
    cpu_dirty_ = false;
    DiscardCpuStorage();
    return true;
}

iTVPTexture2D *GodotRenderManager::CreateTexture2D(const void *pixel, int pitch,
                                                   unsigned int w,
                                                   unsigned int h,
                                                   TVPTextureFormat::e format,
                                                   int) {
    auto *texture = new GodotTexture2D(pixel, pitch, w, h, format);
    vmem_size_ += static_cast<uint64_t>(texture->GetPitch()) * h;
    {
        std::lock_guard<std::mutex> lock(g_method_stats_mutex);
        g_texture_create_count += 1;
    }
    return texture;
}

iTVPTexture2D *GodotRenderManager::CreateTexture2D(tTVPBitmap *bmp) {
    if (bmp == nullptr) {
        return CreateTexture2D(nullptr, 0, 1, 1, TVPTextureFormat::RGBA);
    }
    return CreateTexture2D(bmp->GetScanLine(0), bmp->GetPitch(),
                           bmp->GetWidth(), bmp->GetHeight(),
                           bmp->GetBPP() == 8 ? TVPTextureFormat::Gray
                                               : TVPTextureFormat::RGBA);
}

iTVPTexture2D *GodotRenderManager::CreateTexture2D(TJS::tTJSBinaryStream *) {
    return CreateTexture2D(nullptr, 0, 1, 1, TVPTextureFormat::RGBA);
}

iTVPTexture2D *GodotRenderManager::CreateTexture2D(unsigned int neww,
                                                   unsigned int newh,
                                                   iTVPTexture2D *tex) {
    auto *ret = new GodotTexture2D(nullptr, 0, neww, newh,
                                  tex != nullptr ? tex->GetFormat()
                                                 : TVPTextureFormat::RGBA);
    if (tex != nullptr) {
        const tTVPRect copy_rc(0, 0,
                               std::min<tjs_int>(neww, tex->GetWidth()),
                               std::min<tjs_int>(newh, tex->GetHeight()));
        if (!copy_rc.is_empty()) {
            const void *src_pixels = tex->GetScanLineForRead(0);
            if (src_pixels != nullptr) {
                ret->Update(src_pixels, tex->GetFormat(), tex->GetPitch(),
                            copy_rc);
            }
        }
    }
    return ret;
}

iTVPRenderMethod *GodotRenderManager::GetRenderMethod(const char *name,
                                                      uint32_t *hint) {
    uint32_t hash = 0;
    if (hint != nullptr && *hint != 0) {
        hash = *hint;
    } else {
        hash = tTJSHashFunc<tjs_nchar *>::Make(name);
        if (hint != nullptr) *hint = hash;
    }
    auto it = method_wrappers_.find(hash);
    if (it != method_wrappers_.end()) return it->second;
    iTVPRenderMethod *delegate = SoftwareDelegate()->GetRenderMethod(name, &hash);
    auto *wrapper = new GodotRenderMethod(delegate);
    wrapper->SetName(name);
    method_wrappers_[hash] = wrapper;
    return wrapper;
}

bool GodotRenderManager::GetRenderStat(unsigned int &drawCount,
                                       uint64_t &vmemsize) {
    unsigned int delegate_draws = 0;
    uint64_t delegate_vmem = 0;
    const bool ok = SoftwareDelegate()->GetRenderStat(delegate_draws, delegate_vmem);
    drawCount = draw_count_ + delegate_draws;
    draw_count_ = 0;
    vmemsize = std::max(vmem_size_, delegate_vmem);
    return ok;
}

bool GodotRenderManager::GetTextureStat(iTVPTexture2D *texture,
                                        uint64_t &vmemsize) {
    return SoftwareDelegate()->GetTextureStat(texture, vmemsize);
}

void GodotRenderManager::OperateRect(iTVPRenderMethod *method, iTVPTexture2D *tar,
                                     iTVPTexture2D *reftar,
                                     const tTVPRect &rctar,
                                     const tRenderTexRectArray &textures) {
    ++draw_count_;
    auto *godot_method = dynamic_cast<GodotRenderMethod *>(method);
    iTVPRenderMethod *delegate_method =
        godot_method != nullptr ? godot_method->Delegate() : method;
    const std::string method_name =
        method != nullptr ? method->GetName() : std::string();

    auto *dst = dynamic_cast<GodotTexture2D *>(tar);
    auto *src = textures.size() == 1
        ? dynamic_cast<GodotTexture2D *>(textures[0].first)
        : nullptr;
    auto *src1 = textures.size() == 2
        ? dynamic_cast<GodotTexture2D *>(textures[0].first)
        : nullptr;
    auto *src2 = textures.size() == 2
        ? dynamic_cast<GodotTexture2D *>(textures[1].first)
        : nullptr;

    if (method_name == "Copy" && dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("Copy") &&
        IsGpuRectLargeEnoughForMethod(rctar, method_name.c_str()) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu()) {
        const tTVPRect &src_rc = textures[0].second;
        if (src_rc.get_width() == rctar.get_width() &&
            src_rc.get_height() == rctar.get_height() &&
            dst->CopyGpuFrom(src, rctar, src_rc)) {
            CountGpuFastPath(method_name);
            return;
        }
        if (!src_rc.is_empty()) {
            const tTVPPointD dst_pt[6] = {
                {static_cast<double>(rctar.left), static_cast<double>(rctar.top)},
                {static_cast<double>(rctar.right), static_cast<double>(rctar.top)},
                {static_cast<double>(rctar.left), static_cast<double>(rctar.bottom)},
                {static_cast<double>(rctar.right), static_cast<double>(rctar.top)},
                {static_cast<double>(rctar.left), static_cast<double>(rctar.bottom)},
                {static_cast<double>(rctar.right), static_cast<double>(rctar.bottom)},
            };
            const tTVPPointD src_pt[6] = {
                {static_cast<double>(src_rc.left), static_cast<double>(src_rc.top)},
                {static_cast<double>(src_rc.right), static_cast<double>(src_rc.top)},
                {static_cast<double>(src_rc.left), static_cast<double>(src_rc.bottom)},
                {static_cast<double>(src_rc.right), static_cast<double>(src_rc.top)},
                {static_cast<double>(src_rc.left), static_cast<double>(src_rc.bottom)},
                {static_cast<double>(src_rc.right), static_cast<double>(src_rc.bottom)},
            };
            if (dst->CopyTrianglesGpuFrom(src, 2, rctar, dst_pt, src_pt)) {
                CountGpuFastPath(method_name);
                return;
            }
        }
        if (!RectAbsSizeMatches(rctar, src_rc)) {
            CountCopyFallbackReason("scaled_copy_bridge_failed");
            CountMethodFallback(method);
            SoftwareDelegate()->OperateRect(delegate_method, tar, reftar, rctar, textures);
            if (dst != nullptr) {
                dst->MarkCpuDirty();
            }
            return;
        }
        const double sx0 = src_rc.get_width() < 0 ? src_rc.left - 1 : src_rc.left;
        const double sx1 = src_rc.get_width() < 0 ? src_rc.right - 1 : src_rc.right;
        const double sy0 = src_rc.get_height() < 0 ? src_rc.top - 1 : src_rc.top;
        const double sy1 = src_rc.get_height() < 0 ? src_rc.bottom - 1 : src_rc.bottom;
        const tTVPPointD dst_pt[6] = {
            {static_cast<double>(rctar.left), static_cast<double>(rctar.top)},
            {static_cast<double>(rctar.right), static_cast<double>(rctar.top)},
            {static_cast<double>(rctar.left), static_cast<double>(rctar.bottom)},
            {static_cast<double>(rctar.right), static_cast<double>(rctar.top)},
            {static_cast<double>(rctar.left), static_cast<double>(rctar.bottom)},
            {static_cast<double>(rctar.right), static_cast<double>(rctar.bottom)},
        };
        const tTVPPointD src_pt[6] = {
            {sx0, sy0},
            {sx1, sy0},
            {sx0, sy1},
            {sx1, sy0},
            {sx0, sy1},
            {sx1, sy1},
        };
        if (dst->CopyTrianglesGpuFrom(src, 2, rctar, dst_pt, src_pt)) {
            CountGpuFastPath(method_name);
            return;
        }
    }

    if (method_name == "CopyColor" && dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("CopyColor") &&
        IsGpuRectLargeEnoughForMethod(rctar, method_name.c_str()) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu() &&
        dst->BlendGpuFrom(src, rctar, textures[0].second,
                          TVP_GODOT_GPU_BLEND_COPY_COLOR, 255, 0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (method_name == "AlphaBlend" && dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("AlphaBlend") &&
        IsGpuRectLargeEnoughForMethod(rctar, method_name.c_str()) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu() &&
        dst->BlendGpuFrom(src, rctar, textures[0].second,
                          TVP_GODOT_GPU_BLEND_ALPHA,
                          godot_method != nullptr ? godot_method->Opacity() : 255,
                          0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (method_name == "AlphaBlend_d" && dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("AlphaBlend_d") &&
        IsGpuRectLargeEnoughForMethod(rctar, method_name.c_str()) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu() &&
        dst->BlendGpuFrom(src, rctar, textures[0].second,
                          TVP_GODOT_GPU_BLEND_ALPHA_D,
                          godot_method != nullptr ? godot_method->Opacity() : 255,
                          0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (method_name == "ConstAlphaBlend_d" && dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("ConstAlphaBlend_d") &&
        IsGpuRectLargeEnoughForMethod(rctar, method_name.c_str()) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu() &&
        dst->BlendGpuFrom(src, rctar, textures[0].second,
                          TVP_GODOT_GPU_BLEND_CONST_ALPHA_D,
                          godot_method != nullptr ? godot_method->Opacity() : 255,
                          0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if ((method_name == "AlphaBlend_a" ||
         method_name == "PerspectiveAlphaBlend_a") &&
        dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("AlphaBlend_a") &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu()) {
        const tTVPRect &src_rc = textures[0].second;
        const int opacity = godot_method != nullptr ? godot_method->Opacity() : 255;
        if (IsOpaqueAlphaBlendCopyEnabled() && opacity == 255 && src->IsOpaque()) {
            if (src_rc.get_width() == rctar.get_width() &&
                src_rc.get_height() == rctar.get_height() &&
                dst->CopyGpuFrom(src, rctar, src_rc)) {
                CountGpuFastPath(method_name + ":CopyOpaque");
                return;
            }
            if (!src_rc.is_empty()) {
                const tTVPPointD dst_pt[6] = {
                    {static_cast<double>(rctar.left), static_cast<double>(rctar.top)},
                    {static_cast<double>(rctar.right), static_cast<double>(rctar.top)},
                    {static_cast<double>(rctar.left), static_cast<double>(rctar.bottom)},
                    {static_cast<double>(rctar.right), static_cast<double>(rctar.top)},
                    {static_cast<double>(rctar.left), static_cast<double>(rctar.bottom)},
                    {static_cast<double>(rctar.right), static_cast<double>(rctar.bottom)},
                };
                const tTVPPointD src_pt[6] = {
                    {static_cast<double>(src_rc.left), static_cast<double>(src_rc.top)},
                    {static_cast<double>(src_rc.right), static_cast<double>(src_rc.top)},
                    {static_cast<double>(src_rc.left), static_cast<double>(src_rc.bottom)},
                    {static_cast<double>(src_rc.right), static_cast<double>(src_rc.top)},
                    {static_cast<double>(src_rc.left), static_cast<double>(src_rc.bottom)},
                    {static_cast<double>(src_rc.right), static_cast<double>(src_rc.bottom)},
                };
                if (dst->CopyTrianglesGpuFrom(src, 2, rctar, dst_pt, src_pt)) {
                    CountGpuFastPath(method_name + ":CopyOpaque");
                    return;
                }
            }
        }
        if (dst->BlendGpuFrom(src, rctar, src_rc,
                          TVP_GODOT_GPU_BLEND_ALPHA_BLEND_A,
                          opacity, 0)) {
            CountGpuFastPath(method_name);
            return;
        }
    }

    if (method_name == "FillARGB" && dst != nullptr &&
        IsGpuRectFastPathEnabled("FillARGB") &&
        IsGpuRectLargeEnoughForMethod(rctar, method_name.c_str()) &&
        dst->EnsureGpuHandle() &&
        dst->ClearGpu(godot_method != nullptr ? godot_method->Color() : 0,
                      rctar)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (method_name == "RemoveConstOpacity" && dst != nullptr &&
        IsGpuRectFastPathEnabled("RemoveConstOpacity") &&
        IsGpuRectLargeEnoughForMethod(rctar, method_name.c_str()) &&
        dst->EnsureGpuHandle() &&
        dst->BlendGpuFrom(dst, rctar, rctar,
                          TVP_GODOT_GPU_BLEND_REMOVE_CONST_OPACITY,
                          godot_method != nullptr ? godot_method->Opacity() : 255,
                          0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if ((method_name == "ConstAlphaBlend_SD" ||
         method_name == "ConstAlphaBlend_SD_d") &&
        dst != nullptr && src1 != nullptr && src2 != nullptr &&
        IsGpuRectFastPathEnabled(method_name.c_str()) &&
        IsGpuRectLargeEnoughForMethod(rctar, method_name.c_str()) &&
        dst->EnsureGpuHandle() && src1->EnsureGpuHandle() &&
        src2->EnsureGpuHandle() &&
        src1->UploadCpuToGpu() && src2->UploadCpuToGpu() &&
        dst->BlendGpuFrom2(
            src1, src2, rctar, textures[0].second, textures[1].second,
            method_name == "ConstAlphaBlend_SD_d"
                ? TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD_D
                : TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD,
            godot_method != nullptr ? godot_method->Opacity() : 255, 0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (TraceGpuFallback() && method_name == "Copy") {
        std::fprintf(stderr,
                     "godot_gpu_fallback method=Copy tex_count=%zu dst=%p src=%p "
                     "dst_handle=%llu src_handle=%llu target=(%d,%d,%d,%d) "
                     "src_rect=(%d,%d,%d,%d) large=%d enabled=%d\n",
                     textures.size(), static_cast<void *>(dst), static_cast<void *>(src),
                     static_cast<unsigned long long>(dst != nullptr ? dst->GetGodotGpuHandle() : 0),
                     static_cast<unsigned long long>(src != nullptr ? src->GetGodotGpuHandle() : 0),
                     rctar.left, rctar.top, rctar.right, rctar.bottom,
                     textures.size() == 1 ? textures[0].second.left : 0,
                     textures.size() == 1 ? textures[0].second.top : 0,
                     textures.size() == 1 ? textures[0].second.right : 0,
                     textures.size() == 1 ? textures[0].second.bottom : 0,
                     IsGpuRectLargeEnoughForMethod(rctar, method_name.c_str()) ? 1 : 0,
                     IsGpuRectFastPathEnabled("Copy") ? 1 : 0);
    }
    if (method_name == "Copy") {
        if (textures.size() != 1) {
            CountCopyFallbackReason("texture_count");
        } else if (dst == nullptr) {
            CountCopyFallbackReason("dst_not_godot");
        } else if (src == nullptr) {
            CountCopyFallbackReason("src_not_godot");
        } else if (!IsGpuRectFastPathEnabled("Copy")) {
            CountCopyFallbackReason("disabled");
        } else if (!IsGpuRectLargeEnoughForMethod(rctar, method_name.c_str())) {
            CountCopyFallbackReason("small_rect");
        } else if (rctar.get_width() != textures[0].second.get_width() ||
                   rctar.get_height() != textures[0].second.get_height() ||
                   textures[0].second.get_width() <= 0 ||
                   textures[0].second.get_height() <= 0) {
            std::ostringstream reason;
            reason << "mismatch_d" << rctar.get_width() << "x" << rctar.get_height()
                   << "_s" << textures[0].second.get_width() << "x"
                   << textures[0].second.get_height()
                   << "_src" << textures[0].second.left << "," << textures[0].second.top
                   << "," << textures[0].second.right << "," << textures[0].second.bottom;
            CountCopyFallbackReason(reason.str());
        } else if (!dst->HasGodotGpuHandle()) {
            CountCopyFallbackReason("dst_no_gpu_handle");
        } else if (!src->HasGodotGpuHandle()) {
            CountCopyFallbackReason("src_no_gpu_handle");
        } else {
            CountCopyFallbackReason("bridge_copy_failed");
        }
    }

    CountMethodFallback(method);
    SoftwareDelegate()->OperateRect(delegate_method, tar, reftar, rctar, textures);
    if (dst != nullptr) {
        dst->MarkCpuDirty();
    }
}

void GodotRenderManager::OperateTriangles(iTVPRenderMethod *method, int nTriangles,
                                          iTVPTexture2D *target,
                                          iTVPTexture2D *reftar,
                                          const tTVPRect &rcclip,
                                          const tTVPPointD *pttar,
                                          const tRenderTexQuadArray &textures) {
    ++draw_count_;
    const std::string method_name =
        method != nullptr ? method->GetName() : std::string();
    if (method_name == "Copy") {
        auto *dst = dynamic_cast<GodotTexture2D *>(target);
        auto *src = textures.size() == 1
            ? dynamic_cast<GodotTexture2D *>(textures[0].first)
            : nullptr;
        if (dst != nullptr && src != nullptr &&
            IsGpuRectFastPathEnabled("Copy") &&
            dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
            src->UploadCpuToGpu() &&
            dst->CopyTrianglesGpuFrom(src, static_cast<uint32_t>(nTriangles),
                                      rcclip, pttar, textures[0].second)) {
            CountGpuFastPath(method_name);
            return;
        }
        CountCopyFallbackReason("triangles");
    }
    CountMethodFallback(method);
    auto *godot_method = dynamic_cast<GodotRenderMethod *>(method);
    SoftwareDelegate()->OperateTriangles(
        godot_method != nullptr ? godot_method->Delegate() : method,
        nTriangles, target, reftar, rcclip, pttar, textures);
    if (auto *dst = dynamic_cast<GodotTexture2D *>(target)) {
        dst->MarkCpuDirty();
    }
}

void GodotRenderManager::OperatePerspective(iTVPRenderMethod *method, int nQuads,
                                            iTVPTexture2D *target,
                                            iTVPTexture2D *reftar,
                                            const tTVPRect &rcclip,
                                            const tTVPPointD *pttar,
                                            const tRenderTexQuadArray &textures) {
    ++draw_count_;
    const std::string method_name =
        method != nullptr ? method->GetName() : std::string();
    if (method_name == "Copy") {
        CountCopyFallbackReason("perspective");
    }
    CountMethodFallback(method);
    auto *godot_method = dynamic_cast<GodotRenderMethod *>(method);
    SoftwareDelegate()->OperatePerspective(
        godot_method != nullptr ? godot_method->Delegate() : method,
        nQuads, target, reftar, rcclip, pttar, textures);
    if (auto *dst = dynamic_cast<GodotTexture2D *>(target)) {
        dst->MarkCpuDirty();
    }
}

std::string TVPGetGodotRenderManagerFallbackStats() {
    std::vector<std::pair<std::string, uint64_t>> entries;
    uint64_t texture_creates = 0;
    uint64_t fallbacks = 0;
    uint64_t gpu_fastpaths = 0;
    std::vector<std::pair<std::string, uint64_t>> gpu_entries;
    std::vector<std::pair<std::string, uint64_t>> copy_fallback_entries;
    {
        std::lock_guard<std::mutex> lock(g_method_stats_mutex);
        entries.reserve(g_method_stats.size());
        for (const auto &entry : g_method_stats) {
            entries.push_back(entry);
        }
        gpu_entries.reserve(g_gpu_method_stats.size());
        for (const auto &entry : g_gpu_method_stats) {
            gpu_entries.push_back(entry);
        }
        copy_fallback_entries.reserve(g_copy_fallback_stats.size());
        for (const auto &entry : g_copy_fallback_stats) {
            copy_fallback_entries.push_back(entry);
        }
        texture_creates = g_texture_create_count;
        fallbacks = g_software_fallback_count;
        gpu_fastpaths = g_gpu_fastpath_count;
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });
    std::sort(gpu_entries.begin(), gpu_entries.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });
    std::sort(copy_fallback_entries.begin(), copy_fallback_entries.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });

    std::ostringstream out;
    out << " fallback_ops=" << fallbacks
        << " gpu_ops=" << gpu_fastpaths
        << " gpu_min_area=" << GpuRectMinArea()
        << " gpu_alpha_min_area=" << GpuRectMinAreaForMethod("AlphaBlend")
        << " texture_creates=" << texture_creates
        << " hot=[";
    const size_t limit = std::min<size_t>(entries.size(), 8);
    for (size_t i = 0; i < limit; ++i) {
        if (i != 0) out << ",";
        out << entries[i].first << ":" << entries[i].second;
    }
    out << "] gpu_hot=[";
    const size_t gpu_limit = std::min<size_t>(gpu_entries.size(), 8);
    for (size_t i = 0; i < gpu_limit; ++i) {
        if (i != 0) out << ",";
        out << gpu_entries[i].first << ":" << gpu_entries[i].second;
    }
    out << "] copy_fallback=[";
    const size_t copy_limit = std::min<size_t>(copy_fallback_entries.size(), 8);
    for (size_t i = 0; i < copy_limit; ++i) {
        if (i != 0) out << ",";
        out << copy_fallback_entries[i].first << ":" << copy_fallback_entries[i].second;
    }
    out << "]";
    return out.str();
}

namespace {
iTVPRenderManager *CreateGodotRenderManager() { return new GodotRenderManager(); }

class GodotRenderManagerAutoRegister {
public:
    GodotRenderManagerAutoRegister() {
        TVPRegisterRenderManager("godot_native", CreateGodotRenderManager);
        TVPRegisterRenderManager("gpu_bridge", CreateGodotRenderManager);
        TVPRegisterRenderManager("debug_cpu", CreateGodotRenderManager);
    }
} godot_render_manager_auto_register;
} // namespace

void TVPForceRegisterGodotRenderManager() {}

void TVPSetGodotRenderManagerGpuFastPathEnabled(bool enabled) {
    g_gpu_fastpath_enabled.store(enabled, std::memory_order_relaxed);
}
