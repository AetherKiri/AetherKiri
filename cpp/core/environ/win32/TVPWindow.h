
#ifndef __TVP_WINDOW_H__
#define __TVP_WINDOW_H__

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <mutex>
#include <vector>
#if 0
#include <shellapi.h>
#include <oleidl.h> // for MK_ALT
#include "tvpinputdefs.h"
#include "SystemImpl.h"
#include "ImeControl.h"

#ifndef MK_ALT
#define MK_ALT (0x20)
#endif
#endif
enum {
    ssShift = TVP_SS_SHIFT,
    ssAlt = TVP_SS_ALT,
    ssCtrl = TVP_SS_CTRL,
    ssLeft = TVP_SS_LEFT,
    ssRight = TVP_SS_RIGHT,
    ssMiddle = TVP_SS_MIDDLE,
    ssDouble = TVP_SS_DOUBLE,
    ssRepeat = TVP_SS_REPEAT,
};
#if 0
class tTVPWindow {
	WNDCLASSEX	wc_;
	bool		created_;

protected:
	enum CloseAction {
	  caNone,
	  caHide,
	  caFree,
	  caMinimize
	};
	enum FormState {
		fsCreating,
		fsVisible,
		fsShowing,
		fsModal,
		fsCreatedMDIChild,
		fsActivated
	};

	HWND				window_handle_;

	std::wstring	window_class_name_;
	std::wstring	window_title_;
	SIZE		window_client_size_;
	SIZE		min_size_;
	SIZE		max_size_;
	int			border_style_;
	bool		in_window_;
	bool		ignore_touch_mouse_;

	bool in_mode_;
	int modal_result_;

	bool has_parent_;

	static const UINT SIZE_CHANGE_FLAGS;
	static const UINT POS_CHANGE_FLAGS;
	static const DWORD DEFAULT_EX_STYLE;
	static const ULONG REGISTER_TOUCH_FLAG;
	static const DWORD DEFAULT_TABLETPENSERVICE_PROPERTY;
	static const DWORD MI_WP_SIGNATURE;
	static const DWORD SIGNATURE_MASK;

	bool left_double_click_;

	ImeControl* ime_control_;

	enum ModeFlag {
		FALG_FULLSCREEN = 0x01,
	};
	
	unsigned long flags_;
	void SetFlag( unsigned long f ) {
		flags_ |= f;
	}
	void ResetFlag( unsigned long f ) {
		flags_ &= ~f;
	}
	bool GetFlag( unsigned long f ) {
		return 0 != (flags_ & f);
	}
	
	void UnregisterWindow();

	// window procedure
	static LRESULT WINAPI WndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

	virtual LRESULT WINAPI Proc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

	HRESULT CreateWnd( const std::wstring& classname, const std::wstring& title, int width, int height, HWND hParent=nullptr );

	virtual void OnDestroy();
	virtual void OnPaint();

	inline int GetAltKeyState() const {
		if( ::GetKeyState( VK_MENU  ) < 0 ) {
			return MK_ALT;
		} else {
			return 0;
		}
	}
	inline int GetShiftState( WPARAM wParam ) const {
		int shift = GET_KEYSTATE_WPARAM(wParam) & (MK_SHIFT|MK_CONTROL);
		shift |= GetAltKeyState();
		return shift;
	}
	inline int GetShiftState() const {
		int shift = 0;
		if( ::GetKeyState( VK_MENU  ) < 0 ) shift |= MK_ALT;
		if( ::GetKeyState( VK_SHIFT ) < 0 ) shift |= MK_SHIFT;
		if( ::GetKeyState( VK_CONTROL ) < 0 ) shift |= MK_CONTROL;
		return shift;
	}
	inline int GetMouseButtonState() const {
		int s = 0;
		if(TVPGetAsyncKeyState(VK_LBUTTON)) s |= ssLeft;
		if(TVPGetAsyncKeyState(VK_RBUTTON)) s |= ssRight;
		if(TVPGetAsyncKeyState(VK_MBUTTON)) s |= ssMiddle;
		return s;
	}
	inline bool IsTouchEvent(LPARAM extraInfo) const {
		return (extraInfo & SIGNATURE_MASK) == MI_WP_SIGNATURE;
	}

	void SetMouseCapture() {
		::SetCapture( GetHandle() );
	}
	void ReleaseMouseCapture() {
		::ReleaseCapture();
	}
	HICON GetBigIcon();

