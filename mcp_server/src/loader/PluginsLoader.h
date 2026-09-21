#ifndef MCP_SERVER_PLUGINS_LOADER_H
#define MCP_SERVER_PLUGINS_LOADER_H

#ifdef _WIN32
#include <windows.h>
using LibraryHandle = HMODULE;
#else
#include <dlfcn.h>
using LibraryHandle = void*;
#endif

#include "PluginAPI.h"

#include <memory>
#include <string>
#include <vector>

namespace vx::mcp {

using CreatePluginFunction = PluginAPI* (*)();
using DestroyPluginFunction = void (*)(PluginAPI*);

// Owns every object whose lifetime is tied to one loaded dynamic library.
struct PluginInstance {
    std::string path;
    LibraryHandle handle = nullptr;
    PluginAPI* api = nullptr;
    DestroyPluginFunction destroy_plugin = nullptr;
    std::unique_ptr<NotificationSystem> notification_system;

    PluginInstance() = default;
    PluginInstance(const PluginInstance&) = delete; // 禁止复制，具有插件的唯一所有权
    PluginInstance& operator=(const PluginInstance&) = delete;
    PluginInstance(PluginInstance&&) noexcept = default;
    PluginInstance& operator=(PluginInstance&&) noexcept = default;
};

class PluginLoader {
public:
    PluginLoader() = default;
    ~PluginLoader();

    bool loadAll(const std::string& directory);
    void unloadAll();

    const std::vector<PluginInstance>& plugins() const { return plugins_; }
    std::vector<PluginInstance>& plugins() { return plugins_; }

private:
    bool loadOne(const std::string& path);
    void unloadOne(PluginInstance& plugin) noexcept;
    bool validateAPI(const PluginAPI* api, const std::string& path) const;

    std::vector<PluginInstance> plugins_;
};

// Compatibility name for code that used the original class.
using PluginsLoader = PluginLoader;

} // namespace vx::mcp

#endif // MCP_SERVER_PLUGINS_LOADER_H
