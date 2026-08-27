/**
 * @file EngineLoop.cpp
 * @brief EngineLoop implementation — engine main loop + input event forwarding.
 *
 * This is the Phase 3 core: it drives Application::Run() per frame and
 * converts EngineInputEvent into TVP input events posted to the
 * engine's event queue.
 */

#include "EngineLoop.h"

#include <chrono>
#include <cstdlib>
#include <spdlog/spdlog.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

#include "Application.h"
#include "ConfigManager/IndividualConfigManager.h"
#include "ConfigManager/GlobalConfigManager.h"
#include "Platform.h"
#include "SysInitIntf.h"
#include "RenderManager.h"
#include "ScriptMgnIntf.h"
#include "TickCount.h"
#ifdef __APPLE__
#include <malloc/malloc.h>
#endif

// TVP input event classes + TVPPostInputEvent
#include "WindowIntf.h"
#include "WindowImpl.h"
#include "tvpinputdefs.h"
#include "EventIntf.h"

// Forward declarations for functions used by the engine core
extern bool TVPCheckStartupPath(const std::string& path);
extern void TVPForceSwapBuffer();
extern void TVPHostForceDrawDeviceShow();

// ---------------------------------------------------------------------------
// Global state — previously in MainScene.cpp, now owned by EngineLoop
// ---------------------------------------------------------------------------

static void (*s_postUpdate)() = nullptr;

namespace {
int64_t NowMs() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               clock::now().time_since_epoch())
        .count();
}

bool InputTraceEnabled() {
    const char* value = std::getenv("AETHERKIRI_INPUT_TRACE");
    return value && value[0] && value[0] != '0';
}
} // namespace

#ifdef __ANDROID__
#define AETHER_INPUT_TRACE_LOG(...)                                             \
    do {                                                                        \
        if (InputTraceEnabled()) {                                              \
            __android_log_print(ANDROID_LOG_INFO, "aether-input", __VA_ARGS__); \
        }                                                                       \
    } while (0)
#else
#define AETHER_INPUT_TRACE_LOG(...)                                             \
    do {                                                                        \
    } while (0)
#endif

void TVPSetPostUpdateEvent(void (*f)()) { s_postUpdate = f; }

// Async key/mouse state table — indexed by Windows VK code
// Bit 0 = currently pressed, Bit 4 = was pressed since last query
static tjs_uint8 s_scancode[0x200] = {};

bool TVPGetKeyMouseAsyncState(tjs_uint keycode, bool getcurrent) {
    if (keycode >= sizeof(s_scancode) / sizeof(s_scancode[0]))
        return false;
    tjs_uint8 code = s_scancode[keycode];
    s_scancode[keycode] &= 1;
    return code & (getcurrent ? 1 : 0x10);
}

bool TVPGetJoyPadAsyncState(tjs_uint keycode, bool getcurrent) {
    if (keycode >= sizeof(s_scancode) / sizeof(s_scancode[0]))
        return false;
    tjs_uint8 code = s_scancode[keycode];
    s_scancode[keycode] &= 1;
    return code & (getcurrent ? 1 : 0x10);
}

int TVPDrawSceneOnce(int interval) {
    static tjs_uint64 lastTick = TVPGetRoughTickCount32();
    tjs_uint64 curTick = TVPGetRoughTickCount32();
    int remain = interval - static_cast<int>(curTick - lastTick);
    if (remain <= 0) {
        if (s_postUpdate)
            s_postUpdate();
        TVPHostForceDrawDeviceShow();
        TVPForceSwapBuffer();
        lastTick = curTick;
        return 0;
    } else {
        return remain;
    }
}

// ---------------------------------------------------------------------------
// EngineLoop singleton
// ---------------------------------------------------------------------------

static EngineLoop* s_instance = nullptr;

EngineLoop::EngineLoop() = default;

