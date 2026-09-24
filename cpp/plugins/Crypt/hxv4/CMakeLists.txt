find_package(unofficial-sodium CONFIG REQUIRED)

add_library(hxv4_decoder STATIC
    Hxv4Decoder.cpp
    Hxv4Crypto.cpp
)

target_link_libraries(hxv4_decoder PRIVATE unofficial-sodium::sodium)
target_include_directories(hxv4_decoder PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/cpp/core/plugin
    ${CMAKE_SOURCE_DIR}/cpp/core/base
    ${CMAKE_SOURCE_DIR}/cpp/core/base/impl
    ${CMAKE_SOURCE_DIR}/cpp/core/tjs2
    ${CMAKE_SOURCE_DIR}/cpp/core/common
    ${CMAKE_SOURCE_DIR}/cpp/core/utils
)