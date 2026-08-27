//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TJS2 Script Managing
//---------------------------------------------------------------------------

#include "tjsCommHead.h"

#include "tjs.h"
#include "tjsDebug.h"
#include "tjsArray.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "DebugIntf.h"
#include "WindowIntf.h"
#include "LayerIntf.h"
#include "CDDAIntf.h"
#include "MIDIIntf.h"
#include "WaveIntf.h"
#include "TimerIntf.h"
#include "EventIntf.h"
#include "SystemIntf.h"
#include "PluginImpl.h"
#include "MenuItemIntf.h"
#include "ClipboardIntf.h"
#include "MsgIntf.h"
#include "KAGParser.h"
#include "VideoOvlIntf.h"
#include "PadIntf.h"
#include "TextStream.h"
#include "Random.h"
#include "tjsRandomGenerator.h"
#include "SysInitIntf.h"
#include "PhaseVocoderFilter.h"
#include "BasicDrawDevice.h"
#if defined(__ANDROID__)
#include <android/log.h>
#endif
#include "BinaryStream.h"
#include "SysInitImpl.h"
#include "Application.h"

#include "RectItf.h"
#include "ImageFunction.h"
#include "BitmapIntf.h"
#include "tjsScriptBlock.h"
#include "tjsScriptRecovery.h"
#include "ApplicationSpecialPath.h"
#include "SystemImpl.h"
#include "BitmapLayerTreeOwner.h"
#include "Extension.h"
#include "Platform.h"
#include "Exception.h"
#include "ConfigManager/LocaleConfigManager.h"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <atomic>
#include <cstdlib>
#include <utility>
#include <vector>

namespace {

// Monotonically records actual entries into the engine's storage executor.
// Script-side loader wrappers can use this to detect that they returned
// successfully without delegating to the native loader.
std::atomic<tjs_uint64> TVPStorageExecutionSerial{0};

tjs_uint64 TVPGetStorageExecutionSerial() {
    return TVPStorageExecutionSerial.load(std::memory_order_relaxed);
}

} // namespace

//---------------------------------------------------------------------------
// Script system initialization script
//---------------------------------------------------------------------------
static const tjs_nchar *TVPInitTJSScript =
    // note that this script is stored as narrow string
    TJS_N(R"(const
/* constants */
 /* tTVPBorderStyle */ bsNone=0,  bsSingle=1,  bsSizeable=2,  bsDialog=3,  bsToolWindow=4,  bsSizeToolWin=5,
 /* tTVPUpdateType */ utNormal=0,  utEntire =1,
 /* tTVPMouseButton */  mbLeft=0,  mbRight=1,  mbMiddle=2, mbX1=3, mbX2=4,
 /* tTVPMouseCursorState */ mcsVisible=0, mcsTempHidden=1, mcsHidden=2,
 /* tTVPImeMode */ imDisable=0, imClose=1, imOpen=2, imDontCare=3, imSAlpha=4, imAlpha=5, imHira=6, imSKata=7, imKata=8, imChinese=9, imSHanguel=10, imHanguel=11,
 /* Set of shift state */  ssShift=(1<<0),  ssAlt=(1<<1),  ssCtrl=(1<<2),  ssLeft=(1<<3),  ssRight=(1<<4),  ssMiddle=(1<<5),  ssDouble =(1<<6),  ssRepeat = (1<<7),
 /* TVP_FSF_???? */ fsfFixedPitch=1, fsfSameCharSet=2, fsfNoVertical=4, 
	fsfTrueTypeOnly=8, fsfUseFontFace=0x100, fsfIgnoreSymbol=0x10,
 /* tTVPLayerType */ ltBinder=0, ltCoverRect=1, ltOpaque=1, ltTransparent=2, ltAlpha=2, ltAdditive=3, ltSubtractive=4, ltMultiplicative=5, ltEffect=6, ltFilter=7, ltDodge=8, ltDarken=9, ltLighten=10, ltScreen=11, ltAddAlpha = 12,
	ltPsNormal = 13, ltPsAdditive = 14, ltPsSubtractive = 15, ltPsMultiplicative = 16, ltPsScreen = 17, ltPsOverlay = 18, ltPsHardLight = 19, ltPsSoftLight = 20, ltPsColorDodge = 21, ltPsColorDodge5 = 22, ltPsColorBurn = 23, ltPsLighten = 24, ltPsDarken = 25, ltPsDifference = 26, ltPsDifference5 = 27, ltPsExclusion = 28, 
 /* tTVPBlendOperationMode */ omPsNormal = ltPsNormal,omPsAdditive = ltPsAdditive,omPsSubtractive = ltPsSubtractive,omPsMultiplicative = ltPsMultiplicative,omPsScreen = ltPsScreen,omPsOverlay = ltPsOverlay,omPsHardLight = ltPsHardLight,omPsSoftLight = ltPsSoftLight,omPsColorDodge = ltPsColorDodge,omPsColorDodge5 = ltPsColorDodge5,omPsColorBurn = ltPsColorBurn,omPsLighten = ltPsLighten,omPsDarken = ltPsDarken,omPsDifference = ltPsDifference,omPsDifference5 = ltPsDifference5,omPsExclusion = ltPsExclusion, 
	omAdditive=ltAdditive, omSubtractive=ltSubtractive, omMultiplicative=ltMultiplicative, omDodge=ltDodge, omDarken=ltDarken, omLighten=ltLighten, omScreen=ltScreen, omAddAlpha=ltAddAlpha, omOpaque=ltOpaque, omAlpha=ltAlpha, omAuto = 128,
 /* tTVPDrawFace */ dfBoth=0, dfAlpha = dfBoth, dfAddAlpha = 4, dfMain=1, dfOpaque = dfMain, dfMask=2, dfProvince=3, dfAuto=128,
 /* tTVPHitType */ htMask=0, htProvince=1,
 /* tTVPScrollTransFrom */ sttLeft=0, sttTop=1, sttRight=2, sttBottom=3,
 /* tTVPScrollTransStay */ ststNoStay=0, ststStayDest=1, ststStaySrc=2, 
 /* tTVPKAGDebugLevel */ tkdlNone=0, tkdlSimple=1, tkdlVerbose=2, 
 /* tTVPAsyncTriggerMode */	atmNormal=0, atmExclusive=1, atmAtIdle=2, 
 /* tTVPBBStretchType */ stNearest=0, stFastLinear=1, stLinear=2, stCubic=3, stSemiFastLinear = 4, stFastCubic = 5, stLanczos2 = 6, stFastLanczos2 = 7, stLanczos3 = 8, stFastLanczos3 = 9, stSpline16 = 10, stFastSpline16 = 11, stSpline36 = 12, stFastSpline36 = 13, stAreaAvg = 14, stFastAreaAvg = 15, stGaussian = 16, stFastGaussian = 17, stBlackmanSinc = 18, stFastBlackmanSinc = 19, stRefNoClip = 0x10000,
 /* tTVPClipboardFormat */ cbfText = 1,
 /* TVP_COMPACT_LEVEL_???? */ clIdle = 5, clDeactivate = 10, clMinimize = 15, clAll = 100,
 /* tTVPVideoOverlayMode Add: T.Imoto */ vomOverlay=0, vomLayer=1, vomMixer=2, vomMFEVR=3,
 /* tTVPPeriodEventReason */ perLoop = 0, perPeriod = 1, perPrepare = 2, perSegLoop = 3,
 /* tTVPSoundGlobalFocusMode */ sgfmNeverMute = 0, sgfmMuteOnMinimize = 1, sgfmMuteOnDeactivate = 2,
 /* tTVPTouchDevice */ tdNone=0, tdIntegratedTouch=0x01, tdExternalTouch=0x02, tdIntegratedPen=0x04, tdExternalPen=0x08, tdMultiInput=0x40, tdDigitizerReady=0x80,
    tdMouse=0x0100, tdMouseWheel=0x0200,
 /* Display Orientation */ oriUnknown=0, oriPortrait=1, oriLandscape=2,

/* file attributes */
 faReadOnly=0x01, faHidden=0x02, faSysFile=0x04, faVolumeID=0x08, faDirectory=0x10, faArchive=0x20, faAnyFile=0x3f,
/* mouse cursor constants */
 crDefault = 0x0,
 crNone = -1,
 crArrow = -2,
 crCross = -3,
 crIBeam = -4,
 crSize = -5,
 crSizeNESW = -6,
 crSizeNS = -7,
 crSizeNWSE = -8,
 crSizeWE = -9,
 crUpArrow = -10,
 crHourGlass = -11,
 crDrag = -12,
 crNoDrop = -13,
 crHSplit = -14,
 crVSplit = -15,
 crMultiDrag = -16,
 crSQLWait = -17,
 crNo = -18,
 crAppStart = -19,
 crHelp = -20,
 crHandPoint = -21,
 crSizeAll = -22,
 crHBeam = 1,
/* color constants */
 clScrollBar = 0x80000000,
 clBackground = 0x80000001,
 clActiveCaption = 0x80000002,
 clInactiveCaption = 0x80000003,
 clMenu = 0x80000004,
 clWindow = 0x80000005,
 clWindowFrame = 0x80000006,
 clMenuText = 0x80000007,
 clWindowText = 0x80000008,
 clCaptionText = 0x80000009,
 clActiveBorder = 0x8000000a,
 clInactiveBorder = 0x8000000b,
 clAppWorkSpace = 0x8000000c,
 clHighlight = 0x3399ff,
 clHighlightText = 0x8000000e,
 clBtnFace = 0xf0f0f0,
 clBtnShadow = 0x787878,
 clGrayText = 0x80000011,
 clBtnText = 0x000000,
 clInactiveCaptionText = 0x80000013,
 clBtnHighlight = 0x80000014,
 cl3DDkShadow = 0x80000015,
 cl3DLight = 0x80000016,
 clInfoText = 0x80000017,
 clInfoBk = 0x80000018,
 clNone = 0x1fffffff,
 clAdapt= 0x01ffffff,
 clPalIdx = 0x3000000,
 clAlphaMat = 0x4000000,
/* for Menu.trackPopup (see winuser.h) */
 tpmLeftButton      = 0x0000,
 tpmRightButton     = 0x0002,
 tpmLeftAlign       = 0x0000,
 tpmCenterAlign     = 0x0004,
 tpmRightAlign      = 0x0008,
 tpmTopAlign        = 0x0000,
 tpmVCenterAlign    = 0x0010,
 tpmBottomAlign     = 0x0020,
 tpmHorizontal      = 0x0000,
 tpmVertical        = 0x0040,
 tpmNoNotify        = 0x0080,
 tpmReturnCmd       = 0x0100,
 tpmRecurse         = 0x0001,
 tpmHorPosAnimation = 0x0400,
 tpmHorNegAnimation = 0x0800,
 tpmVerPosAnimation = 0x1000,
 tpmVerNegAnimation = 0x2000,
 tpmNoAnimation     = 0x4000,
/* for Pad.showScrollBars (see Vcl/stdctrls.hpp :: enum TScrollStyle) */
 ssNone       = 0,
 ssHorizontal = 1,
 ssVertical   = 2,
 ssBoth       = 3,
/* virtual keycodes */
 VK_LBUTTON =0x01,
 VK_RBUTTON =0x02,
 VK_CANCEL =0x03,
 VK_MBUTTON =0x04,
 VK_BACK =0x08,
 VK_TAB =0x09,
 VK_CLEAR =0x0C,
 VK_RETURN =0x0D,
 VK_SHIFT =0x10,
 VK_CONTROL =0x11,
 VK_MENU =0x12,
 VK_PAUSE =0x13,
 VK_CAPITAL =0x14,
 VK_KANA =0x15,
 VK_HANGEUL =0x15,
 VK_HANGUL =0x15,
 VK_JUNJA =0x17,
 VK_FINAL =0x18,
 VK_HANJA =0x19,
 VK_KANJI =0x19,
 VK_ESCAPE =0x1B,
 VK_CONVERT =0x1C,
 VK_NONCONVERT =0x1D,
 VK_ACCEPT =0x1E,
 VK_MODECHANGE =0x1F,
 VK_SPACE =0x20,
 VK_PRIOR =0x21,
 VK_NEXT =0x22,
 VK_END =0x23,
 VK_HOME =0x24,
 VK_LEFT =0x25,
 VK_UP =0x26,
 VK_RIGHT =0x27,
 VK_DOWN =0x28,
 VK_SELECT =0x29,
 VK_PRINT =0x2A,
 VK_EXECUTE =0x2B,
 VK_SNAPSHOT =0x2C,
 VK_INSERT =0x2D,
 VK_DELETE =0x2E,
 VK_HELP =0x2F,
 VK_0 =0x30,
 VK_1 =0x31,
 VK_2 =0x32,
 VK_3 =0x33,
 VK_4 =0x34,
 VK_5 =0x35,
 VK_6 =0x36,
 VK_7 =0x37,
 VK_8 =0x38,
 VK_9 =0x39,
 VK_A =0x41,
 VK_B =0x42,
 VK_C =0x43,
 VK_D =0x44,
 VK_E =0x45,
 VK_F =0x46,
 VK_G =0x47,
 VK_H =0x48,
 VK_I =0x49,
 VK_J =0x4A,
 VK_K =0x4B,
 VK_L =0x4C,
 VK_M =0x4D,
 VK_N =0x4E,
 VK_O =0x4F,
 VK_P =0x50,
 VK_Q =0x51,
 VK_R =0x52,
 VK_S =0x53,
 VK_T =0x54,
 VK_U =0x55,
 VK_V =0x56,
 VK_W =0x57,
 VK_X =0x58,
 VK_Y =0x59,
 VK_Z =0x5A,
 VK_LWIN =0x5B,
 VK_RWIN =0x5C,
 VK_APPS =0x5D,
 VK_NUMPAD0 =0x60,
 VK_NUMPAD1 =0x61,
 VK_NUMPAD2 =0x62,
 VK_NUMPAD3 =0x63,
 VK_NUMPAD4 =0x64,
 VK_NUMPAD5 =0x65,
 VK_NUMPAD6 =0x66,
 VK_NUMPAD7 =0x67,
 VK_NUMPAD8 =0x68,
 VK_NUMPAD9 =0x69,
 VK_MULTIPLY =0x6A,
 VK_ADD =0x6B,
 VK_SEPARATOR =0x6C,
 VK_SUBTRACT =0x6D,
 VK_DECIMAL =0x6E,
 VK_DIVIDE =0x6F,
 VK_F1 =0x70,
 VK_F2 =0x71,
 VK_F3 =0x72,
 VK_F4 =0x73,
 VK_F5 =0x74,
 VK_F6 =0x75,
 VK_F7 =0x76,
 VK_F8 =0x77,
 VK_F9 =0x78,
 VK_F10 =0x79,
 VK_F11 =0x7A,
 VK_F12 =0x7B,
 VK_F13 =0x7C,
 VK_F14 =0x7D,
 VK_F15 =0x7E,
 VK_F16 =0x7F,
 VK_F17 =0x80,
 VK_F18 =0x81,
 VK_F19 =0x82,
 VK_F20 =0x83,
 VK_F21 =0x84,
 VK_F22 =0x85,
 VK_F23 =0x86,
 VK_F24 =0x87,
 VK_NUMLOCK =0x90,
 VK_SCROLL =0x91,
 VK_LSHIFT =0xA0,
 VK_RSHIFT =0xA1,
 VK_LCONTROL =0xA2,
 VK_RCONTROL =0xA3,
 VK_LMENU =0xA4,
 VK_RMENU =0xA5,
/* VK_PADXXXX are KIRIKIRI specific */
 VK_PADLEFT =0x1B5,
 VK_PADUP =0x1B6,
 VK_PADRIGHT =0x1B7,
 VK_PADDOWN =0x1B8,
 VK_PAD1 =0x1C0,
 VK_PAD2 =0x1C1,
 VK_PAD3 =0x1C2,
 VK_PAD4 =0x1C3,
 VK_PAD5 =0x1C4,
 VK_PAD6 =0x1C5,
 VK_PAD7 =0x1C6,
 VK_PAD8 =0x1C7,
 VK_PAD9 =0x1C8,
 VK_PAD10 =0x1C9,
 VK_PADANY = 0x1DF,
 VK_PROCESSKEY =0xE5,
 VK_ATTN =0xF6,
 VK_CRSEL =0xF7,
 VK_EXSEL =0xF8,
 VK_EREOF =0xF9,
 VK_PLAY =0xFA,
 VK_ZOOM =0xFB,
 VK_NONAME =0xFC,
 VK_PA1 =0xFD,
 VK_OEM_CLEAR =0xFE,
 frFreeType=0,
 frGDI=1,
/* graphic cache system */
 gcsAuto=-1,
/* image 'mode' tag (mainly is generated by image format converter) constants */
 imageTagLayerType = %[
opaque		:%[type:ltOpaque			],
rect		:%[type:ltOpaque			],
alpha		:%[type:ltAlpha				],
transparent	:%[type:ltAlpha				],
addalpha	:%[type:ltAddAlpha			],
add			:%[type:ltAdditive			],
sub			:%[type:ltSubtractive		],
mul			:%[type:ltMultiplicative	],
dodge		:%[type:ltDodge				],
darken		:%[type:ltDarken			],
lighten		:%[type:ltLighten			],
screen		:%[type:ltScreen			],
psnormal	:%[type:ltPsNormal			],
psadd		:%[type:ltPsAdditive		],
pssub		:%[type:ltPsSubtractive		],
psmul		:%[type:ltPsMultiplicative	],
psscreen	:%[type:ltPsScreen			],
psoverlay	:%[type:ltPsOverlay			],
pshlight	:%[type:ltPsHardLight		],
psslight	:%[type:ltPsSoftLight		],
psdodge		:%[type:ltPsColorDodge		],
psdodge5	:%[type:ltPsColorDodge5		],
psburn		:%[type:ltPsColorBurn		],
pslighten	:%[type:ltPsLighten			],
psdarken	:%[type:ltPsDarken			],
psdiff		:%[type:ltPsDifference		],
psdiff5		:%[type:ltPsDifference5		],
psexcl		:%[type:ltPsExclusion		],
],
/* draw thread num */
 dtnAuto=0
;)");
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// global variables
//---------------------------------------------------------------------------
tTJS *TVPScriptEngine = nullptr;
ttstr TVPStartupScriptName(TJS_W("startup.tjs"));
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Garbage Collection stuff
//---------------------------------------------------------------------------
class tTVPTJSGCCallback : public tTVPCompactEventCallbackIntf {
    void OnCompact(tjs_int level) override {
        if(TVPScriptEngine) {
            if(level >= TVP_COMPACT_LEVEL_MINIMIZE) {
                tjs_int compactLevel = (level >= TVP_COMPACT_LEVEL_MAX) ? 3 : 2;
                TVPScriptEngine->CompactScriptCache(compactLevel);
                TVPScriptEngine->DoGarbageCollection(true);
            } else if(level >= TVP_COMPACT_LEVEL_IDLE) {
                TVPScriptEngine->CompactScriptCache(1);
                TVPScriptEngine->DoGarbageCollection();
            }
        }
    }
} static TVPTJSGCCallback;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPInitScriptEngine
//---------------------------------------------------------------------------
static bool TVPScriptEngineInit = false;
static bool TVPScriptEngineUninit = false;
static bool TVPScriptEngineCompactHookRegistered = false;

