cmake_minimum_required(VERSION 3.20)
project(example-addon-qjs CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(QJS_BUILD_LIBC ON CACHE BOOL "" FORCE) # optional
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL") # optional

include(FetchContent)
FetchContent_Declare(
        quickjs
        GIT_REPOSITORY https://github.com/quickjs-ng/quickjs.git
        GIT_TAG        v0.13.0
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(quickjs)

target_include_directories(qjs PUBLIC
        $<BUILD_INTERFACE:${quickjs_SOURCE_DIR}>
)

set(JSPP_BACKEND "quickjs" CACHE STRING "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_BINARY_DIR}/jspp)

target_link_libraries(jspp PUBLIC qjs)

add_library(example-addon-qjs SHARED example.cc)

target_compile_definitions(example-addon-qjs PRIVATE # optional
        NOMINMAX
        UNICODE
        _UNICODE
)
if(MSVC) # optional
    target_compile_options(example-addon-qjs PRIVATE # optional
            /utf-8
            /EHa
    )
endif()
target_compile_definitions(jspp PUBLIC QJS_BUILD_LIBC) # optional

target_link_libraries(example-addon-qjs PRIVATE jspp qjs)