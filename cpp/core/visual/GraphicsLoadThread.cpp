
#include "tjsCommHead.h"

#include "BitmapIntf.h"
#include "GraphicsLoadThread.h"
#include "ThreadIntf.h"
#include "NativeEventQueue.h"
#include "UserEvent.h"
#include "EventIntf.h"
#include "StorageIntf.h"
#include "LayerBitmapIntf.h"
#include "MsgIntf.h"
#include "UtilStreams.h"
#include "BitmapBitsAlloc.h"
#include "LayerIntf.h"
#include "TVPDecodeArena.h"
#include "BitmapInfomation.h"
#include "Application.h"
#include "DebugIntf.h"

#include <memory>

tTVPTmpBitmapImage::tTVPTmpBitmapImage() : MetaInfo(nullptr) {}
tTVPTmpBitmapImage::~tTVPTmpBitmapImage() {
    if(buffer) {
        tTVPBitmapBitsAlloc::Free(buffer);
        buffer = nullptr;
    }
    if(MetaInfo) {
        delete MetaInfo;
        MetaInfo = nullptr;
    }
}
tTVPImageLoadCommand::tTVPImageLoadCommand() :
    owner_(nullptr), bmp_(nullptr), dest_(nullptr), prefetch_only_(false) {}
tTVPImageLoadCommand::~tTVPImageLoadCommand() {
    if(owner_) {
        owner_->Release();
        owner_ = nullptr;
    }
    if(dest_) {
        delete dest_;
        dest_ = nullptr;
    }
    bmp_ = nullptr;
}

static int TVPLoadGraphicAsync_SizeCallback(void *callbackdata, tjs_uint w,
                                            tjs_uint h,
                                            tTVPGraphicPixelFormat fmt) {
    auto *img = (tTVPTmpBitmapImage *)callbackdata;
    if(img->buffer) {
        tTVPBitmapBitsAlloc::Free(img->buffer);
        img->buffer = nullptr;
    }
    img->width = w;
    img->height = h;
    BitmapInfomation info(w, h, 32);
    img->pitch = info.GetPitchBytes();
    img->buffer = static_cast<tjs_uint32 *>(
        tTVPBitmapBitsAlloc::Alloc(info.GetImageSize(), w, h));
    switch(fmt) {
        case gpfLuminance:
        case gpfRGB:
            img->opaque = true;
            break;
        case gpfPalette:
        case gpfRGBA:
            img->opaque = false;
            break;
    }
    return img->pitch;
}
//---------------------------------------------------------------------------
static void *TVPLoadGraphicAsync_ScanLineCallback(void *callbackdata,
                                                  tjs_int y) {
    auto *img = (tTVPTmpBitmapImage *)callbackdata;
    if(y >= 0) {
        if(y < (tjs_int)img->height && img->buffer) {
            return reinterpret_cast<tjs_uint8 *>(img->buffer) +
                (img->height - static_cast<tjs_uint32>(y) - 1) * img->pitch;
        } else {
            return nullptr;
        }
    }
    return nullptr; // -1 の時のフラッシュ処理は何もしない
}
//---------------------------------------------------------------------------
static void TVPLoadGraphicAsync_MetaInfoPushCallback(void *callbackdata,
                                                     const ttstr &name,
                                                     const ttstr &value) {
    auto *img = (tTVPTmpBitmapImage *)callbackdata;

    if(!img->MetaInfo)
        img->MetaInfo = new std::vector<tTVPGraphicMetaInfoPair>();
    img->MetaInfo->emplace_back(name, value);
}
//---------------------------------------------------------------------------

