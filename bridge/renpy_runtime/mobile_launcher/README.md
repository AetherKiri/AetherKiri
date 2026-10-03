# Host-owned Ren'Py mobile launcher scaffold

This directory is the reviewable boundary for the native fork required to make
Ren'Py run inside an existing AetherKiri/Godot loop. It is intentionally a
contract and build/replacement scaffold. It does not contain a playable
launcher and it does not alter the current `NOT_SUPPORTED` mobile provider.

## Verified official inputs

The pinned 8.5.3 mobile archives in `renpy-mobile-staged/` contain stripped
outputs only. The corresponding official source/build layout is available in
the `renpy-build` repository:

- `runtime/librenpython_android.c`: `SDL_main` initializes the Android
  environment, calls `call_prepare_python`, and enters `start_python`; the
  latter ends at blocking `Py_RunMain`
- `runtime/librenpython.c`: `launcher_main` builds `PyConfig` and ends at
  blocking `Py_RunMain`
- `runtime/jniwrapperstuff.h`: Android JNI export-name helper
- `tasks/renpython.py`: `build_android` compiles
  `librenpython_android.c`; `link_android` links `librenpython.so`;
  `link_ios` archives `librenpython.o` as `librenpython.a`
- `renios/prototype/main.c`: the UIKit prototype calls
  `SDL_RunApp(..., launcher_main, NULL)` (older SDL2/Renios archives use the
  equivalent `SDL_UIKitRunApp` spelling)

The official command-line hooks are documented by `build.sh --print-plan`:

- Android: `./build.sh --platform android rebuild rapt rapt-sdl2`
- iOS: `./build.sh --platform ios rebuild renios`

`build.sh --source-build-plan --renpy-build /path/to/renpy-build` prints the
Ubuntu 24.04 prerequisites, source patch commands, build commands, and exact
RAPT/Renios artifact destinations. `--check-renpy-build` performs a strict
read-only preflight for the source checkout, patch applicability, host tools,
64 GiB disk requirement, and supplied Android/iOS SDK archives. These commands
are opt-in and are not part of the normal mobile CI build because the official
renpy-build process is a large, multi-hour source rebuild and requires licensed
iOS SDK tarballs. CI only validates the plan; it does not pretend to build a
playable fork.

The current official outputs are process launchers. Do not call them from a
Godot frame callback and do not copy `renios/prototype/main.c` into the host
application.

## Lifecycle ABI to implement

`include/renpy_mobile_launcher.h` defines ABI version 1 and the required
exports:

- `renpy_mobile_init(config, host)`
- `renpy_mobile_tick(budget_ms)`
- `renpy_mobile_frame(out_frame)`
- `renpy_mobile_input(event)`
- `renpy_mobile_pause()` / `renpy_mobile_resume()`
- `renpy_mobile_shutdown()`

The eventual fork must move Python/SDL setup out of `SDL_main`/`launcher_main`
and make every lifecycle call return promptly. The calls must not invoke
`Py_RunMain`, `SDL_main`, `SDL_RunApp`, `SDL_UIKitRunApp`, `UIApplicationMain`,
or create an Android Activity. `frame` returns a borrowed RGBA view for the
host to copy; the host callback and ownership rules are in the header.

The header is only an ABI contract. The `.c.template` files under
`patches/android` and `patches/ios` are deliberately non-playable starting
points: init returns `RENPY_MOBILE_NOT_IMPLEMENTED`, all runtime calls remain
invalid until a real implementation initializes them, and no output object is
copied into a mobile package.

`build.sh --compile-contract` compiles those templates with `-Wall -Wextra
-Werror` on the host so CI can validate the C ABI and required exported names.
This is a source-level check, not a Ren'Py SDK rebuild:

```text
bridge/renpy_runtime/mobile_launcher/build.sh --compile-contract \
  --renpy-build /path/to/renpy-build --output-dir /tmp/renpy-mobile-contract
```

The native templates identify the real remaining patch points. In addition to
factoring the C launcher, Ren'Py's Python `renpy/bootstrap.py`, `renpy/main.py`,
and `renpy/display/core.py` must be made cooperatively resumable. Their current
call chain enters `renpy.execution.run_context(True)` and
`Interface.interact_core`, both of which keep control until a script or user
interaction completes. Splitting only `Py_InitializeFromConfig` from
`Py_RunMain` does not make a frame-driven engine. Android also exits through
`android.activity.finishAndRemoveTask()` and Java `System.exit(0)` in
`bootstrap.py` cleanup, which an embedded fork must replace with host-owned
shutdown.

The host-side `src/renpy_mobile_loader.cpp` resolves this ABI when a real
fork is supplied: Android loads all seven symbols from `librenpython.so`, and
iOS uses weak imports from the optional `librenpython.a`. The mobile provider
forwards lifecycle and input calls and copies the borrowed RGBA frame into the
normal Aether frame API. With the official blocking archive, symbol resolution
fails and the provider remains `ENGINE_RESULT_NOT_SUPPORTED`.

## Rebuild and replacement hook

`build.sh` performs a source-only check by default when invoked as:

```text
bridge/renpy_runtime/mobile_launcher/build.sh \
  --check --renpy-build /path/to/renpy-build
```

Use `--print-plan` to print the official source inputs, task hooks, and exact
staged destinations. A future lifecycle fork is built outside this scaffold,
then installed with explicit ABI-matched artifacts:

```text
bridge/renpy_runtime/mobile_launcher/build.sh --install \
  --renpy-build /path/to/renpy-build \
  --stage /workspace/shared/renpy-mobile-staged \
  --android-so-arm64 /path/to/arm64-v8a/librenpython.so \
  --android-so-armv7 /path/to/armeabi-v7a/librenpython.so \
  --android-so-x86_64 /path/to/x86_64/librenpython.so \
  --ios-debug-a /path/to/debug/librenpython.a \
  --ios-release-a /path/to/release/librenpython.a
```

`--install` refuses artifacts that do not expose all seven lifecycle symbols.
It copies only the explicitly supplied files to:

- RAPT: `rapt/prototype/renpyandroid/src/main/jniLibs/{arm64-v8a,armeabi-v7a,x86_64}/librenpython.so`
- Renios: `renios/prototype/prebuilt/{debug,release}/librenpython.a`

The script does not build, sign, package, or claim device/simulator support.
After a real fork is installed, the Android/iOS archive probes and host-owned
lifecycle/input/frame tests must be extended before enabling the provider.
