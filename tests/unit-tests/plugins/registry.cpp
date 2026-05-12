#include <catch2/catch_test_macros.hpp>

#include "ncbind.hpp"

TEST_CASE("first-pass compatibility stubs are registered") {
    const tjs_char *modules[] = {
        TJS_W("flashPlayer.dll"),
        TJS_W("layerExSubImage.dll"),
        TJS_W("gfxEffect.dll"),
        TJS_W("clipboardEx.dll"),
        TJS_W("shellExecute.dll"),
        TJS_W("process.dll"),
        TJS_W("tasktray.dll"),
        TJS_W("adjustMonitor.dll"),
        TJS_W("fpslimit.dll"),
        TJS_W("systemEx.dll"),
        TJS_W("base64.dll"),
        TJS_W("lineParser.dll"),
    };

    for(const auto *module : modules)
        CHECK(ncbAutoRegister::HasModule(module));
}
