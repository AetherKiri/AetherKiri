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
