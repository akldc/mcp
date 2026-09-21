#ifndef MCP_SERVER_PLUGIN_REGISTRIES_H
#define MCP_SERVER_PLUGIN_REGISTRIES_H

#include "PluginAPI.h"
#include "json.hpp"

#include <string>
#include <vector>

namespace vx::mcp {

struct RegisteredTool {  // 表示一个已经注册到server的工具；一个插件可能包含多个工具
    std::string name;
    std::string description;
    nlohmann::json input_schema;
    PluginAPI* plugin = nullptr;  // 指向插件的指针，用于在调用工具时找到对应的插件
};

class ToolManager {
public:
    void registerPlugin(PluginAPI* plugin);
    void unregisterAll();
    nlohmann::json list() const;
    const RegisteredTool* find(const std::string& name) const;

private:
    std::vector<RegisteredTool> tools_; // 存储所有注册的工具
};

struct RegisteredPrompt {
    std::string name;
    std::string description;
    nlohmann::json arguments;
    PluginAPI* plugin = nullptr;
};

class PromptManager {
public:
    void registerPlugin(PluginAPI* plugin);
    void unregisterAll();
    nlohmann::json list() const;
    const RegisteredPrompt* find(const std::string& name) const;

private:
    std::vector<RegisteredPrompt> prompts_;
};

struct RegisteredResource {
    std::string name;
    std::string description;
    std::string uri;
    std::string mime_type;
    PluginAPI* plugin = nullptr;
};

class ResourceManager {
public:
    void registerPlugin(PluginAPI* plugin);
    void unregisterAll();
    nlohmann::json list() const;
    const RegisteredResource* find(const std::string& uri) const;

private:
    std::vector<RegisteredResource> resources_;
};

} // namespace vx::mcp

#endif // MCP_SERVER_PLUGIN_REGISTRIES_H
