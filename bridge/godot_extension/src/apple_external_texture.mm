#include "apple_external_texture.h"

#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>

#include <dlfcn.h>
#include <mutex>

namespace {

id<MTLCommandQueue> ResolveMetalCommandQueue(uint64_t command_queue) {
  if (command_queue == 0) return nil;
#if defined(IOS_ENABLED)
  // Godot's iOS export uses RenderingDeviceDriverMetal, so
  // DRIVER_RESOURCE_COMMAND_QUEUE is already an id<MTLCommandQueue>. Passing
  // it through vkGetMTLCommandQueueMVK corrupts the value (and previously
  // produced the invalid 0x4 receiver seen on device).
  return (__bridge id<MTLCommandQueue>)(
      reinterpret_cast<void *>(command_queue));
#else
  void *native_queue = reinterpret_cast<void *>(command_queue);
  using GetMetalCommandQueueMvk = void (*)(void *, void **);
  auto get_metal_queue = reinterpret_cast<GetMetalCommandQueueMvk>(
      dlsym(RTLD_DEFAULT, "vkGetMTLCommandQueueMVK"));
  if (get_metal_queue == nullptr) return nil;
  get_metal_queue(native_queue, &native_queue);
  return (__bridge id<MTLCommandQueue>)native_queue;
#endif
}

}  // namespace

uint64_t AetherAppleCreateMetalTextureFromPixelBuffer(
    uint64_t metal_device, void *pixel_buffer, uint32_t width,
    uint32_t height) {
  if (metal_device == 0 || pixel_buffer == nullptr || width == 0 ||
      height == 0) {
    return 0;
  }
  id<MTLDevice> device = (__bridge id<MTLDevice>)(
      reinterpret_cast<void *>(metal_device));
  CVPixelBufferRef buffer = static_cast<CVPixelBufferRef>(pixel_buffer);
  CVMetalTextureCacheRef cache = nullptr;
  if (CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device,
                                nullptr, &cache) != kCVReturnSuccess ||
      cache == nullptr) {
    return 0;
  }
  CVMetalTextureRef wrapped = nullptr;
  const CVReturn result = CVMetalTextureCacheCreateTextureFromImage(
      kCFAllocatorDefault, cache, buffer, nullptr,
      MTLPixelFormatBGRA8Unorm, width, height, 0, &wrapped);
  id<MTLTexture> texture = result == kCVReturnSuccess && wrapped != nullptr
                               ? CVMetalTextureGetTexture(wrapped)
                               : nil;
  if (texture != nil) [texture retain];
  if (wrapped != nullptr) CFRelease(wrapped);
  CFRelease(cache);
  return reinterpret_cast<uint64_t>((__bridge void *)texture);
}

void AetherAppleReleaseMetalTexture(uint64_t metal_texture) {
  if (metal_texture == 0) return;
  id<MTLTexture> texture = (__bridge id<MTLTexture>)(
      reinterpret_cast<void *>(metal_texture));
  [texture release];
}

void AetherAppleRetainPixelBuffer(void *pixel_buffer) {
  if (pixel_buffer != nullptr) CFRetain(pixel_buffer);
}

void AetherAppleReleasePixelBuffer(void *pixel_buffer) {
  if (pixel_buffer != nullptr) CFRelease(pixel_buffer);
}

bool AetherApplePollMetalCommandQueue(uint64_t metal_command_queue) {
  id<MTLCommandQueue> queue = ResolveMetalCommandQueue(metal_command_queue);
  if (queue == nil) return false;

  static std::mutex marker_mutex;
  static id<MTLCommandBuffer> pending_marker = nil;
  std::lock_guard<std::mutex> lock(marker_mutex);

  bool previous_completed = false;
  if (pending_marker != nil) {
    const MTLCommandBufferStatus status = pending_marker.status;
    if (status != MTLCommandBufferStatusCompleted &&
        status != MTLCommandBufferStatusError) {
      return false;
    }
    previous_completed = status == MTLCommandBufferStatusCompleted;
    [pending_marker release];
    pending_marker = nil;
  }

  id<MTLCommandBuffer> next_marker = [queue commandBuffer];
  if (next_marker == nil) return previous_completed;
  pending_marker = [next_marker retain];
  [pending_marker commit];
  return previous_completed;
}
