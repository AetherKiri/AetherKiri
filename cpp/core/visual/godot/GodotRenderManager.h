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

private:
    iTVPRenderMethod *delegate_ = nullptr;
    uint32_t color_ = 0;
    int opacity_ = 255;
};

class GodotTexture2D final : public iTVPTexture2D {
public:
    GodotTexture2D(const void *pixel, int pitch, unsigned int w,
                   unsigned int h, TVPTextureFormat::e format);
    ~GodotTexture2D() override;

    TVPTextureFormat::e GetFormat() const override { return format_; }
    const void *GetScanLineForRead(tjs_uint l) override;
    void *GetScanLineForWrite(tjs_uint l) override;
    tjs_int GetPitch() const override { return pitch_; }
    void Update(const void *pixel, TVPTextureFormat::e format, int pitch,
                const tTVPRect &rc) override;
    uint32_t GetPoint(int x, int y) override;
    void SetPoint(int x, int y, uint32_t clr) override;
    void SetSize(unsigned int w, unsigned int h) override;
    bool IsStatic() override { return false; }
    bool IsOpaque() override { return false; }
    krkr::Texture2D *GetAdapterTexture(krkr::Texture2D *origTex) override {
        return origTex;
    }
    uint64_t GetGodotGpuHandle() const { return gpu_handle_; }
    bool HasGodotGpuHandle() const { return gpu_handle_ != 0; }
    bool EnsureGpuHandle();
    bool ClearGpu(uint32_t rgba, const tTVPRect &rc);
    bool CopyGpuFrom(GodotTexture2D *src, const tTVPRect &dst_rc,
                     const tTVPRect &src_rc);
    bool CopyTrianglesGpuFrom(GodotTexture2D *src, uint32_t triangle_count,
                              const tTVPRect &clip_rc,
                              const tTVPPointD *dst_points,
                              const tTVPPointD *src_points);
    bool BlendGpuFrom(GodotTexture2D *src, const tTVPRect &dst_rc,
                      const tTVPRect &src_rc, uint32_t mode, int opacity,
                      uint32_t color);
    bool BlendGpuFrom2(GodotTexture2D *src1, GodotTexture2D *src2,
                       const tTVPRect &dst_rc, const tTVPRect &src1_rc,
                       const tTVPRect &src2_rc, uint32_t mode, int opacity,
                       uint32_t color);
    bool UploadCpuToGpu();
    void MarkGpuDirty() { gpu_dirty_ = true; }
    void MarkCpuDirty() { cpu_dirty_ = true; gpu_dirty_ = false; }
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
};

void TVPForceRegisterGodotRenderManager();
void TVPSetGodotRenderManagerGpuFastPathEnabled(bool enabled);
std::string TVPGetGodotRenderManagerFallbackStats();
