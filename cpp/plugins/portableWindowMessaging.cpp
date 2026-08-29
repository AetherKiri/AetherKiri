#include "EventIntf.h"
#include "StorageIntf.h"
#include "WindowImpl.h"
#include "ncbind.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

namespace {

// WM_COPYDATA is part of the long-standing messenger/msgreceiver contract.
// Keep the numeric value stable on portable hosts so scripts that persist a
// message id continue to work.
constexpr tjs_uint32 kCopyDataMessage = 0x004a;
constexpr tjs_uint32 kPortableMessageFirst = TVP_WM_USER + 0x100;
constexpr tjs_uint32 kPortableMessageLast = 0x7fffffffu;
constexpr tjs_uint32 kMessageEventKindUser = 1;
constexpr tjs_uint32 kMessageEventKindCopyData = 2;
const ttstr kMessageEventName(TJS_W("__aetherKiriMessengerDispatch"));

std::string toUtf8(const ttstr &value) {
    return value.AsStdString();
}

void setResult(tTJSVariant *result, bool value) {
    if(result)
        *result = value;
}

void setResult(tTJSVariant *result, tjs_uint32 value) {
    if(result)
        *result = static_cast<tjs_int64>(value);
}

struct MessageReceiver {
    tTJSVariant receiver;
    tTJSVariant userData;
};

struct MessageAtomTable {
    std::mutex mutex;
    std::map<std::string, tjs_uint32> names;
    std::map<tjs_uint32, std::string> ids;
    tjs_uint32 next = kPortableMessageFirst;
};

MessageAtomTable &messageAtoms() {
    static MessageAtomTable table;
    return table;
}

tjs_uint32 messageId(const tTJSVariant &value) {
    if(value.Type() == tvtInteger)
        return static_cast<tjs_uint32>(static_cast<tjs_int64>(value));
    if(value.Type() != tvtString)
        return 0;

    const std::string name = toUtf8(ttstr(value.GetString()));
    if(name.empty())
        return 0;

    auto &table = messageAtoms();
    std::lock_guard<std::mutex> guard(table.mutex);
    const auto existing = table.names.find(name);
    if(existing != table.names.end())
        return existing->second;

    // RegisterWindowMessage returns an atom in a process-global range. FNV-1a
    // gives stable ids across sessions; linear probing makes collisions
    // deterministic without exposing a platform-specific atom API.
    std::uint32_t hash = 2166136261u;
    for(const unsigned char c : name) {
        hash ^= c;
        hash *= 16777619u;
    }
    tjs_uint32 candidate = kPortableMessageFirst +
        (hash % (kPortableMessageLast - kPortableMessageFirst));
    while(candidate < kPortableMessageLast) {
        const auto collision = table.ids.find(candidate);
        if(collision == table.ids.end() || collision->second == name) {
            table.names.emplace(name, candidate);
            table.ids.emplace(candidate, name);
            return candidate;
        }
        ++candidate;
    }
    // Extremely unlikely fallback; this also keeps the function bounded if a
    // hostile script registers many names.
    while(table.next < kPortableMessageLast &&
          table.ids.find(table.next) != table.ids.end())
        ++table.next;
    if(table.next >= kPortableMessageLast)
        return 0;
    const tjs_uint32 allocated = table.next++;
    table.names.emplace(name, allocated);
    table.ids.emplace(allocated, name);
    return allocated;
}

bool getNativeWindow(iTJSDispatch2 *object, tTJSNI_Window **window) {
    if(window)
        *window = nullptr;
    if(!object || !window)
        return false;
    iTJSNativeInstance *instance = nullptr;
    if(TJS_FAILED(object->NativeInstanceSupport(
           TJS_NIS_GETINSTANCE, tTJSNC_Window::ClassID, &instance)) ||
       !instance)
        return false;
    *window = static_cast<tTJSNI_Window *>(instance);
    return *window != nullptr;
}

bool writeStoreToken(const ttstr &key, std::uint64_t token) {
    if(key.IsEmpty())
        return true;
    // The Win32 plug-in writes <exeName>.<key>. Keep the same relative
    // location, but persist a portable token instead of an HWND.
    for(tjs_int i = 0; i < key.length(); ++i) {
        const tjs_char c = key.c_str()[i];
        if(c == TJS_W('/') || c == TJS_W('\\') || c < 0x20)
            return false;
    }
    ttstr path = TVPGetAppPath();
    if(!path.IsEmpty() && path.c_str()[path.length() - 1] != TJS_W('/') &&
       path.c_str()[path.length() - 1] != TJS_W('\\'))
        path += TJS_W('/');
    path += TJS_W('.');
    path += key;
    const std::string text = std::to_string(token);
    try {
        std::unique_ptr<tTJSBinaryStream> stream(
            TVPCreateStream(path, TJS_BS_WRITE));
        if(!stream)
            return false;
        stream->WriteBuffer(text.data(), static_cast<tjs_uint>(text.size()));
        return true;
    } catch(...) {
        return false;
    }
}

class WindowMessengerCompat;

std::mutex &messengerMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<WindowMessengerCompat *> &messengerInstances() {
    static std::vector<WindowMessengerCompat *> instances;
    return instances;
}

WindowMessengerCompat *findMessenger(iTJSDispatch2 *object);

class MessengerDispatchFunction final : public tTJSDispatch {
public:
    tjs_error FuncCall(tjs_uint32, const tjs_char *membername, tjs_uint32 *,
                       tTJSVariant *result, tjs_int numparams,
                       tTJSVariant **params, iTJSDispatch2 *objthis) override;
};

class WindowMessengerCompat {
public:
    explicit WindowMessengerCompat(iTJSDispatch2 *owner) : owner_(owner) {
        if(owner_)
            owner_->AddRef();
        token_ = nextToken().fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> guard(messengerMutex());
            messengerInstances().push_back(this);
        }
        installDispatchFunction();
    }

