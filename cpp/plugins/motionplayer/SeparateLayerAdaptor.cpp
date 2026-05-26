#include "SeparateLayerAdaptor.h"

#include <algorithm>
#include <utility>

#include "PlayerInternal.h"
#include "PlayerRenderInternal.h"

using namespace motion::internal;

namespace {

    void invalidateObjectVariantLike_0x6AC27C(tTJSVariant &value) {
        if(value.Type() == tvtObject && value.AsObjectNoAddRef()) {
            auto closure = value.AsObjectClosureNoAddRef();
            if(closure.Object) {
                closure.Invalidate(0, nullptr, nullptr, nullptr);
            }
        }
        value.Clear();
    }

    tTJSVariant separateLayerOwnerLike_0x6C69D4(
        const tTJSVariant &targetLayer) {
        tTJSVariant targetCopy(targetLayer);
        tTJSVariant owner;
        if(targetCopy.Type() != tvtObject || !targetCopy.AsObjectNoAddRef()) {
            return owner;
        }

        iTJSDispatch2 *targetObject = targetCopy.AsObjectNoAddRef();
        targetObject->PropGet(0, TJS_W("window"), nullptr, &owner,
                              targetObject);
        return owner;
    }

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

    iTJSDispatch2 *resolveAssignableLayer(const tTJSVariant &value) {
        if(value.Type() != tvtObject || !value.AsObjectNoAddRef()) {
            return nullptr;
        }

        if(auto *adaptor =
               ncbInstanceAdaptor<motion::SeparateLayerAdaptor>::GetNativeInstance(
                   value.AsObjectNoAddRef(), false)) {
            if(auto *privateTarget = adaptor->getPrivateRenderTargetObject()) {
                return privateTarget;
            }
            if(auto *target = tryResolveLayerDispatch(adaptor->getTargetLayer())) {
                return target;
            }
            return adaptor->getOwner();
        }

        return tryResolveLayerDispatch(value);
    }

    bool getIntegerPropertyLike_0x6AC410(iTJSDispatch2 *object,
                                         const tjs_char *name,
                                         tjs_int &out) {
        out = 0;
        if(!object) {
            return false;
        }
        tTJSVariant value;
        const tjs_error hr = object->PropGet(TJS_MEMBERMUSTEXIST, name, nullptr,
                                             &value, object);
        if(TJS_FAILED(hr)) {
            return false;
        }
        out = static_cast<tjs_int>(value.AsInteger());
        return true;
    }

    void setIntegerPropertyLike_0x6AC410(iTJSDispatch2 *object,
                                         const tjs_char *name,
                                         tjs_int value) {
        if(!object) {
            return;
        }
        tTJSVariant variant(value);
        object->PropSet(TJS_MEMBERENSURE, name, nullptr, &variant, object);
    }

    void callSetSizeLike_0x6AC410(iTJSDispatch2 *object,
                                  tjs_int width,
                                  tjs_int height) {
        if(!object) {
            return;
        }
        tTJSVariant widthVar(width);
        tTJSVariant heightVar(height);
        tTJSVariant *args[] = { &widthVar, &heightVar };
        object->FuncCall(0, TJS_W("setSize"), nullptr, nullptr, 2, args,
                         object);
    }

    void callAssignImagesLike_0x6AC410(iTJSDispatch2 *target,
                                       const tTJSVariant &sourceVariant) {
        if(!target) {
            return;
        }
        tTJSVariant sourceArg(sourceVariant);
        tTJSVariant *args[] = { &sourceArg };
        target->FuncCall(0, TJS_W("assignImages"), nullptr, nullptr, 1, args,
                         target);
    }

