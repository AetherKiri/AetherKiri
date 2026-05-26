// PlayerRender.cpp — render state, canvas/source/cache helpers, no-arg draw
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "SourceCache.h"

using namespace motion::internal;

namespace motion {
    // --- Drawing/rendering ---
    void Player::setClearColor(tjs_int color) { _runtime->clearColor = color; }

    void Player::setResizable(bool v) { _runtime->resizable = v; }

    void Player::removeAllTextures() {
        if(_runtime && _runtime->sourceCacheNative) {
            _runtime->sourceCacheNative->clearCache();
        }
    }

    void Player::removeAllBg() { _runtime->backgrounds.clear(); }

    void Player::removeAllCaption() { _runtime->captions.clear(); }

    void Player::registerBg(tTJSVariant bg) { _runtime->backgrounds.push_back(bg); }

    void Player::registerCaption(tTJSVariant caption) {
        _runtime->captions.push_back(caption);
    }

    void Player::unloadUnusedTextures() {}

    tjs_int Player::alphaOpAdd() { return ++_runtime->alphaOpCounter; }

    tTJSVariant Player::captureCanvas() {
        if(_runtime->lastCanvas.Type() == tvtVoid) {
            draw();
        }
        return _runtime->lastCanvas;
    }

    tTJSVariant Player::findSource(ttstr name) {
        if(!_runtime || !_runtime->sourceCacheNative) {
            return {};
        }
        return _runtime->sourceCacheNative->findSource(std::move(name));
    }

    void Player::loadSource(ttstr name) {
        if(_runtime && _runtime->sourceCacheNative) {
            _runtime->sourceCacheNative->loadSourceByName(name, {});
        }
    }

    void Player::clearCache() {
        if(_runtime && _runtime->sourceCacheNative) {
            _runtime->sourceCacheNative->clearCache();
        }
        _runtime->lastCanvas.Clear();
    }

    void Player::setSize(tjs_int w, tjs_int h) {
        _runtime->width = w;
        _runtime->height = h;
    }

    void Player::copyRect(tTJSVariant) {}

    void Player::adjustGamma(tTJSVariant) {}

    void Player::draw() {
        // Keep the no-arg C++ method as a lightweight prepare pass. The real
        // libkrkr2.so draw dispatch happens in drawCompat based on argument type.
        if(!_runtime->visible) {
            _runtime->lastCanvas.Clear();
            return;
        }

        ensureMotionLoaded();
        calcViewParam();
        prepareRenderItems();
    }

} // namespace motion
