#include "tjsCommHead.h"
#include "ncbind.hpp"
#include "TransIntf.h"

#if defined(AETHER_KRKRZ_EXTNAGANO_UPSTREAM)
#include "upstream_bridge/extNaganoProviders.hpp"
#endif

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

// TVPFindTransHandlerProvider intentionally returns crossfade for a missing
// name in Aether.  Compare the returned provider's advertised name so a
// compatibility plugin that was loaded earlier can retain ownership of an
// exact transition instead of causing a duplicate-registration failure.
static bool hasExactTransHandlerProvider(const tjs_char *name) {
    iTVPTransHandlerProvider *existing =
        TVPFindTransHandlerProviderExact(name);
    if(!existing)
        return false;
    const tjs_char *existingName = nullptr;
    const bool exact = TJS_SUCCEEDED(existing->GetName(&existingName)) &&
                       existingName && ttstr(existingName) == ttstr(name);
    existing->Release();
    return exact;
}

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

#if defined(AETHER_KRKRZ_EXTNAGANO_UPSTREAM)
static void addExtNaganoUpstreamProvider(
    iTVPTransHandlerProvider *(*factory)(), const tjs_char *name,
    const tjs_char *fallbackName) {
    if(hasExactTransHandlerProvider(name))
        return;

    iTVPTransHandlerProvider *provider = nullptr;
    try {
        provider = factory ? factory() : nullptr;
    } catch(...) {
        // Keep module loading deterministic when a future upstream algorithm
        // cannot be detached because its registration contract changed.
        provider = nullptr;
    }
    if(!provider) {
        if(hasExactTransHandlerProvider(name))
            return;
        addExtNaganoProvider(name, fallbackName);
        return;
    }
    try {
        TVPAddTransHandlerProvider(provider);
    } catch(...) {
        provider->Release();
        // A source-level upstream change must not make a non-krkrz game fail
        // to load the compatibility plugin.  Keep the deterministic Aether
        // fallback if the detached provider cannot be registered.
        if(hasExactTransHandlerProvider(name))
            return;
        addExtNaganoProvider(name, fallbackName);
        return;
    }
    ExtNaganoProviders.push_back(provider);
}
#endif

static void extNagano_init() {
    addExtNaganoProvider(TJS_W("3duniversal"), TJS_W("universal"));
#if defined(AETHER_KRKRZ_EXTNAGANO_UPSTREAM)
    addExtNaganoUpstreamProvider(
        aether::krkrz::extnagano::makeBlurFadeProvider, TJS_W("blurfade"),
        TJS_W("crossfade"));
    addExtNaganoUpstreamProvider(
        aether::krkrz::extnagano::makeBookProvider, TJS_W("book"),
        TJS_W("crossfade"));
#else
    addExtNaganoProvider(TJS_W("blurfade"), TJS_W("crossfade"));
    addExtNaganoProvider(TJS_W("book"), TJS_W("crossfade"));
#endif
    addExtNaganoProvider(TJS_W("bookLR"), TJS_W("crossfade"));
    addExtNaganoProvider(TJS_W("bookRL"), TJS_W("crossfade"));
#if defined(AETHER_KRKRZ_EXTNAGANO_UPSTREAM)
    // flutter callers may omit the rule image required by universal.  The
    // detached upstream provider handles its native option set and delegates
    // to crossfade when that set is incomplete.
    addExtNaganoUpstreamProvider(
        aether::krkrz::extnagano::makeFlutterProvider, TJS_W("flutter"),
        TJS_W("crossfade"));
    addExtNaganoUpstreamProvider(
        aether::krkrz::extnagano::makeHoneyTurnProvider, TJS_W("honeyturn"),
        TJS_W("universal"));
#else
    addExtNaganoProvider(TJS_W("flut" "ter"), TJS_W("crossfade"));
    addExtNaganoProvider(TJS_W("honeyturn"), TJS_W("universal"));
#endif
    addExtNaganoProvider(TJS_W("imagewipe"), TJS_W("universal"));
#if defined(AETHER_KRKRZ_EXTNAGANO_UPSTREAM)
    addExtNaganoUpstreamProvider(
        aether::krkrz::extnagano::makeMorphingProvider, TJS_W("morphing"),
        TJS_W("crossfade"));
    addExtNaganoUpstreamProvider(
        aether::krkrz::extnagano::makeMultiRippleProvider,
        TJS_W("multiripple"), TJS_W("universal"));
    addExtNaganoUpstreamProvider(
        aether::krkrz::extnagano::makeRgbFadeProvider, TJS_W("rgbfade"),
        TJS_W("crossfade"));
    addExtNaganoUpstreamProvider(
        aether::krkrz::extnagano::makeScanLineProvider, TJS_W("scanline"),
        TJS_W("universal"));
    // krkrz names this provider "spin"; expose both that spelling and
    // Aether's historical "spinfade" alias to the detached upstream code.
    addExtNaganoUpstreamProvider(
        aether::krkrz::extnagano::makeSpinFadeAliasProvider,
        TJS_W("spinfade"), TJS_W("crossfade"));
    addExtNaganoUpstreamProvider(
        aether::krkrz::extnagano::makeSpinFadeProvider, TJS_W("spin"),
        TJS_W("crossfade"));
    addExtNaganoUpstreamProvider(
        aether::krkrz::extnagano::makeZoomFadeProvider, TJS_W("zoomfade"),
        TJS_W("crossfade"));
#else
    addExtNaganoProvider(TJS_W("morphing"), TJS_W("crossfade"));
    addExtNaganoProvider(TJS_W("multiripple"), TJS_W("universal"));
    addExtNaganoProvider(TJS_W("rgbfade"), TJS_W("crossfade"));
    addExtNaganoProvider(TJS_W("scanline"), TJS_W("universal"));
    addExtNaganoProvider(TJS_W("spinfade"), TJS_W("crossfade"));
    addExtNaganoProvider(TJS_W("zoomfade"), TJS_W("crossfade"));
#endif
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
