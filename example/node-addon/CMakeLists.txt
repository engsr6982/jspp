cmake_minimum_required(VERSION 3.20)
project(example-node-addon CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_JS_VERSION)
    message(FATAL_ERROR "Please build this project with cmake-js. (npx cmake-js build)")
endif()

set(JSPP_BACKEND "v8" CACHE STRING "" FORCE)

# Transparently pass the include and lib provided by cmake-js to the host to jspp as dependency sources
set(JSPP_EXTERNAL_INC "${CMAKE_JS_INC}" CACHE STRING "" FORCE)
if (CMAKE_JS_LIB)
    set(JSPP_EXTERNAL_LIB "${CMAKE_JS_LIB}" CACHE STRING "" FORCE)
endif()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_BINARY_DIR}/jspp)

add_library(${PROJECT_NAME} SHARED example.cc)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_JS_INC})
if (CMAKE_JS_LIB)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_JS_LIB})
endif()

# Node.js extension rules: no lib prefix, and the suffix is strictly .node
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")

target_link_libraries(${PROJECT_NAME} PRIVATE jspp)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /EHa)
endif()
