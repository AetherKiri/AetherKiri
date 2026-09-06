#pragma once

#include "../RenderManager.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class GodotRenderMethod final : public iTVPRenderMethod {
public:
    explicit GodotRenderMethod(iTVPRenderMethod *delegate);
    int EnumParameterID(const char *name) override;
    void SetParameterUInt(int id, unsigned int Value) override;
    void SetParameterInt(int id, int Value) override;
    void SetParameterPtr(int id, const void *Value) override;
    void SetParameterFloat(int id, float Value) override;
    void SetParameterColor4B(int id, unsigned int clr) override;
    void SetParameterOpa(int id, int Value) override;
    void SetParameterFloatArray(int id, float *Value, int nElem) override;
    iTVPRenderMethod *SetBlendFuncSeparate(int func, int srcRGB, int dstRGB,
                                           int srcAlpha, int dstAlpha) override;
    bool IsBlendTarget() override;
    iTVPRenderMethod *Delegate() const { return delegate_; }
    uint32_t Color() const { return color_; }
    int Opacity() const { return opacity_; }
    int Phase() const { return phase_; }
    int Vague() const { return vague_; }
    int AreaLeft() const { return area_left_; }
    int AreaTop() const { return area_top_; }
    int AreaRight() const { return area_right_; }
    int AreaBottom() const { return area_bottom_; }

private:
    iTVPRenderMethod *delegate_ = nullptr;
    uint32_t color_ = 0;
    int opacity_ = 255;
    int phase_id_ = -1;
    int vague_id_ = -1;
    int area_left_id_ = -1;
    int area_top_id_ = -1;
    int area_right_id_ = -1;
    int area_bottom_id_ = -1;
    int phase_ = 0;
    int vague_ = 0;
    int area_left_ = 0;
    int area_top_ = 0;
    int area_right_ = 0;
    int area_bottom_ = 0;
};

class GodotTexture2D final : public iTVPTexture2D {
public:
    GodotTexture2D(const void *pixel, int pitch, unsigned int w,
                   unsigned int h, TVPTextureFormat::e format,
                   int create_flags = RENDER_CREATE_TEXTURE_FLAG_ANY);
    ~GodotTexture2D() override;

