cmake_minimum_required(VERSION 3.19)

project(aether_archive_import LANGUAGES CXX)

find_package(7zip CONFIG REQUIRED)
find_package(lz4 CONFIG REQUIRED)

add_library(aether_archive_import STATIC
    src/archive_import.cpp
)
target_compile_features(aether_archive_import PUBLIC cxx_std_17)
target_include_directories(aether_archive_import PUBLIC include)
target_link_libraries(aether_archive_import PRIVATE 7zip::7zip lz4::lz4)
