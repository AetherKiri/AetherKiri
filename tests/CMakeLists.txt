cmake_minimum_required(VERSION 3.16)
project(Tests LANGUAGES CXX)

find_package(Catch2 3 REQUIRED)

include(CTest)
include(Catch)

# Keep the source-level integration contract checked alongside the native
# registry tests.  This is intentionally a small Python check: it validates
# that every declared adapter/source path exists and that the checkout is still
# pinned to the revision recorded in the manifest.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME plugin_manifest_contract
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/plugin_manifest_report.py --strict)
    set_tests_properties(plugin_manifest_contract PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

if(AETHER_BUILD_KRKRZ_CORE_PARITY)
    include("${CMAKE_SOURCE_DIR}/cmake/AetherKrkrzCoreParity.cmake")
    aether_krkrz_add_core_parity_targets()
endif()

set(TEST_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(TEST_FILES_PATH ${CMAKE_CURRENT_SOURCE_DIR}/test_files)
configure_file(test_config.h.in test_config.h)

add_subdirectory(unit-tests/plugins)
add_subdirectory(unit-tests/tjs2)
add_subdirectory(unit-tests/engine_api)
if(AETHERKIRI_ENABLE_ONSCRIPTER)
    add_subdirectory(unit-tests/onscripter_runtime)
endif()
