cmake_minimum_required(VERSION 3.19)
project(core_plugin_module LANGUAGES CXX)

include("${CMAKE_SOURCE_DIR}/cmake/AetherKrkrzIntegration.cmake")

set(AETHER_PLUGIN_INTF_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/PluginIntf.cpp")
aether_krkrz_track_core_source(
    "${AETHER_PLUGIN_INTF_SOURCE}"
    "common/base/PluginIntf.cpp")

set(PLUGIN_PATH ${CMAKE_CURRENT_LIST_DIR})
set(PLUGIN_SOURCE_FILES
        ${PLUGIN_PATH}/ncbind.cpp
        ${AETHER_PLUGIN_INTF_SOURCE}
        ${PLUGIN_PATH}/PluginImpl.cpp
        ${PLUGIN_PATH}/PortableGamepad.cpp
        ${PLUGIN_PATH}/PluginCallTracer.cpp
)

set(PLUGIN_HEADERS_DIR
        ${PLUGIN_PATH}/
)

add_library(${PROJECT_NAME} STATIC ${PLUGIN_SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ${PLUGIN_HEADERS_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC tjs2 PRIVATE core_base_module core_environ_module core_visual_module core_utils_module)

# SDL2 is already the portable event/input owner of the Aether host. Reuse
# that exact target for the gamepad compatibility adapter; on a minimal
# platform without SDL2 the source compiles to an honest zero-device fallback.
find_package(SDL2 CONFIG QUIET)
if(TARGET SDL2::SDL2)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)
    target_compile_definitions(${PROJECT_NAME} PRIVATE AETHERKIRI_HAS_SDL2=1)
elseif(TARGET SDL2::SDL2-static)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2-static)
    target_compile_definitions(${PROJECT_NAME} PRIVATE AETHERKIRI_HAS_SDL2=1)
endif()
