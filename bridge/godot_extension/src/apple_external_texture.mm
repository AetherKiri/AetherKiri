#include "apple_external_texture.h"

#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>

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
