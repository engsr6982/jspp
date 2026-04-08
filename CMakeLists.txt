cmake_minimum_required(VERSION 3.20)

if(WIN32)
    cmake_policy(SET CMP0091 NEW)
endif()

project(jspp CXX)

# ==============================================================================
# Options
# ==============================================================================
set(JSPP_BACKEND "v8" CACHE STRING "Select JS backend: v8 or quickjs")

set(JSPP_EXTERNAL_INC "" CACHE PATH "Optional: Path to JS engine headers")
set(JSPP_EXTERNAL_LIB "" CACHE PATH "Optional: Path to JS engine library files")

message(STATUS "[jspp] Backend: ${JSPP_BACKEND}")
message(STATUS "[jspp] External Inc: ${JSPP_EXTERNAL_INC}")
message(STATUS "[jspp] External Lib: ${JSPP_EXTERNAL_LIB}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# Global Settings
# ==============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

# ==============================================================================
# Target: jspp
# ==============================================================================
add_library(jspp STATIC)

file(GLOB_RECURSE CORE_SRC "src/*.cc")
target_sources(jspp PRIVATE ${CORE_SRC})
target_include_directories(jspp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")


target_compile_definitions(jspp PUBLIC "$<$<CONFIG:Debug>:JSPP_DEBUG>")

# backend specific options
if (${JSPP_BACKEND} STREQUAL "v8")
    include(src-v8/Configure.cmake)
elseif(${JSPP_BACKEND} STREQUAL "quickjs")
    include(src-quickjs/Configure.cmake)
else()
    message(FATAL_ERROR "Invalid backend: ${JSPP_BACKEND}")
endif()


if(WIN32)
    target_compile_options(jspp PUBLIC /utf-8 /W4 /sdl /Zc:__cplusplus /Zc:preprocessor)
elseif(UNIX AND NOT APPLE)
    set_property(TARGET jspp PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_compile_options(jspp PUBLIC -Wall -Wextra)

    if(UNIX AND NOT APPLE)
        target_link_libraries(jspp PUBLIC dl pthread)
    endif()
endif()


# ==============================================================================
# Target: test
# ==============================================================================
option(JSPP_BUILD_TESTS "Build test cases" OFF)
if(JSPP_BUILD_TESTS)
    add_subdirectory(test)
endif()