#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

namespace AetherKiri {

// The values intentionally mirror krkrz's gamepad plug-in contract.  Keeping
// this type independent of SDL and TJS makes the adapter usable by the core,
// the legacy mock host and unit tests without importing a second input ABI.
struct PortableGamepadState {
    bool connected = false;
    bool enabled = true;
    std::string name;
    std::int32_t type = 0; // 1 = mapped/XInput-like, 3 = generic joystick
    std::uint32_t keyState = 0;
    float leftTrigger = 0.0f;
    float rightTrigger = 0.0f;
    float leftThumbStickX = 0.0f;
    float leftThumbStickY = 0.0f;
    float rightThumbStickX = 0.0f;
    float rightThumbStickY = 0.0f;

    // Edge/hold counters in the same order as the upstream Gamepad class:
    // analog-left (4), analog-right (4), digital d-pad (4), then 12 buttons.
    std::array<std::int32_t, 24> edge{};
};

class PortableGamepadManager final {
public:
    static PortableGamepadManager &Instance();

    // `windowHandle` is retained for API compatibility. SDL's portable
    // joystick backends do not require a native HWND/NSWindow pointer.
    bool Initialize(std::intptr_t windowHandle);
    void Refresh();
    void Shutdown();

    [[nodiscard]] std::int32_t Count() const;
    [[nodiscard]] bool Snapshot(std::int32_t index,
                                PortableGamepadState &out) const;
    [[nodiscard]] bool Button(std::int32_t index, std::int32_t button) const;
    [[nodiscard]] float Axis(std::int32_t index, std::int32_t axis) const;
    [[nodiscard]] std::uint32_t KeyState(std::int32_t index) const;
    [[nodiscard]] bool SetVibration(std::int32_t index, float left,
                                     float right);
    [[nodiscard]] bool SetEnabled(std::int32_t index, bool enabled);
    [[nodiscard]] bool Remove(std::int32_t index);
    void ResetEdges(std::int32_t index);

private:
    PortableGamepadManager();
    ~PortableGamepadManager();
    PortableGamepadManager(const PortableGamepadManager &) = delete;
    PortableGamepadManager &operator=(const PortableGamepadManager &) = delete;

    struct Impl;
    Impl *impl_;
};

} // namespace AetherKiri
