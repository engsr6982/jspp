cmake_minimum_required(VERSION 3.20)

if(WIN32)
    cmake_policy(SET CMP0091 NEW)
endif()

project(jspp CXX)

# ==============================================================================
# Options
# ==============================================================================
set(JSPP_BACKEND "v8" CACHE STRING "Select JS backend: v8 or quickjs")

set(JSPP_EXTERNAL_INC "" CACHE PATH "Include directories of the JS engine")
set(JSPP_EXTERNAL_LIB "" CACHE PATH "Full paths to the JS engine libraries")

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

set(JSPP_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(JSPP_BACKEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src-${JSPP_BACKEND}")

file(GLOB_RECURSE CORE_SRC "${JSPP_CORE_DIR}/*.cc")
file(GLOB_RECURSE BACKEND_SRC "${JSPP_BACKEND_DIR}/*.cc")

file(GLOB_RECURSE SRC_FILES "src/*.cc")
file(GLOB_RECURSE HEADER_FILES "src/jspp/*.h" "src/jspp/*.inl")

add_library(jspp STATIC ${CORE_SRC} ${BACKEND_SRC})

target_include_directories(jspp PUBLIC 
    $<BUILD_INTERFACE:${JSPP_CORE_DIR}>
    $<BUILD_INTERFACE:${JSPP_BACKEND_DIR}>
)

if(JSPP_EXTERNAL_INC)
    target_include_directories(jspp PUBLIC ${JSPP_EXTERNAL_INC})
endif()

if(JSPP_EXTERNAL_LIB)
    target_link_libraries(jspp PUBLIC ${JSPP_EXTERNAL_LIB})
endif()


target_compile_definitions(jspp PUBLIC "$<$<CONFIG:Debug>:JSPP_DEBUG>")

# backend specific options
if (${JSPP_BACKEND} STREQUAL "v8")
    target_compile_definitions(jspp PUBLIC "JSPP_BACKEND_V8")
elseif(${JSPP_BACKEND} STREQAL "quickjs")
    target_compile_definitions(jspp PUBLIC "JSPP_BACKEND_QUICKJS")
else()
    message(FATAL_ERROR "Invalid backend: ${JSPP_BACKEND}")
endif()


if(WIN32)
    target_compile_options(jspp PUBLIC /utf-8 /W4 /sdl /Zc:__cplusplus /Zc:preprocessor)
elseif(UNIX AND NOT APPLE)
    if(NOT APPLE)
        set_property(TARGET jspp PROPERTY POSITION_INDEPENDENT_CODE ON)
    endif()

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