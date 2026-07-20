#define NCB_MODULE_NAME TJS_W("DrawDeviceD2D.dll")

#include "ncbind.hpp"
#include "tjs.h"
#include "DrawDevice.h"
#include "visual/WindowIntf.h"
#include <algorithm>
#include <vector>

// Some titles ship the original DrawDeviceD2D.dll while others request the
// extended DrawDeviceD2Dm.dll name.  They are one compatibility implementation
// here, so both filenames must resolve to the same registrar/state.
NCB_REGISTER_MODULE_ALIAS(TJS_W("DrawDeviceD2Dm.dll"), NCB_MODULE_NAME,
                          DrawDeviceD2DMCompat);

namespace {

bool GetMemberVariant(iTJSDispatch2 *obj, const tjs_char *name, tTJSVariant &out) {
    if(!obj)
        return false;
    return TJS_SUCCEEDED(obj->PropGet(TJS_IGNOREPROP, name, nullptr, &out, obj));
}

bool SetMemberVariant(iTJSDispatch2 *obj, const tjs_char *name,
                      const tTJSVariant &value) {
    if(!obj)
        return false;
    tTJSVariant copy(value);
    return TJS_SUCCEEDED(
        obj->PropSet(TJS_MEMBERENSURE, name, nullptr, &copy, obj));
}

bool GetMemberInt(iTJSDispatch2 *obj, const tjs_char *name, tjs_int &out) {
    tTJSVariant value;
    if(!GetMemberVariant(obj, name, value))
        return false;
    out = static_cast<tjs_int>(value);
    return true;
}

bool GetMemberReal(iTJSDispatch2 *obj, const tjs_char *name, tjs_real &out) {
    tTJSVariant value;
    if(!GetMemberVariant(obj, name, value))
        return false;
    out = static_cast<tjs_real>(value);
    return true;
}

bool GetArrayInt(iTJSDispatch2 *obj, tjs_int index, tjs_int &out) {
    if(!obj)
        return false;
    tTJSVariant value;
    if(TJS_FAILED(obj->PropGetByNum(TJS_IGNOREPROP, index, &value, obj)))
        return false;
    out = static_cast<tjs_int>(value);
    return true;
}

bool SetArrayInt(iTJSDispatch2 *obj, tjs_int index, tjs_int value) {
    if(!obj)
        return false;
    tTJSVariant var(value);
    return TJS_SUCCEEDED(obj->PropSetByNum(TJS_MEMBERENSURE, index, &var, obj));
}

bool IsValidRect(const tTVPRect &rect) {
    return rect.right > rect.left && rect.bottom > rect.top;
}

tTVPRect NormalizeRect(const tTVPRect &rect) {
    tTVPRect normalized = rect;
    if(normalized.left > normalized.right)
        std::swap(normalized.left, normalized.right);
    if(normalized.top > normalized.bottom)
        std::swap(normalized.top, normalized.bottom);
    return normalized;
}

bool ExtractRectFromArrayLike(iTJSDispatch2 *obj, tTVPRect &out) {
    tjs_int left = 0;
    tjs_int top = 0;
    tjs_int right = 0;
    tjs_int bottom = 0;
    if(GetArrayInt(obj, 0, left) && GetArrayInt(obj, 1, top) &&
       GetArrayInt(obj, 2, right) && GetArrayInt(obj, 3, bottom)) {
        out = NormalizeRect(tTVPRect(left, top, right, bottom));
        return true;
    }
    return false;
}

bool ExtractRectFromObjectMembers(iTJSDispatch2 *obj, tTVPRect &out) {
    tjs_int left = 0;
    tjs_int top = 0;
    tjs_int right = 0;
    tjs_int bottom = 0;
    if(GetMemberInt(obj, TJS_W("left"), left) &&
       GetMemberInt(obj, TJS_W("top"), top) &&
       GetMemberInt(obj, TJS_W("right"), right) &&
       GetMemberInt(obj, TJS_W("bottom"), bottom)) {
        out = NormalizeRect(tTVPRect(left, top, right, bottom));
        return true;
    }
    tjs_int x = 0;
    tjs_int y = 0;
    tjs_int width = 0;
    tjs_int height = 0;
    if(GetMemberInt(obj, TJS_W("x"), x) && GetMemberInt(obj, TJS_W("y"), y) &&
       GetMemberInt(obj, TJS_W("width"), width) &&
       GetMemberInt(obj, TJS_W("height"), height)) {
        out = NormalizeRect(tTVPRect(x, y, x + width, y + height));
        return true;
    }
    return false;
}

bool ExtractRectFromVariant(const tTJSVariant &value, tTVPRect &out);

bool ExtractRectViaGetRect(iTJSDispatch2 *obj, tTVPRect &out) {
    tTJSVariant methodVar;
    if(!GetMemberVariant(obj, TJS_W("getRect"), methodVar) ||
       methodVar.Type() != tvtObject)
        return false;
    tTJSVariant result;
    tTJSVariantClosure closure = methodVar.AsObjectClosureNoAddRef();
    if(!closure.Object)
        return false;
    if(TJS_FAILED(
           closure.FuncCall(0, nullptr, nullptr, &result, 0, nullptr, obj)))
        return false;
    return ExtractRectFromVariant(result, out);
}

bool ExtractRectFromVariant(const tTJSVariant &value, tTVPRect &out) {
    if(value.Type() != tvtObject)
        return false;
    iTJSDispatch2 *obj = value.AsObjectNoAddRef();
    if(!obj)
        return false;
    if(ExtractRectViaGetRect(obj, out))
        return true;
    if(ExtractRectFromArrayLike(obj, out))
        return true;
    return ExtractRectFromObjectMembers(obj, out);
}

iTJSDispatch2 *CreateRectObject(const tTVPRect &rect) {
    iTJSDispatch2 *array = TJSCreateArrayObject();
    if(!array)
        return nullptr;
    const tTVPRect normalized = NormalizeRect(rect);
    SetArrayInt(array, 0, normalized.left);
    SetArrayInt(array, 1, normalized.top);
    SetArrayInt(array, 2, normalized.right);
    SetArrayInt(array, 3, normalized.bottom);
    SetMemberVariant(array, TJS_W("left"), tTJSVariant(normalized.left));
    SetMemberVariant(array, TJS_W("top"), tTJSVariant(normalized.top));
    SetMemberVariant(array, TJS_W("right"), tTJSVariant(normalized.right));
    SetMemberVariant(array, TJS_W("bottom"), tTJSVariant(normalized.bottom));
    SetMemberVariant(array, TJS_W("x"), tTJSVariant(normalized.left));
    SetMemberVariant(array, TJS_W("y"), tTJSVariant(normalized.top));
    SetMemberVariant(array, TJS_W("width"),
                     tTJSVariant(normalized.get_width()));
    SetMemberVariant(array, TJS_W("height"),
                     tTJSVariant(normalized.get_height()));
    return array;
}

tTJSVariant CreateD2DViewObject() {
    auto *klass = ncbClassInfo<class D2DView>::GetClassObject();
    if(klass) {
        iTJSDispatch2 *instance = nullptr;
        if(TJS_SUCCEEDED(
               klass->CreateNew(0, nullptr, nullptr, &instance, 0, nullptr,
                                klass)) &&
           instance) {
            tTJSVariant result(instance, instance);
            instance->Release();
            return result;
        }
    }
    if(iTJSDispatch2 *fallback = CreateRectObject(tTVPRect())) {
        SetMemberVariant(fallback, TJS_W("type"), tTJSVariant(0));
        SetMemberVariant(fallback, TJS_W("visible"), tTJSVariant(1));
        SetMemberVariant(fallback, TJS_W("fopacity"), tTJSVariant(1.0));
        SetMemberVariant(fallback, TJS_W("opacity"), tTJSVariant(255));
        SetMemberVariant(fallback, TJS_W("order"), tTJSVariant(0));
        SetMemberVariant(fallback, TJS_W("zinterp"), tTJSVariant(0.0));
        tTJSVariant result(fallback, fallback);
        fallback->Release();
        return result;
    }
    return tTJSVariant();
}

tTJSVariant ExtractRootViewVariant(const tTJSVariant &value) {
    if(value.Type() != tvtObject)
        return tTJSVariant();
    iTJSDispatch2 *obj = value.AsObjectNoAddRef();
    if(!obj)
        return tTJSVariant();
    tTJSVariant root;
    if(GetMemberVariant(obj, TJS_W("root"), root))
        return root;
    return tTJSVariant();
}

tTJSVariant CreateD2DViewContainer() {
    const tTJSVariant root = CreateD2DViewObject();
    if(root.Type() != tvtObject)
        return root;

    iTJSDispatch2 *obj = root.AsObjectNoAddRef();
    if(!obj)
        return root;

    SetMemberVariant(obj, TJS_W("root"), root);
    SetMemberVariant(obj, TJS_W("base"), root);
    SetMemberVariant(obj, TJS_W("main"), root);
    return root;
}

bool ResolveRectArguments(tjs_int count, tTJSVariant **params, tTVPRect &out) {
    if(count >= 4) {
        out = NormalizeRect(tTVPRect(static_cast<tjs_int>(*params[0]),
                                     static_cast<tjs_int>(*params[1]),
                                     static_cast<tjs_int>(*params[2]),
                                     static_cast<tjs_int>(*params[3])));
        return true;
    }
    if(count >= 1)
        return ExtractRectFromVariant(*params[0], out);
    return false;
}

} // namespace

