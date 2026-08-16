#include "ncbind.hpp"

extern "C" void TVPRegisterKAGParserExPluginAnchor();
extern "C" void TVPRegisterExtKAGParserPluginAnchor();

namespace {

void linkStaticPluginModules() {
    TVPRegisterKAGParserExPluginAnchor();
    TVPRegisterExtKAGParserPluginAnchor();
}

} // namespace

#define NCB_MODULE_NAME TJS_W("aetherkiri_static_plugin_anchors.dll")
NCB_PRE_REGIST_CALLBACK(linkStaticPluginModules);
