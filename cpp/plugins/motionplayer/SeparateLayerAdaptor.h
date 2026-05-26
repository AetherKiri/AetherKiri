//
// Created by LiDon on 2025/9/15.
//
#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <vector>

#include "tjs.h"

namespace motion {

    struct NativeSLAPayloadLike_0x6DCD0C {
        tTJSVariant layerVariant;
        tjs_int type = 0;
        bool visible = true;
        ttstr key;
        tjs_int flags = 0;
        std::array<float, 8> affine{};
        std::vector<float> vertices;
        std::vector<float> uvs;
        std::array<float, 8> color{};
        std::array<float, 2> origin{};

        static NativeSLAPayloadLike_0x6DCD0C fromLayerVariant(
            const tTJSVariant &layer,
            tjs_uint32 ordinal);
        bool compatibleWithLike_0x6DCB2C(
            const NativeSLAPayloadLike_0x6DCD0C &other) const;
    };

    struct NativeSLANodeLike_0x6DCD0C {
        tjs_uint32 ordinal = 0;
        NativeSLAPayloadLike_0x6DCD0C payload;
    };

    class NativeSLAOrderedMapLike_0x6C6B48 {
    public:
        using Map = std::map<tjs_uint32, NativeSLANodeLike_0x6DCD0C>;
        using iterator = Map::iterator;
        using const_iterator = Map::const_iterator;

        NativeSLAOrderedMapLike_0x6C6B48() = default;
        ~NativeSLAOrderedMapLike_0x6C6B48();

        NativeSLAOrderedMapLike_0x6C6B48(
            const NativeSLAOrderedMapLike_0x6C6B48 &) =
            delete;
        NativeSLAOrderedMapLike_0x6C6B48 &
        operator=(const NativeSLAOrderedMapLike_0x6C6B48 &) = delete;

        iterator begin() { return _nodes.begin(); }
        iterator end() { return _nodes.end(); }
        const_iterator begin() const { return _nodes.begin(); }
        const_iterator end() const { return _nodes.end(); }
        bool empty() const { return _nodes.empty(); }

        NativeSLANodeLike_0x6DCD0C &ensure(tjs_uint32 ordinal);
        iterator find(tjs_uint32 ordinal) { return _nodes.find(ordinal); }
        const_iterator find(tjs_uint32 ordinal) const {
            return _nodes.find(ordinal);
        }
        void erase(iterator it);
        void clear(bool invalidateObjects);
        void swapWith(NativeSLAOrderedMapLike_0x6C6B48 &other);

    private:
        Map _nodes;
    };

    class SeparateLayerAdaptor {
    public:
        explicit SeparateLayerAdaptor(tTJSVariant targetLayer = {});
        ~SeparateLayerAdaptor();

        static tjs_error factory(SeparateLayerAdaptor **result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
            tTJSVariant targetLayer;
            if(numparams > 0 && param[0]) {
                targetLayer = *param[0];
            }
            if(result) *result = new SeparateLayerAdaptor(targetLayer);
            return TJS_S_OK;
        }

        iTJSDispatch2 *getOwner() const {
            return _owner.Type() == tvtObject ? _owner.AsObjectNoAddRef() : nullptr;
        }

        const tTJSVariant &getOwnerVariant() const {
            return _owner;
        }

        // Aligned to libkrkr2.so SeparateLayerAdaptor_ncb_registerMembers (0x6ABFAC)
        bool getAbsolute() const { return _absolute; }
        void setAbsolute(bool v) { _absolute = v; }
        tTJSVariant getTargetLayer() const { return _targetLayer; }
        void setTargetLayer(tTJSVariant v) { _targetLayer = v; }

        tTJSVariant getPrivateRenderTarget() const;
        iTJSDispatch2 *getPrivateRenderTargetObject() const;
        void clear();
        static tjs_error assignCompat(tTJSVariant *result, tjs_int numparams,
                                      tTJSVariant **param,
                                      iTJSDispatch2 *objthis);
        static tjs_error getLayerTreeOwnerInterfaceCompat(
            tTJSVariant *result,
            tjs_int numparams,
            tTJSVariant **param,
            iTJSDispatch2 *objthis);

    private:
        friend iTJSDispatch2 *ensurePrivateMotionGLLLike_0x6D5948(
            SeparateLayerAdaptor &sla,
            const tTJSVariant &ownerVariant,
            const tTJSVariant &targetLayerVariant,
            iTJSDispatch2 *targetLayerObject,
            int canvasWidth,
            int canvasHeight);

        void trackManagedTargetLike_0x6AC410(const tTJSVariant &target,
                                             tjs_uint32 ordinal);
        void clearPrivateRenderState();
        void clearNativeListsForDtor();
        tjs_error assignFromAdaptorLike_0x6AC410(
            const SeparateLayerAdaptor &source,
            iTJSDispatch2 *objthis);
        tTJSVariant resolveLayerNodeLike_0x6C6B48(
            tjs_uint32 ordinal,
            const NativeSLAPayloadLike_0x6DCD0C &sourcePayload,
            iTJSDispatch2 *objthis,
            bool &createdOrChanged);

        // Native SeparateLayerAdaptor layout from libkrkr2.so:
        // +0 owner variant, +20 targetLayer variant, +40 private target
        // variant. The native "state" checked at +56 is the vt field of the
        // +40 tTJSVariant; tvtObject means the PrivateMotionGLL object exists.
        tTJSVariant _owner;
        tTJSVariant _targetLayer;
        tTJSVariant _privateTarget;
        NativeSLAOrderedMapLike_0x6C6B48 _managedTargets;
        NativeSLAOrderedMapLike_0x6C6B48 _assignTargets;
        tjs_uint32 _absolute = 0;
        tjs_uint32 _assignSequence = 0;
    };
} // namespace motion
