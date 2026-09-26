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
# The xp3 archive tools live in the krkr2 engine repository (issue #241
# Phase 2); this checkout owns their wiring and the binary output paths.
# Out-of-tree sources need an explicit binary directory; keep it mirroring
# the engine submodule location.
add_subdirectory("${AETHERKIRI_KRKR2_DIR}/tools/xp3"
    "${CMAKE_BINARY_DIR}/packages/krkr2/tools/xp3")
add_subdirectory("${AETHERKIRI_KRKR2_DIR}/tools/xp3_select"
    "${CMAKE_BINARY_DIR}/packages/krkr2/tools/xp3_select")

if(AETHERKIRI_INTERNAL_ENABLED AND
   COMMAND aetherinternal_add_catsystem2_probe)
    aetherinternal_add_catsystem2_probe()
endif()
