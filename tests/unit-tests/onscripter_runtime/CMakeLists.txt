cmake_minimum_required(VERSION 3.16)
project(TestOnscripterRuntime LANGUAGES CXX)

add_executable(onscripter-runtime-presentation presentation.cpp)
target_compile_features(onscripter-runtime-presentation PRIVATE cxx_std_17)
target_include_directories(onscripter-runtime-presentation PRIVATE
    ${CMAKE_SOURCE_DIR}/bridge/onscripter_runtime/include
)
target_link_libraries(onscripter-runtime-presentation
    PRIVATE Catch2::Catch2WithMain)

catch_discover_tests(onscripter-runtime-presentation)

add_executable(onscripter-runtime-save-storage
    save_storage.cpp
    ${CMAKE_SOURCE_DIR}/bridge/onscripter_runtime/src/onscripter_save_storage.cpp
)
target_compile_features(onscripter-runtime-save-storage PRIVATE cxx_std_17)
target_include_directories(onscripter-runtime-save-storage PRIVATE
    ${CMAKE_SOURCE_DIR}/bridge/onscripter_runtime/include
)
target_link_libraries(onscripter-runtime-save-storage
    PRIVATE Catch2::Catch2WithMain)

catch_discover_tests(onscripter-runtime-save-storage)

add_executable(onscripter-runtime-provider provider.cpp)
target_compile_features(onscripter-runtime-provider PRIVATE cxx_std_17)
target_include_directories(onscripter-runtime-provider PRIVATE
    ${CMAKE_SOURCE_DIR}/bridge/engine_api/include
    ${CMAKE_SOURCE_DIR}/bridge/onscripter_runtime/include
)
target_link_libraries(onscripter-runtime-provider PRIVATE
    Catch2::Catch2WithMain
    aether_onscripter_runtime
    engine_api
)

catch_discover_tests(onscripter-runtime-provider)
