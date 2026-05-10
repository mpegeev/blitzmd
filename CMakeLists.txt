cmake_minimum_required(VERSION 3.15)
project(mdview-wlx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(mdview SHARED
    src/plugin.cpp
    src/plugin.def
)

# WLX expects very specific extensions:
#   .wlx   for 32-bit Total Commander
#   .wlx64 for 64-bit Total Commander
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(mdview PROPERTIES
        SUFFIX ".wlx64"
        PREFIX "")
else()
    set_target_properties(mdview PROPERTIES
        SUFFIX ".wlx"
        PREFIX "")
endif()

target_link_libraries(mdview PRIVATE comctl32)

if(MSVC)
    target_compile_options(mdview PRIVATE /W4 /permissive-)
    target_compile_definitions(mdview PRIVATE
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS)
endif()
