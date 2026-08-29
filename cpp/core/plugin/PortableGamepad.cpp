#include "PortableGamepad.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#if defined(AETHERKIRI_HAS_SDL2)
#include <SDL2/SDL.h>
#endif

namespace AetherKiri {
namespace {

constexpr std::uint32_t kDpadUp = 0x00000001u;
constexpr std::uint32_t kDpadDown = 0x00000002u;
constexpr std::uint32_t kDpadLeft = 0x00000004u;
constexpr std::uint32_t kDpadRight = 0x00000008u;
constexpr std::uint32_t kStart = 0x00000010u;
constexpr std::uint32_t kBack = 0x00000020u;
constexpr std::uint32_t kLeftThumb = 0x00000040u;
constexpr std::uint32_t kRightThumb = 0x00000080u;
constexpr std::uint32_t kLeftShoulder = 0x00000100u;
constexpr std::uint32_t kRightShoulder = 0x00000200u;
constexpr std::uint32_t kA = 0x00001000u;
constexpr std::uint32_t kB = 0x00002000u;
constexpr std::uint32_t kX = 0x00004000u;
constexpr std::uint32_t kY = 0x00008000u;

enum EdgeIndex : std::size_t {
    kAnalogLeftUp = 0,
    kAnalogLeftDown,
    kAnalogLeftLeft,
    kAnalogLeftRight,
    kAnalogRightUp,
    kAnalogRightDown,
    kAnalogRightLeft,
    kAnalogRightRight,
    kDigitalUp,
    kDigitalDown,
    kDigitalLeft,
    kDigitalRight,
    kButtonStart,
    kButtonBack,
    kButtonLeftThumb,
    kButtonRightThumb,
    kButtonLeftShoulder,
    kButtonLeftTrigger,
    kButtonRightShoulder,
    kButtonRightTrigger,
    kButtonA,
    kButtonB,
    kButtonX,
    kButtonY,
};

void updateCounter(std::int32_t &counter, bool pressed) {
    if(pressed) {
        if(counter <= 0)
            counter = 1;
        else if(counter < std::numeric_limits<std::int32_t>::max())
            ++counter;
    } else if(counter >= 1) {
        counter = 0;
    } else if(counter > std::numeric_limits<std::int32_t>::min()) {
        --counter;
    }
}

float normalizeAxis(std::int16_t raw, float deadZone = 0.18f) {
    const float value = static_cast<float>(raw) / 32768.0f;
    const float magnitude = std::abs(value);
    if(magnitude <= deadZone)
        return 0.0f;
    const float scaled = (magnitude - deadZone) / (1.0f - deadZone);
    return std::copysign(std::min(1.0f, scaled), value);
}

float normalizeTrigger(std::int16_t raw) {
    return std::clamp(static_cast<float>(raw) / 32767.0f, 0.0f, 1.0f);
}

std::uint32_t keyForButtonIndex(std::int32_t button) {
    switch(button) {
    case 0: return kDpadUp;
    case 1: return kDpadDown;
    case 2: return kDpadLeft;
    case 3: return kDpadRight;
    case 4: return kStart;
    case 5: return kBack;
    case 6: return kLeftThumb;
    case 7: return kRightThumb;
    case 8: return kLeftShoulder;
    case 9: return kRightShoulder;
    case 10: return kA;
    case 11: return kB;
    case 12: return kX;
    case 13: return kY;
    default: return 0;
    }
}

} // namespace

struct PortableGamepadManager::Impl {
    mutable std::mutex mutex;
    std::intptr_t windowHandle = 0;
    bool initialized = false;
    std::vector<PortableGamepadState> states;

#if defined(AETHERKIRI_HAS_SDL2)
    struct Device {
        SDL_GameController *controller = nullptr;
        SDL_Joystick *joystick = nullptr;
        SDL_JoystickID instance = -1;
        bool mapped = false;
        PortableGamepadState state;
    };
    std::vector<Device> devices;

    static bool isAttached(const Device &device) {
        return device.joystick && SDL_JoystickGetAttached(device.joystick);
    }

    static void closeDevice(Device &device) {
        if(device.controller) {
            SDL_GameControllerClose(device.controller);
            device.controller = nullptr;
            device.joystick = nullptr;
        } else if(device.joystick) {
            SDL_JoystickClose(device.joystick);
            device.joystick = nullptr;
        }
    }

    static bool controllerButton(const Device &device, int button) {
        if(!device.controller)
            return false;
        const auto value = static_cast<SDL_GameControllerButton>(button);
        return SDL_GameControllerGetButton(device.controller, value) != 0;
    }

