#ifndef AETHER_SCREEN_CAPTURE_H
#define AETHER_SCREEN_CAPTURE_H

#include "tjsCommHead.h"

class iTVPBaseBitmap;

struct tTVPScreenCaptureRequest {
    ttstr path;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

void TVPRequestScreenCapture(const ttstr &path, int x, int y, int width,
                             int height);
bool TVPHasPendingScreenCapture();
bool TVPTakeScreenCaptureRequest(tTVPScreenCaptureRequest &request);
bool TVPSaveScreenCapture(const tTVPScreenCaptureRequest &request,
                          const iTVPBaseBitmap *source);
void TVPSetScreenCaptureResult(const ttstr &path, int width, int height,
                               bool success);
bool TVPGetLastScreenCapture(ttstr &path, int &width, int &height,
                             bool &success);

#endif