class D2DView {
public:
    static tjs_error factory(D2DView **result, tjs_int, tTJSVariant **,
                             iTJSDispatch2 *) {
        *result = new D2DView();
        return TJS_S_OK;
    }

    tjs_int getType() const { return type_; }
    void setType(tjs_int value) { type_ = value; }

    tjs_int getVisible() const { return visible_ ? 1 : 0; }
    void setVisible(tjs_int value) { visible_ = value != 0; }

    tjs_real getFopacity() const { return fopacity_; }
    void setFopacity(tjs_real value) { fopacity_ = value; }

    tjs_int getOpacity() const { return opacity_; }
    void setOpacity(tjs_int value) { opacity_ = value; }

    tjs_int getOrder() const { return order_; }
    void setOrder(tjs_int value) { order_ = value; }

    tjs_real getZinterp() const { return zinterp_; }
    void setZinterp(tjs_real value) { zinterp_ = value; }

    tTVPRect getRect() const { return rect_; }
    void setRectValue(const tTVPRect &value) { rect_ = NormalizeRect(value); }

    static tjs_error setRectCallback(tTJSVariant *, tjs_int count,
                                     tTJSVariant **params,
                                     iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<D2DView>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        tTVPRect rect;
        if(!ResolveRectArguments(count, params, rect))
            return TJS_E_BADPARAMCOUNT;
        self->setRectValue(rect);
        return TJS_S_OK;
    }

