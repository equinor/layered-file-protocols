cmake_minimum_required(VERSION 3.5)
project(lfp-Catch2 CXX)

add_library(Catch2 INTERFACE)
add_library(Catch2::Catch2 ALIAS Catch2)
target_include_directories(Catch2
    SYSTEM
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

