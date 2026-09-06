#include "GodotRenderManager.h"

#include "GodotGpuBridge.h"
#include "../LayerBitmapIntf.h"
#include "MsgIntf.h"
#include "tjsHashSearch.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <spdlog/spdlog.h>

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

bool CopyRect(uint8_t *dst, int dst_pitch, int dst_width, int dst_height,
              const uint8_t *src, int src_pitch, int bytes_per_pixel,
              const tTVPRect &rc) {
    if(dst == nullptr || src == nullptr || dst_pitch <= 0 || src_pitch <= 0 ||
       dst_width <= 0 || dst_height <= 0 || bytes_per_pixel <= 0 ||
       rc.right <= rc.left || rc.bottom <= rc.top) {
        return false;
    }

    const int left = std::max(0, rc.left);
    const int top = std::max(0, rc.top);
    const int right = std::min(dst_width, rc.right);
    const int bottom = std::min(dst_height, rc.bottom);
    if(left >= right || top >= bottom) {
        return true;
    }

    const size_t source_x = static_cast<size_t>(left - rc.left) * bytes_per_pixel;
    const size_t width_bytes = static_cast<size_t>(right - left) * bytes_per_pixel;
    if(source_x + width_bytes > static_cast<size_t>(src_pitch) ||
       static_cast<size_t>(right) * bytes_per_pixel >
           static_cast<size_t>(dst_pitch)) {
        return false;
    }
    for(int y = top; y < bottom; ++y) {
        const size_t source_y = static_cast<size_t>(y - rc.top) * src_pitch;
        const size_t destination_y = static_cast<size_t>(y) * dst_pitch;
        std::memcpy(dst + destination_y + static_cast<size_t>(left) * bytes_per_pixel,
                    src + source_y + source_x, width_bytes);
    }
    return true;
}

std::mutex g_method_stats_mutex;
std::unordered_map<std::string, uint64_t> g_method_stats;
std::atomic<uint64_t> g_texture_create_count{0};
std::atomic<uint64_t> g_texture_clone_count{0};
std::atomic<uint64_t> g_texture_resize_count{0};
std::atomic<uint64_t> g_software_fallback_count{0};
std::atomic<uint64_t> g_gpu_fastpath_count{0};
std::atomic<bool> g_gpu_fastpath_enabled{true};
std::unordered_map<std::string, uint64_t> g_gpu_method_stats;
std::unordered_map<std::string, uint64_t> g_copy_fallback_stats;

struct RenderTimingStats {
    uint64_t count = 0;
    uint64_t total_ns = 0;
    uint64_t max_ns = 0;
};

std::unordered_map<std::string, RenderTimingStats> g_render_timing_stats;
uint64_t g_upload_count = 0;
uint64_t g_upload_success_count = 0;
uint64_t g_upload_bytes = 0;
uint64_t g_upload_total_ns = 0;
uint64_t g_upload_max_ns = 0;

