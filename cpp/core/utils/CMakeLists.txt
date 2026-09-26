cmake_minimum_required(VERSION 3.19)
project(core_utils_module LANGUAGES CXX)

set(UTILS_PATH ${CMAKE_CURRENT_LIST_DIR})
set(UTILS_SOURCE_FILES
    ${UTILS_PATH}/ClipboardIntf.cpp
    ${UTILS_PATH}/DebugIntf.cpp
    ${UTILS_PATH}/MathAlgorithms_Default.cpp
    ${UTILS_PATH}/md5.c
    ${UTILS_PATH}/MiscUtility.cpp
    ${UTILS_PATH}/PadIntf.cpp
    ${UTILS_PATH}/Random.cpp
    ${UTILS_PATH}/RealFFT_Default.cpp
    ${UTILS_PATH}/ThreadIntf.cpp
    ${UTILS_PATH}/TickCount.cpp
    ${UTILS_PATH}/TimerIntf.cpp
    ${UTILS_PATH}/VelocityTracker.cpp

    ${UTILS_PATH}/encoding/gbk2unicode.c
    ${UTILS_PATH}/encoding/jis2unicode.c

    ${UTILS_PATH}/win32/ClipboardImpl.cpp
    ${UTILS_PATH}/win32/DebugImpl.cpp
    ${UTILS_PATH}/win32/PadImpl.cpp
    ${UTILS_PATH}/win32/ThreadImpl.cpp
    ${UTILS_PATH}/win32/TimerImpl.cpp
    ${UTILS_PATH}/win32/TVPTimer.cpp
)

set(UTILS_HEADERS_DIR
    ${UTILS_PATH}/
    ${UTILS_PATH}/iconv/
    ${UTILS_PATH}/win32/
)

add_library(${PROJECT_NAME} STATIC ${UTILS_SOURCE_FILES})
# TextTransform moved to bridge/engine_api (Phase 2d): the transform hook is
# runtime-neutral ABI state owned by the engine_api library, and krkr2 engine
# sources (KAGParser, textrender, ...) resolve TVPTransformText against it
# across the DSO boundary. Keep exposing its header path through this module
# so core consumers keep compiling unchanged.
target_include_directories(${PROJECT_NAME} PUBLIC
    ${UTILS_HEADERS_DIR}
    ${CMAKE_SOURCE_DIR}/bridge/engine_api/include
)
target_link_libraries(${PROJECT_NAME} PUBLIC tjs2 PRIVATE core_base_module core_environ_module)
