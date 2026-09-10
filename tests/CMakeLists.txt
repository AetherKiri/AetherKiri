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
        "${CMAKE_SOURCE_DIR}/bridge/engine_api/src/artemis_lua_symbol_prefix.h")
endif()

add_subdirectory(unit-tests/plugins)
add_subdirectory(unit-tests/tjs2)
add_subdirectory(unit-tests/engine_api)
if(AETHERKIRI_ENABLE_ONSCRIPTER)
    add_subdirectory(unit-tests/onscripter_runtime)
endif()
