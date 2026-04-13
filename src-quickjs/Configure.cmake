message(STATUS "[jspp] Configuring QuickJS Backend")

target_compile_definitions(jspp PUBLIC JSPP_BACKEND_QUICKJS)

# set source and include directory
file(GLOB_RECURSE QUICKJS_SRC "src-quickjs/*.cc")
target_sources(jspp PRIVATE ${QUICKJS_SRC})
target_include_directories(jspp PUBLIC "src-quickjs")

set(BUILD_EXECUTABLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/quickjs-ng/CMakeLists.txt")
    add_subdirectory(deps/quickjs-ng EXCLUDE_FROM_ALL)
    target_link_libraries(jspp PUBLIC qjs)
else()
    message(FATAL_ERROR "QuickJS-ng source not found in deps/")
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(jspp PUBLIC dl m)
endif()