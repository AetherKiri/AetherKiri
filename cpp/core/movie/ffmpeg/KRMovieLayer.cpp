#include "KRMovieLayer.h"
#include "VideoCodec.h"
#include "LayerBitmapIntf.h"
#include "Application.h"
#include "VideoOvlImpl.h"
#include <algorithm>
#include <cstdlib>

extern "C" {
#include "libswscale/swscale.h"
}

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

VideoPresentLayer::~VideoPresentLayer() {
    if(m_continuousHookRegistered)
        TVPRemoveContinuousEventHook(this);
}

tTVPBaseTexture *VideoPresentLayer::GetFrontBuffer() {
    BitmapPicture pic;
    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        if(!m_usedPicture) {
            return nullptr;
        }
        BitmapPicture &picbuf = m_picture[m_curPicture];
        pic.MoveFrom(picbuf);
        m_curPicture = (m_curPicture + 1) & (MAX_BUFFER_COUNT - 1);
        --m_usedPicture;
        assert(m_usedPicture >= 0);
        m_condPicture.notify_all();
    }
    FrameMove();
    if(!pic.data[0] || pic.width <= 0 || pic.height <= 0 ||
       !m_BmpBits[0] || !m_BmpBits[1]) {
        return nullptr;
    }
    int n = m_nCurBmpBuff;
    m_nCurBmpBuff = !m_nCurBmpBuff;
    m_BmpBits[n]->Update(pic.data[0], pic.width * 4, 0, 0, pic.width,
                         pic.height);
    return m_BmpBits[n];
}

void VideoPresentLayer::SetVideoBuffer(tTVPBaseTexture *buff1,
                                       tTVPBaseTexture *buff2, long size) {
    m_BmpBits[0] = buff1;
    m_BmpBits[1] = buff2;
    m_nCurBmpBuff = 0;
    m_lastQueuedPicturePts = -1.0;
    if(!m_continuousHookRegistered) {
        TVPAddContinuousEventHook(this);
        m_continuousHookRegistered = true;
    }
}

void VideoPresentLayer::OnContinuousCallback(tjs_uint64 tick) {
    if(!m_usedPicture)
        return;
    double m_curpts = m_pPlayer->GetClock() / DVD_TIME_BASE;
    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        BitmapPicture &picbuf = m_picture[m_curPicture];
        // check pts
        if(picbuf.pts > m_curpts) { // present in future
            return;
        }
    }
#if 0
        do { // skip frame
            pic.Clear();
            picbuf.swap(pic);
            m_curPicture = (m_curPicture + 1) & (MAX_BUFFER_COUNT - 1);
            --m_usedPicture;
        } while (m_usedPicture > 0 && m_curpts >= m_picture[m_curPicture].pts);
        assert(m_usedPicture >= 0);
#endif
    OnPlayEvent(KRMovieEvent::Update, nullptr);
}

int VideoPresentLayer::AddVideoPicture(DVDVideoPicture &pic, int index) {
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
    int width = m_BmpBits[0] ? m_BmpBits[0]->GetWidth()
                             : (pic.iDisplayWidth > 0 ? pic.iDisplayWidth
                                                      : pic.iWidth);
    int height = m_BmpBits[0] ? m_BmpBits[0]->GetHeight()
                              : (pic.iDisplayHeight > 0 ? pic.iDisplayHeight
                                                        : pic.iHeight);

    uint8_t *data = (uint8_t *)TJSAlignedAlloc(width * height * 4, 4);
    uint8_t *dstData[4] = {data, nullptr, nullptr, nullptr};
    int dstLineSize[4] = {width * 4, 0, 0, 0};

    img_convert_ctx = sws_getCachedContext(
        img_convert_ctx, srcWidth, srcHeight, AV_PIX_FMT_YUV420P, width, height,
        AV_PIX_FMT_RGBA, /*sws_flags*/ SWS_FAST_BILINEAR, nullptr, nullptr,
        nullptr);
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
        picbuf.data[0] = data;
        picbuf.pts = pts;
        ++m_usedPicture;
        m_lastQueuedPicturePts = pts;
    }

    return MAX_BUFFER_COUNT - m_usedPicture;
}

void MoviePlayerLayer::BuildGraph(tTJSNI_VideoOverlay *callbackwin,
                                  IStream *stream, const tjs_char *streamname,
                                  const tjs_char *type, uint64_t size) {
    m_pCallbackWin = callbackwin;
    m_pPlayer->SetCallback([this](auto &&PH1, auto &&PH2) {
        OnPlayEvent(std::forward<decltype(PH1)>(PH1),
                    std::forward<decltype(PH2)>(PH2));
    });
    m_pPlayer->OpenFromStream(stream, streamname, type, size);
}

void MoviePlayerLayer::OnPlayEvent(KRMovieEvent msg, void *p) {
    if(msg == KRMovieEvent::Update) {
        NativeEvent ev(WM_GRAPHNOTIFY);
        ev.WParam = EC_UPDATE;
        int frame;
        GetFrame(&frame);
        ev.LParam = frame;
        m_pCallbackWin->PostEvent(ev);
    } else if(msg == KRMovieEvent::Ended) {
        NativeEvent ev(WM_GRAPHNOTIFY);
        ev.WParam = EC_COMPLETE;
        ev.LParam = 0;
        m_pCallbackWin->PostEvent(ev);
    }
}

void MoviePlayerLayer::Play() {
    inherit::Play();
}

NS_KRMOVIE_END