tTVPAsyncImageLoader::tTVPAsyncImageLoader() :
    EventQueue(this, &tTVPAsyncImageLoader::Proc), tTVPThread(true) {
    EventQueue.Allocate();
}
tTVPAsyncImageLoader::~tTVPAsyncImageLoader() {
    ExitRequest();
    WaitFor();
    EventQueue.Deallocate();
    while(!CommandQueue.empty()) {
        tTVPImageLoadCommand *cmd = CommandQueue.front();
        CommandQueue.pop();
        delete cmd;
    }
    while(!LoadedQueue.empty()) {
        tTVPImageLoadCommand *cmd = LoadedQueue.front();
        LoadedQueue.pop();
        delete cmd;
    }
}
void tTVPAsyncImageLoader::ExitRequest() {
    Terminate();
    PushCommandQueueEvent.Set();
}
void tTVPAsyncImageLoader::Execute() {
    // プライオリティは最低にする
    SetPriority(ttpIdle);
    LoadingThread();
}
void tTVPAsyncImageLoader::SendToLoadFinish() {
    NativeEvent ev(TVP_EV_IMAGE_LOAD_THREAD);
    EventQueue.PostEvent(ev);
}
void tTVPAsyncImageLoader::Proc(NativeEvent &ev) {
    if(ev.Message != TVP_EV_IMAGE_LOAD_THREAD) {
        EventQueue.HandlerDefault(ev);
        return;
    }
    HandleLoadedImage();
}
void tTVPAsyncImageLoader::HandleLoadedImage() {
    bool loading;
    do {
        loading = false;
        tTVPImageLoadCommand *cmd = nullptr;
        {
            tTJSCriticalSectionHolder cs(ImageQueueCS);
            if(!LoadedQueue.empty()) {
                cmd = LoadedQueue.front();
                LoadedQueue.pop();
                loading = true;
            }
        }
        if(cmd != nullptr) {
            std::unique_ptr<tTVPImageLoadCommand> command(cmd);
            const ttstr path(cmd->path_);
            const bool prefetch = cmd->prefetch_only_;
            try {
                if(cmd->bmp_)
                    cmd->bmp_->SetLoading(false);
                if(cmd->result_.length() > 0) {
                    if(!prefetch) {
                        tTJSVariant param[4];
                        param[0] = tTJSVariant((iTJSDispatch2 *)nullptr,
                                               (iTJSDispatch2 *)nullptr);
                        param[1] = 1;
                        param[2] = 1;
                        param[3] = cmd->result_.c_str();
                        static ttstr eventname(TJS_W("onLoaded"));
                        if(cmd->owner_ &&
                           cmd->owner_->IsValid(0, nullptr, nullptr,
                                                cmd->owner_) == TJS_S_TRUE) {
                            TVPPostEvent(cmd->owner_, cmd->owner_, eventname, 0,
                                         TVP_EPT_IMMEDIATE, 4, param);
                        }
                    }

                    if(cmd->dest_->MetaInfo) {
                        delete cmd->dest_->MetaInfo;
                        cmd->dest_->MetaInfo = nullptr;
                    }
                } else {
                    iTJSDispatch2 *metainfo = prefetch
                        ? nullptr
                        : TVPMetaInfoPairsToDictionary(cmd->dest_->MetaInfo);
                    const auto release_bitmap = [](tTVPBitmap *bitmap) {
                        if(bitmap)
                            bitmap->Release();
                    };
                    std::unique_ptr<tTVPBitmap, decltype(release_bitmap)>
                        decoded(new tTVPBitmap(cmd->dest_->width,
                                               cmd->dest_->height, 32,
                                               cmd->dest_->buffer),
                                release_bitmap);
                    decoded->IsOpaque = cmd->dest_->opaque;
                    cmd->dest_->buffer = nullptr;

                    try {
                        if(!TVPHasImageCache(path, glmNormal, 0, 0,
                                             TVP_clNone)) {
                            auto *cache_meta = cmd->dest_->MetaInfo;
                            cmd->dest_->MetaInfo = nullptr;
                            TVPPushGraphicCache(path, decoded.get(),
                                                cache_meta);
                        } else {
                            delete cmd->dest_->MetaInfo;
                            cmd->dest_->MetaInfo = nullptr;
                        }
                        if(!prefetch && cmd->bmp_)
                            cmd->bmp_->SetSizeAndImageBuffer(decoded.get());
                    } catch(...) {
                        if(metainfo)
                            metainfo->Release();
                        throw;
                    }
                    if(!prefetch) {
                        tTJSVariant param[4];
                        param[0] = tTJSVariant(metainfo, metainfo);
                        if(metainfo)
                            metainfo->Release();
                        param[1] = 1;
                        param[2] = 0;
                        param[3] = TJS_W("");
                        static ttstr eventname(TJS_W("onLoaded"));
                        if(cmd->owner_ &&
                           cmd->owner_->IsValid(0, nullptr, nullptr,
                                                cmd->owner_) == TJS_S_TRUE) {
                            TVPPostEvent(cmd->owner_, cmd->owner_, eventname, 0,
                                         TVP_EPT_IMMEDIATE, 4, param);
                        }
                    }
                }
            } catch(...) {
                if(prefetch) {
                    FinishPrefetch(cmd->path_);
                    TVPAddImportantLog(TJS_W("Image prefetch failed: ") + path);
                    continue;
                }
                throw;
            }
            if(prefetch)
                FinishPrefetch(cmd->path_);
        }
    } while(loading);
}

