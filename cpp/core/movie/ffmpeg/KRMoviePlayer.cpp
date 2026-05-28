#include <thread>

extern "C" {
#include "libswscale/swscale.h"
}

#include "KRMoviePlayer.h"
#include "VideoCodec.h"
#include "CodecUtils.h"
#include "AudioDevice.h"
#include "WaveMixer.h"
#include "WindowImpl.h"
#include "VideoOvlImpl.h"
#include "LayerIntf.h"
#include <algorithm>
#include <cstdlib>

extern std::thread::id TVPMainThreadID;

NS_KRMOVIE_BEGIN

static inline uint8_t ClampByte(int value) {
    if(value < 0)
        return 0;
    if(value > 255)
        return 255;
    return static_cast<uint8_t>(value);
}

static void ConvertYuv420ToRgba(const DVDVideoPicture &pic, uint8_t *dst,
                                int dstWidth, int dstHeight,
                                int dstStride) {
    const int copyWidth = std::min<int>(dstWidth, pic.iWidth);
    const int copyHeight = std::min<int>(dstHeight, pic.iHeight);
    for(int y = 0; y < copyHeight; ++y) {
        const uint8_t *yRow = pic.data[0] + y * pic.iLineSize[0];
        const uint8_t *uRow = pic.data[1] + (y / 2) * pic.iLineSize[1];
        const uint8_t *vRow = pic.data[2] + (y / 2) * pic.iLineSize[2];
        uint8_t *out = dst + static_cast<size_t>(y) * dstStride;
        for(int x = 0; x < copyWidth; ++x) {
            int c = static_cast<int>(yRow[x]) - 16;
            int d = static_cast<int>(uRow[x / 2]) - 128;
            int e = static_cast<int>(vRow[x / 2]) - 128;
            if(c < 0)
                c = 0;
            out[x * 4 + 0] = ClampByte((298 * c + 409 * e + 128) >> 8);
            out[x * 4 + 1] =
                ClampByte((298 * c - 100 * d - 208 * e + 128) >> 8);
            out[x * 4 + 2] = ClampByte((298 * c + 516 * d + 128) >> 8);
            out[x * 4 + 3] = 0xff;
        }
    }
}

static double VideoSubmitMaxFps() {
    const char *value = std::getenv("AETHERKIRI_GODOT_VIDEO_SUBMIT_FPS");
    if(value == nullptr || value[0] == '\0')
        return 45.0;
    char *end = nullptr;
    double parsed = std::strtod(value, &end);
    if(end == value)
        return 45.0;
    return parsed;
}

TVPMoviePlayer::TVPMoviePlayer() {
    m_pPlayer = new BasePlayer(this);
}

TVPMoviePlayer::~TVPMoviePlayer() {
    delete m_pPlayer;
    if(img_convert_ctx)
        sws_freeContext(img_convert_ctx), img_convert_ctx = nullptr;
}

void TVPMoviePlayer::Release() {
    if(RefCount == 1)
        delete this;
    else
        RefCount--;
}

void TVPMoviePlayer::SetPosition(uint64_t tick) { m_pPlayer->SeekTime(tick); }

void TVPMoviePlayer::GetPosition(uint64_t *tick) {
    if(tick)
        *tick = m_pPlayer->GetTime();
}

void TVPMoviePlayer::GetStatus(tTVPVideoStatus *status) {
    if(m_pPlayer->IsStop())
        *status = vsStopped;
    else if(m_pPlayer->GetSpeed() == 0)
        *status = vsPaused;
    else
        *status = vsPlaying;
    //	else *status = vsProcessing;
}

void TVPMoviePlayer::Rewind() { SetPosition(0); }

void TVPMoviePlayer::SetFrame(int f) {
    // TODO seek accurately
    m_pPlayer->SeekTime(f / m_pPlayer->GetFPS() * DVD_PLAYSPEED_NORMAL);
}

void TVPMoviePlayer::GetFrame(int *f) { *f = m_pPlayer->GetCurrentFrame(); }

void TVPMoviePlayer::GetFPS(double *f) { *f = m_pPlayer->GetFPS(); }

void TVPMoviePlayer::GetNumberOfFrame(int *f) {
    *f = m_pPlayer->GetTotalTime() * m_pPlayer->GetFPS() / DVD_PLAYSPEED_NORMAL;
}

void TVPMoviePlayer::GetTotalTime(int64_t *t) {
    *t = m_pPlayer->GetTotalTime();
}

