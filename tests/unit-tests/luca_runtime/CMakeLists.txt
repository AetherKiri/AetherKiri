add_executable(luca-runtime-provider provider.cpp)
target_compile_features(luca-runtime-provider PRIVATE cxx_std_17)
target_include_directories(luca-runtime-provider PRIVATE
    ${CMAKE_SOURCE_DIR}/abi/include
    ${CMAKE_SOURCE_DIR}/bridge/luca_runtime/include
)
target_compile_definitions(luca-runtime-provider PRIVATE
    LUCA_SMOKE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../fixtures/luca_smoke")
target_link_libraries(luca-runtime-provider PRIVATE
    Catch2::Catch2WithMain
    aether_luca_runtime
)

catch_discover_tests(luca-runtime-provider)