    static std::int16_t controllerAxis(const Device &device, int axis) {
        if(!device.controller)
            return 0;
        return SDL_GameControllerGetAxis(
            device.controller, static_cast<SDL_GameControllerAxis>(axis));
    }

    static bool joystickButton(const Device &device, int button) {
        return device.joystick && button >= 0 &&
            button < SDL_JoystickNumButtons(device.joystick) &&
            SDL_JoystickGetButton(device.joystick, button) != 0;
    }

    static std::int16_t joystickAxis(const Device &device, int axis) {
        if(!device.joystick || axis < 0 ||
           axis >= SDL_JoystickNumAxes(device.joystick))
            return 0;
        return SDL_JoystickGetAxis(device.joystick, axis);
    }

    static void updateMapped(Device &device) {
        auto &state = device.state;
        state.connected = isAttached(device);
        state.name = device.controller
            ? (SDL_GameControllerName(device.controller)
                   ? SDL_GameControllerName(device.controller)
                   : "")
            : (SDL_JoystickName(device.joystick)
                   ? SDL_JoystickName(device.joystick)
                   : "");
        state.type = device.mapped ? 1 : 3;
        state.keyState = 0;

        const std::array<int, 14> buttons = {
            SDL_CONTROLLER_BUTTON_DPAD_UP,
            SDL_CONTROLLER_BUTTON_DPAD_DOWN,
            SDL_CONTROLLER_BUTTON_DPAD_LEFT,
            SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
            SDL_CONTROLLER_BUTTON_START,
            SDL_CONTROLLER_BUTTON_BACK,
            SDL_CONTROLLER_BUTTON_LEFTSTICK,
            SDL_CONTROLLER_BUTTON_RIGHTSTICK,
            SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
            SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
            SDL_CONTROLLER_BUTTON_A,
            SDL_CONTROLLER_BUTTON_B,
            SDL_CONTROLLER_BUTTON_X,
            SDL_CONTROLLER_BUTTON_Y,
        };
        std::array<bool, 14> pressed{};
        if(device.mapped) {
            for(std::size_t i = 0; i < buttons.size(); ++i)
                pressed[i] = controllerButton(device, buttons[i]);
        } else {
            // Generic HID fallback follows the conventional SDL joystick
            // order.  D-pad hats are folded into the first four entries.
            for(std::size_t i = 0; i < 10; ++i)
                pressed[i] = joystickButton(device, static_cast<int>(i));
            const Uint8 hat = device.joystick
                ? SDL_JoystickGetHat(device.joystick, 0) : 0;
            pressed[0] = pressed[0] || (hat & SDL_HAT_UP);
            pressed[1] = pressed[1] || (hat & SDL_HAT_DOWN);
            pressed[2] = pressed[2] || (hat & SDL_HAT_LEFT);
            pressed[3] = pressed[3] || (hat & SDL_HAT_RIGHT);
            for(std::size_t i = 10; i < buttons.size(); ++i)
                pressed[i] = joystickButton(device, static_cast<int>(i));
        }
        const std::array<std::uint32_t, 14> masks = {
            kDpadUp, kDpadDown, kDpadLeft, kDpadRight, kStart, kBack,
            kLeftThumb, kRightThumb, kLeftShoulder, kRightShoulder, kA, kB,
            kX, kY};
        for(std::size_t i = 0; i < pressed.size(); ++i) {
            if(pressed[i])
                state.keyState |= masks[i];
        }

        std::int16_t lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
        if(device.mapped) {
            lx = controllerAxis(device, SDL_CONTROLLER_AXIS_LEFTX);
            ly = controllerAxis(device, SDL_CONTROLLER_AXIS_LEFTY);
            rx = controllerAxis(device, SDL_CONTROLLER_AXIS_RIGHTX);
            ry = controllerAxis(device, SDL_CONTROLLER_AXIS_RIGHTY);
            lt = controllerAxis(device, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
            rt = controllerAxis(device, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
            state.leftTrigger = normalizeTrigger(lt);
            state.rightTrigger = normalizeTrigger(rt);
        } else {
            lx = joystickAxis(device, 0);
            ly = joystickAxis(device, 1);
            rx = joystickAxis(device, 2);
            ry = joystickAxis(device, 3);
            lt = joystickAxis(device, 4);
            rt = joystickAxis(device, 5);
            // Generic triggers are usually centred axes.  Convert the
            // conventional [-1,1] representation to the API's [0,1].
            state.leftTrigger = (normalizeAxis(lt, 0.05f) + 1.0f) * 0.5f;
            state.rightTrigger = (normalizeAxis(rt, 0.05f) + 1.0f) * 0.5f;
            if(std::abs(state.leftTrigger - 0.5f) < 0.05f)
                state.leftTrigger = 0.0f;
            if(std::abs(state.rightTrigger - 0.5f) < 0.05f)
                state.rightTrigger = 0.0f;
        }
        state.leftThumbStickX = normalizeAxis(lx);
        state.leftThumbStickY = normalizeAxis(ly);
        state.rightThumbStickX = normalizeAxis(rx);
        state.rightThumbStickY = normalizeAxis(ry);

        updateCounter(state.edge[kAnalogLeftLeft],
                      state.leftThumbStickX < 0.0f);
        updateCounter(state.edge[kAnalogLeftRight],
                      state.leftThumbStickX > 0.0f);
        updateCounter(state.edge[kAnalogLeftDown],
                      state.leftThumbStickY < 0.0f);
        updateCounter(state.edge[kAnalogLeftUp],
                      state.leftThumbStickY > 0.0f);
        updateCounter(state.edge[kAnalogRightLeft],
                      state.rightThumbStickX < 0.0f);
        updateCounter(state.edge[kAnalogRightRight],
                      state.rightThumbStickX > 0.0f);
        updateCounter(state.edge[kAnalogRightDown],
                      state.rightThumbStickY < 0.0f);
        updateCounter(state.edge[kAnalogRightUp],
                      state.rightThumbStickY > 0.0f);
        for(std::size_t i = 0; i < 4; ++i)
            updateCounter(state.edge[kDigitalUp + i], pressed[i]);
        updateCounter(state.edge[kButtonStart], pressed[4]);
        updateCounter(state.edge[kButtonBack], pressed[5]);
        updateCounter(state.edge[kButtonLeftThumb], pressed[6]);
        updateCounter(state.edge[kButtonRightThumb], pressed[7]);
        updateCounter(state.edge[kButtonLeftShoulder], pressed[8]);
        updateCounter(state.edge[kButtonLeftTrigger], state.leftTrigger > 0.0f);
        updateCounter(state.edge[kButtonRightShoulder], pressed[9]);
        updateCounter(state.edge[kButtonRightTrigger],
                      state.rightTrigger > 0.0f);
        updateCounter(state.edge[kButtonA], pressed[10]);
        updateCounter(state.edge[kButtonB], pressed[11]);
        updateCounter(state.edge[kButtonX], pressed[12]);
        updateCounter(state.edge[kButtonY], pressed[13]);
    }

    void refreshSDL() {
        if(!initialized)
            return;
        SDL_GameControllerUpdate();
        SDL_JoystickUpdate();
        const int count = std::max(0, SDL_NumJoysticks());
        for(int deviceIndex = 0; deviceIndex < count; ++deviceIndex) {
            const SDL_JoystickID instance =
                SDL_JoystickGetDeviceInstanceID(deviceIndex);
            if(instance < 0)
                continue;
            auto found = std::find_if(
                devices.begin(), devices.end(),
                [instance](const Device &device) {
                    return device.instance == instance;
                });
            if(found == devices.end()) {
                Device device;
                device.instance = instance;
                device.mapped = SDL_IsGameController(deviceIndex) == SDL_TRUE;
                if(device.mapped)
                    device.controller = SDL_GameControllerOpen(deviceIndex);
                if(device.controller)
                    device.joystick = SDL_GameControllerGetJoystick(
                        device.controller);
                if(!device.joystick)
                    device.joystick = SDL_JoystickOpen(deviceIndex);
                if(device.joystick) {
                    device.state.enabled = true;
                    devices.push_back(std::move(device));
                }
            }
        }
        for(auto it = devices.begin(); it != devices.end();) {
            if(!isAttached(*it)) {
                closeDevice(*it);
                it = devices.erase(it);
            } else {
                updateMapped(*it);
                ++it;
            }
        }
        states.clear();
        states.reserve(devices.size());
        for(const auto &device : devices)
            states.push_back(device.state);
    }
#endif

    void initialize(std::intptr_t handle) {
        windowHandle = handle;
#if defined(AETHERKIRI_HAS_SDL2)
        const Uint32 wanted = SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER |
                              SDL_INIT_HAPTIC;
        if((SDL_WasInit(wanted) & SDL_INIT_JOYSTICK) == 0)
            SDL_InitSubSystem(wanted);
        SDL_GameControllerEventState(SDL_ENABLE);
        initialized = (SDL_WasInit(SDL_INIT_JOYSTICK) &
                       SDL_INIT_JOYSTICK) != 0;
        refreshSDL();
#else
        initialized = false;
        states.clear();
#endif
    }

    void refresh() {
#if defined(AETHERKIRI_HAS_SDL2)
        refreshSDL();
#endif
    }

    void shutdown() {
#if defined(AETHERKIRI_HAS_SDL2)
        for(auto &device : devices)
            closeDevice(device);
        devices.clear();
#endif
        states.clear();
        initialized = false;
    }
};

PortableGamepadManager &PortableGamepadManager::Instance() {
    static PortableGamepadManager manager;
    return manager;
}

PortableGamepadManager::PortableGamepadManager() : impl_(new Impl()) {}
PortableGamepadManager::~PortableGamepadManager() {
    if(impl_) {
        impl_->shutdown();
        delete impl_;
        impl_ = nullptr;
    }
}

bool PortableGamepadManager::Initialize(std::intptr_t windowHandle) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->initialize(windowHandle);
    return impl_->initialized;
}

void PortableGamepadManager::Refresh() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->refresh();
}
void PortableGamepadManager::Shutdown() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->shutdown();
}

