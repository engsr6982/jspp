message(STATUS "[jspp] Configuring V8 Backend")

target_compile_definitions(jspp PUBLIC JSPP_BACKEND_V8)

set(V8_SRC
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/V8Engine.cc"
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/V8Exception.cc"
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/V8Helper.cc"
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/V8Reference.cc"
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/V8Scope.cc"
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/V8Trampoline.cc"
    "${CMAKE_CURRENT_LIST_DIR}/jspp-backend/V8Value.cc"
)

target_sources(jspp PRIVATE ${V8_SRC})

target_include_directories(jspp PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>"
    "$<INSTALL_INTERFACE:include>"
)
