#include "tjsCommHead.h"
#include "NativeEventQueue.h"
#include "Application.h"

NativeEventQueueImplement::~NativeEventQueueImplement() { Clear(); }

void NativeEventQueueImplement::PostEvent(const NativeEvent &ev) {
    NativeEvent event(ev);
    Application->PostUserMessage(
        [this, event]() mutable { Dispatch(event); }, this,
        static_cast<int>(ev.Message));
}

void NativeEventQueueImplement::Clear(int msg) {
    if(Application == nullptr) return;
    Application->FilterUserMessage(
        [this, msg](
            std::vector<std::tuple<void *, int, tTVPApplication::tMsg>> &lst) {
            for(auto it = lst.begin(); it != lst.end();) {
                if(std::get<0>(*it) == this &&
                   (!msg || std::get<1>(*it) == msg)) {
                    it = lst.erase(it);
                } else {
                    ++it;
                }
            }
        });
}