    TVPTextureFormat::e GetFormat() const override { return format_; }
    const void *GetScanLineForRead(tjs_uint l) override;
    void *GetScanLineForWrite(tjs_uint l) override;
    void *GetScanLineForWriteUninitialized(tjs_uint l) override;
    tjs_int GetPitch() const override { return pitch_; }
    void Update(const void *pixel, TVPTextureFormat::e format, int pitch,
                const tTVPRect &rc) override;
    uint32_t GetPoint(int x, int y) override;
    void SetPoint(int x, int y, uint32_t clr) override;
    void SetSize(unsigned int w, unsigned int h) override;
    bool IsStatic() override { return false; }
    bool IsOpaque() override { return opacity_known_ && opaque_; }
    bool IsCpuCompositeTarget() const override { return cpu_composite_target_; }
    void SetCpuCompositeTarget(bool value) override {
        cpu_composite_target_ = value;
    }
    bool HasKnownTransparency() const { return opacity_known_ && !opaque_; }
    krkr::Texture2D *GetAdapterTexture(krkr::Texture2D *origTex) override {
        return origTex;
    }
    uint64_t GetGodotGpuHandle() const { return gpu_handle_; }
    bool HasGodotGpuHandle() const { return gpu_handle_ != 0; }
    bool HasPendingGpuWrites() const { return gpu_dirty_ && !cpu_dirty_; }
    bool RequiresGpuReadback() const {
        return gpu_handle_ != 0 && gpu_dirty_ && !cpu_dirty_;
    }
    uint64_t BeginGpuReadback() const;
    bool PollGpuReadback(uint64_t request, void *out_pixels,
                         size_t out_pixels_size, uint32_t stride_bytes,
                         bool *ready) const;
    void DiscardGpuReadback(uint64_t request) const;
    bool EnsureGpuHandle();
    bool ClearGpu(uint32_t rgba, const tTVPRect &rc);
    bool CopyGpuFrom(GodotTexture2D *src, const tTVPRect &dst_rc,
                     const tTVPRect &src_rc);
    bool CopyTrianglesGpuFrom(GodotTexture2D *src, uint32_t triangle_count,
                              const tTVPRect &clip_rc,
                              const tTVPPointD *dst_points,
                              const tTVPPointD *src_points);
    bool BlendTrianglesGpuFrom(GodotTexture2D *src, uint32_t triangle_count,
                               const tTVPRect &clip_rc,
                               const tTVPPointD *dst_points,
                               const tTVPPointD *src_points, uint32_t mode,
                               int opacity);
    bool DrawTrianglesGpuFrom(GodotTexture2D *src, uint32_t triangle_count,
                              const tTVPRect &clip_rc,
                              const tTVPPointD *dst_points,
                              const tTVPPointD *src_points, int opacity,
                              uint32_t blend_mode);
    bool DrawExternalTrianglesGpuFrom(
        uint64_t source_gpu_handle, uint32_t triangle_count,
        const tTVPRect &clip_rc, const tTVPPointD *dst_points,
        const tTVPPointD *src_points, int opacity, uint32_t blend_mode);
    bool DrawMaskedTrianglesGpuFrom(
        GodotTexture2D *src, GodotTexture2D *mask,
        uint32_t triangle_count, const tTVPRect &clip_rc,
        const tTVPPointD *dst_points, const tTVPPointD *src_points,
        const tTVPPointD *mask_points, int opacity, uint32_t blend_mode,
        bool use_mask_alpha);
    bool BlendGpuFrom(GodotTexture2D *src, const tTVPRect &dst_rc,
                      const tTVPRect &src_rc, uint32_t mode, int opacity,
                      uint32_t color);
    bool BlendGpuFrom2(GodotTexture2D *src1, GodotTexture2D *src2,
                       const tTVPRect &dst_rc, const tTVPRect &src1_rc,
                       const tTVPRect &src2_rc, uint32_t mode, int opacity,
                       uint32_t color);
    bool BlendGpuFrom3(GodotTexture2D *src1, GodotTexture2D *src2,
                       GodotTexture2D *src3, const tTVPRect &dst_rc,
                       const tTVPRect &src1_rc, const tTVPRect &src2_rc,
                       const tTVPRect &src3_rc, uint32_t mode, int opacity,
                       uint32_t color);
    bool UploadCpuToGpu(bool flush_pending_gpu_writes = true);
    bool UpdateGpuRgba(const void *pixels, uint32_t stride_bytes);
    void MarkGpuDirty() { gpu_dirty_ = true; }
    void MarkCpuDirty() {
        cpu_dirty_ = true;
        gpu_dirty_ = false;
        cpu_pixels_known_zero_ = false;
    }
    void EnsureCpuReadable();

private:
    void CreateGpuHandle(const void *pixel, int pitch);
    void ReleaseGpuHandle();
    void EnsureCpuStorage();
    void DiscardCpuStorage();
    void SetOpacityFromPixels(const void *pixel, int pitch);
    void MarkOpacityUnknown();
    void MarkTransparentKnown();
    void MarkOpaqueKnown();

    TVPTextureFormat::e format_ = TVPTextureFormat::RGBA;
    int pitch_ = 0;
    std::vector<uint8_t> pixels_;
    uint64_t gpu_handle_ = 0;
    bool gpu_dirty_ = false;
    bool cpu_dirty_ = false;
    bool opacity_known_ = false;
    bool opaque_ = false;
    bool cpu_composite_target_ = false;
    bool retain_cpu_shadow_ = false;
    bool discard_unwritten_on_partial_update_ = false;
    // Fresh render targets are initialized to transparent black. Preserve
    // that fact until the first CPU write so the Godot bridge can allocate
    // and clear the GPU image without packing and uploading a multi-megabyte
    // zero-filled staging buffer.
    bool cpu_pixels_known_zero_ = false;
};

class GodotRenderManager final : public iTVPRenderManager {
public:
    GodotRenderManager() = default;

    iTVPTexture2D *CreateTexture2D(const void *pixel, int pitch, unsigned int w,
                                   unsigned int h, TVPTextureFormat::e format,
                                   int flags = RENDER_CREATE_TEXTURE_FLAG_ANY) override;
    iTVPTexture2D *CreateTexture2D(tTVPBitmap *bmp) override;
    iTVPTexture2D *CreateTexture2D(TJS::tTJSBinaryStream *s) override;
    iTVPTexture2D *CreateTexture2D(unsigned int neww, unsigned int newh,
                                   iTVPTexture2D *tex) override;

