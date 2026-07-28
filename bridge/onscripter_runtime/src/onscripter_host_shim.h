#pragma once

// ONScripter is normally an executable and terminates the process from its
// `end` command and several fatal-error paths. The embedded host converts that
// process exit into a C++ exception which is contained by the runtime thread.
#include <SDL.h>
#include <cstdlib>

extern "C" [[noreturn]] void aetherkiri_onscripter_host_exit(int code);
extern "C" void SDLCALL
aetherkiri_onscripter_free_surface(SDL_Surface *surface);
extern "C" int aetherkiri_onscripter_play_video(
    const char *filename, int click_to_skip, int loop);
extern "C" void aetherkiri_onscripter_stop_video();
extern "C" void aetherkiri_onscripter_shutdown_parallel();
extern "C" void aetherkiri_onscripter_configure_video(
    int has_position, int x, int y, int width, int height,
    int asynchronous);

#define exit aetherkiri_onscripter_host_exit
#define SDL_FreeSurface aetherkiri_onscripter_free_surface
