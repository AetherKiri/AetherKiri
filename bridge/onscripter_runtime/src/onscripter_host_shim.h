#pragma once

// ONScripter is normally an executable and terminates the process from its
// `end` command and several fatal-error paths. The embedded host converts that
// process exit into a C++ exception which is contained by the runtime thread.
#include <SDL.h>
#include <cstdlib>

extern "C" [[noreturn]] void aetherkiri_onscripter_host_exit(
    int code, const char *source_file, int source_line);
extern "C" void SDLCALL
aetherkiri_onscripter_free_surface(SDL_Surface *surface);
extern "C" void
aetherkiri_onscripter_publish_frame(SDL_Surface *surface,
                                    const SDL_Rect *dirty_rect);
extern "C" int aetherkiri_onscripter_play_video(
    const char *filename, int click_to_skip, int loop);
extern "C" void aetherkiri_onscripter_stop_video();
extern "C" void aetherkiri_onscripter_shutdown_parallel();
extern "C" int aetherkiri_onscripter_wait_event(SDL_Event *event);
extern "C" void aetherkiri_onscripter_configure_video(
    int has_position, int x, int y, int width, int height,
    int asynchronous);

#define exit(code) aetherkiri_onscripter_host_exit((code), __FILE__, __LINE__)
#define SDL_FreeSurface aetherkiri_onscripter_free_surface
