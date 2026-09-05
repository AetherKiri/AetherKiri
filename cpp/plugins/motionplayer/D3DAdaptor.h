//
// D3DAdaptor — matches libkrkr2.so Motion.D3DAdaptor
// Reverse-engineered from sub_6ADB10 (constructor) and sub_6ACE94 (members)
//
#pragma once

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <spdlog/spdlog.h>
#include "godot/GodotGpuBridge.h"
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

        // Keep one explicit bridge batch open across the complete D3DEmote
        // transaction (draw, capture, redraw, and final layer assignment).
        // Player's own render scopes nest inside this one without draining.
        bool beginGpuBatch() {
            if(_gpuBatchDepth != 0) {
                ++_gpuBatchDepth;
                return _gpuBatch && _gpuBatch->active();
            }

            // Commit the local nesting state only after the scope has been
            // constructed.  If allocation or the bridge callback throws, a
            // script-side compatibility wrapper can continue drawing without
            // leaving this adaptor permanently stuck at depth one.
            auto batch = std::make_unique<TVPGodotGpuBatchScope>();
            _gpuBatch = std::move(batch);
            _gpuBatchDepth = 1;
            return _gpuBatch->active();
        }

        bool endGpuBatch() {
            if(_gpuBatchDepth == 0) return true;
            if(--_gpuBatchDepth != 0) return true;
            if(!_gpuBatch) return true;
            auto batch = std::move(_gpuBatch);
            return batch->finish();
        }

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

        // AffineSourceMotion draws into the shared adaptor and commits the
        // completed surface to its authored character layer in TJS. During a
        // cold native-player replacement, keep that final layer untouched for
        // the first rendered frame so the SDK's default pose is never
        // published as a whole-character flash.
        void setPresentationTarget(tTJSVariant target) {
            _presentationTarget = target;
            _presentationHold = false;
            _presentationProbeLogged = false;
        }

        void clearPresentationTarget() {
            _presentationTarget.Clear();
            _presentationHold = false;
        }

        bool getPresentationHold() const { return _presentationHold; }

        // True when the most recent captureCanvas call copied an already
        // rendered layer through the GPU path. D3DEmote can then hand the
        // captured frame directly to AssignMotionImages; calling the legacy
        // script-side _redrawImage would redraw the same frame a second time.
        bool getGpuCapture() const { return _lastCaptureGpu; }

        // A freshly cloned native E-mote player can be drawn once while KAG
        // is still assembling the destination page. Keep that rendered frame
        // in the private adaptor surface, but let captureCanvas decide whether
        // the currently visible authored target already has a stable image.
        // A hidden page is the destination of the transition and must still
        // receive the new frame; otherwise the transition source becomes an
        // empty page and the character disappears before the fade starts.
        void deferNativePresentationOnce() {
            _deferNativePresentationOnce = true;
        }

        bool preparePresentationHoldIfTargetHasImage() {
            _presentationHold = false;
            auto logger = spdlog::get("plugin");
            const char *debug = std::getenv("AETHERKIRI_MOTION_DEBUG");
            const bool logProbe = debug && *debug && std::strcmp(debug, "0") != 0 &&
                                  logger && !_presentationProbeLogged;
            if(_presentationTarget.Type() != tvtObject) {
                if(logProbe) {
                    logger->info(
                        "native motion presentation target probe: targetType={} targetObject=<null> result=no-object",
                        static_cast<int>(_presentationTarget.Type()));
                    _presentationProbeLogged = true;
                }
                return false;
            }
            auto *targetObject = _presentationTarget.AsObjectNoAddRef();
            tTJSNI_BaseLayer *targetLayer = nullptr;
            const bool layerResolved = targetObject &&
                TJS_SUCCEEDED(targetObject->NativeInstanceSupport(
                    TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                    reinterpret_cast<iTJSNativeInstance **>(&targetLayer))) &&
                targetLayer;
            if(logProbe) {
                logger->info(
                    "native motion presentation target probe: targetObject={} layerResolved={} layer={} hasImage={} mainImage={} size={}x{} visible={} parentVisible={} result={}",
                    static_cast<const void *>(targetObject),
                    layerResolved ? 1 : 0,
                    static_cast<const void *>(targetLayer),
                    layerResolved && targetLayer->GetHasImage() ? 1 : 0,
                    layerResolved && targetLayer->GetMainImage() ? 1 : 0,
                    layerResolved ? targetLayer->GetImageWidth() : 0,
                    layerResolved ? targetLayer->GetImageHeight() : 0,
                    layerResolved && targetLayer->GetVisible() ? 1 : 0,
                    layerResolved && targetLayer->GetParentVisible() ? 1 : 0,
                    layerResolved && targetLayer->GetHasImage() &&
                            targetLayer->GetMainImage()
                        ? "hold"
                        : "publish");
                _presentationProbeLogged = true;
            }
            if(!layerResolved) {
                return false;
            }
            _presentationHold = targetLayer->GetHasImage() &&
                                targetLayer->GetMainImage() != nullptr;
            return _presentationHold;
        }

        // captureCanvas: copies the most recently rendered image into a TJS
        // Layer. Godot-backed layers preserve this CopyRect on the ordered GPU
        // queue instead of downloading the complete canvas and uploading it
        // again every animation frame.
        tjs_error captureCanvas(tTJSVariant *result, tjs_int numparams,
                                tTJSVariant **param, iTJSDispatch2 *objthis) {
            if(numparams < 1 || !param[0]) return TJS_E_BADPARAMCOUNT;

            const bool profileCapture = captureProfileEnabled();
            _lastCaptureGpu = false;
            const auto captureStarted =
                profileCapture ? std::chrono::steady_clock::now()
                               : std::chrono::steady_clock::time_point{};

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
            const bool deferNativePresentation =
                _deferNativePresentationOnce;
            _deferNativePresentationOnce = false;
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
                    if(deferNativePresentation && layer != renderedLayer &&
                       layer->GetVisible() && layer->GetParentVisible() &&
                       layer->GetHasImage() && layer->GetMainImage()) {
                        if(auto logger = spdlog::get("plugin")) {
                            logger->info(
                                "motion d3d capture deferred native first frame: "
                                "target={} rendered={} size={}x{}",
                                static_cast<const void *>(layer),
                                static_cast<const void *>(renderedLayer),
                                layer->GetImageWidth(),
                                layer->GetImageHeight());
                        }
                        if(result) *result = *param[0];
                        return TJS_S_OK;
                    }
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
                        double copyMs = 0.0;
                        if(layer != renderedLayer) {
                            if(!layer->GetHasImage()) layer->SetHasImage(true);
                            layer->SetImageSize(width, height);
                            const auto copyStarted =
                                std::chrono::steady_clock::now();
                            layer->CopyRect(
                                0, 0, renderedLayer->GetMainImage(), nullptr,
                                tTVPRect(0, 0, static_cast<tjs_int>(width),
                                         static_cast<tjs_int>(height)));
                            copyMs = std::chrono::duration<double, std::milli>(
                                         std::chrono::steady_clock::now() -
                                         copyStarted)
                                         .count();
                        }
                        const auto updateStarted =
                            std::chrono::steady_clock::now();
                        layer->Update(false);
                        const double updateMs =
                            std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() -
                                updateStarted)
                                .count();
                        logCaptureProfile(
                            layer == renderedLayer ? "gpu-same-layer"
                                                   : "gpu-copy-rect",
                            width, height, captureStarted, copyMs, updateMs);
                        _lastCaptureGpu = true;
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
            const auto copyStarted = std::chrono::steady_clock::now();
            for(int y = 0; y < _height; ++y) {
                std::memcpy(dst + dstPitch * y,
                            _buffer.data() + srcPitch * y,
                            static_cast<size_t>(srcPitch));
            }
            const double copyMs = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() -
                                      copyStarted)
                                      .count();

            const auto updateStarted = std::chrono::steady_clock::now();
            layer->Update(false);
            const double updateMs = std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() -
                                        updateStarted)
                                        .count();
            logCaptureProfile("cpu-buffer-copy", _width, _height,
                              captureStarted, copyMs, updateMs);

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
        static bool captureProfileEnabled() {
            const char *value =
                std::getenv("AETHERKIRI_MOTION_RENDER_PROFILE");
            return value != nullptr && value[0] != '\0' &&
                   std::strcmp(value, "0") != 0;
        }

        static double captureProfileSlowMs() {
            const char *value =
                std::getenv("AETHERKIRI_MOTION_COPY_SLOW_MS");
            if(value == nullptr || value[0] == '\0') return 5.0;
            char *end = nullptr;
            const double parsed = std::strtod(value, &end);
            return end != value && parsed > 0.0 ? parsed : 5.0;
        }

        static void logCaptureProfile(
            const char *route, uint32_t width, uint32_t height,
            std::chrono::steady_clock::time_point started, double copyMs,
            double updateMs) {
            if(started.time_since_epoch().count() == 0) return;
            const double totalMs =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started)
                    .count();
            if(totalMs < captureProfileSlowMs() &&
               copyMs < captureProfileSlowMs() &&
               updateMs < captureProfileSlowMs()) {
                return;
            }
            if(auto logger = spdlog::get("plugin")) {
                logger->info(
                    "motion capture profile: route={} total_ms={:.3f} "
                    "copy_ms={:.3f} update_ms={:.3f} size={}x{}",
                    route != nullptr ? route : "unknown", totalMs, copyMs,
                    updateMs, width, height);
            }
        }

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
        std::unique_ptr<TVPGodotGpuBatchScope> _gpuBatch;
        std::uint32_t _gpuBatchDepth = 0;
        tTJSVariant _retainedPresentationLayer;
        std::chrono::steady_clock::time_point _uncapturedDrawStartedAt{};
        std::size_t _drawsSinceCapture = 0;
        bool _captureDebugLogged = false;
        tTJSVariant _presentationTarget;
        bool _presentationHold = false;
        bool _lastCaptureGpu = false;
        bool _presentationProbeLogged = false;
        bool _deferNativePresentationOnce = false;
        std::vector<std::uint8_t> _buffer;
    };

} // namespace motion
