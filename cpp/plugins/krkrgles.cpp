#include "tjs.h"
#include "ncbind.hpp"
#include "ScriptMgnIntf.h"
#include "LayerImpl.h"
#include "BitmapIntf.h"
#include "RenderManager.h"
#include "EventIntf.h"
#include "WindowIntf.h"
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <png.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#elif defined(__SSSE3__)
#include <tmmintrin.h>
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#define GLES_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "krkrgles", __VA_ARGS__)
#define GLES_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "krkrgles", __VA_ARGS__)
#else
#define GLES_LOGI(...) ((void)0)
#define GLES_LOGW(...) ((void)0)
#endif

#define NCB_MODULE_NAME TJS_W("krkrgles.dll")

// Live2D model's internal FBO — published by krkrlive2d.cpp
struct Live2DRenderTarget {
    GLuint fbo;
    GLsizei width;
    GLsizei height;
};
extern Live2DRenderTarget g_live2dRenderTarget;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace {

inline tjs_int ToInt(const tTJSVariant &v, tjs_int fallback = 0) {
    switch (v.Type()) {
    case tvtInteger: return static_cast<tjs_int>(v);
    case tvtReal:    return static_cast<tjs_int>(static_cast<tjs_real>(v));
    default:         return fallback;
    }
}

inline tjs_int NormalizeExtent(tjs_int v, tjs_int fb) { return v > 0 ? v : fb; }

using ModuleName = std::basic_string<tjs_char>;

inline ModuleName NormalizeModuleName(tjs_int n, tTJSVariant **p) {
    if (n <= 0 || !p || !p[0] || p[0]->Type() == tvtVoid) return TJS_W("live2d");
    ttstr raw(*p[0]);
    ModuleName out(raw.c_str());
    for (auto &ch : out)
        if (ch >= 'A' && ch <= 'Z') ch += 32;
    if (out.empty()) out = TJS_W("live2d");
    return out;
}

inline void SetResultObject(tTJSVariant *r, iTJSDispatch2 *o) {
    if (r && o) *r = tTJSVariant(o, o);
}

inline void SetObjectProperty(iTJSDispatch2 *o, const tjs_char *n, const tTJSVariant &v) {
    if (!o || !n) return;
    tTJSVariant copy(v);
    o->PropSet(TJS_MEMBERENSURE, n, nullptr, &copy, o);
}

inline void SetObjectMethod(iTJSDispatch2 *o, const tjs_char *n,
                            tTJSNativeClassMethodCallback cb) {
    if (!o || !n || !cb) return;
    iTJSDispatch2 *m = TJSCreateNativeClassMethod(cb);
    if (!m) return;
    tTJSVariant v(m, m);
    o->PropSet(TJS_MEMBERENSURE, n, nullptr, &v, o);
    m->Release();
}

static bool ForceSetScriptMember(const tTJSVariant &target,
                                 const tjs_char *member,
                                 const tTJSVariant &value) {
    if (target.Type() != tvtObject || !member) return false;
    tTJSVariantClosure clo = target.AsObjectClosureNoAddRef();
    if (!clo.Object) return false;
    return TJS_SUCCEEDED(clo.Object->PropSet(
        TJS_MEMBERENSURE | TJS_IGNOREPROP, member, nullptr, &value,
        clo.ObjThis ? clo.ObjThis : clo.Object));
}

static bool ForceSetGlobalObjectMember(const tjs_char *globalName,
                                       const tjs_char *member,
                                       const tTJSVariant &value) {
    tTJS *engine = TVPGetScriptEngine();
    if (!engine || !globalName || !member) return false;
    iTJSDispatch2 *global = engine->GetGlobalNoAddRef();
    if (!global) return false;

    tTJSVariant target;
    if (TJS_FAILED(global->PropGet(0, globalName, nullptr, &target, global)))
        return false;

    bool installed = ForceSetScriptMember(target, member, value);

    tTJSVariant prototype;
    tTJSVariantClosure clo = target.AsObjectClosureNoAddRef();
    if (clo.Object &&
        TJS_SUCCEEDED(clo.Object->PropGet(0, TJS_W("prototype"), nullptr,
                                          &prototype, clo.Object))) {
        installed = ForceSetScriptMember(prototype, member, value) || installed;
    }

    return installed;
}

static bool ForceMirrorGlobalToMember(const tjs_char *targetGlobal,
                                      const tjs_char *sourceGlobal) {
    tTJS *engine = TVPGetScriptEngine();
    if (!engine || !targetGlobal || !sourceGlobal) return false;
    iTJSDispatch2 *global = engine->GetGlobalNoAddRef();
    if (!global) return false;

    tTJSVariant value;
    if (TJS_FAILED(global->PropGet(0, sourceGlobal, nullptr, &value, global)))
        return false;
    return ForceSetGlobalObjectMember(targetGlobal, sourceGlobal, value);
}

static bool CreateScriptObject(const tjs_char *expression, tTJSVariant *result) {
    if (!expression || !result) return false;
    try {
        TVPExecuteExpression(ttstr(expression), result);
        return result->Type() == tvtObject;
    } catch (...) {
        *result = tTJSVariant();
        return false;
    }
}

static const tjs_char *const kDrawAliasTargets[] = {
    TJS_W("KAGWindow"),
    TJS_W("kag"),
    TJS_W("KAGWorldPlugin"),
};

static bool InstallDrawDeviceScriptAliases() {
    try {
        TVPExecuteExpression(
            TJS_W("try {")
            TJS_W("Window.OGLDrawDevice = OGLDrawDevice;")
            TJS_W("Window.GLESAdaptor = GLESAdaptor;")
            TJS_W("} catch(e) { }"),
            static_cast<tTJSVariant *>(nullptr));

        bool installed = false;

        tTJSVariant gpuDevice;
        if (CreateScriptObject(TJS_W("new OGLDrawDevice()"), &gpuDevice) ||
            CreateScriptObject(TJS_W("new GLESAdaptor()"), &gpuDevice)) {
            for (const tjs_char *target : kDrawAliasTargets) {
                installed = ForceSetGlobalObjectMember(
                                target, TJS_W("drawDevice"), gpuDevice) ||
                            installed;
                installed = ForceSetGlobalObjectMember(
                                target, TJS_W("gpuDrawDevice"), gpuDevice) ||
                            installed;
                installed = ForceSetGlobalObjectMember(
                                target, TJS_W("OGLDrawDevice"), gpuDevice) ||
                            installed;
                installed = ForceSetGlobalObjectMember(
                                target, TJS_W("GLESAdaptor"), gpuDevice) ||
                            installed;
            }
        }

        if (TVPMainWindow) {
            const tTJSVariant &windowDrawDevice =
                TVPMainWindow->GetDrawDeviceObject();
            if (windowDrawDevice.Type() == tvtObject) {
                for (const tjs_char *target : kDrawAliasTargets) {
                    installed = ForceSetGlobalObjectMember(
                                    target, TJS_W("nativeDrawDevice"),
                                    windowDrawDevice) ||
                                installed;
                }
            }
        }

        static const tjs_char *kKagGlobalFallbacks[] = {
            TJS_W("System"), TJS_W("Storages"), TJS_W("Scripts"),
            TJS_W("Dictionary"), TJS_W("Debug"), TJS_W("Math"),
            TJS_W("Plugins"), TJS_W("Window"), TJS_W("dm")
        };
        for (const tjs_char *name : kKagGlobalFallbacks) {
            for (const tjs_char *target : kDrawAliasTargets) {
                installed = ForceMirrorGlobalToMember(target, name) ||
                            installed;
            }
        }
        return installed;
    } catch (...) {
        return false;
    }
}

extern "C" bool TVPInstallKrkrGlesDrawDeviceAliases() {
    const bool installed = InstallDrawDeviceScriptAliases();
    if (installed) {
        spdlog::info("krkrgles: installed KAGWindow drawDevice aliases");
    }
    return installed;
}

class DrawDeviceAliasContinuousHook final : public tTVPContinuousEventCallbackIntf {
public:
    void Start() {
        if (registered_) return;
        TVPAddContinuousEventHook(this);
        registered_ = true;
        attempts_ = 0;
        installed_ = false;
    }

    void OnContinuousCallback(tjs_uint64) override {
        if (installed_) return;
        installed_ = InstallDrawDeviceScriptAliases();
        if (installed_) {
            spdlog::info("krkrgles: installed runtime KAGWindow drawDevice aliases");
            return;
        }
        if (++attempts_ == 600) {
            spdlog::warn("krkrgles: runtime KAGWindow drawDevice aliases not installed after 600 ticks");
        }
    }

private:
    bool registered_ = false;
    bool installed_ = false;
    tjs_uint32 attempts_ = 0;
};

static DrawDeviceAliasContinuousHook g_drawDeviceAliasHook;

// ---------------------------------------------------------------------------
// KTX1 texture loader — uploads compressed or uncompressed KTX to GL
// ---------------------------------------------------------------------------
struct KtxHeader {
    uint8_t  identifier[12];
    uint32_t endianness;
    uint32_t glType;
    uint32_t glTypeSize;
    uint32_t glFormat;
    uint32_t glInternalFormat;
    uint32_t glBaseInternalFormat;
    uint32_t pixelWidth;
    uint32_t pixelHeight;
    uint32_t pixelDepth;
    uint32_t numberOfArrayElements;
    uint32_t numberOfFaces;
    uint32_t numberOfMipmapLevels;
    uint32_t bytesOfKeyValueData;
};

// ---------------------------------------------------------------------------
// Software BC7 (BPTC) RGBA8 decoder for GL_COMPRESSED_RGBA_BPTC_UNORM
// Reference: Khronos Data Format Specification §17, Microsoft BC7 Format
// ---------------------------------------------------------------------------

struct BC7ModeInfo {
    uint8_t ns, pb, rb, isb, cb, ab, epb, spb, ib, ib2;
};
static const BC7ModeInfo kBC7Mode[8] = {
    {3,4,0,0,4,0,1,0,3,0},{2,6,0,0,6,0,0,1,3,0},{3,6,0,0,5,0,0,0,2,0},{2,6,0,0,7,0,1,0,2,0},
    {1,0,2,1,5,6,0,0,2,3},{1,0,2,0,7,8,0,0,2,2},{1,0,0,0,7,7,1,0,4,0},{2,6,0,0,5,5,1,0,2,0},
};

static const uint16_t kBC7P2[64] = {
    0xCCCC,0x8888,0xEEEE,0xECC8,0xC880,0xFEEC,0xFEC8,0xEC80,
    0xC800,0xFFEC,0xFE80,0xE800,0xFFE8,0xFF00,0xFFF0,0xF000,
    0xF710,0x008E,0x7100,0x08CE,0x008C,0x7310,0x3100,0x8CCE,
    0x088C,0x3110,0x6666,0x366C,0x17E8,0x0FF0,0x718E,0x399C,
    0xAAAA,0xF0F0,0x5A5A,0x33CC,0x3C3C,0x55AA,0x9696,0xA55A,
    0x73CE,0x13C8,0x324C,0x3BDC,0x6996,0xC33C,0x9966,0x0660,
    0x0272,0x04E4,0x4E40,0x2720,0xC936,0x936C,0x39C6,0x639C,
    0x9336,0x9CC6,0x817E,0xE718,0xCCF0,0x0FCC,0x7744,0xEE22,
};

static const uint32_t kBC7P3[64] = {
    0xAA685050,0x6A5A5040,0x5A5A4200,0x5450A0A8,0xA5A50000,0xA0A05050,0x5555A0A0,0x5A5A5050,
    0xAA550000,0xAA555500,0xAAAA5500,0x90906090,0x94949494,0xA4A4A4A4,0xA9A59450,0x2A0A4250,
    0xA5945040,0x0A425054,0xA5A5A500,0x55A0A0A0,0xA8A85454,0x6A6A4040,0xA4A45000,0x1A1A0500,
    0x0050A4A4,0xAAA59090,0x14696914,0x69691400,0xA08585A0,0xAA821414,0x50A4A450,0x6A5A0200,
    0xA9A58000,0x5090A0A0,0xA8A09050,0x24242424,0x00AA5500,0x24924924,0x24492424,0xAA549524,
    0x0A0A0A0A,0xAA985002,0x0000A5A5,0x96960000,0xA5A5000A,0xA0A0A5A5,0x96000000,0x40804080,
    0xA9A8A9A8,0xAAAAAA44,0x2A4A5254,0x00000000,0xAAAAAAAA,0xAA0A0AAA,0x0A0A0A00,0x0000AAAA,
    0xAAA0AAA0,0x0A0A0000,0x0AA00AA0,0xAA00AA00,0x00AA00AA,0xA0A00A0A,0x0A0A0000,0xAAAA0000,
};

static const uint8_t kBC7A2[64] = {
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15, 2, 8, 2, 2, 8, 8,15, 2, 8, 2, 2, 8, 8, 2, 2,
    15,15, 6, 8, 2, 8,15,15, 2, 8, 2, 2, 2,15,15, 6,
     6, 2, 6, 8,15,15, 2, 2,15,15,15,15,15, 2, 2,15,
};
static const uint8_t kBC7A3a[64] = {
     3, 3,15,15, 8, 3,15,15, 8, 8, 6, 6, 6, 5, 3, 3,
     3, 3, 8,15, 3, 3, 6,10, 5, 8, 8, 6, 8, 5,15,15,
     8,15, 3, 5, 6,10, 8,15,15, 3,15, 5,15,15,15,15,
     3,15, 5, 5, 5, 8, 5,10, 5,10, 8,13,15,12, 3, 3,
};
static const uint8_t kBC7A3b[64] = {
    15, 8, 8, 3,15,15, 3, 8,15,15,15,15,15,15,15, 8,
    15, 8,15, 3,15, 8,15, 8, 3,15, 6,10,15,15,10, 8,
    15, 3,15,10,10, 8, 9,10, 6,15, 8,15, 3, 6, 6, 8,
    15, 3,15,15,15,15,15,15,15,15,15,15, 3,15,15, 8,
};

static const uint8_t kBC7W2[4]  = {0,21,43,64};
static const uint8_t kBC7W3[8]  = {0,9,18,27,37,46,55,64};
static const uint8_t kBC7W4[16] = {0,4,9,13,17,21,26,30,34,38,43,47,51,55,60,64};

static inline uint8_t bc7Lerp(int e0, int e1, int w) {
    return static_cast<uint8_t>(((64 - w) * e0 + w * e1 + 32) >> 6);
}

static void DecodeBC7Block(const uint8_t *src, uint8_t out[4][4][4]) {
    uint64_t lo, hi;
    std::memcpy(&lo, src, 8);
    std::memcpy(&hi, src + 8, 8);

    uint32_t modeBits = static_cast<uint32_t>(lo) & 0xFF;
    if (!modeBits) { std::memset(out, 0, 64); return; }
    int mode = __builtin_ctz(modeBits);

    int bp = mode + 1;
    auto rd = [&](int n) -> uint32_t {
        if (!n) return 0;
        uint32_t v;
        if (bp < 64) {
            v = static_cast<uint32_t>(lo >> bp);
            if (bp + n > 64)
                v |= static_cast<uint32_t>(hi << (64 - bp));
        } else {
            v = static_cast<uint32_t>(hi >> (bp - 64));
        }
        bp += n;
        return v & ((1u << n) - 1);
    };

    const BC7ModeInfo &m = kBC7Mode[mode];
    int part = rd(m.pb), rot = rd(m.rb), isel = rd(m.isb);

    int ep[3][2][4] = {};
    for (int c = 0; c < 3; c++)
        for (int s = 0; s < m.ns; s++) { ep[s][0][c] = rd(m.cb); ep[s][1][c] = rd(m.cb); }
    if (m.ab)
        for (int s = 0; s < m.ns; s++) { ep[s][0][3] = rd(m.ab); ep[s][1][3] = rd(m.ab); }

    int numCh = m.ab ? 4 : 3;
    if (m.epb) {
        for (int s = 0; s < m.ns; s++)
            for (int e = 0; e < 2; e++) {
                int pb = rd(1);
                for (int c = 0; c < numCh; c++) ep[s][e][c] = (ep[s][e][c] << 1) | pb;
            }
    } else if (m.spb) {
        for (int s = 0; s < m.ns; s++) {
            int pb = rd(1);
            for (int e = 0; e < 2; e++)
                for (int c = 0; c < numCh; c++) ep[s][e][c] = (ep[s][e][c] << 1) | pb;
        }
    }

    int cbits = m.cb + ((m.epb || m.spb) ? 1 : 0);
    int abits = m.ab ? m.ab + ((m.epb || m.spb) ? 1 : 0) : 0;
    for (int s = 0; s < m.ns; s++)
        for (int e = 0; e < 2; e++) {
            for (int c = 0; c < 3; c++)
                ep[s][e][c] = (ep[s][e][c] << (8 - cbits)) | (ep[s][e][c] >> (2 * cbits - 8));
            if (m.ab)
                ep[s][e][3] = (ep[s][e][3] << (8 - abits)) | (ep[s][e][3] >> (2 * abits - 8));
            else
                ep[s][e][3] = 255;
        }

    int anchors[3] = {0, 0, 0};
    if (m.ns >= 2) anchors[1] = kBC7A2[part];
    if (m.ns >= 3) { anchors[1] = kBC7A3a[part]; anchors[2] = kBC7A3b[part]; }

    const uint8_t *wt1 = (m.ib == 2) ? kBC7W2 : (m.ib == 3) ? kBC7W3 : kBC7W4;
    int idx1[16];
    for (int i = 0; i < 16; i++) {
        int sub = 0;
        if (m.ns == 2) sub = (kBC7P2[part] >> i) & 1;
        else if (m.ns == 3) sub = (kBC7P3[part] >> (2*i)) & 3;
        bool isAnc = (i == anchors[sub]);
        idx1[i] = rd(m.ib - (isAnc ? 1 : 0));
    }

    int idx2[16] = {};
    const uint8_t *wt2 = nullptr;
    if (m.ib2) {
        wt2 = (m.ib2 == 2) ? kBC7W2 : (m.ib2 == 3) ? kBC7W3 : kBC7W4;
        for (int i = 0; i < 16; i++)
            idx2[i] = rd(m.ib2 - (i == 0 ? 1 : 0));
    }

    for (int i = 0; i < 16; i++) {
        int px = i & 3, py = i >> 2;
        int sub = 0;
        if (m.ns == 2) sub = (kBC7P2[part] >> i) & 1;
        else if (m.ns == 3) sub = (kBC7P3[part] >> (2*i)) & 3;

        int w1 = wt1[idx1[i]];
        uint8_t r = bc7Lerp(ep[sub][0][0], ep[sub][1][0], w1);
        uint8_t g = bc7Lerp(ep[sub][0][1], ep[sub][1][1], w1);
        uint8_t b = bc7Lerp(ep[sub][0][2], ep[sub][1][2], w1);
        uint8_t a = bc7Lerp(ep[sub][0][3], ep[sub][1][3], w1);

        if (m.ib2) {
            int w2v = wt2[idx2[i]];
            if (isel == 0) a = bc7Lerp(ep[sub][0][3], ep[sub][1][3], w2v);
            else { r = bc7Lerp(ep[sub][0][0], ep[sub][1][0], w2v);
                   g = bc7Lerp(ep[sub][0][1], ep[sub][1][1], w2v);
                   b = bc7Lerp(ep[sub][0][2], ep[sub][1][2], w2v); }
        }

        if (rot == 1)      { uint8_t t = a; a = r; r = t; }
        else if (rot == 2) { uint8_t t = a; a = g; g = t; }
        else if (rot == 3) { uint8_t t = a; a = b; b = t; }

        out[py][px][0] = r; out[py][px][1] = g; out[py][px][2] = b; out[py][px][3] = a;
    }
}

static bool DecodeBPTC_RGBA(const uint8_t *data, uint32_t dataSize,
                            uint32_t w, uint32_t h,
                            std::vector<uint8_t> &outRGBA) {
    uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    uint32_t expected = bw * bh * 16;
    if (dataSize < expected) return false;

    outRGBA.resize(static_cast<size_t>(w) * h * 4);

    auto decodeRows = [&](uint32_t rowStart, uint32_t rowEnd) {
        for (uint32_t by = rowStart; by < rowEnd; by++) {
            const uint8_t *block = data + static_cast<size_t>(by) * bw * 16;
            for (uint32_t bx = 0; bx < bw; bx++, block += 16) {
                uint8_t rgba[4][4][4];
                DecodeBC7Block(block, rgba);
                uint32_t px0 = bx * 4, py0 = by * 4;
                if (px0 + 4 <= w && py0 + 4 <= h) {
                    for (int y = 0; y < 4; y++) {
                        uint32_t dstY = py0 + y;
                        size_t off = (static_cast<size_t>(dstY) * w + px0) * 4;
                        std::memcpy(&outRGBA[off], rgba[y], 16);
                    }
                } else {
                    for (int y = 0; y < 4; y++) {
                        uint32_t srcY = py0 + y;
                        if (srcY >= h) break;
                        uint32_t dstY = srcY;
                        for (int x = 0; x < 4; x++) {
                            uint32_t px = px0 + x;
                            if (px >= w) break;
                            size_t off = (static_cast<size_t>(dstY) * w + px) * 4;
                            std::memcpy(&outRGBA[off], rgba[y][x], 4);
                        }
                    }
                }
            }
        }
    };

    constexpr uint32_t kMinRowsForThreading = 64;
    unsigned numThreads = 1;
    if (bh > kMinRowsForThreading) {
        unsigned hw = std::thread::hardware_concurrency();
        numThreads = std::min(std::max(hw, 1u), 8u);
    }

    auto t0 = std::chrono::steady_clock::now();

    if (numThreads <= 1) {
        decodeRows(0, bh);
    } else {
        std::vector<std::thread> threads;
        threads.reserve(numThreads);
        uint32_t rowsPerThread = bh / numThreads;
        uint32_t remainder = bh % numThreads;
        uint32_t start = 0;
        for (unsigned t = 0; t < numThreads; t++) {
            uint32_t end = start + rowsPerThread + (t < remainder ? 1 : 0);
            threads.emplace_back(decodeRows, start, end);
            start = end;
        }
        for (auto &t : threads) t.join();
    }

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    GLES_LOGI("BC7 decode %ux%u: %lld ms (%u threads, %u block-rows)",
              w, h, (long long)ms, numThreads, bh);
    spdlog::info("krkrgles: BC7 decode {}x{}: {} ms ({} threads)", w, h, ms, numThreads);

    return true;
}

static bool IsBPTCFormat(GLenum fmt) { return fmt == 0x8E8C; }

// ---------------------------------------------------------------------------
// Software ETC2 + EAC RGBA8 decoder (fallback for GLES backend)
// ---------------------------------------------------------------------------

const int kETC2ModifierTable[8][4] = {
    {  2,   8,  -2,   -8}, {  5,  17,  -5,  -17}, {  9,  29,  -9,  -29}, { 13,  42, -13,  -42},
    { 18,  60, -18,  -60}, { 24,  80, -24,  -80}, { 33, 106, -33, -106}, { 47, 183, -47, -183}
};
const int kEACModifierTable[16][8] = {
    { -3, -6,  -9, -15,  2,  5,  8, 14}, { -3, -7, -10, -13,  2,  6,  9, 12},
    { -2, -5,  -8, -13,  1,  4,  7, 12}, { -2, -4,  -6, -13,  1,  3,  5, 12},
    { -3, -6,  -8, -12,  2,  5,  7, 11}, { -3, -7,  -9, -11,  2,  6,  8, 10},
    { -4, -7,  -8, -11,  3,  6,  7, 10}, { -3, -5,  -8, -11,  2,  4,  7, 10},
    { -2, -6,  -8, -10,  1,  5,  7,  9}, { -2, -5,  -8, -10,  1,  4,  7,  9},
    { -2, -4,  -8, -10,  1,  3,  7,  9}, { -2, -5,  -7, -10,  1,  4,  6,  9},
    { -3, -4,  -7, -10,  2,  3,  6,  9}, { -1, -2,  -3, -10,  0,  1,  2,  9},
    { -4, -6,  -8,  -9,  3,  5,  7,  8}, { -3, -5,  -7,  -9,  2,  4,  6,  8}
};

inline uint8_t clamp255(int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); }

