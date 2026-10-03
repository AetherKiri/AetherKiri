#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d "${TMPDIR:-/tmp}/aetherkiri-renpy-loader.XXXXXX")"
trap 'rm -rf "$work"' EXIT

cat > "$work/fake_launcher.cpp" <<'CPP'
#include "renpy_mobile_launcher.h"
#include <cstring>
static unsigned char pixel[4] = {0x12, 0x34, 0x56, 0xff};
extern "C" int renpy_mobile_init(const renpy_mobile_config_t*, const renpy_mobile_host_t*) { return 0; }
extern "C" int renpy_mobile_tick(unsigned int) { return 0; }
extern "C" int renpy_mobile_frame(renpy_mobile_frame_t* frame) {
  if (!frame) return -1;
  frame->rgba = pixel; frame->width = 1; frame->height = 1; frame->stride = 4; frame->serial = 7;
  return 0;
}
extern "C" int renpy_mobile_input(const renpy_mobile_input_t*) { return 0; }
extern "C" int renpy_mobile_pause(void) { return 0; }
extern "C" int renpy_mobile_resume(void) { return 0; }
extern "C" void renpy_mobile_shutdown(void) {}
CPP
cat > "$work/test_loader.cpp" <<'CPP'
#include "renpy_mobile_loader.h"
#include <cassert>
#include <cstring>
using namespace aetherkiri::renpy::mobile;
int main() {
  Launcher launcher;
  assert(launcher.available());
  renpy_mobile_config_t config{}; config.struct_size = sizeof(config); config.abi_version = RENPY_MOBILE_LAUNCHER_ABI_VERSION;
  renpy_mobile_host_t host{}; host.struct_size = sizeof(host); host.abi_version = RENPY_MOBILE_LAUNCHER_ABI_VERSION;
  assert(launcher.Init(config, host) == RENPY_MOBILE_OK);
  assert(launcher.Tick(16) == RENPY_MOBILE_OK);
  renpy_mobile_frame_t frame{}; frame.struct_size = sizeof(frame);
  assert(launcher.Frame(&frame) == RENPY_MOBILE_OK);
  assert(frame.width == 1 && frame.height == 1 && frame.stride == 4 && frame.serial == 7);
  renpy_mobile_input_t input{}; input.struct_size = sizeof(input); input.type = RENPY_MOBILE_INPUT_KEY;
  assert(launcher.Input(input) == RENPY_MOBILE_OK);
  assert(launcher.Pause() == RENPY_MOBILE_OK);
  assert(launcher.Resume() == RENPY_MOBILE_OK);
  launcher.Shutdown();
  assert(!launcher.initialized());
  return 0;
}
CPP

cxx="${CXX:-c++}"
"$cxx" -std=c++17 -fPIC -shared -D__ANDROID__ \
  -I"$repo_root/bridge/renpy_runtime/mobile_launcher/include" \
  "$work/fake_launcher.cpp" -o "$work/librenpython.so"
"$cxx" -std=c++17 -D__ANDROID__ \
  -I"$repo_root/bridge/renpy_runtime/mobile_launcher/include" \
  "$work/test_loader.cpp" \
  "$repo_root/bridge/renpy_runtime/mobile_launcher/src/renpy_mobile_loader.cpp" \
  -ldl -o "$work/test_loader"
LD_LIBRARY_PATH="$work${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/test_loader"
printf '%s\n' 'Ren''Py mobile lifecycle loader test passed'
