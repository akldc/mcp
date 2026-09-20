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
else()
    message(WARNING "Python 3 was not found; skipping protocol integration test")
endif()
