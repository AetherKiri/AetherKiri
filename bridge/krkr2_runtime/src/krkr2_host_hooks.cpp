#include "legacy_engine_api.h"

#include "environ/Platform.h"
#include "visual/RenderManager.h"

// Host-side services that the provider-agnostic dispatch layer needs from the
// KiriKiri engine runtime. The dispatch layer must stay free of krkr2core
// headers, so these thin wrappers expose the two services through the
// engine_legacy_* surface resolved at the final engine_api link.

bool engine_legacy_activate_audio_session_for_host(void) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
  return TVPActivateAudioSessionForHost();
#else
  return true;
#endif
}

void engine_legacy_drain_texture_recycle(void) {
  // Provider runtimes bypass the legacy EngineLoop, which is normally
  // responsible for draining textures whose intrusive reference count reached
  // zero during the frame. Artemis uses the same KiriKiri render manager for
  // E-mote, so leaving this queue undrained retains every superseded Metal
  // texture.
  iTVPTexture2D::RecycleProcess();
}
