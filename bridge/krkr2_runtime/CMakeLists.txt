cmake_minimum_required(VERSION 3.19)

project(aether_krkr2_runtime LANGUAGES C CXX)

# The KiriKiri (krkr2) engine runtime glue: the legacy engine_api
# implementation plus host-side texture recycle/audio services. engine_api
# keeps the provider registry and dispatch layer only and consumes this
# library through the installable legacy services table
# (aether_krkr2_install_legacy_services); the Godot extension host links the
# glue so engine_api itself stays free of KiriKiri symbols (Phase 2d).
if(NOT TARGET krkr2core OR NOT TARGET krkr2plugin)
    message(FATAL_ERROR
        "aether_krkr2_runtime requires the krkr2core and krkr2plugin targets.")
endif()

set(KRKR2_RUNTIME_SOURCES
    src/krkr2_legacy_engine_api.cpp
    src/krkr2_legacy_services.cpp
    src/krkr2_host_hooks.cpp
)

add_library(aether_krkr2_runtime STATIC ${KRKR2_RUNTIME_SOURCES})

target_compile_features(aether_krkr2_runtime PRIVATE cxx_std_17)
target_include_directories(aether_krkr2_runtime
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/abi/include"
        "${CMAKE_SOURCE_DIR}/abi/src"
)

# Mirror the engine_api export-macro compilation context so the moved
# translation units produce the same symbol visibility as before the split.
if(WEB OR EMSCRIPTEN)
    target_compile_definitions(aether_krkr2_runtime PRIVATE
        ENGINE_API_EXPORT_SYMBOLS)
elseif(IOS)
    # The iOS static objects are consumed only by the statically linked Godot
    # host. Keep them out of the final Mach-O dynamic export surface.
    set_target_properties(aether_krkr2_runtime PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
else()
    target_compile_definitions(aether_krkr2_runtime PRIVATE
        ENGINE_API_BUILD_SHARED)
endif()

# krkr2 link block (moved verbatim from the engine_api library, now abi/).
if(WEB OR EMSCRIPTEN)
    # The Web side module force-loads krkr2plugin once at the final link.
    # Keep this dependency normal here so CMake can repeat cyclic static
    # libs without whole-archive duplicate symbols.
    target_link_libraries(aether_krkr2_runtime PRIVATE krkr2core krkr2plugin)
else()
    # Keep static NCB registration objects from krkr2plugin alive in the
    # final native engine_api link as well.
    set(_KRKR2PLUGIN_LINK_ITEM "$<LINK_LIBRARY:WHOLE_ARCHIVE,krkr2plugin>")
    target_link_libraries(aether_krkr2_runtime PRIVATE
        krkr2core ${_KRKR2PLUGIN_LINK_ITEM})

    if(LINUX)
        # The runtime modules have circular static-library dependencies.
        # Re-scan them after all consumers have been seen so platform
        # implementations from core_environ_module are retained.
        target_link_libraries(aether_krkr2_runtime PRIVATE
            "$<LINK_GROUP:RESCAN,core_base_module,core_environ_module,core_extension_module,core_plugin_module,core_movie_module,core_sound_module,core_visual_module,core_utils_module,tvpgl_simd,tjs2>"
        )
    endif()
    set_property(TARGET aether_krkr2_runtime
        PROPERTY LINK_LIBRARY_OVERRIDE_krkr2plugin WHOLE_ARCHIVE)
endif()

if(BUILD_GPU_BRIDGE)
    target_compile_definitions(aether_krkr2_runtime PRIVATE
        KRKR_ENABLE_GPU_BRIDGE=1)
endif()

# Private runtime extensions contribute sources that compile against
# krkr2core/krkr2plugin, so they extend the runtime glue instead of the
# engine_api dispatch target. Mirror the build gates that used to live in
# engine_api so krkr2_legacy_services.cpp declares exactly the registration
# symbols the extension objects provide.
if(AETHERKIRI_INTERNAL_ENABLED)
    if(COMMAND aetherinternal_extend_catsystem2)
        aetherinternal_extend_catsystem2(aether_krkr2_runtime)
        target_compile_definitions(aether_krkr2_runtime PRIVATE
            AETHERKIRI_INTERNAL_CATSYSTEM2=1)
    endif()

    if(COMMAND aetherinternal_extend_artemis)
        aetherinternal_extend_artemis(aether_krkr2_runtime)
        target_compile_definitions(aether_krkr2_runtime PRIVATE
            AETHERKIRI_INTERNAL_ARTEMIS=1)
    endif()

    if(COMMAND aetherinternal_extend_wa2)
        aetherinternal_extend_wa2(aether_krkr2_runtime)
        target_compile_definitions(aether_krkr2_runtime PRIVATE
            AETHERKIRI_INTERNAL_WA2=1
            AETHERKIRI_ENABLE_WA2_RUNTIME=1)
    endif()
endif()

# The AetherInternal extension functions above run find_package(OpenAL) in
# this scope, creating a scope-local OpenAL::OpenAL copy. Sanitize its raw
# -Wl,-framework items: this glue is linked by the mixed C++/Swift Godot
# extension whose final link is driven by swiftc (rejects the -Wl spelling).
aetherkiri_sanitize_openal_link()

if(APPLE)
    # The FFMPEG dependency lists appended by the extension functions carry a
    # bare "-pthread" driver flag; macOS pthreads live in libSystem and the
    # swiftc-driven extension link rejects the flag. Drop it from the glue's
    # static-consumer interface.
    get_target_property(_krkr2_runtime_interface aether_krkr2_runtime
        INTERFACE_LINK_LIBRARIES)
    if(_krkr2_runtime_interface)
        list(REMOVE_ITEM _krkr2_runtime_interface "$<LINK_ONLY:-pthread>")
        set_target_properties(aether_krkr2_runtime PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_krkr2_runtime_interface}")
    endif()
endif()

if(TARGET aetherinternal_artemis_lua)
    set(AETHERKIRI_ARTEMIS_LUA_PREFIX_HEADER
        "${CMAKE_CURRENT_SOURCE_DIR}/src/artemis_lua_symbol_prefix.h")
    if(MSVC)
        target_compile_options(aetherinternal_artemis_lua PRIVATE
            /FI"${AETHERKIRI_ARTEMIS_LUA_PREFIX_HEADER}")
        target_compile_options(aether_krkr2_runtime PRIVATE
            /FI"${AETHERKIRI_ARTEMIS_LUA_PREFIX_HEADER}")
    else()
        target_compile_options(aetherinternal_artemis_lua PRIVATE
            -include "${AETHERKIRI_ARTEMIS_LUA_PREFIX_HEADER}")
        target_compile_options(aether_krkr2_runtime PRIVATE
            -include "${AETHERKIRI_ARTEMIS_LUA_PREFIX_HEADER}")
    endif()
endif()

# On Android, link system platform libraries (JNI bridge, logging, dlopen).
if(ANDROID)
    target_link_libraries(aether_krkr2_runtime PRIVATE log android dl)
endif()