    static tjs_error getRectCallback(tTJSVariant *result, tjs_int,
                                     tTJSVariant **, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<D2DView>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        iTJSDispatch2 *rectObj = CreateRectObject(self->getRect());
        if(!rectObj)
            return TJS_E_FAIL;
        if(result)
            *result = tTJSVariant(rectObj, rectObj);
        rectObj->Release();
        return TJS_S_OK;
    }

    static tjs_error getSelfObjectProperty(tTJSVariant *result, tjs_int,
                                           tTJSVariant **,
                                           iTJSDispatch2 *objthis) {
        if(!objthis)
            return TJS_E_FAIL;
        if(result)
            *result = tTJSVariant(objthis, objthis);
        return TJS_S_OK;
    }

private:
    tjs_int type_ = 0;
    bool visible_ = true;
    tjs_real fopacity_ = 1.0;
    tjs_int opacity_ = 255;
    tjs_int order_ = 0;
    tjs_real zinterp_ = 0.0;
    tTVPRect rect_ = tTVPRect(0, 0, 0, 0);
};

NCB_REGISTER_SUBCLASS_DELAY(D2DView) {
    Factory(&D2DView::factory);
    Property(TJS_W("type"), &Class::getType, &Class::setType);
    Property(TJS_W("visible"), &Class::getVisible, &Class::setVisible);
    Property(TJS_W("fopacity"), &Class::getFopacity, &Class::setFopacity);
    Property(TJS_W("opacity"), &Class::getOpacity, &Class::setOpacity);
    Property(TJS_W("order"), &Class::getOrder, &Class::setOrder);
    Property(TJS_W("zinterp"), &Class::getZinterp, &Class::setZinterp);
    RawCallback(TJS_W("root"), &Class::getSelfObjectProperty, 0, 0);
    RawCallback(TJS_W("base"), &Class::getSelfObjectProperty, 0, 0);
    RawCallback(TJS_W("main"), &Class::getSelfObjectProperty, 0, 0);
    RawCallback(TJS_W("setRect"), &Class::setRectCallback, 0);
    RawCallback(TJS_W("getRect"), &Class::getRectCallback, 0);
}

