#include "../../mcp_client/include/agent_rpc/mcp/mcp_client.h"
#include <iostream>

int main()
{
    // STDIO 模式连接
    agent_rpc::mcp::MCPClient client;
    client.connect("../../build/mcp_server/mcp_server", {"--plugins", "../../build/mcp_server/plugins"});

    // 列出工具
    auto tools = client.listTools();
    std::cout<<std::endl;
    int i = 0;
    for (const auto& tool : tools) {
        std::cout<<"["<<i++<<"] "<<tool.name << ": " << tool.description << std::endl<<std::endl;
    }
    std::cout<<std::endl;

    auto prompts = client.listPrompts();
    int j = 0;
    for (const auto& prompt : prompts) {
        std::cout<<"["<<j++<<"] "<<prompt.name << ": " << prompt.description << std::endl<<std::endl;
    }
    std::cout<<std::endl;

    auto resources = client.listResources();
    int k = 0;
    for (const auto& prompt : resources) {
        std::cout<<"["<<k++<<"] "<<prompt.name << ": " << prompt.description << std::endl<<std::endl;
    }
    std::cout<<std::endl;

    std::cout<<"hello"<<std::endl;

    // 调用工具
    auto response = client.callTool("calculator", R"({"expression": "2252-3+10*10"})");
    std::cout << response.result << std::endl;

    client.disconnect();

    return 0;
}