    iTJSDispatch2 *createLayerNodeObjectLike_0x6C6B48(
        iTJSDispatch2 *ownerObject,
        const tTJSVariant &targetLayer) {
        if(!ownerObject) {
            return nullptr;
        }

        tTJSVariant layerClassVar;
        if(!render_detail::getLayerClassDispatchVariantLike_0x5CB08C(
               layerClassVar) ||
           layerClassVar.Type() != tvtObject ||
           !layerClassVar.AsObjectNoAddRef()) {
            return nullptr;
        }

        iTJSDispatch2 *created = nullptr;
        tTJSVariant ownerArg(ownerObject, ownerObject);
        tTJSVariant targetArg(targetLayer);
        tTJSVariant *args[] = { &ownerArg, &targetArg };
        const tjs_error hr = layerClassVar.AsObjectNoAddRef()->CreateNew(
            0, nullptr, nullptr, &created, 2, args,
            layerClassVar.AsObjectNoAddRef());
        if(TJS_FAILED(hr)) {
            return nullptr;
        }
        return created;
    }

} // namespace

namespace motion {

    NativeSLAPayloadLike_0x6DCD0C
    NativeSLAPayloadLike_0x6DCD0C::fromLayerVariant(
        const tTJSVariant &layer,
        tjs_uint32 ordinal) {
        NativeSLAPayloadLike_0x6DCD0C payload;
        payload.layerVariant = layer;
        payload.flags = static_cast<tjs_int>(ordinal);
        if(auto *object = resolveAssignableLayer(layer)) {
            tjs_int value = 0;
            if(getIntegerPropertyLike_0x6AC410(object, TJS_W("type"), value)) {
                payload.type = value;
            }
            if(getIntegerPropertyLike_0x6AC410(object, TJS_W("visible"), value)) {
                payload.visible = value != 0;
            }
            if(getIntegerPropertyLike_0x6AC410(object, TJS_W("left"), value)) {
                payload.affine[0] = static_cast<float>(value);
            }
            if(getIntegerPropertyLike_0x6AC410(object, TJS_W("top"), value)) {
                payload.affine[1] = static_cast<float>(value);
            }
            if(getIntegerPropertyLike_0x6AC410(object, TJS_W("width"), value)) {
                payload.affine[2] = static_cast<float>(value);
            }
            if(getIntegerPropertyLike_0x6AC410(object, TJS_W("height"), value)) {
                payload.affine[3] = static_cast<float>(value);
            }
        }
        return payload;
    }

    bool NativeSLAPayloadLike_0x6DCD0C::compatibleWithLike_0x6DCB2C(
        const NativeSLAPayloadLike_0x6DCD0C &other) const {
        return type == other.type && visible == other.visible &&
               key == other.key && flags == other.flags &&
               affine == other.affine && vertices == other.vertices &&
               uvs == other.uvs && color == other.color &&
               origin == other.origin;
    }

    NativeSLAOrderedMapLike_0x6C6B48::~NativeSLAOrderedMapLike_0x6C6B48() {
        clear(false);
    }

    NativeSLANodeLike_0x6DCD0C &
    NativeSLAOrderedMapLike_0x6C6B48::ensure(tjs_uint32 ordinal) {
        auto [it, inserted] = _nodes.try_emplace(ordinal);
        if(inserted) {
            it->second.ordinal = ordinal;
        }
        return it->second;
    }

    void NativeSLAOrderedMapLike_0x6C6B48::erase(iterator it) {
        if(it != _nodes.end()) {
            _nodes.erase(it);
        }
    }

    void NativeSLAOrderedMapLike_0x6C6B48::clear(bool invalidateObjects) {
        for(auto &entry : _nodes) {
            if(invalidateObjects) {
                invalidateObjectVariantLike_0x6AC27C(
                    entry.second.payload.layerVariant);
            } else {
                entry.second.payload.layerVariant.Clear();
            }
        }
        _nodes.clear();
    }

    void NativeSLAOrderedMapLike_0x6C6B48::swapWith(
        NativeSLAOrderedMapLike_0x6C6B48 &other) {
        if(this == &other) {
            return;
        }
        _nodes.swap(other._nodes);
    }

    SeparateLayerAdaptor::SeparateLayerAdaptor(tTJSVariant targetLayer)
        : _owner(separateLayerOwnerLike_0x6C69D4(targetLayer)),
          _targetLayer(targetLayer) {}

    SeparateLayerAdaptor::~SeparateLayerAdaptor() {
        clearPrivateRenderState();
        clearNativeListsForDtor();
        _privateTarget.Clear();
        _targetLayer.Clear();
        _owner.Clear();
    }

