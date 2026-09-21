#include "PluginsLoader.h"

#include "aixlog.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace vx::mcp {

namespace {

bool isSharedLibrary(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();
#ifdef _WIN32
    return extension == ".dll";
#elif defined(__APPLE__)
    return extension == ".dylib" || extension == ".so";
#else
    return extension == ".so";
#endif
}

void closeLibrary(LibraryHandle handle) {
    if (!handle) {
        return;
    }
#ifdef _WIN32
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
}

} // namespace

PluginLoader::~PluginLoader() {
    unloadAll();
}

bool PluginLoader::loadAll(const std::string& directory) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        LOG(ERROR) << "Plugin directory is not accessible: " << directory << (error ? " - " + error.message() : "")
                   << std::endl;
        return false;
    }

    std::vector<std::string> paths;
    std::filesystem::recursive_directory_iterator iterator(directory, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error) && !error && isSharedLibrary(iterator->path())) {
            paths.push_back(iterator->path().string());
        }
        iterator.increment(error);
    }
    if (error) {
        LOG(ERROR) << "Failed while scanning plugin directory " << directory << ": " << error.message() << std::endl;
        return false;
    }

    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths) {
        if (!loadOne(path)) {
            LOG(ERROR) << "Skipping plugin after load failure: " << path << std::endl;
        }
    }

    LOG(INFO) << "Plugin scan completed: " << plugins_.size() << " plugin(s) loaded from " << directory << std::endl;
    return true;
}

bool PluginLoader::loadOne(const std::string& path) {
    PluginInstance plugin;
    plugin.path = path;

    LOG(INFO) << "Loading plugin library: " << path << std::endl;
#ifdef _WIN32
    plugin.handle = LoadLibraryA(path.c_str());
    if (!plugin.handle) {
        LOG(ERROR) << "LoadLibrary failed for plugin: " << path << std::endl;
        return false;
    }
    auto create_plugin = reinterpret_cast<CreatePluginFunction>(GetProcAddress(plugin.handle, "CreatePlugin"));
    plugin.destroy_plugin = reinterpret_cast<DestroyPluginFunction>(GetProcAddress(plugin.handle, "DestroyPlugin"));
#else
    dlerror();
    plugin.handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!plugin.handle) {
        const char* error = dlerror();
        LOG(ERROR) << "dlopen failed for " << path << ": " << (error ? error : "unknown error") << std::endl;
        return false;
    }

    dlerror();
    auto create_plugin = reinterpret_cast<CreatePluginFunction>(dlsym(plugin.handle, "CreatePlugin"));
    const char* create_error_text = dlerror();
    const std::string create_error = create_error_text ? create_error_text : "";
    dlerror();
    plugin.destroy_plugin = reinterpret_cast<DestroyPluginFunction>(dlsym(plugin.handle, "DestroyPlugin"));
    const char* destroy_error_text = dlerror();
    const std::string destroy_error = destroy_error_text ? destroy_error_text : "";
    if (!create_error.empty() || !destroy_error.empty()) {
        LOG(ERROR) << "dlsym failed for " << path << ": "
                   << (!create_error.empty() ? create_error : destroy_error) << std::endl;
        closeLibrary(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }
#endif

    if (!create_plugin || !plugin.destroy_plugin) {
        LOG(ERROR) << "Plugin does not export CreatePlugin/DestroyPlugin: " << path << std::endl;
        closeLibrary(plugin.handle);
        plugin.handle = nullptr;
        return false;
    }

    try {
        plugin.api = create_plugin();
        if (!validateAPI(plugin.api, path)) {
            if (plugin.api) {
                try {
                    plugin.destroy_plugin(plugin.api);
                } catch (...) {
                    LOG(ERROR) << "DestroyPlugin threw for invalid plugin: " << path << std::endl;
                }
                plugin.api = nullptr;
            }
            closeLibrary(plugin.handle);
            plugin.handle = nullptr;
            return false;
        }

        if (!plugin.api->Initialize()) {
            LOG(ERROR) << "Plugin Initialize failed: " << path << std::endl;
            try {
                plugin.destroy_plugin(plugin.api);
            } catch (...) {
                LOG(ERROR) << "DestroyPlugin threw after Initialize failure: " << path << std::endl;
            }
            plugin.api = nullptr;
            closeLibrary(plugin.handle);
            plugin.handle = nullptr;
            return false;
        }

        const std::string name = plugin.api->GetName();
        const std::string version = plugin.api->GetVersion();
        plugins_.push_back(std::move(plugin));
        LOG(INFO) << "Loaded plugin: " << name << " v" << version << " (" << path << ")" << std::endl;
        return true;
    } catch (const std::exception& ex) {
        LOG(ERROR) << "Exception while loading plugin " << path << ": " << ex.what() << std::endl;
    } catch (...) {
        LOG(ERROR) << "Unknown exception while loading plugin: " << path << std::endl;
    }

    if (plugin.api && plugin.destroy_plugin) {
        try {
            plugin.destroy_plugin(plugin.api);
        } catch (...) {
            LOG(ERROR) << "DestroyPlugin threw while cleaning failed load: " << path << std::endl;
        }
        plugin.api = nullptr;
    }
    closeLibrary(plugin.handle);
    plugin.handle = nullptr;
    return false;
}

