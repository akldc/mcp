/**
 * @file mcp_basic_example.cpp
 * @brief MCPAgentIntegration 的普通 MCP 使用示例（不启用 RAG）
 */

#include "agent_rpc/mcp/mcp_agent_integration.h"

#include <filesystem>
#include <iostream>
#include <string>

using namespace agent_rpc::mcp;

namespace {

struct Options {
    std::string server = "./build/mcp_server/mcp_server";
    std::string plugins = "./build/mcp_server/plugins";
    std::string logs = "./build/example_logs";
};

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  --mcp-server <path>  MCP Server executable\n"
              << "  --plugins <path>     Plugin directory\n"
              << "  --logs <path>        Server log directory\n"
              << "  --help               Show this help\n";
}

bool parseOptions(int argc, char* argv[], Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            printUsage(argv[0]);
            return false;
        }
        if (i + 1 >= argc) {
            std::cerr << "Missing value for option: " << arg << '\n';
            return false;
        }
        if (arg == "--mcp-server") {
            options.server = argv[++i];
        } else if (arg == "--plugins") {
            options.plugins = argv[++i];
        } else if (arg == "--logs") {
            options.logs = argv[++i];
        } else {
            std::cerr << "Unknown option: " << arg << '\n';
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        return argc > 1 && std::string(argv[1]) == "--help" ? 0 : 2;
    }

    std::filesystem::create_directories(options.logs);

    MCPAgentConfig config;
    config.enable_mcp = true;
    config.mcp_server_path = options.server;
    config.mcp_args = {
        "--plugins", options.plugins,
        "--logs", options.logs,
    };

    MCPAgentIntegration integration;
    if (!integration.initialize(config) || !integration.isAvailable()) {
        std::cerr << "MCP is not available: "
                  << integration.getStatusDescription() << '\n';
        return 1;
    }

    const auto tools = integration.getAvailableTools();
    std::cout << "Available tools (" << tools.size() << "):\n";
    for (const auto& tool : tools) {
        std::cout << "  - " << tool.name << ": " << tool.description << '\n';
    }

    const auto result = integration.callTool(
        "calculator", R"({"expression":"123 + 456"})");
    if (!result.success) {
        std::cerr << "calculator failed: " << result.error << '\n';
        integration.shutdown();
        return 1;
    }

    std::cout << "\ncalculator result:\n" << result.result << '\n';
    integration.shutdown();
    return 0;
}
