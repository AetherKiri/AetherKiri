#include "apple_vulkan_external_texture.h"

#import <CoreVideo/CoreVideo.h>
#import <IOSurface/IOSurface.h>

#include <dlfcn.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_metal.h>

namespace {

struct AppleVulkanExternalTexture {
  VkDevice device = VK_NULL_HANDLE;
  VkImage image = VK_NULL_HANDLE;
};

void ReleaseTexture(AppleVulkanExternalTexture *texture) {
  if (texture == nullptr) return;
  if (texture->image != VK_NULL_HANDLE && texture->device != VK_NULL_HANDLE) {
    auto destroy_image = reinterpret_cast<PFN_vkDestroyImage>(
        dlsym(RTLD_DEFAULT, "vkDestroyImage"));
    if (destroy_image != nullptr) {
      destroy_image(texture->device, texture->image, nullptr);
    }
  }
  delete texture;
}

}  // namespace

uint64_t AetherAppleCreateVulkanTextureFromPixelBuffer(
    uint64_t vulkan_device, uint64_t vulkan_physical_device,
    uint64_t vulkan_queue, uint32_t vulkan_queue_family,
    void *pixel_buffer, uint32_t width, uint32_t height, void **resource) {
  if (resource != nullptr) *resource = nullptr;
  if (vulkan_device == 0 || vulkan_physical_device == 0 ||
      vulkan_queue == 0 ||
      pixel_buffer == nullptr || width == 0 || height == 0 ||
      resource == nullptr) {
    return 0;
  }

  auto create_image = reinterpret_cast<PFN_vkCreateImage>(
      dlsym(RTLD_DEFAULT, "vkCreateImage"));
  auto get_format_properties =
      reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(
          dlsym(RTLD_DEFAULT, "vkGetPhysicalDeviceFormatProperties"));
  auto create_command_pool = reinterpret_cast<PFN_vkCreateCommandPool>(
      dlsym(RTLD_DEFAULT, "vkCreateCommandPool"));
  auto allocate_command_buffers =
      reinterpret_cast<PFN_vkAllocateCommandBuffers>(
          dlsym(RTLD_DEFAULT, "vkAllocateCommandBuffers"));
  auto begin_command_buffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(
      dlsym(RTLD_DEFAULT, "vkBeginCommandBuffer"));
  auto cmd_pipeline_barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(
      dlsym(RTLD_DEFAULT, "vkCmdPipelineBarrier"));
  auto end_command_buffer = reinterpret_cast<PFN_vkEndCommandBuffer>(
      dlsym(RTLD_DEFAULT, "vkEndCommandBuffer"));
  auto create_fence = reinterpret_cast<PFN_vkCreateFence>(
      dlsym(RTLD_DEFAULT, "vkCreateFence"));
  auto queue_submit = reinterpret_cast<PFN_vkQueueSubmit>(
      dlsym(RTLD_DEFAULT, "vkQueueSubmit"));
  auto wait_for_fences = reinterpret_cast<PFN_vkWaitForFences>(
      dlsym(RTLD_DEFAULT, "vkWaitForFences"));
  auto destroy_fence = reinterpret_cast<PFN_vkDestroyFence>(
      dlsym(RTLD_DEFAULT, "vkDestroyFence"));
  auto destroy_command_pool = reinterpret_cast<PFN_vkDestroyCommandPool>(
      dlsym(RTLD_DEFAULT, "vkDestroyCommandPool"));
  if (create_image == nullptr || get_format_properties == nullptr ||
      create_command_pool == nullptr || allocate_command_buffers == nullptr ||
      begin_command_buffer == nullptr || cmd_pipeline_barrier == nullptr ||
      end_command_buffer == nullptr || create_fence == nullptr ||
      queue_submit == nullptr || wait_for_fences == nullptr ||
      destroy_fence == nullptr || destroy_command_pool == nullptr) {
    return 0;
  }

  CVPixelBufferRef buffer = static_cast<CVPixelBufferRef>(pixel_buffer);
  if (CVPixelBufferGetPixelFormatType(buffer) !=
          kCVPixelFormatType_32BGRA ||
      CVPixelBufferGetWidth(buffer) != width ||
      CVPixelBufferGetHeight(buffer) != height) {
    return 0;
  }
  IOSurfaceRef io_surface = CVPixelBufferGetIOSurface(buffer);
  if (io_surface == nullptr) return 0;

  const VkDevice device = reinterpret_cast<VkDevice>(vulkan_device);
  const VkPhysicalDevice physical_device =
      reinterpret_cast<VkPhysicalDevice>(vulkan_physical_device);
  VkFormatProperties properties{};
  get_format_properties(physical_device, VK_FORMAT_B8G8R8A8_UNORM,
                        &properties);
  const VkFormatFeatureFlags required_features =
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
      VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
  if ((properties.optimalTilingFeatures & required_features) !=
      required_features) {
    return 0;
  }

  auto *texture = new AppleVulkanExternalTexture();
  texture->device = device;

  // Import the IOSurface while the VkImage is created. MoltenVK then owns the
  // Metal texture view and tracks its Vulkan layout; Godot no longer samples a
  // separate VkImage whose private MTLTexture was modified behind Vulkan's
  // back. The CVPixelBuffer remains retained by the bridge record.
  VkImportMetalIOSurfaceInfoEXT import_info{};
  import_info.sType =
      VK_STRUCTURE_TYPE_IMPORT_METAL_IO_SURFACE_INFO_EXT;
  import_info.ioSurface = io_surface;

  VkImageCreateInfo image_info{};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.pNext = &import_info;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
  image_info.extent = {width, height, 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                     VK_IMAGE_USAGE_STORAGE_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (create_image(device, &image_info, nullptr, &texture->image) !=
      VK_SUCCESS) {
    ReleaseTexture(texture);
    return 0;
  }

  // Godot's extension-texture wrapper creates only a VkImageView and assumes
  // the owner has already placed the native VkImage in the layout matching
  // its declared usage. Establish GENERAL before the producer first touches the
  // IOSurface. Later OpenGL generations replace the pixels, not the layout.
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkCommandPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  pool_info.queueFamilyIndex = vulkan_queue_family;
  if (create_command_pool(device, &pool_info, nullptr, &command_pool) !=
      VK_SUCCESS) {
    ReleaseTexture(texture);
    return 0;
  }

  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo allocation_info{};
  allocation_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocation_info.commandPool = command_pool;
  allocation_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocation_info.commandBufferCount = 1;
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VkImageMemoryBarrier image_barrier{};
  image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  image_barrier.srcAccessMask = 0;
  image_barrier.dstAccessMask =
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_barrier.image = texture->image;
  image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  image_barrier.subresourceRange.baseMipLevel = 0;
  image_barrier.subresourceRange.levelCount = 1;
  image_barrier.subresourceRange.baseArrayLayer = 0;
  image_barrier.subresourceRange.layerCount = 1;
  bool initialized =
      allocate_command_buffers(device, &allocation_info, &command_buffer) ==
          VK_SUCCESS &&
      begin_command_buffer(command_buffer, &begin_info) == VK_SUCCESS;
  if (initialized) {
    cmd_pipeline_barrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &image_barrier);
    initialized = end_command_buffer(command_buffer) == VK_SUCCESS;
  }

  VkFence fence = VK_NULL_HANDLE;
  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  initialized = initialized &&
      create_fence(device, &fence_info, nullptr, &fence) == VK_SUCCESS;
  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer;
  const VkQueue queue = reinterpret_cast<VkQueue>(vulkan_queue);
  initialized = initialized &&
      queue_submit(queue, 1, &submit_info, fence) == VK_SUCCESS &&
      wait_for_fences(device, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
  if (fence != VK_NULL_HANDLE) destroy_fence(device, fence, nullptr);
  destroy_command_pool(device, command_pool, nullptr);
  if (!initialized) {
    ReleaseTexture(texture);
    return 0;
  }

  *resource = texture;
  return reinterpret_cast<uint64_t>(texture->image);
}

void AetherAppleReleaseVulkanTexture(void *external_texture) {
  ReleaseTexture(
      static_cast<AppleVulkanExternalTexture *>(external_texture));
}
