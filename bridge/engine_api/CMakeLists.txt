cmake_minimum_required(VERSION 3.16)

project(engine_api LANGUAGES C CXX)

if(IOS OR WEB OR EMSCRIPTEN)
    add_library(engine_api STATIC
        src/engine_api.cpp
    )
elseif(ANDROID)
    # On Android, include the JNI entry point (JNI_OnLoad, Surface bridge)
    # directly in engine_api.so so that System.loadLibrary("engine_api")
    # triggers JNI_OnLoad and registers JNI native methods.
    add_library(engine_api SHARED
        src/engine_api.cpp
        src/android_jni_bridge.cpp
    )
else()
    add_library(engine_api SHARED
        src/engine_api.cpp
    )
endif()

target_compile_features(engine_api PRIVATE cxx_std_17)

if(IOS OR WEB OR EMSCRIPTEN)
    # iOS static library: ensure symbols are visible when linked into the
    # Godot host export. Web links this static API into the side module.
    target_compile_definitions(engine_api PRIVATE ENGINE_API_EXPORT_SYMBOLS)
else()
    target_compile_definitions(engine_api PRIVATE ENGINE_API_BUILD_SHARED)
endif()
target_include_directories(engine_api
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(TARGET krkr2core AND TARGET krkr2plugin)
    target_compile_definitions(engine_api PRIVATE ENGINE_API_USE_KRKR2_RUNTIME)
    # Keep static NCB registration objects from krkr2plugin alive in the
    # final engine_api link as well. Sub-plugin targets reference krkr2plugin
    # transitively, so force every occurrence of that target to use the same
    # feature instead of mixing DEFAULT and WHOLE_ARCHIVE.
    set(_KRKR2PLUGIN_LINK_ITEM "$<LINK_LIBRARY:WHOLE_ARCHIVE,krkr2plugin>")
    target_link_libraries(engine_api PRIVATE krkr2core ${_KRKR2PLUGIN_LINK_ITEM})
    set_property(TARGET engine_api PROPERTY LINK_LIBRARY_OVERRIDE_krkr2plugin WHOLE_ARCHIVE)
endif()

if(BUILD_GPU_BRIDGE)
    target_compile_definitions(engine_api PRIVATE KRKR_ENABLE_GPU_BRIDGE=1)
endif()

set_target_properties(engine_api PROPERTIES
    OUTPUT_NAME "engine_api"
)

# On Android, link system platform libs.
if(ANDROID)
    target_link_libraries(engine_api PRIVATE log android)
endif()