	static bool HasMenu( HWND hWnd );
public:
	tTVPWindow()
	: window_handle_(nullptr), created_(false), left_double_click_(false), ime_control_(nullptr), border_style_(0), modal_result_(0),
		in_window_(false), ignore_touch_mouse_(false), in_mode_(false), has_parent_(false) {
		min_size_.cx = min_size_.cy = 0;
		max_size_.cx = max_size_.cy = 0;
	}
	virtual ~tTVPWindow();

	bool HasFocus() const {
		return window_handle_ == ::GetFocus();
	}
	bool IsValidHandle() const {
		return ( window_handle_ != nullptr && window_handle_ != INVALID_HANDLE_VALUE && ::IsWindow(window_handle_) );
	}

	virtual bool Initialize();

	void SetWidnowTitle( const std::wstring& title );
	void SetScreenSize( int width, int height );

	HWND GetHandle() { return window_handle_; }
	HWND GetHandle() const { return window_handle_; }

	ImeControl* GetIME() { return ime_control_; }
	const ImeControl* GetIME() const { return ime_control_; }

	static void SetClientSize( HWND hWnd, SIZE& size );

//-- properties
	bool GetVisible() const;
	void SetVisible(bool s);
	void Show() { SetVisible( true ); BringToFront(); }
	void Hide() { SetVisible( false ); }

	bool GetEnable() const;
	void SetEnable( bool s );

	void GetCaption( std::wstring& v ) const;
	void SetCaption( const std::wstring& v );
	
	void SetBorderStyle( enum tTVPBorderStyle st);
	enum tTVPBorderStyle GetBorderStyle() const;

	void SetWidth( int w );
	int GetWidth() const;
	void SetHeight( int h );
	int GetHeight() const;
	void SetSize( int w, int h );
	void GetSize( int &w, int &h );

	void SetLeft( int l );
	int GetLeft() const;
	void SetTop( int t );
	int GetTop() const;
	void SetPosition( int l, int t );
	
	void SetBounds( int x, int y, int width, int height );

	void SetMinWidth( int v ) {
		min_size_.cx = v;
		CheckMinMaxSize();
	}
	int GetMinWidth() const{ return min_size_.cx; }
	void SetMinHeight( int v ) {
		min_size_.cy = v;
		CheckMinMaxSize();
	}
	int GetMinHeight() const { return min_size_.cy; }
	void SetMinSize( int w, int h ) {
		min_size_.cx = w;
		min_size_.cy = h;
		CheckMinMaxSize();
	}

	void SetMaxWidth( int v ) {
		max_size_.cx = v;
		CheckMinMaxSize();
	}
	int GetMaxWidth() const{ return max_size_.cx; }
	void SetMaxHeight( int v ) {
		max_size_.cy = v;
		CheckMinMaxSize();
	}
	int GetMaxHeight() const{ return max_size_.cy; }
	void SetMaxSize( int w, int h ) {
		max_size_.cx = w;
		max_size_.cy = h;
		CheckMinMaxSize();
	}
	void CheckMinMaxSize() {
		int maxw = max_size_.cx;
		int maxh = max_size_.cy;
		int minw = min_size_.cx;
		int minh = min_size_.cy;
		int dw, dh;
		GetSize( dw, dh );
		int sw = dw;
		int sh = dh;
		if( maxw > 0 && dw > maxw ) dw = maxw;
		if( maxh > 0 && dh > maxh ) dh = maxh;
		if( minw > 0 && dw < minw ) dw = minw;
		if( minh > 0 && dh < minh ) dh = minh;
		if( sw != dw || sh != dh ) {
			SetSize( dw, dh );
		}
	}

	void SetInnerWidth( int w );
	int GetInnerWidth() const;
	void SetInnerHeight( int h );
	int GetInnerHeight() const;
	void SetInnerSize( int w, int h );
	
	void BringToFront();
	void SetStayOnTop( bool b );
	bool GetStayOnTop() const;

	int ShowModal();
	void closeModal();
	bool IsModal() const { return in_mode_; }
	void Close();

	void GetClientRect( struct tTVPRect& rt );

