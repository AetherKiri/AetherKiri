cmake_minimum_required(VERSION 3.16)

project(engine_api LANGUAGES C CXX)

if(IOS)
    add_library(engine_api STATIC
        src/engine_api.cpp
    )
elseif(ANDROID)
    # On Android, include the JNI entry point (JNI_OnLoad, Surface bridge)
    # directly in engine_api.so so that System.loadLibrary("engine_api")
    # triggers JNI_OnLoad and registers JNI native methods.
    add_library(engine_api SHARED
        src/engine_api.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../platforms/android/cpp/krkr2_android.cpp
    )
else()
    add_library(engine_api SHARED
        src/engine_api.cpp
    )
endif()

target_compile_features(engine_api PRIVATE cxx_std_17)

if(IOS)
    # iOS static library: ensure symbols are visible when linked into the
    # Godot host export.
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
    # final engine_api link as well.
    set(_KRKR2PLUGIN_LINK_ITEM "$<LINK_LIBRARY:WHOLE_ARCHIVE,krkr2plugin>")
    target_link_libraries(engine_api PRIVATE krkr2core ${_KRKR2PLUGIN_LINK_ITEM})
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
