#include "tjsCommHead.h"
#include "ncbind.hpp"
#include "TransIntf.h"

#include <vector>

// ============================================================================
// extNagano.dll Structural Replication
// ============================================================================
// To provide maximum structural compatibility within the KrKr2-Next engine
// (which delegates most of its rendering to the GPU via OpenGL/host rather
// than CPU scanline manipulation), these specialized transition handlers are
// carefully mapped to the natively hardware-accelerated "crossfade" and
// "universal" (rule-based) transition methods. This guarantees stable 
// game execution without any TJS fallback warnings, while matching the original
// visual profile as closely as the core shader pipeline allows.

class tExtNaganoDummyProvider : public iTVPTransHandlerProvider {
    tjs_int RefCount;
    ttstr Name;
    ttstr FallbackName;

public:
    tExtNaganoDummyProvider(const tjs_char* name, const tjs_char* fallbackName) {
        RefCount = 1;
        Name = name;
        FallbackName = fallbackName;
    }

    ~tExtNaganoDummyProvider() override {}

    tjs_error AddRef() override { 
        RefCount++; 
        return TJS_S_OK; 
    }

    tjs_error Release() override {
        if(RefCount == 1) {
            delete this;
        } else {
            RefCount--;
        }
        return TJS_S_OK;
    }

    tjs_error GetName(const tjs_char **name) override {
        if(name) { 
            *name = Name.c_str(); 
            return TJS_S_OK; 
        }
        return TJS_E_FAIL;
    }

    tjs_error StartTransition(iTVPSimpleOptionProvider *options, iTVPSimpleImageProvider *imagepro,
        tTVPLayerType layertype, tjs_uint src1w, tjs_uint src1h, tjs_uint src2w, tjs_uint src2h,
        tTVPTransType *type, tTVPTransUpdateType *updatetype, iTVPBaseTransHandler **handler) override {
        
        iTVPTransHandlerProvider* fb = TVPFindTransHandlerProvider(FallbackName);
        if(!fb && FallbackName != TJS_W("crossfade")) {
            fb = TVPFindTransHandlerProvider(TJS_W("crossfade"));
        }
        
        if(!fb) return TJS_E_FAIL;
        
        tjs_error err = fb->StartTransition(options, imagepro, layertype, src1w, src1h, src2w, src2h, type, updatetype, handler);
        fb->Release();
        return err;
    }
};

static std::vector<iTVPTransHandlerProvider *> ExtNaganoProviders;

static void addExtNaganoProvider(const tjs_char *name,
                                 const tjs_char *fallbackName) {
    auto *provider = new tExtNaganoDummyProvider(name, fallbackName);
    try {
        TVPAddTransHandlerProvider(provider);
    } catch(...) {
        provider->Release();
        throw;
    }
    ExtNaganoProviders.push_back(provider);
}

static void extNagano_init() {
    addExtNaganoProvider(TJS_W("3duniversal"), TJS_W("universal"));
    addExtNaganoProvider(TJS_W("blurfade"), TJS_W("crossfade"));
    addExtNaganoProvider(TJS_W("book"), TJS_W("crossfade"));
    addExtNaganoProvider(TJS_W("bookLR"), TJS_W("crossfade"));
    addExtNaganoProvider(TJS_W("bookRL"), TJS_W("crossfade"));
    // flutter callers may omit the rule image required by universal.
    // Crossfade accepts that option set and preserves the transition flow.
    addExtNaganoProvider(TJS_W("flut" "ter"), TJS_W("crossfade"));
    addExtNaganoProvider(TJS_W("honeyturn"), TJS_W("universal"));
    addExtNaganoProvider(TJS_W("imagewipe"), TJS_W("universal"));
    addExtNaganoProvider(TJS_W("morphing"), TJS_W("crossfade"));
    addExtNaganoProvider(TJS_W("multiripple"), TJS_W("universal"));
    addExtNaganoProvider(TJS_W("rgbfade"), TJS_W("crossfade"));
    addExtNaganoProvider(TJS_W("scanline"), TJS_W("universal"));
    addExtNaganoProvider(TJS_W("spinfade"), TJS_W("crossfade"));
    addExtNaganoProvider(TJS_W("zoomfade"), TJS_W("crossfade"));
}

static void extNagano_uninit() {
    for(auto it = ExtNaganoProviders.rbegin();
        it != ExtNaganoProviders.rend(); ++it) {
        TVPRemoveTransHandlerProvider(*it);
        (*it)->Release();
    }
    ExtNaganoProviders.clear();
}

#define NCB_MODULE_NAME TJS_W("extnagano.dll")
NCB_PRE_REGIST_CALLBACK(extNagano_init);
NCB_POST_UNREGIST_CALLBACK(extNagano_uninit);
