# AetherKiri Plugin Gap Plan

Date: 2026-05-13

## Scope

Compare and close plugin compatibility gaps between:

- AetherKiri: `cpp/plugins`
- KiriKiri2 reference: `/Users/liuyu/AkitaSummer/kirikiri2/kirikiri2/src/plugins/win32`
- krkrz reference: `/Users/liuyu/AkitaSummer/krkrz`

`krkrz` does not provide a peer `plugins` tree. It is useful mainly as a
reference for plugin loading, exported host APIs, Susie support, and movie
infrastructure.

## Current State

AetherKiri already has a static internal plugin registration path through
`ncbAutoRegister` and `TVPLoadInternalPlugins()`. It also contains several
game-specific compatibility plugins that do not exist in the original
KiriKiri2 tree, including:

- `motionplayer.dll` / `emoteplayer.dll`
- `psbfile.dll`
- `krkrlive2d.dll`
- `krkrgles.dll`
- `DrawDeviceD2Dm.dll`
- `AlphaMovie.dll`
- several compatibility stubs

Compared with KiriKiri2's `src/plugins/win32`, many plugins are still missing
or only represented by link-success stubs.

## Phase 1: Link-Time Compatibility Stubs

Goal: make common `Plugins.link()` calls fail less often while keeping behavior
explicitly conservative.

1. Add no-op module registrations for plugins seen failing in local logs:
   - `flashPlayer.dll`
   - `layerExSubImage.dll`
   - `gfxEffect.dll`
2. Add low-risk no-op registrations for common platform-only plugins where the
   engine already has native or cross-platform substitutes:
   - `shellExecute.dll`
   - `process.dll`
   - `tasktray.dll`
   - `adjustMonitor.dll`
   - `fpslimit.dll`
   - `systemEx.dll`
3. Add tests that assert these modules can be linked through the internal
   plugin registry.

Commit rule: one commit for the stub module set, one commit for tests.

## Phase 2: Enable Existing Implementations

Goal: turn existing but disabled source into actual registered modules.

1. Evaluate and enable `json.dll`.
2. Evaluate `steam` / `DrawDeviceForSteam`; keep disabled unless the dependency
   and behavior are clean across non-Windows targets.
3. Confirm `AlphaMovie.dll`, `packinone.dll`, and `extNagano.dll` are retained
   in final links under whole-archive builds.

Commit rule: each enabled plugin gets its own commit.

## Phase 3: Complete Existing Partial Implementations

Goal: improve behavior where AetherKiri already has real plugin code.

1. `motionplayer.dll` / `emoteplayer.dll`
   - Replace remaining explicit runtime stubs where behavior is known.
   - Keep reverse-engineering-only placeholders documented when behavior is
     genuinely unknown.
   - Add focused tests for player state, timeline, resource lookup, and layer
     update behavior.
2. `psbfile.dll`
   - Complete `collectResources()` for image, motion, scene, MMO, and sound
     archive types.
   - Implement or intentionally document `PSBMedia::GetListAt`.
   - Add tests using existing PSB/PIMG fixtures under `tests/test_files`.
3. `AlphaMovie.dll` / movie stack
   - Decide whether `AlphaMovie` should be a thin wrapper over the existing
     FFmpeg movie path or remain a documented no-op.
   - If implemented, expose state, dimensions, frame/time properties, and stop
     callbacks consistently.

Commit rule: each behavior area and its tests are committed separately.

## Phase 4: High-Value Missing Plugins

Goal: port or reimplement plugins that unlock broad game compatibility.

Priority candidates:

- Data/parsing: `binaryStream`, `encode`, `expat`, `minizip`, `sqlite3`,
  `sqlite3_xp3_vfs`
- Network: `httprequest`, `httpserv`, `xmlhttprequest`
- Graphics/image: `imagesaver`, `layerEx`, `layerExSave`, `layerExGdiPlus`,
  `qrcode`
- Scripting/runtime: `javascript`, `squirrel`, `onigruma`
- System/Windows compatibility: `win32ole`, `wsh`, `windowExProgress`

Selection rule: implement in the order they appear in real game logs or test
fixtures, not by trying to mechanically port every KiriKiri2 plugin.

Commit rule: one plugin per commit unless a helper library is shared.

## Phase 5: Compatibility Audit Tooling

Goal: make gaps visible and prevent regressions.

1. Add a small registry test that lists internal plugin module names and checks
   expected names.
2. Add a script or test fixture that compares AetherKiri plugin registrations
   with the KiriKiri2 reference directory.
3. Add log-based triage documentation:
   - missing module name
   - whether a no-op stub is acceptable
   - whether behavior must be implemented
   - affected game or fixture

Commit rule: tests/tooling first, documentation updates after.

## Current Next Step

Continue Phase 4 in this order:

1. `httprequest.dll`: defer until a libcurl-backed, event-safe design is chosen.
2. Audit remaining graphics/runtime plugins against real game logs before
   adding more broad stubs.

