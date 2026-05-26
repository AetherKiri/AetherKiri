//
// D3DAdaptor — matches libkrkr2.so Motion.D3DAdaptor
// Reverse-engineered from D3DAdaptor_constructor @ 0x6AD518,
// D3DAdaptor_init @ 0x6ADB10, and sub_6ACE94 (members).
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "tjs.h"
#include "LayerIntf.h"

class iTVPTexture2D;

namespace motion {

    class D3DAdaptor {
    public:
        D3DAdaptor() = default;
        ~D3DAdaptor();
        D3DAdaptor(const D3DAdaptor &) = delete;
        D3DAdaptor &operator=(const D3DAdaptor &) = delete;

        static tjs_error factory(D3DAdaptor **result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *);

        // --- Properties ---
        bool getVisible() const { return _visible; }
        void setVisible(bool v) { _visible = v; }
        bool getAlphaOpAdd() const { return _alphaOpAdd; }
        void setAlphaOpAdd(bool v) { _alphaOpAdd = v; }
        bool getCanvasCaptureEnabled() const { return _canvasCaptureEnabled; }
        void setCanvasCaptureEnabled(bool v) { _canvasCaptureEnabled = v; }
        bool getClearEnabled() const { return _clearEnabled; }
        void setClearEnabled(bool v) { _clearEnabled = v; }

        // --- Methods ---
        void setPos(int, int) {}
        void setSize(int w, int h);
        void setClearColor(int color) { _clearColor = color; }
        void setResizable(bool v) { _resizable = v; }
        void removeAllTextures() {}
        void removeAllBg() {}
        void removeAllCaption() {}
        void registerBg() {}
        void registerCaption() {}
        void unloadUnusedTextures() {}

        tjs_error captureCanvas(tTJSVariant *result, tjs_int numparams,
                                tTJSVariant **param, iTJSDispatch2 *objthis);

        // Static callback wrapper for NCB registration
        static tjs_error captureCanvasStatic(tTJSVariant *result, tjs_int numparams,
                                             tTJSVariant **param,
                                             D3DAdaptor *nativeInstance);

        int getWidth() const { return _width; }
        int getHeight() const { return _height; }
        iTJSDispatch2 *getWindowObject() const;
        std::uint8_t *getBuffer() { return _buffer.data(); }
        const std::uint8_t *getBuffer() const { return _buffer.data(); }
        tjs_int getBufferPitch() const { return _width * 4; }
        size_t getBufferSize() const { return _buffer.size(); }
        int getCenterX() const { return _centerX; }
        int getCenterY() const { return _centerY; }
        iTVPTexture2D *targetTexture() const { return _targetTexture; }
        bool hasTargetTexture() const { return _targetTexture != nullptr; }
        bool ensureTargetTexture();
        void clearTargetTexture();
        bool copyTargetTextureRowsForCaptureLike_0x6AD92C(std::uint8_t *dst,
                                                          tjs_int dstPitch);

        void initializeLike_0x6ADB10(const tTJSVariant &window,
                                     int width,
                                     int height,
                                     int centerX,
                                     int centerY);

        void clearBuffer();

    private:
        void allocBuffer();
        void releaseTargetTexture();

        tTJSVariant _window;
        int _width = 0;
        int _height = 0;
        int _centerX = 0;
        int _centerY = 0;
        bool _visible = false;
        bool _canvasCaptureEnabled = false;
        bool _clearEnabled = true;
        bool _resizable = false;
        bool _alphaOpAdd = false;
        int _clearColor = 0;
        iTVPTexture2D *_targetTexture = nullptr;
        std::vector<std::uint8_t> _buffer;
    };

} // namespace motion
