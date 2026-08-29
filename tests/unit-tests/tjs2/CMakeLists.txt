cmake_minimum_required(VERSION 3.16)
project(TestTJS2 LANGUAGES CXX)

add_executable(tjs2-missing
    missing.cpp
    krkrz_script.cpp
    variant-array-stack.cpp
    main.cpp
    stubs.cpp
    $<$<BOOL:${AETHERKIRI_DAP_ENABLED}>:${CMAKE_CURRENT_SOURCE_DIR}/dap_host_stubs.cpp>
    $<$<BOOL:${AETHERKIRI_DAP_ENABLED}>:${CMAKE_SOURCE_DIR}/cpp/core/base/CharacterSet.cpp>
    ${KRKR2CORE_PATH}/plugin/PluginCallTracer.cpp
)

target_link_libraries(tjs2-missing
    PRIVATE
        Catch2::Catch2
        tjs2
)

# The upstream TJS2 corpus stays in its recursive krkrz_dev script submodule.
# Pass its absolute source path to the harness instead of copying scripts into
# the parent repository.
set(_AETHER_KRKRZ_SCRIPT_TEST_ROOT
    "${CMAKE_SOURCE_DIR}/third_party/krkrz_dev/script/test")
set(_AETHER_KRKRZ_TJS2_TEST_ROOT
    "${_AETHER_KRKRZ_SCRIPT_TEST_ROOT}/tjs2")
set(_AETHER_KRKRZ_TJS_TEST_FILES
    "${_AETHER_KRKRZ_TJS2_TEST_ROOT}/test.tjs"
    "${_AETHER_KRKRZ_SCRIPT_TEST_ROOT}/issue226/TestClass.tjs"
    "${_AETHER_KRKRZ_SCRIPT_TEST_ROOT}/issue226/TestForSyntax.tjs"
    "${_AETHER_KRKRZ_SCRIPT_TEST_ROOT}/issue226/startup.tjs"
    "${_AETHER_KRKRZ_SCRIPT_TEST_ROOT}/in_operator/test_in.tjs")
foreach(_aether_krkrz_tjs_file IN LISTS _AETHER_KRKRZ_TJS_TEST_FILES)
    if(NOT EXISTS "${_aether_krkrz_tjs_file}")
        message(FATAL_ERROR
            "krkrz_dev TJS test corpus is missing at ${_aether_krkrz_tjs_file}. "
            "Run: git submodule update --init --recursive third_party/krkrz_dev")
    endif()
endforeach()
target_compile_definitions(tjs2-missing PRIVATE
    AETHER_KRKRZ_TJS2_TEST_ROOT="${_AETHER_KRKRZ_TJS2_TEST_ROOT}")

if(AETHERKIRI_DAP_ENABLED)
    target_include_directories(tjs2-missing PRIVATE
        "${CMAKE_SOURCE_DIR}/cpp/core/base"
        "${CMAKE_SOURCE_DIR}/cpp/core/base/impl"
        "${CMAKE_SOURCE_DIR}/cpp/core/utils"
        "${CMAKE_SOURCE_DIR}/cpp/core/utils/win32")
endif()

catch_discover_tests(tjs2-missing)