`onigruma` is not tracked as a missing plugin because AetherKiri already links
Oniguruma in the core TJS regular expression implementation.

## Progress

- Completed Phase 1 first-pass link compatibility stubs.
  - Commit: `ac85821 Add first-pass plugin compatibility stubs`
  - Added registrations for `flashPlayer.dll`, `layerExSubImage.dll`,
    `gfxEffect.dll`, `clipboardEx.dll`, `shellExecute.dll`, `process.dll`,
    `tasktray.dll`, `adjustMonitor.dll`, `fpslimit.dll`, and `systemEx.dll`.
- Completed registry coverage for the first-pass compatibility modules.
  - Commit: `4ad045b Test compatibility stub registrations`
- Completed a real `base64.dll` implementation.
  - Commit: `dec92b9 Implement base64 plugin`
  - Added `Base64.encode(filename)` and `Base64.decode(base64str, filename)`.
  - `decode` writes the decoded file and returns the MD5 hex digest.
- Completed a real `lineParser.dll` implementation.
  - Commit: `9bbdd91 Implement lineParser plugin`
  - Added `LineParser`, text/storage initialization, `getNextLine()`,
    `parse()` / `parseStorage()`, `currentLineNumber`, and `doLine(text, lineNo)`
    callbacks.
- Replaced the `clipboardEx.dll` link-only stub with a minimal safe
  compatibility layer.
  - Commit: `e389b65 Implement clipboardEx compatibility layer`
  - Added `cbfBitmap`, `cbfTJS`, `Clipboard.hasFormat()`, process-local
    `Clipboard.asTJS`, `Clipboard.setMultipleData()`, bitmap no-op methods, and
    `Window.clipboardWatchEnabled` as a safe stub.
  - Full Win32 bitmap clipboard and clipboard viewer-chain watching remain
    intentionally unsupported.
- Completed a cross-platform `memfile.dll` implementation.
  - Commit: `e5fe807 Implement memfile plugin`
  - Added `mem` storage media backed by `tTVPMemoryStream` plus the original
    `Storages.*Memory*` helper methods.
- Completed a real `encode.dll` implementation.
  - Commit: `59dd328 Implement encode plugin`
  - Added global `Encode.encode(str, encoding)` and
    `Encode.decode(octet, encoding)` for `UTF-8`, `EUC-JP`, and `Shift_JIS`.
  - Reused the original EUC/JIS mapping tables and implemented Shift_JIS
    conversion explicitly instead of relying on platform locale narrow-string
    conversion.
- Completed the core cross-platform `binaryStream.dll` implementation.
  - Commit: `7ad2288 Implement binaryStream core plugin`
  - Added `BinaryStream`, storage open/close/seek/tell, raw octet/string
    read/write, integer LE/BE read/write, `copy`, `compress`, `decompress`,
    progress callback, constants, Adler-32, and optional MD5 digest output.
  - Explicitly rejects non-empty `setFilter()` and `elm.filter`; original
    external DLL filter ABI is Windows-only and not portable.
- Added extra `binaryStream.dll` compatibility aliases.
  - Commit: `22667b9 Add binaryStream compatibility aliases`
  - Added string mode parsing, `readI8LE` / `readI8BE`, `writeI8LE` /
    `writeI8BE`, and upper-case constant aliases.
- Replaced the placeholder `json.dll` implementation with real JSON behavior.
  - Commit: `6cb4b01 Implement json plugin`
  - Added `Scripts.evalJSON`, `Scripts.evalJSONStorage`, `Scripts.saveJSON`,
    and `Scripts.toJSONString` with actual JSON parse / stringify behavior.
- Completed the first `minizip.dll` implementation stage.
  - Commit: `d36b828 Implement minizip Zip and Unzip classes`
  - Added `Zip.open/add/close` and `Unzip.open/list/extract/close` using the
    existing minizip dependency.
  - Password encryption is explicitly unsupported in this first pass.
- Completed `minizip.dll` storage mounting.
  - Commit: `808529c Add minizip storage mounting`
  - Added `Storages.mountZip()`, `Storages.unmountZip()`, and read-only
    `zip://domain/path` storage access backed by in-memory streams.
- Completed a real `expat.dll` implementation.
  - Commit: `b3df1e4 Implement expat plugin`
  - Added `XMLParser`, `parse()`, `parseStorage()`, Expat-backed event
    callbacks, error/current-position properties, and the original typo
    compatibility property `currentButeCount`.
- Completed a real `qrcode.dll` implementation.
  - Commit: `ba6f5d9 Implement qrcode plugin`
  - Added `Layer.drawQRCode(value, ecLevel, qrVersion, autoExtent, maskPattern)`
    using the original QR encoder with cross-platform type/utility
    replacements.
