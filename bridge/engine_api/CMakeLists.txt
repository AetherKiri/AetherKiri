cmake_minimum_required(VERSION 3.16)

project(engine_api LANGUAGES C CXX)

set(ENGINE_API_COMMON_SOURCES
    src/engine_api_dispatch.cpp
    src/engine_runtime_provider.cpp
)

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
        # Runtime-neutral text transform hook shared with the engine sources.
        # The symbol implementation itself is provided by the linked runtime
        # (core_utils_module through aether_krkr2_runtime, or the standalone
        # compile used by the unit tests).
        ${CMAKE_SOURCE_DIR}/cpp/core/utils
)

if(TARGET aether_krkr2_runtime)
    # The legacy KiriKiri implementation (engine_legacy_* symbols, krkr2core
    # and krkr2plugin linkage, private runtime extensions) lives in the
    # runtime glue and is folded into this library at the final link.
    target_link_libraries(engine_api PRIVATE aether_krkr2_runtime)
else()
    # No KiriKiri runtime is available: compile the standalone stub
    # implementation of the engine_legacy_* surface instead.
    target_sources(engine_api PRIVATE src/engine_api_stub.cpp)
endif()

if(AETHERKIRI_INTERNAL_ENABLED AND COMMAND aetherinternal_extend_text_translation)
    aetherinternal_extend_text_translation(engine_api)
endif()

# The dispatch layer declares the private runtime registration entry points
# (AetherInternalRegister*Runtime) and their provider sources live in the
# runtime glue. Mirror the glue-side build gates so the extern declarations
# always match the symbols that will be present at link time.
if(AETHERKIRI_INTERNAL_ENABLED)
    if(COMMAND aetherinternal_extend_catsystem2)
        target_compile_definitions(engine_api PRIVATE
            AETHERKIRI_INTERNAL_CATSYSTEM2=1)
    endif()
    if(COMMAND aetherinternal_extend_artemis)
        target_compile_definitions(engine_api PRIVATE
            AETHERKIRI_INTERNAL_ARTEMIS=1)
    endif()
    if(COMMAND aetherinternal_extend_wa2)
        target_compile_definitions(engine_api PRIVATE
            AETHERKIRI_INTERNAL_WA2=1
            AETHERKIRI_ENABLE_WA2_RUNTIME=1)
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
