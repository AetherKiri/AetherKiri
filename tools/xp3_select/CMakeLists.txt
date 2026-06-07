cmake_minimum_required(VERSION 3.28)
project(xp3_select LANGUAGES CXX VERSION 1.0.0)

add_executable(${PROJECT_NAME}
    main.cpp
)

target_link_libraries(${PROJECT_NAME} PUBLIC tjs2 PRIVATE core_base_module)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PROGRAM_NAME="${PROJECT_NAME}"
    VERSION="${PROJECT_VERSION}"
)
