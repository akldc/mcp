#include "version.h"

#include "StdioTransport.h"
#include "SseTransport.h"
#include "aixlog.hpp"
#include "manager/PluginManager.h"
#include "popl.hpp"
#include "server/Server.h"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

using namespace popl;

namespace {

std::shared_ptr<vx::mcp::Server> server;
std::mutex g_notification_mutex;

#ifndef _WIN32
volatile std::sig_atomic_t g_reload_requested = 0;

void reloadSignalHandler(int) {
    // Async-signal-safe: do not log, allocate, lock, or reload here.
    g_reload_requested = 1;
}
#endif

void stopSignalHandler(int) {
    if (server && server->IsValid()) {
        server->Stop();
    }
}

void clientNotificationCallback(const char* plugin_name, const char* notification) {
    std::lock_guard<std::mutex> lock(g_notification_mutex);
    if (server && server->IsValid()) {
        server->SendNotification(plugin_name, notification);
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string name;
    std::string plugins_directory;
    std::string logs_directory;
    bool verbose = false;

    OptionParser options("Allowed options");
    auto help_option = options.add<Switch>("", "help", "produce help message");
    auto name_option = options.add<Value<std::string>>(
        "n", "name", "the name of the server", "mcp-server");
    auto plugins_option = options.add<Value<std::string>>(
        "p", "plugins", "the directory where to load the plugins", "./plugins");
    auto logs_option = options.add<Value<std::string>>(
        "l", "logs", "the directory where to store the logs", "./logs");
    auto verbose_option = options.add<Value<bool>>(
        "v", "verbose", "enable verbose", verbose);
    auto use_sse_server = options.add<Switch>("s", "sse", "start as sse server");
    name_option->assign_to(&name);
    plugins_option->assign_to(&plugins_directory);
    logs_option->assign_to(&logs_directory);
    verbose_option->assign_to(&verbose);

    try {
        options.parse(argc, argv);
        if (help_option->count() == 1) {
            std::cout << options << std::endl;
            return 0;
        }
    } catch (const std::exception& ex) {
        std::cerr << "Invalid command line: " << ex.what() << std::endl;
        return 2;
    }

    std::filesystem::create_directories(logs_directory);
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::stringstream timestamp;
    timestamp << std::put_time(std::gmtime(&now_time), "%Y-%m-%dT%H-%M-%S");
    const std::string log_filename =
        logs_directory + "/mcp-server_" + timestamp.str() + ".log";
    std::cerr << "Log file: " << log_filename << std::endl;
    auto log_sink = std::make_shared<AixLog::SinkFile>(
        AixLog::Severity::trace, log_filename);
    AixLog::Log::init({log_sink});

    server = std::make_shared<vx::mcp::Server>();
    server->Name(name);
    server->VerboseLevel(verbose ? 1 : 0);

    std::signal(SIGINT, stopSignalHandler);
#ifndef _WIN32
    std::signal(SIGHUP, reloadSignalHandler);   // reload 信号处理函数
    LOG(INFO) << "SIGHUP plugin reload handler installed" << std::endl;
#endif

    auto plugin_manager = std::make_shared<vx::mcp::PluginManager>(plugins_directory, clientNotificationCallback);  // PluginManager,内部包括PluginLoader,ToolManager,ResourceManager,PromptManager
    if (!plugin_manager->loadAll()) {
        LOG(ERROR) << "Initial plugin scan failed; server will continue running"
                   << std::endl;
    }

    server->OverrideCallback("tools/list",
        [plugin_manager](const json& request) {
            return plugin_manager->toolsList(request);
        });
    server->OverrideCallback("tools/call",
        [plugin_manager](const json& request) {
            return plugin_manager->toolsCall(request);
        });
    server->OverrideCallback("prompts/list",
        [plugin_manager](const json& request) {
            return plugin_manager->promptsList(request);
        });
    server->OverrideCallback("prompts/get",
        [plugin_manager](const json& request) {
            return plugin_manager->promptsGet(request);
        });
    server->OverrideCallback("resources/list",
        [plugin_manager](const json& request) {
            return plugin_manager->resourcesList(request);
        });
    server->OverrideCallback("resources/read",
        [plugin_manager](const json& request) {
            return plugin_manager->resourcesRead(request);
        });

#ifndef _WIN32
    server->SetReloadCheckCallback([plugin_manager]() {  // 设置收到reload信号的回调函数
        if (g_reload_requested == 0) {
            return;
        }
        g_reload_requested = 0;
        LOG(INFO) << "SIGHUP observed in server context; starting plugin reload" << std::endl;
        if (!plugin_manager->reloadAll()) {
            LOG(ERROR) << "SIGHUP plugin reload completed with errors" << std::endl;
        }
    });
#endif

    // 底层传输，对Server层透明
    std::shared_ptr<vx::ITransport> transport;  
    if (use_sse_server->is_set()) {
        transport = std::make_shared<vx::transport::SSE>(); 
    } else {
        transport = std::make_shared<vx::transport::Stdio>();
    }

    const bool connected = server->Connect(transport);
    plugin_manager->shutdown();
    return connected ? 0 : 1;
}