    ~WindowMessengerCompat() {
        unregisterNativeReceiver();
        if(owner_) {
            tTJSVariant current;
            if(TJS_SUCCEEDED(owner_->PropGet(TJS_IGNOREPROP,
                                             kMessageEventName.c_str(), nullptr,
                                             &current, owner_)) &&
               current.Type() == tvtObject &&
               current.AsObjectNoAddRef() == dispatchFunction_) {
                owner_->DeleteMember(TJS_IGNOREPROP, kMessageEventName.c_str(),
                                     nullptr, owner_);
            }
        }
        {
            std::lock_guard<std::mutex> guard(messengerMutex());
            auto &instances = messengerInstances();
            instances.erase(std::remove(instances.begin(), instances.end(),
                                        this),
                            instances.end());
        }
        if(dispatchFunction_)
            dispatchFunction_->Release();
        if(owner_)
            owner_->Release();
    }

    iTJSDispatch2 *owner() const { return owner_; }
    std::uint64_t token() const { return token_; }

    bool getMessageEnable() const { return messageEnable_; }
    void setMessageEnable(bool enabled) {
        messageEnable_ = enabled;
        if(enabled)
            registerNativeReceiver();
        else if(!copyDataEnabled_)
            unregisterNativeReceiver();
        if(enabled && !storeKey_.IsEmpty())
            writeStoreToken(storeKey_, token_);
    }

    const tjs_char *getStoreKey() const { return storeKey_.c_str(); }
    void setStoreKey(const tjs_char *value) {
        storeKey_ = value ? value : TJS_W("");
        if(!storeKey_.IsEmpty() && (messageEnable_ || copyDataEnabled_))
            writeStoreToken(storeKey_, token_);
    }

    void enableCopyData(bool enabled) {
        copyDataEnabled_ = enabled;
        if(enabled)
            registerNativeReceiver();
        else if(!messageEnable_)
            unregisterNativeReceiver();
        if(enabled && !storeKey_.IsEmpty())
            writeStoreToken(storeKey_, token_);
    }

