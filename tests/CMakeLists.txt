cmake_minimum_required(VERSION 3.16)
project(Tests LANGUAGES CXX)

find_package(Catch2 3 REQUIRED)

include(CTest)
include(Catch)

set(TEST_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(TEST_FILES_PATH ${CMAKE_CURRENT_SOURCE_DIR}/test_files)
configure_file(test_config.h.in test_config.h)

if(AETHERKIRI_INTERNAL_ENABLED AND
   COMMAND aetherinternal_add_artemis_tests)
    aetherinternal_add_artemis_tests(
        "${CMAKE_SOURCE_DIR}/bridge/krkr2_runtime/src/artemis_lua_symbol_prefix.h")
endif()

# The krkr2 engine test suites live in the engine repository (issue #241
# Phase 2); this checkout owns their wiring. The runtime-neutral transform
# hook TU is compiled directly into those standalone krkr2 tests, so pass
# its path down to them. Out-of-tree sources need an explicit binary
# directory; keep it mirroring the engine submodule location.
set(KRKR2_TEXT_TRANSFORM_SOURCE
    "${CMAKE_SOURCE_DIR}/bridge/engine_api/src/TextTransform.cpp")

add_subdirectory("${AETHERKIRI_KRKR2_DIR}/tests/plugins"
    "${CMAKE_BINARY_DIR}/packages/krkr2/tests/plugins")
add_subdirectory("${AETHERKIRI_KRKR2_DIR}/tests/tjs2"
    "${CMAKE_BINARY_DIR}/packages/krkr2/tests/tjs2")
add_subdirectory(unit-tests/engine_api)
if(TARGET aether_siglus_runtime)
    add_subdirectory(unit-tests/siglus_runtime)
endif()
if(AETHERKIRI_ENABLE_ONSCRIPTER)
    add_subdirectory(unit-tests/onscripter_runtime)
endif()