EngineLoop::~EngineLoop() {
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

EngineLoop* EngineLoop::GetInstance() {
    return s_instance;
}

EngineLoop* EngineLoop::CreateInstance() {
    if (!s_instance) {
        s_instance = new EngineLoop();
    }
    return s_instance;
}

void EngineLoop::Start() {
    started_ = true;
    update_enabled_ = true;
}

void EngineLoop::Tick(float delta) {
    if (!started_)
        return;
    ::Application->Run();
    CompleteInputFrame();
    TVPRepairKagNoTransWait();
    TVPRepairKagEnvironmentWorldReset();
    TVPDeliverContinuousEvent();
    iTVPTexture2D::RecycleProcess();
    // Legacy VideoOverlay layer playback publishes decoded frames from
    // continuous callbacks. Present after them so those frames are not left
    // waiting for an unrelated window update or input event.
    TVPHostForceDrawDeviceShow();
    if (s_postUpdate)
        s_postUpdate();
}

void EngineLoop::CompleteInputFrame() {
    // Mouse-up and click are queued together.  Keep System.getKeyState(...,
    // true) pressed while Application::Run() invokes those handlers, then
    // release it before the next host input frame.  engine_api drives
    // Application::Run() directly, so this cannot live only in Tick().
    constexpr uint16_t mouse_vks[] = {0x01, 0x02, 0x04};
    for (const uint16_t vk : mouse_vks) {
        if ((pending_mouse_release_mask_ & (1u << vk)) != 0)
            s_scancode[vk] &= ~1;
    }
    pending_mouse_release_mask_ = 0;
}

void EngineLoop::ResetPointerState() {
    active_mouse_shift_flags_ = 0;
    pending_mouse_release_mask_ = 0;
    suppress_next_left_click_ = false;
    last_mouse_down_x_ = 0;
    last_mouse_down_y_ = 0;
    last_click_time_ms_ = 0;
    last_click_x_ = 0;
    last_click_y_ = 0;
    constexpr uint16_t mouse_vks[] = {0x01, 0x02, 0x04};
    for (const uint16_t vk : mouse_vks)
        s_scancode[vk] = 0;
}

bool EngineLoop::StartupFrom(const std::string& path) {
    if (!TVPCheckStartupPath(path)) {
        return false;
    }

    IndividualConfigManager* pCfgMgr = IndividualConfigManager::GetInstance();
    auto sepPos = path.find_last_of("/\\");
    if (sepPos != std::string::npos) {
        pCfgMgr->UsePreferenceAt(path.substr(0, sepPos));
    }

    DoStartup(path);
    return true;
}

void EngineLoop::DoStartup(const std::string& path) {
    spdlog::info("EngineLoop::DoStartup starting game from: {}", path);

    ::Application->StartApplication(path);

#ifdef __APPLE__
    malloc_zone_pressure_relief(nullptr, 0);
#endif

    // Run one frame immediately (matches original behavior)
    Tick(0);

    started_ = true;
    update_enabled_ = true;

    spdlog::info("EngineLoop::DoStartup complete");
}

// ---------------------------------------------------------------------------
// Input event handling
// ---------------------------------------------------------------------------

uint32_t EngineLoop::ConvertModifiers(int32_t modifiers) {
    // engine_api modifiers use the same bit layout as TVP_SS_* flags:
    //   bit 0 = Shift  (TVP_SS_SHIFT = 0x01)
    //   bit 1 = Alt    (TVP_SS_ALT   = 0x02)
    //   bit 2 = Ctrl   (TVP_SS_CTRL  = 0x04)
    //   bit 3 = Left   (TVP_SS_LEFT  = 0x08)
    //   bit 4 = Right  (TVP_SS_RIGHT = 0x10)
    //   bit 5 = Middle (TVP_SS_MIDDLE= 0x20)
    return static_cast<uint32_t>(modifiers) & 0xFF;
}

bool EngineLoop::HandleInputEvent(const EngineInputEvent& event) {
    switch (event.type) {
        case kEngineInputPointerDown:
            HandlePointerDown(event);
            return true;
        case kEngineInputPointerMove:
            HandlePointerMove(event);
            return true;
        case kEngineInputPointerUp:
            HandlePointerUp(event);
            return true;
        case kEngineInputPointerScroll:
            HandlePointerScroll(event);
            return true;
        case kEngineInputKeyDown:
            HandleKeyDown(event);
            return true;
        case kEngineInputKeyUp:
            HandleKeyUp(event);
            return true;
        case kEngineInputTextInput:
            HandleTextInput(event);
            return true;
        case kEngineInputBack:
            // Treat "Back" as Escape key press
            HandleKeyDown(event);
            return true;
        default:
            spdlog::warn("EngineLoop::HandleInputEvent: unknown event type {}",
                         event.type);
            return false;
    }
}

bool EngineLoop::IsTouchPointerEvent(const EngineInputEvent& event) {
    return event.pointer_id >= 100000;
}

void EngineLoop::HandlePointerDown(const EngineInputEvent& event) {
    auto* win = TVPMainWindow;
    if (!win) return;

    const tjs_int x = static_cast<tjs_int>(event.x);
    const tjs_int y = static_cast<tjs_int>(event.y);
    const uint32_t shift = ConvertModifiers(event.modifiers);

    // Update cached cursor position for Layer.cursorX/cursorY queries
    if (win->GetForm())
        win->GetForm()->UpdateCursorPos(x, y);

    // Map button index: 0=left, 1=right, 2=middle (matches tTVPMouseButton)
    tTVPMouseButton mb = mbLeft;
    if (event.button == 1)
        mb = mbRight;
    else if (event.button == 2)
        mb = mbMiddle;

    // Update scancode for mouse button async state
    uint16_t vk = 0;
    switch (mb) {
        case mbLeft:   vk = 0x01; break; // VK_LBUTTON
        case mbRight:  vk = 0x02; break; // VK_RBUTTON
        case mbMiddle: vk = 0x04; break; // VK_MBUTTON
        default: break;
    }
    if (vk < sizeof(s_scancode) / sizeof(s_scancode[0])) {
        s_scancode[vk] = 0x11; // pressed + was-pressed
        pending_mouse_release_mask_ &= ~(1u << vk);
    }

    // Combine mouse button state into shift flags
    uint32_t button_flag = 0;
    switch (mb) {
        case mbLeft:   button_flag = TVP_SS_LEFT;   break;
        case mbRight:  button_flag = TVP_SS_RIGHT;  break;
        case mbMiddle: button_flag = TVP_SS_MIDDLE; break;
        default: break;
    }
    active_mouse_shift_flags_ |= button_flag;
    const uint32_t flags = shift | active_mouse_shift_flags_;

    last_mouse_down_x_ = x;
    last_mouse_down_y_ = y;
    AETHER_INPUT_TRACE_LOG("EngineLoop down id=%d x=%d y=%d button=%u flags=%u",
                           event.pointer_id, x, y, event.button, flags);

    // Windows sends WM_LBUTTONDBLCLK before the second WM_LBUTTONDOWN, and the
    // following WM_LBUTTONUP suppresses the normal click event.
    if (mb == mbLeft && !IsTouchPointerEvent(event)) {
        const int64_t now = NowMs();
        const int32_t dx = x - last_click_x_;
        const int32_t dy = y - last_click_y_;
        const bool is_double_click =
            last_click_time_ms_ > 0 && now - last_click_time_ms_ <= 500 &&
            dx * dx + dy * dy <= 64;
        suppress_next_left_click_ = is_double_click;
        if (is_double_click) {
            if (InputTraceEnabled()) {
                spdlog::info("EngineLoop pointer double-click x={} y={}", x, y);
            }
            TVPPostInputEvent(new tTVPOnDoubleClickInputEvent(win, x, y));
            last_click_time_ms_ = 0;
        }
    }

    TVPPostInputEvent(
        new tTVPOnMouseDownInputEvent(win, x, y, mb, flags));
}

void EngineLoop::HandlePointerMove(const EngineInputEvent& event) {
    auto* win = TVPMainWindow;
    if (!win) return;

    const tjs_int x = static_cast<tjs_int>(event.x);
    const tjs_int y = static_cast<tjs_int>(event.y);
    const uint32_t shift =
        ConvertModifiers(event.modifiers) | active_mouse_shift_flags_;

    // Update cached cursor position for Layer.cursorX/cursorY queries
    if (win->GetForm())
        win->GetForm()->UpdateCursorPos(x, y);

    AETHER_INPUT_TRACE_LOG("EngineLoop move id=%d x=%d y=%d shift=%u",
                           event.pointer_id, x, y, shift);

    TVPPostInputEvent(
        new tTVPOnMouseMoveInputEvent(win, x, y, shift),
        TVP_EPT_REMOVE_POST);
}

void EngineLoop::HandlePointerUp(const EngineInputEvent& event) {
    auto* win = TVPMainWindow;
    if (!win) return;

    const tjs_int x = static_cast<tjs_int>(event.x);
    const tjs_int y = static_cast<tjs_int>(event.y);
    uint32_t button_flag = 0;

    // Update cached cursor position for Layer.cursorX/cursorY queries
    if (win->GetForm())
        win->GetForm()->UpdateCursorPos(x, y);

    tTVPMouseButton mb = mbLeft;
    if (event.button == 1)
        mb = mbRight;
    else if (event.button == 2)
        mb = mbMiddle;

    switch (mb) {
        case mbLeft:   button_flag = TVP_SS_LEFT;   break;
        case mbRight:  button_flag = TVP_SS_RIGHT;  break;
        case mbMiddle: button_flag = TVP_SS_MIDDLE; break;
        default: break;
    }
    active_mouse_shift_flags_ &= ~button_flag;
    const uint32_t shift =
        (ConvertModifiers(event.modifiers) & ~button_flag) |
        active_mouse_shift_flags_;

    // Defer scancode release until Application::Run has delivered the queued
    // up/click events. Some KAG widgets query async mouse state in handlers.
    uint16_t vk = 0;
    switch (mb) {
        case mbLeft:   vk = 0x01; break;
        case mbRight:  vk = 0x02; break;
        case mbMiddle: vk = 0x04; break;
        default: break;
    }

    // Match the existing AetherKiri path: click uses the mouse-down
    // coordinates before mouse-up releases transient button layers.
    if (mb == mbLeft) {
        if (suppress_next_left_click_) {
            suppress_next_left_click_ = false;
        } else {
            if (InputTraceEnabled()) {
                spdlog::info("EngineLoop pointer click x={} y={}",
                             last_mouse_down_x_, last_mouse_down_y_);
            }
            TVPPostInputEvent(
                new tTVPOnClickInputEvent(win, last_mouse_down_x_,
                                          last_mouse_down_y_));
            AETHER_INPUT_TRACE_LOG(
                "EngineLoop click id=%d x=%d y=%d up=(%d,%d)",
                event.pointer_id, last_mouse_down_x_, last_mouse_down_y_, x, y);
            last_click_time_ms_ = NowMs();
            last_click_x_ = x;
            last_click_y_ = y;
        }
    }

    AETHER_INPUT_TRACE_LOG("EngineLoop up id=%d x=%d y=%d button=%u shift=%u",
                           event.pointer_id, x, y, event.button, shift);

    TVPPostInputEvent(
        new tTVPOnMouseUpInputEvent(win, x, y, mb, shift));

    if (vk != 0)
        pending_mouse_release_mask_ |= (1u << vk);
}

void EngineLoop::HandlePointerScroll(const EngineInputEvent& event) {
    auto* win = TVPMainWindow;
    if (!win) return;

    const tjs_int x = static_cast<tjs_int>(event.x);
    const tjs_int y = static_cast<tjs_int>(event.y);
    const uint32_t shift =
        ConvertModifiers(event.modifiers) | active_mouse_shift_flags_;

    // delta_y > 0 = scroll up, delta_y < 0 = scroll down
    // TVP expects wheel delta in units (positive = up)
    const tjs_int delta = static_cast<tjs_int>(event.delta_y * 120.0);

    AETHER_INPUT_TRACE_LOG(
        "EngineLoop scroll id=%d x=%d y=%d delta_y=%.3f delta=%d shift=%u",
        event.pointer_id, x, y, event.delta_y, delta, shift);

    if (delta != 0) {
        TVPPostInputEvent(
            new tTVPOnMouseWheelInputEvent(win, shift, delta, x, y));
    }
}

void EngineLoop::HandleKeyDown(const EngineInputEvent& event) {
    auto* win = TVPMainWindow;
    if (!win) return;

    tjs_uint key = static_cast<tjs_uint>(event.key_code);

    // For BACK button, map to VK_ESCAPE
    if (event.type == kEngineInputBack) {
        key = 0x1B; // VK_ESCAPE
    }

    const uint32_t shift = ConvertModifiers(event.modifiers);

    // Update scancode state
    if (key < sizeof(s_scancode) / sizeof(s_scancode[0])) {
        s_scancode[key] = 0x11; // pressed + was-pressed
    }

    TVPPostInputEvent(
        new tTVPOnKeyDownInputEvent(win, key, shift));
}

void EngineLoop::HandleKeyUp(const EngineInputEvent& event) {
    auto* win = TVPMainWindow;
    if (!win) return;

    const tjs_uint key = static_cast<tjs_uint>(event.key_code);
    const uint32_t shift = ConvertModifiers(event.modifiers);

    // Update scancode: clear pressed bit
    if (key < sizeof(s_scancode) / sizeof(s_scancode[0])) {
        s_scancode[key] &= ~1;
    }

    TVPPostInputEvent(
        new tTVPOnKeyUpInputEvent(win, key, shift));
}

void EngineLoop::HandleTextInput(const EngineInputEvent& event) {
    auto* win = TVPMainWindow;
    if (!win) return;

    const uint32_t codepoint = event.unicode_codepoint;
    if(codepoint == 0 || codepoint > 0x10FFFF)
        return;

    // Android's Godot text bridge reports a non-BMP character as two UTF-16
    // code units, while other hosts can report the complete Unicode scalar.
    // Preserve either representation for KiriKiri's UTF-16 input events.
    if(codepoint <= 0xFFFF) {
        TVPPostInputEvent(new tTVPOnKeyPressInputEvent(
            win, static_cast<tjs_char>(codepoint)));
        return;
    }

    const uint32_t scalar = codepoint - 0x10000;
    const tjs_char high = static_cast<tjs_char>(0xD800 + (scalar >> 10));
    const tjs_char low = static_cast<tjs_char>(0xDC00 + (scalar & 0x3FF));
    TVPPostInputEvent(new tTVPOnKeyPressInputEvent(win, high));
    TVPPostInputEvent(new tTVPOnKeyPressInputEvent(win, low));
}
