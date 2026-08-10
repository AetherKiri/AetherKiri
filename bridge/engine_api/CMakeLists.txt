cmake_minimum_required(VERSION 3.16)

project(engine_api LANGUAGES C CXX)

set(ENGINE_API_COMMON_SOURCES
    src/engine_api.cpp
    src/engine_api_dispatch.cpp
    src/engine_runtime_provider.cpp
)

if(IOS OR WEB OR EMSCRIPTEN)
    add_library(engine_api STATIC ${ENGINE_API_COMMON_SOURCES})
elseif(ANDROID)
    # On Android, include the JNI entry point (JNI_OnLoad, Surface bridge)
    # directly in engine_api.so so that System.loadLibrary("engine_api")
    # triggers JNI_OnLoad and registers JNI native methods.
    add_library(engine_api SHARED
        ${ENGINE_API_COMMON_SOURCES}
        src/android_jni_bridge.cpp
    )
else()
    add_library(engine_api SHARED ${ENGINE_API_COMMON_SOURCES})
endif()

target_compile_features(engine_api PRIVATE cxx_std_17)

if(WEB OR EMSCRIPTEN)
    # Web links this static API into the side module and exports its C ABI.
    target_compile_definitions(engine_api PRIVATE ENGINE_API_EXPORT_SYMBOLS)
elseif(IOS)
    # The iOS static API is consumed only by the statically linked Godot host.
    # Keep it out of the final Mach-O dynamic export surface.
    set_target_properties(engine_api PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
else()
    target_compile_definitions(engine_api PRIVATE ENGINE_API_BUILD_SHARED)
endif()
target_include_directories(engine_api
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(TARGET krkr2core AND TARGET krkr2plugin)
    target_compile_definitions(engine_api PRIVATE ENGINE_API_USE_KRKR2_RUNTIME)
    if(WEB OR EMSCRIPTEN)
        # The Web side module force-loads krkr2plugin once at the final link.
        # Keep this dependency normal here so CMake can repeat cyclic static
        # libs without whole-archive duplicate symbols.
        target_link_libraries(engine_api PRIVATE krkr2core krkr2plugin)
    else()
        # Keep static NCB registration objects from krkr2plugin alive in the
        # final native engine_api link as well.
        set(_KRKR2PLUGIN_LINK_ITEM "$<LINK_LIBRARY:WHOLE_ARCHIVE,krkr2plugin>")
        target_link_libraries(engine_api PRIVATE krkr2core ${_KRKR2PLUGIN_LINK_ITEM})

        if(LINUX)
            # The runtime modules have circular static-library dependencies.
            # Re-scan them after all consumers have been seen so platform
            # implementations from core_environ_module are retained.
            target_link_libraries(engine_api PRIVATE
                "$<LINK_GROUP:RESCAN,core_base_module,core_environ_module,core_extension_module,core_plugin_module,core_movie_module,core_sound_module,core_visual_module,core_utils_module,tvpgl_simd,tjs2>"
            )
        endif()
        set_property(TARGET engine_api PROPERTY LINK_LIBRARY_OVERRIDE_krkr2plugin WHOLE_ARCHIVE)
    endif()
endif()

if(BUILD_GPU_BRIDGE)
    target_compile_definitions(engine_api PRIVATE KRKR_ENABLE_GPU_BRIDGE=1)
endif()

if(AETHERKIRI_INTERNAL_ENABLED AND COMMAND aetherinternal_extend_artemis)
    aetherinternal_extend_artemis(engine_api)
endif()

if(TARGET aetherinternal_artemis_lua)
    set(AETHERKIRI_ARTEMIS_LUA_PREFIX_HEADER
        "${CMAKE_CURRENT_SOURCE_DIR}/src/artemis_lua_symbol_prefix.h")
    if(MSVC)
        target_compile_options(aetherinternal_artemis_lua PRIVATE
            /FI"${AETHERKIRI_ARTEMIS_LUA_PREFIX_HEADER}")
        target_compile_options(engine_api PRIVATE
            /FI"${AETHERKIRI_ARTEMIS_LUA_PREFIX_HEADER}")
    else()
        target_compile_options(aetherinternal_artemis_lua PRIVATE
            -include "${AETHERKIRI_ARTEMIS_LUA_PREFIX_HEADER}")
        target_compile_options(engine_api PRIVATE
            -include "${AETHERKIRI_ARTEMIS_LUA_PREFIX_HEADER}")
    endif()
endif()

set_target_properties(engine_api PROPERTIES
    OUTPUT_NAME "engine_api"
)

aetherkiri_limit_runtime_exports(engine_api ENGINE_API)

if(LINUX)
    # Linux exports bundle the engine and its vcpkg runtime libraries beside
    # the GDExtension. Keep the loader path relative to that bundle.
    set_target_properties(engine_api PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# On Android, link system platform libs.
if(ANDROID)
    target_link_libraries(engine_api PRIVATE log android dl)
endif()
