message(STATUS "[jspp] Configuring QuickJS Backend")

target_compile_definitions(jspp PUBLIC JSPP_BACKEND_QUICKJS)

# QuickJS backend sources
set(QUICKJS_SRC
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/BytecodeContainer.cc"
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/QjsEngine.cc"
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/QjsException.cc"
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/QjsHelper.cc"
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/QjsReference.cc"
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/QjsScope.cc"
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/QjsTrampoline.cc"
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/QjsValue.cc"
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/queue/JobQueue.cc"
)

target_sources(jspp PRIVATE ${QUICKJS_SRC})

target_include_directories(jspp PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>"
    "$<INSTALL_INTERFACE:include>"
)

if(UNIX AND NOT APPLE)
    target_link_libraries(jspp PUBLIC dl m)
endif()


# ==============================================================================
# Target: scriptc (quickjs bytecode container compiler tool, optional)
# ==============================================================================
# scriptc compiles an ES module graph into a bytecode container ('JSC1': one
# blob per module plus a module table and an import table). It links the
# quickjs-ng library target (qjs): bytecode embeds a checksum and a format
# version, so the compiler and runtime builds must match. With
# JSPP_EXTERNAL_LIB set, the qjs library is located in the external
# directory; otherwise the qjs target must already exist in the host's CMake
# environment (quickjs-ng's own target name).
option(JSPP_BUILD_TOOLS "Build auxiliary tools (scriptc)" OFF)
if(JSPP_BUILD_TOOLS)
    add_executable(scriptc
        ${CMAKE_CURRENT_LIST_DIR}/tools/scriptc_main.cc
        ${CMAKE_CURRENT_LIST_DIR}/tools/scriptc_core.cc
        ${CMAKE_CURRENT_LIST_DIR}/jspp-backend/BytecodeContainer.cc
    )
    target_compile_features(scriptc PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(scriptc PRIVATE /utf-8)
    endif()
    if(JSPP_EXTERNAL_LIB)
        target_include_directories(scriptc PRIVATE ${JSPP_EXTERNAL_INC})
        target_link_directories(scriptc PRIVATE ${JSPP_EXTERNAL_LIB})
    endif()
    target_link_libraries(scriptc PRIVATE qjs)
    install(TARGETS scriptc RUNTIME DESTINATION bin)
endif()