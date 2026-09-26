cmake_minimum_required(VERSION 3.16)

project(engine_api LANGUAGES C CXX)

set(ENGINE_API_COMMON_SOURCES
    src/engine_api_dispatch.cpp
    src/engine_runtime_provider.cpp
    src/TextTransform.cpp
)

if(ANDROID)
    # The Godot Android host loads engine_api.so directly through the JNI
    # bridge: JNI_OnLoad and the Java_org_github_krkr2_aetherkiri native
    # methods stay in this library even though the KiriKiri runtime itself is
    # linked on the Godot extension side (Phase 2d link flip). The bridge is
    # host-only and carries no krkr2core dependency.
    list(APPEND ENGINE_API_COMMON_SOURCES src/android_jni_bridge.cpp)
endif()

if(IOS OR WEB OR EMSCRIPTEN)
    add_library(engine_api STATIC ${ENGINE_API_COMMON_SOURCES})
else()
    # On Android, the JNI entry point (JNI_OnLoad, Surface bridge) lives in
    # the KiriKiri runtime glue archive and lands in engine_api.so through
    # the aether_krkr2_runtime link, so System.loadLibrary("engine_api")
    # still triggers JNI_OnLoad and registers JNI native methods.
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

if(AETHERKIRI_INTERNAL_ENABLED AND COMMAND aetherinternal_extend_text_translation)
    aetherinternal_extend_text_translation(engine_api)
    # LocalTextTranslation logs through spdlog/fmt; the KiriKiri glue link
    # that used to satisfy those symbols is gone (Phase 2d), so link them
    # into engine_api explicitly.
    find_package(spdlog CONFIG REQUIRED)
    target_link_libraries(engine_api PRIVATE spdlog::spdlog)
endif()

# The KiriKiri runtime glue (aether_krkr2_runtime) is linked by the Godot
# extension host, which installs the legacy services table through
# engine_install_legacy_services() before the first engine_create call.
# engine_api itself stays free of any KiriKiri linkage; hosts without the
# glue simply get ENGINE_RESULT_NOT_SUPPORTED from the built-in "kirikiri"
# backend.

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
