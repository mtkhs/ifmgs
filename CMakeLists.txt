# 3.22: tests/CMakeLists.txt relies on the ENVIRONMENT_MODIFICATION test property.
cmake_minimum_required(VERSION 3.22)

project(ifmgs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Static CRT: the plugins are dropped into a host's folder as-is, so they must
# not depend on a VC++ redistributable. Ghostscript itself is loaded at runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ifmgs requires a 64-bit (x64) build (gsdll64.dll). Pass -A x64.")
endif()

if(MSVC)
    add_compile_options(/W4 /utf-8 /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Ghostscript renderer shared by both plugins: drives GS through the display
# device callback and returns a Windows BMP buffer for PS/EPS or PDF input.
set(RENDERER_SOURCES
    src/ghostscript_render.cpp
    src/path_utils.cpp
)

# ifmgs plugin (IN series - PostScript/EPS viewer)
add_library(ifmgs SHARED
    src/ifmgs.cpp
    src/bmp.cpp
    src/ps_detect.cpp
    ${RENDERER_SOURCES}
)
target_include_directories(ifmgs PRIVATE include)
set_target_properties(ifmgs PROPERTIES
    PREFIX ""
    OUTPUT_NAME "ifmgs"
    SUFFIX ".sph"
    LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/ifmgs.def"
)

# axmgs plugin (AX series - PDF/AI archive plugin)
add_library(axmgs SHARED
    src/axmgs.cpp
    ${RENDERER_SOURCES}
)
target_include_directories(axmgs PRIVATE include)
set_target_properties(axmgs PROPERTIES
    PREFIX ""
    OUTPUT_NAME "axmgs"
    SUFFIX ".sph"
    LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/axmgs.def"
)

option(IFMGS_BUILD_TESTS "Build smoke tests" ON)
if(IFMGS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
