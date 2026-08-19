function(aetherkiri_prepare_onscripter_yuri_sources
         upstream_dir generated_dir out_base_source out_sound_source
         out_command_source out_event_source out_parser_source
         out_parallel_source)
    file(MAKE_DIRECTORY "${generated_dir}")

    file(READ "${upstream_dir}/ONScripter.cpp" base_source)
    set(command_dispatch_original [=[
    if (cmd[0] >= 'a' && cmd[0] <= 'z'){
]=])
    set(command_dispatch_embedded [=[
    // NScripter's deletemenu only removes the native Windows menubar.
    // AetherKiri is embedded in Godot and never creates that menu, so the
    // cross-platform equivalent is an intentional no-op.
    if (!strcmp(cmd, "deletemenu"))
        return RET_CONTINUE;

    if (cmd[0] >= 'a' && cmd[0] <= 'z'){
]=])
    string(FIND "${base_source}" "${command_dispatch_original}"
           command_dispatch_position)
    if(command_dispatch_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri command dispatch changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${command_dispatch_original}" "${command_dispatch_embedded}"
           base_source "${base_source}")

    set(gamecontroller_init_original [=[
#if !defined(IOS)
#if defined(ANDROID)
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
#endif
    if(SDL_InitSubSystem( SDL_INIT_GAMECONTROLLER ) == 0)
        utils::printInfo("Initialize GameController\n");
    controller = SDL_GameControllerOpen(0);
    if(controller != NULL)
        utils::printInfo("GameController found\n");
#endif
]=])
    set(gamecontroller_init_embedded [=[
#if !defined(IOS) && !defined(AETHERKIRI_EMBEDDED_HOST)
#if defined(ANDROID)
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
#endif
    if(SDL_InitSubSystem( SDL_INIT_GAMECONTROLLER ) == 0)
        utils::printInfo("Initialize GameController\n");
    controller = SDL_GameControllerOpen(0);
    if(controller != NULL)
        utils::printInfo("GameController found\n");
#endif
]=])
    string(FIND "${base_source}" "${gamecontroller_init_original}"
           gamecontroller_init_position)
    if(gamecontroller_init_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri game controller initialization changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${gamecontroller_init_original}" "${gamecontroller_init_embedded}"
           base_source "${base_source}")

    set(sdl_shutdown_original [=[
    SDL_DestroyRenderer(renderer);
    SDL_Quit();
}
]=])
    set(sdl_shutdown_embedded [=[
    SDL_DestroyRenderer(renderer);
    // Upstream detaches its persistent pixel-worker threads. Stop and join
    // them while SDL and this GDExtension are still loaded.
    aetherkiri_onscripter_shutdown_parallel();
    SDL_Quit();
}
]=])
    string(FIND "${base_source}" "${sdl_shutdown_original}"
           sdl_shutdown_position)
    if(sdl_shutdown_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri SDL shutdown changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${sdl_shutdown_original}" "${sdl_shutdown_embedded}"
           base_source "${base_source}")

    set(flush_present_original [=[
    SDL_RenderPresent(renderer);
}
]=])
    set(flush_present_embedded [=[
    SDL_RenderPresent(renderer);
    // The embedded Godot host must never read accumulation_surface while a
    // later ONS command is composing it. Publish only this fully committed
    // frame, after the same presentation boundary used by upstream SDL.
    aetherkiri_onscripter_publish_frame(accumulation_surface, &rect);
}
]=])
    string(FIND "${base_source}" "${flush_present_original}"
           flush_present_position)
    if(flush_present_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri flushDirect changed; update the frame-publication overlay.")
    endif()
    string(REPLACE "${flush_present_original}" "${flush_present_embedded}"
           base_source "${base_source}")

    set(generated_base "${generated_dir}/ONScripter.cpp")
    file(WRITE "${generated_base}" "${base_source}")

    file(READ "${upstream_dir}/ONScripter_sound.cpp" sound_source)
    set(sound_mpeg_original [=[
#if !defined(WINRT) && (defined(WIN32) || defined(_WIN32))
    system(absolute_filename);
#elif defined(IOS)
    playVideoIOS(absolute_filename, click_flag, loop_flag);
#elif defined(ANDROID)
    playVideoAndroid(absolute_filename);
#elif defined(WEB)
    playVideoWeb(absolute_filename, click_flag, loop_flag);
#else
    utils::printError( "mpegplay command is disabled.\n" );
#endif
]=])
    set(sound_mpeg_embedded [=[
#if defined(AETHERKIRI_EMBEDDED_HOST)
    ret = aetherkiri_onscripter_play_video(
        filename, click_flag ? 1 : 0, loop_flag ? 1 : 0);
#elif !defined(WINRT) && (defined(WIN32) || defined(_WIN32))
    system(absolute_filename);
#elif defined(IOS)
    playVideoIOS(absolute_filename, click_flag, loop_flag);
#elif defined(ANDROID)
    playVideoAndroid(absolute_filename);
#elif defined(WEB)
    playVideoWeb(absolute_filename, click_flag, loop_flag);
#else
    utils::printError( "mpegplay command is disabled.\n" );
#endif
]=])
    string(FIND "${sound_source}" "${sound_mpeg_original}"
           sound_mpeg_position)
    if(sound_mpeg_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri playMPEG source changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${sound_mpeg_original}" "${sound_mpeg_embedded}"
           sound_source "${sound_source}")

    set(sound_quit_original [=[
              case SDL_QUIT:
                ret = 1;
              case SDL_MOUSEBUTTONUP:
                done_flag = true;
                break;
]=])
    set(sound_quit_embedded [=[
#if defined(AETHERKIRI_EMBEDDED_HOST)
              case SDL_QUIT:
                break;
#else
              case SDL_QUIT:
                ret = 1;
              case SDL_MOUSEBUTTONUP:
                done_flag = true;
                break;
#endif
]=])
    string(FIND "${sound_source}" "${sound_quit_original}"
           sound_quit_position)
    if(sound_quit_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri sound SDL_QUIT handler changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${sound_quit_original}" "${sound_quit_embedded}"
           sound_source "${sound_source}")

    set(sound_avi_original [=[
#if !defined(WINRT) && (defined(WIN32) || defined(_WIN32))
    system(absolute_filename);
#elif defined(IOS)
    playVideoIOS(absolute_filename, click_flag, loop_flag);
#elif defined(ANDROID)
    playVideoAndroid(absolute_filename);
#else
    utils::printError( "avi command is disabled.\n" );
#endif
]=])
    set(sound_avi_embedded [=[
#if defined(AETHERKIRI_EMBEDDED_HOST)
    ret = aetherkiri_onscripter_play_video(
        filename, click_flag ? 1 : 0, 0);
#elif !defined(WINRT) && (defined(WIN32) || defined(_WIN32))
    system(absolute_filename);
#elif defined(IOS)
    playVideoIOS(absolute_filename, click_flag, loop_flag);
#elif defined(ANDROID)
    playVideoAndroid(absolute_filename);
#else
    utils::printError( "avi command is disabled.\n" );
#endif
]=])
    string(FIND "${sound_source}" "${sound_avi_original}"
           sound_avi_position)
    if(sound_avi_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri playAVI source changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${sound_avi_original}" "${sound_avi_embedded}"
           sound_source "${sound_source}")

    set(generated_sound "${generated_dir}/ONScripter_sound.cpp")
    file(WRITE "${generated_sound}" "${sound_source}")

    file(READ "${upstream_dir}/ONScripter_event.cpp" event_source)
    set(timer_callback_original [=[
extern "C" Uint32 SDLCALL timerCallback( Uint32 interval, void *param )
{
    SDL_RemoveTimer( timer_id );
    timer_id = 0;

    SDL_Event event;
    event.type = ONS_TIMER_EVENT;
    SDL_PushEvent( &event );

    return 0;
}
]=])
    set(timer_callback_embedded [=[
extern "C" Uint32 SDLCALL timerCallback( Uint32 interval, void *param )
{
#if defined(AETHERKIRI_EMBEDDED_HOST) && defined(AETHERKIRI_IOS)
    // Godot and the embedded ONS runtime share SDL's process-wide event
    // queue on iOS. Keep retrying the wake event until the ONS wait consumes
    // it; waitEventSub removes the timer immediately after that happens.
    SDL_Event event;
    event.type = ONS_TIMER_EVENT;
    SDL_PushEvent( &event );
    return 8;
#else
    SDL_RemoveTimer( timer_id );
    timer_id = 0;

    SDL_Event event;
    event.type = ONS_TIMER_EVENT;
    SDL_PushEvent( &event );

    return 0;
#endif
}
]=])
    string(FIND "${event_source}" "${timer_callback_original}"
           timer_callback_position)
    if(timer_callback_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri timer callback changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${timer_callback_original}" "${timer_callback_embedded}"
           event_source "${event_source}")

    set(wait_event_sub_original [=[
void ONScripter::waitEventSub(int count)
{
    next_time = count;
    timerEvent(true);

    runEventLoop();
    removeEvent( ONS_BREAK_EVENT );
}
]=])
    set(wait_event_sub_embedded [=[
void ONScripter::waitEventSub(int count)
{
    next_time = count;
#if defined(AETHERKIRI_EMBEDDED_HOST) && defined(AETHERKIRI_IOS)
    // timerEvent(true) implements a zero-duration wait by pushing one
    // ONS_BREAK_EVENT. Avoid the shared queue for that synchronous case and
    // reproduce the exact timeout state handled by runEventLoop.
    if (count == 0) {
        if (automode_flag || autoclick_time > 0)
            current_button_state.button = 0;
        else if (usewheel_flag) {
            current_button_state.button = -5;
            sprintf(current_button_state.str, "TIMEOUT");
        }
        else {
            current_button_state.button = -2;
            sprintf(current_button_state.str, "TIMEOUT");
        }
        return;
    }
#endif
    timerEvent(true);

    runEventLoop();
#if defined(AETHERKIRI_EMBEDDED_HOST) && defined(AETHERKIRI_IOS)
    if (timer_id) {
        SDL_RemoveTimer(timer_id);
        timer_id = 0;
    }
#endif
    removeEvent( ONS_BREAK_EVENT );
}
]=])
    string(FIND "${event_source}" "${wait_event_sub_original}"
           wait_event_sub_position)
    if(wait_event_sub_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri waitEventSub changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${wait_event_sub_original}" "${wait_event_sub_embedded}"
           event_source "${event_source}")

    set(run_event_wait_original [=[
    while ( SDL_WaitEvent(&event) ) {
]=])
    set(run_event_wait_embedded [=[
#if defined(AETHERKIRI_EMBEDDED_HOST)
    while ( aetherkiri_onscripter_wait_event(&event) ) {
#else
    while ( SDL_WaitEvent(&event) ) {
#endif
]=])
    string(FIND "${event_source}" "${run_event_wait_original}"
           run_event_wait_position)
    if(run_event_wait_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri event wait changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${run_event_wait_original}" "${run_event_wait_embedded}"
           event_source "${event_source}")

    set(mouse_event_guard_original [=[
#if !defined(ANDROID) && !defined(IOS) && !defined(WINRT)
          case SDL_MOUSEMOTION:
]=])
    set(mouse_event_guard_embedded [=[
// Godot normalizes touch to SDL mouse events before it reaches this embedded
// runtime. Upstream Android consumes SDL_FINGER* from SDLActivity instead, but
// that JNI event path is intentionally unavailable inside the Godot host.
#if defined(AETHERKIRI_EMBEDDED_HOST) || (!defined(ANDROID) && !defined(IOS) && !defined(WINRT))
          case SDL_MOUSEMOTION:
]=])
    string(FIND "${event_source}" "${mouse_event_guard_original}"
           mouse_event_guard_position)
    if(mouse_event_guard_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri mouse event guard changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${mouse_event_guard_original}" "${mouse_event_guard_embedded}"
           event_source "${event_source}")

    set(quit_event_original [=[
          case SDL_QUIT:
            endCommand();
            break;
]=])
    set(quit_event_embedded [=[
#if defined(AETHERKIRI_EMBEDDED_HOST)
          case SDL_QUIT:
            break;
#else
          case SDL_QUIT:
            endCommand();
            break;
#endif
]=])
    string(FIND "${event_source}" "${quit_event_original}"
           quit_event_position)
    if(quit_event_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri SDL_QUIT handler changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${quit_event_original}" "${quit_event_embedded}"
           event_source "${event_source}")

    set(generated_event "${generated_dir}/ONScripter_event.cpp")
    file(WRITE "${generated_event}" "${event_source}")

    file(READ "${upstream_dir}/ONScripter_command.cpp" command_source)
    set(movie_stop_original [=[
    if (script_h.compareString("stop")){
        script_h.readLabel();
        utils::printError(" [movie stop] is not supported yet!!\n");
        return RET_CONTINUE;
    }
]=])
    set(movie_stop_embedded [=[
    if (script_h.compareString("stop")){
        script_h.readLabel();
        aetherkiri_onscripter_stop_video();
        return RET_CONTINUE;
    }
]=])
    string(FIND "${command_source}" "${movie_stop_original}"
           movie_stop_position)
    if(movie_stop_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri movie stop parser changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${movie_stop_original}" "${movie_stop_embedded}"
           command_source "${command_source}")

    set(movie_flags_original [=[
    bool click_flag = false;
    bool loop_flag = false;
]=])
    set(movie_flags_embedded [=[
    bool click_flag = false;
    bool loop_flag = false;
    bool async_flag = false;
    bool has_position = false;
    int movie_x = 0;
    int movie_y = 0;
    int movie_width = 0;
    int movie_height = 0;
]=])
    string(FIND "${command_source}" "${movie_flags_original}"
           movie_flags_position)
    if(movie_flags_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri movie flags changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${movie_flags_original}" "${movie_flags_embedded}"
           command_source "${command_source}")

    set(movie_pos_original [=[
        if (script_h.compareString("pos")){ // not supported yet
            script_h.readLabel();
            script_h.readInt();
            script_h.readInt();
            script_h.readInt();
            script_h.readInt();
            utils::printError(" [movie pos] is not supported yet!!\n");
        }
]=])
    set(movie_pos_embedded [=[
        if (script_h.compareString("pos")){
            script_h.readLabel();
            movie_x = script_h.readInt();
            movie_y = script_h.readInt();
            movie_width = script_h.readInt();
            movie_height = script_h.readInt();
            has_position = movie_width > 0 && movie_height > 0;
        }
]=])
    string(FIND "${command_source}" "${movie_pos_original}"
           movie_pos_position)
    if(movie_pos_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri movie pos parser changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${movie_pos_original}" "${movie_pos_embedded}"
           command_source "${command_source}")

    set(movie_async_original [=[
        else if (script_h.compareString("async")){ // not supported yet
            script_h.readLabel();
            utils::printError(" [movie async] is not supported yet!!\n");
        }
]=])
    set(movie_async_embedded [=[
        else if (script_h.compareString("async")){
            script_h.readLabel();
            async_flag = true;
        }
]=])
    string(FIND "${command_source}" "${movie_async_original}"
           movie_async_position)
    if(movie_async_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri movie async parser changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${movie_async_original}" "${movie_async_embedded}"
           command_source "${command_source}")

    set(movie_play_original [=[
    if (playMPEG(filename, click_flag, loop_flag)) endCommand();
]=])
    set(movie_play_embedded [=[
    aetherkiri_onscripter_configure_video(
        has_position ? 1 : 0, movie_x, movie_y, movie_width, movie_height,
        async_flag ? 1 : 0);
    if (playMPEG(filename, click_flag, loop_flag)) endCommand();
]=])
    string(FIND "${command_source}" "${movie_play_original}"
           movie_play_position)
    if(movie_play_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri movie playback call changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${movie_play_original}" "${movie_play_embedded}"
           command_source "${command_source}")

    set(generated_command "${generated_dir}/ONScripter_command.cpp")
    file(WRITE "${generated_command}" "${command_source}")

    file(READ "${upstream_dir}/ScriptParser.cpp" parser_source)
    string(CONCAT envdata_save_original
        "int ScriptParser::saveFileIOBuf( const char *filename, int offset, const char *savestr )\n"
        "{\n"
        "    bool use_save_dir = false;\n"
        "    if (strcmp(filename, \"envdata\") != 0) use_save_dir = true;\n")
    string(CONCAT envdata_save_embedded
        "int ScriptParser::saveFileIOBuf( const char *filename, int offset, const char *savestr )\n"
        "{\n"
        "    // The embedded host keeps every mutable file in Godot's writable\n"
        "    // directory, including envdata, so imported game folders may be read-only.\n"
        "    bool use_save_dir = true;\n")
    string(FIND "${parser_source}" "${envdata_save_original}"
           envdata_save_position)
    if(envdata_save_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri envdata save path changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${envdata_save_original}" "${envdata_save_embedded}"
           parser_source "${parser_source}")

    set(envdata_load_original [=[
size_t ScriptParser::loadFileIOBuf( const char *filename )
{
    bool use_save_dir = false;
    if (strcmp(filename, "envdata") != 0) use_save_dir = true;

    FILE *fp;
    if ( (fp = fopen( filename, "rb", use_save_dir )) == NULL )
        return 0;
]=])
    set(envdata_load_embedded [=[
size_t ScriptParser::loadFileIOBuf( const char *filename )
{
    bool use_save_dir = true;

    FILE *fp;
    if ( (fp = fopen( filename, "rb", use_save_dir )) == NULL ){
        // Preserve a packaged legacy envdata as the first-run default, then
        // save subsequent changes only to the writable directory.
        if (strcmp(filename, "envdata") != 0 ||
            (fp = fopen(filename, "rb", false)) == NULL)
            return 0;
    }
]=])
    string(FIND "${parser_source}" "${envdata_load_original}"
           envdata_load_position)
    if(envdata_load_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri envdata load path changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${envdata_load_original}" "${envdata_load_embedded}"
           parser_source "${parser_source}")

    set(generated_parser "${generated_dir}/ScriptParser.cpp")
    file(WRITE "${generated_parser}" "${parser_source}")

    file(READ "${upstream_dir}/Parallel.cpp" parallel_source)
    set(parallel_detach_original [=[
      SDL_DetachThread(thread->thread);
      ++threadCreated;
]=])
    set(parallel_detach_embedded [=[
      // Keep the joinable handle. Detached workers can otherwise execute
      // code from this GDExtension after Godot has unloaded it.
      ++threadCreated;
]=])
    string(FIND "${parallel_source}" "${parallel_detach_original}"
           parallel_detach_position)
    if(parallel_detach_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri parallel worker creation changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${parallel_detach_original}"
                   "${parallel_detach_embedded}"
                   parallel_source "${parallel_source}")

    set(parallel_destructor_original [=[
    ThreadPool::~ThreadPool() {
      sync();
      for (int i = 0; i < threadCreated; ++i) {
        SDL_AtomicSet(&threads[i].threadData.status, Thread::Status::EXIT);
        SDL_SemPost(threads[i].threadData.sem);
      }
    }
]=])
    set(parallel_destructor_embedded [=[
    ThreadPool::~ThreadPool() {
      sync();
      for (int i = 0; i < threadCreated; ++i) {
        SDL_AtomicSet(&threads[i].threadData.status, Thread::Status::EXIT);
        SDL_SemPost(threads[i].threadData.sem);
      }
      for (int i = 0; i < threadCreated; ++i) {
        SDL_WaitThread(threads[i].thread, nullptr);
      }
      delete[] threads;
      threads = nullptr;
      threadCreated = 0;
      threadNum = 0;
    }
]=])
    string(FIND "${parallel_source}" "${parallel_destructor_original}"
           parallel_destructor_position)
    if(parallel_destructor_position EQUAL -1)
        message(FATAL_ERROR
            "OnscripterYuri parallel worker shutdown changed; update the embedded-host overlay.")
    endif()
    string(REPLACE "${parallel_destructor_original}"
                   "${parallel_destructor_embedded}"
                   parallel_source "${parallel_source}")

    string(APPEND parallel_source [=[

#include <new>

extern "C" void aetherkiri_onscripter_shutdown_parallel()
{
    // A game can return to AetherKiri and another ONS session can start in
    // the same process. Stop and join every worker before SDL shuts down,
    // then reconstruct the process-global pool so the next session never
    // dereferences the cleared worker array.
    parallel::threadPool.~ThreadPool();
    new (static_cast<void *>(&parallel::threadPool)) parallel::ThreadPool();
}
]=])
    set(generated_parallel "${generated_dir}/Parallel.cpp")
    file(WRITE "${generated_parallel}" "${parallel_source}")

    set("${out_base_source}" "${generated_base}" PARENT_SCOPE)
    set("${out_sound_source}" "${generated_sound}" PARENT_SCOPE)
    set("${out_command_source}" "${generated_command}" PARENT_SCOPE)
    set("${out_event_source}" "${generated_event}" PARENT_SCOPE)
    set("${out_parser_source}" "${generated_parser}" PARENT_SCOPE)
    set("${out_parallel_source}" "${generated_parallel}" PARENT_SCOPE)
endfunction()
