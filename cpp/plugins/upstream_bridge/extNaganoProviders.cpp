#include "extNaganoProviders.hpp"

#include "PluginStub.h"

#include <utility>

// The selected upstream translation units are compiled as ordinary sources by
// CMake.  Their registration functions are used only to obtain the business
// provider; the provider is detached from the global registry below so that
// Aether remains the sole registry/lifecycle owner.
extern void RegisterRGBFadeTransHandlerProvider();
extern void UnregisterRGBFadeTransHandlerProvider();
extern void RegisterScanLineTransHandlerProvider();
extern void UnregisterScanLineTransHandlerProvider();
extern void RegisterZoomFadeTransHandlerProvider();
extern void UnregisterZoomFadeTransHandlerProvider();
extern void RegisterBlurFadeTransHandlerProvider();
extern void UnregisterBlurFadeTransHandlerProvider();
extern void RegisterBookTransHandlerProvider();
extern void UnregisterBookTransHandlerProvider();
extern void RegisterFlutterTransHandlerProvider();
extern void UnregisterFlutterTransHandlerProvider();
extern void RegisterHoneyTurnTransHandlerProvider();
extern void UnregisterHoneyTurnTransHandlerProvider();
extern void RegisterMorphingTransHandlerProvider();
extern void UnregisterMorphingTransHandlerProvider();
extern void RegisterMultiRippleTransHandlerProvider();
extern void UnregisterMultiRippleTransHandlerProvider();
extern void RegisterSpinFadeTransHandlerProvider();
extern void UnregisterSpinFadeTransHandlerProvider();

namespace aether::krkrz::extnagano {
namespace {

class DelegatingProvider final : public iTVPTransHandlerProvider {
public:
    DelegatingProvider(iTVPTransHandlerProvider *upstream, const tjs_char *name,
                       const tjs_char *fallback)
        : RefCount(1), Upstream(upstream), Name(name), Fallback(fallback) {
        if(Upstream)
            Upstream->AddRef();
    }

    ~DelegatingProvider() override {
        if(Upstream)
            Upstream->Release();
    }

    tjs_error AddRef() override {
        ++RefCount;
        return TJS_S_OK;
    }

    tjs_error Release() override {
        if(RefCount == 1)
            delete this;
        else
            --RefCount;
        return TJS_S_OK;
    }

    tjs_error GetName(const tjs_char **name) override {
        if(!name)
            return TJS_E_FAIL;
        *name = Name.c_str();
        return TJS_S_OK;
    }

    tjs_error StartTransition(
        iTVPSimpleOptionProvider *options, iTVPSimpleImageProvider *imagepro,
        tTVPLayerType layertype, tjs_uint src1w, tjs_uint src1h,
        tjs_uint src2w, tjs_uint src2h, tTVPTransType *type,
        tTVPTransUpdateType *updatetype,
        iTVPBaseTransHandler **handler) override {
        if(!handler)
            return TJS_E_FAIL;
        *handler = nullptr;

        tjs_error upstreamResult = TJS_E_FAIL;
        if(Upstream) {
            try {
                upstreamResult = Upstream->StartTransition(
                    options, imagepro, layertype, src1w, src1h, src2w, src2h,
                    type, updatetype, handler);
                if(TJS_SUCCEEDED(upstreamResult) && *handler)
                    return upstreamResult;
            } catch(...) {
                // Reconstructed upstream handlers may reject an option type
                // by throwing during conversion.  That is an implementation
                // boundary, not a reason to break a legacy game: continue
                // through the same Aether fallback used for a normal error.
                upstreamResult = TJS_E_FAIL;
            }
            if(*handler) {
                (*handler)->Release();
                *handler = nullptr;
            }
        }

        // The Aether registry already supplies the canonical crossfade and
        // universal providers.  A failed upstream option parse therefore
        // falls back to the same behavior used by the legacy implementation.
        iTVPTransHandlerProvider *fallback =
            TVPFindTransHandlerProvider(Fallback);
        if(!fallback && Fallback != TJS_W("crossfade"))
            fallback = TVPFindTransHandlerProvider(TJS_W("crossfade"));
        if(!fallback)
            return upstreamResult;

        const tjs_error fallbackResult = fallback->StartTransition(
            options, imagepro, layertype, src1w, src1h, src2w, src2h, type,
            updatetype, handler);
        fallback->Release();
        return fallbackResult;
    }

private:
    tjs_int RefCount;
    iTVPTransHandlerProvider *Upstream;
    ttstr Name;
    ttstr Fallback;
};

using RegisterFunction = void (*)();

// Register an upstream provider temporarily, detach it from Aether's global
// registry, and return a wrapper owning the final reference.  This preserves
// upstream business code while avoiding duplicate provider names and keeping
// module reload/lifetime rules in Aether.
iTVPTransHandlerProvider *detachProvider(const tjs_char *lookupName,
                                         const tjs_char *exposedName,
                                         const tjs_char *fallback,
                                         RegisterFunction registerFunction,
                                         RegisterFunction unregisterFunction) {
    iTVPTransHandlerProvider *upstream = nullptr;
    bool registered = false;
    try {
        // A different plugin may already own this transition name.  The
        // upstream Register* functions allocate their static provider before
        // TVPAddTransHandlerProvider reports a duplicate, so attempting the
        // registration first would leak that allocation.  Probe the single
        // Aether registry and leave an exact existing owner untouched.
        iTVPTransHandlerProvider *existing =
            TVPFindTransHandlerProviderExact(lookupName);
        if(existing) {
            const tjs_char *existingName = nullptr;
            const bool exact =
                TJS_SUCCEEDED(existing->GetName(&existingName)) &&
                existingName && ttstr(existingName) == ttstr(lookupName);
            existing->Release();
            if(exact)
                return nullptr;
        }

        registerFunction();
        registered = true;
        upstream = TVPFindTransHandlerProvider(lookupName); // adds one reference
        if(!upstream)
            return nullptr;

        const tjs_char *actualName = nullptr;
        if(TJS_FAILED(upstream->GetName(&actualName)) || !actualName ||
           ttstr(actualName) != ttstr(lookupName)) {
            upstream->Release();
            upstream = nullptr;
            unregisterFunction();
            return nullptr;
        }

        // Drop the registry's reference, then let the upstream unregister
        // routine release the provider's construction reference.
        TVPRemoveTransHandlerProvider(upstream);
        unregisterFunction();

        auto *wrapper = new DelegatingProvider(upstream, exposedName, fallback);
        upstream->Release(); // wrapper owns its AddRef now
        upstream = nullptr;
        return wrapper;
    } catch(...) {
        if(upstream)
            upstream->Release();
        // Best effort cleanup; registration may have failed before a provider
        // became visible, and the upstream routine is idempotent for removal.
        if(registered) {
            try {
                unregisterFunction();
            } catch(...) {
            }
        }
        return nullptr;
    }
}

#define AETHER_DEFINE_PROVIDER_FACTORY(SYMBOL, NAME, FALLBACK, REGISTER,      \
                                        UNREGISTER)                             \
    iTVPTransHandlerProvider *SYMBOL() {                                       \
        return detachProvider(TJS_W(NAME), TJS_W(NAME), TJS_W(FALLBACK),       \
                              REGISTER, UNREGISTER);                           \
    }

} // namespace

