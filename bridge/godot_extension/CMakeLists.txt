cmake_minimum_required(VERSION 3.19)

project(aether_kiri_godot LANGUAGES C CXX)

find_package(unofficial-godot-cpp CONFIG REQUIRED)

if(IOS)
    add_library(aether_kiri_godot STATIC src/aether_kiri_godot.cpp)
else()
    add_library(aether_kiri_godot SHARED src/aether_kiri_godot.cpp)
endif()

target_compile_features(aether_kiri_godot PRIVATE cxx_std_17)
target_include_directories(aether_kiri_godot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine_api/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/core/visual/godot
    ${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/core/visual
    ${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/core/tjs2
    ${CMAKE_BINARY_DIR}/cpp/core/tjs2/gen
)

if(TARGET engine_api)
    target_link_libraries(aether_kiri_godot PRIVATE engine_api)
endif()

target_link_libraries(aether_kiri_godot PRIVATE unofficial::godot::cpp)

set_target_properties(aether_kiri_godot PROPERTIES
    OUTPUT_NAME "aether_kiri_godot"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@loader_path"
)
