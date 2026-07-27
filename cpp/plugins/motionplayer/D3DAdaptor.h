//
// D3DAdaptor — matches libkrkr2.so Motion.D3DAdaptor
// Reverse-engineered from sub_6ADB10 (constructor) and sub_6ACE94 (members)
//
#pragma once

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <spdlog/spdlog.h>
#include "tjs.h"
#include "LayerIntf.h"

namespace motion {

    // D3DAdaptor acts as a pixel buffer that Player.draw() renders into.
    // TJS drawAffine then calls captureCanvas() to copy the buffer to a
    // Layer, followed by _redrawImage to display the result.
    class D3DAdaptor {
    public:
        D3DAdaptor() = default;

        static tjs_error factory(D3DAdaptor **result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *) {
            auto logger = spdlog::get("plugin");
            if(logger) {
                logger->warn("D3DAdaptor::factory called, numparams={}", numparams);
            }
            if(numparams < 1) return TJS_E_BADPARAMCOUNT;
            if(!result) return TJS_E_INVALIDPARAM;
            auto *obj = new D3DAdaptor();
            obj->_window = *param[0];
            if(numparams > 1) obj->_width = static_cast<int>(param[1]->AsInteger());
            if(numparams > 2) obj->_height = static_cast<int>(param[2]->AsInteger());
            obj->allocBuffer();
            if(logger) {
                logger->warn("D3DAdaptor::factory OK, w={} h={}", obj->_width, obj->_height);
            }
            *result = obj;
            return TJS_S_OK;
        }

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
        void setSize(int w, int h) {
            _width = w; _height = h;
            allocBuffer();
        }
        void setClearColor(int color) { _clearColor = color; }
        void setResizable(bool v) { _resizable = v; }
        void removeAllTextures() {}
        void removeAllBg() {}
        void removeAllCaption() {}
        void registerBg() {}
        void registerCaption() {}
        void unloadUnusedTextures() {}

        // Retain the layer produced by Player::renderToD3DAdaptor so
        // captureCanvas can keep the transfer on the active render backend.
        // The legacy CPU buffer remains available as a compatibility fallback
        // for callers that did not render through Player first.
        void setRenderedLayer(iTJSDispatch2 *layer) {
            if(layer) {
                _renderedLayer = tTJSVariant(layer, layer);
            } else {
                _renderedLayer.Clear();
            }
        }

        // A normal D3DEmote draw batch calls captureCanvas immediately after
        // one or more Player.draw() calls. D3DAffineSourceMotion presents the
        // adaptor directly and never captures it. Detect the latter from the
        // call sequence so presentation does not depend on motion filenames.
        void notePlayerDraw() {
            const auto now = std::chrono::steady_clock::now();
            if(_drawsSinceCapture == 0) {
                _uncapturedDrawStartedAt = now;
            }
            ++_drawsSinceCapture;
        }

        bool shouldRetainUncapturedPresentation() const {
            if(_drawsSinceCapture == 0) {
                return false;
            }
            return std::chrono::steady_clock::now() -
                       _uncapturedDrawStartedAt >=
                   std::chrono::milliseconds(50);
        }

        void setRetainedPresentationLayer(iTJSDispatch2 *layer) {
            if(layer) {
                _retainedPresentationLayer = tTJSVariant(layer, layer);
            } else {
                _retainedPresentationLayer.Clear();
            }
        }

