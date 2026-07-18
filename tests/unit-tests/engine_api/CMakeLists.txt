cmake_minimum_required(VERSION 3.16)
project(TestEngineApiDiagnostics LANGUAGES CXX)

add_executable(engine-api-diagnostics
    diagnostics.cpp
    ${CMAKE_SOURCE_DIR}/bridge/engine_api/src/engine_api.cpp
)

target_compile_features(engine-api-diagnostics PRIVATE cxx_std_17)
target_include_directories(engine-api-diagnostics PRIVATE
    ${CMAKE_SOURCE_DIR}/bridge/engine_api/include
)
target_link_libraries(engine-api-diagnostics PRIVATE Catch2::Catch2WithMain)

catch_discover_tests(engine-api-diagnostics)
