//
// Created by Li_Dong on 2024/12/9.
// source code url: https://github.com/wamsoft/windowEx/blob
//
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

typedef unsigned long DWORD;

#if !defined(_WIN64) && !defined(_WIN32)
typedef unsigned long ULONG_PTR;
#else
#include <windows.h>
#include <WinDef.h>
#include <WinUser.h>
#endif

#include "win32_dt.h"
#include "ncbind.hpp"
#include "DetectCPU.h"
#include "GraphicsLoaderIntf.h"
#include "EventIntf.h"
#include "WindowImpl.h"
#include "TVPScreen.h"

#define NCB_MODULE_NAME TJS_W("windowEx.dll")
#ifndef _WIN32
#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ENABLE 0x000A
#define WM_SETREDRAW 0x000B
#define WM_SETTEXT 0x000C
#define WM_GETTEXT 0x000D
#define WM_GETTEXTLENGTH 0x000E
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUERYENDSESSION 0x0011
#define WM_QUERYOPEN 0x0013
#define WM_ENDSESSION 0x0016
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SYSCOLORCHANGE 0x0015
#define WM_SHOWWINDOW 0x0018
#define WM_WININICHANGE 0x001A
#define WM_SETTINGCHANGE WM_WININICHANGE
#define WM_DEVMODECHANGE 0x001B
#define WM_ACTIVATEAPP 0x001C
#define WM_FONTCHANGE 0x001D
#define WM_TIMECHANGE 0x001E
#define WM_CANCELMODE 0x001F
#define WM_SETCURSOR 0x0020
#define WM_MOUSEACTIVATE 0x0021
#define WM_CHILDACTIVATE 0x0022
#define WM_QUEUESYNC 0x0023
#define WM_GETMINMAXINFO 0x0024
#define WM_PAINTICON 0x0026
#define WM_ICONERASEBKGND 0x0027
#define WM_NEXTDLGCTL 0x0028
#define WM_SPOOLERSTATUS 0x002A
#define WM_DRAWITEM 0x002B
#define WM_MEASUREITEM 0x002C
#define WM_DELETEITEM 0x002D
#define WM_VKEYTOITEM 0x002E
#define WM_CHARTOITEM 0x002F
#define WM_SETFONT 0x0030
#define WM_GETFONT 0x0031
#define WM_SETHOTKEY 0x0032
#define WM_GETHOTKEY 0x0033
#define WM_QUERYDRAGICON 0x0037
#define WM_COMPAREITEM 0x0039
#define WM_GETOBJECT 0x003D
#define WM_COMPACTING 0x0041
#define WM_COMMNOTIFY 0x0044
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_POWER 0x0048
#define WM_COPYDATA 0x004A
#define WM_CANCELJOURNAL 0x004B
#define WM_NOTIFY 0x004E
#define WM_INPUTLANGCHANGEREQUEST 0x0050
#define WM_INPUTLANGCHANGE 0x0051
#define WM_TCARD 0x0052
#define WM_HELP 0x0053
#define WM_USERCHANGED 0x0054
#define WM_NOTIFYFORMAT 0x0055
#define WM_CONTEXTMENU 0x007B
#define WM_STYLECHANGING 0x007C
#define WM_STYLECHANGED 0x007D
#define WM_DISPLAYCHANGE 0x007E
#define WM_GETICON 0x007F
#define WM_SETICON 0x0080
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCCALCSIZE 0x0083
#define WM_NCHITTEST 0x0084
#define WM_NCPAINT 0x0085
#define WM_NCACTIVATE 0x0086
#define WM_GETDLGCODE 0x0087
#define WM_SYNCPAINT 0x0088
#define WM_NCMOUSEMOVE 0x00A0
#define WM_NCLBUTTONDOWN 0x00A1
#define WM_NCLBUTTONUP 0x00A2
#define WM_NCLBUTTONDBLCLK 0x00A3
#define WM_NCRBUTTONDOWN 0x00A4
#define WM_NCRBUTTONUP 0x00A5
#define WM_NCRBUTTONDBLCLK 0x00A6
#define WM_NCMBUTTONDOWN 0x00A7
#define WM_NCMBUTTONUP 0x00A8
#define WM_NCMBUTTONDBLCLK 0x00A9
#define WM_NCXBUTTONDOWN 0x00AB
#define WM_NCXBUTTONUP 0x00AC
#define WM_NCXBUTTONDBLCLK 0x00AD
#define WM_INPUT_DEVICE_CHANGE 0x00FE
#define WM_INPUT 0x00FF
#define WM_KEYFIRST 0x0100
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_DEADCHAR 0x0103
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_SYSDEADCHAR 0x0107
#define WM_UNICHAR 0x0109
#define WM_KEYLAST 0x0109
#define WM_IME_STARTCOMPOSITION 0x010D
#define WM_IME_ENDCOMPOSITION 0x010E
#define WM_IME_COMPOSITION 0x010F
#define WM_IME_KEYLAST 0x010F
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_HSCROLL 0x0114
#define WM_VSCROLL 0x0115
#define WM_INITMENU 0x0116
#define WM_INITMENUPOPUP 0x0117
#define WM_GESTURE 0x0119
#define WM_GESTURENOTIFY 0x011A
#define WM_MENUSELECT 0x011F
#define WM_MENUCHAR 0x0120
#define WM_ENTERIDLE 0x0121
#define WM_MENURBUTTONUP 0x0122
#define WM_MENUDRAG 0x0123
#define WM_MENUGETOBJECT 0x0124
#define WM_UNINITMENUPOPUP 0x0125
#define WM_MENUCOMMAND 0x0126
#define WM_CHANGEUISTATE 0x0127
#define WM_UPDATEUISTATE 0x0128
#define WM_QUERYUISTATE 0x0129
#define WM_CTLCOLORMSGBOX 0x0132
#define WM_CTLCOLOREDIT 0x0133
#define WM_CTLCOLORLISTBOX 0x0134
#define WM_CTLCOLORBTN 0x0135
#define WM_CTLCOLORDLG 0x0136
#define WM_CTLCOLORSCROLLBAR 0x0137
#define WM_CTLCOLORSTATIC 0x0138
#define WM_MOUSEFIRST 0x0200
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_MOUSEWHEEL 0x020A
#define WM_XBUTTONDOWN 0x020B
#define WM_XBUTTONUP 0x020C
#define WM_XBUTTONDBLCLK 0x020D
#define WM_MOUSEHWHEEL 0x020E
#define WM_MOUSELAST 0x020E
#define WM_PARENTNOTIFY 0x0210
#define WM_ENTERMENULOOP 0x0211
#define WM_EXITMENULOOP 0x0212
#define WM_NEXTMENU 0x0213
#define WM_SIZING 0x0214
#define WM_CAPTURECHANGED 0x0215
#define WM_MOVING 0x0216
#define WM_POWERBROADCAST 0x0218
#define WM_DEVICECHANGE 0x0219
#define WM_MDICREATE 0x0220
#define WM_MDIDESTROY 0x0221
#define WM_MDIACTIVATE 0x0222
#define WM_MDIRESTORE 0x0223
#define WM_MDINEXT 0x0224
#define WM_MDIMAXIMIZE 0x0225
#define WM_MDITILE 0x0226
#define WM_MDICASCADE 0x0227
#define WM_MDIICONARRANGE 0x0228
#define WM_MDIGETACTIVE 0x0229
#define WM_MDISETMENU 0x0230
#define WM_ENTERSIZEMOVE 0x0231
#define WM_EXITSIZEMOVE 0x0232
#define WM_DROPFILES 0x0233
#define WM_MDIREFRESHMENU 0x0234
#define WM_POINTERDEVICECHANGE 0x238
#define WM_POINTERDEVICEINRANGE 0x239
#define WM_POINTERDEVICEOUTOFRANGE 0x23A
#define WM_TOUCH 0x0240
#define WM_NCPOINTERUPDATE 0x0241
#define WM_NCPOINTERDOWN 0x0242
#define WM_NCPOINTERUP 0x0243
#define WM_POINTERUPDATE 0x0245
#define WM_POINTERDOWN 0x0246
#define WM_POINTERUP 0x0247
#define WM_POINTERENTER 0x0249
#define WM_POINTERLEAVE 0x024A
#define WM_POINTERACTIVATE 0x024B
#define WM_POINTERCAPTURECHANGED 0x024C
#define WM_TOUCHHITTESTING 0x024D
#define WM_POINTERWHEEL 0x024E
#define WM_POINTERHWHEEL 0x024F
#define WM_IME_SETCONTEXT 0x0281
#define WM_IME_NOTIFY 0x0282
#define WM_IME_CONTROL 0x0283
#define WM_IME_COMPOSITIONFULL 0x0284
#define WM_IME_SELECT 0x0285
#define WM_IME_CHAR 0x0286
#define WM_IME_REQUEST 0x0288
#define WM_IME_KEYDOWN 0x0290
#define WM_IME_KEYUP 0x0291
#define WM_MOUSEHOVER 0x02A1
#define WM_MOUSELEAVE 0x02A3
#define WM_NCMOUSEHOVER 0x02A0
#define WM_NCMOUSELEAVE 0x02A2
#define WM_WTSSESSION_CHANGE 0x02B1
#define WM_TABLET_FIRST 0x02c0
#define WM_TABLET_LAST 0x02df
#define WM_DPICHANGED 0x02E0
#define WM_CUT 0x0300
#define WM_COPY 0x0301
#define WM_PASTE 0x0302
#define WM_CLEAR 0x0303
#define WM_UNDO 0x0304
#define WM_RENDERFORMAT 0x0305
#define WM_RENDERALLFORMATS 0x0306
#define WM_DESTROYCLIPBOARD 0x0307
#define WM_DRAWCLIPBOARD 0x0308
#define WM_PAINTCLIPBOARD 0x0309
#define WM_VSCROLLCLIPBOARD 0x030A
#define WM_SIZECLIPBOARD 0x030B
#define WM_ASKCBFORMATNAME 0x030C
#define WM_CHANGECBCHAIN 0x030D
#define WM_HSCROLLCLIPBOARD 0x030E
#define WM_QUERYNEWPALETTE 0x030F
#define WM_PALETTEISCHANGING 0x0310
#define WM_PALETTECHANGED 0x0311
#define WM_HOTKEY 0x0312
#define WM_PRINT 0x0317
#define WM_PRINTCLIENT 0x0318
#define WM_APPCOMMAND 0x0319
#define WM_THEMECHANGED 0x031A
#define WM_CLIPBOARDUPDATE 0x031D
#define WM_DWMCOMPOSITIONCHANGED 0x031E
#define WM_DWMNCRENDERINGCHANGED 0x031F
#define WM_DWMCOLORIZATIONCOLORCHANGED 0x0320
#define WM_DWMWINDOWMAXIMIZEDCHANGE 0x0321
#define WM_DWMSENDICONICTHUMBNAIL 0x0323
#define WM_DWMSENDICONICLIVEPREVIEWBITMAP 0x0326
#define WM_GETTITLEBARINFOEX 0x033F
#define WM_HANDHELDFIRST 0x0358
#define WM_HANDHELDLAST 0x035F
#define WM_AFXFIRST 0x0360
#define WM_AFXLAST 0x037F
#define WM_PENWINFIRST 0x0380
#define WM_PENWINLAST 0x038F
#define WM_APP 0x8000
#define WM_USER 0x0400

#define HTBORDER 18 // 沒有重設大小框線的視窗框線中。
#define HTBOTTOM                                                               \
    15 // 可重設大小的視窗的下水準框線中（使用者可以按鼠以垂直調整視窗大小）。
#define HTBOTTOMLEFT                                                           \
    16 // 可重設大小的視窗框線左下角（使用者可以按鼠以對角調整視窗大小）。
#define HTBOTTOMRIGHT                                                          \
    17 // 可重設大小的視窗框線右下角（使用者可以按鼠以對角調整視窗大小）。
#define TCAPTION 2 // 標題列中。
#define TCLIENT 1 // 工作區中。
#define TCLOSE 20 // [關閉] 按鈕中。
#define HTERROR                                                                \
    -2 // 螢幕背景或視窗之間的分隔線上（與 HTNOWHERE
       // 相同，不同之處在於 DefWindowProc
       // 函式會產生系統嗶聲來指出錯誤）。
#define HTGROWBOX 4 // 大小方塊中（與 HTSIZE 相同）。
#define HTHELP 21 // [ 說明] 按鈕中。
#define HTHSCROLL 6 // 水平滾動條中。
#define HTLEFT                                                                 \
    10 // 可重設大小的視窗左框線中（使用者可以按鼠水平調整視窗大小）。
#define HTMENU 5 // 功能表中。
#define HTMAXBUTTON 9 // [最大化] 按鈕中。
#define HTMINBUTTON 8 // [最小化] 按鈕中。
#define HTNOWHERE 0 // 畫面背景或視窗之間的分隔線上。
#define HTREDUCE 8 // [最小化] 按鈕中。
#define HTRIGHT                                                                \
    11 // 可重設大小的視窗右框線中（使用者可以按鼠水平調整視窗大小）。
#define HTSIZE 4 // 大小方塊中（與 HTGROWBOX 相同）。
#define HTSYSMENU 3 // 視窗選單或子視窗的 [ 關閉 ] 按鈕中。
#define HTTOP 12 // 視窗的上水平框線中。
#define HTTOPLEFT 13 // 視窗框線的左上角。
#define HTTOPRIGHT 14 // 視窗框線的右上角。
#define HTTRANSPARENT -1 // 目前由相同線程中另一個視窗所涵蓋的視窗中。
#define HTVSCROLL 7 // 垂直滾動條中。
#define HTZOOM 9 // [最大化] 按鈕中。
#define HTCAPTION 2 // In a title bar.
#define HTCLIENT 1 // In a client area.
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2
#define SW_PARENTOPENING 1
#define SW_PARENTCLOSING 0
#define SC_MOVE 0xF010
#define SC_KEYMENU 0xF100
#define SC_MAXIMIZE 0xF030
#define SC_SCREENSAVE 0xF140
#define SC_MONITORPOWER 0xF170
#define MOD_ALT 0x0001
#define MOD_CONTROL 0x0002
#define MOD_SHIFT 0x0004
#define MOD_WIN 0x0008
#define DBT_DEVICEARRIVAL 0x8000
#define DBT_DEVICEREMOVECOMPLETE 0x8004
#define DBT_DEVTYP_DEVICEINTERFACE 0x00000005
#endif

// イベント名一覧
#define EXEV_MINIMIZE TJS_W("onMinimize")
#define EXEV_MAXIMIZE TJS_W("onMaximize")
#define EXEV_QUERYMAX TJS_W("onMaximizeQuery")
#define EXEV_SHOW TJS_W("onShow")
#define EXEV_HIDE TJS_W("onHide")
#define EXEV_RESIZING TJS_W("onResizing")
#define EXEV_MOVING TJS_W("onMoving")
#define EXEV_MOVE TJS_W("onMove")
#define EXEV_MVSZBEGIN TJS_W("onMoveSizeBegin")
#define EXEV_MVSZEND TJS_W("onMoveSizeEnd")
#define EXEV_DPICHANGE TJS_W("onDPIChanged")
#define EXEV_DISPCHG TJS_W("onDisplayChanged")
#define EXEV_DEVCHG TJS_W("onDeviceChanged")
#define EXEV_ENTERMENU TJS_W("onEnterMenuLoop")
#define EXEV_EXITMENU TJS_W("onExitMenuLoop")
#define EXEV_ACTIVATE TJS_W("onActivateChanged")
#define EXEV_SCREENSV TJS_W("onScreenSave")
#define EXEV_MONITORPW TJS_W("onMonitorPower")
#define EXEV_NCMSMOVE TJS_W("onNcMouseMove")
#define EXEV_NCMSLEAVE TJS_W("onNcMouseLeave")
#define EXEV_NCMSDOWN TJS_W("onNcMouseDown")
#define EXEV_NCMSUP TJS_W("onNcMouseUp")
#define EXEV_SYSMENU TJS_W("onExSystemMenuSelected")
#define EXEV_KEYMENU TJS_W("onStartKeyMenu")
#define EXEV_ACCELKEY TJS_W("onAccelKeyMenu")
#define EXEV_HOTKEY TJS_W("onHotKeyPressed")
#define EXEV_NCMSEV TJS_W("onNonCapMouseEvent")
#define EXEV_MSGHOOK TJS_W("onWindowsMessageHook")

#ifndef TVP_SS_WIN
#define TVP_SS_WIN 0x100
#endif

////////////////////////////////////////////////////////////////

struct WindowEx {
    //--------------------------------------------------------------
    // ユーティリティ

    // ネイティブインスタンスポインタを取得
    static inline WindowEx *GetInstance(iTJSDispatch2 *obj) {
        return ncbInstanceAdaptor<WindowEx>::GetNativeInstance(obj);
    }

    // ウィンドウハンドルを取得
    static HWND GetHWND(iTJSDispatch2 *obj) {
        if(!obj)
            return nullptr;
        tTJSVariant val;
        if(TJS_FAILED(obj->PropGet(TJS_IGNOREPROP, TJS_W("HWND"), nullptr,
                                   &val, obj)))
            return nullptr;
        return (HWND)(tjs_int64)(val);
    }

