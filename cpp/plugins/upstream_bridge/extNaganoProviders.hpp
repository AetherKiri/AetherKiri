#pragma once

#include "TransIntf.h"

namespace aether::krkrz::extnagano {

// Build an Aether-owned provider which delegates to one detached upstream
// extNagano provider.  The upstream provider is kept out of the global
// registry so the Aether fallback can be selected when an option set or image
// shape is outside the upstream implementation's contract.
iTVPTransHandlerProvider *makeRgbFadeProvider();
iTVPTransHandlerProvider *makeScanLineProvider();
iTVPTransHandlerProvider *makeZoomFadeProvider();
iTVPTransHandlerProvider *makeBlurFadeProvider();
iTVPTransHandlerProvider *makeBookProvider();
iTVPTransHandlerProvider *makeFlutterProvider();
iTVPTransHandlerProvider *makeHoneyTurnProvider();
iTVPTransHandlerProvider *makeMorphingProvider();
iTVPTransHandlerProvider *makeMultiRippleProvider();
iTVPTransHandlerProvider *makeSpinFadeProvider();
// krkrz_dev calls this provider "spin", while older Aether/krkr2 scripts
// commonly request "spinfade".  Both names receive the same detached
// upstream algorithm so neither compatibility spelling loses the reuse.
iTVPTransHandlerProvider *makeSpinFadeAliasProvider();

} // namespace aether::krkrz::extnagano
