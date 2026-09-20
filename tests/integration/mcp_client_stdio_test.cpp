#include "agent_rpc/mcp/mcp_client.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

using agent_rpc::mcp::MCPClient;

namespace {

int fail(const std::string& message) {
    std::cerr << "[FAIL] " << message << '\n';
    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 4) {
        return fail("usage: mcp_client_stdio_test <server> <plugins> <logs>");
    }

    const std::string server = argv[1];
    const std::string plugins = argv[2];
    const std::string logs = argv[3];
    std::filesystem::create_directories(logs);

    MCPClient client;
    if (!client.connect(server, {
            "--plugins", plugins,
            "--logs", logs,
        })) {
        return fail("could not connect to MCP Server");
    }

    const auto tools = client.listTools();
    const bool has_calculator = std::any_of(
        tools.begin(), tools.end(), [](const auto& tool) {
            return tool.name == "calculator";
        });
    if (!has_calculator) {
        client.disconnect();
        return fail("calculator was not returned by tools/list");
    }

    const auto response = client.callTool(
        "calculator", R"({"expression":"1+2"})");
    if (response.is_error) {
        client.disconnect();
        return fail("calculator returned an MCP error: " + response.error);
    }
    if (response.result.find("1+2 = 3") == std::string::npos) {
        client.disconnect();
        return fail("unexpected calculator result: " + response.result);
    }

    client.disconnect();
    std::cout << "[PASS] C++ MCPClient STDIO integration\n";
    return 0;
}