inline int extend4to8(int v) { return (v << 4) | v; }
inline int extend5to8(int v) { return (v << 3) | (v >> 2); }
inline int extend6to8(int v) { return (v << 2) | (v >> 4); }
inline int extend7to8(int v) { return (v << 1) | (v >> 6); }

const int kETC2DistTable[8] = { 3, 6, 11, 16, 23, 32, 47, 64 };

void DecodeETC2Block(const uint8_t *src, uint8_t rgba[4][4][4]) {
    uint32_t hi = (static_cast<uint32_t>(src[0]) << 24) |
                  (static_cast<uint32_t>(src[1]) << 16) |
                  (static_cast<uint32_t>(src[2]) << 8)  |
                  static_cast<uint32_t>(src[3]);
    uint32_t lo = (static_cast<uint32_t>(src[4]) << 24) |
                  (static_cast<uint32_t>(src[5]) << 16) |
                  (static_cast<uint32_t>(src[6]) << 8)  |
                  static_cast<uint32_t>(src[7]);

    bool diffBit = (hi >> 1) & 1;
    bool flipBit = hi & 1;

    if (!diffBit) {
        // Individual mode
        int r1 = extend4to8((hi >> 28) & 0xF);
        int r2 = extend4to8((hi >> 24) & 0xF);
        int g1 = extend4to8((hi >> 20) & 0xF);
        int g2 = extend4to8((hi >> 16) & 0xF);
        int b1 = extend4to8((hi >> 12) & 0xF);
        int b2 = extend4to8((hi >>  8) & 0xF);
        int tbl1 = (hi >> 5) & 0x7;
        int tbl2 = (hi >> 2) & 0x7;

        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
            int pixelIdx = x * 4 + y;
            int msb = (lo >> (31 - pixelIdx)) & 1;
            int lsb = (lo >> (15 - pixelIdx)) & 1;
            int idx = (msb << 1) | lsb;
            bool sb2 = flipBit ? (y >= 2) : (x >= 2);
            int mod = kETC2ModifierTable[sb2 ? tbl2 : tbl1][idx];
            rgba[y][x][0] = clamp255((sb2 ? r2 : r1) + mod);
            rgba[y][x][1] = clamp255((sb2 ? g2 : g1) + mod);
            rgba[y][x][2] = clamp255((sb2 ? b2 : b1) + mod);
            rgba[y][x][3] = 255;
        }
        return;
    }

    // Differential mode: check for overflow → T, H, or Planar
    int R = (hi >> 27) & 0x1F;
    int dR = static_cast<int>((hi >> 24) & 0x7); if (dR >= 4) dR -= 8;
    int G = (hi >> 19) & 0x1F;
    int dG = static_cast<int>((hi >> 16) & 0x7); if (dG >= 4) dG -= 8;
    int B = (hi >> 11) & 0x1F;
    int dB = static_cast<int>((hi >>  8) & 0x7); if (dB >= 4) dB -= 8;

    int R2c = R + dR, G2c = G + dG, B2c = B + dB;

    if (R2c < 0 || R2c > 31) {
        // --- T mode (etcpack: THUMB59T) ---
        // R1 = bits[58:57,56:55], G1 = bits[54:51], B1 = bits[50:47]
        // R2 = bits[46:43], G2 = bits[42:39], B2 = bits[38:35], d = bits[34:32]
        int r1 = extend4to8((((src[0] >> 1) & 0x3) << 2) | ((src[0] & 0x1) << 1) | ((src[1] >> 7) & 0x1));
        int g1 = extend4to8((src[1] >> 3) & 0xF);
        int b1 = extend4to8(((src[1] & 0x7) << 1) | ((src[2] >> 7) & 0x1));
        int r2 = extend4to8((src[2] >> 3) & 0xF);
        int g2 = extend4to8(((src[2] & 0x7) << 1) | ((src[3] >> 7) & 0x1));
        int b2 = extend4to8((src[3] >> 3) & 0xF);
        int distIdx = src[3] & 0x7;
        int dist = kETC2DistTable[distIdx];
        int paint[4][3] = {
            { r1, g1, b1 },
            { clamp255(r2 + dist), clamp255(g2 + dist), clamp255(b2 + dist) },
            { r2, g2, b2 },
            { clamp255(r2 - dist), clamp255(g2 - dist), clamp255(b2 - dist) }
        };
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
            int pixelIdx = x * 4 + y;
            int msb = (lo >> (31 - pixelIdx)) & 1;
            int lsb = (lo >> (15 - pixelIdx)) & 1;
            int idx = (msb << 1) | lsb;
            rgba[y][x][0] = clamp255(paint[idx][0]);
            rgba[y][x][1] = clamp255(paint[idx][1]);
            rgba[y][x][2] = clamp255(paint[idx][2]);
            rgba[y][x][3] = 255;
        }
        return;
    }

    if (G2c < 0 || G2c > 31) {
        // --- H mode (etcpack: THUMB58H) ---
        // Color0 = bits[57:46], Color1 = bits[45:34], d = bits[34:33]
        int r1_4 = ((src[0] & 0x3) << 2) | ((src[1] >> 6) & 0x3);
        int g1_4 = (src[1] >> 2) & 0xF;
        int b1_4 = ((src[1] & 0x3) << 2) | ((src[2] >> 6) & 0x3);
        int r2_4 = (src[2] >> 2) & 0xF;
        int g2_4 = ((src[2] & 0x3) << 2) | ((src[3] >> 6) & 0x3);
        int b2_4 = (src[3] >> 2) & 0xF;
        int r1 = extend4to8(r1_4), g1 = extend4to8(g1_4), b1 = extend4to8(b1_4);
        int r2 = extend4to8(r2_4), g2 = extend4to8(g2_4), b2 = extend4to8(b2_4);
        int distIdx = (src[3] >> 1) & 0x3;
        int packed0 = (r1_4 << 8) | (g1_4 << 4) | b1_4;
        int packed1 = (r2_4 << 8) | (g2_4 << 4) | b2_4;
        if (packed0 >= packed1) distIdx = (distIdx << 1) | 1;
        else                    distIdx = distIdx << 1;
        int dist = kETC2DistTable[distIdx];
        int paint[4][3] = {
            { clamp255(r1 + dist), clamp255(g1 + dist), clamp255(b1 + dist) },
            { clamp255(r1 - dist), clamp255(g1 - dist), clamp255(b1 - dist) },
            { clamp255(r2 + dist), clamp255(g2 + dist), clamp255(b2 + dist) },
            { clamp255(r2 - dist), clamp255(g2 - dist), clamp255(b2 - dist) }
        };
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
            int pixelIdx = x * 4 + y;
            int msb = (lo >> (31 - pixelIdx)) & 1;
            int lsb = (lo >> (15 - pixelIdx)) & 1;
            int idx = (msb << 1) | lsb;
            rgba[y][x][0] = clamp255(paint[idx][0]);
            rgba[y][x][1] = clamp255(paint[idx][1]);
            rgba[y][x][2] = clamp255(paint[idx][2]);
            rgba[y][x][3] = 255;
        }
        return;
    }

    if (B2c < 0 || B2c > 31) {
        // --- Planar mode (etcpack: Planar57) ---
        // RO=bits[62:57], GO=bits[54:48], BO=bits[44:39]
        // RH=bits[38:33], GH=bits[32:26], BH=bits[25:20]
        // RV=bits[19:14], GV=bits[13:7],  BV=bits[6:1]
        int RO = extend6to8((src[0] >> 1) & 0x3F);
        int GO = extend7to8(((src[0] & 0x1) << 6) | ((src[1] >> 2) & 0x3F));
        int BO = extend6to8(((src[2] & 0x1F) << 1) | ((src[3] >> 7) & 0x1));
        int RH = extend6to8((src[3] >> 1) & 0x3F);
        int GH = extend7to8(((src[3] & 0x1) << 6) | ((src[4] >> 2) & 0x3F));
        int BH = extend6to8(((src[4] & 0x3) << 4) | ((src[5] >> 4) & 0xF));
        int RV = extend6to8(((src[5] & 0xF) << 2) | ((src[6] >> 6) & 0x3));
        int GV = extend7to8(((src[6] & 0x3F) << 1) | ((src[7] >> 7) & 0x1));
        int BV = extend6to8((src[7] >> 1) & 0x3F);
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
            rgba[y][x][0] = clamp255((x * (RH - RO) + y * (RV - RO) + 4 * RO + 2) >> 2);
            rgba[y][x][1] = clamp255((x * (GH - GO) + y * (GV - GO) + 4 * GO + 2) >> 2);
            rgba[y][x][2] = clamp255((x * (BH - BO) + y * (BV - BO) + 4 * BO + 2) >> 2);
            rgba[y][x][3] = 255;
        }
        return;
    }

    // --- Standard differential mode ---
    int r1 = extend5to8(R), g1 = extend5to8(G), b1 = extend5to8(B);
    int r2 = extend5to8(R2c), g2 = extend5to8(G2c), b2 = extend5to8(B2c);
    int tbl1 = (hi >> 5) & 0x7;
    int tbl2 = (hi >> 2) & 0x7;

    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
        int pixelIdx = x * 4 + y;
        int msb = (lo >> (31 - pixelIdx)) & 1;
        int lsb = (lo >> (15 - pixelIdx)) & 1;
        int idx = (msb << 1) | lsb;
        bool sb2 = flipBit ? (y >= 2) : (x >= 2);
        int mod = kETC2ModifierTable[sb2 ? tbl2 : tbl1][idx];
        rgba[y][x][0] = clamp255((sb2 ? r2 : r1) + mod);
        rgba[y][x][1] = clamp255((sb2 ? g2 : g1) + mod);
        rgba[y][x][2] = clamp255((sb2 ? b2 : b1) + mod);
        rgba[y][x][3] = 255;
    }
}

