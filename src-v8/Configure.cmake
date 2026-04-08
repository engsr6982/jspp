message(STATUS "[jspp] Configuring V8 Backend")

target_compile_definitions(jspp PUBLIC JSPP_BACKEND_V8)

# set source and include directory
file(GLOB_RECURSE V8_SRC "src-v8/*.cc")
target_sources(jspp PRIVATE ${V8_SRC})
target_include_directories(jspp PUBLIC "src-v8")

# external include
if(JSPP_EXTERNAL_INC)
    target_include_directories(jspp PUBLIC ${JSPP_EXTERNAL_INC})
endif()
if(JSPP_EXTERNAL_LIB)
    target_link_libraries(jspp PUBLIC ${JSPP_EXTERNAL_LIB})
endif()
