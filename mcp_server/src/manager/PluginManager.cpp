#include "PluginManager.h"

#include "aixlog.hpp"
#include "utils/MCPBuilder.h"

#include <memory>
#include <utility>

namespace vx::mcp {

PluginManager::PluginManager(std::string plugin_directory, ClientNotificationCallback notification_callback)
    : plugin_directory_(std::move(plugin_directory)),
      notification_callback_(notification_callback) {}

PluginManager::~PluginManager() {
    shutdown();
}

PluginManager::ActiveCallGuard::ActiveCallGuard(PluginManager& manager) : manager_(manager) {
    manager_.beginCall();
}

PluginManager::ActiveCallGuard::~ActiveCallGuard() {
    manager_.endCall();
}

void PluginManager::beginCall() {
    std::unique_lock<std::mutex> lock(state_mutex_);
    if (reloading_) {
        LOG(INFO) << "Plugin call waiting for reload to finish" << std::endl;
    }
    state_cv_.wait(lock, [this] { return !reloading_; }); // 如果正在reload,等待
    ++active_calls_;
}

void PluginManager::endCall() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (active_calls_ > 0) {
        --active_calls_;
    }
    if (active_calls_ == 0) {
        state_cv_.notify_all();  // 唤醒可能正在等待的 reload
    }
}

bool PluginManager::loadAll() {
    return replaceAll("initial plugin load");
}

bool PluginManager::reloadAll() {
    return replaceAll("SIGHUP full plugin reload");
}

bool PluginManager::replaceAll(const char* operation) {
    {
        std::unique_lock<std::mutex> lock(state_mutex_);
        state_cv_.wait(lock, [this] { return !reloading_ || shutdown_; });
        if (shutdown_) {
            LOG(WARNING) << "Ignoring " << operation << " because PluginManager is shutting down" << std::endl;
            return false;
        }
        reloading_ = true;  // 设置reloading,之后重新进来的请求会wait阻塞
        LOG(INFO) << "Starting " << operation << "; blocking new plugin calls" << std::endl;
        if (active_calls_ != 0) {
            LOG(INFO) << "Waiting for " << active_calls_ << " active plugin call(s) before reload" << std::endl;
        }
        state_cv_.wait(lock, [this] { return active_calls_ == 0; });  // 等待正在执行的请求结束
        LOG(INFO) << "All active plugin calls completed" << std::endl;
    }

    bool success = false;
    try {
        unregisterAll();
        loader_.unloadAll();

        success = loader_.loadAll(plugin_directory_);  // 重新加载插件
        if (success) {
            registerLoadedPlugins(); // 重新注册
        }
    } catch (const std::exception& ex) {
        LOG(ERROR) << operation << " failed with exception: " << ex.what() << std::endl;
        success = false;
    } catch (...) {
        LOG(ERROR) << operation << " failed with an unknown exception" << std::endl;
        success = false;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        reloading_ = false;  // 重新设置reloading,开放请求入口
    }
    state_cv_.notify_all();

    if (success) {
        LOG(INFO) << operation << " completed successfully; " << loader_.plugins().size() << " plugin(s) active"
                  << std::endl;
    } else {
        LOG(ERROR) << operation << " failed; server remains running with successfully loaded plugins" << std::endl;
    }
    return success;
}

void PluginManager::registerLoadedPlugins() {
    for (auto& plugin : loader_.plugins()) {
        if (!plugin.api) {
            continue;
        }

        plugin.notification_system = std::make_unique<NotificationSystem>();
        plugin.notification_system->SendToClient = notification_callback_;
        plugin.api->notifications = plugin.notification_system.get();

        switch (plugin.api->GetType()) {
            case PLUGIN_TYPE_TOOLS:
                tool_manager_.registerPlugin(plugin.api);
                break;
            case PLUGIN_TYPE_PROMPTS:
                prompt_manager_.registerPlugin(plugin.api);
                break;
            case PLUGIN_TYPE_RESOURCES:
                resource_manager_.registerPlugin(plugin.api);
                break;
        }
    }
}

void PluginManager::unregisterAll() {
    tool_manager_.unregisterAll();
    resource_manager_.unregisterAll();
    prompt_manager_.unregisterAll();
}