void DecodeEACBlock(const uint8_t *src, uint8_t alpha[4][4]) {
    int base = src[0];
    int multiplier = (src[1] >> 4) & 0xF;
    int tableIdx = src[1] & 0xF;
    if (multiplier == 0) multiplier = 1;

    uint64_t bits = 0;
    for (int i = 2; i < 8; ++i) bits = (bits << 8) | src[i];

    for (int i = 0; i < 16; ++i) {
        int shift = 45 - i * 3;
        int pixIdx = (bits >> shift) & 0x7;
        int val = base + kEACModifierTable[tableIdx][pixIdx] * multiplier;
        int x = i / 4, y = i % 4;
        alpha[y][x] = clamp255(val);
    }
}

bool DecodeETC2RGBA(const uint8_t *compData, uint32_t dataSize,
                           uint32_t w, uint32_t h,
                           std::vector<uint8_t> &outRGBA) {
    uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    uint32_t expectedSize = bw * bh * 16;
    if (dataSize < expectedSize) return false;

    outRGBA.resize(static_cast<size_t>(w) * h * 4);
    const uint8_t *block = compData;

    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx, block += 16) {
            uint8_t alpha[4][4];
            uint8_t rgba[4][4][4];
            DecodeEACBlock(block, alpha);
            DecodeETC2Block(block + 8, rgba);

            for (int y = 0; y < 4; ++y) {
                uint32_t srcY = by * 4 + y;
                if (srcY >= h) break;
                uint32_t dstY = srcY;
                for (int x = 0; x < 4; ++x) {
                    uint32_t px = bx * 4 + x;
                    if (px >= w) break;
                    size_t off = (static_cast<size_t>(dstY) * w + px) * 4;
                    outRGBA[off + 0] = rgba[y][x][0];
                    outRGBA[off + 1] = rgba[y][x][1];
                    outRGBA[off + 2] = rgba[y][x][2];
                    outRGBA[off + 3] = alpha[y][x];
                }
            }
        }
    }
    return true;
}

bool DecodeETC2RGB(const uint8_t *compData, uint32_t dataSize,
                          uint32_t w, uint32_t h,
                          std::vector<uint8_t> &outRGBA) {
    uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    uint32_t expectedSize = bw * bh * 8;
    if (dataSize < expectedSize) return false;

    outRGBA.resize(static_cast<size_t>(w) * h * 4);
    const uint8_t *block = compData;

    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx, block += 8) {
            uint8_t rgba[4][4][4];
            DecodeETC2Block(block, rgba);
            for (int y = 0; y < 4; ++y) {
                uint32_t srcY = by * 4 + y;
                if (srcY >= h) break;
                uint32_t dstY = srcY;
                for (int x = 0; x < 4; ++x) {
                    uint32_t px = bx * 4 + x;
                    if (px >= w) break;
                    size_t off = (static_cast<size_t>(dstY) * w + px) * 4;
                    outRGBA[off + 0] = rgba[y][x][0];
                    outRGBA[off + 1] = rgba[y][x][1];
                    outRGBA[off + 2] = rgba[y][x][2];
                    outRGBA[off + 3] = 255;
                }
            }
        }
    }
    return true;
}

const std::vector<GLint> &GetSupportedCompressedFormats() {
    static std::vector<GLint> cached;
    static bool inited = false;
    if (!inited) {
        GLint n = 0;
        glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &n);
        if (n > 0) {
            cached.resize(n);
            glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, cached.data());
        }
        inited = true;
    }
    return cached;
}