void TVPMoviePlayer::GetVideoSize(long *width, long *height) {
    m_pPlayer->GetVideoSize(width, height);
}

void TVPMoviePlayer::SetPlayRate(double rate) { m_pPlayer->SetSpeed(rate); }

void TVPMoviePlayer::GetPlayRate(double *rate) {
    *rate = m_pPlayer->GetSpeed();
}

iTVPSoundBuffer *TVPMoviePlayer::GetSoundDevice() {
    IDVDStreamPlayerAudio *audioplayer = m_pPlayer->GetAudioPlayer();
    if(!audioplayer)
        return nullptr;
    IAEStream *audiostream = audioplayer->GetOutputDevice()->m_pAudioStream;
    if(!audiostream)
        return nullptr;
    return audiostream->GetNativeImpl();
}

void TVPMoviePlayer::GetAudioBalance(long *balance) {
    iTVPSoundBuffer *alsound = GetSoundDevice();
    if(alsound) {
        *balance = alsound->GetPan() * 100000;
    }
}

void TVPMoviePlayer::SetAudioBalance(long balance) {
    iTVPSoundBuffer *alsound = GetSoundDevice();
    if(alsound) {
        alsound->SetPan(balance / 100000.0f);
    }
}

void TVPMoviePlayer::SetAudioVolume(long volume) {
    iTVPSoundBuffer *alsound = GetSoundDevice();
    if(alsound)
        alsound->SetVolume(volume / 100000.f);
}

void TVPMoviePlayer::GetAudioVolume(long *volume) {
    iTVPSoundBuffer *alsound = GetSoundDevice();
    if(alsound)
        *volume = alsound->GetVolume() * 100000;
}

void TVPMoviePlayer::GetNumberOfAudioStream(unsigned long *streamCount) {
    *streamCount = m_pPlayer->GetAudioStreamCount();
}

void TVPMoviePlayer::SelectAudioStream(unsigned long iStream) {
    m_pPlayer->GetMessageQueue().Put(new CDVDMsgPlayerSetAudioStream(iStream));
    m_pPlayer->SynchronizeDemuxer();
}

void TVPMoviePlayer::GetEnableAudioStreamNum(long *num) {
    *num = m_pPlayer->GetAudioStream();
}

void TVPMoviePlayer::DisableAudioStream() {
    // TODO
}

void TVPMoviePlayer::GetNumberOfVideoStream(unsigned long *streamCount) {
    *streamCount = m_pPlayer->GetVideoStreamCount();
}

void TVPMoviePlayer::SelectVideoStream(unsigned long iStream) {
    m_pPlayer->GetMessageQueue().Put(new CDVDMsgPlayerSetVideoStream(iStream));
    m_pPlayer->SynchronizeDemuxer();
}

void TVPMoviePlayer::GetEnableVideoStreamNum(long *num) {
    *num = m_pPlayer->GetVideoStream();
}

int TVPMoviePlayer::WaitForBuffer(volatile std::atomic_bool &bStop,
                                  int timeout) {
    int remainBuf = MAX_BUFFER_COUNT - m_usedPicture;
    if(remainBuf > 0)
        return remainBuf;
    std::unique_lock<std::mutex> lk(m_mtxPicture);
    while(!bStop && MAX_BUFFER_COUNT <= m_usedPicture && timeout > 0) {
        timeout -= 10;
        m_condPicture.wait_for(lk, std::chrono::milliseconds(10));
    }
    return MAX_BUFFER_COUNT - m_usedPicture - 1;
}

void TVPMoviePlayer::Flush() {
    std::unique_lock<std::mutex> lk(m_mtxPicture);
    for(int i = 0; i < MAX_BUFFER_COUNT; ++i) {
        m_picture[i].Clear();
    }
    m_curpts = 0.0;
    m_usedPicture = 0;
    m_lastQueuedPicturePts = -1.0;
}

void TVPMoviePlayer::FrameMove() { m_pPlayer->FrameMove(); }

void TVPMoviePlayer::SetLoopSegement(int beginFrame, int endFrame) {
    m_pPlayer->SetLoopSegement(beginFrame, endFrame);
}

