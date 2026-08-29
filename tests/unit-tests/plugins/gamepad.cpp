#include <catch2/catch_test_macros.hpp>

#include "PortableGamepad.h"

TEST_CASE("portable gamepad manager has a bounded empty-device contract") {
    auto &manager = AetherKiri::PortableGamepadManager::Instance();
    manager.Shutdown();

    CHECK(manager.Count() == 0);
    CHECK_FALSE(manager.Button(0, 0));
    CHECK(manager.Axis(0, 0) == 0.0f);
    CHECK(manager.KeyState(0) == 0u);
    CHECK_FALSE(manager.SetEnabled(0, true));
    CHECK_FALSE(manager.Remove(0));

    // Initialization is allowed to fail when the host has no SDL joystick
    // backend; the API must still remain callable and report zero devices.
    (void)manager.Initialize(0);
    manager.Refresh();
    CHECK(manager.Count() >= 0);
    manager.ResetEdges(-1);
    manager.Shutdown();
}