        // captureCanvas: copies the most recently rendered image into a TJS
        // Layer. Godot-backed layers preserve this CopyRect on the ordered GPU
        // queue instead of downloading the complete canvas and uploading it
        // again every animation frame.
        tjs_error captureCanvas(tTJSVariant *result, tjs_int numparams,
                                tTJSVariant **param, iTJSDispatch2 *objthis) {
            if(numparams < 1 || !param[0]) return TJS_E_BADPARAMCOUNT;

            iTJSDispatch2 *layerObj = param[0]->AsObjectNoAddRef();
            if(!layerObj) return TJS_E_INVALIDPARAM;

            _drawsSinceCapture = 0;
            if(_retainedPresentationLayer.Type() == tvtObject) {
                auto *presentationObj =
                    _retainedPresentationLayer.AsObjectNoAddRef();
                tTJSNI_BaseLayer *presentation = nullptr;
                if(presentationObj &&
                   TJS_SUCCEEDED(presentationObj->NativeInstanceSupport(
                       TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                       reinterpret_cast<iTJSNativeInstance **>(
                           &presentation))) &&
                   presentation) {
                    presentation->SetVisible(false);
                }
                _retainedPresentationLayer.Clear();
            }

            tTJSNI_BaseLayer *layer = nullptr;
            if(TJS_FAILED(layerObj->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
                return TJS_E_INVALIDPARAM;
            }
            if(_renderedLayer.Type() == tvtObject) {
                auto *renderedLayerObj =
                    _renderedLayer.AsObjectNoAddRef();
                tTJSNI_BaseLayer *renderedLayer = nullptr;
                if(renderedLayerObj &&
                   TJS_SUCCEEDED(renderedLayerObj->NativeInstanceSupport(
                       TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                       reinterpret_cast<iTJSNativeInstance **>(
                           &renderedLayer))) &&
                   renderedLayer) {
                    if(!_captureDebugLogged) {
                        const char *debug =
                            std::getenv("AETHERKIRI_MOTION_DEBUG");
                        if(debug && *debug && std::strcmp(debug, "0") != 0) {
                            const auto describeLayer =
                                [](tTJSNI_BaseLayer *candidate) {
                                    if(!candidate) {
                                        return std::string("<null>");
                                    }
                                    const auto *parent =
                                        candidate->GetParent();
                                    return fmt::format(
                                        "ptr={} name={} parentPtr={} parent={} "
                                        "visible={} parentVisible={} opacity={} "
                                        "order={} overall={} pos=({}, {}) "
                                        "size={}x{} imagePos=({}, {}) image={}x{} "
                                        "hasImage={} type={}",
                                        static_cast<const void *>(candidate),
                                        candidate->GetName().AsStdString(),
                                        static_cast<const void *>(parent),
                                        parent
                                            ? parent->GetName().AsStdString()
                                            : std::string("<none>"),
                                        candidate->GetVisible() ? 1 : 0,
                                        candidate->GetParentVisible() ? 1 : 0,
                                        candidate->GetOpacity(),
                                        candidate->GetOrderIndex(),
                                        candidate->GetOverallOrderIndex(),
                                        candidate->GetLeft(),
                                        candidate->GetTop(),
                                        candidate->GetWidth(),
                                        candidate->GetHeight(),
                                        candidate->GetImageLeft(),
                                        candidate->GetImageTop(),
                                        candidate->GetImageWidth(),
                                        candidate->GetImageHeight(),
                                        candidate->GetHasImage() ? 1 : 0,
                                        static_cast<int>(
                                            candidate->GetType()));
                                };
                            if(auto logger = spdlog::get("plugin")) {
                                logger->info(
                                    "motion d3d capture transfer: source=[{}] target=[{}]",
                                    describeLayer(renderedLayer),
                                    describeLayer(layer));
                            }
                            _captureDebugLogged = true;
                        }
                    }
                    const auto width = static_cast<tjs_uint>(
                        renderedLayer->GetImageWidth());
                    const auto height = static_cast<tjs_uint>(
                        renderedLayer->GetImageHeight());
                    if(width > 0 && height > 0) {
                        if(layer != renderedLayer) {
                            if(!layer->GetHasImage()) layer->SetHasImage(true);
                            layer->SetImageSize(width, height);
                            layer->CopyRect(
                                0, 0, renderedLayer->GetMainImage(), nullptr,
                                tTVPRect(0, 0, static_cast<tjs_int>(width),
                                         static_cast<tjs_int>(height)));
                        }
                        layer->Update(false);
                        if(result) *result = *param[0];
                        return TJS_S_OK;
                    }
                }
            }

            if(_width <= 0 || _height <= 0 || _buffer.empty()) {
                return TJS_S_OK;
            }

            if(!layer->GetHasImage()) layer->SetHasImage(true);
            layer->SetImageSize(static_cast<tjs_uint>(_width),
                                static_cast<tjs_uint>(_height));

            auto *dst = reinterpret_cast<std::uint8_t *>(
                layer->GetMainImagePixelBufferForWrite());
            auto dstPitch = layer->GetMainImagePixelBufferPitch();
            if(!dst || dstPitch <= 0) return TJS_S_OK;

            const auto srcPitch = static_cast<tjs_int>(_width * 4);
            for(int y = 0; y < _height; ++y) {
                std::memcpy(dst + dstPitch * y,
                            _buffer.data() + srcPitch * y,
                            static_cast<size_t>(srcPitch));
            }

            layer->Update(false);

            if(result) *result = *param[0];
            return TJS_S_OK;
        }

        // Static callback wrapper for NCB registration
        static tjs_error captureCanvasStatic(tTJSVariant *result, tjs_int numparams,
                                             tTJSVariant **param,
                                             D3DAdaptor *nativeInstance) {
            if(!nativeInstance) return TJS_E_NATIVECLASSCRASH;
            return nativeInstance->captureCanvas(result, numparams, param, nullptr);
        }

        // Buffer access (for Player to render into)
        int getWidth() const { return _width; }
        int getHeight() const { return _height; }
        iTJSDispatch2 *getWindowObject() const {
            return _window.Type() == tvtObject ? _window.AsObjectNoAddRef()
                                               : nullptr;
        }
        std::uint8_t *getBuffer() { return _buffer.data(); }
        const std::uint8_t *getBuffer() const { return _buffer.data(); }
        tjs_int getBufferPitch() const { return _width * 4; }
        size_t getBufferSize() const { return _buffer.size(); }

        void clearBuffer() {
            if(!_buffer.empty()) {
                std::memset(_buffer.data(), 0, _buffer.size());
            }
        }

    private:
        void allocBuffer() {
            if(_width > 0 && _height > 0) {
                _buffer.resize(static_cast<size_t>(_width) * _height * 4, 0);
            } else {
                _buffer.clear();
            }
        }

        tTJSVariant _window;
        int _width = 0;
        int _height = 0;
        bool _visible = true;
        bool _canvasCaptureEnabled = false;
        bool _clearEnabled = false;
        bool _resizable = false;
        bool _alphaOpAdd = false;
        int _clearColor = 0;
        tTJSVariant _renderedLayer;
        tTJSVariant _retainedPresentationLayer;
        std::chrono::steady_clock::time_point _uncapturedDrawStartedAt{};
        std::size_t _drawsSinceCapture = 0;
        bool _captureDebugLogged = false;
        std::vector<std::uint8_t> _buffer;
    };

} // namespace motion