bool IsCompressedFormatSupported(GLenum internalFormat) {
    for (GLint f : GetSupportedCompressedFormats()) {
        if (static_cast<GLenum>(f) == internalFormat) return true;
    }
    return false;
}

bool IsETC2Format(GLenum fmt) {
    return fmt == 0x9274 || fmt == 0x9278 || // RGB8, RGBA8 (EAC)
           fmt == 0x9275 || fmt == 0x9279 || // SRGB8, SRGB8_A8 (EAC)
           fmt == 0x9276 || fmt == 0x9277;   // punchthrough alpha
}

bool IsETC2WithAlpha(GLenum fmt) {
    return fmt == 0x9278 || fmt == 0x9279;
}

} // end anonymous namespace

extern "C" GLuint LoadKtxTexture(const uint8_t *data, size_t dataSize) {
    if (dataSize < sizeof(KtxHeader)) return 0;
    const KtxHeader *hdr = reinterpret_cast<const KtxHeader *>(data);

    static const uint8_t KTX_MAGIC[12] = {
        0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
    };
    if (std::memcmp(hdr->identifier, KTX_MAGIC, 12) != 0) return 0;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const bool compressed = (hdr->glType == 0 && hdr->glFormat == 0);
    const uint8_t *ptr = data + sizeof(KtxHeader) + hdr->bytesOfKeyValueData;
    uint32_t w = hdr->pixelWidth, h = hdr->pixelHeight;
    uint32_t levels = hdr->numberOfMipmapLevels;
    if (levels == 0) levels = 1;

    GLES_LOGI("KTX %ux%u internalFmt=0x%04X fmt=0x%04X type=0x%04X compressed=%d levels=%u",
              w, h, hdr->glInternalFormat, hdr->glFormat, hdr->glType, compressed, levels);
    spdlog::info("krkrgles: KTX {}x{} internalFmt=0x{:04X} fmt=0x{:04X} type=0x{:04X} compressed={} levels={}",
                 w, h, hdr->glInternalFormat, hdr->glFormat, hdr->glType, compressed, levels);

    enum DecodeMethod { DECODE_NONE, DECODE_ETC2, DECODE_BPTC };
    DecodeMethod decodeMeth = DECODE_NONE;

    if (compressed && IsBPTCFormat(hdr->glInternalFormat) &&
        !IsCompressedFormatSupported(hdr->glInternalFormat)) {
        GLES_LOGI("BPTC format 0x%04X not supported by GPU -> BC7 software decode",
                  hdr->glInternalFormat);
        decodeMeth = DECODE_BPTC;
    } else if (compressed && IsBPTCFormat(hdr->glInternalFormat)) {
        GLES_LOGI("BPTC format 0x%04X supported by GPU -> hardware path",
                  hdr->glInternalFormat);
    } else if (compressed && IsETC2Format(hdr->glInternalFormat) &&
               !IsCompressedFormatSupported(hdr->glInternalFormat)) {
        GLES_LOGI("ETC2 format 0x%04X not supported by GPU -> software decode (withAlpha=%d)",
                  hdr->glInternalFormat, IsETC2WithAlpha(hdr->glInternalFormat));
        decodeMeth = DECODE_ETC2;
    } else if (compressed && IsETC2Format(hdr->glInternalFormat)) {
        GLES_LOGI("ETC2 format 0x%04X supported by GPU -> hardware path",
                  hdr->glInternalFormat);
    } else if (compressed && !IsCompressedFormatSupported(hdr->glInternalFormat)) {
        GLES_LOGW("compressed format 0x%04X not supported, texture skipped", hdr->glInternalFormat);
        spdlog::warn("krkrgles: compressed format 0x{:04X} not supported", hdr->glInternalFormat);
        glDeleteTextures(1, &tex);
        return 0;
    }

    if (decodeMeth != DECODE_NONE) {
        uint32_t mw = w, mh = h;
        for (uint32_t level = 0; level < levels; ++level) {
            if (ptr + 4 > data + dataSize) break;
            uint32_t imageSize;
            std::memcpy(&imageSize, ptr, 4);
            ptr += 4;
            if (ptr + imageSize > data + dataSize) break;

            std::vector<uint8_t> decoded;
            bool ok = false;
            if (decodeMeth == DECODE_BPTC)
                ok = DecodeBPTC_RGBA(ptr, imageSize, mw, mh, decoded);
            else if (IsETC2WithAlpha(hdr->glInternalFormat))
                ok = DecodeETC2RGBA(ptr, imageSize, mw, mh, decoded);
            else
                ok = DecodeETC2RGB(ptr, imageSize, mw, mh, decoded);

            if (ok) {
                glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), GL_RGBA,
                             static_cast<GLsizei>(mw), static_cast<GLsizei>(mh),
                             0, GL_RGBA, GL_UNSIGNED_BYTE, decoded.data());
                GLES_LOGI("  level %u: %ux%u decoded OK (dataSize=%u decodedSize=%zu)",
                          level, mw, mh, imageSize, decoded.size());
            } else {
                GLES_LOGW("  level %u: %ux%u decode FAILED (dataSize=%u)", level, mw, mh, imageSize);
            }
            ptr += (imageSize + 3) & ~3u;
            mw = (mw > 1) ? mw / 2 : 1;
            mh = (mh > 1) ? mh / 2 : 1;
        }
    } else {
        uint32_t mw = w, mh = h;
        for (uint32_t level = 0; level < levels; ++level) {
            if (ptr + 4 > data + dataSize) break;
            uint32_t imageSize;
            std::memcpy(&imageSize, ptr, 4);
            ptr += 4;
            if (ptr + imageSize > data + dataSize) break;

            if (compressed) {
                glCompressedTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level),
                                      hdr->glInternalFormat,
                                      static_cast<GLsizei>(mw), static_cast<GLsizei>(mh),
                                      0, static_cast<GLsizei>(imageSize), ptr);
            } else {
                glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level),
                             static_cast<GLint>(hdr->glInternalFormat),
                             static_cast<GLsizei>(mw), static_cast<GLsizei>(mh),
                             0, hdr->glFormat, hdr->glType, ptr);
            }
            ptr += (imageSize + 3) & ~3u;
            mw = (mw > 1) ? mw / 2 : 1;
            mh = (mh > 1) ? mh / 2 : 1;
        }
    }

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        GLES_LOGW("KTX upload GL error 0x%04X, deleting texture", err);
        spdlog::warn("krkrgles: KTX upload GL error 0x{:04X}", err);
        glDeleteTextures(1, &tex);
        return 0;
    }

    GLES_LOGI("KTX texture loaded OK: texId=%u", tex);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// ---------------------------------------------------------------------------
// PNG texture loader — decode from memory, upload as RGBA8
// ---------------------------------------------------------------------------
namespace {
struct PngMemReader { const uint8_t *data; size_t size; size_t offset; };
static void pngReadMemory(png_structp png, png_bytep out, png_size_t count) {
    auto *r = static_cast<PngMemReader *>(png_get_io_ptr(png));
    if (r->offset + count > r->size) { png_error(png, "read past end"); return; }
    std::memcpy(out, r->data + r->offset, count);
    r->offset += count;
}
} // anon

extern "C" GLuint LoadPngTexture(const uint8_t *data, size_t dataSize) {
    if (dataSize < 8 || png_sig_cmp(data, 0, 8) != 0) return 0;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) return 0;
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, nullptr, nullptr); return 0; }

    std::vector<uint8_t> pixels;
    uint32_t w = 0, h = 0;

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        return 0;
    }

    PngMemReader reader{data, dataSize, 0};
    png_set_read_fn(png, &reader, pngReadMemory);
    png_read_info(png, info);

    w = png_get_image_width(png, info);
    h = png_get_image_height(png, info);
    png_byte colorType = png_get_color_type(png, info);
    png_byte bitDepth  = png_get_bit_depth(png, info);

    if (bitDepth == 16) png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY ||
        colorType == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    png_read_update_info(png, info);

    size_t rowBytes = png_get_rowbytes(png, info);
    pixels.resize(rowBytes * h);
    std::vector<png_bytep> rows(h);
    for (uint32_t y = 0; y < h; ++y) rows[y] = pixels.data() + y * rowBytes;
    png_read_image(png, rows.data());
    png_destroy_read_struct(&png, &info, nullptr);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        GLES_LOGW("PNG upload GL error 0x%04X", err);
        glDeleteTextures(1, &tex);
        return 0;
    }

    GLES_LOGI("PNG texture loaded OK: %ux%u texId=%u", w, h, tex);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

namespace {

// ---------------------------------------------------------------------------
// FBO manager — offscreen render target for Live2D
// ---------------------------------------------------------------------------
class OffscreenFBO {
public:
    bool EnsureSize(GLsizei w, GLsizei h) {
        if (fbo_ && w == width_ && h == height_) return true;
        Destroy();
        width_ = w;
        height_ = h;

        glGenFramebuffers(1, &fbo_);
        glGenTextures(1, &colorTex_);
        glBindTexture(GL_TEXTURE_2D, colorTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, colorTex_, 0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (status != GL_FRAMEBUFFER_COMPLETE) {
            spdlog::error("krkrgles: FBO incomplete (status=0x{:04X})", status);
            Destroy();
            return false;
        }
        spdlog::debug("krkrgles: FBO created {}x{}", w, h);
        return true;
    }

    void Bind(GLint currentFbo = -1) {
        if (!fbo_) return;
        if (currentFbo >= 0)
            prevFbo_ = currentFbo;
        else
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, width_, height_);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void Unbind() {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo_));
    }

    GLint GetPrevFbo() const { return prevFbo_; }

    bool ReadPixels(std::vector<uint8_t> &buf) {
        if (!fbo_) return false;
        buf.resize(static_cast<size_t>(width_) * height_ * 4);
        GLint prev;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev));
        return true;
    }

    void Destroy() {
        if (colorTex_) { glDeleteTextures(1, &colorTex_); colorTex_ = 0; }
        if (fbo_) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
        width_ = height_ = 0;
    }

    GLuint GetFBO() const { return fbo_; }
    GLsizei GetWidth() const { return width_; }
    GLsizei GetHeight() const { return height_; }

    ~OffscreenFBO() { Destroy(); }

private:
    GLuint fbo_ = 0;
    GLuint colorTex_ = 0;
    GLsizei width_ = 0, height_ = 0;
    GLint prevFbo_ = 0;
};

} // end anonymous namespace (for exported symbols below)

// ---------------------------------------------------------------------------
// Find the first Layer object among the callback parameters.
// The game may pass (intFlag, layer, ...) instead of (layer).
// ---------------------------------------------------------------------------
static iTJSDispatch2 *FindLayerInParams(tjs_int n, tTJSVariant **p) {
    if (!p) return nullptr;
    for (tjs_int i = 0; i < n; ++i) {
        if (p[i] && p[i]->Type() == tvtObject) {
            iTJSDispatch2 *obj = p[i]->AsObjectNoAddRef();
            if (!obj) continue;
            tTJSVariant test;
            if (TJS_SUCCEEDED(obj->PropGet(0, TJS_W("imageWidth"), nullptr, &test, obj)))
                return obj;
        }
    }
    return nullptr;
}

static bool IsNumericVariant(const tTJSVariant &value) {
    tTJSVariantType type = value.Type();
    return type != tvtVoid && type != tvtString && type != tvtObject;
}

static void LogDrawCompatArgs(const char *name, tjs_int n, tTJSVariant **p) {
    static std::unordered_map<std::string, int> counts;
    int &count = counts[name ? name : "unknown"];
    if (count >= 40) return;
    ++count;

    std::string message = std::string("krkrgles: ") + (name ? name : "draw") +
                          " args #" + std::to_string(count) +
                          " count=" + std::to_string(n);
    for (tjs_int i = 0; i < n; ++i) {
        message += " [";
        message += std::to_string(i);
        if (!p || !p[i]) {
            message += ":null]";
            continue;
        }
        message += ":t";
        message += std::to_string(static_cast<int>(p[i]->Type()));
        if (IsNumericVariant(*p[i])) {
            message += "=";
            message += std::to_string(static_cast<long long>((tjs_int64)*p[i]));
        } else if (p[i]->Type() == tvtString || p[i]->Type() == tvtOctet) {
            std::string s = ttstr(*p[i]).AsStdString();
            if (s.size() > 48) s.resize(48);
            message += "=\"";
            message += s;
            message += "\"";
        } else if (p[i]->Type() == tvtObject) {
            iTJSDispatch2 *obj = p[i]->AsObjectNoAddRef();
            tTJSVariant width, height;
            if (obj &&
                TJS_SUCCEEDED(obj->PropGet(0, TJS_W("imageWidth"), nullptr,
                                           &width, obj)) &&
                TJS_SUCCEEDED(obj->PropGet(0, TJS_W("imageHeight"), nullptr,
                                           &height, obj))) {
                message += "=Layer(";
                message += std::to_string(static_cast<long long>((tjs_int64)width));
                message += "x";
                message += std::to_string(static_cast<long long>((tjs_int64)height));
                message += ")";
            } else {
                message += "=object";
            }
        }
        message += "]";
    }
    spdlog::info("{}", message);
}

