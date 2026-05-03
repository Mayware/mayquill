cmake_minimum_required(VERSION 4.1.2)

#> https://github.com/Kitware/CMake/blob/v4.3.2/Help/dev/experimental.rst#c-import-std-support
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")

set(CMAKE_CXX_COMPILER /usr/bin/clang++)
set(CMAKE_C_COMPILER /usr/bin/clang)
set(CMAKE_CXX_MODULE_STD ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#> Debug by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

#>                  Use clang's libc, clang complains its own libc exports td
set(CMAKE_CXX_FLAGS "-stdlib=libc++ -Wno-reserved-module-identifier")

project(mayquill)

add_executable(mayquill)

target_sources(mayquill
    PRIVATE
        main.cpp
    PRIVATE FILE_SET CXX_MODULES FILES
        util.cppm
        server.cppm
)

find_package(pugixml REQUIRED)
target_link_libraries(mayquill PRIVATE pugixml::pugixml)