int TVPMoviePlayer::AddVideoPicture(DVDVideoPicture &pic, int index) {
    // from other thread
    if(pic.format != RENDER_FMT_YUV420P)
        return -2;
    if(pic.pts == DVD_NOPTS_VALUE)
        return 0;

    const double pts = pic.pts / DVD_TIME_BASE;
    const double maxSubmitFps = VideoSubmitMaxFps();
    if(maxSubmitFps > 0.0 && m_lastQueuedPicturePts >= 0.0 &&
       pts - m_lastQueuedPicturePts < (1.0 / maxSubmitFps)) {
        return MAX_BUFFER_COUNT - m_usedPicture;
    }

    if(m_usedPicture >= MAX_BUFFER_COUNT) {
        std::unique_lock<std::mutex> lk(m_mtxPicture);
        m_condPicture.wait(lk);
    }
    if(m_usedPicture >= MAX_BUFFER_COUNT)
        return -1;

    int srcWidth = pic.iWidth;
    int srcHeight = pic.iHeight;
    int width = pic.iDisplayWidth > 0 ? pic.iDisplayWidth : pic.iWidth;
    int height = pic.iDisplayHeight > 0 ? pic.iDisplayHeight : pic.iHeight;
    uint8_t *data = (uint8_t *)TJSAlignedAlloc(width * height * 4, 4);
    uint8_t *dstData[4] = {data, nullptr, nullptr, nullptr};
    int dstLineSize[4] = {width * 4, 0, 0, 0};

    img_convert_ctx = sws_getCachedContext(
        img_convert_ctx, srcWidth, srcHeight, AV_PIX_FMT_YUV420P, width, height,
        AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    assert(img_convert_ctx);
    int processed = sws_scale(img_convert_ctx, pic.data, pic.iLineSize, 0,
                              srcHeight, dstData, dstLineSize);
    if(processed <= 0) {
        ConvertYuv420ToRgba(pic, data, width, height, dstLineSize[0]);
    }

    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        BitmapPicture &picbuf =
            m_picture[(m_curPicture + m_usedPicture) & (MAX_BUFFER_COUNT - 1)];
        picbuf.Clear();
        picbuf.width = width;
        picbuf.height = height;
        picbuf.rgba = data;
        picbuf.pts = pts;
        ++m_usedPicture;
        m_lastQueuedPicturePts = pts;
        return MAX_BUFFER_COUNT - m_usedPicture;
    }

    // 	const static std::string sckey("present");
    // 	m_pRootNode->scheduleOnce(std::bind(&PlayerOverlay::PresentPicture,
    // this, std::placeholders::_1), 0, sckey);
}

VideoPresentOverlay::~VideoPresentOverlay() {
    TVPRemoveContinuousEventHook(this);
    ClearNode();
}

void VideoPresentOverlay::ClearNode() {
    // Overlay lifecycle is managed by Application host.
    m_pRootNode = nullptr;
    m_pSprite = nullptr;
    if(m_frameBitmap) {
        delete m_frameBitmap;
        m_frameBitmap = nullptr;
    }
}

// Replace VideoPresentOverlay::PresentPicture with stub
void VideoPresentOverlay::PresentPicture(float dt) {
    BitmapPicture pic;
    m_curpts = m_pPlayer->GetClock() / DVD_TIME_BASE;
    {
        std::unique_lock<std::mutex> lk(m_mtxPicture);
        if(m_usedPicture <= 0)
            return;
        do {
            pic.MoveFrom(m_picture[m_curPicture]);
            --m_usedPicture;
            if(++m_curPicture >= MAX_BUFFER_COUNT)
                m_curPicture = 0;
        } while(m_usedPicture > 0 && m_curpts >= m_picture[m_curPicture].pts);
        assert(m_usedPicture >= 0);
        m_condPicture.notify_all();
    }
    FrameMove();
    if(!pic.rgba) {
        return;
    }
    if(!Visible) {
        return;
    }

    tTJSNI_Window *window = nullptr;
    if(auto *overlay = dynamic_cast<MoviePlayerOverlay *>(this)) {
        window = overlay->GetOwnerWindow();
    }
    if(!window || !window->GetDrawDevice()) {
        return;
    }
    tTJSNI_BaseLayer *primary = window->GetDrawDevice()->GetPrimaryLayer();
    if(!primary || !primary->GetMainImage()) {
        return;
    }

    if(!m_frameBitmap || m_frameBitmap->GetWidth() != pic.width ||
       m_frameBitmap->GetHeight() != pic.height) {
        if(m_frameBitmap)
            delete m_frameBitmap;
        m_frameBitmap = new tTVPBaseTexture(pic.width, pic.height, 32);
    }
    m_frameBitmap->Update(pic.rgba, pic.width * 4, 0, 0, pic.width,
                          pic.height);

    tTVPBaseTexture *target = primary->GetMainImage();
    tTVPRect dest = GetBounds();
    if(dest.get_width() <= 0 || dest.get_height() <= 0) {
        dest = tTVPRect(0, 0, target->GetWidth(), target->GetHeight());
    }
    tTVPRect clip(0, 0, target->GetWidth(), target->GetHeight());
    tTVPRect src(0, 0, pic.width, pic.height);
    target->StretchBlt(clip, dest, m_frameBitmap, src, bmCopy, 255, false,
                       stLinear);
    primary->Update(dest);
}

