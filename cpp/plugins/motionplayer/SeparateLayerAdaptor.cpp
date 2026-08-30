#include "PlayerInternal.h"

using namespace motion::internal;

namespace {

    tTJSNI_BaseLayer *resolveNativeLayer(iTJSDispatch2 *layerObject) {
        if(!layerObject) {
            return nullptr;
        }
        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(layerObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
            return nullptr;
        }
        return layer;
    }

} // namespace

namespace motion {

    void SeparateLayerAdaptor::trackManagedTarget(const tTJSVariant &target) {
        if(target.Type() != tvtObject || !target.AsObjectNoAddRef()) {
            return;
        }
        const auto *ptr = target.AsObjectNoAddRef();
        for(const auto &existing : _managedTargets) {
            if(existing.Type() == tvtObject && existing.AsObjectNoAddRef() == ptr) {
                return;
            }
        }
        _managedTargets.push_back(target);
    }

    void SeparateLayerAdaptor::setPrivateRenderTarget(tTJSVariant v) {
        _privateRenderTarget = v;
        trackManagedTarget(v);
    }

    void SeparateLayerAdaptor::clearPrivateRenderState() {
        for(auto &target : _managedTargets) {
            if(target.Type() != tvtObject || !target.AsObjectNoAddRef()) {
                continue;
            }
            if(auto *layer = resolveNativeLayer(target.AsObjectNoAddRef())) {
                layer->SetVisible(false);
            }
            target.Clear();
        }
        _managedTargets.clear();
        _privateRenderTarget.Clear();
    }

    void SeparateLayerAdaptor::c() { clearPrivateRenderState(); }

    tjs_error SeparateLayerAdaptor::assignCompat(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
        if(result) {
            result->Clear();
        }

        if(!ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
               objthis, true)) {
            return TJS_E_INVALIDOBJECT;
        }
        (void)numparams;
        (void)param;
        // krkrsdl3 leaves SeparateLayerAdaptor::assign as a no-op. The
        // adaptor's private child is already the visible presentation layer;
        // copying it into the authored owner creates a second, offset frame.
        return TJS_S_OK;
    }

} // namespace motion
