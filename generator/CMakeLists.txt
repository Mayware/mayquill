cmake_minimum_required(VERSION 4.1.2)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_MODULE_STD ON)

project(generator)
add_executable(generator)
target_sources(generator
    PRIVATE
        main.cpp
    PRIVATE FILE_SET CXX_MODULES FILES
        parser.cppm
)

find_package(pugixml REQUIRED)
target_link_libraries(generator PRIVATE pugixml::pugixml)
