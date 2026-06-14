/**
 * @file alphamovie.cpp
 * @brief AlphaMovie compatibility plugin backed by the KRMovie FFmpeg path.
 */

#include "tjsCommHead.h"
#include "EventIntf.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "StorageIntf.h"
#include "DebugIntf.h"
#include "ncbind.hpp"
#include "movie/ffmpeg/KRMovieLayer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

#define NCB_MODULE_NAME TJS_W("AlphaMovie.dll")

namespace {

static tTJSNI_BaseLayer *GetNativeLayer(iTJSDispatch2 *object) {
    if(!object)
        return nullptr;
    tTJSNI_BaseLayer *layer = nullptr;
    tjs_error hr = object->NativeInstanceSupport(
        TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
        reinterpret_cast<iTJSNativeInstance **>(&layer));
    if(TJS_FAILED(hr))
        return nullptr;
    return layer;
}

class AlphaMovieLayer : public KRMovie::VideoPresentLayer {
public:
    using Callback = std::function<void(KRMovieEvent, void *)>;

    explicit AlphaMovieLayer(Callback callback) :
        callback_(std::move(callback)) {}

    void BuildGraph(IStream *stream, const tjs_char *streamname,
                    const tjs_char *type, uint64_t size) {
        m_pPlayer->SetCallback(callback_);
        m_pPlayer->OpenFromStream(stream, streamname, type, size);
    }

    void OnPlayEvent(KRMovieEvent msg, void *p) override {
        if(callback_)
            callback_(msg, p);
    }

private:
    Callback callback_;
};

static int ClampPositiveInt64(tjs_int64 value) {
    if(value <= 0)
        return 0;
    if(value > static_cast<tjs_int64>(0x7fffffff))
        return 0x7fffffff;
    return static_cast<int>(value);
}

} // namespace

class AlphaMovie {
public:
    AlphaMovie() = default;
    virtual ~AlphaMovie() { close(); }

    void open(tTJSVariant storage) {
        close();

        filename_ = ttstr(storage);
        if(filename_.IsEmpty()) {
            TVPAddLog(TJS_W("AlphaMovie.open: empty storage name"));
            return;
        }

        stream_ = TVPCreateStream(filename_, TJS_BS_READ);
        if(!stream_) {
            TVPAddLog(ttstr(TJS_W("AlphaMovie.open: failed to open ")) +
                      filename_);
            return;
        }

        ttstr ext = TVPExtractStorageExt(filename_);
        ext.ToLowerCase();
        auto *movie = new AlphaMovieLayer(
            [this](KRMovieEvent msg, void *payload) { queueEvent(msg, payload); });
        movie->BuildGraph(TVPCreateIStream(stream_), filename_.c_str(),
                          ext.c_str(), stream_->GetSize());
        movie->GetVideoSize(&sourceWidth_, &sourceHeight_);

        if(sourceWidth_ <= 0 || sourceHeight_ <= 0) {
            TVPAddLog(ttstr(TJS_W("AlphaMovie.open: invalid video size for ")) +
                      filename_);
            movie->Release();
            movie = nullptr;
            delete stream_;
            stream_ = nullptr;
            sourceWidth_ = 0;
            sourceHeight_ = 0;
            return;
        }

        buffers_[0] = new tTVPBaseTexture(sourceWidth_, sourceHeight_, 32);
        buffers_[1] = new tTVPBaseTexture(sourceWidth_, sourceHeight_, 32);
        movie->SetVideoBuffer(buffers_[0], buffers_[1],
                              sourceWidth_ * sourceHeight_ * 4);

        movie_ = movie;
        width_ = sourceWidth_;
        height_ = sourceHeight_;
        opened_ = true;
        playRequested_ = false;
    }

    void play() {
        if(!movie_)
            return;
        movie_->Play();
        playRequested_ = true;
    }

    void stop() {
        if(!movie_)
            return;
        movie_->Stop();
        playRequested_ = false;
    }