    tTJSVariant SeparateLayerAdaptor::getPrivateRenderTarget() const {
        return _privateTarget;
    }

    iTJSDispatch2 *SeparateLayerAdaptor::getPrivateRenderTargetObject() const {
        return _privateTarget.Type() == tvtObject
                   ? _privateTarget.AsObjectNoAddRef()
                   : nullptr;
    }

    void SeparateLayerAdaptor::trackManagedTargetLike_0x6AC410(
        const tTJSVariant &target,
        tjs_uint32 ordinal) {
        if(target.Type() != tvtObject || !target.AsObjectNoAddRef()) {
            return;
        }
        auto &node = _managedTargets.ensure(ordinal);
        node.payload = NativeSLAPayloadLike_0x6DCD0C::fromLayerVariant(
            target, ordinal);
    }

    void SeparateLayerAdaptor::clearPrivateRenderState() {
        // SeparateLayerAdaptor.clear @ 0x6AC27C treats SLA+40 as a
        // tTJSVariant: when vt==tvtObject it invalidates that object, clears
        // the variant slot, then invalidates and clears the +64/+72 list.
        if(_privateTarget.Type() == tvtObject) {
            invalidateObjectVariantLike_0x6AC27C(_privateTarget);
        } else {
            _privateTarget.Clear();
        }
        _managedTargets.clear(true);
    }

    void SeparateLayerAdaptor::clearNativeListsForDtor() {
        _assignTargets.clear(false);
    }

    void SeparateLayerAdaptor::clear() { clearPrivateRenderState(); }

    tjs_error SeparateLayerAdaptor::getLayerTreeOwnerInterfaceCompat(
        tTJSVariant *result,
        tjs_int,
        tTJSVariant **,
        iTJSDispatch2 *objthis) {
        if(result) {
            result->Clear();
        }

        auto *nativeInstance =
            ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                objthis, true);
        if(!nativeInstance || !result) {
            return nativeInstance ? TJS_S_OK : TJS_E_INVALIDOBJECT;
        }

        const tTJSVariant &owner = nativeInstance->getOwnerVariant();
        if(owner.Type() != tvtObject || !owner.AsObjectNoAddRef()) {
            return TJS_S_OK;
        }

        auto ownerClosure = owner.AsObjectClosureNoAddRef();
        if(!ownerClosure.Object) {
            return TJS_S_OK;
        }