class DrawDeviceD2D : public iTVPDrawDevice {
public:
    DrawDeviceD2D() : viewState_(CreateD2DViewContainer()) {}

    ~DrawDeviceD2D() { DestructExtraManagers(); }

    static tjs_error factory(DrawDeviceD2D **result, tjs_int, tTJSVariant **,
                             iTJSDispatch2 *) {
        *result = new DrawDeviceD2D();
        return TJS_S_OK;
    }

    tjs_int64 getinterface() const {
        return reinterpret_cast<tjs_int64>(
            const_cast<DrawDeviceD2D *>(this));
    }

    tTJSVariant getviewState() {
        EnsureViewStateObject();
        return viewState_;
    }

    void setviewState(const tTJSVariant &value) {
        viewState_ = value;
        EnsureViewStateObject();
        SyncViewStateRect();
        ApplyViewport();
        RequestFullInvalidation();
    }

    void recreate() {
        blanked_ = false;
        SyncViewStateRect();
        ApplyCachedState();
        RequestFullInvalidation();
    }

    void blank() {
        blanked_ = true;
        if(real_)
            real_->Clear();
        RequestFullInvalidation();
    }

    void relocate() {
        SyncViewStateRect();
        ApplyViewport();
        RequestFullInvalidation();
    }

    void present() {
        blanked_ = false;
        if(real_)
            real_->Show();
    }

    tjs_int captureBaseDrawDevice(const tTJSVariant &value) {
        if(value.Type() != tvtObject)
            return 0;
        tTJSVariantClosure closure = value.AsObjectClosureNoAddRef();
        tTJSVariant iface_v;
        if(TJS_FAILED(
               closure.PropGet(0, TJS_W("interface"), nullptr, &iface_v, nullptr)))
            return 0;
        auto *device = reinterpret_cast<iTVPDrawDevice *>(
            (tjs_intptr_t)(tjs_int64)iface_v);
        device = UnwrapDrawDevice(device);
        if(!device || device == this)
            return 0;
        baseDrawDeviceObject_ = value;
        real_ = device;
        if(window_)
            real_->SetWindowInterface(window_);
        ApplyCachedState();
        return 1;
    }

    iTVPDrawDevice *GetReal() const { return real_; }

    static tjs_error getViewStateProperty(tTJSVariant *result, tjs_int,
                                          tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<DrawDeviceD2D>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(result)
            *result = self->getviewState();
        return TJS_S_OK;
    }

    static tjs_error setViewStateProperty(tTJSVariant *, tjs_int count,
                                          tTJSVariant **params,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<DrawDeviceD2D>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(count < 1)
            return TJS_E_BADPARAMCOUNT;
        self->setviewState(*params[0]);
        return TJS_S_OK;
    }

    static tjs_error getInterfaceProperty(tTJSVariant *result, tjs_int,
                                          tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<DrawDeviceD2D>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(result)
            *result = static_cast<tjs_int64>(self->getinterface());
        return TJS_S_OK;
    }

    static tjs_error captureBaseDrawDeviceCallback(tTJSVariant *result,
                                                   tjs_int count,
                                                   tTJSVariant **params,
                                                   iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<DrawDeviceD2D>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(count < 1)
            return TJS_E_BADPARAMCOUNT;
        const tjs_int captured = self->captureBaseDrawDevice(*params[0]);
        if(result)
            *result = captured;
        return TJS_S_OK;
    }

    void Destruct() override {
        if(real_) {
            real_->Destruct();
            real_ = nullptr;
        }
        delete this;
    }

