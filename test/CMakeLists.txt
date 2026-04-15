# On the Windows platform, most v8_monolith.lib are MT runtime
# Forcing a unified MT runtime here to avoid runtime conflicts with test frameworks like catch2
if(WIN32 AND ${JSPP_BACKEND} STREQUAL "v8")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    set_target_properties(jspp PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

# ==============================================================================
# Pull Catch2
# ==============================================================================
include(FetchContent)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
set(CATCH_DEVELOPMENT_BUILD OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.1
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(Catch2)

# ==============================================================================
# Target: jspp_test
# ==============================================================================
file(GLOB_RECURSE TEST_FILES "*.cc")
add_executable(jspp_test ${TEST_FILES})

target_link_libraries(jspp_test PRIVATE jspp Catch2::Catch2)

if(WIN32 AND ${JSPP_BACKEND} STREQUAL "v8")
    target_link_libraries(jspp_test PRIVATE winmm advapi32 dbghelp)

    set_target_properties(jspp_test PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

if (${JSPP_BACKEND} STREQUAL "quickjs")
    set(BUILD_EXECUTABLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

    set(QUICKJS_PROJ_DIR "${CMAKE_SOURCE_DIR}/deps/quickjs-ng")

    if(EXISTS "${QUICKJS_PROJ_DIR}/CMakeLists.txt")
        add_subdirectory(${CMAKE_SOURCE_DIR}/deps/quickjs-ng ${CMAKE_BINARY_DIR}/quickjs-ng)
        target_link_libraries(jspp PUBLIC qjs)
    else()
        message(FATAL_ERROR "QuickJS-ng source not found in deps/")
    endif()
endif()