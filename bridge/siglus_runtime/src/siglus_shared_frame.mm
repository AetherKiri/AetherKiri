#include "siglus_shared_frame.h"
#include "siglus_ffi.h"
#include "GodotGpuBridge.h"
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#include <array>
#include <cstdlib>
#include <cstring>

namespace aetherkiri::siglus {
struct SharedFrame::Impl {
    struct Target {
        CVPixelBufferRef buffer = nullptr;
        id<MTLTexture> native = nil;
        uint64_t godot = 0;
        uint64_t serial = 0;
        bool used = false;
    };
    std::array<Target, 3> targets{};
    uint32_t width = 0, height = 0;
    int active = -1, previous_active = -1, presented = -1;
    uint64_t serial = 0;
    void *runtime = nullptr;
    bool unavailable = false;

    void release() {
        const auto *bridge = TVPGodotGpuBridgeGet();
        for (auto &target : targets) {
            if (target.godot != 0 && bridge != nullptr && bridge->release_texture != nullptr)
                bridge->release_texture(target.godot);
            if (target.native != nil) [target.native release];
            if (target.buffer != nullptr) CVPixelBufferRelease(target.buffer);
            target = {};
        }
        width = height = 0;
        active = presented = -1;
        previous_active = -1;
        serial = 0;
    }
};

SharedFrame::SharedFrame() : impl_(new Impl) {}
SharedFrame::~SharedFrame() { impl_->release(); }

void SharedFrame::reset(void *runtime) {
    siglus_ak_clear_metal_targets(runtime);
    impl_->release();
    impl_->unavailable = false;
}

bool SharedFrame::begin(void *runtime, uint32_t width, uint32_t height) {
    @autoreleasepool {
        auto &state = *impl_;
        state.runtime = runtime;
        if (state.unavailable) return false;
        const char *override = std::getenv("SIGLUS_SHARED_METAL");
        if (override != nullptr && std::strcmp(override, "0") == 0) return false;
        const auto *external = TVPGodotGpuExternalTextureGet();
        const auto *bridge = TVPGodotGpuBridgeGet();
        if (external == nullptr || external->import_apple_pixel_buffer == nullptr ||
            external->prepare_for_native_write == nullptr || bridge == nullptr ||
            bridge->release_texture == nullptr) return false;
        if (state.width != width || state.height != height) {
            reset(runtime);
            id<MTLDevice> device = (__bridge id<MTLDevice>)siglus_ak_metal_device(runtime);
            if (device == nil) { state.unavailable = true; return false; }
            CVMetalTextureCacheRef cache = nullptr;
            bool ok = CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device,
                nullptr, &cache) == kCVReturnSuccess && cache != nullptr;
            NSDictionary *attributes = @{
                (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
                (id)kCVPixelBufferMetalCompatibilityKey: @YES,
            };
            NSDictionary *texture_attributes = @{
                (id)kCVMetalTextureUsage: @(MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead),
            };
            for (auto &target : state.targets) {
                if (!ok) break;
                ok = CVPixelBufferCreate(kCFAllocatorDefault, width, height,
                    kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef)attributes,
                    &target.buffer) == kCVReturnSuccess && target.buffer != nullptr;
                if (!ok) break;
                // create_texture_from_hal requires initialized storage.
                ok = CVPixelBufferLockBaseAddress(target.buffer, 0) == kCVReturnSuccess;
                if (!ok) break;
                void *pixels = CVPixelBufferGetBaseAddress(target.buffer);
                if (pixels != nullptr)
                    std::memset(pixels, 0, CVPixelBufferGetBytesPerRow(target.buffer) * height);
                else ok = false;
                CVPixelBufferUnlockBaseAddress(target.buffer, 0);
                if (!ok) break;
                CVMetalTextureRef wrapped = nullptr;
                ok = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, cache,
                    target.buffer, (__bridge CFDictionaryRef)texture_attributes, MTLPixelFormatBGRA8Unorm, width, height,
                    0, &wrapped) == kCVReturnSuccess && wrapped != nullptr;
                if (ok) target.native = [CVMetalTextureGetTexture(wrapped) retain];
                if (wrapped != nullptr) CFRelease(wrapped);
                if (!ok || target.native == nil) { ok = false; break; }
                target.godot = external->import_apple_pixel_buffer(target.buffer, width, height);
                if (target.godot == 0) { ok = false; break; }
            }
            if (cache != nullptr) CFRelease(cache);
            if (!ok) {
                state.release();
                state.unavailable = true;
                return false;
            }
            state.width = width;
            state.height = height;
        }
        state.previous_active = state.active;
        state.active = -1;
        for (size_t index = 0; index < state.targets.size(); ++index) {
            auto &target = state.targets[index];
            // The currently exposed texture may be sampled by future Godot
            // commands, even after the prior frame's queue marker completed.
            if (static_cast<int>(index) == state.presented) continue;
            // Both consumers must have retired the surface before Rust can
            // select it again: Godot's external texture bridge may still be
            // sampling it, and the producer WGPU queue may still be writing
            // it. Checking only the former can race a just-submitted target
            // when all three IOSurfaces are in flight.
            if (target.used && (!external->prepare_for_native_write(target.godot) ||
                siglus_ak_metal_target_ready(runtime, (__bridge void *)target.native) == 0)) continue;
            if (siglus_ak_select_metal_target(runtime, (__bridge void *)target.native,
                    width, height) < 0) {
                reset(runtime);
                state.unavailable = true;
                return false;
            }
            state.active = static_cast<int>(index);
            return true;
        }
        // Keep the last published texture; simulation continues offscreen.
        return siglus_ak_select_metal_target(runtime, nullptr, width, height) >= 0;
    }
}

bool SharedFrame::publish(uint64_t serial) {
    auto &state = *impl_;
    if (state.active < 0) return state.presented >= 0;
    auto &current = state.targets[state.active];
    current.serial = serial;
    // Reserve the target even before its producer completion callback fires;
    // otherwise the next begin() could select and overwrite this in-flight
    // IOSurface when no previously published frame is available yet.
    current.used = true;
    // The current submission normally is still pending. Promote an older
    // completed target instead, so the provider can keep the shared path
    // enabled without exposing partially rendered pixels or falling back to a
    // blocking CPU readback. The next begin() records the current target as
    // previous_active before choosing another surface.
    int publish_index = state.active;
    if (siglus_ak_metal_target_ready(state.runtime,
            (__bridge void *)current.native) == 0) {
        publish_index = -1;
        if (state.previous_active >= 0 &&
            siglus_ak_metal_target_ready(state.runtime,
                (__bridge void *)state.targets[state.previous_active].native) != 0) {
            publish_index = state.previous_active;
        }
    }
    if (publish_index < 0) return state.presented >= 0;
    auto &target = state.targets[publish_index];
    const auto *external = TVPGodotGpuExternalTextureGet();
    if (external == nullptr || (external->publish_native_write != nullptr &&
        !external->publish_native_write(target.godot))) return false;
    target.used = true;
    // `publish_index` may be the previous completed target when the current
    // WGPU submission is still in flight. Expose the target actually handed
    // to Godot, never the unready current target.
    state.presented = publish_index;
    state.serial = target.serial;
    return true;
}

bool SharedFrame::get(uint64_t *texture, uint32_t *width, uint32_t *height, uint64_t *serial) const {
    const auto &state = *impl_;
    if (state.presented < 0) return false;
    *texture = state.targets[state.presented].godot;
    *width = state.width; *height = state.height; *serial = state.serial;
    return true;
}
}
