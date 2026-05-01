cmake_minimum_required(VERSION 4.1.2)

#> https://github.com/Kitware/CMake/blob/v4.1.2/Help/dev/experimental.rst#c-import-std-support
# set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")

project(mayquill)

set(CMAKE_CXX_STANDARD 23)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_MODULE_STD ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

add_executable(mayquill)

target_sources(mayquill
    PRIVATE
    main.cpp
)