    iTVPRenderMethod *GetRenderMethod(const char *name,
                                      uint32_t *hint = nullptr) override;
    const char *GetName() override { return "GodotNative"; }
    bool GetRenderStat(unsigned int &drawCount, uint64_t &vmemsize) override;
    bool GetTextureStat(iTVPTexture2D *texture, uint64_t &vmemsize) override;
    int EnumParameterID(const char *name) override;
    void SetParameterUInt(int id, unsigned int Value) override;
    void SetParameterInt(int id, int Value) override;
    void SetParameterPtr(int id, const void *Value) override;
    void SetParameterFloat(int id, float Value) override;

    void OperateRect(iTVPRenderMethod *method, iTVPTexture2D *tar,
                     iTVPTexture2D *reftar, const tTVPRect &rctar,
                     const tRenderTexRectArray &textures) override;
    void OperateTriangles(iTVPRenderMethod *method, int nTriangles,
                          iTVPTexture2D *target, iTVPTexture2D *reftar,
                          const tTVPRect &rcclip, const tTVPPointD *pttar,
                          const tRenderTexQuadArray &textures) override;
    void OperatePerspective(iTVPRenderMethod *method, int nQuads,
                            iTVPTexture2D *target, iTVPTexture2D *reftar,
                            const tTVPRect &rcclip, const tTVPPointD *pttar,
                            const tRenderTexQuadArray &textures) override;
    bool IsSoftware() override { return false; }

private:
    iTVPRenderManager *SoftwareDelegate();

    unsigned int draw_count_ = 0;
    uint64_t vmem_size_ = 0;
    iTVPRenderManager *software_delegate_ = nullptr;
    std::unordered_map<uint32_t, GodotRenderMethod *> method_wrappers_;
    int stretch_parameter_id_ = -1;
    int stretch_type_ = 0;
};

void TVPForceRegisterGodotRenderManager();
void TVPSetGodotRenderManagerGpuFastPathEnabled(bool enabled);
std::string TVPGetGodotRenderManagerFallbackStats();

class iTVPBaseBitmap;

// Motion scratch layers are completely repainted before their next use. They
// can temporarily share a GPU texture with an assignImages destination, where
// the generic bitmap path would otherwise copy the old texture just to clear
// it. Detach a shared scratch texture without copying its old pixels, then
// clear the private texture on the ordered GPU queue.
bool TVPGodotClearMotionScratchInPlace(
    iTVPBaseBitmap *bitmap, const tTVPRect &rect, uint32_t argb);
bool TVPGodotUploadRgbaInPlace(
    iTVPBaseBitmap *bitmap, const void *pixels, uint32_t stride_bytes);

// Compose a source bitmap through the alpha union of one or more mask
// bitmaps without synchronously reading Metal textures back to the CPU.
// Destination rectangles are local to mask_scratch/dst; source rectangles
// are local to the corresponding mask bitmap.
bool TVPGodotCompositeAlphaUnionMask(
    iTVPBaseBitmap *dst, iTVPBaseBitmap *src,
    iTVPBaseBitmap *mask_scratch,
    iTVPBaseBitmap *const *masks,
    const tTVPRect *mask_dst_rects,
    const tTVPRect *mask_src_rects,
    size_t mask_count,
    bool use_mask_alpha,
    int width = -1,
    int height = -1);

// Build the alpha union of one or more mask bitmaps on the GPU, then apply it
// to an existing destination while preserving destination RGB.
bool TVPGodotApplyAlphaUnionMask(
    iTVPBaseBitmap *dst, iTVPBaseBitmap *mask_scratch,
    iTVPBaseBitmap *const *masks,
    const tTVPRect *mask_dst_rects,
    const tTVPRect *mask_src_rects,
    size_t mask_count,
    bool threshold_mask_mode,
    int item_flags,
    int width = -1,
    int height = -1);

// Preserve destination RGB and combine only its alpha with source alpha.
// This is the GPU equivalent of E-mote's sub_6AC4E4 mask loop.
bool TVPGodotApplyAlphaMask(
    iTVPBaseBitmap *dst, iTVPBaseBitmap *src,
    const tTVPRect &dst_rect, const tTVPRect &src_rect,
    int threshold, bool threshold_mask_mode, int item_flags);

// Fuse E-mote's positive-alpha stencil and the immediately following
// AlphaBlend_d composition. This preserves the exact integer alpha formula
// while avoiding a temporary texture write and a second compute dispatch.
bool TVPGodotBlendAlphaDWithMask(
    iTVPBaseBitmap *dst, iTVPBaseBitmap *src, iTVPBaseBitmap *mask,
    const tTVPRect &dst_rect, const tTVPRect &src_rect,
    const tTVPRect &mask_rect, int opacity, bool threshold_mask_mode);
