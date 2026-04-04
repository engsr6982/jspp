cmake_minimum_required(VERSION 3.20)

if(WIN32)
    cmake_policy(SET CMP0091 NEW)
endif()

project(v8kit CXX)

# ==============================================================================
# Options
# ==============================================================================
option(V8KIT_BUILD_TESTS "Build test cases" OFF)
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
# Target: v8kit
# ==============================================================================
file(GLOB_RECURSE SRC_FILES "src/*.cc")
file(GLOB_RECURSE HEADER_FILES "src/v8kit/*.h" "src/v8kit/*.inl")

add_library(v8kit STATIC ${SRC_FILES} ${HEADER_FILES})
target_include_directories(v8kit PUBLIC src)

if(V8_INCLUDE_DIR AND EXISTS "${V8_INCLUDE_DIR}")
    target_include_directories(v8kit PUBLIC "${V8_INCLUDE_DIR}")
endif()

target_compile_definitions(v8kit PUBLIC "$<$<CONFIG:Debug>:V8KIT_DEBUG>")

if(WIN32)
    target_compile_options(v8kit PUBLIC /utf-8 /W4 /sdl /Zc:__cplusplus /Zc:preprocessor)
elseif(UNIX AND NOT APPLE)
    set_property(TARGET v8kit PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_compile_options(v8kit PUBLIC -stdlib=libc++)
    target_link_options(v8kit PUBLIC -stdlib=libc++)
    target_link_libraries(v8kit PUBLIC dl pthread) 
endif()

# ==============================================================================
# Testing
# ==============================================================================
if(V8KIT_BUILD_TESTS)
    add_subdirectory(test)
endif()