static tTJSNI_BaseLayer *GetNativeLayer(iTJSDispatch2 *obj) {
    if (!obj) return nullptr;
    tTJSNI_BaseLayer *layer = nullptr;
    if (TJS_SUCCEEDED(obj->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
            reinterpret_cast<iTJSNativeInstance **>(&layer)))) {
        return layer;
    }
    return nullptr;
}

static bool GetBitmapFromObject(iTJSDispatch2 *obj, iTVPBaseBitmap **bitmap,
                                tTVPBlendOperationMode *mode) {
    if (!obj || !bitmap) return false;
    if (tTJSNI_BaseLayer *layer = GetNativeLayer(obj)) {
        *bitmap = layer->GetMainImage();
        if (mode) *mode = layer->GetOperationModeFromType();
        return *bitmap != nullptr;
    }
    tTJSNI_Bitmap *srcbmp = nullptr;
    if (TJS_SUCCEEDED(obj->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, tTJSNC_Bitmap::ClassID,
            reinterpret_cast<iTJSNativeInstance **>(&srcbmp)))) {
        *bitmap = srcbmp->GetBitmap();
        if (mode) *mode = omAlpha;
        return *bitmap != nullptr;
    }
    return false;
}

static bool GetLayerAndObjectArgs(tjs_int n, tTJSVariant **p,
                                  tTJSNI_BaseLayer **dstLayer,
                                  iTJSDispatch2 **srcObject,
                                  tjs_int *dstIndex, tjs_int *srcIndex) {
    if (!p || !dstLayer || !srcObject) return false;
    *dstLayer = nullptr;
    *srcObject = nullptr;
    if (dstIndex) *dstIndex = -1;
    if (srcIndex) *srcIndex = -1;
    for (tjs_int i = 0; i < n; ++i) {
        if (!p[i] || p[i]->Type() != tvtObject) continue;
        iTJSDispatch2 *obj = p[i]->AsObjectNoAddRef();
        if (!obj) continue;
        if (!*dstLayer) {
            if (tTJSNI_BaseLayer *layer = GetNativeLayer(obj)) {
                *dstLayer = layer;
                if (dstIndex) *dstIndex = i;
                continue;
            }
        }
        if (!*srcObject) {
            iTVPBaseBitmap *bitmap = nullptr;
            if (GetBitmapFromObject(obj, &bitmap, nullptr)) {
                *srcObject = obj;
                if (srcIndex) *srcIndex = i;
            }
        }
    }
    return *dstLayer && *srcObject;
}

static bool GetNumericArgs(tjs_int n, tTJSVariant **p, tjs_int start,
                           std::vector<tjs_real> &out) {
    out.clear();
    for (tjs_int i = start; i < n; ++i) {
        if (!p[i] || !IsNumericVariant(*p[i])) continue;
        out.push_back(static_cast<tjs_real>(*p[i]));
    }
    return !out.empty();
}

static bool DrawLayerNative(tjs_int n, tTJSVariant **p) {
    tTJSNI_BaseLayer *dstLayer = nullptr;
    iTJSDispatch2 *srcObject = nullptr;
    tjs_int dstIndex = -1;
    tjs_int srcIndex = -1;
    if (!GetLayerAndObjectArgs(n, p, &dstLayer, &srcObject, &dstIndex,
                               &srcIndex)) {
        return false;
    }

    iTVPBaseBitmap *src = nullptr;
    tTVPBlendOperationMode automode = omAlpha;
    if (!GetBitmapFromObject(srcObject, &src, &automode)) return false;
    const tjs_int firstNumeric =
        srcIndex > dstIndex ? dstIndex + 1 : srcIndex + 1;
    std::vector<tjs_real> nums;
    GetNumericArgs(n, p, firstNumeric, nums);

    tjs_int dx = nums.size() > 0 ? static_cast<tjs_int>(nums[0]) : 0;
    tjs_int dy = nums.size() > 1 ? static_cast<tjs_int>(nums[1]) : 0;
    tjs_int dw = nums.size() > 2 ? static_cast<tjs_int>(nums[2])
                                 : dstLayer->GetWidth();
    tjs_int dh = nums.size() > 3 ? static_cast<tjs_int>(nums[3])
                                 : dstLayer->GetHeight();
    tjs_int sx = nums.size() > 4 ? static_cast<tjs_int>(nums[4]) : 0;
    tjs_int sy = nums.size() > 5 ? static_cast<tjs_int>(nums[5]) : 0;
    tjs_int sw = nums.size() > 6 ? static_cast<tjs_int>(nums[6]) : src->GetWidth();
    tjs_int sh = nums.size() > 7 ? static_cast<tjs_int>(nums[7]) : src->GetHeight();

    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return false;

    tTVPRect dest(dx, dy, dx + dw, dy + dh);
    tTVPRect srcrect(sx, sy, sx + sw, sy + sh);
    try {
        dstLayer->StretchCopy(dest, src, srcrect, stNearest, 0.0);
        return true;
    } catch (...) {
        return false;
    }
}

static bool DrawAffineNative(tjs_int n, tTJSVariant **p) {
    tTJSNI_BaseLayer *dstLayer = nullptr;
    iTJSDispatch2 *srcObject = nullptr;
    tjs_int dstIndex = -1;
    tjs_int srcIndex = -1;
    if (!GetLayerAndObjectArgs(n, p, &dstLayer, &srcObject, &dstIndex,
                               &srcIndex)) {
        return false;
    }

    iTVPBaseBitmap *src = nullptr;
    tTVPBlendOperationMode automode = omAlpha;
    if (!GetBitmapFromObject(srcObject, &src, &automode)) return false;
    const tjs_int firstNumeric =
        srcIndex > dstIndex ? dstIndex + 1 : srcIndex + 1;
    std::vector<tjs_real> nums;
    GetNumericArgs(n, p, firstNumeric, nums);
    if (nums.size() < 12) return false;

    tTVPRect srcrect(static_cast<tjs_int>(nums[0]),
                     static_cast<tjs_int>(nums[1]),
                     static_cast<tjs_int>(nums[0] + nums[2]),
                     static_cast<tjs_int>(nums[1] + nums[3]));
    if (srcrect.get_width() <= 0 || srcrect.get_height() <= 0) return false;

    tTVPBlendOperationMode mode = automode == omAuto ? omAlpha : automode;
    tjs_int opa = nums.size() > 13 ? static_cast<tjs_int>(nums[13]) : 255;
    tTVPBBStretchType type = nums.size() > 14
                                 ? static_cast<tTVPBBStretchType>(
                                       static_cast<tjs_int>(nums[14]))
                                 : stNearest;

    try {
        const bool matrixMode = static_cast<tjs_int>(nums[4]) != 0;
        if (matrixMode) {
            t2DAffineMatrix mat;
            mat.a = nums[5];
            mat.b = nums[6];
            mat.c = nums[7];
            mat.d = nums[8];
            mat.tx = nums[9];
            mat.ty = nums[10];
            dstLayer->OperateAffine(mat, src, srcrect, mode, opa, type);
        } else {
            tTVPPointD points[3];
            points[0].x = nums[5];
            points[0].y = nums[6];
            points[1].x = nums[7];
            points[1].y = nums[8];
            points[2].x = nums[9];
            points[2].y = nums[10];
            dstLayer->OperateAffine(points, src, srcrect, mode, opa, type);
        }
        return true;
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// GPU fast path: blit FBO → Layer's native GL texture via glBlitFramebuffer.
// Returns true if GPU path was used, false if not available.
// Uses native C++ calls instead of TJS dispatch for performance.
// ---------------------------------------------------------------------------
static bool CopyFBOToLayerGPU(GLuint srcFbo, GLsizei srcW, GLsizei srcH,
                              tTJSNI_Layer *layerNI, GLint prevFbo) {
    tTVPBaseTexture *img = layerNI->GetMainImage();
    if (!img) return false;
    auto *tex = img->GetTexture();
    if (!tex) return false;

    GLuint layerTexId = tex->GetNativeGLTextureId();
    if (layerTexId == 0) return false;

    GLsizei intW = static_cast<GLsizei>(tex->GetInternalWidth());
    GLsizei intH = static_cast<GLsizei>(tex->GetInternalHeight());
    if (intW <= 0 || intH <= 0) return false;

    GLsizei layerW = static_cast<GLsizei>(layerNI->GetWidth());
    GLsizei layerH = static_cast<GLsizei>(layerNI->GetHeight());

    static GLuint s_dstFbo = 0;
    static GLuint s_lastAttachedTex = 0;

    if (!s_dstFbo) glGenFramebuffers(1, &s_dstFbo);

    if (layerTexId != s_lastAttachedTex) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_dstFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, layerTexId, 0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            s_lastAttachedTex = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
            return false;
        }
        s_lastAttachedTex = layerTexId;
    } else {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_dstFbo);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);

    GLsizei blitW = (layerW < srcW) ? layerW : srcW;
    GLsizei blitH = (layerH < srcH) ? layerH : srcH;

#if defined(__ANDROID__)
    // Android: no Y flip so Live2D appears right-side up
    glBlitFramebuffer(0, 0, blitW, blitH, 0, 0, blitW, blitH,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
#else
    glBlitFramebuffer(0, 0, blitW, blitH,
                      0, blitH, blitW, 0,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));

    tex->InvalidatePixelCache();
    layerNI->SetImageModified(true);

    tTVPRect rc(0, 0, blitW, blitH);
    layerNI->Update(rc);
    return true;
}

// ---------------------------------------------------------------------------
// Convert RGBA (bottom-up) source buffer to BGRA (top-down) layer buffer.
// ---------------------------------------------------------------------------
static void ConvertRGBA_to_BGRA(const uint8_t *src, GLsizei srcW, GLsizei srcH,
                                uint8_t *dst, tjs_int pitch,
                                tjs_int copyW, tjs_int copyH,
                                bool premultiplyAlpha) {
    if (premultiplyAlpha) {
        for (tjs_int y = 0; y < copyH; ++y) {
#if defined(__ANDROID__)
            const size_t srcRowIdx = static_cast<size_t>(y) * srcW * 4;
#else
            const size_t srcRowIdx = static_cast<size_t>(srcH - 1 - y) * srcW * 4;
#endif
            const uint8_t *srcRow = src + srcRowIdx;
            uint8_t *dstRow = dst + static_cast<size_t>(y) * pitch;
            for (tjs_int x = 0; x < copyW; ++x) {
                const uint32_t r = srcRow[x * 4 + 0];
                const uint32_t g = srcRow[x * 4 + 1];
                const uint32_t b = srcRow[x * 4 + 2];
                const uint32_t a = srcRow[x * 4 + 3];
                dstRow[x * 4 + 0] = static_cast<uint8_t>((b * a + 127) / 255);
                dstRow[x * 4 + 1] = static_cast<uint8_t>((g * a + 127) / 255);
                dstRow[x * 4 + 2] = static_cast<uint8_t>((r * a + 127) / 255);
                dstRow[x * 4 + 3] = static_cast<uint8_t>(a);
            }
        }
        return;
    }
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    const int simdWidth = 16;
    const int simdCopyW = copyW & ~(simdWidth - 1);
#endif
    for (tjs_int y = 0; y < copyH; ++y) {
#if defined(__ANDROID__)
        const size_t srcRowIdx = static_cast<size_t>(y) * srcW * 4;  // no Y flip on Android
#else
        const size_t srcRowIdx = static_cast<size_t>(srcH - 1 - y) * srcW * 4;
#endif
        const uint8_t *srcRow = src + srcRowIdx;
        uint8_t *dstRow = dst + static_cast<size_t>(y) * pitch;
        tjs_int x = 0;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        for (; x < simdCopyW; x += simdWidth) {
            uint8x16x4_t px = vld4q_u8(srcRow + x * 4);
            uint8x16_t tmp = px.val[0];
            px.val[0] = px.val[2];
            px.val[2] = tmp;
            vst4q_u8(dstRow + x * 4, px);
        }
#elif defined(__SSSE3__)
        const __m128i shuffleMask = _mm_setr_epi8(
            2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);
        for (; x + 4 <= copyW; x += 4) {
            __m128i v = _mm_loadu_si128(
                reinterpret_cast<const __m128i *>(srcRow + x * 4));
            v = _mm_shuffle_epi8(v, shuffleMask);
            _mm_storeu_si128(reinterpret_cast<__m128i *>(dstRow + x * 4), v);
        }
#endif
        for (; x < copyW; ++x) {
            dstRow[x * 4 + 0] = srcRow[x * 4 + 2]; // B
            dstRow[x * 4 + 1] = srcRow[x * 4 + 1]; // G
            dstRow[x * 4 + 2] = srcRow[x * 4 + 0]; // R
            dstRow[x * 4 + 3] = srcRow[x * 4 + 3]; // A
        }
    }
}

