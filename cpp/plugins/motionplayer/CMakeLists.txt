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
    PsbValueReader.cpp
    MotionPlayerExtension.cpp
    NodeTree.cpp
    MotionNodeBridge.cpp
    SeparateLayerAdaptor.cpp
)

if(AETHERKIRI_INTERNAL_ENABLED)
    # The public implementation remains part of every build. The private
    # package adds only its E-mote controller extension source and registers
    # it through the public hook; it never replaces this source list.
    aetherinternal_extend_motionplayer(${PROJECT_NAME})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE krkr2plugin)

if(EMSCRIPTEN)
    target_compile_options(${PROJECT_NAME} PRIVATE -msimd128)
    target_link_options(${PROJECT_NAME} INTERFACE -msimd128)
endif()