    void SetWindowInterface(iTVPWindow *window) override {
        window_ = window;
        if(!real_ && TVPMainWindow)
            real_ = UnwrapDrawDevice(TVPMainWindow->GetDrawDevice());
        if(real_)
            real_->SetWindowInterface(window);
        ApplyCachedState();
    }

    void AddLayerManager(iTVPLayerManager *manager) override {
        if(!manager)
            return;
        if(std::find(managers_.begin(), managers_.end(), manager) !=
           managers_.end())
            return;
        managers_.push_back(manager);
        manager->AddRef();
        if(managers_.size() == 1 && real_)
            real_->AddLayerManager(manager);
    }

    void RemoveLayerManager(iTVPLayerManager *manager) override {
        auto it = std::find(managers_.begin(), managers_.end(), manager);
        if(it == managers_.end())
            return;
        const bool wasPrimary = (it == managers_.begin());
        if(wasPrimary && real_)
            real_->RemoveLayerManager(manager);
        manager->Release();
        managers_.erase(it);
    }

    void SetDestRectangle(const tTVPRect &rect) override {
        destRect_ = rect;
        hasDestRect_ = true;
        if(real_)
            real_->SetDestRectangle(rect);
    }

    void SetWindowSize(tjs_int w, tjs_int h) override {
        windowWidth_ = w;
        windowHeight_ = h;
        hasWindowSize_ = true;
        if(real_)
            real_->SetWindowSize(w, h);
    }

    void SetViewport(const tTVPRect &rect) override {
        viewportRect_ = rect;
        hasViewportRect_ = true;
        ApplyViewport();
    }

    void SetClipRectangle(const tTVPRect &rect) override {
        clipRect_ = rect;
        hasClipRect_ = true;
        if(real_)
            real_->SetClipRectangle(rect);
    }

    void GetSrcSize(tjs_int &w, tjs_int &h) override {
        if(real_) {
            real_->GetSrcSize(w, h);
            return;
        }
        w = hasWindowSize_ ? windowWidth_ : 0;
        h = hasWindowSize_ ? windowHeight_ : 0;
    }

    void NotifyLayerResize(iTVPLayerManager *manager) override {
        if(real_)
            real_->NotifyLayerResize(manager);
    }

    void NotifyLayerImageChange(iTVPLayerManager *manager) override {
        if(real_)
            real_->NotifyLayerImageChange(manager);
    }

    void OnClick(tjs_int x, tjs_int y) override {
        if(real_)
            real_->OnClick(x, y);
    }

    void OnDoubleClick(tjs_int x, tjs_int y) override {
        if(real_)
            real_->OnDoubleClick(x, y);
    }

    void OnMouseDown(tjs_int x, tjs_int y, tTVPMouseButton mb,
                     tjs_uint32 flags) override {
        if(real_)
            real_->OnMouseDown(x, y, mb, flags);
    }

    void OnMouseUp(tjs_int x, tjs_int y, tTVPMouseButton mb,
                   tjs_uint32 flags) override {
        if(real_)
            real_->OnMouseUp(x, y, mb, flags);
    }

    void OnMouseMove(tjs_int x, tjs_int y, tjs_uint32 flags) override {
        if(real_)
            real_->OnMouseMove(x, y, flags);
    }

    void OnReleaseCapture() override {
        if(real_)
            real_->OnReleaseCapture();
    }

    void OnMouseOutOfWindow() override {
        if(real_)
            real_->OnMouseOutOfWindow();
    }

    void OnKeyDown(tjs_uint key, tjs_uint32 shift) override {
        if(real_)
            real_->OnKeyDown(key, shift);
    }

    void OnKeyUp(tjs_uint key, tjs_uint32 shift) override {
        if(real_)
            real_->OnKeyUp(key, shift);
    }

    void OnKeyPress(tjs_char key) override {
        if(real_)
            real_->OnKeyPress(key);
    }

    void OnMouseWheel(tjs_uint32 shift, tjs_int delta, tjs_int x,
                      tjs_int y) override {
        if(real_)
            real_->OnMouseWheel(shift, delta, x, y);
    }

    void OnTouchDown(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy,
                     tjs_uint32 id) override {
        if(real_)
            real_->OnTouchDown(x, y, cx, cy, id);
    }

