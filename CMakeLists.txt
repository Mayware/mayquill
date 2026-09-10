cmake_minimum_required(VERSION 4.1.2)

#> https://github.com/Kitware/CMake/blob/v4.3.2/Help/dev/experimental.rst#c-import-std-support
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
set(CMAKE_CXX_MODULE_STD ON)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(mayquill)
add_library(mayquill STATIC)

#> Maylog
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../maylog ${CMAKE_CURRENT_BINARY_DIR}/maylog)
target_link_libraries(mayquill PUBLIC maylog)

file(GLOB_RECURSE GEN_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-modules/*.cppm")
file(GLOB_RECURSE GEN_IMPL_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-modules/*_impl.cpp")
file (GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm")
target_sources(mayquill
    PRIVATE
        src/client_impl.cpp
        ${GEN_IMPL_FILES}
    PUBLIC FILE_SET CXX_MODULES FILES
        ${SRC_FILES}
        ${GEN_FILES}
)