    tjs_uint32 registerReceiver(tjs_int numparams, tTJSVariant **params,
                                tTJSVariant *result) {
        if(numparams < 2 || !params || !params[0] || !params[1])
            return 0;
        const tjs_uint32 id = messageId(*params[1]);
        if(id == 0)
            return 0;
        const tjs_int mode = static_cast<tjs_int>(*params[0]);
        if(mode == wrmRegister) {
            if(numparams < 4 || !params[2] || !params[3])
                return 0;
            MessageReceiver receiver{*params[2], *params[3]};
            receivers_[id] = std::move(receiver);
            registerNativeReceiver();
        } else if(mode == wrmUnregister) {
            receivers_.erase(id);
        }
        setResult(result, id);
        return id;
    }

    bool dispatchUser(tjs_uint32 id, tjs_uint64 wparam, tjs_uint64 lparam) {
        if(!messageEnable_)
            return false;
        const auto found = receivers_.find(id);
        if(found == receivers_.end())
            return false;

        tTJSVariant userData = found->second.userData;
        tTJSVariant wp(static_cast<tjs_int64>(wparam));
        tTJSVariant lp(static_cast<tjs_int64>(lparam));
        tTJSVariant *args[] = {&userData, &wp, &lp};
        tTJSVariant callbackResult;
        try {
            if(found->second.receiver.Type() == tvtObject) {
                const tjs_error error = found->second.receiver
                    .AsObjectClosureNoAddRef()
                    .FuncCall(0, nullptr, nullptr, &callbackResult, 3, args,
                              owner_);
                return TJS_SUCCEEDED(error) &&
                    callbackResult.Type() != tvtVoid &&
                    static_cast<bool>(callbackResult);
            }
            if(found->second.receiver.Type() == tvtString && owner_) {
                const tjs_error error = owner_->FuncCall(
                    0, found->second.receiver.GetString(), nullptr,
                    &callbackResult, 3, args, owner_);
                return TJS_SUCCEEDED(error) &&
                    callbackResult.Type() != tvtVoid &&
                    static_cast<bool>(callbackResult);
            }
        } catch(...) {
            TVPAddLog(TJS_W("messenger receiver callback raised an exception"));
        }
        return false;
    }

    bool dispatchCopyData(const ttstr &key, const ttstr &message) {
        bool handled = false;
        if(!owner_)
            return false;
        if(messageEnable_) {
            tTJSVariant keyValue(key), messageValue(message);
            tTJSVariant *args[] = {&keyValue, &messageValue};
            try {
                handled = TJS_SUCCEEDED(owner_->FuncCall(
                    0, TJS_W("onMessageReceived"), nullptr, nullptr, 2, args,
                    owner_)) || handled;
            } catch(...) {
                TVPAddLog(TJS_W("messenger onMessageReceived raised an exception"));
            }
        }
        if(copyDataEnabled_) {
            tTJSVariant messageValue(message);
            tTJSVariant *args[] = {&messageValue};
            try {
                handled = TJS_SUCCEEDED(owner_->FuncCall(
                    0, TJS_W("onCopyData"), nullptr, nullptr, 1, args,
                    owner_)) || handled;
            } catch(...) {
                TVPAddLog(TJS_W("msgreceiver onCopyData raised an exception"));
            }
        }
        return handled;
    }

    bool dispatchNative(tTVPWindowMessage *message) {
        if(!message)
            return false;
        if(message->Msg == kCopyDataMessage) {
            // Portable sends use an internal packet. Native Win32 callers are
            // handled by the same path only when they pass this magic marker;
            // arbitrary pointers are never dereferenced on a non-Windows host.
            const auto *packet = reinterpret_cast<const PortableCopyData *>(
                static_cast<std::uintptr_t>(message->LParam));
            if(packet && packet->magic == PortableCopyData::kMagic)
                return dispatchCopyData(packet->key, packet->message);
            return false;
        }
        return dispatchUser(message->Msg, message->WParam, message->LParam);
    }