    void OnTouchUp(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy,
                   tjs_uint32 id) override {
        if(real_)
            real_->OnTouchUp(x, y, cx, cy, id);
    }

    void OnTouchMove(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy,
                     tjs_uint32 id) override {
        if(real_)
            real_->OnTouchMove(x, y, cx, cy, id);
    }

    void OnTouchScaling(tjs_real startdist, tjs_real curdist, tjs_real cx,
                        tjs_real cy, tjs_int flag) override {
        if(real_)
            real_->OnTouchScaling(startdist, curdist, cx, cy, flag);
    }

    void OnTouchRotate(tjs_real startangle, tjs_real curangle, tjs_real dist,
                       tjs_real cx, tjs_real cy, tjs_int flag) override {
        if(real_)
            real_->OnTouchRotate(startangle, curangle, dist, cx, cy, flag);
    }

    void OnMultiTouch() override {
        if(real_)
            real_->OnMultiTouch();
    }

    void OnDisplayRotate(tjs_int orientation, tjs_int rotate, tjs_int bpp,
                         tjs_int width, tjs_int height) override {
        if(real_)
            real_->OnDisplayRotate(orientation, rotate, bpp, width, height);
    }

    void RecheckInputState() override {
        if(real_)
            real_->RecheckInputState();
    }

    void SetDefaultMouseCursor(iTVPLayerManager *manager) override {
        if(real_)
            real_->SetDefaultMouseCursor(manager);
    }

    void SetMouseCursor(iTVPLayerManager *manager, tjs_int cursor) override {
        if(real_)
            real_->SetMouseCursor(manager, cursor);
    }

    void GetCursorPos(iTVPLayerManager *manager, tjs_int &x,
                      tjs_int &y) override {
        if(real_)
            real_->GetCursorPos(manager, x, y);
    }

    void SetCursorPos(iTVPLayerManager *manager, tjs_int x,
                      tjs_int y) override {
        if(real_)
            real_->SetCursorPos(manager, x, y);
    }

    void WindowReleaseCapture(iTVPLayerManager *manager) override {
        if(real_)
            real_->WindowReleaseCapture(manager);
    }

    void SetHintText(iTVPLayerManager *manager, iTJSDispatch2 *sender,
                     const ttstr &text) override {
        if(real_)
            real_->SetHintText(manager, sender, text);
    }

    void SetAttentionPoint(iTVPLayerManager *manager, tTJSNI_BaseLayer *layer,
                           tjs_int l, tjs_int t) override {
        if(real_)
            real_->SetAttentionPoint(manager, layer, l, t);
    }

    void DisableAttentionPoint(iTVPLayerManager *manager) override {
        if(real_)
            real_->DisableAttentionPoint(manager);
    }

    void SetImeMode(iTVPLayerManager *manager, tTVPImeMode mode) override {
        if(real_)
            real_->SetImeMode(manager, mode);
    }

    void ResetImeMode(iTVPLayerManager *manager) override {
        if(real_)
            real_->ResetImeMode(manager);
    }

    tTJSNI_BaseLayer *GetPrimaryLayer() override {
        if(real_)
            return real_->GetPrimaryLayer();
        return !managers_.empty() ? managers_.front()->GetPrimaryLayer()
                                  : nullptr;
    }

    tTJSNI_BaseLayer *GetFocusedLayer() override {
        return real_ ? real_->GetFocusedLayer() : nullptr;
    }

    void SetFocusedLayer(tTJSNI_BaseLayer *layer) override {
        if(real_)
            real_->SetFocusedLayer(layer);
    }

    void RequestInvalidation(const tTVPRect &rect) override {
        if(real_)
            real_->RequestInvalidation(rect);
    }

    void Update() override {
        if(!real_)
            return;
        if(blanked_) {
            real_->Clear();
        } else {
            real_->Update();
        }
    }

    void Show() override {
        if(!real_)
            return;
        if(blanked_) {
            real_->Clear();
        }
        real_->Show();
    }

    void StartBitmapCompletion(iTVPLayerManager *manager) override {
        if(real_)
            real_->StartBitmapCompletion(manager);
    }