	// メッセージハンドラ
	virtual void OnActive( HWND preactive ) {}
	virtual void OnDeactive( HWND postactive ) {}
	virtual void OnClose( CloseAction& action ){}
	virtual bool OnCloseQuery() { return true; }
	virtual void OnFocus(HWND hFocusLostWnd) {}
	virtual void OnFocusLost(HWND hFocusingWnd) {}
	virtual void OnMouseDown( int button, int shift, int x, int y ){}
	virtual void OnMouseUp( int button, int shift, int x, int y ){}
	virtual void OnMouseMove( int shift, int x, int y ){}
	virtual void OnMouseDoubleClick( int button, int x, int y ) {}
	virtual void OnMouseClick( int button, int shift, int x, int y ){}
	virtual void OnMouseWheel( int delta, int shift, int x, int y ){}
	virtual void OnKeyUp( WORD vk, int shift ){}
	virtual void OnKeyDown( WORD vk, int shift, int repeat, bool prevkeystate ){}
	virtual void OnKeyPress( WORD vk, int repeat, bool prevkeystate, bool convertkey ){}
	virtual void OnMove( int x, int y ) {}
	virtual void OnResize( UINT_PTR state, int w, int h ) {}
	virtual void OnDropFile( HDROP hDrop ) {}
	virtual int OnMouseActivate( HWND hTopLevelParentWnd, WORD hitTestCode, WORD MouseMsg ) { return MA_ACTIVATE; }
	virtual bool OnSetCursor( HWND hContainsCursorWnd, WORD hitTestCode, WORD MouseMsg ) { return false; }
	virtual void OnEnable( bool enabled ) {}
	virtual void OnEnterMenuLoop( bool entered ) {}
	virtual void OnExitMenuLoop( bool isShortcutMenu ) {}
	virtual void OnDeviceChange( UINT_PTR event, void *data ) {}
	virtual void OnNonClientMouseDown( int button, UINT_PTR hittest, int x, int y ){}
	virtual void OnMouseEnter() {}
	virtual void OnMouseLeave() {}
	virtual void OnShow( UINT_PTR status ) {}
	virtual void OnHide( UINT_PTR status ) {}

	virtual void OnTouchDown( double x, double y, double cx, double cy, DWORD id, DWORD tick ) {}
	virtual void OnTouchMove( double x, double y, double cx, double cy, DWORD id, DWORD tick ) {}
	virtual void OnTouchUp( double x, double y, double cx, double cy, DWORD id, DWORD tick ) {}
	virtual void OnTouchSequenceStart() {}
	virtual void OnTouchSequenceEnd() {}

	virtual void OnDisplayChange( UINT_PTR bpp, WORD hres, WORD vres ) {}
	virtual void OnApplicationActivateChange( bool activated, DWORD thread_id ) {}
};
#endif

// Window-message receiver ABI shared by the Win32 and portable host forms.
// The original krkrz header only exposed this through the Windows SDK.  Keep
// the payload integer-sized on every target so plug-ins can use the same
// receiver callback when the host window is backed by SDL/Godot.
struct tTVPWindowMessage {
    tjs_uint32 Msg = 0;
    tjs_uint64 WParam = 0;
    tjs_uint64 LParam = 0;
    tjs_uint64 Result = 0;
};

#if defined(_WIN32)
using tTVPWindowMessageReceiver = bool(__stdcall *)(
    void *userdata, tTVPWindowMessage *message);
#else
using tTVPWindowMessageReceiver = bool (*)(
    void *userdata, tTVPWindowMessage *message);
#endif

enum tTVPWMRRegMode { wrmRegister = 0, wrmUnregister = 1 };
#ifndef TVP_WM_USER
#define TVP_WM_USER 0x8000
#endif
#ifndef TVP_WM_DETACH
#define TVP_WM_DETACH (TVP_WM_USER + 106)
#endif
#ifndef TVP_WM_ATTACH
#define TVP_WM_ATTACH (TVP_WM_USER + 107)
#endif
#ifndef TVP_WM_FULLSCREEN_CHANGING
#define TVP_WM_FULLSCREEN_CHANGING (TVP_WM_USER + 108)
#endif
#ifndef TVP_WM_FULLSCREEN_CHANGED
#define TVP_WM_FULLSCREEN_CHANGED (TVP_WM_USER + 109)
#endif
enum {
    orientUnknown,
    orientPortrait,
    orientLandscape,
};

// Scene-tree based rendering is removed; Godot handles display.
using TVPOverlayNode = void;