std::int32_t PortableGamepadManager::Count() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return static_cast<std::int32_t>(impl_->states.size());
}

bool PortableGamepadManager::Snapshot(std::int32_t index,
                                      PortableGamepadState &out) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(index < 0 || static_cast<std::size_t>(index) >= impl_->states.size())
        return false;
    out = impl_->states[static_cast<std::size_t>(index)];
    return out.connected;
}

bool PortableGamepadManager::Button(std::int32_t index,
                                    std::int32_t button) const {
    PortableGamepadState state;
    if(!Snapshot(index, state))
        return false;
    if(!state.enabled)
        return false;
    const std::uint32_t mask = keyForButtonIndex(button);
    if(mask == 0)
        return false;
    return (state.keyState & mask) != 0;
}

float PortableGamepadManager::Axis(std::int32_t index,
                                   std::int32_t axis) const {
    PortableGamepadState state;
    if(!Snapshot(index, state))
        return 0.0f;
    if(!state.enabled)
        return 0.0f;
    switch(axis) {
    case 0: return state.leftThumbStickX;
    case 1: return state.leftThumbStickY;
    case 2: return state.rightThumbStickX;
    case 3: return state.rightThumbStickY;
    case 4: return state.leftTrigger;
    case 5: return state.rightTrigger;
    default: return 0.0f;
    }
}

