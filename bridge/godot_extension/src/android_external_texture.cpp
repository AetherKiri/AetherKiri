#include "android_external_texture.h"

#if defined(__ANDROID__)

#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <cstdint>

namespace {

using AcquireHardwareBuffer = void (*)(AHardwareBuffer *);
using ReleaseHardwareBuffer = void (*)(AHardwareBuffer *);

struct AndroidExternalTexture {
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    AHardwareBuffer *hardware_buffer = nullptr;
    ReleaseHardwareBuffer release_hardware_buffer = nullptr;
};

uint32_t SelectMemoryType(VkPhysicalDevice physical_device,
                          uint32_t allowed_types) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
    for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        if ((allowed_types & (1u << index)) != 0u &&
            (properties.memoryTypes[index].propertyFlags &
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0u) {
            return index;
        }
    }
    for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        if ((allowed_types & (1u << index)) != 0u) return index;
    }
    return UINT32_MAX;
}

void ReleaseTexture(AndroidExternalTexture *texture) {
    if (texture == nullptr) return;
    if (texture->image != VK_NULL_HANDLE) {
        vkDestroyImage(texture->device, texture->image, nullptr);
    }
    if (texture->memory != VK_NULL_HANDLE) {
        vkFreeMemory(texture->device, texture->memory, nullptr);
    }
    if (texture->hardware_buffer != nullptr &&
        texture->release_hardware_buffer != nullptr) {
        texture->release_hardware_buffer(texture->hardware_buffer);
    }
    delete texture;
}

} // namespace

uint64_t AetherAndroidCreateVulkanTextureFromHardwareBuffer(
    uint64_t vulkan_device, uint64_t vulkan_physical_device,
    void *hardware_buffer, uint32_t width, uint32_t height,
    void **resource) {
    if (resource != nullptr) *resource = nullptr;
    if (vulkan_device == 0 || vulkan_physical_device == 0 ||
        hardware_buffer == nullptr || width == 0 || height == 0 ||
        resource == nullptr) {
        return 0;
    }

    auto *acquire_hardware_buffer = reinterpret_cast<AcquireHardwareBuffer>(
        dlsym(RTLD_DEFAULT, "AHardwareBuffer_acquire"));
    auto *release_hardware_buffer = reinterpret_cast<ReleaseHardwareBuffer>(
        dlsym(RTLD_DEFAULT, "AHardwareBuffer_release"));
    if (acquire_hardware_buffer == nullptr ||
        release_hardware_buffer == nullptr) {
        return 0;
    }

    const VkDevice device = reinterpret_cast<VkDevice>(vulkan_device);
    const VkPhysicalDevice physical_device =
        reinterpret_cast<VkPhysicalDevice>(vulkan_physical_device);
    const auto get_hardware_buffer_properties =
        reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
            vkGetDeviceProcAddr(
                device, "vkGetAndroidHardwareBufferPropertiesANDROID"));
    if (get_hardware_buffer_properties == nullptr) return 0;

    auto *texture = new AndroidExternalTexture();
    texture->device = device;
    texture->hardware_buffer = static_cast<AHardwareBuffer *>(hardware_buffer);
    texture->release_hardware_buffer = release_hardware_buffer;
    acquire_hardware_buffer(texture->hardware_buffer);

    VkAndroidHardwareBufferFormatPropertiesANDROID format_properties{};
    format_properties.sType =
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;
    VkAndroidHardwareBufferPropertiesANDROID buffer_properties{};
    buffer_properties.sType =
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    buffer_properties.pNext = &format_properties;
    if (get_hardware_buffer_properties(
            device, texture->hardware_buffer, &buffer_properties) !=
            VK_SUCCESS ||
        format_properties.format != VK_FORMAT_R8G8B8A8_UNORM) {
        ReleaseTexture(texture);
        return 0;
    }

    VkExternalMemoryImageCreateInfo external_info{};
    external_info.sType =
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_info.handleTypes =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = &external_info;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &image_info, nullptr, &texture->image) !=
        VK_SUCCESS) {
        ReleaseTexture(texture);
        return 0;
    }

    const uint32_t memory_type = SelectMemoryType(
        physical_device, buffer_properties.memoryTypeBits);
    if (memory_type == UINT32_MAX) {
        ReleaseTexture(texture);
        return 0;
    }
    VkMemoryDedicatedAllocateInfo dedicated_info{};
    dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated_info.image = texture->image;
    VkImportAndroidHardwareBufferInfoANDROID import_info{};
    import_info.sType =
        VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
    import_info.pNext = &dedicated_info;
    import_info.buffer = texture->hardware_buffer;
    VkMemoryAllocateInfo allocation_info{};
    allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation_info.pNext = &import_info;
    allocation_info.allocationSize = buffer_properties.allocationSize;
    allocation_info.memoryTypeIndex = memory_type;
    if (vkAllocateMemory(device, &allocation_info, nullptr,
                         &texture->memory) != VK_SUCCESS ||
        vkBindImageMemory(device, texture->image, texture->memory, 0) !=
            VK_SUCCESS) {
        ReleaseTexture(texture);
        return 0;
    }

    *resource = texture;
    return reinterpret_cast<uint64_t>(texture->image);
}

void AetherAndroidReleaseVulkanTexture(void *resource) {
    ReleaseTexture(static_cast<AndroidExternalTexture *>(resource));
}

#else

uint64_t AetherAndroidCreateVulkanTextureFromHardwareBuffer(
    uint64_t, uint64_t, void *, uint32_t, uint32_t, void **) {
    return 0;
}

void AetherAndroidReleaseVulkanTexture(void *) {}

#endif