// ---------------------------------------------------------------------------
// PBO double-buffer state for async glReadPixels.
// Uses two PBOs: one receives the current frame's async read while the
// other provides the previous frame's data via glMapBufferRange.
// Introduces one frame of latency but eliminates GPU pipeline stalls.
// ---------------------------------------------------------------------------
struct PBOState {
    GLuint pbo[2] = {};
    int idx = 0;
    GLsizei w = 0, h = 0;
    bool primed = false;
    bool disabled = false;

    bool EnsureSize(GLsizei newW, GLsizei newH) {
        if (disabled) return false;
        if (newW == w && newH == h && pbo[0]) return true;
        if (pbo[0]) { glDeleteBuffers(2, pbo); pbo[0] = pbo[1] = 0; }
        while (glGetError() != GL_NO_ERROR) {}
        glGenBuffers(2, pbo);
        size_t sz = static_cast<size_t>(newW) * newH * 4;
        for (int i = 0; i < 2; ++i) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(sz),
                         nullptr, GL_STREAM_READ);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        if (glGetError() != GL_NO_ERROR) {
            if (pbo[0]) glDeleteBuffers(2, pbo);
            pbo[0] = pbo[1] = 0;
            disabled = true;
            GLES_LOGW("PBO creation failed, falling back to sync glReadPixels");
            return false;
        }
        w = newW; h = newH; primed = false;
        return true;
    }
};

// ---------------------------------------------------------------------------
// CPU fallback: read pixels from GL FBO → TJS Layer bitmap.
// GL outputs RGBA bottom-up; krkr2 Layer CPU buffer uses BGRA top-down.
// Uses PBO double-buffering when available (GLES 3.0+) to avoid stalls.
// ---------------------------------------------------------------------------
static bool CopyFBOToLayerCPU(GLuint fbo, GLsizei srcW, GLsizei srcH,
                              tTJSNI_Layer *layerNI, GLint prevFbo,
                              bool premultiplyAlpha) {
    tjs_int layerW = static_cast<tjs_int>(layerNI->GetWidth());
    tjs_int layerH = static_cast<tjs_int>(layerNI->GetHeight());
    auto *dst = reinterpret_cast<uint8_t *>(
        layerNI->GetMainImagePixelBufferForWrite());
    tjs_int pitch = layerNI->GetMainImagePixelBufferPitch();
    if (!dst || layerW <= 0 || layerH <= 0) return false;

    tjs_int copyW = (layerW < srcW) ? layerW : srcW;
    tjs_int copyH = (layerH < srcH) ? layerH : srcH;

    static PBOState s_pbo;
    const uint8_t *srcPixels = nullptr;
    bool mapped = false;

    if (s_pbo.EnsureSize(srcW, srcH)) {
        int readIdx = s_pbo.idx;
        int mapIdx  = 1 - s_pbo.idx;

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, s_pbo.pbo[readIdx]);
        glReadPixels(0, 0, srcW, srcH, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));

        if (s_pbo.primed) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, s_pbo.pbo[mapIdx]);
            srcPixels = static_cast<const uint8_t *>(
                glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
                                 static_cast<GLsizeiptr>(srcW) * srcH * 4,
                                 GL_MAP_READ_BIT));
            if (srcPixels) {
                mapped = true;
            } else {
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            }
        }

        s_pbo.primed = true;
        s_pbo.idx = 1 - s_pbo.idx;
    }

    if (!srcPixels) {
        static std::vector<uint8_t> s_rgba;
        size_t needed = static_cast<size_t>(srcW) * srcH * 4;
        if (s_rgba.size() < needed) s_rgba.resize(needed);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, srcW, srcH, GL_RGBA, GL_UNSIGNED_BYTE, s_rgba.data());
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        srcPixels = s_rgba.data();
    }

    ConvertRGBA_to_BGRA(srcPixels, srcW, srcH, dst, pitch, copyW, copyH,
                        premultiplyAlpha);

    if (mapped) {
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    tTVPRect rc(0, 0, copyW, copyH);
    layerNI->Update(rc);
    return true;
}

// ---------------------------------------------------------------------------
// Copy FBO → Layer with automatic GPU/CPU path selection.
// Resolves native Layer instance once and passes to GPU/CPU paths.
// ---------------------------------------------------------------------------
bool CopyFBOToLayer(GLuint fbo, GLsizei srcW, GLsizei srcH,
                    iTJSDispatch2 *layer, GLint prevFbo,
                    bool premultiplyAlpha = false, bool allowGpu = true) {
    if (!fbo || srcW <= 0 || srcH <= 0 || !layer) return false;
    if (prevFbo < 0) glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    tTJSNI_Layer *layerNI = nullptr;
    if (TJS_FAILED(layer->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
            tTJSNC_Layer::ClassID, (iTJSNativeInstance **)&layerNI)) || !layerNI)
        return false;

    if (allowGpu && !premultiplyAlpha &&
        CopyFBOToLayerGPU(fbo, srcW, srcH, layerNI, prevFbo)) return true;
    return CopyFBOToLayerCPU(fbo, srcW, srcH, layerNI, prevFbo,
                             premultiplyAlpha);
}

// Global registered Layer — set by entryUpdateObject, accessed by krkrlive2d.cpp
static iTJSDispatch2 *g_registeredLayer = nullptr;
iTJSDispatch2 *KrkrGLES_GetRegisteredLayer() { return g_registeredLayer; }

namespace { // reopen anonymous namespace

// ---------------------------------------------------------------------------
// TJS expression helpers
// ---------------------------------------------------------------------------
static tjs_error CreateObjectByExpression(tTJSVariant *result,
                                          const tjs_char *expr,
                                          const char *tag) {
    if (!result || !expr) return TJS_E_FAIL;
    try { TVPExecuteExpression(ttstr(expr), result); }
    catch (...) {
        spdlog::error("krkrgles: {} eval '{}' failed", tag, ttstr(expr).AsStdString());
        result->Clear();
        return TJS_E_FAIL;
    }
    if (result->Type() != tvtObject || result->AsObjectNoAddRef() == nullptr) {
        result->Clear();
        return TJS_E_FAIL;
    }
    return TJS_S_OK;
}

static void InvokeLoadIfPresent(tTJSVariant &obj, tjs_int n, tTJSVariant **p,
                                const char *tag) {
    if (n <= 0 || !p) return;
    iTJSDispatch2 *d = obj.AsObjectNoAddRef();
    if (!d) return;
    tjs_uint hint = 0;
    d->FuncCall(0, TJS_W("load"), &hint, nullptr, n, p, d);
}

// ---------------------------------------------------------------------------
// Capture-callback invoker (capture → callback → render → copyLayer)
// ---------------------------------------------------------------------------
static tjs_error InvokeCaptureCallback(const char *tag, tjs_int w, tjs_int h,
                                       tjs_int n, tTJSVariant **p) {
    if (n <= 1 || !p || !p[1] || p[1]->Type() != tvtObject) return TJS_S_OK;
    tTJSVariantClosure cb = p[1]->AsObjectClosureNoAddRef();
    if (!cb.Object) return TJS_S_OK;
    tTJSVariant target;
    if (n > 0 && p[0]) target = *p[0];
    tTJSVariant wv(w), hv(h), uv, fv;
    if (n > 2 && p[2]) uv = *p[2];
    if (n > 3 && p[3]) fv = *p[3];
    tTJSVariant *args[] = { &target, &wv, &hv, &uv, &fv };
    tjs_int argc = (n > 3) ? 5 : 4;
    return cb.FuncCall(0, nullptr, nullptr, nullptr, argc, args, nullptr);
}

// ---------------------------------------------------------------------------
// Fallback dictionary module (when Cubism adaptor unavailable)
// ---------------------------------------------------------------------------
static tjs_error ReturnTrueCb(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    if (r) *r = true; return TJS_S_OK;
}

static tjs_error ReturnFirstArgOrTrueCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, iTJSDispatch2 *) {
    if (!r) return TJS_S_OK;
    *r = (n > 0 && p) ? *p[0] : tTJSVariant(true);
    return TJS_S_OK;
}

static tjs_error DictSetScreenSizeCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, iTJSDispatch2 *obj) {
    if (obj && p) {
        if (n > 0) SetObjectProperty(obj, TJS_W("screenWidth"), *p[0]);
        if (n > 1) SetObjectProperty(obj, TJS_W("screenHeight"), *p[1]);
    }
    if (r) *r = true; return TJS_S_OK;
}

static tjs_error DictCreateModelCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, iTJSDispatch2 *) {
    tTJSVariant model;
    tjs_error er = CreateObjectByExpression(&model, TJS_W("new Live2DModel()"), "fallback.createModel");
    if (TJS_FAILED(er)) { if (r) r->Clear(); return er; }
    InvokeLoadIfPresent(model, n, p, "fallback.createModel");
    if (r) *r = model;
    return TJS_S_OK;
}

static tjs_error DictCreateMatrixCb(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    return CreateObjectByExpression(r, TJS_W("new Live2DMatrix()"), "fallback.createMatrix");
}

static tjs_error DictCreateDeviceCb(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    return CreateObjectByExpression(r, TJS_W("new Live2DDevice()"), "fallback.createDevice");
}

static tjs_error DictEntryUpdateObjectCb(tTJSVariant *r, tjs_int n,
                                         tTJSVariant **p, iTJSDispatch2 *) {
    if (n > 0 && p) {
        iTJSDispatch2 *layer = FindLayerInParams(n, p);
        if (layer) g_registeredLayer = layer;
    }
    if (r) *r = true; return TJS_S_OK;
}

static tjs_error DictDrawLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                 iTJSDispatch2 *) {
    LogDrawCompatArgs("Fallback.drawLayer", n, p);
    bool drawn = DrawLayerNative(n, p);
    if (r) *r = drawn;
    return TJS_S_OK;
}

static tjs_error DictDrawAffineCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                  iTJSDispatch2 *) {
    LogDrawCompatArgs("Fallback.drawAffine", n, p);
    bool drawn = DrawAffineNative(n, p);
    if (r) *r = drawn;
    return TJS_S_OK;
}

