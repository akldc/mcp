cmake_minimum_required(VERSION 3.20)

project(test_mcp)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(test1 test1.cpp)

target_include_directories(test1 PRIVATE
    ../mcp_client/include
    ../common/include
)

target_link_directories(test1 PRIVATE
    ../build/mcp_client
    ../build/common
)

target_link_libraries(test1
    mcp_client
    mcp_common
    curl
    jsoncpp
    pthread
)