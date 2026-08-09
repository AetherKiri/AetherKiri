#pragma once

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <memory>

namespace godot {

inline constexpr uint32_t AETHERKIRI_FRAME_EFFECT_HOST_API_VERSION = 2;

struct FrameEffectRequest {
    RenderingDevice *rendering_device = nullptr;
    RID source_texture;
    uint32_t input_width = 0;
    uint32_t input_height = 0;
    uint32_t target_width = 0;
    uint32_t target_height = 0;
    uint64_t frame_serial = 0;
    String mode = "auto";
    PackedStringArray custom_chain;
};

struct FrameEffectOutput {
    Ref<Texture2D> texture;
    uint32_t width = 0;
    uint32_t height = 0;
    String pipeline;
};

// A provider is compiled into the Godot extension by an optional package. The
// public host owns only this generic contract; effect shaders, parameters and
// selection policy remain in the provider package.
class FrameEffectProvider {
public:
    virtual ~FrameEffectProvider() = default;

    virtual bool is_available(RenderingDevice *rendering_device,
                              String *reason) const = 0;
    virtual void set_enabled(bool enabled) = 0;
    virtual void set_mode(const String& mode) = 0;
    virtual bool process(const FrameEffectRequest &request,
                         FrameEffectOutput *output,
                         String *error) = 0;
    virtual void release(RenderingDevice *rendering_device) = 0;
    virtual Dictionary status() const = 0;
};

using FrameEffectProviderFactory = std::unique_ptr<FrameEffectProvider> (*)();

bool RegisterFrameEffectProviderFactory(uint32_t host_api_version,
                                        FrameEffectProviderFactory factory);
void UnregisterFrameEffectProviderFactory();
std::unique_ptr<FrameEffectProvider> CreateFrameEffectProvider();

}  // namespace godot
