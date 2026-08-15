#pragma once

#include <cstdint>

uint64_t AetherAppleCreateVulkanTextureFromPixelBuffer(
    uint64_t vulkan_device, uint64_t vulkan_physical_device,
    uint64_t vulkan_queue, uint32_t vulkan_queue_family,
    void *pixel_buffer, uint32_t width, uint32_t height, void **resource);
void AetherAppleReleaseVulkanTexture(void *external_texture);