bool PluginLoader::validateAPI(const PluginAPI* api, const std::string& path) const {
    if (!api || !api->GetName || !api->GetVersion || !api->GetType || !api->Initialize || !api->HandleRequest ||
        !api->Shutdown) {
        LOG(ERROR) << "Plugin has an incomplete base API: " << path << std::endl;
        return false;
    }
    if (!api->GetName() || !api->GetVersion()) {
        LOG(ERROR) << "Plugin returned null name or version: " << path << std::endl;
        return false;
    }

    switch (api->GetType()) {
        case PLUGIN_TYPE_TOOLS:
            if (api->GetToolCount && api->GetTool) return true;
            break;
        case PLUGIN_TYPE_PROMPTS:
            if (api->GetPromptCount && api->GetPrompt) return true;
            break;
        case PLUGIN_TYPE_RESOURCES:
            if (api->GetResourceCount && api->GetResource) return true;
            break;
        default:
            LOG(ERROR) << "Plugin returned an invalid PluginType: " << path << std::endl;
            return false;
    }
    LOG(ERROR) << "Plugin has an incomplete type-specific API: " << path << std::endl;
    return false;
}

void PluginLoader::unloadAll() {
    for (auto& plugin : plugins_) {
        unloadOne(plugin);
    }
    plugins_.clear();
}

void PluginLoader::unloadOne(PluginInstance& plugin) noexcept {
    std::string name = plugin.path;
    if (plugin.api && plugin.api->GetName) {
        try {
            if (const char* plugin_name = plugin.api->GetName()) {
                name = plugin_name;
            }
        } catch (...) {
            LOG(ERROR) << "GetName threw while unloading plugin: " << plugin.path << std::endl;
        }
    }
    LOG(INFO) << "Unloading plugin: " << name << " (" << plugin.path << ")" << std::endl;

    if (plugin.api) {
        try {
            plugin.api->Shutdown();
        } catch (const std::exception& ex) {
            LOG(ERROR) << "Plugin Shutdown threw for " << name << ": " << ex.what() << std::endl;
        } catch (...) {
            LOG(ERROR) << "Plugin Shutdown threw for " << name << std::endl;
        }

        try {
            plugin.destroy_plugin(plugin.api);
        } catch (...) {
            LOG(ERROR) << "DestroyPlugin threw for " << name << std::endl;
        }
        plugin.api = nullptr;
    }

    plugin.notification_system.reset();
    plugin.destroy_plugin = nullptr;

    if (plugin.handle) {
#ifdef _WIN32
        if (!FreeLibrary(plugin.handle)) {
            LOG(ERROR) << "FreeLibrary failed for plugin: " << plugin.path << std::endl;
        }
#else
        if (dlclose(plugin.handle) != 0) {
            const char* error = dlerror();
            LOG(ERROR) << "dlclose failed for " << plugin.path << ": " << (error ? error : "unknown error") << std::endl;
        }
#endif
        plugin.handle = nullptr;
    }

    LOG(INFO) << "Plugin unloaded: " << name << std::endl;
}

} // namespace vx::mcp
