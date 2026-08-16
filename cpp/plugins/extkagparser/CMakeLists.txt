cmake_minimum_required(VERSION 3.19)
project(extkagparser)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE
    ExtKAG.cpp
    ExtKAGParser.cpp
    TJSAry.cpp
    TJSDic.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE krkr2plugin)