void tTVPAsyncImageLoader::FinishPrefetch(const std::string &path) {
    tTJSCriticalSectionHolder cs(InFlightCS);
    InFlightTable.erase(path);
}
//---------------------------------------------------------------------------

// onLoaded( dic, is_async, is_error, error_mes ); エラーは
// sync ( main thead )
void tTVPAsyncImageLoader::LoadRequest(iTJSDispatch2 *owner, tTJSNI_Bitmap *bmp,
                                       const ttstr &name) {
    // tTVPBaseBitmap* dest = new tTVPBaseBitmap( 32, 32, 32 );
    tTVPBaseBitmap dest(TVPGetInitialBitmap());
    iTJSDispatch2 *metainfo = nullptr;
    ttstr nname = TVPNormalizeStorageName(name);
    if(TVPCheckImageCache(nname, &dest, glmNormal, 0, 0, TVP_clNone,
                          &metainfo)) {
        // キャッシュ内に発見、即座に読込みを完了する
        if(bmp) {
            bmp->CopyFrom(&dest);
            bmp->SetLoading(false);
        }
        if(!owner)
            return;
        tTJSVariant param[4];
        param[0] = tTJSVariant(metainfo, metainfo);
        if(metainfo)
            metainfo->Release();
        param[1] = 0; // false
        param[2] = 0; // false
        param[3] = TJS_W(""); // error_mes
        static ttstr eventname(TJS_W("onLoaded"));
        TVPPostEvent(owner, owner, eventname, 0, TVP_EPT_IMMEDIATE, 4, param);
        return;
    }
    if(TVPIsExistentStorage(name) == false) {
        TVPThrowExceptionMessage(TVPCannotFindStorage, name);
    }
    ttstr ext = TVPExtractStorageExt(name);
    if(ext == TJS_W("")) {
        TVPThrowExceptionMessage(TJS_W("Filename extension not found/%1"),
                                 name);
    }

    PushLoadQueue(owner, bmp, nname);
}

// tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);
//	tTJSBinaryStream* stream = TVPCreateStream(nname, TJS_BS_READ);
// TVPCreateStream はロックされているので、非同期で実行可能

void tTVPAsyncImageLoader::PushLoadQueue(iTJSDispatch2 *owner,
                                         tTJSNI_Bitmap *bmp,
                                         const ttstr &nname) {
    auto *cmd = new tTVPImageLoadCommand();
    cmd->owner_ = owner;
    if(owner)
        owner->AddRef();
    cmd->bmp_ = bmp;
    cmd->path_ = nname.AsStdString();
    cmd->dest_ = new tTVPTmpBitmapImage();
    cmd->result_.clear();
    {
        // キューをロックしてプッシュ
        tTJSCriticalSectionHolder cs(CommandQueueCS);
        CommandQueue.push(cmd);
    }
    // 追加したことをイベントで通知
    PushCommandQueueEvent.Set();
}

void tTVPAsyncImageLoader::PrefetchRequest(const ttstr &name) {
    ttstr nname = TVPNormalizeStorageName(name);
    if(TVPHasImageCache(nname, glmNormal, 0, 0, TVP_clNone))
        return;

    const std::string key = nname.AsStdString();
    {
        tTJSCriticalSectionHolder cs(InFlightCS);
        if(!InFlightTable.insert(key).second)
            return;
    }

    ttstr ext = TVPExtractStorageExt(nname);
    if(ext.IsEmpty() || !TVPGetGraphicLoadHandler(ext)) {
        FinishPrefetch(key);
        return;
    }

    auto *cmd = new tTVPImageLoadCommand();
    cmd->path_ = key;
    cmd->dest_ = new tTVPTmpBitmapImage();
    cmd->prefetch_only_ = true;
    {
        tTJSCriticalSectionHolder cs(CommandQueueCS);
        CommandQueue.push(cmd);
    }
    PushCommandQueueEvent.Set();
}

