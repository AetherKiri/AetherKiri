find_package(libjpeg-turbo CONFIG REQUIRED)
add_executable(siglus-runtime-jpeg
    jpeg_decoder.cpp
    ${CMAKE_SOURCE_DIR}/bridge/siglus_runtime/src/siglus_jpeg_decoder.cpp)
target_compile_features(siglus-runtime-jpeg PRIVATE cxx_std_17)
target_include_directories(siglus-runtime-jpeg PRIVATE
    ${CMAKE_SOURCE_DIR}/bridge/siglus_runtime/src)
target_link_libraries(siglus-runtime-jpeg PRIVATE Catch2::Catch2WithMain
    $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>)
catch_discover_tests(siglus-runtime-jpeg)
