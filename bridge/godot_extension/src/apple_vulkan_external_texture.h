#pragma once

#include <cstdint>

void *AetherAppleCreateVulkanTexturePublisher(
    uint64_t vulkan_device, uint64_t vulkan_physical_device,
    uint64_t vulkan_image, void *pixel_buffer, uint32_t width,
    uint32_t height);
bool AetherApplePublishPixelBufferToVulkanTexture(
    void *publisher, uint64_t vulkan_queue);
void AetherAppleReleaseVulkanTexture(void *external_texture);
