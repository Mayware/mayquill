cmake_minimum_required(VERSION 4.1.2)

#> https://github.com/Kitware/CMake/blob/v4.3.2/Help/dev/experimental.rst#c-import-std-support
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
set(CMAKE_CXX_MODULE_STD ON)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#> Debug by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

project(mayquill)
add_executable(mayquill)

file(GLOB_RECURSE GEN_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-modules/*.cppm")
list(FILTER GEN_FILES EXCLUDE REGEX "_impl\\.cpp$")

file(GLOB_RECURSE GEN_IMPL_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-modules/*_impl.cpp")
file (GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm")
target_sources(mayquill
    PRIVATE
        src/main.cpp
        src/client_impl.cpp
        ${GEN_IMPL_FILES}
    PRIVATE FILE_SET CXX_MODULES FILES
        ${SRC_FILES}
        ${GEN_FILES}
)
