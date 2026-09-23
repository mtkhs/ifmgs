# Smoke tests: each executable loads one built plugin and drives it through
# the Susie API on a PS / PDF file it writes itself. Ghostscript must be
# installed; gsdll64.dll is found through PATH, the same way the plugins find
# it under a host.

file(GLOB GS_BIN_DIRS "C:/Program Files/gs/*/bin")
if(GS_BIN_DIRS)
    list(SORT GS_BIN_DIRS)
    list(GET GS_BIN_DIRS -1 GS_BIN_DIR)
    file(TO_NATIVE_PATH "${GS_BIN_DIR}" GS_BIN_DIR)
endif()

foreach(T IN ITEMS smoke_ifmgs smoke_axmgs)
    add_executable(${T} ${T}.cpp)
    target_include_directories(${T} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ${T} COMMAND ${T} "${CMAKE_CURRENT_BINARY_DIR}")
    if(GS_BIN_DIR)
        set_tests_properties(${T} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${GS_BIN_DIR}"
        )
    endif()
endforeach()

target_compile_definitions(smoke_ifmgs PRIVATE PLUGIN_PATH=L"$<TARGET_FILE:ifmgs>")
target_compile_definitions(smoke_axmgs PRIVATE PLUGIN_PATH=L"$<TARGET_FILE:axmgs>")
add_dependencies(smoke_ifmgs ifmgs)
add_dependencies(smoke_axmgs axmgs)
