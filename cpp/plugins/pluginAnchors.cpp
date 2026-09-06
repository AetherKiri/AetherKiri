#include "ncbind.hpp"

extern "C" void TVPRegisterKAGParserExPluginAnchor();
extern "C" void TVPRegisterExtKAGParserPluginAnchor();
extern "C" void TVPRegisterMotionPlayerPluginAnchor();
extern "C" void TVPRegisterTomlPluginAnchor();

namespace {

void linkStaticPluginModules() {
    TVPRegisterKAGParserExPluginAnchor();
    TVPRegisterExtKAGParserPluginAnchor();
    // Force the motionplayer translation unit into the final static link.
    // Its ncbind registrations (Motion.Player/EmotePlayer and methods such
    // as contains, clear and setCoord) live in that unit's static initializers.
    // Without this anchor the archive member can be discarded, leaving a
    // script-visible Motion.Player object with missing members.
    TVPRegisterMotionPlayerPluginAnchor();
    // Keep the built-in TOML decoder in the final static link.  Games such
    // as Angelic Scream load UTF-16 TOML localization files through
    // Scripts.tomlDecodeFromStorage; if this translation unit is discarded,
    // Plugins.link("toml.dll") falls back to the incompatible external
    // parser and localization errors surface as English UI text.
    TVPRegisterTomlPluginAnchor();
}

} // namespace

#define NCB_MODULE_NAME TJS_W("aetherkiri_static_plugin_anchors.dll")
NCB_PRE_REGIST_CALLBACK(linkStaticPluginModules);
