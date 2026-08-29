#include "ncbind.hpp"

// MenuItem and Window.menu are owned by the core implementation in
// cpp/core/visual/impl/MenuItemImpl.cpp.  The old menuExt file attached a
// second hook and replaced the real shortcut tables with zero-valued dummy
// properties.  Keep only the module anchor required by Plugins.link(); no
// class or property is duplicated here.
#define NCB_MODULE_NAME TJS_W("menu.dll")

static void menu_dll_anchor() {}
NCB_PRE_REGIST_CALLBACK(menu_dll_anchor);
