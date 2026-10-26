message(STATUS "[jspp] Configuring QuickJS Backend")

target_compile_definitions(jspp PUBLIC JSPP_BACKEND_QUICKJS)

# set source and include directory
file(GLOB_RECURSE QUICKJS_SRC "src-quickjs/*.cc")
target_sources(jspp PRIVATE ${QUICKJS_SRC})
target_include_directories(jspp PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src-quickjs>"
    "$<INSTALL_INTERFACE:include>")

if(UNIX AND NOT APPLE)
    target_link_libraries(jspp PUBLIC dl m)
endif()


# ==============================================================================
# Target: scriptc (quickjs bytecode compiler tool, optional)
# ==============================================================================
# scriptc compiles an ES module to quickjs bytecode as a single blob. It
# links the quickjs-ng library target (qjs): bytecode embeds a checksum and a
# format version, so the compiler and runtime builds must match. With
# JSPP_EXTERNAL_LIB set, the qjs library is located in the external
# directory; otherwise the qjs target must already exist in the host's CMake
# environment (quickjs-ng's own target name).
option(JSPP_BUILD_TOOLS "Build auxiliary tools (scriptc)" OFF)
if(JSPP_BUILD_TOOLS)
    enable_language(C)
    add_executable(scriptc ${CMAKE_CURRENT_LIST_DIR}/tools/scriptc.c)
    if(JSPP_EXTERNAL_LIB)
        target_include_directories(scriptc PRIVATE ${JSPP_EXTERNAL_INC})
        target_link_directories(scriptc PRIVATE ${JSPP_EXTERNAL_LIB})
    endif()
    target_link_libraries(scriptc PRIVATE qjs)
    install(TARGETS scriptc RUNTIME DESTINATION bin)
endif()