void VideoPresentOverlay::OnContinuousCallback(tjs_uint64 tick) {
    if(!m_usedPicture)
        return;
    double curpts = m_pPlayer->GetClock() / DVD_TIME_BASE;
    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        BitmapPicture &picbuf = m_picture[m_curPicture];
        if(picbuf.pts > curpts)
            return;
    }
    PresentPicture(0.0f);
}

void KRMovie::VideoPresentOverlay::Play() {
    TVPMoviePlayer::Play();
    TVPAddContinuousEventHook(this);
}

void KRMovie::VideoPresentOverlay::Stop() {
    TVPRemoveContinuousEventHook(this);
    TVPMoviePlayer::Stop();
}

MoviePlayerOverlay::~MoviePlayerOverlay() {
    assert(std::this_thread::get_id() == TVPMainThreadID);
    delete m_pPlayer;
    m_pPlayer = nullptr;
}

// Replace MoviePlayerOverlay::SetWindow with stub
void MoviePlayerOverlay::SetWindow(tTJSNI_Window *window) {
    ClearNode();
    m_pOwnerWindow = window;
}

void MoviePlayerOverlay::BuildGraph(tTJSNI_VideoOverlay *callbackwin,
                                    IStream *stream, const tjs_char *streamname,
                                    const tjs_char *type, uint64_t size) {
    m_pCallbackWin = callbackwin;
    m_pPlayer->SetCallback([this](auto &&PH1, auto &&PH2) {
        OnPlayEvent(std::forward<decltype(PH1)>(PH1),
                    std::forward<decltype(PH2)>(PH2));
    });
    m_pPlayer->OpenFromStream(stream, streamname, type, size);
}

const tTVPRect &MoviePlayerOverlay::GetBounds() {
    return m_pCallbackWin->GetBounds();
}

void KRMovie::MoviePlayerOverlay::SetVisible(bool b) {
    VideoPresentOverlay::SetVisible(b);
}

void MoviePlayerOverlay::OnPlayEvent(KRMovieEvent msg, void *p) {
    if(msg == KRMovieEvent::Update) {
        PresentPicture(0.0f);
        if(m_pCallbackWin) {
            int frame;
            GetFrame(&frame);
            NativeEvent ev(WM_GRAPHNOTIFY);
            ev.WParam = EC_UPDATE;
            ev.LParam = frame;
            m_pCallbackWin->PostEvent(ev);
        }
    } else if(msg == KRMovieEvent::Ended) {
        NativeEvent ev(WM_GRAPHNOTIFY);
        ev.WParam = EC_COMPLETE;
        ev.LParam = 0;
        m_pCallbackWin->PostEvent(ev);
    }
}

void TVPMoviePlayer::BitmapPicture::MoveFrom(BitmapPicture &source) {
    if(this == &source)
        return;
    Clear();
    fmt = source.fmt;
    width = source.width;
    height = source.height;
    pts = source.pts;
    for(int i = 0; i < sizeof(data) / sizeof(data[0]); ++i) {
        data[i] = source.data[i];
        source.data[i] = nullptr;
    }
    source.fmt = RENDER_FMT_NONE;
    source.width = 0;
    source.height = 0;
    source.pts = 0.0;
}

void TVPMoviePlayer::BitmapPicture::Clear() {
    for(int i = 0; i < sizeof(data) / sizeof(data[0]); ++i) {
        if(data[i])
            TJSAlignedDealloc(data[i]), data[i] = nullptr;
    }
    fmt = RENDER_FMT_NONE;
    width = 0;
    height = 0;
    pts = 0.0;
}

void VideoPresentOverlay2::SetRootNode(OverlayNode *node) {
    ClearNode();
    m_pRootNode = node;
}

VideoPresentOverlay2 *VideoPresentOverlay2::create() {
    return new VideoPresentOverlay2;
}

NS_KRMOVIE_END
