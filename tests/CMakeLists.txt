cmake_minimum_required(VERSION 3.20)

add_executable(mcp_client_stdio_test integration/mcp_client_stdio_test.cpp)
target_link_libraries(mcp_client_stdio_test PRIVATE
    mcp_client
    mcp_common
)
target_compile_features(mcp_client_stdio_test PRIVATE cxx_std_20)
add_dependencies(mcp_client_stdio_test
    mcp_server calculator sleep weather codereview notification bacio-quote
)

set(MCP_TEST_LOG_DIR "${CMAKE_CURRENT_BINARY_DIR}/logs")
add_test(
    NAME mcp_client_stdio
    COMMAND mcp_client_stdio_test
        $<TARGET_FILE:mcp_server>
        $<TARGET_FILE_DIR:mcp_server>/plugins
        ${MCP_TEST_LOG_DIR}/cpp-client
)
set_tests_properties(mcp_client_stdio PROPERTIES TIMEOUT 30)

add_executable(mcp_client_sse_parser_test integration/mcp_client_sse_parser_test.cpp)
target_link_libraries(mcp_client_sse_parser_test PRIVATE
    mcp_client
    mcp_common
)
target_compile_features(mcp_client_sse_parser_test PRIVATE cxx_std_20)
add_test(NAME mcp_client_sse_parser COMMAND mcp_client_sse_parser_test)

add_executable(rag_core_test integration/rag_core_test.cpp)
target_link_libraries(rag_core_test PRIVATE
    mcp_client
    mcp_common
)
target_compile_features(rag_core_test PRIVATE cxx_std_20)
add_test(NAME rag_core COMMAND rag_core_test)

if(UNIX)
    add_executable(plugin_reload_concurrency_test
        integration/plugin_reload_concurrency_test.cpp
        ${PROJECT_SOURCE_DIR}/mcp_server/src/loader/PluginsLoader.cpp
        ${PROJECT_SOURCE_DIR}/mcp_server/src/manager/PluginRegistries.cpp
        ${PROJECT_SOURCE_DIR}/mcp_server/src/manager/PluginManager.cpp
    )
    target_include_directories(plugin_reload_concurrency_test PRIVATE
        ${PROJECT_SOURCE_DIR}/mcp_server/include
        ${PROJECT_SOURCE_DIR}/mcp_server/src
        ${PROJECT_SOURCE_DIR}/mcp_server/src/interface
        ${PROJECT_SOURCE_DIR}/mcp_server/src/loader
        ${PROJECT_SOURCE_DIR}/mcp_server/src/manager
        ${PROJECT_SOURCE_DIR}/mcp_server/libs_tier_01/aixlog-1.5.0/include
    )
    target_link_libraries(plugin_reload_concurrency_test PRIVATE
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    target_compile_features(plugin_reload_concurrency_test PRIVATE cxx_std_20)
    add_dependencies(plugin_reload_concurrency_test
        calculator sleep weather codereview notification bacio-quote
    )
    add_test(
        NAME plugin_reload_concurrency
        COMMAND plugin_reload_concurrency_test
            $<TARGET_FILE_DIR:mcp_server>/plugins
    )
    set_tests_properties(plugin_reload_concurrency PROPERTIES TIMEOUT 30)
endif()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME mcp_server_stdio_protocol
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_server_stdio.py
            --server $<TARGET_FILE:mcp_server>
            --plugins $<TARGET_FILE_DIR:mcp_server>/plugins
            --logs ${MCP_TEST_LOG_DIR}/server-protocol
    )
    set_tests_properties(mcp_server_stdio_protocol PROPERTIES TIMEOUT 30)

    add_test(
        NAME mcp_server_sse_protocol
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_server_sse.py
            --server $<TARGET_FILE:mcp_server>
            --plugins $<TARGET_FILE_DIR:mcp_server>/plugins
            --logs ${MCP_TEST_LOG_DIR}/server-sse
    )
    set_tests_properties(mcp_server_sse_protocol PROPERTIES TIMEOUT 30)
else()
    message(WARNING "Python 3 was not found; skipping protocol integration test")
endif()
