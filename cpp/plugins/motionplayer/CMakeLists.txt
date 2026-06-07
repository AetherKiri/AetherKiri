cmake_minimum_required(VERSION 3.19)
project(motionplayer)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE
    main.cpp
    EmotePlayer.cpp
    PlayerCore.cpp
    PlayerResource.cpp
    PlayerRender.cpp
    PlayerUpdateLayers.cpp
    PlayerQuery.cpp
    ResourceManager.cpp
    RuntimeSupport.cpp
    NodeTree.cpp
    MotionNodeBridge.cpp
    SeparateLayerAdaptor.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE krkr2plugin)

if(EMSCRIPTEN)
    target_compile_options(${PROJECT_NAME} PRIVATE -msimd128)
    target_link_options(${PROJECT_NAME} INTERFACE -msimd128)
endif()