    void NotifyBitmapCompleted(iTVPLayerManager *manager, tjs_int x, tjs_int y,
                               tTVPBaseTexture *bmp, const tTVPRect &cliprect,
                               tTVPLayerType type, tjs_int opacity) override {
        if(real_)
            real_->NotifyBitmapCompleted(manager, x, y, bmp, cliprect, type,
                                         opacity);
    }

    void EndBitmapCompletion(iTVPLayerManager *manager) override {
        if(real_)
            real_->EndBitmapCompletion(manager);
    }

    void DumpLayerStructure() override {
        if(real_)
            real_->DumpLayerStructure();
    }

    void SetShowUpdateRect(bool b) override {
        if(real_)
            real_->SetShowUpdateRect(b);
    }

    void Clear() override {
        if(real_)
            real_->Clear();
    }

    bool SwitchToFullScreen(int window, tjs_uint w, tjs_uint h, tjs_uint bpp,
                            tjs_uint color, bool changeresolution) override {
        return real_ ? real_->SwitchToFullScreen(window, w, h, bpp, color,
                                                 changeresolution)
                     : false;
    }

    void RevertFromFullScreen(int window, tjs_uint w, tjs_uint h, tjs_uint bpp,
                              tjs_uint color) override {
        if(real_)
            real_->RevertFromFullScreen(window, w, h, bpp, color);
    }

    bool WaitForVBlank(tjs_int *in_vblank, tjs_int *delayed) override {
        return real_ ? real_->WaitForVBlank(in_vblank, delayed) : false;
    }

    void DestructExtraManagers() {
        if(real_ && !managers_.empty()) {
            for(size_t index = 1; index < managers_.size(); ++index)
                managers_[index]->Release();
            managers_.resize(1);
            return;
        }
        for(auto *manager : managers_)
            manager->Release();
        managers_.clear();
    }

private:
    static iTVPDrawDevice *UnwrapDrawDevice(iTVPDrawDevice *device) {
        auto *compat = dynamic_cast<DrawDeviceD2D *>(device);
        return compat ? compat->GetReal() : device;
    }

    void ApplyViewport() {
        if(!real_)
            return;
        if(hasRelocatedRect_ && IsValidRect(relocatedRect_)) {
            real_->SetViewport(relocatedRect_);
            return;
        }
        if(hasViewportRect_)
            real_->SetViewport(viewportRect_);
    }

    void ApplyCachedState() {
        if(!real_)
            return;
        if(window_)
            real_->SetWindowInterface(window_);
        if(hasWindowSize_)
            real_->SetWindowSize(windowWidth_, windowHeight_);
        if(hasDestRect_)
            real_->SetDestRectangle(destRect_);
        if(hasClipRect_)
            real_->SetClipRectangle(clipRect_);
        ApplyViewport();
    }

    void RequestFullInvalidation() {
        if(!real_)
            return;
        if(hasDestRect_) {
            real_->RequestInvalidation(destRect_);
            return;
        }
        if(hasViewportRect_) {
            real_->RequestInvalidation(viewportRect_);
            return;
        }
        if(hasWindowSize_) {
            real_->RequestInvalidation(
                tTVPRect(0, 0, windowWidth_, windowHeight_));
        }
    }

    void SyncViewStateRect() {
        EnsureViewStateObject();
        tTVPRect rect;
        tTJSVariant viewTarget = viewState_;
        if(const tTJSVariant root = ExtractRootViewVariant(viewState_);
           root.Type() != tvtVoid) {
            viewTarget = root;
        }
        if(ExtractRectFromVariant(viewTarget, rect)) {
            relocatedRect_ = NormalizeRect(rect);
            hasRelocatedRect_ = IsValidRect(relocatedRect_);
        } else {
            hasRelocatedRect_ = false;
            relocatedRect_ = tTVPRect(0, 0, 0, 0);
        }
        if(viewTarget.Type() == tvtObject) {
            iTJSDispatch2 *obj = viewTarget.AsObjectNoAddRef();
            tjs_int visible = 1;
            if(GetMemberInt(obj, TJS_W("visible"), visible))
                blanked_ = visible == 0;
        }
    }

