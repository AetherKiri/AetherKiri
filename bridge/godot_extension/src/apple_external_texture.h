#pragma once

#include <cstdint>

// Creates a retained id<MTLTexture> on the RenderingDevice's MTLDevice from
// an IOSurface-backed CVPixelBuffer. The returned retain is transferred to
// RenderingDevice::texture_create_from_extension on success.
uint64_t AetherAppleCreateMetalTextureFromPixelBuffer(
    uint64_t metal_device, void *pixel_buffer, uint32_t width,
    uint32_t height);
void AetherAppleReleaseMetalTexture(uint64_t metal_texture);
void AetherAppleRetainPixelBuffer(void *pixel_buffer);
void AetherAppleReleasePixelBuffer(void *pixel_buffer);

// Polls an empty marker on Godot's Metal command queue without blocking the
// caller. A true result proves that every command buffer submitted before the
// previous marker has completed, so a second graphics API can safely rewrite
// retired IOSurface storage without reading pixels back through the CPU.
bool AetherApplePollMetalCommandQueue(uint64_t metal_command_queue);