void TVPInitScriptEngine() {
    if(TVPScriptEngineInit && TVPScriptEngine)
        return;
    TVPScriptEngineInit = true;
    TVPScriptEngineUninit = false;

    tTJSVariant val;

    // Set eval expression mode
    if(TVPGetCommandLine(TJS_W("-evalcontext"), &val)) {
        ttstr str(val);
        if(str == TJS_W("global")) {
            TJSEvalOperatorIsOnGlobal = true;
            TJSWarnOnNonGlobalEvalOperator = true;
        }
    }

    // Set igonre-prop compat mode
    if(TVPGetCommandLine(TJS_W("-unaryaster"), &val)) {
        ttstr str(val);
        if(str == TJS_W("compat")) {
            TJSUnaryAsteriskIgnoresPropAccess = true;
        }
    }

    // Set debug mode
    if(TVPGetCommandLine(TJS_W("-debug"), &val)) {
        ttstr str(val);
        if(str == TJS_W("yes")) {
            TJSEnableDebugMode = true;
            TVPAddImportantLog((const tjs_char *)TVPWarnDebugOptionEnabled);
            //			if(TVPGetCommandLine(TJS_W("-warnrundelobj"),
            //&val) )
            //			{
            //				str = val;
            //				if(str == TJS_W("yes"))
            //				{
            TJSWarnOnExecutionOnDeletingObject = true;
            //				}
            //			}
        }
    }

#ifdef TVP_START_UP_SCRIPT_NAME
    TVPStartupScriptName = TVP_START_UP_SCRIPT_NAME;
#else
    // Set startup script name
    if(TVPGetCommandLine(TJS_W("-startup"), &val)) {
        ttstr str(val);
        TVPStartupScriptName = str;
    }
#endif

    // create script engine object
    TVPScriptEngine = new tTJS();

    // add kirikiriz
    //	TVPScriptEngine->SetPPValue( TJS_W("kirikiriz"), 1 );

    // set TJSGetRandomBits128
    TJSGetRandomBits128 = TVPGetRandomBits128;

    // script system initialization
    TVPScriptEngine->ExecScript(ttstr(TVPInitTJSScript));

    // set console output gateway handler
    TVPScriptEngine->SetConsoleOutput(TVPGetTJS2ConsoleOutputGateway());

    // set text stream functions
    TJSCreateTextStreamForRead = TVPCreateTextStreamForRead;
    TJSCreateTextStreamForWrite = TVPCreateTextStreamForWrite;

    // set binary stream functions
    TJSCreateBinaryStreamForRead = TVPCreateBinaryStreamForRead;
    TJSCreateBinaryStreamForWrite = TVPCreateBinaryStreamForWrite;

    // register some TVP classes/objects/functions/propeties
    iTJSDispatch2 *dsp;
    iTJSDispatch2 *global = TVPScriptEngine->GetGlobalNoAddRef();

    auto registerObject = [&](const tjs_char *classname, auto instance) {
        auto dsp = instance;
        tTJSVariant val(dsp /*, dsp */);
        dsp->Release();
        global->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, classname, nullptr,
                        &val, global);
    };

    /* classes */
    registerObject(TJS_W("Debug"), TVPCreateNativeClass_Debug());
    registerObject(TJS_W("Font"), TVPCreateNativeClass_Font());
    registerObject(TJS_W("Layer"), TVPCreateNativeClass_Layer());
    registerObject(TJS_W("CDDASoundBuffer"),
                   TVPCreateNativeClass_CDDASoundBuffer());
    registerObject(TJS_W("MIDISoundBuffer"),
                   TVPCreateNativeClass_MIDISoundBuffer());
    registerObject(TJS_W("Timer"), TVPCreateNativeClass_Timer());
    registerObject(TJS_W("AsyncTrigger"), TVPCreateNativeClass_AsyncTrigger());
    registerObject(TJS_W("System"), TVPCreateNativeClass_System());
    registerObject(TJS_W("Storages"), TVPCreateNativeClass_Storages());
    registerObject(TJS_W("Plugins"), TVPCreateNativeClass_Plugins());
    registerObject(TJS_W("VideoOverlay"), TVPCreateNativeClass_VideoOverlay());
    registerObject(TJS_W("Pad"), TVPCreateNativeClass_Pad());
    registerObject(TJS_W("Clipboard"), TVPCreateNativeClass_Clipboard());
    registerObject(TJS_W("Scripts"),
                   TVPCreateNativeClass_Scripts()); // declared in this file
    registerObject(TJS_W("Rect"), TVPCreateNativeClass_Rect());
    registerObject(TJS_W("Bitmap"), TVPCreateNativeClass_Bitmap());
    registerObject(TJS_W("ImageFunction"),
                   TVPCreateNativeClass_ImageFunction());
    registerObject(TJS_W("BitmapLayerTreeOwner"),
                   TVPCreateNativeClass_BitmapLayerTreeOwner());

    /* KAG special support */
    registerObject(TJS_W("KAGParser"), TVPCreateNativeClass_KAGParser());

    /* WaveSoundBuffer and its filters */
    iTJSDispatch2 *waveclass = nullptr;
    registerObject(TJS_W("WaveSoundBuffer"),
                   (waveclass = TVPCreateNativeClass_WaveSoundBuffer()));
    dsp = new tTJSNC_PhaseVocoder();
    val = tTJSVariant(dsp);
    dsp->Release();
    waveclass->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_STATICMEMBER,
                       TJS_W("PhaseVocoder"), nullptr, &val, waveclass);

    /* Window and its drawdevices */
    iTJSDispatch2 *windowclass = nullptr;
    registerObject(TJS_W("Window"),
                   (windowclass = TVPCreateNativeClass_Window()));
    dsp = new tTJSNC_BasicDrawDevice();
    val = tTJSVariant(dsp);
    dsp->Release();
    windowclass->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_STATICMEMBER,
                         TJS_W("BasicDrawDevice"), nullptr, &val, windowclass);

    windowclass->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_STATICMEMBER,
                         TJS_W("PassThroughDrawDevice"), nullptr, &val,
                         windowclass); // compatible for old version kr2

    CreateShortCutKeyCodeTable();

    auto *gWindowMenuProperty = new WindowMenuProperty();
    val = tTJSVariant(gWindowMenuProperty);
    gWindowMenuProperty->Release();
    windowclass->PropSet(TJS_MEMBERENSURE, TJS_W("menu"), nullptr, &val,
                         windowclass);
    registerObject(TJS_W("MenuItem"), TVPCreateNativeClass_MenuItem());

    // Add Extension Classes
    TVPCauseAtInstallExtensionClass(global);

    // Garbage Collection Hook
    if(!TVPScriptEngineCompactHookRegistered) {
        TVPAddCompactEventHook(&TVPTJSGCCallback);
        TVPScriptEngineCompactHookRegistered = true;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPUninitScriptEngine
//---------------------------------------------------------------------------
void TVPUninitScriptEngine() {
    if(TVPScriptEngineUninit && !TVPScriptEngine)
        return;
    TVPScriptEngineUninit = true;

    // Internal ncbind classes retain process-global metadata. Unregister them
    // while the old global TJS object is still alive, then release the script
    // engine. Otherwise the next embedded game session either skips plugins
    // as already loaded or fails their first registration.
    TVPUnloadInternalPlugins();

    // TVPScriptEngine->Shutdown();
    if(TVPScriptEngine)
        TVPScriptEngine->Release();
    /*
        Objects, theirs lives are contolled by reference counter, may
       not be all freed here in some occations.
    */
    TVPScriptEngine = nullptr;
    TVPScriptEngineInit = false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPRestartScriptEngine
//---------------------------------------------------------------------------
void TVPRestartScriptEngine() {
    TVPUninitScriptEngine();
    TVPInitScriptEngine();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetScriptEngine
//---------------------------------------------------------------------------
tTJS *TVPGetScriptEngine() { return TVPScriptEngine; }
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetScriptDispatch
//---------------------------------------------------------------------------
iTJSDispatch2 *TVPGetScriptDispatch() {
    if(TVPScriptEngine)
        return TVPScriptEngine->GetGlobal();
    return nullptr;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExecuteScript
//---------------------------------------------------------------------------
void TVPExecuteScript(const ttstr &content, tTJSVariant *result) {
    if(TVPScriptEngine)
        TVPScriptEngine->ExecScript(content, result);
    else
        TVPThrowInternalError;
}

//---------------------------------------------------------------------------
void TVPExecuteScript(const ttstr &content, const ttstr &name, tjs_int lineofs,
                      tTJSVariant *result) {
    if(TVPScriptEngine)
        TVPScriptEngine->ExecScript(content, result, nullptr, &name, lineofs);
    else
        TVPThrowInternalError;
}

//---------------------------------------------------------------------------
void TVPExecuteScript(const ttstr &content, iTJSDispatch2 *context,
                      tTJSVariant *result) {
    if(TVPScriptEngine)
        TVPScriptEngine->ExecScript(content, result, context);
    else
        TVPThrowInternalError;
}

//---------------------------------------------------------------------------
void TVPExecuteScript(const ttstr &content, const ttstr &name, tjs_int lineofs,
                      iTJSDispatch2 *context, tTJSVariant *result) {
    if(TVPScriptEngine)
        TVPScriptEngine->ExecScript(content, result, context, &name, lineofs);
    else
        TVPThrowInternalError;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExecuteExpression
//---------------------------------------------------------------------------
void TVPExecuteExpression(const ttstr &content, tTJSVariant *result) {
    TVPExecuteExpression(content, nullptr, result);
}

//---------------------------------------------------------------------------
void TVPExecuteExpression(const ttstr &content, const ttstr &name,
                          tjs_int lineofs, tTJSVariant *result) {
    TVPExecuteExpression(content, name, lineofs, nullptr, result);
}

//---------------------------------------------------------------------------
void TVPExecuteExpression(const ttstr &content, iTJSDispatch2 *context,
                          tTJSVariant *result) {
    if(TVPScriptEngine) {
        iTJSConsoleOutput *output = TVPScriptEngine->GetConsoleOutput();
        TVPScriptEngine->SetConsoleOutput(
            nullptr); // once set TJS console to nullptr
        try {
            TVPScriptEngine->EvalExpression(content, result, context);
        } catch(...) {
            TVPScriptEngine->SetConsoleOutput(output);
            throw;
        }
        TVPScriptEngine->SetConsoleOutput(output);
    } else {
        TVPThrowInternalError;
    }
}

//---------------------------------------------------------------------------
void TVPExecuteExpression(const ttstr &content, const ttstr &name,
                          tjs_int lineofs, iTJSDispatch2 *context,
                          tTJSVariant *result) {
    if(TVPScriptEngine) {
        iTJSConsoleOutput *output = TVPScriptEngine->GetConsoleOutput();
        TVPScriptEngine->SetConsoleOutput(
            nullptr); // once set TJS console to nullptr
        try {
            TVPScriptEngine->EvalExpression(content, result, context, &name,
                                            lineofs);
        } catch(...) {
            TVPScriptEngine->SetConsoleOutput(output);
            throw;
        }
        TVPScriptEngine->SetConsoleOutput(output);
    } else {
        TVPThrowInternalError;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExecuteBytecode
//---------------------------------------------------------------------------
void TVPExecuteBytecode(const tjs_uint8 *content, size_t len,
                        iTJSDispatch2 *context, tTJSVariant *result,
                        const tjs_char *name) {
    if(!TVPScriptEngine)
        TVPThrowInternalError;

    TVPScriptEngine->LoadByteCode(content, len, result, context, name);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void TVPExecuteStorage(const ttstr &name, tTJSVariant *result,
                       bool isexpression, const tjs_char *modestr) {
    TVPExecuteStorage(name, nullptr, result, isexpression, modestr);
}
#include <fstream>
#include <filesystem>
#include <tjsByteCodeLoader.h>

bool TVPPatchWorldRestoreFaceVisibility(ttstr &script) {
    using ScriptString = std::basic_string<tjs_char>;
    const auto missing = ScriptString::npos;
    ScriptString source(script.c_str(), script.GetLen());

    const ScriptString functionMarker(TJS_W("function _updateAll("));
    const auto functionPos = source.find(functionMarker);
    if(functionPos == missing)
        return false;

    auto functionEnd = source.find(TJS_W("\n\tfunction "),
                                   functionPos + functionMarker.size());
    if(functionEnd == missing)
        functionEnd = source.size();

    if(source.find(TJS_W("__akRestoreFaceVisible"), functionPos) < functionEnd)
        return false;

    const ScriptString dataMarker(
        TJS_W("\t\t\tvar data = allData.data;"));
    const ScriptString captureMarker(
        TJS_W("\t\t\t\t\t\tcreate.add(info);"));
    const ScriptString clearMarker(TJS_W("\t\t\tenvClear(leave);"));

    const auto findUniqueInFunction =
        [&](const ScriptString &marker) -> ScriptString::size_type {
        const auto first = source.find(marker, functionPos);
        if(first == missing || first >= functionEnd)
            return missing;
        const auto next = source.find(marker, first + marker.size());
        if(next != missing && next < functionEnd)
            return missing;
        return first;
    };

    const auto dataPos = findUniqueInFunction(dataMarker);
    const auto capturePos = findUniqueInFunction(captureMarker);
    const auto clearPos = findUniqueInFunction(clearMarker);
    if(dataPos == missing || capturePos == missing || clearPos == missing ||
       !(dataPos < capturePos && capturePos < clearPos))
        return false;

    const auto lineEndingAfter =
        [&](ScriptString::size_type position,
            const ScriptString &marker) -> ScriptString {
        const auto after = position + marker.size();
        if(after + 1 < source.size() && source[after] == TJS_W('\r') &&
           source[after + 1] == TJS_W('\n'))
            return ScriptString(TJS_W("\r\n"));
        if(after < source.size() && source[after] == TJS_W('\n'))
            return ScriptString(TJS_W("\n"));
        return {};
    };

    const ScriptString dataEol = lineEndingAfter(dataPos, dataMarker);
    const ScriptString captureEol =
        lineEndingAfter(capturePos, captureMarker);
    const ScriptString clearEol = lineEndingAfter(clearPos, clearMarker);
    if(dataEol.empty() || captureEol.empty() || clearEol.empty())
        return false;

    // Insert from the bottom up so the validated source offsets remain valid.
    source.insert(clearPos + clearMarker.size() + clearEol.size(),
                  ScriptString(
                      TJS_W("\t\t\tif (__akRestoreFaceVisible && typeof "
                            "this.removeIgnore != \"undefined\") { try { "
                            "this.removeIgnore(\"face\"); } catch("
                            "__akFaceIgnoreE) {} }")) +
                      clearEol);
    source.insert(capturePos + captureMarker.size() + captureEol.size(),
                  ScriptString(
                      TJS_W("\t\t\t\t\t\ttry { if (info[0] == \"face\" && "
                            "info[2] !== void && (info[2].showmode & 1)) "
                            "__akRestoreFaceVisible = true; } catch("
                            "__akFaceRestoreE) {}")) +
                      captureEol);
    source.insert(dataPos + dataMarker.size() + dataEol.size(),
                  ScriptString(
                      TJS_W("\t\t\tvar __akRestoreFaceVisible = false;")) +
                      dataEol);

    script = ttstr(source);
    return true;
}

tjs_int TVPRepairShiftedNumberedMovieMappings(
    ttstr &script, tTVPStorageExistenceProbe storageExists) {
    using ScriptString = std::basic_string<tjs_char>;
    const auto missing = ScriptString::npos;
    if(!storageExists)
        return 0;

    ScriptString source(script.c_str(), script.GetLen());
    const ScriptString keyPrefix(TJS_W("\"ev_mv"));
    const ScriptString namePrefix(TJS_W("ev_mv"));
    const ScriptString finalSuffix(TJS_W("_02_06"));
    const ScriptString movieSuffix(TJS_W(".mpg"));
    const ScriptString storageMarker(TJS_W("\"storage\",\""));

    const auto parseFamily = [&](const ScriptString &name,
                                 int &family,
                                 size_t &digits) -> bool {
        if(name.size() <= namePrefix.size() + finalSuffix.size() ||
           name.compare(0, namePrefix.size(), namePrefix) != 0 ||
           name.compare(name.size() - finalSuffix.size(), finalSuffix.size(),
                        finalSuffix) != 0)
            return false;

        const size_t begin = namePrefix.size();
        const size_t end = name.size() - finalSuffix.size();
        family = 0;
        digits = end - begin;
        for(size_t i = begin; i < end; ++i) {
            if(name[i] < TJS_W('0') || name[i] > TJS_W('9'))
                return false;
            family = family * 10 + static_cast<int>(name[i] - TJS_W('0'));
        }
        return digits > 0;
    };

    const auto hasIdentityMapping = [&](const ScriptString &name) -> bool {
        const ScriptString key = ScriptString(TJS_W("\"")) + name +
            TJS_W("\"");
        const ScriptString storage = storageMarker + name + movieSuffix +
            TJS_W("\"");
        size_t keyPos = 0;
        while((keyPos = source.find(key, keyPos)) != missing) {
            const size_t lineEnd = source.find(TJS_W('\n'), keyPos);
            const size_t storagePos = source.find(storage, keyPos + key.size());
            if(storagePos != missing &&
               (lineEnd == missing || storagePos < lineEnd))
                return true;
            keyPos += key.size();
        }
        return false;
    };

    struct Correction {
        size_t position;
        size_t length;
        ScriptString logical;
        ScriptString physical;
    };
    std::vector<Correction> corrections;

    size_t keyPos = 0;
    while((keyPos = source.find(keyPrefix, keyPos)) != missing) {
        const size_t logicalBegin = keyPos + 1;
        const size_t logicalEnd = source.find(TJS_W('"'), logicalBegin);
        if(logicalEnd == missing)
            break;

        const ScriptString logical =
            source.substr(logicalBegin, logicalEnd - logicalBegin);
        int logicalFamily = 0;
        size_t logicalDigits = 0;
        if(!parseFamily(logical, logicalFamily, logicalDigits)) {
            keyPos = logicalEnd + 1;
            continue;
        }

        const size_t lineEnd = source.find(TJS_W('\n'), logicalEnd);
        const size_t storagePos = source.find(storageMarker, logicalEnd);
        if(storagePos == missing ||
           (lineEnd != missing && storagePos >= lineEnd)) {
            keyPos = logicalEnd + 1;
            continue;
        }

        const size_t physicalBegin = storagePos + storageMarker.size();
        const size_t physicalEnd = source.find(TJS_W('"'), physicalBegin);
        if(physicalEnd == missing ||
           (lineEnd != missing && physicalEnd >= lineEnd)) {
            keyPos = logicalEnd + 1;
            continue;
        }

        const ScriptString physicalFile =
            source.substr(physicalBegin, physicalEnd - physicalBegin);
        if(physicalFile.size() <= movieSuffix.size() ||
           physicalFile.compare(physicalFile.size() - movieSuffix.size(),
                                movieSuffix.size(), movieSuffix) != 0) {
            keyPos = logicalEnd + 1;
            continue;
        }
        const ScriptString physical = physicalFile.substr(
            0, physicalFile.size() - movieSuffix.size());

        int physicalFamily = 0;
        size_t physicalDigits = 0;
        if(!parseFamily(physical, physicalFamily, physicalDigits) ||
           logicalDigits != physicalDigits ||
           logicalFamily != physicalFamily + 1) {
            keyPos = logicalEnd + 1;
            continue;
        }

        const ScriptString sequencePrefix = logical.substr(
            0, logical.size() - finalSuffix.size());
        if(!hasIdentityMapping(sequencePrefix + TJS_W("_02_01")) ||
           !hasIdentityMapping(sequencePrefix + TJS_W("_02_05"))) {
            keyPos = logicalEnd + 1;
            continue;
        }

        const ScriptString correctedFile = logical + movieSuffix;
        if(!storageExists(ttstr(correctedFile))) {
            keyPos = logicalEnd + 1;
            continue;
        }

        corrections.push_back(
            { physicalBegin, physicalFile.size(), logical, physical });
        keyPos = logicalEnd + 1;
    }

    for(auto it = corrections.rbegin(); it != corrections.rend(); ++it) {
        const ScriptString correctedFile = it->logical + movieSuffix;
        source.replace(it->position, it->length, correctedFile);
        spdlog::info(
            "Corrected shifted numbered movie mapping: logical={} "
            "storage={} corrected={}",
            ttstr(it->logical).AsStdString(),
            (ttstr(it->physical) + TJS_W(".mpg")).AsStdString(),
            ttstr(correctedFile).AsStdString());
    }

    if(corrections.empty())
        return 0;
    script = ttstr(source);
    return static_cast<tjs_int>(corrections.size());
}

bool TVPPatchAffineSourceMotionStorageFallback(ttstr &script) {
    ttstr patched(script);

    // Some titles feed the regular AffineSourceMotion path while packaging
    // only D3D-prefixed E-mote PSBs. Resolve the physical storage name without
    // changing _innerStorage, which remains the title's logical identity.
    // AffineSourceMotion has no _useD3D member, so the safe discriminator is
    // whether the logical resource itself exists.
    patched.Replace(
        TJS_W("\t\t\t\t\tvar s = remove[i];\r\n"
              "\t\t\t\t\tif (s != \"\") {\r\n"
              "\t\t\t\t\t\t_motion_manager.unload(s);\r\n"
              "\t\t\t\t\t}\r\n"),
        TJS_W("\t\t\t\t\tvar s = remove[i];\r\n"
              "\t\t\t\t\tif (s != \"\") {\r\n"
              "\t\t\t\t\t\tvar unloadStorage = s;\r\n"
              "\t\t\t\t\t\tif (!Storages.isExistentStorage(unloadStorage)) {\r\n"
              "\t\t\t\t\t\t\tif (Storages.isExistentStorage(\"dx_\" + unloadStorage)) unloadStorage = \"dx_\" + unloadStorage;\r\n"
              "\t\t\t\t\t\t\telse if (Storages.isExistentStorage(\"dxlow_\" + unloadStorage)) unloadStorage = \"dxlow_\" + unloadStorage;\r\n"
              "\t\t\t\t\t\t}\r\n"
              "\t\t\t\t\t\t_motion_manager.unload(unloadStorage);\r\n"
              "\t\t\t\t\t}\r\n"),
        false);
    patched.Replace(
        TJS_W("\t\t\t\tvar s = create[i];\r\n"
              "\t\t\t\tif (s != \"\") {\r\n"
              "\t\t\t\t\tif (!Storages.isExistentStorage(s)) {\r\n"
              "\t\t\t\t\t\terror(@\"警告:モーション用画像が見つからない:${s}\");\r\n"
              "\t\t\t\t\t} else {\r\n"
              "\t\t\t\t\t\ttry {\r\n"
              "\t\t\t\t\t\t\tvar obj = _motion_manager.load(s);\r\n"),
        TJS_W("\t\t\t\tvar s = create[i];\r\n"
              "\t\t\t\tif (s != \"\") {\r\n"
              "\t\t\t\t\tvar loadStorage = s;\r\n"
              "\t\t\t\t\tif (!Storages.isExistentStorage(loadStorage)) {\r\n"
              "\t\t\t\t\t\tif (Storages.isExistentStorage(\"dx_\" + loadStorage)) loadStorage = \"dx_\" + loadStorage;\r\n"
              "\t\t\t\t\t\telse if (Storages.isExistentStorage(\"dxlow_\" + loadStorage)) loadStorage = \"dxlow_\" + loadStorage;\r\n"
              "\t\t\t\t\t}\r\n"
              "\t\t\t\t\tif (!Storages.isExistentStorage(loadStorage)) {\r\n"
              "\t\t\t\t\t\terror(@\"警告:モーション用画像が見つからない:${s}\");\r\n"
              "\t\t\t\t\t} else {\r\n"
              "\t\t\t\t\t\ttry {\r\n"
              "\t\t\t\t\t\t\tvar obj = _motion_manager.load(loadStorage);\r\n"),
        false);

    if(patched == script)
        return false;
    script = patched;
    return true;
}

static void TVPApplyScriptCompatibilityPatches(const ttstr &shortname,
                                               ttstr &buffer) {
    const ttstr lower = shortname.AsLowerCase();
    const bool standDebug = [] {
        const char *value = std::getenv("AETHERKIRI_STAND_DEBUG");
        return value && *value && *value != '0';
    }();
    const bool sceneDebug = [] {
        const char *value = std::getenv("AETHERKIRI_SCENE_DEBUG");
        return value && *value && *value != '0';
    }();

    if(lower == TJS_W("envinit.tjs")) {
        // Compatibility evidence for future per-game scoping:
        //   Title: もっと！孕ませ！炎のおっぱい異世界おっぱいスパイ学園！
        //   Pre-patch decoded envinit.tjs (UTF-8, CRLF) SHA-256:
        //     eb99e1869dd01e81de23b644f1d5e76fd2d47fb8fa666696b7173671084ed854
        //   Broken mapping anchors:
        //     ev_mv023_02_06 -> ev_mv022_02_06.mpg
        //     ev_mv024_02_06 -> ev_mv023_02_06.mpg
        // The current structural matcher intentionally supports equivalent
        // data revisions. If it ever conflicts with a deliberate alias, use
        // this fingerprint/anchor pair to gate a game compatibility profile.
        TVPRepairShiftedNumberedMovieMappings(buffer,
                                              TVPIsExistentStorage);
    }

    if(lower == TJS_W("messagelayer.tjs")) {
        ttstr patched(buffer);
        // This MessageLayer implementation already contains a native
        // Layer.drawText fallback for outlined text.  On the libgdiplus
        // backend, its optional drawPathString route can finish without
        // writing any glyph pixels, leaving the message window empty.
        // Disable only that optional route and use the script's own fallback.
        patched.Replace(
            TJS_W("if (!vertical && edge && antialiased && typeof lay.drawPathString != \"undefined\") {"),
            TJS_W("if (false && !vertical && edge && antialiased && typeof lay.drawPathString != \"undefined\") {"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info(
                "Applied compatibility patch for MessageLayer native outlined text drawing");
        }
    }

    if(lower == TJS_W("custom.tjs")) {
        std::basic_string<tjs_char> source(buffer.c_str(), buffer.GetLen());
        const std::basic_string<tjs_char> functionMarker(
            TJS_W("function EdgeShadowDrawText(dt, d,x,y,text,col,opa,aa, "
                  "s,scol,sw,sx,sy, e,ecol,eemp,eext) {"));
        const std::basic_string<tjs_char> blockMarker(TJS_W("\tif (d) {"));
        const std::basic_string<tjs_char> gradientMarker(
            TJS_W("var grad = MakeGradationLayer"));
        const std::basic_string<tjs_char> compositeMarker(
            TJS_W("d.operateRect(x, y, tmp"));

        const auto functionPos = source.find(functionMarker);
        const auto blockPos = functionPos == std::basic_string<tjs_char>::npos
            ? std::basic_string<tjs_char>::npos
            : source.find(blockMarker, functionPos + functionMarker.size());
        if(blockPos != std::basic_string<tjs_char>::npos) {
            const auto openPos = source.find(TJS_W('{'), blockPos);
            size_t blockEnd = std::basic_string<tjs_char>::npos;
            int depth = 0;
            for(size_t i = openPos; i < source.size(); ++i) {
                if(source[i] == TJS_W('{'))
                    ++depth;
                else if(source[i] == TJS_W('}') && --depth == 0) {
                    blockEnd = i + 1;
                    break;
                }
            }
            const auto gradientPos = source.find(gradientMarker, blockPos);
            const auto compositePos = source.find(compositeMarker, blockPos);
            if(blockEnd != std::basic_string<tjs_char>::npos &&
               gradientPos < blockEnd && compositePos < blockEnd) {
                const std::basic_string<tjs_char> replacement(
                    TJS_W("\tif (d) {\r\n"
                          "\t\tvar h = d.font.getTextHeight(text);\r\n"
                          "\t\td.drawTextVerticalGradient(x, y, text, "
                          "0xFFFFFF, col & 0xFFFFFF, opa, aa, h);\r\n"
                          "\t}"));
                source.replace(blockPos, blockEnd - blockPos, replacement);
                buffer = ttstr(source);
                spdlog::info(
                    "Applied compatibility patch for native gradient text "
                    "drawing");
            }
        }
    }

    if(lower == TJS_W("mainwindow.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\t\t\tif (elm.language !== void) {\r\n"
                  "\t\t\t\ttextLanguageType = 0; // 標準\r\n"
                  "\t\t\t}\r\n"),
            TJS_W("\t\t\tif (elm.language !== void) {\r\n"
                  "\t\t\t\ttextLanguageType = elm._aetherKiriLocalizedText ? languageType : 0;\r\n"
                  "\t\t\t}\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info(
                "Applied compatibility patch for preselected scene text language");
        }
    }

    if(lower == TJS_W("standaffinesourcelayer.tjs")) {
        const ttstr from(TJS_W(
            "property _width {\r\n"
            "\t\tgetter() {\r\n"
            "\t\t\tif (_standImage !== void) {\r\n"
            "\t\t\t\t_standImage.width;\r\n"
            "\t\t\t}\r\n"
            "\t\t}\r\n"
            "    }"));
        const ttstr to(TJS_W(
            "property _width {\r\n"
            "\t\tgetter() {\r\n"
            "\t\t\tif (_standImage !== void) {\r\n"
            "\t\t\t\treturn _standImage.width;\r\n"
            "\t\t\t}\r\n"
            "\t\t}\r\n"
            "    }"));
        ttstr patched(buffer);
        patched.Replace(from, to, false);
        patched.Replace(TJS_W("\t\t\t\t_standImage.width;\r\n"),
                        TJS_W("\t\t\t\treturn _standImage.width;\r\n"),
                        false);
        patched.Replace(TJS_W("\t\t\t\t_standImage.width;\n"),
                        TJS_W("\t\t\t\treturn _standImage.width;\n"),
                        false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info(
                "Applied compatibility patch for StandAffineSourceLayer._width");
        }

    }

    if(lower == TJS_W("d3d.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("} else if ((sourceClass !== void && typeof sourceClass[\"BMPBaseAffineSourceLayer\"] != \"undefined\") || (sourceClass === void && redraw)) {\r\n"),
            TJS_W("} else if ((sourceClass !== void && (ext == \".STAND\" || ext == \".EVENT\" || typeof sourceClass[\"BMPBaseAffineSourceLayer\"] != \"undefined\")) || (sourceClass === void && redraw)) {\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t\t\t\tisEmote = obj.root.metadata !== void && obj.root.metadata.format == \"emote\";\r\n"),
            TJS_W("\t\t\t\t\t\tisEmote = obj.root.metadata !== void && obj.root.metadata.format == \"emote\" && (options === void || (options.chara === void && options.motion === void));\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info("Applied compatibility patch for D3D source routing and PSB motion classification");
        }
    }

    if(lower == TJS_W("affinesourcemotion.tjs")) {
        if(TVPPatchAffineSourceMotionStorageFallback(buffer)) {
            spdlog::info(
                "Applied compatibility patch for D3D-prefixed AffineSourceMotion resources");
        }
    }

    if(lower == TJS_W("lose_seek.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\t\t}\r\n"
                  "\t\tresetChapterBadge();\r\n"),
            TJS_W("\t\t}\r\n"
                  "\t\tappendChapterTables = [];\r\n"
                  "\t\tif (typeof global.ForeachAppendFileList == \"Object\") {\r\n"
                  "\t\t\tForeachAppendFileList(SystemConfig.scnseekConvFile, function(file, tables) {\r\n"
                  "\t\t\t\ttry {\r\n"
                  "\t\t\t\t\tvar table = Scripts.evalStorage(file);\r\n"
                  "\t\t\t\t\tif (table !== void) tables.add(table);\r\n"
                  "\t\t\t\t} catch (e) {\r\n"
                  "\t\t\t\t\tDebug.notice(@\"${file}:追加シナリオ情報のロードに失敗:${e.message}\");\r\n"
                  "\t\t\t\t}\r\n"
                  "\t\t\t} incontextof global, appendChapterTables);\r\n"
                  "\t\t}\r\n"
                  "\t\tresetChapterBadge();\r\n"),
            false);
        patched.Replace(
            TJS_W("\tvar chapterList;\r\n"),
            TJS_W("\tvar chapterList;\r\n"
                  "\tvar appendChapterTables;\r\n"),
            false);
        const ttstr getChapterInfoFrom(TJS_W(
            "\tfunction getChapterInfo(target) {\r\n"
            "\t\tvar tag = normalizeName(target);\r\n"
            "\t\tif (tag == \"\") return;\r\n"
            "\t\tvar rev = chapterRevs[tag];\r\n"
            "\t\tif (rev != \"\") tag = rev;\r\n"
            "\t\treturn chapterInfo[tag];\r\n"
            "\t}"));
        const ttstr getChapterInfoTo(TJS_W(
            "\tfunction getChapterInfo(target) {\r\n"
            "\t\tvar normalized = normalizeName(target);\r\n"
            "\t\tif (normalized == \"\") return;\r\n"
            "\t\tvar tag = normalized;\r\n"
            "\t\tvar rev = chapterRevs[tag];\r\n"
            "\t\tif (rev != \"\") tag = rev;\r\n"
            "\t\tvar info = chapterInfo[tag];\r\n"
            "\t\tif (info !== void) return info;\r\n"
            "\t\tif (appendChapterTables !== void) {\r\n"
            "\t\t\tfor (var i = 0, count = appendChapterTables.count; i < count; i++) {\r\n"
            "\t\t\t\tvar table = appendChapterTables[i];\r\n"
            "\t\t\t\tif (table === void || table.info === void) continue;\r\n"
            "\t\t\t\ttag = normalized;\r\n"
            "\t\t\t\tif (table.revs !== void) {\r\n"
            "\t\t\t\t\trev = table.revs[tag];\r\n"
            "\t\t\t\t\tif (rev != \"\") tag = rev;\r\n"
            "\t\t\t\t}\r\n"
            "\t\t\t\tinfo = table.info[tag];\r\n"
            "\t\t\t\tif (info !== void) return info;\r\n"
            "\t\t\t}\r\n"
            "\t\t}\r\n"
            "\t}"));
        patched.Replace(getChapterInfoFrom, getChapterInfoTo, false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info(
                "Applied compatibility patch for appended scenario metadata");
        }
    }

    if(lower == TJS_W("motionaffinesourcelayer.tjs")) {
        ttstr patched(buffer);
        // There are two incompatible MotionAffineSourceLayer generations in
        // the wild.  The newer one owns a polymorphic emote/motion player and
        // exposes the storage-type lifecycle below; the older one constructs
        // Motion.Player directly.  Never inject lifecycle references into the
        // old class: unresolved _storageType/removePlayer/createPlayer names
        // abort setOptions before chara/motion can reach the native player.
        const bool hasStorageTypePlayerLifecycle =
            patched.IndexOf(TJS_W("_storageType")) >= 0 &&
            patched.IndexOf(TJS_W("function removePlayer")) >= 0 &&
            patched.IndexOf(TJS_W("function createPlayer")) >= 0;
        patched.Replace(
            TJS_W("\tfunction _loadImages(storage) {\r\n"),
            TJS_W("\tfunction _loadImages(storage, options=void) {\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t_storageType = (_metadata !== void && _metadata.format == \"emote\") ? \"emote\" : \"motion\";\r\n"),
            TJS_W("\t\t\t_storageType = (_metadata !== void && _metadata.format == \"emote\" && (options === void || (options.chara === void && options.motion === void))) ? \"emote\" : \"motion\";\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t_loadImages(storage);\r\n"
                  "\t\t\tcreatePlayer();\r\n"),
            TJS_W("\t\t\t_loadImages(storage, options);\r\n"
                  "\t\t\tif (_metadata !== void && _metadata.format == \"emote\" && options !== void && (options.chara !== void || options.motion !== void)) _storageType = \"motion\";\r\n"
                  "\t\t\tcreatePlayer();\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t_loadImages(storage, options);\r\n"
                  "\t\t\tcreatePlayer();\r\n"),
            TJS_W("\t\t\t_loadImages(storage, options);\r\n"
                  "\t\t\tif (_metadata !== void && _metadata.format == \"emote\" && options !== void && (options.chara !== void || options.motion !== void)) _storageType = \"motion\";\r\n"
                  "\t\t\tcreatePlayer();\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\tinstance._loadImages(_innerStorage);\r\n"
                  "\t\t\tinstance.createPlayer(this);\r\n"),
            TJS_W("\t\t\tinstance._loadImages(_innerStorage, _storageType == \"motion\" ? %[motion:true] : void);\r\n"
                  "\t\t\tinstance._storageType = _storageType;\r\n"
                  "\t\t\tinstance._aetherKiriHasMotion = _aetherKiriHasMotion;\r\n"
                  "\t\t\tinstance._aetherKiriLastMotion = _aetherKiriLastMotion;\r\n"
                  "\t\t\tinstance.createPlayer(this);\r\n"),
            false);
        if(hasStorageTypePlayerLifecycle) {
            patched.Replace(
                TJS_W("\t\t\t\tret.motion = _player.motion;\r\n"),
                TJS_W("\t\t\t\tret.motion = _aetherKiriHasMotion ? _aetherKiriLastMotion : _player.motion;\r\n"),
                false);
            patched.Replace(
                TJS_W("ret.motion = _player.motion;"),
                TJS_W("ret.motion = _aetherKiriHasMotion ? _aetherKiriLastMotion : _player.motion;"),
                false);
        }
        patched.Replace(
            TJS_W("\tfunction _getOptions(ret) {\r\n"
                  "\t\tif (_player !== void && _player.motion != \"\") {\r\n"),
            TJS_W("\tfunction _getOptions(ret) {\r\n"
                  "\t\tif (_player !== void && (_player.motion != \"\" || _aetherKiriHasMotion)) {\r\n"),
            false);
        patched.Replace(
            TJS_W("keys = _player.varibleKeys;"),
            TJS_W("keys = _player.variableKeys;"),
            false);
        patched.Replace(
            TJS_W("\t// 最後に指定した否ループタイムライン\r\n"
                  "\tvar _lastTimeline; \r\n"),
            TJS_W("\t// 最後に指定した否ループタイムライン\r\n"
                  "\tvar _lastTimeline; \r\n"
                  "\tvar _aetherKiriHasMotion = false;\r\n"
                  "\tvar _aetherKiriLastMotion;\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction checkOption(name) {\r\n"
                  "\t\tif (_storageType == \"emote\") {\r\n"
                  "\t\t\t//dm(@\"checkOption:${name}\");\r\n"),
            TJS_W("\tfunction checkOption(name) {\r\n"
                  "\t\tif (_storageType == \"emote\") {\r\n"
                  "\t\t\tif (name == \"motion\") return [name, \"flags\"];\r\n"
                  "\t\t\tif (name == \"chara\" || name == \"tickcount\" || name == \"speed\" || name == \"outline\") return name;\r\n"
                  "\t\t\t//dm(@\"checkOption:${name}\");\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction removePlayer() {\r\n"),
            TJS_W("\tfunction _aetherKiriBindMotionTarget(owner=void) {\r\n"
                  "\t\ttry {\r\n"
                  "\t\t\tif (_player === void) return;\r\n"
                  "\t\t\tif (owner === void && _owners !== void && _owners.count > 0) owner = _owners[_owners.count - 1];\r\n"
                  "\t\t\tif (owner === void) return;\r\n"
                  "\t\t\tvar layerOwner = owner incontextof global.Layer;\r\n"
                  "\t\t\tif (_separate && typeof owner._motionSeparateAdaptor != \"undefined\") {\r\n"
                  "\t\t\t\ttry { owner._motionSeparateAdaptor.targetLayer = layerOwner; } catch(e) {}\r\n"
                  "\t\t\t}\r\n"
                  "\t\t\t_player.targetLayer = layerOwner;\r\n"
                  "\t\t} catch(e) {}\r\n"
                  "\t}\r\n"
                  "\r\n"
                  "\tfunction _aetherKiriTouchMotionOwner(owner=void) {\r\n"
                  "\t\ttry {\r\n"
                  "\t\t\tif (owner === void && _owners !== void && _owners.count > 0) owner = _owners[_owners.count - 1];\r\n"
                  "\t\t\tif (owner !== void && owner instanceof \"AffineLayer\") {\r\n"
                  "\t\t\t\tif (_separate && typeof owner._motionSeparateAdaptor != \"undefined\") return;\r\n"
                  "\t\t\t\towner.calcUpdate();\r\n"
                  "\t\t\t\towner.entryFlip();\r\n"
                  "\t\t\t}\r\n"
                  "\t\t} catch(e) {}\r\n"
                  "\t}\r\n"
                  "\r\n"
                  "\tfunction removePlayer() {\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t_player = void;\r\n"
                  "\t\t\t_lastTimeline = void;\r\n"),
            TJS_W("\t\t\t_player = void;\r\n"
                  "\t\t\t_lastTimeline = void;\r\n"
                  "\t\t\t_aetherKiriHasMotion = false;\r\n"
                  "\t\t\t_aetherKiriLastMotion = void;\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t}\r\n"
                  "\t\t}\r\n"
                  "\t}\r\n"
                  "\r\n"
                  "\tfunction leaveOwner(owner) {\r\n"),
            TJS_W("\t\t\t}\r\n"
                  "\t\t}\r\n"
                  "\t\t_aetherKiriBindMotionTarget(owner);\r\n"
                  "\t\tif (_aetherKiriHasMotion) {\r\n"
                  "\t\t\t_aetherKiriTouchMotionOwner(owner);\r\n"
                  "\t\t}\r\n"
                  "\t}\r\n"
                  "\r\n"
                  "\tfunction leaveOwner(owner) {\r\n"),
            false);
        if(hasStorageTypePlayerLifecycle) {
            patched.Replace(
                TJS_W("\tfunction setOptions(elm) {\r\n"
                      "\t\tvar ret = super.setOptions(elm);\r\n"),
                TJS_W("\tfunction setOptions(elm) {\r\n"
                      "\t\tif (_storageType == \"emote\" && elm !== void && (elm.chara !== void || elm.motion !== void)) {\r\n"
                      "\t\t\tremovePlayer();\r\n"
                      "\t\t\t_storageType = \"motion\";\r\n"
                      "\t\t\tcreatePlayer();\r\n"
                      "\t\t}\r\n"
                      "\t\tvar ret = super.setOptions(elm);\r\n"),
                false);
        }
        patched.Replace(
            TJS_W("\t\t\tif (_player !== void) {\r\n"
                  "\t\t\t\tif (_storageType == \"emote\") {\r\n"),
            TJS_W("\t\t\tif (_player !== void) {\r\n"
                  "\t\t\t\t_aetherKiriBindMotionTarget();\r\n"
                  "\t\t\t\tif (_storageType == \"emote\") {\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t\tif (elm.motion !== void) {\r\n"
                  "\t\t\t\t\tvar flags = elm.flags !== void ? +elm.flags : Motion.PlayFlagForce;\r\n"
                  "\t\t\t\t\t//dm(@\"motion:${elm.motion} flags:${flags}\");\r\n"
                  "\t\t\t\t\t_player.play(elm.motion, flags);\r\n"
                  "\t\t\t\t\tstart = true;\r\n"
                  "\t\t\t\t\tret = \"motion\" if ret === void;\r\n"
                  "\t\t\t\t}\r\n"),
            TJS_W("\t\t\t\tif (elm.motion !== void) {\r\n"
                  "\t\t\t\t\tvar flags = elm.flags !== void ? +elm.flags : Motion.PlayFlagForce;\r\n"
                  "\t\t\t\t\t//dm(@\"motion:${elm.motion} flags:${flags}\");\r\n"
                  "\t\t\t\t\t_player.play(elm.motion, flags);\r\n"
                  "\t\t\t\t\t_aetherKiriHasMotion = true;\r\n"
                  "\t\t\t\t\t_aetherKiriLastMotion = elm.motion;\r\n"
                  "\t\t\t\t\tstart = true;\r\n"
                  "\t\t\t\t\tret = \"motion\" if ret === void;\r\n"
                  "\t\t\t\t}\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t\tif (start && _player.playing) {\r\n"
                  "\t\t\t\t\tonMotionStart();\r\n"
                  "\t\t\t\t}\r\n"),
            TJS_W("\t\t\t\tif (start && _player.playing) {\r\n"
                  "\t\t\t\t\tonMotionStart();\r\n"
                  "\t\t\t\t}\r\n"
                  "\t\t\t\tif (start) {\r\n"
                  "\t\t\t\t\t_aetherKiriBindMotionTarget();\r\n"
                  "\t\t\t\t\t_aetherKiriTouchMotionOwner();\r\n"
                  "\t\t\t\t}\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction drawAffine(target, src) {\r\n"
                  "\t\tif (_player !== void && _player.motion != \"\") {\r\n"),
            TJS_W("\tfunction drawAffine(target, src) {\r\n"
                  "\t\tif (_player !== void && (_player.motion != \"\" || _aetherKiriHasMotion)) {\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t\tif (_player !== void) {\r\n"
                  "\t\t\t\t\tvar name = target.name;\r\n"
                  "\t\t\t\t\tvar neutralColor = target.neutralColor;\r\n"),
            TJS_W("\t\t\t\tif (_player !== void) {\r\n"
                  "\t\t\t\t\tvar name = target.name;\r\n"
                  "\t\t\t\t\tvar neutralColor = target.neutralColor;\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t\t\tif (target == src && typeof src._motionSeparateAdaptor != \"undefined\") {\r\n"
                  "\t\t\t\t\t\ttarget = src._motionSeparateAdaptor;\r\n"
                  "\t\t\t\t\t}\r\n"),
            TJS_W("\t\t\t\t\tif (target == src && typeof src._motionSeparateAdaptor != \"undefined\") {\r\n"
                  "\t\t\t\t\t\ttarget = src._motionSeparateAdaptor;\r\n"
                  "\t\t\t\t\t\ttry { target.targetLayer = src incontextof global.Layer; } catch(e) {}\r\n"
                  "\t\t\t\t\t}\r\n"
            ),
            false);
        patched.Replace(
            TJS_W("\t\t\t\tif (target == src && typeof src._motionSeparateAdaptor != \"undefined\") {\r\n"
                  "\t\t\t\t\ttarget = src._motionSeparateAdaptor;\r\n"
                  "\t\t\t\t}\r\n"),
            TJS_W("\t\t\t\tif (target == src && typeof src._motionSeparateAdaptor != \"undefined\") {\r\n"
                  "\t\t\t\t\ttarget = src._motionSeparateAdaptor;\r\n"
                  "\t\t\t\t\ttry { target.targetLayer = src incontextof global.Layer; } catch(e) {}\r\n"
                  "\t\t\t\t}\r\n"
            ),
            false);
        patched.Replace(
            TJS_W("\t\t\t\t\tif (typeof _player.clear != \"undefined\") {\r\n"
                  "\t\t\t\t\t\t_player.clear(target, neutralColor);\r\n"
                  "\t\t\t\t\t}\r\n"),
            TJS_W("\t\t\t\t\t// AetherKiri: renderToLayer clears the target; native clear rejects some TJS layer wrappers.\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t\tif (typeof _player.clear != \"undefined\") {\r\n"
                  "\t\t\t\t\t_player.clear(target, neutralColor);\r\n"
                  "\t\t\t\t}\r\n"),
            TJS_W("\t\t\t\t// AetherKiri: renderToLayer clears the target; native clear rejects some TJS layer wrappers.\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info(
                "Applied compatibility patch for MotionAffineSourceLayer PSB motion classification");
        }
    }

    if(lower == TJS_W("world.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\t\tvar e = createMsgTag(text, lastText);\r\n"),
            TJS_W("\t\tvar e = createMsgTag(text, lastText);\r\n"
                  "\t\te._aetherKiriLocalizedText = true if (text.language !== void);\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\tvar delays = tagconv.convert(text.text);\r\n"),
            TJS_W("\t\t\tvar delays = tagconv.convert(kag.getLangInfo(text, \"text\"));\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info(
                "Applied compatibility patches for localized scene text");
        }
    }

    if(sceneDebug && lower == TJS_W("motionaffinesourcelayer.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\tfunction entryOwner(owner) {\r\n"
                  "\t\tsuper.entryOwner(owner);\r\n"),
            TJS_W("\tfunction entryOwner(owner) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] MotionAffine entryOwner image:${filename} owner:${owner !== void ? owner.name : void} owners:${_owners !== void ? _owners.count : -1} player:${_player !== void} motion:${_player !== void ? _player.motion : void}\"); } catch(e) {}\r\n"
                  "\t\tsuper.entryOwner(owner);\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\tif (_player !== void) {\r\n"
                  "\t\t\t\t_aetherKiriBindMotionTarget();\r\n"
                  "\t\t\t\tif (_storageType == \"emote\") {\r\n"),
            TJS_W("\t\t\tif (_player !== void) {\r\n"
                  "\t\t\t\t_aetherKiriBindMotionTarget();\r\n"
                  "\t\t\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] MotionAffine setOptions image:${filename} storageType:${_storageType} ownerCount:${_owners !== void ? _owners.count : -1} chara:${elm.chara} motion:${elm.motion} playerMotion:${_player.motion}\"); } catch(e) {}\r\n"
                  "\t\t\t\tif (_storageType == \"emote\") {\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\tif (_player !== void) {\r\n"
                  "\t\t\t\tif (_storageType == \"emote\") {\r\n"),
            TJS_W("\t\t\tif (_player !== void) {\r\n"
                  "\t\t\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] MotionAffine setOptions image:${filename} storageType:${_storageType} ownerCount:${_owners !== void ? _owners.count : -1} chara:${elm.chara} motion:${elm.motion} playerMotion:${_player.motion}\"); } catch(e) {}\r\n"
                  "\t\t\t\tif (_storageType == \"emote\") {\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t\tif (_player !== void) {\r\n"
                  "\t\t\t\t\t_aetherKiriBindMotionTarget(src);\r\n"
                  "\t\t\t\t\tvar name = target.name;\r\n"),
            TJS_W("\t\t\t\tif (_player !== void) {\r\n"
                  "\t\t\t\t\t_aetherKiriBindMotionTarget(src);\r\n"
                  "\t\t\t\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] MotionAffine drawAffine begin image:${filename} target:${target !== void ? target.name : void} src:${src !== void ? src.name : void} type:${src !== void ? src.type : void} motion:${_player.motion} playing:${_player.playing}\"); } catch(e) {}\r\n"
                  "\t\t\t\t\tvar name = target.name;\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t\tif (_player !== void) {\r\n"
                  "\t\t\t\t\tvar name = target.name;\r\n"),
            TJS_W("\t\t\t\tif (_player !== void) {\r\n"
                  "\t\t\t\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] MotionAffine drawAffine begin image:${filename} target:${target !== void ? target.name : void} src:${src !== void ? src.name : void} type:${src !== void ? src.type : void} motion:${_player.motion} playing:${_player.playing}\"); } catch(e) {}\r\n"
                  "\t\t\t\t\tvar name = target.name;\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t\t\t_player.draw(target);\r\n"),
            TJS_W("\t\t\t\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] MotionAffine drawAffine call image:${filename} target:${target !== void ? target : void} targetName:${target !== void ? target.name : void}\"); } catch(e) {}\r\n"
                  "\t\t\t\t\tif (target instanceof \"SeparateLayerAdaptor\" || target instanceof \"D3DAdaptor\") {\r\n"
                  "\t\t\t\t\t\t_player.draw(target);\r\n"
                  "\t\t\t\t\t} else {\r\n"
                  "\t\t\t\t\t\ttry { _player.targetLayer = target; } catch(e) {}\r\n"
                  "\t\t\t\t\t\t_player.draw();\r\n"
                  "\t\t\t\t\t}\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info("Applied scene debug patch for MotionAffineSourceLayer");
        }
    }

    if(lower == TJS_W("d3daffinesourcemotion.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\t\t\t\t_player = new D3DMotionPlayer(_d3dlayer);\r\n"),
            TJS_W("\t\t\t\t_player = new D3DMotionPlayer(_d3dlayer);\r\n"
                  "\t\t\t\ttry { _player.targetLayer = _d3dlayer; } catch(e) {}\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t\t\t_player.play(elm.motion, elm.flags !== void ? +elm.flags : Motion.PlayFlagForce);\r\n"),
            TJS_W("\t\t\t\t\t_player.play(elm.motion, elm.flags !== void ? +elm.flags : Motion.PlayFlagForce);\r\n"
                  "\t\t\t\t\ttry { _player.targetLayer = _d3dlayer; _player.draw(_d3dlayer); } catch(e) {}\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t_player.progress(diff * 60.0 / 1000);\r\n"),
            TJS_W("\t\t\t_player.progress(diff * 60.0 / 1000);\r\n"
                  "\t\t\ttry { _player.targetLayer = _d3dlayer; _player.draw(_d3dlayer); } catch(e) {}\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info(
                "Applied compatibility patch for D3DAffineSourceMotion target redraw");
        }
    }

    if(lower == TJS_W("world.tjs")) {
        ttstr patched(buffer);
        const bool hasPerLayerMessageVisibility =
            buffer.IndexOf(TJS_W("property msgvisible")) >= 0 &&
            buffer.IndexOf(TJS_W("var _msgvisible")) >= 0;
        if(hasPerLayerMessageVisibility) {
            patched.Replace(
                TJS_W("\t\tif (src !== void) {\r\n"
                      "\t\t\tlayer.assign(src);\r\n"
                      "\t\t}\r\n"),
                TJS_W("\t\tif (src !== void) {\r\n"
                      "\t\t\tlayer.assign(src);\r\n"
                      "\t\t\tlayer.msgvisible = msgvisible;\r\n"
                      "\t\t\tlayer.ignore = ignore;\r\n"
                      "\t\t}\r\n"),
                false);
        } else {
            patched.Replace(
                TJS_W("\t\tif (src !== void) {\r\n"
                      "\t\t\tlayer.assign(src);\r\n"
                      "\t\t}\r\n"),
                TJS_W("\t\tif (src !== void) {\r\n"
                      "\t\t\tlayer.assign(src);\r\n"
                      "\t\t\tlayer.ignore = ignore;\r\n"
                      "\t\t}\r\n"),
                false);
        }
        patched.Replace(
            TJS_W("\t\t\t\t\tobj = new EnvLayerObject(this, camera, name, className, classInfo.type == \"dlayer\");\r\n"
                  "\t\t\t\t\tobj.setMessageVisible(msgVisible && !msgHidden);\r\n"),
            TJS_W("\t\t\t\t\tobj = new EnvLayerObject(this, camera, name, className, classInfo.type == \"dlayer\");\r\n"
                  "\t\t\t\t\tif (classInfo.msgwinMode) syncMsgVisibleFromKag();\r\n"
                  "\t\t\t\t\tobj.setMessageVisible(msgVisible && !msgHidden);\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction setMsgwinLayerVisible() {\r\n"
                  "\t\tScripts.foreach(envlayerList, function(id,obj) {\r\n"),
            TJS_W("\tfunction syncMsgVisibleFromKag() {\r\n"
                  "\t\ttry {\r\n"
                  "\t\t\tvar __akMsgVisible = msgVisible;\r\n"
                  "\t\t\tvar __akMsg = (kag.fore !== void && kag.fore.messages !== void) ? kag.fore.messages[kag.currentNum] : void;\r\n"
                  "\t\t\tif (kag.current !== void) __akMsgVisible = __akMsgVisible || kag.current.visible;\r\n"
                  "\t\t\tif (__akMsg !== void) {\r\n"
                  "\t\t\t\t__akMsgVisible = __akMsgVisible || __akMsg.visible;\r\n"
                  "\t\t\t\ttry { if (__akMsg.comp !== void) __akMsgVisible = __akMsgVisible || __akMsg.comp.visible; } catch(__akMsgCompE) {}\r\n"
                  "\t\t\t}\r\n"
                  "\t\t\tmsgVisible = __akMsgVisible;\r\n"
                  "\t\t\ttry { msgHidden = kag.messageLayerHiding; } catch(__akMsgHiddenE) {}\r\n"
                  "\t\t} catch(__akMsgE) {}\r\n"
                  "\t}\r\n"
                  "\r\n"
                  "\tfunction setMsgwinLayerVisible() {\r\n"
                  "\t\tsyncMsgVisibleFromKag();\r\n"
                  "\t\tScripts.foreach(envlayerList, function(id,obj) {\r\n"),
            false);
        const bool patchedFaceRestore =
            TVPPatchWorldRestoreFaceVisibility(patched);
        if(!patchedFaceRestore &&
           patched.IndexOf(TJS_W("function _updateAll(")) >= 0 &&
           (patched.IndexOf(TJS_W("var data = allData.data;")) >= 0 ||
            patched.IndexOf(TJS_W("create.add(info);")) >= 0 ||
            patched.IndexOf(TJS_W("envClear(leave);")) >= 0)) {
            spdlog::warn(
                "Skipped atomic World._updateAll face-visibility patch: "
                "required anchors were incomplete or ambiguous");
        }
        if(patched != buffer) {
            buffer = patched;
            spdlog::info("Applied compatibility patch for world msgwin visibility restore");
        }
    }

    if(sceneDebug && lower == TJS_W("mainwindow.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("    function setCurrentMessageLayerVisible(visible, time) {\r\n"
                  "\r\n"
                  "\t\tif (visible) clearFace(0);\r\n"),
            TJS_W("    function setCurrentMessageLayerVisible(visible, time) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] kag.setCurrentMessageLayerVisible request:${visible} time:${time} currentNum:${currentNum} fore:${fore.messages[currentNum].visible} back:${back.messages[currentNum].visible} hiding:${messageLayerHiding}\"); } catch(e) {}\r\n"
                  "\r\n"
                  "\t\tif (visible) clearFace(0);\r\n"),
            false);
        patched.Replace(
            TJS_W("\ttextwrite : function(elm)\r\n"
                  "\t{\r\n"
                  "\t\ttextWriteEnabled = (elm.enabled !== void) ? +elm.enabled : true;\r\n"),
            TJS_W("\ttextwrite : function(elm)\r\n"
                  "\t{\r\n"
                  "\t\ttextWriteEnabled = (elm.enabled !== void) ? +elm.enabled : true;\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] kag.textwrite enabled:${textWriteEnabled}\"); } catch(e) {}\r\n"),
            false);
        patched.Replace(
            TJS_W("    msgon : function(elm)\r\n"
                  "\t{\r\n"
                  "\t\tmsgState = true;\r\n"),
            TJS_W("    msgon : function(elm)\r\n"
                  "\t{\r\n"
                  "\t\tmsgState = true;\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] kag.msgon enter dramatic:${dramaticModeWorking} skipNoDisp:${skipNoDisp} current:${current.visible} hiding:${messageLayerHiding}\"); } catch(e) {}\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\tvar ret = forEachFunctionHook(\"onMsgon\", elm);\r\n"
                  "\t\tif (ret !== void) {\r\n"
                  "\t\t\treturn ret;\r\n"),
            TJS_W("\t\tvar ret = forEachFunctionHook(\"onMsgon\", elm);\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] kag.msgon hook ret:${ret} current:${current.visible} fore:${fore.messages[currentNum].visible} back:${back.messages[currentNum].visible}\"); } catch(e) {}\r\n"
                  "\t\tif (ret !== void) {\r\n"
                  "\t\t\treturn ret;\r\n"),
            false);
        patched.Replace(
            TJS_W("    dispname : function(elm)\r\n"
                  "    {\r\n"
                  "\t\tforEachEventHook('onPreDispname',,elm);\r\n"),
            TJS_W("    dispname : function(elm)\r\n"
                  "    {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] kag.dispname enter name:${elm !== void ? elm.name : void} textWrite:${textWriteEnabled} current:${current.visible} hiding:${messageLayerHiding} skipNoDisp:${skipNoDisp}\"); } catch(e) {}\r\n"
                  "\t\tforEachEventHook('onPreDispname',,elm);\r\n"),
            false);
        patched.Replace(
            TJS_W("    ch : function(elm)\r\n"
                  "    {\r\n"
                  "\t\tif (textLanguageType != languageType) return 0;\r\n"),
            TJS_W("    ch : function(elm)\r\n"
                  "    {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] kag.ch text:${elm.text} textWrite:${textWriteEnabled} current:${current.visible} opacity:${current.opacity} hiding:${messageLayerHiding} skipNoDisp:${skipNoDisp}\"); } catch(e) {}\r\n"
                  "\t\tif (textLanguageType != languageType) return 0;\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info("Applied scene debug patch for MainWindow message flow");
        }
    }

    if(sceneDebug && lower == TJS_W("kagenvplayer.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\tfunction play(elm, skipNoDisp=false) {\r\n"
                  "\r\n"
                  "\t\t// 録画再生中\r\n"),
            TJS_W("\tfunction play(elm, skipNoDisp=false) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] player.play enter scene:${curSceneName} cur:${cur} point:${curPoint} skipNoDisp:${skipNoDisp} rec:${recplaying}\"); } catch(e) {}\r\n"
                  "\r\n"
                  "\t\t// 録画再生中\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t// 次の行\r\n"
                  "\t\tvar obj = getLine(cur++);\r\n"
                  "\r\n"
                  "\t\t// シーン終端\r\n"),
            TJS_W("\t\t// 次の行\r\n"
                  "\t\tvar obj = getLine(cur++);\r\n"
                  "\t\ttry {\r\n"
                  "\t\t\tvar __akObjType = typeof obj;\r\n"
                  "\t\t\tvar __akObjHead = \"\";\r\n"
                  "\t\t\tvar __akSavePoint = void;\r\n"
                  "\t\t\tif (__akObjType == \"Object\") {\r\n"
                  "\t\t\t\ttry { __akObjHead = obj[0]; } catch(__akHeadE) {}\r\n"
                  "\t\t\t\ttry { __akSavePoint = obj[SAVE_POINT]; } catch(__akPointE) {}\r\n"
                  "\t\t\t}\r\n"
                  "\t\t\tDebug.notice(@\"[AETHERKIRI_SCENE] player.line index:${cur-1} type:${__akObjType} head:${__akObjHead} savePoint:${__akSavePoint}\");\r\n"
                  "\t\t} catch(e) { Debug.notice(@\"[AETHERKIRI_SCENE] player.line log failed:${e.message}\"); }\r\n"
                  "\r\n"
                  "\t\t// シーン終端\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction onRestore(f) {\r\n"
                  "\t\tif (f.scenePlayer !== void) {\r\n"),
            TJS_W("\tfunction onRestore(f) {\r\n"
                  "\t\tif (f.scenePlayer !== void) {\r\n"
                  "\t\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] player.onRestore scene:${f.scenePlayer.scene} point:${f.scenePlayer.point}\"); } catch(e) {}\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\tif (typeof point == \"Integer\") {\r\n"
                  "\t\t\t// 数値指定ポイント\r\n"
                  "\t\t\twhile ((obj = getLine(newcur)) !== void) {\r\n"),
            TJS_W("\t\tif (typeof point == \"Integer\") {\r\n"
                  "\t\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] goToPoint request:${point}\"); } catch(e) {}\r\n"
                  "\t\t\t// 数値指定ポイント\r\n"
                  "\t\t\twhile ((obj = getLine(newcur)) !== void) {\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t\tif (typeof obj == \"Object\" && typeof obj[SAVE_POINT] == \"Integer\" && obj[SAVE_POINT] >= point) {\r\n"
                  "\t\t\t\t\trestore(obj[SAVE_STATE]);\r\n"),
            TJS_W("\t\t\t\tif (typeof obj == \"Object\" && typeof obj[SAVE_POINT] == \"Integer\" && obj[SAVE_POINT] >= point) {\r\n"
                  "\t\t\t\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] goToPoint match cur:${newcur} savePoint:${obj[SAVE_POINT]} saveText:${obj[SAVE_TEXT]} saveStateType:${typeof obj[SAVE_STATE]}\"); } catch(e) {}\r\n"
                  "\t\t\t\t\trestore(obj[SAVE_STATE]);\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\tcase \"Object\":\r\n"
                  "\t\t\tkag.stopAllVoice(0, true);\r\n"
                  "\t\t\tworld._updateAll(obj);\r\n"),
            TJS_W("\t\tcase \"Object\":\r\n"
                  "\t\t\ttry {\r\n"
                  "\t\t\t\tvar __akKeys = Scripts.getObjectKeys(obj).join(\",\");\r\n"
                  "\t\t\t\tvar __akData = obj.data;\r\n"
                  "\t\t\t\tDebug.notice(@\"[AETHERKIRI_SCENE] restore Object keys:${__akKeys} dataCount:${__akData !== void ? __akData.count : -1}\");\r\n"
                  "\t\t\t\tif (__akData !== void) {\r\n"
                  "\t\t\t\t\tfor (var __akI=0; __akI<__akData.count; __akI++) {\r\n"
                  "\t\t\t\t\t\tvar __akInfo = __akData[__akI];\r\n"
                  "\t\t\t\t\t\tvar __akElm = __akInfo[2];\r\n"
                  "\t\t\t\t\t\tvar __akImg = \"\";\r\n"
                  "\t\t\t\t\t\tif (__akElm !== void && __akElm.redraw !== void && __akElm.redraw.imageFile !== void) __akImg = __akElm.redraw.imageFile;\r\n"
                  "\t\t\t\t\t\tDebug.notice(@\"[AETHERKIRI_SCENE] restore data[${__akI}] name:${__akInfo[0]} class:${__akInfo[1]} show:${__akElm !== void ? __akElm.showmode : void} image:${__akImg}\");\r\n"
                  "\t\t\t\t\t}\r\n"
                  "\t\t\t\t}\r\n"
                  "\t\t\t} catch(e) { Debug.notice(@\"[AETHERKIRI_SCENE] restore log failed:${e.message}\"); }\r\n"
                  "\t\t\tkag.stopAllVoice(0, true);\r\n"
                  "\t\t\tworld._updateAll(obj);\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\tcase \"Object\":\r\n"
                  "\t\t\tworld._updateAll(obj);\r\n"
                  "\t\t\tloopVoiceInfo.onRestore(obj);\r\n"),
            TJS_W("\t\tcase \"Object\":\r\n"
                  "\t\t\ttry {\r\n"
                  "\t\t\t\tvar __akData = obj.data;\r\n"
                  "\t\t\t\tDebug.notice(@\"[AETHERKIRI_SCENE] restore Object dataCount:${__akData !== void ? __akData.count : -1}\");\r\n"
                  "\t\t\t\tif (__akData !== void) {\r\n"
                  "\t\t\t\t\tfor (var __akI=0; __akI<__akData.count; __akI++) {\r\n"
                  "\t\t\t\t\t\tvar __akInfo = __akData[__akI];\r\n"
                  "\t\t\t\t\t\tvar __akElm = __akInfo[2];\r\n"
                  "\t\t\t\t\t\tvar __akRedraw = __akElm !== void ? __akElm.redraw : void;\r\n"
                  "\t\t\t\t\t\tvar __akFile = __akRedraw !== void ? __akRedraw.imageFile : void;\r\n"
                  "\t\t\t\t\t\tDebug.notice(@\"[AETHERKIRI_SCENE] restore data[${__akI}] name:${__akInfo[0]} class:${__akInfo[1]} show:${__akElm !== void ? __akElm.showmode : void} redraw:${__akRedraw !== void} imageType:${typeof __akFile} file:${__akFile !== void ? __akFile.file : void} options:${__akFile !== void ? __akFile.options : void}\");\r\n"
                  "\t\t\t\t\t}\r\n"
                  "\t\t\t\t}\r\n"
                  "\t\t\t} catch(e) { Debug.notice(@\"[AETHERKIRI_SCENE] restore log failed:${e.message}\"); }\r\n"
                  "\t\t\tworld._updateAll(obj);\r\n"
                  "\t\t\tloopVoiceInfo.onRestore(obj);\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info("Applied scene debug patch for KAGEnvPlayer");
        }
    } else if(sceneDebug && lower == TJS_W("world.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\tfunction _updateAll(allData, snap=false, restore=true) {\r\n"
                  "\t\tif (allData !== void) {\r\n"),
            TJS_W("\tfunction _updateAll(allData, snap=false, restore=true) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] world._updateAll all:${allData !== void} data:${allData !== void && allData.data !== void ? allData.data.count : -1} snap:${snap} restore:${restore}\"); } catch(e) {}\r\n"
                  "\t\tif (allData !== void) {\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction _updateAll(allData, snap=false) {\r\n"
                  "\t\tif (allData !== void) {\r\n"),
            TJS_W("\tfunction _updateAll(allData, snap=false) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] world._updateAll all:${allData !== void} data:${allData !== void && allData.data !== void ? allData.data.count : -1} snap:${snap}\"); } catch(e) {}\r\n"
                  "\t\tif (allData !== void) {\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction msgonoff(elm, v) {\r\n"
                  "\t\tif (kag.skipNoDisp) {\r\n"),
            TJS_W("\tfunction msgonoff(elm, v) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] world.msgonoff v:${v} envTrans:${envTransMode} nofade:${elm.nofade} skipNoDisp:${kag.skipNoDisp} msgVisible:${kag.fore.messages[kag.currentNum].visible}\"); } catch(e) {}\r\n"
                  "\t\tif (kag.skipNoDisp) {\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction onCurrentMessageVisibleChanged(hidden, page, time) {\r\n"
                  "\t\tupdateMessageVisible();\r\n"),
            TJS_W("\tfunction onCurrentMessageVisibleChanged(hidden, page, time) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] world.onCurrentMessageVisibleChanged hidden:${hidden} page:${page} time:${time}\"); } catch(e) {}\r\n"
                  "\t\tupdateMessageVisible();\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction onMessageHiddenStateChanged(hidden, page) {\r\n"
                  "\t\tupdateMessageVisible();\r\n"),
            TJS_W("\tfunction onMessageHiddenStateChanged(hidden, page) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] world.onMessageHiddenStateChanged hidden:${hidden} page:${page}\"); } catch(e) {}\r\n"
                  "\t\tupdateMessageVisible();\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction update(elm) {\r\n"
                  "\t\tif (targetLayer !== void) {\r\n"
                  "\t\t\tif (elm.redraw !== void) with (elm.redraw) {\r\n"),
            TJS_W("\tfunction update(elm) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] layer.update name:${name} target:${targetLayer !== void} redraw:${elm.redraw !== void} update:${elm.update !== void}\"); } catch(e) {}\r\n"
                  "\t\tif (targetLayer !== void) {\r\n"
                  "\t\t\tif (elm.redraw !== void) with (elm.redraw) {\r\n"
                  "\t\t\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] layer.redraw name:${name} imageType:${typeof .imageFile} file:${.imageFile !== void ? .imageFile.file : void}\"); } catch(e) {}\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction objUpdate(elm) {\r\n"
                  "\t\t//dm(@\"${name}:objUpdate:${elm.showmode}:${elm.trans}:${elm.redraw}:${elm.update}:${world.envTransMode}\");\r\n"
                  "\t\t// 表示状態\r\n"
                  "\t\tvisible = (elm.showmode & 1);\r\n"),
            TJS_W("\tfunction objUpdate(elm) {\r\n"
                  "\t\t//dm(@\"${name}:objUpdate:${elm.showmode}:${elm.trans}:${elm.redraw}:${elm.update}:${world.envTransMode}\");\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] layer.objUpdate name:${name} show:${elm.showmode} trans:${elm.trans !== void} redraw:${elm.redraw !== void} envTrans:${world.envTransMode}\"); } catch(e) {}\r\n"
                  "\t\t// 表示状態\r\n"
                  "\t\tvisible = (elm.showmode & 1);\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction _setOpacity() {\r\n"
                  "\t\tvar o = _opacity * _visvalue / 100;\r\n"
                  "\t\tSUPER.opacity = o;\r\n"),
            TJS_W("\tfunction _setOpacity() {\r\n"
                  "\t\tvar o = _opacity * _visvalue / 100;\r\n"
                  "\t\tSUPER.opacity = o;\r\n"
                  "\t\ttry { if (name == \"face\" || name == \"hide_face\" || name == \"trans_face\") Debug.notice(@\"[AETHERKIRI_SCENE] setOpacity name:${name} o:${o} visvalue:${_visvalue} msgvisible:${_msgvisible} ignore:${_ignore} camera:${_cameraMode}\"); } catch(e) {}\r\n"),
            false);
        patched.Replace(
            TJS_W("\tvar msgvisible = true;\r\n"
                  "\tfunction setMessageVisible(visible) {\r\n"
                  "\t\tmsgvisible = visible;\r\n"),
            TJS_W("\tvar msgvisible = true;\r\n"
                  "\tfunction setMessageVisible(visible) {\r\n"
                  "\t\ttry { if (name == \"face\") Debug.notice(@\"[AETHERKIRI_SCENE] object.setMessageVisible name:${name} value:${visible}\"); } catch(e) {}\r\n"
                  "\t\tmsgvisible = visible;\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\tmsgVisible = __akMsgVisible;\r\n"
                  "\t\t\ttry { msgHidden = kag.messageLayerHiding; } catch(__akMsgHiddenE) {}\r\n"),
            TJS_W("\t\t\tmsgVisible = __akMsgVisible;\r\n"
                  "\t\t\ttry { msgHidden = kag.messageLayerHiding; } catch(__akMsgHiddenE) {}\r\n"
                  "\t\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] syncMsgVisible msgVisible:${msgVisible} msgHidden:${msgHidden} current:${kag.current !== void ? kag.current.visible : void} fore:${__akMsg !== void ? __akMsg.visible : void} comp:${(__akMsg !== void && __akMsg.comp !== void) ? __akMsg.comp.visible : void} hiding:${kag.messageLayerHiding}\"); } catch(__akMsgLogE) {}\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction createObject(name, className) {\r\n"
                  "\t\tvar obj = envobjects[name];\r\n"),
            TJS_W("\tfunction createObject(name, className) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] createObject name:${name} class:${className}\"); } catch(e) {}\r\n"
                  "\t\tvar obj = envobjects[name];\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction onCurrentMessageVisibleChanged(hidden, page, time) {\r\n"
                  "\t\tmsgVisible = !hidden;\r\n"),
            TJS_W("\tfunction onCurrentMessageVisibleChanged(hidden, page, time) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] onCurrentMessageVisibleChanged hidden:${hidden} page:${page} time:${time}\"); } catch(e) {}\r\n"
                  "\t\tmsgVisible = !hidden;\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction onMessageHiddenStateChanged(hidden, page) {\r\n"
                  "\t\tmsgHidden = hidden;\r\n"),
            TJS_W("\tfunction onMessageHiddenStateChanged(hidden, page) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] onMessageHiddenStateChanged hidden:${hidden} page:${page}\"); } catch(e) {}\r\n"
                  "\t\tmsgHidden = hidden;\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction updateImage(elm) {\r\n"
                  "\t\tif (targetLayer !== void) {\r\n"),
            TJS_W("\tfunction updateImage(elm) {\r\n"
                  "\t\ttry { var __akImg = (elm !== void && elm.redraw !== void && elm.redraw.imageFile !== void) ? elm.redraw.imageFile : \"\"; if (__akImg != \"\") Debug.notice(@\"[AETHERKIRI_SCENE] updateImage obj:${name} class:${className} image:${__akImg}\"); } catch(e) {}\r\n"
                  "\t\tif (targetLayer !== void) {\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction updateImageSource(owner, file,  name=\"_image\") {\r\n"
                  "\t\tvar imageSource = owner[name];\r\n"
                  "\t\tvar imageData = getImageData(file);\r\n"),
            TJS_W("\tfunction updateImageSource(owner, file,  name=\"_image\") {\r\n"
                  "\t\tvar imageSource = owner[name];\r\n"
                  "\t\tvar imageData = getImageData(file);\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] updateImageSource owner:${owner !== void ? owner.name : void} slot:${name} file:${file} storage:${imageData !== void ? imageData.storage : void} current:${imageSource !== void ? imageSource.filename : void}\"); } catch(e) {}\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\towner[name] = imageSource;\r\n"
                  "\t\towner.calcUpdate();\r\n"),
            TJS_W("\t\towner[name] = imageSource;\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] updateImageSource result owner:${owner !== void ? owner.name : void} slot:${name} source:${imageSource} filename:${imageSource !== void ? imageSource.filename : void}\"); } catch(e) {}\r\n"
                  "\t\towner.calcUpdate();\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction getImageSource(imageData, owner) {\r\n"
                  "\t\tif (imageData !== void) {\r\n"),
            TJS_W("\tfunction getImageSource(imageData, owner) {\r\n"
                  "\t\ttry { if (imageData !== void && imageData.storage != \"\") Debug.notice(@\"[AETHERKIRI_SCENE] getImageSource owner:${owner !== void ? owner.name : void} storage:${imageData.storage}\"); } catch(e) {}\r\n"
                  "\t\tif (imageData !== void) {\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\tScripts.foreach(absLayers, function(i,v,count) {\r\n"
                  "\t\t\t\tvar abs =  v.absoluteBase + i;\r\n"
                  "\t\t\t\tif (v.absolute != abs) {\r\n"
                  "\t\t\t\t\tv.absolute = abs;\r\n"
                  "\t\t\t\t}\r\n"
                  "\t\t\t\t//dm(@\"${v.name}:absolute:${v.absolute}\");\r\n"
                  "\t\t\t}, absLayers.count);\r\n"),
            TJS_W("\t\t\tScripts.foreach(absLayers, function(i,v,count) {\r\n"
                  "\t\t\t\tvar abs =  v.absoluteBase + i;\r\n"
                  "\t\t\t\tif (v.absolute != abs) {\r\n"
                  "\t\t\t\t\tv.absolute = abs;\r\n"
                  "\t\t\t\t}\r\n"
                  "\t\t\t\ttry {\r\n"
                  "\t\t\t\t\tvar __akName = \"\";\r\n"
                  "\t\t\t\t\ttry { __akName = v.name; } catch(__akNameE) {}\r\n"
                  "\t\t\t\t\tif (__akName == \"\") try { __akName = v.target.name; } catch(__akTargetNameE) {}\r\n"
                  "\t\t\t\t\tif (__akName == \"face\" || __akName == \"秀明\" || __akName == \"和奏\" || v.absoluteBase >= 1000000) {\r\n"
                  "\t\t\t\t\t\tDebug.notice(@\"[AETHERKIRI_SCENE] absolute i:${i}/${count} name:${__akName} base:${v.absoluteBase} calc:${abs} actual:${v.absolute} z:${v.orderzpos} order:${v.order !== void ? v.order : void} vis:${v.visible !== void ? v.visible : void} opa:${v.opacity !== void ? v.opacity : void} width:${v.width !== void ? v.width : void} height:${v.height !== void ? v.height : void}\");\r\n"
                  "\t\t\t\t\t}\r\n"
                  "\t\t\t\t} catch(e) { Debug.notice(@\"[AETHERKIRI_SCENE] absolute log failed:${e.message}\"); }\r\n"
                  "\t\t\t\t//dm(@\"${v.name}:absolute:${v.absolute}\");\r\n"
                  "\t\t\t}, absLayers.count);\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info("Applied scene debug patch for world");
        }
    } else if(sceneDebug && lower == TJS_W("affinesourcelayer.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\t//dm(@\"createAffineSource:${filename}:${storage}\");\r\n"
                  "\tvar sourceInfo = findAffineSource(filename, options);\r\n"
                  "\tvar sourceClass = sourceInfo.sourceClass;\r\n"
                  "\tvar storage = sourceInfo.storage;\r\n"
                  "\tvar image = (sourceClass !== void) ? new sourceClass(window, options) : new global.ImageAffineSourceLayer(window);\r\n"),
            TJS_W("\t//dm(@\"createAffineSource:${filename}:${storage}\");\r\n"
                  "\tvar sourceInfo = findAffineSource(filename, options);\r\n"
                  "\tvar sourceClass = sourceInfo.sourceClass;\r\n"
                  "\tvar storage = sourceInfo.storage;\r\n"
                  "\tvar image = (sourceClass !== void) ? new sourceClass(window, options) : new global.ImageAffineSourceLayer(window);\r\n"
                  "\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] createAffineSource filename:${filename} storage:${storage} ext:${sourceInfo.ext} sourceClass:${sourceClass} image:${image} optStorage:${options !== void ? options.storage : void} optChara:${options !== void ? options.chara : void} optMotion:${options !== void ? options.motion : void}\"); } catch(e) {}\r\n"),
            false);
        patched.Replace(
            TJS_W("\tvar ret = image.loadImages(storage, colorKey, options);\r\n"
                  "\tvar optlist;\r\n"),
            TJS_W("\tvar ret = image.loadImages(storage, colorKey, options);\r\n"
                  "\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] createAffineSource loadImages filename:${filename} image:${image} ret:${ret} imageStorageType:${image._storageType !== void ? image._storageType : void} optChara:${options !== void ? options.chara : void} optMotion:${options !== void ? options.motion : void}\"); } catch(e) {}\r\n"
                  "\tvar optlist;\r\n"),
            false);
        patched.Replace(
            TJS_W("\tif (options !== void) {\r\n"
                  "\t\toptlist = image.setOptions(options);\r\n"
                  "\t}\r\n"),
            TJS_W("\tif (options !== void) {\r\n"
                  "\t\toptlist = image.setOptions(options);\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] createAffineSource setOptions filename:${filename} image:${image} optlist:${optlist} imageStorageType:${image._storageType !== void ? image._storageType : void} optChara:${options.chara} optMotion:${options.motion}\"); } catch(e) {}\r\n"
                  "\t}\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info("Applied scene debug patch for AffineSourceLayer");
        }
    } else if(sceneDebug && lower == TJS_W("kagenvimage.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\t\t\t\tret.imageFile = env.getImageFileDataConv(imageSourceFile, imageSource, options, imageRedraw, zresolution, extend, packopt);\r\n"),
            TJS_W("\t\t\t\tret.imageFile = env.getImageFileDataConv(imageSourceFile, imageSource, options, imageRedraw, zresolution, extend, packopt);\r\n"
                  "\t\t\t\ttry { Debug.notice(@\"[AETHERKIRI_SCENE] getUpdateData obj:${name} sourceFile:${imageSourceFile} source:${imageSource} options:${options} imageFile:${ret.imageFile}\"); } catch(e) {}\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info("Applied scene debug patch for KAGEnvImage");
        }
    } else if(sceneDebug && lower == TJS_W("affinelayer.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\tfunction calcUpdate(l,t,w,h) {\r\n"
                  "\t\tif (_doAffine < 2) {\r\n"),
            TJS_W("\tfunction calcUpdate(l,t,w,h) {\r\n"
                  "\t\ttry { if (_image !== void && (_image instanceof \"StandAffineSourceLayer\" || _image instanceof \"MotionAffineSourceLayer\")) Debug.notice(@\"[AETHERKIRI_SCENE] AffineLayer calcUpdate name:${name} image:${_image.filename} class:${typeof _image} region:${l},${t},${w},${h} affine:${_doAffine} call:${callOnPaint} geom:${left},${top},${width}x${height} visible:${visible} opacity:${opacity}\"); } catch(e) {}\r\n"
                  "\t\tif (_doAffine < 2) {\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\tif (_image !== void) {\r\n"
                  "\t\t\tif (_updateFlag) {\r\n"
                  "\t\t\t\t_image.updateImage(this);\r\n"),
            TJS_W("\t\tif (_image !== void) {\r\n"
                  "\t\t\ttry { if (_image instanceof \"StandAffineSourceLayer\" || _image instanceof \"MotionAffineSourceLayer\") Debug.notice(@\"[AETHERKIRI_SCENE] AffineLayer onPaint name:${name} image:${_image.filename} class:${typeof _image} update:${_updateFlag} affine:${_doAffine} call:${callOnPaint} type:${type} geom:${left},${top},${width}x${height} visible:${visible} opacity:${opacity}\"); } catch(e) {}\r\n"
                  "\t\t\tif (_updateFlag) {\r\n"
                  "\t\t\t\t_image.updateImage(this);\r\n"
                  "\t\t\t\ttry { if (_image instanceof \"StandAffineSourceLayer\" || _image instanceof \"MotionAffineSourceLayer\") Debug.notice(@\"[AETHERKIRI_SCENE] AffineLayer afterUpdate name:${name} image:${_image.filename} geom:${left},${top},${width}x${height} image:${imageWidth}x${imageHeight} af:${_image._afx},${_image._afy} src:${_image.width}x${_image.height}\"); } catch(e) {}\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info("Applied scene debug patch for AffineLayer");
        }
    } else if(sceneDebug && lower == TJS_W("autoface.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\tfunction onEnvParseStartLine(pcd, cd, elm, env) {\r\n"
                  "\t\tif (!autoShow || elm.notext) return;\r\n"
                  "\t\tvar obj;\r\n"
                  "\t\tvar face = env.objects[facename];\r\n"
                  "\t\tif (elm === void || elm.name == \"\" || (obj = findStand(env, elm.name)) === void) {\r\n"),
            TJS_W("\tfunction onEnvParseStartLine(pcd, cd, elm, env) {\r\n"
                  "\t\tif (!autoShow || elm.notext) return;\r\n"
                  "\t\tvar obj;\r\n"
                  "\t\tvar face = env.objects[facename];\r\n"
                  "\t\tif (elm !== void && elm.name != \"\") obj = findStand(env, elm.name);\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_FACE] startLine elm:${elm !== void ? elm.name : void} obj:${obj !== void ? obj.name : void} objClass:${obj !== void ? obj.classInfo.name : void} objDisp:${obj !== void ? obj.disp : void} objShow:${obj !== void ? obj.isShow() : void} faceShow:${face !== void ? face.isShow() : void} faceFollow:${face !== void ? face.followSource : void}\"); } catch(e) { Debug.notice(@\"[AETHERKIRI_FACE] startLine log failed:${e.message}\"); }\r\n"
                  "\t\tif (elm === void || elm.name == \"\" || obj === void) {\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\tif (obj.disp >= KAGEnvImage.INVISIBLE || obj.classInfo.name == \"bu\") {\r\n"
                  "\t\t\t\t// 話者が非表示指定なので全部消しておく\r\n"
                  "\t\t\t\thideFace(pcd, env);\r\n"
                  "\t\t\t\te.disp = 3;\r\n"
                  "\t\t\t} else if (obj.classInfo.name == msgwin) {\r\n"
                  "\t\t\t\t// 話者が顔位置にいるので他のオブジェクトは消しておく\r\n"
                  "\t\t\t\thideFace(pcd, env, obj.name);\r\n"
                  "\t\t\t\te.disp = 3;\r\n"
                  "\t\t\t} else {\r\n"),
            TJS_W("\t\t\tif (obj.disp >= KAGEnvImage.INVISIBLE || obj.classInfo.name == \"bu\") {\r\n"
                  "\t\t\t\ttry { Debug.notice(@\"[AETHERKIRI_FACE] startLine branch hide name:${name} disp:${obj.disp} class:${obj.classInfo.name}\"); } catch(e) {}\r\n"
                  "\t\t\t\t// 話者が非表示指定なので全部消しておく\r\n"
                  "\t\t\t\thideFace(pcd, env);\r\n"
                  "\t\t\t\te.disp = 3;\r\n"
                  "\t\t\t} else if (obj.classInfo.name == msgwin) {\r\n"
                  "\t\t\t\ttry { Debug.notice(@\"[AETHERKIRI_FACE] startLine branch msgwin name:${name} disp:${obj.disp}\"); } catch(e) {}\r\n"
                  "\t\t\t\t// 話者が顔位置にいるので他のオブジェクトは消しておく\r\n"
                  "\t\t\t\thideFace(pcd, env, obj.name);\r\n"
                  "\t\t\t\te.disp = 3;\r\n"
                  "\t\t\t} else {\r\n"
                  "\t\t\t\ttry { Debug.notice(@\"[AETHERKIRI_FACE] startLine branch copy name:${name} disp:${obj.disp} show:${obj.isShow()}\"); } catch(e) {}\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction onEnvParseStartText(pcd, cd, elm, env) {\r\n"
                  "\t\tif (!autoShow || elm.notext) return;\r\n"
                  "\t\tvar obj;\r\n"
                  "\t\tif (elm !== void && elm.name != \"\" && (obj = findStand(env, elm.name)) !== void) {\r\n"),
            TJS_W("\tfunction onEnvParseStartText(pcd, cd, elm, env) {\r\n"
                  "\t\tif (!autoShow || elm.notext) return;\r\n"
                  "\t\tvar obj;\r\n"
                  "\t\tif (elm !== void && elm.name != \"\") obj = findStand(env, elm.name);\r\n"
                  "\t\ttry { var __akFace = env.objects[facename]; Debug.notice(@\"[AETHERKIRI_FACE] startText elm:${elm !== void ? elm.name : void} obj:${obj !== void ? obj.name : void} objClass:${obj !== void ? obj.classInfo.name : void} objDisp:${obj !== void ? obj.disp : void} objShow:${obj !== void ? obj.isShow() : void} faceShow:${__akFace !== void ? __akFace.isShow() : void} faceFollow:${__akFace !== void ? __akFace.followSource : void} event:${env.isEventShow()} force:${forceShow}\"); } catch(e) { Debug.notice(@\"[AETHERKIRI_FACE] startText log failed:${e.message}\"); }\r\n"
                  "\t\tif (elm !== void && elm.name != \"\" && obj !== void) {\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t\tif (eventShow || !env.isEventShow()) {\r\n"
                  "\t\t\t\t\tvar show = forceShow ? true : obj.isShow();\r\n"
                  "\t\t\t\t\tvar face = env.objects[facename];\r\n"
                  "\t\t\t\t\tif (show && (face === void || !face.isShow())) {\r\n"
                  "\t\t\t\t\t\tpcd.addNextTag(facename, %[\"copyfollow\", name, \"disp\", true, \"$eye\", name, \"$lip\", name, \"nosync\", true]);\r\n"
                  "\t\t\t\t\t}\r\n"
                  "\t\t\t\t}\r\n"),
            TJS_W("\t\t\t\tif (eventShow || !env.isEventShow()) {\r\n"
                  "\t\t\t\t\tvar show = forceShow ? true : obj.isShow();\r\n"
                  "\t\t\t\t\tvar face = env.objects[facename];\r\n"
                  "\t\t\t\t\ttry { Debug.notice(@\"[AETHERKIRI_FACE] startText copy name:${name} show:${show} objShow:${obj.isShow()} faceShow:${face !== void ? face.isShow() : void}\"); } catch(e) {}\r\n"
                  "\t\t\t\t\tif (show && (face === void || !face.isShow())) {\r\n"
                  "\t\t\t\t\t\tDebug.notice(@\"[AETHERKIRI_FACE] startText enqueue face disp:${name}\");\r\n"
                  "\t\t\t\t\t\tpcd.addNextTag(facename, %[\"copyfollow\", name, \"disp\", true, \"$eye\", name, \"$lip\", name, \"nosync\", true]);\r\n"
                  "\t\t\t\t\t}\r\n"
                  "\t\t\t\t}\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info("Applied scene debug patch for AutoFace");
        }
    }

    if(!standDebug)
        return;

    if(lower == TJS_W("standaffinesourcelayer.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\tfunction loadImages(storage,colorKey=clNone,options=void) {\r\n"
                  "\t\t_standImage = new StandImage(_window, storage);\r\n"),
            TJS_W("\tfunction loadImages(storage,colorKey=clNone,options=void) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] StandAffine loadImages storage:${storage} options:${options}\"); } catch(e) {}\r\n"
                  "\t\t_standImage = new StandImage(_window, storage);\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction loadImages(storage,colorKey=clNone,options=void) {\r\n"
                  "\t\t_standImage = new StandImage(storage, _window, storage);\r\n"),
            TJS_W("\tfunction loadImages(storage,colorKey=clNone,options=void) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] StandAffine loadImages storage:${storage} options:${options}\"); } catch(e) {}\r\n"
                  "\t\t_standImage = new StandImage(storage, _window, storage);\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction updateImage(src, clean=false) {\r\n"
                  "\t\tif (_standImage !== void) {\r\n"),
            TJS_W("\tfunction updateImage(src, clean=false) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] StandAffine updateImage src:${src} clean:${clean} stand:${_standImage !== void}\"); } catch(e) {}\r\n"
                  "\t\tif (_standImage !== void) {\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info("Applied stand debug patch for StandAffineSourceLayer");
        }
    } else if(lower == TJS_W("standinformation.tjs") ||
              lower == TJS_W("mainwindow.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\t\t\t\t\tvar infofile = .filename + \"_info.txt\";\r\n"),
            TJS_W("\t\t\t\t\tvar infofile = .filename + \"_info.txt\";\r\n"
                  "\t\t\t\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] getChStandInfo storage:${storage} base:${baseName} filename:${.filename} infofile:${infofile}\"); } catch(e) {}\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t\t\tvar infofile = .filename + \"_info.txt\";\r\n"
                  "\t\t\t\t\tif (!Storages.isExistentStorage(infofile)) {\r\n"),
            TJS_W("\t\t\t\t\tvar infofile = .filename + \"_info.txt\";\r\n"
                  "\t\t\t\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] getChStandInfo storage:${storage} base:${baseName} filename:${.filename} infofile:${infofile} exists:${Storages.isExistentStorage(infofile)}\"); } catch(e) {}\r\n"
                  "\t\t\t\t\tif (!Storages.isExistentStorage(infofile)) {\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t\tfile.load(infofile);\r\n"),
            TJS_W("\t\t\t\tfile.load(infofile);\r\n"
                  "\t\t\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] loadStandInfo infofile:${infofile} lines:${file.count}\"); } catch(e) {}\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info("Applied stand debug patch for StandInformation");
        }
    } else if(lower == TJS_W("standimage.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\tfunction updateImage(src) {\r\n\r\n"
                  "\t\tzresolution = typeof src.zresolution != \"undefined\" ? src.zresolution : 100;\r\n"),
            TJS_W("\tfunction updateImage(src) {\r\n\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] updateImage enter storage:${storage} base:${base} dress:${dress} pose:${pose} face:${face} update:${updateFlag} standInfo:${standInfo !== void}\"); } catch(e) {}\r\n"
                  "\t\tzresolution = typeof src.zresolution != \"undefined\" ? src.zresolution : 100;\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\tif (!updateFlag || base === void || dress === void || pose === void || face === void || standInfo === void) {\r\n"
                  "\t\t\treturn;\r\n"
                  "\t\t}\r\n"),
            TJS_W("\t\tif (!updateFlag || base === void || dress === void || pose === void || face === void || standInfo === void) {\r\n"
                  "\t\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] updateImage skip storage:${storage} update:${updateFlag} baseVoid:${base === void} dressVoid:${dress === void} poseVoid:${pose === void} faceVoid:${face === void} standInfoVoid:${standInfo === void}\"); } catch(e) {}\r\n"
                  "\t\t\treturn;\r\n"
                  "\t\t}\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\t//dm(@\"${storage}:立ち絵レベル level:${level} imageLevel:${imageLevel}\");\r\n"
                  "\t\t\t\r\n"
                  "\t\t\tstandlayer = infoBase.getStandPSDLayer(standInfo, file, this);\r\n"),
            TJS_W("\t\t\t//dm(@\"${storage}:立ち絵レベル level:${level} imageLevel:${imageLevel}\");\r\n"
                  "\t\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] select file storage:${storage} base:${base} level:${level} imageLevel:${imageLevel} file:${file}\"); } catch(e) {}\r\n"
                  "\t\t\t\r\n"
                  "\t\t\tstandlayer = infoBase.getStandPSDLayer(standInfo, file, this);\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\tstandlayer.resetSize();\r\n"
                  "\t\t\tstandLevel = level;\r\n"),
            TJS_W("\t\t\tstandlayer.resetSize();\r\n"
                  "\t\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] resetSize storage:${storage} layer:${standlayer.name} size:${standlayer.width}x${standlayer.height} page:${standlayer.pageWidth}x${standlayer.pageHeight} offset:${standlayer.offsetX},${standlayer.offsetY}\"); } catch(e) {}\r\n"
                  "\t\t\tstandLevel = level;\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\tstandlayer.setFace(face, variables);\r\n\r\n"
                  "\t\tupdateFlag = false;\r\n"),
            TJS_W("\t\tstandlayer.setFace(face, variables);\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] updateImage done storage:${storage} base:${base} dress:${dress} pose:${pose} face:${face} ret:${ret}\"); } catch(e) {}\r\n\r\n"
                  "\t\tupdateFlag = false;\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info("Applied stand debug patch for StandImage");
        }
    } else if(lower == TJS_W("psdlayer.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\t\tinitLayers(width, height);\r\n"),
            TJS_W("\t\tinitLayers(width, height);\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] PSD loadDATA basename:${basename} size:${width}x${height} layers:${layers.count} groups:${groups.count}\"); } catch(e) {}\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\tdata.load(storage);\r\n"
                  "\t\tvar count = data.count;\r\n"),
            TJS_W("\t\tdata.load(storage);\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] PSD loadTXT storage:${storage} rawLines:${data.count}\"); } catch(e) {}\r\n"
                  "\t\tvar count = data.count;\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction resetSize() {\r\n"
                  "\t\tif (psdinfo.width > 0 && psdinfo.height > 0) {\r\n"),
            TJS_W("\tfunction resetSize() {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] PSD resetSize psd:${psdinfo !== void} psdsize:${psdinfo !== void ? psdinfo.width : void}x${psdinfo !== void ? psdinfo.height : void}\"); } catch(e) {}\r\n"
                  "\t\tif (psdinfo.width > 0 && psdinfo.height > 0) {\r\n"),
            false);
        patched.Replace(
            TJS_W("\tfunction updateDisp(dispRect=null) {\r\n"
                  "\t\t\r\n"
                  "\t\tif (psdinfo === void) {\r\n"),
            TJS_W("\tfunction updateDisp(dispRect=null) {\r\n"
                  "\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] PSD updateDisp enter width:${width} height:${height} dispRect:${dispRect}\"); } catch(e) {}\r\n"
                  "\t\t\r\n"
                  "\t\tif (psdinfo === void) {\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\treturn [l,t,w,h];\r\n"),
            TJS_W("\t\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] PSD updateDisp region:${l},${t},${w},${h}\"); } catch(e) {}\r\n"
                  "\t\t\treturn [l,t,w,h];\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info("Applied stand debug patch for psdlayer");
        }
    } else if(lower == TJS_W("standlayer.tjs")) {
        ttstr patched(buffer);
        patched.Replace(
            TJS_W("\t\t\tloadImages(storage);\r\n"),
            TJS_W("\t\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] StandPSDInfo loadImages storage:${storage}\"); } catch(e) {}\r\n"
                  "\t\t\tloadImages(storage);\r\n"),
            false);
        patched.Replace(
            TJS_W("\t\t\tsetVisible(name, true);\r\n"),
            TJS_W("\t\t\tsetVisible(name, true);\r\n"
                  "\t\t\ttry { Debug.notice(@\"[AETHERKIRI_STAND] visible layer:${name}\"); } catch(e) {}\r\n"),
            false);
        if(patched != buffer) {
            buffer = patched;
            spdlog::info("Applied stand debug patch for StandLayer");
        }
    }
}

static void TVPExecuteTextScriptWithRecovery(const ttstr &script,
                                             tTJSVariant *result,
                                             iTJSDispatch2 *context,
                                             const ttstr &shortname) {
    try {
        TVPScriptEngine->ExecScript(script, result, context, &shortname);
        return;
    } catch(const eTJSCompileError &error) {
        ttstr recovered = script;
        if(!TJSExperimentalRecoverTrailingToken(TVPScriptEngine, script,
                                                 error.GetPosition(),
                                                 recovered))
            throw;

        spdlog::warn("Recovered trailing script token in '{}' ({} -> {} chars)",
                     shortname.AsStdString(), script.GetLen(),
                     recovered.GetLen());
        TVPScriptEngine->ExecScript(recovered, result, context, &shortname);
    } catch(const eTJSScriptError &error) {
        // Syntax errors accumulated by the lexer are reported as a script
        // error after parsing. Runtime script errors have no compile errors
        // on their block and must propagate unchanged.
        auto *block = error.GetBlockNoAddRef();
        if(!block || block->CompileErrorCount == 0)
            throw;

        ttstr recovered = script;
        if(!TJSExperimentalRecoverTrailingToken(TVPScriptEngine, script,
                                                 error.GetPosition(),
                                                 recovered))
            throw;

        spdlog::warn("Recovered trailing script token in '{}' ({} -> {} chars)",
                     shortname.AsStdString(), script.GetLen(),
                     recovered.GetLen());
        TVPScriptEngine->ExecScript(recovered, result, context, &shortname);
    }
}

const tjs_char *TVPGetD3DStandSourcePatchScript() {
    return TJS_W(
        "(function() {\r\n"
        "\tif (typeof global.D3DAffineLayer == \"undefined\") return;\r\n"
        "\tif (typeof global.__aetherKiriD3DStandOrigLoadImages != \"undefined\") return;\r\n"
        "\tglobal.__aetherKiriD3DStandOrigLoadImages = &global.D3DAffineLayer.loadImages;\r\n"
        "\tglobal.D3DAffineLayer.loadImages = function(filename, colorKey=clNone, options=void, redraw=false) {\r\n"
        "\t\tvar sourceInfo = findAffineSource(filename, options);\r\n"
        "\t\tvar sourceClass = sourceInfo.sourceClass;\r\n"
        "\t\tvar storage = sourceInfo.storage;\r\n"
        "\t\tvar ext = sourceInfo.ext;\r\n"
        "\t\tif (ext != \".STAND\" && ext != \".EVENT\") return (global.__aetherKiriD3DStandOrigLoadImages incontextof this)(filename, colorKey, options, redraw);\r\n"
        "\t\tif (sourceClass === void) return (global.__aetherKiriD3DStandOrigLoadImages incontextof this)(filename, colorKey, options, redraw);\r\n"
        "\t\tif (this._image.filename != filename ||\r\n"
        "\t\t\t(this._image instanceof \"D3DAffineSourcePicture\" && redraw) ||\r\n"
        "\t\t\t(this._image instanceof \"D3DAffineSourceImage\" && ((sourceClass === void && !redraw) || this._image._sourceClass !== sourceClass))\r\n"
        "\t\t\t) {\r\n"
        "\t\t\tinvalidate this._image;\r\n"
        "\t\t\tthis._image = new D3DAffineSourceImage(this, sourceClass);\r\n"
        "\t\t\tthis._image.filename = filename;\r\n"
        "\t\t\tthis._image.loadImages(storage, colorKey, options);\r\n"
        "\t\t\tif (options !== void) this._image.setOptions(options);\r\n"
        "\t\t\tthis.calcAffine();\r\n"
        "\t\t}\r\n"
        "\t};\r\n"
        "})();\r\n");
}

// Some layered CGs have a flat PNG with the same basename.  When the scene
// supplies layer-selection options, prefer the PIMG source so later seton
// changes redraw its layers instead of leaving the flat PNG on screen.
const tjs_char *TVPGetLayeredPimgSourcePatchScript() {
    return TJS_W(
        "(function() {\r\n"
        "\tif (typeof global.findAffineSource == \"undefined\") return;\r\n"
        "\tif (typeof global.__aetherKiriOrigFindAffineSource != \"undefined\") return;\r\n"
        "\tglobal.__aetherKiriOrigFindAffineSource = &global.findAffineSource;\r\n"
        "\tglobal.findAffineSource = function(filename, options=void) {\r\n"
        "\t\tvar sourceInfo = global.__aetherKiriOrigFindAffineSource(filename, options);\r\n"
        "\t\tif (sourceInfo === void || sourceInfo.sourceClass !== void) return sourceInfo;\r\n"
        "\t\tif (options === void || typeof global.AffineSourcePSD == \"undefined\") return sourceInfo;\r\n"
        "\t\tvar layered = false;\r\n"
        "\t\ttry {\r\n"
        "\t\t\tlayered = options.seton !== void || options.layon !== void || options.layoff !== void || options.diff !== void;\r\n"
        "\t\t} catch(e) {}\r\n"
        "\t\tif (!layered || Storages.extractStorageExt(filename) != \"\") return sourceInfo;\r\n"
        "\t\tvar pimgStorage = filename + \".pimg\";\r\n"
        "\t\tif (!Storages.isExistentStorage(pimgStorage)) return sourceInfo;\r\n"
        "\t\tsourceInfo.sourceClass = global.AffineSourcePSD;\r\n"
        "\t\tsourceInfo.storage = pimgStorage;\r\n"
        "\t\tsourceInfo.ext = \".PIMG\";\r\n"
        "\t\treturn sourceInfo;\r\n"
        "\t};\r\n"
        "})();\r\n");
}

const tjs_char *TVPGetD3DEmoteGpuBatchPatchScript() {
    return TJS_W(
        "(function() {\r\n"
        "\tif (typeof global.AffineSourceMotion == \"undefined\") return;\r\n"
        "\tvar klass = global.AffineSourceMotion;\r\n"
        "\tif (typeof klass.drawAffine == \"undefined\") return;\r\n"
        "\tif (typeof klass.__aetherKiriOrigDrawAffine != \"undefined\") return;\r\n"
        "\tklass.__aetherKiriOrigDrawAffine = &klass.drawAffine;\r\n"
        "\tklass.drawAffine = function(target, mtx, src) {\r\n"
        "\t\tvar adaptor = void;\r\n"
        "\t\tvar batchSupported = false;\r\n"
        "\t\ttry {\r\n"
        "\t\t\tif (this._useD3D && this._window !== void) {\r\n"
        "\t\t\t\tadaptor = this._window.motionD3DAdaptor;\r\n"
        "\t\t\t\tbatchSupported = adaptor !== void && typeof adaptor.beginGpuBatch != \"undefined\" && typeof adaptor.endGpuBatch != \"undefined\";\r\n"
        "\t\t\t}\r\n"
        "\t\t} catch(e) {}\r\n"
        "\t\tvar began = false;\r\n"
        "\t\tif (batchSupported) {\r\n"
        "\t\t\ttry { adaptor.beginGpuBatch(); began = true; } catch(e) {}\r\n"
        "\t\t}\r\n"
        "\t\tvar result;\r\n"
        "\t\ttry {\r\n"
        "\t\t\tresult = this.__aetherKiriOrigDrawAffine(target, mtx, src);\r\n"
        "\t\t} catch(e) {\r\n"
        "\t\t\tif (began) try { adaptor.endGpuBatch(); } catch(endError) {}\r\n"
        "\t\t\tthrow e;\r\n"
        "\t\t}\r\n"
        "\t\tif (began) try { adaptor.endGpuBatch(); } catch(endError) {}\r\n"
        "\t\treturn result;\r\n"
        "\t};\r\n"
        "})();\r\n");
}

// KAG action.tjs routes animated properties through a script helper named
// getProperty/setProperty.  In the desktop TJS implementation a function
// invocation receives an object/global proxy as `this`; probing
// target.getProperty from that helper can therefore resolve the helper itself
// through the global fallback instead of reaching a native layer property.
// That leaves MoveAction values in the script object while the actual layer
// (opacity/visible/geometry) never changes.  Preserve the original helper for
// custom action targets, but route layer-like targets through their native
// property access directly.
static const tjs_char *TVPGetActionPropertyRoutingPatchScript() {
    return TJS_W(
        "(function() {\r\n"
        "\tif (typeof global.getProperty == \"undefined\" || typeof global.setProperty == \"undefined\") return;\r\n"
        "\tif (typeof global.__aetherKiriOrigGetProperty != \"undefined\") return;\r\n"
        "\tglobal.__aetherKiriOrigGetProperty = &global.getProperty;\r\n"
        "\tglobal.__aetherKiriOrigSetProperty = &global.setProperty;\r\n"
        "\tglobal.__aetherKiriIsLayerActionTarget = function(target) {\r\n"
        "\t\ttry {\r\n"
        "\t\t\tvar type = target.type;\r\n"
        "\t\t\treturn typeof type == \"Integer\" && type >= 0 && type <= 28;\r\n"
        "\t\t} catch(e) {}\r\n"
        "\t\treturn false;\r\n"
        "\t};\r\n"
        "\tglobal.getProperty = function(target, name, a2=0, a3=0) {\r\n"
        "\t\tif (target !== void && name !== void && name.charAt(0) != \"$\" && global.__aetherKiriIsLayerActionTarget(target)) {\r\n"
        "\t\t\ttry { return target[name]; } catch(e) {}\r\n"
        "\t\t}\r\n"
        "\t\treturn (global.__aetherKiriOrigGetProperty incontextof this)(target, name, a2, a3);\r\n"
        "\t};\r\n"
        "\tglobal.setProperty = function(target, name, value, a3=0, a4=0) {\r\n"
        "\t\tif (target !== void && name !== void && name.charAt(0) != \"$\" && global.__aetherKiriIsLayerActionTarget(target)) {\r\n"
        "\t\t\ttry { target[name] = value; return; } catch(e) {}\r\n"
        "\t\t}\r\n"
        "\t\treturn (global.__aetherKiriOrigSetProperty incontextof this)(target, name, value, a3, a4);\r\n"
        "\t};\r\n"
        "})();\r\n");
}

// The game's language selector (and several save/load/option sheets) are
// constructed through PulldownPanelLayer.  Keep an opt-in trace around the
// generic panel lifecycle so a failed sheet can be distinguished from a
// missing image pack.  This is deliberately diagnostic-only; the wrappers
// return the original result and do not alter panel state.
static const tjs_char *TVPGetDialogLifecycleTracePatchScript() {
    return TJS_W(
        "(function() {\r\n"
        "\tif (typeof global.ScrollablePulldownBase != \"undefined\" &&\r\n"
        "\t\ttypeof global.ScrollablePulldownBase.__aetherKiriTraceUiloaded == \"undefined\" &&\r\n"
        "\t\ttypeof global.ScrollablePulldownBase.onUiloaded != \"undefined\") {\r\n"
        "\t\tglobal.ScrollablePulldownBase.__aetherKiriTraceUiloaded = &global.ScrollablePulldownBase.onUiloaded;\r\n"
        "\t\tglobal.ScrollablePulldownBase.onUiloaded = function(a0=void, a1=void) {\r\n"
        "\t\t\tvar ret = (global.ScrollablePulldownBase.__aetherKiriTraceUiloaded incontextof this)(a0, a1);\r\n"
        "\t\t\ttry {\r\n"
        "\t\t\t\tvar n = this.names; var d = this.dragscr;\r\n"
        "\t\t\t\tvar itemSummary = \"\";\r\n"
        "\t\t\t\tfor (var i = 0; i < 8; i++) { var key = \"item\" + i; itemSummary += key + \"=\" + (n[key] !== void ? typeof n[key] : \"missing\") + \";\"; }\r\n"
        "\t\t\t\tif (typeof global.kag != \"undefined\" && typeof global.kag.warning != \"undefined\") global.kag.warning(\"AetherKiri uiloaded name=\" + this.name + \" items=\" + (this.items !== void ? this.items.count : \"missing\") + \" dragscr=\" + (d !== void ? typeof d : \"missing\") + \" names=\" + (n !== void ? typeof n : \"missing\") + \" \" + itemSummary);\r\n"
        "\t\t\t} catch(e) {}\r\n"
        "\t\t\treturn ret;\r\n"
        "\t\t};\r\n"
        "\t}\r\n"
        "\tif (typeof global.PulldownPanelLayer != \"undefined\" &&\r\n"
        "\t\ttypeof global.PulldownPanelLayer.__aetherKiriTraceOpen == \"undefined\") {\r\n"
        "\t\tglobal.PulldownPanelLayer.__aetherKiriTraceOpen = &global.PulldownPanelLayer.open;\r\n"
        "\t\tglobal.PulldownPanelLayer.open = function(duration=void) {\r\n"
        "\t\t\tvar ret = (global.PulldownPanelLayer.__aetherKiriTraceOpen incontextof this)(duration);\r\n"
        "\t\t\ttry { if (typeof global.kag != \"undefined\" && typeof global.kag.warning != \"undefined\") global.kag.warning(\"AetherKiri panel.open name=\" + this.name + \" visible=\" + this.visible + \" enabled=\" + this.enabled + \" window=\" + typeof this.window + \" panelLayer=\" + typeof this.window.panelLayer); } catch(e) {}\r\n"
        "\t\t\treturn ret;\r\n"
        "\t\t};\r\n"
        "\t}\r\n"
        "\tif (typeof global.KAGWindow != \"undefined\" &&\r\n"
        "\t\ttypeof global.KAGWindow.__aetherKiriTraceShowPanel == \"undefined\" &&\r\n"
        "\t\ttypeof global.KAGWindow.showPanel != \"undefined\") {\r\n"
        "\t\tglobal.KAGWindow.__aetherKiriTraceShowPanel = &global.KAGWindow.showPanel;\r\n"
        "\t\tglobal.KAGWindow.showPanel = function(panel, absolute=void) {\r\n"
        "\t\t\tvar ret = (global.KAGWindow.__aetherKiriTraceShowPanel incontextof this)(panel, absolute);\r\n"
        "\t\t\ttry { if (typeof global.kag != \"undefined\" && typeof global.kag.warning != \"undefined\") global.kag.warning(\"AetherKiri showPanel panel=\" + typeof panel + \" name=\" + panel.name + \" visible=\" + panel.visible + \" enabled=\" + panel.enabled + \" current=\" + this.panelLayer.name + \" showing=\" + this.panelShowing); } catch(e) {}\r\n"
        "\t\t\treturn ret;\r\n"
        "\t\t};\r\n"
        "\t}\r\n"
        "\tif (typeof global.uiloadEntry != \"undefined\" &&\r\n"
        "\t\ttypeof global.__aetherKiriTraceUiloadEntry == \"undefined\") {\r\n"
        "\t\tglobal.__aetherKiriTraceUiloadEntry = &global.uiloadEntry;\r\n"
        "\t\tglobal.uiloadEntry = function(a0=void, a1=void, a2=void, a3=void, a4=void, a5=void, a6=void) {\r\n"
        "\t\t\ttry {\r\n"
        "\t\t\t\tvar names = (a1 !== void ? a1 : void); var result = (a2 !== void ? a2 : void); var cfg = (a3 !== void ? a3 : void); var evals = (a5 !== void ? a5 : void);\r\n"
        "\t\t\t\tvar s = \"AetherKiri uiloadEntry names=\" + (names !== void ? names.count : \"missing\") + \" item=\" + (result !== void && result.item !== void ? \"yes\" : \"no\") + \" item0=\" + (result !== void && result.item0 !== void ? \"yes\" : \"no\") + \" evals=\" + (evals !== void ? typeof evals : \"missing\") + \" evalsCount=\" + (evals !== void && evals.count !== void ? evals.count : \"na\") + \" extra=\" + (cfg !== void && cfg.extratype !== void ? typeof cfg.extratype : \"missing\");\r\n"
        "\t\t\t\tif (typeof global.kag != \"undefined\" && typeof global.kag.warning != \"undefined\") global.kag.warning(s);\r\n"
        "\t\t\t} catch(e) {}\r\n"
        "\t\t\treturn (global.__aetherKiriTraceUiloadEntry incontextof this)(a0, a1, a2, a3, a4, a5, a6);\r\n"
        "\t\t};\r\n"
        "\t}\r\n"
        "})();\r\n");
}

static void TVPApplyPostScriptCompatibilityPatches(const ttstr &shortname) {
    const ttstr lower = shortname.AsLowerCase();
    // The stand/utility scripts are bytecode in the affected title.  Keep a
    // narrowly scoped, opt-in snapshot at the point each block has finished
    // executing; this distinguishes a missing global registration from a
    // later class/prototype dispatch failure without changing script state.
    if((lower == TJS_W("utils.tjs") ||
        lower == TJS_W("standinformation.tjs") ||
        lower == TJS_W("world.tjs")) && [] {
           const char *value = std::getenv("AETHERKIRI_STAND_CONTRACT_TRACE");
           return value && *value && *value != '0';
       }()) {
        try {
            tTJSVariant snapshot;
            TVPExecuteExpression(
                TJS_W("(function(){var g=global;return \"utils=\"+typeof g.loadConfigFile+\",map=\"+typeof g.loadConfigMap+\",find=\"+typeof g.findConfigFiles+\",stand=\"+typeof g.getStandInformation+\",psd=\"+typeof g.getExistPSDImageName+\",SI=\"+typeof g.StandInformation+\",KW=\"+typeof g.KAGWorldPlugin;})()"),
                &snapshot);
            spdlog::info("AetherKiri stand contract after {}: {}",
                         shortname.AsStdString(), ttstr(snapshot).AsStdString());
        } catch(...) {
            spdlog::warn("AetherKiri stand contract snapshot failed after {}",
                         shortname.AsStdString());
        }
    }
    const bool patchWorld = lower == TJS_W("world.tjs");
    const bool patchAffineSource =
        lower == TJS_W("affinesource.tjs") ||
        lower == TJS_W("affinesourcelayer.tjs");
    const bool patchD3DLayer = lower == TJS_W("d3d.tjs");
    const bool patchD3DMotion =
        patchD3DLayer || lower == TJS_W("d3daffinesourcemotion.tjs");
    const bool patchD3DEmote =
        lower == TJS_W("motion.tjs") || lower == TJS_W("d3demote.tjs") ||
        lower == TJS_W("affinesourcemotion.tjs");
    const bool patchMessageText = lower == TJS_W("msghack.tjs");
    const bool patchQuickMenu = lower == TJS_W("quickmenu.tjs");
    const bool patchSimpleAnim = lower == TJS_W("simpleanim.tjs");
    const bool patchAction = lower == TJS_W("action.tjs");
    const bool patchDialogTrace = lower == TJS_W("dialoglayer.tjs") ||
                                  lower == TJS_W("mainwindow.tjs");
    const bool patchUiAutoTrace = lower == TJS_W("uiloader.tjs") ||
                                  lower == TJS_W("saveload.tjs") ||
                                  lower == TJS_W("uimain.tjs") ||
                                  ([] {
                                      const char *trace = std::getenv("AETHERKIRI_DIALOG_TRACE");
                                      return trace && *trace && *trace != '0';
                                  })();
    if(!patchWorld && !patchAffineSource && !patchD3DLayer && !patchD3DMotion &&
       !patchD3DEmote && !patchMessageText && !patchQuickMenu &&
       !patchSimpleAnim && !patchAction && !patchDialogTrace &&
       !patchUiAutoTrace)
        return;

    if(patchDialogTrace || patchUiAutoTrace) {
        const char *trace = std::getenv("AETHERKIRI_DIALOG_TRACE");
        if(trace && *trace && *trace != '0') {
            try {
                TVPExecuteScript(TVPGetDialogLifecycleTracePatchScript(),
                                 TJS_W("AetherKiriDialogLifecycleTrace"), 0,
                                 (tTJSVariant *)nullptr);
                spdlog::info("Applied opt-in dialog lifecycle trace");
            } catch(...) {
                spdlog::warn("Failed to apply dialog lifecycle trace");
            }
        }
    }

    // The title's simpleanim classes are script-defined and are loaded on
    // demand.  Keep an opt-in snapshot of their class dispatch after the
    // bytecode has executed; this distinguishes a missing script from a
    // broken inherited member lookup without affecting normal runs.
    if(patchSimpleAnim) {
        const char *trace = std::getenv("AETHERKIRI_SIMPLEANIM_TRACE");
        if(trace && *trace && *trace != '0') {
            try {
                tTJSVariant result;
                TVPExecuteExpression(
                    TJS_W("(function(){var a=global.GUIAnimButtonObject;"
                          "var b=global.GUIAnimButtonObjectBase;"
                          "var c=global.GUIAnimButtonForMsg;"
                          "var d=global.SystemButtonLayer;"
                          "return \"GUI=\"+typeof a+\",setup=\"+"
                          "typeof a.setup+\",getNum=\"+typeof a.getNum+"
                          "\",Base=\"+typeof b+\",BaseSetup=\"+"
                          "typeof b.setup+\",Msg=\"+typeof c+\",MsgPaint=\"+"
                          "typeof c.onPaint+\",Sys=\"+typeof d+\",SysPaint=\"+"
                          "typeof d.onPaint;})()"),
                    &result);
                spdlog::info("simpleanim dispatch snapshot: {}",
                             ttstr(result).AsStdString());
            } catch(...) {
                spdlog::warn("simpleanim dispatch snapshot failed");
            }
        }
        return;
    }

    if(patchAction) try {
        TVPExecuteScript(TVPGetActionPropertyRoutingPatchScript(),
                         TJS_W("AetherKiriActionPropertyRoutingPatch"), 0,
                         (tTJSVariant *)nullptr);
        spdlog::info("Applied compatibility hook for action layer properties");
    } catch(...) {
        spdlog::warn(
            "Failed to apply compatibility hook for action layer properties");
    }

    if(patchAffineSource) try {
        TVPExecuteScript(
            TVPGetLayeredPimgSourcePatchScript(),
            TJS_W("AetherKiriLayeredPimgSourcePatch"), 0,
            (tTJSVariant *)nullptr);
        spdlog::info(
            "Applied compatibility hook for layered PIMG source routing");
    } catch(...) {
        spdlog::warn(
            "Failed to apply compatibility hook for layered PIMG source routing");
    }

    if(patchD3DEmote) try {
        TVPExecuteScript(
            TVPGetD3DEmoteGpuBatchPatchScript(),
            TJS_W("AetherKiriD3DEmoteGpuBatchPatch"), 0,
            (tTJSVariant *)nullptr);
        spdlog::info(
            "Applied compatibility hook for D3DEmote GPU transaction batching");
    } catch(...) {
        spdlog::warn(
            "Failed to apply compatibility hook for D3DEmote GPU transaction batching");
    }

    // YuzuSoft quick menus still use the generic cursor SE when their proxied
    // window buttons have no per-control onenter script. Keep that fallback on
    // the quick-menu event so other intentionally silent window buttons stay
    // silent.
    if(patchQuickMenu) try {
        TVPExecuteScript(
            TJS_W(
                "(function() {\r\n"
                "\tif (typeof global.QuickMenuLayerBase == \"undefined\") return;\r\n"
                "\tif (typeof global.QuickMenuLayerBase.__aetherKiriOrigOnButtonEnter != \"undefined\") return;\r\n"
                "\tglobal.QuickMenuLayerBase.__aetherKiriOrigOnButtonEnter = &global.QuickMenuLayerBase.onButtonEnter;\r\n"
                "\tglobal.QuickMenuLayerBase.onButtonEnter = function(name) {\r\n"
                "\t\ttry {\r\n"
                "\t\t\tvar button = this.proxy[name].target;\r\n"
                "\t\t\tif ((button.onenter === void || button.onenter == \"\") && typeof global.playSysSE != \"undefined\") {\r\n"
                "\t\t\t\tglobal.playSysSE(\"*.enter\");\r\n"
                "\t\t\t}\r\n"
                "\t\t} catch(e) {}\r\n"
                "\t\treturn (global.QuickMenuLayerBase.__aetherKiriOrigOnButtonEnter incontextof this)(name);\r\n"
                "\t};\r\n"
                "})();\r\n"),
            TJS_W("AetherKiriQuickMenuHoverSoundPatch"), 0,
            (tTJSVariant *)nullptr);
        spdlog::info(
            "Applied compatibility hook for quick-menu hover sound fallback");
    } catch(...) {
        spdlog::warn(
            "Failed to apply compatibility hook for quick-menu hover sound fallback");
    }

    if(patchMessageText) try {
        TVPExecuteScript(
            TJS_W(
                "(function() {\r\n"
                "\tif (typeof global.EdgeShadowDrawText == \"undefined\") return;\r\n"
                "\tif (typeof global.__aetherKiriOrigEdgeShadowDrawText != \"undefined\") return;\r\n"
                "\tglobal.__aetherKiriOrigEdgeShadowDrawText = &global.EdgeShadowDrawText;\r\n"
                "\tglobal.EdgeShadowDrawText = function(dt, d, x, y, text, col, opa, aa, s, scol, sw, sx, sy, e, ecol, eemp, eext) {\r\n"
                "\t\tif (typeof e == \"Integer\" && e != 0 && e != 1 && (ecol === void || ecol == 0 || ecol == 1)) {\r\n"
                "\t\t\ttry {\r\n"
                "\t\t\t\tvar owner = global.kag.fore.messages[0];\r\n"
                "\t\t\t\tif (typeof owner != \"undefined\" && owner.edge !== void && owner.edgeColor !== void && e == owner.edgeColor && e != owner.edge) {\r\n"
                "\t\t\t\t\te = owner.edge;\r\n"
                "\t\t\t\t\tecol = owner.edgeColor;\r\n"
                "\t\t\t\t}\r\n"
                "\t\t\t} catch(ex) {}\r\n"
                "\t\t}\r\n"
                "\t\treturn (global.__aetherKiriOrigEdgeShadowDrawText incontextof this)(dt, d, x, y, text, col, opa, aa, s, scol, sw, sx, sy, e, ecol, eemp, eext);\r\n"
                "\t};\r\n"
                "})();\r\n"),
            TJS_W("AetherKiriMessageEdgeArgumentPatch"), 0,
            (tTJSVariant *)nullptr);
        spdlog::info(
            "Applied compatibility hook for message edge argument routing");
    } catch(...) {
        spdlog::warn(
            "Failed to apply compatibility hook for message edge argument routing");
    }

    if(patchWorld) try {
        TVPExecuteScript(
            TJS_W(
                "(function() {\r\n"
                "\tif (typeof global.EnvLayerObject == \"undefined\") return;\r\n"
                "\tif (typeof global.EnvLayerObject.__aetherKiriOrigCreateLayer != \"undefined\") return;\r\n"
                "\tglobal.EnvLayerObject.__aetherKiriOrigCreateLayer = &global.EnvLayerObject.createLayer;\r\n"
                "\tglobal.EnvLayerObject.createLayer = function(src=void) {\r\n"
                "\t\tvar layer = (global.EnvLayerObject.__aetherKiriOrigCreateLayer incontextof this)(src);\r\n"
                "\t\tif (layer !== void) {\r\n"
                "\t\t\ttry { layer.msgvisible = this.msgvisible; } catch(e) {}\r\n"
                "\t\t\ttry { layer.ignore = this.ignore; } catch(e) {}\r\n"
                "\t\t}\r\n"
                "\t\treturn layer;\r\n"
                "\t};\r\n"
                "})();\r\n"),
            TJS_W("AetherKiriWorldLayerClonePatch"), 0,
            (tTJSVariant *)nullptr);
        spdlog::info("Applied compatibility hook for world layer clone state");
    } catch(...) {
        spdlog::warn("Failed to apply compatibility hook for world layer clone state");
    }

    if(patchD3DLayer) try {
        TVPExecuteScript(
            TVPGetD3DStandSourcePatchScript(),
            TJS_W("AetherKiriD3DStandSourcePatch"), 0, (tTJSVariant *)nullptr);
        spdlog::info(
            "Applied compatibility hook for D3DAffineLayer stand source routing");
    } catch(...) {
        spdlog::warn(
            "Failed to apply compatibility hook for D3DAffineLayer stand source routing");
    }

    if(patchD3DMotion) try {
        TVPExecuteScript(
            TJS_W(
                "(function() {\r\n"
                "\tif (typeof global.D3DAffineSourceMotion == \"undefined\") return;\r\n"
                "\tif (typeof global.__aetherKiriD3DMotionOrigLoadImages != \"undefined\") return;\r\n"
                "\tglobal.__aetherKiriD3DMotionOrigLoadImages = &global.D3DAffineSourceMotion.loadImages;\r\n"
                "\tglobal.__aetherKiriD3DMotionOrigOnUpdate = &global.D3DAffineSourceMotion.onUpdate;\r\n"
                "\tglobal.__aetherKiriD3DMotionBindTarget = function(source) {\r\n"
                "\t\ttry {\r\n"
                "\t\t\tif (source !== void && source._player !== void && source._d3dlayer !== void) {\r\n"
                "\t\t\t\tsource._player.targetLayer = source._d3dlayer;\r\n"
                "\t\t\t}\r\n"
                "\t\t} catch(e) {}\r\n"
                "\t};\r\n"
                "\tglobal.D3DAffineSourceMotion.loadImages = function(storage, colorKey=clNone, options=void) {\r\n"
                "\t\tvar ret = (global.__aetherKiriD3DMotionOrigLoadImages incontextof this)(storage, colorKey, options);\r\n"
                "\t\tglobal.__aetherKiriD3DMotionBindTarget(this);\r\n"
                "\t\treturn ret;\r\n"
                "\t};\r\n"
                "\tglobal.D3DAffineSourceMotion.onUpdate = function(diff) {\r\n"
                "\t\tvar ret = (global.__aetherKiriD3DMotionOrigOnUpdate incontextof this)(diff);\r\n"
                "\t\tglobal.__aetherKiriD3DMotionBindTarget(this);\r\n"
                "\t\treturn ret;\r\n"
                "\t};\r\n"
                "})();\r\n"),
            TJS_W("AetherKiriD3DMotionTargetPatch"), 0,
            (tTJSVariant *)nullptr);
        spdlog::info(
            "Applied compatibility hook for D3DAffineSourceMotion target routing");
    } catch(...) {
        spdlog::warn(
            "Failed to apply compatibility hook for D3DAffineSourceMotion target routing");
    }
}
//---------------------------------------------------------------------------
void TVPExecuteStorage(const ttstr &name, iTJSDispatch2 *context,
                       tTJSVariant *result, bool isexpression,
                       const tjs_char *modestr) {
    // execute storage which contains script
    if(!TVPScriptEngine)
        TVPThrowInternalError;

    TVPStorageExecutionSerial.fetch_add(1, std::memory_order_relaxed);

    // Check if export_scripts is enabled (used by both bytecode and text paths)
    tTJSVariant exportOpt;
    bool doExport = false;
    if(TVPGetCommandLine(TJS_W("export_scripts"), &exportOpt)) {
        ttstr val = exportOpt.AsStringNoAddRef();
        doExport = (val == TJS_W("1") || val == TJS_W("true"));
    }

    { // for bytecode
        ttstr place(TVPSearchPlacedPath(name));
        ttstr shortname(TVPExtractStorageName(place));
        std::unique_ptr<tTJSBinaryStream> stream{ TVPCreateBinaryStreamForRead(
            place, modestr) };
        if(stream) {
            bool isbytecode;
            if(doExport) {
                // Snapshot raw bytes before LoadByteCode consumes the stream
                auto size = static_cast<tjs_uint>(stream->GetSize());
                auto *rawBuf = new tjs_uint8[size];
                stream->Read(rawBuf, size);

                spdlog::debug("export_scripts: loading bytecode '{}' ({} bytes)", name.AsStdString(), size);

                // Load bytecode from memory copy (for execution)
                tTVPMemoryStream memStream(rawBuf, size);
                isbytecode = TVPScriptEngine->LoadByteCode(
                    &memStream, result, context, shortname.c_str());

                if(isbytecode) {
                    // Dump disassembled script using same raw bytes
                    try {
                        spdlog::debug("export_scripts: dumping '{}'", name.AsStdString());
                        auto loader = std::make_unique<tTJSByteCodeLoader>();
                        std::unique_ptr<tTJSScriptBlock,
                                        std::function<void(tTJSScriptBlock *)>>
                            blk{ loader->ReadByteCode(TVPScriptEngine, name.c_str(),
                                                      rawBuf, size),
                                 [](auto *ptr) { ptr->Release(); } };

                        if(blk) {
                            auto tmpPlace = place.AsStdString();
                            auto pos = tmpPlace.find(".xp3>");
                            if(pos != std::string::npos) {
                                tmpPlace.replace(pos, strlen(".xp3>"), "_xp3/");
                                auto absPath = std::filesystem::path{
                                    tmpPlace.substr(strlen("file://."))};
                                spdlog::debug("export_scripts: writing to '{}'", absPath.string());
                                std::filesystem::create_directories(
                                    absPath.parent_path());
                                auto dumpStream = std::make_unique<tTVPMemoryStream>();
                                blk->Dump(dumpStream.get());
                                auto bufSize = dumpStream->GetSize();
                                std::vector<char16_t> buffer(bufSize / sizeof(char16_t));
                                dumpStream->Seek(0, TJS_BS_SEEK_SET);
                                dumpStream->Read(buffer.data(), bufSize);
#if defined(_WIN32)
                                FILE *f = _wfopen(absPath.c_str(), L"wb");
#else
                                FILE *f = fopen(absPath.c_str(), "wb");
#endif
                                if(f) {
                                    char16_t bom = 0xFEFF;
                                    fwrite(&bom, sizeof(char16_t), 1, f);
                                    fwrite(buffer.data(), sizeof(char16_t),
                                           buffer.size(), f);
                                    fclose(f);
                                    spdlog::debug("export_scripts: wrote {} chars", buffer.size());
                                }
                            }
                        } else {
                            spdlog::warn("export_scripts: ReadByteCode returned null for '{}'", name.AsStdString());
                        }
                    } catch(const std::exception &e) {
                        spdlog::error("export_scripts: exception dumping '{}': {}", name.AsStdString(), e.what());
                    } catch(...) {
                        spdlog::error("export_scripts: unknown exception dumping '{}'", name.AsStdString());
                    }
                }
                delete[] rawBuf;
            } else {
                // Normal path (no export)
                isbytecode = TVPScriptEngine->LoadByteCode(
                    stream.get(), result, context, shortname.c_str());
            }

            if(isbytecode) {
                TVPApplyPostScriptCompatibilityPatches(shortname);
                return;
            }
        }
    }

    ttstr place(TVPSearchPlacedPath(name));
    ttstr shortname(TVPExtractStorageName(place));
    std::unique_ptr<iTJSTextReadStream> stream{ TVPCreateTextStreamForRead(
        place, modestr) };
    ttstr buffer;
    stream->Read(buffer, 0);
    TVPApplyScriptCompatibilityPatches(shortname, buffer);

    // Export plain-text script when export_scripts is enabled
    if(doExport) {
        try {
            auto tmpPlace = place.AsStdString();
            auto pos = tmpPlace.find(".xp3>");
            if(pos != std::string::npos) {
                tmpPlace.replace(pos, strlen(".xp3>"), "_xp3/");
                auto absPath = std::filesystem::path{
                    tmpPlace.substr(strlen("file://."))};
                std::filesystem::create_directories(absPath.parent_path());
                std::ofstream of{absPath};
                of << buffer.AsStdString() << std::endl;
                of.close();
            }
        } catch(const std::exception &e) {
            spdlog::error("export_scripts: text export exception for '{}': {}", name.AsStdString(), e.what());
        } catch(...) {
            spdlog::error("export_scripts: text export unknown exception for '{}'", name.AsStdString());
        }
    }

    if(TVPScriptEngine) {

        if(!isexpression)
            TVPExecuteTextScriptWithRecovery(buffer, result, context, shortname);
        else
            TVPScriptEngine->EvalExpression(buffer, result, context,
                                            &shortname);
        TVPApplyPostScriptCompatibilityPatches(shortname);
    }
}

//---------------------------------------------------------------------------
void TVPCompileStorage(const ttstr &name, bool isrequestresult,
                       bool outputdebug, bool isexpression,
                       const ttstr &outputpath) {
    // execute storage which contains script
    if(!TVPScriptEngine)
        TVPThrowInternalError;

    ttstr place(TVPSearchPlacedPath(name));
    ttstr shortname(TVPExtractStorageName(place));
    iTJSTextReadStream *stream = TVPCreateTextStreamForRead(place, TJS_W(""));

    ttstr buffer;
    try {
        stream->Read(buffer, 0);
    } catch(...) {
        stream->Destruct();
        throw;
    }
    stream->Destruct();

    tTJSBinaryStream *outputstream = TVPCreateStream(outputpath, TJS_BS_WRITE);
    if(TVPScriptEngine) {
        try {
            TVPScriptEngine->CompileScript(buffer.c_str(), outputstream,
                                           isrequestresult, outputdebug,
                                           isexpression, name.c_str(), 0);
        } catch(...) {
            delete outputstream;
            throw;
        }
    }
    delete outputstream;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateMessageMapFile
//---------------------------------------------------------------------------
void TVPCreateMessageMapFile(const ttstr &filename) {
#ifdef TJS_TEXT_OUT_CRLF
    ttstr script(TJS_W("{\r\n\tvar r = System.assignMessage;\r\n"));
#else
    ttstr script(TJS_W("{\n\tvar r = System.assignMessage;\n"));
#endif

    script += TJSCreateMessageMapString();

    script += TJS_W("}");

    iTJSTextWriteStream *stream =
        TVPCreateTextStreamForWrite(filename, TJS_W(""));
    try {
        stream->Write(script);
    } catch(...) {
        stream->Destruct();
        throw;
    }

    stream->Destruct();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPDumpScriptEngine
//---------------------------------------------------------------------------
void TVPDumpScriptEngine() {
    TVPTJS2StartDump();
    TVPScriptEngine->SetConsoleOutput(TVPGetTJS2DumpOutputGateway());
    try {
        TVPScriptEngine->Dump();
    } catch(...) {
        TVPTJS2EndDump();
        TVPScriptEngine->SetConsoleOutput(TVPGetTJS2ConsoleOutputGateway());
        throw;
    }
    TVPScriptEngine->SetConsoleOutput(TVPGetTJS2ConsoleOutputGateway());
    TVPTJS2EndDump();
}
//---------------------------------------------------------------------------

bool TVPStartupSuccess = false;

void TVPOpenPatchLibUrl();

static void TVPLogStartupScriptError(const char *stage,
                                     const TJS::eTJSScriptError &e);

//---------------------------------------------------------------------------
// TVPExecuteStartupScript
//---------------------------------------------------------------------------
const tjs_char *TVPGetStartupPatchPrerequisitesScript() {
    return TJS_W(
        "if(typeof global.inSystemMenuStorages == \"undefined\") global.inSystemMenuStorages = [];\n"
        "if(typeof global.kagHookEntries == \"undefined\") global.kagHookEntries = [];\n"
        "if(typeof global.afterInitCallback == \"undefined\") global.afterInitCallback = [];\n"
        "if(typeof global.COMMAND_SYNC == \"undefined\") global.COMMAND_SYNC = 0;\n"
        "if(typeof global.COMMAND_ASYNC == \"undefined\") global.COMMAND_ASYNC = 1;\n"
        "if(typeof global.COMMAND_WAIT == \"undefined\") global.COMMAND_WAIT = 2;\n"
        "if(typeof global.kirikiriz == \"undefined\") global.kirikiriz = false;\n"
        "if(typeof global.kirikiriz_generic == \"undefined\") global.kirikiriz_generic = false;\n");
}

const tjs_char *TVPGetPatchWindowPrerequisitesScript() {
    return TJS_W(
        "if(typeof KAGWindow != \"undefined\") {\n"
        "  if(typeof KAGWindow.inSystemMenuStorages == \"undefined\") KAGWindow.inSystemMenuStorages = global.inSystemMenuStorages;\n"
        "  if(typeof KAGWindow.kagHookEntries == \"undefined\") KAGWindow.kagHookEntries = global.kagHookEntries;\n"
        "  if(typeof KAGWindow.afterInitCallback == \"undefined\") KAGWindow.afterInitCallback = global.afterInitCallback;\n"
        "  if(typeof KAGWindow.COMMAND_SYNC == \"undefined\") KAGWindow.COMMAND_SYNC = global.COMMAND_SYNC;\n"
        "  if(typeof KAGWindow.COMMAND_ASYNC == \"undefined\") KAGWindow.COMMAND_ASYNC = global.COMMAND_ASYNC;\n"
        "  if(typeof KAGWindow.COMMAND_WAIT == \"undefined\") KAGWindow.COMMAND_WAIT = global.COMMAND_WAIT;\n"
        "  if(typeof KAGWindow.kirikiriz == \"undefined\") KAGWindow.kirikiriz = global.kirikiriz;\n"
        "  if(typeof KAGWindow.kirikiriz_generic == \"undefined\") KAGWindow.kirikiriz_generic = global.kirikiriz_generic;\n"
        "}\n");
}

static void TVPInstallStartupPatchPrerequisites() {
    TVPExecuteScript(TVPGetStartupPatchPrerequisitesScript(),
        TJS_W("startup_patch_prereq.tjs"), 0,
        static_cast<tTJSVariant *>(nullptr));
}

static void TVPInstallPatchWindowPrerequisites() {
    try {
        // A title's startup script may explicitly clear compatibility
        // globals. Restore only missing members before wiring them to the
        // window class used by a late patch.
        TVPInstallStartupPatchPrerequisites();
        TVPExecuteScript(TVPGetPatchWindowPrerequisitesScript(),
            TJS_W("patch_window_prereq.tjs"), 0,
            static_cast<tTJSVariant *>(nullptr));
    } catch(const TJS::eTJSScriptError &e) {
        TVPLogStartupScriptError("Patch window prerequisites error", e);
    } catch(const TJS::eTJS &e) {
        spdlog::warn("Patch window prerequisites TJS error: {}",
                     e.GetMessage().AsStdString());
    } catch(...) {
        // Compatibility setup is optional and must never prevent a title
        // from reaching its own patch.tjs.
        spdlog::warn("Patch window prerequisites failed");
    }
}

static void TVPInstallKagRuntimeDefaults() {
    try {
        TVPExecuteScript(TJS_W(
            "if(typeof kag != \"undefined\") {\n"
            "  if(typeof kag.autoMode == \"undefined\") kag.autoMode = false;\n"
            "  if(typeof kag.skipMode == \"undefined\") kag.skipMode = 0;\n"
            "  if(typeof kag.autoModePageWait == \"undefined\") kag.autoModePageWait = 0;\n"
            "  if(typeof kag.autoModeLineWait == \"undefined\") kag.autoModeLineWait = 0;\n"
            "  if(typeof kag.userChSpeed == \"undefined\") kag.userChSpeed = 0;\n"
            "  if(typeof kag.autoModeWaitVoice == \"undefined\") kag.autoModeWaitVoice = 0;\n"
            "}\n"),
            TJS_W("kag_runtime_defaults.tjs"), 0,
            static_cast<tTJSVariant *>(nullptr));
    } catch(const TJS::eTJSScriptError &e) {
        TVPLogStartupScriptError("KAG runtime defaults patch error", e);
    } catch(const TJS::eTJS &e) {
        spdlog::warn("KAG runtime defaults patch TJS error: {}",
                     e.GetMessage().AsStdString());
    } catch(...) {
        spdlog::warn("KAG runtime defaults patch failed");
    }
}

const tjs_char *TVPGetKagLoadContractGuardScript() {
    return TJS_W(
        "if(typeof global.KAGLoadScript == \"Object\" &&\n"
        "   typeof Scripts.getStorageExecutionSerial == \"Object\" &&\n"
        "   typeof Scripts.execStorageNative == \"Object\" &&\n"
        "   typeof global.__aetherKiriOriginalKAGLoadScript == \"undefined\") {\n"
        "  global.__aetherKiriOriginalKAGLoadScript = &global.KAGLoadScript;\n"
        "  global.KAGLoadScript = function(storage) {\n"
        "    var serial = Scripts.getStorageExecutionSerial();\n"
        "    var ret = (global.__aetherKiriOriginalKAGLoadScript incontextof this)(...);\n"
        "    if(storage !== void && storage != \"\" &&\n"
        "       Scripts.getStorageExecutionSerial() == serial)\n"
        "      return Scripts.execStorageNative(...);\n"
        "    return ret;\n"
        "  } incontextof global;\n"
        "}\n");
}

const tjs_char *TVPGetPatchRuntimeRegistryExpression() {
    return TJS_W(
        "(typeof global.loadTrigger == \"Object\" && "
        "typeof global.loadTrigger.instance == \"Object\" && "
        "typeof global.loadTrigger.instance.loadHooks == \"Object\") ? "
        "global.loadTrigger.instance.loadHooks : void");
}

const tjs_char *TVPGetPatchRuntimeInstanceRecoveryScript() {
    return TJS_W(
        "if(typeof global.loadTrigger == \"Object\" && "
        "typeof global.loadTrigger.instance != \"Object\") {\n"
        "  global.loadTrigger.instance = new loadTrigger();\n"
        "}\n");
}

namespace {

class tTVPMergeObjectMembersCallback final : public tTJSDispatch {
public:
    explicit tTVPMergeObjectMembersCallback(iTJSDispatch2 *destination) :
        destination_(destination) {}

    tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                       tTJSVariant *result, tjs_int numparams,
                       tTJSVariant **param, iTJSDispatch2 *) override {
        if(numparams < 3)
            return TJS_E_BADPARAMCOUNT;

        const tjs_uint32 flags =
            static_cast<tjs_uint32>(param[1]->AsInteger());
        if(!(flags & TJS_HIDDENMEMBER)) {
            const tjs_error error = destination_->PropSetByVS(
                TJS_MEMBERENSURE | TJS_IGNOREPROP | flags,
                param[0]->AsStringNoAddRef(), param[2], destination_);
            if(TJS_FAILED(error))
                return error;
        }
        if(result)
            *result = static_cast<tjs_int>(1);
        return TJS_S_OK;
    }

private:
    iTJSDispatch2 *destination_;
};

class tTVPMergeMissingObjectMembersCallback final : public tTJSDispatch {
public:
    explicit tTVPMergeMissingObjectMembersCallback(
        iTJSDispatch2 *destination) :
        destination_(destination) {}

    tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                       tTJSVariant *result, tjs_int numparams,
                       tTJSVariant **param, iTJSDispatch2 *) override {
        if(numparams < 3)
            return TJS_E_BADPARAMCOUNT;

        const tjs_uint32 flags =
            static_cast<tjs_uint32>(param[1]->AsInteger());
        if(!(flags & TJS_HIDDENMEMBER)) {
            const ttstr name(*param[0]);
            tTJSVariant existing;
            const tjs_error get_error =
                destination_->PropGet(TJS_IGNOREPROP, name.c_str(), nullptr,
                                      &existing, destination_);
            if(get_error == TJS_E_MEMBERNOTFOUND) {
                const tjs_error set_error = destination_->PropSet(
                    TJS_MEMBERENSURE | TJS_IGNOREPROP | flags, name.c_str(),
                    nullptr, param[2], destination_);
                if(TJS_FAILED(set_error))
                    return set_error;
            } else if(TJS_FAILED(get_error)) {
                return get_error;
            }
        }
        if(result)
            *result = static_cast<tjs_int>(1);
        return TJS_S_OK;
    }

private:
    iTJSDispatch2 *destination_;
};

using tTVPGlobalCallableSnapshot =
    std::vector<std::pair<ttstr, tTJSVariant>>;

enum class tTVPGlobalCallableKind {
    None,
    Function,
    Class,
};

static tTVPGlobalCallableKind TVPGetGlobalCallableKind(
    const tTJSVariant &value) {
    if(value.Type() != tvtObject)
        return tTVPGlobalCallableKind::None;
    const tTJSVariantClosure closure = value.AsObjectClosureNoAddRef();
    if(!closure.Object)
        return tTVPGlobalCallableKind::None;
    if(closure.IsInstanceOf(0, nullptr, nullptr, TJS_W("Function"),
                            nullptr) == TJS_S_TRUE) {
        return tTVPGlobalCallableKind::Function;
    }
    if(closure.IsInstanceOf(0, nullptr, nullptr, TJS_W("Class"),
                            nullptr) == TJS_S_TRUE) {
        return tTVPGlobalCallableKind::Class;
    }
    return tTVPGlobalCallableKind::None;
}

class tTVPCollectGlobalCallablesCallback final : public tTJSDispatch {
public:
    explicit tTVPCollectGlobalCallablesCallback(
        tTVPGlobalCallableSnapshot &snapshot) :
        snapshot_(snapshot) {}

    tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                       tTJSVariant *result, tjs_int numparams,
                       tTJSVariant **param, iTJSDispatch2 *) override {
        if(numparams < 3)
            return TJS_E_BADPARAMCOUNT;

        const tjs_uint32 flags =
            static_cast<tjs_uint32>(param[1]->AsInteger());
        if(!(flags & TJS_HIDDENMEMBER) &&
           TVPGetGlobalCallableKind(*param[2]) !=
               tTVPGlobalCallableKind::None) {
            snapshot_.emplace_back(ttstr(*param[0]), *param[2]);
        }
        if(result)
            *result = static_cast<tjs_int>(1);
        return TJS_S_OK;
    }

private:
    tTVPGlobalCallableSnapshot &snapshot_;
};

static tTVPGlobalCallableSnapshot TVPCaptureGlobalCallables() {
    tTVPGlobalCallableSnapshot snapshot;
    tTJS *engine = TVPGetScriptEngine();
    iTJSDispatch2 *global =
        engine ? engine->GetGlobalNoAddRef() : nullptr;
    if(!global)
        return snapshot;

    auto *callback = new tTVPCollectGlobalCallablesCallback(snapshot);
    tTJSVariantClosure closure(callback);
    try {
        global->EnumMembers(TJS_IGNOREPROP, &closure, global);
        callback->Release();
    } catch(...) {
        callback->Release();
        throw;
    }
    return snapshot;
}

static size_t TVPRestoreMissingGlobalCallableMembers(
    const tTVPGlobalCallableSnapshot &snapshot) {
    tTJS *engine = TVPGetScriptEngine();
    iTJSDispatch2 *global =
        engine ? engine->GetGlobalNoAddRef() : nullptr;
    if(!global)
        return 0;

    size_t restored_callables = 0;
    for(const auto &[name, original] : snapshot) {
        tTJSVariant replacement;
        if(TJS_FAILED(global->PropGet(TJS_IGNOREPROP, name.c_str(), nullptr,
                                      &replacement, global)) ||
           TVPGetGlobalCallableKind(replacement) !=
               TVPGetGlobalCallableKind(original)) {
            continue;
        }

        const tTJSVariantClosure original_closure =
            original.AsObjectClosureNoAddRef();
        const tTJSVariantClosure replacement_closure =
            replacement.AsObjectClosureNoAddRef();
        if(original_closure.Object == replacement_closure.Object)
            continue;

        try {
            if(TVPMergeMissingObjectMembers(replacement_closure.Object,
                                            original_closure.Object)) {
                ++restored_callables;
            }
        } catch(const TJS::eTJS &e) {
            spdlog::debug(
                "Skipping incompatible late-patched callable '{}': {}",
                name.AsStdString(), e.GetMessage().AsStdString());
        } catch(...) {
            spdlog::debug(
                "Skipping incompatible late-patched callable '{}'",
                name.AsStdString());
        }
    }
    return restored_callables;
}

static bool TVPReadPatchRuntimeRegistry(tTJSVariant &registry) {
    registry.Clear();
    try {
        TVPExecuteExpression(TVPGetPatchRuntimeRegistryExpression(),
                             &registry);
        return registry.Type() == tvtObject &&
               registry.AsObjectNoAddRef() != nullptr;
    } catch(const TJS::eTJSScriptError &e) {
        TVPLogStartupScriptError("Patch runtime registry read error", e);
    } catch(const TJS::eTJS &e) {
        spdlog::warn("Patch runtime registry read TJS error: {}",
                     e.GetMessage().AsStdString());
    } catch(...) {
        spdlog::warn("Patch runtime registry read failed");
    }
    registry.Clear();
    return false;
}

static void TVPRecoverPatchRuntimeInstance() {
    try {
        TVPExecuteScript(TVPGetPatchRuntimeInstanceRecoveryScript(),
                         TJS_W("patch_runtime_instance_recovery.tjs"), 0,
                         static_cast<tTJSVariant *>(nullptr));
    } catch(const TJS::eTJSScriptError &e) {
        TVPLogStartupScriptError("Patch runtime instance recovery error", e);
    } catch(const TJS::eTJS &e) {
        spdlog::warn("Patch runtime instance recovery TJS error: {}",
                     e.GetMessage().AsStdString());
    } catch(...) {
        spdlog::warn("Patch runtime instance recovery failed");
    }
}

} // namespace

bool TVPMergeObjectMembers(iTJSDispatch2 *destination,
                           iTJSDispatch2 *source) {
    if(!destination || !source)
        return false;

    auto *callback = new tTVPMergeObjectMembersCallback(destination);
    tTJSVariantClosure closure(callback);
    try {
        const bool merged = TJS_SUCCEEDED(
            source->EnumMembers(TJS_IGNOREPROP, &closure, source));
        callback->Release();
        return merged;
    } catch(...) {
        callback->Release();
        throw;
    }
}

bool TVPMergeMissingObjectMembers(iTJSDispatch2 *destination,
                                  iTJSDispatch2 *source) {
    if(!destination || !source)
        return false;

    auto *callback =
        new tTVPMergeMissingObjectMembersCallback(destination);
    tTJSVariantClosure closure(callback);
    try {
        const bool merged = TJS_SUCCEEDED(
            source->EnumMembers(TJS_IGNOREPROP, &closure, source));
        callback->Release();
        return merged;
    } catch(...) {
        callback->Release();
        throw;
    }
}

static void TVPInstallKagLoadContractGuard() {
    try {
        TVPExecuteScript(TVPGetKagLoadContractGuardScript(),
            TJS_W("kag_load_contract_guard.tjs"), 0,
            static_cast<tTJSVariant *>(nullptr));
    } catch(const TJS::eTJSScriptError &e) {
        TVPLogStartupScriptError("KAG load contract guard error", e);
    } catch(const TJS::eTJS &e) {
        spdlog::warn("KAG load contract guard TJS error: {}",
                     e.GetMessage().AsStdString());
    } catch(...) {
        spdlog::warn("KAG load contract guard failed");
    }
}

static std::atomic<int> TVPKagNoTransWaitRepairFrames{0};

static bool TVPKagNoTransWaitRepairTraceEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_KAG_WAIT_REPAIR_TRACE");
        return value && *value && *value != '0';
    }();
    return enabled;
}

static void TVPInstallKagNoTransWaitRepairHelper() {
    try {
        TVPExecuteScript(TJS_W(
            "if(typeof global.AetherKiriRepairNoTransWait == \"undefined\") {\n"
            "  global.AetherKiriLastNoTransWaitRepairInfo = \"\";\n"
            "  global.AetherKiriRepairNoTransWait = function() {\n"
            "    try {\n"
            "      global.AetherKiriLastNoTransWaitRepairInfo = \"\";\n"
            "      if(typeof kag != \"Object\" || !kag || kag.conductor === void) return false;\n"
            "      var c = kag.conductor;\n"
            "      if(c.status != c.mWait || c.waitAll === void ||\n"
            "         c.waitAll.trans === void || c.pendings === void ||\n"
            "         c.pendings.count <= 0) return false;\n"
            "      var first = c.pendings[0];\n"
            "      if(first === void || first.tagname != \"envupdate\" ||\n"
            "         first.trans !== void) return false;\n"
            "      for(var i = 0; i < c.pendings.count; i++) {\n"
            "        var pending = c.pendings[i];\n"
            "        if(pending !== void && pending.tagname == \"envupdate\" &&\n"
            "           pending.trans !== void) return false;\n"
            "      }\n"
            "      var hasUpdate = first.pretrans !== void || first.update !== void ||\n"
            "         first.revpretrans !== void || first.revupdate !== void ||\n"
            "         first.stop !== void || first.wait !== void ||\n"
            "         first.msgchange !== void || first.msgoff !== void;\n"
            "      if(!hasUpdate) return false;\n"
            "      var keys = \"\";\n"
            "      if(typeof Scripts == \"Object\") keys = Scripts.getObjectKeys(first).join(\",\");\n"
            "      var updates = (first.update !== void && first.update.count !== void) ? first.update.count : \"\";\n"
            "      global.AetherKiriLastNoTransWaitRepairInfo = \"trigger pending=\" + c.pendings.count + \" update=\" + updates + \" keys=\" + keys;\n"
            "      c.trigger(\"trans\");\n"
            "      return true;\n"
            "    } catch(e) {\n"
            "      global.AetherKiriLastNoTransWaitRepairInfo = \"error=\" + e;\n"
            "      return false;\n"
            "    }\n"
            "  } incontextof global;\n"
            "}\n"),
            TJS_W("kag_notrans_wait_repair.tjs"), 0,
            static_cast<tTJSVariant *>(nullptr));
    } catch(const TJS::eTJSScriptError &e) {
        TVPLogStartupScriptError("KAG no-trans wait repair patch error", e);
    } catch(const TJS::eTJS &e) {
        spdlog::warn("KAG no-trans wait repair patch TJS error: {}",
                     e.GetMessage().AsStdString());
    } catch(...) {
        spdlog::warn("KAG no-trans wait repair patch failed");
    }
}

void TVPArmKagNoTransWaitRepair() {
    TVPKagNoTransWaitRepairFrames.store(120, std::memory_order_relaxed);
    if(TVPKagNoTransWaitRepairTraceEnabled())
        spdlog::info("KAG no-trans wait repair armed");
}

void TVPRepairKagNoTransWait() {
    const int remaining =
        TVPKagNoTransWaitRepairFrames.fetch_sub(1, std::memory_order_relaxed);
    if(remaining <= 0) {
        if(remaining < 0)
            TVPKagNoTransWaitRepairFrames.store(0, std::memory_order_relaxed);
        return;
    }

    try {
        tTJSVariant repaired(false);
        TVPExecuteExpression(
            TJS_W("(typeof global.AetherKiriRepairNoTransWait != \"undefined\") ? "
                  "global.AetherKiriRepairNoTransWait() : false"),
            &repaired);
        const bool did_repair = repaired.operator bool();
        if(TVPKagNoTransWaitRepairTraceEnabled()) {
            tTJSVariant info;
            try {
                TVPExecuteExpression(
                    TJS_W("(typeof global.AetherKiriLastNoTransWaitRepairInfo "
                          "!= \"undefined\") ? "
                          "global.AetherKiriLastNoTransWaitRepairInfo : \"\""),
                    &info);
            } catch(...) {
                info = TJS_W("");
            }
            spdlog::info("KAG no-trans wait repair tick remaining={} result={} "
                         "info={}",
                         remaining, did_repair ? "true" : "false",
                         ttstr(info).AsStdString());
        }
        if(did_repair)
            TVPKagNoTransWaitRepairFrames.store(0,
                                                std::memory_order_relaxed);
    } catch(const TJS::eTJSScriptError &e) {
        if(TVPKagNoTransWaitRepairTraceEnabled())
            spdlog::info("KAG no-trans wait repair script-error message={} "
                         "block={} line={}",
                         e.GetMessage().AsStdString(),
                         e.GetBlockName() ? ttstr(e.GetBlockName()).AsStdString()
                                          : "",
                         e.GetSourceLine());
        TVPKagNoTransWaitRepairFrames.store(0, std::memory_order_relaxed);
    } catch(const TJS::eTJS &e) {
        if(TVPKagNoTransWaitRepairTraceEnabled())
            spdlog::info("KAG no-trans wait repair tjs-error message={}",
                         e.GetMessage().AsStdString());
        TVPKagNoTransWaitRepairFrames.store(0, std::memory_order_relaxed);
    } catch(...) {
        if(TVPKagNoTransWaitRepairTraceEnabled())
            spdlog::info("KAG no-trans wait repair failed");
        TVPKagNoTransWaitRepairFrames.store(0, std::memory_order_relaxed);
    }
}

namespace {

std::atomic<int> TVPKagEnvironmentWorldResetTagBudget{0};
std::atomic<int> TVPKagEnvironmentWorldResetRepairFrames{0};
ttstr TVPKagEnvironmentWorldResetObjectName;

bool TVPKagEnvironmentWorldResetTraceEnabled() {
    static const bool enabled = [] {
        const char *value =
            std::getenv("AETHERKIRI_KAG_WORLD_RESET_REPAIR_TRACE");
        return value && *value && *value != '0';
    }();
    return enabled;
}

bool TVPGetObjectMember(iTJSDispatch2 *object, iTJSDispatch2 *objthis,
                        const tjs_char *name, tTJSVariant &value) {
    value.Clear();
    return object &&
           TJS_SUCCEEDED(object->PropGet(0, name, nullptr, &value,
                                         objthis ? objthis : object));
}

bool TVPGetObjectMember(const tTJSVariant &source, const tjs_char *name,
                        tTJSVariant &value) {
    if(source.Type() != tvtObject)
        return false;
    const tTJSVariantClosure closure = source.AsObjectClosureNoAddRef();
    return TVPGetObjectMember(closure.Object, closure.ObjThis, name, value);
}

bool TVPVariantHasObject(const tTJSVariant &value) {
    return value.Type() == tvtObject &&
           value.AsObjectClosureNoAddRef().Object != nullptr;
}

bool TVPKagEnvironmentObjectMissingInWorld(const ttstr &name) {
    tTJS *engine = TVPGetScriptEngine();
    if(!engine)
        return false;
    iTJSDispatch2 *global = engine->GetGlobalNoAddRef();
    if(!global)
        return false;

    tTJSVariant world;
    tTJSVariant environment;
    tTJSVariant definitions;
    tTJSVariant definition;
    tTJSVariant world_objects;
    tTJSVariant world_object;
    if(!TVPGetObjectMember(global, global, TJS_W("world_object"), world) ||
       !TVPVariantHasObject(world) ||
       !TVPGetObjectMember(world, TJS_W("env"), environment) ||
       !TVPVariantHasObject(environment) ||
       !TVPGetObjectMember(environment, TJS_W("objects"), definitions) ||
       !TVPVariantHasObject(definitions) ||
       !TVPGetObjectMember(definitions, name.c_str(), definition) ||
       !TVPVariantHasObject(definition) ||
       !TVPGetObjectMember(world, TJS_W("envobjects"), world_objects) ||
       !TVPVariantHasObject(world_objects)) {
        return false;
    }

    return !TVPGetObjectMember(world_objects, name.c_str(), world_object) ||
           !TVPVariantHasObject(world_object);
}

} // namespace

void TVPNotifyKagTagForEnvironmentWorldReset(const ttstr &tag_name) {
    if(tag_name == TJS_W("envclear")) {
        // envclear resets only EnvObjectWorld. Keep a bounded watch window so
        // a later command that re-addresses a retained KAGEnvironment object
        // can restore its missing renderer-side peer.
        TVPKagEnvironmentWorldResetObjectName.Clear();
        TVPKagEnvironmentWorldResetRepairFrames.store(
            0, std::memory_order_relaxed);
        TVPKagEnvironmentWorldResetTagBudget.store(
            8192, std::memory_order_relaxed);
        if(TVPKagEnvironmentWorldResetTraceEnabled())
            spdlog::info("KAG environment world-reset repair armed");
        return;
    }

    const int remaining =
        TVPKagEnvironmentWorldResetTagBudget.fetch_sub(
            1, std::memory_order_relaxed);
    if(remaining <= 0) {
        if(remaining < 0) {
            TVPKagEnvironmentWorldResetTagBudget.store(
                0, std::memory_order_relaxed);
        }
        return;
    }

    try {
        if(!TVPKagEnvironmentObjectMissingInWorld(tag_name))
            return;
        TVPKagEnvironmentWorldResetObjectName = tag_name;
        TVPKagEnvironmentWorldResetTagBudget.store(
            0, std::memory_order_relaxed);
        TVPKagEnvironmentWorldResetRepairFrames.store(
            120, std::memory_order_relaxed);
        if(TVPKagEnvironmentWorldResetTraceEnabled()) {
            spdlog::info(
                "KAG environment world-reset repair candidate object={}",
                tag_name.AsStdString());
        }
    } catch(...) {
        // A game without this KAG environment model simply keeps running.
    }
}

void TVPRepairKagEnvironmentWorldReset() {
    const int remaining =
        TVPKagEnvironmentWorldResetRepairFrames.fetch_sub(
            1, std::memory_order_relaxed);
    if(remaining <= 0) {
        if(remaining < 0) {
            TVPKagEnvironmentWorldResetRepairFrames.store(
                0, std::memory_order_relaxed);
        }
        return;
    }

    try {
        const ttstr object_name = TVPKagEnvironmentWorldResetObjectName;
        if(object_name.IsEmpty() ||
           !TVPKagEnvironmentObjectMissingInWorld(object_name)) {
            TVPKagEnvironmentWorldResetRepairFrames.store(
                0, std::memory_order_relaxed);
            return;
        }

        tTJSVariant stable(false);
        TVPExecuteExpression(
            TJS_W("typeof kag == \"Object\" && kag && kag.inStable"),
            &stable);
        if(!stable.operator bool())
            return;

        TVPExecuteExpression(TJS_W("world_object.updateAll()"),
                             static_cast<tTJSVariant *>(nullptr));
        TVPKagEnvironmentWorldResetRepairFrames.store(
            0, std::memory_order_relaxed);
        TVPKagEnvironmentWorldResetObjectName.Clear();
        if(TVPKagEnvironmentWorldResetTraceEnabled()) {
            spdlog::info(
                "KAG environment world-reset repair completed object={}",
                object_name.AsStdString());
        }
    } catch(const TJS::eTJSScriptError &e) {
        if(TVPKagEnvironmentWorldResetTraceEnabled()) {
            spdlog::info(
                "KAG environment world-reset repair script-error message={} "
                "block={} line={}",
                e.GetMessage().AsStdString(),
                e.GetBlockName() ? ttstr(e.GetBlockName()).AsStdString() : "",
                e.GetSourceLine());
        }
    } catch(const TJS::eTJS &e) {
        if(TVPKagEnvironmentWorldResetTraceEnabled()) {
            spdlog::info(
                "KAG environment world-reset repair TJS error message={}",
                e.GetMessage().AsStdString());
        }
    } catch(...) {
        if(TVPKagEnvironmentWorldResetTraceEnabled())
            spdlog::info("KAG environment world-reset repair failed");
    }
}

static void TVPLogStartupScriptError(const char *stage,
                                     const TJS::eTJSScriptError &e) {
    ttstr msg;
    msg += e.GetMessage();
    const tjs_char *pszBlockName = e.GetBlockName();
    if(pszBlockName && *pszBlockName) {
        msg += TJS_W("\n@line(");
        tjs_char tmp[34];
        msg += TJS_int_to_str(e.GetSourceLine(), tmp);
        msg += TJS_W(") ");
        msg += pszBlockName;
    }
    if(e.GetTrace().GetLen() != 0) {
        msg += TJS_W("\n");
        msg += e.GetTrace();
    }
    spdlog::error("{}:\n{}", stage, msg.AsStdString());
    spdlog::default_logger()->flush();
}

void TVPExecuteStartupScript() {
    // The engine library can open more than one game in the same host
    // process.  Do not let a previous game's Storages.setTextEncoding()
    // setting leak into the next title; its patch.tjs may select a different
    // legacy encoding again after startup.
    TVPSetDefaultReadEncoding(TJS_W("utf-8"));

    ttstr strPatchError;
    try {
        TVPInstallStartupPatchPrerequisites();
    } catch(const TJS::eTJSScriptError &e) {
        ttstr &msg = strPatchError;
        msg += e.GetMessage();
        const tjs_char *pszBlockName = e.GetBlockName();
        if(pszBlockName && *pszBlockName) {
            msg += TJS_W("\n@line(");
            tjs_char tmp[34];
            msg += TJS_int_to_str(e.GetSourceLine(), tmp);
            msg += TJS_W(") ");
            msg += pszBlockName;
        }
        msg += TJS_W("\n");
        msg += e.GetTrace();
    } catch(const TJS::eTJS &e) {
        if(!TVPSystemUninitCalled)
            strPatchError = e.GetMessage();
    } catch(const std::exception &e) {
        strPatchError = e.what();
    } catch(const char *e) {
        strPatchError = e;
    } catch(const tjs_char *e) {
        strPatchError = e;
    }

    if(!strPatchError.IsEmpty()) {
        ttstr msg =
            LocaleConfigManager::GetInstance()->GetText("startup_patch_fail");
        msg += "\n";
        msg += strPatchError;
        std::vector<ttstr> btns;
        btns.emplace_back(
            LocaleConfigManager::GetInstance()->GetText("msgbox_ok"));
        btns.emplace_back(
            LocaleConfigManager::GetInstance()->GetText("browse_patch_lib"));
        if(TVPShowSimpleMessageBox(msg, TVPGetPackageVersionString(), btns) ==
           1) {
            TVPOpenPatchLibUrl();
        }
    }

    // execute "startup.tjs"
    try {

        ttstr place(TVPSearchPlacedPath(TVPStartupScriptName));
        spdlog::info("Loading startup script: {}", place.AsStdString());
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_INFO, "krkr2",
                            "Loading startup script: %s",
                            place.AsStdString().c_str());
#endif
        TVPStartupSuccess = false;
        try {
            iTJSTextReadStream *stream = TVPCreateTextStreamForRead(place, "");
            stream->Destruct();
            TVPExecuteStorage(TVPStartupScriptName);
            TVPStartupSuccess = true;
        } catch(const TJS::eTJSScriptError &e) {
            TVPLogStartupScriptError("Startup script error", e);
            if(!TVPIsExistentStorage(TJS_W("system/Initialize.tjs"))) {
                throw;
            }
            spdlog::warn("Startup script failed; falling back to system/Initialize.tjs");
            spdlog::default_logger()->flush();
        } catch(const TJS::eTJS &e) {
            spdlog::error("Startup script TJS error: {}", e.GetMessage().AsStdString());
            spdlog::default_logger()->flush();
            if(!TVPIsExistentStorage(TJS_W("system/Initialize.tjs"))) {
                throw;
            }
            spdlog::warn("Startup script failed; falling back to system/Initialize.tjs");
            spdlog::default_logger()->flush();
        } catch(const std::exception &e) {
            spdlog::error("Startup script std::exception: {}", e.what());
            spdlog::default_logger()->flush();
            if(!TVPIsExistentStorage(TJS_W("system/Initialize.tjs"))) {
                throw;
            }
            spdlog::warn("Startup script failed; falling back to system/Initialize.tjs");
            spdlog::default_logger()->flush();
        } catch(const char *e) {
            spdlog::error("Startup script const char exception: {}", e);
            spdlog::default_logger()->flush();
            if(!TVPIsExistentStorage(TJS_W("system/Initialize.tjs"))) {
                throw;
            }
            spdlog::warn("Startup script failed; falling back to system/Initialize.tjs");
            spdlog::default_logger()->flush();
        } catch(const tjs_char *e) {
            spdlog::error("Startup script tjs_char exception: {}", ttstr(e).AsStdString());
            spdlog::default_logger()->flush();
            if(!TVPIsExistentStorage(TJS_W("system/Initialize.tjs"))) {
                throw;
            }
            spdlog::warn("Startup script failed; falling back to system/Initialize.tjs");
            spdlog::default_logger()->flush();
        } catch(...) {
            spdlog::error("Startup script unknown exception");
            spdlog::default_logger()->flush();
            if(!TVPIsExistentStorage(TJS_W("system/Initialize.tjs"))) {
                throw;
            }
            spdlog::warn("Startup script failed; falling back to system/Initialize.tjs");
            spdlog::default_logger()->flush();
        }
        if(!TVPStartupSuccess) {
            // try direct execute initialize.tjs to compatible for
            // some patch
#if defined(__ANDROID__)
            __android_log_print(ANDROID_LOG_INFO, "krkr2",
                                "Fallback startup script: system/Initialize.tjs");
#endif
            TVPExecuteStorage(TJS_W("system/Initialize.tjs"));
            TVPStartupSuccess = true;
        }
        spdlog::info("Startup script ended.");
        TVPInstallKagRuntimeDefaults();
        TVPInstallKagNoTransWaitRepairHelper();
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_INFO, "krkr2",
                            "Startup script ended successfully");
#endif
        ttstr patch = TVPGetAppPath() + "patch.tjs";
        if(TVPIsExistentStorageNoSearch(patch)) {
            TVPInstallPatchWindowPrerequisites();
            // Root-level compatibility patches run after the framework
            // startup scripts. If one replaces a global function or class,
            // retain nested helpers that the replacement did not redefine
            // while leaving every member explicitly supplied by the patch
            // intact.
            const tTVPGlobalCallableSnapshot savedGlobalCallables =
                TVPCaptureGlobalCallables();
            // A late compatibility patch can replace framework classes and
            // their singleton instances.  Preserve runtime extension hooks
            // registered by game scripts, then merge them into the new
            // instance so class replacement does not silently disable those
            // extensions.
            tTJSVariant savedRuntimeRegistry;
            const bool hasSavedRuntimeRegistry =
                TVPReadPatchRuntimeRegistry(savedRuntimeRegistry);
            try {
                TVPExecuteStorage(patch);
            } catch(...) {
            }
            const size_t restoredCallableCount =
                TVPRestoreMissingGlobalCallableMembers(
                    savedGlobalCallables);
            if(restoredCallableCount != 0) {
                spdlog::info(
                    "Restored missing members on {} late-patched functions or classes",
                    restoredCallableCount);
            }
            if(hasSavedRuntimeRegistry) {
                tTJSVariant replacementRuntimeRegistry;
                bool hasReplacementRuntimeRegistry =
                    TVPReadPatchRuntimeRegistry(replacementRuntimeRegistry);
                if(!hasReplacementRuntimeRegistry) {
                    // Some precompiled late patches register a replacement
                    // class while conditionally skipping its top-level
                    // singleton assignment. Recreate the replacement class's
                    // own instance before restoring runtime hooks.
                    TVPRecoverPatchRuntimeInstance();
                    hasReplacementRuntimeRegistry =
                        TVPReadPatchRuntimeRegistry(replacementRuntimeRegistry);
                }
                if(hasReplacementRuntimeRegistry) {
                    try {
                        if(!TVPMergeObjectMembers(
                               replacementRuntimeRegistry.AsObjectNoAddRef(),
                               savedRuntimeRegistry.AsObjectNoAddRef())) {
                            spdlog::warn(
                                "Patch runtime registry merge failed");
                        }
                    } catch(const TJS::eTJS &e) {
                        spdlog::warn(
                            "Patch runtime registry merge TJS error: {}",
                            e.GetMessage().AsStdString());
                    } catch(...) {
                        spdlog::warn(
                            "Patch runtime registry merge failed");
                    }
                }
            }
        }
        try {
            ttstr patch = TVPGetAppPath() + "AfterStartup.tjs";
            if(TVPIsExistentStorageNoSearch(patch)) {
                TVPExecuteStorage(patch);
                TVPInstallKagRuntimeDefaults();
                TVPInstallKagNoTransWaitRepairHelper();
            }
        } catch(...) {
        }
        TVPInstallKagLoadContractGuard();

    }
    TJS_CONVERT_TO_TJS_EXCEPTION
    //}
    // TVP_CATCH_AND_SHOW_SCRIPT_EXCEPTION(TJS_W("startup"))
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// unhandled exception handler related
//---------------------------------------------------------------------------
static bool TJSGetSystem_exceptionHandler_Object(tTJSVariantClosure &dest) {
    // get System.exceptionHandler
    iTJSDispatch2 *global = TVPGetScriptEngine()->GetGlobalNoAddRef();
    if(!global)
        return false;

    tTJSVariant val;
    tTJSVariant val2;
    tTJSVariantClosure clo;

    tjs_error er;
    er = global->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("System"), nullptr, &val,
                         global);
    if(TJS_FAILED(er))
        return false;

    if(val.Type() != tvtObject)
        return false;

    clo = val.AsObjectClosureNoAddRef();

    if(clo.Object == nullptr)
        return false;

    clo.PropGet(TJS_MEMBERMUSTEXIST, TJS_W("exceptionHandler"), nullptr, &val2,
                nullptr);

    if(val2.Type() != tvtObject)
        return false;

    dest = val2.AsObjectClosure();

    if(!dest.Object) {
        dest.Release();
        return false;
    }

    return true;
}

//---------------------------------------------------------------------------
bool TVPProcessUnhandledException(eTJSScriptException &e) {
    bool result;
    tTJSVariantClosure clo;
    clo.Object = clo.ObjThis = nullptr;

    try {
        // get the script engine
        tTJS *engine = TVPGetScriptEngine();
        if(!engine)
            return false; // the script engine had been shutdown

        // get System.exceptionHandler
        if(!TJSGetSystem_exceptionHandler_Object(clo))
            return false; // System.exceptionHandler cannot be
                          // retrieved

        // execute clo
        tTJSVariant obj(e.GetValue());

        tTJSVariant *pval[] = { &obj };

        tTJSVariant res;

        clo.FuncCall(0, nullptr, nullptr, &res, 1, pval, nullptr);

        result = res.operator bool();
    } catch(eTJSScriptError &e) {
        clo.Release();
        TVPShowScriptException(e);
    } catch(eTJS &e) {
        clo.Release();
        TVPShowScriptException(e);
    } catch(...) {
        clo.Release();
        throw;
    }
    clo.Release();

    return result;
}

//---------------------------------------------------------------------------
bool TVPProcessUnhandledException(eTJSScriptError &e) {
    bool result;
    tTJSVariantClosure clo;
    clo.Object = clo.ObjThis = nullptr;

    try {
        // get the script engine
        tTJS *engine = TVPGetScriptEngine();
        if(!engine)
            return false; // the script engine had been shutdown

        // get System.exceptionHandler
        if(!TJSGetSystem_exceptionHandler_Object(clo))
            return false; // System.exceptionHandler cannot be
                          // retrieved

        // execute clo
        tTJSVariant obj;
        tTJSVariant msg(e.GetMessage());
        tTJSVariant trace(e.GetTrace());
        TJSGetExceptionObject(engine, &obj, msg, &trace);

        tTJSVariant *pval[] = { &obj };

        tTJSVariant res;

        clo.FuncCall(0, nullptr, nullptr, &res, 1, pval, nullptr);

        result = res.operator bool();
    } catch(eTJSScriptError &e) {
        clo.Release();
        TVPShowScriptException(e);
    } catch(eTJS &e) {
        clo.Release();
        TVPShowScriptException(e);
    } catch(...) {
        clo.Release();
        throw;
    }
    clo.Release();

    return result;
}

//---------------------------------------------------------------------------
bool TVPProcessUnhandledException(eTJS &e) {
    bool result;
    tTJSVariantClosure clo;
    clo.Object = clo.ObjThis = nullptr;

    try {
        // get the script engine
        tTJS *engine = TVPGetScriptEngine();
        if(!engine)
            return false; // the script engine had been shutdown

        // get System.exceptionHandler
        if(!TJSGetSystem_exceptionHandler_Object(clo))
            return false; // System.exceptionHandler cannot be
                          // retrieved

        // execute clo
        tTJSVariant obj;
        tTJSVariant msg(e.GetMessage());
        TJSGetExceptionObject(engine, &obj, msg);

        tTJSVariant *pval[] = { &obj };

        tTJSVariant res;

        clo.FuncCall(0, nullptr, nullptr, &res, 1, pval, nullptr);

        result = res.operator bool();
    } catch(eTJSScriptError &e) {
        clo.Release();
        TVPShowScriptException(e);
    } catch(eTJS &e) {
        clo.Release();
        TVPShowScriptException(e);
    } catch(...) {
        clo.Release();
        throw;
    }
    clo.Release();

    return result;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void TVPStartObjectHashMap() {
    // addref ObjectHashMap if the program is being debugged.
    if(TJSEnableDebugMode)
        TJSAddRefObjectHashMap();
}

//---------------------------------------------------------------------------
// TVPBeforeProcessUnhandledException
//---------------------------------------------------------------------------
void TVPBeforeProcessUnhandledException() { TVPDumpHWException(); }
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPShowScriptException
//---------------------------------------------------------------------------
/*
        These functions display the error location, reason, etc.
        And disable the script event dispatching to avoid massive
   occurrence of errors.
*/
extern ttstr TVPGetErrorDialogTitle();

static void TVPTerminateAfterScriptException(const ttstr &reason) {
    const std::string reason_utf8 = reason.AsStdString();
    spdlog::error("TVPTerminateAfterScriptException:\n{}", reason_utf8);
    spdlog::default_logger()->flush();
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR, "krkr2",
                        "TVPTerminateAfterScriptException: %s",
                        reason_utf8.c_str());
#endif
    if(TVPHostSuppressProcessExit) {
        // Embedded host mode: avoid full synchronous teardown from inside
        // script exception handling. Mark runtime terminated and unwind.
        TVPTerminateAsync(1);
        throw EAbort(reason);
    }
    TVPTerminateSync(1);
}

//---------------------------------------------------------------------------
void TVPShowScriptException(eTJS &e) {
    TVPSetSystemEventDisabledState(true);
    TVPOnError();

    if(!TVPSystemUninitCalled) {
        ttstr errstr =
            (ttstr(TVPScriptExceptionRaised) + TJS_W("\n") + e.GetMessage());
        TVPAddLog(ttstr(TVPScriptExceptionRaised) + TJS_W("\n") +
                  e.GetMessage());
        spdlog::error("TVPShowScriptException:\n{}", errstr.AsStdString());
        spdlog::default_logger()->flush();
        TVPShowSimpleMessageBox(errstr, TVPGetErrorDialogTitle());
        // Application->MessageDlg( errstr.AsStdString(),
        // std::wstring(), mtError, mbOK );
        TVPTerminateAfterScriptException(errstr);
    }
}

//---------------------------------------------------------------------------
void TVPShowScriptException(eTJSScriptError &e) {
    TVPSetSystemEventDisabledState(true);
    TVPOnError();

    if(!TVPSystemUninitCalled) {
        ttstr errstr =
            (ttstr(TVPScriptExceptionRaised) + TJS_W("\n") + e.GetMessage());
        TVPAddLog(ttstr(TVPScriptExceptionRaised) + TJS_W("\n") +
                  e.GetMessage());
        if(e.GetTrace().GetLen() != 0)
            TVPAddLog(ttstr(TJS_W("trace : ")) + e.GetTrace());
        spdlog::error("TVPShowScriptException:\n{}", errstr.AsStdString());
        if(e.GetTrace().GetLen() != 0)
            spdlog::error("TVPShowScriptException trace:\n{}",
                          e.GetTrace().AsStdString());
        const tjs_char *scriptName = e.GetBlockName();
        spdlog::error("TVPShowScriptException source: block='{}' line={} pos={}",
                      scriptName != nullptr ? ttstr(scriptName).AsStdString()
                                            : std::string(),
                      e.GetSourceLine(), e.GetPosition());
        spdlog::default_logger()->flush();
        TVPShowSimpleMessageBox(errstr, TVPGetErrorDialogTitle());
        //	Application->MessageDlg( errstr.AsStdString(),
        // Application->GetTitle(), mtStop, mbOK );

#ifdef TVP_ENABLE_EXECUTE_AT_EXCEPTION
        auto *scriptBlock = e.GetBlockNoAddRef();
        const tjs_char *scriptName = scriptBlock ? scriptBlock->GetName()
                                                 : nullptr;
        if(scriptBlock && scriptName != nullptr && scriptName[0] != 0) {
            ttstr path(scriptName);
            try {
                ttstr newpath = TVPGetPlacedPath(path);
                if(newpath.IsEmpty()) {
                    path = TVPNormalizeStorageName(path);
                } else {
                    path = newpath;
                }
                TVPGetLocalName(path);
                std::wstring scriptPath(path.AsStdString());
                tjs_int lineno =
                    1 + scriptBlock->SrcPosToLine(e.GetPosition()) -
                    scriptBlock->GetLineOffset();

#if defined(WIN32) && defined(_DEBUG) && !defined(ENABLE_DEBUGGER)
                // デバッガ実行されている時、Visual Studio
                // で行ジャンプする時の指定をデバッグ出力に出して、break
                // で停止する
                if(::IsDebuggerPresent()) {
                    std::wstring debuglile(
                        std::wstring(L"2>") + path.AsStdString() + L"(" +
                        std::to_wstring(lineno) + L"): error :" +
                        errstr.AsStdString());
                    ::OutputDebugString(debuglile.c_str());
                    // ここで
                    // breakで停止した時、直前の出力行をダブルクリックすれば、例外箇所のスクリプトをVisual
                    // Studioで開ける
                    ::DebugBreak();
                }
#endif
                scriptPath =
                    std::wstring(L"\"") + scriptPath + std::wstring(L"\"");
                tTJSVariant val;
                if(TVPGetCommandLine(TJS_W("-exceptionexe"), &val)) {
                    ttstr exepath(val);
                    // exepath = ttstr(TJS_W("\"")) + exepath +
                    // ttstr(TJS_W("\""));
                    if(TVPGetCommandLine(TJS_W("-exceptionarg"), &val)) {
                        ttstr arg(val);
                        if(!exepath.IsEmpty() && !arg.IsEmpty()) {
                            std::wstring str(arg.AsStdString());
                            str = ApplicationSpecialPath::ReplaceStringAll(
                                str, std::wstring(L"%filepath%"), scriptPath);
                            str = ApplicationSpecialPath::ReplaceStringAll(
                                str, std::wstring(L"%line%"),
                                std::to_wstring(lineno));
                            // exepath = exepath + ttstr(str);
                            //_wsystem( exepath.c_str() );
                            arg = ttstr(str);
                            TVPAddLog(ttstr(TJS_W("(execute) ")) + exepath +
                                      ttstr(TJS_W(" ")) + arg);
                            TVPShellExecute(exepath, arg);
                        }
                    }
                }
            } catch(...) {
            }
        }
#endif
        TVPTerminateAfterScriptException(errstr);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPInitializeStartupScript
//---------------------------------------------------------------------------
void TVPInitializeStartupScript() {
    TVPStartObjectHashMap();

    TVPExecuteStartupScript();
    if(TVPTerminateOnNoWindowStartup && TVPGetWindowCount() == 0) {
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_INFO, "krkr2",
                            "TVPInitializeStartupScript: no window at startup; terminating");
#endif
        // no window is created and main window is invisible
        Application->Terminate();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Scripts
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Scripts::ClassID = -1;

namespace {

tjs_error TVPExecStorageFromScript(tTJSVariant *result, tjs_int numparams,
                                   tTJSVariant **param) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr name = *param[0];

    ttstr modestr;
    if(numparams >= 2 && param[1]->Type() != tvtVoid)
        modestr = *param[1];

    iTJSDispatch2 *context = numparams >= 3 && param[2]->Type() != tvtVoid
        ? param[2]->AsObjectNoAddRef()
        : nullptr;

    TVPExecuteStorage(name, context, result, false, modestr.c_str());
    return TJS_S_OK;
}

class tTJSObjectKeysEnumCaller : public tTJSDispatch {
public:
    explicit tTJSObjectKeysEnumCaller(iTJSDispatch2 *array) : array_(array) {}

    tjs_error FuncCall(tjs_uint32 flag, const tjs_char *membername,
                       tjs_uint32 *hint, tTJSVariant *result,
                       tjs_int numparams, tTJSVariant **param,
                       iTJSDispatch2 *objthis) override {
        if(numparams > 1) {
            tTVInteger memberflag = param[1]->AsInteger();
            if(!(memberflag & TJS_HIDDENMEMBER)) {
                static tjs_uint addhint = 0;
                array_->FuncCall(0, TJS_W("add"), &addhint, nullptr, 1,
                                 &param[0], array_);
            }
        }
        if(result) *result = true;
        return TJS_S_OK;
    }

private:
    iTJSDispatch2 *array_;
};

} // namespace

tTJSNC_Scripts::tTJSNC_Scripts() :
    inherited(TJS_W("Scripts")){
        // registration of native members

        TJS_BEGIN_NATIVE_MEMBERS(Scripts) TJS_DECL_EMPTY_FINALIZE_METHOD
            //----------------------------------------------------------------------
            TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(
                /*TJS class name*/ Scripts){ return TJS_S_OK;
}
TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Scripts)
//----------------------------------------------------------------------

//-- methods

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ execStorage) {
    return TVPExecStorageFromScript(result, numparams, param);
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ execStorage)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ execStorageNative) {
    return TVPExecStorageFromScript(result, numparams, param);
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ execStorageNative)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getStorageExecutionSerial) {
    if(result)
        *result = static_cast<tjs_int64>(TVPGetStorageExecutionSerial());
    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getStorageExecutionSerial)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ evalStorage) {
    // execute expression which stored in storage
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr name = *param[0];

    ttstr modestr;
    if(numparams >= 2 && param[1]->Type() != tvtVoid)
        modestr = *param[1];

    iTJSDispatch2 *context = numparams >= 3 && param[2]->Type() != tvtVoid
        ? param[2]->AsObjectNoAddRef()
        : nullptr;

    TVPExecuteStorage(name, context, result, true, modestr.c_str());

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ evalStorage)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(
    /*func. name*/ compileStorage) // bytecode
{
    if(numparams < 2)
        return TJS_E_BADPARAMCOUNT;

    ttstr name = *param[0];
    ttstr output = *param[1];

    bool isresult = false;
    if(numparams >= 3 && (tjs_int)*param[2]) {
        isresult = true;
    }

    bool outputdebug = false;
    if(numparams >= 4 && (tjs_int)*param[3]) {
        outputdebug = true;
    }

    bool isexpression = false;
    if(numparams >= 5 && (tjs_int)*param[4]) {
        isexpression = true;
    }
    TVPCompileStorage(name, isresult, outputdebug, isexpression, output);

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ compileStorage)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ exec) {
    // execute given string as a script
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr content = *param[0];

    ttstr name;
    tjs_int lineofs = 0;
    if(numparams >= 2 && param[1]->Type() != tvtVoid)
        name = *param[1];
    if(numparams >= 3 && param[2]->Type() != tvtVoid)
        lineofs = *param[2];

    iTJSDispatch2 *context = numparams >= 4 && param[3]->Type() != tvtVoid
        ? param[3]->AsObjectNoAddRef()
        : nullptr;

    if(TVPScriptEngine)
        TVPScriptEngine->ExecScript(content, result, context, &name, lineofs);
    else
        TVPThrowInternalError;

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ exec)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ eval) {
    // execute given string as a script
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr content = *param[0];

    ttstr name;
    tjs_int lineofs = 0;
    if(numparams >= 2 && param[1]->Type() != tvtVoid)
        name = *param[1];
    if(numparams >= 3 && param[2]->Type() != tvtVoid)
        lineofs = *param[2];

    iTJSDispatch2 *context = numparams >= 4 && param[3]->Type() != tvtVoid
        ? param[3]->AsObjectNoAddRef()
        : nullptr;

    if(TVPScriptEngine)
        TVPScriptEngine->EvalExpression(content, result, context, &name,
                                        lineofs);
    else
        TVPThrowInternalError;

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ eval)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ dump) {
    // execute given string as a script
    TVPDumpScriptEngine();

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ dump)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getTraceString) {
    // get current stack trace as string
    tjs_int limit = 0;

    if(numparams >= 1 && param[0]->Type() != tvtVoid)
        limit = *param[0];

    if(result) {
        *result = TJSGetStackTraceString(limit);
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getTraceString)
//----------------------------------------------------------------------
#ifdef TJS_DEBUG_DUMP_STRING
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ dumpStringHeap) {
    // dump all strings held by TJS2 framework
    TJSDumpStringHeap();

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ dumpStringHeap)
#endif
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/
                             setCallMissing) /* UNDOCUMENTED: subject
                                              * to change
                                              */
{
    // set to call "missing" method
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    iTJSDispatch2 *dsp = param[0]->AsObjectNoAddRef();

    if(dsp) {
        tTJSVariant missing(TJS_W("missing"));
        dsp->ClassInstanceInfo(TJS_CII_SET_MISSING, 0, &missing);
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/
                                  setCallMissing) /* UNDOCUMENTED:
                                                     subject to change
                                                   */
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/
                             getClassNames) /* UNDOCUMENTED: subject
                                             * to change
                                             */
{
    // get class name as an array, last (most end) class first.
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    iTJSDispatch2 *dsp = param[0]->AsObjectNoAddRef();

    if(dsp) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        try {
            tjs_uint num = 0;
            while(true) {
                tTJSVariant val;
                tjs_error err = dsp->ClassInstanceInfo(TJS_CII_GET, num, &val);
                if(TJS_FAILED(err))
                    break;
                array->PropSetByNum(TJS_MEMBERENSURE, num, &val, array);
                num++;
            }
            if(result)
                *result = tTJSVariant(array, array);
        } catch(...) {
            array->Release();
            throw;
        }
        array->Release();
    } else {
        return TJS_E_FAIL;
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/
                                  getClassNames) /* UNDOCUMENTED:
                                                    subject to change
                                                  */
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getObjectKeys) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    if(result) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        try {
            tTJSObjectKeysEnumCaller *caller =
                new tTJSObjectKeysEnumCaller(array);
            tTJSVariantClosure closure(caller);
            param[0]->AsObjectClosureNoAddRef().EnumMembers(
                TJS_IGNOREPROP | TJS_ENUM_NO_VALUE, &closure, nullptr);
            caller->Release();

            static tjs_uint sorthint = 0;
            array->FuncCall(0, TJS_W("sort"), &sorthint, nullptr, 0, nullptr,
                            array);
            *result = tTJSVariant(array, array);
        } catch(...) {
            array->Release();
            throw;
        }
        array->Release();
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getObjectKeys)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getObjectCount) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    if(result) {
        tjs_int count = 0;
        param[0]->AsObjectClosureNoAddRef().GetCount(&count, nullptr, nullptr,
                                                     nullptr);
        *result = count;
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getObjectCount)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(textEncoding){
    TJS_BEGIN_NATIVE_PROP_GETTER{ *result = TVPGetDefaultReadEncoding();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TVPSetDefaultReadEncoding(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(textEncoding)
//----------------------------------------------------------------------

TJS_END_NATIVE_MEMBERS
}

//---------------------------------------------------------------------------
tTJSNativeInstance *tTJSNC_Scripts::CreateNativeInstance() {
    // this class cannot create an instance
    TVPThrowExceptionMessage(TVPCannotCreateInstance);

    return nullptr;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateNativeClass_Scripts
//---------------------------------------------------------------------------
tTJSNativeClass *TVPCreateNativeClass_Scripts() {
    auto *cls = new tTJSNC_Scripts();

    // setup some platform-specific members

    //----------------------------------------------------------------------

    // currently none

    //----------------------------------------------------------------------
    return cls;
}
//---------------------------------------------------------------------------