    void EnsureViewStateObject() {
        if(viewState_.Type() != tvtObject) {
            viewState_ = CreateD2DViewContainer();
            return;
        }

        iTJSDispatch2 *obj = viewState_.AsObjectNoAddRef();
        if(!obj)
            return;

        tTJSVariant hasSetRect;
        if(GetMemberVariant(obj, TJS_W("setRect"), hasSetRect))
            return;

        tTVPRect rect;
        const bool hasRect = ExtractRectFromVariant(viewState_, rect);

        tjs_int type = 0;
        tjs_int visible = 1;
        tjs_real fopacity = 1.0;
        tjs_int opacity = 255;
        tjs_int order = 0;
        tjs_real zinterp = 0.0;
        GetMemberInt(obj, TJS_W("type"), type);
        GetMemberInt(obj, TJS_W("visible"), visible);
        GetMemberReal(obj, TJS_W("fopacity"), fopacity);
        GetMemberInt(obj, TJS_W("opacity"), opacity);
        GetMemberInt(obj, TJS_W("order"), order);
        GetMemberReal(obj, TJS_W("zinterp"), zinterp);

        viewState_ = CreateD2DViewContainer();
        if(viewState_.Type() != tvtObject)
            return;

        obj = viewState_.AsObjectNoAddRef();
        if(!obj)
            return;

        SetMemberVariant(obj, TJS_W("type"), tTJSVariant(type));
        SetMemberVariant(obj, TJS_W("visible"), tTJSVariant(visible));
        SetMemberVariant(obj, TJS_W("fopacity"), tTJSVariant(fopacity));
        SetMemberVariant(obj, TJS_W("opacity"), tTJSVariant(opacity));
        SetMemberVariant(obj, TJS_W("order"), tTJSVariant(order));
        SetMemberVariant(obj, TJS_W("zinterp"), tTJSVariant(zinterp));
        if(hasRect) {
            SetMemberVariant(obj, TJS_W("left"), tTJSVariant(rect.left));
            SetMemberVariant(obj, TJS_W("top"), tTJSVariant(rect.top));
            SetMemberVariant(obj, TJS_W("right"), tTJSVariant(rect.right));
            SetMemberVariant(obj, TJS_W("bottom"), tTJSVariant(rect.bottom));
            SetMemberVariant(obj, TJS_W("x"), tTJSVariant(rect.left));
            SetMemberVariant(obj, TJS_W("y"), tTJSVariant(rect.top));
            SetMemberVariant(obj, TJS_W("width"),
                             tTJSVariant(rect.get_width()));
            SetMemberVariant(obj, TJS_W("height"),
                             tTJSVariant(rect.get_height()));
        }
    }

    iTVPDrawDevice *real_ = nullptr;
    iTVPWindow *window_ = nullptr;
    tTJSVariant baseDrawDeviceObject_;
    tTJSVariant viewState_;
    std::vector<iTVPLayerManager *> managers_;
    tTVPRect destRect_ = tTVPRect(0, 0, 0, 0);
    tTVPRect clipRect_ = tTVPRect(0, 0, 0, 0);
    tTVPRect viewportRect_ = tTVPRect(0, 0, 0, 0);
    tTVPRect relocatedRect_ = tTVPRect(0, 0, 0, 0);
    tjs_int windowWidth_ = 0;
    tjs_int windowHeight_ = 0;
    bool hasDestRect_ = false;
    bool hasClipRect_ = false;
    bool hasViewportRect_ = false;
    bool hasRelocatedRect_ = false;
    bool hasWindowSize_ = false;
    bool blanked_ = false;
};

NCB_REGISTER_CLASS(DrawDeviceD2D) {
    Factory(&DrawDeviceD2D::factory);
    RawCallback(TJS_W("interface"), &Class::getInterfaceProperty, 0, 0);
    RawCallback(TJS_W("viewState"), &Class::getViewStateProperty,
                &Class::setViewStateProperty, 0);
    RawCallback(TJS_W("__captureBaseDrawDevice"),
                &Class::captureBaseDrawDeviceCallback, 0);
    Method(TJS_W("recreate"), &Class::recreate);
    Method(TJS_W("blank"), &Class::blank);
    Method(TJS_W("relocate"), &Class::relocate);
    Method(TJS_W("present"), &Class::present);
    NCB_SUBCLASS(ViewState, D2DView);
}
