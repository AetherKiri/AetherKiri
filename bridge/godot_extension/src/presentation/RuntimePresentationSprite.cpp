#include "RuntimePresentationSprite.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/sprite2d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <algorithm>
#include <cstdint>

namespace godot {

void RuntimePresentationSprite::Update(
    Node *parent, Viewport *viewport, const String &payload) {
    const PackedStringArray fields = payload.split("\t", true, 6);
    if (fields.size() != 7) {
        UtilityFunctions::printerr("Malformed presentation_sprite payload");
        return;
    }
    const int32_t frames = static_cast<int32_t>(
        std::max<int64_t>(1, fields[2].to_int()));
    const Ref<Image> image = Image::load_from_file(fields[1]);
    if (image.is_null() || image->get_width() % frames != 0) {
        UtilityFunctions::printerr("Invalid presentation sprite sheet");
        return;
    }
    if (sprite_ == nullptr) {
        sprite_ = memnew(Sprite2D);
        sprite_->set_z_index(101);
        parent->add_child(sprite_);
    }
    sprite_->set_name("RuntimePresentationSprite_" + fields[0]);
    sprite_->set_hframes(frames);
    sprite_->set_texture(ImageTexture::create_from_image(image));
    sprite_->set_frame(0);
    sprite_->set_visible(fields[6] == "1");
    frames_ = frames;
    interval_ms_ = static_cast<int32_t>(
        std::max<int64_t>(1, fields[3].to_int()));
    logical_x_ = fields[4].to_float();
    logical_y_ = fields[5].to_float();
    elapsed_ms_ = 0.0;
    UpdateLayout(viewport);
}

void RuntimePresentationSprite::Tick(Viewport *viewport, double delta_seconds) {
    UpdateLayout(viewport);
    if (sprite_ == nullptr || !sprite_->is_visible()) return;
    elapsed_ms_ += std::max(0.0, delta_seconds) * 1000.0;
    const int32_t frame = static_cast<int32_t>(
        elapsed_ms_ / static_cast<double>(interval_ms_)) % frames_;
    sprite_->set_frame(frame);
}

void RuntimePresentationSprite::Clear() {
    if (sprite_ != nullptr) sprite_->queue_free();
    sprite_ = nullptr;
    elapsed_ms_ = 0.0;
}

void RuntimePresentationSprite::UpdateLayout(Viewport *viewport) {
    if (sprite_ == nullptr || viewport == nullptr) return;
    constexpr double kLogicalWidth = 1280.0;
    constexpr double kLogicalHeight = 720.0;
    const Vector2 viewport_size = viewport->get_visible_rect().size;
    const double scale = std::min(
        static_cast<double>(viewport_size.x) / kLogicalWidth,
        static_cast<double>(viewport_size.y) / kLogicalHeight);
    const Vector2 offset(
        (viewport_size.x - kLogicalWidth * scale) * 0.5,
        (viewport_size.y - kLogicalHeight * scale) * 0.5);
    sprite_->set_position(
        offset + Vector2(logical_x_ * scale, logical_y_ * scale));
    sprite_->set_scale(Vector2(scale, scale));
}

} // namespace godot
