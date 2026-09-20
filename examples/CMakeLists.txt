cmake_minimum_required(VERSION 3.20)

# 普通 MCP Agent 集成示例
add_executable(mcp_basic_example mcp_basic_example.cpp)
target_link_libraries(mcp_basic_example PRIVATE
    mcp_client
    mcp_common
)
target_compile_features(mcp_basic_example PRIVATE cxx_std_20)

# C++ RAG-MCP 工具检索示例
add_executable(rag_mcp_example rag_mcp_example.cpp)
target_link_libraries(rag_mcp_example PRIVATE
    mcp_client
    mcp_common
)
target_compile_features(rag_mcp_example PRIVATE cxx_std_20)
