cmake_minimum_required(VERSION 3.15)
project(libbpg)

add_library(libbpg STATIC libbpg.c)
install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/"
        DESTINATION "include"
        FILES_MATCHING PATTERN "*.h"
)

target_include_directories(${PROJECT_NAME}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/>
        INTERFACE
            $<INSTALL_INTERFACE:include/>
)

find_package(FFMPEG COMPONENTS
        avutil
        avcodec REQUIRED
)
target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})
if(APPLE)
    # vcpkg's FFMPEG_LIBRARIES carry a bare "-pthread" driver flag; macOS
    # pthreads live in libSystem and swiftc-driven links (the mixed
    # C++/Swift Godot extension) reject it.
    get_target_property(_libbpg_interface ${PROJECT_NAME}
        INTERFACE_LINK_LIBRARIES)
    if(_libbpg_interface)
        list(REMOVE_ITEM _libbpg_interface "$<LINK_ONLY:-pthread>")
        set_target_properties(${PROJECT_NAME} PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_libbpg_interface}")
    endif()
endif()