- Completed the synchronous `imagesaver.dll` BMP path.
  - Commit: `3c477b5 Implement imagesaver plugin`
  - Added global `saveLayerImage(layer, filename, "bmp")` with cross-platform
    BMP header writing and bottom-up layer buffer serialization.
- Completed the synchronous `sqlite3.dll` core.
  - Commit: `ad75332 Implement sqlite3 sync plugin core`
  - Added `Sqlite` and `SqliteStatement`, SQLite constants, exec/value/query
    stepping, parameter binding, column access, local-file and in-memory
    database opening, and bundled SQLite amalgamation sources.
  - Deferred original `SqliteThread`, `sqlite3_xp3_vfs`, and custom extension
    functions for follow-up compatibility work.
- Added synchronous `layerExSave.dll` compatibility helpers.
  - Commit: `8116bdb Add layerExSave sync compatibility helpers`
  - Added `Layer.saveLayerImagePng`, `saveLayerImageTlg5`, crop/diff helpers,
    diff pixel marking, blue-to-alpha copy, blank checks, alpha clearing, and
    average color calculation.
  - `saveLayerImagePngOctet` remains explicitly unsupported because it needs a
    PNG encoder that returns an octet rather than writing through
    `Layer.saveLayerImage`.
- Corrected the old link-only `layerExSave.dll` registration.
  - Commit: `4b98bc2 Remove obsolete layerExSave stub`
  - Removed the empty `layerExSave.dll` stub from `extrans.cpp` now that the
    real compatibility module is registered.

## Verification Notes

- `cmake --preset "MacOS Debug Config" -DENABLE_TESTS=ON` completes local
  generation.
- `ninja -C out/macos/debug cpp/plugins/CMakeFiles/krkr2plugin.dir/dummy_plugin_stubs.cpp.o`
  passes.
- `ninja -C out/macos/debug cpp/plugins/CMakeFiles/krkr2plugin.dir/base64.cpp.o`
  passes.
- `cmake --build out/macos/debug --target cpp/plugins/CMakeFiles/krkr2plugin.dir/lineParser.cpp.o -j2`
  passes.
- `cmake --build out/macos/debug --target cpp/plugins/CMakeFiles/krkr2plugin.dir/clipboardEx.cpp.o -j2`
  passes.
- `cmake --build out/macos/debug --target cpp/plugins/CMakeFiles/krkr2plugin.dir/dummy_plugin_stubs.cpp.o -j2`
  passes after removing the old `clipboardEx.dll` stub registration.
- `cmake --build out/macos/debug --target cpp/plugins/CMakeFiles/krkr2plugin.dir/memfile.cpp.o -j2`
  passes.
- `cmake --build out/macos/debug --target cpp/plugins/CMakeFiles/krkr2plugin.dir/encode.cpp.o -j2`
  passes.
- `cmake --build out/macos/debug --target cpp/plugins/CMakeFiles/krkr2plugin.dir/binaryStream.cpp.o -j2`
  passes.
- `cmake --build out/macos/debug --target cpp/plugins/CMakeFiles/krkr2plugin.dir/json/jsonPlugin.cpp.o -j2`
  passes.
- `cmake --build out/macos/debug --target cpp/plugins/CMakeFiles/krkr2plugin.dir/minizip.cpp.o -j2`
  passes.
- `cmake --build out/macos/debug --target cpp/plugins/CMakeFiles/krkr2plugin.dir/expat.cpp.o -j2`
  passes.
- `cmake --build out/macos/debug --target cpp/plugins/CMakeFiles/krkr2plugin.dir/qrcode.cpp.o cpp/plugins/CMakeFiles/krkr2plugin.dir/qrcode/QR_Encode.cpp.o -j2`
  passes.
- `cmake --build out/macos/debug --target cpp/plugins/CMakeFiles/krkr2plugin.dir/imagesaver.cpp.o -j2`
  passes.
- `cmake --build out/macos/debug --target cpp/plugins/CMakeFiles/krkr2plugin.dir/sqlitePlugin.cpp.o cpp/plugins/CMakeFiles/krkr2plugin.dir/sqlite/sqlite3.c.o -j2`
  passes with one macOS SDK deprecation warning from the bundled old SQLite C
  source.
- `cmake --build out/macos/debug --target cpp/plugins/CMakeFiles/krkr2plugin.dir/layerExSaveCompat.cpp.o -j2`
  passes.
- `cmake --build out/macos/debug --target cpp/plugins/CMakeFiles/krkr2plugin.dir/extrans.cpp.o -j2`
  passes after removing the obsolete `layerExSave.dll` stub.
- `ninja -C out/macos/debug tests/unit-tests/plugins/CMakeFiles/motionplayer-dll.dir/registry.cpp.o`
  passes.
- Full `libkrkr2plugin.a` / `krkr2plugin` build is currently blocked before
  plugin linkage by the pre-existing `cpp/core/visual/ogl/RenderManager_ogl.cpp`
  `GL::fGetProcAddress` compile error in the MacOS Debug preset.
