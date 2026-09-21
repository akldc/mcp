#include "manager/PluginManager.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

int fail(const std::string& message) {
    std::cerr << "[FAIL] " << message << '\n';
    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        return fail("usage: plugin_reload_concurrency_test <plugins>");
    }

    vx::mcp::PluginManager manager(argv[1], nullptr);
    if (!manager.loadAll() || manager.loadedPluginCount() == 0) {
        return fail("initial plugin load failed");
    }

    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"id", "sleep"},
        {"method", "tools/call"},
        {"params", {
            {"name", "sleep"},
            {"arguments", {{"milliseconds", 500}}},
        }},
    };

    std::mutex start_mutex;
    std::condition_variable start_cv;
    bool call_started = false;
    nlohmann::json call_response;
    std::thread active_call([&]() {
        {
            std::lock_guard<std::mutex> lock(start_mutex);
            call_started = true;
        }
        start_cv.notify_one();
        call_response = manager.toolsCall(request);
    });

    {
        std::unique_lock<std::mutex> lock(start_mutex);
        start_cv.wait(lock, [&]() { return call_started; });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(75));

    const auto reload_start = std::chrono::steady_clock::now();
    const bool reload_ok = manager.reloadAll();
    const auto reload_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - reload_start);
    active_call.join();

    if (!reload_ok) {
        return fail("reloadAll returned false");
    }
    if (reload_duration.count() < 350) {
        return fail("reload did not wait for the active plugin call");
    }
    if (call_response["result"].value("isError", true)) {
        return fail("active sleep call failed during reload");
    }

    nlohmann::json list_request = {
        {"jsonrpc", "2.0"},
        {"id", "list"},
        {"method", "tools/list"},
        {"params", nlohmann::json::object()},
    };
    const auto listed = manager.toolsList(list_request);
    if (listed["result"]["tools"].empty()) {
        return fail("tool registry is empty after reload");
    }

    manager.shutdown();
    std::cout << "[PASS] reload waited for active calls and restored registrations\n";
    return 0;
}