class iWindowLayer {
protected:
    // The original Win32 form keeps these values in its HWND/WinForms
    // objects.  The portable host has no native window to query, therefore
    // retain the same state explicitly and expose it through the existing
    // Window API.  This is deliberately independent of the renderer so a
    // headless/test host observes the same semantics as a visible host.
    mutable std::mutex WindowStateMutex;
    tTVPMouseCursorState MouseCursorState = mcsVisible;
    tjs_int HintDelay = 500;
    tjs_int ZoomDenom = 1; // Zooming factor denominator (setting)
    tjs_int ZoomNumer = 1; // Zooming factor numerator (setting)
    double TouchScaleThreshold = 5, TouchRotateThreshold = 5;
    tjs_int WindowLeft = 0;
    tjs_int WindowTop = 0;
    tjs_int MinWidth = 0;
    tjs_int MinHeight = 0;
    tjs_int MaxWidth = 0;
    tjs_int MaxHeight = 0;
    tjs_int LayerLeft = 0;
    tjs_int LayerTop = 0;
    tTVPBorderStyle BorderStyle = bsNone;
    bool StayOnTop = false;
    bool FullScreenMode = false;
    bool TrapKey = false;
    bool Focusable = true;
    bool EnableTouch = false;
    bool InnerSunken = false;
    bool ShowScrollBars = true;
    bool UseMouseKey = false;
    tjs_int MouseCursor = 0;
    ttstr HintText;

    struct PortableTouchPoint {
        tjs_uint32 id = 0;
        tjs_real startX = 0;
        tjs_real startY = 0;
        tjs_real x = 0;
        tjs_real y = 0;
        float velocityX = 0;
        float velocityY = 0;
        float velocity = 0;
        std::chrono::steady_clock::time_point timestamp{};
    };
    std::vector<PortableTouchPoint> TouchPoints;
    tjs_int MouseX = 0;
    tjs_int MouseY = 0;
    float MouseVelocityX = 0;
    float MouseVelocityY = 0;
    float MouseVelocity = 0;
    std::chrono::steady_clock::time_point MouseTimestamp{};

    struct PortableMessageReceiver {
        tTVPWindowMessageReceiver Proc = nullptr;
        const void *UserData = nullptr;
    };
    mutable std::mutex WindowMessageMutex;
    std::vector<PortableMessageReceiver> WindowMessageReceivers;

public:
    virtual ~iWindowLayer() = default;
    // Portable hosts can translate logical state changes into the same
    // integer-sized receiver messages that Win32 forms emit.  Native forms
    // keep the default no-op and continue to use their real window message
    // pump.
    virtual void OnWindowStateChanged(tjs_uint32, tjs_uint64, tjs_uint64) {}
    virtual void SetPaintBoxSize(tjs_int w, tjs_int h) = 0;
    virtual bool GetFormEnabled() = 0;
    virtual void SetDefaultMouseCursor() = 0;
    virtual void GetCursorPos(tjs_int &x, tjs_int &y) = 0;
    virtual void SetCursorPos(tjs_int x, tjs_int y) = 0;
    // Update cached cursor position (called from EngineLoop on mouse events).
    // Besides the position this records a small velocity sample, matching the
    // native form's VelocityTracker enough for scripts that use drag inertia.
    virtual void UpdateCursorPos(tjs_int x, tjs_int y) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        const auto now = std::chrono::steady_clock::now();
        if(MouseTimestamp.time_since_epoch().count() != 0) {
            const double seconds = std::chrono::duration<double>(
                now - MouseTimestamp).count();
            if(seconds > 0.000001 && seconds < 2.0) {
                MouseVelocityX = static_cast<float>((x - MouseX) / seconds);
                MouseVelocityY = static_cast<float>((y - MouseY) / seconds);
                MouseVelocity = std::hypot(MouseVelocityX, MouseVelocityY);
            }
        }
        MouseX = x;
        MouseY = y;
        MouseTimestamp = now;
    }
    virtual void SetHintText(const ttstr &text) = 0;
    virtual void SetAttentionPoint(tjs_int left, tjs_int top,
                                   const struct tTVPFont *font,
                                   iTJSDispatch2 *attention_owner) = 0;
    virtual void ZoomRectangle(tjs_int &left, tjs_int &top, tjs_int &right,
                               tjs_int &bottom) = 0;
    virtual void BringToFront() = 0;
    virtual void ShowWindowAsModal() = 0;
    virtual bool GetVisible() = 0;
    virtual void SetVisible(bool bVisible) = 0;
    virtual const char *GetCaption() = 0;
    virtual void SetCaption(const std::string &) = 0;
    virtual void SetWidth(tjs_int w) = 0;
    virtual void SetHeight(tjs_int h) = 0;
    virtual void SetSize(tjs_int w, tjs_int h) = 0;
    virtual void GetSize(tjs_int &w, tjs_int &h) = 0;
    [[nodiscard]] virtual tjs_int GetWidth() const = 0;
    [[nodiscard]] virtual tjs_int GetHeight() const = 0;
    virtual void GetWinSize(tjs_int &w, tjs_int &h) = 0;
    virtual void SetZoom(tjs_int numer, tjs_int denom) = 0;
    virtual void UpdateDrawBuffer(iTVPTexture2D *tex) = 0;
