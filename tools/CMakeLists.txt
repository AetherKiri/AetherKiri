cmake_minimum_required(VERSION 3.28)
project(Tools LANGUAGES CXX)

if (WIN32)
    set(OS_PLATFORM win)
elseif (APPLE)
    set(OS_PLATFORM mac)
elseif (UNIX)
    set(OS_PLATFORM linux)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin/${OS_PLATFORM}/rel")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin/${OS_PLATFORM}/dbg")

# The xp3 tool resolves TVPTransformText/TVPPrefetchText from the
# runtime-neutral transform hook TU owned by this checkout's engine_api
# (Phase 2d); pass its path down to the tool, mirroring the standalone
# krkr2 test suites in tests/CMakeLists.txt.
set(KRKR2_TEXT_TRANSFORM_SOURCE
    "${CMAKE_SOURCE_DIR}/bridge/engine_api/src/TextTransform.cpp")

# The xp3 archive tools live in the krkr2 engine repository (issue #241
# Phase 2); this checkout owns their wiring and the binary output paths.
# Out-of-tree sources need an explicit binary directory; keep it mirroring
# the engine submodule location.
add_subdirectory("${AETHERKIRI_KRKR_DIR}/tools/xp3"
    "${CMAKE_BINARY_DIR}/packages/AetherKrkr/tools/xp3")
add_subdirectory("${AETHERKIRI_KRKR_DIR}/tools/xp3_select"
    "${CMAKE_BINARY_DIR}/packages/AetherKrkr/tools/xp3_select")

if(AETHERKIRI_INTERNAL_ENABLED AND TARGET engine_api AND
   COMMAND aetherinternal_add_catsystem2_probe)
    aetherinternal_add_catsystem2_probe()
endif()
