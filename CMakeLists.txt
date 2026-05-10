cmake_minimum_required(VERSION 3.28)
project(mdview-wlx LANGUAGES CXX)

# C++26 — VS 2026 / MSVC 14.50 supports a subset via /std:c++latest.
# CMake 3.28 знает про cxx_std_26; для более ранних версий упадёт сюда же
# через /std:c++latest по фоллбэку.
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mdview SHARED
    src/plugin.cpp
    src/plugin.def
)

# WLX expects very specific extensions:
#   .wlx   for 32-bit Total Commander
#   .wlx64 for 64-bit Total Commander
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(mdview PROPERTIES SUFFIX ".wlx64" PREFIX "")
else()
    set_target_properties(mdview PROPERTIES SUFFIX ".wlx" PREFIX "")
endif()

target_link_libraries(mdview PRIVATE comctl32)

if(MSVC)
    # /std:c++latest даёт C++26 draft фичи в MSVC 14.50.
    # CMake обычно сам подставляет /std:c++26 для cxx_std_26, но MSVC
    # пока маппит "26" на /std:c++latest — оставим явно для надёжности.
    target_compile_options(mdview PRIVATE
        /W4 /permissive-
        /std:c++latest
        /Zc:preprocessor      # conformant препроцессор, нужен некоторым STL фичам
        /utf-8                # исходники и runtime строки в UTF-8
    )
    target_compile_definitions(mdview PRIVATE
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )

    # Статическая CRT — плагин работает на любой машине без VC++ Redist.
    # Для DLL это безопасно, т.к. мы не передаём CRT-объекты через границу.
    set_property(TARGET mdview PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()