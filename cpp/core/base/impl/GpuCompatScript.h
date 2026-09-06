#pragma once

// Kept separately so the portable draw-device script contract can be tested
// without starting a renderer or loading a game.
inline constexpr char TVP_GPU_COMPAT_SCRIPT[] =
    "// AetherKiri GPULayer/D3D compatibility placeholder.\n"
    "try { Plugins.link(\"krkrgles.dll\"); } catch(e) { }\n"
    "try { Plugins.link(\"krkrlive2d.dll\"); } catch(e) { }\n"
    "try { Window.OGLDrawDevice = global.OGLDrawDevice; } catch(e) { }\n"
    "try { Window.GLESAdaptor = global.GLESAdaptor; } catch(e) { }\n"
    "function KAGWindow_createDrawDevice() {\n"
    "    var dd = null;\n"
    "    try { dd = new global.OGLDrawDevice(); } catch(e) { try { dd = new global.GLESAdaptor(); } catch(e2) { dd = null; } }\n"
    "    try { if(dd !== null) dd.setScreenSize(this.width, this.height); } catch(e) { }\n"
    "    try { this.gpuDrawDevice = dd; } catch(e) { }\n"
    // Uppercase names are constructors used by script-owned lazy getters.
    // Keep the instance in gpuDrawDevice and leave glesAdaptor to the script.
    "    try { this.OGLDrawDevice = global.OGLDrawDevice; } catch(e) { }\n"
    "    try { this.GLESAdaptor = global.GLESAdaptor; } catch(e) { }\n"
    "    try { return new global.Window.BasicDrawDevice(); } catch(e) { }\n"
    "    try { return new global.Window.PassThroughDrawDevice(); } catch(e) { }\n"
    "    return null;\n"
    "}\n"
    "try { KAGWindow.KAGWindow_createDrawDevice = KAGWindow_createDrawDevice; } catch(e) { }\n"
    "try { KAGWindow.prototype.KAGWindow_createDrawDevice = KAGWindow_createDrawDevice; } catch(e) { }\n"
    "try { KAGWindow_createDrawDevice = KAGWindow_createDrawDevice; } catch(e) { }\n";
