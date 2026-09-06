# set(CMAKE_GENERATOR "MinGW Makefiles")
set(CMAKE_GENERATOR "Visual Studio 17 2022")
message("generator is set to ${CMAKE_GENERATOR}")

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows")
endif()
message(STATUS "VCPKG_TARGET_TRIPLET is ${VCPKG_TARGET_TRIPLET}")

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    else()
        message(FATAL_ERROR "VCPKG_ROOT environment variable is not set, and CMAKE_TOOLCHAIN_FILE is not provided")
    endif()
endif()
message(STATUS "CMAKE_TOOLCHAIN_FILE is ${CMAKE_TOOLCHAIN_FILE}")

include("${CMAKE_TOOLCHAIN_FILE}")

project(ifmgs)
enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

# MSVC
add_compile_options(/Zc:__cplusplus)

# Shared Ghostscript renderer (used by both plugins).
# Despite the historical "_pdf" filename in earlier revisions, this layer is
# format-agnostic — it dispatches PS/EPS or PDF rendering to GS via the
# display callback and returns a Windows BMP buffer.
set(RENDERER_SOURCES
    src/ghostscript_render.cpp
)

# ifmgs plugin (IN series - PostScript/EPS viewer)
add_library(ifmgs SHARED
    src/ifmgs.cpp
    src/bmp.cpp
    src/ps_detect.cpp
    src/path_utils.cpp
    ${RENDERER_SOURCES}
)
target_include_directories(ifmgs PRIVATE ./include)
set_target_properties(ifmgs PROPERTIES
    PREFIX ""
    OUTPUT_NAME "ifmgs"
    SUFFIX ".sph"
)

# axmgs plugin (AX series - PDF/AI archive plugin)
add_library(axmgs SHARED
    src/axmgs.cpp
    src/path_utils.cpp
    ${RENDERER_SOURCES}
)
target_include_directories(axmgs PRIVATE ./include)
set_target_properties(axmgs PROPERTIES
    PREFIX ""
    OUTPUT_NAME "axmgs"
    SUFFIX ".sph"
)