void tTVPAsyncImageLoader::FlushPrefetchQueue() {
    std::queue<tTVPImageLoadCommand *> kept;
    std::vector<tTVPImageLoadCommand *> dropped;
    {
        tTJSCriticalSectionHolder cs(CommandQueueCS);
        while(!CommandQueue.empty()) {
            auto *cmd = CommandQueue.front();
            CommandQueue.pop();
            if(cmd->prefetch_only_)
                dropped.push_back(cmd);
            else
                kept.push(cmd);
        }
        CommandQueue.swap(kept);
    }
    for(auto *cmd : dropped) {
        FinishPrefetch(cmd->path_);
        delete cmd;
    }
}

bool tTVPAsyncImageLoader::IsAnyInFlight() {
    tTJSCriticalSectionHolder cs(InFlightCS);
    return !InFlightTable.empty();
}
void tTVPAsyncImageLoader::LoadingThread() {
    while(!GetTerminated()) {
        // キュー追加イベント待ち
        PushCommandQueueEvent.WaitFor(0);
        if(GetTerminated())
            break;
        bool loading;
        do {
            loading = false;
            tTVPImageLoadCommand *cmd = nullptr;

            { // Lock
                tTJSCriticalSectionHolder cs(CommandQueueCS);
                if(!CommandQueue.empty()) {
                    cmd = CommandQueue.front();
                    CommandQueue.pop();
                }
            }
            if(cmd) {
                loading = true;
                LoadImageFromCommand(cmd);
                { // Lock
                    tTJSCriticalSectionHolder cs(ImageQueueCS);
                    LoadedQueue.push(cmd);
                }
                // Send to message
                SendToLoadFinish();
            }
        } while(loading && !GetTerminated());
    }
}
tTVPGraphicHandlerType *TVPGuessGraphicLoadHandler(ttstr &name);
void tTVPAsyncImageLoader::LoadImageFromCommand(tTVPImageLoadCommand *cmd) {
    ttstr name(cmd->path_);
    ttstr ext = TVPExtractStorageExt(name);
    tTVPGraphicHandlerType *handler = nullptr;
    if(ext.IsEmpty()) {
        // missing extension
        handler = TVPGuessGraphicLoadHandler(name);
        //		cmd->result_ = TJS_W("Filename extension not found");
    } else {
        handler = TVPGetGraphicLoadHandler(ext);
    }
    if(handler) {
        try {
            tTVPStreamHolder holder(name);
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
            TVPDecodeArena::Instance().Begin();
#endif
            handler->Load(handler->FormatData, (void *)cmd->dest_,
                          TVPLoadGraphicAsync_SizeCallback,
                          TVPLoadGraphicAsync_ScanLineCallback,
                          TVPLoadGraphicAsync_MetaInfoPushCallback,
                          holder.Get(), -1, glmNormal);
            if(!cmd->dest_->buffer || cmd->dest_->width == 0 ||
               cmd->dest_->height == 0) {
                cmd->result_ =
                    TVPFormatMessage(TVPImageLoadError, name).AsStdString();
            }
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
            TVPDecodeArena::Instance().End();
#endif
        } catch(...) {
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
            TVPDecodeArena::Instance().End();
#endif
            cmd->result_ =
                TVPFormatMessage(TVPImageLoadError, name).AsStdString();
        }
    } else {
        // error
        cmd->result_ =
            TVPFormatMessage(TVPUnknownGraphicFormat, name).AsStdString();
    }
}

void TVPRequestImagePrefetch(const ttstr &name) {
    if(Application && Application->GetAsyncImageLoader())
        Application->GetAsyncImageLoader()->PrefetchRequest(name);
}

void TVPFlushImagePrefetchQueue() {
    if(Application && Application->GetAsyncImageLoader())
        Application->GetAsyncImageLoader()->FlushPrefetchQueue();
}

bool TVPIsImagePrefetchLoading() {
    return Application && Application->GetAsyncImageLoader() &&
        Application->GetAsyncImageLoader()->IsAnyInFlight();
}
