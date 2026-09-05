#pragma once

#include <cstdint>

// Imports an AHardwareBuffer into the RenderingDevice's Vulkan device. The
// returned VkImage remains owned by the opaque resource and must outlive the
// RID created with RenderingDevice::texture_create_from_extension().
uint64_t AetherAndroidCreateVulkanTextureFromHardwareBuffer(
    uint64_t vulkan_device, uint64_t vulkan_physical_device,
    void *hardware_buffer, uint32_t width, uint32_t height,
    void **resource);
void AetherAndroidReleaseVulkanTexture(void *resource);
