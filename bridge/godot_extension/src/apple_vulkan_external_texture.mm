#include "apple_vulkan_external_texture.h"

#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>

#include <dlfcn.h>

namespace {

using GetMetalTextureMvk = void (*)(void *, void **);
using GetMetalCommandQueueMvk = void (*)(void *, void **);
struct AppleVulkanTexturePublisher {
  CVMetalTextureCacheRef cache = nullptr;
  CVMetalTextureRef wrapped_texture = nullptr;
  CVPixelBufferRef pixel_buffer = nullptr;
  id<MTLTexture> destination_texture = nil;
  uint32_t width = 0;
  uint32_t height = 0;
  MTLPixelFormat source_pixel_format = MTLPixelFormatInvalid;
};

}  // namespace

void *AetherAppleCreateVulkanTexturePublisher(
    uint64_t vulkan_device, uint64_t vulkan_physical_device,
    uint64_t vulkan_image, void *pixel_buffer, uint32_t width,
    uint32_t height) {
  if (vulkan_device == 0 || vulkan_physical_device == 0 ||
      vulkan_image == 0 || pixel_buffer == nullptr || width == 0 ||
      height == 0) {
    return nullptr;
  }
  (void)vulkan_device;
  (void)vulkan_physical_device;
  auto get_metal_texture = reinterpret_cast<GetMetalTextureMvk>(
      dlsym(RTLD_DEFAULT, "vkGetMTLTextureMVK"));
  if (get_metal_texture == nullptr) return nullptr;

  id<MTLDevice> metal_device = nil;
  using GetMetalDeviceMvk = void (*)(void *, void **);
  auto get_metal_device = reinterpret_cast<GetMetalDeviceMvk>(
      dlsym(RTLD_DEFAULT, "vkGetMTLDeviceMVK"));
  void *native_device = nullptr;
  if (get_metal_device != nullptr) {
    get_metal_device(reinterpret_cast<void *>(vulkan_physical_device),
                     &native_device);
  }
  metal_device = (__bridge id<MTLDevice>)native_device;
  if (metal_device == nil) return nullptr;

  auto *publisher = new AppleVulkanTexturePublisher();
  publisher->width = width;
  publisher->height = height;
  publisher->pixel_buffer = static_cast<CVPixelBufferRef>(pixel_buffer);
  if (CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, metal_device,
                                nullptr, &publisher->cache) !=
          kCVReturnSuccess ||
      publisher->cache == nullptr) {
    delete publisher;
    return nullptr;
  }
  const OSType pixel_format = CVPixelBufferGetPixelFormatType(
      static_cast<CVPixelBufferRef>(pixel_buffer));
  if (pixel_format == kCVPixelFormatType_32RGBA) {
    publisher->source_pixel_format = MTLPixelFormatRGBA8Unorm;
  } else if (pixel_format == kCVPixelFormatType_32BGRA) {
    publisher->source_pixel_format = MTLPixelFormatBGRA8Unorm;
  } else {
    CFRelease(publisher->cache);
    delete publisher;
    return nullptr;
  }
  if (CVMetalTextureCacheCreateTextureFromImage(
          kCFAllocatorDefault, publisher->cache,
          static_cast<CVPixelBufferRef>(pixel_buffer), nullptr,
          publisher->source_pixel_format, width, height, 0,
          &publisher->wrapped_texture) != kCVReturnSuccess ||
      publisher->wrapped_texture == nullptr ||
      CVMetalTextureGetTexture(publisher->wrapped_texture) == nil) {
    if (publisher->wrapped_texture != nullptr) {
      CFRelease(publisher->wrapped_texture);
    }
    CFRelease(publisher->cache);
    delete publisher;
    return nullptr;
  }
  id<MTLTexture> source =
      CVMetalTextureGetTexture(publisher->wrapped_texture);
  if (source == nil || source.pixelFormat != publisher->source_pixel_format ||
      source.width != width || source.height != height) {
    CFRelease(publisher->wrapped_texture);
    CFRelease(publisher->cache);
    delete publisher;
    return nullptr;
  }
  void* destination_handle = nullptr;
  get_metal_texture(reinterpret_cast<void *>(vulkan_image),
                    &destination_handle);
  id<MTLTexture> destination =
      (__bridge id<MTLTexture>)destination_handle;
  if (destination == nil ||
      destination.pixelFormat != publisher->source_pixel_format ||
      destination.width != width || destination.height != height) {
    CFRelease(publisher->wrapped_texture);
    CFRelease(publisher->cache);
    delete publisher;
    return nullptr;
  }
  publisher->destination_texture = [destination retain];
  return publisher;
}

bool AetherApplePublishPixelBufferToVulkanTexture(
    void *publisher_handle, uint64_t vulkan_queue) {
  auto *publisher =
      static_cast<AppleVulkanTexturePublisher *>(publisher_handle);
  if (publisher == nullptr) {
    return false;
  }
  if (vulkan_queue == 0 || publisher->destination_texture == nil) {
    return false;
  }
  id<MTLTexture> source =
      CVMetalTextureGetTexture(publisher->wrapped_texture);
  if (source == nil ||
      source.pixelFormat != publisher->source_pixel_format ||
      source.width < publisher->width || source.height < publisher->height) {
    return false;
  }
  auto get_metal_queue = reinterpret_cast<GetMetalCommandQueueMvk>(
      dlsym(RTLD_DEFAULT, "vkGetMTLCommandQueueMVK"));
  if (get_metal_queue == nullptr) return false;
  void* queue_handle = nullptr;
  get_metal_queue(reinterpret_cast<void *>(vulkan_queue), &queue_handle);
  id<MTLCommandQueue> queue =
      (__bridge id<MTLCommandQueue>)queue_handle;
  if (queue == nil) return false;

  // Copy the GL-produced IOSurface into a normal MoltenVK-owned texture on
  // Godot's Metal queue. This keeps all pixels on the GPU while avoiding a
  // VkImage view directly backed by storage that legacy OpenGL rewrites.
  @autoreleasepool {
    id<MTLCommandBuffer> command_buffer = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
    if (command_buffer == nil || blit == nil) return false;
    [blit copyFromTexture:source
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(publisher->width, publisher->height, 1)
                toTexture:publisher->destination_texture
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    [command_buffer commit];
  }
  return true;
}

void AetherAppleReleaseVulkanTexture(void *external_texture) {
  auto *publisher =
      static_cast<AppleVulkanTexturePublisher *>(external_texture);
  if (publisher == nullptr) return;
  if (publisher->destination_texture != nil) {
    [publisher->destination_texture release];
  }
  if (publisher->wrapped_texture != nullptr) CFRelease(publisher->wrapped_texture);
  if (publisher->cache != nullptr) CFRelease(publisher->cache);
  delete publisher;
}