    // WindowEx is compiled for every Aether host, including the headless
    // compatibility host.  Resolve the real Aether window first and use its
    // iWindowLayer rather than manufacturing HWND-like values.  The latter
    // was the source of a number of silent no-op behaviours in the old
    // adapter (rectangles were always zero and maximized was always true).
    static tTJSNI_BaseWindow *GetNativeWindow(iTJSDispatch2 *obj) {
        if(!obj)
            return nullptr;
        iTJSNativeInstance *instance = nullptr;
        if(TJS_FAILED(obj->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Window::ClassID, &instance)) ||
           !instance)
            return nullptr;
        return static_cast<tTJSNI_BaseWindow *>(instance);
    }

    static iWindowLayer *GetWindowForm(iTJSDispatch2 *obj) {
        auto *window = GetNativeWindow(obj);
        return window ? window->GetForm() : nullptr;
    }

    static tjs_int GetIntProperty(iTJSDispatch2 *obj, const tjs_char *name,
                                  tjs_int fallback) {
        if(!obj || !name)
            return fallback;
        tTJSVariant value;
        if(TJS_FAILED(obj->PropGet(TJS_IGNOREPROP, name, nullptr, &value,
                                   obj)) ||
           (value.Type() != tvtInteger && value.Type() != tvtReal))
            return fallback;
        return static_cast<tjs_int>(value);
    }

    static tTVPRect GetCurrentRect(iTJSDispatch2 *obj) {
        tjs_int width = GetIntProperty(obj, TJS_W("width"), 0);
        tjs_int height = GetIntProperty(obj, TJS_W("height"), 0);
        if(auto *form = GetWindowForm(obj)) {
            tjs_int formWidth = 0;
            tjs_int formHeight = 0;
            form->GetSize(formWidth, formHeight);
            if(formWidth > 0)
                width = formWidth;
            if(formHeight > 0)
                height = formHeight;
        }
        width = std::max(0, width);
        height = std::max(0, height);
        const tjs_int left = GetIntProperty(obj, TJS_W("left"), 0);
        const tjs_int top = GetIntProperty(obj, TJS_W("top"), 0);
        return tTVPRect(left, top, left + width, top + height);
    }

    static bool SetFormSize(iTJSDispatch2 *obj, tjs_int width,
                            tjs_int height) {
        if(auto *form = GetWindowForm(obj)) {
            if(width <= 0 || height <= 0)
                return false;
            form->SetSize(width, height);
            return true;
        }
        return false;
    }

    static void SetFormVisible(iTJSDispatch2 *obj, bool visible) {
        if(auto *form = GetWindowForm(obj))
            form->SetVisible(visible);
    }

    //--------------------------------------------------------------
    // クラス追加メソッド(RawCallback形式)

    // minimize, maximize, showRestore
    static tjs_error minimize(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                              iTJSDispatch2 *obj) {
        if(auto *self = GetInstance(obj)) {
            self->minimized = true;
            self->maximized = false;
            SetFormVisible(obj, false);
            if(r)
                *r = true;
        } else if(r) {
            *r = false;
        }
        return TJS_S_OK;
    }

    static tjs_error maximize(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                              iTJSDispatch2 *obj) {
        auto *self = GetInstance(obj);
        if(!self)
            return TJS_E_ACCESSDENYED;
        if(!self->maximized)
            self->normalRect = GetCurrentRect(obj);
        const tjs_int width = std::max(1, tTVPScreen::GetDesktopWidth());
        const tjs_int height = std::max(1, tTVPScreen::GetDesktopHeight());
        const bool resized = SetFormSize(obj, width, height);
        self->maximized = resized || self->maximized;
        self->minimized = false;
        SetFormVisible(obj, true);
        if(r)
            *r = self->maximized;
        return TJS_S_OK;
    }

    static tjs_error showRestore(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                 iTJSDispatch2 *obj) {
        auto *self = GetInstance(obj);
        if(!self)
            return TJS_E_ACCESSDENYED;
        bool restored = true;
        if(self->maximized && self->normalRect.right > self->normalRect.left &&
           self->normalRect.bottom > self->normalRect.top) {
            restored = SetFormSize(obj, self->normalRect.get_width(),
                                   self->normalRect.get_height());
        }
        self->maximized = false;
        self->minimized = false;
        SetFormVisible(obj, true);
        if(r)
            *r = restored;
        return TJS_S_OK;
    }

    static tjs_error focusMenuByKey(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                    iTJSDispatch2 *obj) {
        if(n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;
        // There is no native system-menu accelerator on the portable host.
        // Keep the key value observable for scripts and report that the
        // request was accepted by the compatibility layer.
        const tjs_int key = static_cast<tjs_int>(*p[0]);
        if(auto *self = GetInstance(obj))
            self->lastMenuKey = key;
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    // resetWindowIcon
    static tjs_error resetWindowIcon(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                     iTJSDispatch2 *obj) {
        WindowEx::GetHWND(obj);
        if(auto *self = GetInstance(obj))
            self->iconToken.Clear();
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    // setWindowIcon
    static tjs_error setWindowIcon(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                   iTJSDispatch2 *obj) {
        auto *self = GetInstance(obj);
        if(!self)
            return TJS_E_ACCESSDENYED;
        // Store the requested icon value so a subsequent reset can preserve
        // the script-visible state. Native bitmap handles remain host-owned.
        if(n > 0 && p && p[0])
            self->iconToken = *p[0];
        else
            self->iconToken.Clear();
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    // getWindowRect
    static tjs_error getWindowRect(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                   iTJSDispatch2 *obj) {
        if(r)
            r->Clear();
        const tTVPRect rect = GetCurrentRect(obj);

        ncbDictionaryAccessor ncbDictAcc;

        ncbDictAcc.SetValue(TJS_W("x"), rect.left);
        ncbDictAcc.SetValue(TJS_W("y"), rect.top);
        auto w = rect.right - rect.left;
        ncbDictAcc.SetValue(TJS_W("w"), w);
        auto h = rect.bottom - rect.top;
        ncbDictAcc.SetValue(TJS_W("h"), h);
        if(r) {
            auto *dis = ncbDictAcc.GetDispatch();
            r->SetObject(dis, dis);
        }

        return TJS_S_OK;
    }

    // getClientRect
    static tjs_error getClientRect(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                   iTJSDispatch2 *obj) {
        getWindowRect(r, n, p, obj);
        return TJS_S_OK;
    }

    // On a portable host the form has no separate non-client frame, so a
    // client rectangle is the same geometry as the window rectangle. Keep
    // the setter real because KAG scripts commonly restore layouts through
    // this API before the first frame is shown.
    static tjs_error setClientRect(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                   iTJSDispatch2 *obj) {
        if(n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;
        if(p[0]->Type() != tvtObject || !p[0]->AsObjectNoAddRef())
            return TJS_E_INVALIDPARAM;
        auto *form = GetWindowForm(obj);
        if(!form) {
            if(r)
                *r = false;
            return TJS_S_OK;
        }
        tjs_int currentWidth = 0;
        tjs_int currentHeight = 0;
        form->GetSize(currentWidth, currentHeight);
        ncbPropAccessor rect(*p[0]);
        const tjs_int x = rect.getIntValue(TJS_W("x"), form->GetLeft());
        const tjs_int y = rect.getIntValue(TJS_W("y"), form->GetTop());
        const tjs_int width = rect.getIntValue(TJS_W("w"), currentWidth);
        const tjs_int height = rect.getIntValue(TJS_W("h"), currentHeight);
        if(width <= 0 || height <= 0) {
            if(r)
                *r = false;
            return TJS_S_OK;
        }
        form->SetPosition(x, y);
        form->SetSize(width, height);
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    // getNormalRect
    static tjs_error getNormalRect(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                   iTJSDispatch2 *obj) {
        if(r)
            r->Clear();
        auto *self = GetInstance(obj);
        const tTVPRect rect = self && self->normalRect.right > self->normalRect.left
            ? self->normalRect : GetCurrentRect(obj);
        ncbDictionaryAccessor dict;
        dict.SetValue(TJS_W("x"), rect.left);
        dict.SetValue(TJS_W("y"), rect.top);
        dict.SetValue(TJS_W("w"), rect.get_width());
        dict.SetValue(TJS_W("h"), rect.get_height());
        if(r) {
            auto *dispatch = dict.GetDispatch();
            r->SetObject(dispatch, dispatch);
        }
        return TJS_S_OK;
    }

    // property maximized box
    static tjs_error getMaximizeBox(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                    iTJSDispatch2 *obj) {
        if(r) {
            auto *self = GetInstance(obj);
            *r = self ? self->maximizeBox : false;
        }
        return TJS_S_OK;
    }

    static tjs_error setMaximizeBox(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                    iTJSDispatch2 *obj) {
        auto *self = GetInstance(obj);
        if(!self)
            return TJS_E_ACCESSDENYED;
        if(n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;
        self->maximizeBox = static_cast<bool>(*p[0]);
        if(r)
            *r = self->maximizeBox;
        return TJS_S_OK;
    }

    // property minimized box
    static tjs_error getMinimizeBox(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                    iTJSDispatch2 *obj) {
        if(r) {
            auto *self = GetInstance(obj);
            *r = self ? self->minimizeBox : false;
        }
        return TJS_S_OK;
    }

    static tjs_error setMinimizeBox(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                    iTJSDispatch2 *obj) {
        auto *self = GetInstance(obj);
        if(!self)
            return TJS_E_ACCESSDENYED;
        if(n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;
        self->minimizeBox = static_cast<bool>(*p[0]);
        if(r)
            *r = self->minimizeBox;
        return TJS_S_OK;
    }

    // property maximized
    static bool isMaximized(iTJSDispatch2 *obj) {
        auto *self = GetInstance(obj);
        return self && self->maximized;
    }

    static tjs_error getMaximized(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                  iTJSDispatch2 *obj) {
        if(r)
            *r = isMaximized(obj);
        return TJS_S_OK;
    }

    static tjs_error setMaximized(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                  iTJSDispatch2 *obj) {
        if(n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;
        const bool value = static_cast<bool>(*p[0]);
        const tjs_error error = value ? maximize(r, n, p, obj)
                                      : showRestore(r, n, p, obj);
        return error;
    }

    // property minimized
    static bool isMinimized(iTJSDispatch2 *obj) {
        auto *self = GetInstance(obj);
        return self && self->minimized;
    }

    static tjs_error getMinimized(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                  iTJSDispatch2 *obj) {
        if(r)
            *r = isMinimized(obj);
        return TJS_S_OK;
    }

    static tjs_error setMinimized(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                  iTJSDispatch2 *obj) {
        if(n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;
        const bool value = static_cast<bool>(*p[0]);
        if(value)
            return minimize(r, n, p, obj);
        auto *self = GetInstance(obj);
        if(!self)
            return TJS_E_ACCESSDENYED;
        self->minimized = false;
        SetFormVisible(obj, true);
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    // property disableResize
    static tjs_error getDisableResize(tTJSVariant *r, tjs_int n,
                                      tTJSVariant **p, iTJSDispatch2 *obj) {
        WindowEx *self = GetInstance(obj);
        if(r)
            *r = (self != nullptr && self->disableResize);
        return TJS_S_OK;
    }

    static tjs_error setDisableResize(tTJSVariant *r, tjs_int n,
                                      tTJSVariant **p, iTJSDispatch2 *obj) {
        WindowEx *self = GetInstance(obj);
        if(self == nullptr)
            return TJS_E_ACCESSDENYED;
        if(n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;
        self->disableResize = !!p[0]->AsInteger();
        if(r)
            *r = self->disableResize;
        return TJS_S_OK;
    }

    // property disableMove
    static tjs_error getDisableMove(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                    iTJSDispatch2 *obj) {
        WindowEx *self = GetInstance(obj);
        if(r)
            *r = (self != nullptr && self->disableMove);
        return TJS_S_OK;
    }

    static tjs_error setDisableMove(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                    iTJSDispatch2 *obj) {
        WindowEx *self = GetInstance(obj);
        if(self == nullptr)
            return TJS_E_ACCESSDENYED;
        if(n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;
        self->disableMove = !!p[0]->AsInteger();
        if(r)
            *r = self->disableMove;
        //_resetExSystemMenu(self);
        return TJS_S_OK;
    }

    // setOverlayBitmap
    static tjs_error setOverlayBitmap(tTJSVariant *r, tjs_int n,
                                      tTJSVariant **p, iTJSDispatch2 *obj) {
        WindowEx *self = GetInstance(obj);
        return (self != nullptr) ? self->_setOverlayBitmap(n, p)
                                 : TJS_E_ACCESSDENYED;
    }

    // property exSystemMenu
    static tjs_error getExSystemMenu(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                     iTJSDispatch2 *obj) {
        WindowEx *self = GetInstance(obj);
        if(r && self != nullptr)
            *r = tTJSVariant(self->sysMenuModified, self->sysMenuModified);
        return TJS_S_OK;
    }

    static tjs_error setExSystemMenu(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                     iTJSDispatch2 *obj) {
        WindowEx *self = GetInstance(obj);
        if(self == nullptr)
            return TJS_E_ACCESSDENYED;
        if(self->sysMenuModified != nullptr) {
            self->resetSystemMenu();
            self->sysMenuModified->Release();
        }
        if(n > 0 && p && p[0] && p[0]->Type() == tvtObject)
            self->sysMenuModified = p[0]->AsObject();
        self->modifySystemMenu();
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    // resetExSystemMenu
    static tjs_error resetExSystemMenu(tTJSVariant *r, tjs_int n,
                                       tTJSVariant **p, iTJSDispatch2 *obj) {
        auto *self = GetInstance(obj);
        if(!self)
            return TJS_E_ACCESSDENYED;
        if(self->sysMenuModified) {
            self->sysMenuModified->Release();
            self->sysMenuModified = nullptr;
        }
        self->resetSystemMenu();
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    // property enableNCMouseEvent
    static tjs_error getEnNCMEvent(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                   iTJSDispatch2 *obj) {
        WindowEx *self = GetInstance(obj);
        if(r)
            *r = (self != nullptr && self->enableNCMEvent);
        return TJS_S_OK;
    }

    static tjs_error setEnNCMEvent(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                   iTJSDispatch2 *obj) {
        WindowEx *self = GetInstance(obj);
        if(self == nullptr)
            return TJS_E_ACCESSDENYED;
        if(n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;
        self->enableNCMEvent = !!p[0]->AsInteger();
        if(r)
            *r = self->enableNCMEvent;
        return TJS_S_OK;
    }

    // ncHitTest
    static tjs_error nonClientHitTest(tTJSVariant *r, tjs_int n,
                                      tTJSVariant **p, iTJSDispatch2 *obj) {
        if(n < 2)
            return TJS_E_BADPARAMCOUNT;
        if(!p || !p[0] || !p[1])
            return TJS_E_BADPARAMCOUNT;
        const tjs_int x = (static_cast<tjs_int>(*p[0])) & 0xFFFF;
        const tjs_int y = (static_cast<tjs_int>(*p[1])) & 0xFFFF;
        // The Aether host has no separate non-client chrome. Every point is
        // therefore client content; this explicit result is important for
        // scripts that use ncHitTest to decide whether to drag the window.
        (void)x;
        (void)y;
        if(r)
            *r = static_cast<tjs_int>(HTCLIENT);
        return TJS_S_OK;
    }

    // setMessageHook
    static tjs_error setMessageHook(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                    iTJSDispatch2 *obj) {
        WindowEx *self = GetInstance(obj);
        if(self == nullptr)
            return TJS_E_ACCESSDENYED;
        if(n > 0 && (!p || !p[0]))
            return TJS_E_INVALIDPARAM;
        bool on = (n >= 1) && !!p[0]->AsInteger();
        bool ret = false;
        if(n >= 2) {
            if(!p[1])
                return TJS_E_INVALIDPARAM;
            tjs_int num;
            if(p[1]->Type() == tvtString) {
                ttstr key(*p[1]);
                num = getWindowNotificationNum(key);
            } else {
                num = (tjs_int)*p[1];
            }
            if(num < 0 || num >= 0x400)
                return TJS_E_FAIL;
            ret = self->setMessageHookOnel(on, num);
        } else {
            ret = self->setMessageHookAll(on);
        }
        if(r)
            *r = ret;
        return TJS_S_OK;
    }

    // bringTo
    static tjs_error bringTo(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                             iTJSDispatch2 *obj) {
        tTJSNI_BaseWindow *target = GetNativeWindow(obj);
        if(n >= 1 && p && p[0] && p[0]->Type() == tvtObject)
            target = GetNativeWindow(p[0]->AsObjectNoAddRef());
        if(target) {
            if(auto *window = dynamic_cast<tTJSNI_Window *>(target))
                window->BringToFront();
            if(r)
                *r = true;
        } else if(r) {
            *r = false;
        }
        return TJS_S_OK;
    }

    static tjs_error sendToBack(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                iTJSDispatch2 *obj) {
        // iWindowLayer intentionally has no platform-independent z-order
        // operation.  Keep the call observable without claiming that a
        // native reorder happened.
        if(r)
            *r = false;
        return TJS_S_OK;
    }

    static tjs_error registerHotKey(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **params,
                                    iTJSDispatch2 *obj) {
        if(numparams < 1 || !params || !params[0])
            return TJS_E_BADPARAMCOUNT;

        WindowEx *self = GetInstance(obj);
        if(!self)
            return TJS_E_ACCESSDENYED;

        const tjs_int id = params[0]->AsInteger();
        if(numparams == 1) {
            self->hotKeys.erase(id);
        } else {
            if(numparams < 3)
                return TJS_E_BADPARAMCOUNT;
            if(!params[1] || !params[2])
                return TJS_E_INVALIDPARAM;
            self->hotKeys[id] = {
                static_cast<tjs_int>(params[1]->AsInteger()),
                static_cast<tjs_int>(params[2]->AsInteger()),
            };
        }

        if(result)
            *result = true;
        return TJS_S_OK;
    }

    // Device notifications, IME control and DWM corner preferences are
    // Windows-only transport details. Preserve their observable state on a
    // portable form and report whether a form accepted the request; callers
    // can therefore probe the capability without a fake native handle.
    static tjs_error registerDeviceChange(tTJSVariant *result, tjs_int n,
                                          tTJSVariant **params,
                                          iTJSDispatch2 *obj) {
        if(n > 0 && (!params || !params[0]))
            return TJS_E_INVALIDPARAM;
        if(n > 0 && params[0]->Type() == tvtOctet) {
            auto *octet = params[0]->AsOctetNoAddRef();
            if(!octet || octet->GetLength() != 16)
                return TJS_E_INVALIDPARAM;
        }
        auto *self = GetInstance(obj);
        const bool available = self && GetWindowForm(obj);
        if(self)
            self->deviceChangeRegistered = available;
        if(result)
            *result = available;
        return TJS_S_OK;
    }

    static tjs_error acquireImeControl(tTJSVariant *result, tjs_int,
                                       tTJSVariant **,
                                       iTJSDispatch2 *obj) {
        auto *self = GetInstance(obj);
        const bool available = self && GetWindowForm(obj);
        if(self)
            self->imeControlAcquired = available;
        if(result)
            *result = available ? 1 : 0;
        return TJS_S_OK;
    }

    static tjs_error resetImeContext(tTJSVariant *result, tjs_int,
                                     tTJSVariant **,
                                     iTJSDispatch2 *obj) {
        auto *self = GetInstance(obj);
        const bool available = self && GetWindowForm(obj);
        if(self)
            self->imeContextReset = available;
        if(result)
            *result = available ? 1 : 0;
        return TJS_S_OK;
    }

    static tjs_error setWindowCornerPreference(tTJSVariant *result,
                                               tjs_int n, tTJSVariant **p,
                                               iTJSDispatch2 *obj) {
        auto *self = GetInstance(obj);
        if(!self)
            return TJS_E_ACCESSDENYED;
        if(n > 0 && (!p || !p[0]))
            return TJS_E_INVALIDPARAM;
        self->cornerPreference = n > 0 ? static_cast<tjs_int>(*p[0]) : 0;
        if(result)
            *result = GetWindowForm(obj) != nullptr;
        return TJS_S_OK;
    }

    void checkUpdateMenuItem(HMENU menu, int pos, UINT id);

    //--------------------------------------------------------------
    // 拡張イベント用

    // メンバが存在するか
    bool hasMember(tjs_char const *name) const {
        if(!self || !name)
            return false;
        tTJSVariant func;
        return TJS_SUCCEEDED(
            self->PropGet(TJS_MEMBERMUSTEXIST, name, 0, &func, self));
    }

    // TJSメソッド呼び出し
    tjs_error funcCall(tjs_char const *name, tTJSVariant *result,
                       tjs_int numparams = 0, tTJSVariant **params = 0) const {
        if(!self || !name)
            return TJS_E_INVALIDOBJECT;
        try {
            return self->FuncCall(0, name, 0, result, numparams, params, self);
        } catch(...) {
            TVPAddLog(TJS_W("AetherKiri windowEx callback raised an exception"));
            return TJS_E_FAIL;
        }
    }

    bool invokeCallback(tjs_char const *name, tTJSVariant *result,
                        tjs_int numparams = 0,
                        tTJSVariant **params = nullptr) const {
        if(!hasMember(name))
            return false;
        if(TJS_FAILED(funcCall(name, result, numparams, params)))
            return false;
        return result && result->Type() != tvtVoid &&
               result->AsInteger() != 0;
    }

    // 引数なしコールバック
    bool callback(tjs_char const *name) const {
        tTJSVariant rslt;
        return invokeCallback(name, &rslt);
    }

    // variant渡しコールバック
    bool callback(tjs_char const *name, tTJSVariant *v) const {
        tTJSVariant rslt;
        tTJSVariant *params[] = {v};
        return invokeCallback(name, &rslt, 1, params);
    }

    // 座標渡しコールバック
    bool callback(tjs_char const *name, int x, int y) const {
        tTJSVariant vx(x), vy(y);
        tTJSVariant rslt, *params[] = { &vx, &vy };
        return invokeCallback(name, &rslt, 2, params);
    }

    // ４個渡しコールバック
    bool callback(tjs_char const *name, int a, int b, int c, int d) const {
        tTJSVariant va(a), vb(b), vc(c), vd(d);
        tTJSVariant rslt, *params[] = { &va, &vb, &vc, &vd };
        return invokeCallback(name, &rslt, 4, params);
    }

    bool callbackMessage(tjs_char const *name, tjs_uint32 message,
                         tjs_uint64 wparam, tjs_uint64 lparam) const {
        tTJSVariant vm(static_cast<tjs_int64>(message));
        tTJSVariant vw(static_cast<tjs_int64>(wparam));
        tTJSVariant vl(static_cast<tjs_int64>(lparam));
        tTJSVariant result;
        tTJSVariant *params[] = {&vm, &vw, &vl};
        return invokeCallback(name, &result, 3, params);
    }

    bool callbackRect(tjs_char const *name, tjs_uint64 rawRect,
                      tjs_int type) {
#if defined(_WIN32)
        auto *nativeRect = reinterpret_cast<RECT *>(
            static_cast<std::uintptr_t>(rawRect));
        if(!nativeRect)
            return false;
        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        if(!dict)
            return false;
        const auto set = [dict](const tjs_char *key, tjs_int value) {
            tTJSVariant v(value);
            dict->PropSet(TJS_MEMBERENSURE, key, nullptr, &v, dict);
        };
        set(TJS_W("x"), nativeRect->left);
        set(TJS_W("y"), nativeRect->top);
        set(TJS_W("w"), nativeRect->right - nativeRect->left);
        set(TJS_W("h"), nativeRect->bottom - nativeRect->top);
        set(TJS_W("type"), type);
        tTJSVariant rect(dict, dict), result;
        tTJSVariant *params[] = {&rect};
        const bool handled = invokeCallback(name, &result, 1, params);
        if(handled && rect.Type() == tvtObject) {
            ncbPropAccessor values(rect);
            const tjs_int x = values.getIntValue(TJS_W("x"),
                                                 nativeRect->left);
            const tjs_int y = values.getIntValue(TJS_W("y"),
                                                 nativeRect->top);
            const tjs_int w = values.getIntValue(
                TJS_W("w"), nativeRect->right - nativeRect->left);
            const tjs_int h = values.getIntValue(
                TJS_W("h"), nativeRect->bottom - nativeRect->top);
            nativeRect->left = x;
            nativeRect->top = y;
            nativeRect->right = x + std::max(0, w);
            nativeRect->bottom = y + std::max(0, h);
        }
        dict->Release();
        return handled;
#else
        auto *form = GetWindowForm(self);
        if(!form)
            return false;
        tjs_int width = 0, height = 0;
        form->GetSize(width, height);
        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        if(!dict)
            return false;
        const auto set = [dict](const tjs_char *key, tjs_int value) {
            tTJSVariant v(value);
            dict->PropSet(TJS_MEMBERENSURE, key, nullptr, &v, dict);
        };
        set(TJS_W("x"), form->GetLeft());
        set(TJS_W("y"), form->GetTop());
        set(TJS_W("w"), width);
        set(TJS_W("h"), height);
        set(TJS_W("type"), type);
        tTJSVariant rect(dict, dict), result;
        tTJSVariant *params[] = {&rect};
        const bool handled = invokeCallback(name, &result, 1, params);
        if(handled && rect.Type() == tvtObject) {
            ncbPropAccessor values(rect);
            const tjs_int x = values.getIntValue(TJS_W("x"), form->GetLeft());
            const tjs_int y = values.getIntValue(TJS_W("y"), form->GetTop());
            const tjs_int w = values.getIntValue(TJS_W("w"), width);
            const tjs_int h = values.getIntValue(TJS_W("h"), height);
            if(w > 0 && h > 0) {
                form->SetPosition(x, y);
                form->SetSize(w, h);
            }
        }
        dict->Release();
        (void)rawRect;
        return handled;
#endif
    }

    // Deliver the subset of the Win32 WindowEx receiver contract that is
    // meaningful on every host.  Native Win32 forms still receive the same
    // messages from the platform; SDL/Godot forms can inject the integer
    // payload through TVPDeliverWindowMessage.  No portable branch
    // dereferences LPARAM as a platform pointer.
    bool onMessage(tTVPWindowMessage *message) {
        if(!message)
            return false;

        switch(message->Msg) {
        case TVP_WM_ATTACH:
            cachedHWND = reinterpret_cast<HWND>(
                static_cast<std::uintptr_t>(message->LParam));
            // WindowEx instances can be created before the native/portable
            // form is attached.  Retry registration at attach time so the
            // receiver chain is live for the first resize/input message;
            // the host-side registration is idempotent for repeated attach
            // notifications.
            regist(true);
            if(sysMenuModified)
                modifySystemMenu();
            break;
        case TVP_WM_DETACH:
            regist(false);
            resetSystemMenu();
#if defined(_WIN32)
            if(sysMenu) {
                ::DestroyMenu(sysMenu);
                sysMenu = nullptr;
                if(cachedHWND)
                    ::GetSystemMenu(cachedHWND, TRUE);
            }
#else
            sysMenu = nullptr;
#endif
            cachedHWND = nullptr;
            deleteOverlayBitmap();
            break;
        default:
            break;
        }

        if(enableWinMsgHook && message->Msg < 0x400) {
            const std::size_t index = message->Msg / 32u;
            const DWORD mask = static_cast<DWORD>(
                1u << (message->Msg % 32u));
            if(index < std::size(bitHooks) && (bitHooks[index] & mask) != 0 &&
               callbackMessage(EXEV_MSGHOOK, message->Msg,
                               message->WParam, message->LParam))
                return true;
        }

        const tjs_int lowWParam = static_cast<tjs_int>(
            static_cast<std::uint32_t>(message->WParam) & 0xffffu);
        const tjs_int highWParam = static_cast<tjs_int>(
            (static_cast<std::uint64_t>(message->WParam) >> 16) & 0xffffu);
        const tjs_int lowLParam = static_cast<tjs_int>(
            static_cast<std::uint32_t>(message->LParam) & 0xffffu);
        const tjs_int highLParam = static_cast<tjs_int>(
            (static_cast<std::uint64_t>(message->LParam) >> 16) & 0xffffu);

        switch(message->Msg) {
        case WM_SETCURSOR:
            if(enableNCMEvent)
                return callback(EXEV_NCMSEV, lowLParam, highLParam);
            break;
        case WM_MENUCHAR:
            if(callback(EXEV_ACCELKEY, lowWParam, highWParam)) {
#if defined(_WIN32)
                if(cachedHWND)
                    message->Result = static_cast<tjs_uint64>(
                        ::DefWindowProc(cachedHWND, message->Msg,
                                        static_cast<WPARAM>(message->WParam),
                                        static_cast<LPARAM>(message->LParam)));
#endif
                return true;
            }
            break;
        case WM_SYSCOMMAND: {
            const tjs_uint64 command = message->WParam & 0xffffu;
            if(sysMenuModMap && command < 0xf000u) {
                tTJSVariant value;
                if(TJS_SUCCEEDED(sysMenuModMap->PropGetByNum(
                       TJS_IGNOREPROP, static_cast<tjs_int>(command), &value,
                       sysMenuModMap)) && value.Type() == tvtObject &&
                   callback(EXEV_SYSMENU, &value))
                    return true;
            }
            switch(message->WParam & 0xfff0u) {
            case SC_MAXIMIZE:
                return callback(EXEV_QUERYMAX);
            case SC_SCREENSAVE:
                return callback(EXEV_SCREENSV);
            case SC_MONITORPOWER:
                return callback(EXEV_MONITORPW, lowLParam, 0);
            case SC_KEYMENU:
                return callback(EXEV_KEYMENU, lowLParam, 0);
            case SC_MOVE:
                if(disableMove) {
                    message->Result = 0;
                    return true;
                }
                break;
            default:
                break;
            }
            break;
        }
        case WM_SIZE:
            if(message->WParam == SIZE_MINIMIZED)
                callback(EXEV_MINIMIZE);
            else if(message->WParam == SIZE_MAXIMIZED)
                callback(EXEV_MAXIMIZE);
            break;
        case WM_SHOWWINDOW:
            if(message->LParam == SW_PARENTOPENING)
                callback(EXEV_SHOW);
            else if(message->LParam == SW_PARENTCLOSING)
                callback(EXEV_HIDE);
            break;
        case WM_QUERYOPEN:
            callback(EXEV_SHOW);
            break;
        case WM_ENTERSIZEMOVE:
            callback(EXEV_MVSZBEGIN);
            break;
        case WM_EXITSIZEMOVE:
            callback(EXEV_MVSZEND);
            break;
        case WM_DPICHANGED:
            callback(EXEV_DPICHANGE, lowWParam, highWParam);
            break;
        case WM_SIZING:
            if(hasResizing)
                callbackRect(EXEV_RESIZING, message->LParam, lowWParam);
            break;
        case WM_MOVING:
            if(hasMoving)
                callbackRect(EXEV_MOVING, message->LParam, 0);
            break;
        case WM_MOVE:
            if(hasMove)
                callback(EXEV_MOVE, lowLParam, highLParam);
            break;
        case WM_NCHITTEST:
            if(disableResize) {
#if defined(_WIN32)
                LRESULT hit = cachedHWND
                    ? ::DefWindowProc(cachedHWND, message->Msg,
                                      static_cast<WPARAM>(message->WParam),
                                      static_cast<LPARAM>(message->LParam))
                    : HTCLIENT;
                switch(hit) {
                case HTLEFT: case HTRIGHT: case HTTOP: case HTTOPLEFT:
                case HTTOPRIGHT: case HTBOTTOM: case HTBOTTOMLEFT:
                case HTBOTTOMRIGHT:
                    hit = HTBORDER;
                    break;
                default:
                    break;
                }
                message->Result = static_cast<tjs_uint64>(hit);
#else
                message->Result = HTBORDER;
#endif
                return true;
            }
            break;
        case WM_NCLBUTTONDOWN:
            callback(EXEV_NCMSDOWN, lowLParam, highLParam, 0, lowWParam);
            break;
        case WM_NCRBUTTONDOWN:
            callback(EXEV_NCMSDOWN, lowLParam, highLParam, 1, lowWParam);
            break;
        case WM_NCMBUTTONDOWN:
            callback(EXEV_NCMSDOWN, lowLParam, highLParam, 2, lowWParam);
            break;
        case WM_NCLBUTTONUP:
            callback(EXEV_NCMSUP, lowLParam, highLParam, 0, lowWParam);
            break;
        case WM_NCRBUTTONUP:
            callback(EXEV_NCMSUP, lowLParam, highLParam, 1, lowWParam);
            break;
        case WM_NCMBUTTONUP:
            callback(EXEV_NCMSUP, lowLParam, highLParam, 2, lowWParam);
            break;
        case WM_NCMOUSELEAVE:
            callback(EXEV_NCMSLEAVE);
            break;
        case WM_NCMOUSEMOVE:
            if(hasNcMsMove)
                callback(EXEV_NCMSMOVE, lowLParam, highLParam, lowWParam, 0);
            break;
        case WM_INITMENUPOPUP:
#if defined(_WIN32)
            if((message->LParam >> 16) != 0 &&
               (disableResize || disableMove) && cachedHWND) {
                message->Result = static_cast<tjs_uint64>(::DefWindowProc(
                    cachedHWND, message->Msg,
                    static_cast<WPARAM>(message->WParam),
                    static_cast<LPARAM>(message->LParam)));
                if(disableResize)
                    ::EnableMenuItem(reinterpret_cast<HMENU>(
                                         static_cast<std::uintptr_t>(message->WParam)),
                                     SC_SIZE, MF_GRAYED | MF_BYCOMMAND);
                if(disableMove)
                    ::EnableMenuItem(reinterpret_cast<HMENU>(
                                         static_cast<std::uintptr_t>(message->WParam)),
                                     SC_MOVE, MF_GRAYED | MF_BYCOMMAND);
                return true;
            }
            if((message->LParam >> 16) == 0 && menuex) {
                if(cachedHWND)
                    message->Result = static_cast<tjs_uint64>(::DefWindowProc(
                        cachedHWND, message->Msg,
                        static_cast<WPARAM>(message->WParam),
                        static_cast<LPARAM>(message->LParam)));
                checkUpdateMenuItem(reinterpret_cast<HMENU>(
                                        static_cast<std::uintptr_t>(message->WParam)),
                                    0, 0);
                return true;
            }
#endif
            break;
        case WM_ENTERMENULOOP:
            callback(EXEV_ENTERMENU);
            break;
        case WM_EXITMENULOOP:
            callback(EXEV_EXITMENU);
            break;
        case WM_DISPLAYCHANGE:
            callback(EXEV_DISPCHG);
            break;
        case WM_DEVICECHANGE:
            if(deviceChangeRegistered) {
#if defined(_WIN32)
                auto *header = reinterpret_cast<DEV_BROADCAST_HDR *>(
                    static_cast<std::uintptr_t>(message->LParam));
                if(header && header->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE &&
                   (message->WParam == DBT_DEVICEARRIVAL ||
                    message->WParam == DBT_DEVICEREMOVECOMPLETE)) {
                    tTJSVariant value(message->WParam == DBT_DEVICEARRIVAL);
                    callback(EXEV_DEVCHG, &value);
                }
#else
                if(message->WParam == DBT_DEVICEARRIVAL ||
                   message->WParam == DBT_DEVICEREMOVECOMPLETE) {
                    tTJSVariant value(message->WParam == DBT_DEVICEARRIVAL);
                    callback(EXEV_DEVCHG, &value);
                }
#endif
                return true;
            }
            break;
        case WM_HOTKEY: {
            const tjs_int id = static_cast<tjs_int>(message->WParam);
            const tjs_int modifiers = lowLParam;
            tjs_int shift = 0;
            if(modifiers & MOD_ALT) shift |= TVP_SS_ALT;
            if(modifiers & MOD_CONTROL) shift |= TVP_SS_CTRL;
            if(modifiers & MOD_SHIFT) shift |= TVP_SS_SHIFT;
            if(modifiers & MOD_WIN) shift |= TVP_SS_WIN;
            if(callback(EXEV_HOTKEY, id, shift))
                return true;
            break;
        }
        case WM_ACTIVATE:
            return callback(EXEV_ACTIVATE, lowWParam, highWParam);
        default:
            break;
        }
        return false;
    }

#if defined(_WIN32)
    static bool __stdcall receiver(void *userdata, tTVPWindowMessage *message) {
#else
    static bool receiver(void *userdata, tTVPWindowMessage *message) {
#endif
        auto *self = static_cast<WindowEx *>(userdata);
        return self && self->onMessage(message);
    }

    // メニュー更新処理（MenuItemEx用）
    void setMenuItemID(iTJSDispatch2 *, UINT, bool);

    // Message Receiver 登録・解除
    void regist(bool en) {
        auto *window = dynamic_cast<tTJSNI_Window *>(GetNativeWindow(self));
        if(!window)
            return;
        window->RegisterWindowMessageReceiver(
            en ? wrmRegister : wrmUnregister,
            reinterpret_cast<void *>(&receiver), self);
    }

    // ネイティブインスタンスの生成・破棄にあわせてレシーバを登録・解除する
    WindowEx(iTJSDispatch2 *obj) :
        self(obj), menuex(nullptr), sysMenuModified(nullptr),
        sysMenuModMap(nullptr), cachedHWND(nullptr), sysMenu(nullptr),
        externalIcon(nullptr), hasResizing(false), hasMoving(false),
        hasMove(false), hasNcMsMove(false), disableResize(false),
        disableMove(false), enableNCMEvent(false), enableWinMsgHook(false),
        bitHooks{}, maximized(false), minimized(false), maximizeBox(true),
        minimizeBox(true), lastMenuKey(0), deviceChangeRegistered(false),
        imeControlAcquired(false), imeContextReset(false), cornerPreference(0),
        normalRect(0, 0, 0, 0),
        iconToken(), ovbmp(nullptr) {
        regist(true);
        setMessageHookAll(false);
    }

    ~WindowEx() {
        if(menuex)
            menuex->Release();
        if(sysMenuModified)
            sysMenuModified->Release();
        resetSystemMenu();
        deleteOverlayBitmap();
        regist(false);
    }

    void checkExEvents() {
        hasResizing = hasMember(EXEV_RESIZING);
        hasMoving = hasMember(EXEV_MOVING);
        hasMove = hasMember(EXEV_MOVE);
        hasNcMsMove = hasMember(EXEV_NCMSMOVE);
    }

    void deleteOverlayBitmap() {
        if(ovbmp)
            delete ovbmp;
        ovbmp = nullptr;
    }

    void resetSystemMenu();

    void modifySystemMenu();

    bool setMessageHookOnel(bool on, tjs_int num) {
        if(num < 0 || num >= 0x400)
            return false;
        const std::size_t index = static_cast<std::size_t>(num) / 32u;
        const DWORD mask = static_cast<DWORD>(1u << (num % 32));
        if(on)
            bitHooks[index] |= mask;
        else
            bitHooks[index] &= ~mask;
        enableWinMsgHook = std::any_of(
            std::begin(bitHooks), std::end(bitHooks),
            [](DWORD value) { return value != 0; });
        return true;
    }

    bool setMessageHookAll(bool on) {
        std::fill(std::begin(bitHooks), std::end(bitHooks),
                  on ? static_cast<DWORD>(~static_cast<DWORD>(0)) : 0);
        enableWinMsgHook = on;
        return true;
    }

    static tjs_int getWindowNotificationNum(ttstr key) {
        tTJSVariant tmp;
        if(!_getNotificationVariant(tmp))
            TVPThrowExceptionMessage(TJS_W("cache setup failed."));
        ncbPropAccessor nf(tmp);
        return nf.getIntValue(key.c_str(), -1);
    }

    static ttstr getWindowNotificationName(tjs_int num) {
        tTJSVariant tmp;
        if(!_getNotificationVariant(tmp))
            TVPThrowExceptionMessage(TJS_W("cache setup failed."));
        ncbPropAccessor nf(tmp);
        return nf.getStrValue(num);
    }

protected:
    tjs_error _setOverlayBitmap(tjs_int n, tTJSVariant **p) {
        if(n > 0 && p[0]->Type() == tvtObject) {
            if(!ovbmp) {
                ovbmp = new OverlayBitmap();
                if(!ovbmp)
                    return TJS_E_FAIL;
            }
            if(!ovbmp->setBitmap(self, p[0]->AsObjectNoAddRef())) {
                deleteOverlayBitmap();
                return TJS_E_FAIL;
            }
        }
        return TJS_S_OK;
    }

    static bool _getNotificationVariant(tTJSVariant &tmp) {
        iTJSDispatch2 *obj = TVPGetScriptDispatch();
        tmp.Clear();
        bool hasval = TJS_SUCCEEDED(
            obj->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("Window"), 0, &tmp, obj));
        obj->Release();
        if(!hasval)
            return false;

        obj = tmp.AsObjectNoAddRef();
        tmp.Clear();
        if(TJS_FAILED(obj->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("_Notifications"),
                                   0, &tmp, obj))) {
            ncbDictionaryAccessor dict;
#ifndef WM_CTLCOLOR
#define WM_CTLCOLOR 0x0019
#endif
#define WM(key)                                                                \
    dict.SetValue(TJS_W(#key), WM_##key),                                      \
        dict.SetValue(WM_##key, ttstr(TJS_W(#key)))
            WM(NULL), WM(CREATE), WM(DESTROY), WM(MOVE), WM(SIZE), WM(ACTIVATE),
                WM(SETFOCUS), WM(KILLFOCUS), WM(ENABLE), WM(SETREDRAW),
                WM(SETTEXT), WM(GETTEXT), WM(GETTEXTLENGTH), WM(PAINT),
                WM(CLOSE), WM(QUERYENDSESSION), WM(QUERYOPEN), WM(ENDSESSION),
                WM(QUIT), WM(ERASEBKGND), WM(SYSCOLORCHANGE), WM(SHOWWINDOW),
                WM(CTLCOLOR), WM(WININICHANGE), WM(SETTINGCHANGE),
                WM(DEVMODECHANGE), WM(ACTIVATEAPP), WM(FONTCHANGE),
                WM(TIMECHANGE), WM(CANCELMODE), WM(SETCURSOR),
                WM(MOUSEACTIVATE), WM(CHILDACTIVATE), WM(QUEUESYNC),
                WM(GETMINMAXINFO), WM(PAINTICON), WM(ICONERASEBKGND),
                WM(NEXTDLGCTL), WM(SPOOLERSTATUS), WM(DRAWITEM),
                WM(MEASUREITEM), WM(DELETEITEM), WM(VKEYTOITEM), WM(CHARTOITEM),
                WM(SETFONT), WM(GETFONT), WM(SETHOTKEY), WM(GETHOTKEY),
                WM(QUERYDRAGICON), WM(COMPAREITEM), WM(GETOBJECT),
                WM(COMPACTING), WM(COMMNOTIFY), WM(WINDOWPOSCHANGING),
                WM(WINDOWPOSCHANGED), WM(POWER), WM(COPYDATA),
                WM(CANCELJOURNAL), WM(NOTIFY), WM(INPUTLANGCHANGEREQUEST),
                WM(INPUTLANGCHANGE), WM(TCARD), WM(HELP), WM(USERCHANGED),
                WM(NOTIFYFORMAT), WM(CONTEXTMENU), WM(STYLECHANGING),
                WM(STYLECHANGED), WM(DISPLAYCHANGE), WM(GETICON), WM(SETICON),
                WM(NCCREATE), WM(NCDESTROY), WM(NCCALCSIZE), WM(NCHITTEST),
                WM(NCPAINT), WM(NCACTIVATE), WM(GETDLGCODE), WM(SYNCPAINT),
                WM(NCMOUSEMOVE), WM(NCLBUTTONDOWN), WM(NCLBUTTONUP),
                WM(NCLBUTTONDBLCLK), WM(NCRBUTTONDOWN), WM(NCRBUTTONUP),
                WM(NCRBUTTONDBLCLK), WM(NCMBUTTONDOWN), WM(NCMBUTTONUP),
                WM(NCMBUTTONDBLCLK), WM(NCXBUTTONDOWN), WM(NCXBUTTONUP),
                WM(NCXBUTTONDBLCLK), WM(INPUT_DEVICE_CHANGE), WM(INPUT),
                WM(KEYFIRST), WM(KEYDOWN), WM(KEYUP), WM(CHAR), WM(DEADCHAR),
                WM(SYSKEYDOWN), WM(SYSKEYUP), WM(SYSCHAR), WM(SYSDEADCHAR),
                WM(UNICHAR), WM(KEYLAST), WM(KEYLAST), WM(IME_STARTCOMPOSITION),
                WM(IME_ENDCOMPOSITION), WM(IME_COMPOSITION), WM(IME_KEYLAST),
                WM(INITDIALOG), WM(COMMAND), WM(SYSCOMMAND), WM(TIMER),
                WM(HSCROLL), WM(VSCROLL), WM(INITMENU), WM(INITMENUPOPUP),
                WM(MENUSELECT), WM(MENUCHAR), WM(ENTERIDLE), WM(MENURBUTTONUP),
                WM(MENUDRAG), WM(MENUGETOBJECT), WM(UNINITMENUPOPUP),
                WM(MENUCOMMAND), WM(CHANGEUISTATE), WM(UPDATEUISTATE),
                WM(QUERYUISTATE), WM(CTLCOLORMSGBOX), WM(CTLCOLOREDIT),
                WM(CTLCOLORLISTBOX), WM(CTLCOLORBTN), WM(CTLCOLORDLG),
                WM(CTLCOLORSCROLLBAR), WM(CTLCOLORSTATIC), WM(MOUSEFIRST),
                WM(MOUSEMOVE), WM(LBUTTONDOWN), WM(LBUTTONUP),
                WM(LBUTTONDBLCLK), WM(RBUTTONDOWN), WM(RBUTTONUP),
                WM(RBUTTONDBLCLK), WM(MBUTTONDOWN), WM(MBUTTONUP),
                WM(MBUTTONDBLCLK), WM(MOUSEWHEEL), WM(XBUTTONDOWN),
                WM(XBUTTONUP), WM(XBUTTONDBLCLK), WM(MOUSEHWHEEL),
                WM(MOUSELAST), WM(MOUSELAST), WM(MOUSELAST), WM(MOUSELAST),
                WM(PARENTNOTIFY), WM(ENTERMENULOOP), WM(EXITMENULOOP),
                WM(NEXTMENU), WM(SIZING), WM(CAPTURECHANGED), WM(MOVING),
                WM(POWERBROADCAST), WM(DEVICECHANGE), WM(MDICREATE),
                WM(MDIDESTROY), WM(MDIACTIVATE), WM(MDIRESTORE), WM(MDINEXT),
                WM(MDIMAXIMIZE), WM(MDITILE), WM(MDICASCADE),
                WM(MDIICONARRANGE), WM(MDIGETACTIVE), WM(MDISETMENU),
                WM(ENTERSIZEMOVE), WM(EXITSIZEMOVE), WM(DROPFILES),
                WM(MDIREFRESHMENU), WM(IME_SETCONTEXT), WM(IME_NOTIFY),
                WM(IME_CONTROL), WM(IME_COMPOSITIONFULL), WM(IME_SELECT),
                WM(IME_CHAR), WM(IME_REQUEST), WM(IME_KEYDOWN), WM(IME_KEYUP),
                WM(MOUSEHOVER), WM(MOUSELEAVE), WM(NCMOUSEHOVER),
                WM(NCMOUSELEAVE), WM(WTSSESSION_CHANGE), WM(TABLET_FIRST),
                WM(TABLET_LAST), WM(CUT), WM(COPY), WM(PASTE), WM(CLEAR),
                WM(UNDO), WM(RENDERFORMAT), WM(RENDERALLFORMATS),
                WM(DESTROYCLIPBOARD), WM(DRAWCLIPBOARD), WM(PAINTCLIPBOARD),
                WM(VSCROLLCLIPBOARD), WM(SIZECLIPBOARD), WM(ASKCBFORMATNAME),
                WM(CHANGECBCHAIN), WM(HSCROLLCLIPBOARD), WM(QUERYNEWPALETTE),
                WM(PALETTEISCHANGING), WM(PALETTECHANGED), WM(HOTKEY),
                WM(PRINT), WM(PRINTCLIENT), WM(APPCOMMAND), WM(THEMECHANGED),
                WM(CLIPBOARDUPDATE), WM(DWMCOMPOSITIONCHANGED),
                WM(DWMNCRENDERINGCHANGED), WM(DWMCOLORIZATIONCOLORCHANGED),
                WM(DWMWINDOWMAXIMIZEDCHANGE), WM(GETTITLEBARINFOEX),
                WM(HANDHELDFIRST), WM(HANDHELDLAST), WM(AFXFIRST), WM(AFXLAST),
                WM(PENWINFIRST), WM(PENWINLAST),
#undef WM
                tmp = dict;
            if(TJS_FAILED(obj->PropSet(TJS_MEMBERENSURE,
                                       TJS_W("_Notifications"), 0, &tmp, obj)))
                return false;
        }
        return true;
    }

private:
    struct HotKeyRegistration {
        tjs_int key;
        tjs_int modifiers;
    };

    iTJSDispatch2 *self, *menuex;
    iTJSDispatch2 *sysMenuModified,
        *sysMenuModMap; //< システムメニュー改変用
    HWND cachedHWND;
    HMENU sysMenu;
    HICON externalIcon;
    bool hasResizing, hasMoving, hasMove,
        hasNcMsMove; //< メソッドが存在するかフラグ
    bool disableResize; //< サイズ変更禁止
    bool disableMove; //< ウィンドウ移動禁止
    bool enableNCMEvent; //< WM_SETCURSORコールバック
    bool enableWinMsgHook; //< メッセージフック有効
    DWORD bitHooks[0x0400 / 32];
    std::unordered_map<tjs_int, HotKeyRegistration> hotKeys;

    // Portable window state. The host form remains the source of truth for
    // size/visibility; these flags preserve the Win32 WindowEx properties so
    // scripts can round-trip them across platforms.
    bool maximized;
    bool minimized;
    bool maximizeBox;
    bool minimizeBox;
    tjs_int lastMenuKey;
    bool deviceChangeRegistered;
    bool imeControlAcquired;
    bool imeContextReset;
    tjs_int cornerPreference;
    tTVPRect normalRect;
    tTJSVariant iconToken;

public:
    //----------------------------------------------------------
    // オーバーレイビットマップ用サブクラス
    //----------------------------------------------------------
    struct OverlayBitmap {
        OverlayBitmap()
            : overlay(0), bitmap(0), bmpdc(0), bmpx(0), bmpy(0), bmpw(0),
              bmph(0) {}

        ~OverlayBitmap() {
#if defined(_WIN32)
            if(overlay)
                ::DestroyWindow(overlay);
            removeBitmap();
#endif
        }

#if defined(_WIN32)
        static BOOL CALLBACK SearchScrollBox(HWND hwnd, LPARAM param) {
            auto *result = reinterpret_cast<HWND *>(param);
            wchar_t name[256]{};
            ::GetClassNameW(hwnd, name, static_cast<int>(std::size(name)));
            if(ttstr(name) == TJS_W("TScrollBox")) {
                *result = hwnd;
                return FALSE;
            }
            return TRUE;
        }

        static LRESULT WINAPI WndProc(HWND hwnd, UINT message, WPARAM wp,
                                      LPARAM lp) {
            auto *self = reinterpret_cast<OverlayBitmap *>(
                ::GetWindowLongPtr(hwnd, GWLP_USERDATA));
            if(self && message == WM_PAINT) {
                self->onPaint(hwnd);
                return 0;
            }
            return ::DefWindowProc(hwnd, message, wp, lp);
        }

        void drawBitmap(HDC dc) const {
            if(dc && bitmap && bmpdc)
                ::BitBlt(dc, bmpx, bmpy, bmpw, bmph, bmpdc, 0, 0, SRCCOPY);
        }

        void removeBitmap() {
            if(bmpdc)
                ::DeleteDC(bmpdc);
            if(bitmap)
                ::DeleteObject(bitmap);
            bmpdc = nullptr;
            bitmap = nullptr;
        }

        void onPaint(HWND hwnd) {
            PAINTSTRUCT paint{};
            HDC dc = ::BeginPaint(hwnd, &paint);
            drawBitmap(dc);
            ::EndPaint(hwnd, &paint);
        }

        static HBITMAP CopyLayerToBitmap(HDC dc, int alphaThreshold,
                                         iTJSDispatch2 *layer, tjs_int &width,
                                         tjs_int &height, tjs_int *left = nullptr,
                                         tjs_int *top = nullptr) {
            width = 0;
            height = 0;
            if(!dc || !layer)
                return nullptr;
            ncbPropAccessor object(layer);
            if(left)
                *left = object.getIntValue(TJS_W("left"));
            if(top)
                *top = object.getIntValue(TJS_W("top"));
            width = object.getIntValue(TJS_W("imageWidth"));
            height = object.getIntValue(TJS_W("imageHeight"));
            const tjs_int pitch =
                object.getIntValue(TJS_W("mainImageBufferPitch"));
            auto *source = reinterpret_cast<const unsigned char *>(
                object.getIntPtrValue(TJS_W("mainImageBuffer")));
            if(width <= 0 || height <= 0 || pitch == 0 || !source ||
               width > 32768 || height > 32768)
                return nullptr;

            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = alphaThreshold > 0 ? 32 : 24;
            info.bmiHeader.biCompression = BI_RGB;

            unsigned char *pixels = nullptr;
            HBITMAP result = ::CreateDIBSection(
                dc, &info, DIB_RGB_COLORS, reinterpret_cast<void **>(&pixels),
                nullptr, 0);
            if(!result || !pixels) {
                if(result)
                    ::DeleteObject(result);
                return nullptr;
            }

            if(alphaThreshold > 0) {
                const unsigned char threshold = static_cast<unsigned char>(
                    std::clamp(alphaThreshold, 1, 255));
                const std::size_t rowBytes =
                    (static_cast<std::size_t>(width) * 4u + 3u) & ~3u;
                for(tjs_int row = height - 1; row >= 0; --row) {
                    const auto *src = source +
                        static_cast<std::ptrdiff_t>(row) * pitch;
                    auto *dst = pixels +
                        static_cast<std::size_t>(height - 1 - row) * rowBytes;
                    for(tjs_int column = 0; column < width; ++column) {
                        dst[0] = src[0];
                        dst[1] = src[1];
                        dst[2] = src[2];
                        dst[3] = src[3] >= threshold ? 255 : 0;
                        src += 4;
                        dst += 4;
                    }
                }
            } else {
                const std::size_t rowBytes =
                    (static_cast<std::size_t>(width) * 3u + 3u) & ~3u;
                for(tjs_int row = height - 1; row >= 0; --row) {
                    const auto *src = source +
                        static_cast<std::ptrdiff_t>(row) * pitch;
                    auto *dst = pixels +
                        static_cast<std::size_t>(height - 1 - row) * rowBytes;
                    for(tjs_int column = 0; column < width; ++column) {
                        dst[0] = src[0];
                        dst[1] = src[1];
                        dst[2] = src[2];
                        src += 4;
                        dst += 3;
                    }
                }
            }
            return result;
        }

        bool initOverlay(HWND parent) {
            if(!parent)
                return false;
            HINSTANCE instance = ::GetModuleHandle(nullptr);
            if(!instance)
                return false;
            if(!WindowClass) {
                WNDCLASSEXW cls{};
                cls.cbSize = sizeof(cls);
                cls.style = CS_PARENTDC | CS_VREDRAW | CS_HREDRAW;
                cls.lpfnWndProc = WndProc;
                cls.hInstance = instance;
                cls.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
                cls.hbrBackground =
                    reinterpret_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
                cls.lpszClassName =
                    L"AetherKiri WindowEx OverlayBitmap Window Class";
                WindowClass = ::RegisterClassExW(&cls);
                if(!WindowClass && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                    return false;
            }
            if(!overlay) {
                overlay = ::CreateWindowExW(
                    WS_EX_TOPMOST,
                    L"AetherKiri WindowEx OverlayBitmap Window Class",
                    L"WindowExOverlayBitmap", WS_CHILDWINDOW, 0, 0, 1, 1,
                    parent, nullptr, instance, nullptr);
                if(!overlay)
                    return false;
                ::SetWindowLongPtr(overlay, GWLP_USERDATA,
                                   reinterpret_cast<LONG_PTR>(this));
            }
            return true;
        }
#endif

        bool setBitmap(iTJSDispatch2 *win, iTJSDispatch2 *lay) {
            if(!lay || !lay->IsInstanceOf(0, 0, 0, TJS_W("Layer"), lay))
                return false;
#if defined(_WIN32)
            HWND base = WindowEx::GetHWND(win);
            if(!initOverlay(base))
                return false;
            HWND parent = nullptr;
            ::EnumChildWindows(base, SearchScrollBox,
                               reinterpret_cast<LPARAM>(&parent));
            if(!parent)
                parent = base;

            removeBitmap();
            HDC dc = ::GetDC(overlay);
            if(!dc)
                return false;
            bmpdc = ::CreateCompatibleDC(dc);
            bitmap = CopyLayerToBitmap(dc, 0, lay, bmpw, bmph, &bmpx, &bmpy);
            if(bitmap && bmpdc)
                ::SelectObject(bmpdc, bitmap);
            if(!bitmap || !bmpdc) {
                removeBitmap();
                ::ReleaseDC(overlay, dc);
                return false;
            }
            RECT rect{};
            if(!::GetClientRect(parent, &rect)) {
                removeBitmap();
                ::ReleaseDC(overlay, dc);
                return false;
            }
            ::SetParent(overlay, parent);
            ::SetWindowPos(overlay, HWND_TOP, 0, 0, rect.right - rect.left,
                           rect.bottom - rect.top, SWP_NOACTIVATE);
            ::InvalidateRect(overlay, nullptr, TRUE);
            ::UpdateWindow(overlay);
            ::ShowWindow(overlay, SW_SHOWNOACTIVATE);
            drawBitmap(dc);
            ::ReleaseDC(overlay, dc);
            return true;
#else
            (void)win;
            TVPAddLog(TJS_W("AetherKiri windowEx: overlay bitmap requires a host compositor"));
            return false;
#endif
        }

    private:
        HWND overlay;
        HBITMAP bitmap;
        HDC bmpdc;
        tjs_int bmpx, bmpy, bmpw, bmph;
#if defined(_WIN32)
        inline static ATOM WindowClass = 0;
#endif
    } *ovbmp;
};

// 拡張イベント用ネイティブインスタンスゲッタ
NCB_GET_INSTANCE_HOOK(WindowEx){
    /**/ NCB_GET_INSTANCE_HOOK_CLASS(){}

    /**/ ~NCB_GET_INSTANCE_HOOK_CLASS(){}

    NCB_INSTANCE_GETTER(objthis){ ClassT *obj = GetNativeInstance(objthis);
if(!obj)
    SetNativeInstance(objthis, (obj = new ClassT(objthis)));
return obj;
}
}
;

// メソッド追加
NCB_ATTACH_CLASS_WITH_HOOK(WindowEx, Window) {
    Variant(TJS_W("nchtError"), (tjs_int)(HTERROR & 0xFFFF));
    Variant(TJS_W("nchtTransparent"), (tjs_int)(HTTRANSPARENT & 0xFFFF));
    Variant(TJS_W("nchtNoWhere"), (tjs_int)HTNOWHERE);
    Variant(TJS_W("nchtClient"), (tjs_int)HTCLIENT);
    Variant(TJS_W("nchtCaption"), (tjs_int)HTCAPTION);
    Variant(TJS_W("nchtSysMenu"), (tjs_int)HTSYSMENU);
    Variant(TJS_W("nchtSize"), (tjs_int)HTSIZE);
    Variant(TJS_W("nchtGrowBox"), (tjs_int)HTGROWBOX);
    Variant(TJS_W("nchtMenu"), (tjs_int)HTMENU);
    Variant(TJS_W("nchtHScroll"), (tjs_int)HTHSCROLL);
    Variant(TJS_W("nchtVScroll"), (tjs_int)HTVSCROLL);
    Variant(TJS_W("nchtMinButton"), (tjs_int)HTMINBUTTON);
    Variant(TJS_W("nchtReduce"), (tjs_int)HTREDUCE);
    Variant(TJS_W("nchtMaxButton"), (tjs_int)HTMAXBUTTON);
    Variant(TJS_W("nchtZoom"), (tjs_int)HTZOOM);
    Variant(TJS_W("nchtLeft"), (tjs_int)HTLEFT);
    Variant(TJS_W("nchtRight"), (tjs_int)HTRIGHT);
    Variant(TJS_W("nchtTop"), (tjs_int)HTTOP);
    Variant(TJS_W("nchtTopLeft"), (tjs_int)HTTOPLEFT);
    Variant(TJS_W("nchtTopRight"), (tjs_int)HTTOPRIGHT);
    Variant(TJS_W("nchtBottom"), (tjs_int)HTBOTTOM);
    Variant(TJS_W("nchtBottomLeft"), (tjs_int)HTBOTTOMLEFT);
    Variant(TJS_W("nchtBottomRight"), (tjs_int)HTBOTTOMRIGHT);
    Variant(TJS_W("nchtBorder"), (tjs_int)HTBORDER);

    RawCallback(TJS_W("minimize"), &Class::minimize, 0);
    RawCallback(TJS_W("maximize"), &Class::maximize, 0);
    RawCallback(TJS_W("maximizeBox"), &Class::getMaximizeBox,
                &Class::setMaximizeBox, 0);
    RawCallback(TJS_W("minimizeBox"), &Class::getMinimizeBox,
                &Class::setMinimizeBox, 0);
    RawCallback(TJS_W("maximized"), &Class::getMaximized,
                &Class::setMaximized, 0);
    RawCallback(TJS_W("minimized"), &Class::getMinimized,
                &Class::setMinimized, 0);
    RawCallback(TJS_W("showRestore"), &Class::showRestore, 0);
    RawCallback(TJS_W("resetWindowIcon"), &Class::resetWindowIcon, 0);
    RawCallback(TJS_W("setWindowIcon"), &Class::setWindowIcon, 0);
    RawCallback(TJS_W("getWindowRect"), &Class::getWindowRect, 0);
    RawCallback(TJS_W("getClientRect"), &Class::getClientRect, 0);
    RawCallback(TJS_W("setClientRect"), &Class::setClientRect, 0);
    RawCallback(TJS_W("getNormalRect"), &Class::getNormalRect, 0);
    RawCallback(TJS_W("disableResize"), &Class::getDisableResize,
                &Class::setDisableResize, 0);
    RawCallback(TJS_W("disableMove"), &Class::getDisableMove,
                &Class::setDisableMove, 0);
    RawCallback(TJS_W("setOverlayBitmap"), &Class::setOverlayBitmap, 0);
    RawCallback(TJS_W("exSystemMenu"), &Class::getExSystemMenu,
                &Class::setExSystemMenu, 0);
    RawCallback(TJS_W("resetExSystemMenu"), &Class::resetExSystemMenu, 0);
    RawCallback(TJS_W("enableNCMouseEvent"), &Class::getEnNCMEvent,
                &Class::setEnNCMEvent, 0);
    RawCallback(TJS_W("ncHitTest"), &Class::nonClientHitTest, 0);
    RawCallback(TJS_W("focusMenuByKey"), &Class::focusMenuByKey, 0);
    RawCallback(TJS_W("setMessageHook"), &Class::setMessageHook, 0);
    RawCallback(TJS_W("bringTo"), &Class::bringTo, 0);
    RawCallback(TJS_W("sendToBack"), &Class::sendToBack, 0);
    RawCallback(TJS_W("registerDeviceChange"), &Class::registerDeviceChange,
                0);
    RawCallback(TJS_W("registerHotKey"), &Class::registerHotKey, 0);
    RawCallback(TJS_W("acquireImeControl"), &Class::acquireImeControl, 0);
    RawCallback(TJS_W("resetImeContext"), &Class::resetImeContext, 0);
    RawCallback(TJS_W("setWindowCornerPreference"),
                &Class::setWindowCornerPreference, 0);

    Method(TJS_W("registerExEvent"), &Class::checkExEvents);
    Method(TJS_W("getNotificationNum"), &Class::getWindowNotificationNum);
    Method(TJS_W("getNotificationName"), &Class::getWindowNotificationName);
}

////////////////////////////////////////////////////////////////
struct MenuItemEx {
    enum { BMP_ITEM, BMP_CHK, BMP_UNCHK, BMP_MAX };
    enum { BMT_NONE, BMT_SYS, BMT_BMP };

    // メニューを取得
    static HMENU GetHMENU(iTJSDispatch2 *obj) {
        if(!obj)
            return nullptr;
        tTJSVariant val;
        iTJSDispatch2 *global = TVPGetScriptDispatch(), *mi;
        if(global) {
            if(TJS_FAILED(global->PropGet(TJS_IGNOREPROP, TJS_W("MenuItem"),
                                           0, &val, obj)) ||
               val.Type() != tvtObject || !val.AsObjectNoAddRef()) {
                val.Clear();
                mi = obj;
            } else {
                mi = val.AsObjectNoAddRef();
            }
            global->Release();
        } else
            mi = obj;
        if(!mi || TJS_FAILED(mi->PropGet(TJS_IGNOREPROP, TJS_W("HMENU"),
                                         0, &val, obj)) ||
           (val.Type() != tvtInteger && val.Type() != tvtReal))
            return nullptr;
        return (HMENU)(tjs_int64)(val);
    }

    // 親メニューを取得
    static iTJSDispatch2 *GetParentMenu(iTJSDispatch2 *obj) {
        if(!obj)
            return nullptr;
        tTJSVariant val;
        if(TJS_FAILED(obj->PropGet(TJS_IGNOREPROP, TJS_W("parent"), 0, &val,
                                   obj)) ||
           val.Type() != tvtObject)
            return nullptr;
        return val.AsObjectNoAddRef();
    }

    // ルートメニューの子かどうか
    static bool IsRootChild(iTJSDispatch2 *obj) {
        if(!obj)
            return false;
        tTJSVariant par, root;
        if(TJS_FAILED(obj->PropGet(TJS_IGNOREPROP, TJS_W("parent"), 0, &par,
                                   obj)) ||
           TJS_FAILED(obj->PropGet(TJS_IGNOREPROP, TJS_W("root"), 0, &root,
                                   obj)))
            return false;
        iTJSDispatch2 *p = par.Type() == tvtObject ? par.AsObjectNoAddRef()
                                                    : nullptr;
        iTJSDispatch2 *r = root.Type() == tvtObject ? root.AsObjectNoAddRef()
                                                    : nullptr;
        return (p && r && p == r);
    }

    // （泥臭い手段で）インデックスを取得
    static UINT GetIndex(iTJSDispatch2 *obj, iTJSDispatch2 *parent) {
        if(!obj || !parent)
            return (UINT)-1;
        tTJSVariant val, child;
        if(TJS_FAILED(parent->PropGet(TJS_IGNOREPROP, TJS_W("children"), 0,
                                      &child, parent)))
            return (UINT)-1;
        ncbPropAccessor charr(child);
        if(!charr.IsValid())
            return (UINT)-1;

        if(TJS_FAILED(obj->PropGet(TJS_IGNOREPROP, TJS_W("index"), 0, &val,
                                   obj)))
            return (UINT)-1;
        int max = (int)val.AsInteger();
        if(max < 0)
            return (UINT)-1;
        UINT ret = (UINT)max;
        for(int i = 0; i <= max; i++) {
            tTJSVariant vitem;
            if(charr.checkVariant(i, vitem)) {
                ncbPropAccessor item(vitem);
                if(item.IsValid()) {
                    // 非表示の場合はカウントされない
                    if(!item.getIntValue(TJS_W("visible"))) {
                        if(i == max)
                            return (UINT)-1;
                        ret--;
                    }
                }
            }
        }
        return ret;
    }

    // ウィンドウを取得
    static iTJSDispatch2 *GetWindow(iTJSDispatch2 *obj) {
        if(!obj)
            return nullptr;
        tTJSVariant val;
        obj->PropGet(0, TJS_W("root"), 0, &val, obj);
        obj = val.AsObjectNoAddRef();
        if(!obj)
            return nullptr;
        val.Clear();
        obj->PropGet(0, TJS_W("window"), 0, &val, obj);
        return val.AsObjectNoAddRef();
    }

    static HWND GetHWND(iTJSDispatch2 *obj) {
        iTJSDispatch2 *win = GetWindow(obj);
        return WindowEx::GetHWND(win);
    }

    // property rightJustify
    tjs_int getRightJustify() const { return rj > 0; }

    void setRightJustify(tTJSVariant v) {
        rj = !!v.AsInteger();
        updateMenuItemInfo();
    }

    // property bmpItem
    tjs_int getBmpItem() const { return getBmpSelect(BMP_ITEM); }

    void setBmpItem(tTJSVariant v) { setBmpSelect(v, BMP_ITEM); }

    // property bmpChecked
    tjs_int getBmpChecked() const { return getBmpSelect(BMP_CHK); }

    void setBmpChecked(tTJSVariant v) { setBmpSelect(v, BMP_CHK); }

    // property bmpUnchecked
    tjs_int getBmpUnchecked() const { return getBmpSelect(BMP_UNCHK); }

    void setBmpUnchecked(tTJSVariant v) { setBmpSelect(v, BMP_UNCHK); }

    tjs_int64 getBmpSelect(int sel) const {
        if(sel < 0 || sel >= BMP_MAX)
            return 0;
        switch(bmptype[sel]) {
            case BMT_SYS:
                return static_cast<tjs_int64>(
                    reinterpret_cast<std::intptr_t>(bitmap[sel]));
            case BMT_BMP:
                return -1;
            default:
                return 0;
        }
    }

    void setBmpSelect(tTJSVariant &v, int sel) {
        if(sel < 0 || sel >= BMP_MAX)
            return;
        removeBitmap(sel);
        switch(v.Type()) {
        case tvtVoid:
            bmptype[sel] = BMT_NONE;
            break;
        case tvtInteger:
        case tvtString:
            bmptype[sel] = BMT_SYS;
            bitmap[sel] = reinterpret_cast<HBITMAP>(
                static_cast<std::intptr_t>(v.AsInteger()));
            break;
        case tvtObject:
#if defined(_WIN32)
            iTJSDispatch2 *lay = v.AsObjectNoAddRef();
            if(!lay || !lay->IsInstanceOf(0, 0, 0, TJS_W("Layer"), lay))
                TVPThrowExceptionMessage(TJS_W("no layer object."));
            tjs_int w = 0, h = 0;
            HWND hwnd = GetHWND(obj);
            HDC dc = hwnd ? ::GetDC(hwnd) : nullptr;
            bitmap[sel] = WindowEx::OverlayBitmap::CopyLayerToBitmap(
                dc, 64, lay, w, h);
            if(dc)
                ::ReleaseDC(hwnd, dc);
            if(!bitmap[sel])
                return;
            bmptype[sel] = BMT_BMP;
#else
            bitmapObject[sel] = v;
            bmptype[sel] = BMT_BMP;
#endif
            break;
        default:
            break;
        }
        updateMenuItemInfo();
    }

    void removeBitmap(int sel) {
        if(sel < 0 || sel >= BMP_MAX)
            return;
#if defined(_WIN32)
        if(bitmap[sel] && bmptype[sel] == BMT_BMP)
            ::DeleteObject(bitmap[sel]);
#endif
        bmptype[sel] = BMT_NONE;
        bitmap[sel] = nullptr;
        bitmapObject[sel].Clear();
    }

    void updateMenuItemInfo() {
#if defined(_WIN32)
        iTJSDispatch2 *parent = GetParentMenu(obj);
        HMENU hmenu = GetHMENU(parent);
        if(!hmenu)
            return;
        MENUITEMINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_ID | MIIM_BITMAP | MIIM_CHECKMARKS | MIIM_FTYPE;
        UINT indexOrId = id;
        BOOL byPosition = FALSE;
        if(!id || !::GetMenuItemInfo(hmenu, indexOrId, byPosition, &info)) {
            indexOrId = GetIndex(obj, parent);
            byPosition = TRUE;
            if(indexOrId == static_cast<UINT>(-1) ||
               !::GetMenuItemInfo(hmenu, indexOrId, byPosition, &info))
                return;
        }
        if(bmptype[BMP_ITEM])
            info.hbmpItem = bitmap[BMP_ITEM];
        if(bmptype[BMP_CHK])
            info.hbmpChecked = bitmap[BMP_CHK];
        if(bmptype[BMP_UNCHK])
            info.hbmpUnchecked = bitmap[BMP_UNCHK];
        if(rj > 0)
            info.fType |= MFT_RIGHTJUSTIFY;
        else if(rj == 0)
            info.fType &= ~MFT_RIGHTJUSTIFY;
        if(::SetMenuItemInfo(hmenu, indexOrId, byPosition, &info) &&
           info.wID != id)
            updateMenuItemID();
        if(IsRootChild(obj))
            ::DrawMenuBar(GetHWND(obj));
#else
        iTJSDispatch2 *parent = GetParentMenu(obj);
        HMENU hmenu = GetHMENU(parent);
        if(hmenu != nullptr) {
            GetIndex(obj, parent);
        }
#endif
    }

    static UINT GetMenuItemID(iTJSDispatch2 *obj) {
        if(!obj)
            return 0;
#if defined(_WIN32)
        iTJSDispatch2 *parent = GetParentMenu(obj);
        HMENU hmenu = GetHMENU(parent);
        const UINT index = GetIndex(obj, parent);
        if(!hmenu || index == static_cast<UINT>(-1))
            return 0;
        // The native implementation queries MIIM_ID here. Keep the query in
        // the Windows branch so the portable fallback never interprets a
        // sentinel pointer as a menu handle.
        MENUITEMINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_ID;
        return ::GetMenuItemInfo(hmenu, index, TRUE, &info) ? info.wID : 0;
#else
        // A portable menu has no OS command namespace. Allocate a stable
        // per-process range matching the upstream popup IDs and expose it
        // through WindowEx's object map just like the native path.
        static std::atomic<UINT> nextId{0x4000};
        UINT id = nextId.fetch_add(1, std::memory_order_relaxed);
        if(id == 0 || id == static_cast<UINT>(-1) || id > 0xfffe) {
            nextId.store(0x4000, std::memory_order_relaxed);
            id = nextId.fetch_add(1, std::memory_order_relaxed);
        }
        return id;
#endif
    }

    void updateMenuItemID() {
#if defined(_WIN32)
        if(id != 0)
            setMenuItemID(false);
        id = GetMenuItemID(obj);
#else
        // Portable menus do not have an operating-system command namespace.
        // Keep the first allocated id stable for the lifetime of the TJS
        // object; rebuilding a popup must not invalidate Window.menu[id].
        if(id == 0)
            id = GetMenuItemID(obj);
#endif
        if(id != 0)
            setMenuItemID(true);
    }

    void setMenuItemID(bool isset) {
        iTJSDispatch2 *win = GetWindow(obj);
        if(win) {
            WindowEx *wex = WindowEx::GetInstance(win);
            if(wex)
                wex->setMenuItemID(obj, id, isset);
        }
    }

    MenuItemEx(iTJSDispatch2 *_obj) : obj(_obj), id(0), rj(-1) {
        for(int i = 0; i < BMP_MAX; ++i) {
            bmptype[i] = BMT_NONE;
            bitmap[i] = nullptr;
            bitmapObject[i].Clear();
        }
        updateMenuItemID();
    }

    ~MenuItemEx() {
        for(int i = 0; i < BMP_MAX; ++i)
            removeBitmap(i);
        setMenuItemID(false);
    }

private:
    iTJSDispatch2 *obj;
    UINT id;
    tjs_int rj;
    int bmptype[BMP_MAX];
    HBITMAP bitmap[BMP_MAX];
    tTJSVariant bitmapObject[BMP_MAX];

public:
#ifndef _WIN32
    // Opaque, deterministic menu representation used by non-Win32 hosts.
    // A real host UI may consume the structure through MenuItem.onPopupEx;
    // no fake native HMENU escapes this adapter.
    struct PortableMenuModel {
        struct Entry {
            iTJSDispatch2 *object = nullptr;
            ttstr caption;
            tjs_int id = 0;
            bool separator = false;
            bool enabled = true;
            bool checked = false;
            tjs_int group = 0;
            tjs_int menuBreak = 0;
            std::unique_ptr<PortableMenuModel> submenu;

            Entry(iTJSDispatch2 *item, const ttstr &text)
                : object(item), caption(text) {
                if(object)
                    object->AddRef();
            }
            Entry(Entry const &) = delete;
            Entry &operator=(Entry const &) = delete;
            ~Entry() {
                if(object)
                    object->Release();
            }
        };

        std::vector<std::unique_ptr<Entry>> entries;

        Entry *append(iTJSDispatch2 *item, const ttstr &caption) {
            auto entry = std::make_unique<Entry>(item, caption);
            Entry *result = entry.get();
            entries.emplace_back(std::move(entry));
            return result;
        }
    };

    static PortableMenuModel *portableMenu(HMENU menu) {
        return reinterpret_cast<PortableMenuModel *>(menu);
    }

    static bool portableBool(iTJSDispatch2 *object, const tjs_char *name,
                             bool defaultValue) {
        if(!object)
            return defaultValue;
        tTJSVariant value;
        if(TJS_FAILED(object->PropGet(TJS_IGNOREPROP, name, nullptr, &value,
                                      object)) ||
           value.Type() == tvtVoid)
            return defaultValue;
        return value.AsInteger() != 0;
    }

    static tjs_int portableInt(iTJSDispatch2 *object, const tjs_char *name,
                               tjs_int defaultValue) {
        if(!object)
            return defaultValue;
        tTJSVariant value;
        if(TJS_FAILED(object->PropGet(TJS_IGNOREPROP, name, nullptr, &value,
                                      object)) ||
           value.Type() == tvtVoid)
            return defaultValue;
        return static_cast<tjs_int>(value.AsInteger());
    }

    static void portableDescribe(const PortableMenuModel *model,
                                 iTJSDispatch2 *array) {
        if(!model || !array)
            return;
        tjs_int index = 0;
        for(auto const &entry : model->entries) {
            if(!entry)
                continue;
            ncbDictionaryAccessor description;
            description.SetValue(TJS_W("caption"), entry->caption);
            description.SetValue(TJS_W("id"), entry->id);
            description.SetValue(TJS_W("separator"), entry->separator);
            description.SetValue(TJS_W("enabled"), entry->enabled);
            description.SetValue(TJS_W("checked"), entry->checked);
            description.SetValue(TJS_W("group"), entry->group);
            description.SetValue(TJS_W("break"), entry->menuBreak);
            if(entry->object) {
                tTJSVariant item(entry->object);
                description.SetValue(TJS_W("item"), item);
            }
            if(entry->submenu) {
                iTJSDispatch2 *children = TJSCreateArrayObject();
                if(children) {
                    portableDescribe(entry->submenu.get(), children);
                    tTJSVariant childrenValue(children, children);
                    description.SetValue(TJS_W("children"), childrenValue);
                    children->Release();
                }
            }
            auto *dispatch = description.GetDispatch();
            tTJSVariant value(dispatch, dispatch);
            array->PropSetByNum(TJS_MEMBERENSURE, index++, &value, array);
        }
    }
#endif

    static bool InsertMenuItem(HMENU menu, iTJSDispatch2 *obj, WORD &curid,
                               WORD idmv, iTJSDispatch2 *items,
                               ULONG_PTR sysdt) {
#if defined(_WIN32)
        if(obj == nullptr)
            return false;
        tTJSVariant val;
        obj->PropGet(0, TJS_W("visible"), nullptr, &val, obj);
        if(val.Type() != tvtVoid && !(tjs_int)val)
            return false;

        MENUITEMINFO mi{};
        mi.cbSize = sizeof(mi);
        val.Clear();
        obj->PropGet(0, TJS_W("caption"), nullptr, &val, obj);
        ttstr caption(val);
        if(caption == TJS_W("-")) {
            mi.fMask = MIIM_FTYPE;
            mi.fType = MFT_SEPARATOR;
        } else {
            mi.fMask = MIIM_FTYPE | MIIM_STRING;
            mi.dwTypeData = const_cast<LPWSTR>(caption.c_str());
            HMENU submenu = nullptr;
            val.Clear();
            obj->PropGet(0, TJS_W("children"), nullptr, &val, obj);
            if(val.Type() == tvtObject) {
                submenu = CreateMenuList(nullptr, val.AsObjectNoAddRef(), curid,
                                         idmv, items, sysdt);
                if(submenu) {
                    mi.fMask |= MIIM_SUBMENU;
                    mi.hSubMenu = submenu;
                }
            }
            if(!submenu) {
                mi.fMask |= MIIM_ID;
                mi.wID = curid;
                if(items) {
                    tTJSVariant item(obj);
                    items->PropSetByNum(TJS_MEMBERENSURE,
                                        static_cast<tjs_int>(curid), &item,
                                        items);
                }
                curid += idmv;
                val.Clear();
                obj->PropGet(0, TJS_W("checked"), nullptr, &val, obj);
                if(val.Type() != tvtVoid && !!(tjs_int)val) {
                    mi.fMask |= MIIM_STATE;
                    mi.fState |= MFS_CHECKED;
                }
                val.Clear();
                obj->PropGet(0, TJS_W("group"), nullptr, &val, obj);
                if(val.Type() == tvtInteger && (tjs_int)val > 0)
                    mi.fType |= MFT_RADIOCHECK;
            }
            val.Clear();
            obj->PropGet(0, TJS_W("enabled"), nullptr, &val, obj);
            if(val.Type() != tvtVoid && !(tjs_int)val) {
                mi.fMask |= MIIM_STATE;
                mi.fState |= MFS_DISABLED;
            }
        }

        val.Clear();
        obj->PropGet(0, TJS_W("break"), nullptr, &val, obj);
        if(val.Type() != tvtVoid) {
            const tjs_int menuBreak = (tjs_int)val;
            if(menuBreak != 0)
                mi.fType |=
                    menuBreak > 0 ? MFT_MENUBARBREAK : MFT_MENUBREAK;
        }

        UINT position = ::GetMenuItemCount(menu);
        BOOL byPosition = TRUE;
        if(sysdt > 0) {
            mi.fMask |= MIIM_DATA;
            mi.dwItemData = sysdt;
            val.Clear();
            obj->PropGet(0, TJS_W("insertPos"), nullptr, &val, obj);
            if(val.Type() == tvtInteger) {
                const tjs_int requested = (tjs_int)val;
                if(requested >= 0)
                    position = static_cast<UINT>(requested);
            } else {
                val.Clear();
                obj->PropGet(0, TJS_W("insertID"), nullptr, &val, obj);
                if(val.Type() == tvtInteger) {
                    const tjs_int requested = (tjs_int)val;
                    if(requested >= 0) {
                        position = static_cast<UINT>(requested);
                        byPosition = FALSE;
                    }
                }
            }
        }
        return !!::InsertMenuItem(menu, position, byPosition, &mi);
#else
        auto *model = portableMenu(menu);
        if(!model || !obj || !portableBool(obj, TJS_W("visible"), true))
            return false;

        tTJSVariant value;
        obj->PropGet(TJS_IGNOREPROP, TJS_W("caption"), nullptr, &value, obj);
        ttstr caption(value);
        PortableMenuModel::Entry *entry = model->append(obj, caption);
        if(caption == TJS_W("-")) {
            entry->separator = true;
            return true;
        }

        value.Clear();
        obj->PropGet(TJS_IGNOREPROP, TJS_W("children"), nullptr, &value, obj);
        if(value.Type() == tvtObject) {
            HMENU child = CreateMenuList(nullptr, value.AsObjectNoAddRef(),
                                         curid, idmv, items, sysdt);
            if(child) {
                entry->submenu.reset(portableMenu(child));
                entry->enabled = portableBool(obj, TJS_W("enabled"), true);
                entry->menuBreak = portableInt(obj, TJS_W("break"), 0);
                return true;
            }
        }

        entry->id = static_cast<tjs_int>(curid);
        entry->enabled = portableBool(obj, TJS_W("enabled"), true);
        entry->checked = portableBool(obj, TJS_W("checked"), false);
        entry->group = portableInt(obj, TJS_W("group"), 0);
        entry->menuBreak = portableInt(obj, TJS_W("break"), 0);
        if(items) {
            tTJSVariant item(obj);
            items->PropSetByNum(TJS_MEMBERENSURE, entry->id, &item, items);
        }
        if(auto *ex = ncbInstanceAdaptor<MenuItemEx>::GetNativeInstance(obj)) {
            if(ex->id != static_cast<UINT>(entry->id)) {
                ex->setMenuItemID(false);
                ex->id = static_cast<UINT>(entry->id);
                ex->setMenuItemID(true);
            }
        }
        const unsigned step = idmv == 0 ? 1u : static_cast<unsigned>(idmv);
        curid = static_cast<WORD>(static_cast<unsigned>(curid) + step);
        return true;
#endif
    }

    static HMENU CreateMenuList(HMENU menu, iTJSDispatch2 *obj, WORD &curid,
                                WORD idmv, iTJSDispatch2 *items,
                                ULONG_PTR sysdt) {
#if defined(_WIN32)
        HMENU result = nullptr;
        tjs_int count = 0;
        tTJSVariant value;
        if(!obj || TJS_FAILED(obj->PropGet(0, TJS_W("count"), nullptr, &value,
                                           obj)) ||
           value.Type() != tvtInteger || (count = (tjs_int)value) <= 0)
            return nullptr;
        result = menu ? menu : ::CreatePopupMenu();
        if(!result)
            return nullptr;
        bool created = false;
        for(tjs_int index = 0; index < count; ++index) {
            value.Clear();
            if(TJS_SUCCEEDED(obj->PropGetByNum(0, index, &value, obj)) &&
               value.Type() == tvtObject)
                created |= InsertMenuItem(result, value.AsObjectNoAddRef(),
                                          curid, idmv, items, sysdt);
        }
        if(!created && !menu)
            ::DestroyMenu(result);
        return created ? result : nullptr;
#else
        if(!obj)
            return nullptr;
        static thread_local unsigned depth = 0;
        if(depth >= 64)
            return nullptr;
        tTJSVariant countValue;
        if(TJS_FAILED(obj->PropGet(TJS_IGNOREPROP, TJS_W("count"), nullptr,
                                   &countValue, obj)) ||
           countValue.Type() != tvtInteger)
            return nullptr;
        const tjs_int count = static_cast<tjs_int>(countValue);
        if(count <= 0 || count > 4096)
            return nullptr;

        PortableMenuModel *model = portableMenu(menu);
        const bool ownsModel = model == nullptr;
        if(ownsModel)
            model = new(std::nothrow) PortableMenuModel();
        if(!model)
            return nullptr;

        ++depth;
        bool created = false;
        for(tjs_int index = 0; index < count; ++index) {
            tTJSVariant value;
            if(TJS_SUCCEEDED(obj->PropGetByNum(TJS_IGNOREPROP, index, &value,
                                               obj)) &&
               value.Type() == tvtObject)
                created |= InsertMenuItem(
                    reinterpret_cast<HMENU>(model),
                    value.AsObjectNoAddRef(), curid, idmv, items, sysdt);
        }
        --depth;
        if(!created && ownsModel) {
            delete model;
            model = nullptr;
        }
        return reinterpret_cast<HMENU>(model);
#endif
    }

    // MenuItem.popupEx(flags, x=cursorX, y=cursorY,
    // hwnd=this.root.window, rect, menulist=this.children)
    static tjs_error popupEx(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                             iTJSDispatch2 *objthis) {
#if defined(_WIN32)
        iTJSDispatch2 *list = nullptr;
        HWND hwnd = nullptr;
        UINT flags = 0;
        POINT point{};
        TPMPARAMS params{};
        TPMPARAMS *paramsPtr = nullptr;
        ::GetCursorPos(&point);
        if(n >= 1)
            flags = static_cast<UINT>(p[0]->AsInteger());
        if(n >= 2 && p[1]->Type() != tvtVoid)
            point.x = static_cast<LONG>(p[1]->AsInteger());
        if(n >= 3 && p[2]->Type() != tvtVoid)
            point.y = static_cast<LONG>(p[2]->AsInteger());
        if(n >= 4 && p[3]->Type() == tvtObject)
            hwnd = WindowEx::GetHWND(p[3]->AsObjectNoAddRef());
        else
            hwnd = GetHWND(objthis);
        if(n >= 5 && p[4]->Type() == tvtObject) {
            params.cbSize = sizeof(params);
            ncbPropAccessor rect(*p[4]);
            WindowEx::GetRect(&params.rcExclude, rect);
            paramsPtr = &params;
        }
        if(n >= 6 && p[5]->Type() == tvtObject)
            list = p[5]->AsObjectNoAddRef();
        if(!list && objthis) {
            tTJSVariant value;
            objthis->PropGet(0, TJS_W("children"), nullptr, &value, objthis);
            if(value.Type() == tvtObject)
                list = value.AsObjectNoAddRef();
        }
        iTJSDispatch2 *items = TJSCreateDictionaryObject();
        WORD id = 0x4000;
        HMENU menu = CreateMenuList(nullptr, list, id, 1, items, 0);
        if(menu) {
            tTJSVariant value;
            flags |= TPM_NONOTIFY | TPM_RETURNCMD;
            id = static_cast<WORD>(::TrackPopupMenuEx(
                menu, flags, point.x, point.y, hwnd, paramsPtr));
            if(TJS_SUCCEEDED(items->PropGetByNum(
                   TJS_IGNOREPROP, static_cast<tjs_int>(id), &value, items)) &&
               value.Type() == tvtObject && r)
                *r = value;
            ::DestroyMenu(menu);
        }
        items->Release();
        return TJS_S_OK;
#else
        if(r)
            r->Clear();
        UINT flags = 0;
        tjs_int x = 0, y = 0;
        if(n >= 1 && p && p[0])
            flags = static_cast<UINT>(p[0]->AsInteger());
        bool hasX = n >= 2 && p && p[1] && p[1]->Type() != tvtVoid;
        bool hasY = n >= 3 && p && p[2] && p[2]->Type() != tvtVoid;
        if(hasX)
            x = static_cast<tjs_int>(p[1]->AsInteger());
        if(hasY)
            y = static_cast<tjs_int>(p[2]->AsInteger());
        if((!hasX || !hasY) && TVPMainWindow) {
            tjs_int cursorX = 0, cursorY = 0;
            TVPMainWindow->GetCursorPos(cursorX, cursorY);
            if(!hasX)
                x = cursorX;
            if(!hasY)
                y = cursorY;
        }

        iTJSDispatch2 *list = nullptr;
        if(n >= 6 && p && p[5] && p[5]->Type() == tvtObject)
            list = p[5]->AsObjectNoAddRef();
        if(!list && objthis) {
            tTJSVariant children;
            if(TJS_SUCCEEDED(objthis->PropGet(TJS_IGNOREPROP,
                                              TJS_W("children"), nullptr,
                                              &children, objthis)) &&
               children.Type() == tvtObject)
                list = children.AsObjectNoAddRef();
        }

        iTJSDispatch2 *items = TJSCreateDictionaryObject();
        if(!items)
            return TJS_E_FAIL;
        WORD id = 0x4000;
        HMENU menu = CreateMenuList(nullptr, list, id, 1, items, 0);
        if(menu) {
            auto *model = portableMenu(menu);
            iTJSDispatch2 *description = TJSCreateArrayObject();
            if(description)
                portableDescribe(model, description);

            tTJSVariant callback;
            bool called = false;
            if(objthis && description &&
               TJS_SUCCEEDED(objthis->PropGet(
                   TJS_IGNOREPROP, TJS_W("onPopupEx"), nullptr, &callback,
                   objthis)) && callback.Type() == tvtObject &&
               callback.AsObjectNoAddRef()) {
                tTJSVariant vf(static_cast<tjs_int>(flags));
                tTJSVariant vx(x), vy(y), vm(description, description), selected;
                tTJSVariant *args[] = { &vf, &vx, &vy, &vm };
                callback.AsObjectNoAddRef()->FuncCall(0, nullptr, nullptr,
                                                       &selected, 4, args,
                                                       objthis);
                called = true;
                if(selected.Type() == tvtObject) {
                    if(r)
                        *r = selected;
                } else if(selected.Type() == tvtInteger) {
                    tTJSVariant item;
                    if(TJS_SUCCEEDED(items->PropGetByNum(
                           TJS_IGNOREPROP, static_cast<tjs_int>(selected),
                           &item, items)) && item.Type() == tvtObject && r)
                        *r = item;
                }
            }
            if(!called)
                TVPAddLog(TJS_W("AetherKiri windowEx: popupEx requires an onPopupEx host callback"));
            if(description)
                description->Release();
            delete model;
        }
        items->Release();
        return TJS_S_OK;
#endif
    }
};

NCB_GET_INSTANCE_HOOK(MenuItemEx){
    /**/ NCB_GET_INSTANCE_HOOK_CLASS(){}

    /**/ ~NCB_GET_INSTANCE_HOOK_CLASS(){}

    NCB_INSTANCE_GETTER(objthis){ ClassT *obj = GetNativeInstance(objthis);
if(!obj)
    SetNativeInstance(objthis, (obj = new ClassT(objthis)));
return obj;
}
}
;
// Note: MIIM_TYPE is replaced by MIIM_BITMAP, MIIM_FTYPE, and
// MIIM_STRING.
#ifndef _WIN32
#define HBMMENU_CALLBACK -1
#define HBMMENU_SYSTEM 1
#define HBMMENU_MBAR_RESTORE 2
#define HBMMENU_MBAR_MINIMIZE 3
#define HBMMENU_MBAR_CLOSE 5
#define HBMMENU_MBAR_CLOSE_D 6
#define HBMMENU_MBAR_MINIMIZE_D 7
#define HBMMENU_POPUP_CLOSE 8
#define HBMMENU_POPUP_RESTORE 9
#define HBMMENU_POPUP_MAXIMIZE 10
#define HBMMENU_POPUP_MINIMIZE 11
#endif

NCB_ATTACH_CLASS_WITH_HOOK(MenuItemEx, MenuItem) {
    Variant(TJS_W("biSystem"), (tjs_int64)HBMMENU_SYSTEM);
    Variant(TJS_W("biRestore"), (tjs_int64)HBMMENU_MBAR_RESTORE);
    Variant(TJS_W("biMinimize"), (tjs_int64)HBMMENU_MBAR_MINIMIZE);
    Variant(TJS_W("biClose"), (tjs_int64)HBMMENU_MBAR_CLOSE);
    Variant(TJS_W("biCloseDisabled"), (tjs_int64)HBMMENU_MBAR_CLOSE_D);
    Variant(TJS_W("biMinimizeDisabled"), (tjs_int64)HBMMENU_MBAR_MINIMIZE_D);
    Variant(TJS_W("biPopupClose"), (tjs_int64)HBMMENU_POPUP_CLOSE);
    Variant(TJS_W("biPopupRestore"), (tjs_int64)HBMMENU_POPUP_RESTORE);
    Variant(TJS_W("biPopupMaximize"), (tjs_int64)HBMMENU_POPUP_MAXIMIZE);
    Variant(TJS_W("biPopupMinimize"), (tjs_int64)HBMMENU_POPUP_MINIMIZE);

    Property(TJS_W("rightJustify"), &Class::getRightJustify,
             &Class::setRightJustify);
    Property(TJS_W("bmpItem"), &Class::getBmpItem, &Class::setBmpItem);
    Property(TJS_W("bmpChecked"), &Class::getBmpChecked, &Class::setBmpChecked);
    Property(TJS_W("bmpUnchecked"), &Class::getBmpUnchecked,
             &Class::setBmpUnchecked);
}

NCB_ATTACH_FUNCTION(popupEx, MenuItem, MenuItemEx::popupEx);

void WindowEx::checkUpdateMenuItem(HMENU menu, int pos, UINT id) {
    if(id == 0 || id == (UINT)-1)
        return;

    ttstr idstr((tjs_int)(id));
    tTJSVariant var;

    tjs_error chk = menuex->PropGet(TJS_MEMBERMUSTEXIST, idstr.c_str(),
                                    idstr.GetHint(), &var, menuex);
    if(TJS_SUCCEEDED(chk) && var.Type() == tvtObject) {
        iTJSDispatch2 *obj = var.AsObjectNoAddRef();
        MenuItemEx *ex = ncbInstanceAdaptor<MenuItemEx>::GetNativeInstance(obj);
        //        if (ex != nullptr) ex->setMenuItemInfo(menu, pos,
        //        true);
    }
}

void WindowEx::setMenuItemID(iTJSDispatch2 *obj, UINT id, bool set) {
    if(id == 0 || id == (UINT)-1)
        return;

    ttstr idstr((tjs_int)(id));
    if(!set) {
        if(menuex)
            menuex->DeleteMember(TJS_IGNOREPROP, idstr.c_str(),
                                 idstr.GetHint(), menuex);
        return;
    }
    tTJSVariant var(obj, obj);

    if(!menuex)
        menuex = TJSCreateDictionaryObject();
    menuex->PropSet(TJS_MEMBERENSURE, idstr.c_str(), idstr.GetHint(), &var,
                    menuex);
}

void WindowEx::resetSystemMenu() {
    if(sysMenuModMap != nullptr)
        sysMenuModMap->Release();
    sysMenuModMap = nullptr;
}

void WindowEx::modifySystemMenu() {
    resetSystemMenu();
    if(sysMenuModified == nullptr || cachedHWND == nullptr)
        return;
    sysMenuModMap = TJSCreateDictionaryObject();
    WORD id = 0xF000 - 1;
    sysMenu = nullptr;
    sysMenuModMap->Release();
    sysMenuModMap = nullptr;
}

////////////////////////////////////////////////////////////////
struct ConsoleEx {
    struct State {
        tjs_int x = 0;
        tjs_int y = 0;
        tjs_int width = 640;
        tjs_int height = 480;
        bool maximized = false;
    };

    static State &state() {
        static State value;
        return value;
    }

    static bool syncHostSize() {
        if(!TVPMainWindow || !TVPMainWindow->GetForm())
            return false;
        tjs_int width = 0;
        tjs_int height = 0;
        TVPMainWindow->GetForm()->GetSize(width, height);
        if(width > 0)
            state().width = width;
        if(height > 0)
            state().height = height;
        return true;
    }

    static bool applyHostSize(tjs_int width, tjs_int height) {
        if(width <= 0 || height <= 0 || !TVPMainWindow ||
           !TVPMainWindow->GetForm())
            return false;
        TVPMainWindow->GetForm()->SetSize(width, height);
        state().width = width;
        state().height = height;
        return true;
    }

    static tjs_error restoreMaximize(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                     iTJSDispatch2 *obj) {
        state().maximized = false;
        const bool available = syncHostSize();
        if(r)
            *r = available;
        return TJS_S_OK;
    }

    static tjs_error maximize(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                              iTJSDispatch2 *obj) {
        const bool available = applyHostSize(
            std::max(1, tTVPScreen::GetDesktopWidth()),
            std::max(1, tTVPScreen::GetDesktopHeight()));
        state().maximized = available;
        if(r)
            *r = available;
        return TJS_S_OK;
    }

    // getRect
    static tjs_error getRect(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                             iTJSDispatch2 *obj) {
        syncHostSize();
        ncbDictionaryAccessor dict;
        dict.SetValue(TJS_W("x"), state().x);
        dict.SetValue(TJS_W("y"), state().y);
        dict.SetValue(TJS_W("w"), state().width);
        dict.SetValue(TJS_W("h"), state().height);
        if(r) {
            auto *dispatch = dict.GetDispatch();
            r->SetObject(dispatch, dispatch);
        }
        return TJS_S_OK;
    }

    static tjs_error setPos(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                            iTJSDispatch2 *obj) {
        if(n < 2)
            return TJS_E_BADPARAMCOUNT;
        if(!p || !p[0] || !p[1])
            return TJS_E_INVALIDPARAM;
        state().x = static_cast<tjs_int>(*p[0]);
        state().y = static_cast<tjs_int>(*p[1]);
        bool changed = true;
        if(n >= 4 && p[2] && p[3])
            changed = applyHostSize(static_cast<tjs_int>(*p[2]),
                                    static_cast<tjs_int>(*p[3]));
        if(r)
            *r = changed;
        return TJS_S_OK;
    }

    static tjs_error bringAfter(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                iTJSDispatch2 *obj) {
        tTJSNI_BaseWindow *target = WindowEx::GetNativeWindow(obj);
        if(n > 0 && p && p[0] && p[0]->Type() == tvtObject)
            target = WindowEx::GetNativeWindow(p[0]->AsObjectNoAddRef());
        const bool available = target && target->GetForm();
        if(available)
            target->GetForm()->BringToFront();
        if(r)
            *r = available;
        return TJS_S_OK;
    }

    static tjs_error getPlacement(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                  iTJSDispatch2 *obj) {
        syncHostSize();
        ncbDictionaryAccessor dict;
        dict.SetValue(TJS_W("flags"), 0);
        dict.SetValue(TJS_W("showCmd"), state().maximized ? 3 : 1);
        dict.SetValue(TJS_W("minLeft"), 0);
        dict.SetValue(TJS_W("minTop"), 0);
        dict.SetValue(TJS_W("maxLeft"), tTVPScreen::GetDesktopWidth());
        dict.SetValue(TJS_W("maxTop"), tTVPScreen::GetDesktopHeight());
        dict.SetValue(TJS_W("normalLeft"), state().x);
        dict.SetValue(TJS_W("normalTop"), state().y);
        dict.SetValue(TJS_W("normalRight"), state().x + state().width);
        dict.SetValue(TJS_W("normalBottom"), state().y + state().height);
        if(r) {
            auto *dispatch = dict.GetDispatch();
            r->SetObject(dispatch, dispatch);
        }
        return TJS_S_OK;
    }

    static tjs_error setPlacement(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                  iTJSDispatch2 *obj) {
        if(n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;
        if(p[0]->Type() != tvtObject)
            return TJS_E_INVALIDPARAM;
        ncbPropAccessor placement(*p[0]);
        state().x = placement.getIntValue(TJS_W("normalLeft"), state().x);
        state().y = placement.getIntValue(TJS_W("normalTop"), state().y);
        const tjs_int right = placement.getIntValue(
            TJS_W("normalRight"), state().x + state().width);
        const tjs_int bottom = placement.getIntValue(
            TJS_W("normalBottom"), state().y + state().height);
        const tjs_int width = right - state().x;
        const tjs_int height = bottom - state().y;
        const bool changed = width > 0 && height > 0
            ? applyHostSize(width, height) : false;
        state().maximized = placement.getIntValue(TJS_W("showCmd"), 1) == 3;
        if(r)
            *r = changed || !TVPMainWindow;
        return TJS_S_OK;
    }
};

NCB_ATTACH_FUNCTION_WITHTAG(restoreMaximize, Debug_console, Debug.console,
                            ConsoleEx::restoreMaximize);
NCB_ATTACH_FUNCTION_WITHTAG(maximize, Debug_console, Debug.console,
                            ConsoleEx::maximize);
NCB_ATTACH_FUNCTION_WITHTAG(getRect, Debug_console, Debug.console,
                            ConsoleEx::getRect);
NCB_ATTACH_FUNCTION_WITHTAG(setPos, Debug_console, Debug.console,
                            ConsoleEx::setPos);
NCB_ATTACH_FUNCTION_WITHTAG(getPlacement, Debug_console, Debug.console,
                            ConsoleEx::getPlacement);
NCB_ATTACH_FUNCTION_WITHTAG(setPlacement, Debug_console, Debug.console,
                            ConsoleEx::setPlacement);
NCB_ATTACH_FUNCTION_WITHTAG(bringAfter, Debug_console, Debug.console,
                            ConsoleEx::bringAfter);

////////////////////////////////////////////////////////////////
struct PadEx {
    struct SearchWork {
        ttstr name, title;
        HWND result;
    };

    static HWND GetHWND(iTJSDispatch2 *obj) {
        tTJSVariant val, _uuid;
        TVPExecuteExpression(TJS_W("System.createUUID()"), &_uuid);
        obj->PropGet(0, TJS_W("title"), 0, &val, obj);
        obj->PropSet(0, TJS_W("title"), 0, &_uuid, obj);

        SearchWork wk = { TJS_W("TTVPPadForm"), _uuid, nullptr };
        obj->PropSet(0, TJS_W("title"), 0, &val, obj);
        return wk.result;
    }

    // メンバが存在するか
    bool hasMember(tjs_char const *name) const {
        tTJSVariant func;
        return TJS_SUCCEEDED(
            self->PropGet(TJS_MEMBERMUSTEXIST, name, 0, &func, self));
    }

    // TJSメソッド呼び出し
    tjs_error funcCall(tjs_char const *name, tTJSVariant *result,
                       tjs_int numparams = 0, tTJSVariant **params = 0) const {
        //        return Try_iTJSDispatch2_FuncCall(self, 0, name, 0,
        //        result, numparams, params, self);
        return self->FuncCall(0, name, 0, result, numparams, params, self);
    }

    // 引数なしコールバック
    bool callback(tjs_char const *name) const {
        if(!hasMember(name))
            return false;
        tTJSVariant rslt;
        funcCall(name, &rslt, 0, 0);
        return !!rslt.AsInteger();
    }

    void onClose() { callback(TJS_W("onClose")); }

    PadEx(iTJSDispatch2 *obj) : self(obj), hwnd(0) {}

    ~PadEx() {}

    void registerExEvents() {}

private:
    iTJSDispatch2 *self;
    HWND hwnd;
};

NCB_GET_INSTANCE_HOOK(PadEx){
    /**/ NCB_GET_INSTANCE_HOOK_CLASS(){}

    /**/ ~NCB_GET_INSTANCE_HOOK_CLASS(){}

    NCB_INSTANCE_GETTER(objthis){ ClassT *obj = GetNativeInstance(objthis);
if(!obj)
    SetNativeInstance(objthis, (obj = new ClassT(objthis)));
return obj;
}
}
;

NCB_ATTACH_CLASS_WITH_HOOK(PadEx, Pad) {
    Method(TJS_W("registerExEvent"), &Class::registerExEvents);
}
////////////////////////////////////////////////////////////////

namespace {

// Keep the Win32-shaped environment helpers at the adapter boundary.  The
// engine itself continues to use UTF-16 TJS strings and never treats a text
// value as a native window handle.
std::string windowExToUtf8(const ttstr &value) {
    return value.AsStdString();
}

ttstr windowExFromUtf8(const std::string &value) {
    return ttstr(value);
}

const char *windowExGetEnv(const std::string &name) {
    if(name.empty())
        return nullptr;
    return std::getenv(name.c_str());
}

std::string windowExExpandEnv(const std::string &source) {
    std::string expanded;
    expanded.reserve(source.size());
    for(std::size_t i = 0; i < source.size();) {
        if(source[i] == '%') {
            const std::size_t end = source.find('%', i + 1);
            if(end != std::string::npos && end > i + 1) {
                const std::string key = source.substr(i + 1, end - i - 1);
                if(const char *value = windowExGetEnv(key))
                    expanded += value;
                else
                    expanded.append(source, i, end - i + 1);
                i = end + 1;
                continue;
            }
        }
        if(source[i] == '$') {
            std::size_t keyBegin = i + 1;
            std::size_t keyEnd = keyBegin;
            if(keyBegin < source.size() && source[keyBegin] == '{') {
                keyBegin++;
                keyEnd = source.find('}', keyBegin);
                if(keyEnd == std::string::npos || keyEnd == keyBegin) {
                    expanded += source[i++];
                    continue;
                }
                const std::string key =
                    source.substr(keyBegin, keyEnd - keyBegin);
                if(const char *value = windowExGetEnv(key))
                    expanded += value;
                else
                    expanded.append(source, i, keyEnd - i + 1);
                i = keyEnd + 1;
                continue;
            }
            while(keyEnd < source.size() &&
                  (std::isalnum(static_cast<unsigned char>(source[keyEnd])) ||
                   source[keyEnd] == '_'))
                ++keyEnd;
            if(keyEnd > keyBegin) {
                const std::string key =
                    source.substr(keyBegin, keyEnd - keyBegin);
                if(const char *value = windowExGetEnv(key))
                    expanded += value;
                else
                    expanded.append(source, i, keyEnd - i);
                i = keyEnd;
                continue;
            }
        }
        expanded += source[i++];
    }
    return expanded;
}

// A small stable subset of GetSystemMetrics covers the values used by
// portable games.  Display values come from Aether's screen owner; decoration
// and input values use the defaults documented by krkrz.
bool windowExPortableMetric(const ttstr &key, tjs_int &value) {
    const tjs_int width = std::max(1, tTVPScreen::GetDesktopWidth());
    const tjs_int height = std::max(1, tTVPScreen::GetDesktopHeight());
    if(key == TJS_W("CXSCREEN") || key == TJS_W("CXVIRTUALSCREEN") ||
       key == TJS_W("CXFULLSCREEN") || key == TJS_W("CXMAXIMIZED") ||
       key == TJS_W("CXMAXTRACK")) {
        value = width;
        return true;
    }
    if(key == TJS_W("CYSCREEN") || key == TJS_W("CYVIRTUALSCREEN") ||
       key == TJS_W("CYFULLSCREEN") || key == TJS_W("CYMAXIMIZED") ||
       key == TJS_W("CYMAXTRACK")) {
        value = height;
        return true;
    }
    if(key == TJS_W("XVIRTUALSCREEN") || key == TJS_W("YVIRTUALSCREEN")) {
        value = 0;
        return true;
    }
    if(key == TJS_W("CMONITORS")) {
        value = 1;
        return true;
    }
    if(key == TJS_W("MOUSEPRESENT") || key == TJS_W("MOUSEWHEELPRESENT") ||
       key == TJS_W("MOUSEHORIZONTALWHEELPRESENT") ||
       key == TJS_W("SAMEDISPLAYFORMAT")) {
        value = 1;
        return true;
    }
    if(key == TJS_W("CXDOUBLECLK") || key == TJS_W("CYDOUBLECLK") ||
       key == TJS_W("CXDRAG") || key == TJS_W("CYDRAG")) {
        value = 4;
        return true;
    }
    if(key == TJS_W("CXBORDER") || key == TJS_W("CYBORDER")) {
        value = 1;
        return true;
    }
    if(key == TJS_W("CXDLGFRAME") || key == TJS_W("CYDLGFRAME") ||
       key == TJS_W("CXFIXEDFRAME") || key == TJS_W("CYFIXEDFRAME")) {
        value = 3;
        return true;
    }
    if(key == TJS_W("CXFRAME") || key == TJS_W("CYFRAME") ||
       key == TJS_W("CXSIZEFRAME") || key == TJS_W("CYSIZEFRAME")) {
        value = 4;
        return true;
    }
    if(key == TJS_W("CYCAPTION")) {
        value = 22;
        return true;
    }
    if(key == TJS_W("CXCURSOR") || key == TJS_W("CYCURSOR") ||
       key == TJS_W("CXICON") || key == TJS_W("CYICON")) {
        value = 32;
        return true;
    }
    if(key == TJS_W("CXSMICON") || key == TJS_W("CYSMICON")) {
        value = 16;
        return true;
    }
    if(key == TJS_W("CXHSCROLL") || key == TJS_W("CYVSCROLL")) {
        value = 17;
        return true;
    }
    if(key == TJS_W("CXSIZE") || key == TJS_W("CYSIZE")) {
        value = 30;
        return true;
    }
    if(key == TJS_W("CXMINTRACK")) {
        value = 160;
        return true;
    }
    if(key == TJS_W("CYMINTRACK")) {
        value = 27;
        return true;
    }
    if(key == TJS_W("CXMIN") || key == TJS_W("CYMIN")) {
        value = 0;
        return true;
    }
    if(key == TJS_W("CLEANBOOT") || key == TJS_W("DEBUG") ||
       key == TJS_W("IMMENABLED") || key == TJS_W("MEDIACENTER") ||
       key == TJS_W("NETWORK") || key == TJS_W("PENWINDOWS") ||
       key == TJS_W("REMOTECONTROL") || key == TJS_W("REMOTESESSION") ||
       key == TJS_W("SECURE") || key == TJS_W("SHOWSOUNDS") ||
       key == TJS_W("SHUTTINGDOWN") || key == TJS_W("SLOWMACHINE") ||
       key == TJS_W("STARTER") || key == TJS_W("SWAPBUTTON") ||
       key == TJS_W("TABLETPC") || key == TJS_W("DBCSENABLED")) {
        value = 0;
        return true;
    }
    return false;
}

// Win32 exposes the cursor clip rectangle and class-long values as process
// global/window-class state.  The portable host has no HWND/class registry,
// so keep the same observable state at the adapter boundary.  We deliberately
// store logical values only; no fake native handle is handed to an OS API.
struct WindowExPortableState {
    std::mutex mutex;
    bool clipActive = false;
    tTVPRect clipRect{};
    std::unordered_map<std::uintptr_t, std::array<tTVInteger, 4>> classValues;
};

WindowExPortableState &windowExPortableState() {
    static WindowExPortableState state;
    return state;
}

std::uintptr_t windowExHandleKey(HWND handle) {
    return reinterpret_cast<std::uintptr_t>(handle);
}

void windowExClampCursor(tjs_int &x, tjs_int &y) {
    auto &state = windowExPortableState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if(!state.clipActive || state.clipRect.is_empty())
        return;
    x = std::clamp(x, state.clipRect.left, state.clipRect.right - 1);
    y = std::clamp(y, state.clipRect.top, state.clipRect.bottom - 1);
}

void windowExClearClip() {
    auto &state = windowExPortableState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.clipActive = false;
    state.clipRect.clear();
}

void windowExSetClip(const tTVPRect &rect) {
    auto &state = windowExPortableState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.clipActive = !rect.is_empty();
    state.clipRect = rect;
}

bool windowExReadRect(iTJSDispatch2 *object, tTVPRect &rect) {
    if(!object)
        return false;
    auto read = [object](const tjs_char *name, tjs_int &value) {
        tTJSVariant variant;
        if(TJS_FAILED(object->PropGet(TJS_IGNOREPROP, name, nullptr, &variant,
                                      object)) ||
           (variant.Type() != tvtInteger && variant.Type() != tvtReal))
            return false;
        value = static_cast<tjs_int>(variant);
        return true;
    };
    tjs_int x = 0, y = 0, w = 0, h = 0;
    const bool hasX = read(TJS_W("x"), x);
    const bool hasY = read(TJS_W("y"), y);
    const bool hasW = read(TJS_W("w"), w);
    const bool hasH = read(TJS_W("h"), h);
    if(hasX || hasY || hasW || hasH) {
        if(!hasX || !hasY || !hasW || !hasH || w <= 0 || h <= 0)
            return false;
        rect = tTVPRect(x, y, x + w, y + h);
        return !rect.is_empty();
    }

    tjs_int left = 0, top = 0, right = 0, bottom = 0;
    if(!read(TJS_W("left"), left) || !read(TJS_W("top"), top) ||
       !read(TJS_W("right"), right) || !read(TJS_W("bottom"), bottom))
        return false;
    rect = tTVPRect(left, top, right, bottom);
    return !rect.is_empty();
}

int windowExClassValueIndex(const ttstr &name) {
    if(name == TJS_W("CURSOR")) return 0;
    if(name == TJS_W("ICON")) return 1;
    if(name == TJS_W("ICONSM")) return 2;
    if(name == TJS_W("BRBACKGROUND")) return 3;
    return -1;
}

tTVInteger windowExPortableCursorToken(const ttstr &input) {
    ttstr name = input;
    name.ToUpperCase();
    if(name == TJS_W("IDC_ARROW")) return -2;
    if(name == TJS_W("IDC_CROSS")) return -3;
    if(name == TJS_W("IDC_IBEAM")) return -4;
    if(name == TJS_W("IDC_SIZE")) return -5;
    if(name == TJS_W("IDC_SIZENESW")) return -6;
    if(name == TJS_W("IDC_SIZENS")) return -7;
    if(name == TJS_W("IDC_SIZENWSE")) return -8;
    if(name == TJS_W("IDC_SIZEWE")) return -9;
    if(name == TJS_W("IDC_UPARROW")) return -10;
    if(name == TJS_W("IDC_WAIT")) return -11;
    if(name == TJS_W("IDC_NO")) return -18;
    if(name == TJS_W("IDC_HELP")) return -20;
    if(name == TJS_W("IDC_HAND")) return -21;
    if(name == TJS_W("IDC_SIZEALL")) return -22;
    return 0;
}

bool windowExPortableWindowClass(const ttstr &name) {
    if(name.IsEmpty())
        return true;
    ttstr normalized = name;
    normalized.ToUpperCase();
    return normalized == TJS_W("WINDOW") ||
           normalized == TJS_W("TTVPWINDOWFORM") ||
           normalized == TJS_W("TTJSNI_WINDOW");
}

bool windowExPortableWindowCaption(tTJSNI_Window *window, ttstr &caption) {
    if(!window)
        return false;
    window->GetCaption(caption);
    return true;
}

} // namespace

struct System {
    static tjs_int getDoubleClickTime() {
#ifdef _WIN32
        return static_cast<tjs_int>(::GetDoubleClickTime());
#else
        return 500;
#endif
    }

    // System.setDpiAwareness(context).  Keep the upstream return contract
    // (the previous context) on Win32; other hosts report an unavailable
    // native context with zero and leave DPI policy to the host window.
    static tTVInteger setThreadDpiAwarenessContext(tTVInteger context) {
#ifdef _WIN32
        using SetDpiAwarenessContextProc = HANDLE(WINAPI *)(HANDLE);
        auto proc = reinterpret_cast<SetDpiAwarenessContextProc>(
            ::GetProcAddress(::GetModuleHandleW(L"user32.dll"),
                             "SetThreadDpiAwarenessContext"));
        if(!proc)
            return 0;
        return reinterpret_cast<tTVInteger>(proc(
            reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(context))));
#else
        (void)context;
        return 0;
#endif
    }

    static tjs_error getDisplayMonitors(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis) {
        // Return an array with one monitor entry (primary)
        if (result) {
            tjs_int w = tTVPScreen::GetDesktopWidth();
            tjs_int h = tTVPScreen::GetDesktopHeight();

            ncbDictionaryAccessor monDict;
            monDict.SetValue(TJS_W("x"), 0);
            monDict.SetValue(TJS_W("y"), 0);
            monDict.SetValue(TJS_W("w"), w);
            monDict.SetValue(TJS_W("h"), h);
            monDict.SetValue(TJS_W("primary"), 1);

            iTJSDispatch2 *arr = TJSCreateArrayObject();
            tTJSVariant monVar(monDict.GetDispatch(), monDict.GetDispatch());
            tTJSVariant idx(0);
            arr->PropSetByNum(TJS_MEMBERENSURE, 0, &monVar, arr);
            result->SetObject(arr, arr);
            arr->Release();
        }
        return TJS_S_OK;
    }

    static tjs_error getMonitorInfo(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param,
                                    iTJSDispatch2 *objthis) {
        // Return a dictionary with 'monitor' and 'work' sub-dictionaries
        // On non-Windows platforms, monitor == work (no taskbar deduction)
        if (result) {
            tjs_int w = tTVPScreen::GetDesktopWidth();
            tjs_int h = tTVPScreen::GetDesktopHeight();

            // Create 'monitor' rect dict
            ncbDictionaryAccessor monDict;
            monDict.SetValue(TJS_W("x"), 0);
            monDict.SetValue(TJS_W("y"), 0);
            monDict.SetValue(TJS_W("w"), w);
            monDict.SetValue(TJS_W("h"), h);

            // Create 'work' rect dict (same as monitor on macOS)
            ncbDictionaryAccessor workDict;
            workDict.SetValue(TJS_W("x"), 0);
            workDict.SetValue(TJS_W("y"), 0);
            workDict.SetValue(TJS_W("w"), w);
            workDict.SetValue(TJS_W("h"), h);

            // Create result dict
            ncbDictionaryAccessor resultDict;
            tTJSVariant monVar(monDict.GetDispatch(), monDict.GetDispatch());
            resultDict.SetValue(TJS_W("monitor"), monVar);
            tTJSVariant workVar(workDict.GetDispatch(), workDict.GetDispatch());
            resultDict.SetValue(TJS_W("work"), workVar);

            auto *dis = resultDict.GetDispatch();
            result->SetObject(dis, dis);
        }
        return TJS_S_OK;
    }

    static tjs_error getCursorPos(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                  iTJSDispatch2 *obj) {
        if(r)
            r->Clear();
        if(!TVPMainWindow)
            return TJS_S_OK;
        tjs_int x = 0;
        tjs_int y = 0;
        TVPMainWindow->GetCursorPos(x, y);
        windowExClampCursor(x, y);
        ncbDictionaryAccessor dict;
        dict.SetValue(TJS_W("x"), x);
        dict.SetValue(TJS_W("y"), y);
        if(r) {
            auto *dispatch = dict.GetDispatch();
            r->SetObject(dispatch, dispatch);
        }
        return TJS_S_OK;
    }

    static tjs_error setCursorPos(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                  iTJSDispatch2 *obj) {
        if(n < 2)
            return TJS_E_BADPARAMCOUNT;
        if(!p || !p[0] || !p[1])
            return TJS_E_INVALIDPARAM;
        tjs_int x = static_cast<tjs_int>(*p[0]);
        tjs_int y = static_cast<tjs_int>(*p[1]);
        windowExClampCursor(x, y);
        if(TVPMainWindow)
            TVPMainWindow->SetCursorPos(x, y);
        if(r)
            *r = TVPMainWindow != nullptr;
        return TJS_S_OK;
    }

    // System.setClipCursor(rect-or-window=void)
    //
    // Win32 clips the process cursor to a screen-space RECT.  Embedded hosts
    // do not expose that OS primitive, so the same logical boundary is
    // applied to the engine cursor cache and to subsequent set/get calls.
    static tjs_error setClipCursor(tTJSVariant *r, tjs_int n,
                                   tTJSVariant **p,
                                   iTJSDispatch2 *objthis) {
        if(n < 1 || !p || !p[0] || p[0]->Type() == tvtVoid) {
#ifdef _WIN32
            ::ClipCursor(nullptr);
#endif
            windowExClearClip();
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        tTVPRect rect;
        bool valid = false;
        if(p[0]->Type() == tvtObject) {
            iTJSDispatch2 *object = p[0]->AsObjectNoAddRef();
            if(object && object->IsInstanceOf(0, 0, 0, TJS_W("Window"),
                                               object)) {
                if(auto *form = WindowEx::GetWindowForm(object)) {
#ifdef _WIN32
                    // The Win32 contract is a screen-space client rectangle.
                    // Window-layer coordinates are not necessarily screen
                    // coordinates after a position or DPI change.
                    HWND hwnd = WindowEx::GetHWND(object);
                    RECT nativeRect{};
                    POINT origin{0, 0};
                    if(hwnd && ::GetClientRect(hwnd, &nativeRect) &&
                       ::ClientToScreen(hwnd, &origin)) {
                        nativeRect.left += origin.x;
                        nativeRect.top += origin.y;
                        nativeRect.right += origin.x;
                        nativeRect.bottom += origin.y;
                        rect = tTVPRect(nativeRect.left, nativeRect.top,
                                        nativeRect.right, nativeRect.bottom);
                        valid = !rect.is_empty();
                        if(valid)
                            ::ClipCursor(&nativeRect);
                    } else {
                        const tjs_int left = form->GetLeft();
                        const tjs_int top = form->GetTop();
                        rect = tTVPRect(left, top, left + form->GetWidth(),
                                        top + form->GetHeight());
                        valid = !rect.is_empty();
                    }
#else
                    const tjs_int left = form->GetLeft();
                    const tjs_int top = form->GetTop();
                    rect = tTVPRect(left, top, left + form->GetWidth(),
                                    top + form->GetHeight());
                    valid = !rect.is_empty();
#endif
                }
            } else {
                valid = windowExReadRect(object, rect);
#ifdef _WIN32
                if(valid) {
                    RECT nativeRect{rect.left, rect.top, rect.right,
                                    rect.bottom};
                    ::ClipCursor(&nativeRect);
                }
#endif
            }
        }
        if(!valid) {
            if(r)
                *r = false;
            return TJS_S_OK;
        }
        windowExSetClip(rect);
        if(r)
            *r = true;
        (void)objthis;
        return TJS_S_OK;
    }

    static HWND handleFromVariant(const tTJSVariant *value) {
        if(!value)
            return nullptr;
        if(value->Type() == tvtObject)
            return WindowEx::GetHWND(value->AsObjectNoAddRef());
        if(value->Type() != tvtInteger && value->Type() != tvtReal)
            return nullptr;
        return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(
            static_cast<tjs_int64>(*value)));
    }

    // System.findWindowEx(winname=void, clsname=void, parwin=void,
    // childafter=void).  Portable hosts expose logical Window instances, not
    // child HWND trees; a non-null parent therefore has no matching child,
    // while the top-level enumeration and child-after ordering are retained.
    static tjs_error findWindowEx(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                  iTJSDispatch2 *objthis) {
        if(r)
            r->Clear();
        if(n > 4)
            n = 4;

        ttstr windowName;
        ttstr className;
        if(n > 0 && p && p[0] && p[0]->Type() == tvtString)
            windowName = *p[0];
        if(n > 1 && p && p[1] && p[1]->Type() == tvtString)
            className = *p[1];
        const HWND parent = (n > 2 && p && p[2] &&
                             p[2]->Type() != tvtVoid)
            ? handleFromVariant(p[2]) : nullptr;
        const HWND childAfter = (n > 3 && p && p[3] &&
                                 p[3]->Type() != tvtVoid)
            ? handleFromVariant(p[3]) : nullptr;
        if(parent != nullptr)
            return TJS_S_OK; // no child-window hierarchy in the portable host
        if(!windowExPortableWindowClass(className))
            return TJS_S_OK;

        bool afterSeen = childAfter == nullptr;
        const tjs_int count = TVPGetWindowCount();
        for(tjs_int index = 0; index < count; ++index) {
            tTJSNI_Window *window = TVPGetWindowListAt(index);
            if(!window)
                continue;
            const HWND handle = reinterpret_cast<HWND>(window);
            if(!afterSeen) {
                if(handle == childAfter)
                    afterSeen = true;
                continue;
            }
            ttstr caption;
            windowExPortableWindowCaption(window, caption);
            if(!windowName.IsEmpty() && caption != windowName)
                continue;
            if(r)
                *r = static_cast<tTVInteger>(reinterpret_cast<tjs_intptr_t>(
                    window));
            (void)objthis;
            return TJS_S_OK;
        }
        return TJS_S_OK;
    }

    // System.loadCursor(idc_or_res, hmodule=void)
    static tjs_error loadCursor(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                iTJSDispatch2 *objthis) {
        if(n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;
#ifdef _WIN32
        HCURSOR handle = nullptr;
        HINSTANCE module = nullptr;
        if(n > 1 && p[1])
            module = reinterpret_cast<HINSTANCE>(static_cast<std::uintptr_t>(
                static_cast<tjs_int64>(*p[1])));
        if(p[0]->Type() == tvtString) {
            handle = ::LoadCursorW(module, p[0]->GetString());
        } else {
            const tTVInteger value = static_cast<tTVInteger>(*p[0]);
            LPCWSTR resource = nullptr;
            if(value >= 0)
                resource = MAKEINTRESOURCEW(static_cast<WORD>(value));
            else {
                switch(static_cast<int>(value)) {
                case -2: resource = IDC_ARROW; break;
                case -3: resource = IDC_CROSS; break;
                case -4: resource = IDC_IBEAM; break;
                case -5: resource = IDC_SIZE; break;
                case -6: resource = IDC_SIZENESW; break;
                case -7: resource = IDC_SIZENS; break;
                case -8: resource = IDC_SIZENWSE; break;
                case -9: resource = IDC_SIZEWE; break;
                case -10: resource = IDC_UPARROW; break;
                case -11: resource = IDC_WAIT; break;
                case -18: resource = IDC_NO; break;
                case -20: resource = IDC_HELP; break;
                case -21: resource = IDC_HAND; break;
                case -22: resource = IDC_SIZEALL; break;
                default: break;
                }
            }
            if(resource)
                handle = ::LoadCursorW(module, resource);
        }
        if(r)
            *r = static_cast<tTVInteger>(reinterpret_cast<tjs_intptr_t>(handle));
#else
        tTVInteger token = 0;
        if(p[0]->Type() == tvtString)
            token = windowExPortableCursorToken(*p[0]);
        else if(p[0]->Type() == tvtInteger || p[0]->Type() == tvtReal) {
            const tTVInteger value = static_cast<tTVInteger>(*p[0]);
            // Standard negative VCL cursor IDs are already stable logical
            // handles.  Unknown negative IDs have no portable equivalent.
            switch(static_cast<int>(value)) {
            case -2: case -3: case -4: case -5: case -6: case -7:
            case -8: case -9: case -10: case -11: case -18: case -20:
            case -21: case -22:
                token = value;
                break;
            default:
                token = value >= 0 ? value : 0;
                break;
            }
        }
        if(r)
            *r = token;
#endif
        (void)objthis;
        return TJS_S_OK;
    }

    // System.classLongPtr(win, key, setvalue_or_getasvoid=void)
    static tjs_error classLongPtr(tTJSVariant *r, tjs_int n,
                                  tTJSVariant **p,
                                  iTJSDispatch2 *objthis) {
        if(n < 2 || !p || !p[0] || !p[1])
            return TJS_E_BADPARAMCOUNT;
        ttstr key(*p[1]);
        key.ToUpperCase();
        const int keyIndex = windowExClassValueIndex(key);
        if(keyIndex < 0) {
            if(r)
                r->Clear();
            return TJS_S_OK;
        }
        const HWND handle = handleFromVariant(p[0]);
#ifdef _WIN32
        const int nativeIndex = keyIndex == 0 ? GCLP_HCURSOR
            : keyIndex == 1 ? GCLP_HICON
            : keyIndex == 2 ? GCLP_HICONSM : GCLP_HBRBACKGROUND;
        LONG_PTR value = 0;
        if(n > 2 && p[2] && p[2]->Type() != tvtVoid) {
            tTVInteger setValue = static_cast<tTVInteger>(*p[2]);
            if(keyIndex == 3 && p[2]->Type() == tvtString) {
                ttstr brush(*p[2]);
                brush.ToUpperCase();
                if(brush == TJS_W("BLACK")) setValue = BLACK_BRUSH;
                else if(brush == TJS_W("WHITE")) setValue = WHITE_BRUSH;
                else if(brush == TJS_W("GRAY")) setValue = GRAY_BRUSH;
                else if(brush == TJS_W("LTGRAY")) setValue = LTGRAY_BRUSH;
                else if(brush == TJS_W("DKGRAY")) setValue = DKGRAY_BRUSH;
            }
            value = ::SetClassLongPtrW(handle, nativeIndex,
                                       static_cast<LONG_PTR>(setValue));
        } else {
            value = ::GetClassLongPtrW(handle, nativeIndex);
        }
        if(r)
            *r = static_cast<tTVInteger>(value);
#else
        const std::uintptr_t handleKey = windowExHandleKey(handle);
        auto &state = windowExPortableState();
        std::lock_guard<std::mutex> lock(state.mutex);
        auto &values = state.classValues[handleKey];
        tTVInteger oldValue = values[static_cast<std::size_t>(keyIndex)];
        if(n > 2 && p[2] && p[2]->Type() != tvtVoid) {
            tTVInteger setValue = static_cast<tTVInteger>(*p[2]);
            if(keyIndex == 3 && p[2]->Type() == tvtString) {
                ttstr brush(*p[2]);
                brush.ToUpperCase();
                if(brush == TJS_W("BLACK")) setValue = 4;
                else if(brush == TJS_W("WHITE")) setValue = 0;
                else if(brush == TJS_W("GRAY")) setValue = 2;
                else if(brush == TJS_W("LTGRAY")) setValue = 1;
                else if(brush == TJS_W("DKGRAY")) setValue = 3;
            }
            values[static_cast<std::size_t>(keyIndex)] = setValue;
        }
        if(r)
            *r = oldValue;
#endif
        (void)objthis;
        return TJS_S_OK;
    }

    // System.mapVirtualKey(code, maptype).  Preserve the useful keyboard
    // subset on hosts without Win32's user32.dll; unknown mappings return 0
    // exactly as MapVirtualKey does.
    static tjs_uint mapVirtualKey(tjs_int code, tjs_int maptype) {
#ifdef _WIN32
        return static_cast<tjs_uint>(::MapVirtualKeyA(static_cast<UINT>(code),
                                                       static_cast<UINT>(maptype)));
#else
        struct KeyPair { tjs_uint vk; tjs_uint scan; };
        static constexpr KeyPair keys[] = {
            {0x25, 0x4b}, {0x26, 0x48}, {0x27, 0x4d}, {0x28, 0x50},
            {0x2d, 0x52}, {0x2e, 0x53}, {0x08, 0x0e}, {0x09, 0x0f},
            {0x0d, 0x1c}, {0x1b, 0x01}, {0x20, 0x39}, {0x10, 0x2a},
            {0x11, 0x1d}, {0x12, 0x38},
        };
        if(maptype == 2) {
            if((code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z') ||
               (code >= '0' && code <= '9'))
                return static_cast<tjs_uint>(code);
            for(const auto &key : keys)
                if(key.vk == static_cast<tjs_uint>(code))
                    return key.vk;
            return 0;
        }
        if(maptype == 0 || maptype == 4) {
            for(const auto &key : keys)
                if(key.vk == static_cast<tjs_uint>(code))
                    return key.scan;
            if((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9'))
                return static_cast<tjs_uint>(code);
            return 0;
        }
        if(maptype == 1 || maptype == 3) {
            for(const auto &key : keys)
                if(key.scan == static_cast<tjs_uint>(code))
                    return key.vk;
            return 0;
        }
        return 0;
#endif
    }

    static tjs_error getSystemMetrics(tTJSVariant *r, tjs_int n,
                                      tTJSVariant **p, iTJSDispatch2 *objthis) {
        if(n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;

        if(p[0]->Type() != tvtString)
            return TJS_E_INVALIDPARAM;
        ttstr key(p[0]->AsStringNoAddRef());
        if(key == TJS_W(""))
            return TJS_E_INVALIDPARAM;
        key.ToUpperCase();

        tjs_int portableValue = 0;
        if(windowExPortableMetric(key, portableValue)) {
            if(r)
                *r = portableValue;
            return TJS_S_OK;
        }

        tTJSVariant tmp;
        iTJSDispatch2 *obj = TVPGetScriptDispatch();
        bool hasval = TJS_SUCCEEDED(
            obj->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("System"), 0, &tmp, obj));
        obj->Release();
        if(!hasval)
            return TJS_E_FAIL;

        obj = tmp.AsObjectNoAddRef();
        tmp.Clear();
        if(TJS_FAILED(obj->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("metrics"), 0,
                                   &tmp, obj))) {
            ncbDictionaryAccessor dict;
            tmp = dict;
            if(TJS_FAILED(obj->PropSet(TJS_MEMBERENSURE, TJS_W("metrics"), 0,
                                       &tmp, obj)))
                return TJS_E_FAIL;
        }
        ncbPropAccessor metrics(tmp);
        tjs_int num = metrics.getIntValue(key.c_str(), -1);
        if(num < 0)
            return TJS_E_INVALIDPARAM;
 #ifdef _WIN32
        num = static_cast<tjs_int>(::GetSystemMetrics(num));
 #else
        // Unknown Win32 metric IDs have no portable equivalent.  Return a
        // deterministic zero instead of leaving the result variant unset.
        num = 0;
 #endif
        if(r)
            *r = num;
        return TJS_S_OK;
    }

    static tjs_error readEnvValue(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                  iTJSDispatch2 *objthis) {
        if(n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;
        if(p[0]->Type() != tvtString)
            return TJS_E_INVALIDPARAM;
        ttstr name(p[0]->AsStringNoAddRef());
        if(name == TJS_W(""))
            return TJS_E_INVALIDPARAM;
        if(r) {
            r->Clear();
            if(const char *value = windowExGetEnv(windowExToUtf8(name)))
                *r = windowExFromUtf8(value);
        }
        return TJS_S_OK;
    }

    static tjs_error expandEnvString(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                     iTJSDispatch2 *objthis) {
        if(n < 1 || !p || !p[0])
            return TJS_E_BADPARAMCOUNT;
        if(p[0]->Type() != tvtString)
            return TJS_E_INVALIDPARAM;
        if(r)
            *r = windowExFromUtf8(
                windowExExpandEnv(windowExToUtf8(ttstr(*p[0]))));
        return TJS_S_OK;
    }

    static tjs_error setApplicationIcon(tTJSVariant *r, tjs_int n,
                                        tTJSVariant **p, iTJSDispatch2 *obj) {
        if(r)
            *r = false;
        TVPAddLog(TJS_W("AetherKiri windowEx: application icons are owned by the host window"));
        return TJS_S_OK;
    }

    static bool setIconicPreview(bool en) {
        (void)en;
        TVPAddLog(TJS_W("AetherKiri windowEx: iconic preview is unavailable on this host"));
        return false;
    }
};

// Systemに関数を追加
NCB_ATTACH_FUNCTION(getDisplayMonitors, System, System::getDisplayMonitors);
NCB_ATTACH_FUNCTION(getMonitorInfo, System, System::getMonitorInfo);
NCB_ATTACH_FUNCTION(getCursorPos, System, System::getCursorPos);
NCB_ATTACH_FUNCTION(setCursorPos, System, System::setCursorPos);
NCB_ATTACH_FUNCTION(setClipCursor, System, System::setClipCursor);
NCB_ATTACH_FUNCTION(getSystemMetrics, System, System::getSystemMetrics);
NCB_ATTACH_FUNCTION(readEnvValue, System, System::readEnvValue);
NCB_ATTACH_FUNCTION(expandEnvString, System, System::expandEnvString);
NCB_ATTACH_FUNCTION(setApplicationIcon, System, System::setApplicationIcon);
NCB_ATTACH_FUNCTION(setIconicPreview, System, System::setIconicPreview);
NCB_ATTACH_FUNCTION(getDoubleClickTime, System, System::getDoubleClickTime);
NCB_ATTACH_FUNCTION(setDpiAwareness, System,
                    System::setThreadDpiAwarenessContext);
NCB_ATTACH_FUNCTION(findWindowEx, System, System::findWindowEx);
NCB_ATTACH_FUNCTION(classLongPtr, System, System::classLongPtr);
NCB_ATTACH_FUNCTION(loadCursor, System, System::loadCursor);
NCB_ATTACH_FUNCTION(mapVirtualKey, System, System::mapVirtualKey);
NCB_ATTACH_FUNCTION(breathe, System, TVPBreathe);
NCB_ATTACH_FUNCTION(isBreathing, System, TVPGetBreathing);
NCB_ATTACH_FUNCTION(clearGraphicCache, System, TVPClearGraphicCache);
NCB_ATTACH_FUNCTION(getAboutString, System, TVPGetAboutString);
NCB_ATTACH_FUNCTION(getCPUType, System, TVPGetCPUType);

////////////////////////////////////////////////////////////////

struct Scripts {
    static bool outputErrorLogOnEval;

    // property Scripts.outputErrorLogOnEval
    static bool setEvalErrorLog(bool v) {
        bool ret = outputErrorLogOnEval;
        /**/ outputErrorLogOnEval = v;
        return ret;
    }

    // Scripts.eval オーバーライド
    static tjs_error eval(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                          iTJSDispatch2 *objthis) {
        if(outputErrorLogOnEval)
            return evalOrig->FuncCall(0, nullptr, nullptr, r, n, p, objthis);

        if(n < 1)
            return TJS_E_BADPARAMCOUNT;
        ttstr content = *p[0], name;
        tjs_int lineofs = 0;
        if(n >= 2)
            name = *p[1];
        if(n >= 3)
            lineofs = *p[2];

        TVPExecuteExpression(content, name, lineofs, r);
        return TJS_S_OK;
    }

    // 元の Scripts.eval を保存・復帰
    static void Regist() {
        tTJSVariant var;
        TVPExecuteExpression(TJS_W("Scripts.eval"), &var);
        evalOrig = var.AsObject();
    }

    static void UnRegist() {
        if(evalOrig)
            evalOrig->Release();
        evalOrig = nullptr;
    }

    static iTJSDispatch2 *evalOrig;
};

iTJSDispatch2 *Scripts::evalOrig = nullptr; // Scripts.evalの元のオブジェクト
bool Scripts::outputErrorLogOnEval = true; // 切り替えフラグ

// Scriptsに関数を追加
NCB_ATTACH_FUNCTION(eval, Scripts, Scripts::eval);
NCB_ATTACH_FUNCTION(setEvalErrorLog, Scripts, Scripts::setEvalErrorLog);

////////////////////////////////////////////////////////////////
// コールバック指定

static void PreRegistCallback() { Scripts::Regist(); }

static void PostUnregistCallback() { Scripts::UnRegist(); }

NCB_PRE_REGIST_CALLBACK(PreRegistCallback);
NCB_POST_UNREGIST_CALLBACK(PostUnregistCallback);