    void pause() {
        if(!movie_)
            return;
        movie_->Pause();
        playRequested_ = false;
    }

    void close() {
        if(movie_) {
            movie_->Stop();
            movie_->Release();
            movie_ = nullptr;
        }
        if(stream_) {
            delete stream_;
            stream_ = nullptr;
        }
        for(auto *&buffer : buffers_) {
            delete buffer;
            buffer = nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(eventMutex_);
            pendingEvents_.clear();
        }
        filename_.Clear();
        sourceWidth_ = 0;
        sourceHeight_ = 0;
        width_ = 0;
        height_ = 0;
        opened_ = false;
        playRequested_ = false;
    }

    void rewind() { set_position(0); }

    bool get_loop() const { return loop_; }
    void set_loop(bool v) { loop_ = v; }

    bool get_visible() const { return visible_; }
    void set_visible(bool v) { visible_ = v; }

    int get_frame() const {
        if(!movie_)
            return 0;
        int frame = 0;
        movie_->GetFrame(&frame);
        return frame;
    }

    void set_frame(int frame) {
        if(!movie_)
            return;
        movie_->Flush();
        movie_->SetFrame(std::max(0, frame));
    }

    double get_fps() const {
        if(movie_) {
            double fps = 0.0;
            movie_->GetFPS(&fps);
            if(std::isfinite(fps) && fps > 0.0)
                return fps;
        }
        return fps_ > 0.0 ? fps_ : 30.0;
    }

    void set_fps(double fps) {
        if(std::isfinite(fps) && fps > 0.0)
            fps_ = fps;
    }

    int get_position() const {
        if(!movie_)
            return 0;
        uint64_t position = 0;
        movie_->GetPosition(&position);
        return ClampPositiveInt64(static_cast<tjs_int64>(position));
    }

    void set_position(int position) {
        if(!movie_)
            return;
        movie_->Flush();
        movie_->SetPosition(static_cast<uint64_t>(std::max(0, position)));
    }

    int get_width() const { return width_; }
    int get_height() const { return height_; }

    bool get_opened() const { return opened_; }

    bool get_isPlaying() const {
        if(!movie_)
            return false;
        tTVPVideoStatus status = vsStopped;
        movie_->GetStatus(&status);
        return status == vsPlaying;
    }

    int get_totalTime() const {
        if(!movie_)
            return 0;
        int64_t total = 0;
        movie_->GetTotalTime(&total);
        return ClampPositiveInt64(total);
    }

    int get_numberOfFrame() const {
        if(!movie_)
            return 0;
        int frames = 0;
        movie_->GetNumberOfFrame(&frames);
        return std::max(0, frames);
    }

    int get_numOfFrame() const { return get_numberOfFrame(); }

    int get_FPSRate() const {
        return std::max(1, static_cast<int>(std::lround(get_fps() * 1000.0)));
    }

    int get_FPSScale() const { return 1000; }

    int get_screenWidth() const { return screenWidth_; }
    int get_screenHeight() const { return screenHeight_; }

    int showNextImage(iTJSDispatch2 *targetObject) {
        if(!movie_)
            return 0;

        processEvents();

        tTVPBaseTexture *front = takeFrameForPresentation();
        if(!front)
            return 0;

        tTJSNI_BaseLayer *targetLayer = GetNativeLayer(targetObject);
        if(!targetLayer || !targetLayer->GetMainImage())
            return 0;

        tTVPBaseTexture *target = targetLayer->GetMainImage();
        const int targetWidth = static_cast<int>(target->GetWidth());
        const int targetHeight = static_cast<int>(target->GetHeight());
        if(targetWidth <= 0 || targetHeight <= 0)
            return 0;

        const int sourceWidth =
            sourceWidth_ > 0 ? static_cast<int>(sourceWidth_)
                             : static_cast<int>(front->GetWidth());
        const int sourceHeight =
            sourceHeight_ > 0 ? static_cast<int>(sourceHeight_)
                              : static_cast<int>(front->GetHeight());
        if(sourceWidth <= 0 || sourceHeight <= 0)
            return 0;

        tTVPRect clip(0, 0, targetWidth, targetHeight);
        tTVPRect dest(0, 0, targetWidth, targetHeight);
        tTVPRect src(0, 0, sourceWidth, sourceHeight);
        target->StretchBlt(clip, dest, front, src, bmCopy, 255, false,
                           stLinear);
        targetLayer->Update(dest);
        return 1;
    }

private:
    void queueEvent(KRMovieEvent msg, void *) {
        std::lock_guard<std::mutex> lock(eventMutex_);
        pendingEvents_.push_back(msg);
    }

