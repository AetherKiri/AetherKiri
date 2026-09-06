#pragma once

#include <godot_cpp/variant/string.hpp>

namespace godot {

class Node;
class Sprite2D;
class Viewport;

class RuntimePresentationSprite final {
public:
    void Update(Node *parent, Viewport *viewport, const String &payload);
    void Tick(Viewport *viewport, double delta_seconds);
    void Clear();

private:
    void UpdateLayout(Viewport *viewport);

    Sprite2D *sprite_ = nullptr;
    double elapsed_ms_ = 0.0;
    double logical_x_ = 0.0;
    double logical_y_ = 0.0;
    int frames_ = 1;
    int interval_ms_ = 100;
};

} // namespace godot