static tjs_error CreateFallbackModuleObject(tTJSVariant *result, tjs_int w, tjs_int h) {
    spdlog::warn("krkrgles: using fallback module ({}x{})", w, h);
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if (!dict) { if (result) result->Clear(); return TJS_E_FAIL; }
    SetObjectProperty(dict, TJS_W("screenWidth"), tTJSVariant(w));
    SetObjectProperty(dict, TJS_W("screenHeight"), tTJSVariant(h));
    SetObjectMethod(dict, TJS_W("entryUpdateObject"), DictEntryUpdateObjectCb);
    SetObjectMethod(dict, TJS_W("setScreenSize"), DictSetScreenSizeCb);
    SetObjectMethod(dict, TJS_W("makeCurrent"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("beginScene"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("endScene"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("finalize"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("render"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("glesEntry"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("glesRemove"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("capture"), ReturnFirstArgOrTrueCb);
    SetObjectMethod(dict, TJS_W("captureScreen"), ReturnFirstArgOrTrueCb);
    SetObjectMethod(dict, TJS_W("glesCapture"), ReturnFirstArgOrTrueCb);
    SetObjectMethod(dict, TJS_W("glesCaptureScreen"), ReturnFirstArgOrTrueCb);
    SetObjectMethod(dict, TJS_W("copyLayer"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("glesCopyLayer"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("drawLayer"), DictDrawLayerCb);
    SetObjectMethod(dict, TJS_W("glesDrawLayer"), DictDrawLayerCb);
    SetObjectMethod(dict, TJS_W("drawAffine"), DictDrawAffineCb);
    SetObjectMethod(dict, TJS_W("drawAffineGLES"), DictDrawAffineCb);
    SetObjectMethod(dict, TJS_W("createModel"), DictCreateModelCb);
    SetObjectMethod(dict, TJS_W("createMatrix"), DictCreateMatrixCb);
    SetObjectMethod(dict, TJS_W("createDevice"), DictCreateDeviceCb);
    SetResultObject(result, dict);
    dict->Release();
    return TJS_S_OK;
}

// ---------------------------------------------------------------------------
// GLESModule — holds per-module FBO + rendering state
// ---------------------------------------------------------------------------
class GLESModule {
public:
    GLESModule() = default;
    ~GLESModule() { fbo_.Destroy(); }

    OffscreenFBO &GetFBO() { return fbo_; }

    static tjs_error entryUpdateObjectCb(tTJSVariant *r, tjs_int n,
                                         tTJSVariant **p, GLESModule *) {
        if (n > 0 && p) {
            iTJSDispatch2 *layer = FindLayerInParams(n, p);
            if (layer) {
                g_registeredLayer = layer;
            }
        }
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error setScreenSizeCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        if (!s || !p) return TJS_S_OK;
        if (n > 0) s->screenWidth_ = ToInt(*p[0], s->screenWidth_);
        if (n > 1) s->screenHeight_ = ToInt(*p[1], s->screenHeight_);
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error makeCurrentCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error beginSceneCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *s) {
        if (s) {
            tjs_int w = NormalizeExtent(s->screenWidth_, 1920);
            tjs_int h = NormalizeExtent(s->screenHeight_, 1080);
            s->fbo_.EnsureSize(static_cast<GLsizei>(w), static_cast<GLsizei>(h));
            s->fbo_.Bind();
            s->sceneActive_ = true;
        }
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error endSceneCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *s) {
        if (s) {
            s->fbo_.Unbind();
            s->sceneActive_ = false;
        }
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error finalizeCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *s) {
        if (s) s->fbo_.Destroy();
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error captureCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        tjs_int w = NormalizeExtent(s ? s->screenWidth_ : 0, 1920);
        tjs_int h = NormalizeExtent(s ? s->screenHeight_ : 0, 1080);
        InvokeCaptureCallback("GLESModule.capture", w, h, n, p);
        if (r) *r = (n > 0 && p) ? *p[0] : tTJSVariant(true);
        return TJS_S_OK;
    }

    static tjs_error glesCaptureCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        return captureCb(r, n, p, s);
    }

    static tjs_error captureScreenCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        return captureCb(r, n, p, s);
    }

    static tjs_error glesCaptureScreenCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        return captureCb(r, n, p, s);
    }

    static tjs_error copyLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        iTJSDispatch2 *layer = FindLayerInParams(n, p);
        if (layer) {
            if (s && s->fbo_.GetFBO()) {
                GLint prevFbo = s->fbo_.GetPrevFbo();
                CopyFBOToLayer(s->fbo_.GetFBO(), s->fbo_.GetWidth(),
                               s->fbo_.GetHeight(), layer, prevFbo);
            }
            if (g_live2dRenderTarget.fbo) {
                CopyFBOToLayer(g_live2dRenderTarget.fbo,
                               g_live2dRenderTarget.width,
                               g_live2dRenderTarget.height, layer, -1,
                               true, false);
            }
        }
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error glesCopyLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        return copyLayerCb(r, n, p, s);
    }

    static tjs_error drawLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *) {
        LogDrawCompatArgs("GLESModule.drawLayer", n, p);
        bool drawn = DrawLayerNative(n, p);
        if (r) *r = drawn; return TJS_S_OK;
    }

    static tjs_error glesDrawLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        return drawLayerCb(r, n, p, s);
    }

    static tjs_error drawAffineCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *) {
        LogDrawCompatArgs("GLESModule.drawAffine", n, p);
        bool drawn = DrawAffineNative(n, p);
        if (r) *r = drawn; return TJS_S_OK;
    }

    static tjs_error drawAffineGLESCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
        return drawAffineCb(r, n, p, s);
    }

    static tjs_error renderCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error setMatrixCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error createModelCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *) {
        tTJSVariant model;
        tjs_error er = CreateObjectByExpression(&model, TJS_W("new Live2DModel()"), "createModel");
        if (TJS_FAILED(er)) { if (r) r->Clear(); return er; }
        InvokeLoadIfPresent(model, n, p, "createModel");
        if (r) *r = model;
        return TJS_S_OK;
    }

    static tjs_error createMatrixCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *) {
        return CreateObjectByExpression(r, TJS_W("new Live2DMatrix()"), "createMatrix");
    }

    static tjs_error createDeviceCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESModule *) {
        return CreateObjectByExpression(r, TJS_W("new Live2DDevice()"), "createDevice");
    }

    tjs_int getScreenWidth() const { return screenWidth_; }
    void setScreenWidth(tjs_int v) { screenWidth_ = v; }
    tjs_int getScreenHeight() const { return screenHeight_; }
    void setScreenHeight(tjs_int v) { screenHeight_ = v; }

    bool isSceneActive() const { return sceneActive_; }
    OffscreenFBO &getFBO() { return fbo_; }

private:
    tjs_int screenWidth_ = 0;
    tjs_int screenHeight_ = 0;
    bool sceneActive_ = false;
    OffscreenFBO fbo_;
};

// ---------------------------------------------------------------------------
// Module object creation
// ---------------------------------------------------------------------------
static tjs_error CreateModuleObject(tTJSVariant *result, tjs_int w = 0, tjs_int h = 0) {
    auto *mod = new GLESModule();
    mod->setScreenWidth(w);
    mod->setScreenHeight(h);
    iTJSDispatch2 *obj = ncbInstanceAdaptor<GLESModule>::CreateAdaptor(mod);
    if (!obj) { delete mod; return CreateFallbackModuleObject(result, w, h); }
    SetResultObject(result, obj);
    obj->Release();
    return TJS_S_OK;
}

extern "C" tjs_error TVPKrkrGLESCreateModuleObject(tTJSVariant *result,
                                                    tjs_int w, tjs_int h) {
    return CreateModuleObject(result, w, h);
}

// ---------------------------------------------------------------------------
// DrawDevice getModule callback
// ---------------------------------------------------------------------------
static tjs_error DrawDeviceGetModuleCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                       iTJSDispatch2 *objthis) {
    static std::unordered_map<uintptr_t, std::unordered_map<ModuleName, tTJSVariant>> s_mod;
    const ModuleName mn = NormalizeModuleName(n, p);
    const uintptr_t key = reinterpret_cast<uintptr_t>(objthis);
    if (key) {
        auto dit = s_mod.find(key);
        if (dit != s_mod.end()) {
            auto mit = dit->second.find(mn);
            if (mit != dit->second.end() && mit->second.Type() == tvtObject &&
                mit->second.AsObjectNoAddRef())
            { if (r) *r = mit->second; return TJS_S_OK; }
        }
    }
    tTJSVariant created;
    tjs_error er = CreateModuleObject(&created);
    if (TJS_FAILED(er)) { if (r) r->Clear(); return er; }
    if (key && created.Type() == tvtObject && created.AsObjectNoAddRef())
        s_mod[key][mn] = created;
    if (r) *r = created;
    return TJS_S_OK;
}

// ---------------------------------------------------------------------------
// GLESAdaptor — per-window adaptor that proxies to the module
// ---------------------------------------------------------------------------
class GLESAdaptor {
public:
    GLESAdaptor() = default;

    GLESModule *FindModule() {
        if (cachedModule_) return cachedModule_;
        tTJSVariant mv;
        if (TJS_SUCCEEDED(GLESAdaptor::getModuleCb(&mv, 0, nullptr, this))) {
            iTJSDispatch2 *obj = mv.AsObjectNoAddRef();
            if (obj) cachedModule_ = ncbInstanceAdaptor<GLESModule>::GetNativeInstance(obj);
        }
        return cachedModule_;
    }

    static tjs_error getModuleCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        static std::unordered_map<uintptr_t, std::unordered_map<ModuleName, tTJSVariant>> sm;
        const ModuleName mn = NormalizeModuleName(n, p);
        const uintptr_t key = reinterpret_cast<uintptr_t>(s);
        if (key) {
            auto dit = sm.find(key);
            if (dit != sm.end()) {
                auto mit = dit->second.find(mn);
                if (mit != dit->second.end() && mit->second.Type() == tvtObject &&
                    mit->second.AsObjectNoAddRef())
                { if (r) *r = mit->second; return TJS_S_OK; }
            }
        }
        tTJSVariant created;
        tjs_error er = s ? CreateModuleObject(&created, s->screenWidth_, s->screenHeight_)
                         : CreateModuleObject(&created);
        if (TJS_FAILED(er)) { if (r) r->Clear(); return er; }
        if (key && created.Type() == tvtObject && created.AsObjectNoAddRef())
            sm[key][mn] = created;
        if (r) *r = created;
        return TJS_S_OK;
    }

    static tjs_error setScreenSizeCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        if (!s || !p) return TJS_S_OK;
        if (n > 0) s->screenWidth_ = ToInt(*p[0], s->screenWidth_);
        if (n > 1) s->screenHeight_ = ToInt(*p[1], s->screenHeight_);
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error makeCurrentCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error beginSceneCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        auto *mod = s ? s->FindModule() : nullptr;
        if (mod) return GLESModule::beginSceneCb(r, n, p, mod);
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error endSceneCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        auto *mod = s ? s->FindModule() : nullptr;
        if (mod) return GLESModule::endSceneCb(r, n, p, mod);
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error entryUpdateObjectCb(tTJSVariant *r, tjs_int n,
                                         tTJSVariant **p, GLESAdaptor *) {
        if (n > 0 && p) {
            iTJSDispatch2 *layer = FindLayerInParams(n, p);
            if (layer) g_registeredLayer = layer;
        }
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error captureCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        tjs_int w = NormalizeExtent(s ? s->screenWidth_ : 0, 1920);
        tjs_int h = NormalizeExtent(s ? s->screenHeight_ : 0, 1080);
        InvokeCaptureCallback("GLESAdaptor.capture", w, h, n, p);
        if (r) *r = (n > 0 && p) ? *p[0] : tTJSVariant(true);
        return TJS_S_OK;
    }

    static tjs_error glesCaptureCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        return captureCb(r, n, p, s);
    }

    static tjs_error captureScreenCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        return captureCb(r, n, p, s);
    }

    static tjs_error glesCaptureScreenCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        return captureCb(r, n, p, s);
    }

    static tjs_error copyLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        auto *mod = s ? s->FindModule() : nullptr;
        if (mod) return GLESModule::copyLayerCb(r, n, p, mod);
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error glesCopyLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        return copyLayerCb(r, n, p, s);
    }

    static tjs_error drawLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *) {
        LogDrawCompatArgs("GLESAdaptor.drawLayer", n, p);
        bool drawn = DrawLayerNative(n, p);
        if (r) *r = drawn; return TJS_S_OK;
    }

    static tjs_error glesDrawLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        return drawLayerCb(r, n, p, s);
    }

    static tjs_error drawAffineCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *) {
        LogDrawCompatArgs("GLESAdaptor.drawAffine", n, p);
        bool drawn = DrawAffineNative(n, p);
        if (r) *r = drawn; return TJS_S_OK;
    }

    static tjs_error drawAffineGLESCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
        return drawAffineCb(r, n, p, s);
    }

    static tjs_error renderCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error setMatrixCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error createModelCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *) {
        tTJSVariant model;
        tjs_error er = CreateObjectByExpression(&model, TJS_W("new Live2DModel()"), "GLESAdaptor.createModel");
        if (TJS_FAILED(er)) { if (r) r->Clear(); return er; }
        InvokeLoadIfPresent(model, n, p, "GLESAdaptor.createModel");
        if (r) *r = model;
        return TJS_S_OK;
    }

    static tjs_error createMatrixCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        return CreateObjectByExpression(r, TJS_W("new Live2DMatrix()"), "GLESAdaptor.createMatrix");
    }

    static tjs_error createDeviceCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        return CreateObjectByExpression(r, TJS_W("new Live2DDevice()"), "GLESAdaptor.createDevice");
    }

    static tjs_error finalizeCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error glesEntryCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        if (r) *r = true; return TJS_S_OK;
    }

    static tjs_error glesRemoveCb(tTJSVariant *r, tjs_int, tTJSVariant **, GLESAdaptor *) {
        if (r) *r = true; return TJS_S_OK;
    }

    tjs_int getScreenWidth() const { return screenWidth_; }
    void setScreenWidth(tjs_int v) { screenWidth_ = v; }
    tjs_int getScreenHeight() const { return screenHeight_; }
    void setScreenHeight(tjs_int v) { screenHeight_ = v; }

