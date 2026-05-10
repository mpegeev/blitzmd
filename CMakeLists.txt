cmake_minimum_required(VERSION 3.28)
project(mdview-wlx LANGUAGES CXX)

# C++26 в MSVC 14.50 включается флагом /std:c++latest, но CMake 4.1.1 ещё
# не имеет внутреннего маппинга cxx_std_26 -> /std:??? для MSVC, поэтому
# set(CMAKE_CXX_STANDARD 26) валится с ошибкой "compiler does not support".
# Поэтому НЕ выставляем CMAKE_CXX_STANDARD, а пробрасываем флаг напрямую.
#
# Базовый стандарт оставляем C++23 на случай если кто-то соберёт проект
# clang'ом или gcc'шкой — у них cxx_std_23 уже стабильно поддерживается.

set(CMAKE_CXX_STANDARD 23)
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
    # Будет предупреждение об override C++23 -> latest, это ожидаемо.
    target_compile_options(mdview PRIVATE
        /W4 /permissive-
        /std:c++latest
        /Zc:preprocessor      # conformant препроцессор
        /utf-8                # исходники и runtime строки в UTF-8
    )
    target_compile_definitions(mdview PRIVATE
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )

    # Статическая CRT — плагин работает без VC++ Redist на машине пользователя.
    set_property(TARGET mdview PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()