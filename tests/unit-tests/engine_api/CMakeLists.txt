cmake_minimum_required(VERSION 3.16)
project(TestEngineApiDiagnostics LANGUAGES CXX)

add_executable(engine-api-diagnostics
    diagnostics.cpp
    ${CMAKE_SOURCE_DIR}/bridge/engine_api/src/engine_api.cpp
    ${CMAKE_SOURCE_DIR}/bridge/engine_api/src/engine_api_dispatch.cpp
    ${CMAKE_SOURCE_DIR}/bridge/engine_api/src/engine_runtime_provider.cpp
)

target_compile_features(engine-api-diagnostics PRIVATE cxx_std_17)
target_include_directories(engine-api-diagnostics PRIVATE
    ${CMAKE_SOURCE_DIR}/bridge/engine_api/include
    ${CMAKE_SOURCE_DIR}/bridge/engine_api/src
)
target_link_libraries(engine-api-diagnostics PRIVATE
    Catch2::Catch2WithMain
)

if(AETHERKIRI_INTERNAL_ENABLED AND
   COMMAND aetherinternal_extend_catsystem2_tests)
    aetherinternal_extend_catsystem2_tests(engine-api-diagnostics)
endif()

catch_discover_tests(engine-api-diagnostics)
