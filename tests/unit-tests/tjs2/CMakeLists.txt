cmake_minimum_required(VERSION 3.16)
project(TestTJS2 LANGUAGES CXX)

add_executable(tjs2-missing
    missing.cpp
    variant-array-stack.cpp
    main.cpp
    stubs.cpp
    ${KRKR2CORE_PATH}/plugin/PluginCallTracer.cpp
)

target_link_libraries(tjs2-missing
    PRIVATE
        Catch2::Catch2
        tjs2
)

catch_discover_tests(tjs2-missing)
