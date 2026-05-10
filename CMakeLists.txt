cmake_minimum_required(VERSION 3.28)

# Static CRT — должно быть до project() и до FetchContent.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(mdview-wlx LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake по умолчанию подсовывает /W3 для C/C++. Удаляем перед FetchContent,
# чтобы /w на md4c не выдавал D9025 "overriding /W3 with /w".
# Свои warning-флаги мы выставим явно ниже на нашем target.
if(MSVC)
    foreach(flag_var
            CMAKE_C_FLAGS CMAKE_CXX_FLAGS
            CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG
            CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELEASE)
        string(REGEX REPLACE "/W[0-4]" "" ${flag_var} "${${flag_var}}")
    endforeach()
endif()

# ---- md4c via FetchContent --------------------------------------------------
include(FetchContent)
set(BUILD_MD2HTML_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    md4c
    GIT_REPOSITORY https://github.com/mity/md4c.git
    GIT_TAG release-0.5.2
)
FetchContent_MakeAvailable(md4c)

if(TARGET md4c)
    target_include_directories(md4c PUBLIC
        $<BUILD_INTERFACE:${md4c_SOURCE_DIR}/src>)
    if(MSVC)
        target_compile_options(md4c PRIVATE /w)
    endif()
endif()

# ---- our plugin -------------------------------------------------------------
add_library(mdview SHARED
    src/plugin.cpp
    src/plugin.def
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(mdview PROPERTIES SUFFIX ".wlx64" PREFIX "")
else()
    set_target_properties(mdview PROPERTIES SUFFIX ".wlx" PREFIX "")
endif()

target_link_libraries(mdview PRIVATE comctl32 md4c)

if(MSVC)
    target_compile_options(mdview PRIVATE
        /W4 /permissive-
        /std:c++latest
        /Zc:preprocessor
        /utf-8
    )
    target_compile_definitions(mdview PRIVATE
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()