AETHER_DEFINE_PROVIDER_FACTORY(makeRgbFadeProvider, "rgbfade", "crossfade",
                                RegisterRGBFadeTransHandlerProvider,
                                UnregisterRGBFadeTransHandlerProvider)
AETHER_DEFINE_PROVIDER_FACTORY(makeScanLineProvider, "scanline", "universal",
                                RegisterScanLineTransHandlerProvider,
                                UnregisterScanLineTransHandlerProvider)
AETHER_DEFINE_PROVIDER_FACTORY(makeZoomFadeProvider, "zoomfade", "crossfade",
                                RegisterZoomFadeTransHandlerProvider,
                                UnregisterZoomFadeTransHandlerProvider)
AETHER_DEFINE_PROVIDER_FACTORY(makeBlurFadeProvider, "blurfade", "crossfade",
                                RegisterBlurFadeTransHandlerProvider,
                                UnregisterBlurFadeTransHandlerProvider)
AETHER_DEFINE_PROVIDER_FACTORY(makeBookProvider, "book", "crossfade",
                                RegisterBookTransHandlerProvider,
                                UnregisterBookTransHandlerProvider)
AETHER_DEFINE_PROVIDER_FACTORY(makeFlutterProvider, "flutter", "crossfade",
                                RegisterFlutterTransHandlerProvider,
                                UnregisterFlutterTransHandlerProvider)
AETHER_DEFINE_PROVIDER_FACTORY(makeHoneyTurnProvider, "honeyturn", "universal",
                                RegisterHoneyTurnTransHandlerProvider,
                                UnregisterHoneyTurnTransHandlerProvider)
AETHER_DEFINE_PROVIDER_FACTORY(makeMorphingProvider, "morphing", "crossfade",
                                RegisterMorphingTransHandlerProvider,
                                UnregisterMorphingTransHandlerProvider)
AETHER_DEFINE_PROVIDER_FACTORY(makeMultiRippleProvider, "multiripple",
                                "universal", RegisterMultiRippleTransHandlerProvider,
                                UnregisterMultiRippleTransHandlerProvider)
AETHER_DEFINE_PROVIDER_FACTORY(makeSpinFadeProvider, "spin", "crossfade",
                                RegisterSpinFadeTransHandlerProvider,
                                UnregisterSpinFadeTransHandlerProvider)

iTVPTransHandlerProvider *makeSpinFadeAliasProvider() {
    return detachProvider(TJS_W("spin"), TJS_W("spinfade"),
                          TJS_W("crossfade"),
                          RegisterSpinFadeTransHandlerProvider,
                          UnregisterSpinFadeTransHandlerProvider);
}

#undef AETHER_DEFINE_PROVIDER_FACTORY

} // namespace aether::krkrz::extnagano
