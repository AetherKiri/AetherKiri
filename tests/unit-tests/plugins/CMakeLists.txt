cmake_minimum_required(VERSION 3.16)
project(TestPlugins LANGUAGES CXX)

add_executable(motionplayer-dll
    main.cpp
    motionplayer-dll.cpp
    psb-media.cpp
    registry.cpp
    kbad-data-pack.cpp
    textrender-timing.cpp
    text-stream.cpp
    xp3-cx-decoder.cpp
    font-compat.cpp
    font-baseline.cpp
    godot-texture.cpp
    godot-runtime-timing.cpp
    krkrgles-aliases.cpp
    kag-load-contract.cpp
    render-simd.cpp
    script-compatibility.cpp
    project-storage.cpp
)

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
)

if(AETHERKIRI_INTERNAL_ENABLED)
    find_package(lz4 CONFIG REQUIRED)
    target_link_libraries(motionplayer-dll PRIVATE lz4::lz4)
    target_compile_definitions(motionplayer-dll PRIVATE
        AETHERKIRI_EXPECT_INTERNAL_EMOTE=1)
endif()

if(AETHERKIRI_INTERNAL_ENABLED AND COMMAND aetherinternal_extend_krkr2plugin_tests)
    aetherinternal_extend_krkr2plugin_tests(motionplayer-dll)
endif()

catch_discover_tests(motionplayer-dll)