std::uint32_t PortableGamepadManager::KeyState(std::int32_t index) const {
    PortableGamepadState state;
    return Snapshot(index, state) && state.enabled ? state.keyState : 0;
}

bool PortableGamepadManager::SetVibration(std::int32_t index, float left,
                                          float right) {
#if defined(AETHERKIRI_HAS_SDL2)
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(index < 0 || static_cast<std::size_t>(index) >= impl_->devices.size())
        return false;
    auto &device = impl_->devices[static_cast<std::size_t>(index)];
    if(!device.controller)
        return false;
    const auto low = static_cast<Uint16>(std::clamp(left, 0.0f, 1.0f) * 65535.0f);
    const auto high = static_cast<Uint16>(std::clamp(right, 0.0f, 1.0f) * 65535.0f);
    return SDL_GameControllerRumble(device.controller, low, high, 100) == 0;
#else
    (void)index;
    (void)left;
    (void)right;
    return false;
#endif
}

bool PortableGamepadManager::SetEnabled(std::int32_t index, bool enabled) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(index < 0 || static_cast<std::size_t>(index) >= impl_->states.size())
        return false;
    impl_->states[static_cast<std::size_t>(index)].enabled = enabled;
#if defined(AETHERKIRI_HAS_SDL2)
    if(static_cast<std::size_t>(index) < impl_->devices.size())
        impl_->devices[static_cast<std::size_t>(index)].state.enabled = enabled;
#endif
    return true;
}

bool PortableGamepadManager::Remove(std::int32_t index) {
#if defined(AETHERKIRI_HAS_SDL2)
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(index < 0 || static_cast<std::size_t>(index) >= impl_->devices.size())
        return false;
    auto &device = impl_->devices[static_cast<std::size_t>(index)];
    Impl::closeDevice(device);
    impl_->devices.erase(impl_->devices.begin() + index);
    impl_->states.erase(impl_->states.begin() + index);
    return true;
#else
    (void)index;
    return false;
#endif
}

void PortableGamepadManager::ResetEdges(std::int32_t index) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(index < 0) {
        for(auto &state : impl_->states)
            state.edge.fill(0);
        return;
    }
    if(static_cast<std::size_t>(index) < impl_->states.size())
        impl_->states[static_cast<std::size_t>(index)].edge.fill(0);
}

} // namespace AetherKiri