        iTJSDispatch2 *ownerObjThis =
            ownerClosure.ObjThis ? ownerClosure.ObjThis : ownerClosure.Object;
        return ownerClosure.Object->PropGet(
            0, TJS_W("layerTreeOwnerInterface"), nullptr, result,
            ownerObjThis);
    }

    tTJSVariant SeparateLayerAdaptor::resolveLayerNodeLike_0x6C6B48(
        tjs_uint32 ordinal,
        const NativeSLAPayloadLike_0x6DCD0C &sourcePayload,
        iTJSDispatch2 *objthis,
        bool &createdOrChanged) {
        createdOrChanged = true;
        auto &active = _managedTargets.ensure(ordinal);
        active.payload = sourcePayload;
        active.payload.layerVariant.Clear();

        auto retired = _assignTargets.find(ordinal);
        if(retired != _assignTargets.end() &&
           sourcePayload.compatibleWithLike_0x6DCB2C(
               retired->second.payload)) {
            active.payload.layerVariant =
                retired->second.payload.layerVariant;
            createdOrChanged = false;
            _assignTargets.erase(retired);
        }

        if(active.payload.layerVariant.Type() != tvtObject ||
           !active.payload.layerVariant.AsObjectNoAddRef()) {
            if(iTJSDispatch2 *created =
                   createLayerNodeObjectLike_0x6C6B48(objthis,
                                                      _targetLayer)) {
                active.payload.layerVariant = tTJSVariant(created, created);
                created->Release();
            }
        }

        if(iTJSDispatch2 *object =
               resolveAssignableLayer(active.payload.layerVariant)) {
            setIntegerPropertyLike_0x6AC410(
                object, TJS_W("absolute"),
                static_cast<tjs_int>(_absolute + _assignSequence));
            ++_assignSequence;
            setIntegerPropertyLike_0x6AC410(object, TJS_W("hitThreshold"),
                                            256);
        }

        return active.payload.layerVariant;
    }

    tjs_error SeparateLayerAdaptor::assignFromAdaptorLike_0x6AC410(
        const SeparateLayerAdaptor &source,
        iTJSDispatch2 *objthis) {
        // sub_6AC410 first swaps the destination's two native list slots
        // (+64/+72 and +112/+120), then walks the source +64/+72 list.
        _managedTargets.swapWith(_assignTargets);
        _managedTargets.clear(true);
        _assignSequence = 0;

        for(const auto &entry : source._managedTargets) {
            const tjs_uint32 ordinal = entry.first;
            const auto &sourcePayload = entry.second.payload;
            const tTJSVariant &sourceVariant = sourcePayload.layerVariant;
            iTJSDispatch2 *sourceLayerObject =
                resolveAssignableLayer(sourceVariant);
            if(!sourceLayerObject) {
                continue;
            }

            bool createdOrChanged = false;
            tTJSVariant targetVariant = resolveLayerNodeLike_0x6C6B48(
                ordinal, sourcePayload, objthis, createdOrChanged);
            (void)createdOrChanged;
            iTJSDispatch2 *targetLayerObject =
                resolveAssignableLayer(targetVariant);
            if(!targetLayerObject) {
                continue;
            }

            callAssignImagesLike_0x6AC410(targetLayerObject, sourceVariant);

            tjs_int width = 0;
            tjs_int height = 0;
            getIntegerPropertyLike_0x6AC410(sourceLayerObject, TJS_W("width"),
                                            width);
            getIntegerPropertyLike_0x6AC410(sourceLayerObject, TJS_W("height"),
                                            height);
            callSetSizeLike_0x6AC410(targetLayerObject, width, height);

            tjs_int absolute = 0;
            tjs_int visible = 0;
            tjs_int opacity = 0;
            tjs_int type = 0;
            tjs_int left = 0;
            tjs_int top = 0;
            getIntegerPropertyLike_0x6AC410(sourceLayerObject, TJS_W("absolute"),
                                            absolute);
            getIntegerPropertyLike_0x6AC410(sourceLayerObject, TJS_W("visible"),
                                            visible);
            getIntegerPropertyLike_0x6AC410(sourceLayerObject, TJS_W("opacity"),
                                            opacity);
            getIntegerPropertyLike_0x6AC410(sourceLayerObject, TJS_W("type"),
                                            type);
            getIntegerPropertyLike_0x6AC410(sourceLayerObject, TJS_W("left"),
                                            left);
            getIntegerPropertyLike_0x6AC410(sourceLayerObject, TJS_W("top"),
                                            top);

            (void)absolute;
            setIntegerPropertyLike_0x6AC410(targetLayerObject, TJS_W("visible"),
                                            visible);
            setIntegerPropertyLike_0x6AC410(targetLayerObject, TJS_W("opacity"),
                                            opacity);
            setIntegerPropertyLike_0x6AC410(targetLayerObject, TJS_W("type"),
                                            type);
            setIntegerPropertyLike_0x6AC410(targetLayerObject, TJS_W("left"),
                                            left);
            setIntegerPropertyLike_0x6AC410(targetLayerObject, TJS_W("top"),
                                            top);
        }

        _assignTargets.clear(true);
        return TJS_S_OK;
    }

    tjs_error SeparateLayerAdaptor::assignCompat(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
        if(result) {
            result->Clear();
        }

        auto *nativeInstance =
            ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(objthis, true);
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        SeparateLayerAdaptor *sourceAdaptor = nullptr;
        if(numparams > 0 && param && param[0] &&
           param[0]->Type() == tvtObject && param[0]->AsObjectNoAddRef()) {
            sourceAdaptor =
                ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                    param[0]->AsObjectNoAddRef(), false);
        }

        if(sourceAdaptor) {
            return nativeInstance->assignFromAdaptorLike_0x6AC410(
                *sourceAdaptor, objthis);
        }

        return TJS_S_OK;
    }

} // namespace motion
