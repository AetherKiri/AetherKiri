
#ifndef __GRAPHICS_LOAD_THREAD_H__
#define __GRAPHICS_LOAD_THREAD_H__

#include <queue>
#include <set>
#include <vector>
#include "ThreadIntf.h"
#include "NativeEventQueue.h"
#include "GraphicsLoaderIntf.h"

// BaseBitmap
// を使うとリエントラントではないので、別の構造体に独自にロードする必要がある
struct tTVPTmpBitmapImage {
    tjs_uint32 width = 0;
    tjs_uint32 height = 0;
    tjs_int pitch = 0;
    tjs_uint32 *buffer = nullptr;
    bool opaque = false;
    std::vector<tTVPGraphicMetaInfoPair> *MetaInfo;
    tTVPTmpBitmapImage();
    ~tTVPTmpBitmapImage();
    // パレット関連は現状読まない、ファイルに従うのではなく、事前指定方式なので
};

struct tTVPImageLoadCommand {
    iTJSDispatch2 *owner_; // send to event
    class tTJSNI_Bitmap *bmp_; // set bitmap image
    // Commands cross the main/worker boundary. ttstr uses a shared COW buffer
    // whose reference count is not atomic, so keep independent strings here.
    std::string path_;
    tTVPTmpBitmapImage *dest_;
    std::string result_;
    bool prefetch_only_;
    tTVPImageLoadCommand();
    ~tTVPImageLoadCommand();
};

class tTVPAsyncImageLoader : public tTVPThread {
    /** 読込み要求コマンドのキュー用CS */
    tTJSCriticalSection CommandQueueCS;
    /** 読込み済み画像キュー用CS */
    tTJSCriticalSection ImageQueueCS;
    /** Prefetch 進行中タスクテーブル用CS */
    tTJSCriticalSection InFlightCS;

    /** ロード完了後メインスレッドで処理するためのメッセージキュー */
    NativeEventQueue<tTVPAsyncImageLoader> EventQueue;
    /**  読込みスレッドへ読込み要求があったことを伝えるイベント */
    tTVPThreadEvent PushCommandQueueEvent;

    /** 読込み要求コマンドキュー */
    std::queue<tTVPImageLoadCommand *> CommandQueue;
    /** 読込み完了画像キュー */
    std::queue<tTVPImageLoadCommand *> LoadedQueue;
    std::set<std::string> InFlightTable;

private:
    /**
     * 読込みスレッドからメインスレッドへ読込みが完了したことを通知する
     */
    void SendToLoadFinish();
    /**
     * 読込み完了した画像をメインスレッドでBitmapへ格納して、イベント通知する
     */
    void HandleLoadedImage();
    void FinishPrefetch(const std::string &path);

public:
    /**
     * 読込みを読込みスレッドに要求する(キューへ入れる)
     */
    void PushLoadQueue(iTJSDispatch2 *owner, tTJSNI_Bitmap *bmp,
                       const ttstr &nname);

    /**
     * 読込みスレッド実体
     * キューにコマンドが入るのを待ち、イベントが来たらキューからコマンドを取り出して読込み処理を実行
     * 読込みが完了したら読込み済み画像キューに入れてメインスレッドへ完了を通知する
     */
    void LoadingThread();

    /**
     * 画像読込み処理
     */
    void LoadImageFromCommand(tTVPImageLoadCommand *cmd);

    /**
     * 読込みスレッドメイン
     */
    void Execute() override;

    /**
     * メインスレッドハンドラ
     * メインスレッドへのイベント(メッセージ)通知を受ける
     */
    void Proc(NativeEvent &ev);

public:
    tTVPAsyncImageLoader();
    ~tTVPAsyncImageLoader() override;

    /**
     読込みスレッドの終了を要求する(終了は待たない)
     */
    void ExitRequest();

    /**
     * 読込み要求
     * メインスレッドから読込みスレッドへ読込みを要求する。
     * 読込み前にエラーが発生した場合やキャッシュ上に画像があった場合は要求は行われず
     * 即座に終了し、onLoaded イベントを発生させる。
     */
    void LoadRequest(iTJSDispatch2 *owner, tTJSNI_Bitmap *bmp,
                     const ttstr &name);

    /** Decode an image on the worker and populate the graphic cache on main. */
    void PrefetchRequest(const ttstr &name);

    /** Drop queued prefetch work; a command already decoding is allowed to
     * finish. */
    void FlushPrefetchQueue();

    bool IsAnyInFlight();
};

void TVPRequestImagePrefetch(const ttstr &name);
void TVPFlushImagePrefetchQueue();
bool TVPIsImagePrefetchLoading();

#endif // __GRAPHICS_LOAD_THREAD_H__