#if 0
	virtual void AddOverlay(tTJSNI_BaseVideoOverlay *ovl) = 0;
	virtual void RemoveOverlay(tTJSNI_BaseVideoOverlay *ovl) = 0;
	virtual void UpdateOverlay() = 0;
#endif
    virtual void InvalidateClose() = 0;
    virtual bool GetWindowActive() = 0;
    virtual void Close() = 0;
    virtual void OnCloseQueryCalled(bool b) = 0;
    virtual void InternalKeyDown(tjs_uint16 key, tjs_uint32 shift) = 0;
    virtual void OnKeyUp(tjs_uint16 vk, int shift) = 0;
    virtual void OnKeyPress(tjs_uint16 vk, int repeat, bool prevkeystate,
                            bool convertkey) = 0;
    [[nodiscard]] virtual tTVPImeMode GetDefaultImeMode() const = 0;
    virtual void SetImeMode(tTVPImeMode mode) = 0;
    virtual void ResetImeMode() = 0;
    virtual void UpdateWindow(tTVPUpdateType type) = 0;
    virtual void SetVisibleFromScript(bool b) = 0;
    virtual void SetUseMouseKey(bool b) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        UseMouseKey = b;
    }
    [[nodiscard]] virtual bool GetUseMouseKey() const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return UseMouseKey;
    }
    virtual void ResetMouseVelocity() {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        MouseVelocityX = MouseVelocityY = MouseVelocity = 0;
        MouseTimestamp = std::chrono::steady_clock::time_point{};
    }
    virtual void ResetTouchVelocity(tjs_int id) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        for(auto &point : TouchPoints) {
            if(static_cast<tjs_int>(point.id) == id) {
                point.velocityX = point.velocityY = point.velocity = 0;
                point.timestamp = std::chrono::steady_clock::now();
            }
        }
    }
    virtual bool GetMouseVelocity(float &x, float &y, float &speed) const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        x = MouseVelocityX;
        y = MouseVelocityY;
        speed = MouseVelocity;
        return MouseTimestamp.time_since_epoch().count() != 0;
    }
    virtual void TickBeat() = 0;
    virtual TVPOverlayNode *GetPrimaryArea() = 0;

    void SetZoomNumer(tjs_int n) { SetZoom(n, ZoomDenom); }
    [[nodiscard]] tjs_int GetZoomNumer() const { return ZoomNumer; }
    void SetZoomDenom(tjs_int d) { SetZoom(ZoomNumer, d); }
    [[nodiscard]] tjs_int GetZoomDenom() const { return ZoomDenom; }

    // Register a plug-in receiver for the portable host window.  Win32 forms
    // keep their native receiver chain; HostWindowLayer uses this same ABI so
    // messenger/msgreceiver and third-party plug-ins do not silently lose
    // messages on macOS/Linux/Android.
    void RegisterWindowMessageReceiver(tTVPWMRRegMode mode, void *proc,
                                       const void *userdata) {
        const auto receiver = reinterpret_cast<tTVPWindowMessageReceiver>(proc);
        if(!receiver)
            return;
        std::lock_guard<std::mutex> lock(WindowMessageMutex);
        if(mode == wrmRegister) {
            for(const auto &item : WindowMessageReceivers) {
                if(item.Proc == receiver && item.UserData == userdata)
                    return;
            }
            WindowMessageReceivers.push_back({receiver, userdata});
        } else if(mode == wrmUnregister) {
            WindowMessageReceivers.erase(
                std::remove_if(WindowMessageReceivers.begin(),
                               WindowMessageReceivers.end(),
                               [receiver, userdata](
                                   const PortableMessageReceiver &item) {
                                   return item.Proc == receiver &&
                                          item.UserData == userdata;
                               }),
                WindowMessageReceivers.end());
        }
    }

    // Dispatch a synthetic/native message through the registered receiver
    // chain.  A copy of the chain is used so callbacks may unregister while
    // handling a message without invalidating the iteration.
    bool DeliverWindowMessage(tTVPWindowMessage &message) {
        std::vector<PortableMessageReceiver> receivers;
        {
            std::lock_guard<std::mutex> lock(WindowMessageMutex);
            receivers = WindowMessageReceivers;
        }
        bool blocked = false;
        for(const auto &item : receivers) {
            if(item.Proc)
                blocked = item.Proc(const_cast<void *>(item.UserData),
                                    &message) || blocked;
        }
        return blocked;
    }
    void SetLeft(tjs_int value) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        WindowLeft = value;
    }
    void SetTop(tjs_int value) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        WindowTop = value;
    }
    void SetMinWidth(tjs_int value) {
        {
            std::lock_guard<std::mutex> lock(WindowStateMutex);
            MinWidth = std::max<tjs_int>(0, value);
            if(MaxWidth > 0 && MinWidth > MaxWidth)
                MaxWidth = MinWidth;
        }
        tjs_int w = 0, h = 0;
        GetSize(w, h);
        ConstrainSize(w, h);
        SetSize(w, h);
    }
    void SetMaxWidth(tjs_int value) {
        {
            std::lock_guard<std::mutex> lock(WindowStateMutex);
            MaxWidth = std::max<tjs_int>(0, value);
            if(MaxWidth > 0 && MinWidth > MaxWidth)
                MinWidth = MaxWidth;
        }
        tjs_int w = 0, h = 0;
        GetSize(w, h);
        ConstrainSize(w, h);
        SetSize(w, h);
    }
    void SetMinHeight(tjs_int value) {
        {
            std::lock_guard<std::mutex> lock(WindowStateMutex);
            MinHeight = std::max<tjs_int>(0, value);
            if(MaxHeight > 0 && MinHeight > MaxHeight)
                MaxHeight = MinHeight;
        }
        tjs_int w = 0, h = 0;
        GetSize(w, h);
        ConstrainSize(w, h);
        SetSize(w, h);
    }
    void SetMaxHeight(tjs_int value) {
        {
            std::lock_guard<std::mutex> lock(WindowStateMutex);
            MaxHeight = std::max<tjs_int>(0, value);
            if(MaxHeight > 0 && MinHeight > MaxHeight)
                MinHeight = MaxHeight;
        }
        tjs_int w = 0, h = 0;
        GetSize(w, h);
        ConstrainSize(w, h);
        SetSize(w, h);
    }
    void SetInnerWidth(tjs_int v) { SetWidth(v); }
    void SetInnerHeight(tjs_int v) { SetHeight(v); }
    void SetInnerSize(tjs_int w, tjs_int h) { SetSize(w, h); }
    void SetMinSize(tjs_int w, tjs_int h) {
        {
            std::lock_guard<std::mutex> lock(WindowStateMutex);
            MinWidth = std::max<tjs_int>(0, w);
            MinHeight = std::max<tjs_int>(0, h);
            if(MaxWidth > 0 && MinWidth > MaxWidth)
                MaxWidth = MinWidth;
            if(MaxHeight > 0 && MinHeight > MaxHeight)
                MaxHeight = MinHeight;
        }
        tjs_int currentW = 0, currentH = 0;
        GetSize(currentW, currentH);
        ConstrainSize(currentW, currentH);
        SetSize(currentW, currentH);
    }
    void SetMaxSize(tjs_int w, tjs_int h) {
        {
            std::lock_guard<std::mutex> lock(WindowStateMutex);
            MaxWidth = std::max<tjs_int>(0, w);
            MaxHeight = std::max<tjs_int>(0, h);
            if(MaxWidth > 0 && MinWidth > MaxWidth)
                MinWidth = MaxWidth;
            if(MaxHeight > 0 && MinHeight > MaxHeight)
                MinHeight = MaxHeight;
        }
        tjs_int currentW = 0, currentH = 0;
        GetSize(currentW, currentH);
        ConstrainSize(currentW, currentH);
        SetSize(currentW, currentH);
    }
    void SetPosition(tjs_int l, tjs_int t) {
        {
            std::lock_guard<std::mutex> lock(WindowStateMutex);
            WindowLeft = l;
            WindowTop = t;
        }
        const tjs_uint64 packed =
            (static_cast<tjs_uint64>(static_cast<tjs_uint16>(l)) & 0xffffu) |
            ((static_cast<tjs_uint64>(static_cast<tjs_uint16>(t)) & 0xffffu)
             << 16);
        OnWindowStateChanged(0x0003u, 0, packed); // WM_MOVE
    }
    void SetBorderStyle(tTVPBorderStyle value) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        BorderStyle = value;
    }
    void SetStayOnTop(bool value) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        StayOnTop = value;
    }
    void SetFullScreenMode(bool value) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        FullScreenMode = value;
    }
    tjs_int GetLeft() {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return WindowLeft;
    }
    tjs_int GetTop() {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return WindowTop;
    }
    tjs_int GetMinWidth() {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return MinWidth;
    }
    tjs_int GetMaxWidth() {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return MaxWidth;
    }
    tjs_int GetMinHeight() {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return MinHeight;
    }
    tjs_int GetMaxHeight() {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return MaxHeight;
    }
    tjs_int GetInnerWidth() { return GetWidth(); }
    tjs_int GetInnerHeight() { return GetHeight(); }
    bool GetStayOnTop() {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return StayOnTop;
    }
    bool GetFullScreenMode() {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return FullScreenMode;
    }
    [[nodiscard]] tTVPBorderStyle GetBorderStyle() const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return BorderStyle;
    }
    void SetTrapKey(bool value) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        TrapKey = value;
    }
    [[nodiscard]] bool GetTrapKey() const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return TrapKey;
    }
    void RemoveMaskRegion() {}
    void SetMouseCursorState(tTVPMouseCursorState mcs) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        MouseCursorState = mcs;
    }
    [[nodiscard]] tTVPMouseCursorState GetMouseCursorState() const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return MouseCursorState;
    }
    void HideMouseCursor() { SetMouseCursorState(mcsHidden); }
    void SetFocusable(bool value) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        Focusable = value;
    }
    [[nodiscard]] bool GetFocusable() const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return Focusable;
    }
    int GetDisplayRotate() { return 0; }
    int GetDisplayOrientation() {
        tjs_int width = 0, height = 0;
        GetSize(width, height);
        if(width <= 0 || height <= 0)
            return orientUnknown;
        return width >= height ? orientLandscape : orientPortrait;
    }
    void SetEnableTouch(bool value) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        EnableTouch = value;
        if(!value)
            TouchPoints.clear();
    }
    [[nodiscard]] bool GetEnableTouch() const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return EnableTouch;
    }
    void SetHintDelay(tjs_int delay) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        HintDelay = std::max<tjs_int>(0, delay);
    }
    [[nodiscard]] tjs_int GetHintDelay() const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return HintDelay;
    }
    void SetInnerSunken(bool value) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        InnerSunken = value;
    }
    [[nodiscard]] bool GetInnerSunken() const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return InnerSunken;
    }

    // TODO
    void SetMouseCursor(tjs_int handle) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        MouseCursor = handle;
    }
    void SetHintText(iTJSDispatch2 *sender, const ttstr &text) {
        (void)sender;
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        HintText = text;
    }
    virtual void DisableAttentionPoint() {}
    static void GetVideoOffset(tjs_int &ofsx, tjs_int &ofsy) {
        ofsx = 0;
        ofsy = 0;
    }
    void SetTouchScaleThreshold(double threshold) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        TouchScaleThreshold = (std::max)(0.0, threshold);
    }
    double GetTouchScaleThreshold() {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return TouchScaleThreshold;
    }
    void SetTouchRotateThreshold(double threshold) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        TouchRotateThreshold = (std::max)(0.0, threshold);
    }
    double GetTouchRotateThreshold() {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return TouchRotateThreshold;
    }
    [[nodiscard]] tjs_real GetTouchPointStartX(tjs_int index) const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return index >= 0 && static_cast<size_t>(index) < TouchPoints.size()
            ? TouchPoints[static_cast<size_t>(index)].startX : 0;
    }
    [[nodiscard]] tjs_real GetTouchPointStartY(tjs_int index) const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return index >= 0 && static_cast<size_t>(index) < TouchPoints.size()
            ? TouchPoints[static_cast<size_t>(index)].startY : 0;
    }
    [[nodiscard]] tjs_real GetTouchPointX(tjs_int index) const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return index >= 0 && static_cast<size_t>(index) < TouchPoints.size()
            ? TouchPoints[static_cast<size_t>(index)].x : 0;
    }
    [[nodiscard]] tjs_real GetTouchPointY(tjs_int index) const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return index >= 0 && static_cast<size_t>(index) < TouchPoints.size()
            ? TouchPoints[static_cast<size_t>(index)].y : 0;
    }
    [[nodiscard]] tjs_int GetTouchPointID(tjs_int index) const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return index >= 0 && static_cast<size_t>(index) < TouchPoints.size()
            ? static_cast<tjs_int>(TouchPoints[static_cast<size_t>(index)].id) : 0;
    }
    [[nodiscard]] tjs_int GetTouchPointCount() const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return static_cast<tjs_int>(TouchPoints.size());
    }
    bool GetTouchVelocity(tjs_int id, float &x, float &y, float &speed) const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        for(const auto &point : TouchPoints) {
            if(static_cast<tjs_int>(point.id) == id) {
                x = point.velocityX;
                y = point.velocityY;
                speed = point.velocity;
                return true;
            }
        }
        x = y = speed = 0;
        return false;
    }
    void UpdateTouchDown(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy,
                         tjs_uint32 id) {
        (void)cx;
        (void)cy;
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        auto it = std::find_if(TouchPoints.begin(), TouchPoints.end(),
                               [id](const PortableTouchPoint &point) {
                                   return point.id == id;
                               });
        if(it == TouchPoints.end()) {
            PortableTouchPoint point;
            point.id = id;
            point.startX = point.x = x;
            point.startY = point.y = y;
            point.timestamp = std::chrono::steady_clock::now();
            TouchPoints.push_back(point);
        } else {
            it->startX = it->x = x;
            it->startY = it->y = y;
            it->velocityX = it->velocityY = it->velocity = 0;
            it->timestamp = std::chrono::steady_clock::now();
        }
    }
    void UpdateTouchMove(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy,
                         tjs_uint32 id) {
        (void)cx;
        (void)cy;
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        const auto it = std::find_if(TouchPoints.begin(), TouchPoints.end(),
                                     [id](const PortableTouchPoint &point) {
                                         return point.id == id;
                                     });
        if(it == TouchPoints.end())
            return;
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(
            now - it->timestamp).count();
        if(seconds > 0.000001 && seconds < 2.0) {
            it->velocityX = static_cast<float>((x - it->x) / seconds);
            it->velocityY = static_cast<float>((y - it->y) / seconds);
            it->velocity = std::hypot(it->velocityX, it->velocityY);
        }
        it->x = x;
        it->y = y;
        it->timestamp = now;
    }
    void UpdateTouchUp(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy,
                       tjs_uint32 id) {
        UpdateTouchMove(x, y, cx, cy, id);
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        TouchPoints.erase(std::remove_if(TouchPoints.begin(), TouchPoints.end(),
                                         [id](const PortableTouchPoint &point) {
                                             return point.id == id;
                                         }),
                          TouchPoints.end());
    }
    void ResetDrawDevice() {}
    void SendCloseMessage() {}
    void BeginMove() {}
    void SetLayerLeft(tjs_int l) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        LayerLeft = l;
    }
    [[nodiscard]] tjs_int GetLayerLeft() const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return LayerLeft;
    }
    void SetLayerTop(tjs_int t) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        LayerTop = t;
    }
    [[nodiscard]] tjs_int GetLayerTop() const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return LayerTop;
    }
    void SetLayerPosition(tjs_int l, tjs_int t) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        LayerLeft = l;
        LayerTop = t;
    }
    void SetShowScrollBars(bool b) {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        ShowScrollBars = b;
    }
    [[nodiscard]] bool GetShowScrollBars() const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        return ShowScrollBars;
    }

    // Apply the configured min/max limits before a concrete host changes its
    // logical size.  Kept public so HostWindowLayer can use the exact same
    // constraints as the base setters without duplicating state.
    void ConstrainSize(tjs_int &w, tjs_int &h) const {
        std::lock_guard<std::mutex> lock(WindowStateMutex);
        if(MinWidth > 0) w = (std::max)(w, MinWidth);
        if(MaxWidth > 0) w = (std::min)(w, MaxWidth);
        if(MinHeight > 0) h = (std::max)(h, MinHeight);
        if(MaxHeight > 0) h = (std::min)(h, MaxHeight);
        w = std::max<tjs_int>(0, w);
        h = std::max<tjs_int>(0, h);
    }

};

#endif // __TVP_WINDOW_H__
