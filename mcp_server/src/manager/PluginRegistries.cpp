#include "PluginRegistries.h"

#include "aixlog.hpp"

#include <algorithm>

namespace vx::mcp {

void ToolManager::registerPlugin(PluginAPI* plugin) {  // 一个插件（动态库）可能包含多个工具（比如calculator)，遍历插件的所有工具，注册到tools_中
    for (int index = 0; index < plugin->GetToolCount(); ++index) {
        const PluginTool* tool = plugin->GetTool(index);
        if (!tool || !tool->name || !tool->description || !tool->inputSchema) {
            LOG(ERROR) << "Skipping invalid tool descriptor from plugin " << plugin->GetName() << std::endl;
            continue;
        }
        if (find(tool->name)) {
            LOG(WARNING) << "Skipping duplicate tool name: " << tool->name << std::endl;
            continue;
        }
        try {
            tools_.push_back({
                tool->name,
                tool->description,
                nlohmann::json::parse(tool->inputSchema),
                plugin,
            });
            LOG(INFO) << "Registered tool " << tool->name << " from " << plugin->GetName() << std::endl;
        } catch (const nlohmann::json::parse_error& ex) {
            LOG(ERROR) << "Skipping tool with invalid input schema " << tool->name << ": " << ex.what() << std::endl;
        }
    }
}

void ToolManager::unregisterAll() {
    LOG(INFO) << "Unregistering " << tools_.size() << " tool(s)" << std::endl;
    tools_.clear();
}

nlohmann::json ToolManager::list() const {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& tool : tools_) {
        result.push_back({
            {"name", tool.name},
            {"description", tool.description},
            {"inputSchema", tool.input_schema},
        });
    }
    return result;
}

const RegisteredTool* ToolManager::find(const std::string& name) const {
    const auto it = std::find_if(tools_.begin(), tools_.end(),
        [&name](const RegisteredTool& tool) { return tool.name == name; });
    return it == tools_.end() ? nullptr : &*it;
}

void PromptManager::registerPlugin(PluginAPI* plugin) {
    for (int index = 0; index < plugin->GetPromptCount(); ++index) {
        const PluginPrompt* prompt = plugin->GetPrompt(index);
        if (!prompt || !prompt->name || !prompt->description || !prompt->arguments) {
            LOG(ERROR) << "Skipping invalid prompt descriptor from plugin " << plugin->GetName() << std::endl;
            continue;
        }
        if (find(prompt->name)) {
            LOG(WARNING) << "Skipping duplicate prompt name: " << prompt->name << std::endl;
            continue;
        }
        try {
            prompts_.push_back({
                prompt->name,
                prompt->description,
                nlohmann::json::parse(prompt->arguments),
                plugin,
            });
            LOG(INFO) << "Registered prompt " << prompt->name << " from " << plugin->GetName() << std::endl;
        } catch (const nlohmann::json::parse_error& ex) {
            LOG(ERROR) << "Skipping prompt with invalid arguments " << prompt->name << ": " << ex.what() << std::endl;
        }
    }
}

void PromptManager::unregisterAll() {
    LOG(INFO) << "Unregistering " << prompts_.size() << " prompt(s)" << std::endl;
    prompts_.clear();
}

nlohmann::json PromptManager::list() const {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& prompt : prompts_) {
        result.push_back({
            {"name", prompt.name},
            {"description", prompt.description},
            {"arguments", prompt.arguments},
        });
    }
    return result;
}

const RegisteredPrompt* PromptManager::find(const std::string& name) const {
    const auto it = std::find_if(prompts_.begin(), prompts_.end(),
        [&name](const RegisteredPrompt& prompt) { return prompt.name == name; });
    return it == prompts_.end() ? nullptr : &*it;
}

void ResourceManager::registerPlugin(PluginAPI* plugin) {
    for (int index = 0; index < plugin->GetResourceCount(); ++index) {
        const PluginResource* resource = plugin->GetResource(index);
        if (!resource || !resource->name || !resource->description || !resource->uri || !resource->mime) {
            LOG(ERROR) << "Skipping invalid resource descriptor from plugin " << plugin->GetName() << std::endl;
            continue;
        }
        if (find(resource->uri)) {
            LOG(WARNING) << "Skipping duplicate resource URI: " << resource->uri << std::endl;
            continue;
        }
        resources_.push_back({
            resource->name,
            resource->description,
            resource->uri,
            resource->mime,
            plugin,
        });
        LOG(INFO) << "Registered resource " << resource->uri << " from " << plugin->GetName() << std::endl;
    }
}

void ResourceManager::unregisterAll() {
    LOG(INFO) << "Unregistering " << resources_.size() << " resource(s)" << std::endl;
    resources_.clear();
}

nlohmann::json ResourceManager::list() const {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& resource : resources_) {
        result.push_back({
            {"name", resource.name},
            {"description", resource.description},
            {"uri", resource.uri},
            {"mimeType", resource.mime_type},
        });
    }
    return result;
}

const RegisteredResource* ResourceManager::find(const std::string& uri) const {
    const auto it = std::find_if(resources_.begin(), resources_.end(),
        [&uri](const RegisteredResource& resource) { return resource.uri == uri; });
    return it == resources_.end() ? nullptr : &*it;
}

} // namespace vx::mcp