void PluginManager::shutdown() {
    {
        std::unique_lock<std::mutex> lock(state_mutex_);
        if (shutdown_) {
            return;
        }
        state_cv_.wait(lock, [this] { return !reloading_; });
        shutdown_ = true;
        reloading_ = true;
        LOG(INFO) << "PluginManager shutdown: blocking new plugin calls" << std::endl;
        if (active_calls_ != 0) {
            LOG(INFO) << "Waiting for " << active_calls_ << " active plugin call(s) before shutdown" << std::endl;
        }
        state_cv_.wait(lock, [this] { return active_calls_ == 0; });
    }

    unregisterAll();
    loader_.unloadAll();

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        reloading_ = false;
    }
    state_cv_.notify_all();
    LOG(INFO) << "PluginManager shutdown completed" << std::endl;
}

nlohmann::json PluginManager::invokePlugin(PluginAPI* plugin, const nlohmann::json& request) {  // 将request序列化为json字符串，传给插件的HandleRequest函数，返回结果再反序列化为json对象
    std::unique_ptr<char[]> result(plugin->HandleRequest(request.dump().c_str()));
    if (!result) {
        throw std::runtime_error("Plugin returned nullptr");
    }
    return nlohmann::json::parse(result.get());
}

nlohmann::json PluginManager::toolsList(const nlohmann::json& request) {
    ActiveCallGuard guard(*this);
    nlohmann::ordered_json response = MCPBuilder::Response(request);
    response["result"]["tools"] = tool_manager_.list();
    return response;
}

nlohmann::json PluginManager::toolsCall(const nlohmann::json& request) {
    ActiveCallGuard guard(*this);
    nlohmann::ordered_json response = MCPBuilder::Response(request);
    const std::string name = request.at("params").at("name").get<std::string>();
    const RegisteredTool* tool = tool_manager_.find(name);
    if (!tool) {
        response["result"]["isError"] = true;
        response["result"]["content"] = nlohmann::json::array({{
            {"type", "text"},
            {"text", "Tool not found: " + name},
        }});
        return response;
    }

    try {
        response["result"] = invokePlugin(tool->plugin, request);
        response["result"]["isError"] = false;
    } catch (const std::exception& ex) {
        LOG(ERROR) << "Tool plugin call failed for " << name << ": " << ex.what() << std::endl;
        response["result"]["isError"] = true;
        response["result"]["content"] = nlohmann::json::array({{
            {"type", "text"},
            {"text", "Plugin call failed."},
        }});
    }
    return response;
}

nlohmann::json PluginManager::promptsList(const nlohmann::json& request) {
    ActiveCallGuard guard(*this);
    nlohmann::ordered_json response = MCPBuilder::Response(request);
    response["result"]["prompts"] = prompt_manager_.list();
    return response;
}

nlohmann::json PluginManager::promptsGet(const nlohmann::json& request) {
    ActiveCallGuard guard(*this);
    nlohmann::ordered_json response = MCPBuilder::Response(request);
    const std::string name = request.at("params").at("name").get<std::string>();
    const RegisteredPrompt* prompt = prompt_manager_.find(name);
    if (!prompt) {
        return response;
    }
    try {
        response["result"] = invokePlugin(prompt->plugin, request);
    } catch (const std::exception& ex) {
        LOG(ERROR) << "Prompt plugin call failed for " << name << ": " << ex.what() << std::endl;
        return MCPBuilder::Error(MCPBuilder::InternalError, request.at("id"), "Plugin call failed");
    }
    return response;
}

nlohmann::json PluginManager::resourcesList(const nlohmann::json& request) {
    ActiveCallGuard guard(*this);
    nlohmann::ordered_json response = MCPBuilder::Response(request);
    response["result"]["resources"] = resource_manager_.list();
    return response;
}

nlohmann::json PluginManager::resourcesRead(const nlohmann::json& request) {
    ActiveCallGuard guard(*this);
    nlohmann::ordered_json response = MCPBuilder::Response(request);
    const std::string uri = request.at("params").at("uri").get<std::string>();
    const RegisteredResource* resource = resource_manager_.find(uri);
    if (!resource) {
        return response;
    }
    try {
        response["result"] = invokePlugin(resource->plugin, request);
    } catch (const std::exception& ex) {
        LOG(ERROR) << "Resource plugin call failed for " << uri << ": " << ex.what() << std::endl;
        return MCPBuilder::Error(MCPBuilder::InternalError, request.at("id"), "Plugin call failed");
    }
    return response;
}

std::size_t PluginManager::loadedPluginCount() {
    std::unique_lock<std::mutex> lock(state_mutex_);
    state_cv_.wait(lock, [this] { return !reloading_; });
    return loader_.plugins().size();
}

} // namespace vx::mcp