    static tjs_error postDispatch(tTJSVariant *result, tjs_int numparams,
                                  tTJSVariant **params,
                                  iTJSDispatch2 *objthis) {
        if(numparams < 6 || !params || !objthis)
            return TJS_E_BADPARAMCOUNT;
        auto *self = findMessenger(objthis);
        if(!self)
            return TJS_S_OK;
        const tjs_int kind = static_cast<tjs_int>(*params[0]);
        const tjs_uint32 id = static_cast<tjs_uint32>(
            static_cast<tjs_int64>(*params[1]));
        const tjs_uint64 wparam = static_cast<tjs_uint64>(
            static_cast<tjs_int64>(*params[2]));
        const tjs_uint64 lparam = static_cast<tjs_uint64>(
            static_cast<tjs_int64>(*params[3]));
        bool handled = false;
        if(kind == static_cast<tjs_int>(kMessageEventKindUser)) {
            tTVPWindowMessage message;
            message.Msg = id;
            message.WParam = wparam;
            message.LParam = lparam;
            handled = self->dispatchNative(&message);
        } else if(kind == static_cast<tjs_int>(kMessageEventKindCopyData) &&
                  params[4] && params[4]->Type() == tvtString &&
                  params[5] && params[5]->Type() == tvtString) {
            handled = self->dispatchCopyData(ttstr(params[4]->GetString()),
                                              ttstr(params[5]->GetString()));
        }
        setResult(result, handled);
        return TJS_S_OK;
    }

#if defined(_WIN32)
    static bool __stdcall receiverThunk(void *userdata,
                                        tTVPWindowMessage *message) {
#else
    static bool receiverThunk(void *userdata, tTVPWindowMessage *message) {
#endif
        auto *self = static_cast<WindowMessengerCompat *>(userdata);
        return self && self->dispatchNative(message);
    }

    static std::atomic<std::uint64_t> &nextToken() {
        static std::atomic<std::uint64_t> next{1};
        return next;
    }

private:
    struct PortableCopyData {
        static constexpr std::uint32_t kMagic = 0x414b4344; // AKCD
        std::uint32_t magic = kMagic;
        ttstr key;
        ttstr message;
    };

    void installDispatchFunction() {
        if(!owner_)
            return;
        tTJSVariant current;
        if(TJS_SUCCEEDED(owner_->PropGet(TJS_IGNOREPROP,
                                         kMessageEventName.c_str(), nullptr,
                                         &current, owner_)) &&
           current.Type() != tvtVoid)
            return;
        auto *function = new MessengerDispatchFunction();
        dispatchFunction_ = function;
        tTJSVariant value(function, function);
        owner_->PropSet(TJS_MEMBERENSURE, kMessageEventName.c_str(), nullptr,
                        &value, owner_);
        // owner_ holds the script reference; retain one for identity checks.
        dispatchFunction_->AddRef();
        function->Release();
    }

    void registerNativeReceiver() {
        if(registered_)
            return;
        tTJSNI_Window *window = nullptr;
        if(!getNativeWindow(owner_, &window))
            return;
        window->RegisterWindowMessageReceiver(
            wrmRegister, reinterpret_cast<void *>(&receiverThunk), this);
        registered_ = true;
    }

    void unregisterNativeReceiver() {
        if(!registered_)
            return;
        tTJSNI_Window *window = nullptr;
        if(getNativeWindow(owner_, &window))
            window->RegisterWindowMessageReceiver(
                wrmUnregister, reinterpret_cast<void *>(&receiverThunk), this);
        registered_ = false;
    }

    iTJSDispatch2 *owner_ = nullptr;
    iTJSDispatch2 *dispatchFunction_ = nullptr;
    std::uint64_t token_ = 0;
    bool messageEnable_ = false;
    bool copyDataEnabled_ = false;
    bool registered_ = false;
    ttstr storeKey_;
    std::map<tjs_uint32, MessageReceiver> receivers_;

    friend WindowMessengerCompat *findMessenger(iTJSDispatch2 *);
};

WindowMessengerCompat *findMessenger(iTJSDispatch2 *object) {
    std::lock_guard<std::mutex> guard(messengerMutex());
    for(auto *instance : messengerInstances())
        if(instance && instance->owner() == object)
            return instance;
    return nullptr;
}

WindowMessengerCompat *ensureMessenger(iTJSDispatch2 *object) {
    if(!object)
        return nullptr;
    if(auto *existing = findMessenger(object))
        return existing;
    // The class adaptor is created by the hook below. Calling the adaptor
    // directly here also makes postMessage work for a target window that has
    // never touched messenger.dll before.
    auto *existingNative =
        ncbInstanceAdaptor<WindowMessengerCompat>::GetNativeInstance(object);
    if(existingNative)
        return existingNative;
    auto *created = new WindowMessengerCompat(object);
    if(!ncbInstanceAdaptor<WindowMessengerCompat>::SetAdaptorWithNativeInstance(
           object, created))
        delete created;
    return findMessenger(object);
}

std::vector<tTJSNI_Window *> allWindows() {
    std::vector<tTJSNI_Window *> result;
    const tjs_int count = TVPGetWindowCount();
    result.reserve(static_cast<std::size_t>(std::max(0, count)));
    for(tjs_int i = 0; i < count; ++i) {
        if(auto *window = TVPGetWindowListAt(i))
            result.push_back(window);
    }
    return result;
}

void postToWindow(iTJSDispatch2 *source, iTJSDispatch2 *target,
                  tjs_int kind, tjs_uint32 id, tjs_uint64 wparam,
                  tjs_uint64 lparam, const ttstr &key = ttstr(),
                  const ttstr &text = ttstr()) {
    if(!target)
        return;
    ensureMessenger(target);
    tTJSVariant values[6] = {
        tTJSVariant(kind), tTJSVariant(static_cast<tjs_int64>(id)),
        tTJSVariant(static_cast<tjs_int64>(wparam)),
        tTJSVariant(static_cast<tjs_int64>(lparam)), tTJSVariant(key),
        tTJSVariant(text)};
    ttstr eventName = kMessageEventName;
    TVPPostEvent(source, target, eventName, 0, TVP_EPT_POST, 6, values);
};

bool sendUser(WindowMessengerCompat *sender, tjs_uint32 id, tjs_uint64 wparam,
              tjs_uint64 lparam, bool post) {
    if(id == 0)
        return false;
    bool delivered = false;
    iTJSDispatch2 *senderObject = sender ? sender->owner() : nullptr;
    for(auto *window : allWindows()) {
        if(!window)
            continue;
        iTJSDispatch2 *target = window->GetOwnerNoAddRef();
        if(!target || target == senderObject)
            continue;
        if(post) {
            postToWindow(senderObject, target,
                         static_cast<tjs_int>(kMessageEventKindUser), id,
                         wparam, lparam);
            delivered = true;
        } else {
            TVPDeliverWindowMessage(window, id, wparam, lparam);
            delivered = true;
        }
    }
    return delivered;
};

bool sendCopyData(WindowMessengerCompat *sender, const ttstr &key,
                  const ttstr &text, bool post, tjs_uint64 targetToken = 0) {
    bool delivered = false;
    iTJSDispatch2 *senderObject = sender ? sender->owner() : nullptr;
    for(auto *window : allWindows()) {
        if(!window)
            continue;
        iTJSDispatch2 *target = window->GetOwnerNoAddRef();
        if(!target || target == senderObject)
            continue;
        auto *state = ensureMessenger(target);
        if(!state || (targetToken != 0 && state->token() != targetToken))
            continue;
        if(post) {
            postToWindow(senderObject, target,
                         static_cast<tjs_int>(kMessageEventKindCopyData), 0,
                         sender ? sender->token() : 0, 0, key, text);
            delivered = true;
        } else {
            delivered = state->dispatchCopyData(key, text) || delivered;
        }
        if(targetToken != 0)
            break;
    }
    return delivered;
}

tjs_error TJS_INTF_METHOD registerReceiverCb(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **params,
    WindowMessengerCompat *self) {
    if(!self)
        return TJS_E_NATIVECLASSCRASH;
    const tjs_uint32 id = self->registerReceiver(numparams, params, result);
    return id == 0 ? TJS_E_INVALIDPARAM : TJS_S_OK;
}

tjs_error TJS_INTF_METHOD sendUserMessageCb(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **params,
    WindowMessengerCompat *self) {
    if(numparams < 1 || !params || !params[0])
        return TJS_E_BADPARAMCOUNT;
    const tjs_uint32 id = messageId(*params[0]);
    const tjs_uint64 wparam = numparams > 1 ? static_cast<tjs_uint64>(
        static_cast<tjs_int64>(*params[1])) : 0;
    const tjs_uint64 lparam = numparams > 2 ? static_cast<tjs_uint64>(
        static_cast<tjs_int64>(*params[2])) : 0;
    setResult(result, sendUser(self, id, wparam, lparam, false));
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD postUserMessageCb(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **params,
    WindowMessengerCompat *self) {
    if(numparams < 1 || !params || !params[0])
        return TJS_E_BADPARAMCOUNT;
    const tjs_uint32 id = messageId(*params[0]);
    const tjs_uint64 wparam = numparams > 1 ? static_cast<tjs_uint64>(
        static_cast<tjs_int64>(*params[1])) : 0;
    const tjs_uint64 lparam = numparams > 2 ? static_cast<tjs_uint64>(
        static_cast<tjs_int64>(*params[2])) : 0;
    setResult(result, sendUser(self, id, wparam, lparam, true));
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD sendUserMessageDirectCb(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **params,
    WindowMessengerCompat *self) {
    if(numparams < 2 || !params || !params[0] || !params[1])
        return TJS_E_BADPARAMCOUNT;
    const auto token = static_cast<std::uint64_t>(
        static_cast<tjs_int64>(*params[0]));
    const auto id = messageId(*params[1]);
    const auto wp = numparams > 2 ? static_cast<tjs_uint64>(
        static_cast<tjs_int64>(*params[2])) : 0;
    const auto lp = numparams > 3 ? static_cast<tjs_uint64>(
        static_cast<tjs_int64>(*params[3])) : 0;
    bool delivered = false;
    for(auto *window : allWindows()) {
        auto *target = window ? window->GetOwnerNoAddRef() : nullptr;
        auto *state = ensureMessenger(target);
        if(!state || state->token() != token)
            continue;
        TVPDeliverWindowMessage(window, id, wp, lp);
        delivered = true;
        break;
    }
    (void)self;
    setResult(result, delivered);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD postUserMessageDirectCb(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **params,
    WindowMessengerCompat *self) {
    if(numparams < 2 || !params || !params[0] || !params[1])
        return TJS_E_BADPARAMCOUNT;
    const auto token = static_cast<std::uint64_t>(
        static_cast<tjs_int64>(*params[0]));
    const auto id = messageId(*params[1]);
    const auto wp = numparams > 2 ? static_cast<tjs_uint64>(
        static_cast<tjs_int64>(*params[2])) : 0;
    const auto lp = numparams > 3 ? static_cast<tjs_uint64>(
        static_cast<tjs_int64>(*params[3])) : 0;
    bool delivered = false;
    iTJSDispatch2 *source = self ? self->owner() : nullptr;
    for(auto *window : allWindows()) {
        auto *target = window ? window->GetOwnerNoAddRef() : nullptr;
        auto *state = ensureMessenger(target);
        if(!state || state->token() != token)
            continue;
        postToWindow(source, target,
                     static_cast<tjs_int>(kMessageEventKindUser), id, wp, lp);
        delivered = true;
        break;
    }
    setResult(result, delivered);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD sendMessageCb(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **params,
    WindowMessengerCompat *self) {
    if(numparams < 2 || !params || !params[0] || !params[1])
        return TJS_E_BADPARAMCOUNT;
    setResult(result, sendCopyData(self, ttstr(*params[0]),
                                   ttstr(*params[1]), false));
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD postMessageCb(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **params,
    WindowMessengerCompat *self) {
    if(numparams < 2 || !params || !params[0] || !params[1])
        return TJS_E_BADPARAMCOUNT;
    setResult(result, sendCopyData(self, ttstr(*params[0]),
                                   ttstr(*params[1]), true));
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD sendMessageDirectCb(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **params,
    WindowMessengerCompat *self) {
    if(numparams < 3 || !params || !params[0] || !params[1] || !params[2])
        return TJS_E_BADPARAMCOUNT;
    const auto token = static_cast<std::uint64_t>(
        static_cast<tjs_int64>(*params[0]));
    bool delivered = sendCopyData(self, ttstr(*params[1]), ttstr(*params[2]),
                                  false, token);
    setResult(result, delivered);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD postMessageDirectCb(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **params,
    WindowMessengerCompat *self) {
    if(numparams < 3 || !params || !params[0] || !params[1] || !params[2])
        return TJS_E_BADPARAMCOUNT;
    const auto token = static_cast<std::uint64_t>(
        static_cast<tjs_int64>(*params[0]));
    const bool delivered = sendCopyData(self, ttstr(*params[1]),
                                        ttstr(*params[2]), true, token);
    setResult(result, delivered);
    return TJS_S_OK;
}

} // namespace

tjs_error MessengerDispatchFunction::FuncCall(
    tjs_uint32, const tjs_char *membername, tjs_uint32 *, tTJSVariant *result,
    tjs_int numparams, tTJSVariant **params, iTJSDispatch2 *objthis) {
    if(membername)
        return TJS_E_MEMBERNOTFOUND;
    return WindowMessengerCompat::postDispatch(result, numparams, params,
                                               objthis);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("messenger.dll")

NCB_GET_INSTANCE_HOOK(WindowMessengerCompat) {
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *object = GetNativeInstance(objthis);
        if(!object) {
            object = new ClassT(objthis);
            SetNativeInstance(objthis, object);
        }
        return object;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(WindowMessengerCompat, Window) {
    NCB_PROPERTY(messageEnable, getMessageEnable, setMessageEnable);
    NCB_PROPERTY(storeHWND, getStoreKey, setStoreKey);
    NCB_METHOD_RAW_CALLBACK(registerUserMessageReceiver,
                            &registerReceiverCb, 0);
    NCB_METHOD_RAW_CALLBACK(sendUserMessage, &sendUserMessageCb, 0);
    NCB_METHOD_RAW_CALLBACK(postUserMessage, &postUserMessageCb, 0);
    NCB_METHOD_RAW_CALLBACK(sendUserMessageDirect, &sendUserMessageDirectCb, 0);
    NCB_METHOD_RAW_CALLBACK(postUserMessageDirect, &postUserMessageDirectCb, 0);
    NCB_METHOD_RAW_CALLBACK(sendMessage, &sendMessageCb, 0);
    NCB_METHOD_RAW_CALLBACK(postMessage, &postMessageCb, 0);
    NCB_METHOD_RAW_CALLBACK(sendMessageDirect, &sendMessageDirectCb, 0);
    NCB_METHOD_RAW_CALLBACK(postMessageDirect, &postMessageDirectCb, 0);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("msgreceiver.dll")

namespace {
tjs_error TJS_INTF_METHOD startMessageReceiverCb(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **params,
    iTJSDispatch2 *) {
    if(numparams < 1 || !params || !params[0] ||
       params[0]->Type() != tvtObject)
        return TJS_E_BADPARAMCOUNT;
    auto *state = ensureMessenger(params[0]->AsObjectNoAddRef());
    if(!state)
        return TJS_E_INVALIDPARAM;
    state->enableCopyData(true);
    setResult(result, true);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD stopMessageReceiverCb(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **params,
    iTJSDispatch2 *) {
    if(numparams < 1 || !params || !params[0] ||
       params[0]->Type() != tvtObject)
        return TJS_E_BADPARAMCOUNT;
    auto *state = findMessenger(params[0]->AsObjectNoAddRef());
    if(state)
        state->enableCopyData(false);
    setResult(result, state != nullptr);
    return TJS_S_OK;
}
} // namespace

NCB_ATTACH_FUNCTION_WITHTAG(startMessageReceiver, MsgReceiverStart, Window,
                            startMessageReceiverCb);
NCB_ATTACH_FUNCTION_WITHTAG(stopMessageReceiver, MsgReceiverStop, Window,
                            stopMessageReceiverCb);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("tasktray.dll")

class WindowTasktrayCompat {
public:
    explicit WindowTasktrayCompat(iTJSDispatch2 *owner = nullptr)
        : owner_(owner) {}

    bool showTasktrayIcon(const tjs_char *icon = nullptr) {
        visible_ = true;
        if(icon)
            icon_ = icon;
        notify(TJS_W("show"));
        return true;
    }
    bool hideTasktrayIcon() {
        visible_ = false;
        notify(TJS_W("hide"));
        return true;
    }
    bool setTasktrayIcon(const tjs_char *icon = nullptr) {
        if(icon)
            icon_ = icon;
        notify(TJS_W("set"));
        return true;
    }
    bool popupTasktrayInfo(const tjs_char *title, const tjs_char *text,
                           const tjs_char *icon, tjs_int timeout = 0) {
        lastTitle_ = title ? title : TJS_W("");
        lastText_ = text ? text : TJS_W("");
        if(icon)
            icon_ = icon;
        timeout_ = timeout;
        notify(TJS_W("popup"));
        return true;
    }
    bool getVisible() const { return visible_; }
    ttstr getIcon() const { return icon_; }
    ttstr getLastTitle() const { return lastTitle_; }
    ttstr getLastText() const { return lastText_; }
    tjs_int getTimeout() const { return timeout_; }

private:
    void notify(const tjs_char *operation) {
        if(!owner_)
            return;
        tTJSVariant op(operation);
        tTJSVariant *args[] = {&op};
        // Hosts may render a tray icon themselves. The callback is optional;
        // the state remains useful to scripts even when no UI is available.
        owner_->FuncCall(0, TJS_W("onTasktrayChanged"), nullptr, nullptr, 1,
                         args, owner_);
    }

    iTJSDispatch2 *owner_ = nullptr;
    bool visible_ = false;
    ttstr icon_;
    ttstr lastTitle_;
    ttstr lastText_;
    tjs_int timeout_ = 0;
};

NCB_GET_INSTANCE_HOOK(WindowTasktrayCompat) {
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *object = GetNativeInstance(objthis);
        if(!object) {
            object = new ClassT(objthis);
            SetNativeInstance(objthis, object);
        }
        return object;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(WindowTasktrayCompat, Window) {
    NCB_METHOD(showTasktrayIcon);
    NCB_METHOD(hideTasktrayIcon);
    NCB_METHOD(setTasktrayIcon);
    NCB_METHOD(popupTasktrayInfo);
    NCB_PROPERTY_RO(tasktrayVisible, getVisible);
    NCB_PROPERTY_RO(tasktrayIcon, getIcon);
    NCB_PROPERTY_RO(tasktrayTitle, getLastTitle);
    NCB_PROPERTY_RO(tasktrayText, getLastText);
    NCB_PROPERTY_RO(tasktrayTimeout, getTimeout);
}
