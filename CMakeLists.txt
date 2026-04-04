cmake_minimum_required(VERSION 3.20)

if(WIN32)
    cmake_policy(SET CMP0091 NEW)
endif()

project(jspp CXX)

# ==============================================================================
# Options
# ==============================================================================
option(JSPP_BUILD_TESTS "Build test cases" OFF)
set(V8_INCLUDE_DIR "" CACHE PATH "Path to V8 include directory")

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
file(GLOB_RECURSE SRC_FILES "src/*.cc")
file(GLOB_RECURSE HEADER_FILES "src/jspp/*.h" "src/jspp/*.inl")

add_library(jspp STATIC ${SRC_FILES} ${HEADER_FILES})
target_include_directories(jspp PUBLIC src)

if(V8_INCLUDE_DIR AND EXISTS "${V8_INCLUDE_DIR}")
    target_include_directories(jspp PUBLIC "${V8_INCLUDE_DIR}")
endif()

target_compile_definitions(jspp PUBLIC "$<$<CONFIG:Debug>:JSPP_DEBUG>")

if(WIN32)
    target_compile_options(jspp PUBLIC /utf-8 /W4 /sdl /Zc:__cplusplus /Zc:preprocessor)
elseif(UNIX AND NOT APPLE)
    set_property(TARGET jspp PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_compile_options(jspp PUBLIC -stdlib=libc++)
    target_link_options(jspp PUBLIC -stdlib=libc++)
    target_link_libraries(jspp PUBLIC dl pthread) 
endif()

# ==============================================================================
# Testing
# ==============================================================================
if(JSPP_BUILD_TESTS)
    add_subdirectory(test)
endif()