bool DetailedRenderStats() {
    static const bool enabled = []() {
        const char *value = std::getenv("AETHERKIRI_GODOT_RENDER_STATS");
        return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

bool RenderTimingEnabled() {
    static const bool enabled = []() {
        const char *value = std::getenv("AETHERKIRI_GODOT_RENDER_TIMING");
        if (value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0) {
            return true;
        }
        return DetailedRenderStats();
    }();
    return enabled;
}

void RecordRenderTiming(const std::string &name, uint64_t elapsed_ns) {
    if (!RenderTimingEnabled()) return;
    std::lock_guard<std::mutex> lock(g_method_stats_mutex);
    auto &stats = g_render_timing_stats[name];
    ++stats.count;
    stats.total_ns += elapsed_ns;
    stats.max_ns = std::max(stats.max_ns, elapsed_ns);
}

class ScopedRenderTiming final {
public:
    explicit ScopedRenderTiming(const std::string &name)
        : enabled_(RenderTimingEnabled()), name_(name), start_(
              enabled_ ? std::chrono::steady_clock::now()
                       : std::chrono::steady_clock::time_point()) {}

    ~ScopedRenderTiming() {
        if (!enabled_) return;
        const auto elapsed = std::chrono::steady_clock::now() - start_;
        RecordRenderTiming(
            name_, static_cast<uint64_t>(
                       std::chrono::duration_cast<std::chrono::nanoseconds>(
                           elapsed).count()));
    }

private:
    bool enabled_ = false;
    std::string name_;
    std::chrono::steady_clock::time_point start_;
};

class ScopedUploadTiming final {
public:
    ScopedUploadTiming(bool active, size_t bytes)
        : enabled_(active && RenderTimingEnabled()), bytes_(bytes), start_(
              enabled_ ? std::chrono::steady_clock::now()
                       : std::chrono::steady_clock::time_point()) {}

    void Succeeded() { success_ = true; }

    ~ScopedUploadTiming() {
        if (!enabled_) return;
        const auto elapsed = std::chrono::steady_clock::now() - start_;
        const uint64_t elapsed_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
        std::lock_guard<std::mutex> lock(g_method_stats_mutex);
        ++g_upload_count;
        if (success_) ++g_upload_success_count;
        g_upload_bytes += static_cast<uint64_t>(bytes_);
        g_upload_total_ns += elapsed_ns;
        g_upload_max_ns = std::max(g_upload_max_ns, elapsed_ns);
    }

private:
    bool enabled_ = false;
    bool success_ = false;
    size_t bytes_ = 0;
    std::chrono::steady_clock::time_point start_;
};

void CountMethodFallback(iTVPRenderMethod *method) {
    g_software_fallback_count.fetch_add(1, std::memory_order_relaxed);
    if (!DetailedRenderStats()) return;
    std::lock_guard<std::mutex> lock(g_method_stats_mutex);
    const std::string name = method != nullptr ? method->GetName() : "(null)";
    g_method_stats[name] += 1;
}

void CountGpuFastPath(const std::string &name) {
    g_gpu_fastpath_count.fetch_add(1, std::memory_order_relaxed);
    if (!DetailedRenderStats()) return;
    std::lock_guard<std::mutex> lock(g_method_stats_mutex);
    g_gpu_method_stats[name] += 1;
}

void CountCopyFallbackReason(const std::string &reason) {
    if (!DetailedRenderStats()) return;
    std::lock_guard<std::mutex> lock(g_method_stats_mutex);
    g_copy_fallback_stats[reason] += 1;
}

void CountCopyFallbackReason(const char *reason) {
    if (!DetailedRenderStats()) return;
    std::lock_guard<std::mutex> lock(g_method_stats_mutex);
    g_copy_fallback_stats[reason != nullptr ? reason : "(null)"] += 1;
}

bool TraceGpuFallback() {
    static const bool enabled = []() {
        const char *value = std::getenv("AETHERKIRI_GODOT_GPU_TRACE_FALLBACK");
        return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

bool DeferredGodotGpuDrainEnabled() {
    if (TVPGodotGpuBridgeBatchActive()) return true;
    static const bool enabled = []() {
        const char *value = std::getenv("AETHERKIRI_GODOT_DEFER_GPU_DRAIN");
        if (value == nullptr || value[0] == '\0') {
            return TVP_GODOT_DEFER_GPU_DRAIN_DEFAULT;
        }
        return std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

bool IsGpuRectFastPathEnabled(const char *name) {
    if (!g_gpu_fastpath_enabled.load(std::memory_order_relaxed)) {
        return false;
    }
    const auto is_default_enabled = [&]() {
        return std::strcmp(name, "FillARGB") == 0 ||
               std::strcmp(name, "FillMask") == 0 ||
               std::strcmp(name, "Copy") == 0 ||
               std::strcmp(name, "CopyOpaqueImage") == 0 ||
               std::strcmp(name, "RemoveConstOpacity") == 0 ||
               std::strcmp(name, "AlphaBlend") == 0 ||
               std::strcmp(name, "AlphaBlend_a") == 0 ||
               std::strcmp(name, "AlphaBlend_d") == 0 ||
               std::strcmp(name, "ConstAlphaBlend_d") == 0 ||
               std::strcmp(name, "ConstAlphaBlend_SD") == 0 ||
               std::strcmp(name, "ConstAlphaBlend_SD_d") == 0 ||
               std::strcmp(name, "UnivTransBlend") == 0 ||
               std::strcmp(name, "UnivTransBlend_d") == 0 ||
               std::strcmp(name, "UnivTransBlend_a") == 0 ||
               std::strcmp(name, "CopyColor") == 0 ||
               std::strcmp(name, "AdditiveAlphaBlend") == 0 ||
               std::strcmp(name, "AdditiveAlphaBlend_a") == 0 ||
               std::strcmp(name, "PsAddBlend") == 0 ||
               std::strcmp(name, "PsSubBlend") == 0 ||
               std::strcmp(name, "PsScreenBlend") == 0 ||
               std::strcmp(name, "PsMulBlend") == 0 ||
               std::strcmp(name, "PsSoftLightBlend") == 0 ||
               std::strcmp(name, "BoxBlurAlpha") == 0 ||
               std::strcmp(name, "AlphaToAdditiveAlpha") == 0 ||
               std::strcmp(name, "AdditiveAlphaToAlpha") == 0;
    };
    static const std::string setting = []() {
        const char *value = std::getenv("AETHERKIRI_GODOT_GPU_RECT_FASTPATH");
        return value != nullptr ? std::string(value) : std::string();
    }();
    if (setting.empty()) return is_default_enabled();
    if (setting == "0" || setting == "off" || setting == "none") return false;
    if (setting == "1" || setting == "all" || setting == "default") {
        return is_default_enabled();
    }

    size_t start = 0;
    while (start < setting.size()) {
        const size_t end = setting.find_first_of(",;: ", start);
        const std::string token =
            setting.substr(start, end == std::string::npos ? end : end - start);
        if (token == name ||
            ((token == "default" || token == "1" || token == "all") &&
             is_default_enabled())) {
            return true;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

constexpr int kDefaultGpuRectMinArea = 32768;
constexpr int kDefaultAlphaRectMinArea = 32768;

int GpuRectMinArea() {
    static const int min_area = []() {
        const char *value = std::getenv("AETHERKIRI_GODOT_GPU_RECT_MIN_AREA");
        if (value == nullptr || value[0] == '\0') return kDefaultGpuRectMinArea;
        char *end = nullptr;
        long parsed = std::strtol(value, &end, 10);
        if (end == value || parsed < 0) return kDefaultGpuRectMinArea;
        return static_cast<int>(std::min<long>(parsed, 1 << 30));
    }();
    return min_area;
}

int GpuRectMinAreaForMethod(const char *name) {
    if (name != nullptr &&
        (std::strcmp(name, "AlphaBlend") == 0 ||
         std::strcmp(name, "AlphaBlend_a") == 0 ||
         std::strcmp(name, "AlphaBlend_d") == 0)) {
        static const int min_area = []() {
            const char *value =
                std::getenv("AETHERKIRI_GODOT_GPU_ALPHA_RECT_MIN_AREA");
            if (value == nullptr || value[0] == '\0') {
                return kDefaultAlphaRectMinArea;
            }
            char *end = nullptr;
            long parsed = std::strtol(value, &end, 10);
            if (end == value || parsed < 0) return kDefaultAlphaRectMinArea;
            return static_cast<int>(std::min<long>(parsed, 1 << 30));
        }();
        return min_area;
    }
    return GpuRectMinArea();
}

bool IsGpuRectLargeEnoughForMethod(const tTVPRect &rect, const char *name) {
    return rect.get_width() > 0 && rect.get_height() > 0 &&
           rect.get_width() * rect.get_height() >= GpuRectMinAreaForMethod(name);
}

bool ShouldUseGpuRectFastPath(const tTVPRect &rect, const char *name,
                              const GodotTexture2D *dst,
                              const GodotTexture2D *src = nullptr,
                              const GodotTexture2D *src2 = nullptr,
                              const GodotTexture2D *src3 = nullptr) {
    if (IsGpuRectLargeEnoughForMethod(rect, name)) return true;
    return (dst != nullptr && dst->HasPendingGpuWrites()) ||
           (src != nullptr && src->HasPendingGpuWrites()) ||
           (src2 != nullptr && src2->HasPendingGpuWrites()) ||
           (src3 != nullptr && src3->HasPendingGpuWrites()) ||
           (dst != nullptr && dst->RequiresGpuReadback()) ||
           (src != nullptr && src->RequiresGpuReadback()) ||
           (src2 != nullptr && src2->RequiresGpuReadback()) ||
           (src3 != nullptr && src3->RequiresGpuReadback());
}

bool IsOpaqueAlphaBlendCopyEnabled() {
    static const bool enabled = []() {
        const char *value = std::getenv("AETHERKIRI_GODOT_GPU_OPAQUE_COPY");
        return value == nullptr || value[0] == '\0' || std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

bool IsGpuCopyTrianglesEnabled() {
    static const bool enabled = []() {
        const char *value = std::getenv("AETHERKIRI_GODOT_GPU_COPY_TRIANGLES");
        return value == nullptr || value[0] == '\0' || std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

bool IsGpuBlendTrianglesEnabled() {
    static const bool enabled = []() {
        const char *value =
            std::getenv("AETHERKIRI_GODOT_GPU_BLEND_TRIANGLES");
        return value == nullptr || value[0] == '\0' ||
               std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

bool RectAbsSizeMatches(const tTVPRect &dst, const tTVPRect &src) {
    return dst.get_width() > 0 && dst.get_height() > 0 &&
           std::abs(src.get_width()) == dst.get_width() &&
           std::abs(src.get_height()) == dst.get_height();
}

bool RectNeedsAreaDownsample(const tTVPRect &dst, const tTVPRect &src) {
    return dst.get_width() > 0 && dst.get_height() > 0 &&
           (dst.get_width() < std::abs(src.get_width()) ||
            dst.get_height() < std::abs(src.get_height()));
}

bool RectNeedsAlphaAreaDownsample(const tTVPRect &dst, const tTVPRect &src,
                                  const GodotTexture2D *texture) {
    return texture != nullptr && texture->HasKnownTransparency() &&
           RectNeedsAreaDownsample(dst, src);
}

bool TrianglesNeedStrongAreaDownsample(uint32_t triangle_count,
                                       const tTVPPointD *dst_points,
                                       const tTVPPointD *src_points) {
    if (triangle_count == 0 || dst_points == nullptr || src_points == nullptr) {
        return false;
    }
    // Comparing source/destination axis-aligned bounds misclassifies a
    // rotated surface as downsampled whenever one rotated bound gets shorter.
    // That sent the common slanted E-mote presentation through the software
    // rasterizer and forced two full GPU readbacks per frame.  Compare the
    // corresponding triangle edges instead.  The GPU shader does
    // alpha-premultiplied bilinear sampling, so reserve the area-filtered CPU
    // path only for a genuinely strong (>2x along an edge) reduction.
    constexpr double kStrongDownsampleScale = 0.5;
    constexpr double kLengthEpsilon = 0.001;
    for(uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
        const uint32_t base = triangle * 3u;
        for(uint32_t edge = 0; edge < 3u; ++edge) {
            const uint32_t next = (edge + 1u) % 3u;
            const auto &src0 = src_points[base + edge];
            const auto &src1 = src_points[base + next];
            const auto &dst0 = dst_points[base + edge];
            const auto &dst1 = dst_points[base + next];
            const double srcDx = src1.x - src0.x;
            const double srcDy = src1.y - src0.y;
            const double dstDx = dst1.x - dst0.x;
            const double dstDy = dst1.y - dst0.y;
            const double srcLengthSquared = srcDx * srcDx + srcDy * srcDy;
            if(srcLengthSquared <= kLengthEpsilon * kLengthEpsilon) {
                continue;
            }
            const double dstLengthSquared = dstDx * dstDx + dstDy * dstDy;
            const double threshold =
                kStrongDownsampleScale * kStrongDownsampleScale *
                srcLengthSquared;
            if(dstLengthSquared + kLengthEpsilon * kLengthEpsilon <
               threshold) {
                return true;
            }
        }
    }
    return false;
}

bool TrianglesNeedAlphaAreaDownsample(uint32_t triangle_count,
                                      const tTVPPointD *dst_points,
                                      const tTVPPointD *src_points,
                                      const GodotTexture2D *texture) {
    return texture != nullptr && texture->HasKnownTransparency() &&
           TrianglesNeedStrongAreaDownsample(
               triangle_count, dst_points, src_points);
}

bool RectBoundsInsideTexture(const tTVPRect &rc, const GodotTexture2D *texture) {
    if (texture == nullptr) return false;
    const tjs_int left = std::min(rc.left, rc.right);
    const tjs_int right = std::max(rc.left, rc.right);
    const tjs_int top = std::min(rc.top, rc.bottom);
    const tjs_int bottom = std::max(rc.top, rc.bottom);
    return left >= 0 && top >= 0 &&
           right <= static_cast<tjs_int>(texture->GetWidth()) &&
           bottom <= static_cast<tjs_int>(texture->GetHeight()) &&
           left < right && top < bottom;
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
    const int id = delegate_ != nullptr ? delegate_->EnumParameterID(name) : -1;
    if(name != nullptr && std::strcmp(name, "phase") == 0) phase_id_ = id;
    if(name != nullptr && std::strcmp(name, "vague") == 0) vague_id_ = id;
    if(name != nullptr && std::strcmp(name, "area_left") == 0) area_left_id_ = id;
    if(name != nullptr && std::strcmp(name, "area_top") == 0) area_top_id_ = id;
    if(name != nullptr && std::strcmp(name, "area_right") == 0) area_right_id_ = id;
    if(name != nullptr && std::strcmp(name, "area_bottom") == 0) area_bottom_id_ = id;
    return id;
}
void GodotRenderMethod::SetParameterUInt(int id, unsigned int Value) {
    if (delegate_) delegate_->SetParameterUInt(id, Value);
}
void GodotRenderMethod::SetParameterInt(int id, int Value) {
    if(id == phase_id_) phase_ = Value;
    if(id == vague_id_) vague_ = Value;
    if(id == area_left_id_) area_left_ = Value;
    if(id == area_top_id_) area_top_ = Value;
    if(id == area_right_id_) area_right_ = Value;
    if(id == area_bottom_id_) area_bottom_ = Value;
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
                               unsigned int h, TVPTextureFormat::e format,
                               int create_flags)
    : iTVPTexture2D(static_cast<tjs_int>(w), static_cast<tjs_int>(h)),
      format_(format),
      pitch_(pitch > 0 ? pitch : static_cast<int>(w) * BytesPerPixel(format)),
      discard_unwritten_on_partial_update_(
          (create_flags & RENDER_CREATE_TEXTURE_FLAG_NO_COMPRESS) != 0) {
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
    cpu_pixels_known_zero_ = pixel == nullptr;
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
    if (format_ != TVPTextureFormat::RGBA) {
        MarkOpaqueKnown();
        return;
    }
    if (pixel == nullptr || Width <= 0 || Height <= 0) {
        MarkOpacityUnknown();
        return;
    }
    const int stride = pitch > 0 ? pitch : pitch_;
    const auto *bytes = static_cast<const uint8_t *>(pixel);
    for (int y = 0; y < Height; ++y) {
        const uint8_t *row = bytes + static_cast<size_t>(y) * stride;
        for (int x = 0; x < Width; ++x) {
            if (row[x * 4 + 3] != 0xff) {
                MarkTransparentKnown();
                return;
            }
        }
    }
    MarkOpaqueKnown();
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
    const auto *bridge = TVPGodotGpuBridgeGet();
    if (bridge == nullptr || bridge->create_rgba == nullptr) return;
    const void *src = pixel != nullptr
        ? pixel
        : ((format_ == TVPTextureFormat::RGBA && cpu_pixels_known_zero_) ||
                   pixels_.empty()
               ? nullptr
               : pixels_.data());
    uint32_t stride = static_cast<uint32_t>(
        pixel != nullptr && pitch > 0 ? pitch : pitch_);
    std::vector<uint32_t> expanded_gray;
    if(format_ == TVPTextureFormat::Gray) {
        if(src == nullptr) return;
        expanded_gray.resize(static_cast<size_t>(Width) * Height);
        const auto *gray = static_cast<const uint8_t *>(src);
        for(int y = 0; y < Height; ++y) {
            for(int x = 0; x < Width; ++x) {
                const uint32_t value = gray[static_cast<size_t>(y) * stride + x];
                expanded_gray[static_cast<size_t>(y) * Width + x] =
                    value | (value << 8) | (value << 16) | 0xff000000u;
            }
        }
        src = expanded_gray.data();
        stride = static_cast<uint32_t>(Width) * 4u;
    } else if(format_ != TVPTextureFormat::RGBA) {
        return;
    }
    gpu_handle_ = bridge->create_rgba(static_cast<uint32_t>(Width),
                                      static_cast<uint32_t>(Height),
                                      src, stride);
    if (gpu_handle_ == 0) {
        return;
    }
    gpu_dirty_ = false;
    cpu_dirty_ = false;
    cpu_pixels_known_zero_ = false;
    if(format_ == TVPTextureFormat::RGBA && !retain_cpu_shadow_)
        DiscardCpuStorage();
}

bool GodotTexture2D::EnsureGpuHandle() {
    if (gpu_handle_ == 0) {
        CreateGpuHandle(nullptr, 0);
    } else if (cpu_dirty_) {
        ScopedUploadTiming upload_timing(
            true, static_cast<size_t>(Width) * static_cast<size_t>(Height) * 4u);
        const auto *bridge = TVPGodotGpuBridgeGet();
        if (bridge == nullptr || bridge->update_rgba == nullptr ||
            pixels_.empty()) {
            return false;
        }
        const void *upload_pixels = pixels_.data();
        uint32_t upload_pitch = static_cast<uint32_t>(pitch_);
        std::vector<uint32_t> expanded_gray;
        if(format_ == TVPTextureFormat::Gray) {
            expanded_gray.resize(static_cast<size_t>(Width) * Height);
            for(int y = 0; y < Height; ++y) {
                const auto *source = pixels_.data() +
                    static_cast<size_t>(y) * pitch_;
                auto *destination = expanded_gray.data() +
                    static_cast<size_t>(y) * Width;
                for(int x = 0; x < Width; ++x) {
                    const uint32_t value = source[x];
                    destination[x] =
                        value | (value << 8) | (value << 16) | 0xff000000u;
                }
            }
            upload_pixels = expanded_gray.data();
            upload_pitch = static_cast<uint32_t>(Width) * 4u;
        } else if(format_ != TVPTextureFormat::RGBA) {
            return false;
        }
        const tTVPRect full_rect(0, 0, Width, Height);
        if (!bridge->update_rgba(gpu_handle_, upload_pixels,
                                 upload_pitch, &full_rect)) {
            return false;
        }
        gpu_dirty_ = false;
        cpu_dirty_ = false;
        if(!retain_cpu_shadow_) DiscardCpuStorage();
        upload_timing.Succeeded();
    }
    return gpu_handle_ != 0;
}

uint64_t GodotTexture2D::BeginGpuReadback() const {
    const auto *bridge = TVPGodotGpuBridgeGet();
    if(gpu_handle_ == 0 || bridge == nullptr ||
       bridge->begin_read_rgba == nullptr) {
        return 0;
    }
    return bridge->begin_read_rgba(gpu_handle_);
}

bool GodotTexture2D::PollGpuReadback(
    uint64_t request, void *out_pixels, size_t out_pixels_size,
    uint32_t stride_bytes, bool *ready) const {
    const auto *bridge = TVPGodotGpuBridgeGet();
    if(request == 0 || bridge == nullptr ||
       bridge->poll_read_rgba == nullptr) {
        if(ready) *ready = false;
        return false;
    }
    return bridge->poll_read_rgba(
        request, out_pixels, out_pixels_size, stride_bytes, ready);
}

void GodotTexture2D::DiscardGpuReadback(uint64_t request) const {
    const auto *bridge = TVPGodotGpuBridgeGet();
    if(request != 0 && bridge != nullptr &&
       bridge->discard_read_rgba != nullptr) {
        bridge->discard_read_rgba(request);
    }
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
    ScopedRenderTiming cpu_read_timing("EnsureCpuReadable");
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
    if (format_ == TVPTextureFormat::Gray && bridge != nullptr &&
        bridge->read_rgba != nullptr) {
        const uint32_t rgba_pitch = static_cast<uint32_t>(Width) * 4u;
        std::vector<uint8_t> rgba(static_cast<size_t>(rgba_pitch) * Height);
        if (bridge->read_rgba(gpu_handle_, rgba.data(), rgba.size(),
                              rgba_pitch)) {
            for (int y = 0; y < Height; ++y) {
                const auto *source =
                    rgba.data() + static_cast<size_t>(y) * rgba_pitch;
                auto *destination =
                    pixels_.data() + static_cast<size_t>(y) * pitch_;
                for (int x = 0; x < Width; ++x) {
                    destination[x] = source[static_cast<size_t>(x) * 4];
                }
            }
            gpu_dirty_ = false;
            cpu_pixels_known_zero_ = false;
        }
        return;
    }
    if (bridge != nullptr && bridge->read_rgba != nullptr &&
        bridge->read_rgba(gpu_handle_, pixels_.data(), pixels_.size(),
                          static_cast<uint32_t>(pitch_))) {
        gpu_dirty_ = false;
        cpu_pixels_known_zero_ = false;
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
    retain_cpu_shadow_ = true;
    MarkOpacityUnknown();
    MarkCpuDirty();
    return pixels_.data() + static_cast<size_t>(l) * pitch_;
}

void *GodotTexture2D::GetScanLineForWriteUninitialized(tjs_uint l) {
    if (l >= static_cast<tjs_uint>(Height)) return nullptr;
    EnsureCpuStorage();
    if (pixels_.empty()) return nullptr;
    retain_cpu_shadow_ = true;
    MarkOpacityUnknown();
    MarkCpuDirty();
    return pixels_.data() + static_cast<size_t>(l) * pitch_;
}

void GodotTexture2D::Update(const void *pixel, TVPTextureFormat::e format,
                            int pitch, const tTVPRect &rc) {
    if (pixel == nullptr) return;
    const int new_bpp = BytesPerPixel(format);
    if(format != format_) {
        ReleaseGpuHandle();
        format_ = format;
        pitch_ = Width * new_bpp;
        pixels_.assign(static_cast<size_t>(pitch_) * Height, 0);
    }
    const bool full_rect = IsFullTextureRect(rc, Width, Height);
    const bool replace_transient_scratch =
        !full_rect && discard_unwritten_on_partial_update_ &&
        rc.left == 0 && rc.top == 0;
    if (!full_rect && !replace_transient_scratch) {
        EnsureCpuReadable();
    } else {
        if (replace_transient_scratch) {
            pixels_.clear();
        }
        EnsureCpuStorage();
    }
    const int src_pitch = pitch > 0 ? pitch : rc.get_width() * new_bpp;
    const bool copied = CopyRect(pixels_.data(), pitch_, Width, Height,
                                 static_cast<const uint8_t *>(pixel), src_pitch,
                                 new_bpp, rc);
    if (!copied) return;
    if (full_rect) {
        SetOpacityFromPixels(pixels_.data(), pitch_);
    } else {
        MarkOpacityUnknown();
    }
    MarkCpuDirty();
}

uint32_t GodotTexture2D::GetPoint(int x, int y) {
    if (x < 0 || y < 0 || x >= Width || y >= Height ||
        (format_ != TVPTextureFormat::Gray &&
         format_ != TVPTextureFormat::RGBA)) {
        return 0;
    }
    EnsureCpuReadable();
    if (pixels_.empty()) return 0;
    const size_t offset = static_cast<size_t>(y) * pitch_;
    if (format_ == TVPTextureFormat::Gray) {
        return pixels_[offset + static_cast<size_t>(x)];
    }
    uint32_t value = 0;
    std::memcpy(&value, pixels_.data() + offset + static_cast<size_t>(x) * 4,
                4);
    return value;
}

void GodotTexture2D::SetPoint(int x, int y, uint32_t clr) {
    if (x < 0 || y < 0 || x >= Width || y >= Height ||
        (format_ != TVPTextureFormat::Gray &&
         format_ != TVPTextureFormat::RGBA)) {
        return;
    }
    EnsureCpuReadable();
    if (pixels_.empty()) return;
    const size_t offset = static_cast<size_t>(y) * pitch_;
    if (format_ == TVPTextureFormat::Gray) {
        pixels_[offset + static_cast<size_t>(x)] = static_cast<uint8_t>(clr);
    } else {
        std::memcpy(pixels_.data() + offset + static_cast<size_t>(x) * 4,
                    &clr, 4);
        MarkOpacityUnknown();
    }
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
    cpu_pixels_known_zero_ = true;
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

bool GodotTexture2D::BlendTrianglesGpuFrom(
    GodotTexture2D *src, uint32_t triangle_count, const tTVPRect &clip_rc,
    const tTVPPointD *dst_points, const tTVPPointD *src_points, uint32_t mode,
    int opacity) {
    if (mode != TVP_GODOT_GPU_BLEND_ALPHA &&
        mode != TVP_GODOT_GPU_BLEND_ALPHA_D &&
        mode != TVP_GODOT_GPU_BLEND_COPY_COLOR &&
        mode != TVP_GODOT_GPU_BLEND_ALPHA_BLEND_A &&
        mode != TVP_GODOT_GPU_BLEND_CONST_ALPHA_D &&
        mode != TVP_GODOT_GPU_BLEND_PS_SCREEN &&
        mode != TVP_GODOT_GPU_BLEND_PS_MULTIPLY &&
        mode != TVP_GODOT_GPU_BLEND_PS_ADD &&
        mode != TVP_GODOT_GPU_BLEND_PS_SUBTRACT) {
        return false;
    }
    return DrawTrianglesGpuFrom(
        src, triangle_count, clip_rc, dst_points, src_points, opacity,
        TVP_GODOT_GPU_TRIANGLE_TVP_BLEND | mode);
}

bool GodotTexture2D::DrawTrianglesGpuFrom(GodotTexture2D *src,
                                          uint32_t triangle_count,
                                          const tTVPRect &clip_rc,
                                          const tTVPPointD *dst_points,
                                          const tTVPPointD *src_points,
                                          int opacity,
                                          uint32_t blend_mode) {
    if(src == nullptr || src == this || triangle_count == 0 ||
       dst_points == nullptr || src_points == nullptr || gpu_handle_ == 0 ||
       src->gpu_handle_ == 0) {
        return false;
    }
    const auto *bridge = TVPGodotGpuBridgeGet();
    if(bridge == nullptr || bridge->draw_triangles == nullptr) return false;
    const float normalizedOpacity =
        static_cast<float>(std::clamp(opacity, 0, 255)) / 255.0f;
    if(!bridge->draw_triangles(gpu_handle_, src->gpu_handle_, triangle_count,
                               &clip_rc, dst_points, src_points,
                               normalizedOpacity, blend_mode)) {
        return false;
    }
    gpu_dirty_ = true;
    cpu_dirty_ = false;
    MarkOpacityUnknown();
    return true;
}

bool GodotTexture2D::DrawExternalTrianglesGpuFrom(
    uint64_t source_gpu_handle, uint32_t triangle_count,
    const tTVPRect &clip_rc, const tTVPPointD *dst_points,
    const tTVPPointD *src_points, int opacity, uint32_t blend_mode) {
    if(source_gpu_handle == 0 || source_gpu_handle == gpu_handle_ ||
       triangle_count == 0 || dst_points == nullptr ||
       src_points == nullptr || gpu_handle_ == 0) {
        return false;
    }
    const auto *bridge = TVPGodotGpuBridgeGet();
    if(bridge == nullptr || bridge->draw_triangles == nullptr) return false;
    const float normalizedOpacity =
        static_cast<float>(std::clamp(opacity, 0, 255)) / 255.0f;
    if(!bridge->draw_triangles(
           gpu_handle_, source_gpu_handle, triangle_count, &clip_rc,
           dst_points, src_points, normalizedOpacity, blend_mode)) {
        return false;
    }
    gpu_dirty_ = true;
    cpu_dirty_ = false;
    MarkOpacityUnknown();
    return true;
}

bool GodotTexture2D::DrawMaskedTrianglesGpuFrom(
    GodotTexture2D *src, GodotTexture2D *mask,
    uint32_t triangle_count, const tTVPRect &clip_rc,
    const tTVPPointD *dst_points, const tTVPPointD *src_points,
    const tTVPPointD *mask_points, int opacity, uint32_t blend_mode,
    bool use_mask_alpha) {
    if(src == nullptr || mask == nullptr || src == this || mask == this ||
       triangle_count == 0 || dst_points == nullptr || src_points == nullptr ||
       mask_points == nullptr || gpu_handle_ == 0 || src->gpu_handle_ == 0 ||
       mask->gpu_handle_ == 0) {
        return false;
    }
    const auto *bridge = TVPGodotGpuBridgeGet();
    if(bridge == nullptr || bridge->draw_masked_triangles == nullptr) {
        return false;
    }
    const float normalizedOpacity =
        static_cast<float>(std::clamp(opacity, 0, 255)) / 255.0f;
    // The bridge flag is named inverted_mask for its original Cubism caller.
    // Its shader starts from (1 - mask alpha), so true selects ordinary
    // alpha-mask semantics and false selects the inverse mask.
    if(!bridge->draw_masked_triangles(
           gpu_handle_, src->gpu_handle_, mask->gpu_handle_, triangle_count,
           &clip_rc, dst_points, src_points, mask_points, normalizedOpacity,
           blend_mode, use_mask_alpha)) {
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
    if (src == this &&
        mode != TVP_GODOT_GPU_BLEND_REMOVE_CONST_OPACITY &&
        mode != TVP_GODOT_GPU_BLEND_FILL_MASK &&
        mode != TVP_GODOT_GPU_BLEND_BOX_BLUR_ALPHA &&
        mode != TVP_GODOT_GPU_BLEND_ALPHA_TO_ADDITIVE_ALPHA &&
        mode != TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA_TO_ALPHA) {
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

bool GodotTexture2D::BlendGpuFrom3(
    GodotTexture2D *src1, GodotTexture2D *src2, GodotTexture2D *src3,
    const tTVPRect &dst_rc, const tTVPRect &src1_rc,
    const tTVPRect &src2_rc, const tTVPRect &src3_rc, uint32_t mode,
    int opacity, uint32_t color) {
    if(src1 == nullptr || src2 == nullptr || src3 == nullptr ||
       gpu_handle_ == 0 || src1->gpu_handle_ == 0 ||
       src2->gpu_handle_ == 0 || src3->gpu_handle_ == 0) {
        return false;
    }
    const auto *bridge = TVPGodotGpuBridgeGet();
    if(bridge == nullptr || bridge->blend_rect3 == nullptr) return false;
    if(!bridge->blend_rect3(
           gpu_handle_, src1->gpu_handle_, src2->gpu_handle_,
           src3->gpu_handle_, &dst_rc, &src1_rc, &src2_rc, &src3_rc, mode,
           opacity, color)) {
        return false;
    }
    gpu_dirty_ = true;
    cpu_dirty_ = false;
    MarkOpacityUnknown();
    return true;
}

bool GodotTexture2D::UploadCpuToGpu(bool flush_pending_gpu_writes) {
    ScopedUploadTiming upload_timing(
        cpu_dirty_, static_cast<size_t>(Width) * static_cast<size_t>(Height) * 4u);
    if (!cpu_dirty_) {
        if (gpu_dirty_ && flush_pending_gpu_writes) {
            const auto *bridge = TVPGodotGpuBridgeGet();
            if (bridge != nullptr && bridge->flush != nullptr) {
                return bridge->flush();
            }
        }
        return true;
    }
    if (format_ != TVPTextureFormat::RGBA || pixels_.empty()) {
        return false;
    }
    const auto *bridge = TVPGodotGpuBridgeGet();
    if (bridge == nullptr) return false;
    if (gpu_handle_ == 0) {
        CreateGpuHandle(pixels_.data(), pitch_);
        if (gpu_handle_ != 0) upload_timing.Succeeded();
        return gpu_handle_ != 0;
    }
    if (bridge->update_rgba == nullptr) return false;
    const tTVPRect full_rect(0, 0, Width, Height);
    if (!bridge->update_rgba(gpu_handle_, pixels_.data(),
                             static_cast<uint32_t>(pitch_), &full_rect)) {
        return false;
    }
    gpu_dirty_ = false;
    cpu_dirty_ = false;
    if(!retain_cpu_shadow_) DiscardCpuStorage();
    upload_timing.Succeeded();
    return true;
}

bool GodotTexture2D::UpdateGpuRgba(const void *pixels,
                                   uint32_t stride_bytes) {
    if(format_ != TVPTextureFormat::RGBA || pixels == nullptr ||
       stride_bytes < static_cast<uint32_t>(Width) * 4u) {
        return false;
    }
    const auto *bridge = TVPGodotGpuBridgeGet();
    if(bridge == nullptr) return false;
    if(gpu_handle_ == 0) {
        CreateGpuHandle(pixels, static_cast<int>(stride_bytes));
        if(gpu_handle_ == 0) return false;
    } else {
        if(bridge->update_rgba == nullptr) return false;
        const tTVPRect full_rect(0, 0, Width, Height);
        if(!bridge->update_rgba(gpu_handle_, pixels, stride_bytes,
                                &full_rect)) {
            return false;
        }
    }
    gpu_dirty_ = true;
    cpu_dirty_ = false;
    DiscardCpuStorage();
    MarkOpacityUnknown();
    return true;
}

iTVPTexture2D *GodotRenderManager::CreateTexture2D(const void *pixel, int pitch,
                                                   unsigned int w,
                                                   unsigned int h,
                                                   TVPTextureFormat::e format,
                                                   int flags) {
    auto *texture = new GodotTexture2D(pixel, pitch, w, h, format, flags);
    vmem_size_ += static_cast<uint64_t>(texture->GetPitch()) * h;
    g_texture_create_count.fetch_add(1, std::memory_order_relaxed);
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
    g_texture_clone_count.fetch_add(1, std::memory_order_relaxed);
    if (tex == nullptr || tex->GetWidth() != static_cast<tjs_int>(neww) ||
        tex->GetHeight() != static_cast<tjs_int>(newh)) {
        g_texture_resize_count.fetch_add(1, std::memory_order_relaxed);
    }
    auto *ret = new GodotTexture2D(nullptr, 0, neww, newh,
                                  tex != nullptr ? tex->GetFormat()
                                                 : TVPTextureFormat::RGBA);
    if (tex != nullptr) {
        const tTVPRect copy_rc(0, 0,
                               std::min<tjs_int>(neww, tex->GetWidth()),
                               std::min<tjs_int>(newh, tex->GetHeight()));
        if (!copy_rc.is_empty()) {
            // Copy-on-write and grow-only motion scratch layers arrive here
            // while their newest pixels still live exclusively on the GPU.
            // Reading scan line zero would synchronously download the entire
            // Metal texture before uploading it into the replacement texture.
            // Keep that clone on the ordered GPU queue whenever both textures
            // use this backend.
            auto *godot_src = dynamic_cast<GodotTexture2D *>(tex);
            if (godot_src != nullptr &&
                ret->EnsureGpuHandle() && godot_src->EnsureGpuHandle() &&
                godot_src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
                ret->CopyGpuFrom(godot_src, copy_rc, copy_rc)) {
                return ret;
            }
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

int GodotRenderManager::EnumParameterID(const char *name) {
    const int id = SoftwareDelegate()->EnumParameterID(name);
    if(name != nullptr && std::strcmp(name, "StretchType") == 0)
        stretch_parameter_id_ = id;
    return id;
}

void GodotRenderManager::SetParameterUInt(int id, unsigned int Value) {
    SoftwareDelegate()->SetParameterUInt(id, Value);
}

void GodotRenderManager::SetParameterInt(int id, int Value) {
    if(id >= 0 && id == stretch_parameter_id_)
        stretch_type_ = Value;
    SoftwareDelegate()->SetParameterInt(id, Value);
}

void GodotRenderManager::SetParameterPtr(int id, const void *Value) {
    SoftwareDelegate()->SetParameterPtr(id, Value);
}

void GodotRenderManager::SetParameterFloat(int id, float Value) {
    SoftwareDelegate()->SetParameterFloat(id, Value);
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
    ScopedRenderTiming render_timing(method_name.empty() ? "(null)" : method_name);

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
    auto *src3_1 = textures.size() == 3
        ? dynamic_cast<GodotTexture2D *>(textures[0].first)
        : nullptr;
    auto *src3_2 = textures.size() == 3
        ? dynamic_cast<GodotTexture2D *>(textures[1].first)
        : nullptr;
    auto *src3_3 = textures.size() == 3
        ? dynamic_cast<GodotTexture2D *>(textures[2].first)
        : nullptr;
    const bool nearest_scaled = textures.size() == 1 &&
        !RectAbsSizeMatches(rctar, textures[0].second) &&
        (stretch_type_ & stTypeMask) == stNearest;

    // The layer-manager draw buffer is consumed through the CPU-visible
    // bitmap interface after each completion.  GPU rect operations on this
    // target are especially unsafe when a source aliases the destination:
    // Metal/Vulkan do not define a read-only image and writable storage image
    // bound to the same resource.  Keep this one semantic boundary on the
    // software renderer; regular layer textures retain their GPU fast paths.
    if (dst != nullptr && dst->IsCpuCompositeTarget()) {
        if (method_name == "Copy") {
            CountCopyFallbackReason("cpu_composite_target");
        }
        CountMethodFallback(method);
        SoftwareDelegate()->OperateRect(delegate_method, tar, reftar, rctar,
                                        textures);
        dst->MarkCpuDirty();
        return;
    }

    if(const char *trace = std::getenv("AETHERKIRI_RECT_TRACE");
       trace && *trace && *trace != '0' && rctar.get_width() >= 1000 &&
       rctar.get_height() >= 400) {
        spdlog::info("rect-op method={} target={} src_count={} rect=({},{} {}x{})",
                     method_name, static_cast<const void *>(tar), textures.size(),
                     rctar.left, rctar.top, rctar.get_width(),
                     rctar.get_height());
    }

    if (RenderTimingEnabled() && method_name == "BoxBlurAlpha") {
        std::fprintf(
            stderr,
            "godot_box_blur rect=(%d,%d,%d,%d) src_rect=(%d,%d,%d,%d) "
            "dst=%dx%d src=%dx%d dst_gpu=%llu src_gpu=%llu "
            "dst_pending=%d src_pending=%d area=(%d,%d,%d,%d)\n",
            rctar.left, rctar.top, rctar.right, rctar.bottom,
            textures.size() == 1 ? textures[0].second.left : 0,
            textures.size() == 1 ? textures[0].second.top : 0,
            textures.size() == 1 ? textures[0].second.right : 0,
            textures.size() == 1 ? textures[0].second.bottom : 0,
            dst != nullptr ? dst->GetWidth() : 0,
            dst != nullptr ? dst->GetHeight() : 0,
            src != nullptr ? src->GetWidth() : 0,
            src != nullptr ? src->GetHeight() : 0,
            static_cast<unsigned long long>(dst != nullptr
                                                ? dst->GetGodotGpuHandle()
                                                : 0),
            static_cast<unsigned long long>(src != nullptr
                                                ? src->GetGodotGpuHandle()
                                                : 0),
            dst != nullptr && dst->RequiresGpuReadback() ? 1 : 0,
            src != nullptr && src->RequiresGpuReadback() ? 1 : 0,
            godot_method != nullptr ? godot_method->AreaLeft() : 0,
            godot_method != nullptr ? godot_method->AreaTop() : 0,
            godot_method != nullptr ? godot_method->AreaRight() : 0,
            godot_method != nullptr ? godot_method->AreaBottom() : 0);
    }

    // Alpha representation changes are self-operations.  The old delegate
    // path reads the whole texture back to the CPU, converts every pixel,
    // then uploads it again.  Conversion is per-pixel and alias-safe, so keep
    // the texture on the ordered GPU queue instead.  GetTextureForRender()
    // has already detached a copy-on-write target when the bitmap was shared;
    // UploadCpuToGpu() only does work for the rare CPU-created fallback copy.
    if ((method_name == "AlphaToAdditiveAlpha" ||
         method_name == "AdditiveAlphaToAlpha") &&
        textures.size() == 0 && dst != nullptr &&
        IsGpuRectFastPathEnabled(method_name.c_str()) &&
        RectBoundsInsideTexture(rctar, dst) &&
        dst->EnsureGpuHandle() &&
        dst->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendGpuFrom(
            dst, rctar, rctar,
            method_name == "AlphaToAdditiveAlpha"
                ? TVP_GODOT_GPU_BLEND_ALPHA_TO_ADDITIVE_ALPHA
                : TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA_TO_ALPHA,
            255, 0)) {
        CountGpuFastPath(method_name);
        return;
    }

    // BoxBlurAlpha used to fall through to the software delegate. On a
    // GPU-backed E-mote texture that first performs a synchronous full-frame
    // readback before OpenCV runs, which is the source of the 200ms spikes.
    // The game uses symmetric blur areas; dispatch those through the existing
    // queued blend ABI so the source stays GPU-resident. Alias cases are
    // handled by the bridge with a temporary source texture because sampling
    // and writing one image in the same compute dispatch is undefined.
    if (method_name == "BoxBlurAlpha" && godot_method != nullptr &&
        dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("BoxBlurAlpha") &&
        godot_method->AreaLeft() == -godot_method->AreaRight() &&
        godot_method->AreaTop() == -godot_method->AreaBottom() &&
        godot_method->AreaLeft() <= 0 && godot_method->AreaTop() <= 0 &&
        RectAbsSizeMatches(rctar, textures[0].second) &&
        RectBoundsInsideTexture(textures[0].second, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendGpuFrom(
            src, rctar, textures[0].second,
            TVP_GODOT_GPU_BLEND_BOX_BLUR_ALPHA,
            std::abs(godot_method->AreaLeft()),
            static_cast<uint32_t>(std::abs(godot_method->AreaTop())))) {
        CountGpuFastPath(method_name);
        return;
    }
    if (method_name == "Copy" && dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("Copy") &&
        ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled())) {
        const tTVPRect &src_rc = textures[0].second;
        if (!RectBoundsInsideTexture(src_rc, src)) {
            CountCopyFallbackReason("copy_src_out_of_bounds");
            CountMethodFallback(method);
            SoftwareDelegate()->OperateRect(delegate_method, tar, reftar, rctar, textures);
            if (dst != nullptr) {
                dst->MarkCpuDirty();
            }
            return;
        }
        if (src_rc.get_width() == rctar.get_width() &&
            src_rc.get_height() == rctar.get_height() &&
            dst->CopyGpuFrom(src, rctar, src_rc)) {
            CountGpuFastPath(method_name);
            return;
        }
        if(nearest_scaled) {
            CountCopyFallbackReason("scaled_copy_nearest");
            CountMethodFallback(method);
            SoftwareDelegate()->OperateRect(delegate_method, tar, reftar,
                                            rctar, textures);
            dst->MarkCpuDirty();
            return;
        }
        const bool needs_area_downsample =
            RectNeedsAlphaAreaDownsample(rctar, src_rc, src);
        if (!needs_area_downsample && !src_rc.is_empty() &&
            IsGpuCopyTrianglesEnabled()) {
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
            CountCopyFallbackReason(needs_area_downsample
                                        ? "scaled_copy_area_downsample"
                                        : IsGpuCopyTrianglesEnabled()
                                              ? "scaled_copy_bridge_failed"
                                              : "scaled_copy_cpu");
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
        ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst, src) &&
        RectAbsSizeMatches(rctar, textures[0].second) &&
        RectBoundsInsideTexture(textures[0].second, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendGpuFrom(src, rctar, textures[0].second,
                          TVP_GODOT_GPU_BLEND_COPY_COLOR, 255, 0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (method_name == "CopyOpaqueImage" && dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("CopyOpaqueImage") &&
        ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst, src) &&
        RectBoundsInsideTexture(textures[0].second, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendGpuFrom(src, rctar, textures[0].second,
                          TVP_GODOT_GPU_BLEND_COPY_OPAQUE, 255, 0)) {
        CountGpuFastPath(method_name);
        return;
    }

    // Color-map operations are frequently emitted as thousands of tiny
    // rectangles while a KAG character/expression is replaced.  Each GPU
    // dispatch allocates a bridge op, descriptor set, and barrier; on mobile
    // and MoltenVK that CPU submission cost dominates the actual color-map
    // shader and creates a single 100ms+ timer tick.  Keep this path opt-in
    // through AETHERKIRI_GODOT_GPU_RECT_FASTPATH=ApplyColorMap_a.  The normal
    // software delegate is safe here because these source layers are CPU
    // backed before the operation and avoids the per-rectangle GPU roundtrip.
    if (method_name == "ApplyColorMap_a" && dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("ApplyColorMap_a") &&
        RectAbsSizeMatches(rctar, textures[0].second) &&
        RectBoundsInsideTexture(textures[0].second, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendGpuFrom(
            src, rctar, textures[0].second,
            TVP_GODOT_GPU_BLEND_APPLY_COLOR_MAP_A,
            godot_method != nullptr ? godot_method->Opacity() : 255,
            godot_method != nullptr ? godot_method->Color() : 0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if ((method_name == "AdditiveAlphaBlend" ||
         method_name == "AdditiveAlphaBlend_a") &&
        dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled(method_name.c_str()) &&
        RectAbsSizeMatches(rctar, textures[0].second) &&
        RectBoundsInsideTexture(textures[0].second, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendGpuFrom(
            src, rctar, textures[0].second,
            method_name == "AdditiveAlphaBlend_a"
                ? TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA_A
                : TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA,
            godot_method != nullptr ? godot_method->Opacity() : 255, 0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (method_name == "AlphaBlend" && dst != nullptr && src != nullptr &&
        !nearest_scaled &&
        IsGpuRectFastPathEnabled("AlphaBlend") &&
        ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst, src) &&
        RectBoundsInsideTexture(textures[0].second, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendGpuFrom(src, rctar, textures[0].second,
                          TVP_GODOT_GPU_BLEND_ALPHA,
                          godot_method != nullptr ? godot_method->Opacity() : 255,
                          0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (method_name == "AlphaBlend_d" && dst != nullptr && src != nullptr &&
        !nearest_scaled &&
        IsGpuRectFastPathEnabled("AlphaBlend_d") &&
        ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst, src) &&
        RectBoundsInsideTexture(textures[0].second, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendGpuFrom(src, rctar, textures[0].second,
                          TVP_GODOT_GPU_BLEND_ALPHA_D,
                          godot_method != nullptr ? godot_method->Opacity() : 255,
                          0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (method_name == "ConstAlphaBlend_d" && dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("ConstAlphaBlend_d") &&
        ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst, src) &&
        RectAbsSizeMatches(rctar, textures[0].second) &&
        RectBoundsInsideTexture(textures[0].second, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendGpuFrom(src, rctar, textures[0].second,
                          TVP_GODOT_GPU_BLEND_CONST_ALPHA_D,
                          godot_method != nullptr ? godot_method->Opacity() : 255,
                          0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (method_name == "PsScreenBlend" && dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("PsScreenBlend") &&
        ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst, src) &&
        RectAbsSizeMatches(rctar, textures[0].second) &&
        RectBoundsInsideTexture(textures[0].second, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendGpuFrom(src, rctar, textures[0].second,
                          TVP_GODOT_GPU_BLEND_PS_SCREEN,
                          godot_method != nullptr ? godot_method->Opacity() : 255,
                          0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (method_name == "PsAddBlend" && dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("PsAddBlend") &&
        ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst, src) &&
        RectAbsSizeMatches(rctar, textures[0].second) &&
        RectBoundsInsideTexture(textures[0].second, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendGpuFrom(src, rctar, textures[0].second,
                          TVP_GODOT_GPU_BLEND_PS_ADD,
                          godot_method != nullptr ? godot_method->Opacity() : 255,
                          0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (method_name == "PsSoftLightBlend" && dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("PsSoftLightBlend") &&
        ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst, src) &&
        RectBoundsInsideTexture(textures[0].second, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendGpuFrom(src, rctar, textures[0].second,
                          TVP_GODOT_GPU_BLEND_PS_SOFT_LIGHT,
                          godot_method != nullptr ? godot_method->Opacity() : 255,
                          0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (method_name == "PsSubBlend" && dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("PsSubBlend") &&
        ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst, src) &&
        RectAbsSizeMatches(rctar, textures[0].second) &&
        RectBoundsInsideTexture(textures[0].second, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendGpuFrom(src, rctar, textures[0].second,
                          TVP_GODOT_GPU_BLEND_PS_SUBTRACT,
                          godot_method != nullptr ? godot_method->Opacity() : 255,
                          0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (method_name == "PsMulBlend" && dst != nullptr && src != nullptr &&
        IsGpuRectFastPathEnabled("PsMulBlend") &&
        ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst, src) &&
        RectAbsSizeMatches(rctar, textures[0].second) &&
        RectBoundsInsideTexture(textures[0].second, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendGpuFrom(src, rctar, textures[0].second,
                          TVP_GODOT_GPU_BLEND_PS_MULTIPLY,
                          godot_method != nullptr ? godot_method->Opacity() : 255,
                          0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if ((method_name == "AlphaBlend_a" ||
        method_name == "PerspectiveAlphaBlend_a") &&
        dst != nullptr && src != nullptr && !nearest_scaled &&
        IsGpuRectFastPathEnabled("AlphaBlend_a") &&
        ShouldUseGpuRectFastPath(rctar, "AlphaBlend_a", dst, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled())) {
        const tTVPRect &src_rc = textures[0].second;
        const int opacity = godot_method != nullptr ? godot_method->Opacity() : 255;
        if (!RectBoundsInsideTexture(src_rc, src)) {
            CountCopyFallbackReason("alpha_blend_a_src_out_of_bounds");
            CountMethodFallback(method);
            SoftwareDelegate()->OperateRect(delegate_method, tar, reftar, rctar, textures);
            if (dst != nullptr) {
                dst->MarkCpuDirty();
            }
            return;
        }
        if (IsOpaqueAlphaBlendCopyEnabled() && opacity == 255 && src->IsOpaque()) {
            if (src_rc.get_width() == rctar.get_width() &&
                src_rc.get_height() == rctar.get_height() &&
                dst->CopyGpuFrom(src, rctar, src_rc)) {
                CountGpuFastPath(method_name + ":CopyOpaque");
                return;
            }
            const bool needs_area_downsample =
                RectNeedsAlphaAreaDownsample(rctar, src_rc, src);
            if (!needs_area_downsample && !src_rc.is_empty() &&
                IsGpuCopyTrianglesEnabled()) {
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
        // The Godot bridge performs scaled AlphaBlend_a with premultiplied
        // bilinear sampling. Keeping area downscales on the CPU forces a full
        // GPU readback/upload between every transparent motion layer, which is
        // especially expensive for the 0.75x SD animations used by Yuzu games.
        if (dst->BlendGpuFrom(src, rctar, src_rc,
                          TVP_GODOT_GPU_BLEND_ALPHA_BLEND_A,
                          opacity, 0)) {
            CountGpuFastPath(method_name);
            return;
        }
    }

    if (method_name == "FillARGB" && dst != nullptr &&
        IsGpuRectFastPathEnabled("FillARGB") &&
        ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst) &&
        dst->EnsureGpuHandle() &&
        dst->ClearGpu(godot_method != nullptr ? godot_method->Color() : 0,
                      rctar)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (method_name == "FillMask" && dst != nullptr &&
        IsGpuRectFastPathEnabled("FillMask") &&
        ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst, dst) &&
        dst->EnsureGpuHandle() &&
        dst->BlendGpuFrom(dst, rctar, rctar,
                          TVP_GODOT_GPU_BLEND_FILL_MASK,
                          godot_method != nullptr ? godot_method->Opacity() : 255,
                          0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if (method_name == "RemoveConstOpacity" && dst != nullptr &&
        IsGpuRectFastPathEnabled("RemoveConstOpacity") &&
        ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst, dst) &&
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
        ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst, src1, src2) &&
        RectBoundsInsideTexture(textures[0].second, src1) &&
        RectBoundsInsideTexture(textures[1].second, src2) &&
        dst->EnsureGpuHandle() && src1->EnsureGpuHandle() &&
        src2->EnsureGpuHandle() &&
        src1->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        src2->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendGpuFrom2(
            src1, src2, rctar, textures[0].second, textures[1].second,
            method_name == "ConstAlphaBlend_SD_d"
                ? TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD_D
                : TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD,
            godot_method != nullptr ? godot_method->Opacity() : 255, 0)) {
        CountGpuFastPath(method_name);
        return;
    }

    if((method_name == "UnivTransBlend" ||
        method_name == "UnivTransBlend_d" ||
        method_name == "UnivTransBlend_a") &&
       dst != nullptr && src3_1 != nullptr && src3_2 != nullptr &&
       src3_3 != nullptr &&
       IsGpuRectFastPathEnabled(method_name.c_str()) &&
       ShouldUseGpuRectFastPath(rctar, method_name.c_str(), dst, src3_1,
                                src3_2, src3_3) &&
       RectBoundsInsideTexture(textures[0].second, src3_1) &&
       RectBoundsInsideTexture(textures[1].second, src3_2) &&
       RectBoundsInsideTexture(textures[2].second, src3_3) &&
       dst->EnsureGpuHandle() && src3_1->EnsureGpuHandle() &&
       src3_2->EnsureGpuHandle() && src3_3->EnsureGpuHandle() &&
       src3_1->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
       src3_2->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
       src3_3->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
       dst->BlendGpuFrom3(
           src3_1, src3_2, src3_3, rctar, textures[0].second,
           textures[1].second, textures[2].second,
           method_name == "UnivTransBlend_d"
               ? TVP_GODOT_GPU_BLEND_UNIVERSAL_D
               : method_name == "UnivTransBlend_a"
                     ? TVP_GODOT_GPU_BLEND_UNIVERSAL_A
                     : TVP_GODOT_GPU_BLEND_UNIVERSAL,
           godot_method != nullptr ? godot_method->Phase() : 0,
           static_cast<uint32_t>(godot_method != nullptr
                                     ? godot_method->Vague()
                                     : 0))) {
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
    if (DetailedRenderStats() && method_name == "Copy") {
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

    if (DetailedRenderStats() && method_name == "FillARGB") {
        if (dst == nullptr) {
            CountCopyFallbackReason("fill_dst_not_godot");
        } else if (!IsGpuRectFastPathEnabled("FillARGB")) {
            CountCopyFallbackReason("fill_disabled");
        } else if (!IsGpuRectLargeEnoughForMethod(rctar, "FillARGB") &&
                   !dst->HasPendingGpuWrites() &&
                   !dst->RequiresGpuReadback()) {
            CountCopyFallbackReason("fill_small_rect");
        } else if (!dst->HasGodotGpuHandle()) {
            CountCopyFallbackReason("fill_no_gpu_handle");
        } else {
            CountCopyFallbackReason("fill_fastpath_failed");
        }
    }

    CountMethodFallback(method);
    ScopedRenderTiming software_timing(
        method_name.empty() ? "software:(null)" : "software:" + method_name);
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
    auto *godot_method = dynamic_cast<GodotRenderMethod *>(method);
    auto *dst = dynamic_cast<GodotTexture2D *>(target);
    auto *src = textures.size() == 1
        ? dynamic_cast<GodotTexture2D *>(textures[0].first)
        : nullptr;
    if (dst != nullptr && dst->IsCpuCompositeTarget()) {
        CountMethodFallback(method);
        SoftwareDelegate()->OperateTriangles(
            godot_method != nullptr ? godot_method->Delegate() : method,
            nTriangles, target, reftar, rcclip, pttar, textures);
        dst->MarkCpuDirty();
        return;
    }
    if (method_name == "Copy") {
        if (dst != nullptr && src != nullptr &&
            IsGpuRectFastPathEnabled("Copy") &&
            IsGpuCopyTrianglesEnabled() &&
            !TrianglesNeedAlphaAreaDownsample(static_cast<uint32_t>(nTriangles),
                                              pttar, textures[0].second, src) &&
            dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
            src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
            dst->CopyTrianglesGpuFrom(src, static_cast<uint32_t>(nTriangles),
                                      rcclip, pttar, textures[0].second)) {
            CountGpuFastPath(method_name);
            return;
        }
        CountCopyFallbackReason(IsGpuCopyTrianglesEnabled()
                                    ? "triangles"
                                    : "triangles_cpu");
    }

    uint32_t blend_mode = 0;
    const char *fast_path_name = nullptr;
    if (method_name == "AlphaBlend") {
        blend_mode = TVP_GODOT_GPU_BLEND_ALPHA;
        fast_path_name = "AlphaBlend";
    } else if (method_name == "AlphaBlend_d") {
        blend_mode = TVP_GODOT_GPU_BLEND_ALPHA_D;
        fast_path_name = "AlphaBlend_d";
    } else if (method_name == "AlphaBlend_a" ||
               method_name == "PerspectiveAlphaBlend_a") {
        blend_mode = TVP_GODOT_GPU_BLEND_ALPHA_BLEND_A;
        fast_path_name = "AlphaBlend_a";
    } else if (method_name == "CopyColor") {
        blend_mode = TVP_GODOT_GPU_BLEND_COPY_COLOR;
        fast_path_name = "CopyColor";
    } else if (method_name == "ConstAlphaBlend_d") {
        blend_mode = TVP_GODOT_GPU_BLEND_CONST_ALPHA_D;
        fast_path_name = "ConstAlphaBlend_d";
    } else if (method_name == "PsScreenBlend") {
        blend_mode = TVP_GODOT_GPU_BLEND_PS_SCREEN;
        fast_path_name = "PsScreenBlend";
    } else if (method_name == "PsAddBlend") {
        blend_mode = TVP_GODOT_GPU_BLEND_PS_ADD;
        fast_path_name = "PsAddBlend";
    } else if (method_name == "PsSubBlend") {
        blend_mode = TVP_GODOT_GPU_BLEND_PS_SUBTRACT;
        fast_path_name = "PsSubBlend";
    } else if (method_name == "PsMulBlend") {
        blend_mode = TVP_GODOT_GPU_BLEND_PS_MULTIPLY;
        fast_path_name = "PsMulBlend";
    }
    if (fast_path_name != nullptr && nTriangles > 0 && nTriangles <= 64 &&
        dst != nullptr &&
        src != nullptr && IsGpuRectFastPathEnabled(fast_path_name) &&
        IsGpuBlendTrianglesEnabled() &&
        ShouldUseGpuRectFastPath(rcclip, fast_path_name, dst, src) &&
        dst->EnsureGpuHandle() && src->EnsureGpuHandle() &&
        src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) &&
        dst->BlendTrianglesGpuFrom(
            src, static_cast<uint32_t>(nTriangles), rcclip, pttar,
            textures[0].second, blend_mode,
            godot_method != nullptr ? godot_method->Opacity() : 255)) {
        CountGpuFastPath(method_name + ":Triangles");
        return;
    }
    CountMethodFallback(method);
    SoftwareDelegate()->OperateTriangles(
        godot_method != nullptr ? godot_method->Delegate() : method,
        nTriangles, target, reftar, rcclip, pttar, textures);
    if (dst != nullptr) {
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

bool TVPGodotClearMotionScratchInPlace(
    iTVPBaseBitmap *bitmap, const tTVPRect &rect, uint32_t argb) {
    if(bitmap == nullptr || !bitmap->Is32BPP() || rect.is_empty()) {
        return false;
    }
    // Layer.assignImages shares the source texture. D3DEmote immediately
    // reuses its full-window work layer for the next character, so clearing a
    // still-shared texture in place also clears the character that received
    // the preceding frame. The scratch is fully repainted; detach without
    // copying instead of mutating an assignImages destination.
    bitmap->IndependNoCopy();
    auto *texture =
        dynamic_cast<GodotTexture2D *>(bitmap->GetTexture());
    if(texture == nullptr || !texture->EnsureGpuHandle()) {
        return false;
    }
    // iTVPBaseBitmap::Fill converts script-facing AARRGGBB to the backend's
    // AABBGGRR byte layout before invoking FillARGB.
    const uint32_t abgr =
        (argb & 0xff00ff00u) |
        ((argb & 0x000000ffu) << 16u) |
        ((argb & 0x00ff0000u) >> 16u);
    return texture->ClearGpu(abgr, rect);
}

bool TVPGodotUploadRgbaInPlace(iTVPBaseBitmap *bitmap, const void *pixels,
                               uint32_t stride_bytes) {
    if(bitmap == nullptr || !bitmap->Is32BPP() || pixels == nullptr) {
        return false;
    }
    bitmap->IndependNoCopy();
    auto *texture = dynamic_cast<GodotTexture2D *>(bitmap->GetTexture());
    return texture != nullptr &&
        texture->UpdateGpuRgba(pixels, stride_bytes);
}

bool TVPGodotCompositeAlphaUnionMask(
    iTVPBaseBitmap *dst_bitmap, iTVPBaseBitmap *src_bitmap,
    iTVPBaseBitmap *mask_scratch_bitmap,
    iTVPBaseBitmap *const *mask_bitmaps,
    const tTVPRect *mask_dst_rects,
    const tTVPRect *mask_src_rects,
    size_t mask_count,
    bool use_mask_alpha,
    int width,
    int height) {
    if(dst_bitmap == nullptr || src_bitmap == nullptr ||
       mask_scratch_bitmap == nullptr || mask_bitmaps == nullptr ||
       mask_dst_rects == nullptr || mask_src_rects == nullptr ||
       mask_count == 0 || dst_bitmap == src_bitmap ||
       dst_bitmap == mask_scratch_bitmap ||
       src_bitmap == mask_scratch_bitmap) {
        return false;
    }

    if(width <= 0) {
        width = static_cast<int>(dst_bitmap->GetWidth());
    }
    if(height <= 0) {
        height = static_cast<int>(dst_bitmap->GetHeight());
    }
    const tTVPRect full_dst(0, 0, width, height);
    const tTVPRect full_src(0, 0, width, height);
    const tTVPRect full_mask(0, 0, width, height);
    if(full_dst.is_empty() || full_src.is_empty() || full_mask.is_empty() ||
       full_dst.right > static_cast<tjs_int>(dst_bitmap->GetWidth()) ||
       full_dst.bottom > static_cast<tjs_int>(dst_bitmap->GetHeight()) ||
       full_src.right > static_cast<tjs_int>(src_bitmap->GetWidth()) ||
       full_src.bottom > static_cast<tjs_int>(src_bitmap->GetHeight()) ||
       full_mask.right > static_cast<tjs_int>(mask_scratch_bitmap->GetWidth()) ||
       full_mask.bottom > static_cast<tjs_int>(mask_scratch_bitmap->GetHeight())) {
        return false;
    }

    auto *dst = dynamic_cast<GodotTexture2D *>(
        dst_bitmap->GetTextureForRender(true, &full_dst));
    auto *src = dynamic_cast<GodotTexture2D *>(src_bitmap->GetTexture());
    auto *mask_scratch = dynamic_cast<GodotTexture2D *>(
        mask_scratch_bitmap->GetTextureForRender(true, &full_mask));
    if(dst == nullptr || src == nullptr || mask_scratch == nullptr ||
       !dst->EnsureGpuHandle() || !src->EnsureGpuHandle() ||
       !mask_scratch->EnsureGpuHandle() ||
       !src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) ||
       !dst->ClearGpu(0x00000000u, full_dst) ||
       !mask_scratch->ClearGpu(0x00000000u, full_mask)) {
        return false;
    }

    for(size_t i = 0; i < mask_count; ++i) {
        if(mask_bitmaps[i] == nullptr || mask_dst_rects[i].is_empty() ||
           mask_src_rects[i].is_empty() ||
           mask_dst_rects[i].get_width() != mask_src_rects[i].get_width() ||
           mask_dst_rects[i].get_height() != mask_src_rects[i].get_height()) {
            continue;
        }
        auto *mask = dynamic_cast<GodotTexture2D *>(
            mask_bitmaps[i]->GetTexture());
        if(mask == nullptr || !mask->EnsureGpuHandle() ||
           !mask->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) ||
           !mask_scratch->BlendGpuFrom(
               mask, mask_dst_rects[i], mask_src_rects[i],
               // AlphaBlend holds the destination alpha in KiriKiri.  The
               // union scratch starts transparent, so use the destination-
               // alpha variant to accumulate src-over alpha just like the
               // native op-5 mask builder.
               TVP_GODOT_GPU_BLEND_ALPHA_D, 255, 0)) {
            return false;
        }
    }

    const tTVPPointD points[6] = {
        {0.0, 0.0},
        {static_cast<double>(full_dst.right), 0.0},
        {0.0, static_cast<double>(full_dst.bottom)},
        {static_cast<double>(full_dst.right), 0.0},
        {0.0, static_cast<double>(full_dst.bottom)},
        {static_cast<double>(full_dst.right),
         static_cast<double>(full_dst.bottom)},
    };
    return dst->DrawMaskedTrianglesGpuFrom(
        src, mask_scratch, 2, full_dst, points, points, points, 255, 0,
        use_mask_alpha);
}

bool TVPGodotApplyAlphaUnionMask(
    iTVPBaseBitmap *dst_bitmap, iTVPBaseBitmap *mask_scratch_bitmap,
    iTVPBaseBitmap *const *mask_bitmaps,
    const tTVPRect *mask_dst_rects,
    const tTVPRect *mask_src_rects,
    size_t mask_count,
    bool threshold_mask_mode,
    int item_flags,
    int width,
    int height) {
    if(dst_bitmap == nullptr || mask_scratch_bitmap == nullptr ||
       mask_bitmaps == nullptr || mask_dst_rects == nullptr ||
       mask_src_rects == nullptr || mask_count == 0 ||
       dst_bitmap == mask_scratch_bitmap) {
        return false;
    }

    if(width <= 0) {
        width = static_cast<int>(dst_bitmap->GetWidth());
    }
    if(height <= 0) {
        height = static_cast<int>(dst_bitmap->GetHeight());
    }
    const tTVPRect full_rect(0, 0, width, height);
    if(full_rect.is_empty() ||
       full_rect.right > static_cast<tjs_int>(dst_bitmap->GetWidth()) ||
       full_rect.bottom > static_cast<tjs_int>(dst_bitmap->GetHeight()) ||
       full_rect.right >
           static_cast<tjs_int>(mask_scratch_bitmap->GetWidth()) ||
       full_rect.bottom >
           static_cast<tjs_int>(mask_scratch_bitmap->GetHeight())) {
        return false;
    }

    auto *dst = dynamic_cast<GodotTexture2D *>(
        dst_bitmap->GetTextureForRender(true, &full_rect));
    auto *mask_scratch = dynamic_cast<GodotTexture2D *>(
        mask_scratch_bitmap->GetTextureForRender(true, &full_rect));
    if(dst == nullptr || mask_scratch == nullptr ||
       !dst->EnsureGpuHandle() || !mask_scratch->EnsureGpuHandle() ||
       !dst->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) ||
       !mask_scratch->ClearGpu(0x00000000u, full_rect)) {
        return false;
    }

    for(size_t i = 0; i < mask_count; ++i) {
        if(mask_bitmaps[i] == nullptr ||
           mask_bitmaps[i] == mask_scratch_bitmap ||
           mask_dst_rects[i].is_empty() ||
           mask_src_rects[i].is_empty() ||
           mask_dst_rects[i].get_width() !=
               mask_src_rects[i].get_width() ||
           mask_dst_rects[i].get_height() !=
               mask_src_rects[i].get_height()) {
            return false;
        }
        auto *mask = dynamic_cast<GodotTexture2D *>(
            mask_bitmaps[i]->GetTexture());
        if(mask == nullptr || !mask->EnsureGpuHandle() ||
           !mask->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) ||
           !mask_scratch->BlendGpuFrom(
               mask, mask_dst_rects[i], mask_src_rects[i],
               TVP_GODOT_GPU_BLEND_ALPHA_D, 255, 0)) {
            return false;
        }
    }

    const uint32_t color =
        static_cast<uint32_t>(item_flags & 0xff) |
        (static_cast<uint32_t>(threshold_mask_mode ? 1u : 0u) << 8);
    if(!dst->BlendGpuFrom(
           mask_scratch, full_rect, full_rect,
           TVP_GODOT_GPU_BLEND_APPLY_ALPHA_MASK, 64, color)) {
        return false;
    }
    CountGpuFastPath("ApplyAlphaUnionMask");
    return true;
}

bool TVPGodotApplyAlphaMask(
    iTVPBaseBitmap *dst_bitmap, iTVPBaseBitmap *src_bitmap,
    const tTVPRect &dst_rect, const tTVPRect &src_rect,
    int threshold, bool threshold_mask_mode, int item_flags) {
    if(dst_bitmap == nullptr || src_bitmap == nullptr ||
       dst_bitmap == src_bitmap || dst_rect.is_empty() || src_rect.is_empty() ||
       dst_rect.get_width() != src_rect.get_width() ||
       dst_rect.get_height() != src_rect.get_height()) {
        return false;
    }
    auto *dst = dynamic_cast<GodotTexture2D *>(
        dst_bitmap->GetTextureForRender(true, &dst_rect));
    auto *src = dynamic_cast<GodotTexture2D *>(src_bitmap->GetTexture());
    if(dst == nullptr || src == nullptr ||
       !RectBoundsInsideTexture(dst_rect, dst) ||
       !RectBoundsInsideTexture(src_rect, src) ||
       !dst->EnsureGpuHandle() || !src->EnsureGpuHandle() ||
       !dst->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) ||
       !src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled())) {
        return false;
    }
    const uint32_t color =
        static_cast<uint32_t>(item_flags & 0xff) |
        (static_cast<uint32_t>(threshold_mask_mode ? 1u : 0u) << 8);
    if(!dst->BlendGpuFrom(
           src, dst_rect, src_rect,
           TVP_GODOT_GPU_BLEND_APPLY_ALPHA_MASK,
           std::clamp(threshold, 0, 255), color)) {
        return false;
    }
    CountGpuFastPath("ApplyAlphaMask");
    return true;
}

bool TVPGodotBlendAlphaDWithMask(
    iTVPBaseBitmap *dst_bitmap, iTVPBaseBitmap *src_bitmap,
    iTVPBaseBitmap *mask_bitmap, const tTVPRect &dst_rect,
    const tTVPRect &src_rect, const tTVPRect &mask_rect, int opacity,
    bool threshold_mask_mode) {
    if(dst_bitmap == nullptr || src_bitmap == nullptr ||
       mask_bitmap == nullptr || dst_bitmap == src_bitmap ||
       dst_bitmap == mask_bitmap || src_bitmap == mask_bitmap ||
       dst_rect.is_empty() || src_rect.is_empty() || mask_rect.is_empty() ||
       dst_rect.get_width() != src_rect.get_width() ||
       dst_rect.get_height() != src_rect.get_height() ||
       dst_rect.get_width() != mask_rect.get_width() ||
       dst_rect.get_height() != mask_rect.get_height()) {
        return false;
    }
    auto *dst = dynamic_cast<GodotTexture2D *>(
        dst_bitmap->GetTextureForRender(true, &dst_rect));
    auto *src = dynamic_cast<GodotTexture2D *>(src_bitmap->GetTexture());
    auto *mask = dynamic_cast<GodotTexture2D *>(mask_bitmap->GetTexture());
    if(dst == nullptr || src == nullptr || mask == nullptr ||
       !RectBoundsInsideTexture(dst_rect, dst) ||
       !RectBoundsInsideTexture(src_rect, src) ||
       !RectBoundsInsideTexture(mask_rect, mask) ||
       !dst->EnsureGpuHandle() || !src->EnsureGpuHandle() ||
       !mask->EnsureGpuHandle() ||
       !dst->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) ||
       !src->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled()) ||
       !mask->UploadCpuToGpu(!DeferredGodotGpuDrainEnabled())) {
        return false;
    }
    const uint32_t mode = threshold_mask_mode
        ? TVP_GODOT_GPU_BLEND_ALPHA_D_MASK_THRESHOLD
        : TVP_GODOT_GPU_BLEND_ALPHA_D_MASK_MULTIPLY;
    if(!dst->BlendGpuFrom2(
           src, mask, dst_rect, src_rect, mask_rect, mode,
           std::clamp(opacity, 0, 255), 0)) {
        return false;
    }
    CountGpuFastPath("AlphaBlend_d+Mask");
    return true;
}

std::string TVPGetGodotRenderManagerFallbackStats() {
    std::vector<std::pair<std::string, uint64_t>> entries;
    uint64_t texture_creates = 0;
    uint64_t texture_clones = 0;
    uint64_t texture_resizes = 0;
    uint64_t fallbacks = 0;
    uint64_t gpu_fastpaths = 0;
    std::vector<std::pair<std::string, uint64_t>> gpu_entries;
    std::vector<std::pair<std::string, uint64_t>> copy_fallback_entries;
    std::vector<std::pair<std::string, RenderTimingStats>> timing_entries;
    uint64_t upload_count = 0;
    uint64_t upload_success_count = 0;
    uint64_t upload_bytes = 0;
    uint64_t upload_total_ns = 0;
    uint64_t upload_max_ns = 0;
    if (DetailedRenderStats()) {
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
        if (RenderTimingEnabled()) {
            timing_entries.reserve(g_render_timing_stats.size());
            for (const auto &entry : g_render_timing_stats) {
                timing_entries.push_back(entry);
            }
            upload_count = g_upload_count;
            upload_success_count = g_upload_success_count;
            upload_bytes = g_upload_bytes;
            upload_total_ns = g_upload_total_ns;
            upload_max_ns = g_upload_max_ns;
        }
    }
    texture_creates = g_texture_create_count.load(std::memory_order_relaxed);
    texture_clones = g_texture_clone_count.load(std::memory_order_relaxed);
    texture_resizes = g_texture_resize_count.load(std::memory_order_relaxed);
    fallbacks = g_software_fallback_count.load(std::memory_order_relaxed);
    gpu_fastpaths = g_gpu_fastpath_count.load(std::memory_order_relaxed);
    std::sort(entries.begin(), entries.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });
    std::sort(gpu_entries.begin(), gpu_entries.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });
    std::sort(copy_fallback_entries.begin(), copy_fallback_entries.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });
    std::sort(timing_entries.begin(), timing_entries.end(),
              [](const auto &a, const auto &b) {
                  return a.second.total_ns > b.second.total_ns;
              });

    std::ostringstream out;
    out << " fallback_ops=" << fallbacks
        << " gpu_ops=" << gpu_fastpaths
        << " gpu_min_area=" << GpuRectMinArea()
        << " gpu_alpha_min_area=" << GpuRectMinAreaForMethod("AlphaBlend")
        << " texture_creates=" << texture_creates
        << " texture_clones=" << texture_clones
        << " texture_resizes=" << texture_resizes
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
    out << "] timing_hot=[";
    const size_t timing_limit = std::min<size_t>(timing_entries.size(), 8);
    for (size_t i = 0; i < timing_limit; ++i) {
        if (i != 0) out << ",";
        const auto &entry = timing_entries[i];
        const uint64_t avg_us = entry.second.count == 0
                                    ? 0
                                    : entry.second.total_ns /
                                          entry.second.count / 1000u;
        const uint64_t max_us = entry.second.max_ns / 1000u;
        out << entry.first << ":" << entry.second.count
            << "/avg" << avg_us << "us/max" << max_us << "us";
    }
    out << "] uploads=" << upload_count
        << "/ok" << upload_success_count
        << "/bytes" << upload_bytes
        << "/avg" << (upload_count == 0 ? 0 : upload_total_ns / upload_count / 1000u)
        << "us/max" << upload_max_ns / 1000u << "us";
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
