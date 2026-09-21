#ifndef MCP_SERVER_PLUGIN_MANAGER_H
#define MCP_SERVER_PLUGIN_MANAGER_H

#include "PluginRegistries.h"
#include "loader/PluginsLoader.h"
#include "json.hpp"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>

namespace vx::mcp {

/*
1.调用 PluginLoader 加载、卸载动态库；
2.将插件注册到 Tool/Resource/Prompt Manager；
3.处理 MCP 的 list/call/get/read 请求；
4.保证 reload 时不会卸载正在使用的插件。
*/

class PluginManager {
public:
    PluginManager(std::string plugin_directory, ClientNotificationCallback notification_callback);
    ~PluginManager();

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    bool loadAll();
    bool reloadAll();
    void shutdown();

    nlohmann::json toolsList(const nlohmann::json& request);
    nlohmann::json toolsCall(const nlohmann::json& request);
    nlohmann::json promptsList(const nlohmann::json& request);
    nlohmann::json promptsGet(const nlohmann::json& request);
    nlohmann::json resourcesList(const nlohmann::json& request);
    nlohmann::json resourcesRead(const nlohmann::json& request);

    std::size_t loadedPluginCount();

private:
    class ActiveCallGuard {
    public:
        explicit ActiveCallGuard(PluginManager& manager);
        ~ActiveCallGuard();
        ActiveCallGuard(const ActiveCallGuard&) = delete;
        ActiveCallGuard& operator=(const ActiveCallGuard&) = delete;

    private:
        PluginManager& manager_;
    };

    void beginCall();
    void endCall();
    bool replaceAll(const char* operation);
    void registerLoadedPlugins();
    void unregisterAll();
    static nlohmann::json invokePlugin(PluginAPI* plugin, const nlohmann::json& request);

    std::string plugin_directory_;
    ClientNotificationCallback notification_callback_ = nullptr; // 插件向 MCP Client 发送通知的入口
    PluginLoader loader_;   // 动态库实际所有者
    ToolManager tool_manager_;  // 三类注册表
    ResourceManager resource_manager_;
    PromptManager prompt_manager_;

    mutable std::mutex state_mutex_;
    std::condition_variable state_cv_; // 等待reload结束，或者 当前调用归零
    bool reloading_ = false; // 核心标记，当前是否正在发生reload, 如果为true,所有新的请求都会被阻塞等待reload结束
    bool shutdown_ = false;
    std::size_t active_calls_ = 0; // 当前正在访问/调用插件的请求数量,包括所有插件的相关请求，没有独立到每个插件，所以每次是全量reload,不支持单个插件reload
};

} // namespace vx::mcp

#endif // MCP_SERVER_PLUGIN_MANAGER_H
