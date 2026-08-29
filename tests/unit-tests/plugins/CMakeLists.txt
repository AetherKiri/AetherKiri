cmake_minimum_required(VERSION 3.16)
project(TestPlugins LANGUAGES CXX)

add_executable(motionplayer-dll
    main.cpp
    motionplayer-dll.cpp
    psb-media.cpp
    psb-parser.cpp
    registry.cpp
    kbad-data-pack.cpp
    textrender-timing.cpp
    text-stream.cpp
    xp3-cx-decoder.cpp
    font-compat.cpp
    font-baseline.cpp
    font-variations.cpp
    font-stream.cpp
    font-service.cpp
    font-color.cpp
    unicode-codepoint.cpp
    repl-contract.cpp
    debugger-contract.cpp
    sound-simd.cpp
    sound-loop-manager.cpp
    tlg-simd.cpp
    tlg-loader.cpp
    resample-simd.cpp
    visual-simd-leaves.cpp
    godot-texture.cpp
    godot-runtime-timing.cpp
    krkrgles-aliases.cpp
    kag-load-contract.cpp
    render-simd.cpp
    layerex-save-codecs.cpp
    script-compatibility.cpp
    project-storage.cpp
    resource-bundle.cpp
    resource-objects.cpp
    layer-effects.cpp
    tft-save.cpp
    gamepad.cpp
)

if(TARGET aether_krkrz_richtext_lib)
    target_sources(motionplayer-dll PRIVATE richtext-contract.cpp)
    target_include_directories(motionplayer-dll PRIVATE
        "${AETHER_KRKRZ_PLUGIN_ROOT}/krkr_richtext/richtext/include"
        "${AETHER_KRKRZ_PLUGIN_ROOT}/krkr_richtext/richtext/ext/minikin/include"
        "${AETHER_KRKRZ_PLUGIN_ROOT}/krkr_richtext/richtext/ext/minikin/libs")
    target_link_libraries(motionplayer-dll PRIVATE
        aether_krkrz_richtext_lib)
    target_compile_definitions(motionplayer-dll PRIVATE
        AETHERKIRI_RICHTEXT_ENABLED=1)
endif()

if(TARGET aether_krkrz_clipparse)
    target_sources(motionplayer-dll PRIVATE clipfile-contract.cpp)
endif()

set(_KRKR2PLUGIN_LINK_ITEM "$<LINK_LIBRARY:WHOLE_ARCHIVE,krkr2plugin>")

target_link_libraries(motionplayer-dll
    PRIVATE
        Catch2::Catch2
    PUBLIC
        krkr2core
        ${_KRKR2PLUGIN_LINK_ITEM}
)

target_include_directories(motionplayer-dll PRIVATE
    "${TEST_CONFIG_DIR}"
    "${CMAKE_SOURCE_DIR}/cpp/plugins"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/layerExSave"
)

if(TARGET aether_krkrz_clipparse)
    target_include_directories(motionplayer-dll PRIVATE
        "${AETHER_KRKRZ_PLUGIN_ROOT}/clipfile/external/clipparse/clipparse"
        ${AETHER_SQLITE_INCLUDE_DIR})
    target_compile_definitions(motionplayer-dll PRIVATE
        AETHERKIRI_CLIPFILE_ENABLED=1
        SQLITE_ENABLE_DESERIALIZE=1)
endif()

# The plugin keeps SQLite private, but the CLIP contract test calls the public
# serialize/deserialize API directly. Link the same owner into the test so it
# can never accidentally resolve a second system SQLite.
if(TARGET aether_sqlite3)
    target_link_libraries(motionplayer-dll PRIVATE aether_sqlite3)
endif()

if(AETHERKIRI_INTERNAL_ENABLED)
    if(NOT TARGET lz4::lz4)
        include("${CMAKE_SOURCE_DIR}/cmake/AetherKiriDependencyAdapters.cmake")
        aetherkiri_prepare_lz4()
    endif()
    target_link_libraries(motionplayer-dll PRIVATE lz4::lz4)
    target_compile_definitions(motionplayer-dll PRIVATE
        AETHERKIRI_EXPECT_INTERNAL_EMOTE=1)
endif()

if(AETHERKIRI_INTERNAL_ENABLED AND COMMAND aetherinternal_extend_krkr2plugin_tests)
    aetherinternal_extend_krkr2plugin_tests(motionplayer-dll)
endif()

catch_discover_tests(motionplayer-dll)