    void processEvents() {
        std::vector<KRMovieEvent> events;
        {
            std::lock_guard<std::mutex> lock(eventMutex_);
            events.swap(pendingEvents_);
        }
        for(KRMovieEvent msg : events) {
            if(msg != KRMovieEvent::Ended)
                continue;
            if(loop_ && movie_) {
                movie_->Rewind();
                movie_->Play();
                playRequested_ = true;
            } else {
                playRequested_ = false;
            }
        }
    }

    tTVPBaseTexture *takeFrameForPresentation() {
        bool autoStarted = false;
        for(int attempt = 0; attempt < 8; ++attempt) {
            processEvents();
            movie_->FrameMove();
            if(tTVPBaseTexture *front = movie_->GetFrontBuffer()) {
                if(autoStarted && !playRequested_)
                    movie_->Pause();
                return front;
            }

            tTVPVideoStatus status = vsStopped;
            movie_->GetStatus(&status);
            if(status != vsPlaying) {
                movie_->Play();
                autoStarted = true;
            }

            if(attempt < 7) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(attempt < 3 ? 2 : 4));
            }
        }

        if(autoStarted && !playRequested_)
            movie_->Pause();
        return nullptr;
    }

    AlphaMovieLayer *movie_ = nullptr;
    tTJSBinaryStream *stream_ = nullptr;
    tTVPBaseTexture *buffers_[2]{};
    ttstr filename_;
    long sourceWidth_ = 0;
    long sourceHeight_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool opened_ = false;
    bool playRequested_ = false;
    bool loop_ = false;
    bool visible_ = false;
    double fps_ = 30.0;
    int screenWidth_ = 1280;
    int screenHeight_ = 720;
    std::mutex eventMutex_;
    std::vector<KRMovieEvent> pendingEvents_;
};

NCB_REGISTER_CLASS(AlphaMovie) {
    Constructor();

    NCB_METHOD(open);
    NCB_METHOD(play);
    NCB_METHOD(stop);
    NCB_METHOD(pause);
    NCB_METHOD(close);
    NCB_METHOD(rewind);

    NCB_PROPERTY(loop, get_loop, set_loop);
    NCB_PROPERTY(visible, get_visible, set_visible);
    NCB_PROPERTY(frame, get_frame, set_frame);
    NCB_PROPERTY(fps, get_fps, set_fps);
    NCB_PROPERTY(position, get_position, set_position);
    NCB_PROPERTY_RO(width, get_width);
    NCB_PROPERTY_RO(height, get_height);
    NCB_PROPERTY_RO(opened, get_opened);
    NCB_PROPERTY_RO(isPlaying, get_isPlaying);
    NCB_PROPERTY_RO(totalTime, get_totalTime);
    NCB_PROPERTY_RO(numberOfFrame, get_numberOfFrame);
    NCB_PROPERTY_RO(numOfFrame, get_numOfFrame);
    NCB_PROPERTY_RO(FPSRate, get_FPSRate);
    NCB_PROPERTY_RO(FPSScale, get_FPSScale);
    NCB_PROPERTY_RO(screenWidth, get_screenWidth);
    NCB_PROPERTY_RO(screenHeight, get_screenHeight);
    NCB_METHOD(showNextImage);
};