private:
    tjs_int screenWidth_ = 0;
    tjs_int screenHeight_ = 0;
    GLESModule *cachedModule_ = nullptr;
};

// Older scripts refer to this plugin's draw device as OGLDrawDevice.
// Keep it as a thin GLESAdaptor-compatible class so it gets its own TJS
// native class registration while sharing the same implementation.
class OGLDrawDevice : public GLESAdaptor {
public:
    OGLDrawDevice() = default;

    static tjs_error getModuleCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                 OGLDrawDevice *s) {
        return GLESAdaptor::getModuleCb(r, n, p, s);
    }
    static tjs_error setScreenSizeCb(tTJSVariant *r, tjs_int n,
                                     tTJSVariant **p, OGLDrawDevice *s) {
        return GLESAdaptor::setScreenSizeCb(r, n, p, s);
    }
    static tjs_error makeCurrentCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                   OGLDrawDevice *s) {
        return GLESAdaptor::makeCurrentCb(r, n, p, s);
    }
    static tjs_error beginSceneCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                  OGLDrawDevice *s) {
        return GLESAdaptor::beginSceneCb(r, n, p, s);
    }
    static tjs_error endSceneCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                OGLDrawDevice *s) {
        return GLESAdaptor::endSceneCb(r, n, p, s);
    }
    static tjs_error entryUpdateObjectCb(tTJSVariant *r, tjs_int n,
                                         tTJSVariant **p, OGLDrawDevice *s) {
        return GLESAdaptor::entryUpdateObjectCb(r, n, p, s);
    }
    static tjs_error captureCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                               OGLDrawDevice *s) {
        return GLESAdaptor::captureCb(r, n, p, s);
    }
    static tjs_error glesCaptureCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                   OGLDrawDevice *s) {
        return GLESAdaptor::glesCaptureCb(r, n, p, s);
    }
    static tjs_error captureScreenCb(tTJSVariant *r, tjs_int n,
                                     tTJSVariant **p, OGLDrawDevice *s) {
        return GLESAdaptor::captureScreenCb(r, n, p, s);
    }
    static tjs_error glesCaptureScreenCb(tTJSVariant *r, tjs_int n,
                                         tTJSVariant **p, OGLDrawDevice *s) {
        return GLESAdaptor::glesCaptureScreenCb(r, n, p, s);
    }
    static tjs_error copyLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                 OGLDrawDevice *s) {
        return GLESAdaptor::copyLayerCb(r, n, p, s);
    }
    static tjs_error glesCopyLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                     OGLDrawDevice *s) {
        return GLESAdaptor::glesCopyLayerCb(r, n, p, s);
    }
    static tjs_error drawLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                 OGLDrawDevice *s) {
        return GLESAdaptor::drawLayerCb(r, n, p, s);
    }
    static tjs_error glesDrawLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                     OGLDrawDevice *s) {
        return GLESAdaptor::glesDrawLayerCb(r, n, p, s);
    }
    static tjs_error drawAffineCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                  OGLDrawDevice *s) {
        return GLESAdaptor::drawAffineCb(r, n, p, s);
    }
    static tjs_error drawAffineGLESCb(tTJSVariant *r, tjs_int n,
                                      tTJSVariant **p, OGLDrawDevice *s) {
        return GLESAdaptor::drawAffineGLESCb(r, n, p, s);
    }
    static tjs_error renderCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                              OGLDrawDevice *s) {
        return GLESAdaptor::renderCb(r, n, p, s);
    }
    static tjs_error setMatrixCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                 OGLDrawDevice *s) {
        return GLESAdaptor::setMatrixCb(r, n, p, s);
    }
    static tjs_error createModelCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                   OGLDrawDevice *s) {
        return GLESAdaptor::createModelCb(r, n, p, s);
    }
    static tjs_error createMatrixCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                    OGLDrawDevice *s) {
        return GLESAdaptor::createMatrixCb(r, n, p, s);
    }
    static tjs_error createDeviceCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                    OGLDrawDevice *s) {
        return GLESAdaptor::createDeviceCb(r, n, p, s);
    }
    static tjs_error finalizeCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                OGLDrawDevice *s) {
        return GLESAdaptor::finalizeCb(r, n, p, s);
    }
    static tjs_error glesEntryCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                 OGLDrawDevice *s) {
        return GLESAdaptor::glesEntryCb(r, n, p, s);
    }
    static tjs_error glesRemoveCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                  OGLDrawDevice *s) {
        return GLESAdaptor::glesRemoveCb(r, n, p, s);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// NCB Registration
// ---------------------------------------------------------------------------
static void KrkrGlesPreRegist() {}
static void KrkrGlesPostRegist() {
    if (InstallDrawDeviceScriptAliases()) {
        spdlog::info("krkrgles: installed Window draw device aliases");
    } else {
        g_drawDeviceAliasHook.Start();
        spdlog::info("krkrgles: deferred Window draw device aliases");
    }
}
NCB_PRE_REGIST_CALLBACK(KrkrGlesPreRegist);
NCB_POST_REGIST_CALLBACK(KrkrGlesPostRegist);

NCB_REGISTER_CLASS(GLESModule) {
    Constructor();
    NCB_PROPERTY(screenWidth, getScreenWidth, setScreenWidth);
    NCB_PROPERTY(screenHeight, getScreenHeight, setScreenHeight);
    NCB_METHOD_RAW_CALLBACK(entryUpdateObject, &GLESModule::entryUpdateObjectCb, 0);
    NCB_METHOD_RAW_CALLBACK(setScreenSize, &GLESModule::setScreenSizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(makeCurrent, &GLESModule::makeCurrentCb, 0);
    NCB_METHOD_RAW_CALLBACK(beginScene, &GLESModule::beginSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(endScene, &GLESModule::endSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, &GLESModule::finalizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(capture, &GLESModule::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(captureScreen, &GLESModule::captureScreenCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCapture, &GLESModule::glesCaptureCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCaptureScreen, &GLESModule::glesCaptureScreenCb, 0);
    NCB_METHOD_RAW_CALLBACK(copyLayer, &GLESModule::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCopyLayer, &GLESModule::glesCopyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawLayer, &GLESModule::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesDrawLayer, &GLESModule::glesDrawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffine, &GLESModule::drawAffineCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffineGLES, &GLESModule::drawAffineGLESCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &GLESModule::renderCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &GLESModule::setMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createModel, &GLESModule::createModelCb, 0);
    NCB_METHOD_RAW_CALLBACK(createMatrix, &GLESModule::createMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createDevice, &GLESModule::createDeviceCb, 0);
}

NCB_REGISTER_CLASS(GLESAdaptor) {
    Constructor();
    NCB_PROPERTY(screenWidth, getScreenWidth, setScreenWidth);
    NCB_PROPERTY(screenHeight, getScreenHeight, setScreenHeight);
    NCB_METHOD_RAW_CALLBACK(getModule, &GLESAdaptor::getModuleCb, 0);
    NCB_METHOD_RAW_CALLBACK(setScreenSize, &GLESAdaptor::setScreenSizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(makeCurrent, &GLESAdaptor::makeCurrentCb, 0);
    NCB_METHOD_RAW_CALLBACK(beginScene, &GLESAdaptor::beginSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(endScene, &GLESAdaptor::endSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(entryUpdateObject, &GLESAdaptor::entryUpdateObjectCb, 0);
    NCB_METHOD_RAW_CALLBACK(capture, &GLESAdaptor::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCapture, &GLESAdaptor::glesCaptureCb, 0);
    NCB_METHOD_RAW_CALLBACK(captureScreen, &GLESAdaptor::captureScreenCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCaptureScreen, &GLESAdaptor::glesCaptureScreenCb, 0);
    NCB_METHOD_RAW_CALLBACK(copyLayer, &GLESAdaptor::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCopyLayer, &GLESAdaptor::glesCopyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawLayer, &GLESAdaptor::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesDrawLayer, &GLESAdaptor::glesDrawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffine, &GLESAdaptor::drawAffineCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffineGLES, &GLESAdaptor::drawAffineGLESCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &GLESAdaptor::renderCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &GLESAdaptor::setMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createModel, &GLESAdaptor::createModelCb, 0);
    NCB_METHOD_RAW_CALLBACK(createMatrix, &GLESAdaptor::createMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createDevice, &GLESAdaptor::createDeviceCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesEntry, &GLESAdaptor::glesEntryCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesRemove, &GLESAdaptor::glesRemoveCb, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, &GLESAdaptor::finalizeCb, 0);
}

NCB_REGISTER_CLASS(OGLDrawDevice) {
    Constructor();
    NCB_PROPERTY(screenWidth, getScreenWidth, setScreenWidth);
    NCB_PROPERTY(screenHeight, getScreenHeight, setScreenHeight);
    NCB_METHOD_RAW_CALLBACK(getModule, &OGLDrawDevice::getModuleCb, 0);
    NCB_METHOD_RAW_CALLBACK(setScreenSize, &OGLDrawDevice::setScreenSizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(makeCurrent, &OGLDrawDevice::makeCurrentCb, 0);
    NCB_METHOD_RAW_CALLBACK(beginScene, &OGLDrawDevice::beginSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(endScene, &OGLDrawDevice::endSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(entryUpdateObject, &OGLDrawDevice::entryUpdateObjectCb, 0);
    NCB_METHOD_RAW_CALLBACK(capture, &OGLDrawDevice::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCapture, &OGLDrawDevice::glesCaptureCb, 0);
    NCB_METHOD_RAW_CALLBACK(captureScreen, &OGLDrawDevice::captureScreenCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCaptureScreen, &OGLDrawDevice::glesCaptureScreenCb, 0);
    NCB_METHOD_RAW_CALLBACK(copyLayer, &OGLDrawDevice::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCopyLayer, &OGLDrawDevice::glesCopyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawLayer, &OGLDrawDevice::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesDrawLayer, &OGLDrawDevice::glesDrawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffine, &OGLDrawDevice::drawAffineCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffineGLES, &OGLDrawDevice::drawAffineGLESCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &OGLDrawDevice::renderCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &OGLDrawDevice::setMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createModel, &OGLDrawDevice::createModelCb, 0);
    NCB_METHOD_RAW_CALLBACK(createMatrix, &OGLDrawDevice::createMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createDevice, &OGLDrawDevice::createDeviceCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesEntry, &OGLDrawDevice::glesEntryCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesRemove, &OGLDrawDevice::glesRemoveCb, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, &OGLDrawDevice::finalizeCb, 0);
}

NCB_ATTACH_FUNCTION_WITHTAG(getModule, WindowPassThroughDrawDevice,
                            Window.PassThroughDrawDevice, DrawDeviceGetModuleCb);
NCB_ATTACH_FUNCTION_WITHTAG(getModule, WindowBasicDrawDevice,
                            Window.BasicDrawDevice, DrawDeviceGetModuleCb);

extern "C" void TVPRegisterKrkrGLESPluginAnchor() {
    ncbAutoRegister::RegisterInternalPluginEntry(
        TJS_W("krkrgles.dll"),
        ncbAutoRegister::PreRegist,
        &ncbCallbackAutoRegister_PreRegist_KrkrGlesPreRegist_0);
    ncbAutoRegister::RegisterInternalPluginEntry(
        TJS_W("krkrgles.dll"),
        ncbAutoRegister::ClassRegist,
        &ncbNativeClassAutoRegister_GLESModule);
    ncbAutoRegister::RegisterInternalPluginEntry(
        TJS_W("krkrgles.dll"),
        ncbAutoRegister::ClassRegist,
        &ncbNativeClassAutoRegister_GLESAdaptor);
    ncbAutoRegister::RegisterInternalPluginEntry(
        TJS_W("krkrgles.dll"),
        ncbAutoRegister::ClassRegist,
        &ncbNativeClassAutoRegister_OGLDrawDevice);
    ncbAutoRegister::RegisterInternalPluginEntry(
        TJS_W("krkrgles.dll"),
        ncbAutoRegister::PostRegist,
        &ncbCallbackAutoRegister_PostRegist_KrkrGlesPostRegist_0);
}
