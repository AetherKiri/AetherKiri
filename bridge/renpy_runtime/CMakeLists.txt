cmake_minimum_required(VERSION 3.19)
project(aether_renpy_runtime LANGUAGES CXX)

if(IOS OR ANDROID)
    # Mobile builds link the engine in-process.  The desktop subprocess
    # provider cannot be compiled here: it assumes a writable game/libs
    # directory, fork/exec (or CreateProcess), and a desktop Ren'Py launcher.
    # Keep a provider registration stub so selecting runtime=renpy fails with
    # an explicit NOT_SUPPORTED result until the native RAPT/Renios bootstrap
    # and lifecycle/rendering adapters are implemented.
    set(AETHERKIRI_RENPY_SOURCES
        src/renpy_runtime_mobile_provider.cpp
        src/renpy_runtime_ios_adapter.cpp
        mobile_launcher/src/renpy_mobile_loader.cpp)
    if(ANDROID)
        list(APPEND AETHERKIRI_RENPY_SOURCES src/renpy_mobile_adapter.cpp)
    endif()
else()
    if(NOT (WIN32 OR APPLE OR LINUX))
        message(FATAL_ERROR
            "The embedded Ren'Py SDK provider requires a desktop subprocess host")
    endif()
    if(WEB OR EMSCRIPTEN)
        message(FATAL_ERROR
            "The embedded Ren'Py SDK provider is not available on this platform")
    endif()
    if(NOT AETHERKIRI_RENPY_SDK_ROOT OR
       NOT IS_DIRECTORY "${AETHERKIRI_RENPY_SDK_ROOT}")
        message(FATAL_ERROR
            "AETHERKIRI_ENABLE_RENPY requires AETHERKIRI_RENPY_SDK_ROOT to point "
            "to an official Ren'Py SDK directory")
    endif()
    set(AETHERKIRI_RENPY_SOURCES src/renpy_runtime_provider.cpp)
endif()

add_library(aether_renpy_runtime STATIC ${AETHERKIRI_RENPY_SOURCES})
target_compile_features(aether_renpy_runtime PUBLIC cxx_std_17)
set_target_properties(aether_renpy_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(IOS)
    target_compile_definitions(aether_renpy_runtime PRIVATE
        AETHERKIRI_RENPY_MOBILE_STUB=1
        AETHERKIRI_RENPY_IOS=1)
elseif(ANDROID)
    target_compile_definitions(aether_renpy_runtime PRIVATE
        AETHERKIRI_RENPY_MOBILE_STUB=1)
else()
    target_compile_definitions(aether_renpy_runtime PRIVATE
        AETHERKIRI_RENPY_SDK_ROOT="${AETHERKIRI_RENPY_SDK_ROOT}"
        AETHERKIRI_RENPY_OVERLAY_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/python/aether_renpy_overlay.py")
endif()
target_include_directories(aether_renpy_runtime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/mobile_launcher/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../abi/include")
if(NOT TARGET engine_api)
    message(FATAL_ERROR "Ren'Py provider requires the engine_api target")
endif()
target_link_libraries(aether_renpy_runtime PRIVATE engine_api)
if(UNIX AND NOT (IOS OR ANDROID))
    target_link_libraries(aether_renpy_runtime PRIVATE pthread)
endif()

if(ENABLE_TESTS AND NOT (IOS OR ANDROID))
    enable_testing()
    add_executable(renpy-provider-test tests/provider.cpp)
    target_compile_definitions(renpy-provider-test PRIVATE
        AETHERKIRI_RENPY_TEST_SDK_ROOT="${AETHERKIRI_RENPY_SDK_ROOT}")
    target_link_libraries(renpy-provider-test PRIVATE aether_renpy_runtime)
    add_test(NAME renpy-provider COMMAND renpy-provider-test)

    add_executable(renpy-input-mapping-test tests/input_mapping.cpp)
    target_link_libraries(renpy-input-mapping-test PRIVATE aether_renpy_runtime)
    add_test(NAME renpy-input-mapping COMMAND renpy-input-mapping-test)

    add_executable(renpy-mobile-adapter-test
        tests/mobile_adapter.cpp src/renpy_mobile_adapter.cpp)
    target_include_directories(renpy-mobile-adapter-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../abi/include")
    add_test(NAME renpy-mobile-adapter COMMAND renpy-mobile-adapter-test)

    add_executable(renpy-ios-adapter-test
        tests/ios_adapter.cpp src/renpy_runtime_ios_adapter.cpp)
    target_include_directories(renpy-ios-adapter-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../abi/include")
    add_test(NAME renpy-ios-adapter COMMAND renpy-ios-adapter-test)
elseif(ENABLE_TESTS AND (IOS OR ANDROID))
    # This target is compile-only in cross builds.  It verifies the mobile
    # provider ABI and its deliberate NOT_SUPPORTED contract without trying
    # to execute an iOS/Android binary on the build host.
    add_executable(renpy-mobile-provider-test tests/mobile_provider.cpp)
    target_link_libraries(renpy-mobile-provider-test PRIVATE aether_renpy_runtime)
    if(ANDROID)
        # Compile the Android JNI/bootstrap boundary as a standalone test
        # target as well. Cross builds do not execute this binary on the host.
        add_executable(renpy-mobile-adapter-test tests/mobile_adapter.cpp)
        target_link_libraries(renpy-mobile-adapter-test PRIVATE
            aether_renpy_runtime)
    endif()
    if(IOS)
        add_executable(renpy-ios-adapter-test tests/ios_adapter.cpp)
        target_link_libraries(renpy-ios-adapter-test PRIVATE aether_renpy_runtime)
    endif()
endif()
