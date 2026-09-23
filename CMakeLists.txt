cmake_minimum_required(VERSION 3.25)

# Static CRT: the plugin must not depend on any VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(mdview-wlx VERSION 1.0.0 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT MSVC)
    message(FATAL_ERROR "mdview is a Windows plugin and is built with MSVC.")
endif()

# CMake adds /W3 by default; we set warning levels per target.
foreach(flag_var CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
    string(REGEX REPLACE "/W[0-4]" "" ${flag_var} "${${flag_var}}")
endforeach()
# Favor small, fast code in Release: whole-program optimization + COMDAT folding.
string(APPEND CMAKE_C_FLAGS_RELEASE " /Gy /Gw /GL")
string(APPEND CMAKE_CXX_FLAGS_RELEASE " /Gy /Gw /GL")
foreach(kind SHARED EXE STATIC)
    if(kind STREQUAL "STATIC")
        string(APPEND CMAKE_${kind}_LINKER_FLAGS_RELEASE " /LTCG")
    else()
        string(APPEND CMAKE_${kind}_LINKER_FLAGS_RELEASE " /LTCG /OPT:REF /OPT:ICF")
    endif()
endforeach()

option(MDVIEW_ASAN "Build with AddressSanitizer (testing only)" OFF)
if(MDVIEW_ASAN)
    add_compile_options(/fsanitize=address /Zi)
    add_link_options(/INCREMENTAL:NO /DEBUG)
endif()

# ---- md4c (vendored, see third_party/md4c/VERSION.txt) ----------------------
add_library(md4c STATIC
    third_party/md4c/md4c.c
    third_party/md4c/entity.c
)
target_include_directories(md4c PUBLIC third_party/md4c)
target_compile_options(md4c PRIVATE /w /utf-8)

# md4c's own HTML renderer; only used by the test tool as a reference.
add_library(md4c_html STATIC third_party/md4c/md4c-html.c)
target_link_libraries(md4c_html PUBLIC md4c)
target_compile_options(md4c_html PRIVATE /w /utf-8)

# ---- core: parsing, layout and rendering (shared by plugin and tests) -------
set(MDVIEW_HIGHLIGHT_SOURCE src/core/highlight.cpp CACHE FILEPATH "Syntax highlighter implementation")
add_library(mdview_core STATIC
    src/core/util.cpp
    src/core/document.cpp
    src/core/html.cpp
    src/core/frontmatter.cpp
    src/core/parser.cpp
    src/core/texmath.cpp
    ${MDVIEW_HIGHLIGHT_SOURCE}
    src/core/emoji.cpp
    src/core/flags.cpp
    src/core/theme.cpp
    src/core/settings.cpp
    src/core/settings_dialog.cpp
    src/core/images.cpp
    src/core/svg.cpp
    src/core/layout.cpp
    src/core/mathlayout.cpp
    src/core/render.cpp
    src/core/icons.cpp
    src/core/viewer.cpp
    src/core/offscreen.cpp
    src/core/print.cpp
)
target_include_directories(mdview_core PUBLIC src)
target_link_libraries(mdview_core PUBLIC md4c
    d2d1 dwrite windowscodecs winhttp shlwapi uxtheme comctl32 shell32 ole32 oleaut32 user32 gdi32)

set(MDVIEW_CXX_OPTIONS /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:inline /EHsc /GR- /MP)
set(MDVIEW_DEFINITIONS UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00
    MDVIEW_VERSION="${PROJECT_VERSION}" MDVIEW_VER_MAJOR=${PROJECT_VERSION_MAJOR}
    MDVIEW_VER_MINOR=${PROJECT_VERSION_MINOR} MDVIEW_VER_PATCH=${PROJECT_VERSION_PATCH})
target_compile_options(mdview_core PRIVATE ${MDVIEW_CXX_OPTIONS})
target_compile_definitions(mdview_core PUBLIC ${MDVIEW_DEFINITIONS})

# ---- the Lister plugin DLL --------------------------------------------------
# Resource scripts are not scanned for the files they embed.
set_source_files_properties(src/plugin.rc tests/mdview_test.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/twemoji-flags.bin;${CMAKE_SOURCE_DIR}/src/mdview.manifest")
add_library(mdview SHARED src/plugin.cpp src/plugin.def src/plugin.rc)
target_link_libraries(mdview PRIVATE mdview_core delayimp)
# Load rarely needed system DLLs only when used (remote images, printing).
target_link_options(mdview PRIVATE /DELAYLOAD:winhttp.dll /DELAYLOAD:comdlg32.dll /DELAYLOAD:dwmapi.dll)
target_compile_options(mdview PRIVATE ${MDVIEW_CXX_OPTIONS})
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(mdview PROPERTIES PREFIX "" SUFFIX ".wlx64")
else()
    set_target_properties(mdview PROPERTIES PREFIX "" SUFFIX ".wlx")
endif()

# ---- test tool: spec conformance, PNG rendering, benchmarks -----------------
add_executable(mdview_test tests/mdview_test.cpp tests/spec_runner.cpp tests/html_dump.cpp tests/mdview_test.rc)
target_link_libraries(mdview_test PRIVATE mdview_core md4c_html)
target_compile_options(mdview_test PRIVATE ${MDVIEW_CXX_OPTIONS})
set_target_properties(mdview_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")

# ---- unit tests of self-contained modules -----------------------------------
add_executable(highlight_test tests/highlight_test.cpp src/core/highlight.cpp)
target_compile_options(highlight_test PRIVATE ${MDVIEW_CXX_OPTIONS})
add_executable(emoji_test tests/emoji_test.cpp src/core/emoji.cpp)
target_compile_options(emoji_test PRIVATE ${MDVIEW_CXX_OPTIONS})
