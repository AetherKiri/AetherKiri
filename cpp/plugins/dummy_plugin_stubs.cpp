#include "ncbind.hpp"

// Stub modules — register empty entries so Plugins.link() succeeds.
// The engine already has built-in support for the functionality these
// plugins originally provided, but some games explicitly link them by name.

#define NCB_MODULE_NAME TJS_W("k2compat.dll")
static void k2compat_stub() {}
NCB_PRE_REGIST_CALLBACK(k2compat_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("kagexopt.dll")
static void kagexopt_stub() {}
NCB_PRE_REGIST_CALLBACK(kagexopt_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("krkrsteam.dll")
static void krkrsteam_stub() {}
NCB_PRE_REGIST_CALLBACK(krkrsteam_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("krmovie.dll")
static void krmovie_stub() {}
NCB_PRE_REGIST_CALLBACK(krmovie_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("kztouch.dll")
static void kztouch_stub() {}
NCB_PRE_REGIST_CALLBACK(kztouch_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("lzfs.dll")
static void lzfs_stub() {}
NCB_PRE_REGIST_CALLBACK(lzfs_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("win32ole.dll")
static void win32ole_stub() {}
NCB_PRE_REGIST_CALLBACK(win32ole_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExSubImage.dll")
static void layerExSubImage_stub() {}
NCB_PRE_REGIST_CALLBACK(layerExSubImage_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("clipboardEx.dll")
static void clipboardEx_stub() {}
NCB_PRE_REGIST_CALLBACK(clipboardEx_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("shellExecute.dll")
static void shellExecute_stub() {}
NCB_PRE_REGIST_CALLBACK(shellExecute_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("process.dll")
static void process_stub() {}
NCB_PRE_REGIST_CALLBACK(process_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("tasktray.dll")
static void tasktray_stub() {}
NCB_PRE_REGIST_CALLBACK(tasktray_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("adjustMonitor.dll")
static void adjustMonitor_stub() {}
NCB_PRE_REGIST_CALLBACK(adjustMonitor_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("fpslimit.dll")
static void fpslimit_stub() {}
NCB_PRE_REGIST_CALLBACK(fpslimit_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("systemEx.dll")
static void systemEx_stub() {}
NCB_PRE_REGIST_CALLBACK(systemEx_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("gfxEffect.dll")
class gfxFire {
public:
    gfxFire() = default;
};
NCB_REGISTER_CLASS(gfxFire) {
    Constructor();
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("flashPlayer.dll")
class FlashPlayer {
public:
    FlashPlayer() = default;
    FlashPlayer(tjs_int, tjs_int) {}

    void loadMovie(tjs_int, const tjs_char *) {}
    void tGotoFrame(tjs_int) {}
    void tGotoLabel(const tjs_char *) {}
    tjs_int tCurrentFrame() const { return 0; }
    ttstr tCurrentLabel() const { return ttstr(); }
    void tPlay() { playing_ = true; }
    void tStopPlay() { playing_ = false; }
    void setVariable(const tjs_char *, const tjs_char *) {}
    ttstr getVariable(const tjs_char *) const { return ttstr(); }
    void tSetProperty(const tjs_char *, tjs_int) {}
    ttstr tGetProperty(const tjs_char *) const { return ttstr(); }
    void tCallFrame(tjs_int) {}
    void tCallLabel(const tjs_char *) {}
    void tSetPropertyNum(const tjs_char *, tjs_int) {}
    tjs_int tGetPropertyNum(const tjs_char *) const { return 0; }
    void enforceLocalSecurity() {}
    void disableLocalSecurity() {}

    tjs_int getReadyState() const { return 0; }
    tjs_int getTotalFrames() const { return 0; }
    bool getPlaying() const { return playing_; }
    void setPlaying(bool value) { playing_ = value; }
    tjs_int getQuality() const { return quality_; }
    void setQuality(tjs_int value) { quality_ = value; }
    tjs_int getScaleMode() const { return scaleMode_; }
    void setScaleMode(tjs_int value) { scaleMode_ = value; }
    tjs_int getAlignMode() const { return alignMode_; }
    void setAlignMode(tjs_int value) { alignMode_ = value; }
    ttstr getMovie() const { return movie_; }
    void setMovie(const tjs_char *value) { movie_ = value ? value : TJS_W(""); }
    ttstr getWMode() const { return wmode_; }
    void setWMode(const tjs_char *value) { wmode_ = value ? value : TJS_W(""); }
    ttstr getFlashVars() const { return flashVars_; }
    void setFlashVars(const tjs_char *value) {
        flashVars_ = value ? value : TJS_W("");
    }

private:
    bool playing_ = false;
    tjs_int quality_ = 0;
    tjs_int scaleMode_ = 0;
    tjs_int alignMode_ = 0;
    ttstr movie_;
    ttstr wmode_;
    ttstr flashVars_;
};

NCB_REGISTER_CLASS(FlashPlayer) {
    Constructor();
    NCB_CONSTRUCTOR((tjs_int, tjs_int));

    NCB_PROPERTY_RO(readyState, getReadyState);
    NCB_PROPERTY_RO(totalFrames, getTotalFrames);
    NCB_PROPERTY(playing, getPlaying, setPlaying);
    NCB_PROPERTY(quality, getQuality, setQuality);
    NCB_PROPERTY(scaleMode, getScaleMode, setScaleMode);
    NCB_PROPERTY(alignMode, getAlignMode, setAlignMode);
    NCB_PROPERTY(movie, getMovie, setMovie);
    NCB_PROPERTY(wMode, getWMode, setWMode);
    NCB_PROPERTY(flashVars, getFlashVars, setFlashVars);

    NCB_METHOD(loadMovie);
    NCB_METHOD(tGotoFrame);
    NCB_METHOD(tGotoLabel);
    NCB_METHOD(tCurrentFrame);
    NCB_METHOD(tCurrentLabel);
    NCB_METHOD(tPlay);
    NCB_METHOD(tStopPlay);
    NCB_METHOD(setVariable);
    NCB_METHOD(getVariable);
    NCB_METHOD(tSetProperty);
    NCB_METHOD(tGetProperty);
    NCB_METHOD(tCallFrame);
    NCB_METHOD(tCallLabel);
    NCB_METHOD(tSetPropertyNum);
    NCB_METHOD(tGetPropertyNum);
    NCB_METHOD(enforceLocalSecurity);
    NCB_METHOD(disableLocalSecurity);
}
